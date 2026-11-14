#!/usr/bin/env python3
"""Compile the actual service wrappers/resolver with controlled OS doubles."""
import argparse
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile
import xml.etree.ElementTree as ET


def check_export_inputs(root):
    """Pin the linker inputs; actual PE export comparison remains separate."""
    provider = root / '01-Extended DLLs/KxAdvapi'
    definitions = (provider / 'kxadvapi32.def').read_text(encoding='utf-8-sig')
    ordinals = {
        'CloseServiceHandle': 1088, 'CreateServiceA': 1129, 'CreateServiceW': 1130,
        'OpenSCManagerA': 1517, 'OpenSCManagerW': 1518,
        'OpenServiceA': 1519, 'OpenServiceW': 1520,
        'StartServiceA': 1720, 'StartServiceW': 1723,
    }
    for name, ordinal in ordinals.items():
        pattern = r'^\s*' + name + r'\s*=\s*Ext_' + name + r'\s+@' + str(ordinal) + r'\s*(?:;[^\n]*)?$'
        if len(re.findall(pattern, definitions, re.M)) != 1:
            raise AssertionError('Preserve the original Win32 SCM export: ' + name + ' @' + str(ordinal))
    namespace = {'m': 'http://schemas.microsoft.com/developer/msbuild/2003'}
    project = ET.parse(provider / 'KxAdvapi.vcxproj').getroot()
    selections = {}
    for group in project.findall('m:ItemDefinitionGroup', namespace):
        definition = group.find('m:Link/m:ModuleDefinitionFile', namespace)
        if definition is None:
            continue
        match = re.search(r"==\s*'((?:Debug|Release)\|(?:Win32|x64))'", group.get('Condition', ''))
        if match:
            configuration = match.group(1)
            if configuration in selections:
                raise AssertionError('Duplicate SCM export-definition configuration: ' + configuration)
            selections[configuration] = (definition.text or '').strip().lower()
    expected = {configuration + '|' + platform: ('kxadvapi32.def' if platform == 'Win32' else 'kxadvapi.def')
                for configuration in ('Debug', 'Release') for platform in ('Win32', 'x64')}
    if selections != expected:
        raise AssertionError('Keep the pinned Win32 definition separate from the common x64 definition: ' + repr(selections))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', help='C compiler, e.g. cl or cc')
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    root = here.parents[1]
    check_export_inputs(root)
    groups = [
        ('services', 'test_services.c', [('01-Extended DLLs/KxAdvapi/service.c', 'service.c'), ('KxCfgHlp/svcinherit.c', 'svcinherit.c')]),
        ('writer', 'test_writer.c', [('KxCfgHlp/inherit.c', 'inherit.c')]),
        ('com_service', 'test_com_service.c', [('01-Extended DLLs/KxCom/service.c', 'com_service.c')]),
        ('reader', 'test_reader.c', []),
        ('bridge', 'test_bridge.c', []),
    ]
    sources = [root / source for _, _, units in groups for source, _ in units]
    shim = (here / 'service_test_shim.h').read_text(encoding='utf-8')
    doubled = set(re.findall(r'^#define\s+(\w+)\s+Mock\w+', shim, re.M))
    # Fail closed if the production sources acquire a new, undoubled OS call.
    pure = {'if', 'while', 'for', 'return', 'sizeof', 'ARRAYSIZE', 'RtlZeroMemory',
            'FAILED', 'NT_SUCCESS', 'StringEqualI', 'wcslen', 'wcsrchr',
            'KxAdvapiTryInheritService', 'KxCfgTryInheritConfiguration'}
    core = (root / 'KexDll/kexdata.c').read_text(encoding='utf-8-sig')
    first = core.index('NoKexOptionsInRegistry &= LdrQueryImageFileKeyOption(')
    reader_block = core[first:core.index('SafeClose(IfeoKeyHandle);', first)]
    bridge = (root / '01-Extended DLLs/KxAdvapi/servicecfg.c').read_text(encoding='utf-8-sig')
    bridge_function = bridge[bridge.index('VOID WINAPI KxAdvapiTryInheritService('):]
    production = '\n'.join(p.read_text(encoding='utf-8-sig') for p in sources) + '\n' + reader_block + '\n' + bridge_function
    stripped = re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', '', production, flags=re.S)
    calls = set(re.findall(r'\b([A-Za-z_]\w*)\s*\(', stripped))
    helpers = {name for name in calls if name.startswith(('KxAdvapi', 'KxCfg', 'KxComp', 'Ext_'))}
    unknown = calls - doubled - helpers - pure
    if unknown:
        raise AssertionError('Add an explicit safe OS double before testing: ' + ', '.join(sorted(unknown)))
    callers = (root / '01-Extended DLLs/KxCom/firefoxpatch.c').read_text(encoding='utf-8-sig')
    if 'if (!OuterUnknown && !ServerInfo) KxCompTryInheritServiceForClass' not in callers:
        raise AssertionError('COM activation with explicit server information or aggregation must not inherit a local service')
    if 'if (!ServerInfo) KxCompTryInheritServiceForClass' not in callers:
        raise AssertionError('CoGetClassObject must retain its explicit-server gate')
    configured = args.compiler or os.environ.get('CC')
    compiler = ([configured] if args.compiler else shlex.split(configured)) if configured else [shutil.which('cc') or 'cc']
    is_msvc = Path(compiler[0]).stem.lower() == 'cl'
    with tempfile.TemporaryDirectory(prefix='vxkex-service-contracts-') as directory:
        temporary = Path(directory)
        shutil.copyfile(here / 'service_test_shim.h', temporary / 'service_test_shim.h')
        for label, driver, production_units in groups:
            template = (here / driver).read_text(encoding='utf-8-sig')
            rendered = template.replace('/* PRODUCTION_READER_BLOCK */', reader_block).replace('/* PRODUCTION_BRIDGE_FUNCTION */', bridge_function)
            (temporary / driver).write_text(rendered, encoding='utf-8')
            for source, filename in production_units:
                shutil.copyfile(root / source, temporary / filename)
            binary = temporary / (label + ('.exe' if os.name == 'nt' else ''))
            units = [str(temporary / driver)] + [str(temporary / filename) for _, filename in production_units]
            if is_msvc:
                command = compiler + ['/nologo', '/W4', '/WX', '/TC', '/Od', '/DKEX_SERVICE_TEST'] + units + ['/Fe' + str(binary)]
            else:
                command = compiler + ['-std=gnu89', '-O2', '-Wall', '-Wextra', '-Werror', '-Wdeclaration-after-statement', '-DKEX_SERVICE_TEST'] + units + ['-o', str(binary)]
            subprocess.run(command, cwd=temporary, check=True)
            subprocess.run([str(binary)], cwd=temporary, check=True, timeout=20)
    print('Service propagation production-function regressions passed.', flush=True)


if __name__ == '__main__':
    main()
