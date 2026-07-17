/*
 * rt64_render_context.cpp — PocketMonstersStadiumRecomp's RT64 wrapper.
 *
 * Mirrors PokemonStadiumRecomp's rt64_render_context.cpp, stripped of the
 * SS Anne RmlUi overlay seam and the
 * TCP debug-server counters — this is the minimal boot-to-menu renderer. The
 * RT64 application setup and the hardcoded supersampling/MSAA config (verified
 * good on the sibling PSR project) are kept verbatim; env knobs are renamed to
 * the PMS_RT64_* namespace.
 */

// NOMINMAX must be defined before any include that pulls in Windows.h (RT64
// headers do, transitively) or std::max/std::min break.
#define NOMINMAX

#include <memory>
#include <cstring>
#include <algorithm>
#include <cassert>
#include <cstdlib>

#define HLSL_CPU
#include "hle/rt64_application.h"

#include "ultramodern/ultramodern.hpp"
#include "ultramodern/config.hpp"
#include "librecomp/rdp.hpp"

#include "pms_render.h"

static bool sample_positions_supported = false;
static RT64::UserConfiguration::Antialiasing device_max_msaa = RT64::UserConfiguration::Antialiasing::None;
static bool high_precision_fb_enabled = false;

static uint8_t DMEM[0x1000];
static uint8_t IMEM[0x1000];

unsigned int MI_INTR_REG = 0;

static void dummy_check_interrupts() {}

static RT64::UserConfiguration::Antialiasing compute_max_supported_aa(RT64::RenderSampleCounts bits) {
    if (bits & RT64::RenderSampleCount::Bits::COUNT_2) {
        if (bits & RT64::RenderSampleCount::Bits::COUNT_4) {
            if (bits & RT64::RenderSampleCount::Bits::COUNT_8) {
                return RT64::UserConfiguration::Antialiasing::MSAA8X;
            }
            return RT64::UserConfiguration::Antialiasing::MSAA4X;
        }
        return RT64::UserConfiguration::Antialiasing::MSAA2X;
    }
    return RT64::UserConfiguration::Antialiasing::None;
}

static RT64::UserConfiguration::AspectRatio to_rt64(ultramodern::renderer::AspectRatio option) {
    switch (option) {
        case ultramodern::renderer::AspectRatio::Original:    return RT64::UserConfiguration::AspectRatio::Original;
        case ultramodern::renderer::AspectRatio::Expand:      return RT64::UserConfiguration::AspectRatio::Expand;
        case ultramodern::renderer::AspectRatio::Manual:      return RT64::UserConfiguration::AspectRatio::Manual;
        case ultramodern::renderer::AspectRatio::OptionCount: return RT64::UserConfiguration::AspectRatio::OptionCount;
    }
    return RT64::UserConfiguration::AspectRatio::Original;
}

static RT64::UserConfiguration::RefreshRate to_rt64(ultramodern::renderer::RefreshRate option) {
    switch (option) {
        case ultramodern::renderer::RefreshRate::Original:    return RT64::UserConfiguration::RefreshRate::Original;
        case ultramodern::renderer::RefreshRate::Display:     return RT64::UserConfiguration::RefreshRate::Display;
        case ultramodern::renderer::RefreshRate::Manual:      return RT64::UserConfiguration::RefreshRate::Manual;
        case ultramodern::renderer::RefreshRate::OptionCount: return RT64::UserConfiguration::RefreshRate::OptionCount;
    }
    return RT64::UserConfiguration::RefreshRate::Original;
}

static RT64::UserConfiguration::InternalColorFormat to_rt64(ultramodern::renderer::HighPrecisionFramebuffer option) {
    switch (option) {
        case ultramodern::renderer::HighPrecisionFramebuffer::Off:         return RT64::UserConfiguration::InternalColorFormat::Standard;
        case ultramodern::renderer::HighPrecisionFramebuffer::On:          return RT64::UserConfiguration::InternalColorFormat::High;
        case ultramodern::renderer::HighPrecisionFramebuffer::Auto:        return RT64::UserConfiguration::InternalColorFormat::Automatic;
        case ultramodern::renderer::HighPrecisionFramebuffer::OptionCount: return RT64::UserConfiguration::InternalColorFormat::OptionCount;
    }
    return RT64::UserConfiguration::InternalColorFormat::Automatic;
}

static void set_application_user_config(RT64::Application* application,
                                        const ultramodern::renderer::GraphicsConfig& config) {
    // Supersampling is hardcoded (not config-driven): render at 6x the N64 base
    // and box-filter down by 2 for 2x2 SSAA. Verified on PSR to clear the
    // sub-pixel speckle on N64 models; Manual mode is the path the uniform
    // framebuffer-scale fix keeps 2D menus correct under.
    application->userConfig.resolution = RT64::UserConfiguration::Resolution::Manual;
    application->userConfig.resolutionMultiplier = 6.0;
    application->userConfig.downsampleMultiplier = 2;

    switch (config.hr_option) {
        default:
        case ultramodern::renderer::HUDRatioMode::Original:
            application->userConfig.extAspectRatio = RT64::UserConfiguration::AspectRatio::Original;
            break;
        case ultramodern::renderer::HUDRatioMode::Clamp16x9:
            application->userConfig.extAspectRatio = RT64::UserConfiguration::AspectRatio::Manual;
            application->userConfig.extAspectTarget = 16.0/9.0;
            break;
        case ultramodern::renderer::HUDRatioMode::Full:
            application->userConfig.extAspectRatio = RT64::UserConfiguration::AspectRatio::Expand;
            break;
    }

    application->userConfig.aspectRatio = to_rt64(config.ar_option);
    // 4x MSAA hardcoded; the models alias badly without it. PMS_RT64_MSAA below
    // can still override for A/B testing or weaker GPUs.
    application->userConfig.antialiasing = RT64::UserConfiguration::Antialiasing::MSAA4X;
    application->userConfig.refreshRate = to_rt64(config.rr_option);
    application->userConfig.refreshRateTarget = config.rr_manual_value;
    application->userConfig.internalColorFormat = to_rt64(config.hpfb_option);
    application->userConfig.displayBuffering = RT64::UserConfiguration::DisplayBuffering::Triple;
    // True bilinear; three-point filtering leaks color across clamped 2D panels.
    application->userConfig.threePointFiltering = false;

    if (const char* r = std::getenv("PMS_RT64_RES_MULT")) {
        char* end = nullptr;
        double m = std::strtod(r, &end);
        if (end != r && m >= 1.0 && m <= 16.0) {
            application->userConfig.resolution = RT64::UserConfiguration::Resolution::Manual;
            application->userConfig.resolutionMultiplier = m;
            application->userConfig.downsampleMultiplier = 1;
            std::fprintf(stderr, "[pms-rt64] forced resolutionMultiplier = %.2f, downsample = 1\n", m);
        }
    }
    if (const char* d = std::getenv("PMS_RT64_DOWNSAMPLE")) {
        int dm = std::atoi(d);
        if (dm >= 1 && dm <= 8) {
            application->userConfig.downsampleMultiplier = dm;
            std::fprintf(stderr, "[pms-rt64] forced downsampleMultiplier = %d\n", dm);
        }
    }
    if (const char* aa = std::getenv("PMS_RT64_MSAA")) {
        using AA = RT64::UserConfiguration::Antialiasing;
        if (!std::strcmp(aa, "None") || !std::strcmp(aa, "0"))      application->userConfig.antialiasing = AA::None;
        else if (!std::strcmp(aa, "MSAA2X") || !std::strcmp(aa, "2")) application->userConfig.antialiasing = AA::MSAA2X;
        else if (!std::strcmp(aa, "MSAA4X") || !std::strcmp(aa, "4")) application->userConfig.antialiasing = AA::MSAA4X;
        else if (!std::strcmp(aa, "MSAA8X") || !std::strcmp(aa, "8")) application->userConfig.antialiasing = AA::MSAA8X;
        std::fprintf(stderr, "[pms-rt64] forced antialiasing via PMS_RT64_MSAA='%s'\n", aa);
    }
}

static ultramodern::renderer::SetupResult map_setup_result(RT64::Application::SetupResult rt64_result) {
    switch (rt64_result) {
        case RT64::Application::SetupResult::Success:                  return ultramodern::renderer::SetupResult::Success;
        case RT64::Application::SetupResult::DynamicLibrariesNotFound: return ultramodern::renderer::SetupResult::DynamicLibrariesNotFound;
        case RT64::Application::SetupResult::InvalidGraphicsAPI:       return ultramodern::renderer::SetupResult::InvalidGraphicsAPI;
        case RT64::Application::SetupResult::GraphicsAPINotFound:      return ultramodern::renderer::SetupResult::GraphicsAPINotFound;
        case RT64::Application::SetupResult::GraphicsDeviceNotFound:   return ultramodern::renderer::SetupResult::GraphicsDeviceNotFound;
    }
    fprintf(stderr, "Unhandled `RT64::Application::SetupResult` ?\n");
    assert(false);
    std::exit(EXIT_FAILURE);
}

static ultramodern::renderer::GraphicsApi map_graphics_api(RT64::UserConfiguration::GraphicsAPI api) {
    switch (api) {
        case RT64::UserConfiguration::GraphicsAPI::D3D12:     return ultramodern::renderer::GraphicsApi::D3D12;
        case RT64::UserConfiguration::GraphicsAPI::Vulkan:    return ultramodern::renderer::GraphicsApi::Vulkan;
        case RT64::UserConfiguration::GraphicsAPI::Metal:     return ultramodern::renderer::GraphicsApi::Metal;
        case RT64::UserConfiguration::GraphicsAPI::Automatic: return ultramodern::renderer::GraphicsApi::Auto;
    }
    fprintf(stderr, "Unhandled `RT64::UserConfiguration::GraphicsAPI` ?\n");
    assert(false);
    std::exit(EXIT_FAILURE);
}

pms::renderer::RT64Context::RT64Context(uint8_t* rdram,
                                        ultramodern::renderer::WindowHandle window_handle,
                                        bool debug) {
    static unsigned char dummy_rom_header[0x40];

    RT64::Application::Core appCore{};
#if defined(_WIN32)
    appCore.window = window_handle.window;
#elif defined(__linux__) || defined(__ANDROID__)
    appCore.window = window_handle;
#elif defined(__APPLE__)
    appCore.window.window = window_handle.window;
    appCore.window.view = window_handle.view;
#endif

    appCore.checkInterrupts = dummy_check_interrupts;

    appCore.HEADER = dummy_rom_header;
    appCore.RDRAM = rdram;
    appCore.DMEM = DMEM;
    appCore.IMEM = IMEM;

    appCore.MI_INTR_REG = &MI_INTR_REG;

    auto& dpc_regs = recomp::rdp::dp_registers();
    appCore.DPC_START_REG = &dpc_regs.start;
    appCore.DPC_END_REG = &dpc_regs.end;
    appCore.DPC_CURRENT_REG = &dpc_regs.current;
    appCore.DPC_STATUS_REG = &dpc_regs.status;
    appCore.DPC_CLOCK_REG = &dpc_regs.clock;
    appCore.DPC_BUFBUSY_REG = &dpc_regs.bufbusy;
    appCore.DPC_PIPEBUSY_REG = &dpc_regs.pipebusy;
    appCore.DPC_TMEM_REG = &dpc_regs.tmem;

    ultramodern::renderer::ViRegs* vi_regs = ultramodern::renderer::get_vi_regs();
    appCore.VI_STATUS_REG         = &vi_regs->VI_STATUS_REG;
    appCore.VI_ORIGIN_REG         = &vi_regs->VI_ORIGIN_REG;
    appCore.VI_WIDTH_REG          = &vi_regs->VI_WIDTH_REG;
    appCore.VI_INTR_REG           = &vi_regs->VI_INTR_REG;
    appCore.VI_V_CURRENT_LINE_REG = &vi_regs->VI_V_CURRENT_LINE_REG;
    appCore.VI_TIMING_REG         = &vi_regs->VI_TIMING_REG;
    appCore.VI_V_SYNC_REG         = &vi_regs->VI_V_SYNC_REG;
    appCore.VI_H_SYNC_REG         = &vi_regs->VI_H_SYNC_REG;
    appCore.VI_LEAP_REG           = &vi_regs->VI_LEAP_REG;
    appCore.VI_H_START_REG        = &vi_regs->VI_H_START_REG;
    appCore.VI_V_START_REG        = &vi_regs->VI_V_START_REG;
    appCore.VI_V_BURST_REG        = &vi_regs->VI_V_BURST_REG;
    appCore.VI_X_SCALE_REG        = &vi_regs->VI_X_SCALE_REG;
    appCore.VI_Y_SCALE_REG        = &vi_regs->VI_Y_SCALE_REG;

    RT64::ApplicationConfiguration appConfig;
    appConfig.useConfigurationFile = false;

    std::fprintf(stderr, "[pms-rt64] constructing RT64::Application\n"); std::fflush(stderr);
    app = std::make_unique<RT64::Application>(appCore, appConfig);
    std::fprintf(stderr, "[pms-rt64] RT64::Application constructed\n"); std::fflush(stderr);

    auto& cur_config = ultramodern::renderer::get_graphics_config();
    set_application_user_config(app.get(), cur_config);
    app->userConfig.developerMode = debug;
    // Force gbi depth branches to prevent LODs from kicking in.
    app->enhancementConfig.f3dex.forceBranch = true;
    // Scale LODs based on the output resolution.
    app->enhancementConfig.textureLOD.scale = true;

    switch (cur_config.api_option) {
        case ultramodern::renderer::GraphicsApi::D3D12:
            app->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::D3D12; break;
        case ultramodern::renderer::GraphicsApi::Vulkan:
            app->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::Vulkan; break;
        case ultramodern::renderer::GraphicsApi::Metal:
            app->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::Metal; break;
        case ultramodern::renderer::GraphicsApi::Auto:
            app->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::Automatic; break;
    }

    uint32_t thread_id = 0;
#ifdef _WIN32
    thread_id = window_handle.thread_id;
#endif
    // The launcher is now a separate out-of-process executable (pmsj-launcher,
    // src/launcher/pmsj_launcher_main.cpp): it runs pre-boot in its own SDL/GL
    // window, commits launcher.cfg, and spawns this game exe. No RmlUi overlay is
    // registered on RT64's render hooks anymore — the game boots straight from
    // launcher.cfg.

    std::fprintf(stderr, "[pms-rt64] app->setup(thread_id=%u) ...\n", thread_id); std::fflush(stderr);
    setup_result = map_setup_result(app->setup(thread_id));
    chosen_api = map_graphics_api(app->chosenGraphicsAPI);
    std::fprintf(stderr, "[pms-rt64] app->setup -> result=%d api=%d\n",
                 (int)setup_result, (int)chosen_api); std::fflush(stderr);
    if (setup_result != ultramodern::renderer::SetupResult::Success) {
        std::fprintf(stderr, "[pms-rt64] SETUP FAILED — renderer context invalid\n"); std::fflush(stderr);
        app = nullptr;
        return;
    }

    app->setFullScreen(cur_config.wm_option == ultramodern::renderer::WindowMode::Fullscreen);

    if (app->device->getCapabilities().sampleLocations) {
        RT64::RenderSampleCounts color_sample_counts = app->device->getSampleCountsSupported(RT64::RenderFormat::R8G8B8A8_UNORM);
        RT64::RenderSampleCounts depth_sample_counts = app->device->getSampleCountsSupported(RT64::RenderFormat::D32_FLOAT);
        RT64::RenderSampleCounts common_sample_counts = color_sample_counts & depth_sample_counts;
        device_max_msaa = compute_max_supported_aa(common_sample_counts);
        sample_positions_supported = true;
    }
    else {
        device_max_msaa = RT64::UserConfiguration::Antialiasing::None;
        sample_positions_supported = false;
    }

    high_precision_fb_enabled = app->shaderLibrary->usesHDR;
    std::fprintf(stderr, "[pms-rt64] RT64Context ctor complete (max_msaa=%d hdr=%d)\n",
                 (int)device_max_msaa, (int)high_precision_fb_enabled); std::fflush(stderr);
}

pms::renderer::RT64Context::~RT64Context() = default;

void pms::renderer::RT64Context::send_dl(const OSTask* task) {
    static bool first = true;
    if (first) { first = false; std::fprintf(stderr, "[pms-rt64] first send_dl (task type=%u)\n", (unsigned)task->t.type); std::fflush(stderr); }
    app->state->rsp->reset();
    app->interpreter->loadUCodeGBI(task->t.ucode & 0x3FFFFFF, task->t.ucode_data & 0x3FFFFFF, true);
    app->processDisplayLists(app->core.RDRAM, task->t.data_ptr & 0x3FFFFFF, 0, true);
}

void pms::renderer::RT64Context::update_screen() {
    static bool first = true;
    const bool log = first;
    if (first) { first = false; std::fprintf(stderr, "[pms-rt64] first update_screen ...\n"); std::fflush(stderr); }
    app->updateScreen();
    if (log) { std::fprintf(stderr, "[pms-rt64] first update_screen returned\n"); std::fflush(stderr); }
}

void pms::renderer::RT64Context::shutdown() {
    if (app != nullptr) {
        app->end();
    }
}

bool pms::renderer::RT64Context::update_config(
        const ultramodern::renderer::GraphicsConfig& old_config,
        const ultramodern::renderer::GraphicsConfig& new_config) {
    if (old_config == new_config) {
        return false;
    }
    if (new_config.wm_option != old_config.wm_option) {
        app->setFullScreen(new_config.wm_option == ultramodern::renderer::WindowMode::Fullscreen);
    }
    set_application_user_config(app.get(), new_config);
    app->updateUserConfig(true);
    if (new_config.msaa_option != old_config.msaa_option) {
        app->updateMultisampling();
    }
    return true;
}

void pms::renderer::RT64Context::enable_instant_present() {
    app->enhancementConfig.presentation.mode = RT64::EnhancementConfiguration::Presentation::Mode::PresentEarly;
    app->updateEnhancementConfig();
}

uint32_t pms::renderer::RT64Context::get_display_framerate() const {
    return app->presentQueue->ext.sharedResources->swapChainRate;
}

float pms::renderer::RT64Context::get_resolution_scale() const {
    constexpr int ReferenceHeight = 240;
    switch (app->userConfig.resolution) {
        case RT64::UserConfiguration::Resolution::WindowIntegerScale:
            if (app->sharedQueueResources->swapChainHeight > 0) {
                return std::max(float((app->sharedQueueResources->swapChainHeight + ReferenceHeight - 1) / ReferenceHeight), 1.0f);
            }
            return 1.0f;
        case RT64::UserConfiguration::Resolution::Manual:
            return float(app->userConfig.resolutionMultiplier);
        case RT64::UserConfiguration::Resolution::Original:
        default:
            return 1.0f;
    }
}

std::unique_ptr<ultramodern::renderer::RendererContext>
pms::renderer::create_render_context(uint8_t* rdram,
                                     ultramodern::renderer::WindowHandle window_handle,
                                     bool developer_mode) {
    std::fprintf(stderr, "[pms-rt64] create_render_context(developer_mode=%d)\n", (int)developer_mode);
    std::fflush(stderr);
    return std::make_unique<pms::renderer::RT64Context>(rdram, window_handle, developer_mode);
}
