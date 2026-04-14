import sys
import numpy as np
import soundfile as sf

def get_energy(filename):
    data, samplerate = sf.read(filename)
    if len(data.shape) > 1:
        data = np.mean(data, axis=1)
    frame_size = 1024
    num_frames = len(data) // frame_size
    energies = []
    for i in range(num_frames):
        frame = data[i*frame_size : (i+1)*frame_size]
        energy = np.sqrt(np.mean(frame**2))
        energies.append(energy)
    return np.array(energies)

orig = get_energy('original.wav')
bad = get_energy('decoded-aac.wav')
fixed = get_energy('fixed.wav')
base = get_energy('baseline.wav')

print(f"{'Frame':<8} | {'Orig':<10} | {'Bad':<10} | {'Base':<10} | {'Fixed':<10} | {'Bad/Orig':<8} | {'Fix/Orig':<8}")
for i in range(min(len(orig), len(bad), len(fixed), len(base))):
    r_bad = bad[i]/orig[i] if orig[i] > 1e-6 else 1.0
    r_fix = fixed[i]/orig[i] if orig[i] > 1e-6 else 1.0
    if r_bad < 0.5 or r_fix < 0.5 or r_bad > 2.0 or r_fix > 2.0:
        print(f"{i:<8d} | {orig[i]:.6f} | {bad[i]:.6f} | {base[i]:.6f} | {fixed[i]:.6f} | {r_bad:.2f} | {r_fix:.2f}")
