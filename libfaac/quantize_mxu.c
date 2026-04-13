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

void quantize_mxu2(const faac_real * __restrict xr, int * __restrict xi, int n, faac_real sfacfix)
{
    int cnt = 0;

#ifdef __mips__
    const mxu2_v4i32 zero_i = {0, 0, 0, 0};
    const float sfac_f = (float)sfacfix;
    const mxu2_v4f32 sfac = {sfac_f, sfac_f, sfac_f, sfac_f};
    const float magic_f = (float)MAGIC_NUMBER;
    const mxu2_v4f32 magic = {magic_f, magic_f, magic_f, magic_f};
    const mxu2_v4i32 abs_mask = {0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF};

    for (; cnt <= n - 4; cnt += 4)
    {
        mxu2_v4i32 x_orig_i = mxu2_load(&xr[cnt]);

        mxu2_v4i32 sign_mask = mxu2_clts_w(x_orig_i, zero_i);
        mxu2_v4f32 x = (mxu2_v4f32)mxu2_andv((mxu2_v16i8)x_orig_i, (mxu2_v16i8)abs_mask);

        x = mxu2_fmul_w(x, sfac);
        /* (x * sfac)^0.75 approx: sqrt( (x*sfac) * sqrt(x*sfac) ) */
        mxu2_v4f32 sqrt_x = (mxu2_v4f32)mxu2_fsqrt_w((mxu2_v4i32)x);
        x = mxu2_fmul_w(x, sqrt_x);
        x = (mxu2_v4f32)mxu2_fsqrt_w((mxu2_v4i32)x);
        x = mxu2_fadd_w(x, magic);

        mxu2_v4i32 xi_vec = mxu2_vtruncsws((mxu2_v4i32)x);
        xi_vec = (mxu2_v4i32)mxu2_sub_w((mxu2_v4i32)mxu2_xorv((mxu2_v16i8)xi_vec, (mxu2_v16i8)sign_mask), sign_mask);

        mxu2_store(&xi[cnt], xi_vec);
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
    const mxu3_v16i32 zero = {0};
    const float sfac_f = (float)sfacfix;
    uint32_t sfac_u; memcpy(&sfac_u, &sfac_f, 4);
    const mxu3_v16i32 sfac = {sfac_u,sfac_u,sfac_u,sfac_u,sfac_u,sfac_u,sfac_u,sfac_u,sfac_u,sfac_u,sfac_u,sfac_u,sfac_u,sfac_u,sfac_u,sfac_u};
    const float magic_f = (float)MAGIC_NUMBER;
    uint32_t magic_u; memcpy(&magic_u, &magic_f, 4);
    const mxu3_v16i32 magic = {magic_u,magic_u,magic_u,magic_u,magic_u,magic_u,magic_u,magic_u,magic_u,magic_u,magic_u,magic_u,magic_u,magic_u,magic_u,magic_u};
    const uint32_t am = 0x7FFFFFFF;
    const mxu3_v16i32 abs_mask = {am,am,am,am,am,am,am,am,am,am,am,am,am,am,am,am};

    for (; cnt <= n - 16; cnt += 16)
    {
        if (((uintptr_t)&xr[cnt] & 15) == 0 && ((uintptr_t)&xi[cnt] & 15) == 0) {
            mxu3_v16i32 x_orig = MXU3_LOAD(&xr[cnt]);

            mxu3_v16i32 sign_mask = mxu3_cltsw(x_orig, zero);
            mxu3_v16i32 x = mxu3_andv(x_orig, abs_mask);

            x = mxu3_fmulw(x, sfac);

            float *xf = (float *)&x;
            for(int i=0; i<16; i++) {
                xf[i] = sqrtf(xf[i] * sqrtf(xf[i]));
            }

            x = mxu3_faddw(x, magic);
            mxu3_v16i32 xi_vec = mxu3_ftsiw(x);

            xi_vec = mxu3_subw(mxu3_xorv(xi_vec, sign_mask), sign_mask);

            MXU3_STORE(&xi[cnt], xi_vec);
        } else {
            faac_real temp_xr[16] ALIGN_BASE(16);
            memcpy(temp_xr, &xr[cnt], 64);
            mxu3_v16i32 x_orig = MXU3_LOAD(temp_xr);

            mxu3_v16i32 sign_mask = mxu3_cltsw(x_orig, zero);
            mxu3_v16i32 x = mxu3_andv(x_orig, abs_mask);

            x = mxu3_fmulw(x, sfac);

            float *xf = (float *)&x;
            for(int i=0; i<16; i++) {
                xf[i] = sqrtf(xf[i] * sqrtf(xf[i]));
            }

            x = mxu3_faddw(x, magic);
            mxu3_v16i32 xi_vec = mxu3_ftsiw(x);

            xi_vec = mxu3_subw(mxu3_xorv(xi_vec, sign_mask), sign_mask);

            int temp_xi[16] ALIGN_BASE(16);
            MXU3_STORE(temp_xi, xi_vec);
            memcpy(&xi[cnt], temp_xi, 64);
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
