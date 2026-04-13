#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "resample.h"
#include "util.h"

/* 2:1 Downsampler using a 31-tap FIR filter */
static const faac_real filter_coeffs[31] = {
    -0.000454, -0.000840, 0.001091, 0.005047, 0.005831, -0.004118, -0.019567, -0.021575,
    0.010188, 0.059292, 0.091185, 0.063520, -0.038165, -0.170566, -0.264663, 0.767425,
    -0.264663, -0.170566, -0.038165, 0.063520, 0.091185, 0.059292, 0.010188, -0.021575,
    -0.019567, -0.004118, 0.005831, 0.005047, 0.001091, -0.000840, -0.000454
};

Resampler* ResampleOpen(int channels)
{
    Resampler *resampler = (Resampler *)AllocMemory(sizeof(Resampler));
    resampler->channels = channels;
    resampler->buffer_len = 31;
    resampler->buffer = (faac_real *)AllocMemory(channels * resampler->buffer_len * sizeof(faac_real));
    memset(resampler->buffer, 0, channels * resampler->buffer_len * sizeof(faac_real));
    return resampler;
}

void ResampleClose(Resampler *resampler)
{
    if (resampler) {
        if (resampler->buffer) {
            FreeMemory(resampler->buffer);
        }
        FreeMemory(resampler);
    }
}

int Resample2to1(Resampler *resampler, faac_real *input[], int input_len, faac_real *output[])
{
    int ch, i, j;
    int output_len = input_len; /* input_len is per channel */

    for (ch = 0; ch < resampler->channels; ch++) {
        faac_real *ch_input = input[ch];
        faac_real *ch_output = output[ch];
        faac_real *ch_buffer = resampler->buffer + ch * resampler->buffer_len;

        for (i = 0; i < output_len; i++) {
            faac_real sum = 0;
            for (j = 0; j < 31; j++) {
                int idx = 2 * i - j;
                faac_real val;
                if (idx >= 0) {
                    val = ch_input[idx];
                } else {
                    val = ch_buffer[resampler->buffer_len + idx];
                }
                sum += val * filter_coeffs[j];
            }
            ch_output[i] = sum;
        }

        /* Update buffer with the last samples for the next frame */
        if (input_len >= resampler->buffer_len) {
            memcpy(ch_buffer, ch_input + input_len - resampler->buffer_len, resampler->buffer_len * sizeof(faac_real));
        } else {
            /* This case shouldn't happen with FRAME_LEN=1024 */
            memmove(ch_buffer, ch_buffer + input_len, (resampler->buffer_len - input_len) * sizeof(faac_real));
            memcpy(ch_buffer + resampler->buffer_len - input_len, ch_input, input_len * sizeof(faac_real));
        }
    }

    return output_len;
}
