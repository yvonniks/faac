#ifndef SBR_H
#define SBR_H

#include "faac_real.h"
#include "coder.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define SBR_MAX_BANDS 64

typedef struct {
    int sbrPresent;
    int headerSent;

    /* SBR Data */
    int numBands;
    int bands[SBR_MAX_BANDS];

    faac_real envelopes[MAX_CHANNELS][SBR_MAX_BANDS];
    int noise_floor[MAX_CHANNELS][SBR_MAX_BANDS];

} SBRInfo;

SBRInfo* SBRInit(int channels, int sampleRate, int bandWidth);
void SBREnd(SBRInfo *sbrInfo);
struct BitStream;
void SBRAnalysis(SBRInfo *sbrInfo, faac_real *input[MAX_CHANNELS], int numChannels);
int SBRWriteBitstream(SBRInfo *sbrInfo, struct BitStream *bitStream, int writeFlag);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SBR_H */
