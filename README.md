# Army of Two — System Link

**Army of Two (2008) was designed around two-player campaign co-op.** Playing through its campaign with a partner was central to the experience. When EA shut down its servers in 2011, online co-op disappeared. The Xbox 360 version never supported System Link, leaving split-screen as the only remaining way to play the campaign together.

This project exists to restore **multi-system campaign co-op**: two players, each on their own console and screen, sharing the campaign again. The goal is to recover the quality of the original co-op experience, with the game's own menus, mission selection, and gameplay, while keeping it playable without depending on a publisher-operated service.

The current implementation is an experimental LAN patch for the **Xbox 360 version of Army of Two**. One console hosts and another joins using local profiles and the game's existing peer networking. No Xbox Live backend, PC helper process, or DashLaunch plugin is required while playing.

This repository contains the native patch source, generated Xenia-format patch, guarded build tools, and tests. Supply your own supported game executable and original game content. Game assets, executable dumps, SDK files, and development captures are not included.

## What works

- System Link Host and Join entries in the Campaign menu.
- Native mission/checkpoint selection and pre-game co-op lobbies.
- Background LAN discovery, profile names, and controller selection.
- Two-player campaign gameplay on real Xbox 360 hardware.
- Native disconnect handling and session cleanup, including host departure from the pre-game lobby.

The browser's most recent reopen fix (after an unused texture is collected) has automated coverage; repeated controller-driven reopen after disconnect still needs confirmation on that exact revision. This is experimental software, not a compatibility claim for every mission or console setup. Versus, Internet matchmaking, host migration, and retail-console title-update installation are not implemented. See [testing and limitations](docs/TESTING.md).

## Supported game

Only this original executable is supported. Other regions, revisions, and title updates must not be patched by bypassing the guards.

| Identifier | Value |
| --- | --- |
| Title ID | `454107F8` |
| Media ID | `38595BF0` |
| Original Xenia module hash | `2EDC4C4CEC3F7F8D` |
| Original `default.xex` SHA-256 | `246556f56d4062e05d708359f3e7ab0840b4035f426fa380f5bf152436c1d771` |

## Install the release patch (recommended)

Download the **`armyoftwo-systemlink-<version>-patch.zip`** asset from this repository's Releases page and extract it. This is a binary patch for your original `default.xex`; **you do not need to build from source, install the Xbox SDK, or use Wine or Rust**.

Install Python 3 and `xdelta3`, then run from the extracted patch directory:

```sh
python3 apply_xex_delta.py /path/to/original/default.xex \
  army-of-two.xdelta manifest.json default-lan.xex
```

On Windows, use `python` instead of `python3`, and put `xdelta3.exe` on your PATH. The application script checks the original file, patch, and output hashes and preserves your original executable. Copy the resulting `default-lan.xex` alongside your original game content on a modified Xbox 360 and launch it. See [installation and play instructions](docs/INSTALLING.md) for the full steps, requirements, and rollback.

The `.xdelta` file is standard VCDIFF, so compatible xdelta patching frontends can also apply it. The included script is recommended because it verifies the supported game revision and reconstructed executable automatically.

## Building and contributing

[Build instructions](docs/BUILDING.md) cover packaging the checked-in patch and recompiling the native source. [Architecture](docs/ARCHITECTURE.md) describes the hooks and networking.

The TOML uses Xenia's patch syntax, but **enabling it on the original XEX alone is insufficient**: the payload also needs an executable XEX memory page. Both the release binary patch and the source-build pipeline produce an XEX with that change already applied. Use the resulting XEX for hardware and emulator testing.

## Repository layout

- `native/`: freestanding PowerPC C; title-owned initialization, menus, discovery, admission, and cleanup.
- `patches/`: generated TOML plus original-byte guards; comments describe every write.
- `tools/`: compiler/packaging scripts, verified XEX rebuilding, and binary-patch release tools.
- `tests/`: focused tests using synthetic inputs; no game or SDK files needed.

Original project code and patch tooling are licensed **GPL-3.0-only**. See [LICENSE](LICENSE) and [third-party dependencies](THIRD_PARTY.md). This is an independent project, not affiliated with EA or Microsoft. The license does not cover the game or separately supplied SDK.

## AI code-generation disclosure

This project was developed with substantial assistance from generative AI.
AI was used for reverse-engineering hypotheses, implementation, refactoring,
documentation, and build automation under human direction. The behavior was
iteratively reviewed and tested on real Xbox 360 hardware by the project
maintainer. AI-generated code can contain subtle errors; contributors and
users should audit the source and treat native kernel plugins as high-risk
software.