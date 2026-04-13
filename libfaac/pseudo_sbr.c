#include "pseudo_sbr.h"
#include "faac_real.h"
#include <string.h>

static float sbr_rand(unsigned int *seed) {
    *seed = *seed * 1103515245 + 12345;
    return (float)(((*seed >> 16) & 0x7FFF)) / 32768.0f;
}

void PseudoSBR(faacEncStruct *hEncoder, CoderInfo *coderInfo, faac_real *freqBuff) {
    int sfb, w;
    int start_sfb, num_sfb;
    int bins_per_window;
    int num_windows;
    int *sfb_offset = coderInfo->sfb_offset;

    if (hEncoder->sampleRate != 48000)
        return;

    if (hEncoder->config.bandWidth >= (hEncoder->sampleRate / 2))
        return;

    if (coderInfo->block_type == ONLY_SHORT_WINDOW) {
        start_sfb = coderInfo->sfbn_native;
        num_sfb = hEncoder->srInfo->num_cb_short;
        bins_per_window = BLOCK_LEN_SHORT;
        num_windows = MAX_SHORT_WINDOWS;
    } else {
        start_sfb = coderInfo->sfbn_native;
        num_sfb = hEncoder->srInfo->num_cb_long;
        bins_per_window = BLOCK_LEN_LONG;
        num_windows = 1;
    }

    int bw_bin = sfb_offset[start_sfb];
    if (bw_bin <= 0) return;

    for (w = 0; w < num_windows; w++) {
        faac_real *xr = freqBuff + w * bins_per_window;

        for (sfb = start_sfb; sfb < num_sfb; sfb++) {
            int sfb_start = sfb_offset[sfb];
            int sfb_end = sfb_offset[sfb + 1];
            int patch_size = sfb_end - sfb_start;
            int patch = sfb - start_sfb;

            int src_range = bw_bin - patch_size;
            int src_start;

            if (src_range <= 0) {
                src_start = 0;
            } else {
                src_start = bw_bin - patch_size * (patch + 1);
                while (src_start < 0) src_start += src_range;
                src_start %= src_range;
            }

            faac_real enrg = 0;
            faac_real peak = 0;
            int i;
            for (i = 0; i < patch_size; i++) {
                faac_real val = xr[src_start + i];
                faac_real e = val * val;
                enrg += e;
                if (e > peak) peak = e;
            }

            faac_real avg_enrg = enrg / (faac_real)patch_size;
            faac_real papr = (avg_enrg > 1e-6) ? (peak / avg_enrg) : 1.0;

            for (i = 0; i < patch_size; i++) {
                xr[sfb_start + i] = xr[src_start + i] * (faac_real)0.90;
            }

            faac_real noise_fact = (faac_real)0.15;
            if (papr > (faac_real)12.0) noise_fact = (faac_real)0.03;
            else if (papr > (faac_real)6.0) noise_fact = (faac_real)0.08;

            faac_real noise_gain = FAAC_SQRT(avg_enrg) * noise_fact;

            for (i = 0; i < patch_size; i++) {
                xr[sfb_start + i] += (faac_real)(noise_gain * (sbr_rand(&hEncoder->sbrRandState) - 0.5f));
            }
        }
    }
}
