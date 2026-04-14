/*
 * FAAC - Freeware Advanced Audio Coder
 * Copyright (C) 2026 Nils Schimmelmann
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "faac_real.h"
#include "quantize.h"
#include "cpu_compute.h"
#include "util.h"

#ifndef FAAC_PRECISION_SINGLE
#error MXU SIMD quantization only supports single precision float.
#endif

#include "mxu2_shim.h"
#include "mxu3_shim.h"
#include <string.h>
#include <math.h>
#include <stdint.h>

#ifdef __mips__
typedef union {
    mxu3_v16i32 v;
    uint32_t u[16];
    float f[16];
} mxu3_union;

static inline mxu3_v16i32 mxu3_rsqrt_simd(mxu3_v16i32 x, mxu3_v16i32 mu, mxu3_v16i32 ou, mxu3_v16i32 thu, mxu3_v16i32 hu) {
    mxu3_v16i32 i = mxu3_subw(mu, mxu3_srlw(x, ou));
    mxu3_v16i32 xhalf = mxu3_fmulw(x, hu);

    mxu3_v16i32 y = i;
    mxu3_v16i32 y2 = mxu3_fmulw(y, y);
    mxu3_v16i32 term = mxu3_fmulw(xhalf, y2);
    mxu3_v16i32 factor = mxu3_fsubw(thu, term);
    y = mxu3_fmulw(y, factor);

    return y;
}

static inline mxu3_v16i32 mxu3_sqrt_simd(mxu3_v16i32 x, mxu3_v16i32 mu, mxu3_v16i32 ou, mxu3_v16i32 thu, mxu3_v16i32 hu) {
    mxu3_v16i32 rs = mxu3_rsqrt_simd(x, mu, ou, thu, hu);
    return mxu3_fmulw(x, rs);
}
#endif

void quantize_mxu2(const faac_real * __restrict xr, int * __restrict xi, int n, faac_real sfacfix)
{
    int cnt = 0;

#ifdef __mips__
    if (n >= 4) {
        const float sf_f = (float)sfacfix;
        const mxu2_v4f32 sfac_v = {sf_f, sf_f, sf_f, sf_f};
        const float mg_f = (float)MAGIC_NUMBER;
        const mxu2_v4f32 magic_v = {mg_f, mg_f, mg_f, mg_f};
        const mxu2_v4i32 abs_m = {0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF};
        const mxu2_v4i32 zero_v = {0, 0, 0, 0};

        for (; cnt <= n - 4; cnt += 4)
        {
            if (((uintptr_t)&xr[cnt] & 15) == 0 && ((uintptr_t)&xi[cnt] & 15) == 0) {
                mxu2_v4i32 x_orig_i = mxu2_load(&xr[cnt]);
                mxu2_v4i32 sign_mask = mxu2_clts_w(x_orig_i, zero_v);
                mxu2_v4f32 x = (mxu2_v4f32)mxu2_andv((mxu2_v16i8)x_orig_i, (mxu2_v16i8)abs_m);

                x = mxu2_fmul_w(x, sfac_v);
                /* (x * sfac)^0.75 approx: sqrt( (x*sfac) * sqrt(x*sfac) ) */
                mxu2_v4f32 sqrt_x = (mxu2_v4f32)mxu2_fsqrt_w((mxu2_v4i32)x);
                x = mxu2_fmul_w(x, sqrt_x);
                x = (mxu2_v4f32)mxu2_fsqrt_w((mxu2_v4i32)x);
                x = mxu2_fadd_w(x, magic_v);

                mxu2_v4i32 xi_vec = mxu2_vtruncsws((mxu2_v4i32)x);
                xi_vec = (mxu2_v4i32)mxu2_sub_w((mxu2_v4i32)mxu2_xorv((mxu2_v16i8)xi_vec, (mxu2_v16i8)sign_mask), sign_mask);

                mxu2_store(&xi[cnt], xi_vec);
            } else {
                break;
            }
        }
    }
#endif

    for (; cnt < n; cnt++)
    {
        faac_real val = xr[cnt];
        faac_real tmp = FAAC_FABS(val);
        tmp *= sfacfix;
        tmp = FAAC_SQRT(tmp * FAAC_SQRT(tmp));
        int q = (int)(tmp + (faac_real)MAGIC_NUMBER);
        xi[cnt] = (val < 0) ? -q : q;
    }
}

void quantize_mxu3(const faac_real * __restrict xr, int * __restrict xi, int n, faac_real sfacfix)
{
    int cnt = 0;

#ifdef __mips__
    if (n >= 16) {
        mxu3_union sfac_un, magic_un, abs_mask_un, mu_un, ou_un, thu_un, hu_un;
        float sf_f = (float)sfacfix;
        float mg_f = (float)MAGIC_NUMBER;
        for(int i=0; i<16; i++) {
            sfac_un.f[i] = sf_f;
            magic_un.f[i] = mg_f;
            abs_mask_un.u[i] = 0x7FFFFFFF;
            mu_un.u[i] = 0x5f3759df;
            ou_un.u[i] = 1;
            thu_un.f[i] = 1.5f;
            hu_un.f[i] = 0.5f;
        }

        const mxu3_v16i32 sfac_v = sfac_un.v;
        const mxu3_v16i32 magic_v = magic_un.v;
        const mxu3_v16i32 abs_m = abs_mask_un.v;
        const mxu3_v16i32 mu = mu_un.v;
        const mxu3_v16i32 ou = ou_un.v;
        const mxu3_v16i32 thu = thu_un.v;
        const mxu3_v16i32 hu = hu_un.v;
        const mxu3_v16i32 zero_v = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

        for (; cnt <= n - 16; cnt += 16)
        {
            if (((uintptr_t)&xr[cnt] & 63) == 0 && ((uintptr_t)&xi[cnt] & 63) == 0) {
                mxu3_v16i32 x_orig = MXU3_LOAD(&xr[cnt]);
                mxu3_v16i32 sign_mask = mxu3_cltsw(x_orig, zero_v);
                mxu3_v16i32 x = mxu3_andv(x_orig, abs_m);

                x = mxu3_fmulw(x, sfac_v);

                mxu3_v16i32 sqrt1 = mxu3_sqrt_simd(x, mu, ou, thu, hu);
                x = mxu3_fmulw(x, sqrt1);
                x = mxu3_sqrt_simd(x, mu, ou, thu, hu);

                x = mxu3_faddw(x, magic_v);
                mxu3_v16i32 xi_vec = mxu3_ftsiw(x);

                xi_vec = mxu3_subw(mxu3_xorv(xi_vec, sign_mask), sign_mask);

                MXU3_STORE(&xi[cnt], xi_vec);
            } else {
                break;
            }
        }
    }
#endif

    for (; cnt < n; cnt++)
    {
        faac_real val = xr[cnt];
        faac_real tmp = FAAC_FABS(val);
        tmp *= sfacfix;
        tmp = FAAC_SQRT(tmp * FAAC_SQRT(tmp));
        int q = (int)(tmp + (faac_real)MAGIC_NUMBER);
        xi[cnt] = (val < 0) ? -q : q;
    }
}
