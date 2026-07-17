// pmsj_launcher_main.cpp — PocketMonstersStadiumRecomp's standalone pre-boot
// launcher (out-of-process, Dear ImGui via recomp-ui).
//
// Unlike PSR (which links recomp-ui IN-PROCESS and reuses rt64's ImGui), PMS-J
// ships the launcher as its OWN executable built against recomp-ui's OWN
// bundled ImGui + SDL2/GL — so there is no duplicate-ImGui link conflict (two
// separate binaries). The user runs THIS exe; it:
//   1. Loads <exe>/launcher.cfg into a RecompLauncherCSettings.
//   2. Runs the console-generic recomp-ui launcher window (kana-labelled, with
//      a Japanese Gen-1 Transfer Pak cartridge inspector).
//   3. On LAUNCH: writes the edited settings back to launcher.cfg + the chosen
//      ROM to rom.cfg, then SPAWNS PocketMonstersStadiumRecomp.exe (same dir),
//      which boots directly from the config we just committed.
//
// The launcher.cfg key/value format written here is byte-identical to what the
// game's launcher_cfg.cpp reader + transfer_pak.cpp expect (pN_device/pN_rom/
// pN_save + graphics_api/supersampling/antialiasing/window_mode/audio_device),
// and rom.cfg is a single line: the ROM path (see main.cpp read_rom_cfg).

#define NOMINMAX

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include <SDL.h>

#include "recomp_launcher.h"   // recomp-ui C ABI (recomp_launcher_run_window)
#include "launcher_profile.h"  // launcher_profile_apply("n64", &gi)

#include "gen1_charmap_jp.h"   // Japanese Gen-1 in-game text byte -> UTF-8 glyph

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

// ---- exe-dir + small cfg helpers ------------------------------------------

std::filesystem::path exe_dir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(buf).parent_path();
#else
    return std::filesystem::current_path();
#endif
}

std::string trim(std::string s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    const size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string to_lower_str(std::string s) {
    for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

bool parse_bool(const std::string& v, bool fallback) {
    const std::string s = to_lower_str(v);
    if (s == "1" || s == "on" || s == "true" || s == "yes") return true;
    if (s == "0" || s == "off" || s == "false" || s == "no") return false;
    return fallback;
}

std::map<std::string, std::string> read_cfg_map() {
    std::map<std::string, std::string> m;
    std::ifstream f(exe_dir() / "launcher.cfg");
    std::string line;
    while (std::getline(f, line)) {
        const size_t c = line.find_first_of("#;");
        if (c != std::string::npos) line.resize(c);
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = to_lower_str(trim(line.substr(0, eq)));
        std::string v = trim(line.substr(eq + 1));
        if (!k.empty()) m[k] = v;
    }
    return m;
}

void set_str(char* dst, size_t cap, const std::string& src) {
    std::snprintf(dst, cap, "%s", src.c_str());
}

// ---- Transfer Pak inspector (kana labels + Japanese Gen-1 save decode) -----

// GameInfo.tpak_inspect callback: the HOST's cartridge brain. Sniffs the GB
// header @0x134 for the Gen-1 game key, then decodes the Gen-1 save's trainer
// name (via the JAPANESE charmap) + ID, gated on the main-data checksum.
// Cart labels are the kana names copied byte-for-byte from the old RmlUi
// launcher (ui_seam.cpp info_for()); cart_kind: 1=red 2=blue 3=yellow 4=green.
int pmsj_tpak_inspect(const char* rom_path, const char* save_path, RecompLauncherCTpak* out) {
    std::memset(out, 0, sizeof(*out));
    if (rom_path == nullptr || rom_path[0] == '\0') return 0;

    std::ifstream rf(rom_path, std::ios::binary);
    if (!rf) return 0;
    const std::vector<uint8_t> rom((std::istreambuf_iterator<char>(rf)),
                                   std::istreambuf_iterator<char>());
    if (rom.size() < 0x150) return 0; // not a readable GB rom

    // GB game key from the 16-byte title @ 0x134. The Japanese carts carry the
    // same ASCII header titles as the international games.
    const std::string title(reinterpret_cast<const char*>(rom.data() + 0x134), 16);
    auto has = [&](const char* s) { return title.find(s) != std::string::npos; };
    std::string key;
    if      (has("YELLOW")) key = "yellow";
    else if (has("RED"))    key = "red";
    else if (has("BLUE"))   key = "blue";
    else if (has("GREEN"))  key = "green";

    out->valid = 1;
    // Kana labels — EXACT UTF-8 byte sequences copied from ui_seam.cpp info_for()
    // ("ポケットモンスター 赤/青/ピカチュウ/緑").
    if      (key == "red")    { out->cart_kind = 1; std::snprintf(out->cart_label, sizeof(out->cart_label), "\xE3\x83\x9D\xE3\x82\xB1\xE3\x83\x83\xE3\x83\x88\xE3\x83\xA2\xE3\x83\xB3\xE3\x82\xB9\xE3\x82\xBF\xE3\x83\xBC\x20\xE8\xB5\xA4"); }
    else if (key == "blue")   { out->cart_kind = 2; std::snprintf(out->cart_label, sizeof(out->cart_label), "\xE3\x83\x9D\xE3\x82\xB1\xE3\x83\x83\xE3\x83\x88\xE3\x83\xA2\xE3\x83\xB3\xE3\x82\xB9\xE3\x82\xBF\xE3\x83\xBC\x20\xE9\x9D\x92"); }
    else if (key == "yellow") { out->cart_kind = 3; std::snprintf(out->cart_label, sizeof(out->cart_label), "\xE3\x83\x9D\xE3\x82\xB1\xE3\x83\x83\xE3\x83\x88\xE3\x83\xA2\xE3\x83\xB3\xE3\x82\xB9\xE3\x82\xBF\xE3\x83\xBC\x20\xE3\x83\x94\xE3\x82\xAB\xE3\x83\x81\xE3\x83\xA5\xE3\x82\xA6"); }
    else if (key == "green")  { out->cart_kind = 4; std::snprintf(out->cart_label, sizeof(out->cart_label), "\xE3\x83\x9D\xE3\x82\xB1\xE3\x83\x83\xE3\x83\x88\xE3\x83\xA2\xE3\x83\xB3\xE3\x82\xB9\xE3\x82\xBF\xE3\x83\xBC\x20\xE7\xB7\x91"); }
    else                      { out->cart_kind = 0; out->cart_label[0] = '\0'; }

    // Gen-1 save: trainer name + ID, only when the main-data checksum matches.
    // Layout is identical to the international games (pret/pokered-jp sram.asm);
    // only the in-game text ENCODING differs — decode kana via kGen1JpCharmap.
    if (save_path != nullptr && save_path[0] != '\0') {
        std::ifstream sf(save_path, std::ios::binary);
        if (sf) {
            const std::vector<uint8_t> sav((std::istreambuf_iterator<char>(sf)),
                                           std::istreambuf_iterator<char>());
            if (sav.size() >= 0x3524) {
                uint32_t sum = 0;
                for (size_t i = 0x2598; i <= 0x3522; ++i) sum += sav[i];
                const uint8_t calc = static_cast<uint8_t>(~(sum & 0xFF));
                if (calc == sav[0x3523]) {
                    // Trainer name: up to 5 kana, 0x50/0x00-terminated, @0x2598.
                    std::string name;
                    for (size_t i = 0x2598; i <= 0x25A2; ++i) {
                        const uint8_t c = sav[i];
                        if (c == 0x50 || c == 0x00) break;
                        const char* glyph = kGen1JpCharmap[c];
                        if (glyph[0] != '\0') name += glyph;
                    }
                    std::snprintf(out->trainer_name, sizeof(out->trainer_name), "%s", name.c_str());
                    // Trainer ID: 16-bit big-endian @0x2605, shown as 5 digits.
                    const int id = (sav[0x2605] << 8) | sav[0x2606];
                    std::snprintf(out->trainer_id, sizeof(out->trainer_id), "%05d", id);
                }
            }
        }
    }
    return out->valid;
}

// ---- launcher.cfg <-> RecompLauncherCSettings -----------------------------

// Seed a RecompLauncherCSettings from launcher.cfg. Absent keys keep the
// built-in defaults, so a fresh install still launches. (Mirrors PSR's
// load logic so the two projects' launcher.cfg files are interchangeable.)
void load_cfg_into_settings(RecompLauncherCSettings& s) {
    std::memset(&s, 0, sizeof(s));
    s.enable_audio  = 1;
    s.audio_freq    = 48000;
    s.volume        = 100;
    s.output_method = 2; // OpenGL
    s.window_scale  = 3;
    s.renderer      = 0; // Auto
    s.supersampling = 2; // 2x
    s.antialiasing  = 4; // 4x MSAA
    s.fullscreen    = 0; // windowed

    const std::filesystem::path cfg_path = exe_dir() / "launcher.cfg";
    if (!std::filesystem::exists(cfg_path)) {
        // First run: keyboard-driven Player 1 so the launcher (and the game, if
        // the launcher is unavailable) is usable out of the box.
        s.player_src[0] = 1; // Keyboard
        return;
    }

    auto cfg = read_cfg_map();
    auto get = [&](const std::string& k) -> std::string {
        auto it = cfg.find(k);
        return it == cfg.end() ? std::string() : it->second;
    };

    for (int i = 0; i < 4; ++i) {
        const std::string p = "p" + std::to_string(i + 1);
        // Input source: pN_device 0=None, 1=Keyboard, 2+=gamepad -> player_src.
        if (cfg.count(p + "_device")) {
            const int dev = std::atoi(cfg[p + "_device"].c_str());
            s.player_src[i] = (dev <= 0) ? 0 : (dev == 1 ? 1 : 2);
        }
        // Transfer Pak cart: an enabled pak stores pN_rom/pN_save; a pak toggled
        // off stores pN_rom_off/pN_save_off so the path is kept without loading.
        const std::string rom = get(p + "_rom");
        if (!rom.empty()) {
            set_str(s.tpak_rom_path[i], sizeof(s.tpak_rom_path[i]), rom);
            set_str(s.tpak_save_path[i], sizeof(s.tpak_save_path[i]), get(p + "_save"));
            s.tpak_enabled[i] = 1;
        } else {
            const std::string roff = get(p + "_rom_off");
            if (!roff.empty()) {
                set_str(s.tpak_rom_path[i], sizeof(s.tpak_rom_path[i]), roff);
                set_str(s.tpak_save_path[i], sizeof(s.tpak_save_path[i]), get(p + "_save_off"));
                s.tpak_enabled[i] = -1; // present but toggled off
            } else {
                s.tpak_enabled[i] = 0;
            }
        }
    }

    if (cfg.count("graphics_api")) {
        const std::string v = to_lower_str(cfg["graphics_api"]);
        s.renderer = (v == "vulkan") ? 1 : (v == "d3d12") ? 2 : 0;
    }
    if (cfg.count("supersampling")) {
        const std::string v = to_lower_str(cfg["supersampling"]);
        s.supersampling = (v == "off") ? 1 : (v == "4x") ? 4 : 2;
    }
    if (cfg.count("antialiasing")) {
        const std::string v = to_lower_str(cfg["antialiasing"]);
        s.antialiasing = (v == "off") ? 0 : (v == "2x") ? 2 : (v == "8x") ? 8 : 4;
    }
    if (cfg.count("window_mode")) {
        s.fullscreen = (to_lower_str(cfg["window_mode"]) == "fullscreen") ? 1 : 0;
    } else if (cfg.count("fullscreen")) {
        s.fullscreen = parse_bool(cfg["fullscreen"], false) ? 1 : 0;
    }
    if (cfg.count("audio_device")) {
        set_str(s.audio_device, sizeof(s.audio_device), cfg["audio_device"]);
    }
    if (cfg.count("autoplay")) {
        s.skip_launcher = parse_bool(cfg["autoplay"], false) ? 1 : 0;
    }
}

// Persist a committed RecompLauncherCSettings back to launcher.cfg, using the
// exact keys the game's readers expect: transfer_pak.cpp reads pN_rom/pN_save,
// launcher_cfg.cpp reads pN_device/pN_enabled/pN_rom.
void write_settings_to_cfg(const RecompLauncherCSettings& s) {
    std::ofstream f(exe_dir() / "launcher.cfg", std::ios::trunc);
    if (!f) {
        std::fprintf(stderr, "[pmsj-launcher] WARN: cannot write launcher.cfg\n");
        return;
    }
    f << "# PocketMonstersStadiumRecomp launcher configuration - managed by the launcher.\n";
    f << "# pN_device : input source for player N (0=None, 1=Keyboard, 2=Gamepad).\n";
    f << "# pN_enabled: whether player N is active (mirrors pN_device != None).\n";
    f << "# pN_rom / pN_save: the Transfer Pak GB cartridge + save for player N.\n";
    f << "#   A pak toggled off keeps its cart under pN_rom_off/pN_save_off so\n";
    f << "#   the emulated Transfer Pak (which reads pN_rom) does not load it.\n";
    for (int i = 0; i < 4; ++i) {
        const int n = i + 1;
        int src = s.player_src[i];
        if (src < 0) src = 0;
        if (src > 2) src = 2;
        f << "p" << n << "_enabled=" << (src != 0 ? "on" : "off") << "\n";
        f << "p" << n << "_device=" << src << "\n";
        const char* rom = s.tpak_rom_path[i];
        const char* sav = s.tpak_save_path[i];
        if (rom[0] != '\0') {
            const bool on = s.tpak_enabled[i] >= 0;
            const char* rk = on ? "_rom"  : "_rom_off";
            const char* sk = on ? "_save" : "_save_off";
            f << "p" << n << rk << "=" << rom << "\n";
            if (sav[0] != '\0') f << "p" << n << sk << "=" << sav << "\n";
            f << "p" << n << "_tpak=" << (on ? "on" : "off") << "\n";
        }
    }
    const char* api = (s.renderer == 1) ? "vulkan" : (s.renderer == 2) ? "d3d12" : "auto";
    const char* ss  = (s.supersampling <= 1) ? "off" : (s.supersampling >= 4) ? "4x" : "2x";
    const char* aa  = (s.antialiasing == 0) ? "off"
                    : (s.antialiasing == 2) ? "2x"
                    : (s.antialiasing == 8) ? "8x" : "4x";
    f << "# graphics_api=auto|vulkan|d3d12 : render backend.\n";
    f << "graphics_api=" << api << "\n";
    f << "# supersampling=off|2x|4x : 3D supersampling depth.\n";
    f << "supersampling=" << ss << "\n";
    f << "# antialiasing=off|2x|4x|8x : MSAA sample count.\n";
    f << "antialiasing=" << aa << "\n";
    f << "# window_mode=windowed|fullscreen : how the game window opens.\n";
    f << "window_mode=" << (s.fullscreen ? "fullscreen" : "windowed") << "\n";
    f << "# audio_device : output device name substring (empty = system default).\n";
    f << "audio_device=" << s.audio_device << "\n";
    f << "# autoplay=on|off : skip the launcher next time (recomp-ui skip flag).\n";
    f << "autoplay=" << (s.skip_launcher ? "on" : "off") << "\n";
}

// ---- host enumeration for the launcher's audio dropdown --------------------

std::vector<std::string> enumerate_audio_devices() {
    std::vector<std::string> out;
    const int n = SDL_GetNumAudioDevices(0 /* output */);
    for (int i = 0; i < n; ++i) {
        const char* nm = SDL_GetAudioDeviceName(i, 0);
        if (nm != nullptr && nm[0] != '\0') out.emplace_back(nm);
    }
    return out;
}

// ---- rom.cfg (single line: the ROM path the game boots) --------------------

std::string read_rom_cfg() {
    std::ifstream f(exe_dir() / "rom.cfg");
    std::string line;
    if (f && std::getline(f, line)) return trim(line);
    return {};
}

void write_rom_cfg(const std::string& rom_path) {
    std::ofstream f(exe_dir() / "rom.cfg", std::ios::trunc);
    if (f) f << rom_path << "\n";
}

// ---- spawn the game --------------------------------------------------------

bool spawn_game() {
#ifdef _WIN32
    const std::filesystem::path dir  = exe_dir();
    const std::filesystem::path game = dir / L"PocketMonstersStadiumRecomp.exe";
    if (!std::filesystem::exists(game)) {
        std::fprintf(stderr, "[pmsj-launcher] ERROR: game exe not found: %ls\n", game.c_str());
        return false;
    }
    std::wstring cmd = L"\"" + game.wstring() + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    // cmd.data() (mutable) — CreateProcessW may modify the command-line buffer.
    const BOOL ok = CreateProcessW(
        game.wstring().c_str(),      // application
        cmd.data(),                  // command line (argv[0] = quoted exe)
        nullptr, nullptr, FALSE,
        0, nullptr,
        dir.wstring().c_str(),       // working directory = exe dir
        &si, &pi);
    if (!ok) {
        std::fprintf(stderr, "[pmsj-launcher] ERROR: CreateProcessW failed (%lu)\n",
                     (unsigned long)GetLastError());
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
#else
    const std::filesystem::path game = exe_dir() / "PocketMonstersStadiumRecomp";
    std::string cmd = "\"" + game.string() + "\" &";
    return std::system(cmd.c_str()) == 0;
#endif
}

} // namespace

// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    SDL_SetMainReady(); // SDL_MAIN_HANDLED is defined by recomp_target_launcher_ui

    RecompLauncherCSettings settings;
    load_cfg_into_settings(settings);

    RecompLauncherCGameInfo gi;
    std::memset(&gi, 0, sizeof(gi));
    // System identity + RT64 capability defaults, then PMS-J's per-game facts.
    launcher_profile_apply("n64", &gi);
    gi.name         = "Pocket Monsters Stadium"; // JP title (readable Latin form)
    gi.region       = "JPN";
    // ROM identity: SHA-1 of the PMS-J baserom.z64 (MD5 c46e087d966a35095df96799b0b4ffae).
    static const char* const kPmsjSha1[] = {
        "8bc8d2ff7df25b26bd0c353d2adfe83e4e3a7a87",
    };
    gi.known_sha1_hex = kPmsjSha1;
    gi.num_known_sha1 = 1;
    gi.num_players  = 4;
    gi.tpak_slots   = 4;
    gi.tpak_inspect = &pmsj_tpak_inspect;
    gi.hide_rebind  = 1;       // PMS-J has no input_bindings.cpp / rebindable
                               // input.cfg — a key-bind grid would write a file
                               // the game never reads. Source + deadzone only.
    gi.sram_path    = nullptr; // Stadium manages its own FlashRAM save

    static const char* const kRenderers[3] = { "Auto", "Vulkan", "D3D12" };
    gi.renderer_labels = kRenderers;
    gi.num_renderers   = 3;

    // Host-enumerated audio output devices for Settings > Audio. SDL audio must
    // be initialized for the enumeration to return device names.
    SDL_InitSubSystem(SDL_INIT_AUDIO);
    static std::vector<std::string> s_dev_names;
    static std::vector<const char*> s_dev_ptrs;
    s_dev_names = enumerate_audio_devices();
    for (auto& d : s_dev_names) s_dev_ptrs.push_back(d.c_str());
    gi.audio_device_labels = s_dev_ptrs.empty() ? nullptr : s_dev_ptrs.data();
    gi.num_audio_devices   = static_cast<int>(s_dev_ptrs.size());

    // recomp-ui resolves its own assets next to the exe; pass the path too.
    static std::string s_assets;
    s_assets = (exe_dir() / "assets").string();

    // Seed the ROM picker with the last-known ROM (rom.cfg), else a baserom.z64
    // next to the launcher (dev layout).
    std::string initial = read_rom_cfg();
    if (initial.empty()) {
        const std::filesystem::path bare = exe_dir() / "baserom.z64";
        if (std::filesystem::exists(bare)) initial = bare.string();
    }

    char chosen[1024] = {0};
    const int rc = recomp_launcher_run_window(
        "Pocket Monsters Stadium \xE2\x80\x94 Launcher", // em dash
        &settings, &gi, s_assets.c_str(),
        initial.empty() ? nullptr : initial.c_str(),
        chosen, sizeof(chosen));

    if (rc == 1) { // QUIT
        std::fprintf(stderr, "[pmsj-launcher] QUIT\n");
        std::fflush(stderr);
        return 0;
    }

    // rc == 0 LAUNCH: commit + persist, then spawn the game.
    // rc == 2 UNAVAILABLE: no window opened (assets/GL failed) -> boot as if
    // skipped, using whatever config we loaded (no re-write).
    if (rc == 0) {
        write_settings_to_cfg(settings);
        std::string boot_rom = (chosen[0] != '\0') ? std::string(chosen) : initial;
        if (!boot_rom.empty()) write_rom_cfg(boot_rom);
    }

    std::fprintf(stderr, "[pmsj-launcher] %s -> spawning game\n",
                 rc == 0 ? "LAUNCH" : "UNAVAILABLE (skip)");
    std::fflush(stderr);

    if (!spawn_game()) return 1;
    return 0;
}
