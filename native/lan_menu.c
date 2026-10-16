// SPDX-License-Identifier: GPL-3.0-only
/* Loaded-object menu adaptation for the exact supported executable.
 * Run only on the game/UI event thread, after ordinary profile initialization.
 * UProperty.Offset (+0x64), scene arrays, tags and SetCaption were established
 * by inspect_ui_state.py and lan_browser_rows.py / profile-captions.json.
 * Nested SequenceOp property names are verified Engine.xxx exports; their
 * offsets are deliberately obtained from reflection rather than guessed.
 */
#include "lan_runtime.h"

static u32 menu_ptr(u32 p) { return p >= 0x40000000 && p < 0x80000000; }
static u32 menu_equal(u32 p, const char *s) {
    u32 i;
    if (!p) return 0;
    for (i=0; i<256; ++i) {
        if (B(p+i) != (u8)s[i]) return 0;
        if (!s[i]) return 1;
    }
    return 0;
}
static u32 menu_find(u32 klass, u32 outer, const char *path) {
    return (u32)G4(0x8245BA00,klass,
        outer == 0xffffffff ? (u64)-1 : (u64)outer,(u32)path,0);
}
static u32 menu_offset(u32 klass, const char *path, u32 maximum) {
    u32 p=menu_find(klass,0xffffffff,path), n;
    if (!menu_ptr(p)) return 0xffffffff;
    n=W(p+0x64);
    return n <= maximum && !(n&3) ? n : 0xffffffff;
}
static u32 menu_tag(u32 scene, const char *tag) {
    u32 idx=W(scene+0x154), names=W(0x83101004), entry;
    if (!names || idx >= W(0x83101008) || W(scene+0x158)) return 0;
    entry=W(names+4*idx);
    return menu_ptr(entry) && menu_equal(entry+0x10,tag);
}

/* Caption uses the same original UILabelButton virtual as hardware validation.
 * FString is ANSI in this build (StaticFindObject and serialized strings too).
 */
static void menu_caption(u32 button, const char *caption) {
    u32 text[3], n=1;
    while (caption[n-1]) ++n;
    text[0]=(u32)caption; text[1]=n; text[2]=n;
    G2(0x828AEAF8,button,(u32)text);
}

static u32 menu_command(u32 root,u32 scene,u32 off,const char *path,
                        const char *expected,const char *replacement) {
    u32 action=menu_find(root,scene,path);
    u32 field,p,j=0,n=1;
    if(!menu_ptr(action)||off>0x1000||off&3)return 0;
    field=action+off;p=W(field);
    while(replacement[n-1])++n;
    if(!menu_ptr(p)||W(field+4)<n||W(field+8)>256||W(field+8)<n||
       (!menu_equal(p,expected)&&!menu_equal(p,replacement)))return 0;
    do { B(p+j)=(u8)replacement[j]; } while(replacement[j++]);
    W(field+4)=j;
    return 1;
}

/* HostOpen is authored for the mission-setup render target, which the shell's
 * 3D ribbon material displays. Passing zero opens it on the viewport and puts
 * its transformed labels in the wrong coordinate space. Select Mission is
 * redirected to the stock CoopHost scene because the retired parent sequence
 * is not active; Continue retains its original direct OpenScene action. */
static u32 menu_open_scene(u32 owner,const char *path,u32 rewrite_mode,u32 target) {
    u32 engine=W(0x8311F58C),view,interaction,sc,root,scene,output=0,i;
    /* Exact-build offset validated by reflection before exposing Host. */
    u32 cmd_off=0xe0;
    if(!menu_ptr(owner)||!menu_ptr(engine)||
       !menu_ptr(view=W(engine+0x304))||
       !menu_ptr(interaction=W(view+0x5c))||
       !menu_ptr(sc=W(interaction+0x54)))return 0;
    root=W(engine+0x34);
    for(i=0;i<64&&menu_ptr(root)&&W(root+0x3c);++i)root=W(root+0x3c);
    if(i==64||!menu_ptr(root))return 0;
    scene=menu_find(root,0xffffffff,path);
    if(!menu_ptr(scene)||W(W(sc)+0x13c)!=0x828B0078)return 0;
    if(rewrite_mode==1 && !menu_command(root,scene,cmd_off,
        "AO2UILabelButton_1.UIState_Focused_1.UIStateSequence_6.UIAction_ConsoleCommand_1",
        "causeevent HostOpen_SelectMissionSelected","AO2LAN MISSION"))return 0;
    G5(0x828B0078,sc,scene,owner,(u32)&output,target);
    return menu_ptr(output)?output:0;
}

u32 lan_open_host_choice(u32 owner) {
    u32 engine=W(0x8311F58C),root,target,i;
    if(!menu_ptr(engine))goto fail;
    root=W(engine+0x34);
    for(i=0;i<64&&menu_ptr(root)&&W(root+0x3c);++i)root=W(root+0x3c);
    if(i==64||!menu_ptr(root))goto fail;
    target=menu_find(root,0xffffffff,
        "00_shell.mission_setup_menu.mission_setup_menu_t_rtt");
    if(menu_ptr(target)&&menu_open_scene(owner,"ShellUIPackage.HostOpenScene",1,target))return 1;
fail: state.error=0x250;return 0;
}

u32 lan_open_mission_choice(u32 owner) {
    /* Keep the stock selector graph intact. Its native session dispatch now
     * supplies the completed checkpoint URL to the scoped LAN hook. */
    if(menu_open_scene(owner,"ShellUIPackage.CoopHostScene",0,0))return 1;
    if(!state.error)state.error=0x251;
    return 0;
}

void lan_menu_tick(void) {
    u32 engine=W(0x8311F58C), view, interaction, sc, scenes, count, scene=0;
    u32 root, parent, i, cmd_off, out_off, links_off, op_off, input_off;
    u32 buttons[2], actions[2], gates[2], events[2], edge[2], fields[2];
    u32 host_progress;
    const char *commands[2]; const char *old_commands[2];
    const char *captions[2];
    /* menu_ready: 1 success; 2 waiting for UI; 3 waiting for Campaign;
     * 4 class ancestry; 0x50..0x54 property reflection; 6 object lookup;
     * 0x100/0x110 + step = host/join row validation failure (see below).
     * menu_context retains the active scene being examined. */
    state.menu_ready=2;
    if (!menu_ptr(engine)) return;
    view=W(engine+0x304); if (!menu_ptr(view)) return;
    interaction=W(view+0x5c); if (!menu_ptr(interaction)) return;
    sc=W(interaction+0x54); if (!menu_ptr(sc)) return;
    scenes=W(sc+0xc0); count=W(sc+0xc4);
    if (!scenes || count>128 || count>W(sc+0xc8)) return;
    state.menu_ready=3;
    for (i=0;i<count;++i) {
        u32 s=W(scenes+4*i);
        if (menu_ptr(s) && menu_tag(s,"MissionSetupScene")) {
            if (scene) return;
            scene=s;
        }
    }
    if (!scene || !W(scene+0x164)) return;
    state.menu_context=scene;
    if (scene==state.menu_scene) { state.menu_ready=1; return; }
    state.menu_ready=4;
    root=W(engine+0x34);
    for (i=0;i<64 && menu_ptr(root);++i) {
        parent=W(root+0x3c); if (!parent) break; root=parent;
    }
    if (i==64 || !menu_ptr(root)) return;
    state.menu_ready=0x50;
    cmd_off=menu_offset(root,"Engine.UIAction_ConsoleCommand.Command",0x1000);
    if (cmd_off==0xffffffff) return;
    if(cmd_off!=0xe0)return;
    state.menu_ready=0x51;
    out_off=menu_offset(root,"Engine.SequenceOp.OutputLinks",0x1000);
    if (out_off==0xffffffff) return;
    state.menu_ready=0x52;
    links_off=menu_offset(root,"Engine.SequenceOp.SeqOpOutputLink.Links",0x100);
    if (links_off==0xffffffff) return;
    state.menu_ready=0x53;
    op_off=menu_offset(root,"Engine.SequenceOp.SeqOpOutputInputLink.LinkedOp",0x100);
    if (op_off==0xffffffff) return;
    state.menu_ready=0x54;
    input_off=menu_offset(root,"Engine.SequenceOp.SeqOpOutputInputLink.InputLinkIdx",0x100);
    if (input_off==0xffffffff) return;
    state.menu_ready=6;
    buttons[0]=menu_find(root,scene,"Public");
    buttons[1]=menu_find(root,scene,"Private");
    actions[0]=menu_find(root,scene,"Public.UIState_Focused_1.UIStateSequence_6.UIAction_ConsoleCommand_1");
    actions[1]=menu_find(root,scene,"Private.UIState_Focused_21.UIStateSequence_7.UIAction_ConsoleCommand_1");
    gates[0]=menu_find(root,scene,"Public.UIState_Focused_1.UIStateSequence_6.UIAction_CheckPermission_0");
    gates[1]=menu_find(root,scene,"Private.UIState_Focused_21.UIStateSequence_7.UIAction_CheckPermission_0");
    events[0]=menu_find(root,scene,"Public.UIState_Focused_1.UIStateSequence_6.UIEvent_OnClick_0");
    events[1]=menu_find(root,scene,"Private.UIState_Focused_21.UIStateSequence_7.ButtonClickHandler");
    host_progress=menu_find(root,scene,"Public.UIState_Focused_1.UIStateSequence_6.UIAction_GetUnlockedMapIndex_0");
    /* Host first runs the original checkpoint/map/difficulty screens. The
     * scoped Plasma_HostGame hook swaps in LAN only after they build a URL. */
    commands[0]="AO2LAN SETUP"; commands[1]="AO2LAN JOIN";
    old_commands[0]="causeevent MissionSetup_HostPrivate";
    old_commands[1]="causeevent MissionSetup_HostPublic";
    captions[0]="System Link: Host"; captions[1]="System Link: Join";
    /* Validate both rows completely before making either row actionable. */
    for (i=0;i<2;++i) {
        u32 output, links, field, length;
        state.menu_ready=0x100+i*0x10; /* object lookup */
        if (!menu_ptr(buttons[i]) || !menu_ptr(actions[i]) ||
            !menu_ptr(gates[i]) || !menu_ptr(events[i])) return;
        if (!i && !menu_ptr(host_progress)) return;
        state.menu_ready++; /* caption virtual */
        if (W(W(buttons[i])+0x250)!=0x828AEAF8) return;
        field=actions[i]+cmd_off; fields[i]=field;
        length=W(field+4);
        state.menu_ready++; /* command FString bounds */
        if (!menu_ptr(W(field)) || length<11 || length>128 ||
            length>W(field+8) || W(field+8)>256) return;
        state.menu_ready++; /* exact command */
        if (!menu_equal(W(field),old_commands[i]) &&
            !menu_equal(W(field),commands[i])) return;
        output=events[i]+out_off;
        state.menu_ready++; /* one output */
        if (W(output+4)!=1 || W(output+8)<1 || !menu_ptr(W(output))) return;
        links=W(output)+links_off;
        state.menu_ready++; /* one link */
        if (W(links+4)!=1 || W(links+8)<1 || !menu_ptr(W(links))) return;
        edge[i]=W(links)+op_off;
        state.menu_ready++; /* exact permission gate and input index */
        if (W(W(links)+input_off)!=0 ||
            (W(edge[i])!=gates[i] && W(edge[i])!=(i?actions[i]:host_progress))) return;
    }
    for (i=0;i<2;++i) {
        u32 j=0, p=W(fields[i]);
        do { B(p+j)=(u8)commands[i][j]; } while (commands[i][j++]);
        W(fields[i]+4)=j;
        /* Host retains GetUnlockedMapIndex -> CurrentFilter -> Splitscreen ->
         * command, preserving the stock profile-progress initialization. Join
         * has no equivalent setup and goes directly to the LAN browser. */
        W(edge[i])=i?actions[i]:host_progress;
        menu_caption(buttons[i],captions[i]);
    }
    state.menu_scene=scene;
    state.menu_ready=1;
}
