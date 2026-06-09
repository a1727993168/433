// ===========================================================================
//  HW18 - Haptic Paddle  (Raspberry Pi Pico 2 W, C / Pico SDK)
// ---------------------------------------------------------------------------
//  这个程序在 Pico 2W 上一块板子里完成所有事情:
//    1) 读位置  : AS5600 磁编码器  (I2C, 地址 0x36)
//    2) 读电流  : INA219 电流传感器 (I2C, 地址 0x40, 板上 R100=0.1Ω)
//    3) 读力    : HX711 + 半桥称重传感器 391J (软件时序读 24bit)
//    4) 触觉效果: 至少 2 种 (1=凹槽detents, 2=虚拟墙wall, 3=回中弹簧spring)
//                效果根据"位置"算出"期望电流" i_desired  (这相当于 PD 环)
//    5) 电流控制: PI 控制器让"实际电流"跟踪"期望电流", 输出 PWM 给 L298N
//    6) 串口    : 通过 USB 把数据发给电脑上的 Python 画图; 收单字符切换效果
//
//  接线 (和 EasyEDA 电路图一致):
//    I2C0:  SDA=GP4   SCL=GP5      (AS5600 与 INA219 共用这条总线)
//    HX711: DT =GP16  SCK=GP17
//    L298N: ENA=GP13(PWM)  IN1=GP14  IN2=GP15   电机红黄两线->OUT1/OUT2
//    共地:  Pico GND / L298N GND / 电源 GND / 各传感器 GND 全部连一起
// ===========================================================================

#include <stdio.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/pwm.h"
#include "hardware/gpio.h"

// --------------------------- 引脚定义 --------------------------------------
#define I2C_PORT      i2c0
#define PIN_SDA       4
#define PIN_SCL       5

#define PIN_HX_DT     16     // HX711 数据 (输入)
#define PIN_HX_SCK    17     // HX711 时钟 (输出)

#define PIN_ENA       13     // L298N ENA  -> PWM, 控制力的大小(占空比)
#define PIN_IN1       14     // L298N IN1  -> 方向
#define PIN_IN2       15     // L298N IN2  -> 方向

// I2C 设备地址
#define AS5600_ADDR   0x36
#define INA219_ADDR   0x40

// --------------------------- 常量与可调参数 --------------------------------
#define PWM_WRAP      4095          // PWM 计数上限 (12bit), 配合默认时钟 ~30kHz(无声)
#define TWO_PI_F      6.2831853f

// 电流 PI 控制器参数 (单位: 占空比每 mA). >>> 先用这组, 实物上再微调 <<<
#define KP_I          0.0008f       // 比例
#define KI_I          0.0030f       // 积分
#define I_MAX         600.0f        // 期望电流上限 (mA), 保护电机/驱动

// 触觉效果参数 (这些就是"力-位移曲线"的系数, 见 haptic_curves.py)
#define DET_AMP       300.0f        // 凹槽: 力幅值 (mA)
#define DET_N         6.0f          // 凹槽: 一圈几个槽
#define WALL_ANGLE    0.6f          // 虚拟墙: 墙的位置 (rad, 约 ±34°)
#define WALL_K        900.0f        // 虚拟墙: 墙的硬度 (mA/rad)
#define SPRING_K      250.0f        // 回中弹簧: 刚度 (mA/rad)
#define DAMP          12.0f         // 阻尼 (mA per rad/s), 让手感更稳、不抖

#define LOOP_HZ       1000          // 控制环频率 (电流 PI 跑 1kHz)
#define TELE_EVERY    20            // 每 20 个循环发一次串口 (=> ~50Hz 给画图)

// 力传感器开关: 1=接了HX711; 0=没接/坏了, 完全跳过(用电流当力, 老师认可的方案)
#define USE_HX711     0

// 触觉效果编号
enum { EFF_DETENT = 1, EFF_WALL = 2, EFF_SPRING = 3 };
static volatile int g_effect = EFF_DETENT;

// =========================================================================
//  I2C 小工具
// =========================================================================
static bool i2c_read_regs(uint8_t addr, uint8_t reg, uint8_t *buf, size_t n) {
    if (i2c_write_blocking(I2C_PORT, addr, &reg, 1, true) < 0) return false;
    return i2c_read_blocking(I2C_PORT, addr, buf, n, false) >= 0;
}

// =========================================================================
//  AS5600 磁编码器: 读 12bit 原始角度 (0..4095 = 0..360°)
// =========================================================================
static float as5600_read_rad(void) {
    uint8_t b[2];
    if (!i2c_read_regs(AS5600_ADDR, 0x0C, b, 2)) return NAN;   // RAW ANGLE
    uint16_t raw = ((b[0] << 8) | b[1]) & 0x0FFF;
    return (float)raw / 4096.0f * TWO_PI_F;                    // 弧度
}

// =========================================================================
//  INA219 电流传感器: 直接读分流电压寄存器换算电流 (mA), 无需校准寄存器
//    Vshunt LSB = 10uV, R = 0.1Ω  =>  I[mA] = raw * 0.1
// =========================================================================
static void ina219_init(void) {
    // 默认上电配置 0x399F 即可 (32V, ±320mV, 12bit, 连续). 这里显式写一遍.
    uint8_t cfg[3] = {0x00, 0x39, 0x9F};
    i2c_write_blocking(I2C_PORT, INA219_ADDR, cfg, 3, false);
}
static float ina219_read_mA(void) {
    uint8_t b[2];
    if (!i2c_read_regs(INA219_ADDR, 0x01, b, 2)) return NAN;   // shunt voltage
    int16_t raw = (int16_t)((b[0] << 8) | b[1]);
    return raw * 0.1f;                                         // mA (含正负号)
}

// =========================================================================
//  HX711: 软件时序读 24bit (gain=128, 通道A). 返回原始计数(已带符号).
//    用作"力"信号; 若没读到数据(超时)返回上一次的值.
// =========================================================================
#if USE_HX711
static int32_t hx711_read(void) {
    static int32_t last = 0;
    // 等待数据就绪 (DT 变低). 加超时避免卡死.
    int timeout = 100000;
    while (gpio_get(PIN_HX_DT) && --timeout) tight_loop_contents();
    if (timeout == 0) return last;                 // 没接/没准备好 -> 用旧值

    int32_t val = 0;
    for (int i = 0; i < 24; i++) {
        gpio_put(PIN_HX_SCK, 1);
        sleep_us(1);
        val = (val << 1) | gpio_get(PIN_HX_DT);
        gpio_put(PIN_HX_SCK, 0);
        sleep_us(1);
    }
    // 第 25 个脉冲: 设置增益 128 (通道 A)
    gpio_put(PIN_HX_SCK, 1); sleep_us(1);
    gpio_put(PIN_HX_SCK, 0); sleep_us(1);

    if (val & 0x800000) val |= 0xFF000000;         // 24bit 符号扩展到 32bit
    last = val;
    return val;
}
#endif  // USE_HX711

// =========================================================================
//  L298N 电机驱动: duty ∈ [-1, +1]  (符号=方向, 大小=力度)
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
//  触觉效果: 输入 当前角度 phi(rad, 相对开机零点) 和 角速度 omega,
//            输出 "期望电流" i_desired (mA, 带方向). 这就是 PD 那一层.
// =========================================================================
static float haptic_effect(float phi, float omega) {
    float i_des = 0.0f;
    switch (g_effect) {
        case EFF_DETENT:    // 凹槽: 一圈 DET_N 个吸附点, 像棘轮/旋钮的咔哒
            i_des = -DET_AMP * sinf(DET_N * phi);
            break;
        case EFF_WALL:      // 虚拟墙: 越过 ±WALL_ANGLE 就被弹簧推回(撞墙感)
            if      (phi >  WALL_ANGLE) i_des = -WALL_K * (phi - WALL_ANGLE);
            else if (phi < -WALL_ANGLE) i_des = -WALL_K * (phi + WALL_ANGLE);
            else                        i_des = 0.0f;
            break;
        case EFF_SPRING:    // 回中弹簧: 总把手柄拉回 0° (像有弹簧)
            i_des = -SPRING_K * phi;
            break;
    }
    i_des -= DAMP * omega;                          // 加一点阻尼, 手感更稳
    if (i_des >  I_MAX) i_des =  I_MAX;             // 限幅保护
    if (i_des < -I_MAX) i_des = -I_MAX;
    return i_des;
}

// =========================================================================
//  初始化
// =========================================================================
static void hw_init(void) {
    stdio_init_all();                               // USB 串口

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

    // L298N 方向脚
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
//  主程序
// =========================================================================
int main(void) {
    hw_init();
    sleep_ms(2000);                                 // 给 USB 串口连上的时间

    const float dt = 1.0f / LOOP_HZ;                // 控制周期 (s)
    const int   loop_us = 1000000 / LOOP_HZ;

    float integ = 0.0f;                             // 电流环积分项
    float phi0  = as5600_read_rad();                // 开机零点
    if (isnan(phi0)) phi0 = 0.0f;
    float theta_prev = phi0;
    float phi = 0.0f;                               // 相对角度(已展开)
    int32_t force_raw = 0;
    int tele = 0;
    absolute_time_t next = get_absolute_time();

    while (true) {
        // ---- 0. 处理来自电脑的指令 (切换效果) ----
        int c = getchar_timeout_us(0);
        if (c == '1') g_effect = EFF_DETENT;
        else if (c == '2') g_effect = EFF_WALL;
        else if (c == '3') g_effect = EFF_SPRING;
        else if (c == 'z') { phi0 = theta_prev; phi = 0.0f; }  // 把当前位置设为零点

        // ---- 1. 读位置 + 算角速度(处理 0/2π 跳变) ----
        float theta = as5600_read_rad();
        if (!isnan(theta)) {
            float dth = theta - theta_prev;
            if (dth >  M_PI) dth -= TWO_PI_F;
            if (dth < -M_PI) dth += TWO_PI_F;
            phi += dth;                              // 相对开机点的累计角度
            theta_prev = theta;
        }
        float omega = 0.0f;
        static float phi_last = 0.0f;
        omega = (phi - phi_last) / dt;
        phi_last = phi;

        // ---- 2. 触觉效果: 位置 -> 期望电流 (PD 层) ----
        float i_des = haptic_effect(phi, omega);

        // ---- 3. 读实际电流 ----
        float i_meas = ina219_read_mA();
        if (isnan(i_meas)) i_meas = 0.0f;

        // ---- 4. 电流 PI 控制 -> 占空比 ----
        float err = i_des - i_meas;
        integ += err * dt;
        if (integ >  500.0f) integ =  500.0f;        // 积分抗饱和
        if (integ < -500.0f) integ = -500.0f;
        float duty = KP_I * err + KI_I * integ;
        if (duty >  1.0f) duty =  1.0f;
        if (duty < -1.0f) duty = -1.0f;
        motor_set(duty);

        // ---- 5. 偶尔读一下力 + 发数据给电脑画图 ----
        if (++tele >= TELE_EVERY) {
            tele = 0;
#if USE_HX711
            force_raw = hx711_read();                // HX711 慢, 不在快环里读
#else
            force_raw = 0;                           // 没接力传感器: 用电流当力
#endif
            float phi_deg = phi * 180.0f / M_PI;
            // CSV:  角度(度), 期望电流, 实际电流, 力原始值, 当前效果
            printf("%.2f,%.1f,%.1f,%ld,%d\n",
                   phi_deg, i_des, i_meas, (long)force_raw, g_effect);
        }

        // ---- 6. 定频 (1kHz) ----
        next = delayed_by_us(next, loop_us);
        sleep_until(next);
    }
}
