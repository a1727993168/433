"""HW14 - send a sample count to the Pico, read data back, plot time series + FFT.

Usage:
    python collect.py COM5 500
(replace COM5 with your Pico's serial port, 500 = number of samples)
"""
import sys
import serial
import numpy as np
import matplotlib.pyplot as plt

def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "COM5"
    nsamples = int(sys.argv[2]) if len(sys.argv) > 2 else 500

    import time
    ser = serial.Serial(port, 115200, timeout=5)
    time.sleep(2.0)  # let the USB CDC connection settle before talking

    # Ask the Pico for nsamples
    ser.reset_input_buffer()
    ser.write(f"{nsamples}\n".encode())
    ser.flush()

    # First numeric line back is the count (skip any blank/garbage lines)
    count = None
    for _ in range(20):
        line = ser.readline().decode(errors="ignore").strip()
        if line.isdigit():
            count = int(line)
            break
    if count is None:
        print("No response from Pico. Check the COM port and that nothing else "
              "has it open, then unplug/replug the Pico and retry.")
        ser.close()
        return
    print(f"Collecting {count} samples...")

    t_ms, raw, filt = [], [], []
    for _ in range(count):
        line = ser.readline().decode().strip()
        if not line:
            continue
        tt, rr, ff = line.split(",")
        t_ms.append(int(tt))
        raw.append(int(rr))
        filt.append(float(ff))
    ser.close()

    t = np.array(t_ms, dtype=float)
    t = (t - t[0]) / 1000.0          # seconds, start at 0
    raw = np.array(raw, dtype=float)
    filt = np.array(filt, dtype=float)

    # Sample rate from timestamps
    fs = (len(t) - 1) / (t[-1] - t[0])
    print(f"Measured sample rate: {fs:.1f} Hz  (Nyquist {fs/2:.1f} Hz)")

    # ---- FFT (remove DC offset first so the spectrum is readable) ----
    def fft_mag(x):
        x = x - np.mean(x)
        X = np.abs(np.fft.rfft(x)) / len(x)
        f = np.fft.rfftfreq(len(x), d=1.0 / fs)
        return f, X

    f_raw, X_raw = fft_mag(raw)
    f_filt, X_filt = fft_mag(filt)

    # ---- Plots ----
    fig, ax = plt.subplots(2, 1, figsize=(10, 8))

    ax[0].plot(t, raw, label="raw", alpha=0.6)
    ax[0].plot(t, filt, label="IIR filtered", linewidth=2)
    ax[0].set_xlabel("Time (s)")
    ax[0].set_ylabel("HX711 value")
    ax[0].set_title("Force sensor: raw vs filtered")
    ax[0].legend()
    ax[0].grid(True)

    ax[1].plot(f_raw, X_raw, label="raw", alpha=0.6)
    ax[1].plot(f_filt, X_filt, label="IIR filtered", linewidth=2)
    ax[1].set_xlabel("Frequency (Hz)")
    ax[1].set_ylabel("Magnitude")
    ax[1].set_title("FFT of raw vs filtered")
    ax[1].legend()
    ax[1].grid(True)

    plt.tight_layout()
    plt.savefig("hw14_data.png", dpi=120)
    print("Saved hw14_data.png")
    plt.show()

if __name__ == "__main__":
    main()
