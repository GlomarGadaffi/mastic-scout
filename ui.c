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

static const char *TAG = "ui";
static u8g2_t oled;

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
        }
    }
}
