import sys

def fix_file(filepath):
    with open(filepath, 'r') as f:
        lines = f.readlines()

    replacements = {
        'return (int)((signed char *)&v)[lane];':
        '    union { mxu2_v4i32 v; signed char b[16]; } u; u.v = v; return (int)u.b[lane];',
        'return (int)((short *)&v)[lane];':
        '    union { mxu2_v4i32 v; short h[8]; } u; u.v = v; return (int)u.h[lane];',
        'return (int)((int *)&v)[lane];':
        '    union { mxu2_v4i32 v; int w[4]; } u; u.v = v; return (int)u.w[lane];',
        'return (unsigned int)((unsigned char *)&v)[lane];':
        '    union { mxu2_v4i32 v; unsigned char b[16]; } u; u.v = v; return (unsigned int)u.b[lane];',
        'return (unsigned int)((unsigned short *)&v)[lane];':
        '    union { mxu2_v4i32 v; unsigned short h[8]; } u; u.v = v; return (unsigned int)u.h[lane];',
        'return (unsigned int)((unsigned int *)&v)[lane];':
        '    union { mxu2_v4i32 v; unsigned int w[4]; } u; u.v = v; return (unsigned int)u.w[lane];',
        'return ((float *)&v)[lane];':
        '    union { mxu2_v4f32 v; float f[4]; } u; u.v = v; return u.f[lane];',
        'return ((double *)&v)[lane];':
        '    union { mxu2_v4i32 v; double d[2]; } u; u.v = v; return u.d[lane];',
        'signed char val = ((signed char *)&v)[lane];':
        '    union { mxu2_v16i8 v; signed char b[16]; } u; u.v = v; signed char val = u.b[lane];',
        'short val = ((short *)&v)[lane];':
        '    union { mxu2_v8i16 v; short h[8]; } u; u.v = v; short val = u.h[lane];',
        'int val = ((int *)&v)[lane];':
        '    union { mxu2_v4i32 v; int w[4]; } u; u.v = v; int val = u.w[lane];',
        'long long val = ((long long *)&v)[lane];':
        '    union { mxu2_v4i32 v; long long d[2]; } u; u.v = v; long long val = u.d[lane];'
    }

    new_lines = []
    for line in lines:
        for original, replacement in replacements.items():
            if original in line:
                if 'union' not in line:
                    line = replacement + '\n'
                break
        new_lines.append(line)

    with open(filepath, 'w') as f:
        f.writelines(new_lines)

if __name__ == "__main__":
    fix_file(sys.argv[1])
