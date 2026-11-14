#!/usr/bin/env python3
"""Compile actual async implementation with native COM and GIT call fencing."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--compiler',default=None)
    parser.add_argument('--keep-output',type=Path,default=None)
    parser.add_argument('--no-run',action='store_true')
    args=parser.parse_args()
    here=Path(__file__).resolve().parent
    root=here.parents[1]
    source=root/'01-Extended DLLs/KxCom/asyncbool.c'
    raw=source.read_bytes();lines=raw.decode().splitlines(keepends=True)
    if lines[0].strip()!='#include "buildcfg.h"' or lines[1].strip()!='#include "kxcomp.h"':
        raise SystemExit('Production include prefix changed; review extraction')
    compiler=args.compiler or shutil.which('cl')
    if not compiler: raise SystemExit('MSVC/Windows SDK command environment required')
    with tempfile.TemporaryDirectory(prefix='vxkex-async-review-') as temporary:
        out=args.keep_output.resolve() if args.keep_output else Path(temporary)
        out.mkdir(parents=True,exist_ok=True)
        body=out/'async-body.c';body.write_text('#include "test-shim.h"\n'+''.join(lines[2:]),encoding='utf-8')
        objects=[]
        for path,stem,flags in [(source,'actual-headers',['/W3','/Zl','/DWIN32','/DNDEBUG','/D_WINDOWS','/D_USRDLL']),(body,'body',['/W4']),(here/'OverlapProbe.c','probe',['/W4'])]:
            obj=out/(stem+'.obj')
            command=[str(compiler),'/nologo','/c','/WX','/O2','/MT','/Gz','/DUNICODE','/D_UNICODE','/D_WIN32_WINNT=0x0601','/DWINVER=0x0601','/I'+str(here),'/I'+str(root/'00-Common Headers')]+flags+[str(path),'/Fo'+str(obj)]
            subprocess.run(command,cwd=out,check=True)
            if stem!='actual-headers':objects.append(obj)
        executable=out/'CompletedBooleanOverlapProbe.exe'
        link=Path(compiler).parent/'link.exe' if Path(compiler).parent!=Path('.') else Path(shutil.which('link') or 'link')
        subprocess.run([str(link),'/nologo','/OUT:'+str(executable),'/SUBSYSTEM:CONSOLE,6.01','/MANIFEST:NO','/DYNAMICBASE','/NXCOMPAT']+list(map(str,objects))+['kernel32.lib','ole32.lib','oleaut32.lib','uuid.lib'],cwd=out,check=True)
        if not args.no_run: subprocess.run([str(executable)],cwd=out,check=True,timeout=45)
        if source.read_bytes()!=raw: raise SystemExit('Production source changed during test')


if __name__=='__main__': main()
