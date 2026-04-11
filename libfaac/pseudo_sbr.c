/*
 * FAAC - Freeware Advanced Audio Coder
 *
 * pseudo_sbr.c - Encoder-side Pseudo Spectral Band Replication for AAC-LC
 *
 * Algorithm overview:
 *   1. Measure Peak-to-Average Power Ratio (PAPR) of the top coded region to
 *      drive adaptive noise injection (tonal → less noise, noisy → more).
 *   2. Copy spectral patches from the coded region into the empty region
 *      above it, applying a cumulative gain rolloff and per-bin noise dithering.
 *   3. To avoid comb filtering, source windows are transposed downward for
 *      each subsequent patch.
 *   4. Extend coderInfo->sfbn and sfb_offset[] so BlocQuant will quantize
 *      the synthesised bins.
 *
 * The PRNG (LCG) is seeded once at encoder open from the sample rate, so
 * re-encoding the same file produces a bit-identical bitstream (stable MD5).
 */

#include <math.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#include "pseudo_sbr.h"
#include "coder.h"
#include "faac_real.h"

/* -------------------------------------------------------------------------
 * Tuning constants
 * ---------------------------------------------------------------------- */
#define MAX_SBR_PATCHES   4
#define SBR_PATCH_ROLLOFF 0.50f   /* –6 dB gain per subsequent patch       */
#define SBR_FILL_THRESH   100u    /* disable if naturalBW >= 100% of Nyquist */
#define SBR_NOISE_OFFSET  0.05f   /* minimum noise injection fraction       */
#define SBR_NOISE_SLOPE   0.20f   /* noise scales by (1-tonality) * slope   */
#define SBR_PAPR_WINDOW   64      /* bins below bw_bin used for PAPR        */
#define MIN_PATCH_BINS    8       /* don't bother with very small patches   */

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */

/* LCG PRNG — deterministic, fast, 0-overhead state is caller's uint32_t. */
static float lcg_float(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    /* Map high 16 bits to [-0.5, 0.5) */
    return (float)(*state >> 16) / 65536.0f - 0.5f;
}

/*
 * Tonality heuristic using Peak-to-Average Power Ratio (PAPR).
 * Returns a value in [0, 1]: 0.0 = pure noise, 1.0 = highly tonal.
 */
static float compute_tonality(const faac_real *freq, int start, int end, int stride)
{
    float max_e = 0.0f;
    float sum_e = 0.0f;
    int n = end - start;
    int i;

    if (n <= 0)
        return 0.0f;

    for (i = 0; i < n; i++) {
        float val = (float)freq[(start + i) * stride];
        float e = val * val;
        if (e > max_e) max_e = e;
        sum_e += e;
    }

    if (sum_e < 1e-15f)
        return 0.0f;

    float avg_e = sum_e / (float)n;
    float papr = max_e / avg_e;

    /* Normalize PAPR to a tonality score. */
    float score = (papr - 1.0f) / 15.0f;
    if (score > 1.0f) score = 1.0f;
    if (score < 0.0f) score = 0.0f;

    return score;
}

/* Convert a bandwidth in Hz to an MDCT bin index. */
static int bw_to_bin(unsigned int bw_hz, unsigned long sampleRate, int frame_len)
{
    /* bin = bw_hz * frame_len / (sampleRate/2) = bw_hz * 2 * frame_len / sampleRate */
    return (int)((unsigned long long)bw_hz * 2ULL * (unsigned long)frame_len / sampleRate);
}

/* Internal implementation for one window, handles both interleaved and flat layouts */
static void ApplyPseudoSBR(faac_real *freq, int bw_bin, int tgt_bin, int stride, uint32_t *randState)
{
    int papr_start;
    float tonality, noise_mix;
    float cumulative_gain = 1.0f;
    int dst = bw_bin;
    int patch, p;

    papr_start = bw_bin - SBR_PAPR_WINDOW;
    if (papr_start < 0) papr_start = 0;
    tonality = compute_tonality(freq, papr_start, bw_bin, stride);
    noise_mix = SBR_NOISE_OFFSET + (1.0f - tonality) * SBR_NOISE_SLOPE;

    for (patch = 0; patch < MAX_SBR_PATCHES && dst < tgt_bin; patch++) {
        int remaining   = tgt_bin - dst;
        int patch_size  = 128;
        if (patch_size > bw_bin) patch_size = bw_bin;
        if (patch_size > remaining) patch_size = remaining;

        int src_start = bw_bin - patch_size * (patch + 1);
        if (src_start < 0) src_start = 0;

        float src_energy = 0.0f;
        if (patch_size < 1) break;

        for (p = 0; p < patch_size; p++) {
            float val = (float)freq[(src_start + p) * stride];
            src_energy += val * val;
        }

        float noise_scale = (src_energy > 1e-15f && noise_mix < 0.99f)
            ? sqrtf(12.0f * (src_energy / (float)patch_size) * (noise_mix / (1.0f - noise_mix)))
            : 0.0f;

        for (p = 0; p < patch_size; p++) {
            float s = (float)freq[(src_start + p) * stride] * cumulative_gain;
            float n = lcg_float(randState) * noise_scale * cumulative_gain;
            freq[(dst + p) * stride] = (faac_real)(s + n);
        }

        dst             += patch_size;
        cumulative_gain *= SBR_PATCH_ROLLOFF;
    }

    if (dst < tgt_bin) {
        for (p = dst; p < tgt_bin; p++)
            freq[p * stride] = 0.0;
    }
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

int PseudoSBRShouldEnable(unsigned int naturalBW, unsigned long sampleRate,
                           unsigned long bitRate)
{
    unsigned int nyquist;
    (void)bitRate;

    if (!sampleRate)
        return 0;

    nyquist = (unsigned int)(sampleRate / 2);
    if (!nyquist)
        return 0;

    /* Enable only when natural bandwidth leaves a gap */
    return (naturalBW * 100u / nyquist) < SBR_FILL_THRESH;
}

unsigned int PseudoSBRTargetBW(unsigned int naturalBW, unsigned long sampleRate,
                                unsigned long bitRate)
{
    unsigned int nyquist = (unsigned int)(sampleRate / 2);
    unsigned int gap;
    float frac;
    unsigned int target;
    unsigned int cap;

    if (!nyquist || naturalBW >= nyquist)
        return naturalBW;

    gap = nyquist - naturalBW;

    if      (bitRate == 0)      frac = 0.50f; /* VBR fallback */
    else if (bitRate <= 16000)  frac = 0.65f;
    else if (bitRate <= 32000)  frac = 0.55f;
    else if (bitRate <= 48000)  frac = 0.50f;
    else if (bitRate <= 64000)  frac = 0.35f;
    else if (bitRate <= 96000)  frac = 0.25f;
    else if (bitRate <= 128000) frac = 0.15f;
    else if (bitRate <= 160000) frac = 0.10f;
    else                        frac = 0.0f;

    if (frac <= 0.0f)
        return naturalBW;

    target = naturalBW + (unsigned int)((float)gap * frac);

    /* Hard cap at 95% of Nyquist */
    cap = nyquist * 95u / 100u;
    return (target < cap) ? target : cap;
}

void PseudoSBR(CoderInfo *coderInfo, faac_real *freq,
               unsigned long sampleRate,
               unsigned int baseBW, unsigned int targetBW,
               unsigned long bitRate,
               SR_INFO *srInfo,
               uint32_t *randState)
{
    int bw_bin, tgt_bin;
    int sb, offset;
    int win;

    (void)bitRate;

    if (coderInfo->block_type == ONLY_SHORT_WINDOW) {
        bw_bin  = bw_to_bin(baseBW,   sampleRate, BLOCK_LEN_SHORT);
        tgt_bin = bw_to_bin(targetBW, sampleRate, BLOCK_LEN_SHORT);

        if (tgt_bin > BLOCK_LEN_SHORT) tgt_bin = BLOCK_LEN_SHORT;

        if (tgt_bin <= bw_bin) return;

        /* Short blocks in freqBuff are stored window by window: [win0][win1]...
           So stride is 1 for each window. */
        for (win = 0; win < 8; win++) {
            ApplyPseudoSBR(freq + win * BLOCK_LEN_SHORT, bw_bin, tgt_bin, 1, randState);
        }

        /* Extend sfbn and sfb_offset[] for short blocks */
        sb     = coderInfo->sfbn;
        offset = coderInfo->sfb_offset[sb];

        while (sb < srInfo->num_cb_short && sb < NSFB_SHORT && offset < tgt_bin) {
            coderInfo->sfb_offset[sb + 1] = offset + srInfo->cb_width_short[sb];
            offset = coderInfo->sfb_offset[sb + 1];
            sb++;
        }
        coderInfo->sfbn = sb;
    } else {
        bw_bin  = bw_to_bin(baseBW,   sampleRate, BLOCK_LEN_LONG);
        tgt_bin = bw_to_bin(targetBW, sampleRate, BLOCK_LEN_LONG);

        /* Clamp tgt_bin to the last valid scale-factor band end */
        if (srInfo->num_cb_long > 0) {
            int max_bin = 0;
            for (sb = 0; sb < srInfo->num_cb_long; sb++)
                max_bin += srInfo->cb_width_long[sb];
            if (tgt_bin > max_bin) tgt_bin = max_bin;
        }

        if (tgt_bin <= bw_bin) return;

        ApplyPseudoSBR(freq, bw_bin, tgt_bin, 1, randState);

        /* ------------------------------------------------------------------
         * Extend sfbn and sfb_offset[] to cover tgt_bin.
         * ------------------------------------------------------------------ */
        sb     = coderInfo->sfbn;
        offset = coderInfo->sfb_offset[sb];

        while (sb < srInfo->num_cb_long && sb < NSFB_LONG && offset < tgt_bin) {
            coderInfo->sfb_offset[sb + 1] = offset + srInfo->cb_width_long[sb];
            offset = coderInfo->sfb_offset[sb + 1];
            sb++;
        }

        coderInfo->sfbn = sb;
    }
}
