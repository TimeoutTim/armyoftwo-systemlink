# SPDX-License-Identifier: GPL-3.0-only
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class BrowserBackgroundTests(unittest.TestCase):
    def test_reopen_after_unused_original_texture_is_collected(self):
        source = (ROOT/'native/lan_browser.c').read_text()
        functions = source[source.index('static u32 bp('):source.index('static u32 eq(')]
        harness = '#include <assert.h>\ntypedef unsigned int u32;\n'+functions+r'''
int main(void) {
    u32 artwork=0x4437df00, overlay=0x44001000;
    assert(background_allowed(overlay,artwork,overlay));
    assert(background_allowed(artwork,artwork,overlay));
    /* The replacement stays referenced; its predecessor is no longer loaded. */
    overlay=0;
    assert(background_allowed(artwork,artwork,overlay));
    assert(!background_allowed(0,artwork,overlay));
    assert(!background_allowed(0x44002000,artwork,overlay));
    assert(!background_allowed(artwork,0,overlay));
    assert(!background_allowed(1,1,0));
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory);(path/'test.c').write_text(harness)
            subprocess.run(['cc','-std=c99','-Wall','-Werror',str(path/'test.c'),
                            '-o',str(path/'test')],check=True,capture_output=True)
            subprocess.run([str(path/'test')],check=True,capture_output=True)
