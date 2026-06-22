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
7. **#7** UI image transparency — buttons (A/B/L/R/S icons) and the hand
   pointer render with opaque white borders instead of transparent.

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

*Root cause (HIGH confidence — see FINDINGS CRASH-001).* Not missing
sectioning — the fragment **is** recompiled and registered. The miss is
an **undiscovered function entry**: `0x82117ED4` (offset `0x17ED4`) lies
strictly *inside* `func_82117DD0`'s body (`rom_size 0x968`,
`0x17DD0..0x18738`) in `generated/recomp_overlays.inl`. It's a separate
function the game calls indirectly (`jalr`), but PMS's discovery tools
only find prologues in **uncovered gaps** and **cannot resolve indirect
calls / jump tables**, so an indirect-only entry absorbed into an
over-extended neighbor is invisible. `func_82117ED4` exists in no
generated file → `get_function` misses → trampoline/abort.

*Fix layer.* Fragment function-entry discovery + regen (NOT a runtime
patch). Prefer the general option:
  1. **Static, general (preferred):** scan each fragment for the non-leaf
     prologue pattern *everywhere*, and **split** any enclosing function
     when a prologue is found in its interior; optionally seed from
     in-fragment code pointers / relocations (the actual `jalr` source).
     This is the US fix class ("seed from relocation/indirect targets").
  2. **Execution-driven (narrower):** harvest runtime miss addresses
     across a deep-screen sweep, add as explicit entries, regen.
     Whack-a-mole; misses unvisited screens.

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

*RmlUi cart-selection menu (user-requested 2026-06-18).* Plumb an
RmlUi/recompui menu (part of the #3 launcher) that lets the user pick
which GB cart to mount in the Transfer Pak for the GB Tower (R/B/Y →
their Japanese equivalents). **Region constraint:** PMS-J's GB Tower is
expected to accept only the **Japanese** versions of those carts
(Pocket Monsters Aka/Midori/Ao/Pikachu = Red/Green/Blue/Yellow J), so
the picker must validate/limit selections to JP ROMs — needs
verification of exactly which carts PMS accepts. Today the cart is
chosen only via env (`PSR_TRANSFER_PAK_P1_ROM/_SAVE`, US model); this UI
replaces that with an in-app selector.

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

*Status.* Re-confirmed still missing by the user 2026-06-18. **Narrowed
(2026-06-18):** confirmed on screen — "PUSH START" renders (dark) on a fully
black background. Added RSP-task logging (`get_rsp_microcode` in `main.cpp`):
the JPEG decode **task fires every frame** (`M_NJPEGTASK -> njpgdspMain`,
data=0x80082740, dlen=0x20) and `njpgdspMain` (a real 1,614-line recompiled
decoder, `rsp/njpgdspMain.cpp`) runs **cleanly — no "Unhandled jump target"
errors**. So the decode is invoked and completes; the title art is missing
because the decoded image is **not being drawn / composited**, not because the
resource is unloaded or the ucode is a stub. → Next: trace the background draw
in RT64 (`RT64_CIMG_LOG` / RDP log) — the decoded CIMG/framebuffer likely is
not scanned out / composited. This is the RT64-compositing candidate from above.

**ROOT-CAUSED + partial fix (2026-06-18):** the title art is a JPEG decoded
**MCU-by-MCU into YUV (G_IM_FMT_YUV, 16-bit) buffers** (njpeg output at
~0x26C9E0+, +0x200/MCU) and drawn via the display list as ~329 YUV texrects
(confirmed with RT64 `RT64_CIMG_LOG` setColorImage + a new setTextureImage
log). **RT64 did not support YUV** — `rt64_rdp.cpp` loadTile/loadBlock have
`assert(fmt != G_IM_FMT_YUV)` and the texture shader `TextureDecoder.hlsli`
stubbed every YUV case to `float4(0,0,0,1)` (black). Implemented a YUYV 4:2:2
-> RGB (BT.601) decode in `sampleTMEM` (16-bit YUV case). Result went **black
-> green**: the decode runs but reads **zero texels** (green is the exact
output for Y=0,chroma=0, independent of byte order), so the YUV data is not
reaching the shader's TMEM read. **Remaining:** dump the actual TMEM bytes for
a YUV texrect to pin the real layout (interleaved YUYV vs planar) and/or fix
the texel-delivery path (loadTile/loadBlock YUV stride, or a framebuffer-
overlap redirect to an empty FB). Diagnostics added: RT64 setTextureImage log
(RT64_CIMG_LOG), PMS njpeg task-struct dump in get_rsp_microcode.

**DEEPER BLOCKER FOUND (2026-06-18).** The green is **zero texels at the
source**: the YUV texrects `loadBlock` from `0x26C9E0` (= the njpeg task's w0
output addr) and the load runs, but the loaded TMEM bytes are **all 00**, i.e.
`RDRAM[0x26C9E0]` is zero. So **the recompiled `njpgdspMain` is not writing
decoded output to RDRAM** — the JPEG never actually decodes into the buffer the
game draws. (It dispatches via get_rsp_microcode and logs no "Unhandled jump",
but that does not prove it executes + writes.) So #5 needs TWO fixes: (1) RT64
YUV texture decode — **DONE** (`TextureDecoder.hlsli` sampleTMEM 16b YUV ->
BT.601 RGB; correct, will show the image once texels are non-zero); (2) make
the recompiled njpeg ucode actually produce output — **the real remaining
blocker** (RSP-ucode/runtime: confirm njpgdspMain executes via an app-side
wrapper around the fn ptr in get_rsp_microcode + inspect/repro its RDRAM
writeback; possibly an RSP DMA/output-writeback recompilation gap or a
task-completion/sync issue). Until (2), the title background stays green
(decoded-from-zero) rather than black. RT64 YUV `assert`s in loadTile/loadBlock
are debug-only (no-op in release).

**LAYER-2 PINNED (2026-06-18).** Wrapped njpgdspMain (njpeg_wrapper in
`get_rsp_microcode`, main.cpp) to inspect execution + its output buffer
(task w0) before/after. Result: the ucode **executes and returns
RspExitReason::Broke** (normal completion), but its **RDRAM output (w0,
0x26C9E0+) goes non-zero -> all-zero**: the recompiled njpeg writes ZEROS to
the buffer the game then draws. DMEM has ~1400-1700 non-zero bytes after the
run, but that is ambiguous (could be the leftover compressed/DCT *input*, not
decoded output). So layer 2 is inside the recompiled RSP ucode: either the
**output DMA reads a zero/wrong DMEM offset** (DMEM-source-address bug; see
`DO_DMA_WRITE`/`SET_DMA_MEM` at njpgdspMain.cpp ~1565-1575) or the **VU/IDCT
decode produces nothing**. aspMain (audio) uses the same DMA macros and works,
which leans toward the VU/decode or an njpeg-specific DMEM-address issue.
Fixing it requires tracing the recompiled njpgdspMain dataflow (where the
decode writes DMEM vs where the output DMA reads) -- deep RSP-recompilation
debugging, its own focused effort. Diagnostic njpeg_wrapper retained.

**DECODE FIXED (2026-06-18).** Layer 2 root cause: `rsp/njpgdspMain.cpp`
was generated from **Pokemon Stadium US's** njpgdsp and reused for PMS-J;
the binaries differ. PMS-J's runtime ucode (ROM 0x5F980 / 0x8005ED80)
diverges from the recompiled C at IMEM 0x0/0x18 and in the branch layout,
so running PS-US's code on PMS-J's data gave correct IDCT intermediates
(dense DMEM 0x560-0x850) but read mismatched DMEM constants in the color/
packing pass -> every output DMA wrote 0/512 nonzero. Root-caused with an
always-on RSP DMA trace ring + a gated VU store trace, then by diffing
PMS-J's runtime ucode IMEM against the recompiled C. FIX: regenerated from
PMS-J's OWN njpgdsp, recompiling rspboot+njpgdsp as a combined blob like
aspMain (`njpgdspMain.j.1.0.toml` + `tools/build_njpgdsp_combined.py`,
`text_size=0x960` bounds it to code). Now every njpeg run writes 512/512
nonzero and an offline render of the decoded buffer shows the real title
logo. Committed `be7987f` (branch `work/njpeg-decode-regen`). (The earlier
"IMEM 0x18 = SP_RD_LEN DMA loading constants" idea was a mis-decode — it's
`mtc0 SP_STATUS`; the cause was simply the wrong njpgdsp revision.)

**RT64 YUV color FIXED, layout REMAINS (2026-06-18).** With real output
flowing, the prior RT64 YUV decode showed solid green: it read Y from
byte0 / chroma from byte1, but the RDRAM format is `[chroma(byte0),
Y(byte1)]` (offline byte analysis: byte1 = luma gradient stdev~35, byte0
= near-neutral chroma ~0x85 stdev~6). Fixed the byte order + neighbor
offset in `TextureDecoder.hlsli` (committed rt64 `714875c` on
work/pocket-monsters-stadium). The title now "loosely resembles the real
image" (user-confirmed) but a **texrect layout/stride issue still garbles
the block placement** — #5 stays OPEN for that. Next: `RT64_CIMG_LOG` to
capture the YUV texrect params (size/format/coords/stride) and whether the
16x16-block-linear buffer is being sampled as a flat raster.

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

## #7 — UI image transparency (buttons / hand pointer) — **OPEN (render)**

*Symptom (user-confirmed 2026-06-18).* The on-screen button icons (the
A / B / L / R / S prompt badges) and the **hand pointer** cursor render
with **opaque white borders** instead of transparent edges — the sprite's
transparent surround shows as a white box around the art.

*Likely cause (to verify).* A texture-format / alpha path issue: these are
small 2D HUD sprites, probably CI (palette) or IA/I textures where the
transparent index / alpha is not being honored (rendered opaque white).
Could be the TLUT/format the game sets, the combiner/render-mode, or an
RT64 decode/blend path. Render-domain (RT64 is fair game — not gated).

*Method.* Screenshot the menu (hand pointer) + a battle/lead-select
(A/B/L/R badges). Inspect the texture load (format/TLUT) for those sprites
via RT64's texture/CIMG logging; compare against a correctly-transparent
sprite. Same family as the long-standing "pointer-hand transparency"
note in project memory.

*Reproduced 2026-06-18 (deep menu).* Reached the Free-Battle lead-select
("Select Entry Pokemon") and confirmed the issue: the small UI nav-arrow
sprites (the ▼ / ▶▶ paging arrows, the ◄◄ button) render with a faint
opaque box border around them, while the **Pokemon icon sprites in the
same grid render correctly transparent**. So it is a per-sprite texture
format / render-mode difference, NOT a global blend bug — some sprite
class (the button/arrow/badge family) decodes its transparent surround
as opaque, the Pokemon-icon class does not. Next: capture the texture
load for an affected arrow vs a correct Pokemon icon (RT64 texture/CIMG
logging) and compare format/TLUT/render-mode; fix the transparency in
the decode/combiner path that the badge class uses.

*Navigation path to a repro screen* (boot is non-deterministic + the
title cycles an attract demo; the debug-server START needs a long hold,
not a pulse): `hold:start:90` at PUSH START -> the no-cart console
screen -> `press:a` -> "No Pak" P-OS menu (Battle highlighted) ->
`press:a` (Battle) -> `press:a` (Enter? Yes) -> `press:a` (Free Battle)
-> `press:a` (Battle) -> `press:a` (Select Opponent: COM) -> `press:a`
-> lead-select. The ▼ / ▶▶ arrows at the bottom show the border. The
clearer A/B/L/R battle prompts are one step deeper (in-match), behind
the #1 enter-match crash.

*Status.* Characterized + reproduced + navigation path documented; the
affected sprite class is isolated (button/arrow/badge vs Pokemon icon).
Not yet root-caused at the texture-format level.

---

## #8 — Universalize the runtime text hot-swap into the engine — **OPEN (follow-up)**

*Today it is PMS-J-only.* The on-the-fly English text hot-swap (content-hash a
drawn string -> look it up in `translations.json` -> repoint the format arg so the
game renders the replacement, with the table **hot-reloaded** on mtime change)
lives entirely in this repo: `src/main/diagnostics.cpp`
(`pkmnstadium_text_xlate` / `pkmnstadium_textdraw_probe` / the `g_xlate` KV +
`load_translations_locked` / `maybe_reload_translations`) wired in via
`include/trace.h` on `trace_mode = true`. No part of it is in N64ModernRuntime /
N64Recomp, so **no other game in the ecosystem can use it.**

*Why it is not a clean lift.* Audited 2026-06-22: only ~20% is game-agnostic.
- **Reusable core (-> N64ModernRuntime as e.g. `recomp::text_xlate`):**
  `translations.json` load + mtime hot-reload; FNV/`src_hex` content-hash KV
  lookup; per-PC interception-site registration; an ABI-parameterized
  format-arg repoint (which GPR holds the string pointer is config, e.g. r6);
  the always-on string capture/census (`stringdump.log`).
- **Game-specific (must stay as game-provided callbacks):** EUC-JP decode; the
  fit-aware glyph **self-renderer** (re-renders English through the game's own
  font + glyph-draw routine, with spacing/fit tuning); the draw PC(s)
  (`0x8001A944` + the class-1/2 siblings) and their r4/r5/r6/r7 conventions;
  the disjoint scratch-slot management (`swap_scratch`).

*Proposed.* Extract the reusable core into N64ModernRuntime as a general,
game-agnostic facility: a game registers its draw-site PC(s) + the fmt-arg GPR
index + decode/render callbacks + the translations path; the engine owns the
KV, hot-reload, hashing, repoint, and capture. PMS-J becomes the **first
consumer**; future N64 recomps (and PokemonStadiumRecomp) then get the hot-swap
translation layer for free. List it as a new engine feature when landed.

*Status.* Investigated + scoped 2026-06-22. **Deferred by decision** (do the
shared-engine refactor deliberately, not mid-v0.0.1-release-cleanup, to avoid
regressing PMS's carefully-tuned rendering). Relates to the `trace_mode`
decoupling note below and #4 (translation settings UI).

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
