#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "board_config.h"
#include "alert.h"
#include "lora.h"
#include "ui.h"

static const char *TAG = "sentinel";

/* Shared alert pipe. Sensor: detect -> (this) -> lora_tx.
 *                    Base:   lora_rx -> (this) -> ui.        */
QueueHandle_t g_alert_q;

#ifdef ROLE_SENSOR
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "capture.h"
#include "detect.h"
#endif

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    g_alert_q = xQueueCreate(32, sizeof(alert_t));
    ESP_ERROR_CHECK(lora_init());

#if defined(ROLE_SENSOR)
    ESP_LOGI(TAG, "role: SENSOR");
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));   /* monitor only */
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(capture_start(80));                   /* 80 ms dwell */

    /* WiFi driver on core 0; keep detection + radio off it. */
    xTaskCreatePinnedToCore(detect_task,  "detect",  4096, NULL, 6, NULL, 1);
    xTaskCreatePinnedToCore(lora_tx_task, "lora_tx", 4096, NULL, 5, NULL, 1);
    /* NOTE: only lora_tx drains g_alert_q here. For a local sensor OLED too,
     * fan the alert out to a second queue — don't add ui_task on this role or
     * it will race lora_tx for alerts. */

#elif defined(ROLE_BASE)
    ESP_LOGI(TAG, "role: BASE (annunciator)");
    ui_init();
    xTaskCreatePinnedToCore(lora_rx_task, "lora_rx", 4096, NULL, 6, NULL, 1);
    xTaskCreatePinnedToCore(ui_task,      "ui",      4096, NULL, 5, NULL, 1);

#else
#error "Define ROLE_SENSOR or ROLE_BASE (see board_config.h / CMake)"
#endif

    ESP_LOGI(TAG, "sentinel running");
}
