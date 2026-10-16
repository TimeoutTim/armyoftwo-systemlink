# Building

Run these commands from the repository root. The tested host is Linux, with Python 3.11+, Cargo/Rust 1.96.1, and a C compiler (`cc`) for tests. Source recompilation additionally uses Wine and a separately supplied Xenon Xbox 360 XDK compiler. The checked-in patch can be packaged without the XDK or Wine.

All generated game-containing files go under ignored `build/`; optional local inputs go under ignored `inputs/`. Do not commit those files.

## 1. Obtain the original executable

Extract your own game using your preferred Xbox 360 disc extraction tool (for example, extract-xiso), retaining the complete original game directory. Use an untouched `default.xex` whose SHA-256 matches the README. The scripts reject other executables and never overwrite the original. Modified UI packages from older development experiments are not compatible; use original assets.

## 2. Build pinned open-source packaging tools

```sh
cargo build --release --locked --manifest-path tools/xex_rebuild/Cargo.toml
cargo install --locked --git https://github.com/landaire/acceleration \
  --rev 7fdfb071e39c437119b4cf33170a06cf054f9727 \
  --root build/toolchain xex2ool
```

The first tool uses the same pinned `acceleration` revision through Cargo. `Cargo.lock` is tracked. These commands fetch open-source dependencies; they do not fetch a game or SDK.

## 3. Package the checked-in patch

```sh
python3 tools/build_patch.py \
  --source /path/to/your/game/default.xex \
  --xex-tool build/toolchain/bin/xex2ool \
  --output build/release
```

The output directory must not exist. Use a new name for subsequent builds. The script:

1. Checks the original executable hash and extracts its mapped PE.
2. Creates a plaintext/uncompressed packaging baseline and verifies the decoded image is unchanged.
3. Checks the mapped image hash and every original-byte guard before applying the TOML writes.
4. Rebuilds a compressed, encrypted XEX and promotes only the verified payload page to executable.
5. Re-extracts the result and checks exact image equality, page hashes, and the security-header hash.

The playable result is `build/release/default-lan.xex`. The intermediate packaging baseline is **not** the file to install. Neither the rebuilt file nor the baseline is Microsoft-signed for an unmodified retail console.

## 4. Optional: recompile the native C

Supply the Xenon compiler's `bin/win32` directory containing `cl.exe`, `link.exe`, and their accompanying runtime files. These proprietary tools are not distributed here. The compiler uses no SDK headers or linked SDK libraries; the game-specific interfaces are declared in the patch source. This build path currently invokes the compiler through Wine on Linux.

```sh
python3 tools/build_patch.py \
  --source /path/to/your/game/default.xex \
  --xex-tool build/toolchain/bin/xex2ool \
  --output build/from-source \
  --compile \
  --sdk-bin /path/to/XDK/bin/win32 \
  --wine-prefix build/wine-prefix
```

This regenerates the tracked TOML and guards from `native/`, then packages them. Review their diff when changing source. The checked-in payload retains the tested v43 bytes. Compiler/linker versions and output paths can alter debug-record alignment/padding and therefore the payload hash; source compilation is not claimed to be byte-identical across environments. Compiler/linker logs, relocations, and the payload hash are in `build/from-source/native/`. The output must stay under this repository's `build/` directory. Use the pinned compiler toolchain you have validated: other compiler versions may emit unsupported relocations or different payload sizes and are not assumed compatible.

For compilation without packaging, after step 3 has produced the pristine image:

```sh
python3 tools/build_native_lan_patch.py \
  --image build/release/prepared/original-memory-image.pe \
  --output build/native \
  --sdk-bin /path/to/XDK/bin/win32 \
  --wine-prefix build/wine-prefix
```

The expected pristine mapped-PE SHA-256 is `a9452f5d919ff66b822a16b4224d5a0bb0a30b829121579b0d8e092c871b879c`.

## Binary-patch releases

With `xdelta3` installed, generate a VCDIFF bundle locally:

```sh
python3 tools/build_xex_delta.py \
  --source /path/to/your/game/default.xex \
  --target build/release/default-lan.xex \
  --output build/delta \
  --purpose 'Army of Two experimental System Link campaign co-op'
```

The builder verifies exact reconstruction. The bundle includes `army-of-two.xdelta`, a hash manifest, the application script, and instructions. Applying an existing bundle needs only Python and xdelta3, not Rust, Wine, or the SDK. Re-encryption/compression can make the delta large. The source repository tracks the generation tools; binary patches are uploaded as Release assets, not committed to Git. Rebuilt executables are not included in the release bundle.

## Tests

```sh
python3 -m unittest discover -s tests
```

Install `cc` for the host-compiled native logic tests and `xdelta3` for delta roundtrips. The latter tests explicitly skip if xdelta3 is absent. These tests do not exercise the whole game; hardware checks remain necessary.

### Package upload-ready release assets

After committing the source and release notes, run from a clean checkout:

```sh
python3 tools/package_release.py \
  --source /path/to/original/default.xex \
  --target build/release/default-lan.xex \
  --version v0.1.0-alpha.1
```

This creates a patch ZIP, matching committed-source ZIP, and SHA256SUMS file under `build/releases/`. It verifies patch reconstruction before packaging and records the source commit in the manifest. Upload all three files to the same Release. The source archive is included to make the corresponding GPL-3.0 source readily available. Do not upload the original/modified XEX or intermediate images. Existing release outputs are never overwritten; use a fresh output directory when rebuilding an unpublished candidate.
