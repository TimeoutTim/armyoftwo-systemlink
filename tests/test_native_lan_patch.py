# SPDX-License-Identifier: GPL-3.0-only
import importlib.util
from pathlib import Path
import struct
import unittest
import tomllib

spec = importlib.util.spec_from_file_location('native_build', Path(__file__).parents[1]/'tools/build_native_lan_patch.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

class NativePatchTests(unittest.TestCase):
    def test_cleanup_islands_preserve_registers_and_scope_skips(self):
        # Execute the emitted integer subset, checking behavior rather than
        # matching one expected byte stream. PPC compares only CR0 here.
        def execute(code, start, regs, memory, cr):
            pc = start
            def sx16(x): return (x & 0x7fff)-(x & 0x8000)
            def read(a,n): return int.from_bytes(bytes(memory.get(a+i,0) for i in range(n)), 'big')
            def write(a,v,n):
                for i,b in enumerate((v & ((1<<(8*n))-1)).to_bytes(n,'big')): memory[a+i]=b
            for _ in range(100):
                if not start <= pc < start+len(code): return pc,cr
                w=int.from_bytes(code[pc-start:pc-start+4],'big'); pc+=4
                op=w>>26; rt=(w>>21)&31; ra=(w>>16)&31; rb=(w>>11)&31
                address=((regs[ra] if ra else 0)+sx16(w)) & 0xffffffff
                if op==37: write(address,regs[rt],4); regs[ra]=address
                elif op==62: write(address,regs[rt],8)
                elif op==58: regs[rt]=read(address,8)
                elif op==36: write(address,regs[rt],4)
                elif op==32: regs[rt]=read(address,4)
                elif op==15: regs[rt]=(sx16(w)<<16)&0xffffffffffffffff
                elif op==14: regs[rt]=((regs[ra] if ra else 0)+sx16(w))&0xffffffffffffffff
                elif op==24: regs[ra]=regs[rt]|(w&0xffff)
                elif op==11 or (op==31 and ((w>>1)&1023)==0):
                    a=regs[ra]&0xffffffff
                    b=(sx16(w)&0xffffffff) if op==11 else regs[rb]&0xffffffff
                    cr=(cr&0x0fffffff)|((2 if a==b else 4)<<28)
                elif w==0x7D800026: regs[12]=cr
                elif w==0x7D8FF120: cr=regs[12]&0xffffffff
                elif op==16:
                    equal=bool(cr&0x20000000)
                    if equal == (((w>>21)&31)==12): pc=pc-4+sx16(w&0xfffc)
                elif op==18:
                    d=w&0x3fffffc
                    if d&0x2000000: d-=0x4000000
                    pc=pc-4+d
                else: self.fail(hex(w))
            self.fail('Island did not terminate')
        start=m.PAYLOAD+0x9000
        for site,old,skip,adjust,nullreg in m.CLEANUP_GUARDS:
            for failure in (None,'service','magic','owner','game_zero','session_zero','stage'):
                with self.subTest(site=hex(site),failure=failure):
                    regs=[0x1234567800000000+i for i in range(32)]
                    regs[1]=0x70001000; regs[31]=0x45000000+adjust
                    regs[nullreg]=0 if failure!='service' else 0x44000000
                    before=regs.copy(); memory={}
                    def put(a,v):
                        for i,b in enumerate(v.to_bytes(4,'big')): memory[a+i]=b
                    put(0x8314cb80,0 if failure=='magic' else 0x414f324c)
                    put(0x8314cb9c,0 if failure=='game_zero' else 0x45000010 if failure=='owner' else 0x45000000)
                    put(0x8314cbf0,0 if failure=='session_zero' else 0x46000000)
                    put(0x460001a4,2 if failure=='stage' else 5)
                    put(regs[3]&0xffffffff,0x11223344)
                    code=m.cleanup_island(start,site,old,skip,adjust,nullreg)
                    target,cr=execute(code,start,regs,memory,0x95a63c7e)
                    self.assertEqual(target,skip if failure is None else site+4)
                    self.assertEqual(cr,0x95a63c7e)
                    self.assertEqual(regs[1],before[1]); self.assertEqual(regs[12],before[12])
                    if failure is None or nullreg==27: self.assertEqual(regs[11],before[11])
                    else: self.assertEqual(regs[11],0x11223344)
                    if failure is not None and nullreg==27:
                        self.assertEqual(regs[26],(before[27]+0x110)&0xffffffffffffffff)
                    for r in set(range(32))-{1,11,12,26}: self.assertEqual(regs[r],before[r])

    def test_cleanup_islands_padding_limit_and_hook_guards(self):
        payload,hooks=m.append_cleanup_islands(bytes(13))
        self.assertEqual(hooks[0][3],m.PAYLOAD+16)
        self.assertEqual(len(hooks),4)
        for hook,guard in zip(hooks,m.CLEANUP_GUARDS):
            site,replacement,expected,island=hook
            self.assertEqual(expected,guard[1].to_bytes(4,'big'))
            self.assertEqual(replacement,m.branch(site,island))
        with self.assertRaisesRegex(ValueError,'reserved padding'):
            m.append_cleanup_islands(bytes(0xc000))

    def test_standalone_artifact_includes_offline_start_gate(self):
        root = Path(__file__).parents[1]
        doc = tomllib.loads((root/'patches/454107F8 - Army of Two.patch.toml').read_text())
        patch = next(p for p in doc['patch'] if p['name'] == m.NAME)
        writes = {e['address']: bytes.fromhex(e['value']) for e in patch['array']}
        self.assertEqual(writes[0x82964960], bytes.fromhex('48000010'))

    def test_branch_limits_and_signed_backward_displacement(self):
        self.assertEqual(m.branch(0x8308ba44, 0x829643ec).hex(), '4b8d89a8')
        for target in (0x85000000, 0x80000000, 0x83000001):
            with self.assertRaises(ValueError):
                m.branch(0x83000000, target)

    def test_rejects_omitted_writable_storage(self):
        data = bytearray(0x300)
        struct.pack_into('<I', data, 60, 0x80)
        struct.pack_into('<H', data, 0x86, 1)
        struct.pack_into('<H', data, 0x94, 224)
        for name in (b'.data', b'.bss'):
            struct.pack_into('<8sIIII', data, 0x98+224,
                             name, 132, 0x20000, 0, 0)
            with self.assertRaisesRegex(ValueError, 'Writable section'):
                m.relocate_text(data)

    def test_xenon_highadj_low_signed_carry(self):
        # Real Xenon fixups address BE instructions, and HIGHADJ carries a signed
        # low-half addend. This relocation crosses the sign bit of the low half.
        data = bytearray(0x500)
        struct.pack_into('<I', data, 60, 0x80)
        struct.pack_into('<H', data, 0x86, 2)
        struct.pack_into('<H', data, 0x94, 224)
        struct.pack_into('<I', data, 0x98+28, 0x83090000)
        table = 0x98+224
        struct.pack_into('<8sIIII',data,table,b'.text',8,0x10000,8,0x300)
        struct.pack_into('<8sIIII',data,table+40,b'.reloc',16,0x20000,16,0x400)
        data[0x300:0x308] = bytes.fromhex('3d60830a396b0034')
        struct.pack_into('<IIHHHH', data, 0x400, 0x10000,16,0x4000,0x0034,0x2004,0)
        code,delta,records=m.relocate_text(data)
        self.assertEqual(code[:2].hex(),'3d60')
        self.assertEqual(code[4:6].hex(),'396b')
        hi=int.from_bytes(code[2:4],'big')
        lo=int.from_bytes(code[6:8],'big',signed=True)
        self.assertEqual((hi<<16)+lo,m.PAYLOAD+0x34)
        self.assertEqual(len(records),2)
        struct.pack_into('<H',data,0x408,0x3000)
        with self.assertRaises(ValueError):m.relocate_text(data)

if __name__ == '__main__':unittest.main()
