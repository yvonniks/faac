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

#include "cpu_compute.h"

#if defined(SSE2_ARCH)
# if defined(_MSC_VER)
#  include <intrin.h>
# else
#  include <cpuid.h>
# endif
#endif

#if defined(MIPS_ARCH)
#include "mxu2_shim.h"
#include "mxu3_shim.h"
#endif

CPUCaps get_cpu_caps(void)
{
    CPUCaps caps = CPU_CAP_NONE;

#if defined(SSE2_ARCH)
# if defined(_MSC_VER)
    int cpuInfo[4];
    __cpuid(cpuInfo, 1);
    if (cpuInfo[3] & (1 << 26)) caps |= CPU_CAP_SSE2;
# else
    unsigned int eax, ebx, ecx, edx;
    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
        if (edx & (1 << 26)) caps |= CPU_CAP_SSE2;
    }
# endif
#endif

#if defined(MIPS_ARCH)
    if (mxu3_available())
        caps |= CPU_CAP_MXU3;
    else if (mxu2_available())
        caps |= CPU_CAP_MXU2;
#endif

    return caps;
}
