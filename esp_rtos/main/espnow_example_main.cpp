#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "espnow_example.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "nvs_flash.h"
#include "rom/crc.h"
#include "rom/ets_sys.h"
#include "tcpip_adapter.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
extern "C" void app_main();

static const char *TAG = "espnow_example";

static uint8_t example_broadcast_mac[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF,
                                                          0xFF, 0xFF, 0xFF};

const int WIFI_CHANNEL = 4;

const bool IS_COORDINATOR = false; // True for only one device
const gpio_num_t D1 = GPIO_NUM_5;
const gpio_num_t D2 = GPIO_NUM_4;
const gpio_num_t BUTTON_LED = D2;
const gpio_num_t BUTTON_INPUT = D1;
#define HIGH 1
#define LOW 0

uint8_t BROADCAST_MAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
uint8_t WINNER_MAC[6] = {0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU};
uint8_t BUTTON_PRESSED_MAC[6] = {0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU};

enum States_t {
  SLEEP_LISTEN = 1,
  DOOR_DASH_WAITING = 2,
  DOOR_DASH_WINNER = 3,
  DOOR_DASH_LOSER = 4,
  DOOR_DASH_COOL_DOWN_WINNER = 5,
  DOOR_DASH_COOL_DOWN_LOSER = 6,
  DOOR_DASH_COOL_DOWN_UNKNOWN = 7,
} States;

const unsigned long SLEEP_DURATION__us = 2e6;
const unsigned long LISTEN_TIME__ms = 50;
const unsigned long DOOR_DASH_REBROADCAST_INTERVAL__ms = 20;
const unsigned long DOOR_DASH_WAITING_FLASH_FREQUENCY__ms = 500;
const unsigned long DOOR_DASH_WINNER_FLASH_FREQUENCY__ms = 120;
const unsigned long DOOR_DASH_COORDINATION_DURATION__ms = 17e3;
const unsigned long FLASH_DURATION__ms = 5e3;
const unsigned long COOL_DOWN__ms = 15e3;

States_t globalState = SLEEP_LISTEN;
unsigned long globalDoorDashStartedAt = 0;
bool globalHasDeclaredWinner = false;

struct __attribute__((packed)) DataStruct {
  // Device sending to master that the device's button was pressed
  uint8_t button_pressed_mac[6]; // PRESSED_MSG

  // Master sends a message to everyone about who the winner
  uint8_t winner_mac[6]; // WINNER_MSG
};

void buttonCallBackFunction(const uint8_t *senderMac,
                            const uint8_t *incomingData, int len);
void coordinatorCallBackFunction(const uint8_t *senderMac,
                                 const uint8_t *incomingData, int len);

class Serial {
public:
  static void println(const char *msg) {
    static const char *TAG = "Serial";
    ESP_LOGI(TAG, "%s", msg);
  }

  static void println(int value) {
    static const char *TAG = "Serial";
    ESP_LOGI(TAG, "%d", value);
  }

  // You can add more overloads for other types like `double`, `float`, etc.
};

/* WiFi should start before using ESPNOW */
static void example_wifi_init(void) {
  tcpip_adapter_init();

  ESP_ERROR_CHECK(esp_event_loop_create_default());

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(ESPNOW_WIFI_MODE));
}

static esp_err_t example_espnow_init(void) {

  /* Initialize ESPNOW and register sending and receiving callback function.
   */
  ESP_ERROR_CHECK(esp_now_init());
  if (IS_COORDINATOR) {
    Serial::println("Setup finished for coordinator");
    ESP_ERROR_CHECK(esp_now_register_recv_cb(coordinatorCallBackFunction));
  } else {
    Serial::println("Setup finished for button");
    ESP_ERROR_CHECK(esp_now_register_recv_cb(buttonCallBackFunction));
  }

  /* Set primary master key. */
  //   ESP_ERROR_CHECK(esp_now_set_pmk((uint8_t *)CONFIG_ESPNOW_PMK));

  /* Add broadcast peer information to peer list. */
  esp_now_peer_info_t *peer =
      static_cast<esp_now_peer_info_t *>(malloc(sizeof(esp_now_peer_info_t)));
  if (peer == NULL) {
    ESP_LOGE(TAG, "Malloc peer information fail");
    esp_now_deinit();
    return ESP_FAIL;
  }
  memset(peer, 0, sizeof(esp_now_peer_info_t));
  peer->channel = CONFIG_ESPNOW_CHANNEL;
  peer->ifidx = ESPNOW_WIFI_IF;
  peer->encrypt = false;
  memcpy(peer->peer_addr, example_broadcast_mac, ESP_NOW_ETH_ALEN);
  ESP_ERROR_CHECK(esp_now_add_peer(peer));
  free(peer);

  return ESP_OK;
}

void setup_gpio() {
  gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << BUTTON_LED),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&io_conf);

  gpio_config_t config = {.pin_bit_mask = (1ULL << BUTTON_INPUT),
                          .mode = GPIO_MODE_INPUT,
                          .pull_up_en = GPIO_PULLUP_ENABLE,
                          .pull_down_en = GPIO_PULLDOWN_DISABLE,
                          .intr_type = GPIO_INTR_DISABLE};
  gpio_config(&config);
}

void setLed(bool on) { gpio_set_level(BUTTON_LED, !on); }
bool isButtonPressed() { return !gpio_get_level(BUTTON_INPUT); }
void transitionState(States_t newState) {
  globalState = newState;

  switch (newState) {
  case SLEEP_LISTEN:
    Serial::println("Transitioned to SLEEP_LISTEN");
    break;
  case DOOR_DASH_WAITING:
    Serial::println("Transitioned to DOOR_DASH_WAITING");
    break;
  case DOOR_DASH_WINNER:
    Serial::println("Transitioned to DOOR_DASH_WINNER");
    break;
  case DOOR_DASH_LOSER:
    Serial::println("Transitioned to DOOR_DASH_LOSER");
    break;
  case DOOR_DASH_COOL_DOWN_WINNER:
    Serial::println("Transitioned to DOOR_DASH_COOL_DOWN_WINNER");
    break;
  case DOOR_DASH_COOL_DOWN_LOSER:
    Serial::println("Transitioned to DOOR_DASH_COOL_DOWN_LOSER");
    break;
  case DOOR_DASH_COOL_DOWN_UNKNOWN:
    Serial::println("Transitioned to DOOR_DASH_COOL_DOWN_UNKNOWN");
    break;
  default:
    Serial::println("Transitioned to ERROR, unknown state");
    break;
  }
}

void goToSleep() {
  Serial::println("Going to sleep");
  transitionState(SLEEP_LISTEN);
  globalDoorDashStartedAt = 0;
  globalHasDeclaredWinner = false;
  setLed(false);
  esp_now_deinit();
  esp_wifi_stop();
  gpio_wakeup_enable(BUTTON_INPUT, GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
  esp_sleep_enable_timer_wakeup(2e6);
  esp_light_sleep_start();
}

bool isWinnerMsg(DataStruct *data) {
  for (int i = 0; i < 6; i++) {
    if (data->winner_mac[i] != 0) {
      return true;
    }
  }
  return false;
}

void printMac(uint8_t *macaddr) { ESP_LOG_BUFFER_HEX(TAG, macaddr, 6); }

void getMacAddress(uint8_t *mac) {
  // WiFi.macAddress(mac);
  esp_wifi_get_mac(ESPNOW_WIFI_IF, mac);
}

void sendButtonPressed(uint8_t *mac) {
  DataStruct sendingData = {};
  memcpy((uint8_t *)sendingData.button_pressed_mac, mac, 6);
  esp_now_send(BROADCAST_MAC, (uint8_t *)&sendingData,
               sizeof(sendingData)); // NULL means send to all peers
}

void sendWinner(uint8_t *winner) {
  DataStruct sendingData = {};
  // TODO make it cleaner instead of this 6. Make it a struct instead?
  memcpy((uint8_t *)sendingData.winner_mac, winner, 6);
  esp_now_send(BROADCAST_MAC, (uint8_t *)&sendingData,
               sizeof(sendingData)); // NULL means send to all peers
}

void rebroadcast(uint8_t *data, uint8_t len) {
  esp_now_send(BROADCAST_MAC, data, len);
}

bool isMacAddressSelf(uint8_t *mac) {
  uint8_t selfMacAddress[6] = {};
  getMacAddress((uint8_t *)selfMacAddress);
  for (int i = 0; i < 6; i++) {
    if (mac[i] != selfMacAddress[i]) {
      return false;
    }
  }
  return true;
}

uint32_t millis() { return (xTaskGetTickCount() * 1000) / configTICK_RATE_HZ; }

void delay(int millis) { vTaskDelay(millis / portTICK_PERIOD_MS); }

void coordinatorCallBackFunction(const uint8_t *senderMac,
                                 const uint8_t *incomingData, int len) {
  DataStruct *data = (DataStruct *)incomingData;
  if (!globalHasDeclaredWinner) {
    Serial::println("Declare winner: ");
    printMac(data->button_pressed_mac);
    ESP_LOGI(TAG, "\n"); // newline..
    memcpy((uint8_t *)WINNER_MAC, data->button_pressed_mac, 6);

    globalDoorDashStartedAt = millis();
    globalHasDeclaredWinner = true;
  }

  if (globalHasDeclaredWinner) {
    sendWinner((uint8_t *)WINNER_MAC);
  }
}

void buttonCallBackFunction(const uint8_t *senderMac,
                            const uint8_t *incomingData, int len) {

  DataStruct *data = (DataStruct *)incomingData;
  // Handle state changes, and rebroadcasting
  if (isWinnerMsg(data)) { // WINNER_MSG
    if (globalState == SLEEP_LISTEN || globalState == DOOR_DASH_WAITING) {
      memcpy((uint8_t *)WINNER_MAC, data->winner_mac, 6);
      if (isMacAddressSelf(data->winner_mac)) {
        transitionState(DOOR_DASH_WINNER);
      } else {
        transitionState(DOOR_DASH_LOSER);
      }
    }
  } else { // PRESSED_MSG
    if (globalState == SLEEP_LISTEN) {
      memcpy((uint8_t *)BUTTON_PRESSED_MAC, data->button_pressed_mac, 6);
      transitionState(DOOR_DASH_WAITING);
    }
  }
  if (globalDoorDashStartedAt == 0) {
    // Gets reset after the button goes to sleep
    globalDoorDashStartedAt = millis();
  }
}

void ledWinner() {
  // Flash LED fast
  if ((millis() - globalDoorDashStartedAt) %
          (DOOR_DASH_WINNER_FLASH_FREQUENCY__ms * 2) <
      DOOR_DASH_WINNER_FLASH_FREQUENCY__ms) {
    setLed(true);
  } else {
    setLed(false);
  }
}

void ledUnknown() {
  // Slowly flash LED
  if ((millis() - globalDoorDashStartedAt) %
          (DOOR_DASH_WAITING_FLASH_FREQUENCY__ms * 2) <
      DOOR_DASH_WAITING_FLASH_FREQUENCY__ms) {
    setLed(true);
  } else {
    setLed(false);
  }
}

void ledLoser() { setLed(true); }

void runButton(bool btnPressed) {

  // pinMode(BUTTON_LED, OUTPUT);

  bool readyToSleep = false;
  if (!btnPressed) {
    // Wait for a message to have been received. Warning: while(true) loop
    // won't work here because we won't receive an ESP_NOW message callback.
    // It's unclear why -- at startup, it seems we need to wait some time with
    // delay for the setup to happen. Maybe there's some async setup that gets
    // stuck if we have a while(true) loop here. After this line, while (true)
    // loops are fine.
    delay(50);

    if (globalState == SLEEP_LISTEN) {
      // Serial::println("Back to sleep");
      readyToSleep = true;
    }
  }

  // At this point, one of two things has happened: the button was pressed, or
  // we received a message

  if (btnPressed && !readyToSleep) {
    Serial::println("Button pressed");
    transitionState(DOOR_DASH_WAITING);
    globalDoorDashStartedAt = millis();
  }

  unsigned long lastBroadcast = 0;
  while (!readyToSleep) {
    if (globalState == DOOR_DASH_WAITING) {
      ledUnknown();
      // Rebroadcast button pressed every 20ms
      if (millis() - lastBroadcast > DOOR_DASH_REBROADCAST_INTERVAL__ms) {
        if (btnPressed) {
          uint8_t selfMacAddress[6] = {};
          getMacAddress((uint8_t *)selfMacAddress);
          sendButtonPressed((uint8_t *)selfMacAddress);
        } else {
          sendButtonPressed((uint8_t *)BUTTON_PRESSED_MAC);
        }
        lastBroadcast = millis();
      }
      // If more than FLASH_DURATION__ms has passed, cool down
      if (millis() - globalDoorDashStartedAt >
          FLASH_DURATION__ms) { // Should theoretically never happen as long
                                // as the coordinator does its job.
        Serial::println("ERROR, never received a WINNER_MSG before cooldown");
        globalState = DOOR_DASH_COOL_DOWN_UNKNOWN;
      }
    } else if (globalState == DOOR_DASH_WINNER) {
      // Broadcast repeatedly who the winner is
      if (millis() - lastBroadcast > DOOR_DASH_REBROADCAST_INTERVAL__ms) {
        sendWinner((uint8_t *)WINNER_MAC);
        lastBroadcast = millis();
      }
      ledWinner();
      // If more than 5 seconds have passed, go to cool down state
      if (millis() - globalDoorDashStartedAt > FLASH_DURATION__ms) {
        transitionState(DOOR_DASH_COOL_DOWN_WINNER);
      }
    } else if (globalState == DOOR_DASH_LOSER) {
      // Broadcast repeatedly who the winner is
      if (millis() - lastBroadcast > DOOR_DASH_REBROADCAST_INTERVAL__ms) {
        sendWinner((uint8_t *)WINNER_MAC);
        lastBroadcast = millis();
      }
      ledLoser();
      // If more than 5 seconds have passed, go to cool down state
      if (millis() - globalDoorDashStartedAt > FLASH_DURATION__ms) {
        transitionState(DOOR_DASH_COOL_DOWN_LOSER);
      }
    } else if (globalState == DOOR_DASH_COOL_DOWN_WINNER) {
      ledWinner();
      // Sleep after cool down period is over
      if (millis() - globalDoorDashStartedAt >
          FLASH_DURATION__ms + COOL_DOWN__ms) {
        readyToSleep = true;
      }
    } else if (globalState == DOOR_DASH_COOL_DOWN_LOSER) {
      ledLoser();
      // Sleep after cool down period is over
      if (millis() - globalDoorDashStartedAt >
          FLASH_DURATION__ms + COOL_DOWN__ms) {
        readyToSleep = true;
      }
    } else { // DOOR_DASH_COOL_DOWN_UNKNOWN
      ledUnknown();

      // Sleep after cool down period is over
      if (millis() - globalDoorDashStartedAt >
          FLASH_DURATION__ms + COOL_DOWN__ms) {
        readyToSleep = true;
      }
    }
  }
  goToSleep();
}

void setupCoordinator() {
  // Radio_Init();

  while (true) {
    if (globalHasDeclaredWinner && millis() - globalDoorDashStartedAt >
                                       DOOR_DASH_COORDINATION_DURATION__ms) {
      Serial::println("Resetting after doordash");
      globalDoorDashStartedAt = 0;
      globalHasDeclaredWinner = false;
    }
  }
}

void loop() { Serial::println("ERROR, this should never run"); }

void app_main() {
  // Initialize NVS
  ESP_ERROR_CHECK(nvs_flash_init());
  ESP_LOGI(TAG, "Wifi init");
  example_wifi_init();
  ESP_ERROR_CHECK(esp_wifi_start());
  // TODO, can remove later??
  ESP_ERROR_CHECK(
      esp_wifi_set_channel(CONFIG_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));
  esp_wifi_stop();
  if (IS_COORDINATOR) {
    ESP_ERROR_CHECK(esp_wifi_start());

    example_espnow_init();
    setupCoordinator();
  } else {
    setup_gpio();
    setLed(false);

    while (true) {
      bool btnPressed = isButtonPressed();
      // Radio_Init();
      ESP_ERROR_CHECK(esp_wifi_start());

      example_espnow_init();
      runButton(btnPressed);
    }
  }
}
