# mastic-scout

distributed LoRa alert relay. two roles (SENSOR, BASE) communicate via LoRa link to forward proximity/threat alerts. sensors detect events, TX via LoRa; base station RX and annunciates (OLED, buzzer, etc.).

## roles

**SENSOR** (monitor mode):
- runs Wi-Fi in monitor-only mode (80 ms dwell per channel)
- detects beacons/probes in range via air capture
- evaluates alert logic (heuristic or rule-based detection)
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
typedef struct {
    uint32_t sender_id;      // unique sensor ID
    uint32_t timestamp;      // milliseconds since boot
    uint8_t  alert_type;     // 0=proximity, 1=intrusion, etc.
    uint16_t signal_strength; // RSSI from captured packet
    uint8_t  confidence;     // 0-100 detection score
} alert_t;
```

shared alert queue (`g_alert_q`, size 32) feeds both lora_tx (sensor) and ui (base).

## build & flash

```bash
# Sensor:
idf.py -D ROLE_SENSOR build flash monitor

# Base:
idf.py -D ROLE_BASE build flash monitor
```
