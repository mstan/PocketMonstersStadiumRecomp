# PocketMonstersStadiumRecomp

Static recompilation of **Pocket Monsters Stadium (Japan)** — the original
Japanese release that predates the international *Pokémon Stadium* — to a
native PC program.
Built on top of [N64Recomp](https://github.com/N64Recomp/N64Recomp).

This turns the original game into a native PC executable instead of running
it in an emulator. Most of the work is teaching the recompilation toolchain
about this one specific ROM. It is a sibling of
[PokemonStadiumRecomp](https://github.com/mstan/PokemonStadiumRecomp) (the
US *Pokémon Stadium* recomp) and shares the same companion forks:

- [N64Recomp](https://github.com/mstan/N64Recomp) — the static recompiler
- [N64ModernRuntime](https://github.com/mstan/N64ModernRuntime) — the runtime that stands in for the N64's operating system
- [rt64](https://github.com/mstan/rt64) — the graphics renderer

All three track the `main` branch; the exact commit this tree builds and
boots against is recorded in [`n64recomp.pin`](n64recomp.pin).

> **Releases:** an experimental Windows build is published under
> [Releases](../../releases) (**v0.0.1**, very early — see the caveats there).
> No ROM is bundled; you supply your own Pocket Monsters Stadium (J) `.z64`.

> **Why a separate repo from PokemonStadiumRecomp?** PMS-J is the same engine
> family as US Pokémon Stadium (near-identical boot layout — `Idle_ThreadEntry`
> at the same address `0x80000460`), which makes PSR's pret-decompiled source an
> excellent oracle for identifying unnamed PMS functions. But the ROM, symbol
> map, and game logic differ, so it gets its own recompilation tree.

## Status

**Boots to the interactive main menu.** From a cold start the recomp runs
the full resident init (cooperative scheduler, libleo no-drive resolution,
audio engine, resource server), builds graphics tasks, and renders the
iconic terminal / Poké Ball main-menu scene — including the Japanese
"no cartridge inserted" prompt and the オプション (Options) button.

It now boots through the title (the PRESS START / njpeg background renders),
the cart-select screen, the main menu, and into the 3D battle UI, with the
runtime English-translation layer applied. The bring-up trail — the boot
deadlock (scheduler starvation on loop back-edges), truncated delay-slot
corruption, the libultra identifications, and the resource-server layer — is
documented in [`FINDINGS.md`](FINDINGS.md).

**Very experimental — expect bugs.** Known issues (in progress):
- **GB mode / GB Tower does not work properly** — it currently mis-flags
  valid Game Boy save data as "corrupt" and won't load imported Pokémon.
- The English translation is **incomplete** — many strings are still Japanese,
  and some translated text may be wrong or mis-sized.
- Some UI sprites (arrow/hand) still have transparency artifacts, and deeper
  play paths can hit crashes/hangs.

## English translation patch

PMS-J ships only in Japanese. This project includes an **optional runtime
English-translation layer** that replaces the game's on-screen Japanese with
English as it is drawn — no ROM modification, no asset re-packing. The main
menu, battle/tournament setup, cup and rule descriptions, rental Pokémon,
moves, types and stat screens are translated (269 strings and growing).

**How it works (short version):** the recompiled game funnels almost all of
its text through one string-draw routine. A hook reads the source bytes at
that routine, hashes them, looks the hash up in a small JSON table
([`translations.json`](translations.json)), and on a hit re-renders the
English replacement through the game's own font glyphs (the font already
contains Latin letters, so no glyph injection is needed). Untranslated text
falls through unchanged, and the table hot-reloads while the game runs.
Replacement text is not bounded by the original length, and Latin spacing is
tightened so English fits the Japanese text boxes.

The hook is built on the always-on diagnostic-ring + per-function trace
infrastructure in the runtime fork. **For the full design and internals, see
the [N64ModernRuntime fork](https://github.com/mstan/N64ModernRuntime#runtime-text-translation-hook)**
— the runtime that exposes the guest-memory access and trace surface this is
built on.

**Using / extending it:**

```bash
# Translations live next to the executable as translations.json (the repo
# root copy is the canonical data). Regenerate it from the source table any
# time — it hot-reloads, no rebuild needed:
python tools/pms_build_translations.py build      # tools/key_en.tsv -> build/translations.json

# Capture the strings on screens you visit (always-on; survives crashes),
# then list what still needs translating and add lines to tools/key_en.tsv:
python tools/pms_build_translations.py decode     # accumulate -> tools/strings_decoded.tsv
python tools/pms_build_translations.py todo       # -> tools/untranslated.tsv (key <TAB> Japanese)
```

Run with `PMS_TEXTPROBE=1` to arm string capture (and the `textdump` /
`stringdump` / `fontdump` debug-server commands). `PMS_XLATE_GAP` tunes the
letter spacing. The capture/translate tooling lives in [`tools/`](tools/).

## ROM

| Field  | Value |
|--------|-------|
| Title  | Pocket Monsters Stadium (Japan) |
| MD5    | `c46e087d966a35095df96799b0b4ffae` |
| Size   | 16,777,216 bytes (16 MB) |
| Format | `.z64` (big-endian native, magic `80 37 12 40`) |

The ROM is commonly distributed byte-swapped as `.v64`; convert it to native
big-endian `.z64` before use. The original game's data is **not** included —
a legal copy of your own dump is required to build or run this project.
Place it at the repo root as `baserom.z64` (gitignored).

## Layout

```
├── baserom.z64           # canonical PMS-J ROM (gitignored — bring your own)
├── lib/                  # engine forks, junctioned by setup (gitignored)
│   ├── N64ModernRuntime/ # runtime (embeds N64Recomp submodule)
│   └── rt64/             # graphics renderer
├── ghidra/               # Ghidra seed project (live .rep gitignored)
├── generated/            # recompiler C output (gitignored — regen via N64Recomp)
├── rsp/                  # RSP microcode reimplementations (aspMain, njpgdsp)
├── src/main/             # app entry, render context, debug server, diagnostics
├── include/              # app headers
├── tools/                # game-specific tooling (libultra matcher, symbol gen,
│                         #   Ghidra seed/export, delay-slot fixer, fuzz driver)
├── game.toml             # N64Recomp config (patches + build decisions)
├── symbols.toml          # symbol map
├── functions.json        # function-entry list (symbols-file build mode)
├── function_*.txt        # add/drop/merge/rename/size overrides
├── aspMain.j.1.0.toml     # RSP audio microcode descriptor
├── aspmain_combined.bin   # combined audio microcode blob
├── n64recomp.pin         # companion-fork SHA pins
├── CMakeLists.txt        # build entrypoint
├── setup.sh / setup.bat  # provisioning (junctions lib/, inits submodule)
├── FINDINGS.md           # reverse-engineering log
└── README.md
```

## Quick start

```bash
# 1. Provision the engine forks under lib/ (junctions the sister checkouts
#    ../N64ModernRuntime and ../rt64 if present, else clones them).
./setup.sh           # Windows: setup.bat

# 2. Drop your verified PMS-J ROM at the repo root as baserom.z64.

# 3. Configure + build.
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# 4. Run (auto-selects baserom.z64 from the working directory).
./build/PocketMonstersStadiumRecomp        # Windows: build\PocketMonstersStadiumRecomp.exe
```

Useful environment knobs: `PMS_VOLUME` (master volume 0–1),
`PMS_DEBUG_PORT` (enable the scripted debug server), `PMS_BOOT_WATCHDOG=1`
(post-mortem ultra-trace dump on stall/abort).

## Acknowledgements

- [pret/pokestadium](https://github.com/pret/pokestadium) — the US Pokémon
  Stadium disassembly that serves as the engine oracle for PMS-J.
- the [ares](https://ares-emu.net/) emulator team — accuracy reference.

## License

PocketMonstersStadiumRecomp is distributed under the **GNU General Public
License, version 3** — see [`COPYING`](COPYING).

It is built from several components under their own licenses, whose terms
and copyright notices are retained in their respective repositories:

- N64ModernRuntime — GPL-3.0 (`COPYING`)
- N64Recomp — MIT (`LICENSE`)
- rt64 — MIT (`LICENSE`)

The original game's assets are **not** included; a legal copy of the
Pocket Monsters Stadium (J) ROM is required to build or run this project.
