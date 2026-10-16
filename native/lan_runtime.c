// SPDX-License-Identifier: GPL-3.0-only
#include "lan_runtime.h"

u32 alloc(u32 size) {
    u32 p, i, allocator = W(0x830e1978);
    if (!allocator || W(allocator)!=0x820675a8) return 0;
    p=(u32)G5(0x82481d80,allocator,0,size,16,0);
    if (p<0x40000000 || p>=0x80000000) return 0;
    for(i=0;i<size;i++) B(p+i)=0;
    return p;
}

void release(u32 p) {
    if(p)G2(0x82481DF0,W(0x830E1978),p);
}

static u32 command_is(const char *s,const char *expected) {
    u32 i;
    for(i=0;i<64;i++) {
        if(s[i]!=expected[i])return 0;
        if(!s[i])return 1;
    }
    return 0;
}

/* Each trampoline preserves the displaced mflr r12 and branches to entry+4. */
u64 lan_exec(u64 a,u64 b,u64 c,u64 d,u64 e,u64 f,u64 g,u64 h) {
    const char *cmd=(const char *)(u32)b;
    static const char original_host[]="causeevent MissionSetup_HostPrivate";
    u32 request=0;
    if(cmd && state.magic==0x414f324c) {
        if(command_is(cmd,"AO2LAN SETUP")) {
            /* Preserve the shipped host setup flow. Its CoopHost action later
             * supplies the profile-derived checkpoint URL to lan_plasma_host.
             * Retail waits for FESL/Plasma before opening HostOpen. Queue the
             * stock scene and its original render target for the next pump. */
            u64 result;
            state.requests++;
            state.last_command=7;
            if(state.status)return 1;
            state.host_setup=1;
            state.host_exec_context=(state.menu_context>=0x40000000 &&
                state.menu_context<0x80000000)?W(state.menu_context+0x164):0;
            result=G8(0x8308ba50,a,(u32)original_host,c,d,e,f,g,h);
            if(!state.pending)state.pending=7;
            return result;
        }
        /* MissionSetup_HostPrivate issues this while host_setup is armed.
         * Suppress the dead backend connection; the synthetic event above
         * advances the exact success branch instead. */
        if(state.host_setup && command_is(cmd,"pconnect nexus nexus"))return 1;
        if(state.host_setup &&
           (command_is(cmd,"causeevent CoopHost_HostSelected") ||
            command_is(cmd,"causeevent CoopHost_BackSelected") ||
            command_is(cmd,"AO2LAN HOSTBACK"))) {
            state.requests++;
            state.last_command=command_is(cmd,"AO2LAN HOSTBACK")?10:9;
            return 1;
        }
        /* Runs only after a stock lobby quit confirmation succeeds.
         * Its following CloseScene action stays native; queue session teardown
         * after this UI stack unwinds instead of relying on retired PDisconnect. */
        if(state.game && (state.status==2||state.status==5) &&
           (command_is(cmd,"causeevent HostWaiting_BackSelected") ||
            command_is(cmd,"causeevent QuitCoopClientLobby")))request=14;
        else if(command_is(cmd,"AO2LAN HOST"))request=1;
        else if(command_is(cmd,"AO2LAN JOIN")){state.host_setup=0;request=2;}
        else if(command_is(cmd,"AO2LAN SELECT"))request=6;
        else if(command_is(cmd,"AO2LAN MISSION"))request=8;
        else if(command_is(cmd,"AO2LAN REFRESH"))request=5;
        else if(command_is(cmd,"AO2LAN CANCEL"))request=4;
        if(request) {
            state.requests++;
            state.last_command=request;
            if(!state.pending && state.status!=0xff)state.pending=request;
            return 1;
        }
    }
    return G8(0x8308ba50,a,b,c,d,e,f,g,h);
}

/* CoopHost's SetSearchResultsFilter action constructs the selected checkpoint
 * FString, then calls controller vtable +114 (8295DEE0). Retail immediately
 * enters StartCoopMatch and displays MatchFind. Capture the completed URL here,
 * before either happens; initialize LAN after the stock graph closes its scene.
 * The FString belongs to the caller's stack and must be copied before return. */
u64 lan_selected_mission(u64 controller,u64 selected_url,u64 difficulty) {
    u32 f=(u32)selected_url,p,n,cap,copy,i;
    if(state.magic!=0x414f324c || !state.host_setup)
        return G3(0x8308ba80,controller,selected_url,difficulty);
    if(state.host_setup==2 || state.status)return 0;
    if(f<0x40000000 || f>=0x80000000)goto invalid;
    p=W(f);n=W(f+4);cap=W(f+8);
    if(p<0x40000000||p>=0x80000000||n<2||n>128||cap<n||difficulty>2)goto invalid;
    for(i=0;i<n-1;i++)if(B(p+i)<32||B(p+i)>126)goto invalid;
    if(B(p+n-1))goto invalid;
    copy=alloc(n);
    if(!copy)goto invalid;
    for(i=0;i<n;i++)B(copy+i)=B(p+i);
    /* Scene owner is no longer needed after confirmation. Keep the browser
     * context exclusively owned by the browser, including after Cancel. */
    state.host_exec_context=copy;
    state.host_setup=2;
    state.requests++;
    state.last_command=13;
    state.pending=13;
    return 0;
invalid:
    state.error=0x275;state.status=0xff;
    return 0;
}

/* Scoped replacement for Plasma_HostGame. Ordinary game modes retain the
 * original function; only a System Link host setup consumes this call. */
u64 lan_plasma_host(u64 native,u64 map_url,u64 player_skin,u64 game_type) {
    if(state.magic==0x414f324c && state.host_setup && !state.status) {
        state.host_setup=0;
        state.status=1;
        if(lan_prepare_url(1,(const char *)(u32)map_url))state.status=2;
        else state.status=0xff;
        return 0;
    }
    return G4(0x8308ba60,native,map_url,player_skin,game_type);
}

/* Finishing the retired Host_GameCreate sequence synchronously enters its
 * backend matchmaking path before the adjacent UIAction_HostAO2CoopGame gets
 * to run.  Its manager +B68 object is null offline, and the original routine
 * writes through it at 82958210.  Let that call unwind during armed LAN setup;
 * the UI action then constructs the selected URL and reaches lan_plasma_host. */
u64 lan_matchmaking(u64 matchmaking) {
    if(state.magic==0x414f324c && state.host_setup) {
        state.last_command=11;
        return 0;
    }
    return G1(0x8308ba70,matchmaking);
}

void lan_tick(u32 native) {
    u32 request;
    G1(0x8308ba40,native);
    if(state.magic!=0x414f324c) {
        u32 i;
        for(i=0;i<sizeof(LanState);i++)B(0x8314cb80+i)=0;
        state.magic=0x414f324c;
        state.version=1;
    }
    state.ticks++;
    if(!B(0x831228e8))return;
    lan_menu_tick();
    request=state.pending;
    state.pending=0;
    if(request==7 && state.host_setup && state.host_exec_context) {
        if(!lan_open_host_choice(state.host_exec_context))state.status=0xff;
    } else if(request==8 && state.host_setup && state.host_exec_context) {
        if(!lan_open_mission_choice(state.host_exec_context))state.status=0xff;
    } else if(request==13 && state.host_setup==2) {
        u32 url=state.host_exec_context;
        state.host_setup=0;
        state.status=1;
        if(lan_prepare_url(1,(const char *)url))state.status=2;
        else state.status=0xff;
        state.host_exec_context=0;release(url);
    } else if(request==1 && !state.status) {
        state.status=1;
        if(lan_host())state.status=2;
        else state.status=0xff;
    } else if(request==2 && !state.status) {
        state.status=1; state.browser_published=0;
        if(lan_open_browser() && lan_discovery_search())state.status=3;
        else { if(!state.error)state.error=0x200; state.status=0xff; }
    }
    if(request==6 && state.status==3 && state.browser_published) {
        u32 selected=lan_browser_selected();
        LanHostRow *host=(LanHostRow *)lan_browser_host(selected);
        if(host) {
            /* Selection is frozen before closing the native browser. Restore
             * its help/background state before session allocation opens lobby UI. */
            if(lan_browser_cancel() && lan_session_join(host))state.status=4;
            else state.status=0xff;
        }
    }
    if(request==4 && state.status==3) {
        /* Original browser action closes its own scene; no session yet. */
        if(lan_browser_cancel()) { state.status=0; state.browser_published=0; }
        else state.status=0xff;
    }
    if(request==14)lan_session_leave_lobby();
    lan_discovery_tick();
    if(state.status==3) {
        if(lan_browser_update())state.browser_published=1;
        else state.status=0xff;
    }
    lan_session_tick();
}
