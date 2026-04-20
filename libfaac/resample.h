#ifndef RESAMPLE_H
#define RESAMPLE_H

#include "faac_real.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct {
    faac_real *buffer;
    int buffer_len;
    int channels;
} Resampler;

Resampler* ResampleOpen(int channels);
void ResampleClose(Resampler *resampler);
int Resample2to1(Resampler *resampler, faac_real *input[], int input_len, faac_real *output[]);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* RESAMPLE_H */
