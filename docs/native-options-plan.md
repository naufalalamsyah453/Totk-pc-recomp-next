# Native in-game Options + CI/CD plan

Status document for this fork (`naufalalamsyah453/Totk-pc-recomp-next`).
Update the checkboxes whenever a step lands; do not delete completed rows.

## Goal

1. Move the core settings tabs (**Screen, Game, Sound, Controls**) out of the Dear ImGui F1 overlay and into the
   game's own Options screen (pause → System → Options), the way SavageWindRecomp does with
   `runtime/ui/game_options.mm`.
2. Keep **Advanced, Mods, Cheats** and free-form binding capture in the existing F1 overlay.
3. **No local builds.** Everything that compiles, tests or packages runs on GitHub Actions.
4. **Automatic Windows releases** on version tags (`v*`), published as GitHub Release assets.

## Decisions (locked)

| Decision | Choice |
|---|---|
| Scope | Core tabs only: Screen, Game, Sound, Controls → native. Advanced / Mods / Cheats / capture lists stay on F1. |
| Game version (RE first) | **1.4.3 only** (`patches-v143.txt`). 1.2.1 / 1.1.0 later. |
| Repo | Personal fork `naufalalamsyah453/Totk-pc-recomp-next`, based on upstream `valentderah/totk-pc-recomp` main. |
| Build location | **GitHub Actions only** — no local compile, lift or release packaging. |
| Releases | Tag `v*` → automatic Windows `TotK-Setup-…-windows.zip` on GitHub Releases. |
| Plan language | English (AGENTS.md). |

## Status

| # | Item | Status |
|---|---|---|
| 0.1 | Create GitHub repo `Totk-pc-recomp-next` from upstream | done |
| 0.2 | Clone to `C:\Users\tb\projects\Totk-pc-recomp-next` | done |
| 0.3 | Write this plan | done |
| 0.4 | CI workflow: content guard + tests that need no game files | done |
| 0.5 | Release workflow: automatic Windows release on `v*` | done |
| 0.6 | Push plan + workflows, verify CI green | done |
| 0.7 | Clean old account repos (`naufalalamsyah453/totk-pc-recomp`) | done (404, gone) |
| 1.1 | RE: find TotK 1.4.3 Options screen + option table | done (see "Phase 1 findings") |
| 1.2 | RE: row build / read / store / reset / decide / msg lookup sites | done (back/close virtual + widget budget = Phase 2 verify) |
| 2.1 | Generator: host-call patch sites (`recomp/patches.py` / `gen.py`) | done (see "Phase 2"; `back` site + widget budget still open) |
| 3.1 | Runtime: `runtime/ui/game_options.c` (items, cfg read/store bridge, table swap) | done (gated off by `NATIVE_OPTIONS`; page navigation + messages still open) |
| 3.2 | UTF-16 message injection + `ui_text.c` coverage | hook written, not yet run in game: key parser, text resolver and `rt_opt_msg_lookup` at `+0xb3e3c0` (contract in "Message apply path"); open assumption is that the text is NUL-terminated UTF-16 with the length in char16 units - the smoke test (4.2) confirms it |
| 3.3 | Restart tag on the native row titles (`[restart]`) | done - `opt_text_utf8` appends `UI_MENU_RESTART_TAG` for the `!live` keys (HDR, ANISO, OPERATION_MODE, AUDIO_CHANNELS); en, ru, pt-BR and zh-Hans texts added |
| 4.1 | Input: gamepad + mouse→A/B on native rows | pending |
| 4.2 | In-game smoke on 1.4.3 (via CI artifact or issue checklist) | pending |
| 4.3 | Game-menu scrolling: mouse wheel + L/R | done - `menu_cursor.c` turns the wheel's queued notches (`menu_cursor_wheel`, routed from `menu_win.cpp`/`menu_sdl.cpp` while the F1 menu is closed) and L/R presses into 60 ms d-pad steps with a 40 ms gap, in every non-gameplay focus screen, gated by `TOTK_MENU_CURSOR` |
| 5.1 | Map cfg keys → native rows; leave F1 for the rest | pending |
| 6.1 | Port hooks to 1.2.1 (optional, after 1.4.3 works) | pending |
| 7.1 | Green CI on main | done (run 42120809154) |
| 7.2 | First automatic Windows release tag | done - `v0.3.11-test` pre-release (workflow_dispatch, run 42207472902) and `v0.3.10-1` as Latest (tag push, run 42245255950, `TotK-Setup-v0.3.10-1-windows.zip` + `SHA256SUMS.txt`); both built without the optional `GAME_INPUTS_*` secrets |

## Background

### What exists today (this fork)

- F1 overlay menu: `runtime/ui/menu.cpp` (+ `menu_sdl.cpp` / `menu_win.cpp`), drawn into the swapchain on present.
- Settings backend: `runtime/cfg.c` ↔ `totk.ini` (`totk.ini.example` in repo).
- Menu i18n strings: `runtime/ui_text.c` (en, ru, pt-BR, zh-Hans).
- Deep TotK menu RE already in-tree: `runtime/menu_cursor.c`
  (`eui::ScreenMgr` @ 1.4.3 `main+0x3acf960`, `BoxCursor`, screen request bit +0x175, mouse click → A/B).
- Per-version code patches: `recomp/patches-v110.txt`, `patches-v121.txt`, `patches-v143.txt` +
  `recomp/patches.py` (switchable instruction replacements at lift time).
- Upstream open issue requesting this feature: `valentderah/totk-pc-recomp#242`.
- Upstream CI today: only `.github/workflows/content-guard.yml` (`python tools/check_content.py`).

### How SavageWind does native Options (reference)

File: `Revivedx/SavageWindRecomp` → `runtime/ui/game_options.mm`, hooks from `tools/patches.py`.

1. Extends the game's static option table (`g_site.opt_table`, entries ≥ 14 are runtime-owned).
2. Hooks installed into **recompiled game code** at lift time (C snippets calling `rt_opt_*`):
   - `screen_open_request` / `screen_open` — open Options with a chosen page
   - row build / `gdt_flag_type` — kind + flag for each row
   - `rt_opt_read` / `rt_opt_store` / `rt_opt_reset` — value IO (+0x54 on/off, +0x58 value, +0x5c max, …)
   - `msg_lookup` — supply UTF-16 labels/descriptions with the game's colour tags
   - `rt_opt_decide` — A button actions (open subpage, apply, restart…)
   - `rt_opt_tick` / `opt_row_redraw` — per-frame dirty rows, binding capture
   - `rt_opt_back` / `rt_opt_title_tick` / `rt_opt_pause_tick` — page switch = close + reopen screen
3. Game draws rows with its own font, sounds and cursor.
4. Overlay `menu.mm` remains only for things that do not fit option rows (window size list, perf overlay).

### Gap analysis (totk-pc-recomp)

| Piece | SavageWind (BOTW 1.9.0) | TotK here | Work |
|---|---|---|---|
| Option table / Options screen RE | done | **missing** (1.4.3) | Phase 1 |
| Hook sites in lifted C | C injection via patches.py | word-level `site` lines only; need host-call veneer | Phase 2 |
| Runtime pages + cfg bridge | `game_options.mm` | **missing** → new `game_options.c` | Phase 3 |
| Message injection | `msg_lookup` hook | **missing** (TotK msg system) | Phase 3 |
| Input on native rows | gamepad + mouse | gamepad OK; mouse→A/B already in `menu_cursor.c` | Phase 4 |
| Multi-version | 1.8.2/1.9.0 addresses in comments | 1.1.0 / 1.2. / 1.4.3 — start 1.4.3 only | Phase 6 |
| CI | content-guard only | same + this plan's workflows | Phase 0 |
| Release automation | manual / partial | **this fork: full tag → Windows zip** | Phase 0 / 7 |

## CI/CD design (no local builds)

### Principles

- Public repo never holds game content, keys, or lifted C (`tools/check_content.py`, `GENERATED_MARKER`).
- Pull requests and pushes run checks that need **no game files**.
- The installer release (`TotK-Setup`) needs **no game files**: the runtime library is built against the reference
  layout of `recomp/layout.py` (`reference()`), and the player's setup lifts and builds the game locally.
- A full `totk.exe` / installer **prove** run needs extracted game + lifted `generated/v143`.
  Those inputs are **private** (repository secret URL or private artifact), never committed, and optional.
- Releases are produced only by Actions: on a tag `v*`, or by a manual run (Actions tab, "Run workflow") that either
  leaves the zip as an artifact or, with a tag name, publishes it as a pre-release.

### Workflows

#### `.github/workflows/ci.yml` (push + PR)

| Job | Runner | What |
|---|---|---|
| content-guard | ubuntu | `python tools/check_content.py` (same as upstream) |
| unit-no-game | ubuntu | `pack_shader_cache_test.py`, `spin_waits_test.py`, `layout_reference_check.py`, `worktree_test.py` (stdlib / no game; `capstone`, `lz4`) |
| unit-py314 | ubuntu | `installer_test.py`, `ncz_test.py`, `romfs_prompts_test.py` (Python 3.14 + pinned reqs) |
| unit-windows | windows | `flags_check.py`, `excl_mt.py`, `opt_msgkey_check.py` (MSYS2 clang; `capstone`, `unicorn`; expands later) |

Not in CI (need game files or huge toolchains): `jumptables_check.py`, `diff/lift.py`, `diff/func.py`,
`installer_e2e.py`, full `make_release.py --prove`. Track them in Status when a private runner exists.

#### `.github/workflows/release.yml` (tag `v*` or `workflow_dispatch`)

Windows job, high-level:

1. Checkout + setup Python 3.12 / 3.14 + `uv` venvs (pinned `tools/requirements*.txt`).
2. Setup MSYS2 UCRT64 toolchain (clang, lld, ninja, …) and .NET for `totk_xlate`; build `hactool` (`tools/build_hactool.py`).
3. **Restore private game inputs** (optional, only when the secret is present; without it the release is built against the reference layout):
   - `GAME_INPUTS_URL` (+ optional `GAME_INPUTS_TOKEN`): zip containing `generated/v143/**` and, for `--prove`,
     `build/extracted/v143/**` (or a slim subset documented when seeding).
   - Unpack into the workspace paths `make_release.py` expects (`MAIN_ROOT` layout).
4. `python tools/build_xlate.py` (shader translator; no game bytes in the repo).
5. `python tools/make_release.py --out build/release/TotK-Setup --zip build/release/TotK-Setup-<ver>-windows.zip`
   (add `--prove` only when extracted game inputs are present).
6. `python tools/check_content.py` over the release folder (script already does this).
7. Create GitHub Release for the tag and upload the zip (+ sha256).

If `GAME_INPUTS_URL` is unset the job still builds the release (the log says the runtime library is built against the
reference layout) and `--prove` is skipped with a warning. A manual run with the `tag` input empty only uploads the zip
as a workflow artifact; with a tag name it also publishes a pre-release.

### Seeding `GAME_INPUTS` (optional, one-time, outside this repo; only for `--prove`)

Game-derived trees must stay private. Recommended:

1. Zip `generated/v143` (+ optional `build/extracted/v143` for prove) from any machine that already extracted
   the game — **not** from a new local build on the developer laptop if the rule is “no local builds”;
   use an existing archive or a private runner workspace.
2. Store the zip as a **private** release asset on a private companion repo, or any URL Actions can fetch.
3. Set repository secrets `GAME_INPUTS_URL` and, if needed, `GAME_INPUTS_TOKEN` (fine-grained PAT: read-only
   on that private repo).

Without the secrets the release is still built and published, just without the `--prove` check.

## Native Options — technical plan

### Phase 1 — RE TotK 1.4.3 Options screen

Goal: a written map (addresses, structs, call sites) checked into this plan or `docs/` as findings, **not** as
game bytes.

Find and record:

1. **Option table** (analogue of BOTW `g_site.opt_table`): pointer(s), entry size, `count`, `rows`, `order`.
   TotK pause → System → Options is the target screen; title-screen Options if present.
2. **Row object** (BOTW: 0x98 bytes, +0x50 entry index, +0x54 on/off, +0x58 value, +0x5c max, +0x60 min, +0x64
   button state, +0x68 default). Confirm TotK layout.
3. **Entry** (BOTW: 0x30, `kind` first: TOGGLE=0, CHOICE=1, BUTTON=2).
4. **Functions** (xref from menu_cursor’s ScreenMgr knowledge; open request +0x175 bit 2 already known):
   - screen open request / open / close / back
   - build rows from table
   - read / store / reset value
   - decide (A)
   - per-frame tick / redraw
   - message lookup for row title + description
5. Record **all addresses as `main+0x…` for 1.4.3** in this doc (Status Phase 1 rows).

Tooling: whatever RE you use (Ghidra/IDA/binary ninja) on a **local dump the user already owns**; outputs are
addresses and notes only — never pasted game code into the repo.

#### Phase 1 findings — 1.4.3 address map (interim)

Method: lifted C of `main` (local `generated/v143`, never committed); guest VA = `main+0x…` (image base 0).
String refs resolved from ADRP+ADD pairs; roles below are interpretations pending call-site confirmation.

**Screen class (`UISystemOptionScreen`)**

| What | Address |
|---|---|
| class name string | `main+0x2c58d59` |
| factory (malloc 0x220 → ctor) | `main+0x218d58c` |
| ctor (vptr stored at obj+0) | `main+0x218b9a0` |
| dtor / deleting dtor | `main+0x218ba60` / `main+0x218baec` |
| vtable (~101 slots) | `main+0x3732970` |
| slot99 `getName` / slot100 `create` | `main+0x218d580` / `main+0x218d58c` |
| slot49 row decide/focus handler | `main+0x218d274` |
| row refresh loop (switch on kind 0/1/2) | `main+0x218d3e0` |
| row widget setup loop | `main+0x218c114` |
| row text call sites (loops) | `main_0297.c` L9131/L9150, L10166/L10175 |
| pause-menu Options button | `main+0x218bb10` |
| `IsTitleMenu` inline string (after vtable) | `main+0x3732c98` |

Screen object (size 0x220, interim): `+0x1a8` kind-2 list; `+0x1b0/+0x1b8` row array A count/ptr (kind 0);
`+0x1c0/+0x1c8` row array B count/ptr (kind 1); `+0x1e0/+0x1e8` entry list count/ptr (stride 8:
`{u32 kind, u32 row}`); `+0x210` state/result code (init 0xffffffff).

**Vtable `main+0x3732970` — overridden slots (verified by scanning the vtable bytes)**

| slot | main+ | function | role |
|---|---|---|---|
| 0 / 1 | `+0x218ba60` / `+0x218baec` | dtor / deleting dtor | |
| 2 / 3 | `+0x218d514` / `+0x218d558` | event routing (tail to `+0x2001ad0`) | |
| 45 (+0x168) | `+0x218d0cc` | focus/cursor → caption `+0x218c748` | onCursor |
| 49 (+0x188) | `+0x218d274` | decide dispatcher (kind0/1/2) | onDecide |
| 50 (+0x190) | `+0x218d3dc` | empty stub | |
| 83 (+0x298) | `+0x218c114` | layout/widget wiring (`Pa_AppOptionBtn`, `N_SlideArea`, `Pa_ScrollBar`) | |
| 84 (+0x2a0) | `+0x218bb10` | **build rows from OptionSetTable** (alloc entry list + row arrays by table count, widget lookup `Pa_ToggleBtn_%02d` / `Pa_SelectBtn_%02d`) | build_rows |
| 86 / 92 | `+0x218c8ec` / `+0x218c8e8` | unknown (small) | |
| 88 (+0x2c0) | `+0x218c5c0` | **tick/update**: row text + caption + guide refresh | tick |
| 98 / 107 / 109 | `+0x218d55c` / `+0x218d5bc` / `+0x218d5e0` | misc virtuals | |
| 99 (+0x318) | `+0x218d580` | `getName` → `UISystemOptionScreen` | |
| 100 (+0x320) | `+0x218d58c` | **factory** (virtual create — no direct callers by design) | screen_open |

Dirty-flag row refresh: `main+0x2001914` (eui helper, flag key `0x87ebe934`) → `main+0x218d3e0` →
clears bit at obj+0x18. Table load: virtual `main+0x9e7890` (fn ptr at `main+0x3733da8` in a
dispatch table) → `main+0x9ea500` (bgyml path, stride 0x28).

**Option table (`game__ui__OptionSetTable`)**

| What | Address |
|---|---|
| type-name getters OptionSet / OptionSetTable | `main+0x20883a4` / `main+0x2089488` (strings `main+0x2c89a13`, `main+0x2cdcf87`) |
| bgyml path `UI/OptionSetTable/OptionSetTable…bgyml` | string `main+0x2c15d31`, data ptr `main+0x3719430` |
| table loader (stride 0x28, reads obj+0x150) | `main+0x9ea500` (caller `main_0114.c` L21180) |
| count / entry(i) / entry-by-(id,value) | `main+0x2089c14` / `main+0x2089cb4` / `main+0x2089d90` |
| find-by-name + name resolve | `main+0x20883e8`, `main+0x20885f4`, `main+0x7d4310` |
| option-id string tables | `main+0x3718e48` (`UseAmiibo`), `main+0x3714de0` (`EnableWarpLargeDungeonWind`) |
| table singleton (ptr at +0x5e8) | `main+0x3a82e70` |

**Table struct (confirmed from the loader, `main+0x9ea500`, and `count`/`entry`)** — object at
singleton `main+0x3a82e70` → `+0x5e8` = table pointer:

| table | meaning |
|---|---|
| `+0x0` | u32 raw entry count |
| `+0x8` | entries pointer (stride **0x28**) |

Entry (0x28 bytes, confirmed from `entry(i)` `main+0x2089cb4`, `count` `main+0x2089c14` and the
row-copy code):

| entry | meaning |
|---|---|
| `+0x0` | option id (compared by `entry-by-id` `main+0x2089d90`, copied to row+0x48 / row+0x2c) |
| `+0x4` | kind: 0 = toggle, 1 = choice, 2 = button |
| `+0x8` | default candidate (→ row+0x50 / row+0x30) |
| `+0x10` | choice count (kind-1 cycle modulus) |
| `+0x18` | value-array pointer (kind-1 candidate values) |
| `+0x20` | **per-option value key (u32)** — the `w3` argument read/store receive (→ row+0x54 / row+0x30) |
| `+0x24` | condition/filter flag (0 = always shown) |

**Display order / rows (confirmed)**: there is **no separate order or rows array** — unlike the
BOTW reference (`OPT_ORDER`/`OPT_ROWS`). Display order is the table array order; the visible row
count is `count()`'s filtered result. `count(table)` `main+0x2089c14` counts every entry with
`+0x24 == 0` and gates `+0x24 != 0` through predicate `main+0xb4bf84` (an availability check, not a
section/page filter). A **page** is therefore a host-built ordered entry array swapped into table
`+0x8` with its length in `+0x0`.

**Hook argument contract (confirmed)**: `read` (`main+0x980288`) is called as
`read(context, out=x1, binding=x2, w3=row+0x54=entry+0x20)`; kind-0 `store` (`main+0x923d20`) as
`store(context, w1=ldrb widget+0x89, x2=binding, w3=row+0x30=entry+0x20)`; kind-1 `store`
(`main+0x170210`) as `store(context, w1=value, x2=binding, w3=row+0x54=entry+0x20)`. So both hooks
key on **`c->w[3] == our item key`**, with the value in `w1` (store) or written to the out buffer
at `x1` (read). The binding comes from `main+0x3cffe8(context, &binding, 0x834d9d9e)`, a 0x50-byte
record at context+0xc18. Read's out-buffer element type (u32 vs struct) is not yet verified — assume
u32 and validate at smoke test.

**Row build / msg / decide (main_0293.c + main_0297.c)**

| fn | main+ | role |
|---|---|---|
| `f_700212b9d8` | `+0x212b9d8` | kind-2 row text, key `1000_%02d` |
| `f_700212bf04` | `+0x212bf04` | kind-1 label + value sync (copies entry fields, finds value index) |
| `f_700212c0e8` | `+0x212c0e8` | value text: key `10%02d_%02d` + `T_Text_00` |
| `f_700212c6ac` | `+0x212c6ac` | kind-0 row label (same key scheme as kind-1) |
| `f_700212bdd0` | `+0x212bdd0` | row widget setup: `On`, `Decide`, `Pa_ArrowR_00`, `Pa_ArrowL_00` |
| `f_700212c1e0` | `+0x212c1e0` | kind-1 decide: value = (i+1) mod entry+0x10, write, refresh |
| `f_700212c3e0` | `+0x212c3e0` | sibling store path (role unverified) |
| `f_700212acd0` | `+0x212acd0` | Controls tab keys (`BasicControls`, …) |
| `f_7000170210` | `+0x170210` | generic settings write (dataObj, name, valueBuf, …) |
| `f_7000923d20` | `+0x923d20` | generic flag write (used by kind-0 toggle decide) |
| `f_7000980288` | `+0x980288` | generic value read (context, nameBuf, out, type) |
| `f_7000ba1730` | `+0xba1730` | msg apply (widget, suffix, keyObj, …) |
| `f_7000b3e3c0` | `+0xb3e3c0` | message lookup (manager, out, &sheet-name ptr, &key string object); **hook site** |
| `f_70000f4444` / `f_7000ba42c0` | `+0xf4444` / `+0xba42c0` | msg key build (sprintf) / append sheet+key |

**Decide / focus (vtable of the screen class)**

- slot49 `main+0x218d274` — row action dispatcher: kind0 → `main+0x212c818` (toggle write via
  `main+0x923d20`, flag id byte at arg+0x89), kind1 → `main+0x212c1e0` (choice cycle + write via
  `main+0x170210`), kind2 → `main+0x212bcd8` (button press); result code stored at screen+0x210.
- slot45 `main+0x218d0cc` — focus/cursor handler: finds the focused row id, then calls caption
  setter `main+0x218c748` (key `%04d` + `T_Caption_00`).
- Kind semantics confirmed: **0 = toggle, 1 = choice, 2 = button** (same as BOTW reference).
- No direct C callers for slot45/49 — they are eui virtual dispatches (input entry is inside eui).

**Message scheme (sheet `LayoutMsg/SystemOption_00` = `main+0x2c58b8e`)**

| purpose | key format | suffix |
|---|---|---|
| row title (kind1/0) | `10%02d` | `T_Option_00` (`main+0x2ca3195`) |
| value text | `10%02d_%02d` | `T_Text_00` (`main+0x2cb3214`) |
| focused caption | `%04d` | `T_Caption_00` (`main+0x2ced5a5`) |
| kind-2 row | `1000_%02d` | — |
| bottom guide | `20%02d` | `Pa_Guide_%02d/T_Guide_00`, `T_Icon_00` |

**Screen init**: `main+0x218c114` + `main+0x218c2c4` (widgets `Pa_AppOptionBtn_00`,
`Pa_AmiiboBtn_00`, `N_SlideArea_00`, `Pa_ScrollBar_00`, `IsTitleMenu` at `main+0x3732c98`;
row containers via `main+0x212c614` + `main+0x212bdd0`). Row text refresh loops:
`main+0x218c5c0` and `main+0x218d3e0` (the latter iterates the OptionSetTable and switches on kind).

**Kind-1 row object (interim)**: `+0x20` text widget, `+0x30/+0x38` left/right arrow widgets,
`+0x48` option id (copied from entry+0x0), `+0x4c` value index (u32), `+0x50` entry+0x8 (likely
default value), `+0x54` entry+0x20 (value type, passed to read/write).

**Reset**: no dedicated function or UI string exists in the game's screen — the native UI implements
reset by writing entry defaults (candidate: entry+0x8).

**`build_rows` (slot84 `main+0x218bb10`, prologue `d102c3ff` = `sub sp,sp,#0xb0`) — confirmed**:
allocates a kind-2 list (0x38) → screen+0x1a8; loads the table via `adrp 0x7003a82e70` +
`ldr [x8,#0x5e8]`; `bl count`; allocates the entry list `count`×8 → screen+0x1e0 (count) /
+0x1e8 (ptr) as `{u32 kind, u32 row_index_within_kind}`; a per-kind switch via jump table
`0x7002d17458` allocates the kind-0 row array → screen+0x1b0/+0x1b8 and the kind-1 row array →
screen+0x1c0/+0x1c8; per row it looks the widget up with `snprintf(buf,"Pa_ToggleBtn_%02d",i)` /
`Pa_SelectBtn_%02d` (fmts `0x7002c47588` / `0x7002c4759a`) then `bl 0x7000406acc`. **`cbz x0` after
the lookup is a fail-soft null path** — the row object is still allocated without its widget, so an
index past the layout's instances renders as an invisible row rather than crashing.

**Row object fields (confirmed)** — kind-1 sync `main+0x212bf04` (row x0, entry x1): row+0x48 =
entry+0x0, row+0x50 = entry+0x8, row+0x54 = entry+0x20, row+0x4c = value index. Kind-0 sync
`main+0x212c6ac`: row+0x2c = entry+0x0, row+0x30 = entry+0x20. Kind-0/1 decide handlers verify the
pressed widget against the row's own (`ldr x0,[x8,#0x30]; bl 0x28f7680; cmp x0,x20`).

**Widget budget (strategy decided)**: the layout bgyml (with the real `Pa_ToggleBtn_%02d` instance
count) sits inside `UI/LayoutArchived` and is not reachable by string scan; RomFS is not extracted.
So the budget is taken as **the game's original visible entry count** — read `count()`/table `+0x0`
once at install time, *before* the swap — and every page's row count is clamped to it. That count is
by construction displayable, and the `cbz` fail-soft backs it up. Spare-widget headroom is a later
optimisation once the layout is known.

**Message apply path (read from the lifted C of 1.4.3; partly confirmed)**

- `main+0xba1730` is **not** a key-to-text lookup. It sets one named child of a widget tree:
  `x0` = root widget (the row), `x1` = NUL-terminated ASCII **widget path** of child names separated by `/` whose last
  segment is the leaf pane (the `T_Option_00` / `T_Text_00` / `T_Caption_00` names of the message scheme), `x2` = an opaque
  argument it hands on as `x3` of `main+0x610348`, `w3` bit 0 = refresh flag. It returns `w0` = success (the result of
  `main+0x610348`, 0 when the path does not resolve).
- Walk: for each `/` it searches the widget's child list (head at widget `+0x48`, first node `+0x50`, the child's name stored
  inline just before its list node) for a name equal to the segment, then continues in that child (`node+0x10`). At the end of
  the string it calls `main+0x610348(child, child+0x18, leaf, x2_arg, 0, 0xffffffff, &bool)`; when `w3` bit 0 is set and the
  out bool is set it calls `main+0x81b55c` on the widget (and on the widget at `+0x88` when flag bit 2 of `+0x90` is set).
- `main+0xf4444` is a printf into a small string object: `x0` = buffer `{char *data @+0 (starts at x0+0xc), u32 capacity
  @+0x8 (0x20), inline bytes @+0xc}`, `x1` = format, `x2...` = arguments; it ends in `vsnprintf` (`main+0x2bb7c20`). The built
  key string is therefore at `[buffer+0]`, at most 31 characters (ours are 7-10).
- `main+0xba42c0(x0, x1, x2)` forwards to `main+0xb3e3c0(messageManager, x0, x1, x2)` where the manager is the pointer at
  `main+0x396bcf8` (dereferenced once more); it returns 1 when that call returns 0 and logs through `main+0xa9a7bc` /
  `main+0xc28b8c` otherwise. It is the "append sheet + key" step, not the text fetch.
- **Resolved (read from the lifted C of `+0x212c6ac`, `+0x212c0e8`, `+0x610348`, `+0xb3e3c0`)**: the callers build the key with
  `+0xf4444` into a small string object on their stack, then call `+0xba42c0(out, &sheet-name ptr, &key object, 1)`, which
  forwards to `+0xb3e3c0(manager, out, &sheet-name ptr, &key object)`. The key characters are at `[[key object] + 0]`;
  the sheet name is a pointer to an ASCII string. `+0xb3e3c0` copies the sheet name, finds the sheet in the manager's list,
  looks the key up and fills `out` = `{ text pointer @+0, u32 length @+0x8, u64 handle @+0x10 }`. It returns 0 for a text
  found, 1 for an empty one, 2 for a null pointer, 3 when the sheet is missing (out then holds the game's own placeholder
  string and a handle of `~0`). `+0xba1730` receives `out` as its `x2` argument unchanged and `+0x610348` hands
  `[out+0]` and `[out+8]` to the leaf pane's virtual at vtable `+0x1a0` (set text), after storing `[out+0x10]` at
  pane `+0x160`. So the whole text path is: key -> `+0xb3e3c0` -> `out` -> pane. The three leaf names only tell which
  text is wanted; the key already carries the option and value id (`runtime/ui/opt_msgkey.h`).
- **Hook placement**: `+0xb3e3c0` is the lowest point that sees both the key and `out`, and its first instruction
  (`sub sp, sp, #0x160`, word `d10583ff`) writes no flags, so it is a valid `@hostif` entry. `rt_opt_msg_lookup`
  fills `out`, leaves 0 in `x0` and returns 1 for our keys; every other key returns 0 and the original runs.
- **Not yet verified in game**: that the pane's set-text virtual reads NUL-terminated UTF-16 with the length counted in
  char16 units. It is what the game's message sheets hold and what the fallback strings' lengths (13 and 14) suggest, but
  no log line has confirmed it.

**Remaining Phase 2 verify items**: back/close virtual (candidates among slots 86/92/98); exact eui
input dispatcher that calls slot45/49 (framework-internal, not required if hooks sit lower).

### Phase 2 — Generator host-call hooks

**Implemented (2.1 done).** `recomp/patches.py` accepts two host-call tokens as replacement words and
`recomp/gen.py` emits them (precedent: the `rt_spin_gate` inline call for spin waits):

- `@host:<symbol>` — `void <symbol>(Cpu *c)` runs where it sits in the word list, then the remaining words
  (usually the original instruction) run: `@host:s <orig>` = before, `<orig> @host:s` = after. The generator
  spills the register locals to the Cpu before the hook and reloads them after, so a hook sees current
  arguments and its Cpu writes reach the code that follows.
- `@hostif:<symbol>` — `int <symbol>(Cpu *c)` must be the only word of its alternative and must sit at a
  function's entry (enforced): non-zero means the hook took the guest call over — it did the whole operation,
  leaves the caller's expected value in `c->x[0]`, and the generator returns from the lifted function; zero runs
  the original instruction. Entry-only because returning early skips the prologue/epilogue.
- Constraints (enforced or documented): the original of a `@hostif` site must not write flags; hooks must not
  touch `c->nzcv`, `c->sp` or x19-x29; prototypes (`extern void/int <symbol>(Cpu *c);`) are emitted per chunk.

Landed with it:

- Patch set `opts` = `TOTK_NATIVE_OPTIONS` (`recomp/patches-v143.txt`, index 13 in `patches.h`). The default is
  **off**: `patches.c` resolves a set's `on` from its cfg meta row first, and `NATIVE_OPTIONS`'s meta default is
  `"0"`, so with the key unset the sites run the originals and the screen is the game's.
- Eight site lines, every one the flag-neutral prologue at its function's entry (originals checked against the
  1.4.3 NSO): `@host` for `rt_opt_screen_open` (`+0x218d58c`), `rt_opt_build_rows` (`+0x218bb10`, carries
  `flag_type`), `rt_opt_tick` (`+0x218c5c0`), `rt_opt_decide` (`+0x218d274`, may rewrite `c->x[1]` to a
  sentinel); `@hostif` for `rt_opt_read` (`+0x980288`), `rt_opt_store` (`+0x170210` and `+0x923d20`),
  `rt_opt_msg_lookup` (`+0xb3e3c0`, moved from `+0xba1730`).
- The hooks live in `runtime/ui/game_options.c` (auto-compiled: `tools/build_game.py`'s `runtime_sources()` globs
  `ui/*.c` as well as `runtime/*.c`); Phase 3 replaced the Phase 2 fail-soft stubs there with the item table, the
  table swap and the cfg read/store bridge. `reset` has no game site (runtime writes entry defaults), `back` waits
  for the slot 86/92/98 verify, `screen_open_request` is already `runtime/menu_cursor.c`'s +0x175 bit.
- Verified: `patches.check` over all 62 main sites against the NSO; the eight hooked functions emitted through
  the real `Emitter` pass `clang -fsyntax-only`; a full `gen.py --patches` run writes 418/418 chunks (131 s,
  0 unsupported) with `RT_PATCH_COUNT 14` and the eight `rt_patch_on[13]` sites present in 5 chunk files.

SavageWind instead injects **C** (`{ extern void rt_opt_rows(void); rt_opt_rows(); … }`) at lift time; the
`@host` token is this fork's equivalent inside the existing word-patch machinery.

**1.4.3 hook site map (from Phase 1 findings; `main+0x…`)**

| hook | site | notes |
|---|---|---|
| `screen_open` | factory vtable slot100 `+0x218d58c`, or layout slot83 `+0x218c114` | after ctor `+0x218b9a0` |
| `build_rows` | vtable slot84 `+0x218bb10` | allocs entry list + row arrays from table count; widget lookup by name |
| `flag_type` | entry layout inside `+0x218bb10` (entry+0x4 = kind 0/1/2) | |
| `read` | generic getter `+0x980288`; row sync `+0x212bf04` | |
| `store` | choice `+0x212c1e0` → `+0x170210`; toggle `+0x212c818` → `+0x923d20` | |
| `reset` | none in game → runtime-owned (write entry defaults, entry+0x8 candidate) | |
| `msg_lookup` | lookup `+0xb3e3c0` (hook); apply `+0xba1730`; key build `+0xf4444` / `+0xba42c0`; sheet `LayoutMsg/SystemOption_00` | |
| `decide` | vtable slot49 `+0x218d274` | dispatches per kind |
| `tick` | vtable slot88 `+0x218c5c0`; dirty refresh `+0x2001914` → `+0x218d3e0` | |
| `back` | TBD (slots 86/92/98 candidates) | open-request bit +0x175 already known from `menu_cursor.c` |
| pause/title entry | row build also reachable via `+0x218bb10`; pause screens `+0x21679ec` / `+0x216720c` | |

Rows are fixed layout widgets (`Pa_ToggleBtn_%02d`, `Pa_SelectBtn_%02d`): runtime rows can only fill
widgets the bgyml defines. The instance count is still unknown (layout inside `UI/LayoutArchived`),
so Phase 3 clamps every page to the game's original visible entry count read at install time — see
the widget-budget strategy in Phase 1 findings.

### Phase 3 — Runtime `runtime/ui/game_options.c`

Port the structure of `game_options.mm` to C, wiring to existing totk pieces:

- `Item` table built from `cfg` keys (`cfg_get` / `cfg_set` in `runtime/cfg.c`) for Screen / Game / Sound /
  Controls only. The cfg keys per tab are the ones `runtime/ui/menu.cpp` groups under `tab_screen` /
  `tab_game` / `tab_sound` / `tab_controls` (they carry the `TOTK_` prefix in the menu; `cfg.c` stores
  them without it).
- Pages: main section buttons → section pages; game’s own rows on Controls/Video stay (do not remove them).
- `g_entries` copy of game table + `g_order` per page; validate count/rows on init (fail soft like SavageWind).
- **Page mechanism (confirmed)**: no order/rows array in TotK — a page swaps table `+0x8` (host-built
  ordered entry array) and `+0x0` (its length). `install()` copies the game's entries once, appends
  ours with synthetic `entry+0x20` keys, and records the pre-swap visible count as the widget budget;
  `rt_opt_build_rows` writes the current page's array before the original runs.
- **Hook bodies key on `c->w[3]`** (`entry+0x20`): `rt_opt_read` looks the cfg key up and writes the
  value to the out buffer at `c->x[1]`; `rt_opt_store` writes `w1` back. Guest memory is read/written
  through `memcpy` from `(const void *)(uintptr_t)addr` exactly as `menu_cursor.c` does (guest base
  `MOD_MAIN_BASE`, table singleton `MOD_MAIN_BASE + 0x3a82e70` → `+0x5e8`).
- **Curated rows, not every key**: `tab_screen` alone draws ~30 rows and cannot fit one page. Each
  section page shows a curated subset that fits the budget; every cfg key still gets an item-table
  row documenting native-vs-F1 (the "leave it on F1" rule below).
- Messages: hook supplies UTF-16; reuse colour tags the game already understands; strings from `ui_text.c`
  where they already exist, new keys added for both en + existing languages (fallback en).
- Sub-actions that do not fit a row (window size list, restart prompt): either map to a KIND_BUTTON that
  delegates to existing runtime UI, or leave that setting on F1 (document per key in the item table).

Build integration: add the file wherever `runtime/*.c` is listed for `build_game.py` / release runtime lib
(follow existing menu sources). `runtime_sources()` globs `runtime/*.c` (and `runtime/ui/*.c`), so a new
file under `runtime/ui/` is picked up without generator changes.

**Landed (Phase 3 foundation)** — `runtime/ui/game_options.c`:

- The 7 `rt_opt_*` hooks moved here from the Phase 2 stub file (`runtime/native_options.c`, deleted); every one
  is fail-soft and gated on `cfg_bool("TOTK_NATIVE_OPTIONS")`, whose meta default is `"0"`.
- A curated `Item` table (14 rows across Screen/Game/Sound/Controls) with the accepted value strings; a toggle is
  a 2-value choice (`"0"`/`"1"`), so both kinds share one model: **read returns the current value's index (u32) to
  `c->x[1]`, store receives that index in `w1`**, and `w3` (the entry's `+0x20` key) says which item.
- `rt_opt_build_rows` captures the game's entries once and installs a host array of **our rows first, then a copy
  of the game's** (so ours sit inside the layout's widget budget), writing it to table `+0x8`/`+0x0`.
- Our option ids come from `0x8000+i` and value keys from `0x4e000000+i`, ranges the game does not use, so
  entry-by-id and the read/store dispatch never confuse ours with a game entry.
- **Messages (Phase 3.2), host half landed**: `runtime/ui/opt_msgkey.h` classifies a message key as one of our
  title (`10<id>`), value (`10<id>_<vv>`) or caption (`<id>`) keys, keyed on our option-id range
  `[0x8000, 0x8000 + NITEMS)` (tested by `tests/opt_msgkey_check.py`); `rt_opt_message_utf16` in `game_options.c`
  turns such a key into UTF-16 from the menu catalog (`ui_text.c`: every Item carries its title, caption and
  per-value catalog ids, so en, ru, pt-BR and zh-Hans already work). Value ids are the value indices, because our
  entries' value array is `[0, 1, ...]`.
  A caption also carries the notes the F1 menu adds to a description: "set by environment variable" when the key
  comes from the environment, "after restart" for keys the metas table does not mark live (`ANISO`, `HDR`,
  `OPERATION_MODE`, `AUDIO_CHANNELS`). Checked against `runtime/cfg.c`: all 14 item keys exist and every accepted
  value string matches its metas row.
- The **title** of such a key (the same four, not one the environment fixes) carries the tag `[restart]`
  (`UI_MENU_RESTART_TAG`, texts in en, ru, pt-BR and zh-Hans) instead, because the caption's note only shows while
  the row has the focus: a player sees which rows change only after a restart at a glance, without a row of F1-style
  colour or layout changes in the game's own screen.
- **Messages, hook written** (`rt_opt_msg_lookup`, `@hostif` at `main+0xb3e3c0`, see "Message apply path"): for a key of ours it
  fills the out object with a per-key UTF-16 buffer (each title, caption and value has its own buffer that lives for the
  run, in case the pane keeps the pointer instead of copying), the unit count, and a handle of `~0`, then leaves 0 in
  `x0`. Checked on the host with fake guest memory against the real `cfg.c` / `ui_text.c`: our title, value and caption
  keys are taken over with the right text and length, game keys (`1001`, `1001_02`, `0001`) and a value past an item's
  last are left to the game with `out` untouched, and with `TOTK_NATIVE_OPTIONS=0` nothing is taken. Open: the UTF-16
  assumption above, and that the caption key `%04d` is built from the option id (and not the row index) - the parser accepts the bare id.
- Still open after messages: **page navigation** (`rt_opt_decide` + a screen reopen, which would let each
  section have its own page and stop ours from displacing the game's tail rows).

### Phase 4 — Input

- Gamepad: native screen already moves `BoxCursor` — no work expected beyond not breaking `menu_cursor.c`.
- Mouse: existing click→A/B and focus follow should work on native rows; verify after Phase 3.
- Scrolling (landed): the mouse wheel and L/R scroll any of the game's menus that has the focus box, inventory and
  title included, through the same `menu_cursor.c` machinery: the window adapters route wheel notches to
  `menu_cursor_wheel` while the F1 menu is closed, each poll turns queued notches and L/R edges (repeat every 120 ms
  while held) into one 60 ms d-pad press with 40 ms of release between steps, and the raw L/R are cleared while such
  a screen is up (the game turns those pages with ZL/ZR). Runs without `input_kbm_last`, so a gamepad player scrolls
  too; gameplay screens (ability wheel, d-pad strips, map, ...) keep L/R; `TOTK_MENU_CURSOR=0` turns it off.
- Binding capture (Keyboard/Controller pages): **out of scope for native** (stays F1) per locked scope.

### Phase 5 — cfg ↔ native mapping

Explicit matrix (keep updating this table when keys move):

| totk.ini / cfg prefix | Native page | F1 only |
|---|---|---|
| resolution / fullscreen / vsync / fps / upscaler / … (Screen) | yes (choice/toggle rows) | GPU picker lists if too dynamic |
| camera / hud / gameplay toggles (Game) | yes | |
| volume / audio toggles (Sound) | yes | |
| gyro / deadzone / button icons (Controls) | yes where KIND_TOGGLE/CHOICE | full bind tables |
| mods / cheats / advanced / debug | — | yes (F1) |

### Phase 6 — Other versions (after 1.4.3 works)

Repeat Phase 1 address map for 1.2.1 then 1.1.0; add `patches-v121` / `v110` site lines. Runtime stays shared.

## Verification

| Gate | How (always CI or review, never “it built on my machine”) |
|---|---|
| No game content in git | `tools/check_content.py --staged` (local hook) + content-guard job |
| Unit tests | ci.yml jobs green on PR |
| Lifter/patch sanity (game files) | deferred: private runner job; until then manual review of patch lines vs `patches-v143.txt` originals |
| Native options smoke | After Phase 3: checklist on a 1.4.3 install (open Options, each page, A/B, values persist in totk.ini) — run on the player machine or a private runner, results recorded in the PR |
| Release | Tag `v*` → release.yml green → zip on Releases → install smoke on a clean VM or spare folder |

## Risks

| Risk | Mitigation |
|---|---|
| TotK Options table differs enough that “extend table” is hard | Fall back: inject only via screen hooks without extending the static table (build rows ourselves in `build_rows` hook) |
| Host-call veneer fights the lifter’s flag/branch rules | Follow `rt_spin_gate` precedent; keep stubs leaf functions; differential tests when a private runner exists |
| Message hook misses colour tags → mojibake / missing glyphs | Reuse tags from a known-good game string; en first, other languages via existing `ui_text.c` |
| Release without prove ships a broken installer | `--prove` required in release.yml when extracted inputs present; fail closed otherwise |
| Secrets never seeded → release not proven | The release builds against the reference layout; `--prove` stays off until the private zip exists |
| Scope creep (mods/cheats native) | Locked decisions table; reject PRs that move dynamic lists into option rows |

## Non-goals

- Local builds, local release packaging, checking in `generated/` or `build/extracted/`.
- Native pages for Mods / Cheats / Advanced / key-capture lists.
- Supporting 1.1.0 / 1.2.1 in the first native-options milestone.
- Changing upstream `valentderah/totk-pc-recomp` (this is a personal fork; upstream PRs optional later).

## CI/CD + repo bootstrap — execution log

Update this subsection after each infrastructure step.

- [x] Workflows committed and pushed
- [x] First CI run green on `main` (run 42120809154, commit `56e3e06`; fixes landed in `c9a8082`, `6baf44a`, `56e3e06`: ignore-list, `is_link` assert for POSIX symlinks, gitignore patterns without trailing slash, `capstone`/`unicorn` in unit-windows, ABA expectations aligned with the compare-and-swap monitor in `runtime/cpu.h`)
- [x] Release workflow present (builds without game inputs; `--prove` needs `GAME_INPUTS_URL`)
- [x] Old `naufalalamsyah453/totk-pc-recomp` confirmed gone / cleaned (404)
- [x] First `v*` tag → Windows zip on Releases (`v0.3.11-test` pre-release, run 42207472902; then `v0.3.10-1` tagged on the wheel-scroll + restart-tag build, run 42245255950, 6m9s, zip 146 MB - built without the optional `GAME_INPUTS_*` secrets, so packaged without `--prove`)

## References

- SavageWind native options: `runtime/ui/game_options.mm`, `tools/patches.py` (upstream Revivedx/SavageWindRecomp)
- TotK menu cursor RE: `runtime/menu_cursor.c` (this repo)
- Patch mechanism: `recomp/patches.py` header (this repo)
- Content boundary: `AGENTS.md`, `tools/check_content.py`
- Upstream feature request: valentderah/totk-pc-recomp#242
- Build/release scripts: `tools/make_release.py`, `tools/build_xlate.py`, `docs/installer.md`, `docs/building.md`
