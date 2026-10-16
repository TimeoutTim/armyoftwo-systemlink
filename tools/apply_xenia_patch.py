#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Apply guarded Xenia integer/byte-array edits to a pristine mapped PE image.

The TOML remains Xenia-compatible. Build-only original-byte guards live in JSON.
Unsupported runtime/emulator-specific operations fail rather than being skipped.
"""
import argparse
import hashlib
import json
from pathlib import Path
import tomllib

TYPES = {'be8': 1, 'be16': 2, 'be32': 4, 'be64': 8}
META = {'name', 'desc', 'author', 'is_enabled'}


def apply(image, document, guards, names=None):
    if hashlib.sha256(image).hexdigest() != guards['source_image_sha256']:
        raise ValueError('Pristine mapped PE hash mismatch')
    if document.get('title_id') != guards['title_id'] or document.get('hash') != guards['module_hash']:
        raise ValueError('Patch identity mismatch')
    base = guards['image_base']
    expected = {}
    for guard in guards['writes']:
        address = guard['address']
        if address in expected:
            raise ValueError('Duplicate original-byte guard')
        expected[address] = bytes.fromhex(guard['before'])
    selected, edits, occupied = [], [], set()
    all_names = [p['name'] for p in document.get('patch', [])]
    if len(set(all_names)) != len(all_names):
        raise ValueError('Duplicate patch names')
    if names is not None and set(names)-set(all_names):
        raise ValueError('Unknown selected patch')
    for patch in document.get('patch', []):
        if not (patch['name'] in names if names is not None else patch.get('is_enabled', False)):
            continue
        selected.append(patch['name'])
        for kind, writes in patch.items():
            if kind in META:
                continue
            if kind not in TYPES and kind != 'array':
                raise ValueError('Unsupported patch operation: '+kind)
            for entry in writes:
                if set(entry) != {'address', 'value'}:
                    raise ValueError('Unsupported write fields')
                address, value = entry['address'], entry['value']
                data = (bytes.fromhex(value.removeprefix('0x')) if kind == 'array'
                        else value.to_bytes(TYPES[kind], 'big'))
                offset = address-base
                if not data or offset < 0 or offset+len(data) > len(image):
                    raise ValueError('Write outside mapped image')
                before = expected.get(address)
                if before is None or len(before) != len(data) or image[offset:offset+len(data)] != before:
                    raise ValueError('Missing/mismatched original bytes at '+hex(address))
                span = set(range(offset, offset+len(data)))
                if occupied & span:
                    raise ValueError('Overlapping patch writes')
                occupied.update(span)
                edits.append({'address': address, 'offset': offset,
                              'before': before.hex(), 'after': data.hex()})
    if not edits:
        raise ValueError('No enabled/selected edits; select a development component explicitly')
    output = bytearray(image)
    for edit in edits:
        data = bytes.fromhex(edit['after'])
        output[edit['offset']:edit['offset']+len(data)] = data
    return bytes(output), {'selected': selected, 'edits': edits,
                           'output_image_sha256': hashlib.sha256(output).hexdigest()}


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('image', type=Path)
    ap.add_argument('toml', type=Path)
    ap.add_argument('guards', type=Path)
    ap.add_argument('output', type=Path)
    ap.add_argument('--patch-name', action='append')
    args = ap.parse_args()
    candidate, report = apply(args.image.read_bytes(), tomllib.loads(args.toml.read_text()),
                              json.loads(args.guards.read_text()), args.patch_name)
    with args.output.open('xb') as stream:
        stream.write(candidate)
    print(json.dumps(report, indent=2))
