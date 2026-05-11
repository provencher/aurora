#pragma once

#include <aurora/aurora.h>

#include <cstdint>
#include <string>
#include <vector>

namespace aurora::xr {
struct ProbeResult {
  AuroraXRStatus status = AURORA_XR_UNAVAILABLE;
  std::string message;
  std::vector<AuroraXRView> views;
};

void initialize(const AuroraConfig& config, AuroraBackend selectedBackend) noexcept;
void shutdown() noexcept;

void on_aurora_frame_start() noexcept;
void begin_frame() noexcept;
void end_frame_after_submit() noexcept;

AuroraXRStatus status() noexcept;
const char* status_message() noexcept;
bool is_requested() noexcept;
bool is_active() noexcept;
bool should_render() noexcept;
AuroraXRFrameState frame_state() noexcept;
uint32_t view_count() noexcept;
bool get_view(uint32_t index, AuroraXRView* outView) noexcept;

bool begin_eye(uint32_t eyeIndex) noexcept;
void end_eye() noexcept;
bool begin_flat_ui() noexcept;
void end_flat_ui() noexcept;

#ifdef AURORA_HAS_OPENXR
ProbeResult probe_openxr(AuroraBackend selectedBackend) noexcept;
#endif
} // namespace aurora::xr
