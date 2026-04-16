# TNS Optimization Research and Implementation

## Overview
Temporal Noise Shaping (TNS) has been enabled by default in FAAC. This implementation addresses historical quality issues by ensuring structural standard compliance, filter stability, and bitrate-aware triggering.

## Core Implementation Details

### 1. Structural Correctness
To support TNS on short blocks, the encoding loop was modified to apply TNS analysis and filtering *before* spectral grouping (`BlocGroup`). This ensures that each short window is analyzed independently, preserving temporal resolution on transients.

### 2. Standard-Compliant Quantization
- **Mapping**: Reflection coefficients are quantized using standard symmetric `asin` mapping: `index = round(asin(k) * (2^(res-1)-0.5) / (pi/2))`.
- **Stability**: Quantized indices are restricted to `[-7, 7]` (for 4-bit resolution) to prevent reflection coefficients from reaching exactly -1.0, which causes instability in decoder synthesis filters.
- **Compression**: Standard bitstream compression (`coef_compress`) is used when indices fit within the 3-bit range, while maintaining the same scaling factors for decoder compatibility.

### 3. Tiered Auto-TNS Thresholds
To maximize perceptual gains while minimizing "bit-tax" overhead, the Auto-TNS mode uses tiered thresholds based on the per-channel bitrate and block type:

| Block Type | Per-Channel Bitrate | Trigger Threshold (Gain) |
| :--- | :--- | :--- |
| Short | All | > 1.4 |
| Long | < 96 kbps | > 2.4 |
| Long | 96 - 128 kbps | > 2.0 |
| Long | > 128 kbps | > 1.6 |

*Note: TNS is disabled by default in Auto-mode for bitrates below 64 kbps per channel to protect the limited bit budget.*

### 4. Performance Optimization
An energy-based early-exit heuristic was implemented to recover throughput. TNS analysis is skipped for very quiet blocks (energy < 0.1% of full scale), significantly reducing CPU usage on non-impulsive segments with negligible quality impact.

## Conclusion
This implementation provides a robust, standard-compliant TNS tool that improves perceptual quality on transients while protecting tonal signals at lower bitrates, resulting in a consistent positive average MOS delta across the benchmark suite.
