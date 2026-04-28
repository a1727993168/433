#include <stdio.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

#define SPI_PORT     spi0
#define PIN_MISO     16
#define PIN_SCK      18
#define PIN_MOSI     19

#define PIN_CS_RAM   17   // 23k256
#define PIN_CS_DAC   20   // MCP4912

// 23k256 instructions
#define RAM_WRITE    0x02
#define RAM_READ     0x03
#define RAM_WRMR     0x01
#define RAM_SEQ      0x40  // sequential mode

// MCP4912: channel A, unbuffered, 1x gain (/GA=1), active (/SHDN=1)
#define DAC_CONFIG   0x3000

#define NUM_SAMPLES  1000

static inline void ram_cs_low()  { gpio_put(PIN_CS_RAM, 0); }
static inline void ram_cs_high() { gpio_put(PIN_CS_RAM, 1); }
static inline void dac_cs_low()  { gpio_put(PIN_CS_DAC, 0); }
static inline void dac_cs_high() { gpio_put(PIN_CS_DAC, 1); }

void ram_set_sequential_mode() {
    uint8_t buf[2] = {RAM_WRMR, RAM_SEQ};
    ram_cs_low();
    spi_write_blocking(SPI_PORT, buf, 2);
    ram_cs_high();
}

void ram_write(uint16_t addr, const uint8_t *data, size_t len) {
    uint8_t cmd[3] = {RAM_WRITE, (addr >> 8) & 0xFF, addr & 0xFF};
    ram_cs_low();
    spi_write_blocking(SPI_PORT, cmd, 3);
    spi_write_blocking(SPI_PORT, data, len);
    ram_cs_high();
}

void ram_read(uint16_t addr, uint8_t *data, size_t len) {
    uint8_t cmd[3] = {RAM_READ, (addr >> 8) & 0xFF, addr & 0xFF};
    ram_cs_low();
    spi_write_blocking(SPI_PORT, cmd, 3);
    spi_read_blocking(SPI_PORT, 0x00, data, len);
    ram_cs_high();
}

void dac_write(uint16_t word) {
    uint8_t buf[2] = {(word >> 8) & 0xFF, word & 0xFF};
    dac_cs_low();
    spi_write_blocking(SPI_PORT, buf, 2);
    dac_cs_high();
}

int main() {
    stdio_init_all();

    spi_init(SPI_PORT, 1000 * 1000);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

    gpio_set_function(PIN_CS_RAM, GPIO_FUNC_SIO);
    gpio_set_dir(PIN_CS_RAM, GPIO_OUT);
    gpio_put(PIN_CS_RAM, 1);

    gpio_set_function(PIN_CS_DAC, GPIO_FUNC_SIO);
    gpio_set_dir(PIN_CS_DAC, GPIO_OUT);
    gpio_put(PIN_CS_DAC, 1);

    ram_set_sequential_mode();

    // Build 1000-sample sine wave (0V to 3.3V) and store in SRAM
    // Each sample is a 16-bit MCP4912 command word (2 bytes)
    uint8_t samples[NUM_SAMPLES * 2];
    for (int i = 0; i < NUM_SAMPLES; i++) {
        float voltage = (sinf(2.0f * (float)M_PI * i / NUM_SAMPLES) + 1.0f) / 2.0f * 3.3f;
        // Scale to 10-bit DAC value using bit operations
        uint16_t dac_val = (uint16_t)((voltage / 3.3f) * 1023.0f);
        uint16_t cmd = DAC_CONFIG | ((dac_val & 0x3FF) << 2);
        samples[i * 2]     = (cmd >> 8) & 0xFF;
        samples[i * 2 + 1] =  cmd       & 0xFF;
    }
    ram_write(0, samples, NUM_SAMPLES * 2);

    // Read 2 bytes at a time from RAM and write to DAC, 1ms per sample = 1Hz
    uint16_t addr = 0;
    uint8_t buf[2];
    while (true) {
        ram_read(addr, buf, 2);
        uint16_t cmd = ((uint16_t)buf[0] << 8) | buf[1];
        dac_write(cmd);
        addr = (addr + 2) % (NUM_SAMPLES * 2);
        sleep_ms(1);
    }
}
