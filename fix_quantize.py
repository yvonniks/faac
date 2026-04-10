import re

content = open('libfaac/quantize.c').read()

new_qlevel = """static void qlevel(CoderInfo * __restrict coderInfo,
                   const faac_real * __restrict xr0,
                   const faac_real * __restrict bandqual,
                   const faac_real * __restrict bandenrg,
                   int gnum,
                   int pnslevel,
                   int final_pass
                  )
{
    int sb;
#if !defined(__clang__) && defined(__GNUC__) && (GCC_VERSION >= 40600)
    /* 2^0.25 (1.50515 dB) step from AAC specs */
    static const faac_real sfstep = 1.0 / FAAC_LOG10(FAAC_SQRT(FAAC_SQRT(2.0)));
#else
    static const faac_real sfstep = 20 / 1.50515;
#endif
    int gsize = coderInfo->groups.len[gnum];
    faac_real pnsthr = (faac_real)0.1 * pnslevel;

    for (sb = 0; sb < coderInfo->sfbn; sb++)
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

      if (coderInfo->book[coderInfo->bandcnt] != HCB_NONE && !final_pass)
      {
          coderInfo->bandcnt++;
          continue;
      }

      if (final_pass) {
          int book = coderInfo->book[coderInfo->bandcnt];
          if (book == HCB_NONE || book == HCB_ZERO || book == HCB_PNS || book == HCB_INTENSITY || book == HCB_INTENSITY2) {
              coderInfo->bandcnt++;
              continue;
          }
      }

      start = coderInfo->sfb_offset[sb];
      end = coderInfo->sfb_offset[sb+1];

      if (!final_pass) {
          etot = bandenrg[sb] / (faac_real)gsize;
          rmsx = FAAC_SQRT(etot / (faac_real)(end - start));

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
          coderInfo->sf[coderInfo->bandcnt] = SF_OFFSET - sfac;

          if ((SF_OFFSET - sfac) < 10) sfacfix = (faac_real)0.0;
          else sfacfix = FAAC_POW(10, (faac_real)sfac / sfstep);
      } else {
          sfac = SF_OFFSET - coderInfo->sf[coderInfo->bandcnt];
          if (coderInfo->sf[coderInfo->bandcnt] < 10) sfacfix = (faac_real)0.0;
          else sfacfix = FAAC_POW(10, (faac_real)sfac / sfstep);
      }

      end -= start;
      xi = xitab;
      if (sfacfix <= (faac_real)0.0)
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
          if (final_pass) {
              int i;
              for (i = 0; i < gsize * end; i++) {
                  if (xitab[i] > 8191) xitab[i] = 8191;
                  else if (xitab[i] < -8191) xitab[i] = -8191;
              }
          }
      }
      huffbook(coderInfo, xitab, gsize * end);
      coderInfo->bandcnt++;
    }
}"""

new_bloc_quant = """int BlocQuant(CoderInfo * __restrict coder, faac_real * __restrict xr, AACQuantCfg *aacquantCfg)
{
    faac_real bandlvl[MAX_SCFAC_BANDS];
    faac_real bandenrg[MAX_SCFAC_BANDS];
    int cnt;
    faac_real *gxr;
    int lastis, lastsf, lastpns, initpns;
    int changed = 0;

    coder->global_gain = 0;
    coder->bandcnt = 0;
    coder->datacnt = 0;

    // Pass 1: Initial selection and quantization
    gxr = xr;
    for (cnt = 0; cnt < coder->groups.n; cnt++)
    {
        bmask(coder, gxr, bandlvl, bandenrg, cnt, (faac_real)aacquantCfg->quality/DEFQUAL);
        qlevel(coder, gxr, bandlvl, bandenrg, cnt, aacquantCfg->pnslevel, 0);
        gxr += coder->groups.len[cnt] * BLOCK_LEN_SHORT;
    }

    // Determine global_gain
    for (cnt = 0; cnt < coder->bandcnt; cnt++)
    {
        int book = coder->book[cnt];
        if (book && (book != HCB_INTENSITY) && (book != HCB_INTENSITY2))
        {
            coder->global_gain = coder->sf[cnt];
            break;
        }
    }

    // Clamping loop
    lastsf = coder->global_gain;
    lastis = 0;
    lastpns = coder->global_gain - 90;
    initpns = 1;

    for (cnt = 0; cnt < coder->bandcnt; cnt++)
    {
        int book = coder->book[cnt];
        if ((book == HCB_INTENSITY) || (book == HCB_INTENSITY2))
        {
            int diff = coder->sf[cnt] - lastis;
            if (diff < -60) { diff = -60; changed = 1; }
            else if (diff > 60) { diff = 60; changed = 1; }
            lastis += diff;
            coder->sf[cnt] = lastis;
        }
        else if (book == HCB_PNS)
        {
            int diff = coder->sf[cnt] - lastpns;
            if (initpns)
            {
                initpns = 0;
                lastpns += diff;
            }
            else
            {
                if (diff < -60) { diff = -60; changed = 1; }
                else if (diff > 60) { diff = 60; changed = 1; }
                lastpns += diff;
                coder->sf[cnt] = lastpns;
            }
        }
        else if (book)
        {
            int diff = coder->sf[cnt] - lastsf;
            if (diff < -60) { diff = -60; changed = 1; }
            else if (diff > 60) { diff = 60; changed = 1; }
            lastsf += diff;
            coder->sf[cnt] = lastsf;
        }
    }

    // Pass 2: Final quantization if any SF changed
    if (changed)
    {
        coder->datacnt = 0;
        coder->bandcnt = 0;
        gxr = xr;
        for (cnt = 0; cnt < coder->groups.n; cnt++)
        {
            bmask(coder, gxr, bandlvl, bandenrg, cnt, (faac_real)aacquantCfg->quality/DEFQUAL);
            qlevel(coder, gxr, bandlvl, bandenrg, cnt, aacquantCfg->pnslevel, 1);
            gxr += coder->groups.len[cnt] * BLOCK_LEN_SHORT;
        }
    }
    return 1;
}"""

content = re.sub(r"static void qlevel\(CoderInfo \* __restrict coderInfo,[\s\S]*?\}\n\}", new_qlevel, content)
content = re.sub(r"int BlocQuant\(CoderInfo \* __restrict coder, faac_real \* __restrict xr, AACQuantCfg \*aacquantCfg\)[\s\S]*?return 1;\n    \}\n    return 0;\n\}", new_bloc_quant, content)

open('libfaac/quantize.c', 'w').write(content)
