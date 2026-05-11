#include "xr.hpp"

#include <openxr/openxr.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <vector>

namespace aurora::xr {
namespace {
constexpr const char* VulkanEnable2Extension = "XR_KHR_vulkan_enable2";

std::string result_string(XrResult result) {
  return std::string{"XrResult("} + std::to_string(static_cast<int>(result)) + ")";
}

bool extension_available(const std::vector<XrExtensionProperties>& extensions, const char* name) noexcept {
  return std::any_of(extensions.begin(), extensions.end(), [name](const XrExtensionProperties& ext) {
    return std::strcmp(ext.extensionName, name) == 0;
  });
}

ProbeResult unavailable(std::string message) {
  return {
      .status = AURORA_XR_UNAVAILABLE,
      .message = std::move(message),
  };
}

ProbeResult blocked(std::string message, std::vector<AuroraXRView> views = {}) {
  return {
      .status = AURORA_XR_BLOCKED,
      .message = std::move(message),
      .views = std::move(views),
  };
}
} // namespace

ProbeResult probe_openxr(AuroraBackend selectedBackend) noexcept {
  if (selectedBackend != BACKEND_VULKAN) {
    return blocked("OpenXR desktop launch requires BACKEND_VULKAN");
  }

  uint32_t extensionCount = 0;
  XrResult result = xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr);
  if (XR_FAILED(result)) {
    return unavailable("OpenXR runtime unavailable while enumerating instance extensions: " + result_string(result));
  }

  std::vector<XrExtensionProperties> extensions(extensionCount, {XR_TYPE_EXTENSION_PROPERTIES});
  result = xrEnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount, extensions.data());
  if (XR_FAILED(result)) {
    return unavailable("OpenXR runtime unavailable while reading instance extensions: " + result_string(result));
  }
  extensions.resize(extensionCount);

  if (!extension_available(extensions, VulkanEnable2Extension)) {
    return blocked("OpenXR runtime does not expose XR_KHR_vulkan_enable2, required for desktop Vulkan launch");
  }

  const std::array enabledExtensions{VulkanEnable2Extension};
  XrInstanceCreateInfo createInfo{XR_TYPE_INSTANCE_CREATE_INFO};
  std::strncpy(createInfo.applicationInfo.applicationName, "Aurora", XR_MAX_APPLICATION_NAME_SIZE - 1);
  createInfo.applicationInfo.applicationVersion = 1;
  std::strncpy(createInfo.applicationInfo.engineName, "Aurora", XR_MAX_ENGINE_NAME_SIZE - 1);
  createInfo.applicationInfo.engineVersion = 1;
  createInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
  createInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size());
  createInfo.enabledExtensionNames = enabledExtensions.data();

  XrInstance instance = XR_NULL_HANDLE;
  result = xrCreateInstance(&createInfo, &instance);
  if (XR_FAILED(result)) {
    return unavailable("OpenXR instance creation failed: " + result_string(result));
  }

  auto destroyInstance = [&instance] {
    if (instance != XR_NULL_HANDLE) {
      xrDestroyInstance(instance);
      instance = XR_NULL_HANDLE;
    }
  };

  XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
  systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
  XrSystemId systemId = XR_NULL_SYSTEM_ID;
  result = xrGetSystem(instance, &systemInfo, &systemId);
  if (XR_FAILED(result)) {
    destroyInstance();
    return unavailable("No OpenXR head-mounted-display system available: " + result_string(result));
  }

  uint32_t viewCount = 0;
  result = xrEnumerateViewConfigurationViews(instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount,
                                             nullptr);
  if (XR_FAILED(result) || viewCount == 0) {
    destroyInstance();
    return blocked("OpenXR runtime does not expose a primary stereo view configuration: " + result_string(result));
  }

  std::vector<XrViewConfigurationView> xrViews(viewCount, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
  result = xrEnumerateViewConfigurationViews(instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, viewCount,
                                             &viewCount, xrViews.data());
  if (XR_FAILED(result)) {
    destroyInstance();
    return blocked("Failed to query OpenXR primary stereo view configuration: " + result_string(result));
  }
  xrViews.resize(viewCount);

  std::vector<AuroraXRView> views;
  views.reserve(xrViews.size());
  for (const XrViewConfigurationView& xrView : xrViews) {
    AuroraXRView view{};
    view.recommendedWidth = xrView.recommendedImageRectWidth;
    view.recommendedHeight = xrView.recommendedImageRectHeight;
    view.recommendedSampleCount = xrView.recommendedSwapchainSampleCount;
    views.push_back(view);
  }

  destroyInstance();
  return blocked("OpenXR runtime and stereo view configuration detected, but XR rendering is blocked: Aurora's Dawn/WebGPU path does not expose native Vulkan instance/device/queue handles or OpenXR swapchain image interop.",
                 std::move(views));
}
} // namespace aurora::xr
