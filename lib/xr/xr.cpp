#include "xr.hpp"

#include "../logging.hpp"

#include <algorithm>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace aurora::xr {
namespace {
Module Log("aurora::xr");

struct State {
  bool requested = false;
  bool required = false;
  AuroraXRStatus status = AURORA_XR_DISABLED;
  std::string statusMessage = "OpenXR disabled";
  AuroraXRFrameState frameState{};
  std::vector<AuroraXRView> views;
  bool eyeActive = false;
  uint32_t activeEyeIndex = std::numeric_limits<uint32_t>::max();
  bool flatUiActive = false;
};

State g_state;

const char* status_name(AuroraXRStatus status) noexcept {
  switch (status) {
  case AURORA_XR_DISABLED:
    return "disabled";
  case AURORA_XR_UNAVAILABLE:
    return "unavailable";
  case AURORA_XR_BLOCKED:
    return "blocked";
  case AURORA_XR_READY:
    return "ready";
  case AURORA_XR_ACTIVE:
    return "active";
  case AURORA_XR_LOST:
    return "lost";
  default:
    return "unknown";
  }
}

void set_status(AuroraXRStatus status, std::string message) noexcept {
  g_state.status = status;
  g_state.statusMessage = std::move(message);
  g_state.frameState.status = status;
  g_state.frameState.active = status == AURORA_XR_ACTIVE;
  if (status != AURORA_XR_ACTIVE) {
    g_state.frameState.shouldRender = false;
  }
}

void sync_frame_state() noexcept {
  g_state.frameState.status = g_state.status;
  g_state.frameState.requested = g_state.requested;
  g_state.frameState.active = g_state.status == AURORA_XR_ACTIVE;
  g_state.frameState.viewCount = static_cast<uint32_t>(g_state.views.size());
  if (!g_state.frameState.active) {
    g_state.frameState.shouldRender = false;
  }
}
} // namespace

void initialize(const AuroraConfig& config, AuroraBackend selectedBackend) noexcept {
  g_state = {};
  g_state.requested = config.enableOpenXR;
  g_state.required = config.requireOpenXR;
  g_state.frameState.requested = g_state.requested;

  if (!g_state.requested) {
    set_status(AURORA_XR_DISABLED, "OpenXR disabled");
    sync_frame_state();
    return;
  }

  if (selectedBackend != BACKEND_VULKAN) {
    set_status(AURORA_XR_BLOCKED, "OpenXR desktop launch requires BACKEND_VULKAN; selected backend is not Vulkan");
    Log.warn("OpenXR requested but blocked: {}", g_state.statusMessage);
    sync_frame_state();
    return;
  }

#ifdef AURORA_HAS_OPENXR
  ProbeResult result = probe_openxr(selectedBackend);
  g_state.views = std::move(result.views);
  set_status(result.status, std::move(result.message));
#else
  set_status(AURORA_XR_UNAVAILABLE,
             "Aurora was built without OpenXR SDK support; configure with AURORA_ENABLE_OPENXR=ON and an OpenXR loader SDK");
#endif

  sync_frame_state();
  Log.report(g_state.status == AURORA_XR_UNAVAILABLE ? LOG_WARNING : LOG_INFO, "OpenXR {}: {}",
             status_name(g_state.status), g_state.statusMessage);
}

void shutdown() noexcept { g_state = {}; }

void on_aurora_frame_start() noexcept {
  g_state.eyeActive = false;
  g_state.activeEyeIndex = std::numeric_limits<uint32_t>::max();
  g_state.flatUiActive = false;
  g_state.frameState.shouldRender = false;
  sync_frame_state();
}

void begin_frame() noexcept {
  if (g_state.requested) {
    ++g_state.frameState.frameIndex;
  }
  // Until Aurora has a native Vulkan/OpenXR graphics bridge, XR never owns render targets.
  g_state.frameState.shouldRender = false;
  sync_frame_state();
}

void end_frame_after_submit() noexcept {
  g_state.eyeActive = false;
  g_state.activeEyeIndex = std::numeric_limits<uint32_t>::max();
  g_state.flatUiActive = false;
  sync_frame_state();
}

AuroraXRStatus status() noexcept { return g_state.status; }

const char* status_message() noexcept { return g_state.statusMessage.c_str(); }

bool is_requested() noexcept { return g_state.requested; }

bool is_active() noexcept { return g_state.status == AURORA_XR_ACTIVE; }

bool should_render() noexcept { return is_active() && g_state.frameState.shouldRender; }

AuroraXRFrameState frame_state() noexcept {
  sync_frame_state();
  return g_state.frameState;
}

uint32_t view_count() noexcept { return static_cast<uint32_t>(g_state.views.size()); }

bool get_view(uint32_t index, AuroraXRView* outView) noexcept {
  if (outView == nullptr) {
    return false;
  }
  *outView = {};
  if (index >= g_state.views.size()) {
    return false;
  }
  *outView = g_state.views[index];
  return true;
}

bool begin_eye(uint32_t eyeIndex) noexcept {
  if (!should_render() || eyeIndex >= g_state.views.size() || g_state.eyeActive || g_state.flatUiActive) {
    return false;
  }
  // Future implementation: acquire an OpenXR eye swapchain image and redirect GX/EFB rendering.
  return false;
}

void end_eye() noexcept {
  g_state.eyeActive = false;
  g_state.activeEyeIndex = std::numeric_limits<uint32_t>::max();
}

bool begin_flat_ui() noexcept {
  if (!is_active() || g_state.eyeActive || g_state.flatUiActive) {
    return false;
  }
  // Future implementation: acquire a head-locked OpenXR quad-layer swapchain image.
  return false;
}

void end_flat_ui() noexcept { g_state.flatUiActive = false; }
} // namespace aurora::xr

extern "C" {
AuroraXRStatus aurora_xr_get_status() { return aurora::xr::status(); }
const char* aurora_xr_get_status_message() { return aurora::xr::status_message(); }
bool aurora_xr_is_requested() { return aurora::xr::is_requested(); }
bool aurora_xr_is_active() { return aurora::xr::is_active(); }
bool aurora_xr_should_render() { return aurora::xr::should_render(); }
AuroraXRFrameState aurora_xr_get_frame_state() { return aurora::xr::frame_state(); }
uint32_t aurora_xr_get_view_count() { return aurora::xr::view_count(); }
bool aurora_xr_get_view(uint32_t index, AuroraXRView* outView) { return aurora::xr::get_view(index, outView); }
bool aurora_xr_begin_eye(uint32_t eyeIndex) { return aurora::xr::begin_eye(eyeIndex); }
void aurora_xr_end_eye() { aurora::xr::end_eye(); }
bool aurora_xr_begin_flat_ui() { return aurora::xr::begin_flat_ui(); }
void aurora_xr_end_flat_ui() { aurora::xr::end_flat_ui(); }
}
