#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "ssd1306.h"

#define I2C_PORT i2c0
#define SDA_PIN 4
#define SCL_PIN 5

#define MPU_ADDR 0x68

#define WHO_AM_I      0x75
#define PWR_MGMT_1    0x6B
#define ACCEL_CONFIG  0x1C
#define GYRO_CONFIG   0x1B
#define ACCEL_XOUT_H  0x3B

void mpu_write_reg(uint8_t reg, uint8_t value) {
    uint8_t buf[2] = {reg, value};
    i2c_write_blocking(I2C_PORT, MPU_ADDR, buf, 2, false);
}

uint8_t mpu_read_reg(uint8_t reg) {
    uint8_t value;
    i2c_write_blocking(I2C_PORT, MPU_ADDR, &reg, 1, true);
    i2c_read_blocking(I2C_PORT, MPU_ADDR, &value, 1, false);
    return value;
}

void mpu_read_burst(uint8_t reg, uint8_t *buf, uint8_t len) {
    i2c_write_blocking(I2C_PORT, MPU_ADDR, &reg, 1, true);
    i2c_read_blocking(I2C_PORT, MPU_ADDR, buf, len, false);
}

int16_t combine_bytes(uint8_t high, uint8_t low) {
    return (int16_t)((high << 8) | low);
}

void mpu_init() {
    mpu_write_reg(PWR_MGMT_1, 0x00);   // wake up
    mpu_write_reg(ACCEL_CONFIG, 0x00); // +/-2g
    mpu_write_reg(GYRO_CONFIG, 0x18);  // +/-2000 dps
}

void drawLine(int x0, int y0, int x1, int y1, unsigned char color) {
    int dx = abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (1) {
        ssd1306_drawPixel((unsigned char)x0, (unsigned char)y0, color);

        if (x0 == x1 && y0 == y1) {
            break;
        }

        int e2 = 2 * err;

        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }

        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

int main() {
    stdio_init_all();
    sleep_ms(2000);

    // shared I2C for OLED + MPU6050
    i2c_init(I2C_PORT, 400000);
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_PIN);
    gpio_pull_up(SCL_PIN);
    sleep_ms(100);

    ssd1306_setup();
    ssd1306_clear();
    ssd1306_update();

    uint8_t who = mpu_read_reg(WHO_AM_I);
    printf("WHO_AM_I = 0x%02X\n", who);

    if (!(who == 0x68 || who == 0x98)) {
        while (1) {
            sleep_ms(100);
        }
    }

    mpu_init();

    uint8_t data[14];

    while (1) {
        mpu_read_burst(ACCEL_XOUT_H, data, 14);

        int16_t ax_raw = combine_bytes(data[0], data[1]);
        int16_t ay_raw = combine_bytes(data[2], data[3]);
        int16_t az_raw = combine_bytes(data[4], data[5]);

        float ax = ax_raw * 0.000061f;
        float ay = ay_raw * 0.000061f;
        float az = az_raw * 0.000061f;

        printf("A[g]: %6.3f %6.3f %6.3f\n", ax, ay, az);

        int cx = 64;
        int cy = 16;

        // 调这个长度，12~20都行
        int line_len_scale = 40;

        // 按屏幕坐标映射：右正，下负要反一下
        int x1 = cx + (int)(ax * line_len_scale);
        int y1 = cy - (int)(ay * line_len_scale);

        if (x1 < 0) x1 = 0;
        if (x1 > 127) x1 = 127;
        if (y1 < 0) y1 = 0;
        if (y1 > 31) y1 = 31;

        ssd1306_clear();

        // 中心点
        ssd1306_drawPixel(cx, cy, 1);
        ssd1306_drawPixel(cx - 1, cy, 1);
        ssd1306_drawPixel(cx + 1, cy, 1);
        ssd1306_drawPixel(cx, cy - 1, 1);
        ssd1306_drawPixel(cx, cy + 1, 1);

        // 方向线
        drawLine(cx, cy, x1, y1, 1);

        ssd1306_update();
        sleep_ms(50);
    }
}