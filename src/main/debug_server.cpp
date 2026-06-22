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

#include <librecomp/gbcart.hpp>  // Transfer Pak block-I/O ring dump (gbcart_dump)

// Text-draw discovery census dumper + font-sheet dumper (diagnostics.cpp).
extern "C" void pkmnstadium_textdraw_dump(void);
extern "C" void pkmnstadium_fontdump(void);
extern "C" void pkmnstadium_stringdump(void);
extern "C" void pkmnstadium_memscan(unsigned int minlen);

// Live RDRAM base for raw guest-memory dumps (read_mem command). Lets a probe
// disassemble the authoritative, relocated overlay bytes at any miss site.
extern "C" unsigned char* recomp_runtime_get_rdram(void);

// On-demand os-wrapper ring dump (diagnostics.cpp). Writes the libultra-call
// ring to build/last_run_report.txt WITHOUT a crash — for probing a live
// softlock: the last osRecvMesg per thread with no matching ~osRecvMesg.done is
// a thread blocked in recv (the deadlock vertex); cross-ref osSendMesg to that
// queue to see if anything ever signals it.
extern "C" void pms_dump_ultra_trace(const char* tag);

// Scheduler + message observability (shared ultramodern, already present at the
// pinned sha). dump_sched/dump_mesg query these always-on rings + never-evict
// tables to pin a softlock's deadlock vertex: which thread parked on which queue
// (blocked_on_recv with no later wakeup) and whether anyone ever sent to it.
extern "C" uint32_t ultramodern_running_queue_head(void);
extern "C" void   ultramodern_sched_recent_copy(void* out, size_t cap, size_t* n_written, uint64_t* next_seq_out);
extern "C" size_t ultramodern_sched_event_size(void);
extern "C" size_t ultramodern_sched_thread_state_size(void);
extern "C" void   ultramodern_sched_thread_states_copy(void* out, size_t cap, size_t* n_written);
extern "C" void   ultramodern_get_event_queues(uint32_t* out, int count);
extern "C" void   ultramodern_mesg_recent_copy(void* out, size_t cap, size_t* n_written, uint64_t* next_seq_out);
extern "C" size_t ultramodern_mesg_event_size(void);
extern "C" size_t ultramodern_mesg_qstate_size(void);
extern "C" void   ultramodern_mesg_qstates_copy(void* out, size_t cap, size_t* n_written);

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
    if (cmd == "read_mem") {
        // Dump raw guest RDRAM at {"addr":"0x80......","len":N} to build/
        // memdump.bin (big-endian / un-swapped so Ghidra MIPS:BE:32 decodes it
        // directly), and return the first 64 bytes as hex. Reusable: works for
        // any live address — missed entry, its caller, a bad pointer site.
        const std::string addr_s = get_str(line, "addr");
        const uint32_t addr = (uint32_t)std::strtoul(addr_s.c_str(), nullptr, 0);
        uint32_t len = (uint32_t)get_int(line, "len", 256);
        if (len == 0) len = 256;
        if (len > 0x10000u) len = 0x10000u;            // 64 KiB cap
        unsigned char* rdram = recomp_runtime_get_rdram();
        if (rdram == nullptr) {
            return R"({"ok":false,"error":"rdram unavailable"})";
        }
        // Validate KSEG0 range [0x80000000, 0x80800000).
        const uint32_t start = addr & 0x1FFFFFFFu;
        if (!(addr >= 0x80000000u && addr < 0x80800000u &&
              start <= 0x00800000u - len)) {
            return R"({"ok":false,"error":"addr out of range"})";
        }
        std::string buf(len, '\0');
        const uint32_t paddr = addr & 0x1FFFFFFFu;
        for (uint32_t i = 0; i < len; i++) {
            buf[i] = (char)rdram[(paddr + i) ^ 3];     // un-swap to big-endian
        }
        FILE* f = std::fopen("memdump.bin", "wb");
        if (f) { std::fwrite(buf.data(), 1, len, f); std::fclose(f); }
        // Hex preview of the first min(len,64) bytes.
        char hex[64 * 2 + 1];
        const uint32_t prev = len < 64 ? len : 64;
        for (uint32_t i = 0; i < prev; i++)
            std::snprintf(hex + i * 2, 3, "%02X", (unsigned char)buf[i]);
        hex[prev * 2] = '\0';
        std::string out = "{\"ok\":true,\"addr\":\"0x";
        char ab[16]; std::snprintf(ab, sizeof(ab), "%08X", addr);
        out += ab;
        out += "\",\"len\":";
        char lb[16]; std::snprintf(lb, sizeof(lb), "%u", len);
        out += lb;
        out += ",\"wrote\":\"build/memdump.bin\",\"hex\":\"";
        out += hex;
        out += "\"}";
        return out;
    }
    if (cmd == "tracedump") {
        // Dump the os-wrapper ring on demand (live softlock diagnosis) to
        // build/last_run_report.txt. {"tag":"..."} labels the dump.
        std::string tag = get_str(line, "tag");
        if (tag.empty()) tag = "manual";
        pms_dump_ultra_trace(tag.c_str());
        return R"({"ok":true,"wrote":"build/last_run_report.txt"})";
    }
    if (cmd == "gbcart_dump") {
        // Dump the always-on Transfer Pak block-I/O ring to build/gbcart_ring.txt:
        // which ROM banks/addresses the cart access walked + the last N entries.
        // Pins where GB Tower's full-cart read stalls (vs registration's targeted
        // reads), without perturbing boot the way per-event stderr does.
        int tail = (int)std::strtol(get_str(line, "tail").c_str(), nullptr, 0);
        if (tail <= 0) tail = 800;
        librecomp::gbcart::ring_dump("build/gbcart_ring.txt", tail);
        return R"({"ok":true,"wrote":"build/gbcart_ring.txt"})";
    }
    if (cmd == "dump_sched") {
        // Dump the always-on scheduler-event ring + never-evict per-thread table
        // to build/pms_sched_dump.txt. A thread that did WAIT_BEGIN (120) with no
        // later WAIT_RETURN is parked forever; the running thread never yielding =
        // a starving spin loop. Mirrors ultramodern threadqueue.cpp sched_log.
        struct SchedEvent {
            uint64_t seq; uint64_t ms; uint32_t op; uint32_t queue;
            uint32_t thread; uint32_t current_thread; uint32_t head_after;
            uint32_t next_after; uint16_t thread_id; uint16_t current_thread_id;
            int16_t priority; uint16_t pad;
        };
        if (ultramodern_sched_event_size() != sizeof(SchedEvent)) {
            return R"({"ok":false,"error":"sched event size mismatch"})";
        }
        auto op_name = [](uint32_t op) -> const char* {
            switch (op) {
                case 1: return "INSERT"; case 2: return "POP";
                case 3: return "REMOVE"; case 4: return "REMOVE_MISS";
                case 100: return "SCHEDULE_RUNNING"; case 101: return "CHECK_EMPTY";
                case 102: return "CHECK_PEEK"; case 103: return "CHECK_NO_SWAP";
                case 104: return "CHECK_SWAP"; case 110: return "SWAP_ENTER";
                case 111: return "SWAP_INSERT_SELF"; case 112: return "SWAP_WAIT_ENTER";
                case 113: return "SWAP_WAIT_RETURN"; case 120: return "WAIT_BEGIN";
                case 121: return "WAIT_RETURN"; case 122: return "RESUME_SIGNAL";
                case 123: return "RUN_NEXT_ENTER"; case 124: return "RUN_NEXT_EMPTY";
                case 125: return "RUN_NEXT_TARGET"; case 126: return "RUN_NEXT_WAIT_ENTER";
                case 127: return "RUN_NEXT_WAIT_RETURN"; case 130: return "YIELD_ANY_ENTER";
                case 131: return "YIELD_ANY_EMPTY"; case 132: return "YIELD_ANY_TARGET";
                case 133: return "YIELD_ANY_REQUEUE_SELF"; case 134: return "YIELD_ANY_WAIT_ENTER";
                case 135: return "YIELD_ANY_WAIT_RETURN"; default: return "?";
            }
        };
        const size_t RING_CAP = 65536;
        static SchedEvent evs[RING_CAP];
        size_t tail = (size_t)std::strtoul(get_str(line, "tail").c_str(), nullptr, 0);
        if (tail == 0) tail = 600;
        size_t n = 0; uint64_t next_seq = 0;
        ultramodern_sched_recent_copy(evs, RING_CAP, &n, &next_seq);
        struct ThreadSum { uint32_t thread; int16_t pri; uint32_t op; uint32_t queue;
                           uint32_t head_after; uint64_t ms; uint16_t id; uint32_t count;
                           uint32_t mq; uint64_t mq_ms; };
        ThreadSum sums[64]; size_t n_sums = 0;
        uint64_t last_ms = n ? evs[n - 1].ms : 0;
        for (size_t i = 0; i < n; i++) {
            const SchedEvent& e = evs[i];
            if (e.thread_id == 0 && e.thread == 0) continue;
            size_t k = 0; for (; k < n_sums; k++) if (sums[k].id == e.thread_id) break;
            if (k == n_sums && n_sums < 64) { n_sums++; sums[k].count = 0; sums[k].mq = 0; sums[k].mq_ms = 0; }
            if (k < 64) {
                sums[k].id = e.thread_id; sums[k].thread = e.thread; sums[k].pri = e.priority;
                sums[k].op = e.op; sums[k].queue = e.queue; sums[k].head_after = e.head_after;
                sums[k].ms = e.ms; sums[k].count++;
                if (e.op == 1 && e.queue != 0xFFFFFFFFu && e.queue != 0) { sums[k].mq = e.queue; sums[k].mq_ms = e.ms; }
            }
        }
        FILE* f = std::fopen("pms_sched_dump.txt", "w");
        if (f) {
            std::fprintf(f, "=== scheduler ring (next_seq=%llu, %llu events, running_queue_head=0x%08X) ===\n",
                (unsigned long long)next_seq, (unsigned long long)n, ultramodern_running_queue_head());
            uint32_t eq[5] = {0,0,0,0,0};
            ultramodern_get_event_queues(eq, 5);
            std::fprintf(f, "--- event queues: SP=0x%08X DP=0x%08X AI=0x%08X SI=0x%08X VI=0x%08X ---\n",
                eq[0], eq[1], eq[2], eq[3], eq[4]);
            auto eq_name = [&](uint32_t q) -> const char* {
                if (q == 0) return ""; if (q == eq[0]) return " [SP.mq]"; if (q == eq[1]) return " [DP.mq]";
                if (q == eq[2]) return " [AI.mq]"; if (q == eq[3]) return " [SI.mq]"; if (q == eq[4]) return " [VI.mq]";
                return "";
            };
            struct ThreadState {
                uint32_t valid; uint32_t id; uint32_t thread; int32_t priority;
                uint32_t last_op; uint32_t last_queue; uint32_t last_head_after;
                uint64_t last_ms; uint32_t last_mq; uint64_t last_mq_ms; uint64_t count;
            };
            if (ultramodern_sched_thread_state_size() == sizeof(ThreadState)) {
                static ThreadState tss[256]; size_t nts = 0;
                ultramodern_sched_thread_states_copy(tss, 256, &nts);
                uint64_t now_max = 0;
                for (size_t i = 0; i < nts; i++) if (tss[i].last_ms > now_max) now_max = tss[i].last_ms;
                std::fprintf(f, "--- per-thread state (NEVER-EVICT, %llu threads) ---\n", (unsigned long long)nts);
                for (size_t i = 0; i < nts; i++) {
                    const ThreadState& s = tss[i];
                    std::fprintf(f, "  t%-3u (0x%08X, pri=%d)  last=%-22s %lldms_ago  events=%llu  blocked_on_recv_q=0x%08X%s (%lldms_ago)\n",
                        s.id, s.thread, s.priority, op_name(s.last_op),
                        (long long)now_max - (long long)s.last_ms, (unsigned long long)s.count,
                        s.last_mq, eq_name(s.last_mq),
                        s.last_mq ? (long long)now_max - (long long)s.last_mq_ms : -1);
                }
            }
            std::fprintf(f, "--- per-thread LAST event (ring window) ---\n");
            for (size_t k = 0; k < n_sums; k++) {
                const ThreadSum& s = sums[k];
                std::fprintf(f, "  t%-3u (0x%08X, pri=%d)  last=%-22s  %lldms_ago  events=%u  blocked_on_recv_q=0x%08X (%lldms_ago)\n",
                    s.id, s.thread, s.pri, op_name(s.op), (long long)last_ms - (long long)s.ms, s.count,
                    s.mq, s.mq ? (long long)last_ms - (long long)s.mq_ms : -1);
            }
            std::fprintf(f, "--- last %llu raw events ---\n", (unsigned long long)(n < tail ? n : tail));
            size_t start = n > tail ? n - tail : 0;
            for (size_t i = start; i < n; i++) {
                const SchedEvent& e = evs[i];
                std::fprintf(f, "  seq=%llu  +%lldms  cur=t%u  %-22s  thr=t%u(0x%08X,pri=%d)  queue=0x%08X  head_after=0x%08X\n",
                    (unsigned long long)e.seq, (long long)e.ms - (long long)last_ms, e.current_thread_id,
                    op_name(e.op), e.thread_id, e.thread, e.priority, e.queue, e.head_after);
            }
            std::fclose(f);
        }
        char buf[192];
        std::snprintf(buf, sizeof(buf),
            "{\"ok\":true,\"wrote\":\"build/pms_sched_dump.txt\",\"events\":%llu,\"threads\":%llu,\"next_seq\":%llu}",
            (unsigned long long)n, (unsigned long long)n_sums, (unsigned long long)next_seq);
        return buf;
    }
    if (cmd == "dump_mesg") {
        // Dump the always-on message-event ring + never-evict per-queue table to
        // build/pms_mesg_dump.txt. {"mq":"0x..."} filters to one queue (a parked
        // thread's reply queue) to see if a wakeup send was ever made/dropped.
        struct MesgEvent {
            uint64_t seq; uint64_t ms; uint32_t mq; uint32_t msg; uint32_t thread;
            uint16_t thread_id; uint16_t valid_before; uint16_t valid_after;
            uint8_t op; uint8_t block; uint8_t game_thread; uint8_t pad; uint16_t reserved;
        };
        if (ultramodern_mesg_event_size() != sizeof(MesgEvent)) {
            return R"({"ok":false,"error":"mesg event size mismatch"})";
        }
        uint32_t filt = (uint32_t)std::strtoul(get_str(line, "mq").c_str(), nullptr, 0);
        size_t tail = (size_t)std::strtoul(get_str(line, "tail").c_str(), nullptr, 0);
        if (tail == 0) tail = 400;
        auto mop = [](uint8_t op) -> const char* {
            switch (op) {
                case 1: return "SEND_GAME"; case 2: return "SEND_EXTERNAL"; case 3: return "RECV_ENTER";
                case 4: return "RECV_BLOCK"; case 5: return "RECV_RETURN_OK"; case 6: return "EXT_DEQ_OK";
                case 7: return "EXT_DEQ_FULL"; case 8: return "DO_SEND_BLOCK"; default: return "?";
            }
        };
        const size_t RING_CAP = 65536;
        static MesgEvent mevs[RING_CAP];
        size_t n = 0; uint64_t next_seq = 0;
        ultramodern_mesg_recent_copy(mevs, RING_CAP, &n, &next_seq);
        FILE* f = std::fopen("pms_mesg_dump.txt", "w");
        size_t shown = 0, full_drops = 0;
        if (f) {
            uint64_t last_ms = n ? mevs[n - 1].ms : 0;
            std::fprintf(f, "=== mesg ring (next_seq=%llu, %llu in ring, filter_mq=0x%08X) ===\n",
                (unsigned long long)next_seq, (unsigned long long)n, filt);
            for (size_t i = 0; i < n; i++) if (mevs[i].op == 7) full_drops++;
            std::fprintf(f, "EXT_DEQ_FULL (re-queued/near-drop) total in window: %llu\n", (unsigned long long)full_drops);
            // QEVENTS MUST match N64ModernRuntime mesgqueue.cpp's QState (the
            // qstate_size guard below silently skips the table on a mismatch).
            constexpr uint32_t QEVENTS = 64;
            struct QState { uint32_t queue; uint32_t count; MesgEvent last[QEVENTS]; };
            if (ultramodern_mesg_qstate_size() == sizeof(QState)) {
                static QState qs[1024]; size_t nq = 0;
                ultramodern_mesg_qstates_copy(qs, 1024, &nq);
                std::fprintf(f, "--- per-queue last events (NEVER-EVICT depth=%u, %llu queues%s; seq for cross-queue ordering) ---\n",
                    QEVENTS, (unsigned long long)nq, filt ? ", FILTERED" : "");
                for (size_t i = 0; i < nq; i++) {
                    if (filt != 0 && qs[i].queue != filt) continue;
                    std::fprintf(f, "  queue=0x%08X (%u events):\n", qs[i].queue, qs[i].count);
                    uint32_t cnt = qs[i].count; uint32_t shown_n = cnt < QEVENTS ? cnt : QEVENTS;
                    for (uint32_t k = 0; k < shown_n; k++) {
                        uint32_t slot = (cnt - shown_n + k) & (QEVENTS - 1);
                        const MesgEvent& e = qs[i].last[slot];
                        std::fprintf(f, "      seq=%llu ms=%llu t%u %-14s msg=0x%08X valid %u->%u %s%s\n",
                            (unsigned long long)e.seq, (unsigned long long)e.ms,
                            e.thread_id, mop(e.op), e.msg, e.valid_before, e.valid_after,
                            e.block ? "BLOCK" : "NOBLOCK", e.game_thread ? " game" : " host");
                    }
                }
            }
            size_t start = (n > tail && filt == 0) ? n - tail : 0;
            for (size_t i = start; i < n; i++) {
                const MesgEvent& e = mevs[i];
                if (filt != 0 && e.mq != filt) continue;
                std::fprintf(f, "  +%lldms t%u %-14s mq=0x%08X msg=0x%08X valid %u->%u %s%s\n",
                    (long long)e.ms - (long long)last_ms, e.thread_id, mop(e.op), e.mq, e.msg,
                    e.valid_before, e.valid_after, e.block ? "BLOCK" : "NOBLOCK",
                    e.game_thread ? " game" : " host");
                shown++;
            }
            std::fclose(f);
        }
        char buf[224];
        std::snprintf(buf, sizeof(buf),
            "{\"ok\":true,\"wrote\":\"build/pms_mesg_dump.txt\",\"in_ring\":%llu,\"shown\":%llu,\"ext_deq_full\":%llu,\"next_seq\":%llu}",
            (unsigned long long)n, (unsigned long long)shown, (unsigned long long)full_drops, (unsigned long long)next_seq);
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
