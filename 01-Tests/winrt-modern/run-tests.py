#!/usr/bin/env python3
"""Real KxCom source with native COM/URLMON/FTM; no DLL injection or launcher action."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    p=argparse.ArgumentParser()
    p.add_argument('--compiler',default=None)
    p.add_argument('--keep-output',type=Path,default=None)
    a=p.parse_args()
    here=Path(__file__).resolve().parent;root=here.parents[1]
    compiler=a.compiler or shutil.which('cl')
    if not compiler:raise SystemExit('MSVC Windows SDK environment required')
    with tempfile.TemporaryDirectory(prefix='vxkex-winrt-contract-') as temporary:
        out=a.keep_output.resolve() if a.keep_output else Path(temporary);out.mkdir(parents=True,exist_ok=True)
        exe=out/'WinRTContracts.exe'
        excludes=['seh32','KxCfgHlp','KxMi','KxBase','KexGui','KexPathCch','KexSmp','KexMls','KexW32ML']
        args=[str(compiler),'/nologo','/W4','/WX','/O2','/MT','/Gy','/DUNICODE','/D_UNICODE','/DNDEBUG','/DNO_KEXDLL_LIB','/DKW32MLDECLSPEC=','/DKSMPDECLSPEC=','/DKMLSDECLSPEC=','/I'+str(root/'00-Common Headers'),str(here/'WinRTContracts.c'),'/Fo'+str(out/'winrt.obj'),'/Fe'+str(exe),'/link','/ENTRY:mainCRTStartup','/SUBSYSTEM:CONSOLE,6.01','/MANIFEST:NO','/OPT:REF','/DYNAMICBASE','/NXCOMPAT']+['/NODEFAULTLIB:'+name+'.lib' for name in excludes]+['libcmt.lib','kernel32.lib','user32.lib','gdi32.lib','shell32.lib','ole32.lib','oleaut32.lib','urlmon.lib','uuid.lib']
        subprocess.run(args,cwd=out,check=True)
        editbin=Path(compiler).parent/'editbin.exe' if Path(compiler).parent!=Path('.') else Path(shutil.which('editbin') or 'editbin')
        subprocess.run([str(editbin),'/nologo','/SUBSYSTEM:CONSOLE',str(exe)],cwd=out,check=True)
        subprocess.run([str(exe)],cwd=out,check=True,timeout=60)


if __name__=='__main__':main()
