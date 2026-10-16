// SPDX-License-Identifier: GPL-3.0-only
/* Original-build host initialization, executed on the game's event thread.
 * Ported from the hardware-verified local_peer/offline_host/direct_factory probes.
 * Every failure is terminal for this title lifetime: never replay partial setup.
 * No FullGameManager tick or EA backend completion is enabled here.
 */
#include "lan_runtime.h"

static void host_zero(u32 p, u32 n) { while (n--) B(p++) = 0; }
static void host_copy(u32 dst, u32 src, u32 n) {
    while (n--) { B(dst++) = B(src++); }
}
static u32 host_ptr(u32 p) { return p >= 0x40000000 && p < 0x80000000; }
static u32 host_fail(u32 error) { state.error = error; return 0; }
static u32 host_new(u32 size, u32 ctor, u32 vtable) {
    u32 p = alloc(size);
    if (!host_ptr(p)) return 0;
    host_zero(p, size);
    G1(ctor, p);
    return W(p) == vtable ? p : 0;
}

u32 lan_prepare_url(u32 is_host, const char *map_url) {
    static const char fallback_url[] =
        "Checkpoint?LoadSaveGame?CheckpointToLoad=01_00?Difficulty=0";
    u32 root, native, settings, config, service, controller, roster;
    u32 gm, identity, game, scratch, user, local_identity, peer, aux;
    u32 bridge, event, factory, transport, sock, flags, attrs, i, length;
    u32 map_length=0;

    if (!map_url) map_url=fallback_url;
    while (map_length<128 && map_url[map_length]) {
        if ((u8)map_url[map_length]<32 || (u8)map_url[map_length]>126)
            return host_fail(0x12A);
        ++map_length;
    }
    if (!map_length || map_length==128) return host_fail(0x12A);

    root = W(0x8314C964); native = W(0x831228E4);
    settings = W(0x831228D0); config = W(0x8314C998);
    service = W(0x8314C9A0);
    if (!root || !native || !settings || !config || !service ||
        W(native) != 0x820E87C8) return host_fail(0x100);
    if (state.gm || W(root+0xD4) || W(root+0xD8) ||
        W(native+0xB64) || W(0x831228D4)) return host_fail(0x101);
    if (W(settings+0x30) || B(native+0xB9C) ||
        B(0x831228E8) != 1 || !W(native+0x698) ||
        B(native+0x69C) >= 49) return host_fail(0x102);
    roster = W(native+0xB80); controller = W(native+0x6B0);
    if (!roster || (is_host && !W(roster)) ||
        !controller || W(controller) != 0x820E90C8) return host_fail(0x103);
    flags = W(config+0x1F4);
    if (flags != 1 && flags != 3) return host_fail(0x104);

    /* Read normal profile initialization before allocating session containers. */
    scratch = alloc(1024);
    if (!host_ptr(scratch)) return host_fail(0x105);
    host_zero(scratch,1024);
    user = (u32)G1(W(W(service)+0x18),service);
    state.user=user;
    if (user > 3) return host_fail(0x129);
    if ((u32)G2(0x82A3A270,user,scratch+768)) return host_fail(0x106);
    if ((u32)G3(0x82A3B090,user,0,scratch+832)) return host_fail(0x128);
    if (W(scratch+844) != 1 && W(scratch+844) != 2) return host_fail(0x107);
    if (!(W(scratch+768)|W(scratch+772))) return host_fail(0x108);
    for (i=0;i<8;i++)
        if (B(scratch+768+i) != B(scratch+832+i)) return host_fail(0x109);
    for (length=0;length<16 && B(scratch+856+length);length++)
        if (B(scratch+856+length)<32 || B(scratch+856+length)==127)
            return host_fail(0x10A);
    if (!length || length==16) return host_fail(0x10B);
    host_copy((u32)state.name,scratch+856,length+1);
    host_copy((u32)state.xuid,scratch+768,8);
    host_copy(scratch+640,scratch+856,length+1);
    state.user=user; state.lan_id=is_host?0x103:0x101; state.player_id=is_host?257:258;

    gm = host_new(0x2B4,0x82237550,0x821A3DE8); state.gm=gm;
    if (!gm) return host_fail(0x110);
    identity = host_new(0x1B0,0x82270C50,0x821A4F98); state.identity=identity;
    if (!identity) return host_fail(0x111);
    game = host_new(0xDE8,0x82240060,0x821A3ED8); state.game=game;
    if (!game) return host_fail(0x112);
    if (W(identity+0x30) || W(game+0x188) || W(game+0x174) ||
        W(game+0x37C) || W(gm+0x1E8) || W(gm+0x94)) return host_fail(0x113);
    local_identity = (u32)G4(0x82271AD0,identity,0,scratch+640,state.lan_id);
    if (!host_ptr(local_identity) || W(identity+0x30)!=local_identity)
        return host_fail(0x114);
    for (i=0;i<8;i++)
        if (B(local_identity+0xA8+i)!=B(scratch+768+i)) return host_fail(0x115);
    G5(0x822562A8,game+0x180,2,0,0,0);
    G5(0x822562A8,game+0x1AC,2,0,1,0);
    G3(0x822580E8,game+0x1D8,2,scratch+800);
    G3(0x822580E8,game+0x1E8,2,scratch+800);
    G5(0x82256608,game+0xC4C,2,0,0,0);
    W(game+0x14)=gm; W(game+0x1F8)=2;
    W(game+0x1FC)=0; W(game+0x200)=0; W(game+0x204)=0;
    peer=(u32)G2(0x822563B8,game+0x180,0); state.peer=peer;
    if (!host_ptr(peer) || W(peer)!=0x821A40F0) return host_fail(0x116);
    if (!W(game+0x1D8) || W(game+0x1DC)-W(game+0x1D8)!=8)
        return host_fail(0x117);
    W(game+0x174)=peer; W(game+0x17C)=peer; W(W(game+0x1D8))=peer;
    W(scratch+0xC)=state.player_id; W(scratch+0x10)=0;
    W(scratch+0x14)=state.lan_id;
    host_copy(scratch+0x18,scratch+640,length+1);
    host_copy(scratch+0x120,scratch+768,8);
    W(root+0xD8)=identity;
    G5(0x82247140,peer,game+8,0,0xFFFFFFFF,scratch);
    aux=(u32)G2(0x822566A8,game+0xC4C,0);
    if (!aux) return host_fail(0x118);
    G4(0x82248748,aux,game,peer,scratch+0x98);
    W(peer+0xC)=aux;
    G2(0x8223D558,game+8,peer);
    if (W(peer+0x2C)!=6 || W(peer+0x1C)!=local_identity ||
        W(game+0x200)!=1 || W(game+0x204) || W(game+0x1B4)!=1)
        return host_fail(0x119);
    W(root+0xD8)=0;

    G4(0x822248C0,config+0x1F0,scratch+800,scratch+804,scratch+808);
    attrs=(u32)G3(0x822141A0,W(scratch+800),W(scratch+804),W(scratch+808));
    if (!host_ptr(attrs)) return host_fail(0x120);
    W(game+0xC78)=attrs;
    G4(0x822248F0,config+0x1F0,scratch+800,scratch+804,scratch+808);
    attrs=(u32)G3(0x822141A0,W(scratch+800),W(scratch+804),W(scratch+808));
    if (!host_ptr(attrs)) return host_fail(0x121);
    W(game+0xC7C)=attrs;
    bridge=alloc(24); event=is_host?alloc(0x38):0; factory=alloc(32);
    if (!host_ptr(bridge)||(is_host&&!host_ptr(event))||!host_ptr(factory)) return host_fail(0x122);
    host_zero(bridge,24); if(event)host_zero(event,0x38); host_zero(factory,32);
    state.bridge=bridge; state.factory=factory;
    if(event)W(event)=0x820E7DD8;

    /* Publish exactly the ownership graph used by the successful native lobby. */
    W(root+0xD4)=gm; W(root+0xD8)=identity;
    W(gm+0x1E8)=game+8; W(gm+0x1EC)=game;
    W(game+0x18)=gm+0xC; W(game+0x1C)=5; W(game+0xA8)=2;
    W(native+0x6A4)=user; if(is_host)W(native+0x6B4)=1; W(native+0xAF0)=0;
    if(!is_host) { W(settings+0x2C)=1; W(settings+0x30)=0; W(settings+0x34)=0; }
    host_zero(native+0x6F0,0x410);
    host_copy(native+0x6F0,(u32)map_url,map_length+1);
    G1(0x82942238,bridge);
    if (W(bridge)!=0x820E7D40 || W(0x831228D4)!=bridge || !W(bridge+0x14))
        return host_fail(0x123);
    W(native+0xB64)=bridge; B(bridge+0x11)=1;

    /* Initialize listener before delivering the original host-lobby event. */
    W(config+0x1F4)=flags|2;
    G1(0x8224B390,factory);
    if (W(factory)!=0x821A41BC || W(factory+0x1C)) return host_fail(0x124);
    W(gm+0x1E0)=32; W(gm+0x1E4)=32;
    G2(0x8224B5E0,factory,6000);
    transport=W(factory+4); sock=W(factory+0xC);
    if (!transport || !sock || W(transport)!=0x8301A8A0) return host_fail(0x125);
    W(scratch+800)=1;
    if (G5(0x82B4CE18,W(sock+0x18),0xFFFF,0x5801,scratch+800,4) ||
        G5(0x82B4CE18,W(sock+0x18),0xFFFF,0x5802,scratch+800,4))
        return host_fail(0x126);
    W(gm+0x94)=factory;
    if (B(native+0x69C)>=49) return host_fail(0x127);
    if(is_host) {
        /* Stock HostGame82948C70 clears this separate slot-allocation mask
         * before HostCompletion82962FD0 clears/rebuilds the roster. Clearing
         * roster pointers alone leaves old slots occupied after disconnect;
         * CreateLocalPlayers82961BA0 would assign the new host slot 1/2/3.
         * This is a new session, before any local or remote admission. */
        B(native+0xB84)=0;
        W(settings+0x2C)=1;W(settings+0x40)=0;
        W(settings+0x3C)=W(settings+0x30);
        G2(0x8293BA70,native+0x5D0,event);
    }
    /* SDK owns event. Scratch is intentionally retained on this first port;
     * all session allocations are reclaimed when the title exits. */
    state.error=0;
    return 1;
}

u32 lan_prepare(u32 is_host) { return lan_prepare_url(is_host,0); }
u32 lan_host(void) { return lan_prepare_url(1,0); }
