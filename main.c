#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"

#define SERVO_PIN 15

void set_servo_angle(float angle) {
    // SG90 servo: about 0.5ms to 2.5ms pulse width at 50Hz
    float min_pulse_us = 500.0f;
    float max_pulse_us = 2500.0f;

    float pulse_us = min_pulse_us + (angle / 180.0f) * (max_pulse_us - min_pulse_us);

    // With wrap = 20000 and clkdiv = 125:
    // PWM counter runs at 1 MHz, so 1 count = 1 us
    pwm_set_gpio_level(SERVO_PIN, (uint16_t)pulse_us);
}

int main() {
    stdio_init_all();

    gpio_set_function(SERVO_PIN, GPIO_FUNC_PWM);
    uint slice_num = pwm_gpio_to_slice_num(SERVO_PIN);

    // System clock 125 MHz / 125 = 1 MHz
    // So PWM period can be set directly in microseconds
    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, 125.0f);
    pwm_config_set_wrap(&config, 20000);   // 20 ms period = 50 Hz
    pwm_init(slice_num, &config, true);

    while (1) {
        // 0 -> 180
        for (float angle = 0; angle <= 180; angle += 5) {
            set_servo_angle(angle);
            sleep_ms(80);
        }

        sleep_ms(500);

        // 180 -> 0
        for (float angle = 180; angle >= 0; angle -= 5) {
            set_servo_angle(angle);
            sleep_ms(80);
        }

        sleep_ms(500);
    }
}