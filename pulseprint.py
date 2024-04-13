import serial
import matplotlib.pyplot as plt
import numpy as np

# Initialize the plot
plt.ion()
fig, ax = plt.subplots()

# Serial port setup
ser = serial.Serial('/dev/tty.usbmodem14102', 9600)  # replace 'COM3' with your actual port
ser.close()
ser.open()

x = [0]  # Start with time zero
y = [0]  # Start with the signal low

try:
    # Read and plot each pulse width as a high or low in the wave
    while True:
        data = ser.readline().decode().strip()
        pulse_width = int(data)
        
        # Print received pulse width
        print(f"Received pulse width: {pulse_width} microseconds")
        
        # Determine if this pulse width is a 'high' or 'low' in the square wave
        if pulse_width > 250:
            # It's a 'high' signal
            wave_value = 1
        else:
            # It's a 'low' signal
            wave_value = 0
        
        x.append(x[-1] + pulse_width)
        y.append(wave_value)
        x.append(x[-1])
        y.append(1 - wave_value)
        
        # Update the plot
        ax.clear()
        ax.step(x, y, where='post')
        plt.ylim(-0.5, 1.5) 
        plt.draw()
        plt.pause(0.001)
        
finally:
    ser.close()
