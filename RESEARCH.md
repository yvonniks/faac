# TNS Optimization Research and Implementation (v3.2)

## Overview
Temporal Noise Shaping (TNS) has been enabled by default in FAAC. This final revision (v3.2) resolves the MOS regressions and stability issues identified in previous CI runs. The solution balances temporal coding gains, bitstream overhead, and standard-compliant signaling.

## Core Fixes and Heuristics

### 1. Standard-Compliant Signaling and Quantization
Previous attempts at custom resolution signaling were replaced with standard AAC mechanisms.
- **Quantization**: Reflection coefficients are quantized using a symmetric `asin` mapping: $k = \sin(index \cdot \frac{\pi}{2} / 2^{res-1})$.
- **Index Clamping**: Quantized indices are restricted to `[-7, 7]` for 4-bit TNS. This prevents reflection coefficients from reaching exactly -1.0, ensuring the decoder's synthesis filter remains stable and avoiding severe audio artifacts.
- **Dynamic Compression**: The encoder always uses a 4-bit base resolution but utilizes the standard `coef_compress` bit to signal 3-bit transmission when coefficients are small.

### 2. Tiered Auto-TNS Thresholds
To ensure a positive MOS delta, TNS triggering is aware of the per-channel bitrate and block type.
- **Short Blocks**: Gain > 1.2. Transients benefit significantly from TNS, so triggering is more liberal.
- **Long Blocks (< 96 kbps/ch)**: Gain > 2.4. Highly selective to protect spectral quality when bits are scarce.
- **Long Blocks (96-128 kbps/ch)**: Gain > 2.0. Conservative usage for tonal signals.
- **Long Blocks (> 128 kbps/ch)**: Gain > 1.6. standard usage when bit budget allows.

### 3. Throughput Recovery
Encoding speed is maintained via a spectral energy early-exit.
- **Energy Check**: TNS analysis is skipped if the energy in the segment is below 2% of full scale. This significantly reduces CPU usage for silent or low-level segments without impacting quality.

### 4. Structural Fixes
- TNS is now applied *before* spectral grouping for short blocks, as required by the AAC standard.
- Analysis window size for long blocks was corrected to 1024 samples.

## Benchmark Results
The v3.2 implementation achieves a positive average MOS delta across representative samples. It provides noticeable quality improvements on impulsive sounds (drums, castanets) while maintaining transparency and stability on tonal signals.
