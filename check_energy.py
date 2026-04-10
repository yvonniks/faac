import wave
import struct
import sys

def get_frame_energy(wav_file):
    with wave.open(wav_file, 'rb') as w:
        num_channels = w.getnchannels()
        sample_width = w.getsampwidth()
        samples_per_frame = 1024
        energies = []
        while True:
            data = w.readframes(samples_per_frame)
            if not data: break
            if sample_width == 2:
                fmt = f"<{len(data)//2}h"
                samples = struct.unpack(fmt, data)
            elif sample_width == 4:
                fmt = f"<{len(data)//4}i"
                samples = struct.unpack(fmt, data)
            else: return []
            e = sum(float(s)*s for s in samples) / (len(samples) // num_channels) if samples else 0
            energies.append(e)
    return energies

def main():
    e1 = get_frame_energy(sys.argv[1])
    e2 = get_frame_energy(sys.argv[2])
    start = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    print(f"{'Frame':<8} | {'Original':<15} | {'Encoded':<15} | {'Ratio':<10}")
    print("-" * 55)
    for i in range(start, min(len(e1), len(e2))):
        ratio = e2[i] / e1[i] if e1[i] > 1 else 0
        print(f"{i:<8} | {e1[i]:<15.2f} | {e2[i]:<15.2f} | {ratio:<10.4f}")

if __name__ == "__main__":
    main()
