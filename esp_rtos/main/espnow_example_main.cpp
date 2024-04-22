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

const gpio_num_t D1 = GPIO_NUM_5;
const gpio_num_t D2 = GPIO_NUM_4;
const gpio_num_t BUTTON_LED = D2;
const gpio_num_t BUTTON_INPUT = D1;
#define HIGH 1
#define LOW 0

/* WiFi should start before using ESPNOW */
static void example_wifi_init(void) {
  tcpip_adapter_init();

  ESP_ERROR_CHECK(esp_event_loop_create_default());

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(ESPNOW_WIFI_MODE));
}

static void example_espnow_recv_cb(const uint8_t *mac_addr, const uint8_t *data,
                                   int len) {
  ESP_LOGI(TAG, "recv_cb");
}

static esp_err_t example_espnow_init(void) {

  /* Initialize ESPNOW and register sending and receiving callback function. */
  ESP_ERROR_CHECK(esp_now_init());
  ESP_ERROR_CHECK(esp_now_register_recv_cb(example_espnow_recv_cb));

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
      .intr_type = GPIO_INTR_DISABLE,
      .mode = GPIO_MODE_OUTPUT,
      .pin_bit_mask = (1ULL << BUTTON_LED),
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .pull_up_en = GPIO_PULLUP_DISABLE,
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

void app_main() {
  setup_gpio();
  // Initialize NVS
  ESP_ERROR_CHECK(nvs_flash_init());
  ESP_LOGI(TAG, "Wifi init");
  example_wifi_init();
  ESP_ERROR_CHECK(esp_wifi_start());
  // TODO, can remove later??
  ESP_ERROR_CHECK(
      esp_wifi_set_channel(CONFIG_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));
  esp_wifi_stop();

  while (true) {
    int wake_level = !gpio_get_level(BUTTON_INPUT);
    if (wake_level == HIGH) {
      ESP_LOGI(TAG, "Woke up from button press");
    } else {
      ESP_LOGI(TAG, "Woke up from timer");
    }

    ESP_ERROR_CHECK(esp_wifi_start());

    example_espnow_init();

    setLed(true);
    vTaskDelay(50 / portTICK_PERIOD_MS);
    setLed(false);

    ESP_LOGI(TAG, "Waiting for button press..");
    while (true) {
      vTaskDelay(50 / portTICK_PERIOD_MS);
      int btn_pressed = !gpio_get_level(BUTTON_INPUT);
      if (btn_pressed) {
        ESP_LOGI(TAG, "Button pressed");
        setLed(true);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        setLed(false);
        break;
      }
    }

    ESP_LOGI(TAG, "Sleeping...");
    esp_now_deinit();
    esp_wifi_stop();
    gpio_wakeup_enable(BUTTON_INPUT, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    esp_sleep_enable_timer_wakeup(5e6);
    esp_light_sleep_start();
  }
}
