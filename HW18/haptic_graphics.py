"""
HW18 - Haptic Paddle live graphics (PC side)
============================================================================
Reads data from the Pico over USB serial and draws, in real time:
    - a dial + needle showing the paddle angle
    - the current effect's force-displacement features (detents / wall / spring)
    - two bars: desired current and measured current (shows how well PI tracks)
    - the raw force-sensor (HX711) value
Press 1 / 2 / 3 to switch effect; press Z to set the current position as 0 deg.

Each line from the Pico is CSV:
    angle(deg), desired_mA, measured_mA, force_raw, effect_id

Deps:  pip install pyserial pygame
Run:   python haptic_graphics.py            (auto-detect port)
       python haptic_graphics.py COM5       (specify port)
============================================================================
"""
import sys
import math
import serial
import serial.tools.list_ports
import pygame

BAUD = 115200          # USB CDC ignores baud, any value is fine
W, H = 900, 560

EFFECT_NAMES = {1: "1 - Detents", 2: "2 - Virtual Wall", 3: "3 - Spring"}

# Must match the parameters in Pico main.c so the overlays line up
DET_N      = 6.0
WALL_ANGLE = 0.6          # rad
SPRING_K   = 250.0
I_MAX      = 600.0


def find_port(arg):
    if arg:
        return arg
    ports = list(serial.tools.list_ports.comports())
    # Prefer a Pico / Raspberry Pi serial port
    for p in ports:
        desc = (p.description + " " + (p.manufacturer or "")).lower()
        if "pico" in desc or "raspberry" in desc or "usb serial" in desc:
            return p.device
    if ports:
        return ports[0].device
    return None


def draw_dial(screen, font, cx, cy, R, angle_deg, effect):
    """Draw the dial + needle, with the current effect's features overlaid."""
    pygame.draw.circle(screen, (60, 60, 70), (cx, cy), R, 3)
    pygame.draw.circle(screen, (40, 40, 48), (cx, cy), 4)

    a = math.radians(angle_deg)

    if effect == 1:  # detents: green ticks at the attractor points
        for k in range(int(DET_N)):
            slot = 2 * math.pi * k / DET_N
            x1 = cx + (R - 18) * math.sin(slot)
            y1 = cy - (R - 18) * math.cos(slot)
            x2 = cx + R * math.sin(slot)
            y2 = cy - R * math.cos(slot)
            pygame.draw.line(screen, (90, 200, 120), (x1, y1), (x2, y2), 3)
    elif effect == 2:  # virtual wall: red walls at +-WALL_ANGLE
        for sgn in (+1, -1):
            wa = sgn * WALL_ANGLE
            x = cx + R * math.sin(wa)
            y = cy - R * math.cos(wa)
            pygame.draw.line(screen, (220, 80, 80), (cx, cy), (x, y), 4)
    elif effect == 3:  # spring: blue center line at 0 deg
        pygame.draw.line(screen, (90, 150, 230), (cx, cy), (cx, cy - R), 3)

    # needle
    px = cx + (R - 10) * math.sin(a)
    py = cy - (R - 10) * math.cos(a)
    pygame.draw.line(screen, (240, 220, 90), (cx, cy), (px, py), 5)
    pygame.draw.circle(screen, (240, 220, 90), (int(px), int(py)), 7)

    label = font.render(f"{angle_deg:6.1f} deg", True, (230, 230, 230))
    screen.blit(label, (cx - 45, cy + R + 10))


def draw_bar(screen, font, x, y, w, h, value, vmax, color, title):
    """Draw a center-zero bar (value can be positive or negative)."""
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
        print("No serial port found. Plug in the Pico, or specify: python haptic_graphics.py COM5")
        sys.exit(1)
    print(f"Connecting to: {port}")
    try:
        ser = serial.Serial(port, BAUD, timeout=0.05)
    except Exception as e:
        print(f"Failed to open serial port: {e}")
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
        # keyboard: switch effect / zero
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

        # read serial (drain buffer, take the last complete line)
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

        # draw
        screen.fill((28, 28, 34))
        title = big.render("Haptic Paddle  |  " + EFFECT_NAMES.get(effect, "?"),
                           True, (240, 240, 240))
        screen.blit(title, (24, 18))
        hint = font.render("Keys: 1=Detents  2=Wall  3=Spring   Z=zero here   ESC=quit",
                           True, (160, 160, 170))
        screen.blit(hint, (24, 52))

        draw_dial(screen, font, 250, 300, 170, angle, effect)

        draw_bar(screen, font, 560, 130, 60, 320, i_des, I_MAX, (240, 180, 70),
                 "Desired mA")
        draw_bar(screen, font, 700, 130, 60, 320, i_meas, I_MAX, (90, 200, 230),
                 "Measured mA")

        f = font.render(f"Force (HX711): {force}", True, (200, 230, 200))
        screen.blit(f, (560, 480))

        pygame.display.flip()
        clock.tick(60)

    ser.close()
    pygame.quit()


if __name__ == "__main__":
    main()
