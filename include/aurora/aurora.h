#ifndef AURORA_AURORA_H
#define AURORA_AURORA_H

#ifdef __cplusplus
#include <cstddef>
#include <cstdint>

extern "C" {
#else
#include "stdbool.h"
#include "stddef.h"
#include "stdint.h"
#endif

typedef enum {
  BACKEND_AUTO,
  BACKEND_D3D11,
  BACKEND_D3D12,
  BACKEND_METAL,
  BACKEND_VULKAN,
  BACKEND_OPENGL,
  BACKEND_OPENGLES,
  BACKEND_WEBGPU,
  BACKEND_NULL,
} AuroraBackend;

typedef enum {
  LOG_DEBUG,
  LOG_INFO,
  LOG_WARNING,
  LOG_ERROR,
  LOG_FATAL,
} AuroraLogLevel;

typedef struct {
  int32_t x;
  int32_t y;
} AuroraWindowPos;

typedef struct {
  uint32_t width;
  uint32_t height;

  /**
   * Width of the main GX framebuffer.
   */
  uint32_t fb_width;

  /**
   * Height of the main GX framebuffer.
   */
  uint32_t fb_height;

  /**
   * The size of the framebuffer used to present to the operating system.
   * May differ from fb_width if Aurora is instructed to force an aspect ratio or resolution configuration.
   */
  uint32_t native_fb_width;

  /**
   * The size of the framebuffer used to present to the operating system.
   * May differ from fb_height if Aurora is instructed to force an aspect ratio or resolution configuration.
   */
  uint32_t native_fb_height;
  float scale;
} AuroraWindowSize;

typedef struct SDL_Window SDL_Window;
typedef struct AuroraEvent AuroraEvent;

typedef void (*AuroraLogCallback)(AuroraLogLevel level, const char* module, const char* message, unsigned int len);
typedef void (*AuroraImGuiInitCallback)(const AuroraWindowSize* size);

#define MEM1_DEFAULT_SIZE = 24 * 1024 * 1024;
#define ARAM_DEFAULT_SIZE = 16 * 1024 * 1024;

typedef struct {
  const char* appName;
  const char* configPath;
  AuroraBackend desiredBackend;
  uint32_t msaa;
  uint16_t maxTextureAnisotropy;
  bool vsync;
  bool startFullscreen;
  bool allowJoystickBackgroundEvents;
  bool pauseOnFocusLost;
  bool allowTextureReplacements;
  bool allowTextureDumps;
  int32_t windowPosX;
  int32_t windowPosY;
  uint32_t windowWidth;
  uint32_t windowHeight;
  void* iconRGBA8;
  uint32_t iconWidth;
  uint32_t iconHeight;
  AuroraLogCallback logCallback;
  AuroraLogLevel logLevel;
  AuroraImGuiInitCallback imGuiInitCallback;

  /*
   * The size of the GameCube's main memory, or MEM1 on the Wii.
   * Note that it will not be allocated at the exact 0x80000000 address, as that cannot be guaranteed.
   * This can be set to 0 to disable allocating this region.
   */
  uint32_t mem1Size;

  /*
   * The size of the GameCube's ARAM, or MEM2 on the Wii.
   * This can be set to 0 to disable allocating this region.
   */
  uint32_t mem2Size;

  /*
   * Request OpenXR lifecycle support. Desktop OpenXR launch currently requires the Vulkan backend.
   */
  bool enableOpenXR;

  /*
   * If true, aurora_initialize() fails when requested OpenXR support cannot initialize to ready or active.
   */
  bool requireOpenXR;

  /*
   * Optional per-eye OpenXR swapchain dimensions. Set either value to 0 to use the runtime recommendation.
   */
  uint32_t openXREyeWidth;
  uint32_t openXREyeHeight;
} AuroraConfig;

/**
 * Aurora's OpenXR availability/lifecycle state. The XR API is present in every build;
 * SDK/runtime-dependent builds report unavailable or blocked instead of omitting symbols.
 */
typedef enum {
  AURORA_XR_DISABLED,
  AURORA_XR_UNAVAILABLE,
  AURORA_XR_BLOCKED,
  AURORA_XR_READY,
  AURORA_XR_ACTIVE,
  AURORA_XR_LOST,
} AuroraXRStatus;

typedef struct {
  float x;
  float y;
  float z;
} AuroraXRVector3f;

typedef struct {
  float x;
  float y;
  float z;
  float w;
} AuroraXRQuaternionf;

typedef struct {
  AuroraXRQuaternionf orientation;
  AuroraXRVector3f position;
} AuroraXRPose;

typedef struct {
  float angleLeft;
  float angleRight;
  float angleUp;
  float angleDown;
} AuroraXRFov;

typedef struct {
  bool orientationValid;
  bool positionValid;
  bool orientationTracked;
  bool positionTracked;
  bool fovValid;
  AuroraXRPose pose;
  AuroraXRFov fov;
  uint32_t recommendedWidth;
  uint32_t recommendedHeight;
  uint32_t recommendedSampleCount;
} AuroraXRView;

typedef struct {
  AuroraXRStatus status;
  bool requested;
  bool active;
  bool shouldRender;
  uint32_t viewCount;
  uint64_t frameIndex;
} AuroraXRFrameState;

typedef struct {
  AuroraBackend backend;
  const char* configPath;
  SDL_Window* window;
  AuroraWindowSize windowSize;
} AuroraInfo;

AuroraInfo aurora_initialize(int argc, char* argv[], const AuroraConfig* config);
void aurora_shutdown();
const AuroraEvent* aurora_update();
bool aurora_begin_frame();
void aurora_end_frame();

void aurora_set_log_level(AuroraLogLevel level);
void aurora_set_pause_on_focus_lost(bool value);
void aurora_set_background_input(bool value);

AuroraBackend aurora_get_backend();
const AuroraBackend* aurora_get_available_backends(size_t* count);

AuroraXRStatus aurora_xr_get_status();
const char* aurora_xr_get_status_message();
bool aurora_xr_is_requested();
bool aurora_xr_is_active();
bool aurora_xr_should_render();
AuroraXRFrameState aurora_xr_get_frame_state();
uint32_t aurora_xr_get_view_count();
bool aurora_xr_get_view(uint32_t index, AuroraXRView* outView);
bool aurora_xr_begin_eye(uint32_t eyeIndex);
void aurora_xr_end_eye();
bool aurora_xr_begin_flat_ui();
void aurora_xr_end_flat_ui();

#ifdef __cplusplus
}
#endif

#endif
