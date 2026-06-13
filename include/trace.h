/*
 * trace.h — TRACE_ENTRY / TRACE_RETURN hooks for trace_mode = true.
 *
 * N64Recomp emits literal `TRACE_ENTRY()` / `TRACE_RETURN()` at the
 * start/end of every recompiled function when game.toml has
 * `trace_mode = true` (the header is auto-included into every generated
 * funcs_N.c via config.recomp_include).
 *
 * TRACE_ENTRY pushes the function name into the always-on name ring
 * (diagnostics.cpp pkmnstadium_trace_push) read by the boot-stall
 * watchdog and post-mortem dumps — "which game functions are executing
 * right now" for threads that are spinning in call-free code and thus
 * invisible to the libultra-call ring.
 *
 * NOTE: loop back-edge preemption checkpoints (RECOMP_LOOP_CHECKPOINT)
 * are NOT defined here — they are scheduler correctness, emitted
 * unconditionally by the recompiler and defaulted in recomp.h.
 */

#ifndef PMS_TRACE_H
#define PMS_TRACE_H

#ifdef __cplusplus
extern "C" {
#endif

void pkmnstadium_trace_push(const char *name);

/* Text-draw discovery census (diagnostics.cpp). Armed by env PMS_TEXTPROBE=1;
 * a no-op (single bool check) otherwise. Records function entries whose args
 * match the string-draw signature so the PMS-J text-printf function can be
 * found empirically. ctx/rdram are in scope at every TRACE_ENTRY() call site
 * (they are the recompiled function's parameters). */
void pkmnstadium_textdraw_probe(const char *name, unsigned int ra,
                                unsigned int a0, unsigned int a1,
                                unsigned int a2, unsigned int a3);

/* Runtime English-translation hook (diagnostics.cpp). No-op for every function
 * except the string-draw printf; on a KV hit it repoints the fmt arg (ctx->r6)
 * at an English replacement so the original renders English. ctx/rdram are in
 * scope at every TRACE_ENTRY() call site (the recompiled fn's parameters).
 * Passed as void* / unsigned char* to keep this header dependency-free. */
void pkmnstadium_text_xlate(const char *name, unsigned char *rdram, void *ctx);

#ifdef __cplusplus
}
#endif

/* Engine emits these as bare `TRACE_ENTRY()` / `TRACE_RETURN()` without a
 * trailing `;` — bake the terminator into the macro. */
#define TRACE_ENTRY()                                                         \
    pkmnstadium_trace_push(__func__);                                         \
    pkmnstadium_textdraw_probe(__func__, (unsigned int)(ctx)->r31,            \
                               (unsigned int)(ctx)->r4, (unsigned int)(ctx)->r5, \
                               (unsigned int)(ctx)->r6, (unsigned int)(ctx)->r7); \
    pkmnstadium_text_xlate(__func__, (unsigned char*)(rdram), (void*)(ctx));
#define TRACE_RETURN()

#endif /* PMS_TRACE_H */
