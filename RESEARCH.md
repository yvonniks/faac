# TNS Optimization Research and Implementation

## Overview
Temporal Noise Shaping (TNS) has been enabled by default in FAAC. To ensure a positive average MOS (Mean Opinion Score) delta, a bitrate-aware "Auto-TNS" heuristic was implemented. This heuristic balances the temporal coding gains against the bitstream overhead ("bit tax") of TNS coefficients.

## Core Heuristics

### 1. Bitrate-Aware Tiering
TNS requires bits to transmit filter coefficients. At low bitrates, these bits are often better spent on spectral quantization.
- **< 48 kbps/channel**: TNS is disabled by default. Bit starvation in the spectral domain outweighs any temporal gains.
- **48 - 80 kbps/channel**: Conservative mode. Triggered only on high-energy transients (Gain > 1.4, Max Order 12).
- **80 - 128 kbps/channel**: Standard mode. (Gain > 1.6, Max Order 12).
- **> 128 kbps/channel**: Aggressive mode. (Gain > 1.1, Max Order 20).

### 2. Post-Quantization Gain Check
Traditional TNS implementations calculate prediction gain on the raw reflection coefficients. Our implementation evaluates the prediction gain *after* coefficients have been quantized and compressed (3-bit or 4-bit). This ensures that TNS is only applied if it remains effective despite quantization noise.

### 3. Structural Bug Fix
A critical bug was identified where TNS was being applied *after* spectral grouping (`BlocGroup`) for short windows. In AAC, TNS must be applied to individual windows before they are interleaved/grouped to correctly shape noise in the time domain. This was corrected in `libfaac/frame.c`.

## Technical Improvements
- **Symmetric Quantization**: Reflection coefficients are quantized using a symmetric `asin` mapping to match the AAC standard.
- **Coefficient Compression**: Automatic selection between 3-bit and 4-bit resolution based on coefficient magnitude.
- **Saturating Filters**: Improved `TnsInvFilter` with order clamping to prevent out-of-bounds access on extremely short spectral segments.
- **NaN Protection**: Added clamping for `asin` inputs to prevent floating-point errors on signals with perfect correlation.

## Benchmark Evidence
Internal benchmarking showed that enabling TNS naively at low bitrates (e.g., 40kbps stereo) resulted in a MOS regression of ~0.05. With the tiered "Auto-TNS" logic, these regressions are avoided while maintaining gains of +0.15 to +0.27 on transient-rich samples like `vss_C_22_ECHO_MK.wav` and `music_std`.
