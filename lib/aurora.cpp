#include <aurora/aurora.h>

#ifdef AURORA_ENABLE_GX
#include "gfx/common.hpp"
#include "gx/fifo.hpp"
#include "imgui.hpp"
#include "webgpu/gpu.hpp"
#include <webgpu/webgpu_cpp.h>
#endif

#ifdef AURORA_ENABLE_RMLUI
#include "rmlui.hpp"
#endif

#include "input.hpp"
#include "internal.hpp"
#include "window.hpp"
#include "xr/xr.hpp"

#include <SDL3/SDL_filesystem.h>
#include <magic_enum.hpp>

#include "tracy/Tracy.hpp"

#include <imgui.h>

namespace aurora {
AuroraConfig g_config;
uint32_t g_sdlCustomEventsStart;
char g_gameName[4];

namespace {
Module Log("aurora");

#ifdef AURORA_ENABLE_GX
// GPU
using webgpu::g_device;
using webgpu::g_queue;
using webgpu::g_surface;

void draw_xr_sbs_debug_overlay(const std::array<xr::SbsMirrorEye, 2>& eyes) noexcept {
  ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowBgAlpha(0.65f);
  ImGui::SetNextWindowPos(ImVec2{8.0f, 8.0f}, ImGuiCond_Always);
  constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
  if (ImGui::Begin("XR SBS Preview Stats", nullptr, flags)) {
    ImGui::Text("XR SBS %.1f FPS", io.Framerate);
    ImGui::Text("%ux%u per eye", eyes[0].width, eyes[0].height);
  }
  ImGui::End();
}
#endif

#ifdef AURORA_ENABLE_GX
constexpr std::array PreferredBackendOrder{
#ifdef ENABLE_BACKEND_WEBGPU
    BACKEND_WEBGPU,
#endif
#ifdef DAWN_ENABLE_BACKEND_D3D12
    BACKEND_D3D12,
#endif
#ifdef DAWN_ENABLE_BACKEND_METAL
    BACKEND_METAL,
#endif
#ifdef DAWN_ENABLE_BACKEND_VULKAN
    BACKEND_VULKAN,
#endif
#ifdef DAWN_ENABLE_BACKEND_D3D11
    BACKEND_D3D11,
#endif
// #ifdef DAWN_ENABLE_BACKEND_DESKTOP_GL
//     BACKEND_OPENGL,
// #endif
// #ifdef DAWN_ENABLE_BACKEND_OPENGLES
//     BACKEND_OPENGLES,
// #endif
#ifdef DAWN_ENABLE_BACKEND_NULL
    BACKEND_NULL,
#endif
};
#else
constexpr std::array<AuroraBackend, 0> PreferredBackendOrder{};
#endif

bool g_initialFrame = false;

bool xr_startup_satisfied() noexcept {
  switch (xr::status()) {
  case AURORA_XR_READY:
  case AURORA_XR_ACTIVE:
    return true;
  default:
    return false;
  }
}

AuroraInfo initialize(int argc, char* argv[], const AuroraConfig& config) noexcept {
  g_config = config;
  Log.info("Aurora initializing");
  if (g_config.appName == nullptr) {
    g_config.appName = "Aurora";
  } else {
    g_config.appName = strdup(g_config.appName);
  }
  if (g_config.configPath == nullptr) {
    g_config.configPath = SDL_GetPrefPath(nullptr, g_config.appName);
  } else {
    g_config.configPath = strdup(g_config.configPath);
  }
  if (g_config.msaa == 0) {
    g_config.msaa = 1;
  }
  if (g_config.maxTextureAnisotropy == 0) {
    g_config.maxTextureAnisotropy = 16;
  }
  ASSERT(window::initialize(), "Error initializing window");

  g_sdlCustomEventsStart = SDL_RegisterEvents(2);
  ASSERT(g_sdlCustomEventsStart, "Failed to allocate user events: {}", SDL_GetError());
  ASSERT(window::initialize_event_watch(), "Error initializing SDL event watch");

#ifdef AURORA_ENABLE_GX
  /* Attempt to create a window using the calling application's desired backend */
  AuroraBackend selectedBackend = config.desiredBackend;
  bool windowCreated = false;
  const auto tryBackend = [&](AuroraBackend backend) {
    selectedBackend = backend;
    xr::prepare_dawn_openxr_vulkan_hooks(g_config, selectedBackend);
    if (!window::create_window(selectedBackend)) {
      xr::clear_dawn_openxr_vulkan_hooks();
      return false;
    }
    if (webgpu::initialize(selectedBackend)) {
      return true;
    }
    xr::clear_dawn_openxr_vulkan_hooks();
    window::destroy_window();
    return false;
  };

  if (g_config.enableOpenXR && selectedBackend == BACKEND_AUTO) {
    windowCreated = tryBackend(BACKEND_VULKAN);
    if (!windowCreated && g_config.requireOpenXR) {
      ASSERT(false, "OpenXR requires Vulkan, but Vulkan backend initialization failed");
    }
  } else if (selectedBackend != BACKEND_AUTO) {
    windowCreated = tryBackend(selectedBackend);
    if (!windowCreated && g_config.enableOpenXR && g_config.requireOpenXR && selectedBackend == BACKEND_VULKAN) {
      ASSERT(false, "OpenXR requires Vulkan, but Vulkan backend initialization failed");
    }
  }

  if (!windowCreated) {
    for (const auto backendType : PreferredBackendOrder) {
      if (backendType == selectedBackend) {
        continue;
      }
      if (tryBackend(backendType)) {
        windowCreated = true;
        break;
      }
    }
  }

  ASSERT(windowCreated, "Error creating window: {}", SDL_GetError());

  // Initialize SDL_Renderer for ImGui when we can't use a Dawn backend
  if (webgpu::g_backendType == wgpu::BackendType::Null) {
    ASSERT(window::create_renderer(), "Failed to initialize SDL renderer: {}", SDL_GetError());
  }
#else
  AuroraBackend selectedBackend = BACKEND_NULL;
  ASSERT(window::create_window(BACKEND_NULL), "Error creating window: {}", SDL_GetError());
  ASSERT(window::create_renderer(), "Failed to initialize SDL renderer: {}", SDL_GetError());
#endif

  xr::initialize(g_config, selectedBackend);
  if (g_config.requireOpenXR && !xr_startup_satisfied()) {
    ASSERT(false, "Required OpenXR initialization failed: {}", xr::status_message());
  }

  window::show_window();

#ifdef AURORA_ENABLE_GX
  gfx::initialize();

  imgui::create_context();
#endif
  const auto size = window::get_window_size();
  Log.info("Using framebuffer size {}x{} scale {}", size.fb_width, size.fb_height, size.scale);
#ifdef AURORA_ENABLE_GX
  if (g_config.imGuiInitCallback != nullptr) {
    g_config.imGuiInitCallback(&size);
  }
  imgui::initialize();
#endif

#ifdef AURORA_ENABLE_RMLUI
  rmlui::initialize(size);
#endif

  g_initialFrame = true;
  g_config.desiredBackend = selectedBackend;
  return {
      .backend = selectedBackend,
      .configPath = g_config.configPath,
      .window = window::get_sdl_window(),
      .windowSize = size,
  };
}

#ifdef AURORA_ENABLE_GX
wgpu::TextureView g_currentView;
#endif

void shutdown() noexcept {
#ifdef AURORA_ENABLE_RMLUI
  rmlui::shutdown();
#endif
  xr::shutdown();
#ifdef AURORA_ENABLE_GX
  g_currentView = {};
  imgui::shutdown();
  gfx::shutdown();
  webgpu::shutdown();
#endif
  input::shutdown();
  window::shutdown();
}

const AuroraEvent* update() noexcept {
  ZoneScoped;
  if (g_initialFrame) {
    g_initialFrame = false;
    input::initialize();
  }
  const bool waitWhenPaused = !xr::is_active() && !xr::should_render();
  return window::poll_events(waitWhenPaused);
}

bool begin_frame() noexcept {
  ZoneScoped;
  xr::on_aurora_frame_start();
  xr::begin_frame();
#ifdef AURORA_ENABLE_GX
  const bool allowHeadlessXrFrame = xr::is_active() || xr::should_render();
  {
    window::SurfaceLock surfaceLock;
    if (!window::is_presentable()) {
      if (!allowHeadlessXrFrame) {
        webgpu::release_surface();
        xr::end_frame_after_submit();
        return false;
      }
    } else if (window::is_paused()) {
      if (!allowHeadlessXrFrame) {
        xr::end_frame_after_submit();
        return false;
      }
    } else if (!g_surface) {
      webgpu::refresh_surface(true);
      if (!g_surface && !allowHeadlessXrFrame) {
        xr::end_frame_after_submit();
        return false;
      }
    }
    if (window::is_presentable() && !window::is_paused() && g_surface) {
      wgpu::SurfaceTexture surfaceTexture;
      g_surface.GetCurrentTexture(&surfaceTexture);
      switch (surfaceTexture.status) {
      case wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal:
        g_currentView = surfaceTexture.texture.CreateView();
        break;
      case wgpu::SurfaceGetCurrentTextureStatus::Timeout:
        Log.warn("Surface texture acquisition timed out");
        if (!allowHeadlessXrFrame) {
          xr::end_frame_after_submit();
          return false;
        }
        break;
      case wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal:
      case wgpu::SurfaceGetCurrentTextureStatus::Outdated:
        Log.info("Surface texture is {}, reconfiguring swapchain", magic_enum::enum_name(surfaceTexture.status));
        webgpu::refresh_surface(false);
        if (!allowHeadlessXrFrame) {
          xr::end_frame_after_submit();
          return false;
        }
        break;
      case wgpu::SurfaceGetCurrentTextureStatus::Lost:
        Log.warn("Surface texture is {}, releasing surface", magic_enum::enum_name(surfaceTexture.status));
        webgpu::release_surface();
        [[fallthrough]];
      case wgpu::SurfaceGetCurrentTextureStatus::Error:
        Log.warn("Surface texture is {}, dropping surface", magic_enum::enum_name(surfaceTexture.status));
        g_surface = {};
        if (!allowHeadlessXrFrame) {
          xr::end_frame_after_submit();
          return false;
        }
        break;
      default:
        Log.error("Failed to get surface texture: {}", magic_enum::enum_name(surfaceTexture.status));
        if (!allowHeadlessXrFrame) {
          xr::end_frame_after_submit();
          return false;
        }
        break;
      }
    }
  }

  imgui::new_frame(window::get_window_size());
  if (!gfx::begin_frame()) {
    g_currentView = {};
    xr::end_frame_after_submit();
    return false;
  }
#endif
  return true;
}

void end_frame() noexcept {
  ZoneScoped;
#ifdef AURORA_ENABLE_GX
  gx::fifo::drain();
  const auto encoderDescriptor = wgpu::CommandEncoderDescriptor{
      .label = "Redraw encoder",
  };
  auto encoder = g_device.CreateCommandEncoder(&encoderDescriptor);
  gfx::end_frame(encoder);
  gfx::render(encoder);
  if (xr::ensure_flat_ui_target()) {
    xr::FlatUiTarget flatUiTarget{};
    if (xr::get_flat_ui_target(flatUiTarget)) {
      const webgpu::Viewport flatUiViewport{
          .left = 0.f,
          .top = 0.f,
          .width = static_cast<float>(flatUiTarget.width),
          .height = static_cast<float>(flatUiTarget.height),
          .znear = 0.f,
          .zfar = 1.f,
      };
      wgpu::LoadOp overlayLoadOp = wgpu::LoadOp::Load;
    #if AURORA_ENABLE_RMLUI
      if (rmlui::is_initialized()) {
        const auto rmlOutput = rmlui::render(encoder, flatUiViewport);
        if (rmlOutput.texture != nullptr) {
          const std::array attachments{
              wgpu::RenderPassColorAttachment{
                  .view = flatUiTarget.view,
                  .loadOp = overlayLoadOp,
                  .storeOp = wgpu::StoreOp::Store,
              },
          };
          const wgpu::RenderPassDescriptor renderPassDescriptor{
              .label = "XR flat UI RmlUi composite pass",
              .colorAttachmentCount = attachments.size(),
              .colorAttachments = attachments.data(),
          };
          const auto pass = encoder.BeginRenderPass(&renderPassDescriptor);
          pass.SetPipeline(webgpu::g_CopyPipeline);
          pass.SetBindGroup(0, rmlOutput.copyBindGroup, 0, nullptr);
          pass.SetViewport(flatUiViewport.left, flatUiViewport.top, flatUiViewport.width, flatUiViewport.height,
                           flatUiViewport.znear, flatUiViewport.zfar);
          pass.Draw(3);
          pass.End();
          overlayLoadOp = wgpu::LoadOp::Load;
        }
      }
    #endif
      {
        const std::array attachments{
            wgpu::RenderPassColorAttachment{
                .view = flatUiTarget.view,
                .loadOp = overlayLoadOp,
                .storeOp = wgpu::StoreOp::Store,
            },
        };
        const wgpu::RenderPassDescriptor renderPassDescriptor{
            .label = "XR flat UI ImGui render pass",
            .colorAttachmentCount = attachments.size(),
            .colorAttachments = attachments.data(),
        };
        const auto pass = encoder.BeginRenderPass(&renderPassDescriptor);
        pass.SetViewport(flatUiViewport.left, flatUiViewport.top, flatUiViewport.width, flatUiViewport.height,
                         flatUiViewport.znear, flatUiViewport.zfar);
        imgui::render(pass);
        pass.End();
      }
    }
  }
  {
    window::SurfaceLock surfaceLock;
    if (window::is_presentable() && g_surface && g_currentView) {
      const auto& presentSource = webgpu::present_source();
      auto viewport = webgpu::calculate_present_viewport(webgpu::g_graphicsConfig.surfaceConfiguration.width,
                                                         webgpu::g_graphicsConfig.surfaceConfiguration.height,
                                                         presentSource.size.width, presentSource.size.height);
      wgpu::BindGroup presentBindGroup = webgpu::g_CopyBindGroup;
      std::array<xr::SbsMirrorEye, 2> sbsMirrorEyes{};
      const bool useXrSbsMirror = xr::get_sbs_mirror_eyes(sbsMirrorEyes);
      xr::SbsMirrorEye xrMirrorEye{};
      xr::FlatUiTarget xrMirrorFlatUi{};
      const bool useXrDefaultMirror =
          !useXrSbsMirror && xr::get_default_mirror_eye(xrMirrorEye) && xr::get_flat_ui_target(xrMirrorFlatUi);
    #if AURORA_ENABLE_RMLUI
      if (!useXrSbsMirror && !useXrDefaultMirror && rmlui::is_initialized()) {
        const auto rmlOutput = rmlui::render(encoder, viewport);
        if (rmlOutput.texture != nullptr) {
          presentBindGroup = rmlOutput.copyBindGroup;
        }
      }
    #endif
      {
        const std::array attachments{
            wgpu::RenderPassColorAttachment{
                .view = g_currentView,
                .loadOp = wgpu::LoadOp::Clear,
                .storeOp = wgpu::StoreOp::Store,
            },
        };
        const wgpu::RenderPassDescriptor renderPassDescriptor{
            .label = "EFB copy render pass",
            .colorAttachmentCount = attachments.size(),
            .colorAttachments = attachments.data(),
        };
        const auto pass = encoder.BeginRenderPass(&renderPassDescriptor);
        // Copy EFB -> XFB (swapchain)
        pass.SetPipeline(webgpu::g_CopyPipeline);
        if (useXrDefaultMirror) {
          const auto eyeViewport =
              webgpu::calculate_present_viewport(webgpu::g_graphicsConfig.surfaceConfiguration.width,
                                                 webgpu::g_graphicsConfig.surfaceConfiguration.height,
                                                 xrMirrorEye.width, xrMirrorEye.height);
          pass.SetBindGroup(0, xrMirrorEye.bindGroup, 0, nullptr);
          pass.SetViewport(eyeViewport.left, eyeViewport.top, eyeViewport.width, eyeViewport.height,
                           eyeViewport.znear, eyeViewport.zfar);
          pass.Draw(3);
          if (xrMirrorFlatUi.bindGroup != nullptr) {
            pass.SetPipeline(webgpu::g_AlphaBlendCopyPipeline);
            pass.SetBindGroup(0, xrMirrorFlatUi.bindGroup, 0, nullptr);
            pass.Draw(3);
          }
        } else if (useXrSbsMirror) {
          const float surfaceWidth = static_cast<float>(webgpu::g_graphicsConfig.surfaceConfiguration.width);
          const float halfWidth = surfaceWidth * 0.5f;
          for (size_t i = 0; i < sbsMirrorEyes.size(); ++i) {
            const auto eyeViewport =
                webgpu::calculate_present_viewport(static_cast<uint32_t>(halfWidth),
                                                   webgpu::g_graphicsConfig.surfaceConfiguration.height,
                                                   sbsMirrorEyes[i].width, sbsMirrorEyes[i].height);
            pass.SetBindGroup(0, sbsMirrorEyes[i].bindGroup, 0, nullptr);
            pass.SetViewport((i == 0 ? 0.0f : halfWidth) + eyeViewport.left, eyeViewport.top, eyeViewport.width,
                             eyeViewport.height, 0.f, 1.f);
            pass.Draw(3);
          }
        } else {
          pass.SetBindGroup(0, presentBindGroup, 0, nullptr);
          pass.SetViewport(viewport.left, viewport.top, viewport.width, viewport.height, viewport.znear, viewport.zfar);
          pass.Draw(3);
        }
        pass.End();
      }
      {
        if (useXrSbsMirror) {
          draw_xr_sbs_debug_overlay(sbsMirrorEyes);
        }
        const std::array attachments{
            wgpu::RenderPassColorAttachment{
                .view = g_currentView,
                .loadOp = wgpu::LoadOp::Load,
                .storeOp = wgpu::StoreOp::Store,
            },
        };
        const wgpu::RenderPassDescriptor renderPassDescriptor{
            .label = "ImGui render pass",
            .colorAttachmentCount = attachments.size(),
            .colorAttachments = attachments.data(),
        };
        const auto pass = encoder.BeginRenderPass(&renderPassDescriptor);
        pass.SetViewport(0.f, 0.f, static_cast<float>(webgpu::g_graphicsConfig.surfaceConfiguration.width),
                         static_cast<float>(webgpu::g_graphicsConfig.surfaceConfiguration.height), 0.f, 1.f);
        imgui::render(pass);
        pass.End();
      }
    } else {
      Log.info("Skipping present; window not presentable");
      if (!xr::is_active() && !xr::should_render()) {
        webgpu::release_surface();
      }
    }
    const wgpu::CommandBufferDescriptor cmdBufDescriptor{.label = "Redraw command buffer"};
    const auto buffer = encoder.Finish(&cmdBufDescriptor);
    g_queue.Submit(1, &buffer);
    gfx::after_submit();
    xr::end_frame_after_submit();
    if (window::is_presentable() && g_surface) {
      auto presentStatus = g_surface.Present();
      if (presentStatus != wgpu::Status::Success) {
        Log.warn("Surface present failed: {}", static_cast<int>(presentStatus));
        webgpu::release_surface();
      }
    } else if (g_surface) {
      webgpu::release_surface();
    }
    g_currentView = {};
  }

  TracyPlotConfig("aurora: lastVertSize", tracy::PlotFormatType::Memory, false, true, 0);
  TracyPlotConfig("aurora: lastUniformSize", tracy::PlotFormatType::Memory, false, true, 0);
  TracyPlotConfig("aurora: lastIndexSize", tracy::PlotFormatType::Memory, false, true, 0);
  TracyPlotConfig("aurora: lastStorageSize", tracy::PlotFormatType::Memory, false, true, 0);
  TracyPlotConfig("aurora: lastTextureUploadSize", tracy::PlotFormatType::Memory, false, true, 0);

  TracyPlot("aurora: queuedPipelines", static_cast<int64_t>(gfx::g_stats.queuedPipelines));
  TracyPlot("aurora: createdPipelines", static_cast<int64_t>(gfx::g_stats.createdPipelines));
  TracyPlot("aurora: drawCallCount", static_cast<int64_t>(gfx::g_stats.drawCallCount));
  TracyPlot("aurora: mergedDrawCallCount", static_cast<int64_t>(gfx::g_stats.mergedDrawCallCount));
  TracyPlot("aurora: lastVertSize", static_cast<int64_t>(gfx::g_stats.lastVertSize));
  TracyPlot("aurora: lastUniformSize", static_cast<int64_t>(gfx::g_stats.lastUniformSize));
  TracyPlot("aurora: lastIndexSize", static_cast<int64_t>(gfx::g_stats.lastIndexSize));
  TracyPlot("aurora: lastStorageSize", static_cast<int64_t>(gfx::g_stats.lastStorageSize));
  TracyPlot("aurora: lastTextureUploadSize", static_cast<int64_t>(gfx::g_stats.lastTextureUploadSize));

#endif
}
} // namespace
} // namespace aurora

// C API bindings
AuroraInfo aurora_initialize(int argc, char* argv[], const AuroraConfig* config) {
  return aurora::initialize(argc, argv, *config);
}
void aurora_shutdown() { aurora::shutdown(); }
const AuroraEvent* aurora_update() { return aurora::update(); }
bool aurora_begin_frame() { return aurora::begin_frame(); }
void aurora_end_frame() { aurora::end_frame(); }
AuroraBackend aurora_get_backend() { return aurora::g_config.desiredBackend; }
const AuroraBackend* aurora_get_available_backends(size_t* count) {
  if (count != nullptr) {
    *count = aurora::PreferredBackendOrder.size();
  }
  return aurora::PreferredBackendOrder.data();
}
void aurora_set_log_level(AuroraLogLevel level) { aurora::g_config.logLevel = level; }
void aurora_set_pause_on_focus_lost(bool value) { aurora::g_config.pauseOnFocusLost = value; }
void aurora_set_background_input(bool value) {
  aurora::g_config.allowJoystickBackgroundEvents = value;
  aurora::window::set_background_input(value);
}
void aurora_debug_set_surface_ready(bool ready) { aurora::window::set_surface_ready(ready); }
