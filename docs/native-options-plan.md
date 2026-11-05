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
| 0.4 | CI workflow: content guard + tests that need no game files | pending |
| 0.5 | Release workflow: automatic Windows release on `v*` | pending |
| 0.6 | Push plan + workflows, verify CI green | pending |
| 0.7 | Clean old account repos (`naufalalamsyah453/totk-pc-recomp`) | pending |
| 1.1 | RE: find TotK 1.4.3 Options screen + option table | pending |
| 1.2 | RE: row build / read / store / reset / decide / msg lookup sites | pending |
| 2.1 | Generator: host-call patch sites (`recomp/patches.py` / `gen.py`) | pending |
| 3.1 | Runtime: `runtime/ui/game_options.c` (pages, items, cfg bridge) | pending |
| 3.2 | UTF-16 message injection + `ui_text.c` coverage | pending |
| 4.1 | Input: gamepad + mouse→A/B on native rows | pending |
| 4.2 | In-game smoke on 1.4.3 (via CI artifact or issue checklist) | pending |
| 5.1 | Map cfg keys → native rows; leave F1 for the rest | pending |
| 6.1 | Port hooks to 1.2.1 (optional, after 1.4.3 works) | pending |
| 7.1 | Green CI on main | pending |
| 7.2 | First automatic Windows release tag | pending |

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
| unit-no-game | ubuntu | `installer_test.py`, `pack_shader_cache_test.py`, `spin_waits_test.py`, `worktree_test.py` (stdlib / no game) |
| unit-py314 | ubuntu | `ncz_test.py`, `romfs_prompts_test.py` (Python 3.14 + pinned reqs) |
| unit-windows | windows | `flags_check.py`, `excl_mt.py` (MSYS2 clang; expands later) |

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

- [ ] Workflows committed and pushed
- [ ] First CI run green on `main`
- [ ] Release workflow present (may fail closed until `GAME_INPUTS_URL` is set)
- [ ] Old `naufalalamsyah453/totk-pc-recomp` confirmed gone / cleaned
- [ ] First `v*` tag after secrets → Windows zip on Releases

## References

- SavageWind native options: `runtime/ui/game_options.mm`, `tools/patches.py` (upstream Revivedx/SavageWindRecomp)
- TotK menu cursor RE: `runtime/menu_cursor.c` (this repo)
- Patch mechanism: `recomp/patches.py` header (this repo)
- Content boundary: `AGENTS.md`, `tools/check_content.py`
- Upstream feature request: valentderah/totk-pc-recomp#242
- Build/release scripts: `tools/make_release.py`, `tools/build_xlate.py`, `docs/installer.md`, `docs/building.md`
