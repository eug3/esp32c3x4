import serial, time

port='COM5'
baud=115200
print('Open',port,baud)
ser=serial.Serial(port,baud,timeout=1)
start=time.time()
try:
    while time.time()-start<8:
        if ser.in_waiting:
            print(ser.readline().decode('utf-8',errors='ignore').rstrip())
        time.sleep(0.05)
finally:
    ser.close()
    print('monitor end')
