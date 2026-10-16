# Third-party dependencies and provenance

Original patch C, build scripts, tests, and the XEX rebuilding wrapper in this repository are licensed GPL-3.0-only. The full license is in `LICENSE`. The generated native patch is built from the included source; original-byte guards identify the supported game revision.

The Rust wrapper uses [`acceleration`](https://github.com/landaire/acceleration) at commit `7fdfb071e39c437119b4cf33170a06cf054f9727`, including its `xex2`, `xecrypt`, `xenon_types`, and `lzxc` crates. These crates declare MIT OR Apache-2.0 licensing. They are fetched as dependencies, not copied into this repository. The pinned [MIT](https://github.com/landaire/acceleration/blob/7fdfb071e39c437119b4cf33170a06cf054f9727/LICENSE-MIT) and [Apache-2.0](https://github.com/landaire/acceleration/blob/7fdfb071e39c437119b4cf33170a06cf054f9727/LICENSE-APACHE) notices remain upstream. `Cargo.lock` records the resolved dependency versions; retain applicable dependency notices when distributing compiled tools.

Python, Rust, a host C compiler, Wine, and xdelta3 are external tools with their own licenses. The Xenon XDK compiler is a separately supplied proprietary tool and is not redistributed. The project does not include SDK headers or libraries, game content, decompiler output, hardware memory captures, or the original executable.

The README hero image (`docs/images/system-link-menu.jpg`) is a real Xbox 360 screenshot documenting the modified campaign menu. The game's artwork remains the property of its respective rights holders and is not covered by the source-code GPL license.
