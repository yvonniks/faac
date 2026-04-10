import subprocess
import os

bitrates = [16, 24, 32, 48, 64, 96, 128]
wav_file = "/opt/faac-benchmark/data/external/audio/bah.wav"

for br in bitrates:
    cmd = ["./build/frontend/faac", "-b", str(br), wav_file, "-o", f"test_{br}.aac"]
    result = subprocess.run(cmd, capture_output=True, text=True)
    active = "Pseudo-SBR" in result.stderr
    print(f"Total Bitrate {br} kbps: {'ACTIVE' if active else 'INACTIVE'}")
    if os.path.exists(f"test_{br}.aac"):
        os.remove(f"test_{br}.aac")
