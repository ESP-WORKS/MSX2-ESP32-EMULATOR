#!/usr/bin/env python3
"""Validate the reason for the boot VDP collision patch against the user's ROM.
Run: python tests/test_v1_03_boot_rom.py /path/to/MSX2PKANJI.ROM
No copyrighted ROMs are bundled in the emulator source.
"""
import argparse
from pathlib import Path
parser=argparse.ArgumentParser()
parser.add_argument('rom',type=Path)
a=parser.parse_args()
b=a.rom.read_bytes()
assert len(b)==32768 and b[:2]==b'AB', 'Expected MSX2+ Kanji BASIC 32K'
# Image is mapped 4000-BFFF: 7A66 -> 3A66
sig=bytes.fromhex('DB 99 DB 99 E6 20 CA 68 7A')
assert b[0x7A66-0x4000:0x7A66-0x4000+len(sig)]==sig, 'Unexpected boot code'
print('User MSX2PKANJI ROM: 7A68 polls S#0 twice for sprite collision (bit 5): PASS')
