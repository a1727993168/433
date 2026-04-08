#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "pico/cyw43_arch.h"

#define I2C_PORT i2c0
#define SDA_PIN 4
#define SCL_PIN 5
#define MCP23008_ADDR 0x20

// MCP23008 registers
#define IODIR 0x00
#define GPIO  0x09
#define OLAT  0x0A
#define GPPU  0x06

void write_reg(uint8_t reg, uint8_t val) {
    uint8_t buf[2] = {reg, val};
    i2c_write_blocking(I2C_PORT, MCP23008_ADDR, buf, 2, false);
}

uint8_t read_reg(uint8_t reg) {
    uint8_t val;
    i2c_write_blocking(I2C_PORT, MCP23008_ADDR, &reg, 1, true);
    i2c_read_blocking(I2C_PORT, MCP23008_ADDR, &val, 1, false);
    return val;
}

int main() {
    stdio_init_all();

    // 初始化 Pico 2 W 板载 LED 控制
    if (cyw43_arch_init()) {
        return 1;
    }

    // 初始化 I2C
    i2c_init(I2C_PORT, 100 * 1000);
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_PIN);
    gpio_pull_up(SCL_PIN);

    sleep_ms(100);

    // GP0=input, GP7=output
    write_reg(IODIR, 0b01111111);

    // GP0 internal pull-up
    write_reg(GPPU, 0b00000001);

    uint8_t led_state = 0;
    bool heartbeat = false;

    while (1) {
        // Pico 2 W 板载 LED heartbeat
        heartbeat = !heartbeat;
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, heartbeat);

        // 读按钮
        uint8_t gpio_val = read_reg(GPIO);

        if ((gpio_val & 0x01) == 0) {
            // button pressed
            led_state = 0b10000000;   // GP7 = 1
        } else {
            led_state = 0x00;
        }

        // 控制 MCP23008 上的外部 LED
        write_reg(OLAT, led_state);

        sleep_ms(200);
    }
}