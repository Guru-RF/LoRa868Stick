# RF.Guru LoRa868Stick — Protocol Specification

This document describes the over-the-air protocol used between a **LoRa controller** (typically the [LoRa868Stick](https://github.com/Guru-RF/LoRa868Stick)) and a compatible **LoRa receiver** (typically the [8PswitchLORA](https://github.com/Guru-RF/8PswitchLORA)). It covers the RF modem configuration, packet framing, encryption, command grammar, and the ACK reply. Anyone writing a new controller, a new peripheral, reverse-engineering traffic, or porting to another MCU should be able to build an interoperable implementation from this document alone.

---

## 1. RF Modem Configuration

All packets use standard Semtech LoRa modulation on the 868 MHz ISM band.

| Parameter       | Default value | Configurable via |
|-----------------|--------------|------------------|
| Frequency       | 868.000 MHz  | `lora_frequency` |
| TX power        | 23 dBm (PA_BOOST) | `tx_power` |
| Sync word       | 0x12 (public LoRa) | (hard-coded) |
| CRC             | enabled      | (hard-coded) |
| Preamble length | 8 symbols    | (library default) |

Two interoperable modem profiles are defined. **Both ends must use the same profile** or they will not hear each other.

| Profile | Spreading factor | Bandwidth | Coding rate | Typical airtime (18-byte payload) | Link budget vs. `fast` |
|---------|------------------|-----------|-------------|-----------------------------------|------------------------|
| **fast** *(default)* | SF7  | 125 kHz | 4/5 | ~50 ms   | 0 dB |
| **slow**             | SF12 | 125 kHz | 4/8 | ~1.3 s   | +10 dB |

Configured by the `lora_mode` key in `/config.txt` on each device.

---

## 2. Packet Framing

Every over-the-air packet — command or ACK — starts with a fixed 3-byte framing header:

```
+--------+--------+--------+----------------------+
| 0x3C   | 0xAA   | 0x01   | payload (0..250 B)   |
+--------+--------+--------+----------------------+
 byte 0   byte 1   byte 2   bytes 3..N
```

- **Byte 0 — `0x3C` (`<`)** — magic / protocol identifier
- **Byte 1 — `0xAA`** — source / from-address marker
- **Byte 2 — `0x01`** — packet version / flags

Any received packet whose first three bytes don't match is dropped.

The framing header is sent **as-is**, not encrypted and not CRC'd at the application layer. The LoRa MAC CRC already protects the whole frame, which is why the application layer can stay this simple.

---

## 3. Command Payload (Controller → Peripheral)

### 3.1 Encryption

Payload bytes 3..N carry an **AES-128 CBC**-encrypted command. The layout is:

```
+-----------------+-------------------------------+
| IV (16 bytes)   | ciphertext (16 * k bytes)     |
+-----------------+-------------------------------+
```

- **IV** — 16 random bytes, freshly generated for every TX
- **ciphertext** — the plaintext command, **PKCS#7-padded** to a 16-byte boundary, then CBC-encrypted with the 128-bit shared key

The minimum payload size is therefore **16 + 16 = 32 bytes of ciphertext**, plus the 3-byte framing header = **35 bytes on-air**.

The receiver **must** validate the PKCS#7 padding. Any padding byte outside the range `0x01..0x10`, or inconsistent padding bytes, indicates a wrong key or a corrupted packet and the payload must be discarded.

### 3.2 Plaintext grammar

After decryption and padding removal, the plaintext is ASCII. The exact grammar is peripheral-specific. For the 8-port switch family, the format is:

```
name/port
name/port/att
```

| Field | Type | Range | Meaning |
|-------|------|-------|---------|
| `name` | string | 1..15 chars | Target device name. Peripheral ignores commands addressed to any other name. |
| `port` | integer | 0..8 | 1..8 = activate that RF port. 0 = all ports off. |
| `att`  | decimal | 0.0..33.0 | (optional) Requested attenuation in dB. Defaults to `0` when omitted. |

### 3.3 Examples

```
sw0/1          → activate RF port 1 on device "sw0", attenuator off
sw0/3/6.0      → activate RF port 3, attenuator at 6.0 dB
sw0/0          → all ports off
```

The stick itself is agnostic to the plaintext format — it just encrypts whatever bytes you hand it via `#tx#…`, `txack …`, etc. New peripherals can define their own grammar under the same framing + encryption envelope.

---

## 4. ACK Payload (Peripheral → Controller)

After successfully applying a command, the peripheral transmits a **plaintext** ACK using the same 3-byte framing header.

### 4.1 Format (for the 8-port switch)

```
ACK:<name>/<port>/<att>
```

Example: `ACK:sw0/3/6.0`

### 4.2 Intentionally plaintext

The ACK is **not** encrypted. This is deliberate:

- The controller stick in `rx` mode displays both encrypted-decoded and plaintext payloads, so operators immediately see ACKs on their serial terminal.
- The ACK carries no secret — it's information the controller already has (it initiated the command).
- Keeping the ACK short saves airtime, which matters in `slow` mode (1.3 s per TX).

### 4.3 Timing

The peripheral fires the ACK **immediately** after applying the command, before returning to RX. The controller must stay in RX for at least one airtime window after its TX if it wants to catch the ACK.

For an 18-byte ACK (`<\xAA\x01ACK:sw0/3/6.0`-class payload) the round trip from "stick `endPacket()` returns" to "ACK fully received" is:

| Mode | ACK airtime | Receiver process | **Total round-trip** |
|------|-------------|------------------|----------------------|
| **fast** | ~51 ms      | ~10–50 ms        | **~100–150 ms**      |
| **slow** | ~1.7 s      | ~10–50 ms        | **~1.75–1.8 s**      |

The stick's `txack` CLI command and `#txack#…` raw command handle this automatically — they TX, then hold RX open for a window sized to the current `lora_mode`:

| Mode | `txack` wait window |
|------|---------------------|
| **fast** | **300 ms** (~6× ACK airtime) |
| **slow** | **2.5 s**  (~1.5× ACK airtime) |

If the window expires without a header-matching plaintext frame, the stick returns `#txack#timeout` (raw mode) or `ACK: timeout` (CLI).

---

## 5. Shared Secret — AES-128 Key

The AES-128 key is a 16-byte (128-bit) shared secret. It is configured as a 32-character hex string in `/config.txt` on both the stick and the peripheral.

**Default key (do NOT use for production):**

```
000102030405060708090a0b0c0d0e0f
```

To set a real shared secret, generate 16 random bytes and encode as hex:

```bash
openssl rand -hex 16
# e.g. a1b2c3d4e5f60718293a4b5c6d7e8f90
```

Update both ends to the same value. Mismatched keys manifest on the peripheral as a decrypt failure log line, and at the stick as an ACK timeout on `txack`.

---

## 6. Reference Implementations

| Role | File | Key function |
|------|------|--------------|
| Stick encrypt + TX | [`src/main.cpp`](src/main.cpp) | `loraSendEncrypted()` |
| Stick AES-128 CBC (encrypt) | [`src/main.cpp`](src/main.cpp) | `aesCbcEncrypt()` |
| Stick decrypt (RX dump) | [`src/main.cpp`](src/main.cpp) | `aesCbcDecrypt()` |
| Stick ACK wait | [`src/main.cpp`](src/main.cpp) | `loraWaitAck()` |
| Switch decrypt + dispatch | [8PswitchLORA/src/main.cpp](https://github.com/Guru-RF/8PswitchLORA/blob/main/src/main.cpp) | `handleSwitchRequest()` / `aesCbcDecrypt()` |
| Switch ACK TX | [8PswitchLORA/src/main.cpp](https://github.com/Guru-RF/8PswitchLORA/blob/main/src/main.cpp) | `sendAck()` |

---

## 7. Failure Modes

| Symptom (stick `rx` or peripheral log) | Meaning |
|----------------------------------------|---------|
| `too short, dropped` | Packet shorter than 4 bytes; framing header can't even be checked. |
| `header mismatch, dropped` / `no header, raw packet` | Header bytes 0..2 did not match `3C AA 01`. Another LoRa device on the band. |
| `decrypt failed (wrong key?)` | Header matched, but AES decrypt produced invalid PKCS#7 padding. Key mismatch or sender isn't using the same protocol. |
| `#txack#timeout` on the stick | TX succeeded but no plaintext packet returned within the mode-specific window (300 ms fast / 2.5 s slow). Receiver off-air, key mismatch, or name mismatch. |
| `#txack#ACK:…` on the stick | Happy path — peripheral applied the command and replied. |

---

## 8. Change Log

- **v1.0** — initial public protocol (AES-128 CBC, 3-byte header, `fast`/`slow` profiles, plaintext ACK).
