#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "bsp/board_api.h"
#include "tusb.h"
#include "usb_descriptors.h"

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"

#define BUTTON_PIN 14
#define LED_PIN    15

#define I2C_PORT i2c0
#define SDA_PIN 4
#define SCL_PIN 5

#define MPU_ADDR      0x68
#define WHO_AM_I      0x75
#define PWR_MGMT_1    0x6B
#define ACCEL_CONFIG  0x1C
#define GYRO_CONFIG   0x1B
#define ACCEL_XOUT_H  0x3B

enum {
    BLINK_NOT_MOUNTED = 250,
    BLINK_MOUNTED = 1000,
    BLINK_SUSPENDED = 2500,
};

static uint32_t blink_interval_ms = BLINK_NOT_MOUNTED;

// false = IMU mode, true = auto circle mode
static bool auto_mode = false;
static absolute_time_t last_button_time;

// Larger steps for a visible circle; sums to 0 so cursor returns to start
static const int8_t circle_dx[] = { 5, 5, 3, 0, -3, -5, -5, -5, -3, 0, 3, 5 };
static const int8_t circle_dy[] = { 0, 3, 5, 5,  5,  3,  0, -3, -5, -5, -5, -3 };
static int circle_index = 0;
static int circle_tick  = 0;

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

void mpu_init(void) {
    mpu_write_reg(PWR_MGMT_1, 0x00);
    mpu_write_reg(ACCEL_CONFIG, 0x00);
    mpu_write_reg(GYRO_CONFIG, 0x18);
}

void led_blinking_task(void);
void hid_task(void);

// 4 discrete speed levels based on raw accel magnitude
static int8_t accel_to_mouse(int16_t raw) {
    int16_t a = raw < 0 ? -raw : raw;
    int8_t  speed;
    if      (a < 2000)  speed = 0;
    else if (a < 8000)  speed = 2;
    else if (a < 16000) speed = 5;
    else                speed = 10;
    return raw > 0 ? speed : -speed;
}

static void send_mouse_report(int8_t dx, int8_t dy) {
    if (!tud_hid_ready()) return;
    tud_hid_mouse_report(REPORT_ID_MOUSE, 0x00, dx, dy, 0, 0);
}

int main(void)
{
    board_init();
    stdio_init_all();

    gpio_init(BUTTON_PIN);
    gpio_set_dir(BUTTON_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_PIN);

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 0); // start in IMU mode

    i2c_init(I2C_PORT, 400000);
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_PIN);
    gpio_pull_up(SCL_PIN);
    sleep_ms(100);

    uint8_t who = mpu_read_reg(WHO_AM_I);
    if (who == 0x68 || who == 0x98) {
        mpu_init();
    } else {
        while (1) {
            gpio_put(LED_PIN, 1);
            sleep_ms(100);
            gpio_put(LED_PIN, 0);
            sleep_ms(100);
        }
    }

    last_button_time = get_absolute_time();

    tud_init(BOARD_TUD_RHPORT);

    if (board_init_after_tusb) {
        board_init_after_tusb();
    }

    while (1)
    {
        tud_task();
        led_blinking_task();
        hid_task();
    }
}

void tud_mount_cb(void) {
    blink_interval_ms = BLINK_MOUNTED;
}

void tud_umount_cb(void) {
    blink_interval_ms = BLINK_NOT_MOUNTED;
}

void tud_suspend_cb(bool remote_wakeup_en) {
    (void) remote_wakeup_en;
    blink_interval_ms = BLINK_SUSPENDED;
}

void tud_resume_cb(void) {
    blink_interval_ms = tud_mounted() ? BLINK_MOUNTED : BLINK_NOT_MOUNTED;
}

void hid_task(void)
{
    const uint32_t interval_ms = 20;
    static uint32_t start_ms = 0;

    if (board_millis() - start_ms < interval_ms) return;
    start_ms += interval_ms;

    bool pressed = (gpio_get(BUTTON_PIN) == 0);

    if (pressed) {
        if (absolute_time_diff_us(last_button_time, get_absolute_time()) > 250000) {
            auto_mode = !auto_mode;
            last_button_time = get_absolute_time();
        }
    }

    gpio_put(LED_PIN, auto_mode ? 1 : 0);

    if (tud_suspended()) {
        if (pressed) tud_remote_wakeup();
        return;
    }

    if (auto_mode) {
        // advance circle index every 10 ticks (10 × 20ms = 200ms per step)
        // → 12 steps × 200ms = 2.4 s per revolution (slow circle)
        send_mouse_report(circle_dx[circle_index], circle_dy[circle_index]);
        if (++circle_tick >= 10) {
            circle_tick  = 0;
            circle_index = (circle_index + 1) % 12;
        }
        return;
    }

    uint8_t data[6];
    mpu_read_burst(ACCEL_XOUT_H, data, 6);

    int16_t ax_raw = combine_bytes(data[0], data[1]);
    int16_t ay_raw = combine_bytes(data[2], data[3]);

    int8_t dx =  accel_to_mouse(ax_raw);
    int8_t dy = -accel_to_mouse(ay_raw);

    send_mouse_report(dx, dy);
}

void tud_hid_report_complete_cb(uint8_t instance, uint8_t const* report, uint16_t len)
{
    (void) instance;
    (void) report;
    (void) len;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t* buffer, uint16_t reqlen)
{
    (void) instance;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const* buffer, uint16_t bufsize)
{
    (void) instance;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) bufsize;
}

void led_blinking_task(void)
{
    static uint32_t start_ms = 0;
    static bool led_state = false;

    if (!blink_interval_ms) return;
    if (board_millis() - start_ms < blink_interval_ms) return;

    start_ms += blink_interval_ms;
    board_led_write(led_state);
    led_state = !led_state;
}