#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "librecomp/rsp.hpp"
#include "ultramodern/ultra64.h"

extern uint8_t dmem[];

namespace pms::rsp {

constexpr uint32_t kAudioChunkSize = 0x140;
constexpr uint32_t kAudioCommandsDmemOffset = 0x2B0;

static uint32_t load_dmem_word(uint32_t off) {
    uint32_t out = 0;
    for (int i = 0; i < 4; i++) {
        uint8_t b = dmem[(off + uint32_t(i)) ^ 3];
        reinterpret_cast<uint8_t*>(&out)[i ^ 3] = b;
    }
    return out;
}

static uint8_t load_rdram_byte(uint8_t* rdram, uint32_t vaddr) {
    return rdram[(vaddr & 0x7FFFFFu) ^ 3];
}

static void aspmain_pre_task(uint8_t* rdram,
                             ::RspContext* ctx,
                             const char* ucode_name,
                             uint32_t ucode_addr) {
    (void)ucode_addr;

    // The HLE runs aspMain directly, but Stadium's aspMain expects rspboot
    // to have preloaded the first audio command chunk and seeded DMA state.
    uint32_t data_ptr = load_dmem_word(0xFC0 + 0x30);
    uint32_t data_size = load_dmem_word(0xFC0 + 0x34);
    if (data_ptr == 0 || data_size == 0) {
        return;
    }

    uint32_t chunk = data_size > kAudioChunkSize ? kAudioChunkSize : data_size;
    ::recomp::rsp::dma_rdram_to_dmem_external(
        rdram,
        kAudioCommandsDmemOffset,
        data_ptr,
        chunk - 1);

    ctx->r29 = kAudioCommandsDmemOffset;
    ctx->r30 = static_cast<int32_t>(chunk);
    ctx->r27 = data_size;
    ctx->r28 = data_ptr;
    ctx->dma_mem_address = kAudioCommandsDmemOffset;
    ctx->dma_dram_address = data_ptr + chunk;
    ctx->r3 = chunk - 1;
    ctx->r31 = 0x1144;

    static const bool s_debug = std::getenv("PMS_ASPMAIN_DEBUG") != nullptr;
    if (s_debug) {
        std::fprintf(stderr,
            "[pms_aspmain_hook] %s data=0x%08X size=0x%X chunk=0x%X "
            "seeded r29=0x%X r31=0x%X\n",
            ucode_name ? ucode_name : "?",
            data_ptr, data_size, chunk,
            kAudioCommandsDmemOffset, ctx->r31);

        char ops[256] = {};
        char* p = ops;
        uint32_t n_cmds = data_size > 320 ? 40 : data_size / 8;
        for (uint32_t i = 0; i < n_cmds && (p - ops) < 240; i++) {
            p += std::snprintf(p, ops + sizeof(ops) - p, "%02X ",
                               load_rdram_byte(rdram, data_ptr + i * 8));
        }
        std::fprintf(stderr, "[pms_aspmain_hook] ops: %s\n", ops);
        std::fflush(stderr);
    }
}

void register_pre_task_hooks() {
    ::recomp::rsp::set_pre_task_hook("aspMain", aspmain_pre_task);
}

} // namespace pms::rsp
