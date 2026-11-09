#!/usr/bin/env python3
"""Static audit of the original user's MSX2PKANJI.ROM without embedding it.
Run: python tests/test_v1_05_rom_markers.py /path/to/MSX2PKANJI.ROM
"""
from pathlib import Path
import sys
p=Path(sys.argv[1]); b=p.read_bytes()
assert len(b)==0x8000 and b[:2]==b'AB'
# ROM is mapped 4000-BFFF, and DE encodes VDP value E and register D.
markers={
    0x7B26: '11 03 19 CD A9 7B',  # scroll pages on (R25=3)
    0x7A76: '11 00 1A CD A9 7B',  # R26=0
    0x7A7C: '11 00 1B CD A9 7B',  # R27=0
    0x7A3B: '11 00 19 CD A9 7B',  # scroll pages off (R25=0)
    0x7A41: '11 1F 02 CD A9 7B',  # page selection (R2=31)
}
for a, hexseq in markers.items():
    data=bytes.fromhex(hexseq)
    assert b[a-0x4000:a-0x4000+len(data)]==data, f'Unexpected code at {a:04X}'
# Sprite attribute records initially include repeated X=E8h (232) and X=00h.
rows=[b[a-0x4000:a-0x4000+4] for a in range(0x7AB2,0x7ADA,4)]
assert rows[0][1]==0xE8 and rows[1][1]==0xE8
assert any(r[1]==0 for r in rows)
print('v1.05 ROM: sprite guide data and exact R25/R26/R27/R2 end sequence: PASS')
