/*
 * mxu2_shim.h - Ingenic MXU2 128-bit VPR intrinsics
 */
#ifndef MXU2_SHIM_H
#define MXU2_SHIM_H
#include <signal.h>
#include <setjmp.h>
#include <string.h>
#include <stdint.h>

#ifdef __mips__
typedef float mxu2_v4f32 __attribute__((vector_size(16), aligned(16)));
typedef int mxu2_v4i32 __attribute__((vector_size(16), aligned(16)));
typedef signed char mxu2_v16i8 __attribute__((vector_size(16), aligned(16)));
typedef short mxu2_v8i16 __attribute__((vector_size(16), aligned(16)));
typedef unsigned char mxu2_v16u8 __attribute__((vector_size(16), aligned(16)));
typedef unsigned short mxu2_v8u16 __attribute__((vector_size(16), aligned(16)));
typedef unsigned int mxu2_v4u32 __attribute__((vector_size(16), aligned(16)));

#define _MXU2_WORD(enc) ".word " #enc "\n\t"
#define _MXU2_LU1Q(base, idx, vpr) ((0x1C<<26)|((base)<<21)|((idx)<<11)|((vpr)<<6)|0x14)
#define _MXU2_SU1Q(base, idx, vpr) ((0x1C<<26)|((base)<<21)|((idx)<<11)|((vpr)<<6)|0x1C)
#define _MXU2_OP(major, vt, vs, vd, minor) ((18<<26)|(1<<25)|((major)<<21)|((vt)<<16)|((vs)<<11)|((vd)<<6)|(minor))

static inline mxu2_v4i32 mxu2_load(const void *ptr) {
    mxu2_v4i32 r;
    __asm__ __volatile__ ("move $t0, %0\n\t" _MXU2_WORD(0x71000014) "move $t0, %1\n\t" _MXU2_WORD(0x7100001C) : : "r"(ptr), "r"(&r) : "$t0", "memory");
    return r;
}
static inline void mxu2_store(void *ptr, mxu2_v4i32 v) {
    __asm__ __volatile__ ("move $t0, %0\n\t" _MXU2_WORD(0x71000014) "move $t0, %1\n\t" _MXU2_WORD(0x7100001C) : : "r"(&v), "r"(ptr) : "$t0", "memory");
}

#define _MXU2_BINOP(ret, a, b, op) do { \
    mxu2_v4i32 _a=a, _b=b, _r; \
    __asm__ __volatile__ ("move $t0, %1\n\t" _MXU2_WORD(0x71000014) "move $t0, %2\n\t" _MXU2_WORD(0x71000414) _MXU2_WORD(op) "move $t0, %0\n\t" _MXU2_WORD(0x7100081C) : : "r"(&_r), "r"(&_a), "r"(&_b) : "$t0", "memory"); \
    ret = _r; \
} while(0)

static inline mxu2_v16i8 mxu2_andv(mxu2_v16i8 a, mxu2_v16i8 b) { mxu2_v4i32 r; _MXU2_BINOP(r, (mxu2_v4i32)a, (mxu2_v4i32)b, _MXU2_OP(6, 1, 0, 2, 0x38)); return (mxu2_v16i8)r; }
static inline mxu2_v16i8 mxu2_xorv(mxu2_v16i8 a, mxu2_v16i8 b) { mxu2_v4i32 r; _MXU2_BINOP(r, (mxu2_v4i32)a, (mxu2_v4i32)b, _MXU2_OP(6, 1, 0, 2, 0x3B)); return (mxu2_v16i8)r; }
static inline mxu2_v4i32 mxu2_clts_w(mxu2_v4i32 a, mxu2_v4i32 b) { mxu2_v4i32 r; _MXU2_BINOP(r, a, b, _MXU2_OP(0, 1, 0, 2, 0x32)); return r; }
static inline mxu2_v4f32 mxu2_fmul_w(mxu2_v4f32 a, mxu2_v4f32 b) { mxu2_v4i32 r; _MXU2_BINOP(r, (mxu2_v4i32)a, (mxu2_v4i32)b, _MXU2_OP(8, 1, 0, 2, 0x04)); return (mxu2_v4f32)r; }
static inline mxu2_v4f32 mxu2_fadd_w(mxu2_v4f32 a, mxu2_v4f32 b) { mxu2_v4i32 r; _MXU2_BINOP(r, (mxu2_v4i32)a, (mxu2_v4i32)b, _MXU2_OP(8, 1, 0, 2, 0x00)); return (mxu2_v4f32)r; }
static inline mxu2_v4i32 mxu2_sub_w(mxu2_v4i32 a, mxu2_v4i32 b) { mxu2_v4i32 r; _MXU2_BINOP(r, a, b, _MXU2_OP(1, 1, 0, 2, 0x2E)); return r; }

#define _MXU2_UNIOP(ret, a, op) do { \
    mxu2_v4i32 _a=a, _r; \
    __asm__ __volatile__ ("move $t0, %1\n\t" _MXU2_WORD(0x71000014) _MXU2_WORD(op) "move $t0, %0\n\t" _MXU2_WORD(0x7100081C) : : "r"(&_r), "r"(&_a) : "$t0", "memory"); \
    ret = _r; \
} while(0)

static inline mxu2_v4i32 mxu2_fsqrt_w(mxu2_v4i32 a) { mxu2_v4i32 r; _MXU2_UNIOP(r, a, _MXU2_OP(14, 1, 0, 2, 0x00)); return r; }
static inline mxu2_v4i32 mxu2_vtruncsws(mxu2_v4i32 a) { mxu2_v4i32 r; _MXU2_UNIOP(r, a, _MXU2_OP(14, 1, 0, 2, 0x14)); return r; }

static sigjmp_buf _mxu2_probe_jmp;
static void _mxu2_probe_sigill(int s) { (void)s; siglongjmp(_mxu2_probe_jmp, 1); }
static inline int mxu2_available(void) {
    static int cached = -1; if (cached >= 0) return cached;
    struct sigaction sa, old; memset(&sa, 0, sizeof(sa)); sa.sa_handler = _mxu2_probe_sigill; sigaction(SIGILL, &sa, &old);
    if (sigsetjmp(_mxu2_probe_jmp, 1) == 0) {
        mxu2_v4i32 a = {0}, b = {0}, r; _MXU2_BINOP(r, a, b, _MXU2_OP(1, 1, 0, 2, 0x20)); cached = 1;
    } else { cached = 0; }
    sigaction(SIGILL, &old, NULL); return cached;
}
#endif
#endif
