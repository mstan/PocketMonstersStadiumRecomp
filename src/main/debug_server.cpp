/*
 * debug_server.cpp - minimal JSON-line TCP debug server for PMS automation.
 *
 * Commands:
 *   ping
 *   status
 *   set_button {"name":"A","down":true}
 *   set_stick {"x":0,"y":0}
 *   clear_input
 *   fast_forward {"on":true}
 *   set_volume {"value":1.0}
 *   quit
 */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "debug_server.h"

// Text-draw discovery census dumper + font-sheet dumper (diagnostics.cpp).
extern "C" void pkmnstadium_textdraw_dump(void);
extern "C" void pkmnstadium_fontdump(void);
extern "C" void pkmnstadium_stringdump(void);
extern "C" void pkmnstadium_memscan(unsigned int minlen);

// Lookup-miss capture debug hooks (librecomp overlays.cpp). Used to
// validate the always-on capture pipeline without a real crash.
extern "C" void recomp_debug_dump_loaded_sections(void);
extern "C" int  recomp_debug_probe_lookup(uint32_t addr);
extern "C" int  recomp_debug_probe_pointer_site(uint32_t* out_addr);
extern "C" int  recomp_debug_jit_evict_all_resident(void);
extern "C" int  recomp_debug_jit_test(uint32_t vram, uint32_t* out_func_size,
                                      uint32_t* out_code_size,
                                      char* out_err, size_t out_err_cap);

namespace pms::dbg {

std::atomic<bool>     g_fast_forward{false};
std::atomic<uint64_t> g_vi_ticks{0};
std::atomic<uint64_t> g_frame_count{0};

std::atomic<bool>     g_input_override_active{false};
std::atomic<uint16_t> g_buttons_override{0};
std::atomic<int>      g_stick_x_override{0};
std::atomic<int>      g_stick_y_override{0};

std::atomic<float>    g_audio_volume{0.0f};

namespace {

std::atomic<bool> s_running{false};
std::thread s_thread;
SOCKET s_listen_socket = INVALID_SOCKET;

static std::string trim(std::string s) {
    auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    s.erase(s.begin(), std::find_if_not(s.begin(), s.end(), is_space));
    s.erase(std::find_if_not(s.rbegin(), s.rend(), is_space).base(), s.end());
    return s;
}

static std::string get_str(const std::string& body, const char* key) {
    std::string needle = std::string("\"") + key + "\"";
    size_t p = body.find(needle);
    if (p == std::string::npos) {
        return {};
    }
    p = body.find(':', p + needle.size());
    if (p == std::string::npos) {
        return {};
    }
    p = body.find('"', p + 1);
    if (p == std::string::npos) {
        return {};
    }
    size_t q = body.find('"', p + 1);
    if (q == std::string::npos) {
        return {};
    }
    return body.substr(p + 1, q - p - 1);
}

static int get_int(const std::string& body, const char* key, int dflt) {
    std::string needle = std::string("\"") + key + "\"";
    size_t p = body.find(needle);
    if (p == std::string::npos) {
        return dflt;
    }
    p = body.find(':', p + needle.size());
    if (p == std::string::npos) {
        return dflt;
    }
    return std::strtol(body.c_str() + p + 1, nullptr, 10);
}

static float get_float(const std::string& body, const char* key, float dflt) {
    std::string needle = std::string("\"") + key + "\"";
    size_t p = body.find(needle);
    if (p == std::string::npos) {
        return dflt;
    }
    p = body.find(':', p + needle.size());
    if (p == std::string::npos) {
        return dflt;
    }
    return std::strtof(body.c_str() + p + 1, nullptr);
}

static bool get_bool(const std::string& body, const char* key, bool dflt) {
    std::string needle = std::string("\"") + key + "\"";
    size_t p = body.find(needle);
    if (p == std::string::npos) {
        return dflt;
    }
    p = body.find(':', p + needle.size());
    if (p == std::string::npos) {
        return dflt;
    }
    const char* v = body.c_str() + p + 1;
    while (*v && std::isspace((unsigned char)*v)) {
        v++;
    }
    if (_strnicmp(v, "true", 4) == 0 || *v == '1') {
        return true;
    }
    if (_strnicmp(v, "false", 5) == 0 || *v == '0') {
        return false;
    }
    return dflt;
}

static std::string get_cmd(const std::string& line) {
    if (!line.empty() && line[0] == '{') {
        return get_str(line, "cmd");
    }
    size_t p = line.find_first_of(" \t\r\n");
    return line.substr(0, p);
}

static uint16_t button_bit(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(),
        [](unsigned char c) { return (char)std::tolower(c); });
    if (name == "a") return 0x8000;
    if (name == "b") return 0x4000;
    if (name == "z") return 0x2000;
    if (name == "start") return 0x1000;
    if (name == "du" || name == "d_up" || name == "d-up" || name == "up") return 0x0800;
    if (name == "dd" || name == "d_down" || name == "d-down" || name == "down") return 0x0400;
    if (name == "dl" || name == "d_left" || name == "d-left" || name == "left") return 0x0200;
    if (name == "dr" || name == "d_right" || name == "d-right" || name == "right") return 0x0100;
    if (name == "l") return 0x0020;
    if (name == "r") return 0x0010;
    if (name == "cu" || name == "c_up" || name == "c-up") return 0x0008;
    if (name == "cd" || name == "c_down" || name == "c-down") return 0x0004;
    if (name == "cl" || name == "c_left" || name == "c-left") return 0x0002;
    if (name == "cr" || name == "c_right" || name == "c-right") return 0x0001;
    return 0;
}

static std::string handle_line(const std::string& raw_line) {
    const std::string line = trim(raw_line);
    const std::string cmd = get_cmd(line);
    if (cmd.empty()) {
        return R"({"ok":false,"error":"empty command"})";
    }
    if (cmd == "ping") {
        return R"({"ok":true,"pong":true})";
    }
    if (cmd == "status") {
        char buf[512];
        std::snprintf(buf, sizeof(buf),
            "{\"ok\":true,\"vi\":%llu,\"frame\":%llu,"
            "\"fast_forward\":%s,\"input_override\":%s,"
            "\"buttons\":%u,\"sx\":%d,\"sy\":%d,\"volume\":%.3f}",
            (unsigned long long)g_vi_ticks.load(),
            (unsigned long long)g_frame_count.load(),
            g_fast_forward.load() ? "true" : "false",
            g_input_override_active.load() ? "true" : "false",
            (unsigned)g_buttons_override.load(),
            g_stick_x_override.load(),
            g_stick_y_override.load(),
            (double)g_audio_volume.load());
        return buf;
    }
    if (cmd == "set_button") {
        const std::string name = get_str(line, "name");
        const bool down = get_bool(line, "down", true);
        const uint16_t bit = button_bit(name);
        if (bit == 0) {
            return R"({"ok":false,"error":"unknown button"})";
        }
        g_input_override_active.store(true);
        uint16_t cur = g_buttons_override.load();
        if (down) {
            cur |= bit;
        } else {
            cur &= uint16_t(~bit);
        }
        g_buttons_override.store(cur);
        return R"({"ok":true})";
    }
    if (cmd == "set_stick") {
        g_input_override_active.store(true);
        int x = std::clamp(get_int(line, "x", 0), -80, 80);
        int y = std::clamp(get_int(line, "y", 0), -80, 80);
        g_stick_x_override.store(x);
        g_stick_y_override.store(y);
        return R"({"ok":true})";
    }
    if (cmd == "clear_input") {
        g_input_override_active.store(false);
        g_buttons_override.store(0);
        g_stick_x_override.store(0);
        g_stick_y_override.store(0);
        return R"({"ok":true})";
    }
    if (cmd == "fast_forward") {
        g_fast_forward.store(get_bool(line, "on", true));
        return R"({"ok":true})";
    }
    if (cmd == "set_volume") {
        float v = std::clamp(get_float(line, "value", 1.0f), 0.0f, 1.0f);
        g_audio_volume.store(v);
        return R"({"ok":true})";
    }
    if (cmd == "textdump") {
        // Dump the text-draw discovery census (see diagnostics.cpp). Free-run
        // to the screen of interest, then send this to identify the string-draw
        // function + inventory live strings. Capture is always-on (no env gate).
        pkmnstadium_textdraw_dump();
        return R"({"ok":true,"wrote":"textdraw_probe.log"})";
    }
    if (cmd == "stringdump") {
        // Dump the distinct-string inventory (every string drawn by the
        // string-draw printf). Capture is always-on; the inventory also persists
        // continuously to stringdump.log as new strings appear. Sweep screens first.
        pkmnstadium_stringdump();
        return R"({"ok":true,"wrote":"stringdump.log"})";
    }
    if (cmd == "fontdump") {
        // Dump font-slot descriptors + sheet textures (diagnostics.cpp) to
        // answer the Latin-glyph question. Free-run to a text screen first.
        pkmnstadium_fontdump();
        return R"({"ok":true,"wrote":"fontdump.log + font_slotN.i8"})";
    }
    if (cmd == "memscan") {
        // Scan all of RDRAM for EUC-JP text runs (>= minlen, default 24) -> memscan.log.
        // Extracts source text already loaded in RAM (e.g. the Pokedex description
        // table) so it can be translated in bulk without visiting each entry.
        unsigned int minlen = (unsigned int)get_float(line, "minlen", 24.0f);
        pkmnstadium_memscan(minlen);
        return R"({"ok":true,"wrote":"memscan.log"})";
    }
    if (cmd == "dump_sections") {
        // Dump the live loaded-section table to build/loaded_sections.json
        // so a probe can target real interior / reloc offsets.
        recomp_debug_dump_loaded_sections();
        return R"({"ok":true,"wrote":"build/loaded_sections.json"})";
    }
    if (cmd == "probe_lookup") {
        // Force a get_function lookup for {"addr":"0x...."} to exercise the
        // always-on lookup-miss capture (writes build/runtime_captures.json
        // on a miss). Does NOT invoke the result, so a miss does not abort.
        const std::string addr_s = get_str(line, "addr");
        const uint32_t addr = (uint32_t)std::strtoul(addr_s.c_str(), nullptr, 0);
        const int missed = recomp_debug_probe_lookup(addr);
        char buf[128];
        std::snprintf(buf, sizeof(buf),
            "{\"ok\":true,\"addr\":\"0x%08X\",\"missed\":%s}",
            addr, missed ? "true" : "false");
        return buf;
    }
    if (cmd == "jit_test") {
        // B3 validation: compile-only forced JIT of a RESIDENT function
        // {"addr":"0x80......"} through the runtime LiveRecomp pipeline,
        // without registering it. Pick a main-RDRAM (0x80000000-0x807FFFFF)
        // function; overlay link addresses are (correctly) rejected.
        const std::string addr_s = get_str(line, "addr");
        const uint32_t addr = (uint32_t)std::strtoul(addr_s.c_str(), nullptr, 0);
        uint32_t fsize = 0, csize = 0;
        char err[192];
        const int rc = recomp_debug_jit_test(addr, &fsize, &csize, err, sizeof(err));
        // Escape quotes/backslashes in err for JSON safety (kept simple).
        for (char* p = err; *p; ++p) { if (*p == '"' || *p == '\\') *p = '\''; }
        char buf[320];
        std::snprintf(buf, sizeof(buf),
            "{\"ok\":true,\"addr\":\"0x%08X\",\"jit_ok\":%s,"
            "\"func_size\":%u,\"code_size\":%u,\"err\":\"%s\"}",
            addr, rc == 0 ? "true" : "false", fsize, csize, err);
        return buf;
    }
    if (cmd == "jit_evict_all") {
        // B3 in-game execution test: evict all resident functions so their
        // next indirect call routes through the JIT tier. Safe (B3 failures
        // restore the static func). Check jit_compiles in captures afterward.
        const int n = recomp_debug_jit_evict_all_resident();
        char buf[96];
        std::snprintf(buf, sizeof(buf),
            "{\"ok\":true,\"evicted\":%d}", n);
        return buf;
    }
    if (cmd == "probe_pointer_site") {
        // Probe a live reloc offset (not a func start) to exercise the
        // "pointer-site" classification path; eviction-race-free.
        uint32_t chosen = 0;
        const int missed = recomp_debug_probe_pointer_site(&chosen);
        char buf[160];
        std::snprintf(buf, sizeof(buf),
            "{\"ok\":true,\"addr\":\"0x%08X\",\"missed\":%s}",
            chosen, missed ? "true" : "false");
        return buf;
    }
    if (cmd == "quit") {
        std::fflush(stdout);
        std::fflush(stderr);
        std::_Exit(0);
    }
    return R"({"ok":false,"error":"unknown command"})";
}

static void client_loop(SOCKET client) {
    std::string line;
    char ch = 0;
    while (s_running.load()) {
        int got = recv(client, &ch, 1, 0);
        if (got <= 0) {
            break;
        }
        if (ch == '\n') {
            std::string response = handle_line(line);
            response += "\n";
            send(client, response.c_str(), (int)response.size(), 0);
            line.clear();
        } else if (ch != '\r') {
            line.push_back(ch);
            if (line.size() > 4096) {
                line.clear();
            }
        }
    }
    closesocket(client);
}

static void server_thread(int port) {
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::fprintf(stderr, "[pms-debug] WSAStartup failed\n");
        return;
    }

    SOCKET listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket == INVALID_SOCKET) {
        std::fprintf(stderr, "[pms-debug] socket failed: %d\n", WSAGetLastError());
        WSACleanup();
        return;
    }
    s_listen_socket = listen_socket;

    BOOL reuse = TRUE;
    setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((u_short)port);
    if (bind(listen_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        std::fprintf(stderr, "[pms-debug] bind 127.0.0.1:%d failed: %d\n",
                     port, WSAGetLastError());
        closesocket(listen_socket);
        s_listen_socket = INVALID_SOCKET;
        WSACleanup();
        return;
    }
    if (listen(listen_socket, 1) == SOCKET_ERROR) {
        std::fprintf(stderr, "[pms-debug] listen failed: %d\n", WSAGetLastError());
        closesocket(listen_socket);
        s_listen_socket = INVALID_SOCKET;
        WSACleanup();
        return;
    }

    std::fprintf(stderr, "[pms-debug] listening on 127.0.0.1:%d\n", port);
    while (s_running.load()) {
        SOCKET client = accept(listen_socket, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            if (s_running.load()) {
                std::fprintf(stderr, "[pms-debug] accept failed: %d\n", WSAGetLastError());
            }
            break;
        }
        client_loop(client);
    }

    closesocket(listen_socket);
    s_listen_socket = INVALID_SOCKET;
    WSACleanup();
}

} // namespace

void start(int port) {
    if (s_running.exchange(true)) {
        return;
    }
    s_thread = std::thread(server_thread, port);
}

void shutdown() {
    if (!s_running.exchange(false)) {
        return;
    }
    SOCKET sock = s_listen_socket;
    if (sock != INVALID_SOCKET) {
        closesocket(sock);
    }
    if (s_thread.joinable()) {
        s_thread.join();
    }
}

} // namespace pms::dbg
