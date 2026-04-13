# Research: Pseudo SBR for AAC-LC

## Overview
Pseudo SBR (Spectral Band Replication) is an encoder-side-only bandwidth extension technique for AAC-LC. It reconstructs high-frequency components by patching lower-frequency spectral data into the higher bands and applying adaptive noise injection. This implementation ensures full compatibility with standard AAC-LC players as it uses standard bitstream elements (PNS bands).

## Implementation Details

### Spectral Patching
To avoid comb-filtering artifacts, we use transposed source windows. The source starting bin for a given target scale factor band (SFB) is calculated as:
`src_start = bw_bin - patch_size * (patch + 1)`
where `bw_bin` is the natural bandwidth cutoff bin, `patch_size` is the width of the target SFB, and `patch` is the index of the SBR band being reconstructed.

### Natural Bandwidth Reduction
To achieve a significant MOS lift at low bitrates, we reduce the natural (encoded) bandwidth by 25% when SBR is enabled. This reallocates critical bits from high-frequency base-layer quantization to the lower-frequency base-layer, while SBR fills the resulting gap in the spectrum.

### Adaptive Noise Injection
We use the Peak-to-Average Power Ratio (PAPR) of the source patch to determine the amount of noise to inject. This helps maintain the perceptual characteristics of the original signal (tonal vs. noisy).

**Magic Numbers:**
- Natural Bandwidth Scale: `0.75`
- Patch Energy Scale: `0.90`
- Baseline noise factor: `0.15`
- High PAPR (> 12.0) noise factor: `0.03`
- Mid PAPR (> 6.0) noise factor: `0.08`

### Quantization & Bitstream
Reconstructed SBR bands are forced to use Perceptual Noise Substitution (PNS) to provide high-frequency energy with minimal bit consumption.

## Proof of Work
The implementation includes:
- New `libfaac/pseudo_sbr.c` and `libfaac/pseudo_sbr.h`.
- Integration in `libfaac/frame.c` (pipeline), `libfaac/quantize.c` (PNS forcing), and `libfaac/stereo.c` (band limiting).
- CLI support via `--sbr` and `--no-sbr` flags.
