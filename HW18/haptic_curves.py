"""
HW18 - Haptic effect "force vs displacement" curves (for the report)
============================================================================
Plots the desired force (= desired current, since torque is proportional to
current) vs displacement for the 3 haptic effects, normalized to [-1, +1].
This satisfies the assignment item:
    "drawings of what the desired force vs displacement curve would be ...
     even better: equations and a python plot, normalized to +/-1"

Equations (same as haptic_effect() in Pico main.c):
    1) Detents :   F(theta) = -sin(N*theta)                      (periodic attractors)
    2) Wall    :   F(theta) = -K*(theta-tw) for |theta|>tw, else 0  (hard stop)
    3) Spring  :   F(theta) = -K*theta                           (linear centering)

Deps:  pip install numpy matplotlib
Run:   python haptic_curves.py     ->  shows and saves haptic_curves.png
============================================================================
"""
import numpy as np
import matplotlib.pyplot as plt

# Match the firmware parameters
DET_N      = 6.0          # notches per revolution
WALL_ANGLE = 0.6          # rad, wall location
# (stiffness only sets the slope after normalization)

theta = np.linspace(-np.pi, np.pi, 1000)     # displacement: -180 .. +180 deg
deg = np.degrees(theta)


def normalize(y):
    """Scale force to [-1, 1]."""
    m = np.max(np.abs(y))
    return y / m if m > 0 else y


# 1) Detents
f_detent = normalize(-np.sin(DET_N * theta))

# 2) Virtual wall
f_wall = np.zeros_like(theta)
f_wall[theta >  WALL_ANGLE] = -(theta[theta >  WALL_ANGLE] - WALL_ANGLE)
f_wall[theta < -WALL_ANGLE] = -(theta[theta < -WALL_ANGLE] + WALL_ANGLE)
f_wall = normalize(f_wall)

# 3) Centering spring
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
print("Saved haptic_curves.png")
plt.show()
