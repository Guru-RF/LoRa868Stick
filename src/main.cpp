// ============================================================
// RF.Guru LoRa868Stick - LoRa 868 MHz Communication Stick
// Companion transmitter for 8PswitchLORA (same protocol + key)
// RF.Guru - ON6URE
// ============================================================

#include <Arduino.h>
#include <FatFS.h>
#include <FatFSUSB.h>
#include <SPI.h>
#include <LoRa.h>
#include <Crypto.h>
#include <AES.h>
#include <hardware/watchdog.h>

#include "pins.h"
#include "config.h"
#include "cli.h"

// ============================================================
// VERSION
// ============================================================
#define VERSION "RF.Guru_LoRa868Stick 1.0"

// 3-byte packet header. Matches 8PswitchLORA exactly.
static const uint8_t LORA_HEADER[] = { 0x3C, 0xAA, 0x01 };

// Default AES-128 CBC key - matches 8PswitchLORA default.
static const uint8_t DEFAULT_AES_KEY[16] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F
};

// ============================================================
// ANSI color helpers
// ============================================================
static void purple(const char *msg) {
    unsigned long s = millis() / 1000;
    Serial.printf("\x1b[38;5;104m[%lu] %s\x1b[0m\r\n", s, msg);
}
static void yellow(const char *msg) {
    Serial.printf("\x1b[38;5;220m%s\x1b[0m\r\n", msg);
}
static void red(const char *msg) {
    Serial.printf("\x1b[1;5;31m -- %s\x1b[0m\r\n", msg);
}
static void green(const char *msg) {
    Serial.printf("\x1b[1;5;32m%s\x1b[0m\r\n", msg);
}

// ============================================================
// Globals
// ============================================================
Config config;
volatile bool driveConnected = false;
static volatile bool drivePlugEvent = false;
static volatile bool driveUnplugEvent = false;

// ============================================================
// LED helpers (active LOW)
// ============================================================
static inline void ledOn (int pin) { digitalWrite(pin, LOW);  }
static inline void ledOff(int pin) { digitalWrite(pin, HIGH); }

// ============================================================
// Config defaults / YAML
// ============================================================
static void setDefaults(Config &c) {
    c.name          = "stick0";
    c.loraFrequency = 868.000f;
    c.txPower       = 23;
    c.loraMode      = "fast";
    c.heartbeat     = false;
    memcpy(c.aesKey, DEFAULT_AES_KEY, 16);
}

static bool parseHexKey(const String &hex, uint8_t out[16]) {
    if (hex.length() != 32) return false;
    for (int i = 0; i < 16; i++) {
        char a = hex.charAt(i * 2);
        char b = hex.charAt(i * 2 + 1);
        int hi = (a >= '0' && a <= '9') ? a - '0' :
                 (a >= 'a' && a <= 'f') ? a - 'a' + 10 :
                 (a >= 'A' && a <= 'F') ? a - 'A' + 10 : -1;
        int lo = (b >= '0' && b <= '9') ? b - '0' :
                 (b >= 'a' && b <= 'f') ? b - 'a' + 10 :
                 (b >= 'A' && b <= 'F') ? b - 'A' + 10 : -1;
        if (hi < 0 || lo < 0) return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

static const char *defaultYaml() {
    return
        "# ============================================================\n"
        "# RF.Guru LoRa868Stick Configuration\n"
        "# Edit this file and eject the USB drive to apply.\n"
        "# ============================================================\n"
        "\n"
        "# Device name (for logging)\n"
        "name: \"stick0\"\n"
        "\n"
        "# LoRa TX/RX frequency in MHz\n"
        "lora_frequency: 868.000\n"
        "\n"
        "# LoRa TX power in dBm (2-23)\n"
        "tx_power: 23\n"
        "\n"
        "# LoRa modem profile (MUST match the receiver):\n"
        "#   fast - SF7 / BW125k / CR4-5  (default, ~50ms airtime)\n"
        "#   slow - SF12 / BW125k / CR4-8 (+10dB range, ~1.3s airtime)\n"
        "lora_mode: \"fast\"\n"
        "\n"
        "# Periodic [hb] heartbeat line to serial every 10s (true/false)\n"
        "heartbeat: false\n"
        "\n"
        "# AES-128 CBC key as 32 hex chars (must match the receiver)\n"
        "aes_key: \"000102030405060708090a0b0c0d0e0f\"\n";
}

static bool yamlParse(const char *yaml, Config &cfg) {
    String line;
    while (*yaml) {
        line = "";
        while (*yaml && *yaml != '\n') line += *yaml++;
        if (*yaml == '\n') yaml++;

        line.trim();
        if (line.startsWith("#") || line.length() == 0) continue;

        int colonIndex = line.indexOf(':');
        if (colonIndex <= 0) continue;

        String key   = line.substring(0, colonIndex);
        String value = line.substring(colonIndex + 1);
        key.trim();
        value.trim();

        {
            bool inQuote = false;
            int cut = -1;
            for (int i = 0; i < (int)value.length(); i++) {
                char c = value.charAt(i);
                if (c == '"') inQuote = !inQuote;
                else if (c == '#' && !inQuote) { cut = i; break; }
            }
            if (cut >= 0) value = value.substring(0, cut);
            value.trim();
        }

        value.replace("\"", "");
        value.trim();

        if      (key == "name")           cfg.name = value;
        else if (key == "lora_frequency") cfg.loraFrequency = value.toFloat();
        else if (key == "tx_power")       cfg.txPower = constrain(value.toInt(), 2, 23);
        else if (key == "lora_mode") {
            String v = value; v.toLowerCase();
            if (v == "fast" || v == "slow") cfg.loraMode = v;
            else Serial.println("[config] lora_mode: expected 'fast' or 'slow', ignoring");
        }
        else if (key == "heartbeat") {
            String v = value; v.toLowerCase();
            cfg.heartbeat = (v == "true" || v == "1" || v == "yes" || v == "on");
        }
        else if (key == "aes_key") {
            uint8_t k[16];
            if (parseHexKey(value, k)) memcpy(cfg.aesKey, k, 16);
            else Serial.println("[config] aes_key: expected 32 hex chars, ignoring");
        }
    }
    return true;
}

// ============================================================
// USB drive callbacks
// ============================================================
static void unplug(uint32_t i) {
    (void)i;
    driveConnected = false;
    driveUnplugEvent = true;
    rp2040.reboot();
}
static void plug(uint32_t i) {
    (void)i;
    driveConnected = true;
    drivePlugEvent = true;
}
static bool mountable(uint32_t i) {
    (void)i;
    driveConnected = true;
    return true;
}

// ============================================================
// macOS metadata cleanup
// ============================================================
static bool rmRecursive(const String &path) {
    Dir d = FatFS.openDir(path.c_str());
    while (d.next()) {
        String name = d.fileName();
        String full = path;
        if (!full.endsWith("/")) full += "/";
        full += name;
        if (d.isDirectory()) rmRecursive(full);
        else FatFS.remove(full.c_str());
    }
    if (path != "/") return FatFS.rmdir(path.c_str());
    return true;
}

static void cleanMacMetadata() {
    int removed = 0;
    Dir d = FatFS.openDir("/");
    while (d.next()) {
        String name = d.fileName();
        String full = "/" + name;
        bool isMeta = name.startsWith("._") || name == ".DS_Store" ||
                      name == ".Spotlight-V100" || name == ".Trashes" ||
                      name == ".fseventsd" || name == ".TemporaryItems" ||
                      name == ".apdisk";
        if (!isMeta) continue;
        if (d.isDirectory()) {
            if (rmRecursive(full)) removed++;
        } else {
            if (FatFS.remove(full.c_str())) removed++;
        }
    }
    if (removed > 0) {
        Serial.printf("Cleaned %d macOS metadata entries.\r\n", removed);
    }
}

// ============================================================
// AES-128 CBC helpers
// ============================================================
static void xorBlock16(uint8_t *dst, const uint8_t *src) {
    for (int i = 0; i < 16; i++) dst[i] ^= src[i];
}

// Encrypt: output = IV (16 bytes) || ciphertext (PKCS#7-padded).
// Returns total length (16 + paddedLen), or 0 on overflow.
static size_t aesCbcEncrypt(const uint8_t *msg, size_t msgLen,
                            uint8_t *out, size_t outSize) {
    uint8_t iv[16];
    for (int i = 0; i < 4; i++) {
        uint32_t r = random(0, 0x7FFFFFFF);
        memcpy(iv + i * 4, &r, 4);
    }

    uint8_t pad = 16 - (msgLen % 16);
    size_t paddedLen = msgLen + pad;
    if (16 + paddedLen > outSize) return 0;

    uint8_t block[16];
    memcpy(out, iv, 16);

    AES128 aes;
    aes.setKey(config.aesKey, 16);
    const uint8_t *prev = iv;

    for (size_t off = 0; off < paddedLen; off += 16) {
        // Build plaintext block with padding where needed
        size_t take = (off + 16 <= msgLen) ? 16 : (msgLen > off ? msgLen - off : 0);
        memcpy(block, msg + off, take);
        memset(block + take, pad, 16 - take);
        xorBlock16(block, prev);
        aes.encryptBlock(out + 16 + off, block);
        prev = out + 16 + off;
    }
    return 16 + paddedLen;
}

// Decrypt: input = IV || ciphertext. Strips PKCS#7 padding.
// Returns plaintext length, or 0 on malformed input / bad padding.
static size_t aesCbcDecrypt(const uint8_t *payload, size_t payloadLen,
                            uint8_t *out, size_t outSize) {
    if (payloadLen < 32 || (payloadLen - 16) % 16 != 0) return 0;
    size_t cipherLen = payloadLen - 16;
    if (cipherLen > outSize) return 0;

    AES128 aes;
    aes.setKey(config.aesKey, 16);

    const uint8_t *iv = payload;
    const uint8_t *ct = payload + 16;
    const uint8_t *prev = iv;
    for (size_t off = 0; off < cipherLen; off += 16) {
        aes.decryptBlock(out + off, ct + off);
        xorBlock16(out + off, prev);
        prev = ct + off;
    }

    uint8_t pad = out[cipherLen - 1];
    if (pad == 0 || pad > 16) return 0;
    for (size_t i = cipherLen - pad; i < cipherLen; i++) {
        if (out[i] != pad) return 0;
    }
    return cipherLen - pad;
}

// ============================================================
// LoRa TX helpers
// ============================================================
void loraSendRaw(const uint8_t *data, size_t len) {
    ledOn(PIN_LED_LOUT);
    LoRa.beginPacket();
    LoRa.write(data, len);
    LoRa.endPacket();
    ledOff(PIN_LED_LOUT);
}

void loraSendEncrypted(const char *text) {
    uint8_t frame[256];
    memcpy(frame, LORA_HEADER, sizeof(LORA_HEADER));

    size_t encLen = aesCbcEncrypt((const uint8_t *)text, strlen(text),
                                  frame + sizeof(LORA_HEADER),
                                  sizeof(frame) - sizeof(LORA_HEADER));
    if (encLen == 0) {
        red("loraSendEncrypted: message too long");
        return;
    }
    size_t total = sizeof(LORA_HEADER) + encLen;

    char msg[128];
    snprintf(msg, sizeof(msg), "TX encrypted '%s' (%u bytes on air)",
             text, (unsigned)total);
    yellow(msg);

    loraSendRaw(frame, total);
}

// ============================================================
// LoRa RX helpers
// ============================================================
static void hexPreview(char *out, size_t outSize,
                       const uint8_t *data, int len, int maxBytes = 16) {
    size_t pos = 0;
    int n = len < maxBytes ? len : maxBytes;
    for (int i = 0; i < n && pos + 3 < outSize; i++) {
        pos += snprintf(out + pos, outSize - pos, "%02x ", data[i]);
    }
    if (len > maxBytes && pos + 4 < outSize) {
        snprintf(out + pos, outSize - pos, "...");
    }
}

// Print one received packet: decode if it has the header, otherwise raw ASCII.
// Shows both encrypted-and-decrypted, and plaintext (ACKs) for debugging.
static void dumpPacket(const uint8_t *raw, int rawLen, int rssi, float snr) {
    ledOn(PIN_LED_LIN);

    char hex[80];
    hexPreview(hex, sizeof(hex), raw, rawLen);

    char line[192];
    snprintf(line, sizeof(line),
             "RX %d bytes rssi=%d snr=%.1f hex=[%s]",
             rawLen, rssi, snr, hex);
    purple(line);

    // Has our header?
    if (rawLen > (int)sizeof(LORA_HEADER) &&
        memcmp(raw, LORA_HEADER, sizeof(LORA_HEADER)) == 0) {
        const uint8_t *payload = raw + sizeof(LORA_HEADER);
        int payloadLen = rawLen - sizeof(LORA_HEADER);

        // Try AES decrypt first (encrypted TX packets)
        uint8_t plain[256];
        size_t plainLen = aesCbcDecrypt(payload, payloadLen,
                                        plain, sizeof(plain) - 1);
        if (plainLen > 0) {
            plain[plainLen] = '\0';
            char buf[280];
            snprintf(buf, sizeof(buf),
                     "  -> encrypted, decoded: '%s'", (const char *)plain);
            green(buf);
        } else {
            // Not valid AES - probably a plaintext ACK (shorter, no padding)
            char ascii[192];
            size_t n = payloadLen < (int)sizeof(ascii) - 1 ? payloadLen : sizeof(ascii) - 1;
            for (size_t i = 0; i < n; i++) {
                uint8_t b = payload[i];
                ascii[i] = (b >= 0x20 && b < 0x7f) ? (char)b : '.';
            }
            ascii[n] = '\0';
            char buf[280];
            snprintf(buf, sizeof(buf),
                     "  -> plaintext: '%s'", ascii);
            green(buf);
        }
    } else {
        yellow("  -> no header, raw packet");
    }

    ledOff(PIN_LED_LIN);
}

// Dump received packets for `timeoutMs`. Updates watchdog.
void loraRxDump(unsigned long timeoutMs) {
    char msg[64];
    snprintf(msg, sizeof(msg), "RX listening %lus ...", timeoutMs / 1000);
    yellow(msg);

    unsigned long start = millis();
    int count = 0;
    while (millis() - start < timeoutMs) {
        watchdog_update();
        int packetSize = LoRa.parsePacket();
        if (packetSize > 0) {
            uint8_t raw[256];
            int rawLen = 0;
            while (LoRa.available() && rawLen < (int)sizeof(raw)) {
                raw[rawLen++] = (uint8_t)LoRa.read();
            }
            int rssi = LoRa.packetRssi();
            float snr = LoRa.packetSnr();
            dumpPacket(raw, rawLen, rssi, snr);
            count++;
        }
        delay(1);
    }
    Serial.printf("RX done - %d packet%s received\r\n",
                  count, count == 1 ? "" : "s");
}

// Raw packet dump - no decode, just RSSI/SNR/hex. Mirrors 8PswitchLORA `rxtest`.
void loraRxRaw(unsigned long timeoutMs) {
    Serial.printf("rxtest: listening for %lus...\r\n", timeoutMs / 1000);
    unsigned long start = millis();
    int count = 0;
    while (millis() - start < timeoutMs) {
        watchdog_update();
        int packetSize = LoRa.parsePacket();
        if (packetSize > 0) {
            count++;
            int rssi = LoRa.packetRssi();
            float snr = LoRa.packetSnr();
            Serial.printf("  #%d len=%d rssi=%d snr=%.1f hex=[",
                          count, packetSize, rssi, snr);
            int n = 0;
            while (LoRa.available() && n < 32) {
                uint8_t b = LoRa.read();
                Serial.printf("%02x ", b);
                n++;
            }
            while (LoRa.available()) (void)LoRa.read();
            if (packetSize > 32) Serial.print("...");
            Serial.println("]");
        }
        delay(1);
    }
    Serial.printf("rxtest: %d packet%s received\r\n",
                  count, count == 1 ? "" : "s");
}

// Default ACK wait window, sized to the current modem profile.
unsigned long ackTimeoutMs() {
    return (config.loraMode == "slow") ? 2500UL : 300UL;
}

// Wait for a plaintext ACK (header + payload, AES decrypt fails/unused).
// Returns true if any packet with LORA_HEADER arrives within timeoutMs.
// Fills `ackOut` with the payload as ASCII (safe bytes only).
bool loraWaitAck(unsigned long timeoutMs, char *ackOut, size_t ackOutSize) {
    unsigned long start = millis();
    while (millis() - start < timeoutMs) {
        watchdog_update();
        int packetSize = LoRa.parsePacket();
        if (packetSize > (int)sizeof(LORA_HEADER)) {
            uint8_t raw[256];
            int rawLen = 0;
            while (LoRa.available() && rawLen < (int)sizeof(raw)) {
                raw[rawLen++] = (uint8_t)LoRa.read();
            }
            ledOn(PIN_LED_LIN);
            if (memcmp(raw, LORA_HEADER, sizeof(LORA_HEADER)) == 0) {
                const uint8_t *payload = raw + sizeof(LORA_HEADER);
                int payloadLen = rawLen - sizeof(LORA_HEADER);
                size_t n = payloadLen < (int)ackOutSize - 1 ? payloadLen : ackOutSize - 1;
                for (size_t i = 0; i < n; i++) {
                    uint8_t b = payload[i];
                    ackOut[i] = (b >= 0x20 && b < 0x7f) ? (char)b : '.';
                }
                ackOut[n] = '\0';
                ledOff(PIN_LED_LIN);
                return true;
            }
            ledOff(PIN_LED_LIN);
            // else: not our header, keep listening
        }
        delay(1);
    }
    return false;
}

// ============================================================
// Default config writer
// ============================================================
static void writeDefaultConfig(const char *path) {
    FatFS.remove(path);
    File f = FatFS.open(path, "w");
    if (!f) { red("ERR: cannot open config.txt for write"); return; }
    const char *def = defaultYaml();
    size_t defLen = strlen(def);
    size_t total = 0;
    const size_t CHUNK = 128;
    while (total < defLen) {
        size_t want = defLen - total;
        if (want > CHUNK) want = CHUNK;
        size_t w = f.write((const uint8_t *)def + total, want);
        if (w == 0 || w == (size_t)-1) {
            red("config.txt chunk write failed");
            break;
        }
        total += w;
        f.flush();
    }
    f.close();
    Serial.printf("Wrote default config.txt (%u bytes)\r\n", (unsigned)total);
    yamlParse(def, config);
}

static void applyLoraMode() {
    if (config.loraMode == "slow") {
        LoRa.setSpreadingFactor(12);
        LoRa.setSignalBandwidth(125E3);
        LoRa.setCodingRate4(8);
    } else {
        LoRa.setSpreadingFactor(7);
        LoRa.setSignalBandwidth(125E3);
        LoRa.setCodingRate4(5);
    }
}

// ============================================================
// Raw mode (#tx#, #rx#, #txack#) - default serial interface
// ============================================================
static String rawBuf;
static bool   arrowFlag = false;

static void handleRawCommand(const String &cmd) {
    // cmd already had leading '#' stripped and been trimmed
    if (cmd.length() >= 4 && cmd.substring(0, 3) == "tx#") {
        String msg = cmd.substring(3);
        loraSendEncrypted(msg.c_str());
        Serial.println("#tx#done");
    } else if (cmd.length() >= 7 && cmd.substring(0, 6) == "txack#") {
        String msg = cmd.substring(6);
        loraSendEncrypted(msg.c_str());
        char ack[96];
        if (loraWaitAck(ackTimeoutMs(), ack, sizeof(ack))) {
            Serial.printf("#txack#%s\r\n", ack);
        } else {
            Serial.println("#txack#timeout");
        }
    } else if (cmd.length() >= 4 && cmd.substring(0, 3) == "rx#") {
        int sec = cmd.substring(3).toInt();
        if (sec <= 0) sec = 5;
        loraRxDump((unsigned long)sec * 1000UL);
        Serial.println("#rx#done");
    } else {
        Serial.println("Unknown raw command. CTRL+] gives CLI prompt.");
        Serial.println("#tx#<msg>       - encrypted TX");
        Serial.println("#txack#<msg>    - encrypted TX + wait for ACK (300ms fast / 2.5s slow)");
        Serial.println("#rx#<sec>       - listen for N seconds");
    }
}

static void handleRawInput() {
    while (Serial.available()) {
        char c = Serial.read();

        if (c == 0x1D) {
            // Ctrl+] - enter CLI mode
            rawBuf = "";
            cliEnter();
            return;
        }

        if (c == '\r' || c == '\n') {
            if (rawBuf.length() > 0) {
                String line = rawBuf;
                rawBuf = "";
                Serial.println();
                line.trim();
                if (line.length() > 0 && line.charAt(0) == '#') {
                    handleRawCommand(line.substring(1));
                } else if (line.length() > 0) {
                    Serial.println("Unknown message. CTRL+] gives CLI prompt.");
                    Serial.println("#tx#<msg>       - encrypted TX");
                    Serial.println("#txack#<msg>    - encrypted TX + wait for ACK");
                    Serial.println("#rx#<sec>       - listen for N seconds");
                }
            } else {
                Serial.println();
            }
        } else if (c == 0x7f || c == 0x08) {
            if (rawBuf.length() > 0) {
                rawBuf.remove(rawBuf.length() - 1);
                Serial.print("\b \b");
            }
        } else if (c == '[' && arrowFlag == false) {
            // Start of an ANSI escape - consume but don't echo
            arrowFlag = true;
        } else if (arrowFlag) {
            // ANSI tail char - swallow
            arrowFlag = false;
        } else if (c >= 0x20 && c < 0x7f) {
            if (rawBuf.length() < 200) {
                rawBuf += c;
                Serial.write(c);
            }
        }
    }
}

// ============================================================
// SETUP
// ============================================================
void setup() {
    // LEDs first so we can show progress
    pinMode(PIN_LED_SIN,  OUTPUT); ledOff(PIN_LED_SIN);
    pinMode(PIN_LED_SOUT, OUTPUT); ledOff(PIN_LED_SOUT);
    pinMode(PIN_LED_LOUT, OUTPUT); ledOff(PIN_LED_LOUT);
    pinMode(PIN_LED_LIN,  OUTPUT); ledOff(PIN_LED_LIN);

    Serial.begin(115200);
    delay(2000);
    Serial.println("\r\n[boot] starting");

    // --- FatFS ---------------------------------------------------
    Serial.println("[boot] FatFS.begin()");
    Serial.flush();
    bool fsOk = FatFS.begin();
    if (!fsOk) {
        Serial.println("[boot] FatFS mount failed - formatting (5s to abort)...");
        Serial.flush();
        delay(5000);
        if (FatFS.format() && FatFS.begin()) {
            Serial.println("[boot] FatFS formatted & mounted.");
            fsOk = true;
        } else {
            red("FatFS unrecoverable.");
        }
    } else {
        Serial.println("[boot] FatFS mounted.");
    }
    if (fsOk) {
        fatfs::f_setlabel("LORASTCK");
        cleanMacMetadata();
    }

    setDefaults(config);

    // --- Load / create config ------------------------------------
    const char *filePath = "/config.txt";
    bool exists = fsOk && FatFS.exists(filePath);
    Serial.printf("[boot] config.txt exists: %s\r\n", exists ? "yes" : "no");

    bool parsed = false;
    if (exists) {
        File file = FatFS.open(filePath, "r");
        if (file) {
            String yamlContent;
            while (file.available()) yamlContent += (char)file.read();
            file.close();
            yamlContent.trim();
            Serial.printf("[boot] config bytes: %d\r\n", yamlContent.length());

            if (yamlContent == "firmwareupdate") {
                red("Firmware update requested.");
                FatFS.remove(filePath);
                delay(500);
                red("Rebooting into UF2 bootloader...");
                delay(500);
                reset_usb_boot(0, 0);
                while (true);
            }

            if (yamlContent.length() > 0) {
                yamlParse(yamlContent.c_str(), config);
                parsed = true;
            }
        }
    }

    if (fsOk && !parsed) {
        Serial.println("[boot] config missing/empty - writing default.");
        Serial.flush();
        writeDefaultConfig(filePath);
    }

    // --- USB mass storage ----------------------------------------
    Serial.println("[boot] starting FatFSUSB");
    Serial.flush();
    delay(200);
    FatFSUSB.onUnplug(unplug);
    FatFSUSB.onPlug(plug);
    FatFSUSB.driveReady(mountable);
    FatFSUSB.begin();
    delay(5000);
    Serial.println("[boot] FatFSUSB up");

    char banner[128];
    snprintf(banner, sizeof(banner), "%s -=- %s", config.name.c_str(), VERSION);
    red(banner);
    Serial.printf("[boot] config: name=%s freq=%.3f tx_power=%d mode=%s\r\n",
                  config.name.c_str(), config.loraFrequency,
                  config.txPower, config.loraMode.c_str());

    // --- LoRa ----------------------------------------------------
    Serial.println("[boot] LoRa SPI0 setup");
    SPI.setRX(PIN_SPI_MISO);
    SPI.setTX(PIN_SPI_MOSI);
    SPI.setSCK(PIN_SPI_CLK);
    SPI.begin();

    LoRa.setPins(PIN_LORA_CS, PIN_LORA_RST, -1);
    LoRa.setSPI(SPI);
    LoRa.setSPIFrequency(1000000);

    Serial.printf("[boot] LoRa.begin(%ld Hz)\r\n", (long)(config.loraFrequency * 1E6));
    Serial.flush();
    if (!LoRa.begin((long)(config.loraFrequency * 1E6))) {
        red("LoRa INIT ERROR - check wiring / CS / RST");
    } else {
        LoRa.setTxPower(config.txPower);
        LoRa.enableCrc();
        applyLoraMode();

        char msg[128];
        snprintf(msg, sizeof(msg),
                 "LoRa OK (freq=%.3f MHz pwr=%d dBm mode=%s)",
                 config.loraFrequency, config.txPower, config.loraMode.c_str());
        green(msg);
    }

    // --- CLI -----------------------------------------------------
    cliBegin();
    yellow("Raw mode active. '#tx#<msg>', '#rx#<sec>', '#txack#<msg>'.");
    yellow("Press Ctrl+] for interactive CLI.");

    // --- Watchdog ------------------------------------------------
    watchdog_enable(8000, true);
    Serial.println("[boot] watchdog enabled (8s)");
}

// ============================================================
// MAIN LOOP
// ============================================================
static unsigned long lastHeartbeat = 0;

static void heartbeat() {
    if (!config.heartbeat) return;
    unsigned long now = millis();
    if (now - lastHeartbeat < 10000) return;
    lastHeartbeat = now;
    Serial.printf("[hb] up=%lus heap=%u drv=%s mode=%s\r\n",
                  (unsigned long)(now / 1000),
                  (unsigned)rp2040.getFreeHeap(),
                  driveConnected ? "yes" : "no",
                  cliActive() ? "cli" : "raw");
}

void loop() {
    watchdog_update();

    if (drivePlugEvent) {
        drivePlugEvent = false;
        yellow("[usb] drive mounted by host");
    }
    if (driveUnplugEvent) {
        driveUnplugEvent = false;
        yellow("[usb] drive ejected - rebooting");
    }

    ledOn(PIN_LED_SIN);  // briefly flash on any serial activity below
    if (cliActive()) {
        cliTick();
    } else {
        handleRawInput();
    }
    ledOff(PIN_LED_SIN);

    heartbeat();
    delay(5);
}
