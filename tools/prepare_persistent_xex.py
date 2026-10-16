#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Prepare and verify pristine on-disk inputs for a persistent title patch.

This is a packaging baseline, not LAN functionality. Never consumes a live dump
as executable input, never overwrites the original and never deploys a console.
Requires the locally built acceleration/xex2ool tool.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

ORIGINAL = '246556f56d4062e05d708359f3e7ab0840b4035f426fa380f5bf152436c1d771'
BASE = 0x82000000
GUARDS = {
    0x826b86b8: '7d8802a6486a3df19421fda07c8b2378',
    0x82964960: '409a0010388000007fe3fb784bffcc9d',
    0x82237550: '7d8802a648b24f5d9421ff203d40821a',
}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def prepare(source, tool, output):
    source, tool, output = source.resolve(), tool.resolve(), output.resolve()
    original = source.read_bytes()
    if digest(original) != ORIGINAL:
        raise ValueError('Requires the identified original disc executable')
    output.mkdir(parents=True, exist_ok=False)
    report = {'source_sha256': ORIGINAL, 'purpose': 'Packaging baseline only; no LAN code',
              'source': str(source), 'tool': str(tool), 'commands': [], 'verified': False}
    def command(*args):
        result = subprocess.run([str(tool), *map(str, args)], capture_output=True, text=True)
        report['commands'].append({'args': list(map(str, args)), 'returncode': result.returncode,
                                   'stdout': result.stdout, 'stderr': result.stderr})
        if result.returncode:
            raise RuntimeError('XEX conversion failed: '+result.stderr)
    try:
        pe = output/'original-memory-image.pe'
        candidate = output/'packaging-baseline.xex'
        roundtrip = output/'roundtrip-memory-image.pe'
        command('basefile', source, '-o', pe)
        data = pe.read_bytes()
        if data[:2] != b'MZ':
            raise ValueError('Decoded image is not a PE')
        header = struct.unpack_from('<I', data, 60)[0]
        if data[header:header+4] != b'PE\0\0':
            raise ValueError('Invalid PE signature')
        optional = header+24
        if struct.unpack_from('<H', data, header+4)[0] != 0x1f2:
            raise ValueError('Expected PowerPC big-endian image')
        if struct.unpack_from('<I', data, optional+28)[0] != BASE:
            raise ValueError('Unexpected image base')
        # XEX basefile output is a mapped image. Raw-file section offsets are
        # different: patch sites must use RVA, not PointerToRawData.
        for address, expected in GUARDS.items():
            value = bytes.fromhex(expected)
            if data[address-BASE:address-BASE+len(value)] != value:
                raise ValueError('Mapped code guard mismatch at '+hex(address))
        command('patch', source, '--decrypt', '--decompress', '-o', candidate)
        command('basefile', candidate, '-o', roundtrip)
        if roundtrip.read_bytes() != data:
            raise ValueError('Packaging changed decoded executable bytes')
        if source.read_bytes() != original:
            raise ValueError('Source unexpectedly changed')
        report.update(verified=True, memory_image_sha256=digest(data),
                      candidate_sha256=digest(candidate.read_bytes()), image_bytes=len(data),
                      address_mapping='mapped PE: file offset = VA - 0x82000000',
                      code_guards={hex(k): v for k, v in GUARDS.items()},
                      hardware_boot_tested=False)
    except Exception as exc:
        report['error'] = str(exc)
        raise
    finally:
        (output/'manifest.json').write_text(json.dumps(report, indent=2)+'\n')
    return report


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--source', type=Path, required=True)
    ap.add_argument('--tool', type=Path,
                    required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    print(json.dumps(prepare(args.source, args.tool, args.output), indent=2))
