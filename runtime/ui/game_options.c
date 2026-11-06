// The core settings tabs (Screen, Game, Sound, Controls) inside TotK's own Options screen, the way
// SavageWindRecomp puts them in BOTW's (refs/SavageWindRecomp runtime/ui/game_options.mm). The plan and the
// confirmed 1.4.3 addresses live in docs/native-options-plan.md: the option table sits behind the singleton at
// main+0x3a82e70 -> +0x5e8, an entry is 0x28 bytes, and read/store receive the entry's +0x20 value key in w3.
// recomp/patches-v143.txt calls the rt_opt_* below at the entry of the lifted functions (Phase 2); this file is
// Phase 3 - the runtime half that turns those calls into cfg reads and writes.
//
// Every hook is fail-soft and gated on TOTK_NATIVE_OPTIONS (default 0): with the key off the hooks return without
// touching anything and the screen is the game's, byte for byte. The gate exists because none of it has run in the
// game yet (the message hook's text format is an assumption, see rt_opt_msg_lookup), so the feature stays off until
// an in-game smoke test passes.
//
// Contracts, from the lifted callers (plan "Hook argument contract"):
//  - rt_opt_read   @hostif: c->x[0] context, c->x[1] out, c->x[2] binding, w3 = entry+0x20 key. Write the value
//                 index (u32) to out, leave c->x[0] = 1 (found) and return 1 to take the call over.
//  - rt_opt_store  @hostif: c->x[0] context, w1 = value index, c->x[2] binding, w3 = key. Map the index to the
//                 item's cfg value, cfg_set it, leave c->x[0] = 1 and return 1.
//  - rt_opt_msg_lookup @hostif: c->x[1] out object, c->x[3] pointer to the key string object. Fill the out object
//                 with the text, leave c->x[0] = 0 (found) and return 1; see the comment at the hook.
//  - the void ones @host: side effects only; leave c->nzcv, c->sp and x19-x29 alone.
//
// A row is one Item: a cfg key with the values it accepts, in cycle order. A toggle is a two-value choice
// ("0"/"1"), so both kinds share one value model - read returns the current value's index, store receives it -
// and kind only decides how the game's row widget renders (0 = toggle button, 1 = choice with arrows).
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cfg.h"
#include "cpu.h"
#include "host.h"
#include "layout.h"
#include "ui/game_options.h"
#include "ui/opt_msgkey.h"
#include "ui_text.h"
#include "utf16.h"

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
// Texts come from the menu catalog (ui_text.c), so the rows read in the menu language like the F1 overlay does.
// vtext[v] is the catalog id of value v's label, or NO_TEXT when the label is the literal vlit[v] (a number).
#define NO_TEXT (-1)
typedef struct {
    const char *cfg;
    const char *const *vals;
    int nvals;
    int kind;                 // 0 = toggle row, 1 = choice row
    int title, desc;          // catalog ids: the row title and the caption shown while it is focused
    const int *vtext;
    const char *const *vlit;
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

static const int t_onoff[] = {UI_MENU_OFF, UI_MENU_ON};
static const int t_upscaler[] = {UI_MENU_UPSCALER_FSR, UI_MENU_UPSCALER_BILINEAR};
static const int t_perf[] = {UI_MENU_PERF_OFF, UI_MENU_PERF_FPS, UI_MENU_PERF_FULL};
static const int t_aspect[] = {UI_MENU_ASPECT_FIT, UI_MENU_ASPECT_STRETCH};
static const int t_aniso[] = {UI_MENU_ANISO_GAME, NO_TEXT, NO_TEXT, NO_TEXT, NO_TEXT};
static const char *const l_aniso[] = {NULL, "2x", "4x", "8x", "16x"};
static const int t_mode[] = {UI_MENU_MODE_DOCKED, UI_MENU_MODE_HANDHELD};
static const int t_channels[] = {UI_MENU_CHANNELS_AUTO, UI_MENU_CHANNELS_STEREO, UI_MENU_CHANNELS_51};
static const int t_pad[] = {UI_MENU_PAD_LAYOUT_AUTO, UI_MENU_PAD_LAYOUT_NINTENDO, UI_MENU_PAD_LAYOUT_XBOX};
static const int t_mouse[] = {UI_MENU_MOUSE_STICK, UI_MENU_MOUSE_DIRECT};

static Item items[] = {
    // Screen
    {"TOTK_FULLSCREEN", v_onoff, 2, 0, UI_MENU_FULLSCREEN, UI_MENU_FULLSCREEN_DESC, t_onoff, NULL},
    {"TOTK_FXAA", v_onoff, 2, 0, UI_MENU_FXAA, UI_MENU_FXAA_DESC, t_onoff, NULL},
    {"TOTK_HDR", v_onoff, 2, 0, UI_MENU_HDR, UI_MENU_HDR_DESC, t_onoff, NULL},
    {"TOTK_UPSCALER", v_upscaler, 2, 1, UI_MENU_UPSCALER, UI_MENU_UPSCALER_DESC, t_upscaler, NULL},
    {"TOTK_PERF_OVERLAY", v_perf, 3, 1, UI_MENU_PERF_OVERLAY, UI_MENU_PERF_OVERLAY_DESC, t_perf, NULL},
    {"TOTK_ASPECT", v_aspect, 2, 1, UI_MENU_ASPECT, UI_MENU_ASPECT_DESC, t_aspect, NULL},
    {"TOTK_ANISO", v_aniso, 5, 1, UI_MENU_ANISO, UI_MENU_ANISO_DESC, t_aniso, l_aniso},
    // Game
    {"TOTK_ASSUME_FOCUS", v_onoff, 2, 0, UI_MENU_ASSUME_FOCUS, UI_MENU_ASSUME_FOCUS_DESC, t_onoff, NULL},
    {"TOTK_OPERATION_MODE", v_mode, 2, 1, UI_MENU_OPERATION_MODE, UI_MENU_OPERATION_MODE_DESC, t_mode, NULL},
    // Sound
    {"TOTK_AUDIO_CHANNELS", v_channels, 3, 1, UI_MENU_AUDIO_CHANNELS, UI_MENU_AUDIO_CHANNELS_DESC, t_channels, NULL},
    // Controls
    {"TOTK_MOTION", v_onoff, 2, 0, UI_MENU_MOTION, UI_MENU_MOTION_DESC, t_onoff, NULL},
    {"TOTK_MENU_CURSOR", v_onoff, 2, 0, UI_MENU_MENU_CURSOR, UI_MENU_MENU_CURSOR_DESC, t_onoff, NULL},
    {"TOTK_PAD_LAYOUT", v_pad, 3, 1, UI_MENU_PAD_LAYOUT, UI_MENU_PAD_LAYOUT_DESC, t_pad, NULL},
    {"TOTK_MOUSE_MODE", v_mouse, 2, 1, UI_MENU_MOUSE_MODE, UI_MENU_MOUSE_MODE_DESC, t_mouse, NULL},
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

// ---- the texts of our rows
//
// The message key for a row's title, value or caption is built from the option id (runtime/ui/opt_msgkey.h), so an
// id of ours picks the Item and the text comes from the menu catalog. Returns 1 and a NUL-terminated UTF-8 string in
// out when key names one of our texts; 0 when the key is not ours or names nothing (a value id past the item's last).
static int opt_text_utf8(const char *key, char *out, size_t cap) {
    OptMsgKey k;
    if (!cap || !opt_msgkey_parse(key, ITEM_ID_BASE, ITEM_ID_BASE + (uint32_t)NITEMS, &k)) return 0;
    const Item *it = &items[k.id - ITEM_ID_BASE];
    const char *text = NULL;
    switch (k.kind) {
    case OPT_MSG_TITLE:
        text = menu_text((UiTextId)it->title);
        break;
    case OPT_MSG_CAPTION:
        text = menu_text((UiTextId)it->desc);
        break;
    case OPT_MSG_VALUE:
        if (k.value >= (uint32_t)it->nvals) return 0;
        if (it->vtext[k.value] != NO_TEXT) text = menu_text((UiTextId)it->vtext[k.value]);
        else if (it->vlit) text = it->vlit[k.value];
        break;
    default:
        return 0;
    }
    if (!text || !*text) return 0;
    size_t n = strlen(text);
    if (n >= cap) n = cap - 1;
    memcpy(out, text, n);
    out[n] = 0;
    // The title and the caption carry the notes the F1 menu shows on a description: a setting the environment fixes,
    // or one that is only read at start. On a title that note is a short tag in brackets instead, because the caption
    // only shows while the row has the focus: a player must see at a glance which rows change only after a restart.
    if (k.kind == OPT_MSG_CAPTION) {
        const CfgMeta *m = cfg_meta(it->cfg);
        if (cfg_from_env(it->cfg)) snprintf(out + n, cap - n, "\n\n%s %s", menu_text(UI_MENU_SET_BY_ENV), it->cfg);
        else if (m && !m->live) snprintf(out + n, cap - n, "\n\n%s", menu_text(UI_MENU_AFTER_RESTART));
    } else if (k.kind == OPT_MSG_TITLE) {
        const CfgMeta *m = cfg_meta(it->cfg);
        if (m && !m->live && !cfg_from_env(it->cfg)) snprintf(out + n, cap - n, " [%s]", menu_text(UI_MENU_RESTART_TAG));
    }
    return 1;
}

// UTF-16 for the guest, which keeps message text as NUL-terminated char16. Returns the units written before the NUL
// (at most cap - 1), or 0 when key is not one of ours. rt_opt_msg_lookup below hands this text to the game.
size_t rt_opt_message_utf16(const char *key, uint16_t *out, size_t cap) {
    char utf8[512];
    if (!cap || !opt_text_utf8(key, utf8, sizeof utf8)) return 0;
    return utf8_to_utf16(utf8, out, cap);
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

// Diagnostic for finding the screen's owner (plan Phase 3): logs the return address and the frame-pointer chain
// (x29 -> {previous x29, saved lr}) above a hook, main-relative.
static void log_callers(const char *what, Cpu *c) {
    uint64_t base = MOD_MAIN_BASE;
    char chain[256];
    size_t n = 0;
    uint64_t fp = c->x[29];
    for (int d = 0; d < 8 && fp && n + 24 < sizeof chain; d++) {
        uint64_t saved = rd64(fp + 8);
        n += (size_t)snprintf(chain + n, sizeof chain - n, " %llx", (unsigned long long)(saved - base));
        uint64_t next = rd64(fp);
        if (next <= fp) break;
        fp = next;
    }
    chain[n] = 0;
    rt_log("native options: %s, x0=%llx lr=main+0x%llx frames:%s", what, (unsigned long long)c->x[0],
           (unsigned long long)(c->x[30] - base), chain);
}

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
    if (!enabled()) return;
    log_callers("screen created", c);
}

// The screen's destructor body (main+0x218ba60): who tears the Options screen down on B or when its owner closes.
void rt_opt_screen_close(Cpu *c) {
    if (!enabled()) return;
    log_callers("screen destroyed", c);
}

// Row population (main+0x218c5c0, slot 88): runs once per opening, after the rows exist and before their texts are
// filled - not once per frame, as its earlier "tick" name suggested. Nothing to do yet: the original fills our rows'
// texts through the message hook like the game's own.
void rt_opt_tick(Cpu *c) {
    (void)c;
}

// The decide dispatcher (slot49): section buttons and page changes need this, but they need a screen rebuild to
// go with them (SavageWind closes and reopens the screen on a page change), so it stays a no-op until then. With
// it empty the injected rows still toggle and cycle through the game's own kind-0/kind-1 handlers, which reach
// rt_opt_store above.
void rt_opt_decide(Cpu *c) {
    if (!enabled()) return;
    rt_log("native options: decide, screen=%llx lr=main+0x%llx", (unsigned long long)c->x[0],
           (unsigned long long)(c->x[30] - MOD_MAIN_BASE));
}

// The message lookup (main+0xb3e3c0): row titles, value texts and captions come from a sheet key built off the option
// id, so our synthetic ids resolve to nothing in the game's sheet and the rows would read blank. This hook answers
// the lookup for our keys and leaves every other key to the game.
//
// Contract, read from the lifted C of 1.4.3 (plan "Message apply path"): the callers (main+0x212c6ac title,
// main+0x212c0e8 value text, main+0x218c748 caption) build the key with main+0xf4444 into a small string object, then
// reach main+0xb3e3c0(x0 = message manager, x1 = out, x2 = &sheet-name pointer, x3 = &key string object). The key
// characters are at [[x3] + 0]. The lookup fills the out object {char16 *text @+0, u32 length @+8, u64 handle @+0x10}
// and returns 0 for a text it found; main+0xba1730 then hands it to the row's pane, and main+0x610348 passes
// [out+0] and [out+8] to the pane's set-text virtual and stores [out+0x10] at pane+0x160. The game's own "no text"
// results use ~0 for the handle, so that is what we write. A taken hook leaves 0 in c->x[0] (the success the caller
// expects), writes only through x1, and touches no callee-saved register.
//
// Assumed, not yet seen in game: the text is NUL-terminated UTF-16 and the length counts char16 units - what the
// game's message sheets hold. The in-game smoke test (plan 4.2) is what confirms it; until then the feature stays
// behind TOTK_NATIVE_OPTIONS (default 0).
//
// The game may keep the pointer in the pane rather than copy the text, so each key has its own buffer that lives for
// the whole run (a ring of reused buffers could overwrite a text still on screen). A key is rendered again on every
// call, so a language change shows up the next time the screen builds its rows.
#define MSG_UNITS 512
#define MSG_MAXV 8
enum { MSG_SLOTS = NITEMS * (2 + MSG_MAXV) };
static uint16_t g_msg[MSG_SLOTS][MSG_UNITS];

// Slot of a parsed key: title and caption of each item first, then its values.
static int msg_slot(const OptMsgKey *k) {
    int i = (int)(k->id - ITEM_ID_BASE);
    switch (k->kind) {
    case OPT_MSG_TITLE: return i * (2 + MSG_MAXV);
    case OPT_MSG_CAPTION: return i * (2 + MSG_MAXV) + 1;
    case OPT_MSG_VALUE:
        if (k->value >= MSG_MAXV) return -1;
        return i * (2 + MSG_MAXV) + 2 + (int)k->value;
    default: return -1;
    }
}

int rt_opt_msg_lookup(Cpu *c) {
    if (!enabled()) return 0;
    uint64_t out = c->x[1], keyobj = c->x[3];
    if (out < 0x10000 || keyobj < 0x10000) return 0;
    uint64_t kp = rd64(keyobj);
    if (kp < 0x10000 || kp >= (1ull << 47)) return 0;

    // The key string object holds at most 31 characters (main+0xf4444's capacity is 0x20); copy it bounded.
    char key[32];
    size_t n = 0;
    while (n < sizeof key - 1) {
        char ch;
        memcpy(&ch, (const void *)(uintptr_t)(kp + n), 1);
        if (!ch) break;
        key[n++] = ch;
    }
    key[n] = 0;

    OptMsgKey k;
    if (!opt_msgkey_parse(key, ITEM_ID_BASE, ITEM_ID_BASE + (uint32_t)NITEMS, &k)) return 0;
    int slot = msg_slot(&k);
    if (slot < 0 || slot >= MSG_SLOTS) return 0;

    size_t units = rt_opt_message_utf16(key, g_msg[slot], MSG_UNITS);
    if (!units) return 0;   // one of our ids with no text (a value past the last): let the game answer

    wr64(out + 0x0, (uint64_t)(uintptr_t)g_msg[slot]);
    wr32(out + 0x8, (uint32_t)units);
    wr64(out + 0x10, ~0ull);
    c->x[0] = 0;
    return 1;
}
