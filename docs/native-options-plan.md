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
| 2.1 | Generator: host-call patch sites (`recomp/patches.py` / `gen.py`) | pending |
| 3.1 | Runtime: `runtime/ui/game_options.c` (pages, items, cfg bridge) | pending |
| 3.2 | UTF-16 message injection + `ui_text.c` coverage | pending |
| 4.1 | Input: gamepad + mouse→A/B on native rows | pending |
| 4.2 | In-game smoke on 1.4.3 (via CI artifact or issue checklist) | pending |
| 5.1 | Map cfg keys → native rows; leave F1 for the rest | pending |
| 6.1 | Port hooks to 1.2.1 (optional, after 1.4.3 works) | pending |
| 7.1 | Green CI on main | done (run 42120809154) |
| 7.2 | First automatic Windows release tag | pending (needs `GAME_INPUTS_URL` secret) |

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
- A full `totk.exe` / installer **prove** run needs extracted game + lifted `generated/v143`.
  Those inputs are **private** (repository secret URL or private artifact), never committed.
- Releases are produced only by Actions on tag `v*`.

### Workflows

#### `.github/workflows/ci.yml` (push + PR)

| Job | Runner | What |
|---|---|---|
| content-guard | ubuntu | `python tools/check_content.py` (same as upstream) |
| unit-no-game | ubuntu | `pack_shader_cache_test.py`, `spin_waits_test.py`, `worktree_test.py` (stdlib / no game; `capstone`, `lz4`) |
| unit-py314 | ubuntu | `installer_test.py`, `ncz_test.py`, `romfs_prompts_test.py` (Python 3.14 + pinned reqs) |
| unit-windows | windows | `flags_check.py`, `excl_mt.py` (MSYS2 clang; `capstone`, `unicorn`; expands later) |

Not in CI (need game files or huge toolchains): `jumptables_check.py`, `diff/lift.py`, `diff/func.py`,
`installer_e2e.py`, full `make_release.py --prove`. Track them in Status when a private runner exists.

#### `.github/workflows/release.yml` (tag `v*` or `workflow_dispatch`)

Windows job, high-level:

1. Checkout + setup Python 3.12 / 3.14 + `uv` venvs (pinned `tools/requirements*.txt`).
2. Setup MSYS2 UCRT64 toolchain (clang, lld, ninja, …) and .NET for `totk_xlate` if the prove path needs it.
3. **Restore private game inputs** (when secret present):
   - `GAME_INPUTS_URL` (+ optional `GAME_INPUTS_TOKEN`): zip containing `generated/v143/**` and, for `--prove`,
     `build/extracted/v143/**` (or a slim subset documented when seeding).
   - Unpack into the workspace paths `make_release.py` expects (`MAIN_ROOT` layout).
4. `python tools/build_xlate.py` (shader translator; no game bytes in the repo).
5. `python tools/make_release.py --out build/release/TotK-Setup --zip build/release/TotK-Setup-<ver>-windows.zip`
   (add `--prove` only when extracted game inputs are present).
6. `python tools/check_content.py` over the release folder (script already does this).
7. Create GitHub Release for the tag and upload the zip (+ sha256).

If `GAME_INPUTS_URL` is unset: the release job **fails with a clear message** (or is skipped when only
`workflow_dispatch` without inputs) so a tag never publishes a half-broken installer silently.

### Seeding `GAME_INPUTS` (one-time, outside this repo)

Game-derived trees must stay private. Recommended:

1. Zip `generated/v143` (+ optional `build/extracted/v143` for prove) from any machine that already extracted
   the game — **not** from a new local build on the developer laptop if the rule is “no local builds”;
   use an existing archive or a private runner workspace.
2. Store the zip as a **private** release asset on a private companion repo, or any URL Actions can fetch.
3. Set repository secrets `GAME_INPUTS_URL` and, if needed, `GAME_INPUTS_TOKEN` (fine-grained PAT: read-only
   on that private repo).

Until secrets exist: CI stays green on every push; release workflow is ready but will not publish a zip.

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

Entry (interim, from row-copy and refresh code): `+0x0` option id (copied to row+0x48),
`+0x4` kind (0/1/2), `+0x8` default candidate (→ row+0x50), `+0x10` choice count, `+0x20` value
type (→ row+0x54).

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

**Remaining Phase 2 verify items**: back/close virtual (candidates among slots 86/92/98); exact eui
input dispatcher that calls slot45/49 (framework-internal, not required if hooks sit lower);
**widget budget** — `build_rows` looks up indexed widgets (`Pa_ToggleBtn_%02d`, `Pa_SelectBtn_%02d`)
from the bgyml layout, so runtime rows are capped by how many instances the layout defines (count
unknown until the SystemOption layout bgyml is extracted — single-file extraction from the base NSP,
or probe at runtime and fail soft).

### Phase 2 — Generator host-call hooks

Today `recomp/patches.py` replaces **instruction words**; a replacement may end in `bl` to another guest
address. SavageWind instead injects **C** (`{ extern void rt_opt_rows(void); rt_opt_rows(); … }`) at lift time.

Approach (pick during implementation, prefer smallest diff):

1. **Preferred:** extend `recomp/gen.py` with a veneer mechanism: a `site` line may name a host symbol;
   the emitter writes a tiny generated C stub the guest `bl` reaches (same pattern as existing `rt_spin_gate`
   for spin waits — precedent already in-tree).
2. **Alt:** keep pure word patches that branch into a runtime-owned page of guest-callable thunks (more fragile).

Deliverable: patches-v143.txt lines + gen/patches.py support for at least:

`screen_open_request`, `screen_open`, `build_rows`, `flag_type`, `read`, `store`, `reset`, `msg_lookup`,
`decide`, `tick`, `back` (+ title/pause tick if page-switch needs reopen).

**1.4.3 hook site map (from Phase 1 findings; `main+0x…`)**

| hook | site | notes |
|---|---|---|
| `screen_open` | factory vtable slot100 `+0x218d58c`, or layout slot83 `+0x218c114` | after ctor `+0x218b9a0` |
| `build_rows` | vtable slot84 `+0x218bb10` | allocs entry list + row arrays from table count; widget lookup by name |
| `flag_type` | entry layout inside `+0x218bb10` (entry+0x4 = kind 0/1/2) | |
| `read` | generic getter `+0x980288`; row sync `+0x212bf04` | |
| `store` | choice `+0x212c1e0` → `+0x170210`; toggle `+0x212c818` → `+0x923d20` | |
| `reset` | none in game → runtime-owned (write entry defaults, entry+0x8 candidate) | |
| `msg_lookup` | apply `+0xba1730`; key build `+0xf4444` / `+0xba42c0`; sheet `LayoutMsg/SystemOption_00` | |
| `decide` | vtable slot49 `+0x218d274` | dispatches per kind |
| `tick` | vtable slot88 `+0x218c5c0`; dirty refresh `+0x2001914` → `+0x218d3e0` | |
| `back` | TBD (slots 86/92/98 candidates) | open-request bit +0x175 already known from `menu_cursor.c` |
| pause/title entry | row build also reachable via `+0x218bb10`; pause screens `+0x21679ec` / `+0x216720c` | |

Rows are fixed layout widgets (`Pa_ToggleBtn_%02d`, `Pa_SelectBtn_%02d`): runtime rows can only fill
widgets the bgyml defines — verify budget (single-file bgyml extraction or runtime probe) before
choosing inject-vs-remap strategy.

### Phase 3 — Runtime `runtime/ui/game_options.c`

Port the structure of `game_options.mm` to C, wiring to existing totk pieces:

- `Item` table built from `cfg` keys (`cfg_get` / `cfg_set` in `runtime/cfg.c`) for Screen / Game / Sound /
  Controls only.
- Pages: main section buttons → section pages; game’s own rows on Controls/Video stay (do not remove them).
- `g_entries` copy of game table + `g_order` per page; validate count/rows on init (fail soft like SavageWind).
- Messages: hook supplies UTF-16; reuse colour tags the game already understands; strings from `ui_text.c`
  where they already exist, new keys added for both en + existing languages (fallback en).
- Sub-actions that do not fit a row (window size list, restart prompt): either map to a KIND_BUTTON that
  delegates to existing runtime UI, or leave that setting on F1 (document per key in the item table).

Build integration: add the file wherever `runtime/*.c` is listed for `build_game.py` / release runtime lib
(follow existing menu sources).

### Phase 4 — Input

- Gamepad: native screen already moves `BoxCursor` — no work expected beyond not breaking `menu_cursor.c`.
- Mouse: existing click→A/B and focus follow should work on native rows; verify after Phase 3.
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
| Secrets never seeded → no automatic zip | Document seeding; release job fails with actionable error, CI still green |
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
- [x] Release workflow present (may fail closed until `GAME_INPUTS_URL` is set)
- [x] Old `naufalalamsyah453/totk-pc-recomp` confirmed gone / cleaned (404)
- [ ] First `v*` tag after secrets → Windows zip on Releases

## References

- SavageWind native options: `runtime/ui/game_options.mm`, `tools/patches.py` (upstream Revivedx/SavageWindRecomp)
- TotK menu cursor RE: `runtime/menu_cursor.c` (this repo)
- Patch mechanism: `recomp/patches.py` header (this repo)
- Content boundary: `AGENTS.md`, `tools/check_content.py`
- Upstream feature request: valentderah/totk-pc-recomp#242
- Build/release scripts: `tools/make_release.py`, `tools/build_xlate.py`, `docs/installer.md`, `docs/building.md`
