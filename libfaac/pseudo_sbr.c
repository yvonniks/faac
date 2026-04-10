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

#include "pseudo_sbr.h"
#include "coder.h"
#include "faac_real.h"

/* -------------------------------------------------------------------------
 * Tuning constants
 * ---------------------------------------------------------------------- */
#define MAX_SBR_PATCHES   4
#define SBR_PATCH_ROLLOFF 0.50f   /* –6 dB gain per subsequent patch       */
#define SBR_FILL_THRESH   50u     /* disable if naturalBW > 50% of Nyquist */
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
static float compute_tonality(const faac_real *freq, int start, int end)
{
    float max_e = 0.0f;
    float sum_e = 0.0f;
    int n = end - start;
    int i;

    if (n <= 0)
        return 0.0f;

    for (i = start; i < end; i++) {
        float e = (float)(freq[i] * freq[i]);
        if (e > max_e) max_e = e;
        sum_e += e;
    }

    if (sum_e < 1e-15f)
        return 0.0f;

    float avg_e = sum_e / (float)n;
    float papr = max_e / avg_e;

    /* Normalize PAPR to a tonality score.
       PAPR = 1.0 for flat noise (n=1), PAPR = n for a single impulse.
       For n=64, a reasonable "tonal" threshold is around 10-15. */
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

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

int PseudoSBRShouldEnable(unsigned int naturalBW, unsigned long sampleRate,
                           unsigned long bitRate)
{
    unsigned int nyquist;

    if (!bitRate)          /* VBR: naturalBW already = Nyquist */
        return 0;
    if (!sampleRate)
        return 0;

    nyquist = (unsigned int)(sampleRate / 2);
    if (!nyquist)
        return 0;

    /* Enable only when natural bandwidth leaves a meaningful gap */
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

    /*
     * Extension fraction: how much of the gap to fill.
     * Derived from bitRate so it scales automatically as CalcBandwidth()
     * is re-tuned — no hard-coded Hz targets.
     */
    if      (bitRate <= 16000) frac = 0.45f;
    else if (bitRate <= 32000) frac = 0.35f;
    else if (bitRate <= 48000) frac = 0.25f;
    else if (bitRate <= 64000) frac = 0.15f;
    else                       frac = 0.0f;

    if (frac <= 0.0f)
        return naturalBW;

    target = naturalBW + (unsigned int)((float)gap * frac);

    /* Hard cap at 90% of Nyquist */
    cap = nyquist * 9u / 10u;
    return (target < cap) ? target : cap;
}

void PseudoSBR(CoderInfo *coderInfo, faac_real *freq,
               unsigned long sampleRate,
               unsigned int baseBW, unsigned int targetBW,
               unsigned long bitRate,
               const int *cb_width_long, int num_cb_long,
               uint32_t *randState)
{
    int bw_bin, tgt_bin;
    int papr_start;
    float tonality, noise_mix;
    float rolloff;
    int dst, sb, offset;
    int patch, p;

    (void)bitRate;

    /* Only extend long-window blocks; short blocks are transients */
    if (coderInfo->block_type != ONLY_LONG_WINDOW)
        return;

    bw_bin  = bw_to_bin(baseBW,   sampleRate, FRAME_LEN);
    tgt_bin = bw_to_bin(targetBW, sampleRate, FRAME_LEN);

    /* Clamp tgt_bin to the last valid scale-factor band end */
    if (num_cb_long > 0) {
        int max_bin = 0;
        for (sb = 0; sb < num_cb_long; sb++)
            max_bin += cb_width_long[sb];
        if (tgt_bin > max_bin)
            tgt_bin = max_bin;
    }

    /* Sanity: need at least MIN_PATCH_BINS of extension and a valid source */
    if (tgt_bin <= bw_bin + MIN_PATCH_BINS)
        return;
    if (bw_bin <= MIN_PATCH_BINS)
        return;

    /* ------------------------------------------------------------------
     * Tonality calculation using PAPR.
     * ------------------------------------------------------------------ */
    papr_start = bw_bin - SBR_PAPR_WINDOW;
    if (papr_start < 0) papr_start = 0;
    tonality = compute_tonality(freq, papr_start, bw_bin);
    noise_mix = SBR_NOISE_OFFSET + (1.0f - tonality) * SBR_NOISE_SLOPE;

    /* ------------------------------------------------------------------
     * Dynamic Spectral Tilt calculation.
     * Measure energy slope of the top 4 scale factor bands.
     * ------------------------------------------------------------------ */
    rolloff = SBR_PATCH_ROLLOFF;
    if (coderInfo->sfbn >= 8) {
        float e[4];
        int bands_found = 0;
        for (sb = coderInfo->sfbn - 1; sb >= 0 && bands_found < 4; sb--) {
            int start = coderInfo->sfb_offset[sb];
            int end = coderInfo->sfb_offset[sb+1];
            float sum = 0.0f;
            for (p = start; p < end; p++)
                sum += (float)(freq[p] * freq[p]);
            e[bands_found++] = sum / (float)(end - start);
        }
        if (bands_found >= 2) {
            float total_ratio = 0.0f;
            for (p = 0; p < bands_found - 1; p++) {
                if (e[p+1] > 1e-10f)
                    total_ratio += e[p] / e[p+1];
                else
                    total_ratio += 1.0f;
            }
            float avg_ratio = total_ratio / (float)(bands_found - 1);
            rolloff = sqrtf(avg_ratio);
            /* Clamp rolloff to sensible range (-12dB to 0dB) */
            if (rolloff > 1.0f) rolloff = 1.0f;
            if (rolloff < 0.25f) rolloff = 0.25f;
        }
    }

    /* ------------------------------------------------------------------
     * Patch loop: fill [bw_bin, tgt_bin) with copies of the source region.
     * ------------------------------------------------------------------ */
    float cumulative_gain = 1.0f;
    dst = bw_bin;

    for (patch = 0; patch < MAX_SBR_PATCHES && dst < tgt_bin; patch++) {
        int remaining   = tgt_bin - dst;
        /* Use a fixed patch size for consistent transposition, but cap by available bandwidth.
           A size of 128 bins (approx 2.7kHz at 44.1kHz) matches AAC spectral structures. */
        int patch_size  = 128;
        if (patch_size > bw_bin) patch_size = bw_bin;
        if (patch_size > remaining) patch_size = remaining;
        if (patch_size < MIN_PATCH_BINS) patch_size = remaining;

        /* Transpose source window downward for each subsequent patch to avoid comb filtering. */
        int src_start = bw_bin - patch_size * (patch + 1);
        if (src_start < 0) src_start = 0;

        float src_energy = 0.0f;
        if (patch_size < MIN_PATCH_BINS) break;

        for (p = 0; p < patch_size; p++)
            src_energy += (float)(freq[src_start + p] * freq[src_start + p]);

        /* Correct noise scaling: noise_mix is desired energy ratio of noise to total.
           noise_energy = patch_energy * noise_mix / (1 - noise_mix).
           Our uniform PRNG has variance 1/12. noise_scale^2 / 12 = noise_energy_density. */
        float noise_scale = (src_energy > 1e-15f && noise_mix < 0.99f)
            ? sqrtf(12.0f * (src_energy / (float)patch_size) * (noise_mix / (1.0f - noise_mix)))
            : 0.0f;

        for (p = 0; p < patch_size; p++) {
            float s = (float)freq[src_start + p] * cumulative_gain;
            /* Noise floor should also follow the rolloff */
            float n = lcg_float(randState) * noise_scale * cumulative_gain;
            freq[dst + p] = (faac_real)(s + n);
        }

        dst             += patch_size;
        cumulative_gain *= rolloff;
    }

    if (dst < tgt_bin)
        memset(freq + dst, 0, (size_t)(tgt_bin - dst) * sizeof(faac_real));

    /* ------------------------------------------------------------------
     * Extend sfbn and sfb_offset[] to cover tgt_bin.
     * ------------------------------------------------------------------ */
    sb     = coderInfo->sfbn;
    offset = coderInfo->sfb_offset[sb];

    while (sb < num_cb_long && sb < NSFB_LONG && offset < tgt_bin) {
        coderInfo->sfb_offset[sb + 1] = offset + cb_width_long[sb];
        offset = coderInfo->sfb_offset[sb + 1];
        sb++;
    }

    coderInfo->sfbn = sb;
}
