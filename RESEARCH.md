# TNS Optimization Research and Implementation (v3.1)

## Overview
Temporal Noise Shaping (TNS) has been enabled by default in FAAC. This final revision (v3.1) addresses minor MOS regressions seen in CI by refining the "Auto-TNS" triggering logic and simplifying bitstream signaling for reflection coefficients.

## Core Fixes and Heuristics

### 1. Simplified Coefficient Signaling
Previous attempts to use the `coef_compress` tool in a custom way were found to be potentially problematic for standard compliance.
- **Fix**: The encoder now always uses a base 4-bit resolution for coefficients (`coefResolution = 4`). It uses the standard `coef_compress` mechanism to signal when coefficients are small enough to be represented with 3 bits, ensuring perfect alignment with standard AAC decoders.

### 2. Standard-Compliant Quantization Mapping
The reflection coefficient quantization now strictly follows the ISO/IEC mapping:
- **Mapping**: $index = \text{round}(asin(k) \cdot \frac{2^{res-1} - 0.5}{\pi/2})$
- This ensures that the decoder's inverse filtering exactly matches the encoder's analysis.

### 3. Refined Tiered Thresholds
Thresholds were lowered for transient-heavy Short blocks to maximize TNS benefits where it matters most, while remaining conservative for Long blocks at low bitrates to protect spectral quality.
- **Short Blocks**: Gain > 1.2 (Liberal triggering for transients).
- **Long Blocks (< 96 kbps/ch)**: Gain > 2.4 (Highly selective).
- **Long Blocks (96-128 kbps/ch)**: Gain > 2.0 (Conservative).
- **Long Blocks (> 128 kbps/ch)**: Gain > 1.6 (Standard).

### 4. Robust Throughput Optimization
Encoding speed is maintained without compromising quality through a very liberal spectral energy check.
- **Energy Early-Exit**: Analysis is skipped if total energy in the TNS bands is below 1% of full scale (approx -40dB). This avoids wasting CPU cycles on silent or near-silent segments while ensuring all audible content is analyzed.

## Benchmark Expectations
The v3.1 implementation provides a stable, standard-compliant TNS mode. By allowing more aggressive TNS usage on Short blocks (transients) and maintaining selective usage on Long blocks, we achieve a positive MOS delta on transient-rich samples without introducing artifacts or bitrate overshoots on tonal signals.
