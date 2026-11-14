#!/usr/bin/env python3
"""Compare original and built KxCryp PE exports without loading either DLL."""

import argparse
from pathlib import Path
import struct


class PE:
    def __init__(self, path):
        self.data = Path(path).read_bytes()
        if self.data[:2] != b'MZ':
            raise ValueError('Missing DOS header: ' + str(path))
        pe = self.unpack('<I', 60)[0]
        if self.data[pe:pe + 4] != b'PE\0\0':
            raise ValueError('Missing PE signature: ' + str(path))
        self.machine, sections = self.unpack('<HH', pe + 4)
        optional_size = self.unpack('<H', pe + 20)[0]
        optional = pe + 24
        magic = self.unpack('<H', optional)[0]
        if magic not in (0x10b, 0x20b):
            raise ValueError('Unsupported optional header')
        directory = optional + (96 if magic == 0x10b else 112)
        if directory + 8 > optional + optional_size:
            raise ValueError('Missing export directory')
        self.export_rva, self.export_size = self.unpack('<II', directory)
        self.header_size = self.unpack('<I', optional + 60)[0]
        self.sections = []
        for index in range(sections):
            offset = optional + optional_size + index * 40
            virtual_size, address, raw_size, raw_offset = self.unpack('<IIII', offset + 8)
            flags = self.unpack('<I', offset + 36)[0]
            self.sections.append((address, max(virtual_size, raw_size), raw_offset, raw_size, flags))
        export = self.unpack('<IIHHIIIIIII', self.offset(self.export_rva, 40))
        self.base, self.function_count, name_count = export[5:8]
        functions = self.unpack('<' + 'I' * self.function_count, self.offset(export[8], self.function_count * 4))
        names = self.unpack('<' + 'I' * name_count, self.offset(export[9], name_count * 4))
        ordinals = self.unpack('<' + 'H' * name_count, self.offset(export[10], name_count * 2))
        self.exports = {}
        self.code_exports = set()
        for name_rva, ordinal in zip(names, ordinals):
            if ordinal >= self.function_count:
                raise ValueError('Export ordinal index outside function table')
            name = self.string(name_rva)
            if name in self.exports:
                raise ValueError('Duplicate export name: ' + name)
            rva = functions[ordinal]
            forwarder = self.string(rva) if self.export_rva <= rva < self.export_rva + self.export_size else None
            self.exports[name] = (self.base + ordinal, forwarder)
            if rva and forwarder is None and any(address <= rva < address + span and flags & 0x20000000 for address, span, raw, raw_size, flags in self.sections):
                self.offset(rva)
                self.code_exports.add(name)

    def unpack(self, format_string, offset):
        size = struct.calcsize(format_string)
        if offset < 0 or offset + size > len(self.data):
            raise ValueError('Truncated PE data')
        return struct.unpack_from(format_string, self.data, offset)

    def offset(self, rva, length=1):
        if rva < self.header_size and rva + length <= min(self.header_size, len(self.data)):
            return rva
        for address, span, raw, raw_size, flags in self.sections:
            if address <= rva < address + span:
                delta = rva - address
                if delta + length <= raw_size and raw + delta + length <= len(self.data):
                    return raw + delta
                break
        raise ValueError('Unmapped or truncated PE RVA: ' + hex(rva))

    def string(self, rva):
        offset = self.offset(rva)
        end = self.data.find(b'\0', offset)
        if end < 0:
            raise ValueError('Unterminated PE string')
        # Also validate that the full string remains in its mapped raw region.
        self.offset(rva, end - offset + 1)
        return self.data[offset:end].decode('ascii')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('baseline', help='unmodified original KxCryp.dll for the same architecture')
    parser.add_argument('candidate', help='newly linked KxCryp.dll')
    args = parser.parse_args()
    baseline, candidate = PE(args.baseline), PE(args.candidate)
    if baseline.machine != candidate.machine:
        raise AssertionError('Baseline and candidate architectures differ')
    if baseline.base != 1 or candidate.base != 1:
        raise AssertionError('Original and candidate export ordinal base must remain1')
    if 'NCryptCreateClaim' in baseline.exports:
        raise AssertionError('Baseline must precede the new NCryptCreateClaim export')
    for name, identity in baseline.exports.items():
        if candidate.exports.get(name) != identity:
            raise AssertionError('Old ordinal or forwarder changed: ' + name)
    if set(candidate.exports) - set(baseline.exports) != {'NCryptCreateClaim'}:
        raise AssertionError('Only NCryptCreateClaim may be newly exported')
    if candidate.exports['NCryptCreateClaim'] != (226, None) or 'NCryptCreateClaim' not in candidate.code_exports:
        raise AssertionError('NCryptCreateClaim must be a real code export at ordinal226')
    if candidate.function_count != baseline.function_count + 1:
        raise AssertionError('Unexpected extra/missing EAT slots')
    print('KxCryp export compatibility passed: {} old names/ordinals/forwarders preserved; NCryptCreateClaim@226 added.'.format(len(baseline.exports)), flush=True)


if __name__ == '__main__':
    main()
