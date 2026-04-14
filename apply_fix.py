import re

filepath = 'libfaac/quantize.c'
with open(filepath, 'r') as f:
    content = f.read()

# Define the new qlevel and BlocQuant bodies
qlevel_new = r'''// use band quality levels to quantize a group of windows
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
              coderInfo->sf[coderInfo->bandcnt] +=
                  FAAC_LRINT(FAAC_LOG10(etot) * (0.5 * sfstep));
              coderInfo->bandcnt++;
              continue;
          }

          sfac = FAAC_LRINT(FAAC_LOG10(bandqual[sb] / rmsx) * sfstep);

          if ((SF_OFFSET - sfac) < SF_MIN)
              sfacfix = 0.0;
          else
          {
              sfacfix = FAAC_POW(10, sfac / sfstep);

              /* Bitstream saturation check: if gain * peak exceeds the Huffman limit,
               * clamp gain and re-sync the integer scalefactor to prevent overflow. */
              if (sfacfix * bandmaxe[sb] > max_quant_limit)
              {
                  sfacfix = max_quant_limit / bandmaxe[sb];
                  sfac = (int)FAAC_FLOOR(FAAC_LOG10(sfacfix) * sfstep);
              }
          }

          coderInfo->book[coderInfo->bandcnt] = HCB_ESC; // placeholder
          coderInfo->sf[coderInfo->bandcnt++] += SF_OFFSET - sfac;
      }
      else
      {
          int book = coderInfo->book[coderInfo->bandcnt];

          if (book == HCB_PNS || book == HCB_ZERO || book == HCB_INTENSITY || book == HCB_INTENSITY2)
          {
              coderInfo->bandcnt++;
              continue;
          }

          start = coderInfo->sfb_offset[sb];
          end = coderInfo->sfb_offset[sb+1];

          sfac = SF_OFFSET - coderInfo->sf[coderInfo->bandcnt];
          sfacfix = FAAC_POW(10, sfac / sfstep);

          if (sfacfix * bandmaxe[sb] > max_quant_limit)
          {
              sfacfix = max_quant_limit / bandmaxe[sb];
              sfac = (int)FAAC_FLOOR(FAAC_LOG10(sfacfix) * sfstep);
              sfacfix = FAAC_POW(10, sfac / sfstep);
              coderInfo->sf[coderInfo->bandcnt] = SF_OFFSET - sfac;
          }

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
          huffbook(coderInfo, xitab, gsize * end);
          coderInfo->bandcnt++;
      }
    }
}'''

bloc_quant_new = r'''int BlocQuant(CoderInfo * __restrict coder, faac_real * __restrict xr, AACQuantCfg *aacquantCfg)
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

        gxr = xr;
        for (cnt = 0; cnt < coder->groups.n; cnt++)
        {
            bmask(coder, gxr, bandlvl, bandenrg, bandmaxe, cnt,
                  (faac_real)aacquantCfg->quality/DEFQUAL);
            qlevel(coder, gxr, bandlvl, bandenrg, bandmaxe, cnt, aacquantCfg->pnslevel, 1);
            gxr += coder->groups.len[cnt] * BLOCK_LEN_SHORT;
        }

        coder->global_gain = 0;
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

        lastsf = coder->global_gain;
        lastis = 0;
        int lastpns = coder->global_gain - SF_PNS_OFFSET;
        for (cnt = 0; cnt < coder->bandcnt; cnt++)
        {
            int book = coder->book[cnt];
            if ((book == HCB_INTENSITY) || (book == HCB_INTENSITY2))
            {
                int diff = coder->sf[cnt] - lastis;
                diff = clamp_sf_diff(diff);
                lastis += diff;
                coder->sf[cnt] = lastis;
            }
            else if (book == HCB_PNS)
            {
                int diff = coder->sf[cnt] - lastpns;
                diff = clamp_sf_diff(diff);
                lastpns += diff;
                coder->sf[cnt] = lastpns;
            }
            else if ((book != HCB_ZERO) && (book != HCB_NONE))
            {
                int diff = coder->sf[cnt] - lastsf;
                diff = clamp_sf_diff(diff);
                lastsf += diff;
                coder->sf[cnt] = lastsf;
            }
        }

        coder->bandcnt = 0;
        coder->datacnt = 0;
        gxr = xr;
        for (cnt = 0; cnt < coder->groups.n; cnt++)
        {
            bmask(coder, gxr, bandlvl, bandenrg, bandmaxe, cnt,
                  (faac_real)aacquantCfg->quality/DEFQUAL);
            qlevel(coder, gxr, bandlvl, bandenrg, bandmaxe, cnt, aacquantCfg->pnslevel, 2);
            gxr += coder->groups.len[cnt] * BLOCK_LEN_SHORT;
        }

        return 1;
    }
    return 0;
}'''

# Replace using string find/brace matching
def find_function_range(text, signature_start):
    start_pos = text.find(signature_start)
    if start_pos == -1: return None
    brace_start = text.find('{', start_pos)
    if brace_start == -1: return None
    depth = 0
    for i in range(brace_start, len(text)):
        if text[i] == '{': depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0: return (start_pos, i + 1)
    return None

q_range = find_function_range(content, 'static void qlevel(')
if q_range:
    content = content[:q_range[0]] + qlevel_new + content[q_range[1]:]

bq_range = find_function_range(content, 'int BlocQuant(')
if bq_range:
    content = content[:bq_range[0]] + bloc_quant_new + content[bq_range[1]:]

with open(filepath, 'w') as f:
    f.write(content)
