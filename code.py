import time
import board
import pwmio
from adafruit_motor import servo

pwm = pwmio.PWMOut(board.GP15, frequency=50)

my_servo = servo.Servo(pwm, min_pulse=500, max_pulse=2500)

while True:
    for angle in range(0, 181, 5):
        my_servo.angle = angle
        time.sleep(0.08)

    time.sleep(0.5)

    for angle in range(180, -1, -5):
        my_servo.angle = angle
        time.sleep(0.08)

    time.sleep(0.5)
