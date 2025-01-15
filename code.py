import binascii
import time

import adafruit_rfm9x
import board
import busio
import digitalio
import usb_cdc
from digitalio import DigitalInOut
from microcontroller import watchdog as w
from watchdog import WatchDogMode

import config

# configure watchdog
w.timeout = 5
w.mode = WatchDogMode.RESET
w.feed()

# setup usb serial
serial = usb_cdc.data

# leds
sInLED = digitalio.DigitalInOut(board.GP17)
sInLED.direction = digitalio.Direction.OUTPUT
sInLED.value = True

sOutLED = digitalio.DigitalInOut(board.GP25)
sOutLED.direction = digitalio.Direction.OUTPUT
sOutLED.value = True

lOutLED = digitalio.DigitalInOut(board.GP24)
lOutLED.direction = digitalio.Direction.OUTPUT
lOutLED.value = True

lInLED = digitalio.DigitalInOut(board.GP13)
lInLED.direction = digitalio.Direction.OUTPUT
lInLED.value = True

# basic encryption
import aesio
import os

# Padding functions
def pad_message(message, block_size=16):
    padding = block_size - (len(message) % block_size)
    return message + bytes([padding] * padding)

# Encrypt with IV
def encrypt_message(message, key):
    iv = os.urandom(16)
    padded_message = pad_message(message)
    aes = aesio.AES(key, aesio.MODE_CBC, iv)
    encrypted_message = bytearray(len(padded_message))
    aes.encrypt_into(padded_message, encrypted_message)

    return iv + encrypted_message

# Unpadding function
def unpad_message(message):
    padding = message[-1]
    return message[:-padding]

# Decrypt function
def decrypt_message(payload, key):
    iv = payload[:16]
    # Ensure IV length is correct
    if len(iv) != 16:
        raise ValueError(f"Invalid IV length: {len(iv)} (expected 16 bytes)")
    encrypted_message = payload[16:]
    aes = aesio.AES(key, aesio.MODE_CBC, iv)
    decrypted_message = bytearray(len(encrypted_message))
    aes.decrypt_into(encrypted_message, decrypted_message)

    return unpad_message(decrypted_message)


def yellow(data):
    return "\x1b[38;5;220m" + data + "\x1b[0m"


def red(data):
    return "\x1b[1;5;31m" + data + "\x1b[0m"


# our version
print("RF.Guru\nLoRa868Stick 0.1")


def recvSerial():
    if serial is not None:
        if serial.connected:
            if serial.in_waiting > 0:
                sInLED.value = False
                letter = serial.read(serial.in_waiting)
                time.sleep(0.01)
                sInLED.value = True
                return letter


def sendSerial(data):
    sOutLED.value = False
    if serial is not None:
        if serial.connected:
            serial.write(data)
            time.sleep(0.01)
            sOutLED.value = True


# Lora Stuff
RADIO_FREQ_MHZ = 868.000
CS = DigitalInOut(board.GP21)
RESET = DigitalInOut(board.GP20)
spi = busio.SPI(board.GP18, MOSI=board.GP19, MISO=board.GP16)
rfm9x = adafruit_rfm9x.RFM9x(
    spi, CS, RESET, RADIO_FREQ_MHZ, baudrate=1000000, agc=False, crc=True
)
rfm9x.tx_power = 23

sendSerial(b"RF.Guru\r\nLoRa868Stick 0.1\r\n")

string = ""
lastdata = ""
mode = False
arrow = False
modetype = "CLI"
buf = ""
while True:
    w.feed()
    input = recvSerial()
    if input is not None:
        buf = buf + input.decode("utf-8")
        if input == b"\r":
            string = buf
            buf = ""
            sendSerial(b"\r\n")
            if mode is True:
                sendSerial(modetype.encode("utf8"))
                sendSerial(b">")
        elif arrow is True and input == b"A":
            sendSerial(b"\33[2K\r")
            if mode is True:
                sendSerial(modetype.encode("utf8"))
                sendSerial(b">")
            sendSerial(lastdata.encode("utf8"))
            buf = lastdata
            arrow = False
        elif input == b"[":
            arrow = True
        else:
            if input == b"\x7f":
                sendSerial(b"\33[2K\r")
                buf = buf[:-2]
                sendSerial(b"\r")
                if mode is True:
                    sendSerial(modetype.encode("utf8"))
                    sendSerial(b">")
                sendSerial(buf.encode("utf8"))
            if input == b"\x1d":
                buf = ""
                sendSerial(b"\33[2K\r")
                sendSerial(modetype.encode("utf8"))
                sendSerial(b">")
                mode = True
            else:
                sendSerial(input)

    if string != "":
        data = string.strip()
        if mode is True:
            if modetype == "CLI":
                if data == "rx":
                    modetype = "RX"
                    sendSerial(b"\33[2K\r")
                    sendSerial(modetype.encode("utf8"))
                    sendSerial(b">")
                    data = ""
                elif data == "tx":
                    modetype = "TX"
                    sendSerial(b"\33[2K\r")
                    sendSerial(modetype.encode("utf8"))
                    sendSerial(b">")
                    data = ""
                elif data == "q":
                    mode = False
                    modetype = "CLI"
                    sendSerial(b"\33[2K\r")
                    data = ""
                else:
                    sendSerial(b"\33[2K\r")
                    msg = yellow("tx -> TRANSMIT MODE")
                    sendSerial(msg.encode("ascii"))
                    sendSerial(b"\r\n")
                    msg = yellow("rx -> LISTEN 4 ALL LORA PACKETS")
                    sendSerial(msg.encode("ascii"))
                    sendSerial(b"\r\n")
                    msg = yellow("q -> QUIT TO NORMAL MODE")
                    sendSerial(msg.encode("ascii"))
                    sendSerial(b"\r\n")
                    sendSerial(modetype.encode("utf8"))
                    sendSerial(b">")
                    data = ""
            elif modetype == "RX":
                if data == "q":
                    modetype = "CLI"
                    sendSerial(b"\33[2K\r")
                    sendSerial(modetype.encode("utf8"))
                    sendSerial(b">")
                    data = ""
                elif data[:2] == "rx":
                    listentime = int(data[2:])
                    sendSerial(b"\33[2K\r")
                    msg = yellow(
                        "RX:receiving>Waiting for LoRa packets for "
                        + str(listentime)
                        + " seconds ..."
                    )
                    sendSerial(msg.encode("ascii"))
                    sendSerial(b"\r\n")
                    lInLED.value = False
                    packet = "ok"
                    while packet is not None:
                        packet = rfm9x.receive(w, with_header=True, timeout=listentime)
                        if packet is not None:
                            print("RAW Packet:")
                            print(packet)
                            sendSerial(packet[3:])
                            sendSerial(b"\r\n")
                    lInLED.value = True
                    msg = yellow("nothing ;(")
                    sendSerial(msg.encode("ascii"))
                    sendSerial(b"\r\n")
                    modetype = "RX"
                    sendSerial(modetype.encode("utf8"))
                    sendSerial(b">")
                else:
                    sendSerial(b"\33[2K\r")
                    msg = yellow("rx?? -> Listen for lora packets for ?? seconds")
                    sendSerial(msg.encode("ascii"))
                    sendSerial(b"\r\n")
                    msg = yellow("q -> QUIT TO NORMAL MODE")
                    sendSerial(msg.encode("ascii"))
                    sendSerial(b"\r\n")
                    sendSerial(modetype.encode("utf8"))
                    sendSerial(b">")
                    data = ""
            elif modetype == "TX":
                if data != "" and data != "?":
                    if data == "q":
                        modetype = "CLI"
                        sendSerial(b"\33[2K\r")
                        sendSerial(modetype.encode("utf8"))
                        sendSerial(b">")
                        data = ""
                    else:
                        msg = yellow("TX:transmit>" + data)
                        sendSerial(b"\r")
                        sendSerial(msg.encode("ascii"))
                        sendSerial(b"\r\n")
                        sendSerial(modetype.encode("utf8"))
                        sendSerial(b">")
                        lOutLED.value = False
                        rfm9x.send(
                            w,
                            bytes("{}".format("<"), "UTF-8")
                            + binascii.unhexlify("AA")
                            + binascii.unhexlify("01")
                            + encrypt_message(bytes("{}".format(data), "UTF-8"),config.key),
                        )
                        lOutLED.value = True
                else:
                    sendSerial(b"\33[2K\r")
                    msg = yellow("example: sw0/1 -> SWITCHES sw0 port1")
                    sendSerial(msg.encode("ascii"))
                    sendSerial(b"\r\n")
                    msg = yellow("q -> QUIT TO NORMAL MODE")
                    sendSerial(msg.encode("ascii"))
                    sendSerial(b"\r\n")
                    sendSerial(modetype.encode("utf8"))
                    sendSerial(b">")
        else:
            if data[:1] == "#":
                data = data[1:]
                if data[:3] == "tx#":
                    data = data[3:]
                    lOutLED.value = False
                    msg = yellow("TX:transmit>" + data)
                    print(msg)
                    rfm9x.send(
                        w,
                        bytes("{}".format("<"), "UTF-8")
                        + binascii.unhexlify("AA")
                        + binascii.unhexlify("01")
                        + encrypt_message(bytes("{}".format(data), "UTF-8"),config.key),
                    )
                    lOutLED.value = True
                    sendSerial(b"#tx#done\r\n")
                if data[:3] == "rx#":
                    data = data[3:]
                    packet = "ok"
                    lInLED.value = False
                    while packet is not None:
                        packet = rfm9x.receive(w, with_header=True, timeout=int(data))
                        if packet is not None:
                            print("RAW Packet:")
                            print(packet)
                            sendSerial(packet[3:])
                            sendSerial(b"\r\n")
                    sendSerial(b"#rx#done\r\n")
                    lInLED.value = True
            else:
                sendSerial(b"Unknown message !! CTRL + ] Gives CLI promt.\r\n")
                sendSerial(b"#tx#<msg> for direct msg to switches.\r\n")
                sendSerial(b"#rx#<time> receive lora msgs.\r\n")

        lastdata = data
        string = ""
