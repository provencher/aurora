#include "xr.hpp"

#if defined(AURORA_HAS_OPENXR) && defined(AURORA_ENABLE_GX) && defined(AURORA_DAWN_OPENXR_HANDLES)
#define XR_USE_GRAPHICS_API_VULKAN 1
#include <vulkan/vulkan.h>
#endif

#if defined(AURORA_HAS_OPENXR)
#include <openxr/openxr.h>
#if defined(AURORA_ENABLE_GX) && defined(AURORA_DAWN_OPENXR_HANDLES)
#include <openxr/openxr_platform.h>
#endif
#endif

#if defined(AURORA_ENABLE_GX)
#include "../gfx/common.hpp"
#include "../webgpu/gpu.hpp"
#endif

#if defined(AURORA_ENABLE_GX) && defined(AURORA_DAWN_OPENXR_HANDLES)
#include "../webgpu/dawn_vulkan_interop.hpp"
#endif

#include "../internal.hpp"
#include "../logging.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <sstream>
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
  AuroraXRVulkanExtensionValidation vulkanExtensionValidation =
      AURORA_XR_VULKAN_EXTENSION_VALIDATION_UNKNOWN;
  AuroraXRFrameState frameState{};
  std::vector<AuroraXRView> views;
  bool eyeActive = false;
  uint32_t activeEyeIndex = std::numeric_limits<uint32_t>::max();
  bool flatUiActive = false;
  bool sbsMirrorEnabled = false;
};

State g_state;

void set_status(AuroraXRStatus status, std::string message) noexcept;

bool env_flag_enabled(const char* name) noexcept {
  if (const char* value = std::getenv(name); value != nullptr) {
    return value[0] != '\0' && std::strcmp(value, "0") != 0 && std::strcmp(value, "FALSE") != 0 &&
           std::strcmp(value, "false") != 0;
  }
  return false;
}

#if defined(AURORA_HAS_OPENXR) && defined(AURORA_ENABLE_GX) && defined(AURORA_DAWN_OPENXR_HANDLES)
constexpr const char* VulkanEnable2Extension = "XR_KHR_vulkan_enable2";
constexpr const char* VulkanEnableExtension = "XR_KHR_vulkan_enable";
constexpr XrVersion RequestedOpenXRApiVersion = XR_MAKE_VERSION(1, 0, 0);
constexpr uint32_t XrSwapchainSampleCount = 1;
constexpr uint32_t MaxConfiguredEyeDimension = 8192;

struct EyeSwapchainImage {
  XrSwapchainImageVulkanKHR xrImage{XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR};
  wgpu::Texture texture;
  wgpu::TextureView view;
  wgpu::BindGroup mirrorBindGroup;
};

struct EyeSwapchain {
  XrSwapchain swapchain = XR_NULL_HANDLE;
  std::vector<EyeSwapchainImage> images;
  webgpu::TextureWithSampler depth;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t acquiredImageIndex = std::numeric_limits<uint32_t>::max();
  bool acquired = false;
  bool rendered = false;
};

struct FlatUiSwapchainImage {
  XrSwapchainImageVulkanKHR xrImage{XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR};
  wgpu::Texture texture;
  wgpu::TextureView view;
  wgpu::BindGroup mirrorBindGroup;
};

struct FlatUiSwapchain {
  XrSwapchain swapchain = XR_NULL_HANDLE;
  std::vector<FlatUiSwapchainImage> images;
  webgpu::TextureWithSampler depth;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t acquiredImageIndex = std::numeric_limits<uint32_t>::max();
  bool acquired = false;
  bool rendered = false;
};

struct Runtime {
  XrInstance instance = XR_NULL_HANDLE;
  XrSystemId systemId = XR_NULL_SYSTEM_ID;
  XrSession session = XR_NULL_HANDLE;
  XrSpace localSpace = XR_NULL_HANDLE;
  XrSpace viewSpace = XR_NULL_HANDLE;
  XrSessionState sessionState = XR_SESSION_STATE_UNKNOWN;
  bool sessionRunning = false;
  bool frameBegun = false;
  XrFrameState xrFrameState{XR_TYPE_FRAME_STATE};
  std::vector<XrViewConfigurationView> configViews;
  std::vector<XrView> locatedViews;
  std::vector<XrCompositionLayerProjectionView> layerViews;
  std::vector<EyeSwapchain> eyes;
  FlatUiSwapchain flatUi;
  XrCompositionLayerQuad flatUiLayer{XR_TYPE_COMPOSITION_LAYER_QUAD};
  int64_t colorFormat = 0;
  bool suppressProjectionLayers = false;
  bool canQueryVulkanExtensions = false;
  bool dawnOpenXRHooksInstalled = false;
};

Runtime g_runtime;

const char* xr_result_name(XrResult result) noexcept {
  switch (result) {
  case XR_SUCCESS:
    return "XR_SUCCESS";
  case XR_TIMEOUT_EXPIRED:
    return "XR_TIMEOUT_EXPIRED";
  case XR_SESSION_LOSS_PENDING:
    return "XR_SESSION_LOSS_PENDING";
  case XR_EVENT_UNAVAILABLE:
    return "XR_EVENT_UNAVAILABLE";
  case XR_FRAME_DISCARDED:
    return "XR_FRAME_DISCARDED";
  case XR_ERROR_RUNTIME_FAILURE:
    return "XR_ERROR_RUNTIME_FAILURE";
  case XR_ERROR_INITIALIZATION_FAILED:
    return "XR_ERROR_INITIALIZATION_FAILED";
  case XR_ERROR_EXTENSION_NOT_PRESENT:
    return "XR_ERROR_EXTENSION_NOT_PRESENT";
  case XR_ERROR_FORM_FACTOR_UNAVAILABLE:
    return "XR_ERROR_FORM_FACTOR_UNAVAILABLE";
  case XR_ERROR_RUNTIME_UNAVAILABLE:
    return "XR_ERROR_RUNTIME_UNAVAILABLE";
  case XR_ERROR_GRAPHICS_DEVICE_INVALID:
    return "XR_ERROR_GRAPHICS_DEVICE_INVALID";
  case XR_ERROR_SESSION_LOST:
    return "XR_ERROR_SESSION_LOST";
  default:
    return nullptr;
  }
}

std::string result_string(XrResult result) {
  std::string message = std::string{"XrResult("} + std::to_string(static_cast<int>(result));
  if (const char* name = xr_result_name(result); name != nullptr) {
    message += " ";
    message += name;
  }
  message += ")";
  return message;
}

std::string xr_version_string(XrVersion version) {
  return std::to_string(XR_VERSION_MAJOR(version)) + "." + std::to_string(XR_VERSION_MINOR(version)) + "." +
         std::to_string(XR_VERSION_PATCH(version));
}

std::string vk_result_string(VkResult result) {
  return "VkResult(" + std::to_string(static_cast<int>(result)) + ")";
}

std::string runtime_hint() {
  std::string hint =
      "; check the active OpenXR runtime registration and confirm the runtime app/headset is running";
  if (const char* runtimeJson = std::getenv("XR_RUNTIME_JSON"); runtimeJson != nullptr && runtimeJson[0] != '\0') {
    hint += "; XR_RUNTIME_JSON=";
    hint += runtimeJson;
  }
  return hint;
}

std::string append_dawn_interop(std::string message) {
  message += "; Dawn interop: ";
  message += webgpu::probe_dawn_vulkan_interop().message;
  return message;
}

bool extension_available(const std::vector<XrExtensionProperties>& extensions, const char* name) noexcept {
  return std::any_of(extensions.begin(), extensions.end(), [name](const XrExtensionProperties& ext) {
    return std::strcmp(ext.extensionName, name) == 0;
  });
}

bool dawn_vulkan_handles_ready(const dawn::native::vulkan::VulkanDeviceHandles& handles) noexcept {
  return handles.instance != VK_NULL_HANDLE && handles.physicalDevice != VK_NULL_HANDLE &&
         handles.device != VK_NULL_HANDLE && handles.queue != VK_NULL_HANDLE &&
         handles.queueFamilyIndex != std::numeric_limits<uint32_t>::max();
}

int64_t preferred_vk_format() noexcept {
  switch (webgpu::g_graphicsConfig.surfaceConfiguration.format) {
  case wgpu::TextureFormat::BGRA8Unorm:
    return VK_FORMAT_B8G8R8A8_UNORM;
  case wgpu::TextureFormat::BGRA8UnormSrgb:
    return VK_FORMAT_B8G8R8A8_SRGB;
  case wgpu::TextureFormat::RGBA8Unorm:
    return VK_FORMAT_R8G8B8A8_UNORM;
  case wgpu::TextureFormat::RGBA8UnormSrgb:
    return VK_FORMAT_R8G8B8A8_SRGB;
  default:
    return VK_FORMAT_R8G8B8A8_UNORM;
  }
}

wgpu::TextureFormat wgpu_format_from_vk_format(int64_t vkFormat) noexcept {
  switch (vkFormat) {
  case VK_FORMAT_B8G8R8A8_UNORM:
    return wgpu::TextureFormat::BGRA8Unorm;
  case VK_FORMAT_B8G8R8A8_SRGB:
    return wgpu::TextureFormat::BGRA8UnormSrgb;
  case VK_FORMAT_R8G8B8A8_UNORM:
    return wgpu::TextureFormat::RGBA8Unorm;
  case VK_FORMAT_R8G8B8A8_SRGB:
    return wgpu::TextureFormat::RGBA8UnormSrgb;
  default:
    return wgpu::TextureFormat::Undefined;
  }
}

bool load_xr_proc(XrInstance instance, const char* name, PFN_xrVoidFunction* outProc, std::string& message) {
  *outProc = nullptr;
  const XrResult result = xrGetInstanceProcAddr(instance, name, outProc);
  if (XR_FAILED(result) || *outProc == nullptr) {
    message = std::string{"failed to load "} + name + ": " + result_string(result);
    return false;
  }
  return true;
}

bool wait_dawn_vulkan_queue_idle(std::string& message) {
  dawn::native::vulkan::VulkanDeviceHandles handles{};
  if (!webgpu::get_dawn_vulkan_handles(&handles) || !dawn_vulkan_handles_ready(handles)) {
    message = append_dawn_interop("cannot synchronize OpenXR image release because Dawn Vulkan handles are incomplete");
    return false;
  }

  const auto getDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
      webgpu::get_dawn_vulkan_instance_proc_addr("vkGetDeviceProcAddr"));
  if (getDeviceProcAddr == nullptr) {
    message = append_dawn_interop("cannot synchronize OpenXR image release because vkGetDeviceProcAddr is unavailable");
    return false;
  }

  const auto queueWaitIdle = reinterpret_cast<PFN_vkQueueWaitIdle>(getDeviceProcAddr(handles.device, "vkQueueWaitIdle"));
  if (queueWaitIdle == nullptr) {
    message = append_dawn_interop("cannot synchronize OpenXR image release because vkQueueWaitIdle is unavailable");
    return false;
  }

  const VkResult result = queueWaitIdle(handles.queue);
  if (result != VK_SUCCESS) {
    message = append_dawn_interop("vkQueueWaitIdle failed before OpenXR image release: " + vk_result_string(result));
    return false;
  }
  return true;
}

template <typename Proc>
bool read_vulkan_extension_string(Proc proc, XrInstance instance, XrSystemId systemId, std::string& outExtensions,
                                  std::string& message, const char* procName) {
  uint32_t byteCount = 0;
  XrResult result = proc(instance, systemId, 0, &byteCount, nullptr);
  if (XR_FAILED(result)) {
    message = std::string{procName} + " size query failed: " + result_string(result);
    return false;
  }
  if (byteCount == 0) {
    outExtensions.clear();
    return true;
  }

  std::string extensions(byteCount, '\0');
  result = proc(instance, systemId, byteCount, &byteCount, extensions.data());
  if (XR_FAILED(result)) {
    message = std::string{procName} + " failed: " + result_string(result);
    return false;
  }
  extensions.resize(byteCount != 0 && extensions[byteCount - 1] == '\0' ? byteCount - 1 : byteCount);
  extensions.erase(std::remove(extensions.begin(), extensions.end(), '\0'), extensions.end());
  outExtensions = std::move(extensions);
  return true;
}

std::vector<std::string> split_extension_string(std::string_view extensions) {
  std::vector<std::string> names;
  std::istringstream stream{std::string{extensions}};
  std::string name;
  while (stream >> name) {
    while (!name.empty() && name.back() == '\0') {
      name.pop_back();
    }
    names.push_back(std::move(name));
  }
  return names;
}

template <typename ExtensionProperties>
bool extension_property_available(const std::vector<ExtensionProperties>& properties, std::string_view name) {
  return std::any_of(properties.begin(), properties.end(), [&](const ExtensionProperties& property) {
    return name == property.extensionName;
  });
}

std::string join_extensions(const std::vector<std::string>& extensions) {
  std::string joined;
  for (const std::string& extension : extensions) {
    if (!joined.empty()) {
      joined += ", ";
    }
    joined += extension;
  }
  return joined;
}

bool validate_runtime_vulkan_extensions(const dawn::native::vulkan::VulkanDeviceHandles& handles,
                                        std::string_view runtimeInstanceExtensions,
                                        std::string_view runtimeDeviceExtensions, std::string& message,
                                        AuroraXRVulkanExtensionValidation& validation) {
  const auto enumerateInstanceExtensions = reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
      webgpu::get_dawn_vulkan_instance_proc_addr("vkEnumerateInstanceExtensionProperties"));
  if (enumerateInstanceExtensions == nullptr) {
    validation = AURORA_XR_VULKAN_EXTENSION_VALIDATION_INSTANCE_QUERY_UNAVAILABLE;
    message = append_dawn_interop("cannot validate OpenXR runtime Vulkan instance extension requirements because "
                                  "vkEnumerateInstanceExtensionProperties is unavailable");
    return false;
  }

  const auto enumerateDeviceExtensions = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
      webgpu::get_dawn_vulkan_instance_proc_addr("vkEnumerateDeviceExtensionProperties"));
  if (enumerateDeviceExtensions == nullptr) {
    validation = AURORA_XR_VULKAN_EXTENSION_VALIDATION_DEVICE_QUERY_UNAVAILABLE;
    message = append_dawn_interop("cannot validate OpenXR runtime Vulkan device extension requirements because "
                                  "vkEnumerateDeviceExtensionProperties is unavailable");
    return false;
  }

  const std::vector<std::string> requiredInstanceExtensions = split_extension_string(runtimeInstanceExtensions);
  uint32_t instanceExtensionCount = 0;
  auto result = enumerateInstanceExtensions(nullptr, &instanceExtensionCount, nullptr);
  if (result != VK_SUCCESS) {
    validation = AURORA_XR_VULKAN_EXTENSION_VALIDATION_INSTANCE_QUERY_UNAVAILABLE;
    message = append_dawn_interop("vkEnumerateInstanceExtensionProperties failed while validating OpenXR runtime "
                                  "requirements: " +
                                  vk_result_string(result));
    return false;
  }
  std::vector<VkExtensionProperties> instanceExtensions(instanceExtensionCount);
  result = enumerateInstanceExtensions(nullptr, &instanceExtensionCount, instanceExtensions.data());
  if (result != VK_SUCCESS) {
    validation = AURORA_XR_VULKAN_EXTENSION_VALIDATION_INSTANCE_QUERY_UNAVAILABLE;
    message = append_dawn_interop("failed to enumerate Vulkan instance extensions while validating OpenXR runtime "
                                  "requirements: " +
                                  vk_result_string(result));
    return false;
  }
  instanceExtensions.resize(instanceExtensionCount);

  std::vector<std::string> missingInstanceExtensions;
  for (const std::string& extension : requiredInstanceExtensions) {
    if (extension == "VK_NV_external_memory_capabilities") {
      const char* ignoreNv = std::getenv("AURORA_XR_IGNORE_MISSING_NV_EXTERNAL_MEMORY_CAPABILITIES");
      if (ignoreNv != nullptr && ignoreNv[0] != '\0' && ignoreNv[0] != '0') {
        continue;
      }
    }
    if (!extension_property_available(instanceExtensions, extension)) {
      missingInstanceExtensions.push_back(extension);
    }
  }
  if (!missingInstanceExtensions.empty()) {
    validation = AURORA_XR_VULKAN_EXTENSION_VALIDATION_MISSING_INSTANCE;
    message = append_dawn_interop("OpenXR runtime requires Vulkan instance extensions that the active Vulkan loader "
                                  "does not advertise: " +
                                  join_extensions(missingInstanceExtensions));
    return false;
  }

  const std::vector<std::string> requiredDeviceExtensions = split_extension_string(runtimeDeviceExtensions);
  uint32_t deviceExtensionCount = 0;
  result = enumerateDeviceExtensions(handles.physicalDevice, nullptr, &deviceExtensionCount, nullptr);
  if (result != VK_SUCCESS) {
    validation = AURORA_XR_VULKAN_EXTENSION_VALIDATION_DEVICE_QUERY_UNAVAILABLE;
    message = append_dawn_interop("vkEnumerateDeviceExtensionProperties failed while validating OpenXR runtime "
                                  "requirements: " +
                                  vk_result_string(result));
    return false;
  }
  std::vector<VkExtensionProperties> deviceExtensions(deviceExtensionCount);
  result = enumerateDeviceExtensions(handles.physicalDevice, nullptr, &deviceExtensionCount, deviceExtensions.data());
  if (result != VK_SUCCESS) {
    validation = AURORA_XR_VULKAN_EXTENSION_VALIDATION_DEVICE_QUERY_UNAVAILABLE;
    message = append_dawn_interop("failed to enumerate Dawn Vulkan physical-device extensions while validating "
                                  "OpenXR runtime requirements: " +
                                  vk_result_string(result));
    return false;
  }
  deviceExtensions.resize(deviceExtensionCount);

  std::vector<std::string> missingDeviceExtensions;
  for (const std::string& extension : requiredDeviceExtensions) {
    if (!extension_property_available(deviceExtensions, extension)) {
      missingDeviceExtensions.push_back(extension);
    }
  }
  if (!missingDeviceExtensions.empty()) {
    validation = AURORA_XR_VULKAN_EXTENSION_VALIDATION_MISSING_DEVICE;
    message = append_dawn_interop("OpenXR runtime requires Vulkan device extensions that Dawn's selected physical "
                                  "device does not advertise: " +
                                  join_extensions(missingDeviceExtensions));
    return false;
  }

  validation = AURORA_XR_VULKAN_EXTENSION_VALIDATION_VALIDATED;
  return true;
}

void reset_runtime() noexcept {
  for (EyeSwapchain& eye : g_runtime.eyes) {
    if (eye.swapchain != XR_NULL_HANDLE) {
      xrDestroySwapchain(eye.swapchain);
    }
  }
  if (g_runtime.flatUi.swapchain != XR_NULL_HANDLE) {
    xrDestroySwapchain(g_runtime.flatUi.swapchain);
  }
  if (g_runtime.viewSpace != XR_NULL_HANDLE) {
    xrDestroySpace(g_runtime.viewSpace);
  }
  if (g_runtime.localSpace != XR_NULL_HANDLE) {
    xrDestroySpace(g_runtime.localSpace);
  }
  if (g_runtime.session != XR_NULL_HANDLE) {
    xrDestroySession(g_runtime.session);
  }
  if (g_runtime.instance != XR_NULL_HANDLE) {
    xrDestroyInstance(g_runtime.instance);
  }
  g_runtime = {};
}

bool enumerate_vulkan_swapchain_images(XrSwapchain swapchain, std::vector<XrSwapchainImageVulkanKHR>& outImages,
                                       std::string& message) {
  uint32_t imageCount = 0;
  XrResult result = xrEnumerateSwapchainImages(swapchain, 0, &imageCount, nullptr);
  if (XR_FAILED(result) || imageCount == 0) {
    message = append_dawn_interop("xrEnumerateSwapchainImages failed: " + result_string(result));
    return false;
  }

  outImages.assign(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
  result = xrEnumerateSwapchainImages(swapchain, imageCount, &imageCount,
                                      reinterpret_cast<XrSwapchainImageBaseHeader*>(outImages.data()));
  if (XR_FAILED(result)) {
    message = append_dawn_interop("failed to read Vulkan swapchain images: " + result_string(result));
    return false;
  }
  outImages.resize(imageCount);
  return true;
}

void update_view_from_xr(AuroraXRView& out, const XrView& view, XrViewStateFlags viewStateFlags) noexcept {
  out.orientationValid = (viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0;
  out.positionValid = (viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) != 0;
  out.orientationTracked = (viewStateFlags & XR_VIEW_STATE_ORIENTATION_TRACKED_BIT) != 0;
  out.positionTracked = (viewStateFlags & XR_VIEW_STATE_POSITION_TRACKED_BIT) != 0;
  out.fovValid = true;
  out.pose.orientation = {view.pose.orientation.x, view.pose.orientation.y, view.pose.orientation.z,
                          view.pose.orientation.w};
  out.pose.position = {view.pose.position.x, view.pose.position.y, view.pose.position.z};
  out.fov = {view.fov.angleLeft, view.fov.angleRight, view.fov.angleUp, view.fov.angleDown};
}

VkResult vk_result_from_xr_create_result(XrResult xrResult, VkResult vkResult) noexcept {
  if (XR_SUCCEEDED(xrResult)) {
    return vkResult;
  }
  return vkResult != VK_SUCCESS ? vkResult : VK_ERROR_INITIALIZATION_FAILED;
}

void trace_bootstrap(const char* message) noexcept {
  const char* enabled = std::getenv("AURORA_XR_BOOTSTRAP_TRACE");
  if (enabled != nullptr && enabled[0] != '\0' && enabled[0] != '0') {
    std::fprintf(stderr, "[aurora::xr::bootstrap] %s\n", message);
    std::fflush(stderr);
  }
}

VkResult openxr_create_vulkan_instance(void* userdata, PFN_vkGetInstanceProcAddr pfnGetInstanceProcAddr,
                                       const VkInstanceCreateInfo* createInfo,
                                       const VkAllocationCallbacks* allocator, VkInstance* instance) {
  auto* runtime = static_cast<Runtime*>(userdata);
  if (runtime == nullptr || runtime->instance == XR_NULL_HANDLE || runtime->systemId == XR_NULL_SYSTEM_ID ||
      pfnGetInstanceProcAddr == nullptr || createInfo == nullptr || instance == nullptr) {
    const bool enabled = std::getenv("AURORA_XR_BOOTSTRAP_TRACE") != nullptr;
    if (enabled) {
      std::fprintf(stderr,
                   "[aurora::xr::bootstrap] xrCreateVulkanInstanceKHR invalid inputs userdata=%p xrInstance=%p "
                   "system=%llu gip=%p createInfo=%p out=%p\n",
                   userdata, runtime != nullptr ? reinterpret_cast<void*>(runtime->instance) : nullptr,
                   runtime != nullptr ? static_cast<unsigned long long>(runtime->systemId) : 0ull,
                   reinterpret_cast<void*>(pfnGetInstanceProcAddr), createInfo, instance);
      std::fflush(stderr);
    }
    return VK_ERROR_INITIALIZATION_FAILED;
  }

  trace_bootstrap("loading xrCreateVulkanInstanceKHR");
  PFN_xrVoidFunction proc = nullptr;
  XrResult result = xrGetInstanceProcAddr(runtime->instance, "xrCreateVulkanInstanceKHR", &proc);
  if (XR_FAILED(result) || proc == nullptr) {
    trace_bootstrap("failed to load xrCreateVulkanInstanceKHR");
    return VK_ERROR_INITIALIZATION_FAILED;
  }

  XrVulkanInstanceCreateInfoKHR xrCreateInfo{XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};
  xrCreateInfo.systemId = runtime->systemId;
  xrCreateInfo.pfnGetInstanceProcAddr = pfnGetInstanceProcAddr;
  xrCreateInfo.vulkanCreateInfo = createInfo;
  xrCreateInfo.vulkanAllocator = allocator;

  VkResult vkResult = VK_SUCCESS;
  trace_bootstrap("calling xrCreateVulkanInstanceKHR");
  result = reinterpret_cast<PFN_xrCreateVulkanInstanceKHR>(proc)(runtime->instance, &xrCreateInfo, instance, &vkResult);
  trace_bootstrap("returned from xrCreateVulkanInstanceKHR");
  return vk_result_from_xr_create_result(result, vkResult);
}

VkResult openxr_create_vulkan_device(void* userdata, PFN_vkGetInstanceProcAddr pfnGetInstanceProcAddr,
                                     VkInstance instance, VkPhysicalDevice physicalDevice,
                                     const VkDeviceCreateInfo* createInfo, const VkAllocationCallbacks* allocator,
                                     VkDevice* device) {
  auto* runtime = static_cast<Runtime*>(userdata);
  if (runtime == nullptr || runtime->instance == XR_NULL_HANDLE || runtime->systemId == XR_NULL_SYSTEM_ID ||
      pfnGetInstanceProcAddr == nullptr || instance == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE ||
      createInfo == nullptr || device == nullptr) {
    const bool enabled = std::getenv("AURORA_XR_BOOTSTRAP_TRACE") != nullptr;
    if (enabled) {
      std::fprintf(stderr,
                   "[aurora::xr::bootstrap] xrCreateVulkanDeviceKHR invalid inputs userdata=%p xrInstance=%p "
                   "system=%llu gip=%p instance=%p physicalDevice=%p createInfo=%p out=%p\n",
                   userdata, runtime != nullptr ? reinterpret_cast<void*>(runtime->instance) : nullptr,
                   runtime != nullptr ? static_cast<unsigned long long>(runtime->systemId) : 0ull,
                   reinterpret_cast<void*>(pfnGetInstanceProcAddr), reinterpret_cast<void*>(instance),
                   reinterpret_cast<void*>(physicalDevice), createInfo, device);
      std::fflush(stderr);
    }
    return VK_ERROR_INITIALIZATION_FAILED;
  }

  PFN_xrVoidFunction graphicsDeviceProc = nullptr;
  XrResult graphicsDeviceResult =
      xrGetInstanceProcAddr(runtime->instance, "xrGetVulkanGraphicsDevice2KHR", &graphicsDeviceProc);
  VkPhysicalDevice runtimePhysicalDevice = VK_NULL_HANDLE;
  if (XR_SUCCEEDED(graphicsDeviceResult) && graphicsDeviceProc != nullptr) {
    XrVulkanGraphicsDeviceGetInfoKHR deviceGetInfo{XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};
    deviceGetInfo.systemId = runtime->systemId;
    deviceGetInfo.vulkanInstance = instance;
    graphicsDeviceResult = reinterpret_cast<PFN_xrGetVulkanGraphicsDevice2KHR>(graphicsDeviceProc)(
        runtime->instance, &deviceGetInfo, &runtimePhysicalDevice);
  }
  if (std::getenv("AURORA_XR_BOOTSTRAP_TRACE") != nullptr) {
    std::fprintf(stderr,
                 "[aurora::xr::bootstrap] xrCreateVulkanDeviceKHR input instance=%p physicalDevice=%p "
                 "runtimePhysicalDevice=%p runtimeDeviceResult=%d extensionCount=%u\n",
                 reinterpret_cast<void*>(instance), reinterpret_cast<void*>(physicalDevice),
                 reinterpret_cast<void*>(runtimePhysicalDevice), static_cast<int>(graphicsDeviceResult),
                 createInfo->enabledExtensionCount);
    for (uint32_t i = 0; i < createInfo->enabledExtensionCount; ++i) {
      std::fprintf(stderr, "[aurora::xr::bootstrap]   deviceExtension[%u]=%s\n", i,
                   createInfo->ppEnabledExtensionNames != nullptr && createInfo->ppEnabledExtensionNames[i] != nullptr
                       ? createInfo->ppEnabledExtensionNames[i]
                       : "(null)");
    }
    std::fflush(stderr);
  }
  if (XR_SUCCEEDED(graphicsDeviceResult) && runtimePhysicalDevice != VK_NULL_HANDLE) {
    physicalDevice = runtimePhysicalDevice;
  }

  trace_bootstrap("loading xrCreateVulkanDeviceKHR");
  PFN_xrVoidFunction proc = nullptr;
  XrResult result = xrGetInstanceProcAddr(runtime->instance, "xrCreateVulkanDeviceKHR", &proc);
  if (XR_FAILED(result) || proc == nullptr) {
    trace_bootstrap("failed to load xrCreateVulkanDeviceKHR");
    const auto createDevice =
        reinterpret_cast<PFN_vkCreateDevice>(pfnGetInstanceProcAddr(instance, "vkCreateDevice"));
    if (createDevice == nullptr) {
      return VK_ERROR_INITIALIZATION_FAILED;
    }
    return createDevice(physicalDevice, createInfo, allocator, device);
  }

  XrVulkanDeviceCreateInfoKHR xrCreateInfo{XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};
  xrCreateInfo.systemId = runtime->systemId;
  xrCreateInfo.pfnGetInstanceProcAddr = pfnGetInstanceProcAddr;
  xrCreateInfo.vulkanPhysicalDevice = physicalDevice;
  xrCreateInfo.vulkanCreateInfo = createInfo;
  xrCreateInfo.vulkanAllocator = allocator;

  VkResult vkResult = VK_SUCCESS;
  trace_bootstrap("calling xrCreateVulkanDeviceKHR");
  result = reinterpret_cast<PFN_xrCreateVulkanDeviceKHR>(proc)(runtime->instance, &xrCreateInfo, device, &vkResult);
  trace_bootstrap("returned from xrCreateVulkanDeviceKHR");
  return vk_result_from_xr_create_result(result, vkResult);
}

bool ensure_openxr_instance_system_views(std::string& message) {
  if (const char* nullCompositor = std::getenv("XRT_COMPOSITOR_NULL");
      nullCompositor != nullptr &&
      (std::strcmp(nullCompositor, "TRUE") == 0 || std::strcmp(nullCompositor, "true") == 0 ||
       std::strcmp(nullCompositor, "1") == 0)) {
    g_runtime.suppressProjectionLayers = true;
  }

  if (g_runtime.instance != XR_NULL_HANDLE && g_runtime.systemId != XR_NULL_SYSTEM_ID &&
      g_runtime.configViews.size() >= 2) {
    return true;
  }

  uint32_t extensionCount = 0;
  XrResult result = xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr);
  if (XR_FAILED(result)) {
    message = append_dawn_interop("OpenXR runtime unavailable while enumerating instance extensions: " +
                                  result_string(result) + runtime_hint());
    return false;
  }

  std::vector<XrExtensionProperties> extensions(extensionCount, {XR_TYPE_EXTENSION_PROPERTIES});
  result = xrEnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount, extensions.data());
  if (XR_FAILED(result)) {
    message = append_dawn_interop("OpenXR runtime unavailable while reading instance extensions: " +
                                  result_string(result) + runtime_hint());
    return false;
  }
  extensions.resize(extensionCount);
  if (!extension_available(extensions, VulkanEnable2Extension)) {
    message = append_dawn_interop(
        "OpenXR runtime does not expose XR_KHR_vulkan_enable2, required for desktop Vulkan launch");
    return false;
  }

  std::vector<const char*> enabledExtensions{VulkanEnable2Extension};
  g_runtime.canQueryVulkanExtensions = extension_available(extensions, VulkanEnableExtension);
  if (g_runtime.canQueryVulkanExtensions) {
    enabledExtensions.push_back(VulkanEnableExtension);
  }
  XrInstanceCreateInfo createInfo{XR_TYPE_INSTANCE_CREATE_INFO};
  std::strncpy(createInfo.applicationInfo.applicationName, "Aurora", XR_MAX_APPLICATION_NAME_SIZE - 1);
  createInfo.applicationInfo.applicationVersion = 1;
  std::strncpy(createInfo.applicationInfo.engineName, "Aurora", XR_MAX_ENGINE_NAME_SIZE - 1);
  createInfo.applicationInfo.engineVersion = 1;
  createInfo.applicationInfo.apiVersion = RequestedOpenXRApiVersion;
  createInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size());
  createInfo.enabledExtensionNames = enabledExtensions.data();

  result = xrCreateInstance(&createInfo, &g_runtime.instance);
  if (XR_FAILED(result)) {
    message = append_dawn_interop("OpenXR instance creation failed: " + result_string(result) + runtime_hint());
    return false;
  }

  XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
  systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
  result = xrGetSystem(g_runtime.instance, &systemInfo, &g_runtime.systemId);
  if (XR_FAILED(result)) {
    message = append_dawn_interop("No OpenXR head-mounted-display system available: " + result_string(result) +
                                  runtime_hint());
    return false;
  }

  uint32_t viewCount = 0;
  result = xrEnumerateViewConfigurationViews(g_runtime.instance, g_runtime.systemId,
                                             XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, nullptr);
  if (XR_FAILED(result) || viewCount < 2) {
    message = append_dawn_interop("OpenXR runtime does not expose a primary stereo view configuration: " +
                                  result_string(result));
    return false;
  }

  g_runtime.configViews.assign(viewCount, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
  result = xrEnumerateViewConfigurationViews(g_runtime.instance, g_runtime.systemId,
                                             XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, viewCount, &viewCount,
                                             g_runtime.configViews.data());
  if (XR_FAILED(result)) {
    message = append_dawn_interop("Failed to query OpenXR primary stereo view configuration: " +
                                  result_string(result));
    return false;
  }
  g_runtime.configViews.resize(viewCount);

  return true;
}

bool initialize_runtime(std::string& message, std::vector<AuroraXRView>& views) {
  if (!ensure_openxr_instance_system_views(message)) {
    return false;
  }
  XrResult result = XR_SUCCESS;

  dawn::native::vulkan::VulkanDeviceHandles handles{};
  if (!webgpu::get_dawn_vulkan_handles(&handles) || !dawn_vulkan_handles_ready(handles)) {
    message = append_dawn_interop("patched Dawn returned incomplete Vulkan handles or queue family metadata");
    return false;
  }

  g_state.vulkanExtensionValidation = AURORA_XR_VULKAN_EXTENSION_VALIDATION_NOT_QUERIED;
  PFN_xrVoidFunction proc = nullptr;
  std::string detail;
  std::string runtimeInstanceExtensions = "not queried (XR_KHR_vulkan_enable unavailable)";
  std::string runtimeDeviceExtensions = "not queried (XR_KHR_vulkan_enable unavailable)";
  if (g_runtime.canQueryVulkanExtensions) {
    if (load_xr_proc(g_runtime.instance, "xrGetVulkanInstanceExtensionsKHR", &proc, detail)) {
      const auto getInstanceExtensions = reinterpret_cast<PFN_xrGetVulkanInstanceExtensionsKHR>(proc);
      if (!read_vulkan_extension_string(getInstanceExtensions, g_runtime.instance, g_runtime.systemId,
                                        runtimeInstanceExtensions, detail, "xrGetVulkanInstanceExtensionsKHR")) {
        g_state.vulkanExtensionValidation = AURORA_XR_VULKAN_EXTENSION_VALIDATION_INSTANCE_QUERY_UNAVAILABLE;
        message = append_dawn_interop("cannot validate OpenXR runtime Vulkan instance extension requirements: " +
                                      detail);
        return false;
      }
    } else {
      g_state.vulkanExtensionValidation = AURORA_XR_VULKAN_EXTENSION_VALIDATION_INSTANCE_QUERY_UNAVAILABLE;
      message = append_dawn_interop("cannot validate OpenXR runtime Vulkan instance extension requirements: " +
                                    detail);
      return false;
    }

    if (load_xr_proc(g_runtime.instance, "xrGetVulkanDeviceExtensionsKHR", &proc, detail)) {
      const auto getDeviceExtensions = reinterpret_cast<PFN_xrGetVulkanDeviceExtensionsKHR>(proc);
      if (!read_vulkan_extension_string(getDeviceExtensions, g_runtime.instance, g_runtime.systemId,
                                        runtimeDeviceExtensions, detail, "xrGetVulkanDeviceExtensionsKHR")) {
        g_state.vulkanExtensionValidation = AURORA_XR_VULKAN_EXTENSION_VALIDATION_DEVICE_QUERY_UNAVAILABLE;
        message = append_dawn_interop("cannot validate OpenXR runtime Vulkan device extension requirements: " +
                                      detail);
        return false;
      }
    } else {
      g_state.vulkanExtensionValidation = AURORA_XR_VULKAN_EXTENSION_VALIDATION_DEVICE_QUERY_UNAVAILABLE;
      message = append_dawn_interop("cannot validate OpenXR runtime Vulkan device extension requirements: " +
                                    detail);
      return false;
    }

    if (!validate_runtime_vulkan_extensions(handles, runtimeInstanceExtensions, runtimeDeviceExtensions, message,
                                            g_state.vulkanExtensionValidation)) {
      return false;
    }
  }

  if (!load_xr_proc(g_runtime.instance, "xrGetVulkanGraphicsRequirements2KHR", &proc, detail)) {
    message = append_dawn_interop(detail);
    return false;
  }
  const auto getRequirements = reinterpret_cast<PFN_xrGetVulkanGraphicsRequirements2KHR>(proc);
  XrGraphicsRequirementsVulkanKHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR};
  result = getRequirements(g_runtime.instance, g_runtime.systemId, &requirements);
  if (XR_FAILED(result)) {
    message = append_dawn_interop("xrGetVulkanGraphicsRequirements2KHR failed: " + result_string(result));
    return false;
  }
  const XrVersion dawnVulkanVersion =
      XR_MAKE_VERSION(VK_API_VERSION_MAJOR(handles.apiVersion), VK_API_VERSION_MINOR(handles.apiVersion),
                      VK_API_VERSION_PATCH(handles.apiVersion));
  if (dawnVulkanVersion < requirements.minApiVersionSupported) {
    message = append_dawn_interop("Dawn Vulkan API version " + xr_version_string(dawnVulkanVersion) +
                                  " is below the OpenXR runtime minimum " +
                                  xr_version_string(requirements.minApiVersionSupported));
    return false;
  }
  if (requirements.maxApiVersionSupported != 0 && dawnVulkanVersion > requirements.maxApiVersionSupported) {
    message = append_dawn_interop("Dawn Vulkan API version " + xr_version_string(dawnVulkanVersion) +
                                  " is above the OpenXR runtime maximum " +
                                  xr_version_string(requirements.maxApiVersionSupported));
    return false;
  }

  if (!load_xr_proc(g_runtime.instance, "xrGetVulkanGraphicsDevice2KHR", &proc, detail)) {
    message = append_dawn_interop(detail);
    return false;
  }
  const auto getGraphicsDevice = reinterpret_cast<PFN_xrGetVulkanGraphicsDevice2KHR>(proc);
  XrVulkanGraphicsDeviceGetInfoKHR deviceGetInfo{XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};
  deviceGetInfo.systemId = g_runtime.systemId;
  deviceGetInfo.vulkanInstance = handles.instance;
  VkPhysicalDevice runtimePhysicalDevice = VK_NULL_HANDLE;
  trace_bootstrap("calling xrGetVulkanGraphicsDevice2KHR for persistent runtime");
  result = getGraphicsDevice(g_runtime.instance, &deviceGetInfo, &runtimePhysicalDevice);
  trace_bootstrap("returned from xrGetVulkanGraphicsDevice2KHR for persistent runtime");
  if (XR_FAILED(result) || runtimePhysicalDevice != handles.physicalDevice) {
    message = append_dawn_interop("OpenXR runtime rejected Dawn's Vulkan physical device: " + result_string(result));
    return false;
  }

  XrGraphicsBindingVulkanKHR binding{XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR};
  binding.instance = handles.instance;
  binding.physicalDevice = handles.physicalDevice;
  binding.device = handles.device;
  binding.queueFamilyIndex = handles.queueFamilyIndex;
  binding.queueIndex = handles.queueIndex;

  XrSessionCreateInfo sessionCreateInfo{XR_TYPE_SESSION_CREATE_INFO};
  sessionCreateInfo.next = &binding;
  sessionCreateInfo.systemId = g_runtime.systemId;
  trace_bootstrap("calling xrCreateSession for persistent runtime");
  result = xrCreateSession(g_runtime.instance, &sessionCreateInfo, &g_runtime.session);
  trace_bootstrap("returned from xrCreateSession for persistent runtime");
  if (XR_FAILED(result)) {
    message = append_dawn_interop("xrCreateSession with Dawn's Vulkan device failed: " + result_string(result));
    return false;
  }

  XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
  spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
  spaceInfo.poseInReferenceSpace.orientation.w = 1.0f;
  result = xrCreateReferenceSpace(g_runtime.session, &spaceInfo, &g_runtime.localSpace);
  if (XR_FAILED(result)) {
    message = append_dawn_interop("xrCreateReferenceSpace(XR_REFERENCE_SPACE_TYPE_LOCAL) failed: " +
                                  result_string(result));
    return false;
  }
  spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
  result = xrCreateReferenceSpace(g_runtime.session, &spaceInfo, &g_runtime.viewSpace);
  if (XR_FAILED(result)) {
    message = append_dawn_interop("xrCreateReferenceSpace(XR_REFERENCE_SPACE_TYPE_VIEW) failed: " +
                                  result_string(result));
    return false;
  }

  uint32_t formatCount = 0;
  result = xrEnumerateSwapchainFormats(g_runtime.session, 0, &formatCount, nullptr);
  if (XR_FAILED(result) || formatCount == 0) {
    message = append_dawn_interop("xrEnumerateSwapchainFormats failed: " + result_string(result));
    return false;
  }
  std::vector<int64_t> formats(formatCount);
  result = xrEnumerateSwapchainFormats(g_runtime.session, formatCount, &formatCount, formats.data());
  if (XR_FAILED(result)) {
    message = append_dawn_interop("failed to read OpenXR swapchain formats: " + result_string(result));
    return false;
  }
  formats.resize(formatCount);

  const std::array preferredFormats{
      preferred_vk_format(),
      static_cast<int64_t>(VK_FORMAT_R8G8B8A8_UNORM),
      static_cast<int64_t>(VK_FORMAT_B8G8R8A8_UNORM),
      static_cast<int64_t>(VK_FORMAT_R8G8B8A8_SRGB),
      static_cast<int64_t>(VK_FORMAT_B8G8R8A8_SRGB),
  };
  for (const int64_t candidate : preferredFormats) {
    if (std::find(formats.begin(), formats.end(), candidate) != formats.end()) {
      g_runtime.colorFormat = candidate;
      break;
    }
  }
  const wgpu::TextureFormat wgpuFormat = wgpu_format_from_vk_format(g_runtime.colorFormat);
  if (wgpuFormat == wgpu::TextureFormat::Undefined) {
    if (std::getenv("AURORA_XR_BOOTSTRAP_TRACE") != nullptr) {
      for (uint32_t i = 0; i < formatCount; ++i) {
        std::fprintf(stderr, "[aurora::xr::bootstrap] swapchainFormat[%u]=%lld\n", i,
                     static_cast<long long>(formats[i]));
      }
      std::fflush(stderr);
    }
    message = append_dawn_interop("OpenXR runtime does not expose a BGRA8/RGBA8 Vulkan color swapchain format");
    return false;
  }

  const uint32_t eyeCount = 2;
  g_runtime.eyes.resize(eyeCount);
  g_runtime.locatedViews.assign(eyeCount, {XR_TYPE_VIEW});
  g_runtime.layerViews.assign(eyeCount, {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW});
  views.clear();
  views.reserve(eyeCount);

  for (uint32_t eyeIndex = 0; eyeIndex < eyeCount; ++eyeIndex) {
    const XrViewConfigurationView& configView = g_runtime.configViews[eyeIndex];
    if (configView.recommendedImageRectWidth == 0 || configView.recommendedImageRectHeight == 0 ||
        configView.maxSwapchainSampleCount < XrSwapchainSampleCount) {
      message = append_dawn_interop("OpenXR primary stereo view configuration returned invalid dimensions or "
                                    "unsupported sample count");
      return false;
    }

    EyeSwapchain& eye = g_runtime.eyes[eyeIndex];
    eye.width = g_config.openXREyeWidth != 0 ? std::min(g_config.openXREyeWidth, MaxConfiguredEyeDimension)
                                             : configView.recommendedImageRectWidth;
    eye.height = g_config.openXREyeHeight != 0 ? std::min(g_config.openXREyeHeight, MaxConfiguredEyeDimension)
                                               : configView.recommendedImageRectHeight;

    XrSwapchainCreateInfo swapchainCreateInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    swapchainCreateInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT |
                                     XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    swapchainCreateInfo.format = g_runtime.colorFormat;
    swapchainCreateInfo.sampleCount = XrSwapchainSampleCount;
    swapchainCreateInfo.width = eye.width;
    swapchainCreateInfo.height = eye.height;
    swapchainCreateInfo.faceCount = 1;
    swapchainCreateInfo.arraySize = 1;
    swapchainCreateInfo.mipCount = 1;
    result = xrCreateSwapchain(g_runtime.session, &swapchainCreateInfo, &eye.swapchain);
    if (XR_FAILED(result)) {
      message = append_dawn_interop("xrCreateSwapchain failed for eye " + std::to_string(eyeIndex) + ": " +
                                    result_string(result));
      return false;
    }

    std::vector<XrSwapchainImageVulkanKHR> xrImages;
    if (!enumerate_vulkan_swapchain_images(eye.swapchain, xrImages, message)) {
      message += " for eye ";
      message += std::to_string(eyeIndex);
      return false;
    }
    eye.images.assign(xrImages.size(), {});
    for (uint32_t imageIndex = 0; imageIndex < xrImages.size(); ++imageIndex) {
      eye.images[imageIndex].xrImage = xrImages[imageIndex];
    }

    eye.depth = webgpu::create_depth_texture(eye.width, eye.height, XrSwapchainSampleCount);

    AuroraXRView view{};
    view.recommendedWidth = eye.width;
    view.recommendedHeight = eye.height;
    view.recommendedSampleCount = XrSwapchainSampleCount;
    views.push_back(view);
  }

  FlatUiSwapchain& flatUi = g_runtime.flatUi;
  flatUi.width = g_config.openXREyeWidth != 0 ? std::min(g_config.openXREyeWidth, MaxConfiguredEyeDimension)
                                              : g_runtime.eyes[0].width;
  flatUi.height = g_config.openXREyeHeight != 0 ? std::min(g_config.openXREyeHeight, MaxConfiguredEyeDimension)
                                                : g_runtime.eyes[0].height;

  XrSwapchainCreateInfo flatUiSwapchainInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
  flatUiSwapchainInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_SRC_BIT |
                                   XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
  flatUiSwapchainInfo.format = g_runtime.colorFormat;
  flatUiSwapchainInfo.sampleCount = XrSwapchainSampleCount;
  flatUiSwapchainInfo.width = flatUi.width;
  flatUiSwapchainInfo.height = flatUi.height;
  flatUiSwapchainInfo.faceCount = 1;
  flatUiSwapchainInfo.arraySize = 1;
  flatUiSwapchainInfo.mipCount = 1;
  result = xrCreateSwapchain(g_runtime.session, &flatUiSwapchainInfo, &flatUi.swapchain);
  if (XR_FAILED(result)) {
    message = append_dawn_interop("xrCreateSwapchain failed for flat UI: " + result_string(result));
    return false;
  }

  std::vector<XrSwapchainImageVulkanKHR> flatUiImages;
  if (!enumerate_vulkan_swapchain_images(flatUi.swapchain, flatUiImages, message)) {
    message += " for flat UI";
    return false;
  }
  flatUi.images.assign(flatUiImages.size(), {});
  for (uint32_t imageIndex = 0; imageIndex < flatUiImages.size(); ++imageIndex) {
    flatUi.images[imageIndex].xrImage = flatUiImages[imageIndex];
  }
  flatUi.depth = webgpu::create_depth_texture(flatUi.width, flatUi.height, XrSwapchainSampleCount);

  message = append_dawn_interop("OpenXR Vulkan session initialized with Dawn-owned device and per-eye swapchain "
                                "render targets plus a flat UI quad swapchain; runtime Vulkan instance extensions: [" +
                                runtimeInstanceExtensions + "]; runtime Vulkan device extensions: [" +
                                runtimeDeviceExtensions + "]");
  return true;
}

void poll_runtime_events() noexcept {
  XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
  while (xrPollEvent(g_runtime.instance, &event) == XR_SUCCESS) {
    if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
      const auto& stateEvent = *reinterpret_cast<const XrEventDataSessionStateChanged*>(&event);
      if (stateEvent.session == g_runtime.session) {
        g_runtime.sessionState = stateEvent.state;
        if (stateEvent.state == XR_SESSION_STATE_READY && !g_runtime.sessionRunning) {
          XrSessionBeginInfo beginInfo{XR_TYPE_SESSION_BEGIN_INFO};
          beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
          if (XR_SUCCEEDED(xrBeginSession(g_runtime.session, &beginInfo))) {
            g_runtime.sessionRunning = true;
            set_status(AURORA_XR_ACTIVE, g_state.statusMessage);
          }
        } else if (stateEvent.state == XR_SESSION_STATE_STOPPING && g_runtime.sessionRunning) {
          xrEndSession(g_runtime.session);
          g_runtime.sessionRunning = false;
          set_status(AURORA_XR_READY, "OpenXR session stopped; waiting for runtime");
        } else if (stateEvent.state == XR_SESSION_STATE_EXITING ||
                   stateEvent.state == XR_SESSION_STATE_LOSS_PENDING) {
          g_runtime.sessionRunning = false;
          set_status(AURORA_XR_LOST, "OpenXR session is exiting or loss is pending");
        }
      }
    } else if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
      g_runtime.sessionRunning = false;
      set_status(AURORA_XR_LOST, "OpenXR instance loss is pending");
    }
    event = {XR_TYPE_EVENT_DATA_BUFFER};
  }
}

void begin_runtime_frame() noexcept {
  for (EyeSwapchain& eye : g_runtime.eyes) {
    eye.acquired = false;
    eye.rendered = false;
    eye.acquiredImageIndex = std::numeric_limits<uint32_t>::max();
  }
  g_runtime.flatUi.acquired = false;
  g_runtime.flatUi.rendered = false;
  g_runtime.flatUi.acquiredImageIndex = std::numeric_limits<uint32_t>::max();
  g_runtime.frameBegun = false;
  g_state.frameState.shouldRender = false;

  poll_runtime_events();
  if (!g_runtime.sessionRunning) {
    return;
  }

  g_runtime.xrFrameState = {XR_TYPE_FRAME_STATE};
  XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
  XrResult result = xrWaitFrame(g_runtime.session, &waitInfo, &g_runtime.xrFrameState);
  if (XR_FAILED(result)) {
    set_status(AURORA_XR_LOST, "xrWaitFrame failed: " + result_string(result));
    return;
  }

  XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
  result = xrBeginFrame(g_runtime.session, &beginInfo);
  if (XR_FAILED(result)) {
    set_status(AURORA_XR_LOST, "xrBeginFrame failed: " + result_string(result));
    return;
  }
  g_runtime.frameBegun = true;

  if (!g_runtime.xrFrameState.shouldRender) {
    return;
  }

  XrViewLocateInfo locateInfo{XR_TYPE_VIEW_LOCATE_INFO};
  locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
  locateInfo.displayTime = g_runtime.xrFrameState.predictedDisplayTime;
  locateInfo.space = g_runtime.localSpace;
  XrViewState viewState{XR_TYPE_VIEW_STATE};
  uint32_t locatedViewCount = 0;
  result = xrLocateViews(g_runtime.session, &locateInfo, &viewState,
                         static_cast<uint32_t>(g_runtime.locatedViews.size()), &locatedViewCount,
                         g_runtime.locatedViews.data());
  if (XR_FAILED(result) || locatedViewCount != g_runtime.eyes.size()) {
    return;
  }

  for (uint32_t i = 0; i < locatedViewCount && i < g_state.views.size(); ++i) {
    update_view_from_xr(g_state.views[i], g_runtime.locatedViews[i], viewState.viewStateFlags);
  }
  g_state.frameState.shouldRender = true;
}

void end_runtime_frame_after_submit() noexcept {
  if (!g_runtime.frameBegun) {
    return;
  }

  bool submitProjectionLayer = !g_runtime.suppressProjectionLayers && g_state.frameState.shouldRender &&
                               g_runtime.eyes.size() == g_runtime.layerViews.size();
  bool hasAcquiredSwapchainImage = g_runtime.flatUi.acquired;
  for (const EyeSwapchain& eye : g_runtime.eyes) {
    hasAcquiredSwapchainImage = hasAcquiredSwapchainImage || eye.acquired;
  }
  if (hasAcquiredSwapchainImage) {
    std::string syncMessage;
    if (!wait_dawn_vulkan_queue_idle(syncMessage)) {
      set_status(AURORA_XR_LOST, std::move(syncMessage));
      submitProjectionLayer = false;
    }
  }
  for (uint32_t eyeIndex = 0; eyeIndex < g_runtime.eyes.size(); ++eyeIndex) {
    EyeSwapchain& eye = g_runtime.eyes[eyeIndex];
    submitProjectionLayer = submitProjectionLayer && eye.rendered;
    if (eye.acquired) {
      const uint32_t acquiredImageIndex = eye.acquiredImageIndex;
      XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
      const XrResult result = xrReleaseSwapchainImage(eye.swapchain, &releaseInfo);
      if (XR_FAILED(result)) {
        set_status(AURORA_XR_LOST, "xrReleaseSwapchainImage failed: " + result_string(result));
        submitProjectionLayer = false;
      }
      eye.acquired = false;
      eye.acquiredImageIndex = std::numeric_limits<uint32_t>::max();
      if (acquiredImageIndex < eye.images.size()) {
        eye.images[acquiredImageIndex].mirrorBindGroup = {};
        eye.images[acquiredImageIndex].view = {};
        eye.images[acquiredImageIndex].texture = {};
      }
    }
  }

  bool submitFlatUiLayer = !g_runtime.suppressProjectionLayers && g_state.frameState.shouldRender &&
                           g_runtime.flatUi.rendered && g_runtime.flatUi.swapchain != XR_NULL_HANDLE;
  if (g_runtime.flatUi.acquired) {
    const uint32_t acquiredImageIndex = g_runtime.flatUi.acquiredImageIndex;
    XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    const XrResult result = xrReleaseSwapchainImage(g_runtime.flatUi.swapchain, &releaseInfo);
    if (XR_FAILED(result)) {
      set_status(AURORA_XR_LOST, "xrReleaseSwapchainImage failed for flat UI: " + result_string(result));
      submitFlatUiLayer = false;
    }
    g_runtime.flatUi.acquired = false;
    g_runtime.flatUi.acquiredImageIndex = std::numeric_limits<uint32_t>::max();
    if (acquiredImageIndex < g_runtime.flatUi.images.size()) {
      g_runtime.flatUi.images[acquiredImageIndex].view = {};
      g_runtime.flatUi.images[acquiredImageIndex].texture = {};
      g_runtime.flatUi.images[acquiredImageIndex].mirrorBindGroup = {};
    }
  }

  XrCompositionLayerProjection projectionLayer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
  std::array<const XrCompositionLayerBaseHeader*, 2> layers{};
  uint32_t layerCount = 0;
  if (submitProjectionLayer) {
    projectionLayer.space = g_runtime.localSpace;
    projectionLayer.viewCount = static_cast<uint32_t>(g_runtime.layerViews.size());
    projectionLayer.views = g_runtime.layerViews.data();
    layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projectionLayer);
  }
  if (submitFlatUiLayer) {
    layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&g_runtime.flatUiLayer);
  }

  XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
  endInfo.displayTime = g_runtime.xrFrameState.predictedDisplayTime;
  endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
  endInfo.layerCount = layerCount;
  endInfo.layers = layerCount != 0 ? layers.data() : nullptr;
  const XrResult result = xrEndFrame(g_runtime.session, &endInfo);
  if (XR_FAILED(result)) {
    set_status(AURORA_XR_LOST, "xrEndFrame failed: " + result_string(result));
  }
  g_runtime.frameBegun = false;
}
#endif

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

void clear_dawn_openxr_vulkan_hooks() noexcept;

void prepare_dawn_openxr_vulkan_hooks(const AuroraConfig& config, AuroraBackend selectedBackend) noexcept {
  if (!config.enableOpenXR || selectedBackend != BACKEND_VULKAN) {
    clear_dawn_openxr_vulkan_hooks();
    return;
  }

#if defined(AURORA_HAS_OPENXR) && defined(AURORA_ENABLE_GX) && defined(AURORA_DAWN_OPENXR_HANDLES)
  const char* enableRuntimeCreate = std::getenv("AURORA_XR_USE_RUNTIME_VULKAN_CREATE");
  if (enableRuntimeCreate != nullptr && (enableRuntimeCreate[0] == '\0' || enableRuntimeCreate[0] == '0')) {
    clear_dawn_openxr_vulkan_hooks();
    return;
  }

  if (g_runtime.dawnOpenXRHooksInstalled) {
    return;
  }

  std::string message;
  if (!ensure_openxr_instance_system_views(message)) {
    Log.warn("OpenXR Vulkan bootstrap unavailable before Dawn initialization: {}", message);
    return;
  }

  webgpu::set_openxr_vulkan_device_create_callback(&g_runtime, openxr_create_vulkan_device);
  g_runtime.dawnOpenXRHooksInstalled = true;
  Log.info("OpenXR Vulkan bootstrap installed runtime-mediated Dawn creation hooks");
#else
  (void)config;
  (void)selectedBackend;
#endif
}

void clear_dawn_openxr_vulkan_hooks() noexcept {
#if defined(AURORA_ENABLE_GX) && defined(AURORA_DAWN_OPENXR_HANDLES)
  webgpu::clear_openxr_vulkan_hooks();
  g_runtime.dawnOpenXRHooksInstalled = false;
#endif
}

void initialize(const AuroraConfig& config, AuroraBackend selectedBackend) noexcept {
  g_state = {};
  g_state.requested = config.enableOpenXR;
  g_state.required = config.requireOpenXR;
  g_state.sbsMirrorEnabled = env_flag_enabled("AURORA_XR_MIRROR_SBS");
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
#if defined(AURORA_ENABLE_GX) && defined(AURORA_DAWN_OPENXR_HANDLES)
  const char* skipProof = std::getenv("AURORA_XR_SKIP_DAWN_PROOF");
  const bool skipDawnProof = skipProof != nullptr && skipProof[0] != '\0' && skipProof[0] != '0';
  if (!g_runtime.dawnOpenXRHooksInstalled && !skipDawnProof) {
    ProbeResult proof = probe_openxr(selectedBackend);
    const bool proofCleared =
        proof.status == AURORA_XR_READY || proof.status == AURORA_XR_ACTIVE ||
        proof.message.find("cleared both eye swapchain images successfully") != std::string::npos;
    if (!proofCleared) {
      g_state.views = std::move(proof.views);
      set_status(proof.status, std::move(proof.message));
      sync_frame_state();
      Log.report(g_state.status == AURORA_XR_UNAVAILABLE ? LOG_WARNING : LOG_INFO, "OpenXR {}: {}",
                 status_name(g_state.status), g_state.statusMessage);
      return;
    }
  } else {
    Log.info("OpenXR skipping separate Dawn/OpenXR proof session before persistent runtime initialization");
  }

  std::string runtimeMessage;
  std::vector<AuroraXRView> runtimeViews;
  if (initialize_runtime(runtimeMessage, runtimeViews)) {
    g_state.views = std::move(runtimeViews);
    set_status(AURORA_XR_READY, std::move(runtimeMessage));
  } else {
    const AuroraXRStatus failureStatus =
        runtimeMessage.find("runtime unavailable") != std::string::npos ? AURORA_XR_UNAVAILABLE : AURORA_XR_BLOCKED;
    reset_runtime();
    set_status(failureStatus, std::move(runtimeMessage));
  }
#else
  ProbeResult result = probe_openxr(selectedBackend);
  g_state.views = std::move(result.views);
  set_status(result.status, std::move(result.message));
#endif
#else
  set_status(AURORA_XR_UNAVAILABLE,
             "Aurora was built without OpenXR SDK support; configure with AURORA_ENABLE_OPENXR=ON and an OpenXR loader SDK");
#endif

  sync_frame_state();
  Log.report(g_state.status == AURORA_XR_UNAVAILABLE ? LOG_WARNING : LOG_INFO, "OpenXR {}: {}",
             status_name(g_state.status), g_state.statusMessage);
}

void shutdown() noexcept {
  clear_dawn_openxr_vulkan_hooks();
#if defined(AURORA_HAS_OPENXR) && defined(AURORA_ENABLE_GX) && defined(AURORA_DAWN_OPENXR_HANDLES)
  reset_runtime();
#endif
  g_state = {};
}

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
#if defined(AURORA_HAS_OPENXR) && defined(AURORA_ENABLE_GX) && defined(AURORA_DAWN_OPENXR_HANDLES)
  if (g_runtime.instance != XR_NULL_HANDLE && g_runtime.session != XR_NULL_HANDLE) {
    begin_runtime_frame();
  } else
#endif
  {
    g_state.frameState.shouldRender = false;
  }
  sync_frame_state();
}

void end_frame_after_submit() noexcept {
#if defined(AURORA_HAS_OPENXR) && defined(AURORA_ENABLE_GX) && defined(AURORA_DAWN_OPENXR_HANDLES)
  end_runtime_frame_after_submit();
#endif
  g_state.eyeActive = false;
  g_state.activeEyeIndex = std::numeric_limits<uint32_t>::max();
  g_state.flatUiActive = false;
  sync_frame_state();
}

AuroraXRStatus status() noexcept { return g_state.status; }

const char* status_message() noexcept { return g_state.statusMessage.c_str(); }

AuroraXRVulkanExtensionValidation vulkan_extension_validation() noexcept {
  return g_state.vulkanExtensionValidation;
}

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

bool sbs_mirror_enabled() noexcept {
  return g_state.sbsMirrorEnabled;
}

bool get_sbs_mirror_eyes(std::array<SbsMirrorEye, 2>& outEyes) noexcept {
#if defined(AURORA_HAS_OPENXR) && defined(AURORA_ENABLE_GX) && defined(AURORA_DAWN_OPENXR_HANDLES)
  if (!sbs_mirror_enabled() || g_runtime.eyes.size() < outEyes.size()) {
    return false;
  }
  for (size_t i = 0; i < outEyes.size(); ++i) {
    const EyeSwapchain& eye = g_runtime.eyes[i];
    if (!eye.acquired || eye.acquiredImageIndex >= eye.images.size()) {
      return false;
    }
    const EyeSwapchainImage& image = eye.images[eye.acquiredImageIndex];
    if (image.texture == nullptr || image.view == nullptr) {
      return false;
    }
    if (image.mirrorBindGroup == nullptr) {
      return false;
    }
    outEyes[i] = {
        .bindGroup = image.mirrorBindGroup,
        .width = eye.width,
        .height = eye.height,
    };
  }
  return true;
#else
  return false;
#endif
}

bool get_default_mirror_eye(SbsMirrorEye& outEye) noexcept {
  outEye = {};
#if defined(AURORA_HAS_OPENXR) && defined(AURORA_ENABLE_GX) && defined(AURORA_DAWN_OPENXR_HANDLES)
  if (g_runtime.eyes.empty()) {
    return false;
  }
  const EyeSwapchain& eye = g_runtime.eyes[0];
  if (!eye.acquired || eye.acquiredImageIndex >= eye.images.size()) {
    return false;
  }
  const EyeSwapchainImage& image = eye.images[eye.acquiredImageIndex];
  if (image.mirrorBindGroup == nullptr) {
    return false;
  }
  outEye = {
      .bindGroup = image.mirrorBindGroup,
      .width = eye.width,
      .height = eye.height,
  };
  return true;
#else
  return false;
#endif
}

bool acquire_flat_ui_target(bool installEfbTargets) noexcept {
#if defined(AURORA_HAS_OPENXR) && defined(AURORA_ENABLE_GX) && defined(AURORA_DAWN_OPENXR_HANDLES)
  if (!should_render() || g_state.eyeActive || g_state.flatUiActive) {
    return false;
  }

  FlatUiSwapchain& flatUi = g_runtime.flatUi;
  if (!g_runtime.frameBegun || flatUi.swapchain == XR_NULL_HANDLE) {
    return false;
  }

  if (!flatUi.acquired) {
    if (flatUi.rendered) {
      return false;
    }

    XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    XrResult result = xrAcquireSwapchainImage(flatUi.swapchain, &acquireInfo, &flatUi.acquiredImageIndex);
    if (XR_FAILED(result) || flatUi.acquiredImageIndex >= flatUi.images.size()) {
      set_status(AURORA_XR_LOST, "xrAcquireSwapchainImage failed for flat UI: " + result_string(result));
      return false;
    }
    flatUi.acquired = true;

    XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    waitInfo.timeout = XR_INFINITE_DURATION;
    result = xrWaitSwapchainImage(flatUi.swapchain, &waitInfo);
    if (XR_FAILED(result)) {
      XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
      xrReleaseSwapchainImage(flatUi.swapchain, &releaseInfo);
      flatUi.acquired = false;
      flatUi.acquiredImageIndex = std::numeric_limits<uint32_t>::max();
      set_status(AURORA_XR_LOST, "xrWaitSwapchainImage failed for flat UI: " + result_string(result));
      return false;
    }

    FlatUiSwapchainImage& image = flatUi.images[flatUi.acquiredImageIndex];
    const wgpu::TextureFormat wgpuFormat = wgpu_format_from_vk_format(g_runtime.colorFormat);
    const wgpu::TextureDescriptor wrapperDescriptor{
        .label = "OpenXR flat UI swapchain image",
        .usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding |
                 wgpu::TextureUsage::CopySrc | wgpu::TextureUsage::CopyDst,
        .dimension = wgpu::TextureDimension::e2D,
        .size = {.width = flatUi.width, .height = flatUi.height, .depthOrArrayLayers = 1},
        .format = wgpuFormat,
        .mipLevelCount = 1,
        .sampleCount = XrSwapchainSampleCount,
    };
    image.texture = webgpu::wrap_dawn_vulkan_swapchain_image(image.xrImage.image, wrapperDescriptor);
    if (image.texture == nullptr) {
      XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
      xrReleaseSwapchainImage(flatUi.swapchain, &releaseInfo);
      flatUi.acquired = false;
      flatUi.acquiredImageIndex = std::numeric_limits<uint32_t>::max();
      set_status(AURORA_XR_LOST, "Dawn failed to wrap acquired OpenXR flat UI swapchain image");
      return false;
    }
    image.view = image.texture.CreateView();
    const webgpu::TextureWithSampler source{
        .texture = image.texture,
        .view = image.view,
        .size = {.width = flatUi.width, .height = flatUi.height, .depthOrArrayLayers = 1},
        .format = wgpuFormat,
        .sampler = webgpu::g_frameBuffer.sampler,
    };
    image.mirrorBindGroup = webgpu::create_copy_bind_group(source);

    g_runtime.flatUiLayer = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    g_runtime.flatUiLayer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    g_runtime.flatUiLayer.space = g_runtime.viewSpace;
    g_runtime.flatUiLayer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    g_runtime.flatUiLayer.subImage.swapchain = flatUi.swapchain;
    g_runtime.flatUiLayer.subImage.imageRect.offset = {0, 0};
    g_runtime.flatUiLayer.subImage.imageRect.extent = {static_cast<int32_t>(flatUi.width),
                                                       static_cast<int32_t>(flatUi.height)};
    g_runtime.flatUiLayer.subImage.imageArrayIndex = 0;
    g_runtime.flatUiLayer.pose.orientation.w = 1.0f;
    g_runtime.flatUiLayer.pose.position.z = -1.25f;
    g_runtime.flatUiLayer.size = {1.35f, 1.35f * static_cast<float>(flatUi.height) / static_cast<float>(flatUi.width)};
  }

  if (installEfbTargets) {
    FlatUiSwapchainImage& image = flatUi.images[flatUi.acquiredImageIndex];
    const gfx::EfbRenderTargets targets{
        .colorView = image.view,
        .depthView = flatUi.depth.view,
        .copySourceTexture = image.texture,
        .copySourceView = image.view,
        .copySourceDepthView = flatUi.depth.view,
        .targetSize = {.width = flatUi.width, .height = flatUi.height, .depthOrArrayLayers = 1},
        .msaaSamples = XrSwapchainSampleCount,
    };
    if (!gfx::set_efb_render_targets(targets, true)) {
      return false;
    }
  }

  return true;
#else
  return false;
#endif
}

bool ensure_flat_ui_target() noexcept {
  if (g_state.flatUiActive) {
    return false;
  }
  if (!acquire_flat_ui_target(false)) {
    return false;
  }
#if defined(AURORA_HAS_OPENXR) && defined(AURORA_ENABLE_GX) && defined(AURORA_DAWN_OPENXR_HANDLES)
  g_runtime.flatUi.rendered = true;
#endif
  return true;
}

bool get_flat_ui_target(FlatUiTarget& outTarget) noexcept {
  outTarget = {};
#if defined(AURORA_HAS_OPENXR) && defined(AURORA_ENABLE_GX) && defined(AURORA_DAWN_OPENXR_HANDLES)
  const FlatUiSwapchain& flatUi = g_runtime.flatUi;
  if (!flatUi.acquired || flatUi.acquiredImageIndex >= flatUi.images.size()) {
    return false;
  }
  const FlatUiSwapchainImage& image = flatUi.images[flatUi.acquiredImageIndex];
  if (image.texture == nullptr || image.view == nullptr) {
    return false;
  }
  outTarget = {
      .texture = image.texture,
      .view = image.view,
      .bindGroup = image.mirrorBindGroup,
      .width = flatUi.width,
      .height = flatUi.height,
  };
  return true;
#else
  return false;
#endif
}

bool begin_eye(uint32_t eyeIndex) noexcept {
  if (!should_render() || eyeIndex >= g_state.views.size() || g_state.eyeActive || g_state.flatUiActive) {
    return false;
  }
#if defined(AURORA_HAS_OPENXR) && defined(AURORA_ENABLE_GX) && defined(AURORA_DAWN_OPENXR_HANDLES)
  if (eyeIndex >= g_runtime.eyes.size() || !g_runtime.frameBegun) {
    return false;
  }

  EyeSwapchain& eye = g_runtime.eyes[eyeIndex];
  XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
  XrResult result = xrAcquireSwapchainImage(eye.swapchain, &acquireInfo, &eye.acquiredImageIndex);
  if (XR_FAILED(result) || eye.acquiredImageIndex >= eye.images.size()) {
    set_status(AURORA_XR_LOST, "xrAcquireSwapchainImage failed: " + result_string(result));
    return false;
  }
  eye.acquired = true;

  XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
  waitInfo.timeout = XR_INFINITE_DURATION;
  result = xrWaitSwapchainImage(eye.swapchain, &waitInfo);
  if (XR_FAILED(result)) {
    XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xrReleaseSwapchainImage(eye.swapchain, &releaseInfo);
    eye.acquired = false;
    set_status(AURORA_XR_LOST, "xrWaitSwapchainImage failed: " + result_string(result));
    return false;
  }

  EyeSwapchainImage& image = eye.images[eye.acquiredImageIndex];
  const wgpu::TextureFormat wgpuFormat = wgpu_format_from_vk_format(g_runtime.colorFormat);
  const wgpu::TextureDescriptor wrapperDescriptor{
      .label = "OpenXR swapchain image",
      .usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopySrc |
               wgpu::TextureUsage::CopyDst,
      .dimension = wgpu::TextureDimension::e2D,
      .size = {.width = eye.width, .height = eye.height, .depthOrArrayLayers = 1},
      .format = wgpuFormat,
      .mipLevelCount = 1,
      .sampleCount = XrSwapchainSampleCount,
  };
  image.texture = webgpu::wrap_dawn_vulkan_swapchain_image(image.xrImage.image, wrapperDescriptor);
  if (image.texture == nullptr) {
    XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xrReleaseSwapchainImage(eye.swapchain, &releaseInfo);
    eye.acquired = false;
    eye.acquiredImageIndex = std::numeric_limits<uint32_t>::max();
    set_status(AURORA_XR_LOST, "Dawn failed to wrap acquired OpenXR swapchain image");
    return false;
  }
  image.view = image.texture.CreateView();
  {
    const webgpu::TextureWithSampler source{
        .texture = image.texture,
        .view = image.view,
        .size = {.width = eye.width, .height = eye.height, .depthOrArrayLayers = 1},
        .format = wgpuFormat,
        .sampler = webgpu::g_frameBuffer.sampler,
    };
    image.mirrorBindGroup = webgpu::create_copy_bind_group(source);
  }

  const gfx::EfbRenderTargets targets{
      .colorView = image.view,
      .depthView = eye.depth.view,
      .copySourceTexture = image.texture,
      .copySourceView = image.view,
      .copySourceDepthView = eye.depth.view,
      .targetSize = {.width = eye.width, .height = eye.height, .depthOrArrayLayers = 1},
      .msaaSamples = XrSwapchainSampleCount,
  };
  if (!gfx::set_efb_render_targets(targets, true)) {
    XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xrReleaseSwapchainImage(eye.swapchain, &releaseInfo);
    image.mirrorBindGroup = {};
    image.view = {};
    image.texture = {};
    eye.acquired = false;
    eye.acquiredImageIndex = std::numeric_limits<uint32_t>::max();
    return false;
  }

  XrCompositionLayerProjectionView& layerView = g_runtime.layerViews[eyeIndex];
  layerView = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
  layerView.pose = g_runtime.locatedViews[eyeIndex].pose;
  layerView.fov = g_runtime.locatedViews[eyeIndex].fov;
  layerView.subImage.swapchain = eye.swapchain;
  layerView.subImage.imageRect.offset = {0, 0};
  layerView.subImage.imageRect.extent = {static_cast<int32_t>(eye.width), static_cast<int32_t>(eye.height)};
  layerView.subImage.imageArrayIndex = 0;

  g_state.eyeActive = true;
  g_state.activeEyeIndex = eyeIndex;
  return true;
#else
  return false;
#endif
}

void end_eye() noexcept {
#if defined(AURORA_HAS_OPENXR) && defined(AURORA_ENABLE_GX) && defined(AURORA_DAWN_OPENXR_HANDLES)
  if (g_state.eyeActive && g_state.activeEyeIndex < g_runtime.eyes.size()) {
    g_runtime.eyes[g_state.activeEyeIndex].rendered = true;
    gfx::restore_default_efb_render_targets();
  }
#endif
  g_state.eyeActive = false;
  g_state.activeEyeIndex = std::numeric_limits<uint32_t>::max();
}

bool begin_flat_ui() noexcept {
  if (!acquire_flat_ui_target(true)) {
    return false;
  }
  g_state.flatUiActive = true;
  return true;
}

void end_flat_ui() noexcept {
#if defined(AURORA_HAS_OPENXR) && defined(AURORA_ENABLE_GX) && defined(AURORA_DAWN_OPENXR_HANDLES)
  if (g_state.flatUiActive) {
    g_runtime.flatUi.rendered = true;
    gfx::restore_default_efb_render_targets();
  }
#endif
  g_state.flatUiActive = false;
}
} // namespace aurora::xr

extern "C" {
AuroraXRStatus aurora_xr_get_status() { return aurora::xr::status(); }
const char* aurora_xr_get_status_message() { return aurora::xr::status_message(); }
AuroraXRVulkanExtensionValidation aurora_xr_get_vulkan_extension_validation() {
  return aurora::xr::vulkan_extension_validation();
}
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
