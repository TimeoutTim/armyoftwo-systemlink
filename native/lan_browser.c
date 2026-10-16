// SPDX-License-Identifier: GPL-3.0-only
/* Native original-resource browser port. All calls run on the UI/game thread.
 * Original shell has exactly three incoming online-action edges, all output0/
 * link0, validated by build_lan_scenes.py. Replace them before scene cloning.
 * Row/provider construction follows hardware-verified lan_browser_rows.py.
 */
#include "lan_runtime.h"
typedef struct Browser {
    u32 root, sc, interaction, scene, list, store, config, search, settings_class;
    u32 published, count, results, status;
    LanHostRow rows[8];
} Browser;
#define BR ((Browser *)(u32)state.browser_context)
static u32 bp(u32 p) { return p>=0x40000000 && p<0x80000000; }
/* After the texture swap the unused original overlay can be garbage-collected.
 * Reopening requires either the original live texture or our live replacement,
 * never both. Keep rejecting null and unrelated references. */
static u32 background_allowed(u32 current,u32 artwork,u32 overlay) {
    return bp(current)&&bp(artwork)&&
        (current==artwork||(bp(overlay)&&current==overlay));
}
static u32 eq(u32 p,const char *s) {
    u32 i; if(!p)return 0;
    for(i=0;i<256;i++) { if(B(p+i)!=(u8)s[i])return 0; if(!s[i])return 1; }
    return 0;
}
static u32 find(u32 outer,const char *s) {
    return (u32)G4(0x8245BA00,BR->root,outer==0xffffffff?(u64)-1:outer,(u32)s,0);
}
static u32 offset(const char *s) {
    u32 p=find(0xffffffff,s); if(!bp(p))return 0xffffffff;
    return W(p+0x64)<=0x1000 ? W(p+0x64) : 0xffffffff;
}
/* Bool offsets and masks come from this build's reflected UBoolProperty. */
static u32 bool_field(const char *path,u32 *off,u32 *mask) {
    u32 p=find(0xffffffff,path);
    if(!bp(p)||W(p)!=0x82064DB8)return 0;
    *off=W(p+0x64);*mask=W(p+0x80);
    return *off>=0x3c&&*off<0x1000&&!(*off&3)&&*mask&&!(*mask&(*mask-1));
}
static u32 input_name(const char *text) {
    u32 names=W(0x83101004),n=W(0x83101008),i,p;
    if(!bp(names)||n>100000)return 0;
    for(i=1;i<n;i++){p=W(names+i*4);if(bp(p)&&eq(p+16,text))return i;}
    return 0;
}
static u32 name(u32 o,const char *s) {
    u32 i=W(o+0x2c),n=W(0x83101004),p;
    if(!n || i>=W(0x83101008))return 0;
    p=W(n+4*i);return bp(p)&&eq(p+0x10,s);
}
static u32 arr(u32 p,u32 max) {
    return W(p+4)<=W(p+8)&&W(p+8)<=max&&(!W(p+4)||bp(W(p)));
}
static u32 failure(u32 n) { BR->status=n; state.error=n; return 0; }
static u32 focus(void) {
    u32 w=BR->list,v=W(w),p;
    if(W(v+0x144)!=0x828F8BA8||W(v+0x148)!=0x828DE2A8)return 0;
    p=(u32)G1(0x828ADE00,w);if(p>3)return 0;
    if(!G2(0x828F8BA8,w,p))return 0;
    G3(0x828DE2A8,w,BR->scene,p);
    return G3(0x828A4008,BR->scene,1,p)==w;
}
/* Only output0/link0 is used; every expected count and old pointer is checked. */
static u32 edge(u32 event,u32 out,u32 links,u32 op,u32 input,u32 old,u32 replacement) {
    u32 a=event+out,b;
    if(!arr(a,8)||W(a+4)!=1)return 0;
    b=W(a)+links;
    if(!arr(b,8)||W(b+4)!=1)return 0;
    if(W(W(b)+input)||(W(W(b)+op)!=old&&W(W(b)+op)!=replacement))return 0;
    return W(b)+op;
}

/* InputKeyAction retains a singular ActionToExecute pointer between the
 * event byte/padding and the plural action array. Validate all four reflected
 * fields, then key names and targets, before changing serialized bindings. */
static u32 key_name(u32 a,const char *s) {
    u32 idx=W(a),names=W(0x83101004),p;
    if(W(a+4)||idx>=W(0x83101008))return 0;
    p=W(names+4*idx);return bp(p)&&eq(p+0x10,s);
}
static u32 auto_links(u32 event,u32 out,u32 links,u32 op,u32 input,u32 old,u32 replacement) {
    u32 a=event+out,b;
    if(!arr(a,8)||W(a+4)!=1)return 0;
    b=W(a)+links;
    if(!arr(b,8)||W(b+4)>1)return 0;
    if(W(b+4)&&(!edge(event,out,links,op,input,old,replacement)))return 0;
    return b;
}

/* UILabel's native data-store interface is embedded at +330. Verified from
 * exec8267F110 and live HelpText label vtable8204BFA0 (+4=828A5630).
 * The setter copies the FString; our literal and stack header are borrowed. */
static u32 label_text(u32 label,const char *text) {
    u32 f[3],n=1;
    if(!bp(label)||W(label+0x330)!=0x8204BFA0)return 0;
    while(text[n-1])n++;
    f[0]=(u32)text;f[1]=n;f[2]=n;
    G3(0x828A5630,label+0x330,(u32)f,(u64)-1);
    return 1;
}
/* CreateWidget duplicates the archetype and its components and initializes
 * ownership. InsertChild adds the GC-visible child and applies native layout.
 * Keep these on the browser instance so reopening never duplicates templates. */
static u32 browser_footer(u32 scene) {
    u32 templates[2],widget,i;
    templates[0]=find(0xffffffff,"ShellUIHeaders.HelpTextUIScene.BG");
    templates[1]=find(0xffffffff,"ShellUIHeaders.HelpTextUIScene.lblHelpText");
    if(!bp(templates[0])||!bp(templates[1])||W(W(scene)+0x114)!=0x828DDE18)return 0;
    for(i=0;i<2;i++) {
        widget=(u32)G5(0x8290FCC8,scene,scene,W(templates[i]+0x34),templates[i],0);
        if(!bp(widget)||W(widget+0x28)!=scene)return 0;
        if((u32)G4(0x828DDE18,scene,widget,(u64)-1,1)==0xFFFFFFFF)return 0;
        if(i&&!label_text(widget,"<Strings:AO2Game.ShellHelpText.SelectBack>"))return 0;
    }
    return 1;
}

/* The stock artwork is intentionally larger than the viewport. The old
 * flat overlay used 0..1 viewport bounds; merely replacing its texture fits
 * the entire artwork into that box and exposes its outer margins. Reuse the
 * original waiting screen's authored framing. Copy scalar bounds only, not
 * widget/component ownership, docking links, or cached resolved positions. */
static u32 browser_background_bounds(u32 scene) {
    u32 source=find(0xffffffff,"ShellUIPackage.ClientWaitingForHostUIScene.UIImage_9");
    u32 target=find(scene,"UIImage_1");
    u32 pos=offset("Engine.UIScreenObject.Position");
    u32 value=offset("Engine.UIRoot.UIScreenValue_Bounds.Value");
    u32 scale=offset("Engine.UIRoot.UIScreenValue_Bounds.ScaleType");
    u32 invalid=offset("Engine.UIRoot.UIScreenValue_Bounds.bInvalidated");
    u32 aspect=offset("Engine.UIRoot.UIScreenValue_Bounds.AspectRatioMode");
    u32 i,a,b;
    if(!bp(source)||!bp(target)||pos==0xffffffff||value!=0||scale!=16||
       invalid!=20||aspect!=24)return 0;
    a=source+pos;b=target+pos;
    /* shell export15484 and Engine UIImage/UIScreenObject defaults. */
    if(W(a)!=0xbe933333||W(a+4)!=0xbd088889||W(a+8)!=0x45000000||
       W(a+12)!=0x447fffff||B(a+16)!=5||B(a+17)!=5||
       B(a+18)!=3||B(a+19)!=3||B(a+24)!=0)return 0;
    for(i=0;i<4;i++) {
        W(b+i*4)=W(a+i*4);B(b+scale+i)=B(a+scale+i);
        B(b+invalid+i)=1;
    }
    B(b+aspect)=B(a+aspect);
    return 1;
}

u32 lan_open_browser(void) {
    u32 engine=W(0x8311F58C),view,ui,sc,root,i,p,scene,owner=0;
    u32 cmd,out,links,op,input,commands[3],fields[3],events[3],old[3],edges[3];
    u32 widgets[3],components[3],skin,output=0;
    u32 render_properties[2],render_offsets[2],render_masks[2];
    u32 input_states[2],input_old[2],input_edges[4],state_inputs;
    u32 back_key,new_inputs[2],mode_off,presenter,header_off,header_mask;
    u32 help_scene=0,background,background_field,background_art,flat_overlay;
    const char *texts[3];
    if(state.browser_context) {
        if(!bp(state.browser_context)) { state.error=0x236;return 0; }
        if(BR->status!=2)return 0; /* Only a verified closed browser may reopen. */
    } else {
        p=alloc(sizeof(Browser));if(!p)return 0;state.browser_context=p;
    }
    BR->status=0;
    if(!bp(engine)||!bp(view=W(engine+0x304))||!bp(ui=W(view+0x5c))||
       !bp(sc=W(ui+0x54)))return failure(0x210);
    BR->sc=sc;BR->interaction=ui;root=W(engine+0x34);
    for(i=0;i<64&&bp(root)&&W(root+0x3c);i++)root=W(root+0x3c);
    if(i==64||!bp(root))return failure(0x211);BR->root=root;
    if(!arr(sc+0xc0,128))return failure(0x212);
    for(i=0;i<W(sc+0xc4);i++) {
        u32 s=W(W(sc+0xc0)+4*i),idx=W(s+0x154),names=W(0x83101004);
        if(idx>=W(0x83101008))continue;
        p=W(names+idx*4);if(!bp(p))continue;
        if(eq(p+0x10,"Online_Lobby"))return failure(0x213);
        if(eq(p+0x10,"MissionSetupScene"))owner=W(s+0x164);
        if(eq(p+0x10,"HelpTextUIScene"))help_scene=s;
    }
    if(!owner||W(W(sc)+0x13c)!=0x828B0078)return failure(0x214);
    scene=find(0xffffffff,"ShellUIPackage.Online_Lobby");
    skin=find(0xffffffff,"AO2UISkin.ShellUISkin");
    if(!bp(scene)||!bp(skin))return failure(0x215);
    /* The authored browser uses a 2x2 flat overlay. Reuse the original
     * waiting-lobby artwork through this scene's existing fullscreen image,
     * with normal UObject ownership and cloning (no new texture content). */
    background=find(scene,"UIImage_1.UIComp_DrawImage_0.UITexture_0");
    background_field=offset("Engine.UITexture.ImageTexture");
    background_art=find(0xffffffff,"00_shell.Main_Menu.mainmenu_bg_d");
    flat_overlay=find(0xffffffff,"00_shell.lobby_overlay.lobby_overlay_bg_d");
    if(!bp(background)||background_field==0xffffffff||
       !background_allowed(W(background+background_field),background_art,flat_overlay))
        return failure(0x245);
    cmd=offset("Engine.UIAction_ConsoleCommand.Command");
    out=offset("Engine.SequenceOp.OutputLinks");
    links=offset("Engine.SequenceOp.SeqOpOutputLink.Links");
    op=offset("Engine.SequenceOp.SeqOpOutputInputLink.LinkedOp");
    input=offset("Engine.SequenceOp.SeqOpOutputInputLink.InputLinkIdx");
    if((cmd|out|links|op|input)==0xffffffff)return failure(0x216);
    commands[0]=find(scene,"UIList_0.UIState_Active_0.UIStateSequence_3.UIAction_ConsoleCommand_0");
    commands[1]=find(scene,"UIList_0.UIState_Focused_0.UIStateSequence_2.UIAction_ConsoleCommand_0");
    commands[2]=find(scene,"UIComp_Event_97.UISequence_97.UIAction_ConsoleCommand_0");
    events[0]=find(scene,"UIComp_Event_97.UISequence_97.SceneInitializedEvent");
    events[1]=find(scene,"UIList_0.UIState_Focused_0.UIStateSequence_2.UIEvent_OnEnterState_0");
    events[2]=find(scene,"UIList_0.UIState_Focused_0.UIStateSequence_2.UIEvent_SubmitListData_0");
    old[0]=find(scene,"UIComp_Event_97.UISequence_97.UIAction_FindOnlineGames_0");
    old[1]=find(scene,"UIList_0.UIState_Focused_0.UIStateSequence_2.UIAction_FindOnlineGames_0");
    old[2]=find(scene,"UIList_0.UIState_Focused_0.UIStateSequence_2.UIAction_JoinOnlineGame_0");
    texts[0]="AO2LAN REFRESH";texts[1]="AO2LAN SELECT";texts[2]="AO2LAN CANCEL";
    for(i=0;i<3;i++) {
        if(!bp(commands[i])||!bp(events[i])||!bp(old[i]))return failure(0x217);
        fields[i]=commands[i]+cmd;
        if(!arr(fields[i],128)||W(fields[i]+4)<13||
           (!eq(W(fields[i]),i==2?"causeevent Lobby_BackSelected":"causeevent Lobby_JoinedGame")&&
            !eq(W(fields[i]),texts[i])))return failure(0x218);
        edges[i]=i<2 ? auto_links(events[i],out,links,op,input,old[i],commands[0]) :
            edge(events[i],out,links,op,input,old[i],commands[1]);
        if(!edges[i])return failure(0x219);
    }
    state_inputs=offset("Engine.UIState.StateInputActions");
    if(state_inputs==0xffffffff || offset("Engine.UIRoot.InputKeyAction.InputKeyName")!=0 ||
       offset("Engine.UIRoot.InputKeyAction.InputKeyState")!=8 ||
       offset("Engine.UIRoot.InputKeyAction.ActionToExecute")!=12 ||
       offset("Engine.UIRoot.InputKeyAction.ActionsToExecute")!=16)return failure(0x236);
    input_states[0]=find(scene,"UIList_0.UIState_Active_0");
    input_states[1]=find(scene,"UIList_0.UIState_Enabled_0");
    input_old[0]=find(scene,"UIList_0.UIState_Active_0.UIStateSequence_3.UIAction_JoinOnlineGame_0");
    input_old[1]=find(scene,"UIList_0.UIState_Enabled_0.UIStateSequence_0.UIAction_JoinOnlineGame_0");
    for(i=0;i<2;i++) {
        u32 a,j;
        if(!bp(input_states[i])||!bp(input_old[i]))return failure(0x237);
        a=input_states[i]+state_inputs;
        if(!arr(a,8)||(W(a+4)!=2&&W(a+4)!=3))return failure(0x238);
        for(j=0;j<2;j++) {
            u32 item=W(a)+j*28,actions=item+16;
            if(!key_name(item,j?"XboxTypeS_A":"XboxTypeS_Start")||B(item+8)||W(item+12)||
               !arr(actions,8)||W(actions+4)!=1)return failure(0x239);
            input_edges[i*2+j]=W(actions);
            if(W(W(actions))!=input_old[i]&&W(W(actions))!=commands[1])return failure(0x23a);
        }
    }
    /* The shipped list binds A/Start but has no B binding. Use its native
     * InputKeyAction layout to route B to the existing Back console action. */
    back_key=input_name("XboxTypeS_B");if(!back_key)return failure(0x240);
    for(i=0;i<2;i++) {
        u32 a=input_states[i]+state_inputs,j,item,actions;
        if(W(a+4)==3) {
            item=W(a)+56;actions=item+16;
            if(!key_name(item,"XboxTypeS_B")||B(item+8)||W(item+12)||
               !arr(actions,8)||W(actions+4)!=1||W(W(actions))!=commands[2])return failure(0x241);
            new_inputs[i]=W(a);
        } else {
            /* Both TArrays own their Data independently. An interior pointer
             * (formerly p+84) is an invalid free when shell GC destroys this
             * template. Transfer the two existing action arrays unchanged;
             * allocate the new B action array separately. */
            p=alloc(84);if(!p)return failure(0x242);
            actions=alloc(4);if(!actions){release(p);return failure(0x242);}
            for(j=0;j<56;j++)B(p+j)=B(W(a)+j);
            W(actions)=commands[2];
            W(p+56)=back_key;W(p+72)=actions;W(p+76)=1;W(p+80)=1;
            new_inputs[i]=p;
        }
    }
    mode_off=offset("Engine.UIScene.SceneRenderMode");
    presenter=find(scene,"UIList_0.UIComp_ListPresenter_0");
    if(mode_off==0xffffffff||B(scene+mode_off)>1||!bp(presenter)||
       !bool_field("Engine.UIComp_ListPresenter.bDisplayColumnHeaders",&header_off,&header_mask))return failure(0x243);
    if(!bp(help_scene))return failure(0x244);
    widgets[0]=find(scene,"UIList_0.UIScrollbar_0.IncrementButton");
    widgets[1]=find(scene,"UIList_0.UIScrollbar_0.DecrementButton");
    widgets[2]=find(scene,"UIList_0.UIScrollbar_0.Marker");
    for(i=0;i<3;i++) {
        if(!bp(widgets[i])||(W(widgets[i]+0x270)!=47&&W(widgets[i]+0x270)!=15))return failure(0x21a);
        components[i]=find(widgets[i],"BackgroundImageTemplate");
        if(!bp(components[i])||W(components[i]+0x28)!=widgets[i])return failure(0x21b);
    }
    /* Original Campaign scene enables both flags; the browser otherwise
     * occludes the still-active background/help scenes. UBoolProperty ctor
     * 82479898 installs 82064DB8; serializer 824564E8 reads BitMask at +0x80 and
     * sets/clears that mask in a 32-bit destination word (82456548..82456564).
     * Validate both reflected fields completely before changing either. */
    render_properties[0]=find(0xffffffff,"Engine.UIScene.bRenderParentScenes");
    render_properties[1]=find(0xffffffff,"Engine.UIScene.bAlwaysRenderScene");
    for(i=0;i<2;i++) {
        p=render_properties[i];
        if(!bp(p)||W(p)!=0x82064DB8)return failure(0x23b);
        render_offsets[i]=W(p+0x64);render_masks[i]=W(p+0x80);
        if(render_offsets[i]<0x3c||render_offsets[i]>0x3fc||
           (render_offsets[i]&3)||!render_masks[i]||
           (render_masks[i]&(render_masks[i]-1)))return failure(0x23c);
    }
    if(render_offsets[0]==render_offsets[1]&&render_masks[0]==render_masks[1])return failure(0x23c);
    /* Template changes become part of the original scene's cloned instances. */
    if(!browser_background_bounds(scene))return failure(0x246);
    W(background+background_field)=background_art;
    for(i=0;i<3;i++) {
        u32 j=0;
        do { B(W(fields[i])+j)=(u8)texts[i][j]; } while(texts[i][j++]);
        W(fields[i]+4)=j;
        /* Initialization/focus must not start a refresh loop. Native code
         * initiates discovery exactly once, without synthetic UI events. */
        if(i<2)W(edges[i]+4)=0;else W(edges[i])=commands[1];
        G2(0x828D7668,components[i]+0xc8,skin);
        if(!W(components[i]+0xe4))return failure(0x21c);
        W(widgets[i]+0x270)=15;
    }
    for(i=0;i<4;i++)W(input_edges[i])=commands[1];
    for(i=0;i<2;i++) {
        u32 a=input_states[i]+state_inputs,old_data=W(a);
        W(a)=new_inputs[i];W(a+4)=3;W(a+8)=3;
        /* Only release the detached outer storage, not its transferred nested
         * arrays. Reopening an already modified template keeps the same data. */
        if(old_data!=new_inputs[i])release(old_data);
    }
    W(scene+render_offsets[0])|=render_masks[0];
    W(scene+render_offsets[1])&=~render_masks[1];
    /* Match the campaign selector's render mode instead of the browser's
     * inherited default; do not render this browser over later scenes. */
    B(scene+mode_off)=0;
    W(presenter+header_off)&=~header_mask;
    /* Campaign's active HelpTextUIScene already owns A Select/B Back. Keep it
     * rendered through the original parent-scene path; the packaged scripted
     * help manager is not the owner of this live scene. */
    G5(0x828B0078,sc,scene,owner,(u32)&output,0);
    if(!bp(output))return failure(0x21d);BR->scene=output;
    if(!browser_footer(output)||!label_text(find(output,"UILabel_1"),"System Link"))
        return failure(0x246);
    BR->list=find(output,"UIList_0");
    if(!bp(BR->list)||W(W(BR->list)+0x284)!=0x828B7868)return failure(0x21e);
    if(!focus())return failure(0x21f);
    BR->status=1;return 1;
}

/* Frozen discovery snapshot. Names are packed 16-byte original profile bytes;
 * native providers own copied strings, and list element values equal indexes. */
u32 lan_browser_publish(const char *names,u32 count) {
    u32 dc,stores,n,i,store=0,cfg,search,klass,results,indices[8],header[3],transient;
    if(!state.browser_context||BR->status!=1||BR->published||count>8||!names)return 0;
    dc=W(BR->interaction+0x7c);if(!bp(dc)||!arr(dc+0x48,128))return failure(0x220);
    stores=W(dc+0x48);n=W(dc+0x4c);
    for(i=0;i<n;i++) { u32 s=W(stores+4*i);if(bp(s)&&name(s,"AO2OnlineSearch_DataStore")){if(store)return failure(0x221);store=s;} }
    if(!store||W(store)!=0x82043660||!arr(store+0x94,32)||W(store+0xa0)>=W(store+0x98))return failure(0x222);
    cfg=W(store+0x94)+W(store+0xa0)*0x28;search=W(cfg+0x1c);klass=W(cfg+4);
    if(!bp(search)||W(search)!=0x82043320||!bp(klass)||!name(klass,"AO2OnlineSettings"))return failure(0x223);
    if(!arr(cfg+0x10,4096)||W(cfg+0x14)||
       !arr(search+0x84,4096)||W(search+0x88)||W(BR->list+0x4bc))return failure(0x224);
    if(W(search+0x8c) && W(search+0x8c)!=8)return failure(0x224);
    /* Search owns this array. Its old address may have died with shell GC. */
    BR->results=W(search+0x84);
    if(offset("Engine.OnlineGameSettings.OwningPlayerName")!=0x8c||
       offset("Engine.OnlineGameSettings.NumOpenPrivateConnections")!=0x78||
       offset("Engine.OnlineGameSettings.NumOpenPublicConnections")!=0x74)return failure(0x225);
    BR->store=store;BR->config=cfg;BR->search=search;BR->settings_class=klass;
    if(!count){BR->count=0;BR->published=1;return 1;}
    results=BR->results;
    if(!results){results=alloc(64);if(!results)return failure(0x226);BR->results=results;}
    transient=(u32)G0(0x824552B0);
    for(i=0;i<count;i++) {
        u32 obj,p,j,len=0;u64 flags=((u64)W(0x830E1964)<<32)|W(0x830E1968);
        while(len<16&&names[i*16+len]){u8 c=(u8)names[i*16+len];if(c<32||c==127)return failure(0x227);len++;}
        if(!len)return failure(0x227);
        obj=(u32)G8(0x8246F4C0,klass,transient,0,0,0,flags,0,0);
        if(!bp(obj)||W(obj+0x34)!=klass||W(obj+0x8c)||W(obj+0x90)||W(obj+0x94))return failure(0x228);
        p=alloc(len+1);if(!p)return failure(0x229);
        for(j=0;j<len;j++)B(p+j)=(u8)names[i*16+j];
        W(obj+0x8c)=p;W(obj+0x90)=len+1;W(obj+0x94)=len+1;
        W(obj+0x74)=1;W(obj+0x78)=0;W(results+8*i)=obj;indices[i]=i;
    }
    W(search+0x84)=results;W(search+0x88)=count;W(search+0x8c)=8;
    BR->published=1;BR->count=count;BR->results=results;
    G1(0x828DCAC8,store);
    if(!arr(cfg+0x10,4096)||W(cfg+0x14)!=count)return failure(0x22a);
    for(i=0;i<count;i++) {u32 p=W(W(cfg+0x10)+i*4);if(!bp(p)||W(p+0x6c)!=W(results+8*i))return failure(0x22b);}
    header[0]=(u32)indices;header[1]=count;header[2]=count;
    if(G4(0x828B7868,BR->list,(u32)header,(u64)-1,0)!=count)return failure(0x22c);
    if(!focus())return failure(0x22d);
    return 1;
}

u32 lan_browser_selected(void) {
    u32 i,widget;
    if(!state.browser_context||BR->status!=1||!BR->published)return 0xffffffff;
    widget=BR->list;i=W(widget+0x358);
    /* UE3 grows allocation capacity beyond the live element count (one row
     * was observed with Max=18). Bound storage separately from LAN rows. */
    if(BR->count>8||!arr(widget+0x4b8,4096)||W(widget+0x4bc)!=BR->count||
       i>=W(widget+0x4bc))return 0xffffffff;
    i=W(W(widget+0x4b8)+i*4);
    if(i>=BR->count||!arr(BR->config+0x10,4096)||W(BR->config+0x14)!=BR->count)return 0xffffffff;
    if(W(W(W(BR->config+0x10)+4*i)+0x6c)!=W(BR->results+8*i))return 0xffffffff;
    return i;
}

/* Native UIList owns its element/cell storage. Remove through ClearElements,
 * then rebuild providers and insert rows using the existing native APIs. */
u32 lan_browser_host(u32 index) {
    return state.browser_context&&BR->status==1&&BR->published&&index<BR->count
        ? (u32)&BR->rows[index] : 0;
}
u32 lan_browser_update(void) {
    u32 count=lan_discovery_count(),i,j,changed,selected=0xFFFFFFFF,target=0;
    u32 selected_ip=0,selected_nonce=0;
    char names[8*16];
    if(!state.browser_context||BR->status!=1||count>8)return 0;
    changed=!BR->published||count!=BR->count;
    for(i=0;i<count;i++) {
        LanHostRow *row=(LanHostRow*)lan_discovery_row(i);
        if(!row)return 0;
        if(i>=BR->count||row->ip!=BR->rows[i].ip||row->nonce!=BR->rows[i].nonce)changed=1;
        for(j=0;j<16;j++) {
            names[i*16+j]=row->name[j];
            if(i<BR->count&&row->name[j]!=BR->rows[i].name[j])changed=1;
        }
    }
    if(!changed)return 1; /* Heartbeats never rebuild or move focus. */
    if(BR->published) {
        selected=lan_browser_selected();
        if(selected<BR->count) {
            selected_ip=BR->rows[selected].ip;selected_nonce=BR->rows[selected].nonce;
        }
        G1(0x828C3860,BR->list);
        if(W(BR->list+0x4BC))return failure(0x247);
        if(!bp(BR->search)||W(BR->search+0x88)!=BR->count)return failure(0x248);
        W(BR->search+0x88)=0;
        G1(0x828DCAC8,BR->store);
        if(W(BR->config+0x14))return failure(0x249);
        BR->published=0;BR->count=0;
    }
    if(!lan_browser_publish(names,count))return 0;
    for(i=0;i<count;i++) {
        LanHostRow *row=(LanHostRow*)lan_discovery_row(i);
        for(j=0;j<sizeof(LanHostRow);j++)((u8*)&BR->rows[i])[j]=((u8*)row)[j];
        if(row->ip==selected_ip&&row->nonce==selected_nonce)target=i;
    }
    /* execSetIndex8266BD30 uses UIList v+260(this,index,clamp,skipNotify).
     * Native selection updates both highlight and scrolling; never poke +358. */
    if(count)G4(W(W(BR->list)+0x260),BR->list,target,1,1);
    return 1;
}

/* Close/reopen uses a fresh UIList instead of mutating its private cell arrays.
 * Native CloseScene ABI: wrapper8265E518 -> sceneclient v+108=828B0210.
 * On the frontend it dispatches UI-only v+170, bypassing network close handling.
 */
static u32 active_browser(void) {
    u32 i,a=BR->sc+0xc0;
    if(!arr(a,128))return 2;
    for(i=0;i<W(a+4);i++)if(W(W(a)+4*i)==BR->scene)return 1;
    return 0;
}
u32 lan_browser_cancel(void) {
    u32 settings,active;
    if(!state.browser_context||BR->status!=1)return 0;
    settings=W(0x831228D0);
    if(state.gm||!bp(settings)||W(settings+8)||W(settings+0x30))return failure(0x230);
    active=active_browser();if(active>1)return failure(0x231);
    if(active) {
        if(W(W(BR->sc)+0x108)!=0x828B0210)return failure(0x232);
        if(!G2(0x828B0210,BR->sc,BR->scene)||active_browser())return failure(0x233);
    }
    if(BR->published) {
        if(!bp(BR->search)||!bp(BR->store)||!arr(BR->search+0x84,8)||
           W(BR->search+0x88)!=BR->count)return failure(0x234);
        if(BR->count && (W(BR->search+0x84)!=BR->results||W(BR->search+0x8c)!=8))return failure(0x234);
        W(BR->search+0x88)=0;
        /* BuildSearchResults starts with native TArray.Empty then recreates
         * one provider per result; zero results therefore clears providers. */
        G1(0x828DCAC8,BR->store);
        if(W(BR->config+0x14))return failure(0x235);
    }
    BR->published=0;BR->count=0;BR->scene=0;BR->list=0;BR->status=2;
    return 1;
}
u32 lan_browser_refresh(void) {
    if(!lan_browser_cancel())return 0;
    return lan_open_browser();
}
