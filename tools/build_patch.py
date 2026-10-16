#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build a verified standalone XEX from a separately supplied original game.

Uses the checked-in patch by default. --compile regenerates it from native C.
All game-containing inputs and outputs stay outside the tracked source tree.
"""
import argparse
import json
from pathlib import Path
import subprocess
import tomllib

from prepare_persistent_xex import prepare
from apply_xenia_patch import apply
from build_native_lan_patch import build, NAME

ROOT = Path(__file__).resolve().parents[1]
PATCH = ROOT/'patches/454107F8 - Army of Two.patch.toml'
GUARDS = PATCH.with_name('454107F8 - Army of Two.guards.json')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True, help='Untouched supported default.xex')
    parser.add_argument('--xex-tool', type=Path, required=True, help='Pinned acceleration xex2ool executable')
    parser.add_argument('--rebuild-tool', type=Path,
                        default=ROOT/'tools/xex_rebuild/target/release/ao2-xex-rebuild')
    parser.add_argument('--output', type=Path, default=ROOT/'build/release')
    parser.add_argument('--compile', action='store_true', help='Regenerate checked-in patch from C')
    parser.add_argument('--sdk-bin', type=Path, help='Separately supplied XDK bin/win32')
    parser.add_argument('--wine-prefix', type=Path, default=ROOT/'build/wine-prefix')
    args = parser.parse_args()
    if args.compile and not args.sdk_bin:
        parser.error('--compile requires --sdk-bin')
    output = args.output.resolve()
    output.relative_to(ROOT/'build')  # Keep generated game content in ignored build/.
    for tool in (args.xex_tool, args.rebuild_tool):
        if not tool.is_file():
            parser.error('Missing tool: '+str(tool))
    output.mkdir(parents=True, exist_ok=False)
    prepare(args.source, args.xex_tool, output/'prepared')
    pristine = output/'prepared/original-memory-image.pe'
    if args.compile:
        build(ROOT, pristine, output/'native', args.sdk_bin.resolve(), args.wine_prefix.resolve())
    patched, report = apply(pristine.read_bytes(), tomllib.loads(PATCH.read_text()),
                            json.loads(GUARDS.read_text()), [NAME])
    candidate = output/'patched-memory-image.pe'
    with candidate.open('xb') as stream:
        stream.write(patched)
    (output/'patch-report.json').write_text(json.dumps(report, indent=2)+'\n')
    subprocess.run([str(args.rebuild_tool.resolve()),
                    str(output/'prepared/packaging-baseline.xex'), str(candidate),
                    str(output/'default-lan.xex')], check=True, cwd=ROOT)
    print('Verified standalone executable: '+str(output/'default-lan.xex'))


if __name__ == '__main__':
    main()
