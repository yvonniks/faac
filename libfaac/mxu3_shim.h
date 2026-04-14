/*
 * mxu3_shim.h - Ingenic MXU3 512-bit VPR intrinsics
 * Based on work by gtxaspec at https://github.com/gtxaspec/ingenic-mxu
 */
#ifndef MXU3_SHIM_H
#define MXU3_SHIM_H
#include <signal.h>
#include <setjmp.h>
#include <string.h>
#include <stdint.h>

#ifdef __mips__
typedef float mxu3_v16f32 __attribute__((vector_size(64), aligned(64)));
typedef int mxu3_v16i32 __attribute__((vector_size(64), aligned(64)));
typedef unsigned int mxu3_v16u32 __attribute__((vector_size(64), aligned(64)));

static inline mxu3_v16i32 MXU3_LOAD(const void *p) {
    mxu3_v16i32 r;
    __asm__ __volatile__ ("move $t0, %0\n\t" ".word 0x71000813\n\t.word 0x71000853\n\t.word 0x71000893\n\t.word 0x710008D3" "\n\t" "move $t0, %1\n\t" ".word 0x71000057\n\t.word 0x71000097\n\t.word 0x710000D7\n\t.word 0x71000117" : : "r"(p), "r"(&r) : "$t0", "memory");
    return r;
}
static inline void MXU3_STORE(void *p, mxu3_v16i32 v) {
    __asm__ __volatile__ ("move $t0, %0\n\t" ".word 0x71000813\n\t.word 0x71000853\n\t.word 0x71000893\n\t.word 0x710008D3" "\n\t" "move $t0, %1\n\t" ".word 0x71000057\n\t.word 0x71000097\n\t.word 0x710000D7\n\t.word 0x71000117" : : "r"(&v), "r"(p) : "$t0", "memory");
}

#define _MXU3_BINOP(ret, a, b, op) do { \
    mxu3_v16i32 _a=a, _b=b, _r; \
    __asm__ __volatile__ ("move $t0, %1\n\t" ".word 0x71000813\n\t.word 0x71000853\n\t.word 0x71000893\n\t.word 0x710008D3" "\n\t" "move $t0, %2\n\t" ".word 0x71000913\n\t.word 0x71000953\n\t.word 0x71000993\n\t.word 0x710009D3" "\n\t" ".word " #op "+0x000\n\t.word " #op "+0x040\n\t.word " #op "+0x080\n\t.word " #op "+0x0C0" "\n\t" "move $t0, %0\n\t" ".word 0x71000257\n\t.word 0x71000297\n\t.word 0x710002D7\n\t.word 0x71000317" : : "r"(&_r), "r"(&_a), "r"(&_b) : "$t0", "memory"); \
    ret = _r; \
} while(0)

static inline mxu3_v16i32 mxu3_addw(mxu3_v16i32 a, mxu3_v16i32 b) { mxu3_v16i32 r; _MXU3_BINOP(r, a, b, 0x4a800002); return r; }
static inline mxu3_v16i32 mxu3_subw(mxu3_v16i32 a, mxu3_v16i32 b) { mxu3_v16i32 r; _MXU3_BINOP(r, a, b, 0x4a80000a); return r; }
static inline mxu3_v16i32 mxu3_faddw(mxu3_v16i32 a, mxu3_v16i32 b) { mxu3_v16i32 r; _MXU3_BINOP(r, a, b, 0x4a800003); return r; }
static inline mxu3_v16i32 mxu3_fsubw(mxu3_v16i32 a, mxu3_v16i32 b) { mxu3_v16i32 r; _MXU3_BINOP(r, a, b, 0x4a80000b); return r; }
static inline mxu3_v16i32 mxu3_fmulw(mxu3_v16i32 a, mxu3_v16i32 b) { mxu3_v16i32 r; _MXU3_BINOP(r, a, b, 0x4a600023); return r; }
static inline mxu3_v16i32 mxu3_andv(mxu3_v16i32 a, mxu3_v16i32 b) { mxu3_v16i32 r; _MXU3_BINOP(r, a, b, 0x4a600002); return r; }
static inline mxu3_v16i32 mxu3_xorv(mxu3_v16i32 a, mxu3_v16i32 b) { mxu3_v16i32 r; _MXU3_BINOP(r, a, b, 0x4a600006); return r; }
static inline mxu3_v16i32 mxu3_cltsw(mxu3_v16i32 a, mxu3_v16i32 b) { mxu3_v16i32 r; _MXU3_BINOP(r, a, b, 0x4a000036); return r; }
static inline mxu3_v16i32 mxu3_srlw(mxu3_v16i32 a, mxu3_v16i32 b) { mxu3_v16i32 r; _MXU3_BINOP(r, a, b, 0x4a200032); return r; }

#define _MXU3_UNIOP(ret, a, op) do { \
    mxu3_v16i32 _a=a, _r; \
    __asm__ __volatile__ ("move $t0, %1\n\t" ".word 0x71000813\n\t.word 0x71000853\n\t.word 0x71000893\n\t.word 0x710008D3" "\n\t" ".word " #op "+0x000\n\t.word " #op "+0x040\n\t.word " #op "+0x080\n\t.word " #op "+0x0C0" "\n\t" "move $t0, %0\n\t" ".word 0x71000257\n\t.word 0x71000297\n\t.word 0x710002D7\n\t.word 0x71000317" : : "r"(&_r), "r"(&_a) : "$t0", "memory"); \
    ret = _r; \
} while(0)

static inline mxu3_v16i32 mxu3_ftruncsw(mxu3_v16i32 a) { mxu3_v16i32 r; _MXU3_UNIOP(r, a, 0x70c0002e); return r; }

static sigjmp_buf _mxu3_sigill_jmp;
static void _mxu3_sigill_handler(int s) { (void)s; siglongjmp(_mxu3_sigill_jmp, 1); }
static inline int mxu3_available(void) {
    static int cached = -1; if (cached >= 0) return cached;
    struct sigaction sa, old; memset(&sa, 0, sizeof(sa)); sa.sa_handler = _mxu3_sigill_handler; sigaction(SIGILL, &sa, &old);
    if (sigsetjmp(_mxu3_sigill_jmp, 1) == 0) {
        mxu3_v16i32 a = {0}, b = {0}, r; r = mxu3_addw(a, b); cached = 1;
    } else { cached = 0; }
    sigaction(SIGILL, &old, NULL); return cached;
}
#endif
#endif
