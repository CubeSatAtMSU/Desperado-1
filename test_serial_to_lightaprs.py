#!/home/cubesat/env/bin/python3
import serial

def gatherLightAPRSData():
  try:
    ser = serial.Serial('/dev/ttyUSB0', 57600)  # Adjust baud rate if needed
    while True:
      if ser.in_waiting > 0:
        data = ser.readline().decode('utf-8').rstrip()
        return data
        #print("data" + str(data))
  except serial.SerialException as e:
    print(f"Error: {e}")
  finally:
    if 'ser' in locals() and ser.is_open:
      ser.close()

print("data: " + str(gatherLightAPRSData()))
