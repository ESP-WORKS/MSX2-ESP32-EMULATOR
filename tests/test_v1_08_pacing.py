#!/usr/bin/env python3
"""v1.06: limitador de EPS com periodo real do quadro MSX e divida de ate 4 quadros.
Extrai hal_video_pace_emu_frame() de src/hal_video.cpp e simula o relogio.
Requer g++ no PC (nao precisa de ESP32/PlatformIO)."""
from pathlib import Path
import subprocess, tempfile, os
root = Path(__file__).resolve().parents[1]
s = (root/'src/hal_video.cpp').read_text()
a = s.index('/* v1.08: prazo de quadro EXATO')
b = s.index('static void reset_fps_measurement(void);', a)
c = s.index('extern "C" void hal_video_pace_emu_frame(void)')
d = s.index('// ---------------------------------------------------------------------------', c)
src = r'''#include <stdint.h>
#include <stdio.h>
static int64_t T=0;
int64_t hal_time_us(void){return T;}
void hal_delay_ms(int ms){T+=ms*1000;}
void delayMicroseconds(uint32_t us){T+=us;}
static unsigned emulation_hz=60;
''' + s[a:b] + s[c:d] + r'''
int main(){
  hal_video_pace_emu_frame();
  int64_t t0=T; const int N=60000;
  for(int i=0;i<N;i++){ T+=10000; hal_video_pace_emu_frame(); }
  double avg=(double)(T-t0)/N;
  if(avg<16666.5||avg>16666.9){printf("periodo medio %.3f us\n",avg);return 1;}
  int64_t st=T; for(int i=0;i<10;i++){ T+=(i==0?40000:10000); hal_video_pace_emu_frame(); }
  if((double)(T-st)>10*16666.667+20) return 2;   /* pico isolado recuperado */
  st=T; T+=200000; hal_video_pace_emu_frame();   /* atraso longo: reancora */
  int64_t t1=T; T+=1000; hal_video_pace_emu_frame();
  if(T-t1<15000) return 3;                        /* sem rajada apos reancorar */
  puts("v1.08 pacing: 60.000 Hz exato (16666.667 us), pico recuperado, reancora sem rajada: PASS");
  return 0;}
'''
with tempfile.TemporaryDirectory() as t:
    cpp = os.path.join(t, 'p.cpp'); exe = os.path.join(t, 'p')
    Path(cpp).write_text(src)
    subprocess.check_call(['g++', '-O2', '-o', exe, cpp])
    subprocess.check_call([exe])
