// HW14 - HX711 force sensor read on Pico 2W (RP2350)
// Bit-bang the HX711, IIR low-pass filter, stream raw/filtered/time over USB serial.
#include <stdio.h>
#include "pico/stdlib.h"

// ---- Pin configuration ----
#define PIN_SCK 14   // clock, output
#define PIN_DT  15   // data, input

// ---- IIR first-order low-pass: y[n] = a*y[n-1] + (1-a)*x[n] ----
// Smaller a = more smoothing. With fs=80Hz, a=0.8 puts the cutoff well below
// the 25-30Hz touch noise. Tune from the Python FFT plot.
#define IIR_A 0.8f

void hx711_init(void) {
    gpio_init(PIN_SCK);
    gpio_set_dir(PIN_SCK, GPIO_OUT);
    gpio_put(PIN_SCK, 0);

    gpio_init(PIN_DT);
    gpio_set_dir(PIN_DT, GPIO_IN);
    // The HX711 drives DT, no pull needed, but a pull-up is harmless.
}

// Read one 24-bit sample, sign-extend to 32-bit signed int.
// Waits until DT goes low (data ready), then clocks 25 times (gain 128).
int32_t hx711_read(void) {
    // Wait for data ready (DT low)
    while (gpio_get(PIN_DT)) {
        tight_loop_contents();
    }

    uint32_t raw = 0;
    for (int i = 0; i < 24; i++) {
        gpio_put(PIN_SCK, 1);
        sleep_us(1);
        raw = (raw << 1) | gpio_get(PIN_DT);
        gpio_put(PIN_SCK, 0);
        sleep_us(1);
    }
    // 25th pulse -> sets gain 128 for next read
    gpio_put(PIN_SCK, 1);
    sleep_us(1);
    gpio_put(PIN_SCK, 0);
    sleep_us(1);

    // sign-extend 24-bit two's complement to 32-bit signed int
    if (raw & 0x800000) {
        raw |= 0xFF000000;
    }
    return (int32_t)raw;
}

int main() {
    stdio_init_all();
    hx711_init();

    // Discard a couple of readings while the ADC settles, and seed the filter.
    for (int i = 0; i < 5; i++) hx711_read();
    float y = (float)hx711_read();

    while (true) {
        // Wait for the computer to send a number of samples to collect.
        int n = 0;
        if (scanf("%d", &n) != 1) {
            continue;
        }
        if (n <= 0) continue;

        // Collect into static buffers (avoids large stack frames).
        static int32_t  raw_buf[4096];
        static float    filt_buf[4096];
        static uint32_t t_buf[4096];
        if (n > 4096) n = 4096;

        for (int i = 0; i < n; i++) {
            int32_t x = hx711_read();
            y = IIR_A * y + (1.0f - IIR_A) * (float)x;

            raw_buf[i]  = x;
            filt_buf[i] = y;
            t_buf[i]    = to_ms_since_boot(get_absolute_time());
        }

        // Print everything back: time_ms, raw, filtered  (CSV)
        printf("%d\n", n);
        for (int i = 0; i < n; i++) {
            printf("%u,%d,%.2f\n", t_buf[i], raw_buf[i], filt_buf[i]);
        }
    }
}
