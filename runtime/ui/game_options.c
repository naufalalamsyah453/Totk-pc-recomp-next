// The core settings tabs (Screen, Game, Sound, Controls) inside TotK's own Options screen, the way
// SavageWindRecomp puts them in BOTW's (refs/SavageWindRecomp runtime/ui/game_options.mm). The plan and the
// confirmed 1.4.3 addresses live in docs/native-options-plan.md: the option table sits behind the singleton at
// main+0x3a82e70 -> +0x5e8, an entry is 0x28 bytes, and read/store receive the entry's +0x20 value key in w3.
// recomp/patches-v143.txt calls the rt_opt_* below at the entry of the lifted functions (Phase 2); this file is
// Phase 3 - the runtime half that turns those calls into cfg reads and writes.
//
// Every hook is fail-soft and gated on TOTK_NATIVE_OPTIONS (default 0): with the key off the hooks return without
// touching anything and the screen is the game's, byte for byte. The gate exists because the rows are not labelled
// yet - the message hook that would supply their titles (Phase 3.2) is still a stub - so the feature stays off
// until an in-game smoke test passes.
//
// Contracts, from the lifted callers (plan "Hook argument contract"):
//  - rt_opt_read   @hostif: c->x[0] context, c->x[1] out, c->x[2] binding, w3 = entry+0x20 key. Write the value
//                 index (u32) to out, leave c->x[0] = 1 (found) and return 1 to take the call over.
//  - rt_opt_store  @hostif: c->x[0] context, w1 = value index, c->x[2] binding, w3 = key. Map the index to the
//                 item's cfg value, cfg_set it, leave c->x[0] = 1 and return 1.
//  - the void ones @host: side effects only; leave c->nzcv, c->sp and x19-x29 alone.
//
// A row is one Item: a cfg key with the values it accepts, in cycle order. A toggle is a two-value choice
// ("0"/"1"), so both kinds share one value model - read returns the current value's index, store receives it -
// and kind only decides how the game's row widget renders (0 = toggle button, 1 = choice with arrows).
#include <stdint.h>
#include <string.h>

#include "cfg.h"
#include "cpu.h"
#include "layout.h"

// Guest memory: the recomp maps the guest address space into this process, so a guest pointer is a valid host
// pointer, exactly as menu_cursor.c reads the screen manager.
static uint64_t rd64(uint64_t a) {
    uint64_t v;
    memcpy(&v, (const void *)(uintptr_t)a, 8);
    return v;
}
static uint32_t rd32(uint64_t a) {
    uint32_t v;
    memcpy(&v, (const void *)(uintptr_t)a, 4);
    return v;
}
static void wr32(uint64_t a, uint32_t v) { memcpy((void *)(uintptr_t)a, &v, 4); }
static void wr64(uint64_t a, uint64_t v) { memcpy((void *)(uintptr_t)a, &v, 8); }

// The option table (1.4.3): the singleton object is at main+0x3a82e70, its +0x5e8 field is the table, and the
// table holds a u32 count at +0x0 and the entries pointer at +0x8 (stride 0x28). There is no order or rows array:
// display order is the array order and the visible row count is count()'s filtered result (plan Phase 1).
#define OPT_SINGLETON 0x3a82e70ull
#define OPT_TABLE_FIELD 0x5e8ull
#define TBL_COUNT 0x0u
#define TBL_ENTRIES 0x8u

// One option entry, 0x28 bytes. The offsets are the ones the lifted row-sync and decide code reads; +0xc and
// +0x14 are not yet identified and are copied verbatim from the game's entries (zero for ours).
typedef struct {
    uint32_t id;      // +0x0  option id: entry-by-id and the "10%02d" message key
    uint32_t kind;    // +0x4  0 = toggle, 1 = choice, 2 = button
    uint32_t def;     // +0x8  default value index
    uint32_t pad0;    // +0xc  unidentified
    uint32_t nvals;   // +0x10 value count (the choice cycle modulus)
    uint32_t pad1;    // +0x14 unidentified
    uint64_t vals;    // +0x18 u32 value-id array; ours is [0, 1, ...] so an id is the value index
    uint32_t key;     // +0x20 per-option value key: the w3 read/store dispatch on
    uint32_t cond;    // +0x24 0 = always shown (count() gates the rest through a predicate)
} Ent;

static uint64_t opt_table(void) {
    uint64_t obj = rd64(MOD_MAIN_BASE + OPT_SINGLETON);
    if (obj < 0x10000 || obj >= (1ull << 47)) return 0;
    uint64_t t = rd64(obj + OPT_TABLE_FIELD);
    return (t >= 0x10000 && t < (1ull << 47)) ? t : 0;
}

// ---- the rows we add
//
// One Item per cfg key. The cfg name carries the "TOTK_" prefix cfg_get/cfg_set expect; vals are the accepted
// values in the order the row cycles them, and they are the same strings the metas table in runtime/cfg.c lists
// after "|". The set is curated: tab_screen alone draws about thirty rows in the F1 menu, which cannot fit one
// page of the game's fixed row widgets (plan Phase 3), so only the high-traffic keys come across here and the
// rest stay on F1 until the page navigation that can partition them exists.
typedef struct {
    const char *cfg;
    const char *const *vals;
    int nvals;
    int kind;   // 0 = toggle row, 1 = choice row
} Item;

static const char *const v_onoff[] = {"0", "1"};
static const char *const v_upscaler[] = {"fsr", "bilinear"};
static const char *const v_perf[] = {"off", "fps", "full"};
static const char *const v_aspect[] = {"fit", "stretch"};
static const char *const v_aniso[] = {"game", "2", "4", "8", "16"};
static const char *const v_mode[] = {"docked", "handheld"};
static const char *const v_channels[] = {"auto", "stereo", "5.1"};
static const char *const v_pad[] = {"auto", "nintendo", "xbox"};
static const char *const v_mouse[] = {"stick", "direct"};

static Item items[] = {
    // Screen
    {"TOTK_FULLSCREEN", v_onoff, 2, 0}, {"TOTK_FXAA", v_onoff, 2, 0}, {"TOTK_HDR", v_onoff, 2, 0},
    {"TOTK_UPSCALER", v_upscaler, 2, 1}, {"TOTK_PERF_OVERLAY", v_perf, 3, 1}, {"TOTK_ASPECT", v_aspect, 2, 1},
    {"TOTK_ANISO", v_aniso, 5, 1},
    // Game
    {"TOTK_ASSUME_FOCUS", v_onoff, 2, 0}, {"TOTK_OPERATION_MODE", v_mode, 2, 1},
    // Sound
    {"TOTK_AUDIO_CHANNELS", v_channels, 3, 1},
    // Controls
    {"TOTK_MOTION", v_onoff, 2, 0}, {"TOTK_MENU_CURSOR", v_onoff, 2, 0}, {"TOTK_PAD_LAYOUT", v_pad, 3, 1},
    {"TOTK_MOUSE_MODE", v_mouse, 2, 1},
};
enum { NITEMS = (int)(sizeof items / sizeof items[0]) };

// Our option ids and value keys are handed out from ranges the game does not use, so entry-by-id and the read/
// store dispatch never mistake one of ours for a game entry. The value key is what read/store see in w3; the id
// is what entry-by-id and the "10%02d" message key use.
#define ITEM_ID_BASE 0x8000u
#define ITEM_KEY_BASE 0x4e000000u
static uint32_t item_id(int i) { return ITEM_ID_BASE + (uint32_t)i; }
static uint32_t item_key(int i) { return ITEM_KEY_BASE + (uint32_t)i; }
static int item_by_key(uint32_t key) {
    for (int i = 0; i < NITEMS; i++)
        if (item_key(i) == key) return i;
    return -1;
}

// ---- install: capture the game's entries once, then present ours in front of them
//
// Our rows go first so they land inside the layout's widget budget: build_rows looks up Pa_ToggleBtn_%02d by row
// index and its null check (cbz) is fail-soft, so rows past the last instance the bgyml defines render invisible
// rather than crash. Putting ours first means a budget that only fits the game's original rows still shows every
// one of ours and pushes the tail of the game's out of sight until the page navigation (which gives each its own
// page) exists.
#define MAX_ENTRIES 512
static Ent g_swap[MAX_ENTRIES];
static uint32_t g_our_vals[NITEMS][8];
static uint32_t g_swap_n;
static int g_ready;

static int enabled(void) { return cfg_bool("TOTK_NATIVE_OPTIONS"); }

// The default value index for an item, from the metas table's default string.
static uint32_t item_default(const Item *it) {
    const CfgMeta *m = cfg_meta(it->cfg);
    if (m && m->def)
        for (int v = 0; v < it->nvals; v++)
            if (!strcmp(m->def, it->vals[v])) return (uint32_t)v;
    return 0;
}

// Fills g_swap with our entries followed by a copy of the game's. Returns 0 (and leaves g_ready clear) when the
// table is not in a shape to extend, which leaves the screen untouched.
static int install(uint64_t table) {
    uint32_t gn = rd32(table + TBL_COUNT);
    uint64_t ge = rd64(table + TBL_ENTRIES);
    if (!ge || !gn || gn + (uint32_t)NITEMS > MAX_ENTRIES) return 0;
    for (int i = 0; i < NITEMS; i++) {
        const Item *it = &items[i];
        Ent *e = &g_swap[i];
        memset(e, 0, sizeof *e);
        e->id = item_id(i);
        e->kind = (uint32_t)it->kind;
        e->def = item_default(it);
        e->nvals = (uint32_t)it->nvals;
        // The value ids are 0..n-1, so an id and the row's value index are the same number: store's w1 and our
        // read's out are both that index, and mapping it back to a cfg string is a table lookup.
        for (int v = 0; v < it->nvals && v < 8; v++) g_our_vals[i][v] = (uint32_t)v;
        e->vals = (uint64_t)(uintptr_t)g_our_vals[i];
        e->key = item_key(i);
        e->cond = 0;   // always shown: count() counts it without the predicate
    }
    memcpy(g_swap + NITEMS, (const void *)(uintptr_t)ge, (size_t)gn * sizeof(Ent));
    g_swap_n = gn + (uint32_t)NITEMS;
    g_ready = 1;
    return 1;
}

// ---- the hooks

// The screen building its rows (slot84): present our array before the original reads the table, so it allocates
// and wires rows for whatever count is installed. Runs every build; install only ever captures the game's own
// entries, which is the state before the first swap.
void rt_opt_build_rows(Cpu *c) {
    (void)c;
    if (!enabled()) return;
    uint64_t table = opt_table();
    if (!table) return;
    if (!g_ready && !install(table)) return;
    wr64(table + TBL_ENTRIES, (uint64_t)(uintptr_t)g_swap);
    wr32(table + TBL_COUNT, g_swap_n);
}

// The generic value read: when w3 is one of our keys, answer with the current value's index instead of letting
// the game look the option up. out (c->x[1]) is where the caller expects a u32.
int rt_opt_read(Cpu *c) {
    if (!enabled()) return 0;
    int i = item_by_key((uint32_t)c->x[3]);
    if (i < 0) return 0;
    const Item *it = &items[i];
    const char *cur = cfg_value(it->cfg);
    uint32_t idx = item_default(it);
    for (int v = 0; v < it->nvals; v++)
        if (!strcmp(cur, it->vals[v])) {
            idx = (uint32_t)v;
            break;
        }
    wr32((uint64_t)c->x[1], idx);
    c->x[0] = 1;   // "found": the caller branches on w0's low bit
    return 1;
}

// The generic value write: w1 is the chosen value index (a toggle passes 0/1, a choice the cycled index). Map it
// back through the item's values and store it in cfg.
int rt_opt_store(Cpu *c) {
    if (!enabled()) return 0;
    int i = item_by_key((uint32_t)c->x[3]);
    if (i < 0) return 0;
    const Item *it = &items[i];
    uint32_t idx = (uint32_t)c->x[1];
    if (idx >= (uint32_t)it->nvals) idx = item_default(it);
    cfg_set(it->cfg, it->vals[idx]);
    c->x[0] = 1;
    return 1;
}

// The screen opening: no side effect yet - the page state and the reopen-on-page-change path land with the
// decide hook (plan Phase 3).
void rt_opt_screen_open(Cpu *c) {
    (void)c;
}

// Per-frame tick: nothing yet; the caption and guide refresh the original already does covers our rows too once
// their messages resolve (Phase 3.2).
void rt_opt_tick(Cpu *c) {
    (void)c;
}

// The decide dispatcher (slot49): section buttons and page changes need this, but they need a screen rebuild to
// go with them (SavageWind closes and reopens the screen on a page change), so it stays a no-op until then. With
// it empty the injected rows still toggle and cycle through the game's own kind-0/kind-1 handlers, which reach
// rt_opt_store above.
void rt_opt_decide(Cpu *c) {
    (void)c;
}

// The message apply: the row titles and value texts come from the sheet key "10%02d" built off the option id, so
// our synthetic ids resolve to nothing and the rows read blank until this supplies UTF-16 for them (Phase 3.2).
// Returning 0 leaves the call to the game, which is the correct fail-soft while the feature is gated off.
int rt_opt_msg_lookup(Cpu *c) {
    (void)c;
    return 0;
}
