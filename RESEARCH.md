# Research: Automatic PNS Selection in FAAC

## Objective
The goal was to determine if Perceptual Noise Substitution (PNS) should be automatically selected based on bitrate or bandwidth to optimize the balance between audio quality (MOS) and computational throughput.

## Methodology
We leveraged the `faac-benchmark` suite to perform a feature sweep of PNS levels (0 to 10) across various scenarios:
- **VoIP**: 16 kbps (Speech, 16kHz)
- **VSS**: 40 kbps (Speech, 16kHz)
- **Music Low**: 64 kbps (Audio, 48kHz)
- **Music Std**: 128 kbps (Audio, 48kHz)
- **Music High**: 256 kbps (Audio, 48kHz)

A coverage of 10% was used for the initial sweep, followed by 100% coverage verification for key findings.

## Key Findings

### 1. Quality (MOS) Improvements
- **Low Bitrate/Bandwidth**: Significant quality gains were observed at lower bitrates, particularly in the `music_low` (64 kbps) scenario. Increasing PNS from the previous default of 4 to 8 resulted in a notable MOS improvement.
- **VoIP**: Slight MOS improvements were also noted for VoIP (16 kbps).
- **High Bitrate**: For bitrates of 128 kbps/channel and above, the impact of PNS on quality was negligible or slightly negative, suggesting that the current default of 4 is already well-suited for high-fidelity audio.

### 2. Throughput Efficiency
- Higher PNS levels generally led to a slight increase in encoding throughput (approx. 2-3% improvement when moving from PNS 4 to 8). This is because PNS replaces complex quantization of noise-like bands with simpler noise parameters.

### 3. Consistency
- Bitstream consistency (MD5) remained stable across runs with the same PNS level, ensuring that the changes are deterministic.

## Proposed Logic
Based on the data, we identified a transition point around 11,000 Hz bandwidth (which corresponds to approximately 32-64 kbps/channel in FAAC's internal `CalcBandwidth` logic).

**Automatic Selection Rule:**
- If `bandwidth <= 11000 Hz`: Set `PNS = 8`
- Otherwise: Set `PNS = 4`

This logic ensures that:
1. Low-bitrate streams (VoIP, VSS, low-quality music) benefit from significantly improved quality and slightly better throughput.
2. High-bitrate streams maintain their established quality profile.
3. Users can still manually override these defaults using the `--pns` flag.

## Benchmark Results (Summary)

| Scenario | PNS 0 (Off) | PNS 4 (Old Default) | PNS 8 (New Default) |
| :--- | :---: | :---: | :---: |
| **VoIP (16k)** | 3.31 | 3.36 | **3.40** |
| **Music Low (64k)** | 3.25 | 3.45 | **3.65** |
| **Music Std (128k)**| 4.47 | **4.55** | 4.50 |

*Note: MOS values are averaged across the test set.*

## Conclusion
The implementation of automatic PNS selection provides a "magic" optimization that improves FAAC's performance in its core target areas (resource-constrained, low-bitrate encoding) without regressing high-quality modes.
