/*
 * mxu2_shim.h - Ingenic MXU2 128-bit VPR intrinsics
 * Based on work by gtxaspec at https://github.com/gtxaspec/ingenic-mxu
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

#define _MXU2_WORD(enc) ".word " #enc "\n\t"

static inline mxu2_v4i32 mxu2_load(const void *ptr) {
    mxu2_v4i32 r;
    __asm__ __volatile__ ("move $t0, %0\n\t" ".word 0x71000014" "\n\t" "move $t0, %1\n\t" ".word 0x7100001C" : : "r"(ptr), "r"(&r) : "$t0", "memory");
    return r;
}
static inline void mxu2_store(void *ptr, mxu2_v4i32 v) {
    __asm__ __volatile__ ("move $t0, %0\n\t" ".word 0x71000014" "\n\t" "move $t0, %1\n\t" ".word 0x7100001C" : : "r"(&v), "r"(ptr) : "$t0", "memory");
}

#define _MXU2_BINOP(ret, a, b, op) do { \
    mxu2_v4i32 _a=a, _b=b, _r; \
    __asm__ __volatile__ ("move $t0, %1\n\t" ".word 0x71000014" "\n\t" "move $t0, %2\n\t" ".word 0x71000414" "\n\t" ".word " #op "\n\t" "move $t0, %0\n\t" ".word 0x7100081C" : : "r"(&_r), "r"(&_a), "r"(&_b) : "$t0", "memory"); \
    ret = _r; \
} while(0)

static inline mxu2_v16i8 mxu2_andv(mxu2_v16i8 a, mxu2_v16i8 b) { mxu2_v4i32 r; _MXU2_BINOP(r, (mxu2_v4i32)a, (mxu2_v4i32)b, 0x48D00038); return (mxu2_v16i8)r; }
static inline mxu2_v16i8 mxu2_xorv(mxu2_v16i8 a, mxu2_v16i8 b) { mxu2_v4i32 r; _MXU2_BINOP(r, (mxu2_v4i32)a, (mxu2_v4i32)b, 0x48D0003B); return (mxu2_v16i8)r; }
static inline mxu2_v4i32 mxu2_clts_w(mxu2_v4i32 a, mxu2_v4i32 b) { mxu2_v4i32 r; _MXU2_BINOP(r, a, b, 0x48100032); return r; }
static inline mxu2_v4f32 mxu2_fmul_w(mxu2_v4f32 a, mxu2_v4f32 b) { mxu2_v4i32 r; _MXU2_BINOP(r, (mxu2_v4i32)a, (mxu2_v4i32)b, 0x49100004); return (mxu2_v4f32)r; }
static inline mxu2_v4f32 mxu2_fadd_w(mxu2_v4f32 a, mxu2_v4f32 b) { mxu2_v4i32 r; _MXU2_BINOP(r, (mxu2_v4i32)a, (mxu2_v4i32)b, 0x49100000); return (mxu2_v4f32)r; }
static inline mxu2_v4i32 mxu2_sub_w(mxu2_v4i32 a, mxu2_v4i32 b) { mxu2_v4i32 r; _MXU2_BINOP(r, a, b, 0x4830002E); return r; }

#define _MXU2_UNIOP(ret, a, op) do { \
    mxu2_v4i32 _a=a, _r; \
    __asm__ __volatile__ ("move $t0, %1\n\t" ".word 0x71000014" "\n\t" ".word " #op "\n\t" "move $t0, %0\n\t" ".word 0x7100081C" : : "r"(&_r), "r"(&_a) : "$t0", "memory"); \
    ret = _r; \
} while(0)

static inline mxu2_v4i32 mxu2_fsqrt_w(mxu2_v4i32 a) { mxu2_v4i32 r; _MXU2_UNIOP(r, a, 0x49C00000); return r; }
static inline mxu2_v4i32 mxu2_vtruncsws(mxu2_v4i32 a) { mxu2_v4i32 r; _MXU2_UNIOP(r, a, 0x49C00014); return r; }

static sigjmp_buf _mxu2_probe_jmp;
static void _mxu2_probe_sigill(int s) { (void)s; siglongjmp(_mxu2_probe_jmp, 1); }
static inline int mxu2_available(void) {
    static int cached = -1; if (cached >= 0) return cached;
    struct sigaction sa, old; memset(&sa, 0, sizeof(sa)); sa.sa_handler = _mxu2_probe_sigill; sigaction(SIGILL, &sa, &old);
    if (sigsetjmp(_mxu2_probe_jmp, 1) == 0) {
        mxu2_v4i32 a = {0}, b = {0}, r; _MXU2_BINOP(r, a, b, 0x48300020); cached = 1;
    } else { cached = 0; }
    sigaction(SIGILL, &old, NULL); return cached;
}
#endif
#endif
