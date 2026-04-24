# LoRa868Stick – USB LoRa Controller Stick for 868 MHz

The **RF.Guru LoRa868Stick** is a small USB stick that turns any computer into a LoRa controller on the 868 MHz ISM band. Plug it in, type a command on the serial line, and the stick transmits it — encrypted with AES-128 CBC — to a matching peripheral such as the [8-port LoRa switch](https://github.com/Guru-RF/8PswitchLORA). It also has an `rx` mode that listens for and decodes everything on the frequency for easy debugging.

Typical uses:

- Remote-control an [8PswitchLORA](https://github.com/Guru-RF/8PswitchLORA) (or any other peer speaking the [protocol](PROTOCOL.md))
- Scripted test automation over a LoRa link (raw `#tx#…` / `#txack#…` / `#rx#…` interface)
- Bench debugging: `rx` dumps every frame on-air with RSSI/SNR, plaintext + encrypted-decoded
- Light-weight LoRa beacon/stress generator

When ordered, your stick comes with the **latest firmware pre-installed**.

Buy here [LoRa868Stick (868 MHz)](https://shop.rf.guru/)

---

## 🧠 How it Works

Power the stick over USB-C. It enumerates as **two** USB devices:

1. A **USB CDC serial** port at 115 200 baud — your command interface
2. A **USB mass storage drive** labelled `LORASTCK` — carries `/config.txt`

On the serial line the stick starts in **raw mode**, which expects `#cmd#args` frames and is ideal for scripting. Press **Ctrl+]** to drop into the interactive **CLI** (`CLI> ` prompt) with tab-friendly commands like `tx`, `txack`, `rx`, `status`, `freq`, etc. Type `q` or `exit` to return to raw mode.

---

## 💡 Status LEDs

The stick has four small status LEDs:

| LED (GP) | Meaning |
|-----------|---------|
| `sIN` (17)  | serial input activity |
| `sOUT` (25) | serial output activity |
| `lOUT` (24) | LoRa TX in progress |
| `lIN`  (13) | LoRa RX (packet / ACK received) |

All LEDs are active-LOW.

---

## 🛠 Configuration – `config.txt`

All settings live in `/config.txt` on the USB drive. Delete it and eject to regenerate a fresh default. If the filesystem itself is corrupted, the firmware prints `formatting (5s to abort via reset)...` on serial before auto-reformatting — giving you a window to pull power if you didn't intend that.

### Default `config.txt`

```yaml
# ============================================================
# RF.Guru LoRa868Stick Configuration
# Edit this file and eject the USB drive to apply.
# ============================================================

# Device name (for logging)
name: "stick0"

# LoRa TX/RX frequency in MHz
lora_frequency: 868.000

# LoRa TX power in dBm (2-23)
tx_power: 23

# LoRa modem profile (MUST match the receiver):
#   fast - SF7 / BW125k / CR4-5  (default, ~50ms airtime)
#   slow - SF12 / BW125k / CR4-8 (+10dB range, ~1.3s airtime)
lora_mode: "fast"

# Periodic [hb] heartbeat line to serial every 10s (true/false)
heartbeat: false

# AES-128 CBC key as 32 hex chars (must match the receiver)
aes_key: "000102030405060708090a0b0c0d0e0f"
```

### Fields

| Key              | Type / range | Purpose |
|------------------|--------------|---------|
| `name`           | 1..15 chars  | Device identifier shown in logs. |
| `lora_frequency` | MHz          | Carrier frequency (default 868.000 for EU ISM). |
| `tx_power`       | 2..23 dBm    | TX power. |
| `lora_mode`      | `fast`/`slow`| Modem profile. Must match the receiver. |
| `heartbeat`      | `true`/`false` | Enable the periodic `[hb]` line on serial every 10 s. Default `false` (quiet). |
| `aes_key`        | 32 hex chars | Shared AES-128 key. Must match the receiver. |

---

## 📡 Over-the-Air Protocol

See [PROTOCOL.md](PROTOCOL.md) for the full specification. Quick summary:

- **Frequency**: 868 MHz
- **Modem**: SF7/BW125k/CR4-5 (fast) or SF12/BW125k/CR4-8 (slow), sync word 0x12, CRC on
- **Framing**: 3-byte header `0x3C 0xAA 0x01` followed by the payload
- **Command**: AES-128 CBC encrypted (IV || ciphertext), plaintext format depends on peripheral (`name/port[/att]` for the 8-port switch)
- **ACK**: plaintext reply with the same 3-byte header, e.g. `ACK:sw0/3/6.0`

Minimum on-air frame is 35 bytes (3 header + 16 IV + 16 ciphertext block).

---

## 🗣 Serial Interface

Two modes share the same serial port:

### Raw mode (default — script-friendly)

| Command | Effect |
|---------|--------|
| `#tx#<msg>`      | Encrypt `<msg>` with AES-128 CBC and transmit. Replies `#tx#done`. |
| `#txack#<msg>`   | Same as above, then hold RX open for an ACK. Window is sized to `lora_mode`: **300 ms** in `fast`, **2.5 s** in `slow`. Replies `#txack#<ack>` or `#txack#timeout`. |
| `#rx#<sec>`      | Listen for `<sec>` seconds. Every frame is dumped with RSSI/SNR/hex; encrypted frames are decoded when the key matches, plaintext frames (ACKs) are shown as ASCII. Replies `#rx#done`. |
| `Ctrl+]`         | Enter interactive CLI. |

Unknown input prints a short usage hint.

### CLI mode (interactive — entered with Ctrl+])

Prompt: `CLI> `. Type `help` for the full list. Highlights:

| Command | Effect |
|---------|--------|
| `help` / `?`        | List all CLI commands |
| `status`            | Print config + runtime state (heap, uptime, RSSI, key) |
| `ls` / `cat`        | Filesystem listing / print `/config.txt` |
| `tx <msg>`          | Encrypted TX |
| `txack <msg>`       | Encrypted TX + wait for ACK (300 ms in `fast`, 2.5 s in `slow`), print it |
| `rx [sec]`          | Listen, decode encrypted + plaintext (default 10 s) |
| `rxtest [sec]`      | Dump every raw LoRa packet — no decode, no filter (band scanner) |
| `freq <MHz>`        | Retune the LoRa radio live |
| `power <2..23>`     | Change TX power |
| `mode <fast\|slow>` | Switch modem profile |
| `heartbeat <on\|off>` | Toggle the periodic `[hb]` log line |
| `free`              | Print uptime + free heap |
| `formatdisk`        | **Wipe** the filesystem (3 s warning, then reboots) |
| `reboot`            | Software reset |
| `bootloader`        | Jump to UF2 bootloader mode |
| `q` / `exit`        | Leave the CLI, return to raw mode |

The CLI echoes what you type, handles backspace, and works as a real shell.

---

## 🔁 Firmware Update

**Pre-built UF2 files are attached to every [GitHub release](https://github.com/Guru-RF/LoRa868Stick/releases).** You don't need to build the firmware yourself — grab `LoRa868Stick-vX.Y.Z.uf2` from the latest release's **Assets** section.

To flash it:

1. Open `config.txt` on the `LORASTCK` drive
2. Replace its contents with the single word `firmwareupdate`
3. Save the file
4. **Eject or safely remove** the `LORASTCK` drive
5. The device reboots into **UF2 bootloader mode** and appears as a drive named `RPI-RP2`
6. Drag the downloaded `LoRa868Stick-*.uf2` file onto the `RPI-RP2` drive
7. The firmware updates and reboots automatically
   ✅ Your configuration (`config.txt`) remains untouched

Alternatively, from the serial CLI type `bootloader` — same effect.

### Building from source (optional)

If you want to build your own firmware, you need [PlatformIO](https://platformio.org/install) (`pip install platformio`):

```bash
pio run -e pico                 # build firmware → .pio/build/pico/firmware.uf2
pio run -e pico -t upload       # build + flash via USB (device must be in UF2 mode)
```

Every push to `main` and every tag is built automatically via GitHub Actions ([`.github/workflows/build.yml`](.github/workflows/build.yml)). Pushing a tag like `v1.2.3` triggers an attached UF2 on the matching release.

---

## 🔄 Restoring Default Configuration

Delete `config.txt` from the `LORASTCK` drive and eject. On next boot the firmware writes a fresh default file.

For a completely clean start, use `formatdisk` from the serial CLI — it wipes the whole filesystem, then reboots and regenerates the default `config.txt`.

---

## 📻 RFI Considerations

Transmitting at high power while the USB CDC is live can couple RF into the USB data lines on some PCB builds, temporarily disturbing the serial console. To minimise this:

- Keep `tx_power` as low as your link budget allows (5–10 dBm is often plenty on the bench)
- Use a **ferrite choke** on the USB cable near the stick
- Keep the USB cable short
- Terminate the antenna output into **50 Ω** during bench testing — this eliminates the issue entirely and confirms it's RFI, not firmware

The *transmission* itself is not affected by CDC state. Only the ASCII trace on the serial console may drop characters around a high-power TX event.

---

## 📦 Features Summary

- Controller-style USB stick for 868 MHz LoRa with pre-installed firmware
- Two-mode serial: scriptable raw `#cmd#` interface + interactive `CLI>` shell (Ctrl+])
- AES-128 CBC encrypted TX with freshly random IV per packet
- `rx` mode decodes both encrypted frames and plaintext ACKs — ideal for debugging
- `txack` transmit-and-wait-for-ACK (window auto-sized to `lora_mode` — 300 ms fast / 2.5 s slow)
- Two modem profiles: `fast` (SF7, ~50 ms airtime) and `slow` (SF12, +10 dB range)
- USB Mass Storage drive `LORASTCK` for live config editing
- YAML-style `config.txt` with live reload on eject
- Auto-cleanup of macOS metadata every boot (no more `.Trashes` filling the drive)
- Optional heartbeat (`[hb]`) log line, configurable from disk or CLI
- `firmwareupdate` keyword in `config.txt` triggers UF2 bootloader for drag-and-drop update
- GitHub Actions CI builds a UF2 on every push to `main` and attaches one to every release tag
- Watchdog-protected main loop (8 s) — a locked-up receive path self-heals in < 10 s

---

## 📃 License

This project is licensed under the [MIT License](LICENSE).

---

## 🧃 Credits

Developed by [ON6URE – RF.Guru](https://rf.guru)
Firmware and documentation licensed under MIT.
