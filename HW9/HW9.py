import numpy as np
import matplotlib.pyplot as plt

# ===== Choose file =====
filename = "sigD.csv"

# ===== Load CSV =====
data = np.loadtxt(filename, delimiter=",")

time = data[:, 0]
signal = data[:, 1]

# ===== Sample info =====
N = len(signal)
dt = time[1] - time[0]

# ===== Original FFT =====
fft_original = np.abs(np.fft.fft(signal))
freqs = np.fft.fftfreq(N, dt)

half = N // 2
freqs_half = freqs[:half]
fft_original_half = fft_original[:half]

# ===== IIR Filter =====
A = 0.95
B = 0.05   # A + B must equal 1

filtered = np.zeros(len(signal))
filtered[0] = signal[0]

for i in range(1, len(signal)):
    filtered[i] = A * filtered[i - 1] + B * signal[i]

# ===== Filtered FFT =====
fft_filtered = np.abs(np.fft.fft(filtered))
fft_filtered_half = fft_filtered[:half]

# ===== Plot =====
plt.figure(figsize=(10, 7))

plt.subplot(2, 1, 1)
plt.plot(time, signal, color="black", label="unfiltered")
plt.plot(time, filtered, color="red", label="filtered")
plt.title(f"{filename} IIR Filter, A={A}, B={B}")
plt.xlabel("Time")
plt.ylabel("Signal")
plt.legend()

plt.subplot(2, 1, 2)
plt.plot(freqs_half, fft_original_half, color="black", label="unfiltered FFT")
plt.plot(freqs_half, fft_filtered_half, color="red", label="filtered FFT")
plt.title("FFT Comparison")
plt.xlabel("Frequency")
plt.ylabel("Magnitude")
plt.legend()

plt.tight_layout()
plt.show()
