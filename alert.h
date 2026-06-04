#pragma once
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define ALERT_MAGIC 0xA5

typedef enum {
    SIG_NONE = 0,
    SIG_DEAUTH,
    SIG_DEAUTH_FLOOD,
    SIG_BEACON_FLOOD,
    SIG_AUTH_FLOOD,
    SIG_EVIL_TWIN,
    SIG_EAPOL,
    SIG_JAM_SUSPECT,     /* reserved: sustained RX with no decodable mgmt */
    SIG__MAX
} sig_t;

/* 17 bytes on the air. Packed + fixed-width so sender/receiver agree without
 * a serializer. Keep it small: LoRa airtime scales with payload length. */
typedef struct __attribute__((packed)) {
    uint8_t  magic;          /* ALERT_MAGIC — frame/version guard */
    uint8_t  sensor_id;      /* which sensor (you have two; allows >1 later)  */
    uint8_t  sig;            /* sig_t */
    uint8_t  channel;        /* 2.4GHz channel the event was seen on */
    int8_t   rssi;           /* dBm of the triggering frame */
    uint8_t  src[6];         /* addr2 of the suspect frame */
    uint16_t count;          /* per-second rate, for flood signatures */
    uint32_t uptime_s;       /* sensor uptime at trigger */
} alert_t;

static inline const char *sig_name(uint8_t s)
{
    static const char *n[SIG__MAX] = {
        "none", "deauth", "deauth_flood", "beacon_flood",
        "auth_flood", "evil_twin", "eapol", "jam?"
    };
    return (s < SIG__MAX) ? n[s] : "?";
}

/* Sensor side: detect_task fills these, lora_tx_task drains.
 * Base side: lora_rx_task fills these, ui_task drains. Same queue, both roles. */
extern QueueHandle_t g_alert_q;
