/**********************************************************************

This software module was originally developed by Texas Instruments
and edited by         in the course of
development of the MPEG-2 NBC/MPEG-4 Audio standard
ISO/IEC 13818-7, 14496-1,2 and 3. This software module is an
implementation of a part of one or more MPEG-2 NBC/MPEG-4 Audio tools
as specified by the MPEG-2 NBC/MPEG-4 Audio standard. ISO/IEC gives
users of the MPEG-2 NBC/MPEG-4 Audio standards free license to this
software module or modifications thereof for use in hardware or
software products claiming conformance to the MPEG-2 NBC/ MPEG-4 Audio
standards. Those intending to use this software module in hardware or
software products are advised that this use may infringe existing
patents. The original developer of this software module and his/her
company, the subsequent editors and their companies, and ISO/IEC have
no liability for use of this software module or modifications thereof
in an implementation. Copyright is not released for non MPEG-2
NBC/MPEG-4 Audio conforming products. The original developer retains
full right to use the code for his/her own purpose, assign or donate
the code to a third party and to inhibit third party from using the
code for non MPEG-2 NBC/MPEG-4 Audio conforming products. This
copyright notice must be included in all copies or derivative works.

Copyright (c) 1997.
**********************************************************************/
/*
 * $Id: tns.c,v 1.11 2012/03/01 18:34:17 knik Exp $
 */

#include <math.h>
#include "frame.h"
#include "coder.h"
#include "bitstream.h"
#include "tns.h"
#include "util.h"

/***********************************************/
/* TNS Bitrate and Gain Thresholds             */
/***********************************************/
#define TNS_BR_LOW        64
#define TNS_BR_MID        96
#define TNS_BR_HIGH       128

/* Auto-mode thresholds */
#define TNS_GAIN_AUTO_SHORT  1.1
#define TNS_GAIN_AUTO_LONG   2.0
#define TNS_GAIN_AUTO_HIGH   1.4
#define TNS_GAIN_INITIAL     1.4

/***********************************************/
/* TNS Profile/Frequency Dependent Parameters  */
/***********************************************/
/* Limit bands to > 2.0 kHz */
static unsigned short tnsMinBandNumberLong[12] =
{ 11, 12, 15, 16, 17, 20, 25, 26, 24, 28, 30, 31 };
static unsigned short tnsMinBandNumberShort[12] =
{ 2, 2, 2, 3, 3, 4, 6, 6, 8, 10, 10, 12 };

/**************************************/
/* Main/Low Profile TNS Parameters    */
/**************************************/
static unsigned short tnsMaxBandsLongMainLow[12] =
{ 31, 31, 34, 40, 42, 51, 46, 46, 42, 42, 42, 39 };

static unsigned short tnsMaxBandsShortMainLow[12] =
{ 9, 9, 10, 14, 14, 14, 14, 14, 14, 14, 14, 14 };

static unsigned short tnsMaxOrderLongMain = 20;
static unsigned short tnsMaxOrderLongLow = 12;
static unsigned short tnsMaxOrderShortMainLow = 7;


/*************************/
/* Function prototypes   */
/*************************/
static void Autocorrelation(int maxOrder,        /* Maximum autocorr order */
                     int dataSize,        /* Size of the data array */
                     faac_real* data,        /* Data array */
                     faac_real* rArray);     /* Autocorrelation array */

static faac_real LevinsonDurbin(int maxOrder,        /* Maximum filter order */
                      int dataSize,        /* Size of the data array */
                      faac_real* data,        /* Data array */
                      faac_real* kArray);     /* Reflection coeff array */

static void StepUp(int fOrder, faac_real* kArray, faac_real* aArray);

static void QuantizeReflectionCoeffs(int fOrder,int coeffRes,faac_real* rArray,int* indexArray);
static int TruncateCoeffs(int fOrder,faac_real threshold,faac_real* kArray);
static void TnsInvFilter(int length,faac_real* spec,TnsFilterData* filter, faac_real *temp);


/*****************************************************/
/* InitTns:                                          */
/*****************************************************/
void TnsInit(faacEncStruct* hEncoder)
{
    unsigned int channel;
    int fsIndex = hEncoder->sampleRateIdx;
    int profile = hEncoder->config.aacObjectType;

    for (channel = 0; channel < hEncoder->numChannels; channel++) {
        TnsInfo *tnsInfo = &hEncoder->coderInfo[channel].tnsInfo;

        switch( profile ) {
        case MAIN:
        case LTP:
            tnsInfo->tnsMaxBandsLong = tnsMaxBandsLongMainLow[fsIndex];
            tnsInfo->tnsMaxBandsShort = tnsMaxBandsShortMainLow[fsIndex];
            if (hEncoder->config.mpegVersion == 1) { /* MPEG2 */
                tnsInfo->tnsMaxOrderLong = tnsMaxOrderLongMain;
            } else { /* MPEG4 */
                if (fsIndex <= 5) /* fs > 32000Hz */
                    tnsInfo->tnsMaxOrderLong = 12;
                else
                    tnsInfo->tnsMaxOrderLong = 20;
            }
            tnsInfo->tnsMaxOrderShort = tnsMaxOrderShortMainLow;
            break;
        case LOW :
            tnsInfo->tnsMaxBandsLong = tnsMaxBandsLongMainLow[fsIndex];
            tnsInfo->tnsMaxBandsShort = tnsMaxBandsShortMainLow[fsIndex];
            tnsInfo->tnsMaxOrderLong = tnsMaxOrderLongLow;
            tnsInfo->tnsMaxOrderShort = tnsMaxOrderShortMainLow;
            break;
        }
        tnsInfo->tnsMinBandNumberLong = tnsMinBandNumberLong[fsIndex];
        tnsInfo->tnsMinBandNumberShort = tnsMinBandNumberShort[fsIndex];
        tnsInfo->bitRate = hEncoder->config.bitRate;
        tnsInfo->useTns = hEncoder->config.useTns;
    }
}


/*****************************************************/
/* TnsEncode:                                        */
/*****************************************************/
void TnsEncode(TnsInfo* tnsInfo,       /* TNS info */
               int numberOfBands,       /* Number of bands per window */
               int maxSfb,              /* max_sfb */
               enum WINDOW_TYPE blockType,   /* block type */
               int* sfbOffsetTable,     /* Scalefactor band offset table */
               faac_real* spec,            /* Spectral data array */
               faac_real* temp)
{
    int numberOfWindows,windowSize;
    int startBand,stopBand,order;    /* Bands over which to apply TNS */
    int w, i;
    int startIndex,length;
    faac_real gain;

    switch( blockType ) {
    case ONLY_SHORT_WINDOW :
        numberOfWindows = MAX_SHORT_WINDOWS;
        windowSize = BLOCK_LEN_SHORT;
        startBand = tnsInfo->tnsMinBandNumberShort;
        stopBand = numberOfBands; // Align with max_sfb
        order = tnsInfo->tnsMaxOrderShort;
        break;

    default:
        numberOfWindows = 1;
        windowSize = BLOCK_LEN_LONG;
        startBand = tnsInfo->tnsMinBandNumberLong;
        stopBand = numberOfBands; // Align with max_sfb
        order = tnsInfo->tnsMaxOrderLong;
        break;
    }

    /* Bitrate per channel in kbps */
    int br_per_ch = tnsInfo->bitRate / 1000;

    /* In Auto-TNS mode, disable TNS for bitrates < TNS_BR_LOW kbps per channel */
    if (tnsInfo->useTns == -1 && br_per_ch < TNS_BR_LOW) return;

    /* Make sure that start and stop bands < maxSfb */
    startBand = min(startBand,maxSfb);
    stopBand = min(stopBand,maxSfb);
    startBand = max(startBand,0);
    stopBand = max(stopBand,0);

    tnsInfo->tnsDataPresent = 0;

    if (tnsInfo->useTns == 0) return;

    /* Perform analysis and filtering for each window */
    for (w=0;w<numberOfWindows;w++) {

        TnsWindowData* windowData = &tnsInfo->windowData[w];
        TnsFilterData* tnsFilter = windowData->tnsFilter;
        faac_real* k = tnsFilter->kCoeffs;

        windowData->numFilters=0;
        windowData->coefResolution = 4;
        startIndex = w * windowSize + sfbOffsetTable[startBand];
        length = sfbOffsetTable[stopBand] - sfbOffsetTable[startBand];

        if (length <= 0) continue;

        /* Energy early-exit: skip quiet blocks to save CPU */
        faac_real energy = 0.0;
        for (i = 0; i < length; i++) {
            faac_real s = spec[startIndex + i];
            energy += s * s;
        }
        if (energy < (faac_real)length * 0.005) continue;

        gain = LevinsonDurbin(order,length,&spec[startIndex],k);

        /* Determine threshold for current window */
        faac_real threshold = TNS_GAIN_INITIAL;
        if (tnsInfo->useTns == -1) {
            if (blockType == ONLY_SHORT_WINDOW) {
                threshold = TNS_GAIN_AUTO_SHORT;
            } else {
                if (br_per_ch < TNS_BR_MID) threshold = 2.4;
                else if (br_per_ch < TNS_BR_HIGH) threshold = TNS_GAIN_AUTO_LONG;
                else threshold = TNS_GAIN_AUTO_HIGH;
            }
        }

        if (gain > threshold) {
            int truncatedOrder;
            faac_real pred_gain;
            faac_real k_quant[TNS_MAX_ORDER+1];
            int index_quant[TNS_MAX_ORDER+1];

            truncatedOrder = TruncateCoeffs(order, DEF_TNS_COEFF_THRESH, k);

            if (truncatedOrder > 0) {
                /* Evaluate prediction gain after quantization */
                faac_real error = 1.0;
                int can_compress = 1;

                for (i=1; i<=truncatedOrder; i++) {
                    /* Clamp k to avoid stability issues */
                    if (k[i] > 0.99) k[i] = 0.99;
                    if (k[i] < -0.99) k[i] = -0.99;
                    k_quant[i] = k[i];
                }

                /* Standard 4-bit quantization mapping */
                QuantizeReflectionCoeffs(truncatedOrder, 4, k_quant, index_quant);

                /* Check if indices fit in 3 bits with 4-bit scaling */
                for (i=1; i<=truncatedOrder; i++) {
                    if (index_quant[i] < -4 || index_quant[i] > 3) {
                        can_compress = 0;
                        break;
                    }
                }

                /* To be perfectly clear to decoders, if we want to signal 3-bit scaling,
                   we should use the 3-bit scaling factor. */
                if (can_compress) {
                    QuantizeReflectionCoeffs(truncatedOrder, 3, k_quant, index_quant);
                }

                for (i=1; i<=truncatedOrder; i++) {
                    error *= (1.0 - k_quant[i] * k_quant[i]);
                }
                pred_gain = (error > 0.0) ? (1.0 / error) : 100.0;

                if (pred_gain > threshold && truncatedOrder > 1) {
                    int target_order = truncatedOrder;
                    if (target_order > length) target_order = length;

                    windowData->numFilters++;
                    tnsInfo->tnsDataPresent = 1;
                    tnsFilter->direction = 0;
                    tnsFilter->coefCompress = can_compress;
                    windowData->coefResolution = 4;
                    tnsFilter->length = stopBand - startBand;
                    tnsFilter->order = target_order;
                    for (i=1; i<=target_order; i++) {
                        tnsFilter->index[i] = index_quant[i];
                        tnsFilter->kCoeffs[i] = k_quant[i];
                    }
                    StepUp(target_order, tnsFilter->kCoeffs, tnsFilter->aCoeffs);
                    TnsInvFilter(length, &spec[startIndex], tnsFilter, temp);
                }
            }
        }
    }
}


/*****************************************************/
/* TnsEncodeFilterOnly:                              */
/*****************************************************/
void TnsEncodeFilterOnly(TnsInfo* tnsInfo,           /* TNS info */
                         int numberOfBands,          /* Number of bands per window */
                         int maxSfb,                 /* max_sfb */
                         enum WINDOW_TYPE blockType, /* block type */
                         int* sfbOffsetTable,        /* Scalefactor band offset table */
                         faac_real* spec,               /* Spectral data array */
                         faac_real* temp)
{
    int numberOfWindows,windowSize;
    int startBand,stopBand;
    int w;
    int startIndex,length;

    switch( blockType ) {
    case ONLY_SHORT_WINDOW :
        numberOfWindows = MAX_SHORT_WINDOWS;
        windowSize = BLOCK_LEN_SHORT;
        startBand = tnsInfo->tnsMinBandNumberShort;
        stopBand = numberOfBands;
        break;

    default:
        numberOfWindows = 1;
        windowSize = BLOCK_LEN_LONG;
        startBand = tnsInfo->tnsMinBandNumberLong;
        stopBand = numberOfBands;
        break;
    }

    startBand = min(startBand,maxSfb);
    stopBand = min(stopBand,maxSfb);
    startBand = max(startBand,0);
    stopBand = max(stopBand,0);


    for(w=0;w<numberOfWindows;w++)
    {
        TnsWindowData* windowData = &tnsInfo->windowData[w];
        TnsFilterData* tnsFilter = windowData->tnsFilter;

        startIndex = w * windowSize + sfbOffsetTable[startBand];
        length = sfbOffsetTable[stopBand] - sfbOffsetTable[startBand];

        if (tnsInfo->tnsDataPresent  &&  windowData->numFilters) {
            TnsInvFilter(length,&spec[startIndex],tnsFilter,temp);
        }
    }
}




/********************************************************/
/* TnsInvFilter:                                        */
/********************************************************/
static void TnsInvFilter(int length,faac_real* spec,TnsFilterData* filter, faac_real *temp)
{
    int i,j,k=0;
    int order=filter->order;
    faac_real* a=filter->aCoeffs;

    if (order >= length) order = length > 0 ? (length - 1) : 0;
    if (order <= 0) return;

    if (filter->direction) {
        temp[length-1]=spec[length-1];
        for (i=length-2;i>(length-1-order);i--) {
            temp[i]=spec[i];
            k++;
            for (j=1;j<=k;j++) {
                spec[i]+=temp[i+j]*a[j];
            }
        }
        for (i=length-1-order;i>=0;i--) {
            temp[i]=spec[i];
            for (j=1;j<=order;j++) {
                spec[i]+=temp[i+j]*a[j];
            }
        }
    } else {
        temp[0]=spec[0];
        for (i=1;i<order;i++) {
            temp[i]=spec[i];
            for (j=1;j<=i;j++) {
                spec[i]+=temp[i-j]*a[j];
            }
        }
        for (i=order;i<length;i++) {
            temp[i]=spec[i];
            for (j=1;j<=order;j++) {
                spec[i]+=temp[i-j]*a[j];
            }
        }
    }
}





/*****************************************************/
/* TruncateCoeffs:                                   */
/*****************************************************/
static int TruncateCoeffs(int fOrder,faac_real threshold,faac_real* kArray)
{
    int i;
    for (i = fOrder; i >= 0; i--) {
        kArray[i] = (FAAC_FABS(kArray[i])>threshold) ? kArray[i] : 0.0;
        if (kArray[i]!=0.0) return i;
    }
    return 0;
}

/*****************************************************/
/* QuantizeReflectionCoeffs:                         */
/*****************************************************/
static void QuantizeReflectionCoeffs(int fOrder,
                              int coeffRes,
                              faac_real* rArray,
                              int* indexArray)
{
    int i;
    /* Standard symmetric mapping: fac = 2^(res-1) / (pi/2) */
    faac_real fac = (faac_real)(1 << (coeffRes - 1)) / (M_PI / 2.0);
    int low = -(1 << (coeffRes - 1)) + 1; /* restricted range for stability */
    int high = (1 << (coeffRes - 1)) - 1;

    for (i=1;i<=fOrder;i++) {
        faac_real val = rArray[i];
        if (val > 0.99) val = 0.99;
        if (val < -0.99) val = -0.99;
        val = FAAC_ASIN(val);
        indexArray[i] = FAAC_LRINT(val * fac);
        if (indexArray[i] < low) indexArray[i] = low;
        if (indexArray[i] > high) indexArray[i] = high;
        rArray[i] = FAAC_SIN((faac_real)indexArray[i] / fac);
    }
}

/*****************************************************/
/* Autocorrelation,                                  */
/*****************************************************/
static void Autocorrelation(int maxOrder,        /* Maximum autocorr order */
                     int dataSize,        /* Size of the data array */
                     faac_real* data,        /* Data array */
                     faac_real* rArray)      /* Autocorrelation array */
{
    int order,index;
    for (order=0;order<=maxOrder;order++) {
        faac_real sum = 0.0;
        int n = dataSize - order;
        faac_real *p1 = data;
        faac_real *p2 = data + order;
        for (index=0;index<n;index++) {
            sum += (*p1++) * (*p2++);
        }
        rArray[order] = sum;
    }
}



/*****************************************************/
/* LevinsonDurbin:                                   */
/*****************************************************/
static faac_real LevinsonDurbin(int fOrder,          /* Filter order */
                      int dataSize,        /* Size of the data array */
                      faac_real* data,        /* Data array */
                      faac_real* kArray)      /* Reflection coeff array */
{
    int order,i;
    faac_real signal;
    faac_real error, kTemp;
    faac_real aArray1[TNS_MAX_ORDER+1];
    faac_real aArray2[TNS_MAX_ORDER+1];
    faac_real rArray[TNS_MAX_ORDER+1] = {0};
    faac_real* aPtr = aArray1;
    faac_real* aLastPtr = aArray2;
    faac_real* aTemp;

    Autocorrelation(fOrder,dataSize,data,rArray);
    signal=rArray[0];

    if (!signal) {
        kArray[0]=1.0;
        for (order=1;order<=fOrder;order++) {
            kArray[order]=0.0;
        }
        return 0;
    } else {
        kArray[0]=1.0;
        aPtr[0]=1.0;
        aLastPtr[0]=1.0;
        error=rArray[0];

        for (order=1;order<=fOrder;order++) {
            kTemp = aLastPtr[0]*rArray[order-0];
            for (i=1;i<order;i++) {
                kTemp += aLastPtr[i]*rArray[order-i];
            }
            if (error <= 0.0 || FAAC_FABS(kTemp) >= error) {
                error = 0.0;
                break;
            }
            kTemp = -kTemp/error;
            kArray[order]=kTemp;
            aPtr[order]=kTemp;
            for (i=1;i<order;i++) {
                aPtr[i] = aLastPtr[i] + kTemp*aLastPtr[order-i];
            }
            error = error * (1 - kTemp*kTemp);
            if (error <= 0.0) break;

            aTemp=aLastPtr;
            aLastPtr=aPtr;
            aPtr=aTemp;
        }
        if (error <= 0.0) return 100.0;
        return (signal > 0.0) ? (signal / error) : 0.0;
    }
}


/*****************************************************/
/* StepUp:                                           */
/*****************************************************/
static void StepUp(int fOrder,faac_real* kArray,faac_real* aArray)
{
    faac_real aTemp[TNS_MAX_ORDER+2];
    int i,order;

    aArray[0]=1.0;
    aTemp[0]=1.0;
    for (order=1;order<=fOrder;order++) {
        aArray[order]=0.0;
        for (i=1;i<=order;i++) {
            aTemp[i] = aArray[i] + kArray[order]*aArray[order-i];
        }
        for (i=1;i<=order;i++) {
            aArray[i]=aTemp[i];
        }
    }
}
