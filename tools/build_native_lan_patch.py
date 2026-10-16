#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compile freestanding Xenon C and generate the guarded Xenia-format patch.
Requires the locally supplied XDK and Wine; no SDK or game files are distributed.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess

BASE = 0x82000000
PAYLOAD = 0x82204000
SOURCE_SHA = 'a9452f5d919ff66b822a16b4224d5a0bb0a30b829121579b0d8e092c871b879c'
NAME = 'System Link campaign co-op (experimental)'


def helpers():
    code, symbols, strings = b'', b'', b''
    for prefix in ('save', 'rest'):
        for reg in range(14, 32):
            name = f'__{prefix}gprlr_{reg}'.encode()+b'\0'
            symbols += struct.pack('<IIIhHBB', 0, 4+len(strings), len(code), 1, 0x20, 2, 0)
            strings += name
            opcode = 0xf8000000 if prefix == 'save' else 0xe8000000
            code += struct.pack('>I', opcode | reg << 21 | 1 << 16 | (-8*(33-reg) & 0xffff))
        code += bytes.fromhex('9181fff84e800020' if prefix == 'save' else '8181fff87d8803a64e800020')
    header = struct.pack('<HHIIIHH', 0x1f2, 1, 0, 60+len(code), 36, 0, 0)
    section = struct.pack('<8sIIIIIIHHI', b'.text', 0, 0, len(code), 60, 0, 0, 0, 0, 0x60500020)
    return header+section+code+symbols+struct.pack('<I', len(strings)+4)+strings


def relocate_text(data):
    pe = struct.unpack_from('<I', data, 60)[0]
    count = struct.unpack_from('<H', data, pe+6)[0]
    opt_size = struct.unpack_from('<H', data, pe+20)[0]
    image_base = struct.unpack_from('<I', data, pe+24+28)[0]
    sections = {}
    for i in range(count):
        name, size, rva, raw_size, raw = struct.unpack_from('<8sIIII', data, pe+24+opt_size+40*i)
        section_name = name.rstrip(b'\0')
        if section_name in (b'.data', b'.bss', b'.sdata', b'.sbss') and size:
            raise ValueError('Writable section would be omitted from payload: '
                             + section_name.decode('ascii'))
        sections[section_name] = (size, rva, raw)
    size, start, raw = sections[b'.text']
    payload = bytearray(data[raw:raw+size])
    delta = PAYLOAD-(image_base+start)
    rs, _, rp = sections[b'.reloc']
    pos, end, applied = rp, rp+rs, []
    while pos < end:
        page, block_size = struct.unpack_from('<II', data, pos)
        if block_size < 8 or pos+block_size > end:
            raise ValueError('Malformed relocation block')
        cursor = pos+8
        while cursor < pos+block_size:
            entry = struct.unpack_from('<H', data, cursor)[0]; cursor += 2
            kind, offset = entry >> 12, page+(entry & 0xfff)-start
            extra = None
            if kind == 4:
                extra = struct.unpack_from('<h', data, cursor)[0]; cursor += 2
            if kind == 0 or not 0 <= offset < size:
                continue
            # Xenon fixups point at the BE instruction, not its immediate half.
            if kind not in (2, 4) or offset+4 > size:
                raise ValueError(f'Unsupported text relocation {kind}')
            word = int.from_bytes(payload[offset:offset+4], 'big')
            if kind == 4:
                if word >> 26 != 15:
                    raise ValueError('HIGHADJ must target addis')
                immediate = (((word & 0xffff) << 16)+extra+delta+0x8000) >> 16
            else:
                immediate = (word & 0xffff)+delta
            payload[offset:offset+4] = ((word & 0xffff0000) | (immediate & 0xffff)).to_bytes(4, 'big')
            applied.append({'offset': offset, 'type': kind})
        pos += block_size
    # Strip linker debug timestamps/path records from the embedded bytes. They
    # are not executable data and would otherwise make identical builds differ.
    debug_rva, debug_size = struct.unpack_from('<II', data, pe+24+96+6*8)
    if debug_size:
        if debug_size % 28 or not start <= debug_rva <= start+size-debug_size:
            raise ValueError('Unexpected debug directory location')
        for entry in range(debug_rva-start, debug_rva-start+debug_size, 28):
            length, address = struct.unpack_from('<II', payload, entry+16)
            if not start <= address <= start+size-length:
                raise ValueError('Unexpected debug data location')
            payload[address-start:address-start+length] = bytes(length)
        payload[debug_rva-start:debug_rva-start+debug_size] = bytes(debug_size)
    if len(payload) > 0xC000:
        raise ValueError('Native code exceeds verified padding')
    return bytes(payload), delta, applied


def branch(source, target):
    delta = target-source
    if delta % 4 or not -0x2000000 <= delta < 0x2000000:
        raise ValueError('Branch out of range')
    return (0x48000000 | (delta & 0x3fffffc)).to_bytes(4, 'big')



# Missing optional online services are valid for our offline ownership graph.
# site, displaced instruction, skip target, object adjustment, null register
CLEANUP_GUARDS = (
    (0x822403E4, 0x81630000, 0x822403FC, 0, 3),
    (0x8223AC20, 0x81630000, 0x8223AC74, 8, 3),
    (0x8223AE70, 0x81630000, 0x8223AE88, 8, 3),
    (0x8223AE90, 0x3B5B0110, 0x8223AF2C, 8, 27),
)


def cleanup_island(address, site, displaced, skip, object_adjust, null_register):
    """Guard only LAN destruction; preserve full r11/r12, CR, LR and CTR.

    LanState.game/session_context are +1C/+70; JoinContext.teardown is +1A4.
    A private 32-byte frame avoids overwriting the destructor's local storage.
    No calls occur, so saving LR/CTR is unnecessary. All failure exits restore
    registers/CR before replaying the exact displaced instruction.
    """
    words = []
    failures = []
    def emit(word): words.append(word)
    def fail_unless_equal():
        failures.append(len(words)); emit(0)  # bne cr0, ordinary
    emit(0x9421FFE0)  # stwu r1,-32(r1)
    emit(0xF9610008)  # std r11,8(r1)
    emit(0xF9810010)  # std r12,16(r1)
    emit(0x7D800026)  # mfcr r12
    emit(0x91810018)  # stw r12,24(r1)
    emit(0x2C000000 | (null_register << 16))  # cmpwi absent service,0
    fail_unless_equal()
    emit(0x3D608315)  # lis r11,0x8315
    emit(0x818BCB80)  # lwz r12,LanState.magic(r11)
    emit(0x3D60414F); emit(0x616B324C)  # r11 = AO2L
    emit(0x7C0C5800)  # cmpw r12,r11
    fail_unless_equal()
    emit(0x3D608315); emit(0x818BCB9C)  # lwz r12,state.game
    emit(0x2C0C0000)  # cmpwi r12,0
    nonzero = len(words); emit(0)  # beq ordinary
    if object_adjust:
        emit(0x398C0000 | object_adjust)  # addi r12,r12,8
    emit(0x7C1F6000)  # cmpw r31,r12
    fail_unless_equal()
    emit(0x818BCBF0)  # lwz r12,state.session_context
    emit(0x2C0C0000)
    session_zero = len(words); emit(0)  # beq ordinary
    emit(0x818C01A4)  # lwz r12,JoinContext.teardown
    emit(0x2C0C0005)
    fail_unless_equal()
    restore = [0x81810018, 0x7D8FF120, 0xE9610008, 0xE9810010,
               0x38210020]  # lwz CR; mtcrf 255; ld r11/r12; addi sp,32
    words.extend(restore)
    emit(int.from_bytes(branch(address+4*len(words), skip), 'big'))
    ordinary = len(words)
    words.extend(restore)
    emit(displaced)
    emit(int.from_bytes(branch(address+4*len(words), site+4), 'big'))
    for index in failures:
        words[index] = 0x40820000 | ((ordinary-index)*4)
    for index in (nonzero, session_zero):
        words[index] = 0x41820000 | ((ordinary-index)*4)
    return struct.pack('>'+str(len(words))+'I', *words)


def append_cleanup_islands(payload):
    payload = bytearray(payload)
    payload.extend(bytes((-len(payload)) % 16))
    hooks = []
    for site, old, skip, adjustment, register in CLEANUP_GUARDS:
        address = PAYLOAD+len(payload)
        payload.extend(cleanup_island(address, site, old, skip, adjustment, register))
        hooks.append((site, branch(site,address), old.to_bytes(4,'big'), address))
    if len(payload) > 0xC000:
        raise ValueError('Native payload and cleanup islands exceed reserved padding')
    return bytes(payload), hooks


def build(root, original, output, sdk, wine_prefix):
    image = original.read_bytes()
    if hashlib.sha256(image).hexdigest() != SOURCE_SHA:
        raise ValueError('Wrong original mapped image')
    output.mkdir(parents=True, exist_ok=True)
    output = output.relative_to(root)
    env = dict(os.environ, WINEPREFIX=str(wine_prefix), WINEDEBUG='-all')
    if not all((sdk/(tool+'.exe')).is_file() for tool in ('cl', 'link')):
        raise ValueError('--sdk-bin must contain the Xenon cl.exe and link.exe')
    def run(tool, args):
        with (output/(tool+'.log')).open('wb') as log:
            subprocess.run(['wine', str(sdk/(tool+'.exe')), *args], env=env, cwd=root,
                           stdout=log, stderr=subprocess.STDOUT, check=True)
    sources = ['lan_runtime', 'lan_host', 'lan_menu', 'lan_browser', 'lan_discovery', 'lan_session']
    run('cl', ['/nologo', '/c', '/O1', '/GS-', '/Zl', '/Fo'+str(output)+'/',
               *[str(Path('native')/(s+'.c')) for s in sources]])
    (output/'helpers.obj').write_bytes(helpers())
    run('link', ['/NOLOGO', '/MACHINE:PPCBE', '/SUBSYSTEM:XBOX', '/NODEFAULTLIB',
                 '/ENTRY:lan_tick', '/INCLUDE:lan_exec', '/INCLUDE:lan_plasma_host',
                 '/INCLUDE:lan_matchmaking', '/INCLUDE:lan_selected_mission',
                 '/BASE:0x83090000', '/FIXED:NO', '/XEX:NO',
                 '/MERGE:.rdata=.text', '/OUT:'+str(output/'lan.pe'), '/MAP:'+str(output/'lan.map'),
                 *[str(output/(s+'.obj')) for s in sources], str(output/'helpers.obj')])
    payload, delta, relocations = relocate_text((output/'lan.pe').read_bytes())
    symbols = {}
    for name, address in re.findall(r'\s(lan_tick|lan_exec|lan_plasma_host|lan_matchmaking|lan_selected_mission)\s+([0-9a-fA-F]{8})\s', (output/'lan.map').read_text()):
        symbols[name] = int(address, 16)+delta
    if set(symbols) != {'lan_tick', 'lan_exec', 'lan_plasma_host', 'lan_matchmaking', 'lan_selected_mission'}:
        raise ValueError('Missing entry symbols')
    payload, cleanup_hooks = append_cleanup_islands(payload)
    writes = []
    def write(address, value, expected=None):
        before = image[address-BASE:address-BASE+len(value)]
        if expected is not None and before != expected:
            raise ValueError('Guard mismatch '+hex(address))
        writes.append((address, before, value))
    write(PAYLOAD, payload, bytes(len(payload)))
    for site, replacement, expected, island in cleanup_hooks:
        write(site, replacement, expected)
    for trampoline, original_entry, name in [(0x8308ba40,0x829643e8,'lan_tick'),
                                               (0x8308ba50,0x826b86b8,'lan_exec'),
                                               (0x8308ba60,0x82948c70,'lan_plasma_host'),
                                               (0x8308ba70,0x829581f0,'lan_matchmaking'),
                                               (0x8308ba80,0x8295dee0,'lan_selected_mission')]:
        write(trampoline, bytes.fromhex('7d8802a6')+branch(trampoline+4, original_entry+4), bytes(8))
        write(original_entry, branch(original_entry, symbols[name]), bytes.fromhex('7d8802a6'))
    # Extend existing virtual ranges into verified padding. Original raw pointers
    # are irrelevant to this memory-layout XEX basefile; no section is moved.
    for offset, old, new in [(0x2a0,0xe7ba40,0xe7bc00),(0x278,0x28f8,0xf000),
                             (0x294,0x40000040,0x60000040),(0x2f0,0xacb7c,0xacc00)]:
        write(BASE+offset, struct.pack('<I',new), struct.pack('<I',old))
    write(0x8314cb80, bytes(128), bytes(128))
    # Required by the working offline Start flow; ship with the native payload
    # so selecting this standalone entry cannot omit the readiness component.
    write(0x82964960, bytes.fromhex('48000010'), bytes.fromhex('409a0010'))
    patch = root/'patches/454107F8 - Army of Two.patch.toml'
    text = patch.read_text().split('# BEGIN GENERATED NATIVE LAN')[0]
    text = text.split('# Development component only.')[0].rstrip()+'\n\n'
    text += '# BEGIN GENERATED NATIVE LAN — tools/build_native_lan_patch.py\n'
    text += '# Build component: this larger payload requires the XEX loader page at 0x82200000\n'
    text += '# to become executable. tools/xex_rebuild performs that guarded metadata change.\n'
    text += '# Enabling this entry on the original XEX in Xenia is NOT sufficient.\n'
    text += '[[patch]]\n    name = '+json.dumps(NAME)+'\n'
    text += '    desc = "Binary-rebuild component: executable XEX page promotion required. Experimental native host, browser, discovery and joining. Includes offline readiness gate; Campaign LAN; experimental lifecycle and UI. Original assets required."\n'
    text += '    author = "ArmyOfTwo LAN project"\n    is_enabled = false\n'
    guards = {'title_id':'454107F8','module_hash':'2EDC4C4CEC3F7F8D','image_base':BASE,
              'source_image_sha256':SOURCE_SHA,'writes':[]}
    descriptions = {
        0x82964960: 'Offline readiness gate: bne 0x82964970 -> b 0x82964970 (409A0010 -> 48000010).\n'
                    'Required for prepared LAN Start without Xbox Live; retain matching-player checks and timeout.',
        PAYLOAD: f'Install {len(payload)} bytes of compiled native LAN code and constants in verified unused padding.\n'
                 'Contains menu routing, local session initialization, browser, UDP discovery, lobby admission and LAN platform completion.\n'
                 'Generated from native/*.c; little-endian PE relocation records are resolved into PowerPC instructions.',
        0x8308ba40: 'Original Plasma event-pump trampoline: execute displaced mflr r12, then branch to 0x829643EC.',
        0x829643e8: f'Replace Plasma pump entry mflr r12 with branch to lan_tick at 0x{symbols["lan_tick"]:08X}.\n'
                    'The wrapper calls the original pump, then processes LAN commands/network state on the game thread.',
        0x8308ba50: 'Original EngineExec trampoline: execute displaced mflr r12, then branch to 0x826B86BC.',
        0x826b86b8: f'Replace EngineExec entry mflr r12 with branch to lan_exec at 0x{symbols["lan_exec"]:08X}.\n'
                    'Intercept AO2LAN commands; forward unrelated commands through the original trampoline.',
        0x8308ba60: 'Original Plasma_HostGame trampoline: execute displaced mflr r12, then branch to 0x82948C74.',
        0x82948c70: f'Replace Plasma_HostGame entry mflr r12 with branch to lan_plasma_host at 0x{symbols["lan_plasma_host"]:08X}.\n'
                    'Consume the original UI-selected checkpoint URL only for an armed System Link host; forward every other call.',
        0x8308ba70: 'Original backend matchmaking trampoline: execute displaced mflr r12, then branch to 0x829581F4.',
        0x829581f0: f'Replace backend matchmaking entry mflr r12 with branch to lan_matchmaking at 0x{symbols["lan_matchmaking"]:08X}.\n'
                    'During armed System Link setup, skip the retired backend call whose required object is null; forward unrelated calls.',
        0x8308ba80: 'Original selected-mission dispatch trampoline: displaced mflr r12, then branch to 0x8295DEE4.',
        0x8295dee0: f'Replace selected-mission dispatch with branch to lan_selected_mission at 0x{symbols["lan_selected_mission"]:08X}.\n'
                    'For armed LAN hosting, copy the stock checkpoint/difficulty URL and queue LAN initialization after scene close.\n'
                    'Bypass StartCoopMatch and its MatchFind popup; forward every unrelated call unchanged.',
        BASE+0x2a0: 'PE .text VirtualSize: 0xE7BA40 -> 0xE7BC00 (little-endian), covering the five trampolines.',
        BASE+0x278: 'PE BINKBSS VirtualSize: 0x28F8 -> 0xF000 (little-endian), covering the native payload padding.',
        BASE+0x294: 'PE BINKBSS characteristics: 0x40000040 -> 0x60000040 (little-endian), adding execute permission.\n'
                    'The XEX page descriptor must also change from read-only type 3 to code type 1 during rebuild.',
        BASE+0x2f0: 'PE .data VirtualSize: 0xACB7C -> 0xACC00 (little-endian), including the reserved LAN state block.',
        0x8314cb80: 'Reserve/zero 128 bytes of existing .data tail padding for LanState. No original state is overwritten.\n'
                    'This zero-to-zero write documents and guards the reservation; the native tick initializes its magic/version.',
    }
    cleanup_reasons = ('full-game scheduler unregister', 'base-game backend cancellation',
                       'base-game scheduler unregister', 'base-game backend task removal')
    for (site, replacement, expected, island), guard, reason in zip(cleanup_hooks, CLEANUP_GUARDS, cleanup_reasons):
        descriptions[site] = (f'Guard {reason}: branch to 0x{island:08X}.\n'
            f'Only for the owned LAN game during teardown stage 5 with its service absent, skip to 0x{guard[2]:08X}.\n'
            f'Otherwise restore registers/CR and execute original instruction {expected.hex().upper()}; preserve native resource cleanup.')
    for address, before, after in writes:
        text += '\n'+''.join('    # '+line+'\n' for line in descriptions[address].splitlines())
        text += f'\n    [[patch.array]]\n        address = 0x{address:08X}\n        value = "{after.hex()}"\n'
        guards['writes'].append({'address':address,'before':before.hex()})
    patch.write_text(text)
    patch.with_name(patch.name.replace('.patch.toml','.guards.json')).write_text(json.dumps(guards,indent=2)+'\n')
    report = {'payload_address':PAYLOAD,'payload_size':len(payload),'symbols':symbols,'relocations':relocations,
              'payload_sha256':hashlib.sha256(payload).hexdigest()}
    (output/'build.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='relocations'},indent=2))

if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--image',type=Path,required=True)
    ap.add_argument('--output',type=Path,default=Path('build/native'))
    ap.add_argument('--sdk-bin', type=Path, required=True, help='Separately supplied XDK bin/win32 directory')
    ap.add_argument('--wine-prefix', type=Path, default=Path('build/wine-prefix'))
    args=ap.parse_args()
    build(Path(__file__).resolve().parents[1],args.image.resolve(),args.output.resolve(),args.sdk_bin.resolve(),args.wine_prefix.resolve())
