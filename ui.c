#include "ui.h"
#include "alert.h"
#include "board_config.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <stdio.h>
#include <string.h>

/* OLED: SSD1306 via u8g2. Vendor the u8g2 ESP-IDF HAL (u8g2_esp32_hal.{c,h})
 * or add u8g2 from the component registry. The calls below are the standard
 * u8g2 API; only u8g2_esp32_hal_init wiring is board-specific. */
#include "u8g2.h"
#include "u8g2_esp32_hal.h"

/* MQTT egress to an orbic-fusion broker (ROLE_BASE only -- ui_init/ui_task
 * are never called on ROLE_SENSOR, see main.c). */
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "mqtt_client.h"

static const char *TAG = "ui";
static u8g2_t oled;

static esp_mqtt_client_handle_t s_mqtt_client;
static volatile bool            s_mqtt_connected;
static char                     s_mqtt_topic[64];

/* ---- haptics (LEDC on the vibration motor) ----------------------------- */
#define HAPTIC_TIMER   LEDC_TIMER_0
#define HAPTIC_CH      LEDC_CHANNEL_0
#define HAPTIC_FULL    255          /* 8-bit duty */

/* pattern = NULL-terminated list of on,off millisecond pairs */
typedef struct { uint16_t on, off; } pulse_t;
static const pulse_t P_DEAUTH[]  = {{60,60},{60,0},{0,0}};                 /* tap-tap   */
static const pulse_t P_FLOOD[]   = {{500,0},{0,0}};                        /* long buzz */
static const pulse_t P_EVIL[]    = {{80,80},{80,80},{80,0},{0,0}};         /* triple    */
static const pulse_t P_EAPOL[]   = {{200,0},{0,0}};                        /* one med   */
static const pulse_t P_DEFAULT[] = {{120,0},{0,0}};

static const pulse_t *pattern_for(uint8_t sig)
{
    switch (sig) {
    case SIG_DEAUTH:                                   return P_DEAUTH;
    case SIG_DEAUTH_FLOOD: case SIG_BEACON_FLOOD:
    case SIG_AUTH_FLOOD:   case SIG_JAM_SUSPECT:        return P_FLOOD;
    case SIG_EVIL_TWIN:                                 return P_EVIL;
    case SIG_EAPOL:                                     return P_EAPOL;
    default:                                            return P_DEFAULT;
    }
}

void haptic(uint8_t sig)
{
    for (const pulse_t *p = pattern_for(sig); p->on || p->off; p++) {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, HAPTIC_CH, HAPTIC_FULL);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, HAPTIC_CH);
        vTaskDelay(pdMS_TO_TICKS(p->on));
        ledc_set_duty(LEDC_LOW_SPEED_MODE, HAPTIC_CH, 0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, HAPTIC_CH);
        if (p->off) vTaskDelay(pdMS_TO_TICKS(p->off));
    }
}

/* ---- MQTT egress -------------------------------------------------------- */
static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();   /* auto-retry */
    }
}

static void mqtt_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *data)
{
    (void)arg; (void)base; (void)data;
    switch (event_id) {
    case MQTT_EVENT_CONNECTED:
        s_mqtt_connected = true;
        ESP_LOGI(TAG, "mqtt connected -> %s", s_mqtt_topic);
        break;
    case MQTT_EVENT_DISCONNECTED:
        s_mqtt_connected = false;
        ESP_LOGW(TAG, "mqtt disconnected");
        break;
    default:
        break;
    }
}

/* Brings up WiFi STA + esp-mqtt. Called once from ui_init(), base role only.
 * Non-fatal on failure (matches this file's own no-display-hardware
 * tolerance elsewhere): a base with bad WiFi creds still draws/buzzes
 * locally, it just never reaches the fusion backend. */
static void mqtt_start(void)
{
    snprintf(s_mqtt_topic, sizeof(s_mqtt_topic), "deauth-detector/%s/alert", BASE_SENSOR_ID);

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) { ESP_LOGW(TAG, "esp_netif_init failed: %s", esp_err_to_name(err)); return; }
    err = esp_event_loop_create_default();
    if (err != ESP_OK) { ESP_LOGW(TAG, "event loop create failed: %s", esp_err_to_name(err)); return; }
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wcfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    wifi_config_t sta_cfg = {0};
    strncpy((char *)sta_cfg.sta.ssid, BASE_WIFI_SSID, sizeof(sta_cfg.sta.ssid) - 1);
    strncpy((char *)sta_cfg.sta.password, BASE_WIFI_PASS, sizeof(sta_cfg.sta.password) - 1);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_connect();

    esp_mqtt_client_config_t mcfg = {
        .broker.address.uri = BASE_MQTT_BROKER_URI,
        .credentials.client_id = BASE_SENSOR_ID,
    };
    s_mqtt_client = esp_mqtt_client_init(&mcfg);
    if (!s_mqtt_client) { ESP_LOGW(TAG, "esp_mqtt_client_init failed"); return; }
    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    err = esp_mqtt_client_start(s_mqtt_client);
    if (err != ESP_OK) ESP_LOGW(TAG, "esp_mqtt_client_start failed: %s", esp_err_to_name(err));
}

/* JSON-encode the same fields alert_t carries on the wire (see spec/
 * envelope-schema.md in orbic-fusion for how "raw" vs. JSON payloads are
 * distinguished downstream). QoS 0, non-blocking -- never stall ui_task on
 * a slow/absent link, matching this codebase's LoRa-side "just don't wait"
 * posture (RADIOLIB_ERR_RX_TIMEOUT just loops in lora_rx_task). */
static void publish_alert(const alert_t *a)
{
    if (!s_mqtt_client || !s_mqtt_connected) return;
    char json[160];
    int len = snprintf(json, sizeof(json),
        "{\"t\":\"alert\",\"sig\":\"%s\",\"channel\":%u,\"rssi\":%d,"
        "\"src\":\"%02x:%02x:%02x:%02x:%02x:%02x\",\"count\":%u,\"uptime_s\":%u}",
        sig_name(a->sig), a->channel, a->rssi,
        a->src[0], a->src[1], a->src[2], a->src[3], a->src[4], a->src[5],
        (unsigned)a->count, (unsigned)a->uptime_s);
    if (len > 0 && len < (int)sizeof(json))
        esp_mqtt_client_publish(s_mqtt_client, s_mqtt_topic, json, len, 0, 0);
}

/* ---- init -------------------------------------------------------------- */
void ui_init(void)
{
    ledc_timer_config_t t = {
        .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = HAPTIC_TIMER, .freq_hz = 20000, .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&t);
    ledc_channel_config_t c = {
        .gpio_num = MOTOR_GPIO, .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = HAPTIC_CH, .timer_sel = HAPTIC_TIMER, .duty = 0, .hpoint = 0,
    };
    ledc_channel_config(&c);

    u8g2_esp32_hal_t hal = U8G2_ESP32_HAL_DEFAULT;
    hal.sda = OLED_SDA; hal.scl = OLED_SCL;
    u8g2_esp32_hal_init(hal);
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(
        &oled, U8G2_R0, u8g2_esp32_i2c_byte_cb, u8g2_esp32_gpio_and_delay_cb);
    u8x8_SetI2CAddress(&oled.u8x8, OLED_ADDR << 1);
    u8g2_InitDisplay(&oled);
    u8g2_SetPowerSave(&oled, 0);
    ESP_LOGI(TAG, "ui up");

    mqtt_start();
}

static void draw_alert(const alert_t *a)
{
    char l1[24], l2[24];
    snprintf(l1, sizeof(l1), "%s", sig_name(a->sig));
    if (a->count)
        snprintf(l2, sizeof(l2), "ch%u  %u/s  s%u", a->channel, a->count, a->sensor_id);
    else
        snprintf(l2, sizeof(l2), "ch%u %ddBm %02x%02x%02x",
                 a->channel, a->rssi, a->src[3], a->src[4], a->src[5]);

    u8g2_ClearBuffer(&oled);
    u8g2_SetFont(&oled, u8g2_font_ncenB10_tr);
    u8g2_DrawStr(&oled, 0, 22, l1);
    u8g2_SetFont(&oled, u8g2_font_6x12_tr);
    u8g2_DrawStr(&oled, 0, 44, l2);
    u8g2_SendBuffer(&oled);
}

void ui_task(void *arg)
{
    alert_t a;
    for (;;) {
        if (xQueueReceive(g_alert_q, &a, portMAX_DELAY) == pdTRUE) {
            draw_alert(&a);
            haptic(a.sig);
            /* Same alert, not a second queue consumer -- g_alert_q has
             * exactly one reader on this role already (see the ROLE_SENSOR
             * note in main.c about why a second consumer would race). */
            publish_alert(&a);
        }
    }
}
