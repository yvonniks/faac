import sys
with open('libfaac/sbr.c', 'r') as f:
    content = f.read()
search = """    sbr->bs_amp_res    = 0;   /* 1.5 dB amplitude resolution */
    sbr->bs_start_freq = 15;  /* kx ≈ 31–32 (≈11.6–12 kHz) — just below LC core Nyquist */
    sbr->bs_stop_freq  = 14;  /* k2 = 2*kx — full upper-octave SBR extension */"""
replace = """    sbr->bs_amp_res    = 0;   /* 1.5 dB amplitude resolution */
    /* Search for bs_start_freq to match kx ≈ 16 (Fs/4 crossover in 64-band synthesis space) */
    int min_diff = 1000, best_start = 15;
    for (int i = 0; i < 16; i++) {
        int kx = compute_kx(sampleRate, i);
        int diff = abs(kx - 16);
        if (diff < min_diff) { min_diff = diff; best_start = i; }
    }
    sbr->bs_start_freq = best_start;
    sbr->bs_stop_freq  = 14;  /* k2 = 2*kx — full upper-octave SBR extension */"""
with open('libfaac/sbr.c', 'w') as f:
    f.write(content.replace(search, replace))
