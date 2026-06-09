"""
HW18 - 触觉效果的 "力 vs 位移" 曲线 (用于报告/作业图)
============================================================================
作用: 画出 3 种触觉效果的期望力(=期望电流, 因为力矩∝电流)随位移变化的曲线,
      并把力归一化到 [-1, +1]. 这正好满足作业里:
        "drawings of desired force vs displacement curve ...
         even better: equations and a python plot, normalized to +/-1"

公式 (和 Pico main.c 里 haptic_effect() 一致):
    1) 凹槽 Detents :   F(θ) = -sin(N·θ)                  (周期性吸附点)
    2) 虚拟墙 Wall   :   F(θ) = -K·(θ-θw)  当 |θ|>θw, 否则 0   (撞墙)
    3) 回中弹簧Spring:   F(θ) = -K·θ                       (线性回中)

依赖:  pip install numpy matplotlib
运行:  python haptic_curves.py     ->  显示并保存 haptic_curves.png
============================================================================
"""
import numpy as np
import matplotlib.pyplot as plt

# 和固件一致的参数
DET_N      = 6.0          # 一圈几个槽
WALL_ANGLE = 0.6          # rad, 墙的位置
# (刚度系数在归一化后只决定斜率, 这里取便于显示的值)

theta = np.linspace(-np.pi, np.pi, 1000)     # 位移: -180° .. +180°
deg = np.degrees(theta)


def normalize(y):
    """把力缩放到 [-1, 1]."""
    m = np.max(np.abs(y))
    return y / m if m > 0 else y


# --- 1) 凹槽 Detents ---
f_detent = normalize(-np.sin(DET_N * theta))

# --- 2) 虚拟墙 Wall ---
f_wall = np.zeros_like(theta)
f_wall[theta >  WALL_ANGLE] = -(theta[theta >  WALL_ANGLE] - WALL_ANGLE)
f_wall[theta < -WALL_ANGLE] = -(theta[theta < -WALL_ANGLE] + WALL_ANGLE)
f_wall = normalize(f_wall)

# --- 3) 回中弹簧 Spring ---
f_spring = normalize(-theta)


fig, axes = plt.subplots(3, 1, figsize=(8, 9), sharex=True)
fig.suptitle("HW18 Haptic Effects: Desired Force vs Displacement (normalized)",
             fontsize=14, fontweight="bold")

axes[0].plot(deg, f_detent, color="#5ac878", lw=2)
axes[0].set_title(r"1) Detents:   $F(\theta) = -\sin(N\theta)$,  N=%d" % int(DET_N))

axes[1].plot(deg, f_wall, color="#d85050", lw=2)
axes[1].axvline( np.degrees(WALL_ANGLE), ls="--", color="gray", lw=1)
axes[1].axvline(-np.degrees(WALL_ANGLE), ls="--", color="gray", lw=1)
axes[1].set_title(r"2) Virtual Wall:   $F=-K(\theta-\theta_w)$ for $|\theta|>\theta_w$, else 0")

axes[2].plot(deg, f_spring, color="#5a8fd8", lw=2)
axes[2].set_title(r"3) Centering Spring:   $F(\theta) = -K\theta$")

for ax in axes:
    ax.axhline(0, color="black", lw=0.8)
    ax.axvline(0, color="black", lw=0.5)
    ax.set_ylabel("Force (norm.)")
    ax.set_ylim(-1.2, 1.2)
    ax.grid(alpha=0.3)
axes[2].set_xlabel("Paddle displacement (degrees)")

plt.tight_layout(rect=[0, 0, 1, 0.97])
plt.savefig("haptic_curves.png", dpi=130)
print("已保存 haptic_curves.png")
plt.show()
