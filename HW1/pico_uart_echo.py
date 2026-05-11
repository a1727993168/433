import sys
import uselect
from machine import UART, Pin

# UART0: GP0=TX -> STM32 PA1(RX), GP1=RX <- STM32 PA0(TX)
uart = UART(0, baudrate=115200, tx=Pin(0), rx=Pin(1))

poll = uselect.poll()
poll.register(sys.stdin, uselect.POLLIN)
poll.register(uart, uselect.POLLIN)

while True:
    try:
        events = poll.poll(10)
        for obj, event in events:
            if obj is sys.stdin:
                data = sys.stdin.buffer.read(1)
                if data and data != b'\x03':  # 忽略 Ctrl+C，不转发给 STM32
                    uart.write(data)
            elif obj is uart:
                data = uart.read(1)
                if data:
                    sys.stdout.buffer.write(data)
    except KeyboardInterrupt:
        pass  # 忽略 Ctrl+C，脚本继续运行
