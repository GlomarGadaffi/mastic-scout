# sentinel — two-board 802.11 IDS with out-of-band LoRa alerting

Native ESP-IDF (no Arduino). Two LilyGO T3-S3 MVSRBoards: one sniffs 2.4 GHz
and detects attacks, the other rides in your pocket and annunciates over a
sub-GHz LoRa link that survives the very jamming/deauth conditions the sensor
exists to catch.

## Topology

```
   BOARD A  (ROLE_SENSOR)                 BOARD B  (ROLE_BASE)
   ┌─────────────────────────┐           ┌──────────────────────────┐
   │ promiscuous capture (cb) │           │   lora_rx_task           │
   │        │ g_frame_q       │  SX1262   │        │ g_alert_q       │
   │ detect_task ── emit() ───┼──868/915──┼──▶ validate ──▶ ui_task  │
   │        │ g_alert_q       │   LoRa    │   OLED line + haptic     │
   │ lora_tx_task ── TX ──────┼──────────▶│   (no WiFi at all)       │
   └─────────────────────────┘           └──────────────────────────┘
        watches 2.4 GHz                       deaf to 2.4 GHz on purpose
```

Why split this way: an IDS that alerts over the network it monitors dies in a
2.4 GHz jam or deauth flood — exactly when you need the alert. SX1262 sub-GHz is
spectrally orthogonal, so the annunciator keeps receiving while 2.4 burns.

## One image, two roles

Role is compile-time. In `main/CMakeLists.txt`:

```cmake
# Board A (sensor):
target_compile_definitions(${COMPONENT_LIB} PRIVATE ROLE_SENSOR SENSOR_ID=1)
# Board B (annunciator): comment the line above, use:
# target_compile_definitions(${COMPONENT_LIB} PRIVATE ROLE_BASE)
```

```
idf.py set-target esp32s3
idf.py build flash monitor     # flash A as sensor, rebuild with ROLE_BASE, flash B
```

## Authoritative pinout (T3-S3 V1.2 + MVSR), from LilyGo utilities.h

| function | GPIO | | function | GPIO |
|----------|------|-|----------|------|
| LoRa SCK | 5  | | OLED SDA/SCL | 18 / 17 |
| LoRa MISO| 3  | | SD CS/MOSI/SCK/MISO | 13 / 11 / 14 / 2 |
| LoRa MOSI| 6  | | Vibration motor | 46 |
| LoRa CS  | 7  | | (mic EN shares 35 — unused here) |
| LoRa RST | 8  | | |
| LoRa BUSY| 34 | (silk "FSPI_CS0") | |
| LoRa DIO1| 33 | (silk "FSPI_HD")  | |

LoRa SPI host (5/3/6) and SD SPI host (14/11/2) are separate — log and uplink
paths don't contend. DIO1/BUSY corroborated by the Meshtastic tlora_t3s3_v1 fix.
**Verify** against your silk if you have a different RF variant.

## SX1262 vs SX1280 — pick SX1262 for this

SX1280 is LoRa **2.4G**: same band as the WiFi you're sniffing and as any 2.4
jammer. Its uplink desenses against your own front end and dies in the jam case.
SX1262 (868/915) is the correct choice for a WiFi IDS. The 2.4G part only wins
for ranging or single-band BOM — neither applies.

## Signatures (sensor)

deauth (each), deauth/disassoc flood, beacon flood, auth flood, evil-twin
(known SSID under wrong BSSID — populate `known[]`), EAPOL/handshake activity.
Stubs wired for KARMA and a jam heuristic. 1 s tumbling window for rate sigs.

## Alert wire format

17-byte packed `alert_t` (alert.h): magic, sensor_id, sig, channel, rssi,
src[6], count, uptime_s. Fixed-width so sender/receiver agree without a
serializer; small so LoRa airtime stays low. `sensor_id` lets you add a second
sensor later (you have two boards; this config uses one as base, but the format
already supports N sensors → 1 base).

## Threading discipline (the load-bearing rule)

Nothing slow runs inline with capture or detection:
- promiscuous callback: snapshot + enqueue, drop-on-full, never block.
- detect: classify, `emit()` an alert to g_alert_q, move on.
- LoRa TX (tens of ms airtime): its own task, drains g_alert_q.
- OLED/haptic on base: its own task.

On the sensor only `lora_tx_task` drains g_alert_q. Want a local OLED on the
sensor too? Fan the alert to a second queue — do **not** add ui_task on the
sensor role or it races lora_tx for alerts.

## Dependencies to vendor

- **RadioLib** (`jgromes/radiolib`) + its `EspHal.h` from
  examples/NonArduino/ESP-IDF → copy into `main/`. This is the no-Arduino HAL.
- **u8g2** + an ESP-IDF HAL (`u8g2_esp32_hal.{c,h}`) → copy into `main/` if the
  component you pick doesn't bundle it.

See `main/idf_component.yml`.

## Files

```
main/
  board_config.h   pins, role select, RF params
  ieee80211.h      frame-control decode + subtype constants
  frame_queue.h    bounded RX snapshot (callback -> detect)
  capture.c/.h     all-frames promiscuous + esp_timer hop  (sensor)
  detect.c/.h      signature engine -> g_alert_q           (sensor)
  alert.h          17B over-the-air alert struct + queue
  lora.cpp/.h      RadioLib SX1262 via EspHal; tx + rx tasks
  ui.c/.h          u8g2 OLED line + LEDC haptic patterns    (base)
  main.c           role branch: sensor vs annunciator
```
