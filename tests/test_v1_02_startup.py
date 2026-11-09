#!/usr/bin/env python3
"""Regression checks for v1.02. Requires a C compiler, not ESP32/PlatformIO."""
from pathlib import Path
import subprocess, tempfile

root = Path(__file__).resolve().parents[1]
glue=(root/'src/msx_glue.c').read_text()
core=(root/'src/fmsx/fMSX/MSX.c').read_text()
menu=(root/'src/fmsx/fMSX/Menu.c').read_text()
main=(root/'src/main.cpp').read_text()
assert 'Layout Keyboard BR %c\\n' in menu
assert 'Layout Teclado BR' not in menu
assert 'int MSXKeyboardBR = 1;' in core
assert 'static int FrameSkipAuto = 1;' in glue
assert 'HAL_HOT_SKIP' in glue and 'set_runtime_frame_skip_auto(!FrameSkipAuto)' in glue
assert 'MSX2PKANJI.ROM' in core and 'OpeningFileSize==0x8000' in core
assert any(('MSX2+ FAST VGA32 v'+v) in main for v in ('1.02','1.03','1.04','1.05','1.06','1.07','1.08'))

# Compile and execute the actual boot-option selection extracted from msx_glue.c.
section=glue.split('    /* AUTO e o padrao desde a v1.02.',1)[1].split('    if (hal_rc_get("msx_fbram"',1)[0]
section='    /* AUTO e o padrao desde a v1.02.'+section
src=r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int FrameSkipAuto=1;
static const char *a=NULL,*b=NULL;
static int hal_rc_get(const char *key,char *v,size_t n){
 const char *x=strcmp(key,"msx_frameskip")==0?a:
               strcmp(key,"msx_autoskip")==0?b:NULL;
 if(!x)return 0;
 snprintf(v,n,"%s",x); return 1;
}
static void hal_log(const char *fmt,...){(void)fmt;}
static void configure(void){
 char value[160]; FrameSkipAuto=1;
''' + section + r'''
}
int main(void){
 struct Test {const char *legacy,*auto_val; int expected;} cases[]={
  {NULL,NULL,1},{NULL,"0",0},{NULL,"1",1},
  {"0",NULL,0},{"50",NULL,1},{"AUTO",NULL,1},
  {"50","0",0},{"0","1",1},{"0","0",0}
 };
 for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);++i){
  a=cases[i].legacy;b=cases[i].auto_val;configure();
  if(FrameSkipAuto!=cases[i].expected) return (int)i+1;
 }
 puts("Frame Skip: 9 startup/override cases PASS");return 0;
}
'''

# Compile and execute the actual F4 polarity selection from MSX.c with test ROM bytes.
f4=core.split('void MSXSetF4Mode(int Mode)',1)[1].split('\nbyte *MemMap[4][4][8];',1)[0]
f4='void MSXSetF4Mode(int Mode)'+f4
src_f4=r'''
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
typedef unsigned char byte;
static byte *MSX2POpeningROM=0;
static byte MSX2PResetF4=0;
static byte MSX2PF4Inverted=0;
static int MSX2PF4Override=-1;
static byte *EmptyRAM=0;
static int Verbose=0;
byte *MemMap[4][4][8];
''' +f4+r'''
int main(void){
 static byte bios[0x8000],sub[0x4000],opening[0x8000];
 memset(bios,0xFE,sizeof(bios));memset(sub,0xFE,sizeof(sub));
 MemMap[0][0][0]=bios;MemMap[3][1][0]=sub;
 MSX2POpeningROM=opening;
 MSXSetF4Mode(-1);MSX2PF4PowerOn();
 if(MSX2PResetF4!=0x00) return 1; /* Unknown/Sony + opening => normal */
 memcpy(bios+400,"Panasonic",9);MSX2PF4PowerOn();
 if(MSX2PResetF4!=0xFF) return 2;
 memset(bios+400,0xFE,9);memcpy(sub+200,"Sanyo",5);MSX2PF4PowerOn();
 if(MSX2PResetF4!=0xFF) return 3;
 MSXSetF4Mode(0);MSX2PF4PowerOn();
 if(MSX2PResetF4!=0) return 4;
 MSXSetF4Mode(1);MSX2PF4PowerOn();
 if(MSX2PResetF4!=0xFF) return 5;
 puts("F4: normal/Panasonic/Sanyo/manual OFF/manual ON PASS");return 0;
}
'''

with tempfile.TemporaryDirectory() as d:
 for name,source in [('skip',src),('f4',src_f4)]:
  file=Path(d)/(name+'.c');exe=Path(d)/name;file.write_text(source)
  subprocess.run(['gcc','-std=gnu11','-O2','-Wall','-Wextra',str(file),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)

inc=['-Isrc','-Isrc/fmsx','-Isrc/fmsx/fMSX','-Isrc/fmsx/EMULib','-Isrc/fmsx/Z80']
for f in ['src/msx_glue.c','src/fmsx/fMSX/MSX.c','src/fmsx/fMSX/Menu.c']:
 subprocess.run(['gcc','-std=gnu11','-fsyntax-only','-DMSX_ESP32','-DMSX_NATIVE_RGB222=1',
                 '-Wno-implicit-function-declaration',*inc,f],cwd=root,check=True)
 print(f,'C syntax PASS')
print('v1.02 source regression checks PASS')
