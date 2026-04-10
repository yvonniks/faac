# TNS Optimization Research and Implementation (v3)

## Overview
Temporal Noise Shaping (TNS) has been enabled by default in FAAC. This revision addresses quality regressions and stability issues identified in previous iterations. The goal is to provide a robust "Auto-TNS" mode that yields a positive average MOS delta across all scenarios.

## Core Fixes and Heuristics

### 1. Stability Fix: Reflection Coefficient Clamping
A critical issue was identified where reflection coefficients could reach exactly -1.0 (quantized index -8 for 4-bit resolution). This causes the decoder's synthesis filter to become unstable or leads to division-by-zero, resulting in severe audio artifacts.
- **Fix**: The quantized index is now clamped to `[-(2^(res-1)-1), 2^(res-1)-1]`. For 4-bit TNS, this restricts indices to `[-7, 7]`, ensuring coefficients stay within `[-0.98, 0.98]`.

### 2. Standard-Compliant Quantization
The quantization mapping was aligned with the ISO/IEC 14496-3 standard.
- **Mapping**: $k = \sin(index \cdot \frac{\pi}{2} / 2^{res-1})$
- **Implementation**: Used a symmetric mapping with `fac = (1 << (res-1)) / (M_PI / 2.0)` to ensure bitstream compatibility with standard decoders.

### 3. Tiered "Auto-TNS" Logic
TNS is now more selective, especially for Long blocks at high bitrates where spectral resolution is usually preferable.
- **< 64 kbps/ch**: TNS disabled (Auto mode).
- **64 - 128 kbps/ch**: Conservative thresholds (Gain > 2.0 - 2.4).
- **> 128 kbps/ch**: Standard threshold (Gain > 2.0).
- **Short Blocks**: More liberal triggering (Gain > 1.4) as TNS is significantly more beneficial for transient signals.

### 4. Throughput Optimization
To mitigate the performance impact of TNS analysis:
- **Energy Early-Exit**: Analysis is skipped if the total energy in the TNS bands is below a threshold.
- **Early Termination**: Levinson-Durbin recursion stops early if prediction gain is not improving.
- **Optimized Autocorrelation**: Streamlined the inner loop of the autocorrelation calculation.

## Technical Improvements
- **Profile Compliance**: Strictly enforces a maximum order of 12 for LC profile Long blocks.
- **NaN Protection**: Maintained clamping for `asin()` inputs to prevent floating-point errors.
- **Bitstream Consistency**: Refined the signaling of coefficient resolution and compression to ensure the encoder and bitstream writer are always in sync.

## Benchmark Expectations
The v3 implementation resolves the severe MOS regressions caused by filter instability. By using more conservative thresholds for Long blocks, we avoid the "pre-echo" like artifacts sometimes caused by TNS on tonal signals, while retaining the temporal benefits for transients in Short blocks. Overall, this is expected to yield a positive average MOS delta.
