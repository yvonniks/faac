import numpy as np
import soundfile as sf
import sys

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
master = get_energy('master.wav')
fixed = get_energy('fixed.wav')
bad_from_zip = get_energy('decoded-aac.wav')

print(f"{'Frame':<8} | {'Orig':<10} | {'Zip':<10} | {'Master':<10} | {'Fixed':<10} | {'Zip/Or':<6} | {'Mst/Or':<6} | {'Fix/Or':<6}")
for i in range(min(len(orig), len(master), len(fixed), len(bad_from_zip))):
    r_zip = bad_from_zip[i]/orig[i] if orig[i] > 1e-6 else 1.0
    r_mst = master[i]/orig[i] if orig[i] > 1e-6 else 1.0
    r_fix = fixed[i]/orig[i] if orig[i] > 1e-6 else 1.0
    if r_zip < 0.5 or r_mst < 0.5 or r_fix < 0.5 or abs(r_mst - r_fix) > 0.05:
         print(f"{i:<8d} | {orig[i]:.6f} | {bad_from_zip[i]:.6f} | {master[i]:.6f} | {fixed[i]:.6f} | {r_zip:.2f} | {r_mst:.2f} | {r_fix:.2f}")
