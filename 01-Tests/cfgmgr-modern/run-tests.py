#!/usr/bin/env python3
"""Compile actual cfgmgr.c and exercise its owned Windows notification backend."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--compiler',default=None)
    parser.add_argument('--keep-output',type=Path,default=None)
    args=parser.parse_args()
    here=Path(__file__).resolve().parent
    root=here.parents[1]
    header=(root/'00-Common Headers/KxBase.h').read_text(encoding='utf-8-sig')
    declarations=header[header.index('typedef enum _CM_NOTIFY_FILTER_TYPE {'):header.index('typedef enum _FIRMWARE_TYPE {')]
    declarations='#define TYPEDEF_TYPE_NAME(Type) Type, *P##Type\n#define GEN_STD_TYPEDEFS(Type) typedef Type *P##Type;\n'+declarations
    source=(root/'01-Extended DLLs/KxBase/cfgmgr.c').read_text(encoding='utf-8-sig')
    source=source.replace('#include "buildcfg.h"','').replace('#include "kxbasep.h"','')
    harness=(here/'cfgmgr_test.c').read_text(encoding='utf-8').replace('/* CM_NOTIFICATION_DECLARATIONS */',declarations).replace('/* CM_NOTIFICATION_IMPLEMENTATION */',source)
    compiler=args.compiler or shutil.which('cl')
    if not compiler: raise SystemExit('MSVC Windows SDK environment required')
    with tempfile.TemporaryDirectory(prefix='vxkex-cfgmgr-contract-') as temporary:
        out=args.keep_output.resolve() if args.keep_output else Path(temporary)
        out.mkdir(parents=True,exist_ok=True)
        actual=out/'cfgmgr_contract.c'; actual.write_text(harness,encoding='utf-8')
        exe=out/'cfgmgr_contract.exe'
        command=[str(compiler),'/nologo','/W4','/WX','/wd4201','/O2','/MT','/DUNICODE','/D_UNICODE','/D_WIN32_WINNT=0x0601',str(actual),'/Fo'+str(out/'test.obj'),'/Fe'+str(exe),'/link','kernel32.lib','user32.lib']
        subprocess.run(command,cwd=out,check=True)
        subprocess.run([str(exe)],cwd=out,check=True,timeout=30)


if __name__=='__main__': main()
