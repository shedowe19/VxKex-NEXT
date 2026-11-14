#!/usr/bin/env python3
"""Test the production loader guard with native APIs replaced by safe doubles."""
from pathlib import Path
import argparse,os,re,shlex,shutil,subprocess,tempfile

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler')
    args=parser.parse_args()
    here=Path(__file__).resolve().parent
    root=here.parents[1]
    header=root/'VxKexLdr/parentguard.h'
    guard=header.read_text(encoding='utf-8-sig')
    creation=(root/'VxKexLdr/creaproc.c').read_text(encoding='utf-8-sig')
    if '#include "parentguard.h"' not in creation:raise AssertionError('Compile the actual guard into VxKexLdr')
    if 'PROCESS_CREATE_PROCESS | PROCESS_QUERY_LIMITED_INFORMATION' not in creation:raise AssertionError('Parent token queries require query-limited access')
    pattern=r'if\s*\(!VklCanUseParentProcess\(ParentProcessHandle\)\)\s*\{\s*SafeClose\(ParentProcessHandle\);\s*ParentProcessHandle\s*=\s*NULL;\s*goto NoExtendedStartupInfo;\s*\}'
    if not re.search(pattern,creation):raise AssertionError('Rejected parent must close once, clear its handle and use ordinary startup')
    if creation.index('VklCanUseParentProcess(ParentProcessHandle)')>creation.index('UpdateProcThreadAttribute('):raise AssertionError('Check before selecting a parent')
    if 'ProcessCreationFlags = 0;' not in creation or 'ProcessCreationFlags |= EXTENDED_STARTUPINFO_PRESENT;' not in creation:raise AssertionError('Retain the ordinary/native startup fallback')
    driver=(here/'test_parent_guard.c').read_text()
    doubled=set(re.findall(r'^#define\s+(\w+)\s+Mock\w+',driver,re.M))
    stripped=re.sub(r'/\*.*?\*/|//[^\n]*','',guard,flags=re.S)
    calls=set(re.findall(r'\b([A-Za-z_]\w*)\s*\(',stripped))
    unknown=calls-doubled-{'if','for','sizeof','VklCanUseParentProcess'}
    if unknown:raise AssertionError('Add a safe native API double first: '+repr(sorted(unknown)))
    configured=args.compiler or os.environ.get('CC')
    compiler=([configured] if args.compiler else shlex.split(configured)) if configured else [shutil.which('cc') or 'cc']
    msvc=Path(compiler[0]).stem.lower()=='cl'
    with tempfile.TemporaryDirectory(prefix='vxkex-loader-parent-') as directory:
        temp=Path(directory)
        shutil.copyfile(header,temp/'parentguard.h')
        shutil.copyfile(here/'test_parent_guard.c',temp/'test_parent_guard.c')
        exe=temp/('guard.exe' if os.name=='nt' else 'guard')
        options=['/nologo','/TC','/MT','/W4','/WX','/Od'] if msvc else ['-std=gnu89','-O2','-Wall','-Wextra','-Werror','-Wdeclaration-after-statement']
        command=compiler+options+[str(temp/'test_parent_guard.c')]+(['/Fe'+str(exe)] if msvc else ['-o',str(exe)])
        subprocess.run(command,cwd=temp,check=True)
        subprocess.run([str(exe)],cwd=temp,check=True,timeout=20)
    print('Loader elevation production guard and integration checks passed.')

if __name__=='__main__':main()
