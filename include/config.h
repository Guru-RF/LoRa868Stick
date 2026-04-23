// ============================================================
// RF.Guru LoRa868Stick - shared Config struct
// ============================================================

#pragma once

#include <Arduino.h>

struct Config {
    String  name;
    float   loraFrequency;
    int     txPower;
    String  loraMode;   // "fast" or "slow"
    bool    heartbeat;  // periodic [hb] line to serial
    uint8_t aesKey[16];
};

extern Config config;
extern volatile bool driveConnected;

// LoRa helpers, defined in main.cpp
void loraSendEncrypted(const char *text);
void loraSendRaw(const uint8_t *data, size_t len);
bool loraWaitAck(unsigned long timeoutMs, char *ackOut, size_t ackOutSize);
void loraRxDump(unsigned long timeoutMs);
void loraRxRaw(unsigned long timeoutMs);
