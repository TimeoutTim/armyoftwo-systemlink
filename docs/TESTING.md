# Validation and limitations

The release source was extracted from the development revision `86fea1e` (v43) into a new repository without its development history or artifacts.

## Observed on real hardware

Two consoles have hosted, discovered each other by local profile name, joined a native co-op lobby, and entered the same campaign map with synchronized movement. Gameplay joining/leaving and subsequent hosting were user-confirmed during development. Host B in the pre-game lobby now ends both sessions and displays the native disconnect dialog. Continuous browser discovery and improved original-artwork framing were also user-confirmed.

The last change fixes Join reopening after cleanup: the browser's replacement texture remains referenced while the unused original overlay can be collected. The old guard incorrectly required both textures. Live object inspection established the missing original texture and the valid replacement reference; a host-compiled regression test exercises the corrected guard. A controller-driven repeat of that exact v43 reopen sequence remains pending.

## Automated checks

The clean repository keeps tests for patch application guards, payload relocation, cleanup-island register preservation and scoping, discovery-cache expiry/restart behavior, lobby command routing, background-reference validation, and synthetic VCDIFF roundtrips. These tests do not need game files or the SDK. Full packaging additionally validates mapped-image equality and XEX header/page hashes.

For each game-facing change, check:

1. Open Join before a host exists; create a host and check it appears.
2. Join and reach the lobby; leave on the host before gameplay.
3. Dismiss the native disconnect message and reopen Join without restarting.
4. Host/join again and start a mission; verify both players move together.
5. Leave during gameplay and check native handling and subsequent session creation.
6. Cancel an empty host lobby and host again.

## Not established

- Every mission, checkpoint, difficulty, DLC combination, or long-running session.
- Host departure during partial admission, sudden network loss, and all timeout cases.
- More than two campaign players, Versus, host migration, cross-subnet discovery, Internet play, or a replacement Xbox Live service.
- Emulator-to-emulator and emulator-to-console gameplay compatibility.
- Other executable revisions or operation on unmodified retail consoles.

Treat the current build as an experimental release candidate. Keep the original game executable and separate test saves available.

## Clean-repository build validation

The initial release preparation passed all 21 retained tests. The Rust rebuild
tool and pinned xex2ool were built from their declared dependencies. Native C
compiled with explicitly supplied compiler files and a fresh Wine prefix.
Packaging the checked-in patch from the supported original executable produced
SHA-256 `2ae49c1bd0b991d236c2c87f0d665fbe8d6c70af7ba0934f61950fd6abcdf30b`,
byte-identical to the v43 executable deployed during development. The optional
xdelta bundle also passed exact reconstruction. These generated artifacts are
local build outputs and are not part of the repository or source archive.
