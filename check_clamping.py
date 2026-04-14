import os
import re

filepath = 'libfaac/quantize.c'
with open(filepath, 'r') as f:
    content = f.read()

# Add a print statement when clamping occurs
content = content.replace(
    'diff = clamp_sf_diff(diff);',
    'int old_diff = diff; diff = clamp_sf_diff(diff); if (old_diff != diff) printf("CLAMPED: %d -> %d\\n", old_diff, diff);'
)

with open(filepath, 'w') as f:
    f.write(content)
