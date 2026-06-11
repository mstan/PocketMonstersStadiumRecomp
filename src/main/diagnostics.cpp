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
#include <string>

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
