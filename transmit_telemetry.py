#!/home/cubesat/env/bin/python3
# Nick Grabbs - MSUCubeSat - Desperado1 - 12/18/2024
# for this first iteration we can use csv to transmit the data
# and dump into excel.  in the future we should adhere to a 
# standard like CCSDS (Consultative Committee for Space Data Systems)

# csv example
# timestamp,cubesat_id,battery_voltage,gps_latitude,gps_longitude,altitude,temperature
# 2024-12-18T12:00:00Z,CubeSat001,3.7,34.0522,-118.2437,500,22.5

# SPDX-FileCopyrightText: 2018 Brent Rubell for Adafruit Industries
#
# SPDX-License-Identifier: MIT

"""
Example for using the RFM9x Radio with Raspberry Pi.

Learn Guide: https://learn.adafruit.com/lora-and-lorawan-for-raspberry-pi
Author: Brent Rubell for Adafruit Industries
"""
import time
from datetime import datetime, timezone
import busio
from digitalio import DigitalInOut, Direction, Pull
import board
import adafruit_ssd1306
import adafruit_rfm9x
import socket
import os
import json
import smbus

# Button A
btnA = DigitalInOut(board.D5)
btnA.direction = Direction.INPUT
btnA.pull = Pull.UP

# Button B
btnB = DigitalInOut(board.D6)
btnB.direction = Direction.INPUT
btnB.pull = Pull.UP

# Button C
btnC = DigitalInOut(board.D12)
btnC.direction = Direction.INPUT
btnC.pull = Pull.UP

# Create the I2C interface.
i2c = busio.I2C(board.SCL, board.SDA)

# 128x32 OLED Display
reset_pin = DigitalInOut(board.D4)
display = adafruit_ssd1306.SSD1306_I2C(128, 32, i2c, reset=reset_pin)
# Clear the display.
display.fill(0)
display.show()
width = display.width
height = display.height

# Configure LoRa Radio
tx_enable = False
transmit_frequency = 10 # 60 seconds?
last_transmit = time.time()
tx_freq = 915.0

# DFROBOT UPS hat config
addr=0x10 #ups i2c address
bus=smbus.SMBus(1) #i2c-1
vcellH=bus.read_byte_data(addr,0x03)
vcellL=bus.read_byte_data(addr,0x04)
socH=bus.read_byte_data(addr,0x05)
socL=bus.read_byte_data(addr,0x06)

# loRa radio confg cont.
CS = DigitalInOut(board.CE1)
RESET = DigitalInOut(board.D25)
spi = busio.SPI(board.SCK, MOSI=board.MOSI, MISO=board.MISO)
rfm9x = adafruit_rfm9x.RFM9x(spi, CS, RESET, tx_freq)
rfm9x.tx_power = 5
prev_packet = None

system_name = socket.gethostname()
ip = '127.0.0.1'

def gatherUPSData():
    capacity=(((vcellH&0x0F)<<8)+vcellL)*1.25 #capacity
    electricity=((socH<<8)+socL)*0.003906 #current electric quantity percentage
    return {"capacity": capacity, "electricity": electricity}

while True:
    display.fill(0)
    if tx_enable:

        ## get current ip address
        routes = json.loads(os.popen("ip -j -4 route").read())

        # get ups data from dfrobot hat
        upsdata = gatherUPSData()

        for r in routes:
            if r.get("dev") == "wlan0" and r.get("prefsrc"):
                ip = r['prefsrc']
                continue
        display.text('tx en on: ' + str(tx_freq) + 'MHz', 0, 0, 1)
#        display.text(str(tx_freq) + 'MHz', 0, 10, 1)
        display.text(str(ip), 0, 10, 1)


        if (time.time() - last_transmit) > transmit_frequency:
#            print('Transmitting beacon on ' + str(tx_freq) + 'MHz from host: ' + system_name + ' ' + str(datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")) + '\n')
            tx_string = 'tx:' + str(datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")) + ',' + system_name + ',' + str(upsdata["capacity"]) + "," + str(upsdata["electricity"]) + ',34.0522,-118.2437,500,22.5'
            print(tx_string)
            rfm9x.send(bytes(tx_string,"utf-8"))
            last_transmit = time.time()
            display.text('tx beacon!', 0, 20, 1)
    else:
        display.text('tx: disabled', 0, 0 , 1)

    if not btnA.value:
        display.fill(0)
        tx_enable = not tx_enable
        display.text('Toggle TX!', 0, 15, 1)


    display.show()
    time.sleep(0.1)

