// SPDX-License-Identifier: GPL-3.0-only
#ifndef AO2_LAN_RUNTIME_H
#define AO2_LAN_RUNTIME_H
typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned __int64 u64;
#define W(a) (*(volatile u32 *)(u32)(a))
#define B(a) (*(volatile u8 *)(u32)(a))
typedef struct LanState {
    u32 magic, version, status, error, requests;
    u32 gm, identity, game, peer, bridge, factory, user, lan_id, player_id;
    u8 name[16], xuid[8];
    u32 menu_context, ticks, pending, last_command, menu_ready, menu_scene;
    u32 browser_context, discovery, session_context, browser_published;
    u32 host_setup, host_exec_context;
} LanState;
#define state (*(volatile LanState *)0x8314cb80)
typedef char assert_state_fits[(sizeof(LanState)==128)?1:-1];
/* Cleanup machine-code islands depend on these two ownership offsets. */
typedef char assert_game_offset[((u32)&((LanState*)0)->game==0x1C)?1:-1];
typedef char assert_session_offset[((u32)&((LanState*)0)->session_context==0x70)?1:-1];
#define G0(f) ((u64 (*)(void))(f))()
#define G1(f,a) ((u64 (*)(u64))(f))((u64)(a))
#define G2(f,a,b) ((u64 (*)(u64,u64))(f))((u64)(a),(u64)(b))
#define G3(f,a,b,c) ((u64 (*)(u64,u64,u64))(f))((u64)(a),(u64)(b),(u64)(c))
#define G4(f,a,b,c,d) ((u64 (*)(u64,u64,u64,u64))(f))((u64)(a),(u64)(b),(u64)(c),(u64)(d))
#define G5(f,a,b,c,d,e) ((u64 (*)(u64,u64,u64,u64,u64))(f))((u64)(a),(u64)(b),(u64)(c),(u64)(d),(u64)(e))
#define G6(f,a,b,c,d,e,g) ((u64 (*)(u64,u64,u64,u64,u64,u64))(f))((u64)(a),(u64)(b),(u64)(c),(u64)(d),(u64)(e),(u64)(g))
#define G7(f,a,b,c,d,e,g,h) ((u64 (*)(u64,u64,u64,u64,u64,u64,u64))(f))((u64)(a),(u64)(b),(u64)(c),(u64)(d),(u64)(e),(u64)(g),(u64)(h))
#define G8(f,a,b,c,d,e,g,h,i) ((u64 (*)(u64,u64,u64,u64,u64,u64,u64,u64))(f))((u64)(a),(u64)(b),(u64)(c),(u64)(d),(u64)(e),(u64)(g),(u64)(h),(u64)(i))
#include "lan_host_rows.h"
u32 alloc(u32 size);
void release(u32 p);
void lan_discovery_reset(void);
u32 lan_prepare(u32 is_host);
u32 lan_prepare_url(u32 is_host, const char *map_url);
u32 lan_browser_publish(const char *names, u32 count);
u32 lan_browser_selected(void);
u32 lan_browser_update(void);
u32 lan_browser_host(u32 index);
u32 lan_browser_cancel(void);
u32 lan_browser_refresh(void);
u32 lan_discovery_search(void);
void lan_discovery_tick(void);
u32 lan_discovery_count(void);
u32 lan_discovery_row(u32 index);
u32 lan_discovery_searching(void);
u32 lan_discovery_nonce(void);
u32 lan_discovery_send(u32 ip, const void *packet, u32 len);
u32 lan_session_join(const LanHostRow *host);
void lan_session_tick(void);
u32 lan_session_leave_lobby(void);
void lan_session_receive(u32 ip, const void *packet, u32 len);
u32 lan_host(void);
u64 lan_plasma_host(u64 native, u64 map_url, u64 player_skin, u64 game_type);
u64 lan_matchmaking(u64 matchmaking);
void lan_menu_tick(void);
u32 lan_open_host_choice(u32 owner);
u32 lan_open_mission_choice(u32 owner);
u64 lan_selected_mission(u64 controller,u64 selected_url,u64 difficulty);
u32 lan_open_browser(void);
#endif
