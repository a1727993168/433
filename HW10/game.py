import pgzrun
import serial
import threading

# ── 修改这里：你的 Pico 串口号（Windows 一般是 COM3、COM4 之类）──
PORT = "COM5"
BAUD = 115200

WIDTH  = 800
HEIGHT = 600
TITLE  = "MPU6050 Ball"

# 共享传感器数据（子线程写，主线程读）
_lock = threading.Lock()
_ax = _ay = _az = 0
_gx = _gy = _gz = 0
connected = False

def _serial_thread():
    global _ax, _ay, _az, _gx, _gy, _gz, connected
    try:
        ser = serial.Serial(PORT, BAUD, timeout=1)
        connected = True
        while True:
            line = ser.readline().decode("utf-8", errors="ignore").strip()
            if not line:
                continue
            parts = line.split(",")
            if len(parts) == 6:
                with _lock:
                    _ax, _ay, _az = int(parts[0]), int(parts[1]), int(parts[2])
                    _gx, _gy, _gz = int(parts[3]), int(parts[4]), int(parts[5])
    except Exception as e:
        print(f"Serial error: {e}")
        connected = False

threading.Thread(target=_serial_thread, daemon=True).start()

# 小球状态
ball_x  = float(WIDTH  // 2)
ball_y  = float(HEIGHT // 2)
ball_vx = 0.0
ball_vy = 0.0
RADIUS  = 22
GRAVITY_SCALE = 350   # 加速度计 → 像素/s²
DAMPING       = 0.92
BOUNCE        = 0.55

def update(dt):
    global ball_x, ball_y, ball_vx, ball_vy

    with _lock:
        ax, ay = _ax, _ay

    # MPU6050 默认量程 ±2g → 16384 LSB/g
    tilt_x =  ax / 16384.0
    tilt_y =  ay / 16384.0

    ball_vx += tilt_x * GRAVITY_SCALE * dt
    ball_vy += tilt_y * GRAVITY_SCALE * dt

    ball_vx *= DAMPING
    ball_vy *= DAMPING

    ball_x += ball_vx * dt
    ball_y += ball_vy * dt

    # 边界反弹
    if ball_x < RADIUS:
        ball_x  = RADIUS
        ball_vx =  abs(ball_vx) * BOUNCE
    elif ball_x > WIDTH - RADIUS:
        ball_x  = WIDTH - RADIUS
        ball_vx = -abs(ball_vx) * BOUNCE

    if ball_y < RADIUS:
        ball_y  = RADIUS
        ball_vy =  abs(ball_vy) * BOUNCE
    elif ball_y > HEIGHT - RADIUS:
        ball_y  = HEIGHT - RADIUS
        ball_vy = -abs(ball_vy) * BOUNCE

def draw():
    screen.fill((20, 24, 40))

    # 边框
    screen.draw.rect(
        Rect(0, 0, WIDTH, HEIGHT),
        (80, 100, 160)
    )

    # 小球（带高光感）
    bx, by = int(ball_x), int(ball_y)
    screen.draw.filled_circle((bx, by), RADIUS, (255, 200, 50))
    screen.draw.filled_circle((bx - 6, by - 6), RADIUS // 3, (255, 240, 160))

    # 传感器数值
    with _lock:
        ax, ay, az = _ax, _ay, _az
        gx, gy, gz = _gx, _gy, _gz

    screen.draw.text(f"Accel  X:{ax:6d}  Y:{ay:6d}  Z:{az:6d}", (10, 10),  color=(160, 200, 255), fontsize=22)
    screen.draw.text(f"Gyro   X:{gx:6d}  Y:{gy:6d}  Z:{gz:6d}", (10, 36),  color=(160, 255, 200), fontsize=22)

    status_color = (80, 220, 80) if connected else (220, 80, 80)
    status_text  = f"Serial: {PORT}  OK" if connected else f"Serial: {PORT}  NOT CONNECTED"
    screen.draw.text(status_text, (10, HEIGHT - 28), color=status_color, fontsize=20)

    screen.draw.text("Tilt the Pico to roll the ball!", (220, HEIGHT - 28),
                     color=(140, 140, 180), fontsize=20)

pgzrun.go()
