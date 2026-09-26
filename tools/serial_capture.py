# 临时串口抓取脚本：复位 ESP32 后立即读取串口日志 N 秒
# 用法: python serial_capture.py <port> <seconds>
import subprocess
import sys
import time

import serial

ESPTOOL = r"D:\dev\arduino_pack\data\packages\esp32\tools\esptool_py\5.3.0\esptool.exe"

port = sys.argv[1] if len(sys.argv) > 1 else "COM19"
duration = int(sys.argv[2]) if len(sys.argv) > 2 else 90

print(f"[capture] resetting {port} via esptool ...")
subprocess.run(
    [ESPTOOL, "--port", port, "--chip", "esp32c3", "--baud", "115200", "--no-stub", "run"],
    capture_output=True,
    text=True,
)

ser = serial.Serial(port, 115200, timeout=1)
print(f"[capture] attached, reading {duration}s ...")
start = time.time()
buf = b""
while time.time() - start < duration:
    data = ser.read(4096)
    if data:
        buf += data
        sys.stdout.write(data.decode("utf-8", "replace"))
        sys.stdout.flush()
ser.close()
print(f"\n[capture] done, {len(buf)} bytes")
