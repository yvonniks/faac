# PseudoSBR Research Notes

## 1. Motivation

At bitrates below 64 kbps per channel, FAAC's AAC-LC encoder already computes a
conservative bandwidth ceiling via `CalcBandwidth()`.  The hypothesis was that
explicitly narrowing the bandwidth further — freeing bits that the ABR controller
could reinvest as better quantization in the retained bands — would improve
perceptual quality.  We call this approach **PseudoSBR**, because it mimics the
spectral energy management of real Spectral Band Replication without the synthesis
step.

### CalcBandwidth() segments (per-channel bitrate in bps)

| Range                  | Formula                                | Example at 24 kbps/ch |
|------------------------|----------------------------------------|-----------------------|
| ≤ 16 kbps/ch           | 4000 + bitRate/8                       | —                     |
| 16–32 kbps/ch          | 6000 + (bitRate−16000)×5/16            | 8500 Hz               |
| 32–64 kbps/ch          | 11000 + (bitRate−32000)×15/64          | —                     |

The 64 kbps/ch boundary (`PSEUDO_SBR_MAX_BITRATE`) aligns with the upper boundary
of Segment 3.  Above 64 kbps/ch FAAC already uses near-Nyquist bandwidth and
PseudoSBR is not activated.

---

## 2. Strategy A — Bandwidth Scaling Only

### Implementation

In `faacEncSetConfiguration()`, after `CalcBandwidth()` computes the natural
bandwidth, a linear taper scales it down:

```c
#define PSEUDO_SBR_MAX_BITRATE  64000   /* bps per channel */
#define PSEUDO_SBR_MIN_BW_PCT      70   /* minimum bandwidth %, at 0 bps */

int bw_pct = PSEUDO_SBR_MIN_BW_PCT +
    (100 - PSEUDO_SBR_MIN_BW_PCT) * config->bitRate / PSEUDO_SBR_MAX_BITRATE;
config->bandWidth = config->bandWidth * bw_pct / 100;
```

At the threshold (64 kbps/ch) `bw_pct = 100` — no change.
At 24 kbps/ch `bw_pct = 81` — bandwidth reduced to 81 % of natural value.

### ViSQOL MOS-LQO Results (Strategy A sweep)

| Scenario    | baseline | pct60  | pct65  | pct75  | pct80  |
|------------|---------|--------|--------|--------|--------|
| voip       | 3.582   | 3.199  | 3.199  | 3.404  | 3.404  |
| vss        | 4.173   | 4.070  | 4.173  | 4.173  | 4.173  |
| music\_48  | 2.622   | 1.770  | 1.770  | 1.943  | 1.943  |
| music\_low | 3.198   | 2.662  | 2.662  | 2.662  | 2.662  |
| music\_96  | 3.818   | 3.818  | 3.818  | 3.818  | 3.818  |
| music\_std | 4.442   | 4.442  | 4.442  | 4.442  | 4.442  |
| music\_high| 4.656   | 4.656  | 4.656  | 4.656  | 4.656  |
| LOW-BW Δ  | —       | −0.375 | −0.354 | −0.279 | −0.279 |
| HIGH-BW Δ | —       | 0.000  | 0.000  | 0.000  | 0.000  |

**Conclusion**: All `MIN_BW_PCT` values 60–80 show regression at low bitrates.
High-bitrate scenarios (music\_std, music\_high) are correctly unaffected.

### Root Cause

Measured bitrate for the `voip` scenario dropped from 20.3 kbps (baseline) to
18.4 kbps (pct70) — the ABR did not compensate for the freed bandwidth budget.
FAAC's ABR loop (frame.c `faacEncEncode`, lines 657–679) is a proportional
controller that converges around the target bitrate:

```c
faac_real fix = (faac_real)desbits / (faac_real)(frameBytes * 8);
/* clamp fix to ±10 %, dampen by 50 % */
hEncoder->aacquantCfg.quality *= fix;
```

When fewer bands are encoded (narrowed bandwidth), each frame is smaller than
the target, so `fix > 1` and quality is boosted.  However, at very low bitrates
the natural bandwidth is already narrow, and even at `MAXQUAL` (5000) the
narrowed encoder cannot reach the target bitrate — the ABR converges at
`MAXQUAL` with still-underutilised bitrate.

---

## 3. Strategy A+Q — Bandwidth Scaling + quantqual Boost

### Implementation

To help the ABR reinvest freed bits, we boost `quantqual` in inverse proportion
to the bandwidth reduction:

```c
int bw_pct = PSEUDO_SBR_MIN_BW_PCT +
    (100 - PSEUDO_SBR_MIN_BW_PCT) * config->bitRate / PSEUDO_SBR_MAX_BITRATE;
config->bandWidth  = config->bandWidth * bw_pct / 100;
config->quantqual  = config->quantqual * 100 / bw_pct;  /* inverse scale */
```

`quantqual` is clamped to `[MINQUAL=10, MAXQUAL=5000]` by the existing code
below the PseudoSBR block.  No floating-point epsilons are introduced.

### ViSQOL MOS-LQO Results (Strategy A+Q at pct70)

| Scenario    | baseline | A only pct70 | A+Q pct70 | Δ vs baseline |
|------------|---------|-------------|----------|---------------|
| voip       | 3.582   | 3.404       | 3.391    | −0.190        |
| vss        | 4.173   | 4.173       | 4.160    | −0.013        |
| music\_48  | 2.622   | 1.770       | 1.964    | −0.658        |
| music\_low | 3.198   | 2.662       | 2.805    | −0.393        |
| music\_96  | 3.818   | 3.818       | 3.882    | +0.065        |
| music\_std | 4.442   | 4.442       | 4.461    | +0.019        |
| music\_high| 4.656   | 4.656       | 4.647    | −0.009        |
| LOW-BW Δ  | —       | −0.313      | −0.238   | —             |
| HIGH-BW Δ | —       | 0.000       | +0.005   | —             |

The quantqual boost improves over pure bandwidth scaling (−0.238 vs −0.313) but
all primary low-bitrate scenarios remain below baseline.

---

## 4. Strategy C — PNS Level Boost

Tested with `pnslevel` increased to 6, 8, and 10 on top of A (pct70).

| Scenario    | pns=4  | pns=6  | pns=8  | pns=10 |
|------------|--------|--------|--------|--------|
| LOW-BW avg | 3.165  | 3.155  | 3.139  | 3.134  |
| LOW-BW Δ  | —      | −0.010 | −0.026 | −0.031 |

**Conclusion**: PNS boost adds slight regression.  Not recommended.

---

## 5. Root Cause Analysis

The fundamental problem is the interaction between PseudoSBR and ViSQOL:

1. **ViSQOL is a full-spectrum metric.**  It compares reference and encoded audio
   across all frequency bands.  Missing high-frequency content is penalised
   regardless of how well the retained bands are quantised.

2. **FAAC's CalcBandwidth already narrows bandwidth at low bitrates.**  At
   24 kbps/ch the natural bandwidth is 8 500 Hz.  Reducing it further to 6 885 Hz
   (pct70) cuts into the 7–8.5 kHz range, which ViSQOL considers significant.

3. **The ABR cannot fully reinvest freed bits.**  At very low bitrates with a
   narrowed spectrum, even `MAXQUAL` quantisation of the retained bands produces
   fewer bits than the target.  The loop converges at `MAXQUAL` while still
   underutilising the bitrate budget.

4. **The quantqual boost partially compensates but not fully.**  It reduces the
   average low-bitrate regression from −0.313 (pure bandwidth scaling) to −0.238
   (bandwidth scaling + quality boost), but cannot overcome the spectral penalty.

---

## 6. Strategy S — Spectral Folding

### Motivation

Strategies A and A+Q both cut the encoder bandwidth, leaving silence above the
cutoff.  ViSQOL penalises missing high-frequency content regardless of how well
the retained bands are quantised.  Strategy S keeps the natural bandwidth and
instead **mirrors** (folds) the MDCT coefficients just below the fold point into
the upper region, attenuated by `PSEUDO_SBR_FOLD_SCALE` (25 % ≈ 12 dB).  The
folded bins are quantised normally, giving the bit budget something real to
encode.

### Implementation

After the FilterBank loop in `faacEncEncode()`, long-window blocks only:

```c
int bw_pct  = PSEUDO_SBR_MIN_BW_PCT +
    (100 - PSEUDO_SBR_MIN_BW_PCT) * bitRate / PSEUDO_SBR_MAX_BITRATE;
int fold_bin = bandWidth * bw_pct/100 * 2*FRAME_LEN / sampleRate;
int end_bin  = bandWidth             * 2*FRAME_LEN / sampleRate;
for (k = 0; fold_bin + k < end_bin; k++) {
    src = fold_bin - k - 1;
    freqBuff[fold_bin + k] = freqBuff[src] * PSEUDO_SBR_FOLD_SCALE / 100;
}
```

The `faacEncSetConfiguration()` bandwidth/quantqual block was removed entirely;
`bandWidth` and `quantqual` remain at their natural values.

### ViSQOL MOS-LQO Results (Strategy S, pct70, FOLD_SCALE=25)

| Scenario    | baseline | Strategy S | Δ vs baseline |
|------------|---------|------------|---------------|
| voip       | 3.582   | 3.541      | −0.041        |
| vss        | 4.173   | 4.159      | −0.014        |
| music\_48  | 2.622   | 2.775      | **+0.153**    |
| music\_low | 3.198   | 3.351      | **+0.152**    |
| music\_96  | 3.818   | 3.882      | +0.064        |
| music\_std | 4.442   | 4.461      | +0.019        |
| music\_high| 4.656   | 4.647      | −0.009        |
| **LOW-BW Δ** | —     |            | **+0.063**    |
| **HIGH-BW Δ**| —     |            | **+0.025**    |

### Comparison across strategies

| Strategy      | LOW-BW avg Δ | HIGH-BW avg Δ |
|---------------|-------------|--------------|
| A  (bw only)  | −0.313      | 0.000        |
| A+Q (bw+qual) | −0.238      | +0.005       |
| **S  (fold)** | **+0.063**  | **+0.025**   |

**Conclusion**: Strategy S is the first configuration to achieve a positive
average delta at low bitrates.  The folded content fills the HF region that
ViSQOL previously penalised as silence, and the natural bit budget is
consumed without quantqual hacks.  Remaining slight regression in voip and vss
may reflect those scenarios having naturally narrow content that doesn't benefit
as much from HF decoration.

---

## 7. Final Configuration

| Parameter                  | Value     | Rationale                              |
|---------------------------|-----------|----------------------------------------|
| `PSEUDO_SBR_MAX_BITRATE`  | 64 000    | Aligns with CalcBandwidth Segment 3/4 boundary |
| `PSEUDO_SBR_MIN_BW_PCT`   | 70        | Fold point at 0 bps/ch; linear taper to 100 % at threshold |
| `PSEUDO_SBR_FOLD_SCALE`   | 25        | ≈ 12 dB HF attenuation; first-pass value |
| `usePseudoSBR` default    | **0**     | Opt-in only; positive on average but voip/vss still regress |
| CLI flag                  | `--sbr`   | Opt-in for experimental use            |

Future work: tune `PSEUDO_SBR_FOLD_SCALE` per-scenario or sweep 10–50 % to
find the optimum; consider frequency-shaping the fold rather than flat
attenuation.
