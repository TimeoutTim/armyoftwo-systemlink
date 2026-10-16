#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Apply a hash-verified standard xdelta3 patch without replacing the source."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile


def sha256(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024*1024), b''):
            h.update(block)
    return h.hexdigest()


def xdelta(args):
    env = dict(os.environ)
    env.pop('XDELTA', None)
    subprocess.run(['xdelta3', *map(str, args)], env=env, check=True,
                   capture_output=True, text=True)


def apply(source, patch, manifest, output):
    source, patch, manifest, output = map(Path, (source, patch, manifest, output))
    metadata = json.loads(manifest.read_text())
    if metadata.get('format') != 'ao2-xdelta3-v1':
        raise ValueError('Unsupported patch manifest')
    if output.exists() or output.is_symlink():
        raise FileExistsError('Output already exists; original files are never overwritten')
    for path, key in ((source, 'source'), (patch, 'patch')):
        expected = metadata[key]
        if path.stat().st_size != expected['bytes'] or sha256(path) != expected['sha256']:
            raise ValueError(key+' does not match the supported patch input')
    with tempfile.TemporaryDirectory(prefix='.ao2-patch-', dir=output.parent) as tmp:
        candidate = Path(tmp)/'candidate.xex'
        xdelta(['-d', '-D', '-R', '-s', source, patch, candidate])
        target = metadata['target']
        if candidate.stat().st_size != target['bytes'] or sha256(candidate) != target['sha256']:
            raise ValueError('Reconstructed executable hash mismatch')
        # Atomic publication fails if another process created the destination.
        os.link(candidate, output)
    return metadata


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('source', type=Path)
    ap.add_argument('patch', type=Path)
    ap.add_argument('manifest', type=Path)
    ap.add_argument('output', type=Path)
    args = ap.parse_args()
    apply(args.source, args.patch, args.manifest, args.output)
    print('Verified patched executable: '+str(args.output))
