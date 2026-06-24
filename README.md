# deauth-detector

distributed 802.11 attack detector with a LoRa back-haul. two roles (SENSOR, BASE): sensors watch WiFi management frames for attack signatures and TX a compact alert over LoRa; the base RX and annunciates (OLED, buzzer).

## roles

**SENSOR** (monitor mode):
- runs Wi-Fi in monitor mode (80 ms dwell per channel)
- classifies management frames into attack signatures: deauth, deauth/auth/beacon floods, evil-twin, EAPOL capture, jam-suspect (`sig_t` in `alert.h`)
- TX alerts via LoRa to base on matched conditions
- core loop: capture → detect → queue alert → lora_tx_task drains queue

**BASE** (annunciator):
- RX all LoRa frames
- dequeues alerts, annunciates via OLED or LED/buzzer
- displays alert type, sender, timestamp
- no detection logic — pure display relay

## hardware

- **ESP32** (both roles)
- **SX1262 LoRa radio** (SPI, IRQ, reset GPIO)
- **OLED display** (base station, optional — SSD1306 or similar)
- **piezo buzzer** (base station, optional)

pin mapping: `board_config.h`

## alert protocol

```c
typedef struct __attribute__((packed)) {   // 17 bytes on the air
    uint8_t  magic;       // ALERT_MAGIC — frame/version guard
    uint8_t  sensor_id;   // which sensor
    uint8_t  sig;         // sig_t: attack signature
    uint8_t  channel;     // 2.4 GHz channel of the event
    int8_t   rssi;        // dBm of the triggering frame
    uint8_t  src[6];      // addr2 of the suspect frame
    uint16_t count;       // per-second rate (flood signatures)
    uint32_t uptime_s;    // sensor uptime at trigger
} alert_t;
```

packed and fixed-width so sender and receiver agree without a serializer (LoRa airtime scales with payload). one `g_alert_q` (depth 32) is shared by both roles: sensor-side `detect_task` fills it and `lora_tx_task` drains; base-side `lora_rx_task` fills and `ui_task` drains.

## build & flash

```bash
# Sensor:
idf.py -D ROLE_SENSOR build flash monitor

# Base:
idf.py -D ROLE_BASE build flash monitor
```
