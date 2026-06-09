"""
HW18 - Haptic Paddle 实时图形 (电脑端)
============================================================================
作用: 通过 USB 串口读 Pico 发来的数据, 实时画出:
        - 一个表盘 + 指针, 显示手柄当前角度
        - 当前触觉效果的"力-位移"示意 (凹槽点 / 墙 / 弹簧)
        - 一个力条, 显示电机正在施加的力(期望电流)和实际电流
        - 读到的力传感器(HX711)数值
      按键盘 1 / 2 / 3 切换触觉效果; 按 Z 把当前位置设为 0°.

Pico 发来的每行是 CSV:
        角度(度), 期望电流mA, 实际电流mA, 力原始值, 效果编号

依赖:  pip install pyserial pygame
运行:  python haptic_graphics.py            (自动找端口)
       python haptic_graphics.py COM5       (指定端口)
============================================================================
"""
import sys
import math
import serial
import serial.tools.list_ports
import pygame

BAUD = 115200          # USB CDC 其实和波特率无关, 随便填
W, H = 900, 560

EFFECT_NAMES = {1: "1 - Detents (凹槽)", 2: "2 - Virtual Wall (虚拟墙)", 3: "3 - Spring (回中弹簧)"}

# 这些要和 Pico main.c 里的参数一致, 才能把曲线画对
DET_N      = 6.0
WALL_ANGLE = 0.6          # rad
SPRING_K   = 250.0
I_MAX      = 600.0


def find_port(arg):
    if arg:
        return arg
    ports = list(serial.tools.list_ports.comports())
    # 优先找 Pico / 树莓派的串口
    for p in ports:
        desc = (p.description + " " + (p.manufacturer or "")).lower()
        if "pico" in desc or "raspberry" in desc or "usb serial" in desc:
            return p.device
    if ports:
        return ports[0].device
    return None


def draw_dial(screen, font, cx, cy, R, angle_deg, effect):
    """画表盘 + 指针, 并叠加当前效果的特征(槽/墙/弹簧中心)。"""
    pygame.draw.circle(screen, (60, 60, 70), (cx, cy), R, 3)
    pygame.draw.circle(screen, (40, 40, 48), (cx, cy), 4)

    a = math.radians(angle_deg)

    if effect == 1:  # 凹槽: 在吸附点(force=0且稳定)处画绿色刻度
        # 期望电流 = -A*sin(N*phi), 稳定吸附点在 sin=0 且斜率为正 -> N*phi = 2πk
        for k in range(int(DET_N)):
            slot = 2 * math.pi * k / DET_N
            x1 = cx + (R - 18) * math.sin(slot)
            y1 = cy - (R - 18) * math.cos(slot)
            x2 = cx + R * math.sin(slot)
            y2 = cy - R * math.cos(slot)
            pygame.draw.line(screen, (90, 200, 120), (x1, y1), (x2, y2), 3)
    elif effect == 2:  # 虚拟墙: 在 ±WALL_ANGLE 画红色墙
        for sgn in (+1, -1):
            wa = sgn * WALL_ANGLE
            x = cx + R * math.sin(wa)
            y = cy - R * math.cos(wa)
            pygame.draw.line(screen, (220, 80, 80), (cx, cy), (x, y), 4)
        # 越界区涂红
    elif effect == 3:  # 弹簧: 0° 处画蓝色中心线
        pygame.draw.line(screen, (90, 150, 230), (cx, cy), (cx, cy - R), 3)

    # 指针
    px = cx + (R - 10) * math.sin(a)
    py = cy - (R - 10) * math.cos(a)
    pygame.draw.line(screen, (240, 220, 90), (cx, cy), (px, py), 5)
    pygame.draw.circle(screen, (240, 220, 90), (int(px), int(py)), 7)

    label = font.render(f"{angle_deg:6.1f} deg", True, (230, 230, 230))
    screen.blit(label, (cx - 45, cy + R + 10))


def draw_bar(screen, font, x, y, w, h, value, vmax, color, title):
    """画一个中心对称的力条 (value 可正可负)。"""
    pygame.draw.rect(screen, (50, 50, 58), (x, y, w, h), border_radius=4)
    mid = y + h // 2
    pygame.draw.line(screen, (110, 110, 120), (x, mid), (x + w, mid), 1)
    frac = max(-1.0, min(1.0, value / vmax))
    bar_h = int(abs(frac) * (h // 2))
    if frac >= 0:
        pygame.draw.rect(screen, color, (x + 2, mid - bar_h, w - 4, bar_h))
    else:
        pygame.draw.rect(screen, color, (x + 2, mid, w - 4, bar_h))
    t = font.render(f"{title}: {value:7.1f}", True, (220, 220, 220))
    screen.blit(t, (x, y - 22))


def main():
    port = find_port(sys.argv[1] if len(sys.argv) > 1 else None)
    if not port:
        print("找不到串口! 请插上 Pico, 或手动指定: python haptic_graphics.py COM5")
        sys.exit(1)
    print(f"连接串口: {port}")
    try:
        ser = serial.Serial(port, BAUD, timeout=0.05)
    except Exception as e:
        print(f"打开串口失败: {e}")
        sys.exit(1)

    pygame.init()
    screen = pygame.display.set_mode((W, H))
    pygame.display.set_caption("HW18 Haptic Paddle - Live")
    clock = pygame.time.Clock()
    font = pygame.font.SysFont("consolas", 20)
    big = pygame.font.SysFont("consolas", 26, bold=True)

    angle = 0.0
    i_des = i_meas = 0.0
    force = 0
    effect = 1
    buf = ""

    running = True
    while running:
        # ---- 键盘事件: 切换效果 / 调零 ----
        for ev in pygame.event.get():
            if ev.type == pygame.QUIT:
                running = False
            elif ev.type == pygame.KEYDOWN:
                if ev.key in (pygame.K_1, pygame.K_KP1):
                    ser.write(b'1')
                elif ev.key in (pygame.K_2, pygame.K_KP2):
                    ser.write(b'2')
                elif ev.key in (pygame.K_3, pygame.K_KP3):
                    ser.write(b'3')
                elif ev.key == pygame.K_z:
                    ser.write(b'z')
                elif ev.key == pygame.K_ESCAPE:
                    running = False

        # ---- 读串口 (一次读完缓冲, 取最后一条完整行) ----
        try:
            data = ser.read(4096).decode(errors="ignore")
        except Exception:
            data = ""
        buf += data
        if "\n" in buf:
            lines = buf.split("\n")
            buf = lines[-1]
            for line in reversed(lines[:-1]):
                parts = line.strip().split(",")
                if len(parts) == 5:
                    try:
                        angle  = float(parts[0])
                        i_des  = float(parts[1])
                        i_meas = float(parts[2])
                        force  = int(parts[3])
                        effect = int(parts[4])
                        break
                    except ValueError:
                        continue

        # ---- 画面 ----
        screen.fill((28, 28, 34))
        title = big.render("Haptic Paddle  |  " + EFFECT_NAMES.get(effect, "?"),
                           True, (240, 240, 240))
        screen.blit(title, (24, 18))
        hint = font.render("按键: 1=凹槽  2=墙  3=弹簧   Z=当前位置归零   ESC=退出",
                           True, (160, 160, 170))
        screen.blit(hint, (24, 52))

        draw_dial(screen, font, 250, 300, 170, angle, effect)

        draw_bar(screen, font, 560, 130, 60, 320, i_des, I_MAX, (240, 180, 70),
                 "期望电流mA")
        draw_bar(screen, font, 700, 130, 60, 320, i_meas, I_MAX, (90, 200, 230),
                 "实际电流mA")

        f = font.render(f"力传感器(HX711): {force}", True, (200, 230, 200))
        screen.blit(f, (560, 480))

        pygame.display.flip()
        clock.tick(60)

    ser.close()
    pygame.quit()


if __name__ == "__main__":
    main()
