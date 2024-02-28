import usb_cdc
import board
import storage
import supervisor
from digitalio import DigitalInOut, Direction, Pull

supervisor.set_usb_identification("RF.Guru", "LoRaStick")

usb_cdc.enable(console=False, data=True)

new_name = "LoRa868"
storage.remount("/", readonly=False)
m = storage.getmount("/")
m.label = new_name
storage.remount("/", readonly=False)
