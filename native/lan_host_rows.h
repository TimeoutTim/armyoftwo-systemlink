// SPDX-License-Identifier: GPL-3.0-only
#ifndef AO2_LAN_HOST_ROWS_H
#define AO2_LAN_HOST_ROWS_H
/* Shared discovery cache operations; no title memory or network dependencies. */
typedef struct LanHostRow {
    u32 ip, port, last_seen;
    u8 name[16], xuid[8];
    u32 nonce;
} LanHostRow;

static u32 lan_rows_expire(LanHostRow *rows,u32 count,u32 now) {
    u32 i,keep=0,j;
    for(i=0;i<count;i++) {
        if(now-rows[i].last_seen>=5000u)continue;
        if(keep!=i)for(j=0;j<sizeof(LanHostRow);j++)
            ((u8*)&rows[keep])[j]=((u8*)&rows[i])[j];
        keep++;
    }
    return keep;
}
static u32 lan_rows_slot(const LanHostRow *rows,u32 count,u32 ip) {
    u32 i;
    /* One title endpoint per console. A restarted host replaces its old nonce. */
    for(i=0;i<count;i++)if(rows[i].ip==ip)return i;
    return count;
}
#endif
