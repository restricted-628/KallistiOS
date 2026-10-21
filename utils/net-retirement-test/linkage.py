#!/usr/bin/env python3
"""Exercise actual preflight body with optional providers in a real archive."""
import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile
parser=argparse.ArgumentParser()
parser.add_argument('--cc',default='clang')
parser.add_argument('--ar',default='ar')
parser.add_argument('--nm',default='nm')
parser.add_argument('--sh',action='store_true',help='SH-4 freestanding link/symbol check, no execution')
a=parser.parse_args()
source=(Path(__file__).resolve().parents[2]/'kernel/arch/dreamcast/kernel/init.c').read_text()
start=source.index('int arch_net_shutdown_preflight(void) {')
end=source.index('\n}\n',start)+3
body=source[start:end]
pre='''#define ENOSYS 38
#define NULL ((void *)0)
int errno;
int calls;
struct net_shutdown_binding { void (*shutdown)(void); int (*check)(void); };
extern int net_quiesce_checked(void) __attribute__((weak));
extern int bba_shutdown_preflight(void) __attribute__((weak));
extern void net_shutdown(void) __attribute__((weak));
extern void eth_shutdown(void) __attribute__((weak));
extern const struct net_shutdown_binding arch_net_shutdown_custom __attribute__((weak));
extern const struct net_shutdown_binding arch_eth_shutdown_custom __attribute__((weak));
void (*net_shutdown_weak)(void), (*eth_shutdown_weak)(void);
'''
provider='''extern int calls;
void legacy_provider(void) {}
void net_shutdown(void) {}
void eth_shutdown(void) {}
int net_quiesce_checked(void) { ++calls; return 0; }
int bba_shutdown_preflight(void) { ++calls; return 0; }
'''
force='extern void legacy_provider(void);'
pull='legacy_provider();'
custom='void custom(void) {}'
paired=custom+'''int custom_check(void) { ++calls; return 0; }
const struct net_shutdown_binding arch_net_shutdown_custom={custom,custom_check};
'''
cases={
 'off': ('','',0,0,False),
 'on': (force,pull+'net_shutdown_weak=net_shutdown; eth_shutdown_weak=eth_shutdown;',0,2,True),
 'manual': ('','manual_init();',0,3,True),
 'unpaired': (force+custom,pull+'net_shutdown_weak=custom;',-1,0,True),
 'paired': (force+paired,pull+'net_shutdown_weak=custom;',0,3,True),
}
flags=['-std=gnu17','-O0','-Wall','-Wextra','-Werror']
if a.sh: flags+=['-m4-single','-ml','-ffreestanding']
if not a.sh and sys.platform=='darwin':
 for name in cases:
  print(name+': SKIP Mach-O optional-data/archive semantics differ; use --sh')
 sys.exit(0)
with tempfile.TemporaryDirectory(prefix='net-retirement-link-') as temp:
 d=Path(temp)
 (d/'provider.c').write_text(provider)
 subprocess.run([a.cc,*flags,'-c',str(d/'provider.c'),'-o',str(d/'provider.o')],check=True)
 subprocess.run([a.ar,'rcs',str(d/'providers.a'),str(d/'provider.o')],check=True)
 for name,(decl,setup,expected,count,defined) in cases.items():
  if name=='manual':
   decl='extern int manual_init(void);'
   (d/'manual.c').write_text('extern int net_quiesce_checked(void); int manual_init(void) { return net_quiesce_checked(); }')
   subprocess.run([a.cc,*flags,'-c',str(d/'manual.c'),'-o',str(d/'manual.o')],check=True)
  code=pre+body+decl+f'\nint main(void) {{ {setup} int r=arch_net_shutdown_preflight(); return r!={expected} || calls!={count}; }}\n'
  path=d/(name+'.c');path.write_text(code)
  exe=d/(name+'.elf')
  cmd=[a.cc,*flags,str(path)]
  if name=='manual':cmd+=[str(d/'manual.o')]
  cmd+=[str(d/'providers.a'),'-o',str(exe)]
  if a.sh:cmd+=['-nostdlib','-Wl,-e,_main']
  subprocess.run(cmd,check=True)
  symbols=subprocess.check_output([a.nm,str(exe)],text=True)
  actual=bool(re.search(r'\b[TW] _?net_quiesce_checked$',symbols,re.M))
  assert actual==defined,(name,symbols)
  if not a.sh:subprocess.run([str(exe)],check=True)
  print(f'{name}: '+('SH-4 link/symbol PASS' if a.sh else 'host archive/execution PASS'))
