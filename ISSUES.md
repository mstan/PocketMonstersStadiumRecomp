# Open issues — PocketMonstersStadiumRecomp (PMS-J)

Living list of known gaps for the Japanese Pocket Monsters Stadium
recomp. Use this as a pre-flight before starting a work session.

Sibling reference: `../PokemonStadiumRecomp/ISSUES.md` (Pokemon Stadium
US) — the same engine, further along. Many fixes here are ports of
fixes already landed there.

## Current state (2026-06-13)

Boots to the interactive **main menu**. English runtime-translation
layer ships (269 strings; see README "English translation patch").
Navigation works through the main menu, GB/cartridge prompts, Battle /
Free Battle / Tournament mode select + descriptions, cup select, rule
screens, trainer select, rental Pokémon, movesets, types, stat labels.

**Crashes on deeper screens** (the active priority this session):
navigating into Options, or into an actual battle from Free Battle,
aborts the process. Root cause is shared (see #1).

## Priority order (user-set 2026-06-13)

The focus this session is **session stability**, then feature parity
with the US build (Transfer Pak, SS Anne launcher) and the translation
settings UI. Translation *coverage* work is paused but new strings
encountered during stability testing get localized opportunistically.

1. **#1** Deep-screen crashes (Options; Free Battle → match) — overlay/
   fragment lookup-miss. **Top priority.**
2. **#2** GB Transfer Pak support (port from US; Japanese carts).
3. **#3** SS Anne launcher port (+ language-override settings page, #4).
4. **#4** Translation language-override settings (Off / Original / English / …).
5. **#5** PRESS START background image never renders.
6. **#6** Translation coverage + polish (paused; see README/handoff).

Issue numbers are stable IDs, not strictly the work order.

---

## #1 — Deep-screen crashes: overlay/fragment lookup-miss — **OPEN (top priority)**

*Symptom.* Two user-reported reliable crashes, plus a general class:
- **Options** from the title/main menu → crash.
- **Free Battle** → pick a party → enter the match → crash.
- General: sweeping into deep screens aborts the runner
  (`func_82004E9C` / `func_820058BC` in `generated/funcs_114.c`).

*Evidence (last_error.log).*
- `get_function lookup miss: 0x82117ED4` and `0x80115490` —
  "no loaded code section contains this as runtime or link-time address".
  The bad target window in RDRAM is **all zeroes** (overlay code never
  populated that region), and execution falls into the post-call
  lookup-miss trampoline.
- A separate run shows a ~3800-deep call-trail dump out of
  `func_82004E9C`/`func_820058BC` → host-side runaway (stack overflow
  class), structurally similar to US issues #7/#11 (constant-SP tailcall
  depth) but presenting on PMS's overlay path.

*Root cause (HIGH confidence on the mechanism).* PMS overlay/fragment
sectioning is incomplete. `src/main/register_overlays.cpp` registers
**only the resident kernel** (one static section @ `0x80000400`,
`num_sections = 1`). A runtime fragment registrar exists
(`[reg-frag]`/`[synth-frag]` synthesized sections, with eviction), and
it covers many fragments, but some overlay call targets
(`0x82117ED4`, `0x80115490`, …) are never registered → `get_function`
misses → trampoline/abort. The US build sidesteps this entirely because
its 77 fragments have unique VRAM addresses and are statically
recompiled into one ELF (US CLAUDE.md decision #6); PMS has not yet
reproduced that static sectioning.

*Fix layer.* Recompiler / `game.toml` sectioning (NOT an app-side
patch). Two candidate paths, prefer the complete one:
  1. **Static sectioning** (preferred, mirrors US): give every overlay/
     fragment its own recompiled section so `get_function` always
     resolves — no runtime synthesis needed for the missing ones.
  2. Extend the runtime synth-frag registrar to cover the missed
     targets (only if static sectioning proves infeasible for PMS's
     fragment layout).

*Discipline.* This is a load-bearing recompiler/sectioning change —
**branch the forks first**; do not regress the booting build. Verify
with the exact repro routes (Options; Free Battle → party → match).

*Status.* Diagnosed, not fixed. Key addrs: miss targets `0x82117ED4`,
`0x80115490`; diverging funcs `func_82004E9C`/`func_820058BC`
(funcs_114.c). Capture fresh `last_error.log` on each repro.

---

## #2 — GB Transfer Pak support — **OPEN (not yet implemented)**

*What.* The N64 accessory that imports a Gen-1/2 party from a real Game
Boy cart. Fully implemented in the US build
(`../PokemonStadiumRecomp/src/main/transfer_pak.{cpp,h}`: accessory
state machine + ROM-only/MBC1/MBC3/MBC5 cart model + battery-RAM save
persistence; three libultra shims `__osContRamRead`/`__osContRamWrite`/
`__osPfsGetStatus` route bus traffic into it). **None of this exists in
PMS yet** (PMS `main.cpp` reports ports 1-3 absent, `Pak::None`).

*Scope note (region).* PMS is the Japanese game, so support targets the
**Japanese carts** — Pokémon Red / Green (and Blue (J) / Yellow (J) if
the game accepts them; needs verification — PMS may predate Yellow).

*Assets.* No Pokémon **Green** icon exists (US shipped Red/Blue/Yellow,
ChatGPT-generated). Need a placeholder (Bulbasaur-based) until a proper
one is supplied.

*Dependency.* The import/registration flow lives on a deep screen, so
this likely needs **#1** resolved (or at least the registration path
reachable) to exercise end-to-end. The accessory code itself is
independent and can be ported first.

*Status.* Not started. Port path identified.

---

## #3 — SS Anne launcher port — **OPEN (not yet implemented)**

*What.* The in-app first-screen launcher (RmlUi/recompui) shipped in the
US build (`../PokemonStadiumRecomp/src/ui/` tree + `ui_launcher.cpp`):
game/art from the cart header, interactive enable toggles, per-slot
controller assignment, PLAY gate, per-port input routing, PLAY →
start_game; launcher-first by default with an autoboot bypass env.
**PMS has no `src/ui/` and boots straight into the game.**

*Minimum viable.* Even before the full launcher: a screen that checks
the ROM SHA and launches (the runner already does ROM validation in
`main.cpp`; the launcher would front it with UI).

*Adds for PMS.* Beyond US feature parity, the launcher should host the
**translation language-override settings page (#4)**.

*Dependency.* Pulls in the RmlUi/recompui dependency stack (forked in US
from Zelda64Recomp). Large port; CMake + deps + the full `src/ui/`
element library.

*Status.* Not started. Architecture for the settings page tracked in #4.

---

## #4 — Translation language-override settings — **OPEN (design)**

*What.* A launcher settings page to choose the text language override,
persisted across runs and applied to the runtime translation hook. Must
include an **Off / Original Language** option (no override → native
Japanese). Today only English exists; design for N languages.

*Current mechanism.* The translation layer is a content-hash KV hook
riding the universal `TRACE_ENTRY` at the string-draw routine
(`0x8001A944`), loading `translations.json` next to the exe (see
README). It is currently always-on (English) and depends on
`trace_mode = true`.

*Design questions (to settle with the user — see #4 architecture note):*
  - Where settings persist (launcher.cfg vs a dedicated config).
  - How a language maps to a translation file (e.g. `translations.en.json`,
    `translations.<lang>.json`; Off → load none).
  - Live-switch vs restart-to-apply.
  - Decoupling from `trace_mode` for a clean release (#6 / release prep).

*Status.* Design in progress; awaiting architecture sign-off.

---

## #5 — PRESS START background image never renders — **OPEN (likely easy)**

*Symptom.* On the title/PRESS START screen, the background image that
shows in the US build / on hardware does not appear in the PMS recomp.

*Notes / leads.* Not a missing JPEG microcode — both `aspMain` and
`njpgdspMain` RSP microcodes are recompiled (`rsp/`) and wired in
`get_rsp_microcode`. Candidates to investigate with a live repro +
screenshots: title-screen background resource load (resource-server
PRESJPEG path in FINDINGS BOOT-003), a fragment/overlay that holds the
title art (could overlap #1), or an RT64 compositing/layer issue.

*Status.* Not yet root-caused; needs a live run + screenshot.

---

## #6 — Translation coverage + polish — **PAUSED**

Tracked in the session handoff and README. Outstanding: coverage gaps on
unvisited screens (battle/results, full rosters, Pokédex/Party/Options,
GB Tower, save/load), typewriter send-out generalization, long-string
wrapping, dynamic `%s`/`%d` verification, streamed-font spacing,
nickname localization quality. Method: `PMS_TEXTPROBE=1` capture →
`tools/pms_build_translations.py` decode/todo/build → author into
`tools/key_en.tsv`. Paused in favor of stability; new strings hit during
testing get localized opportunistically.

---

## Release prep (tracked, not blocking dev)

- **trace_mode decoupling.** The translation hook rides `trace_mode =
  true`. For a clean release either keep it on (current) or migrate to a
  reimplemented replacement of `0x8001A944` (full reimpl required —
  `reimplemented` emits no original body). See #4.
- **GPLv3 compliance.** Any binary release must ship corresponding
  source at the build commit + all component license files (PSR is
  GPLv3). See memory `project_gpl_release_compliance.md`.
</content>
</invoke>
