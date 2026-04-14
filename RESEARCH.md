# Pseudo SBR Investigation for AAC-LC (48 kHz)

## Overview
This research investigates the implementation of a "Pseudo" SBR (Spectral Band Replication) technique for the FAAC encoder, specifically targeting low-bitrate music scenarios (24, 32, 40, 48 kbps per channel) at a 48 kHz sample rate.

Pseudo SBR synthesized high-frequency content by patching lower frequency bands into the higher spectrum and applying adaptive noise injection, combined with Perceptual Noise Substitution (PNS) to maintain bitstream compatibility with standard AAC-LC decoders.

## Methodology
1. **Natural Bandwidth Reduction**: To save bits for the base layer, the natural encoding bandwidth was adaptively reduced by 5-25% depending on the bitrate.
2. **Spectral Patching**: Source bins were copied from the base layer to the SBR region using transposed source windows to avoid comb-filtering artifacts.
3. **Adaptive Noise Injection**: Noise was added to the synthesized bands based on the Peak-to-Average Power Ratio (PAPR) of the source material, preserving tonal vs. noisy characteristics.
4. **PNS Integration**: All synthesized SBR bands were forced to use PNS (Perceptual Noise Substitution) to ensure zero additional bitrate overhead for the spectral shape while providing a plausible high-frequency texture.

## Results (MOS Delta)
Evaluated on `Coral.16b48k.wav` using ViSQOL (48kHz audio mode):

| Bitrate (kbps/ch) | Baseline MOS | SBR MOS | Delta |
|-------------------|--------------|---------|-------|
| 24                | 1.729        | 2.435   | +0.706|
| 32                | 1.840        | 2.150   | +0.310|
| 40                | 2.346        | 2.450   | +0.104|
| 48                | 2.265        | 2.304   | +0.039|

*Note: 24 kbps/ch (48 kbps Stereo) shows the most significant lift as the base layer benefits most from the bandwidth reduction and SBR texture filling.*

## Magic Numbers
* **SBR Synthesis Scale**: 0.90 (90% amplitude preservation of patched bins).
* **Noise Injection Factors**:
    * Baseline: 0.15
    * Tonal (PAPR > 12): 0.03
    * Mid (PAPR > 6): 0.08
* **Bandwidth Scaling**:
    * < 32 kbps/ch: 0.75x
    * < 48 kbps/ch: 0.85x
    * >= 48 kbps/ch: 0.90x

## Conclusion
Pseudo SBR is highly effective for AAC-LC at bitrates below 40 kbps/channel at 48 kHz. It allows the encoder to focus bits on the critical low-frequency range while maintaining a subjectively brighter and more "complete" soundstage using standard-compliant PNS elements.
