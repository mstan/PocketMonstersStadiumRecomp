// launcher_cfg.cpp — game-side reader for <exe>/launcher.cfg.
//
// This is the kept, RmlUi-free remnant of the old src/main/ui_seam.cpp: only the
// launcher.cfg → per-port input-routing logic the game needs at boot. The
// launcher itself is now an out-of-process executable (pmsj-launcher) that
// writes launcher.cfg in the format read here (identical to the format the
// launcher's write_settings_to_cfg emits).

#define NOMINMAX

#include "launcher_cfg.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

#include <SDL.h>

#include "app_paths.h"

namespace pkmnstadium::ui_seam {
namespace {

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

// Read <exe>/launcher.cfg into a map. Keys are lowercased; values are trimmed.
std::map<std::string, std::string> read_cfg_map() {
    std::map<std::string, std::string> m;
    std::ifstream f(pms::exe_dir() / "launcher.cfg");
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

// The instance id of the k-th connected SDL game controller (device-index
// order). The launcher's C ABI reports player_src (None/Keyboard/Gamepad) but
// NOT which specific pad each gamepad-player picked, so gamepad-players are
// bound to pads by port order here — the same rule the PSR host uses.
int nth_gamepad_instance(int k) {
    if (k < 0) return -1;
    SDL_GameControllerUpdate();
    int seen = 0;
    const int njoy = SDL_NumJoysticks();
    for (int i = 0; i < njoy; ++i) {
        if (!SDL_IsGameController(i)) continue;
        if (seen == k) return static_cast<int>(SDL_JoystickGetDeviceInstanceID(i));
        ++seen;
    }
    return -1;
}

} // namespace

PortAssignment port_assignment(int port) {
    PortAssignment pa;
    if (port < 0 || port > 3) return pa;

    const std::filesystem::path cfg_path = pms::exe_dir() / "launcher.cfg";

    // No launcher.cfg (game launched directly, without the launcher): make
    // Player 1 a keyboard so the game is playable out of the box.
    if (!std::filesystem::exists(cfg_path)) {
        if (port == 0) { pa.enabled = true; pa.kind = DeviceKind::Keyboard; }
        return pa;
    }

    auto cfg = read_cfg_map();
    const std::string p = "p" + std::to_string(port + 1);

    // Input source: pN_device 0=None, 1=Keyboard, 2(+)=Gamepad. Any value >=2 is
    // treated as "gamepad" (the specific pad is re-derived by enumeration order).
    int src = 0;
    if (auto it = cfg.find(p + "_device"); it != cfg.end()) {
        const int dev = std::atoi(it->second.c_str());
        src = (dev <= 0) ? 0 : (dev == 1 ? 1 : 2);
    }

    // Transfer Pak cart: an enabled pak (pN_rom present; pN_rom_off means the pak
    // is present but toggled off) lets an input-less port still respond so the
    // game sees the pak on that port.
    bool tpak_on = false;
    if (auto it = cfg.find(p + "_rom"); it != cfg.end() && !it->second.empty()) {
        tpak_on = true;
    }

    pa.enabled = (src != 0) || tpak_on;
    if (src == 1) {
        pa.kind = DeviceKind::Keyboard;
    } else if (src == 2) {
        pa.kind = DeviceKind::Gamepad;
        // Bind connected pads to gamepad-players in port order.
        int ordinal = 0;
        for (int j = 0; j < port; ++j) {
            const std::string pj = "p" + std::to_string(j + 1);
            if (auto it = cfg.find(pj + "_device"); it != cfg.end() && std::atoi(it->second.c_str()) >= 2) {
                ++ordinal;
            }
        }
        pa.gamepad_instance = nth_gamepad_instance(ordinal);
    }
    return pa;
}

} // namespace pkmnstadium::ui_seam
