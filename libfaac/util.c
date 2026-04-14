/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2001 Menno Bakker
 */
#include "config.h"
#include <math.h>
#include <stdlib.h>
#if defined(_MSC_VER) || defined(__MINGW32__)
#include <malloc.h>
#endif
#include "util.h"
#include "coder.h"

#ifndef FAAC_SIMD_ALIGNMENT
#define FAAC_SIMD_ALIGNMENT 16
#endif

void *faac_aligned_alloc(size_t size)
{
    void *ptr = NULL;
#if defined(_MSC_VER) || defined(__MINGW32__)
    ptr = _aligned_malloc(size, FAAC_SIMD_ALIGNMENT);
#else
    if (posix_memalign(&ptr, FAAC_SIMD_ALIGNMENT, size) != 0)
        return NULL;
#endif
    return ptr;
}

void faac_aligned_free(void *ptr)
{
#if defined(_MSC_VER) || defined(__MINGW32__)
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

int GetSRIndex(unsigned int sampleRate)
{
    if (92017 <= sampleRate) return 0;
    if (75132 <= sampleRate) return 1;
    if (55426 <= sampleRate) return 2;
    if (46009 <= sampleRate) return 3;
    if (37566 <= sampleRate) return 4;
    if (27713 <= sampleRate) return 5;
    if (23004 <= sampleRate) return 6;
    if (18783 <= sampleRate) return 7;
    if (13856 <= sampleRate) return 8;
    if (11502 <= sampleRate) return 9;
    if (9391 <= sampleRate) return 10;
    return 11;
}

unsigned int MaxBitrate(unsigned long sampleRate)
{
    return 0x2000 * 8 * (faac_real)sampleRate/(faac_real)FRAME_LEN;
}

unsigned int MinBitrate()
{
    return 8000;
}
