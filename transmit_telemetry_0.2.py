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
import serial
import csv

# Button A
#btnA = DigitalInOut(board.D5)
#btnA.direction = Direction.INPUT
#btnA.pull = Pull.UP

# Button B
#btnB = DigitalInOut(board.D6)
#btnB.direction = Direction.INPUT
#btnB.pull = Pull.UP

# Button C
#btnC = DigitalInOut(board.D12)
#btnC.direction = Direction.INPUT
#btnC.pull = Pull.UP

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

# LightAPRS

serial_port = '/dev/ttyUSB0'
if not os.path.exists(serial_port):
    print("USB port was not ready sleeping for 60 seconds and trying again")
    time.sleep(60)

serial_speed= 57600

#time sleep 60 for convience, no specific reason for 60 seconds, could be more or less
time.sleep(60)
serial_connection = serial.Serial(serial_port, serial_speed)
lightAPRSData = {
    'gps': '',
    'txc': '',
    'temp': '',
    'pressure': '',
    'power': '',
    'sat_valid': ''
}

system_name = socket.gethostname()
ip = '127.0.0.1'

main_loop_frequency = 5

##open csv to add labels/titles to the rows
with open('/home/cubesat/data_to_be_transmitted.csv', 'w+', newline = '') as open_csv_file_to_format:
    csv_file = csv.writer(open_csv_file_to_format) 
    csv_file.writerow(["Time", "System_name", "Capacity", "Electricity", "GPS", "Tx Count", "Temperature", "Pressure", "Power", "Sat_valid"])
    

# main loop
while True:
    display.fill(0)

    ## get current ip address
    routes = json.loads(os.popen("ip -j -4 route").read())
    for r in routes:
        if r.get("dev") == "wlan0" and r.get("prefsrc"):
            ip = r['prefsrc']
            continue

    # get ups data from dfrobot hat
    capacity=(((vcellH&0x0F)<<8)+vcellL)*1.25 #capacity
    electricity=((socH<<8)+socL)*0.003906 #current electric quantity percentage

    # get lightaprs stuff if possible
    #000/002/A=000407 001TxC  22.30C 1009.50hPa  4.54V 07S http://www.lightaprs.com
    if serial_connection.in_waiting > 0:
        tempData = serial_connection.readline().decode('utf-8').rstrip().split()
        lightAPRSData['gps'] = tempData[0]
        lightAPRSData['txc'] = tempData[1]
        lightAPRSData['temp'] = tempData[2]
        lightAPRSData['pressure'] = tempData[3]
        lightAPRSData['power'] = tempData[4]
        lightAPRSData['sat_valid'] = tempData[5]
        #lightAPRSData = serial_connection.readline().decode('utf-8').rstrip().split()
        print('lightAPRS data: ' + str(lightAPRSData))

    if (time.time() - last_transmit) > (transmit_frequency / 5):
        with open('/home/cubesat/data_to_be_transmitted.csv', 'a+', newline = '') as open_csv_file:
           csv_file = csv.writer(open_csv_file) 
           csv_file.writerow([str(datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")),
         system_name,
         str(capacity),
         str(electricity),
         str(lightAPRSData["gps"]),
         str(lightAPRSData["txc"]), 
         str(lightAPRSData["temp"]),
         str(lightAPRSData["pressure"]),
         str(lightAPRSData["power"]),
         str(lightAPRSData["sat_valid"])])

    # transmit telemetry data 
    if (time.time() - last_transmit) > transmit_frequency:

        tx_string = ('tx:' + str(datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")) +
        ',' + system_name + 
        ',' + str(capacity) + 
        ',' + str(electricity) + 
        ',' + str(lightAPRSData["gps"]) + 
        ',' + str(lightAPRSData["txc"]) +  # means tx count
        ',' + str(lightAPRSData["temp"]) + 
        ',' + str(lightAPRSData["pressure"]) + 
        ',' + str(lightAPRSData["power"]) + 
        ',' + str(lightAPRSData["sat_valid"]))

        print('tx_string: ' + tx_string)
        rfm9x.send(bytes(tx_string,"utf-8"))
        last_transmit = time.time()
        display.text('tx beacon!', 0, 20, 1)

    # display status on oled
    display.text('tx en on: ' + str(tx_freq) + 'MHz', 0, 0, 1)
    display.text(str(ip), 0, 10, 1)
    #display.text('Toggle TX!', 0, 15, 1)


    display.show()

    ##sleep before next loop for 5 seconds???? why?????
    ##time.sleep(main_loop_frequency)

    ##Time sleep 2 seconds instead b/c why was it 5????
    time.sleep(2)
