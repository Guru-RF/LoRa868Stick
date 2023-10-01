import board
import time
import busio
import digitalio
import usb_cdc
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

# our version
print("RF.Guru\nLoRa868Stick 0.1")

def recvSerial():
    if serial is not None:
        if serial.connected:
            if serial.in_waiting > 0:
                sInLED.value = False
                letter = serial.read().decode('utf-8')
                time.sleep(0.01)
                sInLED.value = True
                return letter
 
def sendSerial(data):
    data = bytes(data, 'utf-8')
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

sendSerial("RF.Guru\r\nLoRa868Stick 0.1\r\n")

string = ""
buf = ""
while True:
    input = recvSerial()
    if input is not None:
        buf = buf + input
        sendSerial(input)
        if input is "\r":
            string = buf
            sendSerial(string)