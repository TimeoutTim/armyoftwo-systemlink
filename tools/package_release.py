#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Create a verified patch ZIP, matching source ZIP, and SHA-256 checksums.

Run from a clean committed checkout. Never includes the source/target XEX.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import zipfile

from apply_xex_delta import sha256
from build_xex_delta import build

ROOT = Path(__file__).resolve().parents[1]


def package(source, target, version, output):
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]*', version):
        raise ValueError('Invalid release version')
    subprocess.run(['git', 'diff', '--quiet', 'HEAD', '--'], cwd=ROOT, check=True)
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    name = 'armyoftwo-systemlink-'+version
    bundle = output/(name+'-patch')
    archive = output/(name+'-patch.zip')
    sources = output/(name+'-source.zip')
    checksums = output/(name+'-SHA256SUMS.txt')
    if any(p.exists() for p in (bundle, archive, sources, checksums)):
        raise FileExistsError('Release outputs already exist; choose a new output directory')
    metadata = build(source, target, bundle, 'Army of Two System Link '+version)
    metadata.update(release_version=version, source_revision=revision)
    (bundle/'manifest.json').write_text(json.dumps(metadata, indent=2)+'\n')
    for source_file, destination in [('LICENSE', 'LICENSE'),
                                     ('docs/INSTALLING.md', 'INSTALLING.md'),
                                     ('docs/RELEASE_NOTES.md', 'RELEASE_NOTES.md')]:
        shutil.copyfile(ROOT/source_file, bundle/destination)
    (bundle/'SOURCE.txt').write_text(
        'Original project code: GPL-3.0-only. Game content is not relicensed.\n'
        'Corresponding source archive: '+sources.name+'\n'
        'Source commit: '+revision+'\n'
        'Publish the matching source archive alongside this patch.\n')
    files = ['army-of-two.xdelta', 'manifest.json', 'apply_xex_delta.py',
             'INSTALL.txt', 'INSTALLING.md', 'RELEASE_NOTES.md', 'LICENSE', 'SOURCE.txt']
    with zipfile.ZipFile(archive, 'x', compression=zipfile.ZIP_DEFLATED) as z:
        for filename in files:
            z.write(bundle/filename, name+'-patch/'+filename)
    subprocess.run(['git', 'archive', '--format=zip', '--prefix='+name+'-source/',
                    '--output='+str(sources), revision], cwd=ROOT, check=True)
    checksums.write_text(''.join(sha256(p)+'  '+p.name+'\n' for p in (archive, sources)))
    print(json.dumps({'version': version, 'source_revision': revision,
                      'patch_archive': str(archive), 'source_archive': str(sources),
                      'checksums': str(checksums), 'roundtrip_verified': True}, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--target', type=Path, required=True)
    parser.add_argument('--version', required=True)
    parser.add_argument('--output', type=Path, default=ROOT/'build/releases')
    args = parser.parse_args()
    package(args.source, args.target, args.version, args.output)
