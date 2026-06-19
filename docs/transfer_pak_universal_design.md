# Universal Transfer Pak emulation — design spec

Status: proposed (2026-06-19). Owner: shared engine (N64ModernRuntime/librecomp +
N64Recomp). Driving game: PocketMonstersStadiumRecomp (PMS-J); also covers
PokemonStadiumRecomp (PSR-US) and future N64 recomps.

## 1. Goal & principle

Emulate the N64 Transfer Pak (a.k.a. GB Pak) **once, in the shared engine**, behind
libultra's standard accessory names, backed by **one game-agnostic GB-cart model** —
instead of a per-game shim in each app. This deletes PSR-US's bespoke
`src/main/transfer_pak.cpp` and makes the Transfer Pak work for any game that either
(a) uses standard libultra accessory names, or (b) routes through the universal SI
primitive `__osSiRawStartDma`.

Aligns with the project doctrine: build the seam that works for **every** site, not
just the one under investigation.

## 2. Why these seams (background)

librecomp already HLE's the controller / EEPROM / Controller-Pak libultra APIs by
name (`cont.cpp`, `eep.cpp`, `pak.cpp`), bound automatically through N64Recomp's
`reimplemented_funcs` set (`N64Recomp/src/symbol_lists.cpp`). The Transfer Pak is the
one device class never implemented there. The relevant accessory primitives —
`__osContRamRead`, `__osContRamWrite`, `__osPfsGetStatus`, `__osSiRawStartDma`,
`__osSiRawReadIo`, `__osSiRawWriteIo` — are currently in N64Recomp's **`ignored_funcs`**
set (emitted as `<name>_recomp` externs with **no** librecomp body), which is exactly
why each app supplies its own. Moving them to `reimplemented_funcs` + giving them a
shared librecomp body is the entire idea.

Every game funnels all accessory I/O through `__osSiRawStartDma` (writes the 64-byte
PIF command block, triggers the joybus). That is the single naming-agnostic chokepoint.

## 3. Architecture — three shared components

```
 app (launcher)  --register cart paths-->  [engine cart config]
                                               |
 recompiled game                              \|/
   |  jal __osContRamRead  ------> Level A HLE (gbpak.cpp)  --\
   |  (standard-named libultra)                                 >--> Port/Cartridge model
   |                                                          --/      (accessory proto + MBC + save)
   |  jal <custom block loop> -> jal __osSiRawStartDma -> Level B joybus processor (si_joybus.cpp) -/
   |  (custom-named libultra, e.g. PMS-J)
```

All three live in N64ModernRuntime. Components A and B both converge on the same
block-level API of component 1: `read_block(port, block_addr, out[32])` /
`write_block(port, block_addr, in[32])`.

### Component 1 — shared GB-cart model (`librecomp/src/gbcart.cpp` + `include/librecomp/gbcart.hpp`)

Promote PSR's `transfer_pak.cpp` `Port` + `Cartridge` verbatim (it is already
game-agnostic) into the engine. Two layers:

- **`Cartridge`** — GB ROM + battery RAM, MBC1/3/5 + ROM-only banking, header parse
  (`0x147` type, `0x149` RAM size), write-through `flush_save()`. (Unchanged from PSR.)
- **`Port`** — the Transfer Pak *accessory protocol*: the `addr & 0x7FFF` windows
  (`0x0000-1FFF` enable 0x84/0xFE, `0x2000-2FFF` bank select, `0x3000-3FFF` status,
  `0x4000+` cart window = `bank*0x4000 + off`), reset/power state machine. (Unchanged.)

Public C++ API (the only surface A and B use):
```cpp
namespace librecomp::gbcart {
    constexpr int block_size = 32;
    void set_cart(int port, const std::filesystem::path& rom, const std::filesystem::path& save); // register / clear
    bool has_pak(int port);                                  // a cart is configured
    int  read_block (int port, uint16_t block_addr, uint8_t out[32]);  // 0 ok / pfs err
    int  write_block(int port, uint16_t block_addr, const uint8_t in[32]);
    void flush_all();                                         // on quit
}
```
`block_addr` is the libultra block address (× 32 = the accessory byte address that
`Port::read/write` maps). Config source: an explicit `set_cart` (below) — NOT a file
read inside the engine, so the engine stays app-agnostic.

### Component 2 — Level A: libultra accessory API HLE (`librecomp/src/gbpak.cpp`)

`extern "C" <name>_recomp(uint8_t* rdram, recomp_context* ctx)` bodies, matching the
existing `pak.cpp` style, for the standard libultra accessory entry points:

- `__osContRamRead_recomp` (mq=r4, channel=r5, address=r6:u16, buffer=r7) →
  `gbcart::read_block(channel, address, blk)`; on success copy 32 bytes to `MEM_B(i,buffer)`.
- `__osContRamWrite_recomp` → read 32 bytes from `buffer`, `gbcart::write_block(...)`, `flush`.
- `__osPfsGetStatus_recomp` (mq=r4, channel=r5) → `gbcart::has_pak(channel) ? 0 : 1`.
- `osGbpakInit/GetStatus/ReadId/ReadWrite/Power/CheckConnector_recomp` → thin wrappers
  over the same model (only if a target game calls the high-level GB-Pak API directly).

These are bodies for names already in `ignored_funcs`; **promote them to
`reimplemented_funcs`** (§5) so librecomp's body is the default. (PSR keeps booting:
its app-level shim, if still compiled, overrides via object-file-wins; once verified,
delete the app shim.) Covers PSR-US and any standard-named future game with **zero
per-game code**.

The SI-DMA pacing helper PSR uses (`wait_for_cont_ram_transfer`, modeling SI latency
so the game's `osRecvMesg` ordering holds) moves here too.

### Component 3 — Level B: SI/joybus processor (`librecomp/src/si_joybus.cpp`)

`__osSiRawStartDma_recomp(dir=r4, dramAddr=r5)` — the universal chokepoint. The 64-byte
PIF command block lives in RDRAM at `dramAddr` (libultra DMAs it to/from PIF RAM
`0x1FC007C0`; we process it in place):

- **OS_WRITE (1):** stash/ignore (the command block is already in RDRAM at `dramAddr`).
- **OS_READ (0):** parse the PIF command block and fill responses, then it's read back.

Joybus frame walk (per channel, standard hardware wire format — same for every
libultra revision): each frame is `[tx][rx][cmd][tx data…][rx data…]`; `0x00` skip,
`0xFD` reset, `0xFE` end, `0xFF` pad. Dispatch on `cmd`:
- `0x00` **Info**: return device type + status; set the "pak inserted" status bit from
  `gbcart::has_pak(channel)`.
- `0x02` **Read accessory**: decode the 2 address bytes (top 11 bits = block, low 5 =
  addr-CRC), `gbcart::read_block(channel, block, out)`, write 32 data bytes + the
  computed **data-CRC** byte into the rx region.
- `0x03` **Write accessory**: decode address, take 32 data bytes, `gbcart::write_block`,
  write the data-CRC byte.
- `0x01` controller state: leave to the existing controller HLE path (Level B only
  needs to *not corrupt* it — see Risks).

CRCs (`__osContAddressCrc` 5-bit / `__osContDataCrc` 8-bit) must be computed correctly so
the game's verification passes. Helper: a small `joybus_crc.{hpp,cpp}` (standard polynomials).

This makes PMS-J's custom `FUN_`-named block loop work through the same engine code as
everything else — it bottoms out at `__osSiRawStartDma`.

## 4. Cart-registration API (app ↔ engine)

The launcher already owns the config UI + writes `launcher.cfg`. Add a thin engine API
so the app registers carts (keeps file IO in the app, model in the engine):
```cpp
// ultramodern/include … input.hpp (or a new gbcart.hpp surface)
void ultramodern::input::set_transfer_pak(int port, const char* rom_path, const char* save_path); // "" clears
```
- App: on PLAY (or boot), read `launcher.cfg` / `PSR_TRANSFER_PAK_P*_ROM|_SAVE` and call
  `set_transfer_pak` per port. The existing `transfer_pak.cpp` config parser shrinks to
  this registration call.
- Presence source of truth becomes the engine model: `pak.cpp::osPfsIsPlug_recomp` and the
  Level-B Info command query `gbcart::has_pak(port)` directly (today osPfsIsPlug reads the
  app's `connected_device_info().connected_pak`; switch it to the model so app and engine
  agree). The app's `get_connected_device_info` can still report `Pak::RumblePak` as the
  presence stand-in for its own input bookkeeping.
- `flush_all()` on `ultramodern::quit()` (the SDL_QUIT path already calls
  `join_saving_thread`; add `gbcart::flush_all()` there). TP writes stay write-through too.

## 5. N64Recomp changes (`src/symbol_lists.cpp`)

Move from `ignored_funcs` → `reimplemented_funcs` (so librecomp's body is linked):
`__osContRamRead`, `__osContRamWrite`, `__osPfsGetStatus`, and (Level B)
`__osSiRawStartDma`. Leave `__osSiRawReadIo/WriteIo` ignored unless a game needs them.
This is a shared-engine change → re-gen affects every game, but the new bodies default to
"no pak / pass-through" when no cart is registered, so non-TP games are unaffected.

Caveat for `__osSiRawStartDma`: some games may rely on it for **non-accessory** SI (e.g.
boot-time PIF). The Level-B body must faithfully pass through controller-info/state and any
non-joybus use; gate the accessory handling to `cmd ∈ {0x02,0x03}` and otherwise reproduce
the stock behavior (or delegate to a minimal default). Validate per game (§7).

## 6. Per-game binding

- **PSR-US** (pret names): `__osContRamRead/Write`, `__osPfsGetStatus` already standard →
  Level A binds automatically once they're `reimplemented`. **Delete** PSR's
  `src/main/transfer_pak.cpp` + its CMake/source entry; replace its config load with the
  `set_transfer_pak` calls. No regen logic change beyond the shared symbol-list move.
- **PMS-J** (custom layout): name the one universal primitive in `symbols.toml`:
  `{ name = "__osSiRawStartDma", vram = 0x80052cc0, size = … }` (currently `FUN_80052cc0`).
  Re-gen → it becomes the HLE'd primitive → Level B handles its block loop. Drop the ported
  app `transfer_pak.cpp`; register carts via `set_transfer_pak` from the launcher config.
  (Optionally also name `FUN_80053a90` = the detect so the `DAT_800aaff9` gate is satisfied
  through the model rather than forced — confirm during impl.)
- **Future games**: standard-named → free (Level A). Odd layouts → name `__osSiRawStartDma`
  (one symbol) → free (Level B).

## 7. Validation plan

1. Unit: a host test that drives `read_block/write_block` against a known GB save + a
   golden joybus command block (CRCs match libultra's).
2. PSR-US: build with Level A (app shim deleted) → GB Tower R/B/Y boot + play (regression vs
   today's app-shim behavior). This is the cleanest oracle (it already works).
3. PMS-J: name `__osSiRawStartDma`, regen, build → drive to the party-import screen, import
   a JP cart, confirm the party loads + the GB save persists.
4. Stadium 2 + any non-TP game: boot + run (confirm the `__osSiRawStartDma` reimpl doesn't
   regress non-accessory SI). 0-divergence vs pre-change where a shadow oracle exists.
5. Save durability: quit mid-write, reload, confirm GB `.sav` intact (write-through + flush).

## 8. Phasing (each independently shippable)

- **P1** Cart model into the engine (`gbcart.{hpp,cpp}`) + `set_transfer_pak` API +
  `flush_all` on quit. No behavior change yet (no HLE wired).
- **P2** Level A (`gbpak.cpp`) + promote the 3 names to `reimplemented`. Migrate PSR-US
  (delete app shim) → validate on the working oracle. **This alone makes PSR-US engine-native
  and gives every standard-named future game the TP for free.**
- **P3** Level B (`si_joybus.cpp` + CRCs) + promote `__osSiRawStartDma`. Name it in PMS-J's
  symbols.toml, regen, validate the in-game import. **This is the piece that makes PMS-J
  (and any custom layout) work.**
- **P4** Delete PMS's app `transfer_pak.cpp`; the launcher only registers carts.

## 9. Risks / open questions

- `__osSiRawStartDma` reimpl is load-bearing (boot-time SI, controller path). Must pass
  through everything non-accessory; validate across all three games before promoting.
- Exact joybus command-block construction per libultra revision: wire format is
  hardware-fixed, but confirm PMS-J's read/write command bytes (decompile the read
  single-block under `FUN_80051f40`) match the `0x02/0x03` dispatch before relying on it.
- Whether PMS-J's detect (`FUN_80053a90` → `DAT_800aaff9`) is satisfied once Level B answers
  the Info/status command, or still needs naming/forcing. Resolve in P3.
- Controller-info status bits / Transfer-Pak device-type byte exact values (so the game's
  `osGbpak*`/detect recognizes a TP, not a Controller Pak) — pin during P3.

## 10. What is reused vs new

- Reused: PSR `transfer_pak.cpp` `Port`+`Cartridge` (→ gbcart), the launcher cart-picker +
  `launcher.cfg`, `connected_device_info` presence plumbing, N64Recomp's name-binding.
- New: `gbcart.{hpp,cpp}`, `gbpak.cpp` (Level A), `si_joybus.cpp` + `joybus_crc` (Level B),
  `set_transfer_pak` API, the symbol-list moves, one PMS-J symbol name.
