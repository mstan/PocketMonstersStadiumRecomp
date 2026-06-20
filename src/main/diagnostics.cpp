/*
 * diagnostics.cpp — host-provided diagnostic hooks the shared N64ModernRuntime
 * fork calls on its failure paths.
 *
 * The junctioned runtime (lib/N64ModernRuntime) hardcodes, with no fallback,
 * calls to a handful of host-defined symbols:
 *   - pkmnstadium_trace_{write_idx,at,capacity} — read by librecomp's
 *     unhandled_lookup_trampoline (overlays.cpp) to dump the recent function
 *     trace when a LOOKUP_FUNC fails.
 *   - psr_post_mortem_dump — called by that trampoline and by ultramodern's
 *     do_send() on a controlled abort.
 * The names are PSR-branded because the fork grew them for PokemonStadiumRecomp;
 * any host linking this runtime must define them. We provide REAL
 * implementations (an always-on ring + a real dump), not stubs.
 *
 * NOTE (root-cause follow-up): the runtime should expose these as a registered
 * callback interface with no-op defaults instead of magic host-named symbols,
 * so neither host has to define PSR-branded functions. Tracked for a later
 * shared-runtime cleanup; not done here to avoid destabilizing the released PSR.
 */

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "app_paths.h"
#include "recomp.h"   // recomp_context + extern "C" tolerant-emit declarations
#include <librecomp/ultra_trace.hpp>  // always-on libultra-call ring (boot-stall diag)
#include <ultramodern/scheduler_tick.hpp>
#include <ultramodern/ultramodern.hpp>

extern "C" unsigned char* recomp_runtime_get_rdram(void);
extern "C" uint32_t ultramodern_running_queue_head(void);
extern "C" void ultramodern_mesg_recent_copy(void* out, size_t cap, size_t* n_written, uint64_t* next_seq_out);
extern "C" size_t ultramodern_mesg_event_size(void);
extern "C" void ultramodern_sched_recent_copy(void* out, size_t cap, size_t* n_written, uint64_t* next_seq_out);
extern "C" size_t ultramodern_sched_event_size(void);
extern "C" void recomp_sp_task_recent_copy(void* out, size_t cap, size_t* n_written, uint64_t* next_seq_out);
extern "C" size_t recomp_sp_task_event_size(void);

// ---- Shared-runtime debug counters -----------------------------------------
// librecomp's sp.cpp (sp_task_log::record) and PSR's rt64 fork timestamp task
// submissions with these. PMS doesn't drive a debug UI off them, but the
// shared runtime references them, so they must be defined. Real atomics.
namespace pkmnstadium { namespace dbg {
    std::atomic<uint64_t> g_frame_count{0};
    std::atomic<uint64_t> g_send_dl_count{0};
    std::atomic<uint64_t> g_send_dl_gfx_count{0};
}}

// ---- rt64 GDL-walk probe hook ----------------------------------------------
// The PSR rt64 fork's interpreter calls this for every G_DL CALL target while
// walking a display list (PSR records a ring for freeze diagnosis). PMS has no
// consumer yet, so this is a real no-op — the honest "not instrumented" state,
// not a bug-hiding stub. Wire a ring here if a DL-walk freeze needs tracing.
extern "C" void pkmnstadium_gdl_walk_snapshot(
    uint32_t /*target_vaddr*/, uint32_t /*parent_vaddr*/,
    const uint8_t* /*head_ptr*/, uint64_t /*submit_seq*/) {
}

// ---- Tolerant-emit handlers (recomp.h) -------------------------------------
// The engine emits calls to these for code it can't translate at compile time.
// Per recomp.h they are LOUD ABORTS, not stubs: untranslatable code must fail
// visibly, not silently misbehave. (Currently the only call site in PMS is the
// lone `tlbwi` in osMapTLBRdb — a debug/RDB-only function that is dead under
// HLE, so this should never fire during a normal boot.)
static void unhandled_abort(const char* kind, uint32_t pc, const char* detail) {
    const std::string path = pms::app_file("last_error.log").string();
    if (FILE* f = std::fopen(path.c_str(), "a")) {
        std::fprintf(f, "\n=== N64Recomp UNHANDLED %s ===\n  PC:     0x%08X\n  Detail: %s\n",
                     kind, pc, detail ? detail : "");
        std::fclose(f);
    }
    std::fprintf(stderr,
        "\n=== N64Recomp UNHANDLED %s ===\n  PC:     0x%08X\n  Detail: %s\n"
        "Engine could not translate this code path. Aborting.\n",
        kind, pc, detail ? detail : "");
    std::fflush(stderr);
    std::abort();
}

extern "C" void recomp_unhandled_branch(uint8_t*, recomp_context*, uint32_t instr_vram, uint32_t branch_target) {
    char d[32]; std::snprintf(d, sizeof d, "-> 0x%08X", branch_target);
    unhandled_abort("BRANCH", instr_vram, d);
}
extern "C" void recomp_unhandled_call(uint8_t*, recomp_context*, uint32_t instr_vram, uint32_t target) {
    char d[32]; std::snprintf(d, sizeof d, "-> 0x%08X", target);
    unhandled_abort("CALL", instr_vram, d);
}
extern "C" void recomp_unhandled_jalr(uint8_t*, recomp_context*, uint32_t instr_vram, uint64_t target_value, int rd) {
    char d[48]; std::snprintf(d, sizeof d, "rd=%d val=0x%016llX", rd, (unsigned long long)target_value);
    unhandled_abort("JALR", instr_vram, d);
}
extern "C" uint64_t recomp_unhandled_cop0_read(uint8_t*, recomp_context*, uint32_t instr_vram, int cop0_reg) {
    char d[24]; std::snprintf(d, sizeof d, "cop0 reg %d", cop0_reg);
    unhandled_abort("COP0_READ", instr_vram, d);
    return 0;
}
extern "C" void recomp_unhandled_cop0_write(uint8_t*, recomp_context*, uint32_t instr_vram, int cop0_reg, uint64_t value) {
    char d[48]; std::snprintf(d, sizeof d, "cop0 reg %d = 0x%016llX", cop0_reg, (unsigned long long)value);
    unhandled_abort("COP0_WRITE", instr_vram, d);
}
extern "C" void recomp_unhandled_instruction(uint8_t*, recomp_context*, uint32_t instr_vram, const char* opcode_name) {
    // TLB and cache instructions are vestigial under flat-memory HLE: there is
    // no TLB (all guest addresses map directly) and no CPU cache, so a libultra
    // routine that issues tlbwi/tlbwr/tlbp/tlbr or `cache` (e.g. osMapTLBRdb,
    // called from __osInitialize during boot) has no host effect. Treat them as
    // no-ops and let the rest of the (translated) function run. Everything else
    // is a genuine translation gap and must fail loudly.
    auto is_noop_under_hle = [](const char* op) {
        if (!op) return false;
        static const char* kNoop[] = {"tlbwi", "tlbwr", "tlbp", "tlbr", "cache"};
        for (const char* n : kNoop) if (std::strcmp(op, n) == 0) return true;
        return false;
    };
    if (is_noop_under_hle(opcode_name)) {
        static std::atomic<bool> warned{false};
        if (!warned.exchange(true)) {
            std::fprintf(stderr, "[PMS] ignoring HLE-vestigial instruction '%s' @ 0x%08X "
                         "(TLB/cache have no host effect; logged once)\n",
                         opcode_name, instr_vram);
            std::fflush(stderr);
        }
        return;
    }
    unhandled_abort("INSTRUCTION", instr_vram, opcode_name);
}

namespace {
constexpr uint32_t kScriptDiagCap = 128;
struct ScriptDispatchEvent {
    uint64_t seq;
    uint32_t stream;
    uint32_t opcode;
    uint32_t table_addr;
    uint32_t handler;
    uint32_t ra;
    uint32_t s1_global;
    uint32_t s2_arg0;
    uint32_t a0;
    uint32_t a1;
    uint32_t v0;
    uint8_t bytes[16];
};

std::atomic<uint64_t> g_script_diag_widx{0};
ScriptDispatchEvent g_script_diag_ring[kScriptDiagCap] = {};

bool pms_diag_rdram_range(uint32_t vaddr, uint32_t len) {
    const uint32_t start = vaddr & 0x1FFFFFFFu;
    return (vaddr >= 0x80000000u && vaddr < 0x80800000u &&
            len <= 0x00800000u && start <= 0x00800000u - len);
}

void pms_diag_copy_bytes(uint8_t* rdram, uint32_t vaddr, uint8_t* out, uint32_t len) {
    std::memset(out, 0, len);
    if (!pms_diag_rdram_range(vaddr, len)) {
        return;
    }
    const uint32_t paddr = vaddr & 0x1FFFFFFFu;
    for (uint32_t i = 0; i < len; i++) {
        out[i] = rdram[(paddr + i) ^ 3];
    }
}

uint32_t pms_diag_read_u32(uint8_t* rdram, uint32_t vaddr) {
    if (!pms_diag_rdram_range(vaddr, 4)) {
        return 0;
    }
    const uint32_t paddr = vaddr & 0x1FFFFFFFu;
    uint32_t value = 0;
    for (uint32_t i = 0; i < 4; i++) {
        value = (value << 8) | rdram[(paddr + i) ^ 3];
    }
    return value;
}

void pms_diag_print_words(uint8_t* rdram, const char* label, uint32_t addr, uint32_t count) {
    std::fprintf(stderr, "[pms-script] %s @ %08X:", label, addr);
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t cur = addr + i * 4u;
        std::fprintf(stderr, " [%08X]=%08X", cur, pms_diag_read_u32(rdram, cur));
    }
    std::fprintf(stderr, "\n");
}
}

extern "C" void pms_diag_script_dispatch(uint8_t* rdram, recomp_context* ctx) {
    if (std::getenv("PMS_SCRIPT_DIAG") == nullptr) {
        return;
    }

    const uint32_t stream = static_cast<uint32_t>(ctx->r16);
    const uint32_t opcode = static_cast<uint32_t>(ctx->r14) & 0xFFu;
    const uint32_t table_base = static_cast<uint32_t>(ctx->r18);
    const uint32_t table_addr = static_cast<uint32_t>(ctx->r24);
    const uint32_t handler = static_cast<uint32_t>(ctx->r25);
    const uint64_t seq = g_script_diag_widx.fetch_add(1, std::memory_order_relaxed);
    ScriptDispatchEvent ev{};
    ev.seq = seq;
    ev.stream = stream;
    ev.opcode = opcode;
    ev.table_addr = table_addr;
    ev.handler = handler;
    ev.ra = static_cast<uint32_t>(ctx->r31);
    ev.s1_global = static_cast<uint32_t>(ctx->r17);
    ev.s2_arg0 = static_cast<uint32_t>(ctx->r18);
    ev.a0 = static_cast<uint32_t>(ctx->r4);
    ev.a1 = static_cast<uint32_t>(ctx->r5);
    ev.v0 = static_cast<uint32_t>(ctx->r2);
    pms_diag_copy_bytes(rdram, stream, ev.bytes, sizeof(ev.bytes));
    g_script_diag_ring[seq % kScriptDiagCap] = ev;

    if (handler >= 0x80000400u && handler < 0x80080000u) {
        return;
    }

    std::fprintf(stderr,
        "[pms-script] bad dispatch stream=0x%08X opcode=0x%02X table=0x%08X "
        "entry=0x%08X handler=0x%08X s1_global=0x%08X\n",
        stream, opcode, table_base, table_addr, handler, static_cast<uint32_t>(ctx->r17));

    std::fprintf(stderr, "[pms-script] stream bytes:");
    uint8_t stream_bytes[32];
    pms_diag_copy_bytes(rdram, stream, stream_bytes, sizeof(stream_bytes));
    for (uint32_t i = 0; i < sizeof(stream_bytes); i++) {
        std::fprintf(stderr, " %02X", static_cast<uint32_t>(stream_bytes[i]));
    }
    std::fprintf(stderr, "\n[pms-script] table words:");
    for (int i = -4; i <= 4; i++) {
        const uint32_t addr = table_addr + uint32_t(i * 4);
        std::fprintf(stderr, " [%08X]=%08X", addr, pms_diag_read_u32(rdram, addr));
    }
    std::fprintf(stderr, "\n");
    if (section_addresses != nullptr) {
        const uint32_t section27 = static_cast<uint32_t>(section_addresses[27]);
        std::fprintf(stderr,
            "[pms-script] section_addresses[13]=0x%08X [27]=0x%08X\n",
            static_cast<uint32_t>(section_addresses[13]), section27);
        pms_diag_print_words(rdram, "section27+0x11820", section27 + 0x11820u, 12);
        pms_diag_print_words(rdram, "section27+0x1184C", section27 + 0x1184Cu, 8);
    }

    const uint64_t first = (seq + 1 > 32) ? (seq + 1 - 32) : 0;
    std::fprintf(stderr, "[pms-script] recent dispatches:\n");
    for (uint64_t i = first; i <= seq; i++) {
        const ScriptDispatchEvent& cur = g_script_diag_ring[i % kScriptDiagCap];
        if (cur.seq != i) {
            continue;
        }
        std::fprintf(stderr,
            "  #%llu stream=%08X op=%02X handler=%08X table=%08X "
            "ra=%08X s1=%08X s2=%08X a0=%08X a1=%08X v0=%08X bytes=",
            (unsigned long long)cur.seq, cur.stream, cur.opcode,
            cur.handler, cur.table_addr, cur.ra, cur.s1_global,
            cur.s2_arg0, cur.a0, cur.a1, cur.v0);
        for (uint8_t b : cur.bytes) {
            std::fprintf(stderr, "%02X", static_cast<uint32_t>(b));
        }
        std::fprintf(stderr, "\n");
    }
    std::fflush(stderr);
}

namespace {
// Always-on function-name trace ring. Generated code pushes one entry per
// recompiled-function entry when trace_mode is enabled in game.toml (currently
// off, so the ring is empty until tracing is turned on for a debug session).
// The runtime reads it backward on a lookup-miss to show the call path.
constexpr uint32_t kTraceCap = 4096;
struct TraceNameEvent {
    const char* name;
    uint32_t thread;
    uint16_t thread_id;
};

struct MesgEvent {
    uint64_t seq;
    uint64_t ms;
    uint32_t mq;
    uint32_t msg;
    uint32_t thread;
    uint16_t thread_id;
    uint16_t valid_before;
    uint16_t valid_after;
    uint8_t op;
    uint8_t block;
    uint8_t game_thread;
    uint8_t pad;
    uint16_t reserved;
};

static_assert(sizeof(MesgEvent) == 40, "MesgEvent layout must match ultramodern");

struct SchedEvent {
    uint64_t seq;
    uint64_t ms;
    uint32_t op;
    uint32_t queue;
    uint32_t thread;
    uint32_t current_thread;
    uint32_t head_after;
    uint32_t next_after;
    uint16_t thread_id;
    uint16_t current_thread_id;
    int16_t priority;
    uint16_t pad;
};

static_assert(sizeof(SchedEvent) == 48, "SchedEvent layout must match ultramodern");

struct SpTaskEvent {
    uint64_t seq;
    uint64_t ms;
    uint64_t frame;
    uint64_t send_dl;
    uint32_t mips_ra;
    uint32_t task_ptr;
    uint32_t wrapper_ptr;
    uint32_t suspect;
    uint32_t task_type;
    uint32_t task_flags;
    uint32_t ucode;
    uint32_t data_ptr;
    uint32_t data_size;
    uint32_t output_buff;
    uint32_t output_buff_size;
    uint32_t wrapper_words[12];
    uint32_t task_words[16];
};

const char* sp_task_type_name(uint32_t t) {
    switch (t) {
    case M_GFXTASK: return "M_GFXTASK";
    case M_AUDTASK: return "M_AUDTASK";
    case M_VIDTASK: return "M_VIDTASK";
    case M_NJPEGTASK: return "M_NJPEGTASK";
    case 6: return "M_HVQMTASK";
    default: return "UNKNOWN";
    }
}

TraceNameEvent g_trace_ring[kTraceCap] = {};
std::atomic<uint64_t> g_trace_widx{0};

TraceNameEvent current_trace_name_event(const char* name) {
    TraceNameEvent ev{};
    ev.name = name;
    PTR(OSThread) self = ultramodern::this_thread();
    if (self != NULLPTR) {
        if (unsigned char* rdram = recomp_runtime_get_rdram()) {
            OSThread* t = TO_PTR(OSThread, self);
            ev.thread = static_cast<uint32_t>(self);
            ev.thread_id = static_cast<uint16_t>(t->id);
        }
    }
    return ev;
}

bool is_guest_ptr(uint32_t ptr) {
    return pms_diag_rdram_range(ptr, 1);
}

template <typename T>
T* diag_to_ptr(uint8_t* rdram, uint32_t ptr) {
    if (rdram == nullptr || !pms_diag_rdram_range(ptr, sizeof(T))) {
        return nullptr;
    }
    return reinterpret_cast<T*>(&rdram[ptr & 0x1FFFFFFFu]);
}

template <size_t N>
void append_unique(uint32_t (&items)[N], size_t& count, uint32_t value) {
    if (!is_guest_ptr(value)) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        if (items[i] == value) {
            return;
        }
    }
    if (count < N) {
        items[count++] = value;
    }
}

const char* thread_state_name(uint16_t state) {
    switch (state) {
    case STOPPED: return "STOPPED";
    case QUEUED: return "QUEUED";
    case RUNNING: return "RUNNING";
    case BLOCKED: return "BLOCKED";
    default: return "UNKNOWN";
    }
}

const char* mesg_op_name(uint8_t op) {
    switch (op) {
    case 1: return "SEND_GAME";
    case 2: return "SEND_EXTERNAL";
    case 3: return "RECV_ENTER";
    case 4: return "RECV_BLOCK";
    case 5: return "RECV_RETURN_OK";
    case 6: return "EXT_DEQ_OK";
    case 7: return "EXT_DEQ_FULL";
    case 8: return "DO_SEND_BLOCK";
    default: return "UNKNOWN";
    }
}

bool mesg_is_block(uint8_t op) {
    return op == 4 || op == 8;
}

void format_fourcc(uint32_t value, char out[5]) {
    bool printable = true;
    for (int i = 0; i < 4; i++) {
        const unsigned char c = static_cast<unsigned char>((value >> (24 - i * 8)) & 0xFFu);
        out[i] = static_cast<char>(c);
        if (c < 0x20 || c > 0x7E) {
            printable = false;
        }
    }
    out[4] = '\0';
    if (!printable) {
        out[0] = '\0';
    }
}

void print_mesg_event(FILE* f, const char* prefix, const MesgEvent& ev) {
    char fourcc[5] = {};
    format_fourcc(ev.msg, fourcc);
    std::fprintf(f,
                 "%s#%-7llu t=%6llums tid=%2u th=%08X %-14s mq=%08X msg=%08X",
                 prefix,
                 (unsigned long long)ev.seq,
                 (unsigned long long)ev.ms,
                 ev.thread_id,
                 ev.thread,
                 mesg_op_name(ev.op),
                 ev.mq,
                 ev.msg);
    if (fourcc[0] != '\0') {
        std::fprintf(f, " '%s'", fourcc);
    }
    std::fprintf(f, " valid=%u->%u block=%u game=%u\n",
                 ev.valid_before,
                 ev.valid_after,
                 ev.block,
                 ev.game_thread);
}

const char* sched_op_name(uint32_t op) {
    switch (op) {
    case 1: return "INSERT";
    case 2: return "POP";
    case 3: return "REMOVE";
    case 4: return "REMOVE_MISS";
    case 100: return "SCHEDULE_RUNNING";
    case 101: return "CHECK_EMPTY";
    case 102: return "CHECK_PEEK";
    case 103: return "CHECK_NO_SWAP";
    case 104: return "CHECK_SWAP";
    case 110: return "SWAP_ENTER";
    case 111: return "SWAP_INSERT_SELF";
    case 112: return "SWAP_WAIT_ENTER";
    case 113: return "SWAP_WAIT_RETURN";
    case 120: return "WAIT_BEGIN";
    case 121: return "WAIT_RETURN";
    case 122: return "RESUME_SIGNAL";
    case 123: return "RUN_NEXT_ENTER";
    case 124: return "RUN_NEXT_EMPTY";
    case 125: return "RUN_NEXT_TARGET";
    case 126: return "RUN_NEXT_WAIT_ENTER";
    case 127: return "RUN_NEXT_WAIT_RETURN";
    case 130: return "YIELD_ANY_ENTER";
    case 131: return "YIELD_ANY_EMPTY";
    case 132: return "YIELD_ANY_TARGET";
    case 133: return "YIELD_ANY_REQUEUE_SELF";
    case 134: return "YIELD_ANY_WAIT_ENTER";
    case 135: return "YIELD_ANY_WAIT_RETURN";
    default: return "UNKNOWN";
    }
}

void print_sched_event(FILE* f, const char* prefix, const SchedEvent& ev) {
    std::fprintf(f,
                 "%s#%-6llu t=%6llums cur=%2u/%08X %-11s q=%08X th=%2u/%08X pri=%d head=%08X next=%08X\n",
                 prefix,
                 (unsigned long long)ev.seq,
                 (unsigned long long)ev.ms,
                 ev.current_thread_id,
                 ev.current_thread,
                 sched_op_name(ev.op),
                 ev.queue,
                 ev.thread_id,
                 ev.thread,
                 ev.priority,
                 ev.head_after,
                 ev.next_after);
}

void print_sp_task_event(FILE* f, const char* prefix, const SpTaskEvent& ev) {
    std::fprintf(f,
                 "%s#%-6llu t=%6llums type=%s(%u) task=%08X wrapper=%08X "
                 "ra=%08X flags=%08X ucode=%08X data=%08X size=%08X out=%08X out_size=%08X suspect=%08X frame=%llu send_dl=%llu\n",
                 prefix,
                 (unsigned long long)ev.seq,
                 (unsigned long long)ev.ms,
                 sp_task_type_name(ev.task_type), ev.task_type,
                 ev.task_ptr, ev.wrapper_ptr, ev.mips_ra, ev.task_flags,
                 ev.ucode, ev.data_ptr, ev.data_size,
                 ev.output_buff, ev.output_buff_size, ev.suspect,
                 (unsigned long long)ev.frame,
                 (unsigned long long)ev.send_dl);
}

void print_thread_list(FILE* f, unsigned char* rdram, uint32_t head) {
    (void)rdram;
    if (!is_guest_ptr(head)) {
        std::fprintf(f, " empty\n");
        return;
    }

    uint32_t cur = head;
    int n = 0;
    while (is_guest_ptr(cur) && n < 16) {
        if (!pms_diag_rdram_range(cur, sizeof(OSThread))) {
            std::fprintf(f, " %08X(invalid thread range)", cur);
            break;
        }
        OSThread* t = diag_to_ptr<OSThread>(rdram, cur);
        if (t == nullptr) {
            std::fprintf(f, " %08X(invalid thread range)", cur);
            break;
        }
        std::fprintf(f, " %08X(id=%d pri=%d next=%08X q=%08X)",
                     cur, t->id, t->priority,
                     static_cast<uint32_t>(t->next),
                     static_cast<uint32_t>(t->queue));
        cur = static_cast<uint32_t>(t->next);
        n++;
    }
    if (is_guest_ptr(cur)) {
        std::fprintf(f, " ...");
    }
    std::fprintf(f, "\n");
}
} // namespace

extern "C" void pkmnstadium_trace_push(const char* name) {
    const uint64_t i = g_trace_widx.fetch_add(1, std::memory_order_relaxed);
    g_trace_ring[i % kTraceCap] = current_trace_name_event(name);
}

extern "C" uint64_t pkmnstadium_trace_write_idx(void) {
    return g_trace_widx.load(std::memory_order_relaxed);
}

extern "C" const char* pkmnstadium_trace_at(uint64_t idx) {
    return g_trace_ring[idx % kTraceCap].name;
}

extern "C" uint32_t pkmnstadium_trace_capacity(void) {
    return kTraceCap;
}

// ---- Text-draw discovery census --------------------------------------------
//
// PMS-J relaid the code vs. the US PokemonStadium oracle, so the string-draw
// function (US func_8001F1E8: a glyph-byte-string printf that loops issuing
// texture-rect draws) is at an unknown PMS-J address. We find it empirically,
// not by static address-guessing.
//
// trace_mode=true makes the recompiler emit TRACE_ENTRY() at every function
// entry; we extend that macro (include/trace.h) to also forward (ra,a0..a3)
// here. This census is ALWAYS-ON from process start for every user (no env
// gate) and keeps a bounded per-PC tally of every function entry whose
// arguments match the string-draw signature:
//
//   a0 (x) and a1 (y) are small (screen coords), and
//   a2 (fmt) points at a NUL-terminated run of nonzero RDRAM bytes (glyphs).
//
// The real string-drawer is the FUN_ called far more often than any incidental
// match, with a2 consistently pointing at glyph byte-strings. The dump (debug
// server "textdump", or any controlled abort) ranks candidates by call count
// and shows a sample of the source bytes — that sample is also the live
// encoding inventory + natural content-hash keys for the translation layer.
namespace {
constexpr uint32_t kTextCandCap   = 256; // distinct PCs tracked
constexpr uint32_t kTextSampleLen = 64;  // source bytes shown per PC (find aid)
constexpr uint32_t kSrcMax        = 256; // max source string captured/hashed
constexpr uint32_t kStringDrawPC  = 0x8001A944u; // PMS-J string-draw printf
constexpr uint32_t kDescDrawPC    = 0x8001A920u; // PMS-J Pokedex description draw;
                                                 // string in a3/r7 (NOT r6), drawn
                                                 // via _Printf("%s", desc). Found via
                                                 // the LONGTEXT discovery probe.
constexpr uint32_t kPrintfPC      = 0x80055CE0u; // PMS-J _Printf (vararg fmt);
                                                 // a0=writeproc a1=dest a2=fmt.
                                                 // Universal formatter chokepoint
                                                 // for battle/overlay text not
                                                 // covered by the class-1 conv.

// FNV-1a 64 over raw source glyph bytes — the translation KV key.
inline uint64_t pms_fnv1a(const uint8_t* p, uint32_t n) {
    uint64_t h = 1469598103934665603ull;
    for (uint32_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}

// ---- String inventory (full translation authoring) -------------------------
// Every text fn aggregates into ONE PC (the string-draw printf), so the per-PC
// census can't inventory distinct strings. This table is keyed by content-hash
// and records every distinct source string drawn by kStringDrawPC across a full
// screen sweep — the authoritative list to translate. Dump via "stringdump".
constexpr uint32_t kStrInvCap = 4096;
struct StrInvEntry {
    uint64_t key;            // FNV-1a64 over bytes[0..len)
    uint8_t  bytes[kSrcMax];
    uint16_t len;
    uint16_t a0, a1;         // last-seen coords
    uint32_t count;
    bool     used;
};
StrInvEntry g_strinv[kStrInvCap];
std::mutex  g_strinv_mtx;
std::atomic<uint32_t> g_strinv_dropped{0};

// Write stringdump.log from g_strinv. Caller MUST hold g_strinv_mtx. Returns
// the number of distinct strings written. Used both for the on-demand dump and
// for continuous persistence (written on every newly-seen string) so a crash
// mid-capture never loses the inventory.
uint32_t dump_stringinv_locked() {
    const std::string path = pms::app_file("stringdump.log").string();
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return 0;
    static uint32_t order[kStrInvCap]; // lock-held → single-threaded; off-stack
    uint32_t n = 0;
    for (uint32_t i = 0; i < kStrInvCap; ++i) if (g_strinv[i].used) order[n++] = i;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t best = i;
        for (uint32_t j = i + 1; j < n; ++j)
            if (g_strinv[order[j]].count > g_strinv[order[best]].count) best = j;
        uint32_t t = order[i]; order[i] = order[best]; order[best] = t;
    }
    std::fprintf(f, "=== string inventory (distinct strings drawn by 0x%08X) ===\n", kStringDrawPC);
    std::fprintf(f, "  distinct strings: %u   dropped(table full): %u\n", n,
                 g_strinv_dropped.load(std::memory_order_relaxed));
    std::fprintf(f, "  format: <key> <count> <x> <y> <len> <src_hex>\n\n");
    for (uint32_t i = 0; i < n; ++i) {
        const StrInvEntry& e = g_strinv[order[i]];
        std::fprintf(f, "0x%016llX %6u %4u %4u %3u ",
                     (unsigned long long)e.key, e.count, e.a0, e.a1, e.len);
        for (uint16_t k = 0; k < e.len; ++k) std::fprintf(f, "%02x", e.bytes[k]);
        std::fprintf(f, "\n");
    }
    std::fclose(f);
    return n;
}

void strinv_upsert(const uint8_t* b, uint16_t len, uint16_t a0, uint16_t a1) {
    if (len == 0) return;
    const uint64_t key = pms_fnv1a(b, len);
    std::lock_guard<std::mutex> lk(g_strinv_mtx);
    StrInvEntry* slot = nullptr;
    uint32_t freei = kStrInvCap;
    for (uint32_t i = 0; i < kStrInvCap; ++i) {
        if (g_strinv[i].used) {
            if (g_strinv[i].key == key) { slot = &g_strinv[i]; break; }
        } else if (freei == kStrInvCap) {
            freei = i;
        }
    }
    bool is_new = false;
    if (slot == nullptr) {
        if (freei == kStrInvCap) { g_strinv_dropped.fetch_add(1, std::memory_order_relaxed); return; }
        slot = &g_strinv[freei];
        slot->used = true; slot->key = key; slot->len = len;
        std::memcpy(slot->bytes, b, len);
        slot->count = 0;
        is_new = true;
    }
    slot->a0 = a0; slot->a1 = a1;
    slot->count++;
    // Persist continuously: every newly-seen string is flushed to disk so a
    // crash (e.g. the known deep-overlay tailcall abort) never loses captures.
    if (is_new) dump_stringinv_locked();
}

// ---- Generalized text-draw coverage ----------------------------------------
// PMS draws text through MANY routines (~70 distinct PCs seen), not just
// kStringDrawPC. Each takes the source string pointer in one of the first arg
// registers (a0..a3). To translate ALL of them we (1) detect a text string in
// any arg during the census, and (2) maintain a lock-free set of PCs proven to
// draw text so the xlate hook only pays the arg-scan cost for real text PCs.

// Does `ptr` reference a NUL-terminated EUC-JP/ASCII string of >=2 bytes whose
// FIRST byte is plainly text? Fills out[0..*outlen). The RAM-range gate is the
// real discriminator: coords/small ints and non-pointers are rejected here, so
// only genuine RAM pointers reach the byte scan. Returns false otherwise.
bool read_text_arg(unsigned char* rdram, uint32_t ptr, uint8_t* out, uint16_t* outlen) {
    if (ptr < 0x80000000u || ptr >= 0x80800000u) return false; // RDRAM range only
    if (!pms_diag_rdram_range(ptr, 2)) return false;
    const uint32_t pa = ptr & 0x1FFFFFFFu;          // RDRAM is byte-swizzled (^3)
    uint16_t n = 0;
    bool terminated = false;
    for (uint32_t i = 0; i < kSrcMax; ++i) {
        if (!pms_diag_rdram_range(ptr + i, 1)) break;
        unsigned char c = rdram[(pa + i) ^ 3];
        if (c == 0) { terminated = (i >= 2); break; }
        // EVERY byte must be valid EUC-JP/ASCII text (newline, printable ASCII,
        // or a EUC-JP lead/kana byte). Rejects binary structs/pointers/markers.
        const bool textish = (c == 0x0A) || (c >= 0x20 && c <= 0x7E) ||
                             c == 0x8E || c == 0x8F || (c >= 0xA1 && c <= 0xFE);
        if (!textish) return false;
        out[n++] = c;
    }
    if (!terminated || n < 2) return false;
    *outlen = n;
    return true;
}

// Lock-free open-addressing set of text-draw PCs (slot value 0 == empty).
constexpr uint32_t kTextPcSetN = 512;               // power of two; >> ~70 seen
std::atomic<uint32_t> g_textpc_set[kTextPcSetN];
inline bool textpc_contains(uint32_t pc) {
    uint32_t h = (pc * 2654435761u) & (kTextPcSetN - 1);
    for (uint32_t i = 0; i < kTextPcSetN; ++i) {
        uint32_t v = g_textpc_set[(h + i) & (kTextPcSetN - 1)].load(std::memory_order_relaxed);
        if (v == pc) return true;
        if (v == 0) return false;
    }
    return false;
}
inline void textpc_add(uint32_t pc) {
    if (pc == 0) return;
    uint32_t h = (pc * 2654435761u) & (kTextPcSetN - 1);
    for (uint32_t i = 0; i < kTextPcSetN; ++i) {
        uint32_t idx = (h + i) & (kTextPcSetN - 1);
        uint32_t v = g_textpc_set[idx].load(std::memory_order_relaxed);
        if (v == pc) return;
        if (v == 0) {
            uint32_t expected = 0;
            if (g_textpc_set[idx].compare_exchange_strong(expected, pc, std::memory_order_relaxed))
                return;
            if (g_textpc_set[idx].load(std::memory_order_relaxed) == pc) return;
        }
    }
}

struct TextDrawCand {
    uint32_t pc;                  // guest VRAM, parsed from "FUN_80XXXXXX"
    uint32_t ra;                  // representative caller (latest)
    uint32_t a0, a1, a2;          // latest sample args
    uint64_t count;               // qualifying call count
    uint8_t  sample[kTextSampleLen];
    uint8_t  sample_len;          // bytes captured (excludes NUL)
    bool     used;
};

TextDrawCand g_textcand[kTextCandCap];
std::mutex   g_textcand_mtx;
std::atomic<uint64_t> g_text_qualifying{0}; // total qualifying calls seen

bool textprobe_armed() {
    static const bool armed = [] {
        const char* e = std::getenv("PMS_TEXTPROBE");
        return e && (e[0] == '1' || e[0] == 't' || e[0] == 'T' || e[0] == 'y' || e[0] == 'Y');
    }();
    return armed;
}

uint32_t parse_fun_pc(const char* name) {
    // names are "FUN_80XXXXXX" (or "func_80XXXXXX"): hex address after '_'.
    if (name == nullptr) return 0;
    const char* us = std::strrchr(name, '_');
    const char* p = us ? us + 1 : name;
    return (uint32_t)std::strtoul(p, nullptr, 16);
}
} // namespace

// Forwarded from include/trace.h's TRACE_ENTRY() on every recompiled-function
// entry (only when trace_mode=true). Hot path: cheap reject first.
extern "C" void pkmnstadium_textdraw_probe(const char* name,
                                           uint32_t ra,
                                           uint32_t a0, uint32_t a1,
                                           uint32_t a2, uint32_t a3) {
    (void)a3;
    // ALWAYS-ON capture (no env gate). The distinct-string inventory
    // (stringdump.log) and the text-PC registration below run for EVERY user
    // continuously from process start — the always-on ring-buffer model: we
    // never "arm" capture and hope to catch the window, we record every unique
    // on-screen string as it is first drawn and persist it immediately. This
    // also seeds the text-PC set so the class-1 sibling routines translate
    // during normal play (not only under a former PMS_TEXTPROBE capture).
    // PMS_TEXTPROBE now only adds the developer watch-string discovery
    // (xlate_discovery, in the xlate hook); the inventory itself is unconditional.
    unsigned char* rdram = recomp_runtime_get_rdram();
    if (rdram == nullptr) {
        return;
    }
    const uint32_t pc = parse_fun_pc(name);
    uint8_t buf[kSrcMax];
    uint16_t len = 0;

    // _Printf is the universal vararg formatter (a0=writeproc, a1=dest, a2=fmt).
    // Most on-screen text — including battle/overlay labels NOT covered by the
    // class-1 (x=a0,y=a1,str=a2) convention — is built through it. Inventory its
    // format string (a2) regardless of the coord gate so authoring captures the
    // battle strings, and flag it for the xlate hook.
    if (pc == kPrintfPC) {
        if (read_text_arg(rdram, a2, buf, &len)) {
            g_text_qualifying.fetch_add(1, std::memory_order_relaxed);
            strinv_upsert(buf, len, (uint16_t)a0, (uint16_t)a1);
            textpc_add(pc);
        }
        return;
    }

    // Class-1 text-draw convention (proven clean): x=a0, y=a1, str=a2. This is
    // what the menu/selection/description routines all use (kStringDrawPC plus
    // ~7 siblings like 0x8002C1B4 / 0x82005D90 / 0x80028060). The a0/a1 coord
    // gate is the key precision filter — it rejects binary data and resource
    // headers ("Yay0"/"PRESJPEG"/"DONE") that decode as valid EUC-JP by chance.
    if (a0 >= 0x1000u || a1 >= 0x1000u) {
        return; // a0,a1 must look like screen coords
    }
    if (!read_text_arg(rdram, a2, buf, &len)) {
        return; // a2 must point at a NUL-terminated EUC-JP/ASCII text string
    }

    g_text_qualifying.fetch_add(1, std::memory_order_relaxed);

    // Inventory EVERY distinct string drawn by ANY class-1 text routine (the
    // authoring source: stringdump.log), and flag the PC for the xlate hook.
    strinv_upsert(buf, len, (uint16_t)a0, (uint16_t)a1);
    textpc_add(pc);

    std::lock_guard<std::mutex> lk(g_textcand_mtx);
    TextDrawCand* slot = nullptr;
    for (uint32_t i = 0; i < kTextCandCap; ++i) {
        if (g_textcand[i].used && g_textcand[i].pc == pc) { slot = &g_textcand[i]; break; }
    }
    if (slot == nullptr) {
        for (uint32_t i = 0; i < kTextCandCap; ++i) {
            if (!g_textcand[i].used) { slot = &g_textcand[i]; slot->used = true; slot->pc = pc; break; }
        }
        if (slot == nullptr) { return; } // table full (>256 distinct PCs — unexpected)
    }
    slot->ra = ra;
    slot->a0 = a0; slot->a1 = a1; slot->a2 = a2;
    slot->count++;
    uint8_t n = (len < kTextSampleLen) ? len : (uint8_t)kTextSampleLen;
    std::memcpy(slot->sample, buf, n);
    slot->sample_len = n;
}

extern "C" void pkmnstadium_textdraw_dump(void) {
    const std::string path = pms::app_file("textdraw_probe.log").string();
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) {
        std::fprintf(stderr, "[PMS] textdraw_dump: cannot open %s\n", path.c_str());
        return;
    }
    std::time_t t = std::time(nullptr);
    char ts[64] = {0};
    std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
    std::fprintf(f, "=== text-draw census ===\n");
    std::fprintf(f, "  time:            %s\n", ts);
    std::fprintf(f, "  capture:         always-on (every user)\n");
    std::fprintf(f, "  dev discovery:   %s\n", textprobe_armed() ? "on (PMS_TEXTPROBE)" : "off");
    std::fprintf(f, "  total fn entries:%llu\n", (unsigned long long)pkmnstadium_trace_write_idx());
    std::fprintf(f, "  qualifying calls:%llu\n",
                 (unsigned long long)g_text_qualifying.load(std::memory_order_relaxed));

    // Snapshot + sort by count desc (small table; simple selection sort).
    TextDrawCand snap[kTextCandCap];
    {
        std::lock_guard<std::mutex> lk(g_textcand_mtx);
        std::memcpy(snap, g_textcand, sizeof(snap));
    }
    uint32_t order[kTextCandCap];
    uint32_t n = 0;
    for (uint32_t i = 0; i < kTextCandCap; ++i) { if (snap[i].used) order[n++] = i; }
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t best = i;
        for (uint32_t j = i + 1; j < n; ++j) {
            if (snap[order[j]].count > snap[order[best]].count) best = j;
        }
        uint32_t tmp = order[i]; order[i] = order[best]; order[best] = tmp;
    }

    std::fprintf(f, "  distinct PCs:    %u\n\n", n);
    std::fprintf(f, "  (key = FNV-1a64 of source bytes; for translations.json use src_hex below.\n");
    std::fprintf(f, "   keys are exact only for strings <= %u bytes — longer ones are truncated.)\n\n",
                 kTextSampleLen);
    std::fprintf(f, "  rank  pc          count     a0   a1   a2(fmt)    key                 sample (hex / ascii)\n");
    for (uint32_t i = 0; i < n; ++i) {
        const TextDrawCand& c = snap[order[i]];
        const uint64_t key = pms_fnv1a(c.sample, c.sample_len);
        std::fprintf(f, "  %3u   0x%08X  %-8llu  %4u %4u 0x%08X  0x%016llX  ",
                     i, c.pc, (unsigned long long)c.count, c.a0, c.a1, c.a2,
                     (unsigned long long)key);
        for (uint32_t k = 0; k < c.sample_len; ++k) std::fprintf(f, "%02X", c.sample[k]);
        std::fprintf(f, "  |");
        for (uint32_t k = 0; k < c.sample_len; ++k) {
            unsigned char ch = c.sample[k];
            std::fprintf(f, "%c", (ch >= 0x20 && ch < 0x7F) ? ch : '.');
        }
        std::fprintf(f, "|\n");
    }
    std::fclose(f);
    std::fprintf(stderr, "[PMS] textdraw census written: %s (%u PCs, %llu qualifying calls)\n",
                 path.c_str(), n,
                 (unsigned long long)g_text_qualifying.load(std::memory_order_relaxed));
    std::fflush(stderr);
}

// Dump the distinct-string inventory (every string drawn by the string-draw
// printf this session). Authoritative source for authoring translations.json:
// each line gives the FNV key + full src_hex; decode EUC-JP with
// tools/pms_build_translations.py. Run a full screen sweep first.
extern "C" void pkmnstadium_stringdump(void) {
    uint32_t n;
    { std::lock_guard<std::mutex> lk(g_strinv_mtx); n = dump_stringinv_locked(); }
    std::fprintf(stderr, "[PMS] string inventory written: stringdump.log (%u distinct)\n", n);
    std::fflush(stderr);
}

// Scan ALL of RDRAM for EUC-JP text runs (>= minlen bytes, containing at least
// one JP byte) and write them to memscan.log as "<addr> <len> <hex>". Finds
// source text already loaded/decompressed in RAM (e.g. the Pokedex description
// table) so it can be extracted in bulk WITHOUT visiting each entry in game.
// A run ends at the first non-EUC-JP byte (NUL/pointer/binary), so table entries
// separate cleanly. Decode the hex with euc_jp in the host tool.
extern "C" void pkmnstadium_memscan(uint32_t minlen) {
    unsigned char* rdram = recomp_runtime_get_rdram();
    if (rdram == nullptr) return;
    if (minlen < 4) minlen = 4;
    const std::string path = pms::app_file("memscan.log").string();
    FILE* f = std::fopen(path.c_str(), "w");
    if (f == nullptr) return;
    const uint32_t base = 0x80000000u, end = 0x80800000u;
    auto textish = [](unsigned char c) {
        return (c == 0x0A) || (c >= 0x20 && c <= 0x7E) || c == 0x8E || c == 0x8F ||
               (c >= 0xA1 && c <= 0xFE);
    };
    uint8_t buf[2048];
    uint32_t found = 0;
    uint32_t a = base;
    while (a < end) {
        if (!textish(rdram[(a & 0x1FFFFFFFu) ^ 3])) { ++a; continue; }
        uint32_t n = 0; bool hasJP = false; uint32_t p = a;
        while (p < end && n < sizeof(buf)) {
            unsigned char cc = rdram[(p & 0x1FFFFFFFu) ^ 3];
            if (!textish(cc)) break;
            if (cc >= 0x80) hasJP = true;
            buf[n++] = cc; ++p;
        }
        if (n >= minlen && hasJP) {
            std::fprintf(f, "0x%08X %u ", a, n);
            for (uint32_t k = 0; k < n; ++k) std::fprintf(f, "%02x", buf[k]);
            std::fprintf(f, "\n");
            ++found;
        }
        a = p + 1;
    }
    std::fclose(f);
    std::fprintf(stderr, "[PMS] memscan: %u EUC-JP runs (>=%u bytes) -> memscan.log\n",
                 found, minlen);
    std::fflush(stderr);
}

// ---- Runtime English-translation layer -------------------------------------
//
// Hooks the string-draw printf (PMS-J 0x8001A944) at its TRACE_ENTRY (the
// recompiler emits one at every fn entry; trace.h forwards rdram+ctx here).
// Strategy: content-hash the source glyph bytes (the fmt at a2=ctx->r6), look
// up an English replacement, and on a hit write the English (ASCII, NUL-term)
// into transient scratch on the calling thread's guest stack and repoint
// ctx->r6 at it. The ORIGINAL function then runs unchanged: _Printf formats it
// (dynamic %d/%s args stay intact — the English template keeps the specifiers)
// and renders it glyph-by-glyph through the existing Latin glyphs. So:
//   - no engine change / no regen (vs. the recompiler "reimplemented" path),
//   - replacement length is unbounded (we never write back into game RAM),
//   - dynamic format strings work for free.
// KV store = translations.json next to the exe; hot-reloaded on mtime change.
// Keyed by FNV-1a64 of the raw source bytes (robust to RDRAM buffer reuse).
namespace {
// Per-entry translation value. orig_w = the Japanese footprint in GLYPH units
// (max line; computed by tools/pms_build_translations and embedded as "orig_w"
// — documentation + the default fit budget). fit_w = optional override ("fit_w"
// in the JSON): when >=0 the English is condensed to fit_w*slot_adv px instead
// of the Japanese footprint, so an entry can use more (or less) space than the
// original when there is room. -1 means "match Japanese" (the default).
struct XlateVal { std::string en; int orig_w = -1; int fit_w = -1; };
std::unordered_map<uint64_t, XlateVal> g_xlate;        // hash(src) -> value
std::mutex g_xlate_mtx;
std::atomic<bool> g_xlate_armed{false};                // lock-free fast reject
std::filesystem::file_time_type g_xlate_mtime{};
bool g_xlate_ever_loaded = false;
std::atomic<uint64_t> g_xlate_calls{0};
std::atomic<uint64_t> g_xlate_hits{0};

// Reentrancy guard. self_render_static draws via the recompiled glyph routines
// (FUN_8001a3e4 / FUN_80019f70), which carry TRACE_ENTRY() and thus re-enter
// this translation hook on the SAME thread. Those routines have coord-like args
// and are themselves registered text PCs, so without this guard the reentry
// would recurse back into self_render_static — infinite recursion plus a
// non-recursive-mutex re-lock (calibrate_slot) that throws std::system_error.
// When set, the hook is a no-op so the glyph routines run normally.
thread_local bool g_xlate_active = false;
struct XlateActiveGuard {
    bool prev;
    XlateActiveGuard() : prev(g_xlate_active) { g_xlate_active = true; }
    ~XlateActiveGuard() { g_xlate_active = prev; }
};

int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Read a JSON string value starting at the opening quote; returns bytes with
// basic escapes (\" \\ \n \t \/). Advances `i` past the closing quote.
std::string json_read_string(const std::string& s, size_t& i) {
    std::string out;
    if (i >= s.size() || s[i] != '"') return out;
    ++i;
    while (i < s.size() && s[i] != '"') {
        char c = s[i++];
        if (c == '\\' && i < s.size()) {
            char e = s[i++];
            switch (e) {
            case 'n': out.push_back('\n'); break;
            case 't': out.push_back('\t'); break;
            case '"': out.push_back('"');  break;
            case '\\':out.push_back('\\'); break;
            case '/': out.push_back('/');  break;
            default:  out.push_back(e);    break;
            }
        } else {
            out.push_back(c);
        }
    }
    if (i < s.size()) ++i; // closing quote
    return out;
}

// Read an optional integer field "<key>": <int> within [start,end). Returns
// def if absent/unparseable. Tolerant (whitespace after the colon).
int json_read_int(const std::string& s, const char* key, size_t start, size_t end, int def) {
    std::string k = std::string("\"") + key + "\"";
    size_t p = s.find(k, start);
    if (p == std::string::npos || p >= end) return def;
    p = s.find(':', p + k.size());
    if (p == std::string::npos || p >= end) return def;
    ++p;
    while (p < end && (s[p] == ' ' || s[p] == '\t')) ++p;
    bool neg = (p < end && s[p] == '-'); if (neg) ++p;
    if (p >= end || s[p] < '0' || s[p] > '9') return def;
    long v = 0; bool any = false;
    while (p < end && s[p] >= '0' && s[p] <= '9') { v = v * 10 + (s[p] - '0'); ++p; any = true; }
    if (!any) return def;
    return neg ? (int)-v : (int)v;
}

// Minimal tolerant scan: for each entry (bounded by the next "src_hex") read
// "target", plus optional integer fields "orig_w" / "fit_w". Schema
// (hot-reloadable): [ { "src_hex":"a5d7a5aa", "target":"Hello",
//                       "orig_w":4, "fit_w":8, "note":"..." }, ... ]
void load_translations_locked() {
    g_xlate.clear();
    const std::string path = pms::app_file("translations.json").string();
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return;
    std::string data;
    { char buf[4096]; size_t r; while ((r = std::fread(buf, 1, sizeof(buf), f)) > 0) data.append(buf, r); }
    std::fclose(f);

    size_t i = 0;
    const std::string SRC = "\"src_hex\"";
    while ((i = data.find(SRC, i)) != std::string::npos) {
        // Bound this entry by the next src_hex so per-entry fields don't bleed.
        size_t entry_end = data.find(SRC, i + SRC.size());
        if (entry_end == std::string::npos) entry_end = data.size();
        i += SRC.size();
        size_t q = data.find('"', i);
        if (q == std::string::npos || q >= entry_end) { i = entry_end; continue; }
        std::string hex = json_read_string(data, q);
        // bytes from hex
        std::vector<uint8_t> bytes;
        for (size_t k = 0; k + 1 < hex.size(); k += 2) {
            int hi = hexval(hex[k]), lo = hexval(hex[k + 1]);
            if (hi < 0 || lo < 0) { bytes.clear(); break; }
            bytes.push_back((uint8_t)((hi << 4) | lo));
        }
        size_t t = data.find("\"target\"", q);
        std::string target;
        if (t != std::string::npos && t < entry_end) {
            t = data.find('"', t + 8);
            if (t != std::string::npos) target = json_read_string(data, t);
        }
        const int orig_w = json_read_int(data, "orig_w", q, entry_end, -1);
        const int fit_w  = json_read_int(data, "fit_w",  q, entry_end, -1);
        i = entry_end;
        if (!bytes.empty() && !target.empty()) {
            g_xlate[pms_fnv1a(bytes.data(), (uint32_t)bytes.size())] =
                XlateVal{ std::move(target), orig_w, fit_w };
        }
    }
    g_xlate_armed.store(!g_xlate.empty(), std::memory_order_relaxed);
    // Seed the chokepoints so general coverage works in normal play (not only
    // during a capture): the universal formatter AND the primary string-draw.
    textpc_add(kStringDrawPC);
    // Seed the universal formatter chokepoint so general coverage (battle/
    // overlay text built via _Printf) works in normal play, not only during a
    // PMS_TEXTPROBE capture (which is what otherwise populates the set).
    textpc_add(kPrintfPC);
    std::fprintf(stderr, "[PMS] translations loaded: %zu entries from %s\n",
                 g_xlate.size(), path.c_str());
    std::fflush(stderr);
}

void maybe_reload_translations() {
    std::lock_guard<std::mutex> lk(g_xlate_mtx);
    const std::filesystem::path p = pms::app_file("translations.json");
    std::error_code ec;
    auto mt = std::filesystem::last_write_time(p, ec);
    if (ec) { // file missing
        if (!g_xlate_ever_loaded) { g_xlate_ever_loaded = true; }
        return;
    }
    if (!g_xlate_ever_loaded || mt != g_xlate_mtime) {
        g_xlate_mtime = mt;
        g_xlate_ever_loaded = true;
        load_translations_locked();
    }
}

bool write_guest_str(uint8_t* rdram, uint32_t va, const std::string& s) {
    if (!pms_diag_rdram_range(va, (uint32_t)s.size() + 1)) return false;
    const uint32_t pa = va & 0x1FFFFFFFu;
    for (size_t k = 0; k < s.size(); ++k) rdram[(pa + k) ^ 3] = (uint8_t)s[k];
    rdram[(pa + s.size()) ^ 3] = 0;
    return true;
}
} // namespace

// The PMS-J glyph drawer (emitted): draws one glyph at (r4=x, r5=y, r6=code)
// and appends to the display list. We call it directly for proportional render.
extern "C" void FUN_8001a3e4(uint8_t* rdram, recomp_context* ctx);
// code -> glyph sheet index (emitted), used to locate a glyph's texture tile.
extern "C" void FUN_80019f70(uint8_t* rdram, recomp_context* ctx);

namespace {
inline uint8_t guest_rb(uint8_t* rdram, uint32_t va) {
    return pms_diag_rdram_range(va, 1) ? rdram[(va & 0x1FFFFFFFu) ^ 3] : 0;
}

// Fallback typographic ratios (percent of slot advance) used before a slot is
// calibrated or if its font can't be measured.
int glyph_width_pct(uint8_t c) {
    static const uint8_t pct[95] = { // 0x20..0x7E
        50,30,42,72,62,86,82,26,40,40,52,66,30,52,30,46,
        62,62,62,62,62,62,62,62,62,62,30,30,66,66,66,55,
        90,70,70,72,76,66,62,78,78,34,50,72,60,92,80,80,
        68,80,72,66,62,78,70,96,72,68,64,40,46,40,55,60,
        34,60,62,54,62,60,40,62,62,28,34,58,28,92,62,62,
        62,62,46,52,42,62,58,88,58,58,52,46,26,46,60
    };
    if (c < 0x20 || c > 0x7E) return 60;
    return pct[c - 0x20];
}

// Per-slot calibrated advances. The game fonts are monospace per slot (wide for
// Latin). For RESIDENT proportional sheets we measure each glyph's real ink
// width; for sheets streamed into a shared buffer (unmeasurable -> 'i' and 'm'
// scan the same width) we fall back to a single tight uniform advance. Both
// avoid the uneven/overlapping look of a fixed guess-table.
struct SlotMetrics {
    std::atomic<bool> ready{false};
    bool proportional = false;
    uint8_t adv[95] = {};   // per-glyph advance px (proportional)
    uint8_t mono = 8;       // uniform advance px (non-proportional)
};
SlotMetrics g_slotm[8];
std::mutex  g_slotm_mtx;

// ink right-edge+1 (px) of glyph ch in slot's tile; 0 if blank, -1 if unreadable
int measure_glyph(uint8_t* rdram, const recomp_context* ctx, uint8_t w, uint8_t h,
                  uint32_t texbase, uint8_t ch) {
    recomp_context g = *ctx; g.r4 = ch;
    FUN_80019f70(rdram, &g);
    const uint32_t tile = texbase + (uint32_t)g.r2 * (uint32_t)w * (uint32_t)h;
    if (!pms_diag_rdram_range(tile, (uint32_t)w * h)) return -1;
    for (int c = (int)w - 1; c >= 0; --c)
        for (uint32_t y = 0; y < h; ++y)
            if (guest_rb(rdram, tile + y * (uint32_t)w + (uint32_t)c) != 0) return c + 1;
    return 0;
}

void calibrate_slot(uint8_t* rdram, const recomp_context* ctx, uint32_t fs,
                    uint8_t slot, uint8_t slot_adv) {
    if (slot >= 8 || g_slotm[slot].ready.load(std::memory_order_acquire)) return;
    std::lock_guard<std::mutex> lk(g_slotm_mtx);
    if (g_slotm[slot].ready.load(std::memory_order_relaxed)) return;
    const uint32_t desc = fs + (uint32_t)slot * 8u;
    const uint8_t h = guest_rb(rdram, desc + 1), w = guest_rb(rdram, desc + 2);
    const uint32_t texbase = pms_diag_read_u32(rdram, desc + 4);
    if (w == 0 || h == 0 || w > 64 || h > 64 ||
        !pms_diag_rdram_range(texbase, (uint32_t)w * h)) {
        return;  // not measurable yet — retry on a later draw
    }
    SlotMetrics& m = g_slotm[slot];
    const int wi = measure_glyph(rdram, ctx, w, h, texbase, 'i');
    const int wm = measure_glyph(rdram, ctx, w, h, texbase, 'm');
    if (wi > 0 && wm > 0 && wi * 10 < wm * 7 && wm <= w) {   // 'i' clearly < 'm'
        m.proportional = true;
        for (int c = 0; c < 95; ++c) {
            int iw = measure_glyph(rdram, ctx, w, h, texbase, (uint8_t)(0x20 + c));
            if (iw <= 0) iw = (c == 0) ? (int)w / 3 : (int)w * glyph_width_pct(0x20 + c) / 100;
            iw += 1;
            m.adv[c] = (uint8_t)(iw < 2 ? 2 : (iw > 63 ? 63 : iw));
        }
    } else {                                                // monospace / ephemeral
        int ma = (int)slot_adv * 62 / 100;
        m.mono = (uint8_t)(ma < 4 ? 4 : ma);
    }
    m.ready.store(true, std::memory_order_release);
}

// ---- Targeted class-2 discovery --------------------------------------------
// Find which routine draws specific known-untranslated strings, and via which
// register/stack slot, to learn the battle-text convention (class-1 = a2/r6
// does not cover them). Capture-only (PMS_TEXTPROBE). Appends to
// xlate_discovery.log: one line per (pc, watch-string, source) first seen.
struct WatchStr { const uint8_t* b; uint16_t n; const char* label; };
const uint8_t kWlTrainer[] = {0xA5,0xC8,0xA5,0xEC,0xA1,0xBC,0xA5,0xCA,0xA1,0xBC}; // トレーナー
const uint8_t kWlBack[]    = {0xA4,0xE2,0xA4,0xC9,0xA4,0xEB};                     // もどる
const uint8_t kWlConfirm[] = {0xA4,0xAB,0xA4,0xAF,0xA4,0xCB,0xA4,0xF3};           // かくにん
const uint8_t kWlGround[]  = {0xA4,0xB8,0xA4,0xE1,0xA4,0xF3};                     // じめん (Ground type)
const uint8_t kWlNormalT[] = {0xA5,0xCE,0xA1,0xBC,0xA5,0xDE,0xA5,0xEB};           // ノーマル (Normal type box)
const uint8_t kWlSeedCat[] = {0xA4,0xBF,0xA4,0xCD,0xA5,0xDD,0xA5,0xB1,0xA5,0xE2,0xA5,0xF3}; // たねポケモン (Seed category)
const WatchStr g_watch[] = {
    {kWlTrainer, 10, "Trainer"}, {kWlBack, 6, "Back"}, {kWlConfirm, 8, "Confirm"},
    {kWlGround, 6, "Ground"}, {kWlNormalT, 8, "NormalT"}, {kWlSeedCat, 12, "SeedCat"},
};
// Broadened source set: args, a couple temps/returns, saved regs, and more stack
// slots — to locate text-draw conventions beyond a0..a3 (e.g. the category label,
// which is neither r6 nor r7).
static const char* kSrcName[] = {
    "a0/r4","a1/r5","a2/r6","a3/r7","t0/r8","t1/r9","v0/r2","v1/r3",
    "s0/r16","s1/r17","s2/r18","s3/r19",
    "sp+10","sp+14","sp+18","sp+1C","sp+20","sp+24","sp+28","sp+2C",
};
constexpr int kNSrc = 20;
std::mutex g_disc_mtx;
uint64_t g_disc_seen[128];
uint32_t g_disc_n = 0;

bool guest_prefix_eq(unsigned char* rdram, uint32_t ptr, const uint8_t* want, uint16_t wlen) {
    if (!pms_diag_rdram_range(ptr, (uint32_t)wlen + 1)) return false;
    const uint32_t pa = ptr & 0x1FFFFFFFu;
    for (uint16_t i = 0; i < wlen; ++i) if (rdram[(pa + i) ^ 3] != want[i]) return false;
    return true;
}

void xlate_discovery(unsigned char* rdram, recomp_context* ctx, uint32_t pc) {
    const uint32_t sp = (uint32_t)ctx->r29;
    uint32_t srcs[kNSrc];
    srcs[0] = (uint32_t)ctx->r4;  srcs[1] = (uint32_t)ctx->r5;
    srcs[2] = (uint32_t)ctx->r6;  srcs[3] = (uint32_t)ctx->r7;
    srcs[4] = (uint32_t)ctx->r8;  srcs[5] = (uint32_t)ctx->r9;
    srcs[6] = (uint32_t)ctx->r2;  srcs[7] = (uint32_t)ctx->r3;
    srcs[8] = (uint32_t)ctx->r16; srcs[9] = (uint32_t)ctx->r17;
    srcs[10] = (uint32_t)ctx->r18; srcs[11] = (uint32_t)ctx->r19;
    srcs[12] = pms_diag_read_u32(rdram, sp + 0x10); srcs[13] = pms_diag_read_u32(rdram, sp + 0x14);
    srcs[14] = pms_diag_read_u32(rdram, sp + 0x18); srcs[15] = pms_diag_read_u32(rdram, sp + 0x1C);
    srcs[16] = pms_diag_read_u32(rdram, sp + 0x20); srcs[17] = pms_diag_read_u32(rdram, sp + 0x24);
    srcs[18] = pms_diag_read_u32(rdram, sp + 0x28); srcs[19] = pms_diag_read_u32(rdram, sp + 0x2C);
    for (uint32_t wi = 0; wi < sizeof(g_watch) / sizeof(g_watch[0]); ++wi) {
        for (int s = 0; s < kNSrc; ++s) {
            if (!guest_prefix_eq(rdram, srcs[s], g_watch[wi].b, g_watch[wi].n)) continue;
            const uint64_t k = ((uint64_t)pc << 8) | ((uint64_t)wi << 4) | (uint64_t)s;
            std::lock_guard<std::mutex> lk(g_disc_mtx);
            bool seen = false;
            for (uint32_t i = 0; i < g_disc_n; ++i) if (g_disc_seen[i] == k) { seen = true; break; }
            if (seen) continue;
            if (g_disc_n < 128) g_disc_seen[g_disc_n++] = k;
            const std::string path = pms::app_file("xlate_discovery.log").string();
            FILE* f = std::fopen(path.c_str(), "a");
            if (f) {
                std::fprintf(f,
                    "%-8s pc=0x%08X via %-6s  a0=0x%08X a1=0x%08X a2=0x%08X a3=0x%08X sp=0x%08X ra=0x%08X\n",
                    g_watch[wi].label, pc, kSrcName[s],
                    (uint32_t)ctx->r4, (uint32_t)ctx->r5, (uint32_t)ctx->r6, (uint32_t)ctx->r7,
                    sp, (uint32_t)ctx->r31);
                std::fclose(f);
            }
        }
    }

    // General LONG-TEXT discovery: log any source pointing at a long (>=24 byte)
    // EUC-JP run — finds the routine + register convention for the Pokedex
    // descriptions / multi-line text that the class-1 (r6+coords) probe misses.
    // Logs the bytes too, so the description src_hex is captured here as well.
    // Once per (pc, source).
    {
        uint8_t lbuf[kSrcMax];
        uint16_t llen = 0;
        for (int s = 0; s < kNSrc; ++s) {
            if (!read_text_arg(rdram, srcs[s], lbuf, &llen)) continue;
            if (llen < 24) continue;
            const uint64_t k = 0x4000000000000000ull | ((uint64_t)pc << 4) | (uint64_t)s;
            std::lock_guard<std::mutex> lk(g_disc_mtx);
            bool seen = false;
            for (uint32_t i = 0; i < g_disc_n; ++i) if (g_disc_seen[i] == k) { seen = true; break; }
            if (seen) continue;
            if (g_disc_n < 128) g_disc_seen[g_disc_n++] = k;
            const std::string path = pms::app_file("xlate_discovery.log").string();
            FILE* f = std::fopen(path.c_str(), "a");
            if (f) {
                std::fprintf(f, "LONGTEXT pc=0x%08X via %-6s len=%u ra=0x%08X hex=",
                             pc, kSrcName[s], (unsigned)llen, (uint32_t)ctx->r31);
                for (uint16_t k2 = 0; k2 < llen; ++k2) std::fprintf(f, "%02x", lbuf[k2]);
                std::fprintf(f, "\n");
                std::fclose(f);
            }
        }
    }

    // PCLOG: for the printf/writeproc/draw PCs in the category path, dump every
    // arg-register string (r4..r7) so we can see exactly how the "%sポケモン"
    // category is streamed (which routine reads the format vs the classifier).
    if (pc == 0x80055CE0u || pc == 0x8001A920u || pc == 0x8001AC3Cu) {
        uint64_t regv[4] = { ctx->r4, ctx->r5, ctx->r6, ctx->r7 };
        const char* rn[4] = { "r4", "r5", "r6", "r7" };
        for (int s = 0; s < 4; ++s) {
            uint8_t b[kSrcMax]; uint16_t bl = 0;
            if (!read_text_arg(rdram, (uint32_t)regv[s], b, &bl)) continue;
            const uint64_t hh = pms_fnv1a(b, bl);
            const uint64_t k = 0x6000000000000000ull ^ ((uint64_t)pc << 4) ^ (uint64_t)s ^ (hh << 8);
            std::lock_guard<std::mutex> lk(g_disc_mtx);
            bool seen = false;
            for (uint32_t i = 0; i < g_disc_n; ++i) if (g_disc_seen[i] == k) { seen = true; break; }
            if (seen) continue;
            if (g_disc_n < 128) g_disc_seen[g_disc_n++] = k;
            const std::string path = pms::app_file("xlate_discovery.log").string();
            FILE* f = std::fopen(path.c_str(), "a");
            if (f) {
                std::fprintf(f, "PCLOG    pc=0x%08X %s=0x%08X len=%u ra=0x%08X hex=",
                             pc, rn[s], (uint32_t)regv[s], (unsigned)bl, (uint32_t)ctx->r31);
                for (uint16_t k2 = 0; k2 < bl; ++k2) std::fprintf(f, "%02x", b[k2]);
                std::fprintf(f, "\n");
                std::fclose(f);
            }
        }
    }
}

// Self-render a static (no-%) English string at (r4,r5) using the game's glyph
// drawer, with PROPORTIONAL spacing and per-line AUTO-FIT to the original JP
// footprint, then suppress the caller's own draw via an empty fmt at r6. Shared
// by kStringDrawPC AND the class-1 siblings (titles/menus/move names) — all use
// x=r4, y=r5, str=r6, so giving them the fit-aware renderer stops English from
// spilling past where the Japanese did. `scratch` is caller-provided guest
// stack space. Returns true on success.
// budget_glyphs: when >=0, condense each English line to budget_glyphs*slot_adv
// px (the "fit_w" override — lets an entry use more/less than the JP footprint).
// When -1, fit to the per-line Japanese footprint computed from `src` (default).
bool self_render_static(unsigned char* rdram, recomp_context* ctx,
                        const uint8_t* src, uint32_t len, const std::string& eng,
                        uint32_t scratch, int budget_glyphs = -1) {
    // Block the translation hook for the recompiled glyph routines we call below.
    XlateActiveGuard _reentry_guard;
    const uint32_t st = pms_diag_read_u32(rdram, 0x800AD200u);
    uint8_t line_h = 16, slot = 0, slot_adv = 12;
    if (pms_diag_rdram_range(st, 0x40)) {
        line_h   = guest_rb(rdram, st + 0x3Au);
        slot     = guest_rb(rdram, st + 0x38u);
        slot_adv = guest_rb(rdram, st + (uint32_t)slot * 8u);
    }
    if (line_h == 0) line_h = 16;
    static const int gap = [] {
        const char* e = std::getenv("PMS_XLATE_GAP");
        int v = e ? std::atoi(e) : -1000;
        return (v >= 0 && v <= 16) ? v : 1;   // extra px between glyphs
    }();

    calibrate_slot(rdram, ctx, st, slot, slot_adv);
    const SlotMetrics& m = g_slotm[slot < 8 ? slot : 0];
    const bool ready = m.ready.load(std::memory_order_acquire);

    auto adv_for = [&](uint8_t c) -> int {
        int a;
        if (ready && m.proportional && c >= 0x20 && c <= 0x7E) a = m.adv[c - 0x20] + gap;
        else if (ready)                                        a = m.mono;
        else a = (int)slot_adv * glyph_width_pct(c) / 100 + gap;
        return a < 3 ? 3 : a;
    };

    // AUTO-FIT: condense each line so the English fits the original Japanese
    // footprint (jp_glyphs * full-width slot_adv) — never spilling past where the
    // JP text did. Kill-switch PMS_XLATE_FIT=0. (Condenses inter-glyph advance;
    // the glyph drawer is fixed-size, so very large ratios pack tightly.)
    static const bool fit_on = [] {
        const char* e = std::getenv("PMS_XLATE_FIT");
        return !(e && e[0] == '0');
    }();
    constexpr int kMaxLines = 24;
    float line_scale[kMaxLines];
    for (int i = 0; i < kMaxLines; ++i) line_scale[i] = 1.0f;
    if (fit_on && slot_adv > 0) {
        int en_w[kMaxLines] = {0}; int en_line = 0;
        for (size_t i = 0; i < eng.size(); ++i) {
            uint8_t c = (uint8_t)eng[i];
            if (c == '\n') { if (en_line < kMaxLines - 1) ++en_line; continue; }
            if (en_line < kMaxLines) en_w[en_line] += adv_for(c);
        }
        const int en_count = en_line + 1;
        if (budget_glyphs >= 0) {
            // Explicit override: condense each English line to the same budget
            // (budget_glyphs * slot_adv px). budget_glyphs==0 disables condensing.
            const int bud = budget_glyphs * (int)slot_adv;
            if (bud > 0) {
                for (int l = 0; l < en_count && l < kMaxLines; ++l)
                    if (en_w[l] > bud && en_w[l] > 0)
                        line_scale[l] = (float)bud / (float)en_w[l];
            }
        } else {
            // Default: fit to the per-line Japanese footprint (match JP width).
            int jp_w[kMaxLines] = {0}; int jp_line = 0;
            for (uint32_t i = 0; i < len; ) {
                uint8_t c = src[i];
                if (c == 0x0A) { if (jp_line < kMaxLines - 1) ++jp_line; ++i; continue; }
                i += (c >= 0x80) ? 2u : 1u;            // 2-byte JIS vs 1-byte glyph
                if (jp_line < kMaxLines) jp_w[jp_line] += (int)slot_adv;
            }
            const int jp_count = jp_line + 1;
            if (en_count == jp_count) {
                for (int l = 0; l < en_count && l < kMaxLines; ++l)
                    if (en_w[l] > jp_w[l] && en_w[l] > 0)
                        line_scale[l] = (float)jp_w[l] / (float)en_w[l];
            } else {
                int et = 0, jt = 0;
                for (int l = 0; l < kMaxLines; ++l) { et += en_w[l]; jt += jp_w[l]; }
                const float s = (et > jt && et > 0) ? (float)jt / (float)et : 1.0f;
                for (int l = 0; l < kMaxLines; ++l) line_scale[l] = s;
            }
        }
    }

    const int x0 = (int)(int16_t)(uint16_t)ctx->r4;
    int x = x0;
    int y = (int)(int16_t)(uint16_t)ctx->r5;
    int line = 0;
    recomp_context g;
    for (size_t i = 0; i < eng.size(); ++i) {
        uint8_t c = (uint8_t)eng[i];
        if (c == '\n') { y += (int)line_h; x = x0; if (line < kMaxLines - 1) ++line; continue; }
        g = *ctx;
        g.r4 = (uint32_t)(int32_t)x;   // glyphs are left-aligned within slot_adv
        g.r5 = (uint32_t)(int32_t)y;
        g.r6 = c;
        FUN_8001a3e4(rdram, &g);
        int adv = adv_for(c);
        const float sc = line_scale[line < kMaxLines ? line : kMaxLines - 1];
        if (sc < 1.0f) { adv = (int)((float)adv * sc + 0.5f); if (adv < 3) adv = 3; }
        x += adv;
    }
    // Suppress the caller's own draw with an empty fmt.
    if (pms_diag_rdram_range(scratch, 1)) {
        rdram[(scratch & 0x1FFFFFFFu) ^ 3] = 0;
        ctx->r6 = scratch;
    }
    return true;
}

// Generalized translation for text-draw routines OTHER than kStringDrawPC.
// The class-1 siblings (menus/titles/move names, e.g. 0x8002C1B4 / 0x82005D90 /
// 0x80028060) share the (x=a0, y=a1, str=a2) convention, so the source string
// is at ctx->r6 AND x/y are at r4/r5 — meaning they can use the SAME fit-aware
// self-renderer as kStringDrawPC (so their English stops spilling past the JP
// footprint). _Printf (no x/y in r4/r5) and %-templates keep the fmt-swap path.
// Runs only for PCs the census flagged as text drawers (lock-free set gate) and
// only translates KNOWN strings (KV-gated), so a stray pointer is harmless.
void xlate_general(unsigned char* rdram, recomp_context* ctx, uint32_t pc) {
    if (!textpc_contains(pc)) return;
    uint8_t src[kSrcMax];
    uint16_t len = 0;
    if (!read_text_arg(rdram, (uint32_t)ctx->r6, src, &len)) return;
    const uint64_t key = pms_fnv1a(src, len);
    XlateVal v;
    {
        std::lock_guard<std::mutex> lk(g_xlate_mtx);
        auto it = g_xlate.find(key);
        if (it == g_xlate.end()) return;
        v = it->second;
    }
    const uint32_t sp = (uint32_t)ctx->r29;
    if (sp < 0x80000C00u) return;                  // need stack headroom
    const uint32_t scratch = (sp - 0x800u) & ~7u;  // per-call -> reentrancy-safe
    if (v.en.size() > 0x300u) return;

    // Coordinate-based text drawers (x=r4, y=r5 look like screen coords) share
    // kStringDrawPC's (x=r4,y=r5,str=r6) convention, so give them the SAME
    // fit-aware self-renderer — this is what stops sibling-drawn menu items
    // (e.g. PC 0x8A100808: View Data / Distribution) from spilling offscreen.
    // Routines where r4/r5 are NOT coords (e.g. _Printf: r4=writeproc) and
    // %-templates keep the fmt-swap path (dynamic args / other use of r6 intact).
    // budget = fit_w override when set, else -1 = match the JP footprint.
    const bool coords = ((uint32_t)ctx->r4 < 0x1000u && (uint32_t)ctx->r5 < 0x1000u);
    if (coords && v.en.find('%') == std::string::npos) {
        if (self_render_static(rdram, ctx, src, len, v.en, scratch, v.fit_w))
            g_xlate_hits.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    // fmt-swap: repoint r6 at the English replacement and let the ORIGINAL
    // routine render it.
    if (!write_guest_str(rdram, scratch, v.en)) return;
    ctx->r6 = scratch;
    g_xlate_hits.fetch_add(1, std::memory_order_relaxed);
}

// Pokedex DESCRIPTION + CATEGORY draw (kDescDrawPC = 0x8001A920): this writeproc
// receives the full string in r7 (multi-line flavor descriptions — the
// _Printf("%s", desc) vararg) OR r4 (the "<X>ポケモン" category label),
// depending on the caller. Neither is r6, which is why the class-1 probe never
// saw them. Check r7 then r4; on a KV hit, fmt-swap that register to the English
// so the original routine renders it. KV-gated, so a non-string register is a
// harmless no-op.
void xlate_desc(unsigned char* rdram, recomp_context* ctx) {
    const uint32_t sp = (uint32_t)ctx->r29;
    if (sp < 0x80000C00u) return;
    const uint32_t scratch = (sp - 0x800u) & ~7u;
    // The writeproc receives the %s segment in r7 (descriptions), r4 (full result),
    // or r5 (the "%sポケモン" category classifier, read from a table) depending on
    // the _Printf call site — check all three. KV-gated, first match wins.
    uint64_t* cand[3] = { &ctx->r7, &ctx->r4, &ctx->r5 };
    for (int i = 0; i < 3; ++i) {
        uint8_t src[kSrcMax];
        uint16_t len = 0;
        if (!read_text_arg(rdram, (uint32_t)*cand[i], src, &len)) continue;
        const uint64_t key = pms_fnv1a(src, len);
        XlateVal v;
        {
            std::lock_guard<std::mutex> lk(g_xlate_mtx);
            auto it = g_xlate.find(key);
            if (it == g_xlate.end()) continue;
            v = it->second;
        }
        if (v.en.size() > 0x300u) continue;
        if (!write_guest_str(rdram, scratch, v.en)) continue;
        *cand[i] = scratch;
        g_xlate_hits.fetch_add(1, std::memory_order_relaxed);
        return;
    }
}
} // namespace

// Forwarded from include/trace.h's TRACE_ENTRY() on every recompiled-fn entry.
// Acts for kStringDrawPC (self-render path) and, via xlate_general, every other
// text-draw routine the census discovered. A cheap no-op for non-text functions.
extern "C" void pkmnstadium_text_xlate(const char* name,
                                       unsigned char* rdram, void* ctxv) {
    // Reentrant call from our own self_render_static glyph drawing — no-op so the
    // recompiled glyph routines execute normally (prevents infinite recursion).
    if (g_xlate_active) return;
    // Reload check (and first load) every 1024 fn entries — not per call.
    if ((g_xlate_calls.fetch_add(1, std::memory_order_relaxed) & 0x3FFu) == 0) {
        maybe_reload_translations();
    }
    if (!g_xlate_armed.load(std::memory_order_relaxed)) return; // lock-free reject
    if (rdram == nullptr || ctxv == nullptr) return;
    recomp_context* ctx = static_cast<recomp_context*>(ctxv);
    const uint32_t pc = parse_fun_pc(name);
    if (textprobe_armed()) xlate_discovery(rdram, ctx, pc); // capture-only
    // Pokedex descriptions draw via 0x8001A920 with the text in a3/r7.
    if (pc == kDescDrawPC) { xlate_desc(rdram, ctx); return; }
    // Every text routine except the primary string-draw printf goes through the
    // generalized fmt-swap path; kStringDrawPC keeps its proportional self-render.
    if (pc != kStringDrawPC) { xlate_general(rdram, ctx, pc); return; }

    const uint32_t fmt = (uint32_t)ctx->r6;
    uint8_t src[kSrcMax];
    uint32_t len = 0;
    const uint32_t pa = fmt & 0x1FFFFFFFu;
    for (; len < kSrcMax; ++len) {
        if (!pms_diag_rdram_range(fmt + len, 1)) break;
        uint8_t c = rdram[(pa + len) ^ 3];
        if (c == 0) break;
        src[len] = c;
    }
    if (len == 0) return;

    const uint64_t key = pms_fnv1a(src, len);
    XlateVal v;
    {
        std::lock_guard<std::mutex> lk(g_xlate_mtx);
        auto it = g_xlate.find(key);
        if (it == g_xlate.end()) return;
        v = it->second;
    }
    // Transient scratch on the caller's guest stack, well below any frame the
    // original + _Printf will use (~0x300). Per-call sp -> reentrancy-safe.
    const uint32_t sp = (uint32_t)ctx->r29;
    if (sp < 0x80000C00u) return; // not enough stack headroom for scratch
    const uint32_t scratch = (sp - 0x800u) & ~7u;
    if (v.en.size() > 0x300u) return;             // sanity cap

    // %-format strings keep the original formatting path (dynamic args intact):
    // swap the fmt pointer and let the original _Printf + render run.
    if (v.en.find('%') != std::string::npos) {
        if (!write_guest_str(rdram, scratch, v.en)) return;
        ctx->r6 = scratch;
        g_xlate_hits.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    // Static strings: self-render with proportional spacing + auto-fit. budget =
    // fit_w override when set, else -1 = match the per-line Japanese footprint.
    if (self_render_static(rdram, ctx, src, len, v.en, scratch, v.fit_w))
        g_xlate_hits.fetch_add(1, std::memory_order_relaxed);
}

// ---- Font-sheet dumper -----------------------------------------------------
//
// The text system's state lives behind a pointer at 0x800AD200 (= US oracle
// D_800AC870). The struct holds an array of 8-byte font-slot descriptors at
// offset 0, then state fields:
//   desc[slot]: [0]=advance, [1]=tile height, [2]=tile width, [4..7]=texture
//               base addr (RDRAM); slots occupy +0x00..+0x37 (7 slots).
//   +0x38 current slot, +0x39 char spacing, +0x3A line height, +0x3B flags.
// The glyph drawer (0x8001A3E4) loads each glyph as an 8-bit (I/IA) tile from
// base + glyph_index*w*h. Dumping the sheets answers the gating translation
// question: does the font contain Latin glyphs (so ASCII <0x80 single-byte
// codes render English directly)? Render the .i8 dumps with tools/
// pms_fontrender.py and look.
extern "C" void pkmnstadium_fontdump(void) {
    const std::string rpt = pms::app_file("fontdump.log").string();
    FILE* f = std::fopen(rpt.c_str(), "w");
    if (!f) { std::fprintf(stderr, "[PMS] fontdump: cannot open %s\n", rpt.c_str()); return; }

    unsigned char* rdram = recomp_runtime_get_rdram();
    if (rdram == nullptr) { std::fprintf(f, "rdram not set\n"); std::fclose(f); return; }

    const uint32_t fs = pms_diag_read_u32(rdram, 0x800AD200u);
    std::fprintf(f, "=== font dump ===\n");
    std::fprintf(f, "  font-state ptr @0x800AD200 -> 0x%08X\n", fs);
    if (!pms_diag_rdram_range(fs, 0x40)) {
        std::fprintf(f, "  (font state not yet initialized / out of range)\n");
        std::fclose(f);
        std::fprintf(stderr, "[PMS] fontdump: font state uninitialized (0x%08X)\n", fs);
        return;
    }
    uint8_t state[0x40];
    pms_diag_copy_bytes(rdram, fs, state, sizeof(state));
    std::fprintf(f, "  cur_slot=%u spacing=%u line_height=%u flags=0x%02X\n\n",
                 state[0x38], state[0x39], state[0x3A], state[0x3B]);

    std::fprintf(f, "  slot  adv  w    h    texbase     dumped\n");
    for (uint32_t slot = 0; slot < 7; ++slot) {
        const uint32_t desc = fs + slot * 8u;
        uint8_t d[8];
        pms_diag_copy_bytes(rdram, desc, d, sizeof(d));
        const uint8_t  adv = d[0];
        const uint8_t  h   = d[1];
        const uint8_t  w   = d[2];
        const uint32_t texbase = pms_diag_read_u32(rdram, desc + 4u);

        if (!pms_diag_rdram_range(texbase, (uint32_t)w * h) || w == 0 || h == 0 ||
            w > 64 || h > 64) {
            std::fprintf(f, "  %u     %3u  %-4u %-4u 0x%08X  (skip)\n",
                         slot, adv, w, h, texbase);
            continue;
        }
        // Dump a generous region (up to 256 glyphs) so the full sheet is covered.
        uint32_t bytes = (uint32_t)w * h * 256u;
        if (bytes > 0x40000u) bytes = 0x40000u;
        if (!pms_diag_rdram_range(texbase, bytes)) {
            bytes = 0x800000u - (texbase & 0x1FFFFFFFu); // clamp to end of RDRAM
            if (bytes > 0x40000u) bytes = 0x40000u;
        }
        char name[32];
        std::snprintf(name, sizeof(name), "font_slot%u.i8", slot);
        const std::string outp = pms::app_file(name).string();
        FILE* of = std::fopen(outp.c_str(), "wb");
        uint32_t wrote = 0;
        if (of) {
            uint8_t chunk[4096];
            for (uint32_t off = 0; off < bytes; off += sizeof(chunk)) {
                uint32_t n = bytes - off;
                if (n > sizeof(chunk)) n = sizeof(chunk);
                pms_diag_copy_bytes(rdram, texbase + off, chunk, n);
                wrote += (uint32_t)std::fwrite(chunk, 1, n, of);
            }
            std::fclose(of);
        }
        std::fprintf(f, "  %u     %3u  %-4u %-4u 0x%08X  %u bytes -> %s (tile %ux%u)\n",
                     slot, adv, w, h, texbase, wrote, name, w, h);
    }
    std::fclose(f);
    std::fprintf(stderr, "[PMS] fontdump written: %s\n", rpt.c_str());
    std::fflush(stderr);
}

// Controlled-abort dump. Writes a small report next to the exe so a lookup-miss
// or bad-message-queue abort leaves a durable record (the SEH crash filter in
// main.cpp handles hard faults; this covers the runtime's own abort paths).
extern "C" void pms_dump_ultra_trace(const char* tag);  // defined below

extern "C" void psr_post_mortem_dump(const char* reason, void* /*fault_info*/) {
    const std::string path = pms::app_file("last_run_report.txt").string();
    FILE* f = std::fopen(path.c_str(), "a");
    if (!f) return;
    std::time_t t = std::time(nullptr);
    char ts[64] = {0};
    std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
    std::fprintf(f, "\n=== post-mortem dump ===\n");
    std::fprintf(f, "  time:   %s\n", ts);
    std::fprintf(f, "  reason: %s\n", reason ? reason : "(null)");
    if (unsigned char* rdram = recomp_runtime_get_rdram()) {
        std::fprintf(f, "  rdram_base: %p\n", (void*)rdram);
    }
    const uint64_t widx = pkmnstadium_trace_write_idx();
    const uint32_t cap  = pkmnstadium_trace_capacity();
    std::fprintf(f, "  trace ring (write_idx=%llu, capacity=%u):\n",
                 (unsigned long long)widx, cap);
    if (widx > 0) {
        uint64_t n = (widx < 64) ? widx : 64;
        for (uint64_t i = 0; i < n; i++) {
            uint64_t slot = (widx - n + i) % cap;
            const char* nm = pkmnstadium_trace_at(slot);
            std::fprintf(f, "    %4llu: %s\n", (unsigned long long)slot, nm ? nm : "(null)");
        }
    }
    std::fclose(f);
    // The always-on libultra-call ring is the primary diagnostic surface —
    // append it (boot ring + recent tail) to the same report.
    pms_dump_ultra_trace(reason ? reason : "post-mortem");
    std::fprintf(stderr, "[PMS] post-mortem dump written: %s (reason=%s)\n",
                 path.c_str(), reason ? reason : "(null)");
    std::fflush(stderr);
}

// Boot-stall watchdog dump (diagnostic; called from main.cpp's vi_callback,
// which keeps ticking even when the game thread stalls before producing
// graphics). Queries the always-on libultra-call ring (recording from process
// start — see librecomp/ultra_trace.hpp). Dumping at two VI-frame marks lets a
// reader distinguish a BLOCKED game thread (recent seq frozen between dumps,
// last event = osRecvMesg) from an ACTIVE spin/poll (seq advancing through a
// small cycle of os calls). This is query-the-ring, not arm-then-capture.
extern "C" void pms_dump_ultra_trace(const char* tag) {
    const std::string path = pms::app_file("last_run_report.txt").string();
    FILE* f = std::fopen(path.c_str(), "a");
    if (f == nullptr) return;

    const uint64_t widx = recomp_ultra_trace_write_idx();
    std::fprintf(f, "\n=== ultra-trace watchdog dump [%s] ===\n", tag ? tag : "(null)");
    std::fprintf(f, "  total os-wrapper calls (recent write_idx): %llu\n",
                 (unsigned long long)widx);

    recomp_ultra_trace_event ev{};
    uint32_t created_threads[96] = {};
    uint32_t created_queues[128] = {};
    size_t created_thread_count = 0;
    size_t created_queue_count = 0;

    // Boot ring: the first os calls from process start (non-evicting).
    const uint32_t bc = recomp_ultra_trace_boot_count();
    std::fprintf(f, "  -- boot ring (%u events from process start) --\n", bc);
    for (uint32_t i = 0; i < bc; i++) {
        if (recomp_ultra_trace_boot_get(i, &ev)) {
            std::fprintf(f,
                         "    b%-4u t=%6llums tid=%2u th=%08X %-22s ra=%08X a0=%08X a1=%08X a2=%08X a3=%08X\n",
                         i, (unsigned long long)ev.ms, ev.thread_id, ev.thread,
                         ev.name, ev.pc, ev.a0, ev.a1, ev.a2, ev.a3);
            if (std::strcmp(ev.name, "osCreateThread_recomp") == 0) {
                append_unique(created_threads, created_thread_count, ev.a0);
            } else if (std::strcmp(ev.name, "osCreateMesgQueue_recomp") == 0) {
                append_unique(created_queues, created_queue_count, ev.a0);
            }
        }
    }

    // Recent ring tail: the last os calls (current spin / last call before block).
    const uint64_t n = (widx < 96) ? widx : 96;
    std::fprintf(f, "  -- recent ring (last %llu events) --\n", (unsigned long long)n);
    for (uint64_t i = (widx > n ? widx - n : 0); i < widx; i++) {
        if (recomp_ultra_trace_get(i, &ev)) {
            std::fprintf(f,
                         "    #%-6llu t=%6llums tid=%2u th=%08X %-22s ra=%08X a0=%08X a1=%08X a2=%08X a3=%08X\n",
                         (unsigned long long)ev.seq, (unsigned long long)ev.ms,
                         ev.thread_id, ev.thread, ev.name,
                         ev.pc, ev.a0, ev.a1, ev.a2, ev.a3);
        }
    }

    {
        constexpr size_t kSpCopyCap = 4096;
        constexpr size_t kSpTailCap = 64;
        static SpTaskEvent sp_events[kSpCopyCap];
        size_t spn = 0;
        uint64_t spnext = 0;
        uint64_t counts[8] = {};
        const SpTaskEvent* last_gfx = nullptr;
        const size_t runtime_size = recomp_sp_task_event_size();

        std::fprintf(f, "  -- SP task ring --\n");
        std::fprintf(f,
                     "    event_size runtime=%llu watchdog=%llu copy_cap=%llu\n",
                     (unsigned long long)runtime_size,
                     (unsigned long long)sizeof(SpTaskEvent),
                     (unsigned long long)kSpCopyCap);
        if (runtime_size != sizeof(SpTaskEvent)) {
            std::fprintf(f, "    size mismatch; skipping SP task ring copy\n");
        } else {
            recomp_sp_task_recent_copy(sp_events, kSpCopyCap, &spn, &spnext);
            for (size_t i = 0; i < spn; i++) {
                const uint32_t t = sp_events[i].task_type;
                if (t < 8) {
                    counts[t]++;
                }
                if (t == M_GFXTASK) {
                    last_gfx = &sp_events[i];
                }
            }
            std::fprintf(f,
                         "    copied %llu most-recent tasks, next_seq=%llu counts: gfx=%llu aud=%llu vid=%llu njpeg=%llu hvqm=%llu unknown=%llu\n",
                         (unsigned long long)spn,
                         (unsigned long long)spnext,
                         (unsigned long long)counts[M_GFXTASK],
                         (unsigned long long)counts[M_AUDTASK],
                         (unsigned long long)counts[M_VIDTASK],
                         (unsigned long long)counts[M_NJPEGTASK],
                         (unsigned long long)counts[6],
                         (unsigned long long)counts[0]);
            if (last_gfx != nullptr) {
                std::fprintf(f, "    last graphics task:\n");
                print_sp_task_event(f, "      ", *last_gfx);
            } else {
                std::fprintf(f, "    no graphics task in copied window\n");
            }

            const size_t tail = spn < kSpTailCap ? spn : kSpTailCap;
            std::fprintf(f, "    tail (%llu tasks):\n", (unsigned long long)tail);
            for (size_t i = spn - tail; i < spn; i++) {
                print_sp_task_event(f, "      ", sp_events[i]);
            }
        }
    }

    {
        constexpr size_t kMesgCopyCap = 8192;
        constexpr size_t kMesgTailCap = 384;
        static MesgEvent mesg_events[kMesgCopyCap];
        struct SummarySlot {
            bool seen;
            MesgEvent ev;
        };
        SummarySlot last_by_thread[256] = {};
        SummarySlot last_block_by_thread[256] = {};

        size_t mn = 0;
        uint64_t mnext = 0;
        const size_t runtime_size = ultramodern_mesg_event_size();
        std::fprintf(f, "  -- message queue ring --\n");
        std::fprintf(f,
                     "    event_size runtime=%llu watchdog=%llu copy_cap=%llu\n",
                     (unsigned long long)runtime_size,
                     (unsigned long long)sizeof(MesgEvent),
                     (unsigned long long)kMesgCopyCap);
        if (runtime_size != sizeof(MesgEvent)) {
            std::fprintf(f, "    size mismatch; skipping message ring copy\n");
        } else {
            ultramodern_mesg_recent_copy(mesg_events, kMesgCopyCap, &mn, &mnext);
            std::fprintf(f, "    copied %llu most-recent events, next_seq=%llu\n",
                         (unsigned long long)mn,
                         (unsigned long long)mnext);

            for (size_t i = 0; i < mn; i++) {
                const uint16_t tid = mesg_events[i].thread_id;
                if (tid < 256) {
                    last_by_thread[tid].seen = true;
                    last_by_thread[tid].ev = mesg_events[i];
                    if (mesg_is_block(mesg_events[i].op)) {
                        last_block_by_thread[tid].seen = true;
                        last_block_by_thread[tid].ev = mesg_events[i];
                    }
                }
            }

            std::fprintf(f, "    last event by thread in copied window:\n");
            for (uint32_t tid = 0; tid < 256; tid++) {
                if (last_by_thread[tid].seen) {
                    print_mesg_event(f, "      ", last_by_thread[tid].ev);
                }
            }

            std::fprintf(f, "    last blocking event by thread in copied window:\n");
            bool any_block = false;
            for (uint32_t tid = 0; tid < 256; tid++) {
                if (last_block_by_thread[tid].seen) {
                    any_block = true;
                    print_mesg_event(f, "      ", last_block_by_thread[tid].ev);
                }
            }
            if (!any_block) {
                std::fprintf(f, "      none\n");
            }

            const size_t tail = mn < kMesgTailCap ? mn : kMesgTailCap;
            std::fprintf(f, "    tail (%llu events):\n", (unsigned long long)tail);
            for (size_t i = mn - tail; i < mn; i++) {
                print_mesg_event(f, "      ", mesg_events[i]);
            }
        }
    }

    {
        constexpr size_t kSchedCopyCap = 65536;
        constexpr size_t kSchedTailCap = 512;
        static SchedEvent sched_events[kSchedCopyCap];
        size_t sn = 0;
        uint64_t snext = 0;
        const size_t runtime_size = ultramodern_sched_event_size();

        std::fprintf(f, "  -- scheduler queue ring --\n");
        std::fprintf(f,
                     "    event_size runtime=%llu watchdog=%llu copy_cap=%llu\n",
                     (unsigned long long)runtime_size,
                     (unsigned long long)sizeof(SchedEvent),
                     (unsigned long long)kSchedCopyCap);
        if (runtime_size != sizeof(SchedEvent)) {
            std::fprintf(f, "    size mismatch; skipping scheduler ring copy\n");
        } else {
            ultramodern_sched_recent_copy(sched_events, kSchedCopyCap, &sn, &snext);
            std::fprintf(f, "    copied %llu most-recent events, next_seq=%llu\n",
                         (unsigned long long)sn,
                         (unsigned long long)snext);
            const size_t first = sn > kSchedTailCap ? sn - kSchedTailCap : 0;
            if (first > 0) {
                std::fprintf(f, "    tail (%llu of %llu events):\n",
                             (unsigned long long)(sn - first),
                             (unsigned long long)sn);
            }
            for (size_t i = first; i < sn; i++) {
                print_sched_event(f, "      ", sched_events[i]);
            }
        }
    }

    {
        std::fprintf(f, "  -- loop checkpoints by guest thread --\n");
        const uint32_t cap = ultramodern_loop_checkpoint_capacity();
        ultramodern_loop_checkpoint_event lev{};
        for (uint32_t tid = 0; tid < cap; tid++) {
            if (ultramodern_loop_checkpoint_get(tid, &lev)) {
                std::fprintf(f,
                             "    tid=%2u th=%08X pc=%08X count=%llu ms=%u\n",
                             lev.thread_id, lev.thread, lev.pc,
                             (unsigned long long)lev.count, lev.ms);
            }
        }
    }

    {
        unsigned char* rdram = recomp_runtime_get_rdram();
        std::fprintf(f, "  -- live scheduler/thread/queue state --\n");
        if (rdram == nullptr) {
            std::fprintf(f, "    rdram unavailable\n");
        } else {
            const uint32_t running_head = ultramodern_running_queue_head();
            std::fprintf(f, "    running_queue head=%08X:", running_head);
            print_thread_list(f, rdram, running_head);

            std::fprintf(f, "    threads (%llu discovered):\n",
                         (unsigned long long)created_thread_count);
            for (size_t i = 0; i < created_thread_count; i++) {
                const uint32_t ptr = created_threads[i];
                if (!is_guest_ptr(ptr)) {
                    continue;
                }
                if (!pms_diag_rdram_range(ptr, sizeof(OSThread))) {
                    std::fprintf(f, "      %08X invalid thread range\n", ptr);
                    continue;
                }
                OSThread* t = diag_to_ptr<OSThread>(rdram, ptr);
                if (t == nullptr) {
                    std::fprintf(f, "      %08X invalid thread range\n", ptr);
                    continue;
                }
                std::fprintf(f,
                             "      %08X id=%d pri=%d state=%s(%u) q=%08X next=%08X sp=%08X ctx=%p\n",
                             ptr, t->id, t->priority, thread_state_name(t->state),
                             t->state, static_cast<uint32_t>(t->queue),
                             static_cast<uint32_t>(t->next),
                             static_cast<uint32_t>(t->sp),
                             static_cast<void*>(t->context));
            }

            std::fprintf(f, "    queues (%llu discovered):\n",
                         (unsigned long long)created_queue_count);
            for (size_t i = 0; i < created_queue_count; i++) {
                const uint32_t ptr = created_queues[i];
                if (!is_guest_ptr(ptr)) {
                    continue;
                }
                if (!pms_diag_rdram_range(ptr, sizeof(OSMesgQueue))) {
                    std::fprintf(f, "      %08X invalid queue range\n", ptr);
                    continue;
                }
                OSMesgQueue* mq = diag_to_ptr<OSMesgQueue>(rdram, ptr);
                if (mq == nullptr) {
                    std::fprintf(f, "      %08X invalid queue range\n", ptr);
                    continue;
                }
                const int valid_count = mq->validCount;
                const int first = mq->first;
                const int msg_count = mq->msgCount;
                const uint32_t msg_base = static_cast<uint32_t>(mq->msg);
                std::fprintf(f,
                             "      %08X recv=%08X send=%08X valid=%d first=%d msgCount=%d msg=%08X\n",
                             ptr, static_cast<uint32_t>(mq->blocked_on_recv),
                             static_cast<uint32_t>(mq->blocked_on_send),
                             valid_count, first, msg_count, msg_base);
                std::fprintf(f, "        recv_wait:");
                print_thread_list(f, rdram, static_cast<uint32_t>(mq->blocked_on_recv));
                std::fprintf(f, "        send_wait:");
                print_thread_list(f, rdram, static_cast<uint32_t>(mq->blocked_on_send));
                if (is_guest_ptr(msg_base) &&
                    valid_count > 0 && valid_count <= msg_count &&
                    msg_count > 0 && msg_count < 0x10000 &&
                    first >= 0 && first < msg_count) {
                    const int to_dump = valid_count < 8 ? valid_count : 8;
                    OSMesg* messages = diag_to_ptr<OSMesg>(rdram, msg_base);
                    std::fprintf(f, "        messages:");
                    for (int j = 0; j < to_dump; j++) {
                        const int idx = (first + j) % msg_count;
                        const uint64_t msg_addr =
                            static_cast<uint64_t>(msg_base) +
                            static_cast<uint64_t>(idx) * sizeof(OSMesg);
                        if (msg_addr > 0xFFFFFFFFull ||
                            !pms_diag_rdram_range(static_cast<uint32_t>(msg_addr), sizeof(OSMesg))) {
                            std::fprintf(f, " [%d]=<invalid range>", idx);
                            break;
                        }
                        if (messages == nullptr) {
                            std::fprintf(f, " [%d]=<invalid base>", idx);
                            break;
                        }
                        std::fprintf(f, " [%d]=%08X", idx,
                                     static_cast<uint32_t>(messages[idx]));
                    }
                    if (valid_count > to_dump) {
                        std::fprintf(f, " ...");
                    }
                    std::fprintf(f, "\n");
                }
            }
        }
    }

    // Function-name ring tail (populated when game.toml trace_mode=true emits
    // TRACE_ENTRY hooks). Shows which recompiled functions are executing RIGHT
    // NOW — the only visibility into threads spinning in call-free guest code.
    {
        const uint64_t fw = pkmnstadium_trace_write_idx();
        const uint32_t fcap = pkmnstadium_trace_capacity();
        // Dump the ENTIRE retained history (the whole ring if it hasn't
        // wrapped, else the last full window). Consecutive repeats are
        // run-length collapsed so a heartbeat loop doesn't bury the
        // interesting one-shot boot entries.
        const uint64_t fn = (fw < fcap) ? fw : fcap;
        std::fprintf(f, "  -- function-name ring (write_idx=%llu, %llu entries, RLE) --\n",
                     (unsigned long long)fw, (unsigned long long)fn);
        TraceNameEvent prev{};
        uint64_t run_start = 0, run_len = 0;
        auto flush_run = [&](void) {
            if (run_len == 0) return;
            if (run_len == 1) {
                std::fprintf(f, "    f%-6llu tid=%2u th=%08X %s\n",
                             (unsigned long long)run_start, prev.thread_id, prev.thread,
                             prev.name ? prev.name : "(null)");
            } else {
                std::fprintf(f, "    f%-6llu tid=%2u th=%08X %s x%llu\n",
                             (unsigned long long)run_start, prev.thread_id, prev.thread,
                             prev.name ? prev.name : "(null)", (unsigned long long)run_len);
            }
        };
        for (uint64_t i = (fw > fn ? fw - fn : 0); i < fw; i++) {
            const TraceNameEvent cur = g_trace_ring[i % fcap];
            if (prev.name != nullptr && cur.name != nullptr &&
                cur.thread == prev.thread &&
                cur.thread_id == prev.thread_id &&
                std::strcmp(cur.name, prev.name) == 0) {
                run_len++;
            } else {
                flush_run();
                prev = cur;
                run_start = i;
                run_len = 1;
            }
        }
        flush_run();
    }

    std::fclose(f);
    std::fprintf(stderr, "[pms-watchdog] ultra-trace dump [%s] widx=%llu boot=%u -> %s\n",
                 tag ? tag : "(null)", (unsigned long long)widx, bc, path.c_str());
    std::fflush(stderr);
}
