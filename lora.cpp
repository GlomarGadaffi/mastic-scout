/* RadioLib under pure ESP-IDF — no Arduino.
 *
 * RadioLib ships an ESP-IDF HAL in examples/NonArduino/ESP-IDF/main/EspHal.h.
 * Vendor that file next to this one (or add RadioLib as a managed component and
 * copy EspHal.h in). It implements RadioLib's HAL over esp-idf spi_master +
 * gpio, so SX1262 talks on our dedicated host with zero Arduino dependency.
 *
 * Compiled as C++ (RadioLib is C++); app_main calls the extern "C" entry points
 * declared in lora.h. */

#include "EspHal.h"          // <-- vendor from RadioLib/examples/NonArduino/ESP-IDF
#include <RadioLib.h>

extern "C" {
#include "lora.h"
#include "alert.h"
#include "board_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
}

static const char *TAG = "lora";

/* Dedicated SPI host for the radio (separate from SD). */
static EspHal *hal = nullptr;
static SX1262 *radio = nullptr;

extern "C" esp_err_t lora_init(void)
{
    hal = new EspHal(LORA_SCK, LORA_MISO, LORA_MOSI);
    radio = new SX1262(new Module(hal, LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY));

    int st = radio->begin(LORA_FREQ_MHZ, LORA_BW_KHZ, LORA_SF, LORA_CR,
                          LORA_SYNC_WORD, LORA_TX_DBM);
    if (st != RADIOLIB_ERR_NONE) {
        ESP_LOGE(TAG, "SX1262 begin failed: %d", st);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "SX1262 up: %.1f MHz SF%d BW%.0f", LORA_FREQ_MHZ, LORA_SF, LORA_BW_KHZ);
    return ESP_OK;
}

/* SENSOR: block on the alert queue, transmit each alert. TX airtime is tens of
 * ms; because detection already enqueued and moved on, that latency never
 * touches the capture/detect path. */
extern "C" void lora_tx_task(void *arg)
{
    alert_t a;
    for (;;) {
        if (xQueueReceive(g_alert_q, &a, portMAX_DELAY) == pdTRUE) {
            int st = radio->transmit((uint8_t *)&a, sizeof(a));
            if (st != RADIOLIB_ERR_NONE)
                ESP_LOGW(TAG, "tx err %d (%s)", st, sig_name(a.sig));
        }
    }
}

/* BASE: receive frames, validate, push into the local queue for the UI task. */
extern "C" void lora_rx_task(void *arg)
{
    uint8_t buf[64];
    for (;;) {
        int n = radio->receive(buf, sizeof(buf));   /* blocking single-shot RX */
        if (n == RADIOLIB_ERR_NONE) {
            size_t len = radio->getPacketLength();
            if (len == sizeof(alert_t) && buf[0] == ALERT_MAGIC) {
                alert_t a;
                memcpy(&a, buf, sizeof(a));
                xQueueSend(g_alert_q, &a, 0);
            }
        }
        /* RADIOLIB_ERR_RX_TIMEOUT just loops; fine for a base station. */
    }
}
