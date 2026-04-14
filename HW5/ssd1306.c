#include <string.h>
#include "ssd1306.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"

#define SSD1306_ADDRESS 0x3C

unsigned char buffer[513];

void ssd1306_command(unsigned char c) {
    uint8_t buf[2];
    buf[0] = 0x00;
    buf[1] = c;
    i2c_write_blocking(i2c0, SSD1306_ADDRESS, buf, 2, false);
}

void ssd1306_setup() {
    sleep_ms(100);

    ssd1306_command(0xAE);
    ssd1306_command(0xD5);
    ssd1306_command(0x80);

    ssd1306_command(0xA8);
    ssd1306_command(0x1F);

    ssd1306_command(0xD3);
    ssd1306_command(0x00);

    ssd1306_command(0x40);

    ssd1306_command(0x8D);
    ssd1306_command(0x14);

    ssd1306_command(0x20);
    ssd1306_command(0x00);

    ssd1306_command(0xA1);
    ssd1306_command(0xC8);

    ssd1306_command(0xDA);
    ssd1306_command(0x02);

    ssd1306_command(0x81);
    ssd1306_command(0x8F);

    ssd1306_command(0xD9);
    ssd1306_command(0xF1);

    ssd1306_command(0xDB);
    ssd1306_command(0x40);

    ssd1306_command(0xA4);
    ssd1306_command(0xA6);

    ssd1306_command(0xAF);

    ssd1306_clear();
    ssd1306_update();
}

void ssd1306_update() {
    ssd1306_command(0x22);
    ssd1306_command(0);
    ssd1306_command(3);

    ssd1306_command(0x21);
    ssd1306_command(0);
    ssd1306_command(127);

    i2c_write_blocking(i2c0, SSD1306_ADDRESS, buffer, 513, false);
}

void ssd1306_drawPixel(unsigned char x, unsigned char y, unsigned char color) {
    if (x >= 128 || y >= 32) return;

    if (color) {
        buffer[1 + x + (y / 8) * 128] |= (1 << (y % 8));
    } else {
        buffer[1 + x + (y / 8) * 128] &= ~(1 << (y % 8));
    }
}

void ssd1306_clear() {
    memset(buffer, 0, 513);
    buffer[0] = 0x40;
}