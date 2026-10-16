// SPDX-License-Identifier: GPL-3.0-only
/* Decentralized two-player admission. Engine calls run only from session_tick.
 * Packet retries repeat observations, never constructors or native events.
 * Partial construction failure remains terminal; completed sessions clean up
 * only after native removal and return to the shell.
 */
#include "lan_runtime.h"
#define JOIN_MAGIC 0x41324A43
#define JOIN_VERSION 1
#define JOIN_TIMEOUT 45000
#define PH_REQUEST 1
#define PH_PREPARED 2
#define PH_LINK 3
#define PH_ADMITTED 4
#define PH_ROSTER 5
#define PH_DONE 6

typedef struct JoinPacket {
    u32 magic, version, size, host_nonce, client_nonce, phase, role;
    u32 content_flags;
    u8 name[16], xuid[8];
    char map[128];
} JoinPacket;
typedef struct JoinContext {
    u32 host, ip, host_nonce, client_nonce, phase, remote_phase;
    u32 created, sent, failed, remote, conn, endpoint, registered;
    u32 roster_event, join_event, clock_started, table;
    u32 content_flags, remote_flags;
    u8 remote_name[16], remote_xuid[8];
    char map[128];
    JoinPacket inbox;
    u32 inbox_pending, start_armed, teardown;
} JoinContext;
/* Builder cleanup islands read the final word at +1A4 directly. */
typedef char assert_join_context_layout[(sizeof(JoinContext)==0x1A8)?1:-1];
static void jzero(void *p,u32 n) { volatile u8 *d=(volatile u8*)p;while(n--)*d++=0; }
static void jcopy(void *p,const void *s,u32 n) { volatile u8 *d=(volatile u8*)p;const u8 *a=(const u8*)s;while(n--)*d++=*a++; }
static u32 jequal(const void *p,const void *q,u32 n) { const u8*a=(const u8*)p,*b=(const u8*)q;while(n--)if(*a++!=*b++)return 0;return 1; }
static u32 jptr(u32 p) { return p>=0x40000000 && p<0x80000000; }
static u32 jclock(void) { return (u32)G1(W(0x821A3198+8),0x830DF564); }
static JoinContext *jc(void) { return (JoinContext*)state.session_context; }
static void jfail(JoinContext *c,u32 error) { c->failed=1;state.error=error;state.status=0xFF; }
static u32 jname(const u8 *p) { u32 i;for(i=0;i<16;i++){if(!p[i])return i!=0;if(p[i]<32 || p[i]==127)return 0;}return 0; }
static u32 jmap(const char *p) { u32 i;for(i=0;i<128;i++){if(!p[i])return i!=0;if((u8)p[i]<32 || (u8)p[i]>126)return 0;}return 0; }
static JoinContext *jnew(void) {
    JoinContext*c=(JoinContext*)alloc(sizeof(JoinContext));
    if(!jptr((u32)c)){state.error=0x401;state.status=0xFF;return 0;}
    jzero(c,sizeof(*c));c->created=jclock();state.session_context=(u32)c;return c;
}
static u32 jflags(JoinContext*c) {
    u32 p=W(0x83122970);
    if(!p){G1(0x82943948,0);p=W(0x83122970);}
    if(!p){jfail(c,0x402);return 0;}c->content_flags=W(p);return 1;
}
static void jsend(JoinContext*c,u32 now) {
    JoinPacket p;jzero(&p,sizeof(p));p.magic=JOIN_MAGIC;p.version=JOIN_VERSION;
    p.size=sizeof(p);p.host_nonce=c->host_nonce;p.client_nonce=c->client_nonce;
    p.phase=c->phase;p.role=c->host;p.content_flags=c->content_flags;
    jcopy(p.name,(const void*)state.name,16);jcopy(p.xuid,(const void*)state.xuid,8);
    jcopy(p.map,c->map,128);lan_discovery_send(c->ip,&p,sizeof(p));c->sent=now;
}
/* Called by discovery's game-thread poll, with a complete datagram. */
void lan_session_receive(u32 ip,const void *data,u32 size) {
    const JoinPacket*p=(const JoinPacket*)data;JoinContext*c=jc();
    if(size!=sizeof(*p)||p->magic!=JOIN_MAGIC||p->version!=JOIN_VERSION||
       p->size!=size||!p->host_nonce||!p->client_nonce||p->role>1||
       p->phase<PH_REQUEST||p->phase>PH_DONE||!jname(p->name)||
       !(W((u32)p->xuid)|W((u32)p->xuid+4)))return;
    if(!c){
        if(state.status!=2||state.lan_id!=0x103||p->role||p->phase!=PH_REQUEST||
           p->host_nonce!=lan_discovery_nonce())return;
        c=jnew();if(!c)return;c->host=1;c->ip=ip;c->host_nonce=p->host_nonce;
        c->client_nonce=p->client_nonce;
        jcopy(c->remote_name,p->name,16);jcopy(c->remote_xuid,p->xuid,8);
        c->remote_flags=p->content_flags;
        jcopy(c->map,(const void*)(W(0x831228E4)+0x6F0),128);
        if(!jmap(c->map)){jfail(c,0x403);return;}
    }
    if(c->failed||c->teardown||c->ip!=ip||c->host_nonce!=p->host_nonce||
       c->client_nonce!=p->client_nonce||p->role==c->host)return;
    if(!jequal(c->remote_name,p->name,16)||!jequal(c->remote_xuid,p->xuid,8))return;
    /* The selected host's accepted map is immutable for this join. */
    if(!c->host){
        if(!jmap(p->map))return;
        if(!c->remote_phase){jcopy(c->map,p->map,128);c->remote_flags=p->content_flags;}
        else if(!jequal(c->map,p->map,128)||c->remote_flags!=p->content_flags)return;
    }else if(c->remote_flags!=p->content_flags)return;
    if(p->phase<c->remote_phase || (c->inbox_pending && p->phase<c->inbox.phase))return;
    jcopy(&c->inbox,p,sizeof(*p));c->inbox_pending=1;
}
u32 lan_session_join(const LanHostRow *row) {
    JoinContext*c;if(jc()||state.gm||!row||!row->ip||!row->nonce||!jname(row->name))return 0;
    if(!lan_prepare(0))return 0;
    c=jnew();if(!c)return 0;c->ip=row->ip;c->host_nonce=row->nonce;
    c->client_nonce=lan_discovery_nonce();if(!c->client_nonce)c->client_nonce=1;
    jcopy(c->remote_name,row->name,16);jcopy(c->remote_xuid,row->xuid,8);
    if(!jflags(c))return 0;c->phase=PH_REQUEST;jsend(c,jclock());return 1;
}
static void jip(char*out,u32 ip) {
    u32 i,v,n;char tmp[3];
    for(i=0;i<4;i++){v=(ip>>(24-i*8))&255;n=0;do{tmp[n++]=(char)('0'+v%10);v/=10;}while(v);
        while(n)*out++=tmp[--n];if(i!=3)*out++='.';}*out=0;
}

/* Backend-free concrete EndGame virtuals. The stock +A4 path calls the absent
 * EA backend before enqueueing platform End; its completion reaches stock +A8,
 * which calls the backend again. Preserve the peer control and platform task,
 * but replace only those two backend-dependent concrete operations. */
static void jend(u32 g,u32 pregame) {
    JoinContext*c=jc();u32 packet[0x120], delegate[6], i;
    if(!c||c->teardown||g!=state.game || W(g)!=c->table || (!pregame&&!B(g+0xC0)))return;
    B(g+0xC0)=0;
    G2(0x8223BA80,g+8,0);
    for(i=0;i<0x120;i++)packet[i]=0;
    G2(0x8224BC08,(u32)packet,0x13);
    G5(W(W(g+8)+0xA0),g+8,(u32)packet,1,1,1);
    delegate[0]=0x82274FF0; delegate[1]=g; delegate[2]=0x82245128;
    delegate[3]=0; delegate[4]=0; delegate[5]=0;
    G2(0x822558C0,g+0x238,(u32)delegate);
    packet[0]=0x821A41DC;
    G1(0x82235EA0,(u32)packet+8);
}
static void jend_start(u32 g) { jend(g,0); }
static void jend_finish(u32 g) {
    JoinContext*c=jc();
    if(!c||g!=state.game||W(g)!=c->table)return;
    W(g+0x210)=1;W(g+0x214)=0;W(g+0x218)=0;c->teardown=1;
}
/* Install the same scoped lifecycle table for an empty host lobby too. */
static u32 jtable(JoinContext*c) {
    u32 g=state.game;
    /* Private concrete table suppresses only backend attribute publication. */
    if(W(g)!=0x821A3ED8){jfail(c,0x418);return 0;}
    c->table=alloc(0xD0);if(!jptr(c->table)){jfail(c,0x419);return 0;}
    jcopy((void*)c->table,(const void*)0x821A3ED8,0xD0);
    W(c->table+0x98)=0x822635A0;
    W(c->table+0xA4)=(u32)jend_start;W(c->table+0xA8)=(u32)jend_finish;
    W(g)=c->table;
    return 1;
}

/* The stock HostWaiting Back confirmation closes its UI even when the shell
 * parent no longer owns a backend session. Use the existing reliable End
 * control (0x13) and platform completion for this unstarted LAN lobby too. */
u32 lan_session_leave_lobby(void) {
    JoinContext*c=jc();u32 n=W(0x831228E4),g=state.game;
    if(!g||!state.bridge||!n||B(g+0xC0)||B(n+0x69C))return 0;
    if(!c) {
        if(state.status!=2||state.lan_id!=0x103)return 0;
        c=jnew();if(!c)return 0;c->host=1;c->phase=PH_DONE;
        if(!jtable(c))return 0;
        B(g+0x238+0x104)=1; /* Existing direct-LAN platform operation path. */
    }
    if(c->failed||c->teardown||c->phase!=PH_DONE||W(g)!=c->table)return 0;
    B(state.bridge+0x11)=0; /* Same receive-disable as native82942368. */
    jend(g,1);
    c->teardown=1; /* Drain queued End completion before manager removal. */
    return 1;
}
static u32 jpeer(JoinContext*c) {
    u32 g=state.game,remote,aux,scratch,conn,slot,i,k,tr,sock,seen[4],count=0;
    char ip[16];u32 offsets[4]={0x10,0x14,0x1C,0x20};
    if(!g||W(g+0x188)!=1||W(g+0x1B4)!=1||W(g+0x16C)||
       W(state.peer+0x2C)!=6){jfail(c,0x410);return 0;}
    scratch=alloc(1024);if(!jptr(scratch)){jfail(c,0x411);return 0;}jzero((void*)scratch,1024);
    jip(ip,c->ip);c->endpoint=(u32)G5(0x822124E0,(u32)ip,6001,(u32)ip,6001,0);
    if(!c->endpoint||W(c->endpoint)!=0x821A2CBC){jfail(c,0x412);return 0;}
    G2(0x82212C68,c->endpoint,c->endpoint+0x14);
    if(W(c->endpoint+0xC)!=c->ip){jfail(c,0x413);return 0;}
    remote=(u32)G2(0x822563B8,g+0x180,0);c->remote=remote;
    if(!jptr(remote)||W(remote)!=0x821A40F0){jfail(c,0x414);return 0;}
    W(g+0x228)=0;slot=W(g+0x1D8);
    if(!c->host){W(g+0x174)=remote;W(state.peer+0x14)=1;}
    W(slot)=c->host?state.peer:remote;W(slot+4)=c->host?remote:state.peer;
    W(g+0xC4)=1;W(g+0xC8)=1;W(g+0xCC)=c->host?257:258;W(g+0xD0)=0;W(g+0xD4)=0x103;W(g+0xA8)=2;
    W(scratch+0xC)=c->host?258:257;W(scratch+0x10)=0;W(scratch+0x14)=c->host?0x101:0x103;
    jcopy((void*)(scratch+0x18),c->remote_name,16);W(scratch+0x118)=c->endpoint;
    jcopy((void*)(scratch+0x120),c->remote_xuid,8);
    G5(0x82247140,remote,g+8,c->host?1:0,0xFFFFFFFF,scratch);
    aux=(u32)G2(0x822566A8,g+0xC4C,0);if(!aux){jfail(c,0x415);return 0;}
    G4(0x82248748,aux,g,remote,scratch+0x98);W(remote+0xC)=aux;
    if(c->host){G2(0x82247408,remote,1);conn=W(remote+0x28);}
    else{G2(0x8223C318,g+8,c->endpoint);conn=W(g+0x16C);W(remote+0x28)=conn;}
    c->conn=conn;if(!conn||W(conn)!=0x821A415C){jfail(c,0x416);return 0;}
    jzero((void*)(conn+0x2C),0x48);W(scratch+800)=1;
    for(i=0;i<4;i++){
        tr=W(conn+offsets[i]);if(!tr)continue;sock=W(tr+0x80);if(!sock)continue;
        for(k=0;k<count;k++)if(seen[k]==sock)break;if(k<count)continue;seen[count++]=sock;
        if(G5(0x82B4CE18,W(sock+0x18),0xFFFF,0x5801,scratch+800,4)||
           G5(0x82B4CE18,W(sock+0x18),0xFFFF,0x5802,scratch+800,4)){jfail(c,0x417);return 0;}
    }
    if(!jtable(c))return 0;
    if(!c->host)jcopy((void*)(W(0x831228E4)+0x6F0),c->map,128);
    return 1;
}
/* Port of the hardware-proven lan_start_probe setup. Keep the original
 * Start control packet and completion events, bypassing backend housekeeping
 * only for this session's private concrete vtable. No synthetic Start input. */
static u32 jarm_start(JoinContext*c) {
    u32 g=state.game,p=g+0x238,n=W(0x831228E4);
    if(c->start_armed)return 1;
    if(W(g)!=c->table||W(c->table+0x98)!=0x822635A0||
       W(c->table+0xA0)!=0x82244AA8||B(g+0xC0)||B(g+0xB8)||
       W(g+0x1B4)!=2||!W(c->conn+0xC)||
       (c->host&&B(n+0xD25))){jfail(c,0x441);return 0;}
    if(W(p+0xE0)!=W(p+0xF0)||W(p+0x110)||B(p+0x13C)){
        jfail(c,0x442);return 0;}
    /* LAN platform operations complete through the original task queue.
     * This is direct UDP; the flag does not claim secure keys are registered. */
    B(p+0x104)=1;
    if(c->host)W(c->table+0xA0)=0x82244E88;
    c->start_armed=1;return 1;
}
static u32 jregister(JoinContext*c) {
    u32 g=state.game,n=W(0x831228E4);
    if(!W(c->conn+0xC)||!state.bridge||W(n+0xB64)!=state.bridge||!B(state.bridge+0x11)){
        jfail(c,0x420);return 0;}
    G2(0x8223D558,g+8,c->remote);
    if(W(c->remote+0x2C)!=6||W(g+0x200)!=2||W(g+0x204)||W(g+0x1B4)!=2||!B(c->conn+0x11E)){
        jfail(c,0x421);return 0;}c->registered=1;return jarm_start(c);
}
static u32 jroster(JoinContext*c) {
    u32 n=W(0x831228E4),r=W(n+0xB80),p,e;
    if(B(n+0x69C)>=49)return 0;
    if(W(r+4)!=1||(B(n+0xB84)&3)!=1){jfail(c,0x430);return 0;}
    p=alloc(0x3C);e=alloc(0x40);if(!jptr(p)||!jptr(e)){jfail(c,0x431);return 0;}
    jzero((void*)p,0x3C);W(p)=0xFFFFFFFF;W(p+4)=1;W(p+8)=0;W(p+0xC)=0x101;
    W(p+0x18)=0xFFFFFFFF;W(p+0x1C)=0xFFFFFFFF;jcopy((void*)(p+0x30),c->remote_xuid,8);W(p+0x38)=c->remote_flags;
    G2(0x8293D8C0,e,p);if(W(e)!=0x820E7E10){jfail(c,0x432);return 0;}
    W(n+0x6B4)=2;B(n+0xB84)|=2;c->roster_event=1;
    G2(0x8293BA70,n+0x5D0,e);return 1;
}
/* Our manually published game is absent from GM's owned-game list, so
 * ManagerRemoveGame detaches it but does not destroy it. Use nondeleting
 * destructors: these top-level objects came from the engine allocator, while
 * SDK deleting destructors use a different allocator. Preserve dependencies. */
static void jcleanup(JoinContext *c,u32 n) {
    u32 root=W(0x8314C964),settings=W(0x831228D0),gm=state.gm;
    u32 game=state.game,bridge=state.bridge,factory=state.factory,id=state.identity;
    if(!root||!settings||!W(n+0x698)||B(n+0x69C)||B(n+0xB9C)||
       W(settings+0x2C)||W(settings+0x30)||W(settings+0x34))return;
    if(!jptr(gm)||!jptr(game)||!jptr(bridge)||!jptr(factory)||!jptr(id)||
       W(root+0xD4)!=gm||W(root+0xD8)!=id||W(n+0xB64)!=bridge||
       W(0x831228D4)!=bridge||W(gm+0x94)!=factory||
       W(gm+0x1E8)||(W(gm+0x1EC)&&W(gm+0x1EC)!=game)||
       W(gm+0x21C)!=gm+0x21C||W(gm+0x220)!=gm+0x21C||
       W(gm+0x1F8)||W(gm+0x1DC)||W(bridge+4)||W(bridge+8)||
       W(bridge+12)||B(bridge+0x11)) {jfail(c,0x444);return;}
    if(W(gm+0x48)||W(gm+0x88)||W(game+0x318)!=W(game+0x328)||
       W(game+0x348)||W(game+0x374))return;
    /* Stage markers retained if a hardware exception interrupts destruction. */
    c->teardown=4;G1(0x8293CCB8,bridge);W(n+0xB64)=0;
    c->teardown=5;G1(0x82240330,game);W(gm+0x1EC)=0;
    c->teardown=6;G1(0x8224B4D8,factory);W(gm+0x94)=0;
    c->teardown=7;G1(0x82237860,gm);
    c->teardown=8;G1(0x82270EF0,id);
    W(root+0xD4)=0;W(root+0xD8)=0;
    release(bridge);release(game);release(factory);release(gm);release(id);
    release(c->table);
    state.gm=state.identity=state.game=state.peer=state.bridge=state.factory=0;
    state.session_context=0;release((u32)c);
    lan_discovery_reset();
    state.lan_id=state.player_id=0;state.host_setup=state.host_exec_context=0;
    state.browser_published=state.pending=0;state.error=0;state.status=0;
}
void lan_session_tick(void) {
    JoinContext*c=jc();u32 now,n,r,record,event,p;
    if(!c||c->failed)return;now=jclock();n=W(0x831228E4);r=W(n+0xB80);
    if(c->teardown) {
        if(c->teardown==2){jcleanup(c,n);return;}
        if(c->teardown!=1)return;
        /* The platform callback latches teardown. Drain its task completely,
         * then stop all narrow pumps before local removal may release peers or
         * the game. Never dereference retained transport pointers afterward. */
        p=state.game+0x238;
        if(c->teardown==1 && (W(p+0xE0)!=W(p+0xF0)||W(p+0x110)||B(p+0x13C))) {
            G1(0x82255EE0,p);return;
        }
        if(c->teardown==1 && (W(state.gm+0x48)||W(state.gm+0x88)))return;
        if(c->teardown==1) {
            /* Native bridge removal can re-enter join configuration when its
             * online IDs are populated. Our backend-free bridge owns neither. */
            if(W(state.bridge+4)||W(state.bridge+8)||W(state.bridge+0xC)) {
                c->teardown=3;state.error=0x443;state.status=0xFF;return;
            }
            c->conn=0;c->remote=0;c->registered=0;c->start_armed=0;
            c->teardown=2;state.status=6;
            G2(0x82238F88,state.gm,state.game+8);
        }
        return;
    }
    if(c->phase<PH_DONE && now-c->created>JOIN_TIMEOUT){jfail(c,0x440);return;}
    if(c->inbox_pending){c->remote_phase=c->inbox.phase;c->inbox_pending=0;}
    if(c->host&&!c->phase){if(!jflags(c)||!jpeer(c))return;c->phase=PH_PREPARED;}
    if(!c->host&&c->phase==PH_REQUEST&&c->remote_phase>=PH_PREPARED){
        if(!jpeer(c))return;c->phase=PH_PREPARED;
    }
    if(c->conn&&c->remote_phase>=PH_PREPARED){
        if(!c->clock_started){W(c->conn+0x118)=now;c->clock_started=1;}
        G2(0x8224A7E8,c->conn,now);
        if(c->phase==PH_PREPARED&&W(c->conn+0xC))c->phase=PH_LINK;
    }
    if(c->phase==PH_LINK&&c->remote_phase>=PH_LINK){if(!jregister(c))return;c->phase=PH_ADMITTED;}
    if(c->registered){
        G2(0x8223BDA0,state.game+8,now);G2(0x8223B2C8,state.game+8,now);
        if(c->start_armed)G1(0x82255EE0,state.game+0x238);
        if(c->teardown)return;
    }
    if(c->host&&c->phase==PH_ADMITTED&&c->remote_phase>=PH_ADMITTED){
        if(!c->roster_event){if(!jroster(c))return;}
        else if(W(r+4)==2){
            record=(u32)G2(0x8293C540,r,0x101);
            if(!record||W(record+4)!=1||!jequal((void*)(record+0x4C),c->remote_xuid,8)){jfail(c,0x433);return;}
            W(record+0x24)=1;c->phase=PH_ROSTER;
        }
    }
    if(!c->host&&c->phase==PH_ADMITTED&&c->remote_phase>=PH_ROSTER){
        if(!c->join_event){
            if(B(n+0x69C)>=49)return;
            event=alloc(12);if(!jptr(event)){jfail(c,0x434);return;}
            W(event)=0x820E7DD4;W(event+4)=0;W(event+8)=0;c->join_event=1;
            G2(0x8293BA70,n+0x5D0,event);
        }else if(W(r+4)==2)c->phase=PH_DONE;
    }
    if(c->host&&c->phase==PH_ROSTER&&c->remote_phase==PH_DONE&&W(r+4)==2)c->phase=PH_DONE;
    if(c->phase==PH_DONE)state.status=5;
    if(now-c->sent>=250)jsend(c,now);
}
