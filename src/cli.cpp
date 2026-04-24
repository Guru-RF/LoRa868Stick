// ============================================================
// RF.Guru LoRa868Stick - Serial CLI (entered via Ctrl+])
// ============================================================

#include "cli.h"
#include "config.h"

#include <Arduino.h>
#include <FatFS.h>
#include <LoRa.h>

static String buf;
static bool   active = false;

static void prompt() {
    Serial.print("CLI> ");
}

static void help() {
    Serial.println(F("CLI commands:"));
    Serial.println(F("  help / ?             - this list"));
    Serial.println(F("  status               - print config + runtime state"));
    Serial.println(F("  ls                   - list files on the drive"));
    Serial.println(F("  cat                  - print config.txt"));
    Serial.println(F("  tx <msg>             - send encrypted message"));
    Serial.println(F("  txack <msg>          - send encrypted + wait for ACK (300ms fast / 2.5s slow)"));
    Serial.println(F("  rx [sec]             - listen, decode encrypted + plaintext (default 10s)"));
    Serial.println(F("  rxtest [sec]         - dump every raw LoRa packet (no decode)"));
    Serial.println(F("  freq <MHz>           - retune LoRa"));
    Serial.println(F("  power <2..23>        - set LoRa TX power (dBm)"));
    Serial.println(F("  mode <fast|slow>     - set LoRa modem profile"));
    Serial.println(F("  heartbeat <on|off>   - toggle periodic [hb] line"));
    Serial.println(F("  free                 - uptime + free heap"));
    Serial.println(F("  formatdisk           - WIPE the USB drive and reboot"));
    Serial.println(F("  reboot               - software reset"));
    Serial.println(F("  bootloader           - jump to UF2 bootloader"));
    Serial.println(F("  q / exit             - leave CLI, return to raw mode"));
}

static void status() {
    Serial.printf("name:           %s\r\n", config.name.c_str());
    Serial.printf("lora_frequency: %.3f MHz\r\n", config.loraFrequency);
    Serial.printf("tx_power:       %d dBm\r\n", config.txPower);
    Serial.printf("lora_mode:      %s\r\n", config.loraMode.c_str());
    Serial.printf("heartbeat:      %s\r\n", config.heartbeat ? "on" : "off");
    Serial.print  ("aes_key:        ");
    for (int i = 0; i < 16; i++) Serial.printf("%02x", config.aesKey[i]);
    Serial.println();
    Serial.printf("driveConnected: %s\r\n", driveConnected ? "yes" : "no");
    Serial.printf("uptime:         %lu s\r\n", (unsigned long)(millis() / 1000));
    Serial.printf("free heap:      %u bytes\r\n", (unsigned)rp2040.getFreeHeap());
    Serial.printf("lora rssi:      %d dBm (last packet)\r\n", LoRa.packetRssi());
}

static void cat() {
    File f = FatFS.open("/config.txt", "r");
    if (!f) { Serial.println(F("cat: cannot open /config.txt")); return; }
    while (f.available()) Serial.write(f.read());
    f.close();
    Serial.println();
}

static void ls() {
    Dir d = FatFS.openDir("/");
    uint32_t count = 0, total = 0;
    while (d.next()) {
        String name = d.fileName();
        uint32_t sz = d.fileSize();
        total += sz;
        count++;
        Serial.print("  ");
        if (sz < 1024)
            Serial.printf("%6u B  ", (unsigned)sz);
        else if (sz < 1024 * 1024)
            Serial.printf("%6.1f KB ", sz / 1024.0f);
        else
            Serial.printf("%6.2f MB ", sz / 1048576.0f);
        Serial.println(name);
    }
    Serial.printf("%lu file%s, %.1f KB total\r\n",
                  (unsigned long)count, count == 1 ? "" : "s", total / 1024.0f);
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

static void exec(String line) {
    line.trim();
    if (line.length() == 0) return;

    int sp = line.indexOf(' ');
    String head = sp < 0 ? line : line.substring(0, sp);
    String arg  = sp < 0 ? String() : line.substring(sp + 1);
    arg.trim();

    if (head == "help" || head == "?") {
        help();
    } else if (head == "status") {
        status();
    } else if (head == "ls") {
        ls();
    } else if (head == "cat") {
        cat();
    } else if (head == "tx") {
        if (arg.length() == 0) { Serial.println(F("tx: need message")); return; }
        loraSendEncrypted(arg.c_str());
    } else if (head == "txack") {
        if (arg.length() == 0) { Serial.println(F("txack: need message")); return; }
        loraSendEncrypted(arg.c_str());
        char ack[96];
        if (loraWaitAck(ackTimeoutMs(), ack, sizeof(ack))) {
            Serial.printf("ACK: %s\r\n", ack);
        } else {
            Serial.println(F("ACK: timeout"));
        }
    } else if (head == "rx") {
        int sec = arg.length() ? arg.toInt() : 10;
        if (sec <= 0) sec = 10;
        loraRxDump((unsigned long)sec * 1000UL);
    } else if (head == "rxtest") {
        int sec = arg.length() ? arg.toInt() : 10;
        if (sec <= 0) sec = 10;
        loraRxRaw((unsigned long)sec * 1000UL);
    } else if (head == "freq") {
        double mhz = arg.toFloat();
        if (mhz < 100.0 || mhz > 1000.0) {
            Serial.println(F("freq: expected MHz in LoRa range"));
            return;
        }
        config.loraFrequency = (float)mhz;
        LoRa.end();
        if (!LoRa.begin((long)(mhz * 1E6))) {
            Serial.println(F("LoRa re-init failed"));
            return;
        }
        LoRa.setTxPower(config.txPower);
        LoRa.enableCrc();
        applyLoraMode();
        Serial.printf("lora tuned -> %.3f MHz\r\n", mhz);
    } else if (head == "power") {
        int p = arg.toInt();
        if (p < 2 || p > 23) { Serial.println(F("power: 2..23")); return; }
        config.txPower = p;
        LoRa.setTxPower(p);
        Serial.printf("tx_power -> %d dBm\r\n", p);
    } else if (head == "mode") {
        String v = arg; v.toLowerCase();
        if (v != "fast" && v != "slow") {
            Serial.println(F("mode: expected 'fast' or 'slow'"));
            return;
        }
        config.loraMode = v;
        applyLoraMode();
        Serial.printf("lora_mode -> %s\r\n", v.c_str());
    } else if (head == "heartbeat") {
        String v = arg; v.toLowerCase();
        if (v == "on" || v == "true" || v == "1" || v == "yes") {
            config.heartbeat = true;
        } else if (v == "off" || v == "false" || v == "0" || v == "no") {
            config.heartbeat = false;
        } else {
            Serial.println(F("heartbeat: expected 'on' or 'off'"));
            return;
        }
        Serial.printf("heartbeat -> %s\r\n", config.heartbeat ? "on" : "off");
    } else if (head == "free") {
        Serial.printf("uptime:    %lu s\r\n", (unsigned long)(millis() / 1000));
        Serial.printf("free heap: %u bytes\r\n", (unsigned)rp2040.getFreeHeap());
    } else if (head == "formatdisk") {
        Serial.println(F("formatdisk: wiping filesystem in 3s..."));
        Serial.flush();
        delay(3000);
        FatFS.end();
        if (FatFS.format())
            Serial.println(F("format ok, rebooting..."));
        else
            Serial.println(F("format FAILED, rebooting anyway..."));
        Serial.flush();
        delay(200);
        rp2040.reboot();
    } else if (head == "reboot") {
        Serial.println(F("rebooting..."));
        delay(100);
        rp2040.reboot();
    } else if (head == "bootloader") {
        Serial.println(F("jumping to UF2 bootloader..."));
        delay(200);
        reset_usb_boot(0, 0);
        while (true);
    } else if (head == "q" || head == "exit") {
        active = false;
        Serial.println(F("leaving CLI"));
        return;
    } else {
        Serial.print(F("? "));
        Serial.println(line);
    }
}

void cliBegin() {
    buf.reserve(128);
}

void cliEnter() {
    active = true;
    buf = "";
    Serial.println();
    Serial.println(F("[CLI] entered - type 'help' or 'q' to leave"));
    prompt();
}

bool cliActive() {
    return active;
}

void cliTick() {
    if (!active) return;
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\r' || c == '\n') {
            Serial.println();
            if (buf.length() > 0) {
                String line = buf;
                buf = "";
                exec(line);
            }
            if (active) prompt();
        } else if (c == 0x7f || c == 0x08) {
            if (buf.length() > 0) {
                buf.remove(buf.length() - 1);
                Serial.print("\b \b");
            }
        } else if (c == 0x1D) {
            // Ctrl+] while already in CLI — ignore (consume)
        } else if (c >= 0x20 && c < 0x7f) {
            if (buf.length() < 200) {
                buf += c;
                Serial.write(c);
            }
        }
    }
}
