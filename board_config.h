#pragma once
/* LilyGO T3-S3 (V1.2 motherboard, SX1262) + MVSRBoard backplate.
 * Pins from Xinyuan-LilyGO/LilyGo-LoRa-Series examples/.../utilities.h
 * DIO1/BUSY corroborated by the Meshtastic tlora_t3s3_v1 fix.
 * VERIFY against your board's silk if you bought a different RF variant. */

/* ---- role select: define exactly ONE (or pass -D from CMake) ----------- */
#if !defined(ROLE_SENSOR) && !defined(ROLE_BASE)
#define ROLE_SENSOR     /* deployed sniffer/IDS. flash the other board ROLE_BASE */
#endif

/* ---- LoRa SX1262 (its own SPI host: SPI2/HSPI) ------------------------- */
#define LORA_SCK        5
#define LORA_MISO       3
#define LORA_MOSI       6
#define LORA_CS         7
#define LORA_RST        8
#define LORA_BUSY       34      /* == silk "FSPI_CS0": pad is taken by the radio */
#define LORA_DIO1       33      /* == silk "FSPI_HD":  pad is taken by the radio */

/* 868.0 (EU) or 915.0 (US/ITU-2). SX1262 is sub-GHz — spectrally clear of the
 * 2.4 GHz band the sensor monitors, which is the whole point of the uplink. */
#define LORA_FREQ_MHZ   915.0
#define LORA_BW_KHZ     125.0
#define LORA_SF         9       /* SF9: ~tens-of-ms airtime for a 17B alert */
#define LORA_CR         5       /* 4/5 */
#define LORA_TX_DBM     17      /* raise toward 22 if your antenna/region allow */
#define LORA_SYNC_WORD  0x2B    /* private; both boards must match */

/* ---- OLED (SSD1306, I2C) ---------------------------------------------- */
#define OLED_SDA        18
#define OLED_SCL        17
#define OLED_ADDR       0x3C

/* ---- Vibration motor (MVSR backplate) --------------------------------- */
#define MOTOR_GPIO      46

/* ---- SD (separate SPI host from LoRa) --------------------------------- */
#define SD_CS           13
#define SD_MOSI         11
#define SD_SCK          14
#define SD_MISO         2

/* WiFi monitor: 2.4 GHz only on the S3. channel list lives in capture.c. */

/* ---- ROLE_BASE: WiFi STA + MQTT egress to an orbic-fusion broker ------- */
/* No config UI on this board -- credentials are compiled in. Override all
 * four before flashing a real deployment. SENSOR_ID here is a string (the
 * MQTT topic's second segment), unrelated to alert_t's numeric sensor_id. */
#define BASE_WIFI_SSID          "your-wifi-ssid"
#define BASE_WIFI_PASS          "your-wifi-password"
#define BASE_MQTT_BROKER_URI    "mqtt://192.168.1.1:1883"
#define BASE_SENSOR_ID          "base-01"
