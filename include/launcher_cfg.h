#ifndef PKMNSTADIUM_LAUNCHER_CFG_H
#define PKMNSTADIUM_LAUNCHER_CFG_H

// launcher_cfg — game-side reader for the config the out-of-process launcher
// (src/launcher/pmsj_launcher_main.cpp) writes to <exe>/launcher.cfg.
//
// The RmlUi in-app launcher (formerly src/main/ui_seam.cpp) has been replaced
// by a standalone pmsj-launcher executable that writes launcher.cfg and spawns
// this game. The game no longer draws or holds on a launcher; it just reads the
// committed config at boot. Only the per-port input routing the game needs at
// boot lives here (the emulated Transfer Pak reads pN_rom/pN_save directly in
// src/main/transfer_pak.cpp).
//
// The namespace is preserved as pkmnstadium::ui_seam so main.cpp's existing
// port_assignment / DeviceKind / PortAssignment call sites resolve unchanged.

namespace pkmnstadium::ui_seam {
    enum class DeviceKind { None = 0, Keyboard = 1, Gamepad = 2 };

    struct PortAssignment {
        bool enabled = false;
        DeviceKind kind = DeviceKind::None;
        int gamepad_instance = -1; // SDL_JoystickID when kind == Gamepad
    };

    // Per N64 port (0..3 == Player 1..4). Reads <exe>/launcher.cfg on every call:
    // pN_device (0=None, 1=Keyboard, 2=Gamepad), pN_enabled, and pN_rom (an
    // enabled Transfer Pak cart makes an input-less port still respond). Gamepad
    // ports are bound to connected SDL controllers in port order (the launcher's
    // C ABI reports only "gamepad", not which physical pad). When launcher.cfg is
    // absent (game launched directly, without the launcher) Player 1 defaults to
    // the keyboard so the game is still playable.
    PortAssignment port_assignment(int port);
}

#endif // PKMNSTADIUM_LAUNCHER_CFG_H
