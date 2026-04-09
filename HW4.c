#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/adc.h"
#include "pico/cyw43_arch.h"
#include "ssd1306.h"
#include "font.h"

#define I2C_PORT i2c0
#define SDA_PIN 4
#define SCL_PIN 5

void drawChar(int x, int y, char c) {
    int index = c - 0x20;   // font starts at ASCII space

    for (int col = 0; col < 5; col++) {
        char line = ASCII[index][col];
        for (int row = 0; row < 8; row++) {
            if ((line >> row) & 0x01) {
                ssd1306_drawPixel(x + col, y + row, 1);
            } else {
                ssd1306_drawPixel(x + col, y + row, 0);
            }
        }
    }
}

void drawMessage(int x, int y, char *message) {
    int i = 0;
    while (message[i] != '\0') {
        drawChar(x + i * 6, y, message[i]);   // 5 pixels wide + 1 space
        i++;
    }
}

int main() {
    stdio_init_all();

    if (cyw43_arch_init()) {
        return 1;
    }

    i2c_init(I2C_PORT, 400000);
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_PIN);
    gpio_pull_up(SCL_PIN);

    ssd1306_setup();

    adc_init();
    adc_gpio_init(26);   // GP26 = ADC0
    adc_select_input(0); // ADC0

    char msg[40];
    char fpsmsg[40];

    unsigned int t0 = to_us_since_boot(get_absolute_time());
    unsigned int t1;

    while (1) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);

        uint16_t adc_value = adc_read();
        float voltage = adc_value * 3.3f / 4095.0f;

        t1 = to_us_since_boot(get_absolute_time());
        float fps = 1000000.0f / (t1 - t0);
        t0 = t1;

        sprintf(msg, "ADC0: %.2f V", voltage);
        sprintf(fpsmsg, "FPS: %.2f", fps);

        ssd1306_clear();
        drawMessage(0, 0, msg);
        drawMessage(0, 24, fpsmsg);   // bottom line
        ssd1306_update();

        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
    }
}