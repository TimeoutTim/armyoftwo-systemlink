// SPDX-License-Identifier: GPL-3.0-only
/* Title-owned, nonblocking LAN discovery and session-control datagrams.
 * ABIs: pristine PE thunk ordinals 3/4/6/7/11/20/24 map to the original
 * wrappers below. Their assembly inserts XAM caller=1. Ordinal 0x35 is
 * XNetRandom(caller,buffer,length); 830DF564 virtual+8 is SDK milliseconds.
 * Discovery never accepts an IP supplied by the packet: use recvfrom source.
 */
#include "lan_runtime.h"

#define DISC_MAGIC 0x41324C44u /* A2LD */
#define SESSION_MAGIC 0x41324A43u /* A2JC */
#define DISC_PORT 6002u
#define DISC_ROWS 8u
#define DISC_SIZE 48u
#define DISC_CAPACITY 1024u

typedef struct Discovery {
    u32 socket, nonce, query, searching, started, last_query;
    u32 count, error, received, sent;
    LanHostRow rows[DISC_ROWS];
    u8 address[16], from[16], packet[DISC_CAPACITY], outgoing[DISC_SIZE];
    u32 from_length, option;
} Discovery;

static void dzero(u32 p,u32 n) { while(n--)B(p++)=0; }
static void dcopy(u32 d,u32 s,u32 n) { while(n--)B(d++)=B(s++); }
static u32 now_ms(void) { return (u32)G1(W(0x821A3198+8),0x830DF564); }
static u32 last_error(void) { return (u32)G0(0x82B4D010); }
static u32 derr(Discovery *d,u32 error) {
    d->error=error; state.error=error;
    if(d->socket!=0xFFFFFFFFu)G1(0x82B4CDE0,d->socket);
    d->socket=0xFFFFFFFFu;
    return 0;
}
static Discovery *context(void) {
    Discovery *d;
    u32 p;
    if(state.discovery)return (Discovery *)state.discovery;
    /* Respect the title's existing WSA/XNet startup ownership. */
    if(!W(0x8314BCA4) || W(0x830DF564)!=0x821A3198)return 0;
    p=alloc(sizeof(Discovery));
    if(!p)return 0;
    d=(Discovery *)p; state.discovery=p;
    d->socket=0xFFFFFFFFu;
    if((u32)G3(0x8308B564,1,(u32)&d->nonce,4) || !d->nonce) {
        derr(d,0x300); return d;
    }
    d->socket=(u32)G3(0x82B4CDC8,2,2,17); /* AF_INET/SOCK_DGRAM/UDP */
    if(d->socket==0xFFFFFFFFu) { derr(d,0x301); return d; }
    d->option=1;
    /* FIONBIO precedes every I/O call. Broadcast and both hardware-verified
     * direct-UDP options are required; no insecure retry after a failed option. */
    if((u32)G3(0x82B4CE00,d->socket,0x8004667E,(u32)&d->option) ||
       (u32)G5(0x82B4CE18,d->socket,0xFFFF,0x20,(u32)&d->option,4) ||
       (u32)G5(0x82B4CE18,d->socket,0xFFFF,0x5801,(u32)&d->option,4) ||
       (u32)G5(0x82B4CE18,d->socket,0xFFFF,0x5802,(u32)&d->option,4)) {
        derr(d,0x302); return d;
    }
    /* Big-endian Xbox sockaddr: u16 family, network-order u16 port/IP. */
    W((u32)d->address)=(2u<<16)|DISC_PORT;
    if((u32)G3(0x82B4CE88,d->socket,(u32)d->address,16))derr(d,0x303);
    return d;
}

u32 lan_discovery_send(u32 ip,const void *packet,u32 len) {
    Discovery *d=context();
    u32 result;
    if(!d || d->error || !ip || !packet || !len || len>DISC_CAPACITY)return 0;
    dzero((u32)d->address,16);
    W((u32)d->address)=(2u<<16)|DISC_PORT;
    W((u32)d->address+4)=ip;
    result=(u32)G6(0x82B4CFE8,d->socket,(u32)packet,len,0,(u32)d->address,16);
    if(result==len) { d->sent++; return 1; }
    return 0; /* Nonblocking send failures are retried by the owning protocol. */
}

static void discovery_packet(Discovery *d,u32 type,u32 query) {
    u32 p=(u32)d->outgoing;
    dzero(p,DISC_SIZE);
    W(p)=DISC_MAGIC; W(p+4)=1; W(p+8)=type;
    W(p+12)=d->nonce; W(p+16)=query;
    if(type==2) {
        W(p+20)=6000;
        dcopy(p+24,(u32)state.name,16);
        dcopy(p+40,(u32)state.xuid,8);
    }
}

u32 lan_discovery_search(void) {
    Discovery *d=context();
    if(!d || d->error)return 0;
    d->count=0; dzero((u32)d->rows,sizeof(d->rows));
    d->started=now_ms(); d->last_query=d->started;
    d->query++; if(!d->query)d->query=1;
    d->searching=1;
    discovery_packet(d,1,d->query);
    lan_discovery_send(0xFFFFFFFFu,d->outgoing,DISC_SIZE);
    return 1;
}

u32 lan_discovery_searching(void) {
    Discovery *d=(Discovery *)state.discovery;
    return d && !d->error ? d->searching : 0;
}
u32 lan_discovery_nonce(void) {
    Discovery *d=context();
    return d && !d->error ? d->nonce : 0;
}
u32 lan_discovery_count(void) {
    Discovery *d=(Discovery *)state.discovery;
    return d && !d->error ? d->count : 0;
}
u32 lan_discovery_row(u32 index) {
    Discovery *d=(Discovery *)state.discovery;
    return d && !d->error && index<d->count ? (u32)&d->rows[index] : 0;
}

static u32 waiting_host(void) {
    u32 native=W(0x831228E4),roster;
    if(!state.game || !state.factory || state.lan_id!=0x103 ||
       W(state.game)!=0x821A3ED8 || B(state.game+0xC0) || !native)return 0;
    roster=W(native+0xB80);
    return roster && W(roster+4)==1 && !B(native+0xD25);
}

static void accept_offer(Discovery *d,u32 ip,u32 now) {
    u32 p=(u32)d->packet,i,j,n;
    LanHostRow *row;
    if(!d->searching || W(p+16)!=d->query || W(p+20)!=6000 ||
       !(W(p+40)|W(p+44)))return;
    for(n=0;n<16 && B(p+24+n);n++)
        if(B(p+24+n)<32 || B(p+24+n)==127)return;
    if(!n || n==16)return;
    i=lan_rows_slot(d->rows,d->count,ip);
    if(i==DISC_ROWS)return;
    if(i==d->count)d->count++;
    row=&d->rows[i]; row->ip=ip; row->port=6000; row->last_seen=now;
    row->nonce=W(p+12);
    for(j=0;j<16;j++)row->name[j]=B(p+24+j);
    for(j=0;j<8;j++)row->xuid[j]=B(p+40+j);
}

void lan_discovery_tick(void) {
    Discovery *d;
    u32 now,i,len,p,ip,type;
    /* Opening a browser or becoming host starts ownership. Existing context
     * remains polled for session control even when no search is active. */
    if(!state.discovery && !state.game)return;
    d=context(); if(!d || d->error)return;
    now=now_ms();
    /* Search lasts for the browser lifetime, not a two-second snapshot. */
    d->searching=state.status==3;
    if(d->searching)d->count=lan_rows_expire(d->rows,d->count,now);
    if(d->searching && now-d->last_query>=1000) {
        d->last_query=now;
        discovery_packet(d,1,d->query);
        lan_discovery_send(0xFFFFFFFFu,d->outgoing,DISC_SIZE);
    }
    for(i=0;i<4;i++) {
        d->from_length=16;
        len=(u32)G6(0x82B4CF60,d->socket,(u32)d->packet,DISC_CAPACITY,
                   0,(u32)d->from,(u32)&d->from_length);
        if(len==0xFFFFFFFFu) {
            u32 error=last_error();
            /* A malformed oversize datagram is discarded; next tick continues. */
            if(error!=10035 && error!=10040)d->received++;
            break;
        }
        d->received++;
        if(d->from_length!=16 || W((u32)d->from)!=((2u<<16)|DISC_PORT))continue;
        ip=W((u32)d->from+4); p=(u32)d->packet;
        if(!ip || ip==0xFFFFFFFFu || len<4)continue;
        if(W(p)==SESSION_MAGIC) {
            lan_session_receive(ip,(const void *)p,len);
            continue;
        }
        if(len!=DISC_SIZE || W(p)!=DISC_MAGIC || W(p+4)!=1 ||
           !W(p+12) || W(p+12)==d->nonce)continue;
        type=W(p+8);
        if(type==1 && waiting_host()) {
            discovery_packet(d,2,W(p+16));
            lan_discovery_send(ip,d->outgoing,DISC_SIZE);
        } else if(type==2)accept_offer(d,ip,now);
    }
}

/* End the discovery instance too: a new socket/nonce rejects delayed control
 * datagrams from the old session. The next Host/Join creates it lazily. */
void lan_discovery_reset(void) {
    Discovery *d=(Discovery *)state.discovery;
    if(!d)return;
    state.discovery=0;
    if(d->socket!=0xFFFFFFFFu)G1(0x82B4CDE0,d->socket);
    release((u32)d);
}
