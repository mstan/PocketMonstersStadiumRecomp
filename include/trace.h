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

#ifdef __cplusplus
}
#endif

/* Engine emits these as bare `TRACE_ENTRY()` / `TRACE_RETURN()` without a
 * trailing `;` — bake the terminator into the macro. */
#define TRACE_ENTRY()  pkmnstadium_trace_push(__func__);
#define TRACE_RETURN()

#endif /* PMS_TRACE_H */
