/*
 * debug_server.h - minimal TCP debug server for PocketMonstersStadiumRecomp.
 */

#ifndef PMS_DEBUG_SERVER_H
#define PMS_DEBUG_SERVER_H

#include <atomic>
#include <cstdint>

namespace pms::dbg {

void start(int port = 4372);
void shutdown();

extern std::atomic<bool>     g_fast_forward;
extern std::atomic<uint64_t> g_vi_ticks;
extern std::atomic<uint64_t> g_frame_count;

extern std::atomic<bool>     g_input_override_active;
extern std::atomic<uint16_t> g_buttons_override;
extern std::atomic<int>      g_stick_x_override;
extern std::atomic<int>      g_stick_y_override;

extern std::atomic<float>    g_audio_volume;

} // namespace pms::dbg

#endif
