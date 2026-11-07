// The core settings inside TotK's own Options screen, the way SavageWindRecomp puts them in BOTW's
// (refs/SavageWindRecomp runtime/ui/game_options.mm), but on ONE page: a row per setting, grouped by a header row per
// section (Screen, Graphics, Sound, Controls, Game), with the game's own rows after them. There is no page switch: the
// screen is built once per opening, which is all the machinery a single list needs. The plan and the confirmed 1.4.3
// addresses live in docs/native-options-plan.md: the option table sits behind the singleton at
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
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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
// values in the order the row cycles them. A slider of the F1 menu becomes a choice over a short list of steps
// (a row cycles values, it has no slider), and such a numeric item reads as the step nearest to the stored value, so a
// value the player typed into totk.ini between two steps still shows and the next press moves off it. The keys left out
// are the ones that need more than a row of values: the bind tables and their capture, the amiibo browser, the GPU
// picker, and the Advanced, Mods and Cheats tabs - those stay on F1.
// Texts come from the menu catalog (ui_text.c), so the rows read in the menu language like the F1 overlay does. A
// value's label is the catalog text vtext[v], else the literal vlit[v], else the value followed by unit.
#define NO_TEXT (-1)
typedef struct {
    const char *cfg;
    const char *const *vals;
    int nvals;
    int kind;                 // 0 = toggle row, 1 = choice row
    int sect;                 // the section the row sits under (SECT_*)
    int title, desc;          // catalog ids: the row title and the caption shown while it is focused
    const int *vtext;         // per value: a catalog id or NO_TEXT; NULL: no catalog labels
    const char *const *vlit;  // per value: a literal label or NULL; NULL: none
    const char *unit;         // appended to the value for a label with neither; NULL: none
    int num;                  // numeric: the row reads the step nearest to the stored value
    const char *fallback;     // cfg key whose value counts while this one is unset
} Item;

enum { SECT_SCREEN, SECT_GRAPHICS, SECT_SOUND, SECT_CONTROLS, SECT_GAME, NSECT };
static const int sect_title[NSECT] = {UI_MENU_TAB_SCREEN, UI_MENU_SECT_GRAPHICS, UI_MENU_TAB_SOUND, UI_MENU_TAB_CONTROLS,
                                      UI_MENU_TAB_GAME};

static const char *const v_onoff[] = {"0", "1"};
static const int t_onoff[] = {UI_MENU_OFF, UI_MENU_ON};

static const char *const v_window[] = {"1280x720", "1366x768", "1600x900", "1920x1080", "2560x1440", "3200x1800", "3840x2160"};
static const char *const l_window[] = {"1280 × 720", "1366 × 768", "1600 × 900", "1920 × 1080", "2560 × 1440", "3200 × 1800", "3840 × 2160"};
static const char *const v_present[] = {"mailbox", "fifo", "immediate"};
static const int t_present[] = {UI_MENU_PRESENT_MAILBOX, UI_MENU_PRESENT_VSYNC, UI_MENU_PRESENT_IMMEDIATE};
static const char *const v_fps[] = {"30", "40", "45", "60", "90", "120", "144", "display"};
static const int t_fps[] = {UI_MENU_FPS_30, UI_MENU_FPS_40, UI_MENU_FPS_45, UI_MENU_FPS_60, UI_MENU_FPS_90,
                            UI_MENU_FPS_120, UI_MENU_FPS_144, UI_MENU_FPS_DISPLAY};
static const char *const v_aspect[] = {"fit", "stretch"};
static const int t_aspect[] = {UI_MENU_ASPECT_FIT, UI_MENU_ASPECT_STRETCH};
static const char *const v_fov[] = {"70", "75", "80", "85", "90", "95", "100", "105", "110", "115", "120", "125", "130", "135", "140"};
static const char *const v_perf[] = {"off", "fps", "full"};
static const int t_perf[] = {UI_MENU_PERF_OFF, UI_MENU_PERF_FPS, UI_MENU_PERF_FULL};
static const char *const v_outres[] = {"1280x720", "1600x900", "1920x1080", "2560x1440", "3200x1800", "3840x2160",
                                       "2560x1080", "3440x1440", "3840x1600", "5120x2160", "3840x1080", "5120x1440",
                                       "1280x800", "1680x1050", "1920x1200", "2560x1600", "1920x1280", "2160x1440",
                                       "2256x1504", "2880x1920", "3000x2000"};
static const char *const l_outres[] = {"1280 × 720", "1600 × 900", "1920 × 1080", "2560 × 1440", "3200 × 1800", "3840 × 2160",
                                       "2560 × 1080", "3440 × 1440", "3840 × 1600", "5120 × 2160", "3840 × 1080", "5120 × 1440",
                                       "1280 × 800", "1680 × 1050", "1920 × 1200", "2560 × 1600", "1920 × 1280", "2160 × 1440",
                                       "2256 × 1504", "2880 × 1920", "3000 × 2000"};
static const char *const v_rscale[] = {"game", "50", "67", "75", "100", "125", "150", "200"};
static const int t_rscale[] = {UI_MENU_RENDER_SCALE_GAME, NO_TEXT, NO_TEXT, NO_TEXT, NO_TEXT, NO_TEXT, NO_TEXT, NO_TEXT};

static const char *const v_upscaler[] = {"fsr", "bilinear"};
static const int t_upscaler[] = {UI_MENU_UPSCALER_FSR, UI_MENU_UPSCALER_BILINEAR};
static const char *const v_pct10[] = {"0", "10", "20", "30", "40", "50", "60", "70", "80", "90", "100"};
static const char *const v_aniso[] = {"game", "2", "4", "8", "16"};
static const int t_aniso[] = {UI_MENU_ANISO_GAME, NO_TEXT, NO_TEXT, NO_TEXT, NO_TEXT};
static const char *const l_aniso[] = {NULL, "2x", "4x", "8x", "16x"};
static const char *const v_sres[] = {"game", "256", "512", "768", "1024", "1536", "2048"};
static const int t_sres[] = {UI_MENU_SHADOW_RES_GAME, NO_TEXT, NO_TEXT, NO_TEXT, NO_TEXT, NO_TEXT, NO_TEXT};
static const char *const v_sscale[] = {"1", "2", "3", "4"};
static const char *const l_sscale[] = {"960 (1x)", "1920 (2x)", "2880 (3x)", "3840 (4x)"};
static const char *const v_srange[] = {"100", "150", "200", "300"};
static const char *const l_srange[] = {"160 m (1x)", "240 m (1.5x)", "320 m (2x)", "480 m (3x)"};
static const char *const v_pcss[] = {"0", "20", "40", "60", "80", "100", "120", "140", "160", "180", "200"};
static const char *const v_dlss[] = {"dlaa", "quality", "balanced", "performance", "ultra_performance"};
static const int t_dlss[] = {UI_MENU_DLSS_DLAA, UI_MENU_DLSS_QUALITY, UI_MENU_DLSS_BALANCED, UI_MENU_DLSS_PERFORMANCE,
                             UI_MENU_DLSS_ULTRA};
static const char *const v_dlsspreset[] = {"default", "k", "j"};
static const int t_dlsspreset[] = {UI_MENU_DLSS_PRESET_DEFAULT, UI_MENU_DLSS_PRESET_K, UI_MENU_DLSS_PRESET_J};
static const char *const v_dlsschars[] = {"0", "35", "65", "100"};
static const int t_dlsschars[] = {UI_MENU_DLSS_CHARS_OFF, UI_MENU_DLSS_CHARS_LIGHT, UI_MENU_DLSS_CHARS_MEDIUM,
                                  UI_MENU_DLSS_CHARS_FULL};
static const char *const v_cpreset[] = {"default", "vivid", "clarity", "custom"};
static const int t_cpreset[] = {UI_MENU_COLOR_DEFAULT, UI_MENU_COLOR_VIVID, UI_MENU_COLOR_CLARITY, UI_MENU_COLOR_CUSTOM};
static const char *const v_bright[] = {"-30", "-25", "-20", "-15", "-10", "-5", "0", "5", "10", "15", "20", "25", "30"};
static const char *const v_contrast[] = {"70", "80", "90", "100", "110", "120", "130", "140", "150"};
static const char *const v_satur[] = {"50", "60", "70", "80", "90", "100", "110", "120", "130", "140", "150", "160", "170", "180", "190", "200"};
static const char *const v_gamma[] = {"0.8", "0.85", "0.9", "0.95", "1", "1.05", "1.1", "1.15", "1.2"};
static const char *const v_peak[] = {"400", "600", "800", "1000", "1200", "1500", "2000", "3000", "4000", "10000"};
static const char *const v_nits[] = {"80", "100", "120", "150", "203", "250", "300", "400", "500"};

static const char *const v_volume[] = {"0", "5", "10", "15", "20", "25", "30", "35", "40", "45", "50", "55", "60", "65", "70", "75", "80", "85", "90", "95", "100"};
static const char *const v_channels[] = {"auto", "stereo", "5.1"};
static const int t_channels[] = {UI_MENU_CHANNELS_AUTO, UI_MENU_CHANNELS_STEREO, UI_MENU_CHANNELS_51};

static const char *const v_pad[] = {"auto", "nintendo", "xbox"};
static const int t_pad[] = {UI_MENU_PAD_LAYOUT_AUTO, UI_MENU_PAD_LAYOUT_NINTENDO, UI_MENU_PAD_LAYOUT_XBOX};
static const char *const v_prompts[] = {"nintendo", "xbox", "pc"};
static const int t_prompts[] = {UI_MENU_PROMPTS_NINTENDO, UI_MENU_PROMPTS_XBOX, UI_MENU_PROMPTS_PC};
static const char *const v_rstr[] = {"0", "20", "40", "60", "80", "100", "130", "160", "200", "250", "300"};
static const char *const v_rcurve[] = {"0.2", "0.3", "0.4", "0.5", "0.6", "0.7", "0.8", "0.9", "1", "1.2", "1.5"};
static const char *const v_mouse[] = {"stick", "direct"};
static const int t_mouse[] = {UI_MENU_MOUSE_STICK, UI_MENU_MOUSE_DIRECT};
static const char *const v_msens[] = {"0", "0.25", "0.5", "0.75", "1", "1.25", "1.5", "2", "3", "4", "5", "7", "10"};
static const char *const v_walkkey[] = {"none", "lshift", "lalt", "lctrl", "tab", "capslock"};
static const int t_walkkey[] = {UI_MENU_OFF, NO_TEXT, NO_TEXT, NO_TEXT, NO_TEXT, NO_TEXT};
static const char *const l_walkkey[] = {NULL, "Shift", "Alt", "Ctrl", "Tab", "Caps Lock"};
static const char *const v_wscale[] = {"0.2", "0.3", "0.4", "0.51", "0.6", "0.7", "0.8", "0.9"};
static const char *const v_dead[] = {"0", "2", "4", "6", "8", "10", "12", "15", "20", "25", "30", "40", "50"};
static const char *const v_ssens[] = {"0.1", "0.25", "0.5", "0.75", "1", "1.25", "1.5", "2", "2.5", "3"};

static const char *g_lang_vals[17] = {""}, *g_lang_names[17];   // the game language list, filled by install
static const char *const v_mode[] = {"docked", "handheld"};
static const int t_mode[] = {UI_MENU_MODE_DOCKED, UI_MENU_MODE_HANDHELD};
static const char *const v_menulang[] = {"", "en", "ru", "pt-BR", "zh-Hans"};
static const int t_menulang[] = {UI_MENU_MENU_LANG_AUTO, UI_MENU_LANGNAME_EN, UI_MENU_LANGNAME_RU, UI_MENU_LANGNAME_PT_BR,
                                 UI_MENU_LANGNAME_ZH_HANS};

#define N(a) ((int)(sizeof a / sizeof *a))
#define TOGGLE(cfg, sect, title, desc) {cfg, v_onoff, 2, 0, sect, title, desc, t_onoff, NULL, NULL, 0, NULL}
#define CATALOG(cfg, sect, v, t, title, desc) {cfg, v, N(v), 1, sect, title, desc, t, NULL, NULL, 0, NULL}
#define LABELS(cfg, sect, v, l, title, desc) {cfg, v, N(v), 1, sect, title, desc, NULL, l, NULL, 0, NULL}
#define STEPS(cfg, sect, v, unit, title, desc) {cfg, v, N(v), 1, sect, title, desc, NULL, NULL, unit, 1, NULL}

// The rows of one section are contiguous and the sections are in SECT_* order (install checks it).
static Item items[] = {
    // Screen
    TOGGLE("TOTK_FULLSCREEN", SECT_SCREEN, UI_MENU_FULLSCREEN, UI_MENU_FULLSCREEN_DESC),
    LABELS("TOTK_WINDOW_SIZE", SECT_SCREEN, v_window, l_window, UI_MENU_WINDOW_SIZE, UI_MENU_WINDOW_SIZE_DESC),
    CATALOG("TOTK_VK_PRESENT", SECT_SCREEN, v_present, t_present, UI_MENU_PRESENT, UI_MENU_PRESENT_DESC),
    CATALOG("TOTK_FPS_LIMIT", SECT_SCREEN, v_fps, t_fps, UI_MENU_FPS_LIMIT, UI_MENU_FPS_LIMIT_DESC),
    CATALOG("TOTK_ASPECT", SECT_SCREEN, v_aspect, t_aspect, UI_MENU_ASPECT, UI_MENU_ASPECT_DESC),
    STEPS("TOTK_FOV", SECT_SCREEN, v_fov, " %", UI_MENU_FOV, UI_MENU_FOV_DESC),
    CATALOG("TOTK_PERF_OVERLAY", SECT_SCREEN, v_perf, t_perf, UI_MENU_PERF_OVERLAY, UI_MENU_PERF_OVERLAY_DESC),
    LABELS("TOTK_OUTPUT_RES", SECT_SCREEN, v_outres, l_outres, UI_MENU_OUTPUT_RES, UI_MENU_OUTPUT_RES_DESC),
    {"TOTK_RENDER_SCALE", v_rscale, N(v_rscale), 1, SECT_SCREEN, UI_MENU_RENDER_SCALE, UI_MENU_RENDER_SCALE_DESC, t_rscale,
     NULL, " %", 0, NULL},
    // Graphics
    CATALOG("TOTK_UPSCALER", SECT_GRAPHICS, v_upscaler, t_upscaler, UI_MENU_UPSCALER, UI_MENU_UPSCALER_DESC),
    STEPS("TOTK_SHARPNESS", SECT_GRAPHICS, v_pct10, " %", UI_MENU_SHARPNESS, UI_MENU_SHARPNESS_DESC),
    TOGGLE("TOTK_FXAA", SECT_GRAPHICS, UI_MENU_FXAA, UI_MENU_FXAA_DESC),
    {"TOTK_ANISO", v_aniso, N(v_aniso), 1, SECT_GRAPHICS, UI_MENU_ANISO, UI_MENU_ANISO_DESC, t_aniso, l_aniso, NULL, 0, NULL},
    {"TOTK_SHADOW_RES", v_sres, N(v_sres), 1, SECT_GRAPHICS, UI_MENU_SHADOW_RES, UI_MENU_SHADOW_RES_DESC, t_sres, NULL, "", 0,
     NULL},
    LABELS("TOTK_SHADOW_SCALE", SECT_GRAPHICS, v_sscale, l_sscale, UI_MENU_SHADOW_SCALE, UI_MENU_SHADOW_SCALE_DESC),
    LABELS("TOTK_SHADOW_RANGE", SECT_GRAPHICS, v_srange, l_srange, UI_MENU_SHADOW_RANGE, UI_MENU_SHADOW_RANGE_DESC),
    STEPS("TOTK_PCSS", SECT_GRAPHICS, v_pcss, " %", UI_MENU_PCSS, UI_MENU_PCSS_DESC),
    STEPS("TOTK_CONTACT_SHADOWS", SECT_GRAPHICS, v_pct10, " %", UI_MENU_CONTACT_SHADOWS, UI_MENU_CONTACT_SHADOWS_DESC),
    TOGGLE("TOTK_DLAA", SECT_GRAPHICS, UI_MENU_DLAA, UI_MENU_DLAA_DESC),
    CATALOG("TOTK_DLSS", SECT_GRAPHICS, v_dlss, t_dlss, UI_MENU_DLSS, UI_MENU_DLSS_DESC),
    CATALOG("TOTK_DLSS_PRESET", SECT_GRAPHICS, v_dlsspreset, t_dlsspreset, UI_MENU_DLSS_PRESET, UI_MENU_DLSS_PRESET_DESC),
    CATALOG("TOTK_DLSS_CHAR_MASK", SECT_GRAPHICS, v_dlsschars, t_dlsschars, UI_MENU_DLSS_CHARS, UI_MENU_DLSS_CHARS_DESC),
    CATALOG("TOTK_COLOR_PRESET", SECT_GRAPHICS, v_cpreset, t_cpreset, UI_MENU_COLOR_PRESET, UI_MENU_COLOR_PRESET_DESC),
    STEPS("TOTK_COLOR_BRIGHTNESS", SECT_GRAPHICS, v_bright, " %", UI_MENU_COLOR_BRIGHTNESS, UI_MENU_COLOR_BRIGHTNESS_DESC),
    STEPS("TOTK_COLOR_CONTRAST", SECT_GRAPHICS, v_contrast, " %", UI_MENU_COLOR_CONTRAST, UI_MENU_COLOR_CONTRAST_DESC),
    STEPS("TOTK_COLOR_SATURATION", SECT_GRAPHICS, v_satur, " %", UI_MENU_COLOR_SATURATION, UI_MENU_COLOR_SATURATION_DESC),
    STEPS("TOTK_COLOR_GAMMA", SECT_GRAPHICS, v_gamma, "", UI_MENU_COLOR_GAMMA, UI_MENU_COLOR_GAMMA_DESC),
    TOGGLE("TOTK_HDR", SECT_GRAPHICS, UI_MENU_HDR, UI_MENU_HDR_DESC),
    STEPS("TOTK_HDR_PEAK_NITS", SECT_GRAPHICS, v_peak, " nits", UI_MENU_HDR_PEAK, UI_MENU_HDR_PEAK_DESC),
    STEPS("TOTK_HDR_GAME_NITS", SECT_GRAPHICS, v_nits, " nits", UI_MENU_HDR_GAME, UI_MENU_HDR_GAME_DESC),
    STEPS("TOTK_HDR_UI_NITS", SECT_GRAPHICS, v_nits, " nits", UI_MENU_HDR_UI, UI_MENU_HDR_UI_DESC),
    STEPS("TOTK_HDR_HUE", SECT_GRAPHICS, v_pct10, " %", UI_MENU_HDR_HUE, UI_MENU_HDR_HUE_DESC),
    // Sound
    STEPS("TOTK_VOLUME", SECT_SOUND, v_volume, " %", UI_MENU_VOLUME, UI_MENU_VOLUME_DESC),
    CATALOG("TOTK_AUDIO_CHANNELS", SECT_SOUND, v_channels, t_channels, UI_MENU_AUDIO_CHANNELS, UI_MENU_AUDIO_CHANNELS_DESC),
    // Controls
    CATALOG("TOTK_PAD_LAYOUT", SECT_CONTROLS, v_pad, t_pad, UI_MENU_PAD_LAYOUT, UI_MENU_PAD_LAYOUT_DESC),
    CATALOG("TOTK_PROMPTS", SECT_CONTROLS, v_prompts, t_prompts, UI_MENU_PROMPTS, UI_MENU_PROMPTS_DESC),
    TOGGLE("TOTK_MOTION", SECT_CONTROLS, UI_MENU_MOTION, UI_MENU_MOTION_DESC),
    STEPS("TOTK_RUMBLE_STRENGTH", SECT_CONTROLS, v_rstr, " %", UI_MENU_RUMBLE_STRENGTH, UI_MENU_RUMBLE_STRENGTH_DESC),
    STEPS("TOTK_RUMBLE_CURVE", SECT_CONTROLS, v_rcurve, "", UI_MENU_RUMBLE_CURVE, UI_MENU_RUMBLE_CURVE_DESC),
    TOGGLE("TOTK_RUMBLE_TRIGGERS", SECT_CONTROLS, UI_MENU_RUMBLE_TRIGGERS, UI_MENU_RUMBLE_TRIGGERS_DESC),
    CATALOG("TOTK_MOUSE_MODE", SECT_CONTROLS, v_mouse, t_mouse, UI_MENU_MOUSE_MODE, UI_MENU_MOUSE_MODE_DESC),
    STEPS("TOTK_MOUSE_SENS", SECT_CONTROLS, v_msens, "", UI_MENU_MOUSE_SENS, UI_MENU_MOUSE_SENS_DESC),
    TOGGLE("TOTK_MENU_CURSOR", SECT_CONTROLS, UI_MENU_MENU_CURSOR, UI_MENU_MENU_CURSOR_DESC),
    {"TOTK_WALK_KEY", v_walkkey, N(v_walkkey), 1, SECT_CONTROLS, UI_MENU_WALK_KEY, UI_MENU_WALK_KEY_DESC, t_walkkey, l_walkkey,
     NULL, 0, NULL},
    STEPS("TOTK_WALK_SCALE", SECT_CONTROLS, v_wscale, "", UI_MENU_WALK_SCALE, UI_MENU_WALK_SCALE_DESC),
    {"TOTK_LSTICK_DEADZONE", v_dead, N(v_dead), 1, SECT_CONTROLS, UI_MENU_LSTICK_DEADZONE, UI_MENU_STICK_DEADZONE_DESC, NULL,
     NULL, " %", 1, "TOTK_STICK_DEADZONE"},
    {"TOTK_RSTICK_DEADZONE", v_dead, N(v_dead), 1, SECT_CONTROLS, UI_MENU_RSTICK_DEADZONE, UI_MENU_STICK_DEADZONE_DESC, NULL,
     NULL, " %", 1, "TOTK_STICK_DEADZONE"},
    STEPS("TOTK_STICK_SENS", SECT_CONTROLS, v_ssens, "", UI_MENU_STICK_SENS, UI_MENU_STICK_SENS_DESC),
    // Game (the F1 Game and Menu tabs)
    {"TOTK_LANG", g_lang_vals, 1, 1, SECT_GAME, UI_MENU_GAME_LANG, UI_MENU_GAME_LANG_DESC, NULL, g_lang_names, NULL, 0, NULL},
    CATALOG("TOTK_OPERATION_MODE", SECT_GAME, v_mode, t_mode, UI_MENU_OPERATION_MODE, UI_MENU_OPERATION_MODE_DESC),
    TOGGLE("TOTK_ASSUME_FOCUS", SECT_GAME, UI_MENU_ASSUME_FOCUS, UI_MENU_ASSUME_FOCUS_DESC),
    CATALOG("TOTK_MENU_LANG", SECT_GAME, v_menulang, t_menulang, UI_MENU_MENU_LANG, UI_MENU_MENU_LANG_DESC),
    TOGGLE("TOTK_DISCORD_RPC", SECT_GAME, UI_MENU_DISCORD_RPC, UI_MENU_DISCORD_RPC_DESC),
};
enum { NITEMS = (int)(sizeof items / sizeof items[0]) };

// Our option ids and value keys are handed out from ranges the game does not use, so entry-by-id and the read/
// store dispatch never mistake one of ours for a game entry. The value key is what read/store see in w3; the id
// is what entry-by-id and the "10%02d" message key use. The section header rows follow the items in both ranges.
#define ITEM_ID_BASE 0x8000u
#define ITEM_KEY_BASE 0x4e000000u
static uint32_t item_id(int i) { return ITEM_ID_BASE + (uint32_t)i; }
static uint32_t item_key(int i) { return ITEM_KEY_BASE + (uint32_t)i; }
static int item_by_key(uint32_t key) {
    for (int i = 0; i < NITEMS; i++)
        if (item_key(i) == key) return i;
    return -1;
}

// A section header is a toggle row that is only a label: its value reads as off and a press changes nothing.
#define SECT_KEY_BASE (ITEM_KEY_BASE + 0x100u)
static uint32_t sect_id(int s) { return ITEM_ID_BASE + (uint32_t)NITEMS + (uint32_t)s; }
static uint32_t sect_key(int s) { return SECT_KEY_BASE + (uint32_t)s; }
static int sect_by_key(uint32_t key) { return key >= SECT_KEY_BASE && key < SECT_KEY_BASE + NSECT ? (int)(key - SECT_KEY_BASE) : -1; }

// ---- the texts of our rows
//
// The message key for a row's title, value or caption is built from the option id (runtime/ui/opt_msgkey.h), so an
// id of ours picks the Item and the text comes from the menu catalog. Returns 1 and a NUL-terminated UTF-8 string in
// out when key names one of our texts; 0 when the key is not ours or names nothing (a value id past the item's last).

// The label of value v: its catalog text, else its literal, else the value and the unit.
static const char *value_label(const Item *it, int v, char *buf, size_t cap) {
    if (it->vtext && it->vtext[v] != NO_TEXT) return menu_text((UiTextId)it->vtext[v]);
    if (it->vlit && it->vlit[v]) return it->vlit[v];
    if (it->unit) {
        snprintf(buf, cap, "%s%s", it->vals[v], it->unit);
        return buf;
    }
    return it->vals[v];
}

static int opt_text_utf8(const char *key, char *out, size_t cap) {
    OptMsgKey k;
    if (!cap || !opt_msgkey_parse(key, ITEM_ID_BASE, ITEM_ID_BASE + (uint32_t)NITEMS + NSECT, &k)) return 0;
    if (k.id >= ITEM_ID_BASE + (uint32_t)NITEMS) {   // a section header: only its title has a text
        if (k.kind != OPT_MSG_TITLE) return 0;
        const char *name = menu_text((UiTextId)sect_title[k.id - ITEM_ID_BASE - (uint32_t)NITEMS]);
        if (!name || !*name) return 0;
        snprintf(out, cap, "\xe2\x80\x94 %s \xe2\x80\x94", name);   // em dashes around the name
        return 1;
    }
    const Item *it = &items[k.id - ITEM_ID_BASE];
    const char *text = NULL;
    char lit[64];
    switch (k.kind) {
    case OPT_MSG_TITLE:
        text = menu_text((UiTextId)it->title);
        break;
    case OPT_MSG_CAPTION:
        text = menu_text((UiTextId)it->desc);
        break;
    case OPT_MSG_VALUE:
        if (k.value >= (uint32_t)it->nvals) return 0;
        text = value_label(it, (int)k.value, lit, sizeof lit);
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

// ---- values: which index a stored string is
//
// An exact string match first; a numeric item then takes the step nearest to the stored number. -1: no match.
static int index_of(const Item *it, const char *cur) {
    for (int v = 0; v < it->nvals; v++)
        if (!strcmp(cur, it->vals[v])) return v;
    if (!it->num || !*cur) return -1;
    double x = strtod(cur, NULL), best = 1e300;
    int at = -1;
    for (int v = 0; v < it->nvals; v++) {
        double d = fabs(strtod(it->vals[v], NULL) - x);
        if (d < best) best = d, at = v;
    }
    return at;
}

// The default value index for an item, from the metas table's default string (the fallback key's when the item has
// none of its own, as the per-stick dead zones do).
static uint32_t item_default(const Item *it) {
    const CfgMeta *m = cfg_meta(it->cfg);
    if ((!m || !m->def || !*m->def) && it->fallback) m = cfg_meta(it->fallback);
    int at = m && m->def ? index_of(it, m->def) : -1;
    return at < 0 ? 0u : (uint32_t)at;
}

// The index of the value in effect: the key's, or its fallback's while the key is unset.
static uint32_t item_index(const Item *it) {
    const char *cur = cfg_value(it->cfg);
    if (!*cur && it->fallback) cur = cfg_value(it->fallback);
    int at = index_of(it, cur);
    return at < 0 ? item_default(it) : (uint32_t)at;
}

// ---- install: capture the game's entries once, then present ours in front of them
//
// Our rows go first so they land inside the layout's widget budget: build_rows looks up Pa_ToggleBtn_%02d by row
// index and its null check (cbz) is fail-soft, so rows past the last instance the bgyml defines render invisible
// rather than crash. Putting ours first means a budget that only fits the game's original rows still shows every
// one of ours and pushes the tail of the game's out of sight. The view is one list: a header row per section, its
// items, then a copy of the game's own entries. It is built once and written on every build.
#define MAX_ENTRIES 512
#define MSG_MAXV 24   // the most values an item can have: the message slots and the value-id arrays are sized by it
static Ent g_rows[MAX_ENTRIES];
static uint32_t g_rows_n;
static uint32_t g_our_vals[NITEMS][MSG_MAXV];
static uint32_t g_sect_vals[2] = {0, 1};
static int g_ready;

static int enabled(void) { return cfg_bool("TOTK_NATIVE_OPTIONS"); }

// The game languages the running build ships are only known at run time: fill the language item's list (the automatic
// choice first, then one value per Mals language file, each under its own name).
static void fill_languages(void) {
    UiGameLang langs[16];
    int n = ui_text_game_langs(rt_game_version, langs, 16);
    g_lang_vals[0] = "";
    g_lang_names[0] = menu_text(UI_MENU_GAME_LANG_AUTO);
    for (int i = 0; i < n; i++) {
        g_lang_vals[i + 1] = langs[i].code;
        g_lang_names[i + 1] = menu_text(langs[i].name);
    }
    for (int i = 0; i < NITEMS; i++)
        if (!strcmp(items[i].cfg, "TOTK_LANG")) items[i].nvals = n + 1;
}

// Fills g_rows with the sections' rows followed by a copy of the game's. Returns 0 (and leaves g_ready clear) when
// the table is not in a shape to extend or an item does not fit its limits, which leaves the screen untouched.
static int install(uint64_t table) {
    uint32_t gn = rd32(table + TBL_COUNT);
    uint64_t ge = rd64(table + TBL_ENTRIES);
    if (!ge || !gn || gn + (uint32_t)NITEMS + NSECT > MAX_ENTRIES) return 0;
    fill_languages();
    for (int i = 0; i < NITEMS; i++)
        if (items[i].nvals < 1 || items[i].nvals > MSG_MAXV || items[i].sect < 0 || items[i].sect >= NSECT ||
            (i && items[i].sect < items[i - 1].sect)) {
            rt_log("native options: item %s breaks a limit (values %d, section %d); the rows stay unswapped",
                   items[i].cfg, items[i].nvals, items[i].sect);
            return 0;
        }
    uint32_t n = 0;
    for (int s = 0; s < NSECT; s++) {
        Ent *h = &g_rows[n++];
        memset(h, 0, sizeof *h);
        h->id = sect_id(s);
        h->kind = 0;
        h->nvals = 2;
        h->vals = (uint64_t)(uintptr_t)g_sect_vals;
        h->key = sect_key(s);
        for (int i = 0; i < NITEMS; i++) {
            if (items[i].sect != s) continue;
            const Item *it = &items[i];
            Ent *e = &g_rows[n++];
            memset(e, 0, sizeof *e);
            e->id = item_id(i);
            e->kind = (uint32_t)it->kind;
            e->def = item_default(it);
            e->nvals = (uint32_t)it->nvals;
            // The value ids are 0..n-1, so an id and the row's value index are the same number: store's w1 and our
            // read's out are both that index, and mapping it back to a cfg string is a table lookup.
            for (int v = 0; v < it->nvals; v++) g_our_vals[i][v] = (uint32_t)v;
            e->vals = (uint64_t)(uintptr_t)g_our_vals[i];
            e->key = item_key(i);
            e->cond = 0;   // always shown: count() counts it without the predicate
        }
    }
    memcpy(g_rows + n, (const void *)(uintptr_t)ge, (size_t)gn * sizeof(Ent));
    g_rows_n = n + gn;
    g_ready = 1;
    return 1;
}

// ---- the hooks

// The screen building its rows (slot84): present our list before the original reads the table, so it allocates and
// wires rows for whatever count is installed. Runs every build; install only ever captures the game's own entries,
// which is the state before the first swap.
void rt_opt_build_rows(Cpu *c) {
    (void)c;
    if (!enabled()) return;
    uint64_t table = opt_table();
    if (!table) return;
    if (!g_ready && !install(table)) return;
    wr64(table + TBL_ENTRIES, (uint64_t)(uintptr_t)g_rows);
    wr32(table + TBL_COUNT, g_rows_n);
}

// The generic value read: when w3 is one of our keys, answer with the current value's index instead of letting
// the game look the option up. out (c->x[1]) is where the caller expects a u32.
int rt_opt_read(Cpu *c) {
    if (!enabled()) return 0;
    if (sect_by_key((uint32_t)c->x[3]) >= 0) {   // a section header: always "off"
        wr32((uint64_t)c->x[1], 0);
        c->x[0] = 1;
        return 1;
    }
    int i = item_by_key((uint32_t)c->x[3]);
    if (i < 0) return 0;
    wr32((uint64_t)c->x[1], item_index(&items[i]));
    c->x[0] = 1;   // "found": the caller branches on w0's low bit
    return 1;
}

// The colour adjustment's rules from the F1 menu (rows_color): a preset writes the four values, and moving one of the
// four makes the profile Custom.
static void color_follow(const Item *it, const char *value) {
    static const struct { const char *name, *b, *c, *s, *g; } presets[] = {
        {"default", "0", "100", "100", "1"}, {"vivid", "0", "115", "135", "0.95"}, {"clarity", "2", "110", "115", "1"}};
    if (!strcmp(it->cfg, "TOTK_COLOR_PRESET")) {
        for (size_t p = 0; p < sizeof presets / sizeof *presets; p++)
            if (!strcmp(value, presets[p].name)) {
                cfg_set("TOTK_COLOR_BRIGHTNESS", presets[p].b), cfg_set("TOTK_COLOR_CONTRAST", presets[p].c);
                cfg_set("TOTK_COLOR_SATURATION", presets[p].s), cfg_set("TOTK_COLOR_GAMMA", presets[p].g);
            }
    } else if (!strncmp(it->cfg, "TOTK_COLOR_", 11) && strcmp(cfg_value("TOTK_COLOR_PRESET"), "custom")) {
        cfg_set("TOTK_COLOR_PRESET", "custom");
    }
}

// The generic value write: w1 is the chosen value index (a toggle passes 0/1, a choice the cycled index). Map it
// back through the item's values, store it in cfg and write totk.ini (the F1 menu does that a moment later from its own
// frame loop, which does not run while it is closed). An empty value is the automatic choice: the key goes back to
// unset, as the F1 rows do.
int rt_opt_store(Cpu *c) {
    if (!enabled()) return 0;
    if (sect_by_key((uint32_t)c->x[3]) >= 0) {   // a press on a section header changes nothing
        c->x[0] = 1;
        return 1;
    }
    int i = item_by_key((uint32_t)c->x[3]);
    if (i < 0) return 0;
    const Item *it = &items[i];
    uint32_t idx = (uint32_t)c->x[1];
    if (idx >= (uint32_t)it->nvals) idx = item_default(it);
    const char *value = it->vals[idx];
    if (*value) {
        cfg_set(it->cfg, value);
        color_follow(it, value);
    } else {
        const char *k = it->cfg;
        cfg_reset(&k, 1);
    }
    cfg_save();
    c->x[0] = 1;
    return 1;
}

// The screen opening (the factory's entry, main+0x218d58c): nothing to do, a single list needs no per-opening state.
void rt_opt_screen_open(Cpu *c) { (void)c; }

// Row population (main+0x218c5c0, slot 88): runs once per opening, after the rows exist and before their texts are
// filled - not once per frame. Nothing to do: the original fills our rows' texts through the message hook like the
// game's own.
void rt_opt_tick(Cpu *c) { (void)c; }

// The decide dispatcher (slot49): nothing to do. The injected rows toggle and cycle through the game's own kind-0 and
// kind-1 handlers, which reach rt_opt_store above.
void rt_opt_decide(Cpu *c) { (void)c; }

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
enum { MSG_SLOTS = (NITEMS + NSECT) * (2 + MSG_MAXV) };
static uint16_t g_msg[MSG_SLOTS][MSG_UNITS];

// Slot of a parsed key: title and caption of each row first, then its values. A section header takes the title slot.
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
    if (!opt_msgkey_parse(key, ITEM_ID_BASE, ITEM_ID_BASE + (uint32_t)NITEMS + NSECT, &k)) return 0;
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
