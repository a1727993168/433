#include <stdio.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

#define SPI_PORT spi0
#define PIN_MISO 16
#define PIN_CS   17
#define PIN_SCK  18
#define PIN_MOSI 19

#define DAC_MAX  1023   // 10-bit
#define SAMPLES  1000   // updates per second (1ms per step)
#define SINE_HZ  2
#define TRI_HZ   1

// MCP4912 16-bit frame:
//  [15]   A/!B  : 0=channel A, 1=channel B
//  [14]   BUF   : 0=unbuffered
//  [13]   !GA   : 1=1x gain
//  [12]   !SHDN : 1=active
//  [11:2] D9:D0 : 10-bit value
//  [1:0]  X     : don't care
static void dac_write(uint8_t channel, uint16_t value) {
    uint16_t word = (channel ? (1u << 15) : 0)
                  | (0u << 14)   // unbuffered
                  | (1u << 13)   // 1x gain
                  | (1u << 12)   // output active
                  | ((value & 0x3FF) << 2);
    uint8_t buf[2] = { (word >> 8) & 0xFF, word & 0xFF };
    gpio_put(PIN_CS, 0);
    spi_write_blocking(SPI_PORT, buf, 2);
    gpio_put(PIN_CS, 1);
}

int main() {
    stdio_init_all();

    spi_init(SPI_PORT, 1000 * 1000);   // 1 MHz
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CS,   GPIO_FUNC_SIO);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);

    // triangle period in samples
    const uint32_t tri_period = SAMPLES / TRI_HZ;   // 1000

    absolute_time_t next = get_absolute_time();
    uint32_t t = 0;

    while (true) {
        // --- Channel A: 2 Hz sine, 0-3.3V ---
        float angle = 2.0f * (float)M_PI * SINE_HZ * t / SAMPLES;
        uint16_t sine_val = (uint16_t)((sinf(angle) + 1.0f) * 0.5f * DAC_MAX);

        // --- Channel B: 1 Hz triangle, 0-3.3V ---
        uint32_t phase = t % tri_period;   // 0 .. 999
        uint16_t tri_val;
        if (phase < tri_period / 2) {
            tri_val = (uint16_t)((float)phase / (tri_period / 2) * DAC_MAX);
        } else {
            tri_val = (uint16_t)((float)(tri_period - phase) / (tri_period / 2) * DAC_MAX);
        }

        dac_write(0, sine_val);   // channel A
        dac_write(1, tri_val);    // channel B

        t++;
        next = delayed_by_us(next, 1000);   // 1 ms tick
        sleep_until(next);
    }
}
