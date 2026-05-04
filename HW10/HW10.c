#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

#define MPU6050_ADDR 0x68
#define I2C_PORT     i2c0
#define SDA_PIN      4
#define SCL_PIN      5

static void mpu6050_init() {
    uint8_t buf[2] = {0x6B, 0x00};  // wake up from sleep
    i2c_write_blocking(I2C_PORT, MPU6050_ADDR, buf, 2, false);
}

static void mpu6050_read(int16_t accel[3], int16_t gyro[3]) {
    uint8_t reg = 0x3B;
    uint8_t data[14];
    i2c_write_blocking(I2C_PORT, MPU6050_ADDR, &reg, 1, true);
    i2c_read_blocking(I2C_PORT, MPU6050_ADDR, data, 14, false);

    accel[0] = (int16_t)((data[0]  << 8) | data[1]);
    accel[1] = (int16_t)((data[2]  << 8) | data[3]);
    accel[2] = (int16_t)((data[4]  << 8) | data[5]);
    // data[6..7] = temperature, skip
    gyro[0]  = (int16_t)((data[8]  << 8) | data[9]);
    gyro[1]  = (int16_t)((data[10] << 8) | data[11]);
    gyro[2]  = (int16_t)((data[12] << 8) | data[13]);
}

int main() {
    stdio_init_all();

    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_PIN);
    gpio_pull_up(SCL_PIN);

    sleep_ms(100);
    mpu6050_init();

    int16_t accel[3], gyro[3];

    while (true) {
        mpu6050_read(accel, gyro);
        // Protocol: ax,ay,az,gx,gy,gz\n
        printf("%d,%d,%d,%d,%d,%d\n",
               accel[0], accel[1], accel[2],
               gyro[0],  gyro[1],  gyro[2]);
        sleep_ms(50);  // 20 Hz
    }
}
