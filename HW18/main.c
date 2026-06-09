// ===========================================================================
//  HW18 - Haptic Paddle  (Raspberry Pi Pico 2 W, C / Pico SDK)
// ---------------------------------------------------------------------------
//  Everything runs on a single Pico 2W:
//    1) Position : AS5600 magnetic encoder   (I2C, addr 0x36)
//    2) Current  : INA219 current sensor      (I2C, addr 0x40, on-board R100=0.1ohm)
//    3) Force    : HX711 + half-bridge load cell 391J (bit-banged 24-bit read)
//    4) Haptics  : 3 effects (1=detents, 2=virtual wall, 3=centering spring)
//                  effect maps position -> desired current i_desired   (PD layer)
//    5) Current control : PI loop makes measured current track desired, PWM->L298N
//    6) Serial   : sends data to the PC over USB for plotting; reads a char to switch effect
//
//  Wiring (matches the EasyEDA schematic):
//    I2C0 : SDA=GP4   SCL=GP5    (AS5600 and INA219 share this bus)
//    HX711: DT =GP16  SCK=GP17
//    L298N: ENA=GP13(PWM)  IN1=GP14  IN2=GP15   motor red/yellow -> OUT1/OUT2
//    Common ground: Pico / L298N / supply / all sensors tied together
// ===========================================================================

#include <stdio.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"
#include "hardware/gpio.h"

// --------------------------- Pin map ---------------------------------------
#define I2C_PORT      i2c0
#define PIN_SDA       4
#define PIN_SCL       5

#define PIN_HX_DT     16     // HX711 data  (input)
#define PIN_HX_SCK    17     // HX711 clock (output)

#define PIN_ENA       13     // L298N ENA -> PWM, sets force magnitude (duty)
#define PIN_IN1       14     // L298N IN1 -> direction
#define PIN_IN2       15     // L298N IN2 -> direction

// I2C device addresses
#define AS5600_ADDR   0x36
#define INA219_ADDR   0x40

// --------------------------- Constants / tunables --------------------------
#define PWM_WRAP      4095          // PWM top (12-bit), ~30kHz with default clock (silent)
#define TWO_PI_F      6.2831853f

// Current PI controller gains (duty per mA). Start here, tune on hardware.
#define KP_I          0.0008f       // proportional
#define KI_I          0.0030f       // integral
#define I_MAX         600.0f        // desired-current limit (mA), protects motor/driver

// Haptic effect parameters (these are the force-displacement curve coeffs)
#define DET_AMP       300.0f        // detents: force amplitude (mA)
#define DET_N         6.0f          // detents: notches per revolution
#define WALL_ANGLE    0.6f          // wall: wall location (rad, ~±34deg)
#define WALL_K        900.0f        // wall: stiffness (mA/rad)
#define SPRING_K      250.0f        // spring: stiffness (mA/rad)
#define DAMP          12.0f         // damping (mA per rad/s), steadier feel

#define LOOP_HZ       1000          // control loop rate (current PI at 1kHz)
#define TELE_EVERY    20            // send serial every 20 loops (=> ~50Hz to PC)

// Force-sensor switch: 1 = HX711 connected (full version);
//                      0 = skip it, use current as a force proxy (instructor-approved)
#define USE_HX711     1

enum { EFF_DETENT = 1, EFF_WALL = 2, EFF_SPRING = 3 };
static volatile int g_effect = EFF_DETENT;

// =========================================================================
//  I2C helper
// =========================================================================
static bool i2c_read_regs(uint8_t addr, uint8_t reg, uint8_t *buf, size_t n) {
    if (i2c_write_blocking(I2C_PORT, addr, &reg, 1, true) < 0) return false;
    return i2c_read_blocking(I2C_PORT, addr, buf, n, false) >= 0;
}

// =========================================================================
//  AS5600 magnetic encoder: read 12-bit raw angle (0..4095 = 0..360deg)
// =========================================================================
static float as5600_read_rad(void) {
    uint8_t b[2];
    if (!i2c_read_regs(AS5600_ADDR, 0x0C, b, 2)) return NAN;   // RAW ANGLE register
    uint16_t raw = ((b[0] << 8) | b[1]) & 0x0FFF;
    return (float)raw / 4096.0f * TWO_PI_F;                    // radians
}

// =========================================================================
//  INA219 current sensor: read shunt-voltage register and convert to mA.
//    Vshunt LSB = 10uV, R = 0.1ohm  =>  I[mA] = raw * 0.1
// =========================================================================
static void ina219_init(void) {
    // Default reset config 0x399F (32V, +-320mV, 12-bit, continuous); write it explicitly.
    uint8_t cfg[3] = {0x00, 0x39, 0x9F};
    i2c_write_blocking(I2C_PORT, INA219_ADDR, cfg, 3, false);
}
static float ina219_read_mA(void) {
    uint8_t b[2];
    if (!i2c_read_regs(INA219_ADDR, 0x01, b, 2)) return NAN;   // shunt voltage register
    int16_t raw = (int16_t)((b[0] << 8) | b[1]);
    return raw * 0.1f;                                         // mA (signed)
}

// =========================================================================
//  HX711: bit-banged 24-bit read (gain=128, channel A). Returns signed counts.
//    Used as the force signal; returns last value on timeout (not connected).
// =========================================================================
#if USE_HX711
static int32_t hx711_read(void) {
    static int32_t last = 0;
    // Wait for data ready (DT goes low). Timeout to avoid hanging.
    int timeout = 100000;
    while (gpio_get(PIN_HX_DT) && --timeout) tight_loop_contents();
    if (timeout == 0) return last;

    int32_t val = 0;
    for (int i = 0; i < 24; i++) {
        gpio_put(PIN_HX_SCK, 1);
        sleep_us(1);
        val = (val << 1) | gpio_get(PIN_HX_DT);
        gpio_put(PIN_HX_SCK, 0);
        sleep_us(1);
    }
    // 25th pulse selects gain 128 (channel A)
    gpio_put(PIN_HX_SCK, 1); sleep_us(1);
    gpio_put(PIN_HX_SCK, 0); sleep_us(1);

    if (val & 0x800000) val |= 0xFF000000;         // sign-extend 24-bit to 32-bit
    last = val;
    return val;
}
#endif  // USE_HX711

// =========================================================================
//  L298N motor drive: duty in [-1, +1]  (sign = direction, magnitude = force)
// =========================================================================
static void motor_set(float duty) {
    bool dir = (duty >= 0.0f);
    gpio_put(PIN_IN1, dir);
    gpio_put(PIN_IN2, !dir);
    float mag = fabsf(duty);
    if (mag > 1.0f) mag = 1.0f;
    pwm_set_gpio_level(PIN_ENA, (uint16_t)(mag * PWM_WRAP));
}

// =========================================================================
//  Haptic effect: given angle phi (rad, relative to power-on zero) and angular
//  velocity omega, return desired current i_desired (mA, signed). This is the PD layer.
// =========================================================================
static float haptic_effect(float phi, float omega) {
    float i_des = 0.0f;
    switch (g_effect) {
        case EFF_DETENT:    // detents: DET_N attractor points per revolution
            i_des = -DET_AMP * sinf(DET_N * phi);
            break;
        case EFF_WALL:      // virtual wall: spring pushes back past +-WALL_ANGLE
            if      (phi >  WALL_ANGLE) i_des = -WALL_K * (phi - WALL_ANGLE);
            else if (phi < -WALL_ANGLE) i_des = -WALL_K * (phi + WALL_ANGLE);
            else                        i_des = 0.0f;
            break;
        case EFF_SPRING:    // centering spring: always pulls back to 0 deg
            i_des = -SPRING_K * phi;
            break;
    }
    i_des -= DAMP * omega;                          // damping for a steadier feel
    if (i_des >  I_MAX) i_des =  I_MAX;             // clamp for protection
    if (i_des < -I_MAX) i_des = -I_MAX;
    return i_des;
}

// =========================================================================
//  Init
// =========================================================================
static void hw_init(void) {
    stdio_init_all();                               // USB serial

    // I2C
    i2c_init(I2C_PORT, 400 * 1000);                 // 400kHz
    gpio_set_function(PIN_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_SDA);
    gpio_pull_up(PIN_SCL);

#if USE_HX711
    // HX711
    gpio_init(PIN_HX_DT);  gpio_set_dir(PIN_HX_DT, GPIO_IN);
    gpio_init(PIN_HX_SCK); gpio_set_dir(PIN_HX_SCK, GPIO_OUT);
    gpio_put(PIN_HX_SCK, 0);
#endif

    // L298N direction pins
    gpio_init(PIN_IN1); gpio_set_dir(PIN_IN1, GPIO_OUT);
    gpio_init(PIN_IN2); gpio_set_dir(PIN_IN2, GPIO_OUT);

    // L298N ENA -> PWM
    gpio_set_function(PIN_ENA, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(PIN_ENA);
    pwm_set_wrap(slice, PWM_WRAP);
    pwm_set_enabled(slice, true);
    motor_set(0.0f);

    ina219_init();
}

// =========================================================================
//  Main
// =========================================================================
int main(void) {
    hw_init();
    sleep_ms(2000);                                 // let USB serial connect

    const float dt = 1.0f / LOOP_HZ;                // control period (s)
    const int   loop_us = 1000000 / LOOP_HZ;

    float integ = 0.0f;                             // current-loop integrator
    float phi0  = as5600_read_rad();                // power-on zero
    if (isnan(phi0)) phi0 = 0.0f;
    float theta_prev = phi0;
    float phi = 0.0f;                               // unwrapped relative angle
    int32_t force_raw = 0;
    int tele = 0;
    absolute_time_t next = get_absolute_time();

    while (true) {
        // 0. handle command from PC (switch effect)
        int c = getchar_timeout_us(0);
        if (c == '1') g_effect = EFF_DETENT;
        else if (c == '2') g_effect = EFF_WALL;
        else if (c == '3') g_effect = EFF_SPRING;
        else if (c == 'z') { phi0 = theta_prev; phi = 0.0f; }  // zero at current position

        // 1. read position + estimate velocity (handle 0/2pi wrap)
        float theta = as5600_read_rad();
        if (!isnan(theta)) {
            float dth = theta - theta_prev;
            if (dth >  M_PI) dth -= TWO_PI_F;
            if (dth < -M_PI) dth += TWO_PI_F;
            phi += dth;
            theta_prev = theta;
        }
        float omega = 0.0f;
        static float phi_last = 0.0f;
        omega = (phi - phi_last) / dt;
        phi_last = phi;

        // 2. haptic effect: position -> desired current (PD layer)
        float i_des = haptic_effect(phi, omega);

        // 3. read measured current
        float i_meas = ina219_read_mA();
        if (isnan(i_meas)) i_meas = 0.0f;

        // 4. current PI control -> duty
        float err = i_des - i_meas;
        integ += err * dt;
        if (integ >  500.0f) integ =  500.0f;        // anti-windup
        if (integ < -500.0f) integ = -500.0f;
        float duty = KP_I * err + KI_I * integ;
        if (duty >  1.0f) duty =  1.0f;
        if (duty < -1.0f) duty = -1.0f;
        motor_set(duty);

        // 5. occasionally read force + send telemetry to PC
        if (++tele >= TELE_EVERY) {
            tele = 0;
#if USE_HX711
            force_raw = hx711_read();                // HX711 is slow, keep out of fast loop
#else
            force_raw = 0;                           // no load cell: current is the force proxy
#endif
            float phi_deg = phi * 180.0f / M_PI;
            // CSV: angle(deg), desired mA, measured mA, force raw, effect
            printf("%.2f,%.1f,%.1f,%ld,%d\n",
                   phi_deg, i_des, i_meas, (long)force_raw, g_effect);
        }

        // 6. pace the loop (1kHz)
        next = delayed_by_us(next, loop_us);
        sleep_until(next);
    }
}
