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
        purpose+'\n\nRequires Python 3 and xdelta3 on PATH; no compiler or Xbox SDK.\n'
        'Keep the original default.xex. In this directory run:\n'
        'python apply_xex_delta.py /path/to/original/default.xex army-of-two.xdelta '
        'manifest.json default-lan.xex\n\n'
        'On Windows use python; on Linux/macOS python3 may be required.\n'
        'Quote paths containing spaces. xdelta3.exe must be on PATH on Windows.\n'
        'The output must not already exist. Input and output hashes are checked.\n'
        'The patch is standard VCDIFF and can also be applied with xdelta3 directly;\n'
        'the provided script adds source-version and final-output verification.\n')
    return metadata


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--source', type=Path, required=True)
    ap.add_argument('--target', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--purpose', required=True)
    args = ap.parse_args()
    print(json.dumps(build(args.source, args.target, args.output, args.purpose), indent=2))
