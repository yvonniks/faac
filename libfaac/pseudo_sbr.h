#ifndef PSEUDO_SBR_H
#define PSEUDO_SBR_H

#include "coder.h"
#include "frame.h"

#ifdef __cplusplus
extern "C" {
#endif

void PseudoSBR(faacEncStruct *hEncoder, CoderInfo *coderInfo, faac_real *freqBuff);

#ifdef __cplusplus
}
#endif

#endif /* PSEUDO_SBR_H */
