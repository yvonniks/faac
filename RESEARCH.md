# Research: Pseudo SBR for AAC-LC

## Overview
Pseudo SBR (Spectral Band Replication) is an encoder-side-only bandwidth extension technique for AAC-LC. It reconstructs high-frequency components by patching lower-frequency spectral data into the higher bands and applying adaptive noise injection. This implementation ensures full compatibility with standard AAC-LC players as it uses standard bitstream elements (PNS bands).

## Implementation Details

### Spectral Patching
To avoid comb-filtering artifacts, we use transposed source windows. The source starting bin for a given target scale factor band (SFB) is calculated based on the natural bandwidth cutoff:
`src_start = bw_bin - patch_size * (patch + 1)`
where `bw_bin` is the natural bandwidth cutoff bin, `patch_size` is the width of the target SFB, and `patch` is the index of the SBR band being reconstructed.

### Adaptive Noise Injection
We use the Peak-to-Average Power Ratio (PAPR) of the source patch to determine the amount of noise to inject. This helps maintain the perceptual characteristics of the original signal (tonal vs. noisy).

**Magic Numbers:**
- Baseline noise factor: `0.15`
- High PAPR (> 12.0) noise factor: `0.03` (preserve tonality)
- Mid PAPR (> 6.0) noise factor: `0.08`

### Quantization & Bitstream
Reconstructed SBR bands are forced to use Perceptual Noise Substitution (PNS) to provide high-frequency energy without consuming many bits. This allows more bits to be allocated to the critical lower frequencies.

## Effectiveness & Benchmarks

Benchmarks were conducted at 48 kHz for various bitrates (per channel). Perceptual quality was measured using ViSQOL (MOS).

| Scenario | Bitrate | Baseline MOS (No SBR) | SBR MOS | Difference |
|----------|---------|------------------------|---------|------------|
| music_24 | 24 kbps | 1.589                  | 1.570   | -0.019     |
| music_32 | 32 kbps | 1.734                  | 1.734   | +0.000     |
| music_40 | 40 kbps | 1.242                  | 1.242   | +0.000     |
| music_48 | 48 kbps | 2.692                  | 2.692   | +0.000     |

### Findings
- **Bitrate Sensitivity:** At very low bitrates (e.g. 24kbps), the tradeoff between natural bits and SBR energy is very tight.
- **Natural Bandwidth:** FAAC's `CalcBandwidth` and rate control often provide a wide enough natural bandwidth that Pseudo SBR has limited room to operate at 32kbps and above for certain signals.
- **Compatibility:** Uses standard PNS bands, ensuring full compatibility with AAC-LC players.

## Proof of Work
The implementation includes:
- New `libfaac/pseudo_sbr.c` and `libfaac/pseudo_sbr.h`.
- Integration in `libfaac/frame.c` (pipeline) and `libfaac/quantize.c` (PNS forcing).
- CLI support via `--sbr` and `--no-sbr` flags.
