#!/usr/bin/env python3
"""v1.08: simula o servo de taxa do audio (msx_glue.c) contra um DAC com erro de
taxa. Le as constantes direto do fonte. Exige: com erro de ate +-0,4% a fila
nunca enche (o que limitaria o emulador) nem esvazia (estalos)."""
import re, sys
from pathlib import Path
root = Path(__file__).resolve().parents[1]
g = (root/'src/msx_glue.c').read_text()
def const(name): return int(re.search(r'#define %s\s+(\d+)u?' % name, g).group(1))
NTSC, PAL, TARGET = const('AUDIO_SCALE_NTSC_Q16'), const('AUDIO_SCALE_PAL_Q16'), const('AUDIO_TARGET_FILL')
assert 'int bias = (AUDIO_TARGET_FILL - fill) * 8 / 20;' in g
assert 'bias > 24 ? 24 : bias < -8 ? -8 : bias' in g
# constantes de tempo do nucleo (MSX.h / LoopZ80)
CPU_CLOCK, CPU_HPERIOD = 3579545, 228
Q14 = (1000000*CPU_HPERIOD*16384 + CPU_CLOCK//2)//CPU_CLOCK
STREAM = 2560; CAP = STREAM + 6*128      # fila + DMA

def sim(lines, scale, hz, dac_err, seconds=1200):
    frame_us_wall = 1e6/hz
    frac = 0; bias = 0; q = 0.0; mn = 10**9; mx = 0; full = 0; empty = 0
    cons_per_frame = 24000*(1+dac_err)/hz
    for f in range(int(seconds*hz)):
        # producao: blocos de 8 linhas (ultimo bloco com o resto)
        ln = 0
        while ln < lines:
            span = min(8, lines-ln); ln += span
            us = (span*Q14) >> 14
            frac += (us*3*scale) >> 16
            frac += bias
            if frac < 0: frac = 0
            n, frac = divmod(frac, 125)
            q += n
        warm = f > 10*hz            # a partida (fila vazia) e fora do regime
        if q > STREAM:
            full += warm; q = STREAM
        q -= cons_per_frame
        if q < 0:
            empty += warm; q = 0
        fill = int(q*2*100/(STREAM*2))
        b = (TARGET - fill)*8//20
        bias = 24 if b > 24 else -8 if b < -8 else b
        if f > 10*hz: mn = min(mn, fill); mx = max(mx, fill)
    return mn, mx, full, empty
ok = True
for name, lines, scale, hz in (('NTSC', 262, NTSC, 60), ('PAL', 313, PAL, 50)):
    for err in (-0.004, -0.002, 0.0, 0.002, 0.004):
        mn, mx, full, empty = sim(lines, scale, hz, err)
        good = full == 0 and empty == 0 and mn >= 20 and mx <= 95
        ok &= good
        print(f"{name} erro DAC {err*100:+.1f}%: fila {mn}%..{mx}% cheia={full} vazia={empty} {'OK' if good else 'FALHA'}")
print("v1.08 servo de audio: PASS" if ok else "v1.08 servo de audio: FAIL")
sys.exit(0 if ok else 1)
