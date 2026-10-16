# SPDX-License-Identifier: GPL-3.0-only
"""Execute the real command dispatcher with a stubbed original game Exec."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class LobbyExitTests(unittest.TestCase):
    def test_confirmed_host_back_queues_cleanup_only_for_lan_lobbies(self):
        source = (ROOT/'native/lan_runtime.c').read_text()
        dispatcher = source[source.index('static u32 command_is'):source.index('/* CoopHost\'s SetSearchResultsFilter')]
        harness = r'''
#include <assert.h>
#include <stdint.h>
typedef uintptr_t u32;
typedef uint64_t u64;
#define W(a) (*(u32 *)(uintptr_t)(a))
struct {u32 magic,host_setup,requests,last_command,status,host_exec_context,
            menu_context,pending,game;} state;
static unsigned original_calls;
static u64 original(u64 a,u64 b,u64 c,u64 d,u64 e,u64 f,u64 g,u64 h) {
    original_calls++;return 99;
}
#define G8(fn,a,b,c,d,e,f,g,h) original(a,b,c,d,e,f,g,h)
'''+dispatcher+r'''
static u64 command(const char *s) {return lan_exec(0,(uintptr_t)s,0,0,0,0,0,0);}
int main(void) {
    const char *back="causeevent HostWaiting_BackSelected";
    state.magic=0x414f324c;
    /* Retail/non-LAN confirmation still reaches the original handler. */
    assert(command(back)==99 && original_calls==1 && !state.pending);
    state.game=0x45000000;state.status=2;
    assert(command(back)==1 && state.pending==14 && original_calls==1);
    state.pending=0;state.status=5;
    assert(command(back)==1 && state.pending==14 && original_calls==1);
    /* The active Startup co-op lobby uses this command for both roles. */
    state.pending=0;
    assert(command("causeevent QuitCoopClientLobby")==1 && state.pending==14);
    state.pending=0;state.game=0;
    assert(command("causeevent QuitCoopClientLobby")==99 && !state.pending);
    state.game=0x45000000;
    /* An in-progress UI request is never overwritten. */
    state.pending=6;assert(command(back)==1 && state.pending==6);
    /* Other scenes and normal start commands remain the game's responsibility. */
    state.pending=0;
    assert(command("causeevent HostWaiting_StartSelected")==99 && !state.pending);
    assert(command("causeevent Unrelated_BackSelected")==99 && !state.pending);
    state.status=6;assert(command(back)==99 && !state.pending);
    state.status=0xff;assert(command(back)==99 && !state.pending);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)
            (path/'test.c').write_text(harness)
            subprocess.run(['cc', '-std=c99', '-Wall', '-Werror', str(path/'test.c'),
                            '-o', str(path/'test')],check=True,capture_output=True)
            subprocess.run([str(path/'test')],check=True,capture_output=True)
