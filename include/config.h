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

// Default ACK wait window for txack, sized to the current modem profile:
//   fast (SF7) : ACK airtime ~50 ms  -> 300 ms (~6x margin)
//   slow (SF12): ACK airtime ~1.7 s  -> 2500 ms (~1.5x margin)
unsigned long ackTimeoutMs();
