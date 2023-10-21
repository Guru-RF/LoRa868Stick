import board
import time
import busio
import digitalio
import usb_cdc
import binascii
import adafruit_rfm9x
from digitalio import DigitalInOut, Direction, Pull

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
                letter = serial.read()
                #.decode('utf-8')
                time.sleep(0.01)
                sInLED.value = True
                return letter
 
def sendSerial(data):
    sOutLED.value = False
    serial.write(data)
    time.sleep(0.01)
    sOutLED.value = True


# Lora Stuff
RADIO_FREQ_MHZ = 868.000
CS = DigitalInOut(board.GP21)
RESET = DigitalInOut(board.GP20)
spi = busio.SPI(board.GP18, MOSI=board.GP19, MISO=board.GP16)
rfm9x = adafruit_rfm9x.RFM9x(spi, CS, RESET, RADIO_FREQ_MHZ, baudrate=1000000, agc=False,crc=True)
rfm9x.tx_power = 5

sendSerial(b"RF.Guru\r\nLoRa868Stick 0.1\r\n")

string = ""
lastdata = ""
mode = False
arrow = False
modetype = "CLI"
buf = ""
while True:
    input = recvSerial()
    if input is not None:
        buf = buf + input.decode('utf-8')
        if input == b'\r':
            string = buf
            buf = ""
            sendSerial(b'\r\n')
            if mode is True:
                sendSerial(modetype.encode('utf8'))
                sendSerial(b'>')
        elif arrow is True and input == b'A':
            sendSerial(b'\33[2K\r')
            if mode is True:
                sendSerial(modetype.encode('utf8'))
                sendSerial(b'>')
            sendSerial(lastdata.encode('utf8'))
            buf = lastdata
            arrow = False
        elif input == b'[':
            arrow = True
        else:
            if input == b'\x7f':
                sendSerial(b'\33[2K\r')
                buf = buf[:-2]
                sendSerial(b'\r')
                if mode is True:
                    sendSerial(modetype.encode('utf8'))
                    sendSerial(b'>')
                sendSerial(buf.encode('utf8'))
            if input == b'\x1d':
                buf = ""
                sendSerial(b'\33[2K\r')
                sendSerial(modetype.encode('utf8'))
                sendSerial(b'>')
                mode = True
            else: 
                sendSerial(input)

    if string is not "":
        data = string.strip()
        if mode is True:
            if modetype == 'CLI':
                if data == 'rx':
                    modetype = "RX"
                    sendSerial(b'\33[2K\r')
                    sendSerial(modetype.encode('utf8'))
                    sendSerial(b'>')
                    data = ""
                elif data == 'sw':
                    modetype = "SWITCH"
                    sendSerial(b'\33[2K\r')
                    sendSerial(modetype.encode('utf8'))
                    sendSerial(b'>')
                    data = ""
                elif data == "q":
                    mode = False
                    modetype = "CLI"
                    sendSerial(b'\33[2K\r')
                    data = ""
                else:
                    sendSerial(b'\33[2K\r')
                    msg = yellow("sw -> SWITCH MODE")
                    sendSerial(msg.encode('ascii'))
                    sendSerial(b'\r\n')
                    msg = yellow("rx -> LISTEN 4 ALL LORA PACKETS")
                    sendSerial(msg.encode('ascii'))
                    sendSerial(b'\r\n')
                    msg = yellow("q -> QUIT TO NORMAL MODE")
                    sendSerial(msg.encode('ascii'))
                    sendSerial(b'\r\n')
                    sendSerial(modetype.encode('utf8'))
                    sendSerial(b'>')
                    data = ""
            elif modetype == 'RX':
                if data == "q":
                    modetype = "CLI"
                    sendSerial(b'\33[2K\r')
                    sendSerial(modetype.encode('utf8'))
                    sendSerial(b'>')
                    data = ""
                elif data[:2] == 'rx':
                    listentime = int(data[2:])
                    sendSerial(b'\33[2K\r')
                    msg = yellow("Waiting for LoRa packets for " + str(listentime) + " seconds ...")
                    sendSerial(msg.encode('ascii'))
                    sendSerial(b'\r\n')
                    lInLED.value = False
                    packet = "ok"
                    while packet is not None:
                        packet = rfm9x.receive(with_header=True,timeout=listentime)
                        if packet is not None:
                            print("RAW Packet:")
                            print(packet)
                            sendSerial(packet[3:])
                            sendSerial(b'\r\n')
                    lInLED.value = True
                    msg = yellow("nothing ;(")
                    sendSerial(msg.encode('ascii'))
                    sendSerial(b'\r\n')
                    modetype = "RX"
                    sendSerial(modetype.encode('utf8'))
                    sendSerial(b'>')
                else:
                    sendSerial(b'\33[2K\r')
                    msg = yellow("rx?? -> Listen for lora packets for ?? seconds")
                    sendSerial(msg.encode('ascii'))
                    sendSerial(b'\r\n')
                    msg = yellow("q -> QUIT TO NORMAL MODE")
                    sendSerial(msg.encode('ascii'))
                    sendSerial(b'\r\n')
                    sendSerial(modetype.encode('utf8'))
                    sendSerial(b'>')
                    data = ""
            elif modetype == 'SWITCH':
                if data is not "" and data is not "?":
                    if data == "q":
                        modetype = "CLI"
                        sendSerial(b'\33[2K\r')
                        sendSerial(modetype.encode('utf8'))
                        sendSerial(b'>')
                        data = ""
                    else:
                        msg = yellow("TX>" + data)
                        sendSerial(b'\r')
                        sendSerial(msg.encode('ascii'))
                        sendSerial(b'\r\n')
                        sendSerial(modetype.encode('utf8'))
                        sendSerial(b'>')
                        lOutLED.value = False
                        rfm9x.send(
                            bytes("{}".format("<"), "UTF-8") + binascii.unhexlify("AA") + binascii.unhexlify("01") +
                            bytes("{}".format(data), "UTF-8")
                        )
                        lOutLED.value = True
                else:
                    sendSerial(b'\33[2K\r')
                    msg = yellow("example: sw0/1 -> SWITCHES sw0 port1")
                    sendSerial(msg.encode('ascii'))
                    sendSerial(b'\r\n')
                    msg = yellow("q -> QUIT TO NORMAL MODE")
                    sendSerial(msg.encode('ascii'))
                    sendSerial(b'\r\n')
                    sendSerial(modetype.encode('utf8'))
                    sendSerial(b'>')
        else:
            if data[:1] == "#":
                data = data[1:]
                if data[:3] == "sw#":
                    data = data[3:]
                    lOutLED.value = False
                    msg = yellow("TX>" + data)
                    print(msg)
                    rfm9x.send(
                        bytes("{}".format("<"), "UTF-8") + binascii.unhexlify("AA") + binascii.unhexlify("01") +
                        bytes("{}".format(data), "UTF-8")
                    )
                    lOutLED.value = True
                    sendSerial(b'#sw#done\r\n')
                if data[:3] == "rx#":
                    data = data[3:]
                    packet = "ok"
                    lInLED.value = False
                    while packet is not None:
                        packet = rfm9x.receive(with_header=True,timeout=int(data))
                        if packet is not None:
                            print("RAW Packet:")
                            print(packet)
                            sendSerial(packet[3:])
                            sendSerial(b'\r\n')
                    sendSerial(b'#rx#done\r\n')
                    lInLED.value = True
            else:
                sendSerial(b'Unknown message !! CTRL + ] Gives CLI promt.\r\n')
                sendSerial(b'#sw#<msg> for direct msg to switches.\r\n')
                sendSerial(b'#rx#<time> receive lora msgs.\r\n')

        lastdata = data
        string = ""