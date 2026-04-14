/****************************************************************************
    Quantizer core functions
    quality setting, error distribution, etc.

    Copyright (C) 2017 Krzysztof Nikiel

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
****************************************************************************/

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "quantize.h"
#include "huff2.h"
#include "cpu_compute.h"

#ifdef __GNUC__
#define GCC_VERSION (__GNUC__ * 10000 \
                     + __GNUC_MINOR__ * 100 \
                     + __GNUC_PATCHLEVEL__)
#endif

typedef void (*QuantizeFunc)(const faac_real * __restrict xr, int * __restrict xi, int n, faac_real sfacfix);

#if defined(HAVE_SSE2)
extern void quantize_sse2(const faac_real * __restrict xr, int * __restrict xi, int n, faac_real sfacfix);
#endif

static void quantize_scalar(const faac_real * __restrict xr, int * __restrict xi, int n, faac_real sfacfix)
{
    const faac_real magic = MAGIC_NUMBER;
    int cnt;
    for (cnt = 0; cnt < n; cnt++)
    {
        faac_real val = xr[cnt];
        faac_real tmp = FAAC_FABS(val);

        tmp *= sfacfix;
        tmp = FAAC_SQRT(tmp * FAAC_SQRT(tmp));

        int q = (int)(tmp + magic);
        if (q > MAX_HUFF_ESC_VAL)
            q = MAX_HUFF_ESC_VAL;
        xi[cnt] = (val < 0) ? -q : q;
    }
}

static QuantizeFunc qfunc = quantize_scalar;
static faac_real sfstep;
static faac_real max_quant_limit;

void QuantizeInit(void)
{
#if defined(HAVE_SSE2)
    CPUCaps caps = get_cpu_caps();
    if (caps & CPU_CAP_SSE2)
        qfunc = quantize_sse2;
    else
#endif
        qfunc = quantize_scalar;

    /* 2^0.25 (1.50515 dB) step from AAC specs */
    sfstep = 1.0 / FAAC_LOG10(FAAC_SQRT(FAAC_SQRT(2.0)));

    /* Inverse-quantizer headroom: ensures (x * gain)^0.75 + 0.4054 <= 8191.
     * Pre-calculated to avoid redundant runtime power functions. */
    max_quant_limit = FAAC_POW((faac_real)MAX_HUFF_ESC_VAL + 1.0 - MAGIC_NUMBER, 4.0/3.0);
}
#define NOISEFLOOR 0.4

// band sound masking
static void bmask(CoderInfo * __restrict coderInfo, faac_real * __restrict xr0, faac_real * __restrict bandqual,
                  faac_real * __restrict bandenrg, faac_real * __restrict bandmaxe, int gnum, faac_real quality)
{
  int sfb, start, end, cnt;
  int *cb_offset = coderInfo->sfb_offset;
  int last;
  faac_real avgenrg;
  faac_real powm = 0.4;
  faac_real totenrg = 0.0;
  int gsize = coderInfo->groups.len[gnum];
  const faac_real *xr;
  int win;
  int enrgcnt = 0;
  int total_len = coderInfo->sfb_offset[coderInfo->sfbn];

  for (win = 0; win < gsize; win++)
  {
      xr = xr0 + win * BLOCK_LEN_SHORT;
      for (cnt = 0; cnt < total_len; cnt++)
      {
          totenrg += xr[cnt] * xr[cnt];
      }
  }
  enrgcnt = gsize * total_len;

  if (totenrg < ((NOISEFLOOR * NOISEFLOOR) * (faac_real)enrgcnt))
  {
      for (sfb = 0; sfb < coderInfo->sfbn; sfb++)
      {
          bandqual[sfb] = 0.0;
          bandenrg[sfb] = 0.0;
      }

      return;
  }

  for (sfb = 0; sfb < coderInfo->sfbn; sfb++)
  {
    faac_real avge, maxe;
    faac_real target;

    start = cb_offset[sfb];
    end = cb_offset[sfb + 1];

    avge = 0.0;
    maxe = 0.0;
    for (win = 0; win < gsize; win++)
    {
        xr = xr0 + win * BLOCK_LEN_SHORT + start;
        int n = end - start;
        for (cnt = 0; cnt < n; cnt++)
        {
            faac_real val = xr[cnt];
            faac_real e = val * val;
            avge += e;
            if (maxe < e)
                maxe = e;
        }
    }
    bandenrg[sfb] = avge;
    /* Track peak magnitude to identify potential Huffman overflows. */
    bandmaxe[sfb] = FAAC_SQRT(maxe);
    maxe *= gsize;

#define NOISETONE 0.2
    if (coderInfo->block_type == ONLY_SHORT_WINDOW)
    {
        last = BLOCK_LEN_SHORT;
        avgenrg = totenrg / last;
        avgenrg *= end - start;

        target = NOISETONE * FAAC_POW(avge/avgenrg, powm);
        target += (1.0 - NOISETONE) * 0.45 * FAAC_POW(maxe/avgenrg, powm);

        target *= 1.5;
    }
    else
    {
        last = BLOCK_LEN_LONG;
        avgenrg = totenrg / last;
        avgenrg *= end - start;

        target = NOISETONE * FAAC_POW(avge/avgenrg, powm);
        target += (1.0 - NOISETONE) * 0.45 * FAAC_POW(maxe/avgenrg, powm);
    }

    target *= 10.0 / (1.0 + ((faac_real)(start+end)/last));

    bandqual[sfb] = target * quality;
  }
}

enum {MAXSHORTBAND = 36};
// use band quality levels to quantize a group of windows
static void qlevel(CoderInfo * __restrict coderInfo,
                   const faac_real * __restrict xr0,
                   const faac_real * __restrict bandqual,
                   const faac_real * __restrict bandenrg,
                   const faac_real * __restrict bandmaxe,
                   int gnum,
                   int pnslevel,
                   int pass
                  )
{
    int sb;
    int gsize = coderInfo->groups.len[gnum];
    faac_real pnsthr = 0.1 * pnslevel;

    for (sb = 0; sb < coderInfo->sfbn && coderInfo->bandcnt < MAX_SCFAC_BANDS; sb++)
    {
      faac_real sfacfix;
      int sfac;
      faac_real rmsx;
      faac_real etot;
      int xitab[8 * MAXSHORTBAND];
      int *xi;
      int start, end;
      const faac_real *xr;
      int win;

      if (pass == 1)
      {
          if (coderInfo->book[coderInfo->bandcnt] != HCB_NONE)
          {
              coderInfo->bandcnt++;
              continue;
          }

          start = coderInfo->sfb_offset[sb];
          end = coderInfo->sfb_offset[sb+1];

          etot = bandenrg[sb] / (faac_real)gsize;
          rmsx = FAAC_SQRT(etot / (end - start));

          if ((rmsx < NOISEFLOOR) || (!bandqual[sb]))
          {
              coderInfo->book[coderInfo->bandcnt++] = HCB_ZERO;
              continue;
          }

          if (bandqual[sb] < pnsthr)
          {
              coderInfo->book[coderInfo->bandcnt] = HCB_PNS;
              coderInfo->sf[coderInfo->bandcnt] =
                  FAAC_LRINT(FAAC_LOG10(etot) * (0.5 * sfstep));
              coderInfo->bandcnt++;
              continue;
          }

          sfac = FAAC_LRINT(FAAC_LOG10(bandqual[sb] / rmsx) * sfstep);

          if ((SF_OFFSET - sfac) < SF_MIN)
          {
              coderInfo->book[coderInfo->bandcnt++] = HCB_ZERO;
              continue;
          }

          sfacfix = FAAC_POW(10, sfac / sfstep);

          if (sfacfix * bandmaxe[sb] > max_quant_limit)
          {
              sfacfix = max_quant_limit / bandmaxe[sb];
              sfac = (int)FAAC_FLOOR(FAAC_LOG10(sfacfix) * sfstep);
          }

          /* Pass 1 implementation: Identical to original single-pass
           * logic. This ensures that in "clean" frames (no illegal deltas),
           * the output is bit-identical to the baseline. */
          sfacfix = FAAC_POW(10, sfac / sfstep);
          end -= start;
          xi = xitab;
          for (win = 0; win < gsize; win++)
          {
              xr = xr0 + win * BLOCK_LEN_SHORT + start;
              qfunc(xr, xi, end, sfacfix);
              xi += end;
          }
          huffbook(coderInfo, xitab, gsize * end);
          coderInfo->sf[coderInfo->bandcnt++] = SF_OFFSET - sfac;
      }
      else // pass 2
      {
          int book = coderInfo->book[coderInfo->bandcnt];

          if (book == HCB_PNS || book == HCB_ZERO || book == HCB_INTENSITY || book == HCB_INTENSITY2 || book == HCB_NONE)
          {
              coderInfo->bandcnt++;
              continue;
          }

          start = coderInfo->sfb_offset[sb];
          end = coderInfo->sfb_offset[sb+1];

          sfac = SF_OFFSET - coderInfo->sf[coderInfo->bandcnt];
          sfacfix = FAAC_POW(10, sfac / sfstep);

          end -= start;
          xi = xitab;
          if (sfacfix <= 0.0)
          {
              memset(xi, 0, gsize * end * sizeof(int));
          }
          else
          {
              for (win = 0; win < gsize; win++)
              {
                  xr = xr0 + win * BLOCK_LEN_SHORT + start;
                  qfunc(xr, xi, end, sfacfix);
                  xi += end;
              }
          }

          /* Pass 2 refinement: Preserve the scalefactor chain. */
          int prev_book = coderInfo->book[coderInfo->bandcnt];
          huffbook(coderInfo, xitab, gsize * end);
          if (coderInfo->book[coderInfo->bandcnt] == HCB_ZERO && prev_book != HCB_ZERO) {
              coderInfo->book[coderInfo->bandcnt] = HCB_ESC;
              huffcode(xitab, gsize * end, HCB_ESC, coderInfo);
          }

          coderInfo->bandcnt++;
      }
    }
}

int BlocQuant(CoderInfo * __restrict coder, faac_real * __restrict xr, AACQuantCfg *aacquantCfg)
{
    faac_real bandlvl[MAX_SCFAC_BANDS];
    faac_real bandenrg[MAX_SCFAC_BANDS];
    faac_real bandmaxe[MAX_SCFAC_BANDS];
    int cnt;
    faac_real *gxr;

    coder->global_gain = 0;
    coder->bandcnt = 0;
    coder->datacnt = 0;

    {
        int lastis;
        int lastsf;
        int band_idx = 0;

        // Pass 1: Identical to original single-pass quantization
        gxr = xr;
        for (cnt = 0; cnt < coder->groups.n; cnt++)
        {
            bmask(coder, gxr, bandlvl + band_idx, bandenrg + band_idx, bandmaxe + band_idx, cnt,
                  (faac_real)aacquantCfg->quality/DEFQUAL);
            qlevel(coder, gxr, bandlvl + band_idx, bandenrg + band_idx, bandmaxe + band_idx, cnt, aacquantCfg->pnslevel, 1);
            gxr += coder->groups.len[cnt] * BLOCK_LEN_SHORT;
            band_idx += coder->sfbn;
        }

        // Determine global gain

        for (cnt = 0; cnt < coder->bandcnt; cnt++)
        {
            int book = coder->book[cnt];
            if (!book)
                continue;
            if ((book != HCB_INTENSITY) && (book != HCB_INTENSITY2))
            {
                coder->global_gain = coder->sf[cnt];

                break;
            }
        }

        /* Regression avoidance: check if we actually have illegal deltas.
         * If the bitstream is already valid, we return immediately with Pass 1 results,
         * which are identical to the baseline. */
        int needs_pass2 = 0;
        lastsf = coder->global_gain;
        lastis = 0;
        int lastpns = coder->global_gain - SF_PNS_OFFSET;
        int pns_init = 1;

        for (cnt = 0; cnt < coder->bandcnt; cnt++) {
            int book = coder->book[cnt];
            if ((book == HCB_INTENSITY) || (book == HCB_INTENSITY2)) {
                int diff = coder->sf[cnt] - lastis;
                if (diff > 60 || diff < -60) { needs_pass2 = 1; break; }
                lastis = coder->sf[cnt];
            } else if (book == HCB_PNS) {
                int diff = coder->sf[cnt] - lastpns;
                if (!pns_init && (diff > 60 || diff < -60)) { needs_pass2 = 1; break; }
                lastpns = coder->sf[cnt];
                pns_init = 0;
            } else if ((book != HCB_ZERO) && (book != HCB_NONE)) {
                int diff = coder->sf[cnt] - lastsf;
                if (diff > 60 || diff < -60) { needs_pass2 = 1; break; }
                lastsf = coder->sf[cnt];
            }
        }

        if (!needs_pass2) return 1;

        // If we reach here, illegal deltas were detected. Apply clamping and Pass 2.
        int first_h = -1, first_p = -1, first_i = -1;
        for (cnt = 0; cnt < coder->bandcnt; cnt++) {
            int b = coder->book[cnt];
            if (b == HCB_PNS) { if (first_p == -1) first_p = cnt; }
            else if (b == HCB_INTENSITY || b == HCB_INTENSITY2) { if (first_i == -1) first_i = cnt; }
            else if (b != HCB_ZERO && b != HCB_NONE) { if (first_h == -1) first_h = cnt; }
        }

        // Forward passes
        lastsf = (first_h != -1) ? coder->sf[first_h] : 0;
        lastis = 0;
        lastpns = (first_p != -1) ? coder->sf[first_p] : 0;
        int p_init = 1;
        for (cnt = 0; cnt < coder->bandcnt; cnt++) {
            int b = coder->book[cnt];
            if (b == HCB_INTENSITY || b == HCB_INTENSITY2) {
                if (coder->sf[cnt] < lastis - 60) coder->sf[cnt] = lastis - 60;
                lastis = coder->sf[cnt];
            } else if (b == HCB_PNS) {
                if (p_init) p_init = 0;
                else if (coder->sf[cnt] < lastpns - 60) coder->sf[cnt] = lastpns - 60;
                lastpns = coder->sf[cnt];
            } else if (b != HCB_ZERO && b != HCB_NONE) {
                if (coder->sf[cnt] < lastsf - 60) coder->sf[cnt] = lastsf - 60;
                lastsf = coder->sf[cnt];
            }
        }
        // Backward passes
        int nextsf = -1, nextis = -1, nextpns = -1;
        for (cnt = coder->bandcnt - 1; cnt >= 0; cnt--) {
            int b = coder->book[cnt];
            if (b == HCB_INTENSITY || b == HCB_INTENSITY2) {
                if (nextis != -1 && coder->sf[cnt] < nextis - 60) coder->sf[cnt] = nextis - 60;
                nextis = coder->sf[cnt];
            } else if (b == HCB_PNS) {
                if (nextpns != -1 && coder->sf[cnt] < nextpns - 60) coder->sf[cnt] = nextpns - 60;
                nextpns = coder->sf[cnt];
            } else if (b != HCB_ZERO && b != HCB_NONE) {
                if (nextsf != -1 && coder->sf[cnt] < nextsf - 60) coder->sf[cnt] = nextsf - 60;
                nextsf = coder->sf[cnt];
            }
        }
        if (first_h != -1) coder->global_gain = coder->sf[first_h];

        // Pass 2: Quantize using final clamped scalefactors
        coder->bandcnt = 0;
        coder->datacnt = 0;
        gxr = xr;
        band_idx = 0;
        for (cnt = 0; cnt < coder->groups.n; cnt++)
        {
            qlevel(coder, gxr, bandlvl + band_idx, bandenrg + band_idx, bandmaxe + band_idx, cnt, aacquantCfg->pnslevel, 2);
            gxr += coder->groups.len[cnt] * BLOCK_LEN_SHORT;
            band_idx += coder->sfbn;
        }

        return 1;
    }

    return 0;
}

void CalcBW(unsigned *bw, int rate, SR_INFO *sr, AACQuantCfg *aacquantCfg)
{
    // find max short frame band
    int max = *bw * (BLOCK_LEN_SHORT << 1) / rate;
    int cnt;
    int l;

    l = 0;
    for (cnt = 0; cnt < sr->num_cb_short; cnt++)
    {
        if (l >= max)
            break;
        l += sr->cb_width_short[cnt];
    }
    aacquantCfg->max_cbs = cnt;
    if (aacquantCfg->pnslevel)
        *bw = (faac_real)l * rate / (BLOCK_LEN_SHORT << 1);

    // find max long frame band
    max = *bw * (BLOCK_LEN_LONG << 1) / rate;
    l = 0;
    for (cnt = 0; cnt < sr->num_cb_long; cnt++)
    {
        if (l >= max)
            break;
        l += sr->cb_width_long[cnt];
    }
    aacquantCfg->max_cbl = cnt;
    aacquantCfg->max_l = l;

    *bw = (faac_real)l * rate / (BLOCK_LEN_LONG << 1);
}

enum {MINSFB = 2};

static void calce(faac_real * __restrict xr, const int * __restrict bands, faac_real e[NSFB_SHORT], int maxsfb,
                  int maxl)
{
    int sfb;
    int l;

    // mute lines above cutoff freq
    for (l = maxl; l < bands[maxsfb]; l++)
        xr[l] = 0.0;

    for (sfb = MINSFB; sfb < maxsfb; sfb++)
    {
        e[sfb] = 0;
        for (l = bands[sfb]; l < bands[sfb + 1]; l++)
            e[sfb] += xr[l] * xr[l];
    }
}

static void resete(faac_real min[NSFB_SHORT], faac_real max[NSFB_SHORT],
                   faac_real e[NSFB_SHORT], int maxsfb)
{
    int sfb;
    for (sfb = MINSFB; sfb < maxsfb; sfb++)
        min[sfb] = max[sfb] = e[sfb];
}

#define PRINTSTAT 0
#if PRINTSTAT
static int groups = 0;
static int frames = 0;
#endif
void BlocGroup(faac_real *xr, CoderInfo *coderInfo, AACQuantCfg *cfg)
{
    int win, sfb;
    faac_real e[NSFB_SHORT];
    faac_real min[NSFB_SHORT];
    faac_real max[NSFB_SHORT];
    const faac_real thr = 3.0;
    int win0;
    int fastmin;
    int maxsfb, maxl;

    if (coderInfo->block_type != ONLY_SHORT_WINDOW)
    {
        coderInfo->groups.n = 1;
        coderInfo->groups.len[0] = 1;
        return;
    }

    maxl = cfg->max_l / 8;
    maxsfb = cfg->max_cbs;
    fastmin = ((maxsfb - MINSFB) * 3) >> 2;

#if PRINTSTAT
    frames++;
#endif
    calce(xr, coderInfo->sfb_offset, e, maxsfb, maxl);
    resete(min, max, e, maxsfb);
    win0 = 0;
    coderInfo->groups.n = 0;
    for (win = 1; win < MAX_SHORT_WINDOWS; win++)
    {
        int fast = 0;

        calce(xr + win * BLOCK_LEN_SHORT, coderInfo->sfb_offset, e, maxsfb, maxl);
        for (sfb = MINSFB; sfb < maxsfb; sfb++)
        {
            if (min[sfb] > e[sfb])
                min[sfb] = e[sfb];
            if (max[sfb] < e[sfb])
                max[sfb] = e[sfb];

            if (max[sfb] > thr * min[sfb])
                fast++;
        }
        if (fast > fastmin)
        {
            coderInfo->groups.len[coderInfo->groups.n++] = win - win0;
            win0 = win;
            resete(min, max, e, maxsfb);
        }
    }
    coderInfo->groups.len[coderInfo->groups.n++] = win - win0;
#if PRINTSTAT
    groups += coderInfo->groups.n;
#endif
}

void BlocStat(void)
{
#if PRINTSTAT
    printf("frames:%d; groups:%d; g/f:%f\n", frames, groups, (faac_real)groups/frames);
#endif
}
