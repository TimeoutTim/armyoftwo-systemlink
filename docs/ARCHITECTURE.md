# Architecture

The patch is specific to one executable revision. It adds freestanding PowerPC code to verified unused image padding, redirects a small number of native entry points, and uses original game UI objects and peer networking. Every generated write has a comment; `*.guards.json` records its required original bytes. The complete pristine image hash is also checked.

| Source | Responsibility |
| --- | --- |
| `lan_runtime.c` | Entry wrappers, command mailbox, game-thread tick, allocator wrappers |
| `lan_host.c` | Backend-free local identity/session construction and selected campaign URL |
| `lan_menu.c` | Campaign menu routing and native host/mission views |
| `lan_browser.c` | Original Online_Lobby scene, native rows, focus, background, footer, refresh |
| `lan_discovery.c` | Nonblocking UDP queries/offers and session packet dispatch |
| `lan_host_rows.h` | Host cache replacement and five-second expiry |
| `lan_session.c` | Peer admission, roster synchronization, Start and staged cleanup |
| `lan_runtime.h` | Exact-build interfaces and reserved state layout |

Discovery runs every second while the browser is open. Row snapshots keep selection tied to an endpoint/session instance. Session initialization and cleanup run on the game's tick, without a PC worker or resident dashboard plugin. The game remains responsible for campaign replication and its native Start/End controls. This does not reimplement the retired EA backend.

The code uses absolute addresses and validated object layouts. It must not be applied to an arbitrary title update or another game revision. The state block begins at `0x8314CB80`; compiled code starts at `0x82204000`. Build-time checks bound the payload and validate branch ranges and supported relocations. Cleanup islands preserve registers and apply only to the owned LAN game's destruction when optional online services are absent.

The XEX rebuild tool also promotes the payload's existing read-only page descriptor to executable. Changing PE section flags alone is insufficient. It rebuilds container hashes and checks the decoded result, but does not produce a Microsoft retail signature.

The browser is the original `ShellUIPackage.Online_Lobby` UIList. The inspected game resources contain no second session-list widget; nearby matchmaking screens are filters or waiting views. The patch uses the original waiting-screen artwork and its authored framing. Versus support will need separate mode/session integration and testing; the shared browser does not imply that Versus is implemented.
