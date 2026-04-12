#ifndef CONFIG_H
#define CONFIG_H

// ============================================================
// LoRa868Stick Configuration
// RF.Guru - ON6URE
// ============================================================

// ============================================================
// GPIO Pin Assignments
// ============================================================

// Status LEDs (active LOW)
#define PIN_LED_SIN      17   // Serial Input LED
#define PIN_LED_SOUT     25   // Serial Output LED
#define PIN_LED_LOUT     24   // LoRa Output LED
#define PIN_LED_LIN      13   // LoRa Input LED

// LoRa Radio (RFM9x) SPI
#define PIN_SPI_SCK      18
#define PIN_SPI_MOSI     19
#define PIN_SPI_MISO     16
#define PIN_LORA_CS      21
#define PIN_LORA_RST     20

// ============================================================
// Radio Configuration
// ============================================================

#define RADIO_FREQ       868E6   // 868.000 MHz
#define TX_POWER         23      // dBm (max for RFM95)

// ============================================================
// Encryption Key (AES-128 CBC)
// ============================================================
// IMPORTANT: Change this key to match your deployment!
// Must be exactly 16 bytes for AES-128.
static const uint8_t AES_KEY[16] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F
};

// ============================================================
// Timing
// ============================================================

#define WATCHDOG_TIMEOUT_MS  5000   // Watchdog timeout in ms

#endif // CONFIG_H
