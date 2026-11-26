// Unreal console variables found by their registered names, and the texture streaming pool the profile's ue_texture_pool raises.
// The rewriter finds each TAutoConsoleVariable the host may write in main (dm/tools/ue_cvar.py: its static constructor's registration, in any Unreal 4 / 5 build) and names it in main's
// fixups (UE_CVAR:<name>); the object holds at +0x10 the pointer to the variable's value, the game thread's copy, then the render thread's. Called on every present (hle_nvn.c)
#include "hle.h"
#include <stdlib.h>

static int rd(uint64_t a, void *out, size_t n) { return a >= 0x1000 && !plat_mem_read(a, out, n); }

// main's offset of the TAutoConsoleVariable registered under `name`, 0 if the rewriter found none (not an Unreal main, not one of ue_cvar.py's NAMES, or registered at two objects)
uint64_t dm_ue_cvar(const char *name) {
    for (int i = 0; i < g_nmod; i++) if (!strcmp(g_mod[i].name, "main"))
        for (int k = 0; k < g_mod[i].n_ue_cvar; k++) if (!strcmp(g_mod[i].ue_cvar[k].name, name)) return g_mod[i].ue_cvar[k].off;
    return 0;
}

// PEANUTS: Persistent Unreal CVar overrides via DM_UE_CVARS="<hex_off>=f<float>|<hex_off>=i<int>,..."
// Writes both game-thread and render-thread copies of each TAutoConsoleVariable (+0x10 value ptr) whenever the game resets them.
static void custom_cvars_tick(uint8_t *base) {
    enum { MAX_OV = 16 };
    static struct { uint64_t off; int is_f32, nlog; union { float f; int32_t i; } v; } ov[MAX_OV];
    static int inited = 0, nov = 0;
    if (!inited) {
        inited = 1;
        const char *e = getenv("DM_UE_CVARS");
        if (!e || !*e) return;
        const char *p = e;
        while (*p && nov < MAX_OV) {
            while (*p == ' ' || *p == ',' || *p == ';') p++;
            if (!*p) break;
            char *eq = NULL;
            uint64_t off = strtoull(p, &eq, 16);
            if (!eq || *eq != '=' || !off) break;
            p = eq + 1;
            int is_f32 = (*p == 'f' || *p == 'F');
            if (*p == 'f' || *p == 'F' || *p == 'i' || *p == 'I') p++;
            char *end = NULL;
            ov[nov].off = off;
            ov[nov].is_f32 = is_f32;
            ov[nov].nlog = 0;
            if (is_f32) ov[nov].v.f = strtof(p, &end);
            else ov[nov].v.i = (int32_t)strtol(p, &end, 0);
            if (!end || end == p) break;
            if (is_f32) LOG("main: Unreal CVar override main+%#llx = %g (DM_UE_CVARS)", (unsigned long long)off, ov[nov].v.f);
            else LOG("main: Unreal CVar override main+%#llx = %d (DM_UE_CVARS)", (unsigned long long)off, ov[nov].v.i);
            nov++;
            p = end;
        }
    }
    if (!nov || !base) return;
    for (int k = 0; k < nov; k++) {
        uint64_t val = 0;
        if (!rd((uint64_t)(uintptr_t)(base + ov[k].off + 0x10), &val, sizeof val) || !val) continue;
        for (int c = 0; c < 2; c++) {
            if (ov[k].is_f32) {
                float cur, *ptr = (float *)(uintptr_t)(val + 4 * (uint64_t)c);
                if (!rd((uint64_t)(uintptr_t)ptr, &cur, sizeof cur) || cur == ov[k].v.f) continue;
                uint32_t bits; memcpy(&bits, &ov[k].v.f, 4);
                dm_guest_write_prepare(ptr, 4); __atomic_store_n((uint32_t *)ptr, bits, __ATOMIC_RELAXED);
                if (ov[k].nlog < 4) { ov[k].nlog++; LOG("main: Unreal CVar main+%#llx (%s thread) %g → %g (DM_UE_CVARS)", (unsigned long long)ov[k].off, c ? "render" : "game", cur, ov[k].v.f); }
            } else {
                int32_t cur, *ptr = (int32_t *)(uintptr_t)(val + 4 * (uint64_t)c);
                if (!rd((uint64_t)(uintptr_t)ptr, &cur, sizeof cur) || cur == ov[k].v.i) continue;
                dm_guest_write_prepare(ptr, 4); __atomic_store_n(ptr, ov[k].v.i, __ATOMIC_RELAXED);
                if (ov[k].nlog < 4) { ov[k].nlog++; LOG("main: Unreal CVar main+%#llx (%s thread) %d → %d (DM_UE_CVARS)", (unsigned long long)ov[k].off, c ? "render" : "game", cur, ov[k].v.i); }
            }
        }
    }
}

// ue_texture_pool: r.Streaming.PoolSize, the megabytes Unreal's texture streamer keeps resident; past it the streamer drops the top mips of the textures in view. The game's
// device profile sets it (DRAGON QUEST VII Reimagined: 200 on the Switch, 1000 on Windows) and the streaming manager reads it again whenever it changes. The host raises a smaller
// value the game sets to the target, whenever the game sets it back; a larger one, or -1 (no pool size: the engine sizes its pool otherwise), stays. The profile key applies on the
// Mac; on iOS only DM_UE_TEXTURE_POOL turns it on (Documents/env.txt), until an iPhone run shows the memory fits. DM_UE_TEXTURE_POOL=0 leaves the game's value
void dm_ue_cvar_tick(void) {
    uint8_t *base = NULL;
    for (int i = 0; i < g_nmod; i++) if (!strcmp(g_mod[i].name, "main")) base = g_mod[i].base;
    custom_cvars_tick(base);
    static int state = -1, target, nlog; static uint64_t obj_off; static const char *src;
    if (!state) return;
    if (state < 0) {
        state = 0;
        const char *e = getenv("DM_UE_TEXTURE_POOL");
        target = e ? atoi(e) : PLAT_IOS ? 0 : DM_GAME_UE_TEXTURE_POOL; src = e ? "DM_UE_TEXTURE_POOL" : "game profile ue_texture_pool";
        if (target <= 0) { if (DM_GAME_UE_TEXTURE_POOL) LOG("main: Unreal r.Streaming.PoolSize left as the game sets it (%s)", e ? src : "iOS without DM_UE_TEXTURE_POOL"); return; }
        obj_off = dm_ue_cvar("r.Streaming.PoolSize");
        if (!obj_off) { LOG("[RED] main: Unreal r.Streaming.PoolSize not found in main by the rewriter (ue_cvar.py; %s %d MB not applied)", src, target); return; }
        LOG("main: Unreal r.Streaming.PoolSize at main+%#llx, raised to %d MB (%s)", (unsigned long long)obj_off, target, src);
        state = 1;
    }
    uint64_t val;
    if (!base || !rd((uint64_t)(uintptr_t)(base + obj_off + 0x10), &val, sizeof val) || !val) return;   // not created yet
    for (int c = 0; c < 2; c++) {
        int32_t cur, *p = (int32_t *)(uintptr_t)(val + 4 * (uint64_t)c);
        if (!rd((uint64_t)(uintptr_t)p, &cur, sizeof cur) || cur <= 0 || cur >= target) continue;
        dm_guest_write_prepare(p, sizeof *p); __atomic_store_n(p, target, __ATOMIC_RELAXED);
        if (nlog < 8) { nlog++; LOG("main: Unreal r.Streaming.PoolSize (%s thread) %d → %d MB (%s)", c ? "render" : "game", cur, target, src); }
    }
}
