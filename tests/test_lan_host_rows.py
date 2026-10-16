# SPDX-License-Identifier: GPL-3.0-only
"""Execute the discovery cache code used by the Xbox payload on the host."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class HostRowsTests(unittest.TestCase):
    def test_late_hosts_heartbeats_expiry_restart_and_clock_wrap(self):
        source = r'''
#include <assert.h>
typedef unsigned int u32;
typedef unsigned char u8;
#include "lan_host_rows.h"
int main(void) {
    LanHostRow rows[8] = {0};
    u32 n=0,i;
    /* An empty browser can remain open long past the old two-second window. */
    n=lan_rows_expire(rows,n,60000); assert(n==0);
    i=lan_rows_slot(rows,n,101); assert(i==0);
    rows[i].ip=101; rows[i].nonce=11; rows[i].last_seen=60000; n++;
    i=lan_rows_slot(rows,n,103); assert(i==1);
    rows[i].ip=103; rows[i].nonce=22; rows[i].last_seen=61000; n++;
    /* Heartbeats update the existing row without duplicates or reordering. */
    i=lan_rows_slot(rows,n,101); assert(i==0);
    rows[i].last_seen=64000;
    n=lan_rows_expire(rows,n,65999); assert(n==2);
    n=lan_rows_expire(rows,n,66000); assert(n==1 && rows[0].ip==101);
    /* Restart at the same address replaces the previous session instance. */
    i=lan_rows_slot(rows,n,101); assert(i==0);
    rows[i].nonce=33; rows[i].last_seen=67000;
    assert(n==1 && rows[0].nonce==33);
    /* Compaction preserves surviving identity, including profile metadata. */
    rows[1]=rows[0]; rows[1].ip=103; rows[1].name[0]='E';
    rows[1].xuid[7]=42; rows[1].last_seen=71000; n=2;
    n=lan_rows_expire(rows,n,72000);
    assert(n==1 && rows[0].ip==103 && rows[0].name[0]=='E');
    assert(rows[0].xuid[7]==42 && rows[0].nonce==33);
    rows[0].last_seen=0xfffffff0u;
    n=lan_rows_expire(rows,n,0x1377u); assert(n==1);
    n=lan_rows_expire(rows,n,0x1378u); assert(n==0);
    /* Full capacity returns an explicit out-of-range insertion index. */
    for(i=0;i<8;i++)rows[i].ip=i+1;
    assert(lan_rows_slot(rows,8,9)==8);
    assert(lan_rows_slot(rows,8,8)==7);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path/'test.c').write_text(source)
            subprocess.run(['cc', '-std=c99', '-Wall', '-Werror',
                            '-I', str(ROOT/'native'), str(path/'test.c'),
                            '-o', str(path/'test')], check=True, capture_output=True)
            subprocess.run([str(path/'test')], check=True, capture_output=True)
