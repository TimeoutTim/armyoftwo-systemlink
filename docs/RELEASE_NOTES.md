# System Link campaign co-op — experimental release

This release restores two-console LAN campaign co-op to the supported Xbox 360 edition of Army of Two (2008), using local profiles and native game menus. No EA server or Xbox Live login is required.

The patch asset contains a standard xdelta3/VCDIFF patch, a hash-checking application script, a manifest, installation instructions, and license information. It requires your original game executable and content. Apply it with Python 3 and xdelta3; no source compilation or SDK is required. See INSTALLING.md in the release archive.

The initial release payload matches development build v43. Host/join, shared campaign gameplay, live discovery, and pre-game host departure have been tested on two real consoles. The latest browser reopen guard fix has automated coverage, but its exact post-disconnect controller sequence still awaits confirmation. Versus, host migration, other executable revisions, and stock retail consoles are unsupported.

Keep your original executable and use a separate game copy. The patch ZIP does not contain an installable XEX. The binary delta is approximately 6 MB because rebuilding the compressed/encrypted container changes most of its bytes; it is not a small list of memory edits.

The manifest identifies the exact original and patched executable hashes and source commit. The matching source ZIP is provided alongside the patch under GPL-3.0-only; this license applies to the original project code, not the game.
