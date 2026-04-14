import sys
import numpy as np
import soundfile as sf

def get_energy(filename):
    data, samplerate = sf.read(filename)
    if len(data.shape) > 1:
        data = np.mean(data, axis=1)

    # Use frame size of 1024
    frame_size = 1024
    num_frames = len(data) // frame_size
    energies = []
    for i in range(num_frames):
        frame = data[i*frame_size : (i+1)*frame_size]
        energy = np.sqrt(np.mean(frame**2))
        energies.append(energy)
    return np.array(energies)

e1 = get_energy(sys.argv[1]) # original
e2 = get_energy(sys.argv[2]) # degraded (bad)
e3 = get_energy(sys.argv[3]) # fixed

print("Frame | Original | Bad | Fixed")
for i in range(min(len(e1), len(e2), len(e3))):
    print(f"{i:5d} | {e1[i]:.6f} | {e2[i]:.6f} | {e3[i]:.6f}")
