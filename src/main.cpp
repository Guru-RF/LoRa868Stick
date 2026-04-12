// ============================================================
// LoRa868Stick - LoRa 868 MHz Communication Stick
// Converted from CircuitPython to Arduino C++ for RP2040 (UF2)
// RF.Guru - ON6URE
// ============================================================

#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <Crypto.h>
#include <AES.h>
#include <hardware/watchdog.h>

#include "config.h"

// ============================================================
// LED Helpers (active LOW)
// ============================================================
static void ledOn(int pin)  { digitalWrite(pin, LOW);  }
static void ledOff(int pin) { digitalWrite(pin, HIGH); }

// ============================================================
// Serial Helpers
// ============================================================
static void sendSerial(const char* str) {
    Serial.print(str);
}

static void sendSerial(const uint8_t* data, size_t len) {
    Serial.write(data, len);
}

static void sendSerialByte(uint8_t b) {
    Serial.write(b);
}

// ============================================================
// ANSI Color Helpers
// ============================================================
static void sendYellow(const char* text) {
    Serial.print("\x1b[38;5;220m");
    Serial.print(text);
    Serial.print("\x1b[0m");
}

static void sendYellow(const String& text) {
    Serial.print("\x1b[38;5;220m");
    Serial.print(text);
    Serial.print("\x1b[0m");
}

// ============================================================
// AES-128 CBC Encryption (manual CBC using block cipher)
// ============================================================
static AES128 aes;

// PKCS7 padding
static size_t padMessage(const uint8_t* input, size_t inputLen,
                         uint8_t* output, size_t outputSize) {
    uint8_t padding = 16 - (inputLen % 16);
    size_t paddedLen = inputLen + padding;
    if (paddedLen > outputSize) return 0;
    memcpy(output, input, inputLen);
    memset(output + inputLen, padding, padding);
    return paddedLen;
}

// XOR 16 bytes in place: dst ^= src
static void xorBlock(uint8_t* dst, const uint8_t* src) {
    for (int i = 0; i < 16; i++) dst[i] ^= src[i];
}

// Encrypt: returns IV (16 bytes) + ciphertext
static size_t encryptMessage(const uint8_t* message, size_t msgLen,
                             uint8_t* output, size_t outputSize) {
    // Generate random IV
    uint8_t iv[16];
    for (int i = 0; i < 4; i++) {
        uint32_t r = random(0, 0x7FFFFFFF);
        memcpy(iv + i * 4, &r, 4);
    }

    // Pad message
    uint8_t padded[256];
    size_t paddedLen = padMessage(message, msgLen, padded, sizeof(padded));
    if (paddedLen == 0 || (16 + paddedLen) > outputSize) return 0;

    // Copy IV to output
    memcpy(output, iv, 16);

    // CBC encrypt: each block XOR'd with previous ciphertext (or IV)
    aes.clear();
    aes.setKey(AES_KEY, 16);
    const uint8_t* prev = iv;
    for (size_t off = 0; off < paddedLen; off += 16) {
        uint8_t block[16];
        memcpy(block, padded + off, 16);
        xorBlock(block, prev);
        aes.encryptBlock(output + 16 + off, block);
        prev = output + 16 + off;
    }

    return 16 + paddedLen;
}

// Decrypt: input is IV (16 bytes) + ciphertext
static size_t decryptMessage(const uint8_t* payload, size_t payloadLen,
                             uint8_t* output, size_t outputSize) {
    if (payloadLen < 32) return 0; // minimum: 16 IV + 16 block

    const uint8_t* iv = payload;
    const uint8_t* ciphertext = payload + 16;
    size_t cipherLen = payloadLen - 16;

    if (cipherLen > outputSize) return 0;

    // CBC decrypt: decrypt block then XOR with previous ciphertext (or IV)
    aes.clear();
    aes.setKey(AES_KEY, 16);
    const uint8_t* prev = iv;
    for (size_t off = 0; off < cipherLen; off += 16) {
        aes.decryptBlock(output + off, ciphertext + off);
        xorBlock(output + off, prev);
        prev = ciphertext + off;
    }

    // Remove PKCS7 padding
    uint8_t padVal = output[cipherLen - 1];
    if (padVal > 0 && padVal <= 16) {
        return cipherLen - padVal;
    }
    return cipherLen;
}

// ============================================================
// LoRa TX
// ============================================================
static void loraSend(const uint8_t* data, size_t len) {
    ledOn(PIN_LED_LOUT);
    LoRa.beginPacket();
    LoRa.write(data, len);
    LoRa.endPacket();
    ledOff(PIN_LED_LOUT);
}

static void loraSendEncrypted(const char* message) {
    uint8_t encrypted[256];
    size_t encLen = encryptMessage((const uint8_t*)message, strlen(message),
                                   encrypted, sizeof(encrypted));
    if (encLen == 0) return;

    // Build packet: '<' + 0xAA + 0x01 + encrypted
    uint8_t packet[259];
    packet[0] = '<';
    packet[1] = 0xAA;
    packet[2] = 0x01;
    memcpy(packet + 3, encrypted, encLen);

    loraSend(packet, 3 + encLen);
}

// ============================================================
// LoRa RX (blocking with timeout, feeds watchdog)
// ============================================================
static bool loraReceive(uint8_t* buf, size_t* len, unsigned long timeoutSec) {
    unsigned long startMs = millis();
    unsigned long timeoutMs = timeoutSec * 1000UL;

    while (millis() - startMs < timeoutMs) {
        watchdog_update();
        int packetSize = LoRa.parsePacket();
        if (packetSize > 0) {
            size_t i = 0;
            while (LoRa.available() && i < 255) {
                buf[i++] = LoRa.read();
            }
            *len = i;
            return true;
        }
        delay(1);
    }
    return false;
}

// ============================================================
// State
// ============================================================
static String inputBuf  = "";
static String lastData  = "";
static bool   cliMode   = false;
static bool   arrowFlag = false;
static String modeType  = "CLI";

// ============================================================
// CLI Command Handlers
// ============================================================
static void showPrompt() {
    sendSerial(modeType.c_str());
    sendSerial(">");
}

static void clearLine() {
    sendSerial("\33[2K\r");
}

static void handleCLI(const String& data) {
    if (data == "rx") {
        modeType = "RX";
        clearLine();
        showPrompt();
    } else if (data == "tx") {
        modeType = "TX";
        clearLine();
        showPrompt();
    } else if (data == "q") {
        cliMode = false;
        modeType = "CLI";
        clearLine();
    } else {
        clearLine();
        sendYellow("tx -> TRANSMIT MODE");
        sendSerial("\r\n");
        sendYellow("rx -> LISTEN 4 ALL LORA PACKETS");
        sendSerial("\r\n");
        sendYellow("q -> QUIT TO NORMAL MODE");
        sendSerial("\r\n");
        showPrompt();
    }
}

static void handleRX(const String& data) {
    if (data == "q") {
        modeType = "CLI";
        clearLine();
        showPrompt();
    } else if (data.length() >= 3 && data.substring(0, 2) == "rx") {
        int listenTime = data.substring(2).toInt();
        if (listenTime <= 0) listenTime = 5;

        clearLine();
        String msg = "RX:receiving>Waiting for LoRa packets for " +
                     String(listenTime) + " seconds ...";
        sendYellow(msg);
        sendSerial("\r\n");

        ledOn(PIN_LED_LIN);
        uint8_t buf[256];
        size_t len;
        bool gotPacket = true;
        while (gotPacket) {
            gotPacket = loraReceive(buf, &len, listenTime);
            if (gotPacket && len > 3) {
                // Skip 3-byte header, send payload
                sendSerial(buf + 3, len - 3);
                sendSerial("\r\n");
            }
        }
        ledOff(PIN_LED_LIN);

        sendYellow("nothing ;(");
        sendSerial("\r\n");
        modeType = "RX";
        showPrompt();
    } else {
        clearLine();
        sendYellow("rx?? -> Listen for lora packets for ?? seconds");
        sendSerial("\r\n");
        sendYellow("q -> QUIT TO NORMAL MODE");
        sendSerial("\r\n");
        showPrompt();
    }
}

static void handleTX(const String& data) {
    if (data.length() == 0 || data == "?") {
        clearLine();
        sendYellow("example: sw0/1 -> SWITCHES sw0 port1");
        sendSerial("\r\n");
        sendYellow("q -> QUIT TO NORMAL MODE");
        sendSerial("\r\n");
        showPrompt();
    } else if (data == "q") {
        modeType = "CLI";
        clearLine();
        showPrompt();
    } else {
        String msg = "TX:transmit>" + data;
        sendSerial("\r");
        sendYellow(msg);
        sendSerial("\r\n");
        showPrompt();
        loraSendEncrypted(data.c_str());
    }
}

static void handleRawCommand(const String& data) {
    if (data.length() >= 4 && data.substring(0, 3) == "tx#") {
        String msg = data.substring(3);
        sendYellow("TX:transmit>" + msg);
        sendSerial("\r\n");
        loraSendEncrypted(msg.c_str());
        sendSerial("#tx#done\r\n");
    } else if (data.length() >= 4 && data.substring(0, 3) == "rx#") {
        int listenTime = data.substring(3).toInt();
        if (listenTime <= 0) listenTime = 5;

        ledOn(PIN_LED_LIN);
        uint8_t buf[256];
        size_t len;
        bool gotPacket = true;
        while (gotPacket) {
            gotPacket = loraReceive(buf, &len, listenTime);
            if (gotPacket && len > 3) {
                sendSerial(buf + 3, len - 3);
                sendSerial("\r\n");
            }
        }
        sendSerial("#rx#done\r\n");
        ledOff(PIN_LED_LIN);
    }
}

// ============================================================
// Setup
// ============================================================
void setup() {
    // Enable watchdog
    watchdog_enable(WATCHDOG_TIMEOUT_MS, true);
    watchdog_update();

    // USB Serial
    Serial.begin(115200);

    // LED pins
    pinMode(PIN_LED_SIN, OUTPUT);
    pinMode(PIN_LED_SOUT, OUTPUT);
    pinMode(PIN_LED_LOUT, OUTPUT);
    pinMode(PIN_LED_LIN, OUTPUT);
    ledOff(PIN_LED_SIN);
    ledOff(PIN_LED_SOUT);
    ledOff(PIN_LED_LOUT);
    ledOff(PIN_LED_LIN);

    // Configure SPI pins for LoRa module
    SPI.setRX(PIN_SPI_MISO);
    SPI.setTX(PIN_SPI_MOSI);
    SPI.setSCK(PIN_SPI_SCK);

    // Initialize LoRa
    LoRa.setPins(PIN_LORA_CS, PIN_LORA_RST, -1);
    LoRa.setSPI(SPI);

    if (!LoRa.begin(RADIO_FREQ)) {
        // LoRa init failed — blink all LEDs
        while (1) {
            watchdog_update();
            ledOn(PIN_LED_SIN);
            ledOn(PIN_LED_SOUT);
            delay(200);
            ledOff(PIN_LED_SIN);
            ledOff(PIN_LED_SOUT);
            delay(200);
        }
    }

    LoRa.setTxPower(TX_POWER);
    LoRa.enableCrc();

    delay(500);
    watchdog_update();

    Serial.print("RF.Guru\r\nLoRa868Stick 0.2\r\n");
}

// ============================================================
// Main Loop
// ============================================================
void loop() {
    watchdog_update();

    // Read serial input
    while (Serial.available() > 0) {
        ledOn(PIN_LED_SIN);
        char c = Serial.read();

        if (c == '\r') {
            // Enter pressed — process command
            String command = inputBuf;
            inputBuf = "";
            sendSerial("\r\n");

            if (cliMode) {
                showPrompt();
            }

            // Process command
            command.trim();
            if (command.length() > 0 || cliMode) {
                if (cliMode) {
                    if (modeType == "CLI") {
                        handleCLI(command);
                    } else if (modeType == "RX") {
                        handleRX(command);
                    } else if (modeType == "TX") {
                        handleTX(command);
                    }
                } else {
                    if (command.charAt(0) == '#') {
                        handleRawCommand(command.substring(1));
                    } else {
                        sendSerial("Unknown message !! CTRL + ] Gives CLI promt.\r\n");
                        sendSerial("#tx#<msg> for direct msg to switches.\r\n");
                        sendSerial("#rx#<time> receive lora msgs.\r\n");
                    }
                }
                lastData = command;
            }
        } else if (arrowFlag && c == 'A') {
            // Up arrow — recall last command
            clearLine();
            if (cliMode) {
                showPrompt();
            }
            Serial.print(lastData);
            inputBuf = lastData;
            arrowFlag = false;
        } else if (c == '[') {
            arrowFlag = true;
        } else if (c == 0x7F) {
            // Backspace
            if (inputBuf.length() > 0) {
                inputBuf.remove(inputBuf.length() - 1);
            }
            clearLine();
            Serial.print("\r");
            if (cliMode) {
                showPrompt();
            }
            Serial.print(inputBuf);
        } else if (c == 0x1D) {
            // Ctrl+] — enter CLI mode
            inputBuf = "";
            clearLine();
            cliMode = true;
            modeType = "CLI";
            showPrompt();
        } else {
            inputBuf += c;
            sendSerialByte(c);
        }

        delay(10);
        ledOff(PIN_LED_SIN);
    }
}
