#include "librecomp/rsp.hpp"
#include "librecomp/rsp_vu_impl.hpp"
RspExitReason njpgdspMain_impl(uint8_t* rdram, RspContext* ctx) {
    uint32_t&                 r1 = ctx->r1;   uint32_t&  r2 = ctx->r2;   uint32_t&  r3 = ctx->r3;   uint32_t&  r4 = ctx->r4;   uint32_t&  r5 = ctx->r5;   uint32_t&  r6 = ctx->r6;   uint32_t&  r7 = ctx->r7;
    uint32_t&  r8 = ctx->r8;  uint32_t&  r9 = ctx->r9;   uint32_t& r10 = ctx->r10; uint32_t& r11 = ctx->r11; uint32_t& r12 = ctx->r12; uint32_t& r13 = ctx->r13; uint32_t& r14 = ctx->r14; uint32_t& r15 = ctx->r15;
    uint32_t& r16 = ctx->r16; uint32_t& r17 = ctx->r17; uint32_t& r18 = ctx->r18; uint32_t& r19 = ctx->r19; uint32_t& r20 = ctx->r20; uint32_t& r21 = ctx->r21; uint32_t& r22 = ctx->r22; uint32_t& r23 = ctx->r23;
    uint32_t& r24 = ctx->r24; uint32_t& r25 = ctx->r25; uint32_t& r26 = ctx->r26; uint32_t& r27 = ctx->r27; uint32_t& r28 = ctx->r28; uint32_t& r29 = ctx->r29; uint32_t& r30 = ctx->r30; uint32_t& r31 = ctx->r31;
    uint32_t& dma_mem_address = ctx->dma_mem_address; uint32_t& dma_dram_address = ctx->dma_dram_address; uint32_t& jump_target = ctx->jump_target;
    const char * debug_file = NULL; int debug_line = 0;
    RSP& rsp = ctx->rsp;
    r1 = 0xFC0;
    ctx->watchdog_count = 0;
    // j           L_1064
    // addi        $1, $zero, 0xFC0
    r1 = RSP_ADD32(0, 0XFC0);
    goto L_1064;
    // addi        $1, $zero, 0xFC0
    r1 = RSP_ADD32(0, 0XFC0);
    // lw          $2, 0x10($1)
    r2 = RSP_MEM_W_LOAD(0X10, r1);
    // addi        $3, $zero, 0xF7F
    r3 = RSP_ADD32(0, 0XF7F);
    // addi        $7, $zero, 0x1080
    r7 = RSP_ADD32(0, 0X1080);
    // mtc0        $7, SP_MEM_ADDR
    SET_DMA_MEM(r7);
    // mtc0        $2, SP_DRAM_ADDR
    SET_DMA_DRAM(r2);
    // mtc0        $3, SP_RD_LEN
    DO_DMA_READ(r3);
L_1020:
    ctx->pc_trail[ctx->pc_trail_idx & 31] = 0x1020;
    ctx->pc_trail_idx++;
    if (++ctx->watchdog_count > 100000000ULL) {
        fprintf(stderr, "[rsp watchdog] hung at PC 0x1020 after %llu transitions; PC trail (oldest..newest):\n", (unsigned long long)ctx->watchdog_count);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (ctx->pc_trail_idx + i) & 31;
            fprintf(stderr, "  [%2u] PC=0x%04X\n", i, ctx->pc_trail[pos]);
        }
        fprintf(stderr, "[rsp watchdog] gprs: r1=%08X r2=%08X r3=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X jt=%08X dma_mem=%08X dma_dram=%08X\n",
            ctx->r1, ctx->r2, ctx->r3, ctx->r25, ctx->r26, ctx->r27, ctx->r28, ctx->r29, ctx->r30, ctx->r31, ctx->jump_target, ctx->dma_mem_address, ctx->dma_dram_address);
        return RspExitReason::Watchdog;
    }
    // mfc0        $4, SP_DMA_BUSY
    r4 = 0;
    // bne         $4, $zero, L_1020
    if (r4 != 0) {
        // nop
    
        goto L_1020;
    }
    // nop

    // jal         0x103C
    r31 = 0x1034;
    // nop

    goto L_103C;
    // nop

L_1034:
    ctx->pc_trail[ctx->pc_trail_idx & 31] = 0x1034;
    ctx->pc_trail_idx++;
    if (++ctx->watchdog_count > 100000000ULL) {
        fprintf(stderr, "[rsp watchdog] hung at PC 0x1034 after %llu transitions; PC trail (oldest..newest):\n", (unsigned long long)ctx->watchdog_count);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (ctx->pc_trail_idx + i) & 31;
            fprintf(stderr, "  [%2u] PC=0x%04X\n", i, ctx->pc_trail[pos]);
        }
        fprintf(stderr, "[rsp watchdog] gprs: r1=%08X r2=%08X r3=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X jt=%08X dma_mem=%08X dma_dram=%08X\n",
            ctx->r1, ctx->r2, ctx->r3, ctx->r25, ctx->r26, ctx->r27, ctx->r28, ctx->r29, ctx->r30, ctx->r31, ctx->jump_target, ctx->dma_mem_address, ctx->dma_dram_address);
        return RspExitReason::Watchdog;
    }
    // jr          $7
    jump_target = r7;
    debug_file = __FILE__; debug_line = __LINE__;
    // mtc0        $zero, SP_SEMAPHORE
    goto do_indirect_jump;
    // mtc0        $zero, SP_SEMAPHORE
L_103C:
    ctx->pc_trail[ctx->pc_trail_idx & 31] = 0x103C;
    ctx->pc_trail_idx++;
    if (++ctx->watchdog_count > 100000000ULL) {
        fprintf(stderr, "[rsp watchdog] hung at PC 0x103C after %llu transitions; PC trail (oldest..newest):\n", (unsigned long long)ctx->watchdog_count);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (ctx->pc_trail_idx + i) & 31;
            fprintf(stderr, "  [%2u] PC=0x%04X\n", i, ctx->pc_trail[pos]);
        }
        fprintf(stderr, "[rsp watchdog] gprs: r1=%08X r2=%08X r3=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X jt=%08X dma_mem=%08X dma_dram=%08X\n",
            ctx->r1, ctx->r2, ctx->r3, ctx->r25, ctx->r26, ctx->r27, ctx->r28, ctx->r29, ctx->r30, ctx->r31, ctx->jump_target, ctx->dma_mem_address, ctx->dma_dram_address);
        return RspExitReason::Watchdog;
    }
    // mfc0        $8, SP_STATUS
    r8 = 0;
    // andi        $8, $8, 0x80
    r8 = r8 & 0X80;
    // bne         $8, $zero, L_1050
    if (r8 != 0) {
        // nop
    
        goto L_1050;
    }
    // nop

    // jr          $ra
    jump_target = r31;
    debug_file = __FILE__; debug_line = __LINE__;
    // mtc0        $zero, SP_SEMAPHORE
    goto do_indirect_jump;
L_1050:
    ctx->pc_trail[ctx->pc_trail_idx & 31] = 0x1050;
    ctx->pc_trail_idx++;
    if (++ctx->watchdog_count > 100000000ULL) {
        fprintf(stderr, "[rsp watchdog] hung at PC 0x1050 after %llu transitions; PC trail (oldest..newest):\n", (unsigned long long)ctx->watchdog_count);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (ctx->pc_trail_idx + i) & 31;
            fprintf(stderr, "  [%2u] PC=0x%04X\n", i, ctx->pc_trail[pos]);
        }
        fprintf(stderr, "[rsp watchdog] gprs: r1=%08X r2=%08X r3=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X jt=%08X dma_mem=%08X dma_dram=%08X\n",
            ctx->r1, ctx->r2, ctx->r3, ctx->r25, ctx->r26, ctx->r27, ctx->r28, ctx->r29, ctx->r30, ctx->r31, ctx->jump_target, ctx->dma_mem_address, ctx->dma_dram_address);
        return RspExitReason::Watchdog;
    }
    // mtc0        $zero, SP_SEMAPHORE
    // ori         $8, $zero, 0x5200
    r8 = 0 | 0X5200;
    // mtc0        $8, SP_STATUS
    // break       0
    return RspExitReason::Broke;
    // nop

L_1064:
    ctx->pc_trail[ctx->pc_trail_idx & 31] = 0x1064;
    ctx->pc_trail_idx++;
    if (++ctx->watchdog_count > 100000000ULL) {
        fprintf(stderr, "[rsp watchdog] hung at PC 0x1064 after %llu transitions; PC trail (oldest..newest):\n", (unsigned long long)ctx->watchdog_count);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (ctx->pc_trail_idx + i) & 31;
            fprintf(stderr, "  [%2u] PC=0x%04X\n", i, ctx->pc_trail[pos]);
        }
        fprintf(stderr, "[rsp watchdog] gprs: r1=%08X r2=%08X r3=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X jt=%08X dma_mem=%08X dma_dram=%08X\n",
            ctx->r1, ctx->r2, ctx->r3, ctx->r25, ctx->r26, ctx->r27, ctx->r28, ctx->r29, ctx->r30, ctx->r31, ctx->jump_target, ctx->dma_mem_address, ctx->dma_dram_address);
        return RspExitReason::Watchdog;
    }
    // lw          $2, 0x4($1)
    r2 = RSP_MEM_W_LOAD(0X4, r1);
    // andi        $2, $2, 0x2
    r2 = r2 & 0X2;
    // beq         $2, $zero, L_108C
    if (r2 == 0) {
        // nop
    
        goto L_108C;
    }
    // nop

    // jal         0x103C
    r31 = 0x107C;
    // nop

    goto L_103C;
    // nop

L_107C:
    ctx->pc_trail[ctx->pc_trail_idx & 31] = 0x107C;
    ctx->pc_trail_idx++;
    if (++ctx->watchdog_count > 100000000ULL) {
        fprintf(stderr, "[rsp watchdog] hung at PC 0x107C after %llu transitions; PC trail (oldest..newest):\n", (unsigned long long)ctx->watchdog_count);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (ctx->pc_trail_idx + i) & 31;
            fprintf(stderr, "  [%2u] PC=0x%04X\n", i, ctx->pc_trail[pos]);
        }
        fprintf(stderr, "[rsp watchdog] gprs: r1=%08X r2=%08X r3=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X jt=%08X dma_mem=%08X dma_dram=%08X\n",
            ctx->r1, ctx->r2, ctx->r3, ctx->r25, ctx->r26, ctx->r27, ctx->r28, ctx->r29, ctx->r30, ctx->r31, ctx->jump_target, ctx->dma_mem_address, ctx->dma_dram_address);
        return RspExitReason::Watchdog;
    }
    // mfc0        $2, DPC_STATUS
    r2 = 0;
    // ori         $21, $zero, 0x2800
    r21 = 0 | 0X2800;
    // lw          $22, 0x4($1)
    r22 = RSP_MEM_W_LOAD(0X4, r1);
    // lw          $23, 0x38($1)
    r23 = RSP_MEM_W_LOAD(0X38, r1);
L_108C:
    ctx->pc_trail[ctx->pc_trail_idx & 31] = 0x108C;
    ctx->pc_trail_idx++;
    if (++ctx->watchdog_count > 100000000ULL) {
        fprintf(stderr, "[rsp watchdog] hung at PC 0x108C after %llu transitions; PC trail (oldest..newest):\n", (unsigned long long)ctx->watchdog_count);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (ctx->pc_trail_idx + i) & 31;
            fprintf(stderr, "  [%2u] PC=0x%04X\n", i, ctx->pc_trail[pos]);
        }
        fprintf(stderr, "[rsp watchdog] gprs: r1=%08X r2=%08X r3=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X jt=%08X dma_mem=%08X dma_dram=%08X\n",
            ctx->r1, ctx->r2, ctx->r3, ctx->r25, ctx->r26, ctx->r27, ctx->r28, ctx->r29, ctx->r30, ctx->r31, ctx->jump_target, ctx->dma_mem_address, ctx->dma_dram_address);
        return RspExitReason::Watchdog;
    }
    // lw          $24, 0x3C($1)
    r24 = RSP_MEM_W_LOAD(0X3C, r1);
    // andi        $22, $22, 0x1
    r22 = r22 & 0X1;
    // beq         $22, $zero, L_10BC
    if (r22 == 0) {
        // mtc0        $21, SP_STATUS
            goto L_10BC;
    }
    // mtc0        $21, SP_STATUS
    // lw          $3, 0x1E0($zero)
    r3 = RSP_MEM_W_LOAD(0X1E0, 0);
    // lw          $25, 0x1E4($zero)
    r25 = RSP_MEM_W_LOAD(0X1E4, 0);
    // lw          $26, 0x1E8($zero)
    r26 = RSP_MEM_W_LOAD(0X1E8, 0);
    // lw          $9, 0x1EC($zero)
    r9 = RSP_MEM_W_LOAD(0X1EC, 0);
    // lw          $10, 0x1F0($zero)
    r10 = RSP_MEM_W_LOAD(0X1F0, 0);
    // lw          $11, 0x1F4($zero)
    r11 = RSP_MEM_W_LOAD(0X1F4, 0);
    // j           L_1138
    // lw          $12, 0x1F8($zero)
    r12 = RSP_MEM_W_LOAD(0X1F8, 0);
    goto L_1138;
    // lw          $12, 0x1F8($zero)
    r12 = RSP_MEM_W_LOAD(0X1F8, 0);
L_10BC:
    ctx->pc_trail[ctx->pc_trail_idx & 31] = 0x10BC;
    ctx->pc_trail_idx++;
    if (++ctx->watchdog_count > 100000000ULL) {
        fprintf(stderr, "[rsp watchdog] hung at PC 0x10BC after %llu transitions; PC trail (oldest..newest):\n", (unsigned long long)ctx->watchdog_count);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (ctx->pc_trail_idx + i) & 31;
            fprintf(stderr, "  [%2u] PC=0x%04X\n", i, ctx->pc_trail[pos]);
        }
        fprintf(stderr, "[rsp watchdog] gprs: r1=%08X r2=%08X r3=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X jt=%08X dma_mem=%08X dma_dram=%08X\n",
            ctx->r1, ctx->r2, ctx->r3, ctx->r25, ctx->r26, ctx->r27, ctx->r28, ctx->r29, ctx->r30, ctx->r31, ctx->jump_target, ctx->dma_mem_address, ctx->dma_dram_address);
        return RspExitReason::Watchdog;
    }
    // lw          $29, 0x34($1)
    r29 = RSP_MEM_W_LOAD(0X34, r1);
    // lw          $27, 0x30($1)
    r27 = RSP_MEM_W_LOAD(0X30, r1);
    // addi        $28, $zero, 0x1E0
    r28 = RSP_ADD32(0, 0X1E0);
    // jal         0x18F0
    r31 = 0x10D0;
    // addi        $29, $29, -0x1
    r29 = RSP_ADD32(r29, -0X1);
    goto L_18F0;
    // addi        $29, $29, -0x1
    r29 = RSP_ADD32(r29, -0X1);
L_10D0:
    ctx->pc_trail[ctx->pc_trail_idx & 31] = 0x10D0;
    ctx->pc_trail_idx++;
    if (++ctx->watchdog_count > 100000000ULL) {
        fprintf(stderr, "[rsp watchdog] hung at PC 0x10D0 after %llu transitions; PC trail (oldest..newest):\n", (unsigned long long)ctx->watchdog_count);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (ctx->pc_trail_idx + i) & 31;
            fprintf(stderr, "  [%2u] PC=0x%04X\n", i, ctx->pc_trail[pos]);
        }
        fprintf(stderr, "[rsp watchdog] gprs: r1=%08X r2=%08X r3=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X jt=%08X dma_mem=%08X dma_dram=%08X\n",
            ctx->r1, ctx->r2, ctx->r3, ctx->r25, ctx->r26, ctx->r27, ctx->r28, ctx->r29, ctx->r30, ctx->r31, ctx->jump_target, ctx->dma_mem_address, ctx->dma_dram_address);
        return RspExitReason::Watchdog;
    }
    // jal         0x1948
    r31 = 0x10D8;
    // addi        $29, $zero, 0x7F
    r29 = RSP_ADD32(0, 0X7F);
    goto L_1948;
    // addi        $29, $zero, 0x7F
    r29 = RSP_ADD32(0, 0X7F);
L_10D8:
    ctx->pc_trail[ctx->pc_trail_idx & 31] = 0x10D8;
    ctx->pc_trail_idx++;
    if (++ctx->watchdog_count > 100000000ULL) {
        fprintf(stderr, "[rsp watchdog] hung at PC 0x10D8 after %llu transitions; PC trail (oldest..newest):\n", (unsigned long long)ctx->watchdog_count);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (ctx->pc_trail_idx + i) & 31;
            fprintf(stderr, "  [%2u] PC=0x%04X\n", i, ctx->pc_trail[pos]);
        }
        fprintf(stderr, "[rsp watchdog] gprs: r1=%08X r2=%08X r3=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X jt=%08X dma_mem=%08X dma_dram=%08X\n",
            ctx->r1, ctx->r2, ctx->r3, ctx->r25, ctx->r26, ctx->r27, ctx->r28, ctx->r29, ctx->r30, ctx->r31, ctx->jump_target, ctx->dma_mem_address, ctx->dma_dram_address);
        return RspExitReason::Watchdog;
    }
    // lw          $27, 0x1EC($zero)
    r27 = RSP_MEM_W_LOAD(0X1EC, 0);
    // jal         0x18F0
    r31 = 0x10E4;
    // addi        $28, $zero, 0x60
    r28 = RSP_ADD32(0, 0X60);
    goto L_18F0;
    // addi        $28, $zero, 0x60
    r28 = RSP_ADD32(0, 0X60);
L_10E4:
    ctx->pc_trail[ctx->pc_trail_idx & 31] = 0x10E4;
    ctx->pc_trail_idx++;
    if (++ctx->watchdog_count > 100000000ULL) {
        fprintf(stderr, "[rsp watchdog] hung at PC 0x10E4 after %llu transitions; PC trail (oldest..newest):\n", (unsigned long long)ctx->watchdog_count);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (ctx->pc_trail_idx + i) & 31;
            fprintf(stderr, "  [%2u] PC=0x%04X\n", i, ctx->pc_trail[pos]);
        }
        fprintf(stderr, "[rsp watchdog] gprs: r1=%08X r2=%08X r3=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X jt=%08X dma_mem=%08X dma_dram=%08X\n",
            ctx->r1, ctx->r2, ctx->r3, ctx->r25, ctx->r26, ctx->r27, ctx->r28, ctx->r29, ctx->r30, ctx->r31, ctx->jump_target, ctx->dma_mem_address, ctx->dma_dram_address);
        return RspExitReason::Watchdog;
    }
    // jal         0x1948
    r31 = 0x10EC;
    // lw          $27, 0x1F0($zero)
    r27 = RSP_MEM_W_LOAD(0X1F0, 0);
    goto L_1948;
    // lw          $27, 0x1F0($zero)
    r27 = RSP_MEM_W_LOAD(0X1F0, 0);
L_10EC:
    ctx->pc_trail[ctx->pc_trail_idx & 31] = 0x10EC;
    ctx->pc_trail_idx++;
    if (++ctx->watchdog_count > 100000000ULL) {
        fprintf(stderr, "[rsp watchdog] hung at PC 0x10EC after %llu transitions; PC trail (oldest..newest):\n", (unsigned long long)ctx->watchdog_count);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (ctx->pc_trail_idx + i) & 31;
            fprintf(stderr, "  [%2u] PC=0x%04X\n", i, ctx->pc_trail[pos]);
        }
        fprintf(stderr, "[rsp watchdog] gprs: r1=%08X r2=%08X r3=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X jt=%08X dma_mem=%08X dma_dram=%08X\n",
            ctx->r1, ctx->r2, ctx->r3, ctx->r25, ctx->r26, ctx->r27, ctx->r28, ctx->r29, ctx->r30, ctx->r31, ctx->jump_target, ctx->dma_mem_address, ctx->dma_dram_address);
        return RspExitReason::Watchdog;
    }
    // jal         0x18F0
    r31 = 0x10F4;
    // addi        $28, $zero, 0xE0
    r28 = RSP_ADD32(0, 0XE0);
    goto L_18F0;
    // addi        $28, $zero, 0xE0
    r28 = RSP_ADD32(0, 0XE0);
L_10F4:
    ctx->pc_trail[ctx->pc_trail_idx & 31] = 0x10F4;
    ctx->pc_trail_idx++;
    if (++ctx->watchdog_count > 100000000ULL) {
        fprintf(stderr, "[rsp watchdog] hung at PC 0x10F4 after %llu transitions; PC trail (oldest..newest):\n", (unsigned long long)ctx->watchdog_count);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (ctx->pc_trail_idx + i) & 31;
            fprintf(stderr, "  [%2u] PC=0x%04X\n", i, ctx->pc_trail[pos]);
        }
        fprintf(stderr, "[rsp watchdog] gprs: r1=%08X r2=%08X r3=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X jt=%08X dma_mem=%08X dma_dram=%08X\n",
            ctx->r1, ctx->r2, ctx->r3, ctx->r25, ctx->r26, ctx->r27, ctx->r28, ctx->r29, ctx->r30, ctx->r31, ctx->jump_target, ctx->dma_mem_address, ctx->dma_dram_address);
        return RspExitReason::Watchdog;
    }
    // jal         0x1948
    r31 = 0x10FC;
    // lw          $27, 0x1F4($zero)
    r27 = RSP_MEM_W_LOAD(0X1F4, 0);
    goto L_1948;
    // lw          $27, 0x1F4($zero)
    r27 = RSP_MEM_W_LOAD(0X1F4, 0);
L_10FC:
    ctx->pc_trail[ctx->pc_trail_idx & 31] = 0x10FC;
    ctx->pc_trail_idx++;
    if (++ctx->watchdog_count > 100000000ULL) {
        fprintf(stderr, "[rsp watchdog] hung at PC 0x10FC after %llu transitions; PC trail (oldest..newest):\n", (unsigned long long)ctx->watchdog_count);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (ctx->pc_trail_idx + i) & 31;
            fprintf(stderr, "  [%2u] PC=0x%04X\n", i, ctx->pc_trail[pos]);
        }
        fprintf(stderr, "[rsp watchdog] gprs: r1=%08X r2=%08X r3=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X jt=%08X dma_mem=%08X dma_dram=%08X\n",
            ctx->r1, ctx->r2, ctx->r3, ctx->r25, ctx->r26, ctx->r27, ctx->r28, ctx->r29, ctx->r30, ctx->r31, ctx->jump_target, ctx->dma_mem_address, ctx->dma_dram_address);
        return RspExitReason::Watchdog;
    }
    // jal         0x18F0
    r31 = 0x1104;
    // addi        $28, $zero, 0x160
    r28 = RSP_ADD32(0, 0X160);
    goto L_18F0;
    // addi        $28, $zero, 0x160
    r28 = RSP_ADD32(0, 0X160);
L_1104:
    ctx->pc_trail[ctx->pc_trail_idx & 31] = 0x1104;
    ctx->pc_trail_idx++;
    if (++ctx->watchdog_count > 100000000ULL) {
        fprintf(stderr, "[rsp watchdog] hung at PC 0x1104 after %llu transitions; PC trail (oldest..newest):\n", (unsigned long long)ctx->watchdog_count);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (ctx->pc_trail_idx + i) & 31;
            fprintf(stderr, "  [%2u] PC=0x%04X\n", i, ctx->pc_trail[pos]);
        }
        fprintf(stderr, "[rsp watchdog] gprs: r1=%08X r2=%08X r3=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X jt=%08X dma_mem=%08X dma_dram=%08X\n",
            ctx->r1, ctx->r2, ctx->r3, ctx->r25, ctx->r26, ctx->r27, ctx->r28, ctx->r29, ctx->r30, ctx->r31, ctx->jump_target, ctx->dma_mem_address, ctx->dma_dram_address);
        return RspExitReason::Watchdog;
    }
    // jal         0x1948
    r31 = 0x110C;
    // lw          $9, 0x1E8($zero)
    r9 = RSP_MEM_W_LOAD(0X1E8, 0);
    goto L_1948;
    // lw          $9, 0x1E8($zero)
    r9 = RSP_MEM_W_LOAD(0X1E8, 0);
L_110C:
    ctx->pc_trail[ctx->pc_trail_idx & 31] = 0x110C;
    ctx->pc_trail_idx++;
    if (++ctx->watchdog_count > 100000000ULL) {
        fprintf(stderr, "[rsp watchdog] hung at PC 0x110C after %llu transitions; PC trail (oldest..newest):\n", (unsigned long long)ctx->watchdog_count);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (ctx->pc_trail_idx + i) & 31;
            fprintf(stderr, "  [%2u] PC=0x%04X\n", i, ctx->pc_trail[pos]);
        }
        fprintf(stderr, "[rsp watchdog] gprs: r1=%08X r2=%08X r3=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X jt=%08X dma_mem=%08X dma_dram=%08X\n",
            ctx->r1, ctx->r2, ctx->r3, ctx->r25, ctx->r26, ctx->r27, ctx->r28, ctx->r29, ctx->r30, ctx->r31, ctx->jump_target, ctx->dma_mem_address, ctx->dma_dram_address);
        return RspExitReason::Watchdog;
    }
    // lw          $25, 0x1E0($zero)
    r25 = RSP_MEM_W_LOAD(0X1E0, 0);
    // bgtz        $9, L_1128
    if (RSP_SIGNED(r9) > 0) {
        // lw          $3, 0x1E4($zero)
        r3 = RSP_MEM_W_LOAD(0X1E4, 0);
        goto L_1128;
    }
    // lw          $3, 0x1E4($zero)
    r3 = RSP_MEM_W_LOAD(0X1E4, 0);
    // addi        $10, $zero, 0x1FF
    r10 = RSP_ADD32(0, 0X1FF);
    // addi        $11, $zero, 0xFF
    r11 = RSP_ADD32(0, 0XFF);
    // j           L_1134
    // addi        $12, $zero, 0x200
    r12 = RSP_ADD32(0, 0X200);
    goto L_1134;
    // addi        $12, $zero, 0x200
    r12 = RSP_ADD32(0, 0X200);
L_1128:
    ctx->pc_trail[ctx->pc_trail_idx & 31] = 0x1128;
    ctx->pc_trail_idx++;
    if (++ctx->watchdog_count > 100000000ULL) {
        fprintf(stderr, "[rsp watchdog] hung at PC 0x1128 after %llu transitions; PC trail (oldest..newest):\n", (unsigned long long)ctx->watchdog_count);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (ctx->pc_trail_idx + i) & 31;
            fprintf(stderr, "  [%2u] PC=0x%04X\n", i, ctx->pc_trail[pos]);
        }
        fprintf(stderr, "[rsp watchdog] gprs: r1=%08X r2=%08X r3=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X jt=%08X dma_mem=%08X dma_dram=%08X\n",
            ctx->r1, ctx->r2, ctx->r3, ctx->r25, ctx->r26, ctx->r27, ctx->r28, ctx->r29, ctx->r30, ctx->r31, ctx->jump_target, ctx->dma_mem_address, ctx->dma_dram_address);
        return RspExitReason::Watchdog;
    }
    // addi        $10, $zero, 0x2FF
    r10 = RSP_ADD32(0, 0X2FF);
    // addi        $11, $zero, 0x1FF
    r11 = RSP_ADD32(0, 0X1FF);
    // addi        $12, $zero, 0x300
    r12 = RSP_ADD32(0, 0X300);
L_1134:
    ctx->pc_trail[ctx->pc_trail_idx & 31] = 0x1134;
    ctx->pc_trail_idx++;
    if (++ctx->watchdog_count > 100000000ULL) {
        fprintf(stderr, "[rsp watchdog] hung at PC 0x1134 after %llu transitions; PC trail (oldest..newest):\n", (unsigned long long)ctx->watchdog_count);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (ctx->pc_trail_idx + i) & 31;
            fprintf(stderr, "  [%2u] PC=0x%04X\n", i, ctx->pc_trail[pos]);
        }
        fprintf(stderr, "[rsp watchdog] gprs: r1=%08X r2=%08X r3=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X jt=%08X dma_mem=%08X dma_dram=%08X\n",
            ctx->r1, ctx->r2, ctx->r3, ctx->r25, ctx->r26, ctx->r27, ctx->r28, ctx->r29, ctx->r30, ctx->r31, ctx->jump_target, ctx->dma_mem_address, ctx->dma_dram_address);
        return RspExitReason::Watchdog;
    }
    // sub         $26, $25, $12
    r26 = RSP_SUB32(r25, r12);
L_1138:
    ctx->pc_trail[ctx->pc_trail_idx & 31] = 0x1138;
    ctx->pc_trail_idx++;
    if (++ctx->watchdog_count > 100000000ULL) {
        fprintf(stderr, "[rsp watchdog] hung at PC 0x1138 after %llu transitions; PC trail (oldest..newest):\n", (unsigned long long)ctx->watchdog_count);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (ctx->pc_trail_idx + i) & 31;
            fprintf(stderr, "  [%2u] PC=0x%04X\n", i, ctx->pc_trail[pos]);
        }
        fprintf(stderr, "[rsp watchdog] gprs: r1=%08X r2=%08X r3=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X jt=%08X dma_mem=%08X dma_dram=%08X\n",
            ctx->r1, ctx->r2, ctx->r3, ctx->r25, ctx->r26, ctx->r27, ctx->r28, ctx->r29, ctx->r30, ctx->r31, ctx->jump_target, ctx->dma_mem_address, ctx->dma_dram_address);
        return RspExitReason::Watchdog;
    }
    // addi        $27, $25, 0x0
    r27 = RSP_ADD32(r25, 0X0);
    // addi        $28, $zero, 0x1E0
    r28 = RSP_ADD32(0, 0X1E0);
    // jal         0x18F0
    r31 = 0x1148;
    // add         $29, $zero, $10
    r29 = RSP_ADD32(0, r10);
    goto L_18F0;
    // add         $29, $zero, $10
    r29 = RSP_ADD32(0, r10);
L_1148:
    ctx->pc_trail[ctx->pc_trail_idx & 31] = 0x1148;
    ctx->pc_trail_idx++;
    if (++ctx->watchdog_count > 100000000ULL) {
        fprintf(stderr, "[rsp watchdog] hung at PC 0x1148 after %llu transitions; PC trail (oldest..newest):\n", (unsigned long long)ctx->watchdog_count);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (ctx->pc_trail_idx + i) & 31;
            fprintf(stderr, "  [%2u] PC=0x%04X\n", i, ctx->pc_trail[pos]);
        }
        fprintf(stderr, "[rsp watchdog] gprs: r1=%08X r2=%08X r3=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X jt=%08X dma_mem=%08X dma_dram=%08X\n",
            ctx->r1, ctx->r2, ctx->r3, ctx->r25, ctx->r26, ctx->r27, ctx->r28, ctx->r29, ctx->r30, ctx->r31, ctx->jump_target, ctx->dma_mem_address, ctx->dma_dram_address);
        return RspExitReason::Watchdog;
    }
    // add         $2, $zero, $3
    r2 = RSP_ADD32(0, r3);
    // lqv         $v2[0], 0x20($zero)
    rsp.LQV<0>(rsp.vpu.r[2], 0, 0X2);
    // lqv         $v3[0], 0x30($zero)
    rsp.LQV<0>(rsp.vpu.r[3], 0, 0X3);
    // vxor        $v0, $v0, $v0
    rsp.VXOR<0>(rsp.vpu.r[0], rsp.vpu.r[0], rsp.vpu.r[0]);
    // jal         0x1948
    r31 = 0x1160;
    // add         $25, $25, $12
    r25 = RSP_ADD32(r25, r12);
    goto L_1948;
    // add         $25, $25, $12
    r25 = RSP_ADD32(r25, r12);
L_1160:
    ctx->pc_trail[ctx->pc_trail_idx & 31] = 0x1160;
    ctx->pc_trail_idx++;
    if (++ctx->watchdog_count > 100000000ULL) {
        fprintf(stderr, "[rsp watchdog] hung at PC 0x1160 after %llu transitions; PC trail (oldest..newest):\n", (unsigned long long)ctx->watchdog_count);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (ctx->pc_trail_idx + i) & 31;
            fprintf(stderr, "  [%2u] PC=0x%04X\n", i, ctx->pc_trail[pos]);
        }
        fprintf(stderr, "[rsp watchdog] gprs: r1=%08X r2=%08X r3=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X jt=%08X dma_mem=%08X dma_dram=%08X\n",
            ctx->r1, ctx->r2, ctx->r3, ctx->r25, ctx->r26, ctx->r27, ctx->r28, ctx->r29, ctx->r30, ctx->r31, ctx->jump_target, ctx->dma_mem_address, ctx->dma_dram_address);
        return RspExitReason::Watchdog;
    }
    // beq         $2, $3, L_1174
    if (r2 == r3) {
        // addi        $27, $zero, 0xC60
        r27 = RSP_ADD32(0, 0XC60);
        goto L_1174;
    }
    // addi        $27, $zero, 0xC60
    r27 = RSP_ADD32(0, 0XC60);
    // addi        $28, $26, 0x0
    r28 = RSP_ADD32(r26, 0X0);
    // jal         0x191C
    r31 = 0x1174;
    // add         $29, $zero, $11
    r29 = RSP_ADD32(0, r11);
    goto L_191C;
    // add         $29, $zero, $11
    r29 = RSP_ADD32(0, r11);
L_1174:
    ctx->pc_trail[ctx->pc_trail_idx & 31] = 0x1174;
    ctx->pc_trail_idx++;
    if (++ctx->watchdog_count > 100000000ULL) {
        fprintf(stderr, "[rsp watchdog] hung at PC 0x1174 after %llu transitions; PC trail (oldest..newest):\n", (unsigned long long)ctx->watchdog_count);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (ctx->pc_trail_idx + i) & 31;
            fprintf(stderr, "  [%2u] PC=0x%04X\n", i, ctx->pc_trail[pos]);
        }
        fprintf(stderr, "[rsp watchdog] gprs: r1=%08X r2=%08X r3=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X jt=%08X dma_mem=%08X dma_dram=%08X\n",
            ctx->r1, ctx->r2, ctx->r3, ctx->r25, ctx->r26, ctx->r27, ctx->r28, ctx->r29, ctx->r30, ctx->r31, ctx->jump_target, ctx->dma_mem_address, ctx->dma_dram_address);
        return RspExitReason::Watchdog;
    }
    // addi        $3, $3, -0x1
    r3 = RSP_ADD32(r3, -0X1);
    // addi        $4, $zero, 0x1E0
    r4 = RSP_ADD32(0, 0X1E0);
    // addi        $5, $zero, 0x4E0
    r5 = RSP_ADD32(0, 0X4E0);
    // addi        $7, $zero, 0x3
    r7 = RSP_ADD32(0, 0X3);
    // addi        $8, $9, 0x2
    r8 = RSP_ADD32(r9, 0X2);
    // addi        $6, $zero, 0x60
    r6 = RSP_ADD32(0, 0X60);
    // lqv         $v1[0], 0x0($zero)
    rsp.LQV<0>(rsp.vpu.r[1], 0, 0X0);
    // lqv         $v16[0], 0x0($4)
    rsp.LQV<0>(rsp.vpu.r[16], r4, 0X0);
    // lqv         $v17[0], 0x10($4)
    rsp.LQV<0>(rsp.vpu.r[17], r4, 0X1);
    // lqv         $v18[0], 0x20($4)
    rsp.LQV<0>(rsp.vpu.r[18], r4, 0X2);
    // lqv         $v19[0], 0x30($4)
    rsp.LQV<0>(rsp.vpu.r[19], r4, 0X3);
    // lqv         $v20[0], 0x40($4)
    rsp.LQV<0>(rsp.vpu.r[20], r4, 0X4);
    // lqv         $v21[0], 0x50($4)
    rsp.LQV<0>(rsp.vpu.r[21], r4, 0X5);
    // lqv         $v22[0], 0x60($4)
    rsp.LQV<0>(rsp.vpu.r[22], r4, 0X6);
    // lqv         $v23[0], 0x70($4)
    rsp.LQV<0>(rsp.vpu.r[23], r4, 0X7);
    // addi        $4, $4, 0x80
    r4 = RSP_ADD32(r4, 0X80);
L_11B4:
    ctx->pc_trail[ctx->pc_trail_idx & 31] = 0x11B4;
    ctx->pc_trail_idx++;
    if (++ctx->watchdog_count > 100000000ULL) {
        fprintf(stderr, "[rsp watchdog] hung at PC 0x11B4 after %llu transitions; PC trail (oldest..newest):\n", (unsigned long long)ctx->watchdog_count);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (ctx->pc_trail_idx + i) & 31;
            fprintf(stderr, "  [%2u] PC=0x%04X\n", i, ctx->pc_trail[pos]);
        }
        fprintf(stderr, "[rsp watchdog] gprs: r1=%08X r2=%08X r3=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X jt=%08X dma_mem=%08X dma_dram=%08X\n",
            ctx->r1, ctx->r2, ctx->r3, ctx->r25, ctx->r26, ctx->r27, ctx->r28, ctx->r29, ctx->r30, ctx->r31, ctx->jump_target, ctx->dma_mem_address, ctx->dma_dram_address);
        return RspExitReason::Watchdog;
    }
    // lqv         $v5[0], 0x0($6)
    rsp.LQV<0>(rsp.vpu.r[5], r6, 0X0);
    // lqv         $v6[0], 0x10($6)
    rsp.LQV<0>(rsp.vpu.r[6], r6, 0X1);
    // lqv         $v7[0], 0x20($6)
    rsp.LQV<0>(rsp.vpu.r[7], r6, 0X2);
    // lqv         $v8[0], 0x30($6)
    rsp.LQV<0>(rsp.vpu.r[8], r6, 0X3);
    // lqv         $v9[0], 0x40($6)
    rsp.LQV<0>(rsp.vpu.r[9], r6, 0X4);
    // lqv         $v10[0], 0x50($6)
    rsp.LQV<0>(rsp.vpu.r[10], r6, 0X5);
    // lqv         $v11[0], 0x60($6)
    rsp.LQV<0>(rsp.vpu.r[11], r6, 0X6);
    // lqv         $v12[0], 0x70($6)
    rsp.LQV<0>(rsp.vpu.r[12], r6, 0X7);
    // addi        $7, $7, -0x1
    r7 = RSP_ADD32(r7, -0X1);
L_11D8:
    ctx->pc_trail[ctx->pc_trail_idx & 31] = 0x11D8;
    ctx->pc_trail_idx++;
    if (++ctx->watchdog_count > 100000000ULL) {
        fprintf(stderr, "[rsp watchdog] hung at PC 0x11D8 after %llu transitions; PC trail (oldest..newest):\n", (unsigned long long)ctx->watchdog_count);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (ctx->pc_trail_idx + i) & 31;
            fprintf(stderr, "  [%2u] PC=0x%04X\n", i, ctx->pc_trail[pos]);
        }
        fprintf(stderr, "[rsp watchdog] gprs: r1=%08X r2=%08X r3=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X jt=%08X dma_mem=%08X dma_dram=%08X\n",
            ctx->r1, ctx->r2, ctx->r3, ctx->r25, ctx->r26, ctx->r27, ctx->r28, ctx->r29, ctx->r30, ctx->r31, ctx->jump_target, ctx->dma_mem_address, ctx->dma_dram_address);
        return RspExitReason::Watchdog;
    }
    // sqv         $v24[0], 0x0($5)
    rsp.SQV<0>(rsp.vpu.r[24], r5, 0X0);
    // vmudh       $v16, $v16, $v5
    rsp.VMUDH<0>(rsp.vpu.r[16], rsp.vpu.r[16], rsp.vpu.r[5]);
    // sqv         $v25[0], 0x10($5)
    rsp.SQV<0>(rsp.vpu.r[25], r5, 0X1);
    // vmudh       $v17, $v17, $v6
    rsp.VMUDH<0>(rsp.vpu.r[17], rsp.vpu.r[17], rsp.vpu.r[6]);
    // sqv         $v26[0], 0x20($5)
    rsp.SQV<0>(rsp.vpu.r[26], r5, 0X2);
    // vmudh       $v18, $v18, $v7
    rsp.VMUDH<0>(rsp.vpu.r[18], rsp.vpu.r[18], rsp.vpu.r[7]);
    // sqv         $v27[0], 0x30($5)
    rsp.SQV<0>(rsp.vpu.r[27], r5, 0X3);
    // vmudh       $v19, $v19, $v8
    rsp.VMUDH<0>(rsp.vpu.r[19], rsp.vpu.r[19], rsp.vpu.r[8]);
    // sqv         $v28[0], 0x40($5)
    rsp.SQV<0>(rsp.vpu.r[28], r5, 0X4);
    // vmudh       $v20, $v20, $v9
    rsp.VMUDH<0>(rsp.vpu.r[20], rsp.vpu.r[20], rsp.vpu.r[9]);
    // sqv         $v29[0], 0x50($5)
    rsp.SQV<0>(rsp.vpu.r[29], r5, 0X5);
    // vmudh       $v21, $v21, $v10
    rsp.VMUDH<0>(rsp.vpu.r[21], rsp.vpu.r[21], rsp.vpu.r[10]);
    // sqv         $v30[0], 0x60($5)
    rsp.SQV<0>(rsp.vpu.r[30], r5, 0X6);
    // vmudh       $v22, $v22, $v11
    rsp.VMUDH<0>(rsp.vpu.r[22], rsp.vpu.r[22], rsp.vpu.r[11]);
    // sqv         $v31[0], 0x70($5)
    rsp.SQV<0>(rsp.vpu.r[31], r5, 0X7);
    // vmudh       $v23, $v23, $v12
    rsp.VMUDH<0>(rsp.vpu.r[23], rsp.vpu.r[23], rsp.vpu.r[12]);
    // vmudn       $v24, $v16, $v1[0]
    rsp.VMUDN<8>(rsp.vpu.r[24], rsp.vpu.r[16], rsp.vpu.r[1]);
    // vmudn       $v25, $v17, $v1[0]
    rsp.VMUDN<8>(rsp.vpu.r[25], rsp.vpu.r[17], rsp.vpu.r[1]);
    // lqv         $v16[0], 0x0($4)
    rsp.LQV<0>(rsp.vpu.r[16], r4, 0X0);
    // vmudn       $v26, $v18, $v1[0]
    rsp.VMUDN<8>(rsp.vpu.r[26], rsp.vpu.r[18], rsp.vpu.r[1]);
    // lqv         $v17[0], 0x10($4)
    rsp.LQV<0>(rsp.vpu.r[17], r4, 0X1);
    // vmudn       $v27, $v19, $v1[0]
    rsp.VMUDN<8>(rsp.vpu.r[27], rsp.vpu.r[19], rsp.vpu.r[1]);
    // lqv         $v18[0], 0x20($4)
    rsp.LQV<0>(rsp.vpu.r[18], r4, 0X2);
    // vmudn       $v28, $v20, $v1[0]
    rsp.VMUDN<8>(rsp.vpu.r[28], rsp.vpu.r[20], rsp.vpu.r[1]);
    // lqv         $v19[0], 0x30($4)
    rsp.LQV<0>(rsp.vpu.r[19], r4, 0X3);
    // vmudn       $v29, $v21, $v1[0]
    rsp.VMUDN<8>(rsp.vpu.r[29], rsp.vpu.r[21], rsp.vpu.r[1]);
    // lqv         $v20[0], 0x40($4)
    rsp.LQV<0>(rsp.vpu.r[20], r4, 0X4);
    // vmudn       $v30, $v22, $v1[0]
    rsp.VMUDN<8>(rsp.vpu.r[30], rsp.vpu.r[22], rsp.vpu.r[1]);
    // lqv         $v21[0], 0x50($4)
    rsp.LQV<0>(rsp.vpu.r[21], r4, 0X5);
    // vmudn       $v31, $v23, $v1[0]
    rsp.VMUDN<8>(rsp.vpu.r[31], rsp.vpu.r[23], rsp.vpu.r[1]);
    // lqv         $v22[0], 0x60($4)
    rsp.LQV<0>(rsp.vpu.r[22], r4, 0X6);
    // lqv         $v23[0], 0x70($4)
    rsp.LQV<0>(rsp.vpu.r[23], r4, 0X7);
    // addi        $4, $4, 0x80
    r4 = RSP_ADD32(r4, 0X80);
    // addi        $8, $8, -0x1
    r8 = RSP_ADD32(r8, -0X1);
    // bgtz        $8, L_11D8
    if (RSP_SIGNED(r8) > 0) {
        // addi        $5, $5, 0x80
        r5 = RSP_ADD32(r5, 0X80);
        goto L_11D8;
    }
    // addi        $5, $5, 0x80
    r5 = RSP_ADD32(r5, 0X80);
    // addi        $8, $zero, 0x1
    r8 = RSP_ADD32(0, 0X1);
    // bgtz        $7, L_11B4
    if (RSP_SIGNED(r7) > 0) {
        // addi        $6, $6, 0x80
        r6 = RSP_ADD32(r6, 0X80);
        goto L_11B4;
    }
    // addi        $6, $6, 0x80
    r6 = RSP_ADD32(r6, 0X80);
    // sqv         $v24[0], 0x0($5)
    rsp.SQV<0>(rsp.vpu.r[24], r5, 0X0);
    // sqv         $v25[0], 0x10($5)
    rsp.SQV<0>(rsp.vpu.r[25], r5, 0X1);
    // sqv         $v26[0], 0x20($5)
    rsp.SQV<0>(rsp.vpu.r[26], r5, 0X2);
    // sqv         $v27[0], 0x30($5)
    rsp.SQV<0>(rsp.vpu.r[27], r5, 0X3);
    // sqv         $v28[0], 0x40($5)
    rsp.SQV<0>(rsp.vpu.r[28], r5, 0X4);
    // sqv         $v29[0], 0x50($5)
    rsp.SQV<0>(rsp.vpu.r[29], r5, 0X5);
    // sqv         $v30[0], 0x60($5)
    rsp.SQV<0>(rsp.vpu.r[30], r5, 0X6);
    // sqv         $v31[0], 0x70($5)
    rsp.SQV<0>(rsp.vpu.r[31], r5, 0X7);
    // addi        $7, $9, 0x4
    r7 = RSP_ADD32(r9, 0X4);
    // addi        $4, $zero, 0x560
    r4 = RSP_ADD32(0, 0X560);
    // addi        $5, $zero, 0x8E0
    r5 = RSP_ADD32(0, 0X8E0);
L_12A0:
    ctx->pc_trail[ctx->pc_trail_idx & 31] = 0x12A0;
    ctx->pc_trail_idx++;
    if (++ctx->watchdog_count > 100000000ULL) {
        fprintf(stderr, "[rsp watchdog] hung at PC 0x12A0 after %llu transitions; PC trail (oldest..newest):\n", (unsigned long long)ctx->watchdog_count);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (ctx->pc_trail_idx + i) & 31;
            fprintf(stderr, "  [%2u] PC=0x%04X\n", i, ctx->pc_trail[pos]);
        }
        fprintf(stderr, "[rsp watchdog] gprs: r1=%08X r2=%08X r3=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X jt=%08X dma_mem=%08X dma_dram=%08X\n",
            ctx->r1, ctx->r2, ctx->r3, ctx->r25, ctx->r26, ctx->r27, ctx->r28, ctx->r29, ctx->r30, ctx->r31, ctx->jump_target, ctx->dma_mem_address, ctx->dma_dram_address);
        return RspExitReason::Watchdog;
    }
    // addi        $7, $7, -0x1
    r7 = RSP_ADD32(r7, -0X1);
    // lqv         $v24[0], 0x0($4)
    rsp.LQV<0>(rsp.vpu.r[24], r4, 0X0);
    // lqv         $v25[0], 0x10($4)
    rsp.LQV<0>(rsp.vpu.r[25], r4, 0X1);
    // lqv         $v26[0], 0x20($4)
    rsp.LQV<0>(rsp.vpu.r[26], r4, 0X2);
    // lqv         $v27[0], 0x30($4)
    rsp.LQV<0>(rsp.vpu.r[27], r4, 0X3);
    // lqv         $v28[0], 0x40($4)
    rsp.LQV<0>(rsp.vpu.r[28], r4, 0X4);
    // lqv         $v29[0], 0x50($4)
    rsp.LQV<0>(rsp.vpu.r[29], r4, 0X5);
    // lqv         $v30[0], 0x60($4)
    rsp.LQV<0>(rsp.vpu.r[30], r4, 0X6);
    // lqv         $v31[0], 0x70($4)
    rsp.LQV<0>(rsp.vpu.r[31], r4, 0X7);
    // ssv         $v24[0], 0x0($5)
    rsp.SSV<0>(rsp.vpu.r[24], r5, 0X0);
    // ssv         $v24[2], 0x10($5)
    rsp.SSV<2>(rsp.vpu.r[24], r5, 0X8);
    // ssv         $v24[4], 0x2($5)
    rsp.SSV<4>(rsp.vpu.r[24], r5, 0X1);
    // ssv         $v24[6], 0x4($5)
    rsp.SSV<6>(rsp.vpu.r[24], r5, 0X2);
    // ssv         $v24[8], 0x12($5)
    rsp.SSV<8>(rsp.vpu.r[24], r5, 0X9);
    // ssv         $v24[10], 0x20($5)
    rsp.SSV<10>(rsp.vpu.r[24], r5, 0X10);
    // ssv         $v24[12], 0x30($5)
    rsp.SSV<12>(rsp.vpu.r[24], r5, 0X18);
    // ssv         $v24[14], 0x22($5)
    rsp.SSV<14>(rsp.vpu.r[24], r5, 0X11);
    // ssv         $v25[0], 0x14($5)
    rsp.SSV<0>(rsp.vpu.r[25], r5, 0XA);
    // ssv         $v25[2], 0x6($5)
    rsp.SSV<2>(rsp.vpu.r[25], r5, 0X3);
    // ssv         $v25[4], 0x8($5)
    rsp.SSV<4>(rsp.vpu.r[25], r5, 0X4);
    // ssv         $v25[6], 0x16($5)
    rsp.SSV<6>(rsp.vpu.r[25], r5, 0XB);
    // ssv         $v25[8], 0x24($5)
    rsp.SSV<8>(rsp.vpu.r[25], r5, 0X12);
    // ssv         $v25[10], 0x32($5)
    rsp.SSV<10>(rsp.vpu.r[25], r5, 0X19);
    // ssv         $v25[12], 0x40($5)
    rsp.SSV<12>(rsp.vpu.r[25], r5, 0X20);
    // ssv         $v25[14], 0x50($5)
    rsp.SSV<14>(rsp.vpu.r[25], r5, 0X28);
    // ssv         $v26[0], 0x42($5)
    rsp.SSV<0>(rsp.vpu.r[26], r5, 0X21);
    // ssv         $v26[2], 0x34($5)
    rsp.SSV<2>(rsp.vpu.r[26], r5, 0X1A);
    // ssv         $v26[4], 0x26($5)
    rsp.SSV<4>(rsp.vpu.r[26], r5, 0X13);
    // ssv         $v26[6], 0x18($5)
    rsp.SSV<6>(rsp.vpu.r[26], r5, 0XC);
    // ssv         $v26[8], 0xA($5)
    rsp.SSV<8>(rsp.vpu.r[26], r5, 0X5);
    // ssv         $v26[10], 0xC($5)
    rsp.SSV<10>(rsp.vpu.r[26], r5, 0X6);
    // ssv         $v26[12], 0x1A($5)
    rsp.SSV<12>(rsp.vpu.r[26], r5, 0XD);
    // ssv         $v26[14], 0x28($5)
    rsp.SSV<14>(rsp.vpu.r[26], r5, 0X14);
    // ssv         $v27[0], 0x36($5)
    rsp.SSV<0>(rsp.vpu.r[27], r5, 0X1B);
    // ssv         $v27[2], 0x44($5)
    rsp.SSV<2>(rsp.vpu.r[27], r5, 0X22);
    // ssv         $v27[4], 0x52($5)
    rsp.SSV<4>(rsp.vpu.r[27], r5, 0X29);
    // ssv         $v27[6], 0x60($5)
    rsp.SSV<6>(rsp.vpu.r[27], r5, 0X30);
    // ssv         $v27[8], 0x70($5)
    rsp.SSV<8>(rsp.vpu.r[27], r5, 0X38);
    // ssv         $v27[10], 0x62($5)
    rsp.SSV<10>(rsp.vpu.r[27], r5, 0X31);
    // ssv         $v27[12], 0x54($5)
    rsp.SSV<12>(rsp.vpu.r[27], r5, 0X2A);
    // ssv         $v27[14], 0x46($5)
    rsp.SSV<14>(rsp.vpu.r[27], r5, 0X23);
    // ssv         $v28[0], 0x38($5)
    rsp.SSV<0>(rsp.vpu.r[28], r5, 0X1C);
    // ssv         $v28[2], 0x2A($5)
    rsp.SSV<2>(rsp.vpu.r[28], r5, 0X15);
    // ssv         $v28[4], 0x1C($5)
    rsp.SSV<4>(rsp.vpu.r[28], r5, 0XE);
    // ssv         $v28[6], 0xE($5)
    rsp.SSV<6>(rsp.vpu.r[28], r5, 0X7);
    // ssv         $v28[8], 0x1E($5)
    rsp.SSV<8>(rsp.vpu.r[28], r5, 0XF);
    // ssv         $v28[10], 0x2C($5)
    rsp.SSV<10>(rsp.vpu.r[28], r5, 0X16);
    // ssv         $v28[12], 0x3A($5)
    rsp.SSV<12>(rsp.vpu.r[28], r5, 0X1D);
    // ssv         $v28[14], 0x48($5)
    rsp.SSV<14>(rsp.vpu.r[28], r5, 0X24);
    // ssv         $v29[0], 0x56($5)
    rsp.SSV<0>(rsp.vpu.r[29], r5, 0X2B);
    // ssv         $v29[2], 0x64($5)
    rsp.SSV<2>(rsp.vpu.r[29], r5, 0X32);
    // ssv         $v29[4], 0x72($5)
    rsp.SSV<4>(rsp.vpu.r[29], r5, 0X39);
    // ssv         $v29[6], 0x74($5)
    rsp.SSV<6>(rsp.vpu.r[29], r5, 0X3A);
    // ssv         $v29[8], 0x66($5)
    rsp.SSV<8>(rsp.vpu.r[29], r5, 0X33);
    // ssv         $v29[10], 0x58($5)
    rsp.SSV<10>(rsp.vpu.r[29], r5, 0X2C);
    // ssv         $v29[12], 0x4A($5)
    rsp.SSV<12>(rsp.vpu.r[29], r5, 0X25);
    // ssv         $v29[14], 0x3C($5)
    rsp.SSV<14>(rsp.vpu.r[29], r5, 0X1E);
    // ssv         $v30[0], 0x2E($5)
    rsp.SSV<0>(rsp.vpu.r[30], r5, 0X17);
    // ssv         $v30[2], 0x3E($5)
    rsp.SSV<2>(rsp.vpu.r[30], r5, 0X1F);
    // ssv         $v30[4], 0x4C($5)
    rsp.SSV<4>(rsp.vpu.r[30], r5, 0X26);
    // ssv         $v30[6], 0x5A($5)
    rsp.SSV<6>(rsp.vpu.r[30], r5, 0X2D);
    // ssv         $v30[8], 0x68($5)
    rsp.SSV<8>(rsp.vpu.r[30], r5, 0X34);
    // ssv         $v30[10], 0x76($5)
    rsp.SSV<10>(rsp.vpu.r[30], r5, 0X3B);
    // ssv         $v30[12], 0x78($5)
    rsp.SSV<12>(rsp.vpu.r[30], r5, 0X3C);
    // ssv         $v30[14], 0x6A($5)
    rsp.SSV<14>(rsp.vpu.r[30], r5, 0X35);
    // ssv         $v31[0], 0x5C($5)
    rsp.SSV<0>(rsp.vpu.r[31], r5, 0X2E);
    // ssv         $v31[2], 0x4E($5)
    rsp.SSV<2>(rsp.vpu.r[31], r5, 0X27);
    // ssv         $v31[4], 0x5E($5)
    rsp.SSV<4>(rsp.vpu.r[31], r5, 0X2F);
    // ssv         $v31[6], 0x6C($5)
    rsp.SSV<6>(rsp.vpu.r[31], r5, 0X36);
    // ssv         $v31[8], 0x7A($5)
    rsp.SSV<8>(rsp.vpu.r[31], r5, 0X3D);
    // ssv         $v31[10], 0x7C($5)
    rsp.SSV<10>(rsp.vpu.r[31], r5, 0X3E);
    // ssv         $v31[12], 0x6E($5)
    rsp.SSV<12>(rsp.vpu.r[31], r5, 0X37);
    // ssv         $v31[14], 0x7E($5)
    rsp.SSV<14>(rsp.vpu.r[31], r5, 0X3F);
    // addi        $4, $4, 0x80
    r4 = RSP_ADD32(r4, 0X80);
    // bgtz        $7, L_12A0
    if (RSP_SIGNED(r7) > 0) {
        // addi        $5, $5, 0x80
        r5 = RSP_ADD32(r5, 0X80);
        goto L_12A0;
    }
    // addi        $5, $5, 0x80
    r5 = RSP_ADD32(r5, 0X80);
    // jal         0x1948
    r31 = 0x13D8;
    // add         $26, $26, $12
    r26 = RSP_ADD32(r26, r12);
    goto L_1948;
    // add         $26, $26, $12
    r26 = RSP_ADD32(r26, r12);
L_13D8:
    ctx->pc_trail[ctx->pc_trail_idx & 31] = 0x13D8;
    ctx->pc_trail_idx++;
    if (++ctx->watchdog_count > 100000000ULL) {
        fprintf(stderr, "[rsp watchdog] hung at PC 0x13D8 after %llu transitions; PC trail (oldest..newest):\n", (unsigned long long)ctx->watchdog_count);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (ctx->pc_trail_idx + i) & 31;
            fprintf(stderr, "  [%2u] PC=0x%04X\n", i, ctx->pc_trail[pos]);
        }
        fprintf(stderr, "[rsp watchdog] gprs: r1=%08X r2=%08X r3=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X jt=%08X dma_mem=%08X dma_dram=%08X\n",
            ctx->r1, ctx->r2, ctx->r3, ctx->r25, ctx->r26, ctx->r27, ctx->r28, ctx->r29, ctx->r30, ctx->r31, ctx->jump_target, ctx->dma_mem_address, ctx->dma_dram_address);
        return RspExitReason::Watchdog;
    }
    // beq         $3, $zero, L_13EC
    if (r3 == 0) {
        // addi        $27, $25, 0x0
        r27 = RSP_ADD32(r25, 0X0);
        goto L_13EC;
    }
    // addi        $27, $25, 0x0
    r27 = RSP_ADD32(r25, 0X0);
    // addi        $28, $zero, 0x1E0
    r28 = RSP_ADD32(0, 0X1E0);
    // jal         0x18F0
    r31 = 0x13EC;
    // add         $29, $zero, $10
    r29 = RSP_ADD32(0, r10);
    goto L_18F0;
    // add         $29, $zero, $10
    r29 = RSP_ADD32(0, r10);
L_13EC:
    ctx->pc_trail[ctx->pc_trail_idx & 31] = 0x13EC;
    ctx->pc_trail_idx++;
    if (++ctx->watchdog_count > 100000000ULL) {
        fprintf(stderr, "[rsp watchdog] hung at PC 0x13EC after %llu transitions; PC trail (oldest..newest):\n", (unsigned long long)ctx->watchdog_count);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (ctx->pc_trail_idx + i) & 31;
            fprintf(stderr, "  [%2u] PC=0x%04X\n", i, ctx->pc_trail[pos]);
        }
        fprintf(stderr, "[rsp watchdog] gprs: r1=%08X r2=%08X r3=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X jt=%08X dma_mem=%08X dma_dram=%08X\n",
            ctx->r1, ctx->r2, ctx->r3, ctx->r25, ctx->r26, ctx->r27, ctx->r28, ctx->r29, ctx->r30, ctx->r31, ctx->jump_target, ctx->dma_mem_address, ctx->dma_dram_address);
        return RspExitReason::Watchdog;
    }
    // addi        $7, $9, 0x4
    r7 = RSP_ADD32(r9, 0X4);
    // addi        $4, $zero, 0x8E0
    r4 = RSP_ADD32(0, 0X8E0);
    // addi        $5, $zero, 0x4E0
    r5 = RSP_ADD32(0, 0X4E0);
    // addi        $21, $zero, 0xF60
    r21 = RSP_ADD32(0, 0XF60);
    // lqv         $v19[0], 0x30($4)
    rsp.LQV<0>(rsp.vpu.r[19], r4, 0X3);
    // lqv         $v21[0], 0x50($4)
    rsp.LQV<0>(rsp.vpu.r[21], r4, 0X5);
    // lqv         $v23[0], 0x70($4)
    rsp.LQV<0>(rsp.vpu.r[23], r4, 0X7);
    // lqv         $v17[0], 0x10($4)
    rsp.LQV<0>(rsp.vpu.r[17], r4, 0X1);
    // lqv         $v16[0], 0x0($4)
    rsp.LQV<0>(rsp.vpu.r[16], r4, 0X0);
    // lqv         $v20[0], 0x40($4)
    rsp.LQV<0>(rsp.vpu.r[20], r4, 0X4);
    // lqv         $v22[0], 0x60($4)
    rsp.LQV<0>(rsp.vpu.r[22], r4, 0X6);
    // lqv         $v18[0], 0x20($4)
    rsp.LQV<0>(rsp.vpu.r[18], r4, 0X2);
    // addi        $4, $4, 0x80
    r4 = RSP_ADD32(r4, 0X80);
L_1420:
    ctx->pc_trail[ctx->pc_trail_idx & 31] = 0x1420;
    ctx->pc_trail_idx++;
    if (++ctx->watchdog_count > 100000000ULL) {
        fprintf(stderr, "[rsp watchdog] hung at PC 0x1420 after %llu transitions; PC trail (oldest..newest):\n", (unsigned long long)ctx->watchdog_count);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (ctx->pc_trail_idx + i) & 31;
            fprintf(stderr, "  [%2u] PC=0x%04X\n", i, ctx->pc_trail[pos]);
        }
        fprintf(stderr, "[rsp watchdog] gprs: r1=%08X r2=%08X r3=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X jt=%08X dma_mem=%08X dma_dram=%08X\n",
            ctx->r1, ctx->r2, ctx->r3, ctx->r25, ctx->r26, ctx->r27, ctx->r28, ctx->r29, ctx->r30, ctx->r31, ctx->jump_target, ctx->dma_mem_address, ctx->dma_dram_address);
        return RspExitReason::Watchdog;
    }
    // addi        $7, $7, -0x1
    r7 = RSP_ADD32(r7, -0X1);
    // vmulf       $v10, $v19, $v2[2]
    rsp.VMULF<10>(rsp.vpu.r[10], rsp.vpu.r[19], rsp.vpu.r[2]);
    // vmacf       $v10, $v21, $v2[4]
    rsp.VMACF<12>(rsp.vpu.r[10], rsp.vpu.r[21], rsp.vpu.r[2]);
    // vmulf       $v11, $v23, $v2[0]
    rsp.VMULF<8>(rsp.vpu.r[11], rsp.vpu.r[23], rsp.vpu.r[2]);
    // sqv         $v24[0], 0x0($5)
    rsp.SQV<0>(rsp.vpu.r[24], r5, 0X0);
    // vmacf       $v11, $v17, $v2[5]
    rsp.VMACF<13>(rsp.vpu.r[11], rsp.vpu.r[17], rsp.vpu.r[2]);
    // sqv         $v31[0], 0x70($5)
    rsp.SQV<0>(rsp.vpu.r[31], r5, 0X7);
    // vmulf       $v8, $v17, $v2[0]
    rsp.VMULF<8>(rsp.vpu.r[8], rsp.vpu.r[17], rsp.vpu.r[2]);
    // sqv         $v27[0], 0x30($5)
    rsp.SQV<0>(rsp.vpu.r[27], r5, 0X3);
    // vmacf       $v8, $v23, $v2[1]
    rsp.VMACF<9>(rsp.vpu.r[8], rsp.vpu.r[23], rsp.vpu.r[2]);
    // sqv         $v28[0], 0x40($5)
    rsp.SQV<0>(rsp.vpu.r[28], r5, 0X4);
    // vmulf       $v9, $v21, $v2[2]
    rsp.VMULF<10>(rsp.vpu.r[9], rsp.vpu.r[21], rsp.vpu.r[2]);
    // sqv         $v25[0], 0x10($5)
    rsp.SQV<0>(rsp.vpu.r[25], r5, 0X1);
    // vmacf       $v9, $v19, $v2[3]
    rsp.VMACF<11>(rsp.vpu.r[9], rsp.vpu.r[19], rsp.vpu.r[2]);
    // sqv         $v30[0], 0x60($5)
    rsp.SQV<0>(rsp.vpu.r[30], r5, 0X6);
    // vmulf       $v6, $v16, $v3[0]
    rsp.VMULF<8>(rsp.vpu.r[6], rsp.vpu.r[16], rsp.vpu.r[3]);
    // sqv         $v26[0], 0x20($5)
    rsp.SQV<0>(rsp.vpu.r[26], r5, 0X2);
    // vmacf       $v6, $v20, $v3[1]
    rsp.VMACF<9>(rsp.vpu.r[6], rsp.vpu.r[20], rsp.vpu.r[3]);
    // sqv         $v29[0], 0x50($5)
    rsp.SQV<0>(rsp.vpu.r[29], r5, 0X5);
    // vsub        $v5, $v11, $v10
    rsp.VSUB<0>(rsp.vpu.r[5], rsp.vpu.r[11], rsp.vpu.r[10]);
    // vsub        $v4, $v8, $v9
    rsp.VSUB<0>(rsp.vpu.r[4], rsp.vpu.r[8], rsp.vpu.r[9]);
    // vadd        $v12, $v8, $v9
    rsp.VADD<0>(rsp.vpu.r[12], rsp.vpu.r[8], rsp.vpu.r[9]);
    // vadd        $v15, $v11, $v10
    rsp.VADD<0>(rsp.vpu.r[15], rsp.vpu.r[11], rsp.vpu.r[10]);
    // vmulf       $v13, $v5, $v3[0]
    rsp.VMULF<8>(rsp.vpu.r[13], rsp.vpu.r[5], rsp.vpu.r[3]);
    // vmacf       $v13, $v4, $v3[1]
    rsp.VMACF<9>(rsp.vpu.r[13], rsp.vpu.r[4], rsp.vpu.r[3]);
    // vmulf       $v14, $v5, $v3[0]
    rsp.VMULF<8>(rsp.vpu.r[14], rsp.vpu.r[5], rsp.vpu.r[3]);
    // vmacf       $v14, $v4, $v3[0]
    rsp.VMACF<8>(rsp.vpu.r[14], rsp.vpu.r[4], rsp.vpu.r[3]);
    // vmulf       $v4, $v16, $v3[0]
    rsp.VMULF<8>(rsp.vpu.r[4], rsp.vpu.r[16], rsp.vpu.r[3]);
    // vmacf       $v4, $v20, $v3[0]
    rsp.VMACF<8>(rsp.vpu.r[4], rsp.vpu.r[20], rsp.vpu.r[3]);
    // vmulf       $v5, $v22, $v3[2]
    rsp.VMULF<10>(rsp.vpu.r[5], rsp.vpu.r[22], rsp.vpu.r[3]);
    // vmacf       $v5, $v18, $v3[4]
    rsp.VMACF<12>(rsp.vpu.r[5], rsp.vpu.r[18], rsp.vpu.r[3]);
    // vmulf       $v7, $v18, $v3[2]
    rsp.VMULF<10>(rsp.vpu.r[7], rsp.vpu.r[18], rsp.vpu.r[3]);
    // vmacf       $v7, $v22, $v3[3]
    rsp.VMACF<11>(rsp.vpu.r[7], rsp.vpu.r[22], rsp.vpu.r[3]);
    // nop

    // vadd        $v8, $v4, $v5
    rsp.VADD<0>(rsp.vpu.r[8], rsp.vpu.r[4], rsp.vpu.r[5]);
    // vsub        $v11, $v4, $v5
    rsp.VSUB<0>(rsp.vpu.r[11], rsp.vpu.r[4], rsp.vpu.r[5]);
    // vadd        $v9, $v6, $v7
    rsp.VADD<0>(rsp.vpu.r[9], rsp.vpu.r[6], rsp.vpu.r[7]);
    // vsub        $v10, $v6, $v7
    rsp.VSUB<0>(rsp.vpu.r[10], rsp.vpu.r[6], rsp.vpu.r[7]);
    // vadd        $v16, $v8, $v15
    rsp.VADD<0>(rsp.vpu.r[16], rsp.vpu.r[8], rsp.vpu.r[15]);
    // vsub        $v23, $v8, $v15
    rsp.VSUB<0>(rsp.vpu.r[23], rsp.vpu.r[8], rsp.vpu.r[15]);
    // vadd        $v19, $v11, $v12
    rsp.VADD<0>(rsp.vpu.r[19], rsp.vpu.r[11], rsp.vpu.r[12]);
    // vsub        $v20, $v11, $v12
    rsp.VSUB<0>(rsp.vpu.r[20], rsp.vpu.r[11], rsp.vpu.r[12]);
    // vadd        $v17, $v9, $v14
    rsp.VADD<0>(rsp.vpu.r[17], rsp.vpu.r[9], rsp.vpu.r[14]);
    // vsub        $v22, $v9, $v14
    rsp.VSUB<0>(rsp.vpu.r[22], rsp.vpu.r[9], rsp.vpu.r[14]);
    // vadd        $v18, $v10, $v13
    rsp.VADD<0>(rsp.vpu.r[18], rsp.vpu.r[10], rsp.vpu.r[13]);
    // vsub        $v21, $v10, $v13
    rsp.VSUB<0>(rsp.vpu.r[21], rsp.vpu.r[10], rsp.vpu.r[13]);
    // stv         $v16[0], 0x0($21)
    rsp.STV<0>(16, r21, 0X0);
    // stv         $v16[2], 0x10($21)
    rsp.STV<2>(16, r21, 0X1);
    // stv         $v16[4], 0x20($21)
    rsp.STV<4>(16, r21, 0X2);
    // stv         $v16[6], 0x30($21)
    rsp.STV<6>(16, r21, 0X3);
    // stv         $v16[8], 0x40($21)
    rsp.STV<8>(16, r21, 0X4);
    // stv         $v16[10], 0x50($21)
    rsp.STV<10>(16, r21, 0X5);
    // stv         $v16[12], 0x60($21)
    rsp.STV<12>(16, r21, 0X6);
    // stv         $v16[14], 0x70($21)
    rsp.STV<14>(16, r21, 0X7);
    // ltv         $v24[0], 0x0($21)
    rsp.LTV<0>(24, r21, 0X0);
    // ltv         $v24[14], 0x10($21)
    rsp.LTV<14>(24, r21, 0X1);
    // ltv         $v24[12], 0x20($21)
    rsp.LTV<12>(24, r21, 0X2);
    // ltv         $v24[10], 0x30($21)
    rsp.LTV<10>(24, r21, 0X3);
    // ltv         $v24[8], 0x40($21)
    rsp.LTV<8>(24, r21, 0X4);
    // ltv         $v24[6], 0x50($21)
    rsp.LTV<6>(24, r21, 0X5);
    // ltv         $v24[4], 0x60($21)
    rsp.LTV<4>(24, r21, 0X6);
    // ltv         $v24[2], 0x70($21)
    rsp.LTV<2>(24, r21, 0X7);
    // vmulf       $v10, $v27, $v2[2]
    rsp.VMULF<10>(rsp.vpu.r[10], rsp.vpu.r[27], rsp.vpu.r[2]);
    // vmacf       $v10, $v29, $v2[4]
    rsp.VMACF<12>(rsp.vpu.r[10], rsp.vpu.r[29], rsp.vpu.r[2]);
    // vmulf       $v11, $v31, $v2[0]
    rsp.VMULF<8>(rsp.vpu.r[11], rsp.vpu.r[31], rsp.vpu.r[2]);
    // vmacf       $v11, $v25, $v2[5]
    rsp.VMACF<13>(rsp.vpu.r[11], rsp.vpu.r[25], rsp.vpu.r[2]);
    // vmulf       $v8, $v25, $v2[0]
    rsp.VMULF<8>(rsp.vpu.r[8], rsp.vpu.r[25], rsp.vpu.r[2]);
    // vmacf       $v8, $v31, $v2[1]
    rsp.VMACF<9>(rsp.vpu.r[8], rsp.vpu.r[31], rsp.vpu.r[2]);
    // vmulf       $v9, $v29, $v2[2]
    rsp.VMULF<10>(rsp.vpu.r[9], rsp.vpu.r[29], rsp.vpu.r[2]);
    // vmacf       $v9, $v27, $v2[3]
    rsp.VMACF<11>(rsp.vpu.r[9], rsp.vpu.r[27], rsp.vpu.r[2]);
    // vmulf       $v6, $v24, $v3[0]
    rsp.VMULF<8>(rsp.vpu.r[6], rsp.vpu.r[24], rsp.vpu.r[3]);
    // vmacf       $v6, $v28, $v3[1]
    rsp.VMACF<9>(rsp.vpu.r[6], rsp.vpu.r[28], rsp.vpu.r[3]);
    // vsub        $v5, $v11, $v10
    rsp.VSUB<0>(rsp.vpu.r[5], rsp.vpu.r[11], rsp.vpu.r[10]);
    // vsub        $v4, $v8, $v9
    rsp.VSUB<0>(rsp.vpu.r[4], rsp.vpu.r[8], rsp.vpu.r[9]);
    // vadd        $v12, $v8, $v9
    rsp.VADD<0>(rsp.vpu.r[12], rsp.vpu.r[8], rsp.vpu.r[9]);
    // vadd        $v15, $v11, $v10
    rsp.VADD<0>(rsp.vpu.r[15], rsp.vpu.r[11], rsp.vpu.r[10]);
    // vmulf       $v13, $v5, $v3[0]
    rsp.VMULF<8>(rsp.vpu.r[13], rsp.vpu.r[5], rsp.vpu.r[3]);
    // vmacf       $v13, $v4, $v3[1]
    rsp.VMACF<9>(rsp.vpu.r[13], rsp.vpu.r[4], rsp.vpu.r[3]);
    // vmulf       $v14, $v5, $v3[0]
    rsp.VMULF<8>(rsp.vpu.r[14], rsp.vpu.r[5], rsp.vpu.r[3]);
    // lqv         $v19[0], 0x30($4)
    rsp.LQV<0>(rsp.vpu.r[19], r4, 0X3);
    // vmacf       $v14, $v4, $v3[0]
    rsp.VMACF<8>(rsp.vpu.r[14], rsp.vpu.r[4], rsp.vpu.r[3]);
    // lqv         $v21[0], 0x50($4)
    rsp.LQV<0>(rsp.vpu.r[21], r4, 0X5);
    // vmulf       $v4, $v24, $v3[0]
    rsp.VMULF<8>(rsp.vpu.r[4], rsp.vpu.r[24], rsp.vpu.r[3]);
    // lqv         $v23[0], 0x70($4)
    rsp.LQV<0>(rsp.vpu.r[23], r4, 0X7);
    // vmacf       $v4, $v28, $v3[0]
    rsp.VMACF<8>(rsp.vpu.r[4], rsp.vpu.r[28], rsp.vpu.r[3]);
    // lqv         $v17[0], 0x10($4)
    rsp.LQV<0>(rsp.vpu.r[17], r4, 0X1);
    // vmulf       $v5, $v30, $v3[2]
    rsp.VMULF<10>(rsp.vpu.r[5], rsp.vpu.r[30], rsp.vpu.r[3]);
    // lqv         $v16[0], 0x0($4)
    rsp.LQV<0>(rsp.vpu.r[16], r4, 0X0);
    // vmacf       $v5, $v26, $v3[4]
    rsp.VMACF<12>(rsp.vpu.r[5], rsp.vpu.r[26], rsp.vpu.r[3]);
    // lqv         $v20[0], 0x40($4)
    rsp.LQV<0>(rsp.vpu.r[20], r4, 0X4);
    // vmulf       $v7, $v26, $v3[2]
    rsp.VMULF<10>(rsp.vpu.r[7], rsp.vpu.r[26], rsp.vpu.r[3]);
    // lqv         $v22[0], 0x60($4)
    rsp.LQV<0>(rsp.vpu.r[22], r4, 0X6);
    // vmacf       $v7, $v30, $v3[3]
    rsp.VMACF<11>(rsp.vpu.r[7], rsp.vpu.r[30], rsp.vpu.r[3]);
    // lqv         $v18[0], 0x20($4)
    rsp.LQV<0>(rsp.vpu.r[18], r4, 0X2);
    // nop

    // vadd        $v8, $v4, $v5
    rsp.VADD<0>(rsp.vpu.r[8], rsp.vpu.r[4], rsp.vpu.r[5]);
    // vsub        $v11, $v4, $v5
    rsp.VSUB<0>(rsp.vpu.r[11], rsp.vpu.r[4], rsp.vpu.r[5]);
    // vadd        $v9, $v6, $v7
    rsp.VADD<0>(rsp.vpu.r[9], rsp.vpu.r[6], rsp.vpu.r[7]);
    // vsub        $v10, $v6, $v7
    rsp.VSUB<0>(rsp.vpu.r[10], rsp.vpu.r[6], rsp.vpu.r[7]);
    // vmulf       $v24, $v8, $v1[1]
    rsp.VMULF<9>(rsp.vpu.r[24], rsp.vpu.r[8], rsp.vpu.r[1]);
    // vmacf       $v24, $v15, $v1[1]
    rsp.VMACF<9>(rsp.vpu.r[24], rsp.vpu.r[15], rsp.vpu.r[1]);
    // vmacf       $v31, $v15, $v1[2]
    rsp.VMACF<10>(rsp.vpu.r[31], rsp.vpu.r[15], rsp.vpu.r[1]);
    // vmulf       $v27, $v11, $v1[1]
    rsp.VMULF<9>(rsp.vpu.r[27], rsp.vpu.r[11], rsp.vpu.r[1]);
    // vmacf       $v27, $v12, $v1[1]
    rsp.VMACF<9>(rsp.vpu.r[27], rsp.vpu.r[12], rsp.vpu.r[1]);
    // vmacf       $v28, $v12, $v1[2]
    rsp.VMACF<10>(rsp.vpu.r[28], rsp.vpu.r[12], rsp.vpu.r[1]);
    // vmulf       $v25, $v9, $v1[1]
    rsp.VMULF<9>(rsp.vpu.r[25], rsp.vpu.r[9], rsp.vpu.r[1]);
    // vmacf       $v25, $v14, $v1[1]
    rsp.VMACF<9>(rsp.vpu.r[25], rsp.vpu.r[14], rsp.vpu.r[1]);
    // vmacf       $v30, $v14, $v1[2]
    rsp.VMACF<10>(rsp.vpu.r[30], rsp.vpu.r[14], rsp.vpu.r[1]);
    // vmulf       $v26, $v10, $v1[1]
    rsp.VMULF<9>(rsp.vpu.r[26], rsp.vpu.r[10], rsp.vpu.r[1]);
    // vmacf       $v26, $v13, $v1[1]
    rsp.VMACF<9>(rsp.vpu.r[26], rsp.vpu.r[13], rsp.vpu.r[1]);
    // vmacf       $v29, $v13, $v1[2]
    rsp.VMACF<10>(rsp.vpu.r[29], rsp.vpu.r[13], rsp.vpu.r[1]);
    // addi        $4, $4, 0x80
    r4 = RSP_ADD32(r4, 0X80);
    // bgtz        $7, L_1420
    if (RSP_SIGNED(r7) > 0) {
        // addi        $5, $5, 0x80
        r5 = RSP_ADD32(r5, 0X80);
        goto L_1420;
    }
    // addi        $5, $5, 0x80
    r5 = RSP_ADD32(r5, 0X80);
    // sqv         $v24[0], 0x0($5)
    rsp.SQV<0>(rsp.vpu.r[24], r5, 0X0);
    // sqv         $v31[0], 0x70($5)
    rsp.SQV<0>(rsp.vpu.r[31], r5, 0X7);
    // sqv         $v27[0], 0x30($5)
    rsp.SQV<0>(rsp.vpu.r[27], r5, 0X3);
    // sqv         $v28[0], 0x40($5)
    rsp.SQV<0>(rsp.vpu.r[28], r5, 0X4);
    // sqv         $v25[0], 0x10($5)
    rsp.SQV<0>(rsp.vpu.r[25], r5, 0X1);
    // sqv         $v30[0], 0x60($5)
    rsp.SQV<0>(rsp.vpu.r[30], r5, 0X6);
    // sqv         $v26[0], 0x20($5)
    rsp.SQV<0>(rsp.vpu.r[26], r5, 0X2);
    // sqv         $v29[0], 0x50($5)
    rsp.SQV<0>(rsp.vpu.r[29], r5, 0X5);
    // bgtz        $9, L_1700
    if (RSP_SIGNED(r9) > 0) {
        // nop
    
        goto L_1700;
    }
    // nop

    // addi        $4, $zero, 0x560
    r4 = RSP_ADD32(0, 0X560);
    // addi        $20, $4, 0x100
    r20 = RSP_ADD32(r4, 0X100);
    // addi        $5, $zero, 0xC40
    r5 = RSP_ADD32(0, 0XC40);
    // addi        $7, $zero, 0x8
    r7 = RSP_ADD32(0, 0X8);
    // lqv         $v1[0], 0x10($zero)
    rsp.LQV<0>(rsp.vpu.r[1], 0, 0X1);
    // lqv         $v16[0], 0x0($20)
    rsp.LQV<0>(rsp.vpu.r[16], r20, 0X0);
    // lqv         $v15[0], 0x0($4)
    rsp.LQV<0>(rsp.vpu.r[15], r4, 0X0);
    // lqv         $v14[0], 0x80($4)
    rsp.LQV<0>(rsp.vpu.r[14], r4, 0X8);
    // lqv         $v17[0], 0x80($20)
    rsp.LQV<0>(rsp.vpu.r[17], r20, 0X8);
    // addi        $4, $4, 0x10
    r4 = RSP_ADD32(r4, 0X10);
    // addi        $20, $20, 0x10
    r20 = RSP_ADD32(r20, 0X10);
L_163C:
    ctx->pc_trail[ctx->pc_trail_idx & 31] = 0x163C;
    ctx->pc_trail_idx++;
    if (++ctx->watchdog_count > 100000000ULL) {
        fprintf(stderr, "[rsp watchdog] hung at PC 0x163C after %llu transitions; PC trail (oldest..newest):\n", (unsigned long long)ctx->watchdog_count);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (ctx->pc_trail_idx + i) & 31;
            fprintf(stderr, "  [%2u] PC=0x%04X\n", i, ctx->pc_trail[pos]);
        }
        fprintf(stderr, "[rsp watchdog] gprs: r1=%08X r2=%08X r3=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X jt=%08X dma_mem=%08X dma_dram=%08X\n",
            ctx->r1, ctx->r2, ctx->r3, ctx->r25, ctx->r26, ctx->r27, ctx->r28, ctx->r29, ctx->r30, ctx->r31, ctx->jump_target, ctx->dma_mem_address, ctx->dma_dram_address);
        return RspExitReason::Watchdog;
    }
    // addi        $7, $7, -0x1
    r7 = RSP_ADD32(r7, -0X1);
    // sfv         $v24[0], 0x0($5)
    rsp.SFV<0>(rsp.vpu.r[24], r5, 0X0);
    // sfv         $v24[8], 0x10($5)
    rsp.SFV<8>(rsp.vpu.r[24], r5, 0X1);
    // addi        $5, $5, 0x1
    r5 = RSP_ADD32(r5, 0X1);
    // shv         $v23[0], 0x0($5)
    rsp.SHV<0>(rsp.vpu.r[23], r5, 0X0);
    // shv         $v22[0], 0x10($5)
    rsp.SHV<0>(rsp.vpu.r[22], r5, 0X1);
    // addi        $5, $5, 0x1
    r5 = RSP_ADD32(r5, 0X1);
    // sfv         $v25[0], 0x0($5)
    rsp.SFV<0>(rsp.vpu.r[25], r5, 0X0);
    // sfv         $v25[8], 0x10($5)
    rsp.SFV<8>(rsp.vpu.r[25], r5, 0X1);
    // vge         $v20, $v16, $v1[0]
    rsp.VGE<8>(rsp.vpu.r[20], rsp.vpu.r[16], rsp.vpu.r[1]);
    // vge         $v19, $v15, $v1[0]
    rsp.VGE<8>(rsp.vpu.r[19], rsp.vpu.r[15], rsp.vpu.r[1]);
    // vge         $v18, $v14, $v1[0]
    rsp.VGE<8>(rsp.vpu.r[18], rsp.vpu.r[14], rsp.vpu.r[1]);
    // vge         $v21, $v17, $v1[0]
    rsp.VGE<8>(rsp.vpu.r[21], rsp.vpu.r[17], rsp.vpu.r[1]);
    // vlt         $v20, $v20, $v1[1]
    rsp.VLT<9>(rsp.vpu.r[20], rsp.vpu.r[20], rsp.vpu.r[1]);
    // vlt         $v19, $v19, $v1[1]
    rsp.VLT<9>(rsp.vpu.r[19], rsp.vpu.r[19], rsp.vpu.r[1]);
    // vlt         $v18, $v18, $v1[1]
    rsp.VLT<9>(rsp.vpu.r[18], rsp.vpu.r[18], rsp.vpu.r[1]);
    // vlt         $v21, $v21, $v1[1]
    rsp.VLT<9>(rsp.vpu.r[21], rsp.vpu.r[21], rsp.vpu.r[1]);
    // vadd        $v19, $v19, $v1[6]
    rsp.VADD<14>(rsp.vpu.r[19], rsp.vpu.r[19], rsp.vpu.r[1]);
    // vadd        $v18, $v18, $v1[6]
    rsp.VADD<14>(rsp.vpu.r[18], rsp.vpu.r[18], rsp.vpu.r[1]);
    // vmudm       $v19, $v19, $v1[3]
    rsp.VMUDM<11>(rsp.vpu.r[19], rsp.vpu.r[19], rsp.vpu.r[1]);
    // vmudm       $v18, $v18, $v1[3]
    rsp.VMUDM<11>(rsp.vpu.r[18], rsp.vpu.r[18], rsp.vpu.r[1]);
    // vadd        $v19, $v19, $v1[4]
    rsp.VADD<12>(rsp.vpu.r[19], rsp.vpu.r[19], rsp.vpu.r[1]);
    // vadd        $v18, $v18, $v1[4]
    rsp.VADD<12>(rsp.vpu.r[18], rsp.vpu.r[18], rsp.vpu.r[1]);
    // vmudm       $v20, $v20, $v1[5]
    rsp.VMUDM<13>(rsp.vpu.r[20], rsp.vpu.r[20], rsp.vpu.r[1]);
    // vmudm       $v21, $v21, $v1[5]
    rsp.VMUDM<13>(rsp.vpu.r[21], rsp.vpu.r[21], rsp.vpu.r[1]);
    // vadd        $v20, $v20, $v1[2]
    rsp.VADD<10>(rsp.vpu.r[20], rsp.vpu.r[20], rsp.vpu.r[1]);
    // vadd        $v21, $v21, $v1[2]
    rsp.VADD<10>(rsp.vpu.r[21], rsp.vpu.r[21], rsp.vpu.r[1]);
    // lqv         $v16[0], 0x0($20)
    rsp.LQV<0>(rsp.vpu.r[16], r20, 0X0);
    // lqv         $v15[0], 0x0($4)
    rsp.LQV<0>(rsp.vpu.r[15], r4, 0X0);
    // lqv         $v14[0], 0x80($4)
    rsp.LQV<0>(rsp.vpu.r[14], r4, 0X8);
    // lqv         $v17[0], 0x80($20)
    rsp.LQV<0>(rsp.vpu.r[17], r20, 0X8);
    // vmudn       $v24, $v20, $v1[2]
    rsp.VMUDN<10>(rsp.vpu.r[24], rsp.vpu.r[20], rsp.vpu.r[1]);
    // vmudn       $v23, $v19, $v1[2]
    rsp.VMUDN<10>(rsp.vpu.r[23], rsp.vpu.r[19], rsp.vpu.r[1]);
    // vmudn       $v22, $v18, $v1[2]
    rsp.VMUDN<10>(rsp.vpu.r[22], rsp.vpu.r[18], rsp.vpu.r[1]);
    // vmudn       $v25, $v21, $v1[2]
    rsp.VMUDN<10>(rsp.vpu.r[25], rsp.vpu.r[21], rsp.vpu.r[1]);
    // addi        $4, $4, 0x10
    r4 = RSP_ADD32(r4, 0X10);
    // addi        $20, $20, 0x10
    r20 = RSP_ADD32(r20, 0X10);
    // bgtz        $7, L_163C
    if (RSP_SIGNED(r7) > 0) {
        // addi        $5, $5, 0x1E
        r5 = RSP_ADD32(r5, 0X1E);
        goto L_163C;
    }
    // addi        $5, $5, 0x1E
    r5 = RSP_ADD32(r5, 0X1E);
    // sfv         $v24[0], 0x0($5)
    rsp.SFV<0>(rsp.vpu.r[24], r5, 0X0);
    // sfv         $v24[8], 0x10($5)
    rsp.SFV<8>(rsp.vpu.r[24], r5, 0X1);
    // addi        $5, $5, 0x1
    r5 = RSP_ADD32(r5, 0X1);
    // shv         $v23[0], 0x0($5)
    rsp.SHV<0>(rsp.vpu.r[23], r5, 0X0);
    // shv         $v22[0], 0x10($5)
    rsp.SHV<0>(rsp.vpu.r[22], r5, 0X1);
    // addi        $5, $5, 0x1
    r5 = RSP_ADD32(r5, 0X1);
    // sfv         $v25[0], 0x0($5)
    rsp.SFV<0>(rsp.vpu.r[25], r5, 0X0);
    // sfv         $v25[8], 0x10($5)
    rsp.SFV<8>(rsp.vpu.r[25], r5, 0X1);
    // j           L_186C
    // nop

    goto L_186C;
    // nop

L_1700:
    ctx->pc_trail[ctx->pc_trail_idx & 31] = 0x1700;
    ctx->pc_trail_idx++;
    if (++ctx->watchdog_count > 100000000ULL) {
        fprintf(stderr, "[rsp watchdog] hung at PC 0x1700 after %llu transitions; PC trail (oldest..newest):\n", (unsigned long long)ctx->watchdog_count);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (ctx->pc_trail_idx + i) & 31;
            fprintf(stderr, "  [%2u] PC=0x%04X\n", i, ctx->pc_trail[pos]);
        }
        fprintf(stderr, "[rsp watchdog] gprs: r1=%08X r2=%08X r3=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X jt=%08X dma_mem=%08X dma_dram=%08X\n",
            ctx->r1, ctx->r2, ctx->r3, ctx->r25, ctx->r26, ctx->r27, ctx->r28, ctx->r29, ctx->r30, ctx->r31, ctx->jump_target, ctx->dma_mem_address, ctx->dma_dram_address);
        return RspExitReason::Watchdog;
    }
    // addi        $4, $zero, 0x560
    r4 = RSP_ADD32(0, 0X560);
    // addi        $20, $4, 0x200
    r20 = RSP_ADD32(r4, 0X200);
    // addi        $5, $zero, 0xC20
    r5 = RSP_ADD32(0, 0XC20);
    // addi        $7, $zero, 0x2
    r7 = RSP_ADD32(0, 0X2);
    // addi        $8, $zero, 0x3
    r8 = RSP_ADD32(0, 0X3);
    // lqv         $v1[0], 0x10($zero)
    rsp.LQV<0>(rsp.vpu.r[1], 0, 0X1);
    // lqv         $v18[0], 0x0($20)
    rsp.LQV<0>(rsp.vpu.r[18], r20, 0X0);
    // lqv         $v16[0], 0x0($4)
    rsp.LQV<0>(rsp.vpu.r[16], r4, 0X0);
    // lqv         $v14[0], 0x80($4)
    rsp.LQV<0>(rsp.vpu.r[14], r4, 0X8);
    // lqv         $v17[0], 0x10($4)
    rsp.LQV<0>(rsp.vpu.r[17], r4, 0X1);
    // lqv         $v15[0], 0x90($4)
    rsp.LQV<0>(rsp.vpu.r[15], r4, 0X9);
    // lqv         $v19[0], 0x80($20)
    rsp.LQV<0>(rsp.vpu.r[19], r20, 0X8);
    // addi        $4, $4, 0x20
    r4 = RSP_ADD32(r4, 0X20);
    // addi        $20, $20, 0x10
    r20 = RSP_ADD32(r20, 0X10);
L_1738:
    ctx->pc_trail[ctx->pc_trail_idx & 31] = 0x1738;
    ctx->pc_trail_idx++;
    if (++ctx->watchdog_count > 100000000ULL) {
        fprintf(stderr, "[rsp watchdog] hung at PC 0x1738 after %llu transitions; PC trail (oldest..newest):\n", (unsigned long long)ctx->watchdog_count);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (ctx->pc_trail_idx + i) & 31;
            fprintf(stderr, "  [%2u] PC=0x%04X\n", i, ctx->pc_trail[pos]);
        }
        fprintf(stderr, "[rsp watchdog] gprs: r1=%08X r2=%08X r3=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X jt=%08X dma_mem=%08X dma_dram=%08X\n",
            ctx->r1, ctx->r2, ctx->r3, ctx->r25, ctx->r26, ctx->r27, ctx->r28, ctx->r29, ctx->r30, ctx->r31, ctx->jump_target, ctx->dma_mem_address, ctx->dma_dram_address);
        return RspExitReason::Watchdog;
    }
    // addi        $8, $8, -0x1
    r8 = RSP_ADD32(r8, -0X1);
    // sfv         $v30[0], 0x0($5)
    rsp.SFV<0>(rsp.vpu.r[30], r5, 0X0);
    // sfv         $v30[8], 0x10($5)
    rsp.SFV<8>(rsp.vpu.r[30], r5, 0X1);
    // sfv         $v30[0], 0x20($5)
    rsp.SFV<0>(rsp.vpu.r[30], r5, 0X2);
    // sfv         $v30[8], 0x30($5)
    rsp.SFV<8>(rsp.vpu.r[30], r5, 0X3);
    // addi        $5, $5, 0x1
    r5 = RSP_ADD32(r5, 0X1);
    // shv         $v28[0], 0x0($5)
    rsp.SHV<0>(rsp.vpu.r[28], r5, 0X0);
    // shv         $v26[0], 0x10($5)
    rsp.SHV<0>(rsp.vpu.r[26], r5, 0X1);
    // shv         $v29[0], 0x20($5)
    rsp.SHV<0>(rsp.vpu.r[29], r5, 0X2);
    // shv         $v27[0], 0x30($5)
    rsp.SHV<0>(rsp.vpu.r[27], r5, 0X3);
    // addi        $5, $5, 0x1
    r5 = RSP_ADD32(r5, 0X1);
    // sfv         $v31[0], 0x0($5)
    rsp.SFV<0>(rsp.vpu.r[31], r5, 0X0);
    // sfv         $v31[8], 0x10($5)
    rsp.SFV<8>(rsp.vpu.r[31], r5, 0X1);
    // sfv         $v31[0], 0x20($5)
    rsp.SFV<0>(rsp.vpu.r[31], r5, 0X2);
    // sfv         $v31[8], 0x30($5)
    rsp.SFV<8>(rsp.vpu.r[31], r5, 0X3);
    // vge         $v24, $v18, $v1[0]
    rsp.VGE<8>(rsp.vpu.r[24], rsp.vpu.r[18], rsp.vpu.r[1]);
    // vge         $v22, $v16, $v1[0]
    rsp.VGE<8>(rsp.vpu.r[22], rsp.vpu.r[16], rsp.vpu.r[1]);
    // vge         $v20, $v14, $v1[0]
    rsp.VGE<8>(rsp.vpu.r[20], rsp.vpu.r[14], rsp.vpu.r[1]);
    // vge         $v23, $v17, $v1[0]
    rsp.VGE<8>(rsp.vpu.r[23], rsp.vpu.r[17], rsp.vpu.r[1]);
    // vge         $v21, $v15, $v1[0]
    rsp.VGE<8>(rsp.vpu.r[21], rsp.vpu.r[15], rsp.vpu.r[1]);
    // vge         $v25, $v19, $v1[0]
    rsp.VGE<8>(rsp.vpu.r[25], rsp.vpu.r[19], rsp.vpu.r[1]);
    // vlt         $v24, $v24, $v1[1]
    rsp.VLT<9>(rsp.vpu.r[24], rsp.vpu.r[24], rsp.vpu.r[1]);
    // vlt         $v22, $v22, $v1[1]
    rsp.VLT<9>(rsp.vpu.r[22], rsp.vpu.r[22], rsp.vpu.r[1]);
    // vlt         $v20, $v20, $v1[1]
    rsp.VLT<9>(rsp.vpu.r[20], rsp.vpu.r[20], rsp.vpu.r[1]);
    // vlt         $v23, $v23, $v1[1]
    rsp.VLT<9>(rsp.vpu.r[23], rsp.vpu.r[23], rsp.vpu.r[1]);
    // vlt         $v21, $v21, $v1[1]
    rsp.VLT<9>(rsp.vpu.r[21], rsp.vpu.r[21], rsp.vpu.r[1]);
    // vlt         $v25, $v25, $v1[1]
    rsp.VLT<9>(rsp.vpu.r[25], rsp.vpu.r[25], rsp.vpu.r[1]);
    // vadd        $v22, $v22, $v1[6]
    rsp.VADD<14>(rsp.vpu.r[22], rsp.vpu.r[22], rsp.vpu.r[1]);
    // vadd        $v20, $v20, $v1[6]
    rsp.VADD<14>(rsp.vpu.r[20], rsp.vpu.r[20], rsp.vpu.r[1]);
    // vadd        $v23, $v23, $v1[6]
    rsp.VADD<14>(rsp.vpu.r[23], rsp.vpu.r[23], rsp.vpu.r[1]);
    // vadd        $v21, $v21, $v1[6]
    rsp.VADD<14>(rsp.vpu.r[21], rsp.vpu.r[21], rsp.vpu.r[1]);
    // vmudm       $v22, $v22, $v1[3]
    rsp.VMUDM<11>(rsp.vpu.r[22], rsp.vpu.r[22], rsp.vpu.r[1]);
    // vmudm       $v20, $v20, $v1[3]
    rsp.VMUDM<11>(rsp.vpu.r[20], rsp.vpu.r[20], rsp.vpu.r[1]);
    // vmudm       $v23, $v23, $v1[3]
    rsp.VMUDM<11>(rsp.vpu.r[23], rsp.vpu.r[23], rsp.vpu.r[1]);
    // vmudm       $v21, $v21, $v1[3]
    rsp.VMUDM<11>(rsp.vpu.r[21], rsp.vpu.r[21], rsp.vpu.r[1]);
    // vadd        $v22, $v22, $v1[4]
    rsp.VADD<12>(rsp.vpu.r[22], rsp.vpu.r[22], rsp.vpu.r[1]);
    // vadd        $v20, $v20, $v1[4]
    rsp.VADD<12>(rsp.vpu.r[20], rsp.vpu.r[20], rsp.vpu.r[1]);
    // vadd        $v23, $v23, $v1[4]
    rsp.VADD<12>(rsp.vpu.r[23], rsp.vpu.r[23], rsp.vpu.r[1]);
    // vadd        $v21, $v21, $v1[4]
    rsp.VADD<12>(rsp.vpu.r[21], rsp.vpu.r[21], rsp.vpu.r[1]);
    // vmudm       $v24, $v24, $v1[5]
    rsp.VMUDM<13>(rsp.vpu.r[24], rsp.vpu.r[24], rsp.vpu.r[1]);
    // vmudm       $v25, $v25, $v1[5]
    rsp.VMUDM<13>(rsp.vpu.r[25], rsp.vpu.r[25], rsp.vpu.r[1]);
    // vadd        $v24, $v24, $v1[2]
    rsp.VADD<10>(rsp.vpu.r[24], rsp.vpu.r[24], rsp.vpu.r[1]);
    // vadd        $v25, $v25, $v1[2]
    rsp.VADD<10>(rsp.vpu.r[25], rsp.vpu.r[25], rsp.vpu.r[1]);
    // lqv         $v18[0], 0x0($20)
    rsp.LQV<0>(rsp.vpu.r[18], r20, 0X0);
    // lqv         $v16[0], 0x0($4)
    rsp.LQV<0>(rsp.vpu.r[16], r4, 0X0);
    // lqv         $v14[0], 0x80($4)
    rsp.LQV<0>(rsp.vpu.r[14], r4, 0X8);
    // lqv         $v17[0], 0x10($4)
    rsp.LQV<0>(rsp.vpu.r[17], r4, 0X1);
    // lqv         $v15[0], 0x90($4)
    rsp.LQV<0>(rsp.vpu.r[15], r4, 0X9);
    // lqv         $v19[0], 0x80($20)
    rsp.LQV<0>(rsp.vpu.r[19], r20, 0X8);
    // vmudn       $v30, $v24, $v1[2]
    rsp.VMUDN<10>(rsp.vpu.r[30], rsp.vpu.r[24], rsp.vpu.r[1]);
    // vmudn       $v28, $v22, $v1[2]
    rsp.VMUDN<10>(rsp.vpu.r[28], rsp.vpu.r[22], rsp.vpu.r[1]);
    // vmudn       $v26, $v20, $v1[2]
    rsp.VMUDN<10>(rsp.vpu.r[26], rsp.vpu.r[20], rsp.vpu.r[1]);
    // vmudn       $v29, $v23, $v1[2]
    rsp.VMUDN<10>(rsp.vpu.r[29], rsp.vpu.r[23], rsp.vpu.r[1]);
    // vmudn       $v27, $v21, $v1[2]
    rsp.VMUDN<10>(rsp.vpu.r[27], rsp.vpu.r[21], rsp.vpu.r[1]);
    // vmudn       $v31, $v25, $v1[2]
    rsp.VMUDN<10>(rsp.vpu.r[31], rsp.vpu.r[25], rsp.vpu.r[1]);
    // addi        $4, $4, 0x20
    r4 = RSP_ADD32(r4, 0X20);
    // addi        $20, $20, 0x10
    r20 = RSP_ADD32(r20, 0X10);
    // bgtz        $8, L_1738
    if (RSP_SIGNED(r8) > 0) {
        // addi        $5, $5, 0x3E
        r5 = RSP_ADD32(r5, 0X3E);
        goto L_1738;
    }
    // addi        $5, $5, 0x3E
    r5 = RSP_ADD32(r5, 0X3E);
    // addi        $7, $7, -0x1
    r7 = RSP_ADD32(r7, -0X1);
    // addi        $8, $zero, 0x5
    r8 = RSP_ADD32(0, 0X5);
    // bgtz        $7, L_1738
    if (RSP_SIGNED(r7) > 0) {
        // addi        $4, $4, 0x80
        r4 = RSP_ADD32(r4, 0X80);
        goto L_1738;
    }
    // addi        $4, $4, 0x80
    r4 = RSP_ADD32(r4, 0X80);
    // sfv         $v30[0], 0x0($5)
    rsp.SFV<0>(rsp.vpu.r[30], r5, 0X0);
    // sfv         $v30[8], 0x10($5)
    rsp.SFV<8>(rsp.vpu.r[30], r5, 0X1);
    // sfv         $v30[0], 0x20($5)
    rsp.SFV<0>(rsp.vpu.r[30], r5, 0X2);
    // sfv         $v30[8], 0x30($5)
    rsp.SFV<8>(rsp.vpu.r[30], r5, 0X3);
    // addi        $5, $5, 0x1
    r5 = RSP_ADD32(r5, 0X1);
    // shv         $v28[0], 0x0($5)
    rsp.SHV<0>(rsp.vpu.r[28], r5, 0X0);
    // shv         $v26[0], 0x10($5)
    rsp.SHV<0>(rsp.vpu.r[26], r5, 0X1);
    // shv         $v29[0], 0x20($5)
    rsp.SHV<0>(rsp.vpu.r[29], r5, 0X2);
    // shv         $v27[0], 0x30($5)
    rsp.SHV<0>(rsp.vpu.r[27], r5, 0X3);
    // addi        $5, $5, 0x1
    r5 = RSP_ADD32(r5, 0X1);
    // sfv         $v31[0], 0x0($5)
    rsp.SFV<0>(rsp.vpu.r[31], r5, 0X0);
    // sfv         $v31[8], 0x10($5)
    rsp.SFV<8>(rsp.vpu.r[31], r5, 0X1);
    // sfv         $v31[0], 0x20($5)
    rsp.SFV<0>(rsp.vpu.r[31], r5, 0X2);
    // sfv         $v31[8], 0x30($5)
    rsp.SFV<8>(rsp.vpu.r[31], r5, 0X3);
L_186C:
    ctx->pc_trail[ctx->pc_trail_idx & 31] = 0x186C;
    ctx->pc_trail_idx++;
    if (++ctx->watchdog_count > 100000000ULL) {
        fprintf(stderr, "[rsp watchdog] hung at PC 0x186C after %llu transitions; PC trail (oldest..newest):\n", (unsigned long long)ctx->watchdog_count);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (ctx->pc_trail_idx + i) & 31;
            fprintf(stderr, "  [%2u] PC=0x%04X\n", i, ctx->pc_trail[pos]);
        }
        fprintf(stderr, "[rsp watchdog] gprs: r1=%08X r2=%08X r3=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X jt=%08X dma_mem=%08X dma_dram=%08X\n",
            ctx->r1, ctx->r2, ctx->r3, ctx->r25, ctx->r26, ctx->r27, ctx->r28, ctx->r29, ctx->r30, ctx->r31, ctx->jump_target, ctx->dma_mem_address, ctx->dma_dram_address);
        return RspExitReason::Watchdog;
    }
    // jal         0x1948
    r31 = 0x1874;
    // nop

    goto L_1948;
    // nop

L_1874:
    ctx->pc_trail[ctx->pc_trail_idx & 31] = 0x1874;
    ctx->pc_trail_idx++;
    if (++ctx->watchdog_count > 100000000ULL) {
        fprintf(stderr, "[rsp watchdog] hung at PC 0x1874 after %llu transitions; PC trail (oldest..newest):\n", (unsigned long long)ctx->watchdog_count);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (ctx->pc_trail_idx + i) & 31;
            fprintf(stderr, "  [%2u] PC=0x%04X\n", i, ctx->pc_trail[pos]);
        }
        fprintf(stderr, "[rsp watchdog] gprs: r1=%08X r2=%08X r3=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X jt=%08X dma_mem=%08X dma_dram=%08X\n",
            ctx->r1, ctx->r2, ctx->r3, ctx->r25, ctx->r26, ctx->r27, ctx->r28, ctx->r29, ctx->r30, ctx->r31, ctx->jump_target, ctx->dma_mem_address, ctx->dma_dram_address);
        return RspExitReason::Watchdog;
    }
    // beq         $3, $zero, L_18C4
    if (r3 == 0) {
        // mfc0        $22, SP_STATUS
        r22 = 0;
        goto L_18C4;
    }
    // mfc0        $22, SP_STATUS
    r22 = 0;
    // andi        $22, $22, 0x80
    r22 = r22 & 0X80;
    // beq         $22, $zero, L_1160
    if (r22 == 0) {
        // add         $25, $25, $12
        r25 = RSP_ADD32(r25, r12);
        goto L_1160;
    }
    // add         $25, $25, $12
    r25 = RSP_ADD32(r25, r12);
    // sub         $25, $25, $12
    r25 = RSP_SUB32(r25, r12);
    // sw          $3, 0x1E0($zero)
    RSP_MEM_W_STORE(0X1E0, 0, r3);
    // sw          $25, 0x1E4($zero)
    RSP_MEM_W_STORE(0X1E4, 0, r25);
    // sw          $26, 0x1E8($zero)
    RSP_MEM_W_STORE(0X1E8, 0, r26);
    // sw          $9, 0x1EC($zero)
    RSP_MEM_W_STORE(0X1EC, 0, r9);
    // sw          $10, 0x1F0($zero)
    RSP_MEM_W_STORE(0X1F0, 0, r10);
    // sw          $11, 0x1F4($zero)
    RSP_MEM_W_STORE(0X1F4, 0, r11);
    // sw          $12, 0x1F8($zero)
    RSP_MEM_W_STORE(0X1F8, 0, r12);
    // addi        $27, $zero, 0x0
    r27 = RSP_ADD32(0, 0X0);
    // addi        $28, $23, 0x0
    r28 = RSP_ADD32(r23, 0X0);
    // jal         0x191C
    r31 = 0x18B8;
    // addi        $29, $24, -0x1
    r29 = RSP_ADD32(r24, -0X1);
    goto L_191C;
    // addi        $29, $24, -0x1
    r29 = RSP_ADD32(r24, -0X1);
L_18B8:
    ctx->pc_trail[ctx->pc_trail_idx & 31] = 0x18B8;
    ctx->pc_trail_idx++;
    if (++ctx->watchdog_count > 100000000ULL) {
        fprintf(stderr, "[rsp watchdog] hung at PC 0x18B8 after %llu transitions; PC trail (oldest..newest):\n", (unsigned long long)ctx->watchdog_count);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (ctx->pc_trail_idx + i) & 31;
            fprintf(stderr, "  [%2u] PC=0x%04X\n", i, ctx->pc_trail[pos]);
        }
        fprintf(stderr, "[rsp watchdog] gprs: r1=%08X r2=%08X r3=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X jt=%08X dma_mem=%08X dma_dram=%08X\n",
            ctx->r1, ctx->r2, ctx->r3, ctx->r25, ctx->r26, ctx->r27, ctx->r28, ctx->r29, ctx->r30, ctx->r31, ctx->jump_target, ctx->dma_mem_address, ctx->dma_dram_address);
        return RspExitReason::Watchdog;
    }
    // ori         $21, $zero, 0x1000
    r21 = 0 | 0X1000;
    // jal         0x1948
    r31 = 0x18C4;
    // mtc0        $21, SP_STATUS
    goto L_1948;
    // mtc0        $21, SP_STATUS
L_18C4:
    ctx->pc_trail[ctx->pc_trail_idx & 31] = 0x18C4;
    ctx->pc_trail_idx++;
    if (++ctx->watchdog_count > 100000000ULL) {
        fprintf(stderr, "[rsp watchdog] hung at PC 0x18C4 after %llu transitions; PC trail (oldest..newest):\n", (unsigned long long)ctx->watchdog_count);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (ctx->pc_trail_idx + i) & 31;
            fprintf(stderr, "  [%2u] PC=0x%04X\n", i, ctx->pc_trail[pos]);
        }
        fprintf(stderr, "[rsp watchdog] gprs: r1=%08X r2=%08X r3=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X jt=%08X dma_mem=%08X dma_dram=%08X\n",
            ctx->r1, ctx->r2, ctx->r3, ctx->r25, ctx->r26, ctx->r27, ctx->r28, ctx->r29, ctx->r30, ctx->r31, ctx->jump_target, ctx->dma_mem_address, ctx->dma_dram_address);
        return RspExitReason::Watchdog;
    }
    // addi        $27, $zero, 0xC60
    r27 = RSP_ADD32(0, 0XC60);
    // addi        $28, $26, 0x0
    r28 = RSP_ADD32(r26, 0X0);
    // jal         0x191C
    r31 = 0x18D4;
    // add         $29, $zero, $11
    r29 = RSP_ADD32(0, r11);
    goto L_191C;
    // add         $29, $zero, $11
    r29 = RSP_ADD32(0, r11);
L_18D4:
    ctx->pc_trail[ctx->pc_trail_idx & 31] = 0x18D4;
    ctx->pc_trail_idx++;
    if (++ctx->watchdog_count > 100000000ULL) {
        fprintf(stderr, "[rsp watchdog] hung at PC 0x18D4 after %llu transitions; PC trail (oldest..newest):\n", (unsigned long long)ctx->watchdog_count);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (ctx->pc_trail_idx + i) & 31;
            fprintf(stderr, "  [%2u] PC=0x%04X\n", i, ctx->pc_trail[pos]);
        }
        fprintf(stderr, "[rsp watchdog] gprs: r1=%08X r2=%08X r3=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X jt=%08X dma_mem=%08X dma_dram=%08X\n",
            ctx->r1, ctx->r2, ctx->r3, ctx->r25, ctx->r26, ctx->r27, ctx->r28, ctx->r29, ctx->r30, ctx->r31, ctx->jump_target, ctx->dma_mem_address, ctx->dma_dram_address);
        return RspExitReason::Watchdog;
    }
    // jal         0x1948
    r31 = 0x18DC;
    // addi        $21, $zero, 0x4000
    r21 = RSP_ADD32(0, 0X4000);
    goto L_1948;
    // addi        $21, $zero, 0x4000
    r21 = RSP_ADD32(0, 0X4000);
L_18DC:
    ctx->pc_trail[ctx->pc_trail_idx & 31] = 0x18DC;
    ctx->pc_trail_idx++;
    if (++ctx->watchdog_count > 100000000ULL) {
        fprintf(stderr, "[rsp watchdog] hung at PC 0x18DC after %llu transitions; PC trail (oldest..newest):\n", (unsigned long long)ctx->watchdog_count);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (ctx->pc_trail_idx + i) & 31;
            fprintf(stderr, "  [%2u] PC=0x%04X\n", i, ctx->pc_trail[pos]);
        }
        fprintf(stderr, "[rsp watchdog] gprs: r1=%08X r2=%08X r3=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X jt=%08X dma_mem=%08X dma_dram=%08X\n",
            ctx->r1, ctx->r2, ctx->r3, ctx->r25, ctx->r26, ctx->r27, ctx->r28, ctx->r29, ctx->r30, ctx->r31, ctx->jump_target, ctx->dma_mem_address, ctx->dma_dram_address);
        return RspExitReason::Watchdog;
    }
    // mtc0        $21, SP_STATUS
    // break       0
    return RspExitReason::Broke;
    // nop

L_18E8:
    ctx->pc_trail[ctx->pc_trail_idx & 31] = 0x18E8;
    ctx->pc_trail_idx++;
    if (++ctx->watchdog_count > 100000000ULL) {
        fprintf(stderr, "[rsp watchdog] hung at PC 0x18E8 after %llu transitions; PC trail (oldest..newest):\n", (unsigned long long)ctx->watchdog_count);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (ctx->pc_trail_idx + i) & 31;
            fprintf(stderr, "  [%2u] PC=0x%04X\n", i, ctx->pc_trail[pos]);
        }
        fprintf(stderr, "[rsp watchdog] gprs: r1=%08X r2=%08X r3=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X jt=%08X dma_mem=%08X dma_dram=%08X\n",
            ctx->r1, ctx->r2, ctx->r3, ctx->r25, ctx->r26, ctx->r27, ctx->r28, ctx->r29, ctx->r30, ctx->r31, ctx->jump_target, ctx->dma_mem_address, ctx->dma_dram_address);
        return RspExitReason::Watchdog;
    }
    // j           L_18E8
    // nop

    goto L_18E8;
    // nop

L_18F0:
    ctx->pc_trail[ctx->pc_trail_idx & 31] = 0x18F0;
    ctx->pc_trail_idx++;
    if (++ctx->watchdog_count > 100000000ULL) {
        fprintf(stderr, "[rsp watchdog] hung at PC 0x18F0 after %llu transitions; PC trail (oldest..newest):\n", (unsigned long long)ctx->watchdog_count);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (ctx->pc_trail_idx + i) & 31;
            fprintf(stderr, "  [%2u] PC=0x%04X\n", i, ctx->pc_trail[pos]);
        }
        fprintf(stderr, "[rsp watchdog] gprs: r1=%08X r2=%08X r3=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X jt=%08X dma_mem=%08X dma_dram=%08X\n",
            ctx->r1, ctx->r2, ctx->r3, ctx->r25, ctx->r26, ctx->r27, ctx->r28, ctx->r29, ctx->r30, ctx->r31, ctx->jump_target, ctx->dma_mem_address, ctx->dma_dram_address);
        return RspExitReason::Watchdog;
    }
    // mfc0        $30, SP_SEMAPHORE
    r30 = 0;
L_18F4:
    ctx->pc_trail[ctx->pc_trail_idx & 31] = 0x18F4;
    ctx->pc_trail_idx++;
    if (++ctx->watchdog_count > 100000000ULL) {
        fprintf(stderr, "[rsp watchdog] hung at PC 0x18F4 after %llu transitions; PC trail (oldest..newest):\n", (unsigned long long)ctx->watchdog_count);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (ctx->pc_trail_idx + i) & 31;
            fprintf(stderr, "  [%2u] PC=0x%04X\n", i, ctx->pc_trail[pos]);
        }
        fprintf(stderr, "[rsp watchdog] gprs: r1=%08X r2=%08X r3=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X jt=%08X dma_mem=%08X dma_dram=%08X\n",
            ctx->r1, ctx->r2, ctx->r3, ctx->r25, ctx->r26, ctx->r27, ctx->r28, ctx->r29, ctx->r30, ctx->r31, ctx->jump_target, ctx->dma_mem_address, ctx->dma_dram_address);
        return RspExitReason::Watchdog;
    }
    // bne         $30, $zero, L_18F4
    if (r30 != 0) {
        // mfc0        $30, SP_SEMAPHORE
        r30 = 0;
        goto L_18F4;
    }
    // mfc0        $30, SP_SEMAPHORE
    r30 = 0;
    // mfc0        $30, SP_DMA_FULL
    r30 = 0;
L_1900:
    ctx->pc_trail[ctx->pc_trail_idx & 31] = 0x1900;
    ctx->pc_trail_idx++;
    if (++ctx->watchdog_count > 100000000ULL) {
        fprintf(stderr, "[rsp watchdog] hung at PC 0x1900 after %llu transitions; PC trail (oldest..newest):\n", (unsigned long long)ctx->watchdog_count);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (ctx->pc_trail_idx + i) & 31;
            fprintf(stderr, "  [%2u] PC=0x%04X\n", i, ctx->pc_trail[pos]);
        }
        fprintf(stderr, "[rsp watchdog] gprs: r1=%08X r2=%08X r3=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X jt=%08X dma_mem=%08X dma_dram=%08X\n",
            ctx->r1, ctx->r2, ctx->r3, ctx->r25, ctx->r26, ctx->r27, ctx->r28, ctx->r29, ctx->r30, ctx->r31, ctx->jump_target, ctx->dma_mem_address, ctx->dma_dram_address);
        return RspExitReason::Watchdog;
    }
    // bne         $30, $zero, L_1900
    if (r30 != 0) {
        // mfc0        $30, SP_DMA_FULL
        r30 = 0;
        goto L_1900;
    }
    // mfc0        $30, SP_DMA_FULL
    r30 = 0;
    // mtc0        $28, SP_MEM_ADDR
    SET_DMA_MEM(r28);
    // mtc0        $27, SP_DRAM_ADDR
    SET_DMA_DRAM(r27);
    // mtc0        $29, SP_RD_LEN
    DO_DMA_READ(r29);
    // jr          $ra
    jump_target = r31;
    debug_file = __FILE__; debug_line = __LINE__;
    // nop

    goto do_indirect_jump;
    // nop

L_191C:
    ctx->pc_trail[ctx->pc_trail_idx & 31] = 0x191C;
    ctx->pc_trail_idx++;
    if (++ctx->watchdog_count > 100000000ULL) {
        fprintf(stderr, "[rsp watchdog] hung at PC 0x191C after %llu transitions; PC trail (oldest..newest):\n", (unsigned long long)ctx->watchdog_count);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (ctx->pc_trail_idx + i) & 31;
            fprintf(stderr, "  [%2u] PC=0x%04X\n", i, ctx->pc_trail[pos]);
        }
        fprintf(stderr, "[rsp watchdog] gprs: r1=%08X r2=%08X r3=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X jt=%08X dma_mem=%08X dma_dram=%08X\n",
            ctx->r1, ctx->r2, ctx->r3, ctx->r25, ctx->r26, ctx->r27, ctx->r28, ctx->r29, ctx->r30, ctx->r31, ctx->jump_target, ctx->dma_mem_address, ctx->dma_dram_address);
        return RspExitReason::Watchdog;
    }
    // mfc0        $30, SP_SEMAPHORE
    r30 = 0;
L_1920:
    ctx->pc_trail[ctx->pc_trail_idx & 31] = 0x1920;
    ctx->pc_trail_idx++;
    if (++ctx->watchdog_count > 100000000ULL) {
        fprintf(stderr, "[rsp watchdog] hung at PC 0x1920 after %llu transitions; PC trail (oldest..newest):\n", (unsigned long long)ctx->watchdog_count);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (ctx->pc_trail_idx + i) & 31;
            fprintf(stderr, "  [%2u] PC=0x%04X\n", i, ctx->pc_trail[pos]);
        }
        fprintf(stderr, "[rsp watchdog] gprs: r1=%08X r2=%08X r3=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X jt=%08X dma_mem=%08X dma_dram=%08X\n",
            ctx->r1, ctx->r2, ctx->r3, ctx->r25, ctx->r26, ctx->r27, ctx->r28, ctx->r29, ctx->r30, ctx->r31, ctx->jump_target, ctx->dma_mem_address, ctx->dma_dram_address);
        return RspExitReason::Watchdog;
    }
    // bne         $30, $zero, L_1920
    if (r30 != 0) {
        // mfc0        $30, SP_SEMAPHORE
        r30 = 0;
        goto L_1920;
    }
    // mfc0        $30, SP_SEMAPHORE
    r30 = 0;
    // mfc0        $30, SP_DMA_FULL
    r30 = 0;
L_192C:
    ctx->pc_trail[ctx->pc_trail_idx & 31] = 0x192C;
    ctx->pc_trail_idx++;
    if (++ctx->watchdog_count > 100000000ULL) {
        fprintf(stderr, "[rsp watchdog] hung at PC 0x192C after %llu transitions; PC trail (oldest..newest):\n", (unsigned long long)ctx->watchdog_count);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (ctx->pc_trail_idx + i) & 31;
            fprintf(stderr, "  [%2u] PC=0x%04X\n", i, ctx->pc_trail[pos]);
        }
        fprintf(stderr, "[rsp watchdog] gprs: r1=%08X r2=%08X r3=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X jt=%08X dma_mem=%08X dma_dram=%08X\n",
            ctx->r1, ctx->r2, ctx->r3, ctx->r25, ctx->r26, ctx->r27, ctx->r28, ctx->r29, ctx->r30, ctx->r31, ctx->jump_target, ctx->dma_mem_address, ctx->dma_dram_address);
        return RspExitReason::Watchdog;
    }
    // bne         $30, $zero, L_192C
    if (r30 != 0) {
        // mfc0        $30, SP_DMA_FULL
        r30 = 0;
        goto L_192C;
    }
    // mfc0        $30, SP_DMA_FULL
    r30 = 0;
    // mtc0        $27, SP_MEM_ADDR
    SET_DMA_MEM(r27);
    // mtc0        $28, SP_DRAM_ADDR
    SET_DMA_DRAM(r28);
    // mtc0        $29, SP_WR_LEN
    DO_DMA_WRITE(r29);
    // jr          $ra
    jump_target = r31;
    debug_file = __FILE__; debug_line = __LINE__;
    // nop

    goto do_indirect_jump;
    // nop

L_1948:
    ctx->pc_trail[ctx->pc_trail_idx & 31] = 0x1948;
    ctx->pc_trail_idx++;
    if (++ctx->watchdog_count > 100000000ULL) {
        fprintf(stderr, "[rsp watchdog] hung at PC 0x1948 after %llu transitions; PC trail (oldest..newest):\n", (unsigned long long)ctx->watchdog_count);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (ctx->pc_trail_idx + i) & 31;
            fprintf(stderr, "  [%2u] PC=0x%04X\n", i, ctx->pc_trail[pos]);
        }
        fprintf(stderr, "[rsp watchdog] gprs: r1=%08X r2=%08X r3=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X jt=%08X dma_mem=%08X dma_dram=%08X\n",
            ctx->r1, ctx->r2, ctx->r3, ctx->r25, ctx->r26, ctx->r27, ctx->r28, ctx->r29, ctx->r30, ctx->r31, ctx->jump_target, ctx->dma_mem_address, ctx->dma_dram_address);
        return RspExitReason::Watchdog;
    }
    // mfc0        $30, SP_DMA_BUSY
    r30 = 0;
L_194C:
    ctx->pc_trail[ctx->pc_trail_idx & 31] = 0x194C;
    ctx->pc_trail_idx++;
    if (++ctx->watchdog_count > 100000000ULL) {
        fprintf(stderr, "[rsp watchdog] hung at PC 0x194C after %llu transitions; PC trail (oldest..newest):\n", (unsigned long long)ctx->watchdog_count);
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t pos = (ctx->pc_trail_idx + i) & 31;
            fprintf(stderr, "  [%2u] PC=0x%04X\n", i, ctx->pc_trail[pos]);
        }
        fprintf(stderr, "[rsp watchdog] gprs: r1=%08X r2=%08X r3=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X jt=%08X dma_mem=%08X dma_dram=%08X\n",
            ctx->r1, ctx->r2, ctx->r3, ctx->r25, ctx->r26, ctx->r27, ctx->r28, ctx->r29, ctx->r30, ctx->r31, ctx->jump_target, ctx->dma_mem_address, ctx->dma_dram_address);
        return RspExitReason::Watchdog;
    }
    // bne         $30, $zero, L_194C
    if (r30 != 0) {
        // mfc0        $30, SP_DMA_BUSY
        r30 = 0;
        goto L_194C;
    }
    // mfc0        $30, SP_DMA_BUSY
    r30 = 0;
    // mtc0        $zero, SP_SEMAPHORE
    // jr          $ra
    jump_target = r31;
    debug_file = __FILE__; debug_line = __LINE__;
    // nop

    goto do_indirect_jump;
    // nop

    return RspExitReason::ImemOverrun;
do_indirect_jump:
    switch ((jump_target | 0x1000) & 0X1FFF) { 
        case 0x107C: goto L_107C;
        case 0x10FC: goto L_10FC;
        case 0x1034: goto L_1034;
        case 0x10F4: goto L_10F4;
        case 0x18D4: goto L_18D4;
        case 0x10EC: goto L_10EC;
        case 0x10E4: goto L_10E4;
        case 0x10D8: goto L_10D8;
        case 0x18B8: goto L_18B8;
        case 0x10D0: goto L_10D0;
        case 0x1104: goto L_1104;
        case 0x110C: goto L_110C;
        case 0x1148: goto L_1148;
        case 0x1160: goto L_1160;
        case 0x1174: goto L_1174;
        case 0x13D8: goto L_13D8;
        case 0x13EC: goto L_13EC;
        case 0x1874: goto L_1874;
        case 0x18C4: goto L_18C4;
        case 0x18DC: goto L_18DC;
    }
    printf("Unhandled jump target 0x%04X in microcode njpgdspMain, coming from [%s:%d]\n", jump_target, debug_file, debug_line);
    printf("Register dump: r0  = %08X r1  = %08X r2  = %08X r3  = %08X r4  = %08X r5  = %08X r6  = %08X r7  = %08X\n"
           "               r8  = %08X r9  = %08X r10 = %08X r11 = %08X r12 = %08X r13 = %08X r14 = %08X r15 = %08X\n"
           "               r16 = %08X r17 = %08X r18 = %08X r19 = %08X r20 = %08X r21 = %08X r22 = %08X r23 = %08X\n"
           "               r24 = %08X r25 = %08X r26 = %08X r27 = %08X r28 = %08X r29 = %08X r30 = %08X r31 = %08X\n",
           0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12, r13, r14, r15, r16,
           r17, r18, r19, r20, r21, r22, r23, r24, r25, r26, r27, r28, r29, r30, r31);
    return RspExitReason::UnhandledJumpTarget;
}

RspExitReason njpgdspMain(uint8_t* rdram, [[maybe_unused]] uint32_t ucode_addr) {
    static thread_local RspContext persistent_ctx{};
    // Pre-task hook: if a runtime registered a hook keyed by
    // this ucode's name, call it here. Lets game-specific code
    // replicate parts of rspboot's setup that the static
    // recompilation can't infer (initial GPRs, DMA-engine
    // residue, pre-loaded command data in DMEM). Inline
    // null-check by the std::unordered_map lookup — typical
    // cost is one branch when no hook is registered.
    recomp::rsp::run_pre_task_hook(rdram, &persistent_ctx, "njpgdspMain", ucode_addr);
    return njpgdspMain_impl(rdram, &persistent_ctx);
}
