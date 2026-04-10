def calc_bw(br):
    if br <= 16000: return 4000 + br // 8
    if br <= 32000: return 6000 + (br - 16000) * 5 // 16
    if br <= 64000: return 11000 + (br - 32000) * 15 // 64
    return 18500 # approx

def sbr_active(br, sr):
    bw = calc_bw(br)
    nyquist = sr // 2
    active = (bw * 100 // nyquist) < 40
    print(f"BR {br} -> BW {bw}, Nyq {nyquist}, % {bw*100/nyquist:.1f}, Active: {active}")

sbr_active(16000, 48000)
sbr_active(24000, 48000)
sbr_active(32000, 48000)
sbr_active(48000, 48000)
sbr_active(64000, 48000)
