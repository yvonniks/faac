import sys
import re

def fix_content(content):
    # Fix the objectIndex warnings by using a union for extraction
    # Patterns for extraction like ((signed char *)&v)[lane]

    # mtcpus_b
    content = content.replace(
        'return (int)((signed char *)&v)[lane];',
        '{ union { mxu2_v4i32 v; signed char b[16]; } _u; _u.v = v; return (int)_u.b[lane]; }'
    )
    # mtcpus_h
    content = content.replace(
        'return (int)((short *)&v)[lane];',
        '{ union { mxu2_v4i32 v; short h[8]; } _u; _u.v = v; return (int)_u.h[lane]; }'
    )
    # mtcpus_w/d
    content = content.replace(
        'return (int)((int *)&v)[lane];',
        '{ union { mxu2_v4i32 v; int w[4]; } _u; _u.v = v; return (int)_u.w[lane]; }'
    )
    # mtcpuu_b
    content = content.replace(
        'return (unsigned int)((unsigned char *)&v)[lane];',
        '{ union { mxu2_v4i32 v; unsigned char b[16]; } _u; _u.v = v; return (unsigned int)_u.b[lane]; }'
    )
    # mtcpuu_h
    content = content.replace(
        'return (unsigned int)((unsigned short *)&v)[lane];',
        '{ union { mxu2_v4i32 v; unsigned short h[8]; } _u; _u.v = v; return (unsigned int)_u.h[lane]; }'
    )
    # mtcpuu_w/d
    content = content.replace(
        'return (unsigned int)((unsigned int *)&v)[lane];',
        '{ union { mxu2_v4i32 v; unsigned int w[4]; } _u; _u.v = v; return (unsigned int)_u.w[lane]; }'
    )
    # mtfpu_w
    content = content.replace(
        'return ((float *)&v)[lane];',
        '{ union { mxu2_v4f32 v; float f[4]; } _u; _u.v = v; return _u.f[lane]; }'
    )
    # mtfpu_d
    content = content.replace(
        'return ((double *)&v)[lane];',
        '{ union { mxu2_v4i32 v; double d[2]; } _u; _u.v = v; return _u.d[lane]; }'
    )

    # repx functions
    content = content.replace(
        'signed char val = ((signed char *)&v)[lane];',
        'union { mxu2_v16i8 v; signed char b[16]; } _u; _u.v = v; signed char val = _u.b[lane];'
    )
    content = content.replace(
        'short val = ((short *)&v)[lane];',
        'union { mxu2_v8i16 v; short h[8]; } _u; _u.v = v; short val = _u.h[lane];'
    )
    content = content.replace(
        'int val = ((int *)&v)[lane];',
        'union { mxu2_v4i32 v; int w[4]; } _u; _u.v = v; int val = _u.w[lane];'
    )
    content = content.replace(
        'long long val = ((long long *)&v)[lane];',
        'union { mxu2_v4i32 v; long long d[2]; } _u; _u.v = v; long long val = _u.d[lane];'
    )

    # Fix branch predicates that use (p[0] | p[1] | p[2] | p[3])
    # Original:
    # static __inline__ int mxu2_bnez16b(mxu2_v16i8 v) {
    #    unsigned int *p = (unsigned int *)&v;
    #    return (p[0] | p[1] | p[2] | p[3]) != 0;
    # }

    branch_funcs = ['mxu2_bnez16b', 'mxu2_bnez8h', 'mxu2_bnez4w', 'mxu2_bnez2d', 'mxu2_bnez1q',
                    'mxu2_beqz16b', 'mxu2_beqz8h', 'mxu2_beqz4w', 'mxu2_beqz2d', 'mxu2_beqz1q']

    for fn in branch_funcs:
        pattern = r'static __inline__ int ' + fn + r'\(mxu2_v16i8 v\) \{.*?\}'
        if 'bnez' in fn:
            repl = 'static __inline__ int ' + fn + '(mxu2_v16i8 v) { union { mxu2_v16i8 v; uint32_t p[4]; } _u; _u.v = v; return (_u.p[0] | _u.p[1] | _u.p[2] | _u.p[3]) != 0; }'
        else:
            repl = 'static __inline__ int ' + fn + '(mxu2_v16i8 v) { union { mxu2_v16i8 v; uint32_t p[4]; } _u; _u.v = v; return (_u.p[0] | _u.p[1] | _u.p[2] | _u.p[3]) == 0; }'
        content = re.sub(pattern, repl, content, flags=re.DOTALL)

    return content

with open('libfaac/mxu2_shim.h', 'r') as f:
    content = f.read()
with open('libfaac/mxu2_shim.h', 'w') as f:
    f.write(fix_content(content))
