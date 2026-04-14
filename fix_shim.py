import sys
import re

def fix_mxu2(filepath):
    with open(filepath, 'r') as f:
        content = f.read()

    # Generic replace for extract pattern
    # Match: return (TYPE)((PTR_TYPE *)&v)[lane];
    # Replace with union-based access

    # mtcpus/u
    pattern = r'return \((int|unsigned int)\)\(\((signed char|unsigned char|short|unsigned short|int|unsigned int) \*\)\&v\)\[lane\];'

    def repl_extract(m):
        cast_type = m.group(1)
        elem_type = m.group(2)
        count = 16 if 'char' in elem_type else (8 if 'short' in elem_type else 4)
        member = 'b' if 'char' in elem_type else ('h' if 'short' in elem_type else 'w')
        return '{{ union {{ mxu2_v4i32 v; {0} {1}[{2}]; }} _u; _u.v = v; return ({3})_u.{1}[lane]; }}'.format(elem_type, member, count, cast_type)

    content = re.sub(pattern, repl_extract, content)

    # mtfpu
    content = content.replace('return ((float *)&v)[lane];', '{ union { mxu2_v4f32 v; float f[4]; } _u; _u.v = v; return _u.f[lane]; }')
    content = content.replace('return ((double *)&v)[lane];', '{ union { mxu2_v4i32 v; double d[2]; } _u; _u.v = v; return _u.d[lane]; }')

    # repx
    pattern_repx = r'(\w+) val = \(\((\w+ \*) \*\)\&v\)\[lane\];'
    # Wait, original is: signed char val = ((signed char *)&v)[lane];
    content = re.sub(r'(signed char|short|int|long long) val = \(\(\1 \*\)\&v\)\[lane\];',
                     r'union { mxu2_v4i32 v; \1 val_arr[16]; } _u; _u.v = (mxu2_v4i32)v; \1 val = _u.val_arr[lane];', content)

    # Branch predicates
    branch_funcs = ['bnez16b', 'bnez8h', 'bnez4w', 'bnez2d', 'bnez1q',
                    'beqz16b', 'beqz8h', 'beqz4w', 'beqz2d', 'beqz1q']
    for fn in branch_funcs:
        pattern = r'static __inline__ int mxu2_' + fn + r'\(mxu2_v16i8 v\) \{.*?\}'
        if 'bnez' in fn:
            repl = 'static __inline__ int mxu2_' + fn + '(mxu2_v16i8 v) { union { mxu2_v16i8 v; uint32_t p[4]; } _u; _u.v = v; return (_u.p[0] | _u.p[1] | _u.p[2] | _u.p[3]) != 0; }'
        else:
            repl = 'static __inline__ int mxu2_' + fn + '(mxu2_v16i8 v) { union { mxu2_v16i8 v; uint32_t p[4]; } _u; _u.v = v; return (_u.p[0] | _u.p[1] | _u.p[2] | _u.p[3]) == 0; }'
        content = re.sub(pattern, repl, content, flags=re.DOTALL)

    with open(filepath, 'w') as f:
        f.write(content)

fix_mxu2('libfaac/mxu2_shim.h')
