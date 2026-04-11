/****************************************************************************
    Intensity Stereo

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

#define _USE_MATH_DEFINES

#include <math.h>
#include "stereo.h"
#include "huff2.h"

#define LOG2_CONST 3.32192809489f

static void mixed_mode(CoderInfo *cl, CoderInfo *cr, ChannelInfo *chi,
                       faac_real *sl0, faac_real *sr0, int *sfcnt,
                       int wstart, int wend, faac_real quality)
{
    int sfb;
    int win;
    int sfmin = (cl->block_type == ONLY_SHORT_WINDOW) ? 1 : 8;
    const faac_real step = 10/1.50515;

    for (sfb = 0; sfb < sfmin; sfb++) {
        chi->msInfo.ms_used[*sfcnt] = 0;
        (*sfcnt)++;
    }

    for (sfb = sfmin; sfb < cl->sfbn; sfb++) {
        int l, start, end;
        faac_real enrgl = 0, enrgr = 0, correl = 0;
        start = cl->sfb_offset[sfb];
        end = cl->sfb_offset[sfb + 1];
        int len = end - start;

        for (win = wstart; win < wend; win++) {
            faac_real *sl = sl0 + win * BLOCK_LEN_SHORT;
            faac_real *sr = sr0 + win * BLOCK_LEN_SHORT;
            for (l = start; l < end; l++) {
                faac_real lx = sl[l];
                faac_real rx = sr[l];
                enrgl += lx * lx;
                enrgr += rx * rx;
                correl += lx * rx;
            }
        }

        faac_real enrgm = 0.25f * (enrgl + enrgr + 2.0f * correl);
        faac_real enrgs = 0.25f * (enrgl + enrgr - 2.0f * correl);

        float bits_lr = 0.5f * len * (float)((FAAC_LOG10(enrgl/len + 1.0f) + FAAC_LOG10(enrgr/len + 1.0f)) * LOG2_CONST);
        float bits_ms = 0.5f * len * (float)((FAAC_LOG10(enrgm/len + 1.0f) + FAAC_LOG10(enrgs/len + 1.0f)) * LOG2_CONST);
        float bits_is = 0.5f * len * (float)(FAAC_LOG10(enrgm/len + 1.0f) * LOG2_CONST);

        float r = 0;
        if (enrgl > 1e-15f && enrgr > 1e-15f)
            r = (float)(correl / FAAC_SQRT(enrgl * enrgr));

        float is_penalty = 1.10f;
        if (cl->block_type == ONLY_SHORT_WINDOW) is_penalty *= 1.15f;
        if (quality > 1.0f) is_penalty *= 1.10f;
        bits_is *= is_penalty;

        int use_ms = (bits_ms < bits_lr);
        int use_is = (bits_is < bits_lr && bits_is < bits_ms && r > 0.90f && sfb > 4);

        if (use_is) {
            faac_real efix = enrgl + enrgr;
            faac_real enrgs_legacy = 4.0f * enrgm;
            faac_real vfix = FAAC_SQRT(efix / (enrgs_legacy + 1e-15f));
            int sf = FAAC_LRINT(FAAC_LOG10(max(enrgl, 1e-15f) / (efix + 1e-15f)) * step);
            int pan = FAAC_LRINT(FAAC_LOG10(max(enrgr, 1e-15f) / (efix + 1e-15f)) * step) - sf;

            if (pan <= 30 && pan >= -30) {
                cl->sf[*sfcnt] = sf;
                cr->sf[*sfcnt] = -pan;
                cr->book[*sfcnt] = HCB_INTENSITY;
                for (win = wstart; win < wend; win++) {
                    faac_real *sl = sl0 + win * BLOCK_LEN_SHORT;
                    faac_real *sr = sr0 + win * BLOCK_LEN_SHORT;
                    for (l = start; l < end; l++) {
                        faac_real sum = sl[l] + sr[l];
                        sl[l] = sum * vfix;
                        sr[l] = 0;
                    }
                }
                chi->msInfo.ms_used[*sfcnt] = 0;
            } else {
                use_is = 0;
            }
        }

        if (!use_is && use_ms) {
            chi->msInfo.ms_used[*sfcnt] = 1;
            for (win = wstart; win < wend; win++) {
                faac_real *sl = sl0 + win * BLOCK_LEN_SHORT;
                faac_real *sr = sr0 + win * BLOCK_LEN_SHORT;
                for (l = start; l < end; l++) {
                    faac_real lx = sl[l];
                    faac_real rx = sr[l];
                    sl[l] = 0.5f * (lx + rx);
                    sr[l] = 0.5f * (lx - rx);
                }
            }
        } else if (!use_is) {
            chi->msInfo.ms_used[*sfcnt] = 0;
        }
        (*sfcnt)++;
    }
}

static void stereo(CoderInfo *cl, CoderInfo *cr,
                   faac_real *sl0, faac_real *sr0, int *sfcnt,
                   int wstart, int wend, faac_real phthr
                  )
{
    int sfb;
    int win;
    int sfmin;

    if (!phthr)
        return;

    phthr = 1.0 / phthr;

    if (cl->block_type == ONLY_SHORT_WINDOW)
        sfmin = 1;
    else
        sfmin = 8;

    (*sfcnt) += sfmin;

    for (sfb = sfmin; sfb < cl->sfbn; sfb++)
    {
        int l, start, end;
        faac_real sum, diff;
        faac_real enrgs, enrgd, enrgl, enrgr;
        int hcb = HCB_NONE;
        const faac_real step = 10/1.50515;
        faac_real ethr;
        faac_real vfix, efix;

        start = cl->sfb_offset[sfb];
        end = cl->sfb_offset[sfb + 1];

        enrgs = enrgd = enrgl = enrgr = 0.0;
        for (win = wstart; win < wend; win++)
        {
            faac_real *sl = sl0 + win * BLOCK_LEN_SHORT;
            faac_real *sr = sr0 + win * BLOCK_LEN_SHORT;

            for (l = start; l < end; l++)
            {
                faac_real lx = sl[l];
                faac_real rx = sr[l];

                sum = lx + rx;
                diff = lx - rx;

                enrgs += sum * sum;
                enrgd += diff * diff;
                enrgl += lx * lx;
                enrgr += rx * rx;
            }
        }

        ethr = FAAC_SQRT(enrgl) + FAAC_SQRT(enrgr);
        ethr *= ethr;
        ethr *= phthr;
        efix = enrgl + enrgr;
        if (enrgs >= ethr)
        {
            hcb = HCB_INTENSITY;
            vfix = FAAC_SQRT(efix / enrgs);
        }
        else if (enrgd >= ethr)
        {
            hcb = HCB_INTENSITY2;
            vfix = FAAC_SQRT(efix / enrgd);
        }

        if (hcb != HCB_NONE)
        {
            int sf = FAAC_LRINT(FAAC_LOG10(enrgl / efix) * step);
            int pan = FAAC_LRINT(FAAC_LOG10(enrgr/efix) * step) - sf;

            if (pan > 30)
            {
                cl->book[*sfcnt] = HCB_ZERO;
                (*sfcnt)++;
                continue;
            }
            if (pan < -30)
            {
                cr->book[*sfcnt] = HCB_ZERO;
                (*sfcnt)++;
                continue;
            }
            cl->sf[*sfcnt] = sf;
            cr->sf[*sfcnt] = -pan;
            cr->book[*sfcnt] = hcb;

            for (win = wstart; win < wend; win++)
            {
                faac_real *sl = sl0 + win * BLOCK_LEN_SHORT;
                faac_real *sr = sr0 + win * BLOCK_LEN_SHORT;
                for (l = start; l < end; l++)
                {
                    if (hcb == HCB_INTENSITY)
                        sum = sl[l] + sr[l];
                    else
                        sum = sl[l] - sr[l];

                    sl[l] = sum * vfix;
                }
            }
        }
        (*sfcnt)++;
    }
}

static void midside(CoderInfo *coder, ChannelInfo *channel,
                    faac_real *sl0, faac_real *sr0, int *sfcnt,
                    int wstart, int wend,
                    faac_real thrmid, faac_real thrside
                   )
{
    int sfb;
    int win;
    int sfmin;

    if (coder->block_type == ONLY_SHORT_WINDOW)
        sfmin = 1;
    else
        sfmin = 8;

    for (sfb = 0; sfb < sfmin; sfb++)
    {
        channel->msInfo.ms_used[*sfcnt] = 0;
        (*sfcnt)++;
    }
    for (sfb = sfmin; sfb < coder->sfbn; sfb++)
    {
        int ms = 0;
        int l, start, end;
        faac_real sum, diff;
        faac_real enrgs, enrgd, enrgl, enrgr;

        start = coder->sfb_offset[sfb];
        end = coder->sfb_offset[sfb + 1];

        enrgs = enrgd = enrgl = enrgr = 0.0;
        for (win = wstart; win < wend; win++)
        {
            faac_real *sl = sl0 + win * BLOCK_LEN_SHORT;
            faac_real *sr = sr0 + win * BLOCK_LEN_SHORT;

            for (l = start; l < end; l++)
            {
                faac_real lx = sl[l];
                faac_real rx = sr[l];

                sum = 0.5 * (lx + rx);
                diff = 0.5 * (lx - rx);

                enrgs += sum * sum;
                enrgd += diff * diff;
                enrgl += lx * lx;
                enrgr += rx * rx;
            }
        }

        if ((min(enrgl, enrgr) * thrmid) >= max(enrgs, enrgd))
        {
            enum {PH_NONE, PH_IN, PH_OUT};
            int phase = PH_NONE;

            if ((enrgs * thrmid * 2.0) >= (enrgl + enrgr))
            {
                ms = 1;
                phase = PH_IN;
            }
            else if ((enrgd * thrmid * 2.0) >= (enrgl + enrgr))
            {
                ms = 1;
                phase = PH_OUT;
            }

            if (ms)
            {
                for (win = wstart; win < wend; win++)
                {
                    faac_real *sl = sl0 + win * BLOCK_LEN_SHORT;
                    faac_real *sr = sr0 + win * BLOCK_LEN_SHORT;
                    for (l = start; l < end; l++)
                    {
                        if (phase == PH_IN)
                        {
                            sum = sl[l] + sr[l];
                            diff = 0;
                        }
                        else
                        {
                            sum = 0;
                            diff = sl[l] - sr[l];
                        }

                        sl[l] = 0.5 * sum;
                        sr[l] = 0.5 * diff;
                    }
                }
            }
        }

        if (min(enrgl, enrgr) <= (thrside * max(enrgl, enrgr)))
        {
            for (win = wstart; win < wend; win++)
            {
                faac_real *sl = sl0 + win * BLOCK_LEN_SHORT;
                faac_real *sr = sr0 + win * BLOCK_LEN_SHORT;
                for (l = start; l < end; l++)
                {
                    if (enrgl < enrgr)
                        sl[l] = 0.0;
                    else
                        sr[l] = 0.0;
                }
            }
        }

        channel->msInfo.ms_used[*sfcnt] = ms;
        (*sfcnt)++;
    }
}


void AACstereo(CoderInfo *coder,
               ChannelInfo *channel,
               faac_real *s[MAX_CHANNELS],
               int maxchan,
               faac_real quality,
               int mode
              )
{
    int chn;
    static const faac_real thr075 = 1.09 /* ~0.75dB */ - 1.0;
    static const faac_real thrmax = 1.25 /* ~2dB */ - 1.0;
    static const faac_real sidemin = 0.1; /* -20dB */
    static const faac_real sidemax = 0.3; /* ~-10.5dB */
    static const faac_real isthrmax = M_SQRT2 - 1.0;
    faac_real thrmid, thrside;
    faac_real isthr;

    thrmid = 1.0;
    thrside = 0.0;
    isthr = 1.0;

    switch (mode)
    {
    case JOINT_MS:
        thrmid = thr075 / quality;
        if (thrmid > thrmax)
            thrmid = thrmax;

        thrside = sidemin / quality;
        if (thrside > sidemax)
            thrside = sidemax;

        thrmid += 1.0;
        break;
    case JOINT_IS:
        isthr = 0.18 / (quality * quality);
        if (isthr > isthrmax)
            isthr = isthrmax;

        isthr += 1.0;
        break;
    }

    // convert into energy
    thrmid *= thrmid;
    thrside *= thrside;
    isthr *= isthr;

    for (chn = 0; chn < maxchan; chn++)
    {
        int group;
        int bookcnt = 0;
        CoderInfo *cp = coder + chn;

        if (!channel[chn].present)
            continue;

        for (group = 0; group < cp->groups.n; group++)
        {
            int band;
            for (band = 0; band < cp->sfbn; band++)
            {
                cp->book[bookcnt] = HCB_NONE;
                cp->sf[bookcnt] = 0;
                bookcnt++;
            }
        }
    }
    for (chn = 0; chn < maxchan; chn++)
    {
        int rch;
        int cnt;
        int group;
        int sfcnt = 0;
        int start = 0;

        if (!channel[chn].present)
            continue;
        if (!((channel[chn].type == ELEMENT_CPE) && (channel[chn].ch_is_left)))
            continue;

        rch = channel[chn].paired_ch;

        channel[chn].common_window = 0;
        channel[chn].msInfo.is_present = 0;
        channel[rch].msInfo.is_present = 0;

        if (coder[chn].block_type != coder[rch].block_type)
            continue;
        if (coder[chn].groups.n != coder[rch].groups.n)
            continue;

        channel[chn].common_window = 1;
        for (cnt = 0; cnt < coder[chn].groups.n; cnt++)
            if (coder[chn].groups.len[cnt] != coder[rch].groups.len[cnt])
            {
                channel[chn].common_window = 0;
                goto skip;
            }

        if (mode == JOINT_MS || mode == JOINT_MIXED)
        {
            channel[chn].common_window = 1;
            channel[chn].msInfo.is_present = 1;
            channel[rch].msInfo.is_present = 1;
        }

        for (group = 0; group < coder[chn].groups.n; group++)
        {
            int end = start + coder[chn].groups.len[group];
            switch(mode) {
            case JOINT_MS:
                midside(coder + chn, channel + chn, s[chn], s[rch], &sfcnt,
                        start, end, thrmid, thrside);
                break;
            case JOINT_IS:
                stereo(coder + chn, coder + rch, s[chn], s[rch], &sfcnt, start, end, isthr);
                break;
            case JOINT_MIXED:
                mixed_mode(coder + chn, coder + rch, channel + chn, s[chn], s[rch], &sfcnt, start, end, quality);
                break;
            }
            start = end;
        }
        skip:;
    }
}
