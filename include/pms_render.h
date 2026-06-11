/*
 * pms_render.h — RT64-backed RendererContext for PocketMonstersStadiumRecomp.
 *
 * Mirrors PokemonStadiumRecomp's pokestadium_render.h (which is itself
 * adapted from Zelda64Recomp's zelda_render.h). No texture-pack / mod-loader
 * machinery and no recompui dependency — just the RT64 context the runtime
 * needs to present frames.
 */

#ifndef PMS_RENDER_H
#define PMS_RENDER_H

#include <memory>

#include "ultramodern/renderer_context.hpp"

namespace RT64 {
    struct Application;
}

namespace pms {
namespace renderer {

class RT64Context final : public ultramodern::renderer::RendererContext {
public:
    RT64Context(uint8_t* rdram, ultramodern::renderer::WindowHandle window_handle, bool debug);
    ~RT64Context() override;

    bool valid() override { return app != nullptr; }

    bool update_config(const ultramodern::renderer::GraphicsConfig& old_config,
                       const ultramodern::renderer::GraphicsConfig& new_config) override;
    void enable_instant_present() override;
    void send_dl(const OSTask* task) override;
    void update_screen() override;
    void shutdown() override;
    uint32_t get_display_framerate() const override;
    float get_resolution_scale() const override;

private:
    std::unique_ptr<RT64::Application> app;
};

std::unique_ptr<ultramodern::renderer::RendererContext>
create_render_context(uint8_t* rdram,
                      ultramodern::renderer::WindowHandle window_handle,
                      bool developer_mode);

} // namespace renderer
} // namespace pms

#endif
