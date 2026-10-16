#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build a standard VCDIFF release artifact and prove exact reconstruction."""
import argparse
import json
from pathlib import Path
import shutil
import tempfile

from apply_xex_delta import apply, sha256, xdelta
from prepare_persistent_xex import ORIGINAL


def build(source, target, output, purpose, require_original=True):
    source, target, output = map(Path, (source, target, output))
    if require_original and sha256(source) != ORIGINAL:
        raise ValueError('Requires the supported untouched Army of Two executable')
    if source.resolve() == target.resolve():
        raise ValueError('Source and candidate must be separate files')
    output.mkdir(parents=True, exist_ok=False)
    patch = output/'army-of-two.xdelta'
    xdelta(['-e', '-D', '-A', '-s', source, target, patch])
    def info(path):
        return {'bytes': path.stat().st_size, 'sha256': sha256(path)}
    metadata = {'format': 'ao2-xdelta3-v1', 'purpose': purpose,
                'source': info(source), 'target': info(target), 'patch': info(patch),
                'roundtrip_verified': False}
    manifest = output/'manifest.json'
    manifest.write_text(json.dumps(metadata, indent=2)+'\n')
    with tempfile.TemporaryDirectory(prefix='ao2-delta-verify-') as tmp:
        apply(source, patch, manifest, Path(tmp)/'rebuilt.xex')
    metadata['roundtrip_verified'] = True
    manifest.write_text(json.dumps(metadata, indent=2)+'\n')
    shutil.copyfile(Path(__file__).with_name('apply_xex_delta.py'), output/'apply_xex_delta.py')
    (output/'INSTALL.txt').write_text(
        purpose+'\n\nStandard xdelta3/VCDIFF patch; no compiler or Xbox SDK needed.\n\n'
        'Windows: download the free, self-contained Delta Patcher:\n'
        'https://github.com/marco-calautti/DeltaPatcher/releases\n'
        'Patch a COPY of your original default.xex with army-of-two.xdelta.\n'
        'Keep checksum validation enabled. Retain the original as a backup.\n\n'
        'Linux CLI (requires xdelta3):\n'
        'xdelta3 -d -s /path/to/original/default.xex army-of-two.xdelta default-lan.xex\n\n'
        'Compare source/output SHA-256 hashes with manifest.json.\n'
        'Optional automatic hash checks (Python 3 and xdelta3 on PATH):\n'
        'python3 apply_xex_delta.py /path/to/original/default.xex army-of-two.xdelta '
        'manifest.json default-lan.xex\n\n'
        'On Windows use python and put xdelta3.exe on PATH for the helper.\n'
        'Quote paths containing spaces. Use a new output filename.\n')
    return metadata


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--source', type=Path, required=True)
    ap.add_argument('--target', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--purpose', required=True)
    args = ap.parse_args()
    print(json.dumps(build(args.source, args.target, args.output, args.purpose), indent=2))
