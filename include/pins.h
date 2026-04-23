// ============================================================
// RF.Guru LoRa868Stick - Hardware Pin Definitions
// RP2040 GPIO Mapping
// ============================================================

#pragma once

// Status LEDs (active LOW: LOW = on)
#define PIN_LED_SIN    17   // Serial Input LED
#define PIN_LED_SOUT   25   // Serial Output LED
#define PIN_LED_LOUT   24   // LoRa Output LED
#define PIN_LED_LIN    13   // LoRa Input LED

// LoRa SPI0 (RFM95)
#define PIN_SPI_MISO   16
#define PIN_SPI_CLK    18
#define PIN_SPI_MOSI   19
#define PIN_LORA_CS    21
#define PIN_LORA_RST   20
