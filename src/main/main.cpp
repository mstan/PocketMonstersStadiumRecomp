/*
 * main.cpp — PocketMonstersStadiumRecomp runner entry point.
 *
 * Minimal-but-complete boot-to-menu runner, modeled on PokemonStadiumRecomp's
 * main.cpp. Deliberately omits the
 * post-boot subsystems PSR accreted — SS Anne launcher, Transfer Pak, GB Tower
 * microcodes, Ares oracle, TCP debug server, audio diagnostic rings — none of
 * which are on the first-boot critical path. Everything kept is implemented for
 * real; no stubs.
 *
 * What is intentionally NOT carried over from PSR and why:
 *   - on_init gExpansionRAMStart poke (PSR forces 0x80068B90 for Stadium-US's
 *     6MB pool path). That address is Stadium-US-specific; blindly applying it
 *     to PMS-J would corrupt an unrelated global. If PMS needs an expansion-pak
 *     nudge it is a post-first-boot triage item, found by measurement.
 *   - Recompiled RSP audio microcode (aspMain). PMS's ucode has not been
 *     recompiled yet (next milestone). get_rsp_microcode logs loudly and
 *     returns nullptr for now — honest "not yet built", not a silenced bug.
 */

#include <array>
#include <atomic>
#include <csignal>
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <dbghelp.h>
// Avoid Windows.h macro pollution clobbering identifiers in librecomp /
// ultramodern headers below.
#undef ERROR
#undef OUT
#undef IN
#undef OPTIONAL
#undef min
#undef max

#include <SDL.h>
#include <SDL_syswm.h>

#include "app_paths.h"
#include "debug_server.h"
#include "launcher_cfg.h"  // game-side reader for the launcher's launcher.cfg
#include "transfer_pak.h"  // emulated N64 Transfer Pak (GB cart bus)

namespace pms {
// Defined here (where <windows.h> is included with WIN32_LEAN_AND_MEAN /
// NOMINMAX) and shared via app_paths.h with the other translation units.
std::filesystem::path exe_dir() {
#ifdef _WIN32
    char buf[MAX_PATH] = {0};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    return std::filesystem::path(buf).parent_path();
#else
    return std::filesystem::current_path();
#endif
}

std::filesystem::path app_file(const std::string& name) {
    return exe_dir() / name;
}
} // namespace pms

#include "recomp.h"
#include <librecomp/game.hpp>
#include <librecomp/ultra_trace.hpp>
#include <ultramodern/ultramodern.hpp>
#include <ultramodern/error_handling.hpp>

#include "pms_render.h"

extern "C" void recomp_entrypoint(uint8_t* rdram, recomp_context* ctx);
extern "C" void pms_dump_ultra_trace(const char* tag);  // boot-stall watchdog (diagnostics.cpp)

namespace pms { void register_overlays(); }
namespace pms::rsp { void register_pre_task_hooks(); }

extern RspUcodeFunc aspMain;
extern RspUcodeFunc njpgdspMain;

// ---- Runtime knobs live in pms::dbg so automation can inspect/mutate them. --

// ---- RSP microcode dispatch ------------------------------------------------
// PMS's audio/JPEG microcodes have not been recompiled yet (next milestone:
// identify the ucode boot signature in ROM and recompile with RSPRecomp, as PSR
// did for its aspMain). Until then, graphics tasks (M_GFXTASK) still render —
// they go through the renderer's send_dl, NOT through this dispatch — so the
// menu can draw. Audio/other tasks have no host microcode; we log the task type
// loudly (so the first one that fires is visible in the boot log) and return
// nullptr. This is the honest "not built yet" state, not a silenced bug.
static bool rdram_be_word(uint8_t* rdram, uint32_t vaddr, uint32_t& out) {
    constexpr uint32_t kRdramSize = 0x800000u;
    if (rdram == nullptr) {
        return false;
    }
    uint32_t paddr = vaddr & 0x1FFFFFFFu;
    if (paddr > kRdramSize - sizeof(uint32_t)) {
        return false;
    }
    out = (uint32_t(rdram[(paddr + 0) ^ 3]) << 24) |
          (uint32_t(rdram[(paddr + 1) ^ 3]) << 16) |
          (uint32_t(rdram[(paddr + 2) ^ 3]) << 8) |
          (uint32_t(rdram[(paddr + 3) ^ 3]) << 0);
    return true;
}

static void log_rsp_words(uint8_t* rdram, const char* label, uint32_t vaddr) {
    uint32_t words[4] = {};
    bool ok = true;
    for (uint32_t i = 0; i < 4; i++) {
        ok = rdram_be_word(rdram, vaddr + i * sizeof(uint32_t), words[i]) && ok;
    }
    if (ok) {
        std::fprintf(stderr, "[PMS]   %s @0x%08X: %08X %08X %08X %08X\n",
                     label, vaddr, words[0], words[1], words[2], words[3]);
    } else {
        std::fprintf(stderr, "[PMS]   %s @0x%08X: <out of RDRAM>\n", label, vaddr);
    }
}

static void log_rsp_task_signature(uint8_t* rdram, const OSTask* task) {
    std::fprintf(stderr,
        "[PMS] RSP task type=%u flags=0x%X boot=0x%08X boot_size=0x%X "
        "ucode=0x%08X ucode_size=0x%X ucode_data=0x%08X ucode_data_size=0x%X "
        "data=0x%08X data_size=0x%X out=0x%08X out_size=0x%08X\n",
        (unsigned)task->t.type, (unsigned)task->t.flags,
        (unsigned)task->t.ucode_boot, (unsigned)task->t.ucode_boot_size,
        (unsigned)task->t.ucode, (unsigned)task->t.ucode_size,
        (unsigned)task->t.ucode_data, (unsigned)task->t.ucode_data_size,
        (unsigned)task->t.data_ptr, (unsigned)task->t.data_size,
        (unsigned)task->t.output_buff, (unsigned)task->t.output_buff_size);
    log_rsp_words(rdram, "boot", (uint32_t)task->t.ucode_boot);
    log_rsp_words(rdram, "ucode", (uint32_t)task->t.ucode);
    log_rsp_words(rdram, "ucode_data", (uint32_t)task->t.ucode_data);
    log_rsp_words(rdram, "data", (uint32_t)task->t.data_ptr);
}

// #5 diag: the njpeg task's decode-output buffer (task struct w0), captured in
// get_rsp_microcode so the wrapper below can inspect that exact address.
static std::atomic<uint32_t> g_njpeg_out{0};

// #5 diag: wraps njpgdspMain so we can confirm it actually EXECUTES and whether
// it writes the decoded MCU to its RDRAM output buffer (before vs after). The
// title background draws zeros (green) from this buffer, so the question is
// whether the recompiled JPEG ucode produces output at all.
static RspExitReason njpeg_wrapper(uint8_t* rdram, uint32_t ucode_addr) {
    static std::atomic<int> wn{0};
    const int c = wn.fetch_add(1);
    const uint32_t out = g_njpeg_out.load(std::memory_order_relaxed);
    auto rd = [&](uint32_t pa) -> uint32_t {
        const uint32_t o = pa & 0x1FFFFFFFu;
        return ((uint32_t)rdram[(o + 0) ^ 3] << 24) | ((uint32_t)rdram[(o + 1) ^ 3] << 16) |
               ((uint32_t)rdram[(o + 2) ^ 3] << 8) | (uint32_t)rdram[(o + 3) ^ 3];
    };
    const uint32_t b0 = rd(out), b1 = rd(out + 4);
    const RspExitReason r = njpgdspMain(rdram, ucode_addr);
    if (c < 6) {
        // Did the decode leave anything in DMEM? Distinguishes a VU/decode bug
        // (DMEM all-zero -> nothing decoded) from a DMA/address bug (DMEM has
        // data but the output DMA wrote zeros to RDRAM).
        uint32_t dmem_nz = 0, dmem_first = 0xFFFFFFFFu;
        for (uint32_t i = 0; i < 0x1000u; ++i) {
            if (dmem[i] != 0) { ++dmem_nz; if (dmem_first == 0xFFFFFFFFu) dmem_first = i; }
        }
        std::fprintf(stderr,
            "[PMS] njpeg run #%d exit=%d out=0x%08X  before=%08X %08X  after=%08X %08X  dmem_nz=%u first=0x%X\n",
            c, (int)r, out, b0, b1, rd(out), rd(out + 4), dmem_nz, dmem_first);
        std::fflush(stderr);
    }
    return r;
}

static RspUcodeFunc* get_rsp_microcode(uint8_t* rdram, const OSTask* task) {
    static std::atomic<bool> warned{false};
    if (task->t.type == M_AUDTASK) {
        static std::atomic<bool> a{false};
        if (!a.exchange(true)) { std::fprintf(stderr, "[PMS] RSP M_AUDTASK -> aspMain (first)\n"); std::fflush(stderr); }
        return aspMain;
    }
    if (task->t.type == M_NJPEGTASK) {
        // PMS-J #5 diag: the PRESS START background is a JPEG; confirm the
        // decode task actually fires and with what params.
        // Capture the decode-output address (task struct w0) for njpeg_wrapper.
        {
            const uint32_t sb = (uint32_t)task->t.data_ptr & 0x1FFFFFFFu;
            const uint32_t w0 = ((uint32_t)rdram[(sb + 0) ^ 3] << 24) | ((uint32_t)rdram[(sb + 1) ^ 3] << 16) |
                                ((uint32_t)rdram[(sb + 2) ^ 3] << 8) | (uint32_t)rdram[(sb + 3) ^ 3];
            g_njpeg_out.store(w0, std::memory_order_relaxed);
        }
        static std::atomic<uint32_t> n{0};
        uint32_t c = n.fetch_add(1);
        if (c < 6) {
            std::fprintf(stderr,
                "[PMS] RSP M_NJPEGTASK #%u -> njpgdspMain (data=0x%08X dlen=0x%X ucode=0x%08X)\n",
                c, (uint32_t)task->t.data_ptr, (uint32_t)task->t.data_size,
                (uint32_t)task->t.ucode);
            // Dump the njpeg task structure (its words include the source MCU
            // pointer and the decode OUTPUT buffer) so #5 can tell whether the
            // decoded image is drawn via the DL or blitted to a framebuffer.
            const uint32_t base = (uint32_t)task->t.data_ptr & 0x1FFFFFFFu;
            std::fprintf(stderr, "[PMS] njpeg task struct:");
            for (int w = 0; w < 8; ++w) {
                const uint32_t o = base + (uint32_t)w * 4u;
                const uint32_t val = ((uint32_t)rdram[(o + 0) ^ 3] << 24) |
                                     ((uint32_t)rdram[(o + 1) ^ 3] << 16) |
                                     ((uint32_t)rdram[(o + 2) ^ 3] << 8) |
                                     ((uint32_t)rdram[(o + 3) ^ 3]);
                std::fprintf(stderr, " w%d=0x%08X", w, val);
            }
            std::fprintf(stderr, "\n");
            std::fflush(stderr);
        }
        return njpeg_wrapper;
    }
    if (!warned.exchange(true)) {
        std::fprintf(stderr,
            "[PMS] get_rsp_microcode: no registered RSP microcode for task type=%u.\n",
            (unsigned)task->t.type);
        log_rsp_task_signature(rdram, task);
        std::fflush(stderr);
    }
    return nullptr;
}

// ---- Audio (SDL2 queue; ported from PSR, diagnostic rings removed) ---------
constexpr int input_channels  = 2;
constexpr int output_channels = 2;
constexpr int bytes_per_frame = output_channels * sizeof(float);
constexpr size_t duplicated_input_frames = 4;

static SDL_AudioDeviceID audio_device = 0;
static SDL_AudioCVT audio_convert{};
static uint32_t sample_rate        = 32000;
static uint32_t output_sample_rate = 48000;
static uint32_t discarded_output_frames = 0;

static void update_audio_converter() {
    int ret = SDL_BuildAudioCVT(&audio_convert, AUDIO_F32, input_channels, sample_rate,
                                AUDIO_F32, output_channels, output_sample_rate);
    if (ret < 0) {
        fprintf(stderr, "Error creating SDL audio converter: %s\n", SDL_GetError());
        throw std::runtime_error("Error creating SDL audio converter");
    }
    discarded_output_frames = static_cast<uint32_t>(duplicated_input_frames * output_sample_rate / sample_rate);
}

static void queue_samples(int16_t* audio_data, size_t sample_count) {
    static std::vector<float> swap_buffer;
    static std::array<float, duplicated_input_frames * input_channels> duplicated_sample_buffer{};

    size_t resampled_sample_count = sample_count + duplicated_input_frames * input_channels;
    size_t max_sample_count = std::max(resampled_sample_count, resampled_sample_count * size_t(audio_convert.len_mult));
    if (max_sample_count > swap_buffer.size()) {
        swap_buffer.resize(max_sample_count);
    }

    for (size_t i = 0; i < duplicated_input_frames * input_channels; i++) {
        swap_buffer[i] = duplicated_sample_buffer[i];
    }

    const float main_volume = pms::dbg::g_audio_volume.load();
    if (main_volume == 0.0f) {
        return;  // muted — skip conversion entirely (saves CPU on long runs)
    }
    // int16->float, swap stereo (libultra interleaves R,L per word).
    for (size_t i = 0; i < sample_count; i += input_channels) {
        swap_buffer[i + 0 + duplicated_input_frames * input_channels] = audio_data[i + 1] * (0.5f / 32768.0f) * main_volume;
        swap_buffer[i + 1 + duplicated_input_frames * input_channels] = audio_data[i + 0] * (0.5f / 32768.0f) * main_volume;
    }

    if (sample_count <= duplicated_input_frames * input_channels) {
        return;
    }
    for (size_t i = 0; i < duplicated_input_frames * input_channels; i++) {
        duplicated_sample_buffer[i] = swap_buffer[i + sample_count];
    }

    audio_convert.buf = reinterpret_cast<Uint8*>(swap_buffer.data());
    audio_convert.len = static_cast<int>((sample_count + duplicated_input_frames * input_channels) * sizeof(swap_buffer[0]));

    if (SDL_ConvertAudio(&audio_convert) < 0) {
        fprintf(stderr, "Error using SDL audio converter: %s\n", SDL_GetError());
        return;
    }

    uint64_t cur_queued_microseconds = uint64_t(SDL_GetQueuedAudioSize(audio_device)) / bytes_per_frame * 1000000 / sample_rate;
    uint32_t num_bytes_to_queue = audio_convert.len_cvt - output_channels * discarded_output_frames * sizeof(swap_buffer[0]);
    float* samples_to_queue = swap_buffer.data() + output_channels * discarded_output_frames / 2;

    // Drain runaway queue depth by decimating (keeps latency bounded if the
    // game over-produces, e.g. under turbo).
    uint32_t skip_factor = static_cast<uint32_t>(cur_queued_microseconds / 100000);
    if (skip_factor != 0) {
        uint32_t skip_ratio = 1u << skip_factor;
        num_bytes_to_queue /= skip_ratio;
        for (size_t i = 0; i < num_bytes_to_queue / (output_channels * sizeof(swap_buffer[0])); i++) {
            samples_to_queue[2 * i + 0] = samples_to_queue[2 * skip_ratio * i + 0];
            samples_to_queue[2 * i + 1] = samples_to_queue[2 * skip_ratio * i + 1];
        }
    }

    SDL_QueueAudio(audio_device, samples_to_queue, num_bytes_to_queue);
}

static size_t get_frames_remaining() {
    if (pms::dbg::g_fast_forward.load()) {
        return 0;  // report empty so the game keeps producing without waiting
    }
    constexpr float buffer_offset_frames = 1.0f;
    uint64_t buffered_byte_count = SDL_GetQueuedAudioSize(audio_device);
    buffered_byte_count = buffered_byte_count * 2 * sample_rate / output_sample_rate / output_channels;
    uint32_t frames_per_vi = (sample_rate / 60);
    if (buffered_byte_count > uint64_t(buffer_offset_frames * bytes_per_frame * frames_per_vi)) {
        buffered_byte_count -= uint64_t(buffer_offset_frames * bytes_per_frame * frames_per_vi);
    } else {
        buffered_byte_count = 0;
    }
    return static_cast<size_t>(buffered_byte_count / bytes_per_frame);
}

static void set_frequency(uint32_t freq) {
    sample_rate = freq;
    update_audio_converter();
}

static void reset_audio(uint32_t output_freq) {
    SDL_AudioSpec spec_desired{};
    spec_desired.freq     = (int)output_freq;
    spec_desired.format   = AUDIO_F32;
    spec_desired.channels = (Uint8)output_channels;
    spec_desired.samples  = 0x100;

    audio_device = SDL_OpenAudioDevice(nullptr, false, &spec_desired, nullptr, 0);
    if (audio_device == 0) {
        fprintf(stderr, "SDL error opening audio device: %s\n", SDL_GetError());
        std::exit(EXIT_FAILURE);
    }
    SDL_PauseAudioDevice(audio_device, 0);
    output_sample_rate = output_freq;
    update_audio_converter();
}

// ---- Window / gfx callbacks (SDL2-backed) ----------------------------------

static SDL_Window* g_window = nullptr;

// The game's SDL window, assigned in create_window(). (Formerly also aliased by
// the in-app RmlUi launcher overlay, now an out-of-process executable.)
SDL_Window* window = nullptr;

static ultramodern::gfx_callbacks_t::gfx_data_t create_gfx() {
    fprintf(stderr, "[PMS] create_gfx: SDL_InitSubSystem(VIDEO)\n"); fflush(stderr);
    SDL_InitSubSystem(SDL_INIT_VIDEO);
    return {};
}

static ultramodern::renderer::WindowHandle create_window(ultramodern::gfx_callbacks_t::gfx_data_t) {
    fprintf(stderr, "[PMS] create_window: SDL_CreateWindow\n"); fflush(stderr);
    g_window = SDL_CreateWindow(
        "Pocket Monsters Stadium (PocketMonstersStadiumRecomp)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        960, 720,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN
    );
    if (!g_window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        std::exit(EXIT_FAILURE);
    }
    window = g_window;  // alias consumed by the SS Anne launcher overlay
    SDL_ShowWindow(g_window);

    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    SDL_GetWindowWMInfo(g_window, &wmInfo);
#if defined(_WIN32)
    return ultramodern::renderer::WindowHandle{ wmInfo.info.win.window, GetCurrentThreadId() };
#elif defined(__linux__)
    return ultramodern::renderer::WindowHandle{ wmInfo.info.x11.window };
#elif defined(__APPLE__)
    return ultramodern::renderer::WindowHandle{ wmInfo.info.cocoa.window, nullptr };
#endif
}

// TAB-hold momentary turbo. The persistent state comes from PMS_TURBO; holding
// TAB forces fast-forward on, releasing restores the persistent value.
static std::atomic<bool> s_turbo_persistent{false};

static void update_gfx(void*) {
    pms::dbg::g_frame_count.fetch_add(1);
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) {
            // Signal quit, flush the saving thread (coalesces writes for up to
            // ~1.3s — a bare _Exit can lose a just-made save), then hard-exit
            // past the joins that can hang on game_thread/RT64 teardown.
            ultramodern::quit();
            ultramodern::join_saving_thread();
            std::fflush(stdout);
            std::fflush(stderr);
            std::_Exit(0);
        }
        if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_TAB && ev.key.repeat == 0) {
            pms::dbg::g_fast_forward.store(true);
        } else if (ev.type == SDL_KEYUP && ev.key.keysym.sym == SDLK_TAB) {
            pms::dbg::g_fast_forward.store(s_turbo_persistent.load());
        }
    }
}

// ---- Input (SDL GameController + keyboard) ---------------------------------
// No stub: with no controller connected, an active port still feeds keyboard
// input and reads return real idle input.
//
// Per-port routing: the out-of-process launcher commits its config to
// launcher.cfg, and at boot each N64 port (0..3 == Player 1..4) is routed to a
// specific device read from that file. Under PMS_AUTOBOOT the legacy
// single-human model applies: port 0 = keyboard + first pad, ports 1-3 present
// only when a Transfer Pak cart is configured.

static SDL_GameController* g_pad = nullptr;

static std::atomic<bool> g_launcher_input_active{false};
static pkmnstadium::ui_seam::DeviceKind g_port_kind[4] = {};
static int g_port_instance[4] = {-1, -1, -1, -1};
static bool g_port_enabled[4] = {false, false, false, false};
static SDL_GameController* g_port_pad[4] = {nullptr, nullptr, nullptr, nullptr};

// Snapshot the launcher's assignment (from launcher.cfg) into the per-port
// routing tables. Called once from the boot thread. SDL handles are opened
// lazily in poll_inputs (on the gfx thread that owns the subsystem).
static void apply_launcher_input_config() {
    for (int port = 0; port < 4; ++port) {
        const auto pa = pkmnstadium::ui_seam::port_assignment(port);
        g_port_enabled[port] = pa.enabled;
        g_port_kind[port] = pa.enabled ? pa.kind : pkmnstadium::ui_seam::DeviceKind::None;
        g_port_instance[port] = pa.gamepad_instance;
    }
    g_launcher_input_active.store(true);
    std::fprintf(stderr, "[input] launcher config applied: "
                 "P1=%d P2=%d P3=%d P4=%d (0=None 1=Kbd 2=Pad)\n",
                 (int)g_port_kind[0], (int)g_port_kind[1],
                 (int)g_port_kind[2], (int)g_port_kind[3]);
    std::fflush(stderr);
}

// Open the SDL gamepad with the given instance id (find its device index).
// Reuses an already-open handle (e.g. one opened for launcher navigation).
static SDL_GameController* open_pad_by_instance(int instance) {
    if (SDL_GameController* existing = SDL_GameControllerFromInstanceID(instance)) {
        return existing;
    }
    const int njoy = SDL_NumJoysticks();
    for (int i = 0; i < njoy; ++i) {
        if (SDL_IsGameController(i) && SDL_JoystickGetDeviceInstanceID(i) == instance) {
            return SDL_GameControllerOpen(i);
        }
    }
    return nullptr;
}

static void ensure_pad_open() {
    if (g_pad) return;
    // osContInit queries device info before the runner's frame loop first calls
    // SDL_GameControllerUpdate, so pump once here or a plugged-in pad reads as
    // "not connected".
    SDL_PumpEvents();
    SDL_GameControllerUpdate();
    int njoy = SDL_NumJoysticks();
    for (int i = 0; i < njoy; i++) {
        if (SDL_IsGameController(i)) {
            g_pad = SDL_GameControllerOpen(i);
            if (g_pad) {
                const char* nm = SDL_GameControllerName(g_pad);
                fprintf(stderr, "[input] OPENED controller: name='%s'\n", nm ? nm : "(null)");
                fflush(stderr);
                return;
            }
        }
    }
}

static void poll_inputs() {
    SDL_GameControllerUpdate();
    if (g_launcher_input_active.load()) {
        // Lazily open each port's assigned gamepad (on this gfx thread).
        for (int port = 0; port < 4; ++port) {
            if (g_port_kind[port] == pkmnstadium::ui_seam::DeviceKind::Gamepad &&
                g_port_pad[port] == nullptr && g_port_instance[port] >= 0) {
                g_port_pad[port] = open_pad_by_instance(g_port_instance[port]);
            }
        }
    } else {
        ensure_pad_open();
    }
}

static bool get_n64_input(int controller_num, uint16_t* buttons_out, float* x_out, float* y_out) {
    *buttons_out = 0;
    *x_out = 0.0f;
    *y_out = 0.0f;

    // Pick the device driving this N64 port. In launcher mode each port is
    // routed explicitly by the SS Anne config; otherwise the legacy single-human
    // model applies (port 0 = keyboard + first pad, ports 1-3 = Transfer-Pak idle).
    SDL_GameController* pad = nullptr;
    bool use_kb = false;
    bool primary_port = false;  // the port that receives the TCP debug override
    if (g_launcher_input_active.load()) {
        if (controller_num < 0 || controller_num > 3 || !g_port_enabled[controller_num]) {
            return false;  // disabled / out-of-range slot: absent
        }
        switch (g_port_kind[controller_num]) {
            case pkmnstadium::ui_seam::DeviceKind::Gamepad:
                pad = g_port_pad[controller_num];
                break;
            case pkmnstadium::ui_seam::DeviceKind::Keyboard:
                use_kb = true;
                break;
            default:
                // No controller assigned: respond idle iff a Transfer Pak cart is
                // present, so the game still sees the pak on that port.
                return pkmnstadium::transfer_pak::has_transfer_pak(controller_num);
        }
        primary_port = (controller_num == 0);
    } else {
        if (controller_num != 0) {
            return pkmnstadium::transfer_pak::has_transfer_pak(controller_num);
        }
        pad = g_pad;
        use_kb = true;
        primary_port = true;
    }

    // TCP override (debug harness) layers only onto the primary port.
    uint16_t override_buttons = 0;
    float override_x = 0.0f;
    float override_y = 0.0f;
    if (primary_port && pms::dbg::g_input_override_active.load()) {
        override_buttons = pms::dbg::g_buttons_override.load();
        override_x = float(pms::dbg::g_stick_x_override.load());
        override_y = float(pms::dbg::g_stick_y_override.load());
    }

    uint16_t b = 0;
    int16_t lx = 0, ly = 0, rx = 0, ry = 0;

    if (pad) {
        auto pressed = [&](SDL_GameControllerButton btn) {
            return SDL_GameControllerGetButton(pad, btn) ? 1 : 0;
        };
        // N64 button bit layout (libultra contStat):
        //   0x8000 A   0x4000 B   0x2000 Z   0x1000 Start
        //   0x0800 D-Up 0x0400 D-Down 0x0200 D-Left 0x0100 D-Right
        //   0x0020 L   0x0010 R
        //   0x0008 C-Up 0x0004 C-Down 0x0002 C-Left 0x0001 C-Right
        if (pressed(SDL_CONTROLLER_BUTTON_A))             b |= 0x8000;
        if (pressed(SDL_CONTROLLER_BUTTON_B))             b |= 0x4000;
        if (pressed(SDL_CONTROLLER_BUTTON_LEFTSHOULDER))  b |= 0x2000;
        if (pressed(SDL_CONTROLLER_BUTTON_START))         b |= 0x1000;
        if (pressed(SDL_CONTROLLER_BUTTON_DPAD_UP))       b |= 0x0800;
        if (pressed(SDL_CONTROLLER_BUTTON_DPAD_DOWN))     b |= 0x0400;
        if (pressed(SDL_CONTROLLER_BUTTON_DPAD_LEFT))     b |= 0x0200;
        if (pressed(SDL_CONTROLLER_BUTTON_DPAD_RIGHT))    b |= 0x0100;
        if (pressed(SDL_CONTROLLER_BUTTON_LEFTSTICK))     b |= 0x0020;
        if (pressed(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) b |= 0x0010;

        rx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTX);
        ry = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTY);
        lx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
        ly = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
    }

    // Keyboard fallback / supplement (Project64/Mupen-style layout). Only when
    // this port is keyboard-driven (legacy port 0, or a Keyboard-assigned slot).
    const Uint8* ks = use_kb ? SDL_GetKeyboardState(nullptr) : nullptr;
    if (ks != nullptr) {
        if (ks[SDL_SCANCODE_X])      b |= 0x8000;  // A
        if (ks[SDL_SCANCODE_Z])      b |= 0x4000;  // B
        if (ks[SDL_SCANCODE_LSHIFT] || ks[SDL_SCANCODE_SPACE]) b |= 0x2000;  // Z
        if (ks[SDL_SCANCODE_RETURN] || ks[SDL_SCANCODE_KP_ENTER]) b |= 0x1000;  // Start
        if (ks[SDL_SCANCODE_UP])     b |= 0x0800;
        if (ks[SDL_SCANCODE_DOWN])   b |= 0x0400;
        if (ks[SDL_SCANCODE_LEFT])   b |= 0x0200;
        if (ks[SDL_SCANCODE_RIGHT])  b |= 0x0100;
        if (ks[SDL_SCANCODE_Q])      b |= 0x0020;  // L
        if (ks[SDL_SCANCODE_E])      b |= 0x0010;  // R
        if (ks[SDL_SCANCODE_I])      b |= 0x0008;  // C-Up
        if (ks[SDL_SCANCODE_K])      b |= 0x0004;  // C-Down
        if (ks[SDL_SCANCODE_J])      b |= 0x0002;  // C-Left
        if (ks[SDL_SCANCODE_L])      b |= 0x0001;  // C-Right

        int32_t kx = 0, ky = 0;
        if (ks[SDL_SCANCODE_A]) kx -= 32767;
        if (ks[SDL_SCANCODE_D]) kx += 32767;
        if (ks[SDL_SCANCODE_W]) ky -= 32767;  // SDL Y-axis: up = negative
        if (ks[SDL_SCANCODE_S]) ky += 32767;
        int32_t sx = int32_t(lx) + kx;
        int32_t sy = int32_t(ly) + ky;
        if (sx >  32767) sx =  32767;
        if (sx < -32768) sx = -32768;
        if (sy >  32767) sy =  32767;
        if (sy < -32768) sy = -32768;
        lx = (int16_t)sx;
        ly = (int16_t)sy;
    }
    *buttons_out = b;

    // C-buttons from right stick.
    constexpr int16_t deadzone = 8000;
    if (ry < -deadzone) *buttons_out |= 0x0008;
    if (ry >  deadzone) *buttons_out |= 0x0004;
    if (rx < -deadzone) *buttons_out |= 0x0002;
    if (rx >  deadzone) *buttons_out |= 0x0001;

    // Radial deadzone on the left stick, rescaled outside the deadzone so a
    // partial tilt still reaches full N64 range. Without this, resting drift
    // reads as a held tilt and combines with button presses.
    constexpr int16_t LSTICK_DEADZONE = 8000;
    auto apply_deadzone = [](int16_t raw) -> float {
        int32_t v = raw;
        if (v >  LSTICK_DEADZONE) return float(v - LSTICK_DEADZONE) / float(32767 - LSTICK_DEADZONE) * 80.0f;
        if (v < -LSTICK_DEADZONE) return float(v + LSTICK_DEADZONE) / float(32767 - LSTICK_DEADZONE) * 80.0f;
        return 0.0f;
    };
    *x_out =  apply_deadzone(lx);
    *y_out = -apply_deadzone(ly);

    *buttons_out |= override_buttons;
    if (override_x != 0.0f) {
        *x_out = override_x;
    }
    if (override_y != 0.0f) {
        *y_out = override_y;
    }
    return true;
}

static void set_rumble(int controller_num, bool on) {
    SDL_GameController* pad = nullptr;
    if (g_launcher_input_active.load()) {
        if (controller_num >= 0 && controller_num < 4) pad = g_port_pad[controller_num];
    } else if (controller_num == 0) {
        pad = g_pad;
    }
    if (!pad) return;
    SDL_GameControllerRumble(pad, on ? 0xFFFF : 0, on ? 0xFFFF : 0, 1000);
}

static ultramodern::input::connected_device_info_t get_connected_device_info(int controller_num) {
    const bool has_transfer_pak = pkmnstadium::transfer_pak::has_transfer_pak(controller_num);
    ultramodern::input::connected_device_info_t info{};

    // Launcher mode: a port is present iff its slot is enabled AND it has either
    // an assigned controller or a Transfer Pak cart. Disabled slots are absent,
    // which is how the launcher suppresses unconfigured players.
    if (g_launcher_input_active.load()) {
        const bool enabled = (controller_num >= 0 && controller_num < 4) && g_port_enabled[controller_num];
        const bool has_dev = enabled &&
            (g_port_kind[controller_num] != pkmnstadium::ui_seam::DeviceKind::None || has_transfer_pak);
        info.connected_device = has_dev
            ? ultramodern::input::Device::Controller
            : ultramodern::input::Device::None;
        info.connected_pak = (enabled && has_transfer_pak)
            ? ultramodern::input::Pak::RumblePak
            : ultramodern::input::Pak::None;
        return info;
    }

    // Legacy: libultra's osContInit() queries before the first poll, so lazy-
    // detect the pad here too (else "Controller 1 not connected").
    if (controller_num == 0) {
        ensure_pad_open();
    }
    // Port 0 always present (keyboard/pad/TCP feed get_n64_input); ports 1-3
    // present only when a Transfer Pak cart is configured. ultramodern's Pak enum
    // has no first-class TransferPak, so RumblePak is the "pak present" stand-in
    // (Stadium discriminates the real pak type over the bus in transfer_pak.cpp).
    info.connected_device = (controller_num == 0 || has_transfer_pak)
        ? ultramodern::input::Device::Controller
        : ultramodern::input::Device::None;
    info.connected_pak = has_transfer_pak
        ? ultramodern::input::Pak::RumblePak
        : ultramodern::input::Pak::None;
    return info;
}

// ---- Crash diagnostics -----------------------------------------------------

#ifdef _WIN32
static LONG WINAPI pms_crash_filter(EXCEPTION_POINTERS* info) {
    const std::string err_log_path = pms::app_file("last_error.log").string();
    FILE* f = fopen(err_log_path.c_str(), "a");
    if (f) {
        setvbuf(f, nullptr, _IONBF, 0);
        fprintf(f, "\n=== UNHANDLED EXCEPTION ===\n");
        fprintf(f, "  code:    0x%08lX\n", info->ExceptionRecord->ExceptionCode);
        fprintf(f, "  address: %p\n",      info->ExceptionRecord->ExceptionAddress);
        HANDLE proc = GetCurrentProcess();
        SymInitialize(proc, NULL, TRUE);
        CONTEXT* ctx = info->ContextRecord;
        STACKFRAME64 frame{};
        DWORD machine = IMAGE_FILE_MACHINE_AMD64;
        frame.AddrPC.Offset = ctx->Rip; frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = ctx->Rbp; frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Offset = ctx->Rsp; frame.AddrStack.Mode = AddrModeFlat;
        char symbuf[sizeof(SYMBOL_INFO) + 256];
        SYMBOL_INFO* sym = (SYMBOL_INFO*)symbuf;
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 255;
        IMAGEHLP_LINE64 line{}; line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
        for (int i = 0; i < 24; i++) {
            if (!StackWalk64(machine, proc, GetCurrentThread(), &frame, ctx, NULL,
                             SymFunctionTableAccess64, SymGetModuleBase64, NULL)) break;
            if (!frame.AddrPC.Offset) break;
            DWORD64 disp64 = 0; DWORD disp32 = 0;
            const char* name = "?";
            if (SymFromAddr(proc, frame.AddrPC.Offset, &disp64, sym)) name = sym->Name;
            const char* file = "?"; DWORD lineno = 0;
            if (SymGetLineFromAddr64(proc, frame.AddrPC.Offset, &disp32, &line)) {
                file = line.FileName; lineno = line.LineNumber;
            }
            fprintf(f, "  #%02d 0x%016llX %s (%s:%lu)\n",
                i, (unsigned long long)frame.AddrPC.Offset, name, file, lineno);
        }
        if (info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION
            && info->ExceptionRecord->NumberParameters >= 2) {
            uintptr_t fault_host = (uintptr_t)info->ExceptionRecord->ExceptionInformation[1];
            fprintf(f, "  access:  %s @ 0x%p\n",
                info->ExceptionRecord->ExceptionInformation[0] == 0 ? "read" :
                info->ExceptionRecord->ExceptionInformation[0] == 1 ? "write" : "execute",
                (void*)fault_host);
            uint8_t* rdram_base = recomp_runtime_get_rdram();
            if (rdram_base != nullptr) {
                intptr_t off = (intptr_t)(fault_host - (uintptr_t)rdram_base);
                uint32_t vaddr = (uint32_t)(0x80000000u + (uint32_t)off);
                fprintf(f, "  rdram_base: %p\n", (void*)rdram_base);
                fprintf(f, "  rdram_off:  0x%llX (%lld dec)\n",
                    (unsigned long long)(uint64_t)off, (long long)off);
                fprintf(f, "  decoded vaddr: 0x%08X\n", vaddr);
            }
        }
        fclose(f);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

static LONG CALLBACK pms_vectored_exception_logger(EXCEPTION_POINTERS* info) {
    if (info == nullptr || info->ExceptionRecord == nullptr) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    DWORD code = info->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION &&
        code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_STACK_OVERFLOW) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    static volatile LONG logged = 0;
    if (InterlockedExchange(&logged, 1) == 0) {
        pms_crash_filter(info);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

// ---- Error handling --------------------------------------------------------

static void error_message_box(const char* msg) {
    const std::string err_log_path = pms::app_file("last_error.log").string();
    FILE* f = fopen(err_log_path.c_str(), "a");
    if (f) {
        fprintf(f, "[PMS ERROR] %s\n", msg);
        fclose(f);
    }
    fprintf(stderr, "[PMS ERROR] %s\n", msg);
    fflush(stderr);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "PocketMonstersStadiumRecomp", msg, nullptr);
}

static std::string get_game_thread_name(const OSThread*) {
    return std::string("PocketMonstersStadium");
}

// ---- Boot ------------------------------------------------------------------

// lookup.cpp defines this with C++ linkage.
gpr get_entrypoint_address();

int main(int argc, char** argv) {
    // Unbuffered stderr: when redirected to a file, block buffering loses the
    // tail if the process aborts (e.g. an RT64/driver fast-fail). Unbuffered
    // means every line — ours and the runtime's/RT64's — is durable.
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    std::fprintf(stderr, "[PMS] main() entered\n"); std::fflush(stderr);

#ifdef _WIN32
    const bool crash_handler_disabled = std::getenv("PMS_DISABLE_CRASH_HANDLER") != nullptr;
    if (!crash_handler_disabled) {
        SetUnhandledExceptionFilter(pms_crash_filter);
        AddVectoredExceptionHandler(1, pms_vectored_exception_logger);
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        // abort()/terminate() bypass SetUnhandledExceptionFilter, so catch them
        // too — early-boot recompiled code or an RT64 assert can take this path
        // and would otherwise vanish without a trace.
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        std::signal(SIGABRT, [](int) {
            const std::string p = pms::app_file("last_error.log").string();
            FILE* f = std::fopen(p.c_str(), "a");
            if (f) { setvbuf(f, nullptr, _IONBF, 0); std::fprintf(f, "\n=== SIGABRT (abort) ===\n"); }
            std::fprintf(stderr, "[PMS] SIGABRT (abort) caught — backtrace:\n");
            HANDLE proc = GetCurrentProcess();
            SymInitialize(proc, NULL, TRUE);
            void* frames[32];
            USHORT n = CaptureStackBackTrace(0, 32, frames, NULL);
            char symbuf[sizeof(SYMBOL_INFO) + 256];
            SYMBOL_INFO* sym = (SYMBOL_INFO*)symbuf;
            sym->SizeOfStruct = sizeof(SYMBOL_INFO);
            sym->MaxNameLen = 255;
            IMAGEHLP_LINE64 line{}; line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
            for (USHORT i = 0; i < n; i++) {
                DWORD64 d64 = 0; DWORD d32 = 0;
                const char* name = "?";
                if (SymFromAddr(proc, (DWORD64)frames[i], &d64, sym)) name = sym->Name;
                const char* file = "?"; DWORD ln = 0;
                if (SymGetLineFromAddr64(proc, (DWORD64)frames[i], &d32, &line)) { file = line.FileName; ln = line.LineNumber; }
                std::fprintf(stderr, "    #%02u 0x%016llX %s (%s:%lu)\n", i, (unsigned long long)(uintptr_t)frames[i], name, file, ln);
                if (f) std::fprintf(f, "    #%02u 0x%016llX %s (%s:%lu)\n", i, (unsigned long long)(uintptr_t)frames[i], name, file, ln);
            }
            std::fflush(stderr);
            if (f) std::fclose(f);
        });
        std::set_terminate([]() {
            const char* what = "(no active exception)";
            try { if (auto e = std::current_exception()) std::rethrow_exception(e); }
            catch (const std::exception& ex) { what = ex.what(); }
            catch (...) { what = "(non-std exception)"; }
            const std::string p = pms::app_file("last_error.log").string();
            if (FILE* f = std::fopen(p.c_str(), "a")) {
                std::fprintf(f, "\n=== std::terminate ===\n  what: %s\n", what); std::fclose(f);
            }
            std::fprintf(stderr, "[PMS] std::terminate: %s\n", what); std::fflush(stderr);
            std::abort();
        });
    }
    SDL_setenv("SDL_AUDIODRIVER", "wasapi", true);  // stable sample queueing
#endif

    // PMS_VOLUME overrides the default-muted master volume (accepts e.g. "1.0").
    if (const char* vol_env = std::getenv("PMS_VOLUME")) {
        float v = (float)std::strtod(vol_env, nullptr);
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        pms::dbg::g_audio_volume.store(v);
        std::fprintf(stderr, "[PMS] PMS_VOLUME=%s -> master volume %.3f\n", vol_env, v);
    } else {
        std::fprintf(stderr, "[PMS] master volume defaulting to 0 (muted). Set PMS_VOLUME=1.0 to unmute.\n");
    }

    // PMS_TURBO=1 enables fast-forward (off by default). Hold TAB for momentary.
    bool turbo_default = false;
    if (const char* t = std::getenv("PMS_TURBO")) {
        char c = t[0];
        turbo_default = !(c == '0' || c == 'f' || c == 'F' || c == 'n' || c == 'N');
    }
    pms::dbg::g_fast_forward.store(turbo_default);
    s_turbo_persistent.store(turbo_default);

    int debug_port = 4372;
    if (const char* p = std::getenv("PMS_DEBUG_PORT")) {
        debug_port = (int)std::strtol(p, nullptr, 10);
        if (debug_port <= 0 || debug_port > 65535) {
            debug_port = 4372;
        }
    }
    pms::dbg::start(debug_port);

    std::fprintf(stderr, "[PMS] before SDL_InitSubSystem\n"); std::fflush(stderr);
    SDL_InitSubSystem(SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER);
    reset_audio(48000);
    std::fprintf(stderr, "[PMS] SDL audio/controller init + reset_audio OK\n"); std::fflush(stderr);

    recomp::Version project_version{0, 1, 0, ""};
    recomp::register_config_path(std::filesystem::current_path());

    recomp::GameEntry game{};
    game.rom_hash            = 0x227A835ED03DDF58ULL;  // XXH3_64bits of baserom.z64
                                                       // (Pocket Monsters Stadium (J), md5 c46e087d...)
    game.internal_name       = "POKEMON STADIUM";       // ROM header @ 0x20
    game.game_id             = u8"pocketmonsters.j";
    game.mod_game_id         = "pocketmonsters";
    // Save type ASSUMPTION: matching the sibling (Pokemon Stadium US uses 1Mbit
    // FlashRAM); the J original's real chip is unconfirmed. Not on the
    // boot-to-menu path (only matters when the game saves). Verify and correct
    // once registration/save is exercised.
    game.save_type           = recomp::SaveType::Flashram;
    game.is_enabled          = true;
    game.has_compressed_code = false;
    game.entrypoint_address  = get_entrypoint_address();
    game.entrypoint          = recomp_entrypoint;

    recomp::register_game(game);
    std::fprintf(stderr, "[PMS] game registered\n"); std::fflush(stderr);

    // Feed the generated section table into librecomp so LOOKUP_FUNC resolves.
    pms::register_overlays();
    std::fprintf(stderr, "[PMS] overlays registered\n"); std::fflush(stderr);

    // ---- ROM resolution + validation --------------------------------------
    //   1. argv[1] (CLI override, not persisted)
    //   2. <exe_dir>/rom.cfg (last good pick)
    //   3. baserom.z64 next to exe or one dir up (dev layout)
    //   4. Win32 file picker (loop until valid or cancel)
    {
        std::u8string game_id = game.game_id;
        std::filesystem::path exe_dir = pms::exe_dir();
        std::filesystem::path cfg_path = exe_dir / "rom.cfg";

        auto read_rom_cfg = [&]() -> std::filesystem::path {
            std::ifstream f(cfg_path);
            std::string line;
            if (f && std::getline(f, line)) return line;
            return {};
        };
        auto write_rom_cfg = [&](const std::filesystem::path& p) {
            std::ofstream f(cfg_path, std::ios::trunc);
            if (f) f << p.string() << "\n";
        };
        auto show_picker = [&](std::filesystem::path& out) -> bool {
#ifdef _WIN32
            char picked[MAX_PATH] = {0};
            OPENFILENAMEA ofn{};
            ofn.lStructSize = sizeof(ofn);
            ofn.lpstrFilter = "N64 ROM (*.z64;*.n64;*.v64)\0*.z64;*.n64;*.v64\0All Files (*.*)\0*.*\0";
            ofn.lpstrFile   = picked;
            ofn.nMaxFile    = MAX_PATH;
            ofn.lpstrTitle  = "Select Pocket Monsters Stadium (J) ROM";
            ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
            if (!GetOpenFileNameA(&ofn)) return false;
            out = picked;
            return true;
#else
            return false;
#endif
        };

        std::filesystem::path rom_path;
        bool cli_override = false;
        if (argc >= 2 && argv[1] && argv[1][0] != '-') {
            rom_path = argv[1];
            cli_override = true;
        }
        if (rom_path.empty()) {
            auto saved = read_rom_cfg();
            if (!saved.empty() && std::filesystem::exists(saved)) rom_path = saved;
        }
        if (rom_path.empty()) {
            auto legacy = exe_dir / "baserom.z64";
            if (!std::filesystem::exists(legacy)) legacy = exe_dir.parent_path() / "baserom.z64";
            if (std::filesystem::exists(legacy)) rom_path = legacy;
        }

        while (true) {
            if (!rom_path.empty() && std::filesystem::exists(rom_path)) {
                std::fprintf(stderr, "[PMS] selecting ROM: %s\n", rom_path.string().c_str()); std::fflush(stderr);
                auto err = recomp::select_rom(rom_path, game_id);
                if (err == recomp::RomValidationError::Good) {
                    std::fprintf(stderr, "[PMS] select_rom OK\n"); std::fflush(stderr);
                    if (!cli_override) write_rom_cfg(rom_path);
                    break;
                }
                const char* err_name = "validation error";
                switch (err) {
                    case recomp::RomValidationError::FailedToOpen:     err_name = "could not open file"; break;
                    case recomp::RomValidationError::NotARom:          err_name = "not a recognizable N64 ROM"; break;
                    case recomp::RomValidationError::IncorrectRom:     err_name = "wrong game"; break;
                    case recomp::RomValidationError::NotYet:           err_name = "not yet supported"; break;
                    case recomp::RomValidationError::IncorrectVersion: err_name = "wrong region/revision"; break;
                    default: break;
                }
                std::fprintf(stderr, "[PMS] select_rom error: %d (%s)\n", (int)err, err_name); std::fflush(stderr);
#ifdef _WIN32
                std::string msg = "The selected ROM did not validate (" + std::string(err_name) + ").\n\n"
                                  "Path: " + rom_path.string() + "\n\n"
                                  "Required: Pocket Monsters Stadium (J)\n"
                                  "Required MD5: c46e087d966a35095df96799b0b4ffae (native .z64)\n\n"
                                  "Please select the correct ROM.";
                MessageBoxA(NULL, msg.c_str(), "PocketMonstersStadiumRecomp — wrong ROM", MB_ICONWARNING | MB_OK);
#endif
                rom_path.clear();
            }
            if (!show_picker(rom_path)) {
                std::fprintf(stderr, "[PMS] no ROM selected — exiting\n"); std::fflush(stderr);
                return 1;
            }
        }
    }

    // The launcher is now a separate out-of-process executable (pmsj-launcher)
    // that runs pre-boot, commits launcher.cfg, and spawns this game exe. The
    // game boots directly from that committed config: a dedicated thread reads
    // the per-port controller routing (launcher_cfg::port_assignment) + loads the
    // Transfer Pak config, then calls start_game (kept off the render thread).
    // The short delay before start_game satisfies the VI-thread grace window (the
    // dummy VI mode must be installed before game_status flips to Running, else
    // update_vi derefs a null mode).
    // PMS_AUTOBOOT=1 ignores launcher.cfg and uses the legacy single-human input
    // model (dev/regression).
    const bool autoboot = std::getenv("PMS_AUTOBOOT") != nullptr;
    std::thread([game_id = game.game_id, autoboot]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (autoboot) {
            std::fprintf(stderr, "[PMS] PMS_AUTOBOOT=1 -> legacy single-human input\n"); std::fflush(stderr);
        } else {
            apply_launcher_input_config();  // per-port routing from launcher.cfg
        }
        // Load the Transfer Pak config the launcher wrote (launcher.cfg) / env
        // overrides, before the game's first accessory probe.
        pkmnstadium::transfer_pak::initialize();
        recomp::start_game(game_id);
        std::fprintf(stderr, "[PMS] start_game fired\n"); std::fflush(stderr);
    }).detach();
    std::fprintf(stderr, "[PMS] booting from launcher.cfg (set PMS_AUTOBOOT=1 for legacy input)\n");
    std::fflush(stderr);

    recomp::rsp::callbacks_t rsp_callbacks{
        .get_rsp_microcode = get_rsp_microcode,
    };
    pms::rsp::register_pre_task_hooks();

    ultramodern::renderer::callbacks_t renderer_callbacks{
        .create_render_context = pms::renderer::create_render_context,
    };
    ultramodern::audio_callbacks_t audio_callbacks{
        .queue_samples        = queue_samples,
        .get_frames_remaining = get_frames_remaining,
        .set_frequency        = set_frequency,
    };
    ultramodern::input::callbacks_t input_callbacks{
        .poll_input                = poll_inputs,
        .get_input                 = get_n64_input,
        .set_rumble                = set_rumble,
        .get_connected_device_info = get_connected_device_info,
    };
    ultramodern::gfx_callbacks_t gfx_callbacks{
        .create_gfx    = create_gfx,
        .create_window = create_window,
        .update_gfx    = update_gfx,
    };
    // VI heartbeat — periodic liveness log so a silent hang vs a live-but-blank
    // window is distinguishable in the boot log.
    static auto vi_heartbeat = []() {
        static std::atomic<uint64_t> ticks{0};
        uint64_t n = ticks.fetch_add(1) + 1;
        pms::dbg::g_vi_ticks.store(n);
        if (n % 60 == 0) {
            std::fprintf(stderr, "[PMS] VI heartbeat: %llu frames\n", (unsigned long long)n);
            std::fflush(stderr);
        }
        // Boot-stall watchdog (opt-in via PMS_BOOT_WATCHDOG): the VI callback
        // keeps ticking even when the game thread stalls before graphics, so
        // dump the always-on libultra-call ring at two marks to see what the
        // game thread is waiting on. Defined in diagnostics.cpp.
        static const bool wd = std::getenv("PMS_BOOT_WATCHDOG") != nullptr;
        if (wd && (n == 240 || n == 720)) {
            pms_dump_ultra_trace(n == 240 ? "vi=240" : "vi=720");
        }
    };
    ultramodern::events::callbacks_t events_callbacks{
        .vi_callback = vi_heartbeat,
    };
    ultramodern::error_handling::callbacks_t error_handling_callbacks{
        .message_box = error_message_box,
    };
    ultramodern::threads::callbacks_t threads_callbacks{
        .get_game_thread_name = get_game_thread_name,
    };

    std::fprintf(stderr, "[PMS] calling recomp::start\n"); std::fflush(stderr);
    recomp::start(
        project_version,
        {},
        rsp_callbacks,
        renderer_callbacks,
        audio_callbacks,
        input_callbacks,
        gfx_callbacks,
        events_callbacks,
        error_handling_callbacks,
        threads_callbacks
    );

    std::fprintf(stderr, "[PMS] recomp::start returned\n"); std::fflush(stderr);
    return EXIT_SUCCESS;
}
