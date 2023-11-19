/* ESPNOW Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

/*
   This example shows how to use ESPNOW.
   Prepare two device, one for sending ESPNOW data and another for receiving
   ESPNOW data.
*/
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

static const char *TAG = "espnow_example";

static uint8_t example_broadcast_mac[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF,
                                                          0xFF, 0xFF, 0xFF};

#define D1 5
#define D2 4
#define D8 15
#define WAKEUP_PIN D8
#define HIGH 1
#define LOW 0
#define GPIO_OUTPUT_PIN_SEL ((1ULL << D1) | (1ULL << D2))

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
  esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
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

  gpio_config_t io_conf;
  // disable interrupt
  io_conf.intr_type = GPIO_INTR_DISABLE;
  // set as output mode
  io_conf.mode = GPIO_MODE_OUTPUT;
  // bit mask of the pins that you want to set,e.g.GPIO15/16
  io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
  // disable pull-down mode
  io_conf.pull_down_en = 0;
  // disable pull-up mode
  io_conf.pull_up_en = 0;
  // configure GPIO with the given settings
  gpio_config(&io_conf);

  // Wakeup pin
  gpio_config_t config = {.pin_bit_mask = (1ULL << WAKEUP_PIN),
                          .mode = GPIO_MODE_INPUT,
                          .pull_down_en = false, // TODO make false..
                          .pull_up_en = false,
                          .intr_type = GPIO_INTR_DISABLE};
  gpio_config(&config);
}

void app_main() {
  setup_gpio();
  // Initialize NVS
  ESP_ERROR_CHECK(nvs_flash_init());
  ESP_LOGI(TAG, "Wifi init");
  example_wifi_init();
  ESP_ERROR_CHECK(esp_wifi_start());
  // TODO, can remove later??
  ESP_ERROR_CHECK(esp_wifi_set_channel(CONFIG_ESPNOW_CHANNEL, 0));
  esp_wifi_stop();

  while (true) {
    // ESP_LOGI(TAG, "Wifi start");
    int wake_level = gpio_get_level(WAKEUP_PIN);
    if (wake_level == HIGH) {
      ESP_LOGI(TAG, "Woke up from button press");
    } else {
      ESP_LOGI(TAG, "Woke up from timer");
    }

    gpio_set_level(D1, HIGH);
    ESP_ERROR_CHECK(esp_wifi_start());
    gpio_set_level(D1, LOW);

    // ESP_LOGI(TAG, "espnow init");
    example_espnow_init();
    gpio_set_level(D1, HIGH);

    gpio_set_level(D2, HIGH);
    // ESP_LOGI(TAG, "Sleep to listen");
    // TickType_t start = xTaskGetTickCount();
    vTaskDelay(50 / portTICK_PERIOD_MS);
    // gpio_set_level(D2, LOW);
    // vTaskDelay(10 / portTICK_PERIOD_MS);
    // gpio_set_level(D2, HIGH);
    // vTaskDelay(10 / portTICK_PERIOD_MS);
    // TickType_t end = xTaskGetTickCount();
    // ESP_LOGI(TAG, "Slept for %d ticks", (end - start));
    // ESP_LOGI(TAG, "end %d", end);
    // ESP_LOGI(TAG, "portTICK %d", portTICK_PERIOD_MS);

    gpio_set_level(D2, LOW);
    gpio_set_level(D1, LOW);

    // ESP_LOGI(TAG, "deinit esp now");
    esp_now_deinit();
    gpio_set_level(D1, HIGH);

    // ESP_LOGI(TAG, "wifi stop");
    esp_wifi_stop();
    gpio_set_level(D1, LOW);

    // ESP_LOGI(TAG, "Going to sleep");
    gpio_wakeup_enable(WAKEUP_PIN, GPIO_INTR_HIGH_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    esp_sleep_enable_timer_wakeup(2e6);
    esp_light_sleep_start();

    // ESP_LOGI(TAG, "Wake up");
    // don't sleep before looping back into sleep
  }
}
