#include "xr.hpp"

#if defined(AURORA_ENABLE_GX) && defined(AURORA_DAWN_OPENXR_HANDLES)
#define XR_USE_GRAPHICS_API_VULKAN 1
#include "../webgpu/gpu.hpp"
#endif

#ifdef AURORA_ENABLE_GX
#include "../webgpu/dawn_vulkan_interop.hpp"
#endif

#include <openxr/openxr.h>
#if defined(AURORA_ENABLE_GX) && defined(AURORA_DAWN_OPENXR_HANDLES)
#include <openxr/openxr_platform.h>
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace aurora::xr {
namespace {
constexpr const char* VulkanEnable2Extension = "XR_KHR_vulkan_enable2";
constexpr XrVersion RequestedOpenXRApiVersion = XR_MAKE_VERSION(1, 0, 0);

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
  case XR_SPACE_BOUNDS_UNAVAILABLE:
    return "XR_SPACE_BOUNDS_UNAVAILABLE";
  case XR_SESSION_NOT_FOCUSED:
    return "XR_SESSION_NOT_FOCUSED";
  case XR_FRAME_DISCARDED:
    return "XR_FRAME_DISCARDED";
  case XR_ERROR_VALIDATION_FAILURE:
    return "XR_ERROR_VALIDATION_FAILURE";
  case XR_ERROR_RUNTIME_FAILURE:
    return "XR_ERROR_RUNTIME_FAILURE";
  case XR_ERROR_OUT_OF_MEMORY:
    return "XR_ERROR_OUT_OF_MEMORY";
  case XR_ERROR_API_VERSION_UNSUPPORTED:
    return "XR_ERROR_API_VERSION_UNSUPPORTED";
  case XR_ERROR_INITIALIZATION_FAILED:
    return "XR_ERROR_INITIALIZATION_FAILED";
  case XR_ERROR_FUNCTION_UNSUPPORTED:
    return "XR_ERROR_FUNCTION_UNSUPPORTED";
  case XR_ERROR_FEATURE_UNSUPPORTED:
    return "XR_ERROR_FEATURE_UNSUPPORTED";
  case XR_ERROR_EXTENSION_NOT_PRESENT:
    return "XR_ERROR_EXTENSION_NOT_PRESENT";
  case XR_ERROR_LIMIT_REACHED:
    return "XR_ERROR_LIMIT_REACHED";
  case XR_ERROR_SIZE_INSUFFICIENT:
    return "XR_ERROR_SIZE_INSUFFICIENT";
  case XR_ERROR_HANDLE_INVALID:
    return "XR_ERROR_HANDLE_INVALID";
  case XR_ERROR_INSTANCE_LOST:
    return "XR_ERROR_INSTANCE_LOST";
  case XR_ERROR_SESSION_RUNNING:
    return "XR_ERROR_SESSION_RUNNING";
  case XR_ERROR_SESSION_NOT_RUNNING:
    return "XR_ERROR_SESSION_NOT_RUNNING";
  case XR_ERROR_SESSION_LOST:
    return "XR_ERROR_SESSION_LOST";
  case XR_ERROR_SYSTEM_INVALID:
    return "XR_ERROR_SYSTEM_INVALID";
  case XR_ERROR_PATH_INVALID:
    return "XR_ERROR_PATH_INVALID";
  case XR_ERROR_PATH_COUNT_EXCEEDED:
    return "XR_ERROR_PATH_COUNT_EXCEEDED";
  case XR_ERROR_PATH_FORMAT_INVALID:
    return "XR_ERROR_PATH_FORMAT_INVALID";
  case XR_ERROR_PATH_UNSUPPORTED:
    return "XR_ERROR_PATH_UNSUPPORTED";
  case XR_ERROR_LAYER_INVALID:
    return "XR_ERROR_LAYER_INVALID";
  case XR_ERROR_LAYER_LIMIT_EXCEEDED:
    return "XR_ERROR_LAYER_LIMIT_EXCEEDED";
  case XR_ERROR_SWAPCHAIN_RECT_INVALID:
    return "XR_ERROR_SWAPCHAIN_RECT_INVALID";
  case XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED:
    return "XR_ERROR_SWAPCHAIN_FORMAT_UNSUPPORTED";
  case XR_ERROR_ACTION_TYPE_MISMATCH:
    return "XR_ERROR_ACTION_TYPE_MISMATCH";
  case XR_ERROR_SESSION_NOT_READY:
    return "XR_ERROR_SESSION_NOT_READY";
  case XR_ERROR_SESSION_NOT_STOPPING:
    return "XR_ERROR_SESSION_NOT_STOPPING";
  case XR_ERROR_TIME_INVALID:
    return "XR_ERROR_TIME_INVALID";
  case XR_ERROR_REFERENCE_SPACE_UNSUPPORTED:
    return "XR_ERROR_REFERENCE_SPACE_UNSUPPORTED";
  case XR_ERROR_FILE_ACCESS_ERROR:
    return "XR_ERROR_FILE_ACCESS_ERROR";
  case XR_ERROR_FILE_CONTENTS_INVALID:
    return "XR_ERROR_FILE_CONTENTS_INVALID";
  case XR_ERROR_FORM_FACTOR_UNSUPPORTED:
    return "XR_ERROR_FORM_FACTOR_UNSUPPORTED";
  case XR_ERROR_FORM_FACTOR_UNAVAILABLE:
    return "XR_ERROR_FORM_FACTOR_UNAVAILABLE";
  case XR_ERROR_API_LAYER_NOT_PRESENT:
    return "XR_ERROR_API_LAYER_NOT_PRESENT";
  case XR_ERROR_CALL_ORDER_INVALID:
    return "XR_ERROR_CALL_ORDER_INVALID";
  case XR_ERROR_GRAPHICS_DEVICE_INVALID:
    return "XR_ERROR_GRAPHICS_DEVICE_INVALID";
  case XR_ERROR_POSE_INVALID:
    return "XR_ERROR_POSE_INVALID";
  case XR_ERROR_INDEX_OUT_OF_RANGE:
    return "XR_ERROR_INDEX_OUT_OF_RANGE";
  case XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED:
    return "XR_ERROR_VIEW_CONFIGURATION_TYPE_UNSUPPORTED";
  case XR_ERROR_ENVIRONMENT_BLEND_MODE_UNSUPPORTED:
    return "XR_ERROR_ENVIRONMENT_BLEND_MODE_UNSUPPORTED";
  case XR_ERROR_NAME_DUPLICATED:
    return "XR_ERROR_NAME_DUPLICATED";
  case XR_ERROR_NAME_INVALID:
    return "XR_ERROR_NAME_INVALID";
  case XR_ERROR_ACTIONSET_NOT_ATTACHED:
    return "XR_ERROR_ACTIONSET_NOT_ATTACHED";
  case XR_ERROR_ACTIONSETS_ALREADY_ATTACHED:
    return "XR_ERROR_ACTIONSETS_ALREADY_ATTACHED";
  case XR_ERROR_LOCALIZED_NAME_DUPLICATED:
    return "XR_ERROR_LOCALIZED_NAME_DUPLICATED";
  case XR_ERROR_LOCALIZED_NAME_INVALID:
    return "XR_ERROR_LOCALIZED_NAME_INVALID";
  case XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING:
    return "XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING";
  case XR_ERROR_RUNTIME_UNAVAILABLE:
    return "XR_ERROR_RUNTIME_UNAVAILABLE";
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
#ifdef AURORA_ENABLE_GX
  message += "; Dawn interop: ";
  message += aurora::webgpu::probe_dawn_vulkan_interop().message;
#endif
  return message;
}

#if defined(AURORA_ENABLE_GX) && defined(AURORA_DAWN_OPENXR_HANDLES)
std::string vk_result_string(VkResult result) {
  return std::string{"VkResult("} + std::to_string(static_cast<int>(result)) + ")";
}

std::string xr_version_string(XrVersion version) {
  return std::to_string(XR_VERSION_MAJOR(version)) + "." + std::to_string(XR_VERSION_MINOR(version)) + "." +
         std::to_string(XR_VERSION_PATCH(version));
}

const char* vk_format_name(int64_t format) noexcept {
  switch (format) {
  case VK_FORMAT_B8G8R8A8_UNORM:
    return "VK_FORMAT_B8G8R8A8_UNORM";
  case VK_FORMAT_R8G8B8A8_UNORM:
    return "VK_FORMAT_R8G8B8A8_UNORM";
  default:
    return nullptr;
  }
}

std::string vk_format_string(int64_t format) {
  std::string message = std::string{"VkFormat("} + std::to_string(format);
  if (const char* name = vk_format_name(format); name != nullptr) {
    message += " ";
    message += name;
  }
  message += ")";
  return message;
}

bool dawn_vulkan_handles_ready(const dawn::native::vulkan::VulkanDeviceHandles& handles) noexcept {
  return handles.instance != VK_NULL_HANDLE && handles.physicalDevice != VK_NULL_HANDLE &&
         handles.device != VK_NULL_HANDLE && handles.queue != VK_NULL_HANDLE &&
         handles.queueFamilyIndex != std::numeric_limits<uint32_t>::max();
}
#endif

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

#if defined(AURORA_ENABLE_GX) && defined(AURORA_DAWN_OPENXR_HANDLES)
struct DawnOpenXRProofResult {
  bool succeeded = false;
  std::string message;
};

constexpr uint32_t ProofSwapchainSampleCount = 1;

int64_t preferred_vk_format() noexcept {
  switch (webgpu::g_graphicsConfig.surfaceConfiguration.format) {
  case wgpu::TextureFormat::BGRA8Unorm:
  case wgpu::TextureFormat::BGRA8UnormSrgb:
    return VK_FORMAT_B8G8R8A8_UNORM;
  case wgpu::TextureFormat::RGBA8Unorm:
  case wgpu::TextureFormat::RGBA8UnormSrgb:
    return VK_FORMAT_R8G8B8A8_UNORM;
  default:
    return VK_FORMAT_R8G8B8A8_UNORM;
  }
}

wgpu::TextureFormat wgpu_format_from_vk_format(int64_t vkFormat) noexcept {
  switch (vkFormat) {
  case VK_FORMAT_B8G8R8A8_UNORM:
    return wgpu::TextureFormat::BGRA8Unorm;
  case VK_FORMAT_R8G8B8A8_UNORM:
    return wgpu::TextureFormat::RGBA8Unorm;
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

struct VulkanClearFns {
  PFN_vkGetPhysicalDeviceProperties getPhysicalDeviceProperties = nullptr;
  PFN_vkGetDeviceProcAddr getDeviceProcAddr = nullptr;
  PFN_vkCreateCommandPool createCommandPool = nullptr;
  PFN_vkDestroyCommandPool destroyCommandPool = nullptr;
  PFN_vkAllocateCommandBuffers allocateCommandBuffers = nullptr;
  PFN_vkFreeCommandBuffers freeCommandBuffers = nullptr;
  PFN_vkBeginCommandBuffer beginCommandBuffer = nullptr;
  PFN_vkEndCommandBuffer endCommandBuffer = nullptr;
  PFN_vkCmdPipelineBarrier cmdPipelineBarrier = nullptr;
  PFN_vkCmdClearColorImage cmdClearColorImage = nullptr;
  PFN_vkQueueSubmit queueSubmit = nullptr;
  PFN_vkQueueWaitIdle queueWaitIdle = nullptr;
};

bool load_vulkan_clear_fns(const dawn::native::vulkan::VulkanDeviceHandles& handles, VulkanClearFns& fns,
                           std::string& message) {
  fns.getPhysicalDeviceProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
      webgpu::get_dawn_vulkan_instance_proc_addr("vkGetPhysicalDeviceProperties"));
  fns.getDeviceProcAddr =
      reinterpret_cast<PFN_vkGetDeviceProcAddr>(webgpu::get_dawn_vulkan_instance_proc_addr("vkGetDeviceProcAddr"));
  if (fns.getPhysicalDeviceProperties == nullptr || fns.getDeviceProcAddr == nullptr) {
    message = "patched Dawn did not expose required Vulkan loader entry points";
    return false;
  }

  const auto loadDeviceProc = [&](const char* name) {
    return fns.getDeviceProcAddr(handles.device, name);
  };
  fns.createCommandPool = reinterpret_cast<PFN_vkCreateCommandPool>(loadDeviceProc("vkCreateCommandPool"));
  fns.destroyCommandPool = reinterpret_cast<PFN_vkDestroyCommandPool>(loadDeviceProc("vkDestroyCommandPool"));
  fns.allocateCommandBuffers = reinterpret_cast<PFN_vkAllocateCommandBuffers>(loadDeviceProc("vkAllocateCommandBuffers"));
  fns.freeCommandBuffers = reinterpret_cast<PFN_vkFreeCommandBuffers>(loadDeviceProc("vkFreeCommandBuffers"));
  fns.beginCommandBuffer = reinterpret_cast<PFN_vkBeginCommandBuffer>(loadDeviceProc("vkBeginCommandBuffer"));
  fns.endCommandBuffer = reinterpret_cast<PFN_vkEndCommandBuffer>(loadDeviceProc("vkEndCommandBuffer"));
  fns.cmdPipelineBarrier = reinterpret_cast<PFN_vkCmdPipelineBarrier>(loadDeviceProc("vkCmdPipelineBarrier"));
  fns.cmdClearColorImage = reinterpret_cast<PFN_vkCmdClearColorImage>(loadDeviceProc("vkCmdClearColorImage"));
  fns.queueSubmit = reinterpret_cast<PFN_vkQueueSubmit>(loadDeviceProc("vkQueueSubmit"));
  fns.queueWaitIdle = reinterpret_cast<PFN_vkQueueWaitIdle>(loadDeviceProc("vkQueueWaitIdle"));

  if (fns.createCommandPool == nullptr || fns.destroyCommandPool == nullptr || fns.allocateCommandBuffers == nullptr ||
      fns.freeCommandBuffers == nullptr || fns.beginCommandBuffer == nullptr || fns.endCommandBuffer == nullptr ||
      fns.cmdPipelineBarrier == nullptr || fns.cmdClearColorImage == nullptr || fns.queueSubmit == nullptr ||
      fns.queueWaitIdle == nullptr) {
    message = "failed to load Vulkan command functions from Dawn's VkDevice";
    return false;
  }
  return true;
}

bool clear_openxr_swapchain_image(const dawn::native::vulkan::VulkanDeviceHandles& handles,
                                  const VulkanClearFns& fns, VkImage image, std::string& message) {
  VkCommandPool commandPool = VK_NULL_HANDLE;
  VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

  VkCommandPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  poolInfo.queueFamilyIndex = handles.queueFamilyIndex;

  VkResult vkResult = fns.createCommandPool(handles.device, &poolInfo, nullptr, &commandPool);
  if (vkResult != VK_SUCCESS) {
    message = "vkCreateCommandPool failed: " + vk_result_string(vkResult);
    return false;
  }

  auto destroyPool = [&] {
    if (commandPool != VK_NULL_HANDLE) {
      fns.destroyCommandPool(handles.device, commandPool, nullptr);
      commandPool = VK_NULL_HANDLE;
    }
  };

  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = commandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = 1;
  vkResult = fns.allocateCommandBuffers(handles.device, &allocInfo, &commandBuffer);
  if (vkResult != VK_SUCCESS) {
    destroyPool();
    message = "vkAllocateCommandBuffers failed: " + vk_result_string(vkResult);
    return false;
  }

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkResult = fns.beginCommandBuffer(commandBuffer, &beginInfo);
  if (vkResult != VK_SUCCESS) {
    fns.freeCommandBuffers(handles.device, commandPool, 1, &commandBuffer);
    destroyPool();
    message = "vkBeginCommandBuffer failed: " + vk_result_string(vkResult);
    return false;
  }

  VkImageMemoryBarrier transferBarrier{};
  transferBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  transferBarrier.srcAccessMask = 0;
  transferBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  transferBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  transferBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  transferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  transferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  transferBarrier.image = image;
  transferBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  transferBarrier.subresourceRange.baseMipLevel = 0;
  transferBarrier.subresourceRange.levelCount = 1;
  transferBarrier.subresourceRange.baseArrayLayer = 0;
  transferBarrier.subresourceRange.layerCount = 1;
  fns.cmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &transferBarrier);

  VkClearColorValue clearColor{};
  clearColor.float32[0] = 0.0f;
  clearColor.float32[1] = 0.1f;
  clearColor.float32[2] = 0.2f;
  clearColor.float32[3] = 1.0f;
  VkImageSubresourceRange clearRange{};
  clearRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  clearRange.baseMipLevel = 0;
  clearRange.levelCount = 1;
  clearRange.baseArrayLayer = 0;
  clearRange.layerCount = 1;
  fns.cmdClearColorImage(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &clearRange);

  VkImageMemoryBarrier colorBarrier = transferBarrier;
  colorBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  colorBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  colorBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  colorBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  fns.cmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &colorBarrier);

  vkResult = fns.endCommandBuffer(commandBuffer);
  if (vkResult != VK_SUCCESS) {
    fns.freeCommandBuffers(handles.device, commandPool, 1, &commandBuffer);
    destroyPool();
    message = "vkEndCommandBuffer failed: " + vk_result_string(vkResult);
    return false;
  }

  vkResult = fns.queueWaitIdle(handles.queue);
  if (vkResult != VK_SUCCESS) {
    fns.freeCommandBuffers(handles.device, commandPool, 1, &commandBuffer);
    destroyPool();
    message = "pre-clear vkQueueWaitIdle failed: " + vk_result_string(vkResult);
    return false;
  }

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;
  vkResult = fns.queueSubmit(handles.queue, 1, &submitInfo, VK_NULL_HANDLE);
  if (vkResult != VK_SUCCESS) {
    fns.freeCommandBuffers(handles.device, commandPool, 1, &commandBuffer);
    destroyPool();
    message = "vkQueueSubmit failed: " + vk_result_string(vkResult);
    return false;
  }

  vkResult = fns.queueWaitIdle(handles.queue);
  fns.freeCommandBuffers(handles.device, commandPool, 1, &commandBuffer);
  destroyPool();
  if (vkResult != VK_SUCCESS) {
    message = "post-clear vkQueueWaitIdle failed: " + vk_result_string(vkResult);
    return false;
  }
  return true;
}

DawnOpenXRProofResult run_dawn_openxr_vulkan_clear_proof(XrInstance instance, XrSystemId systemId,
                                                         const std::vector<XrViewConfigurationView>& xrViews) {
  dawn::native::vulkan::VulkanDeviceHandles handles{};
  if (!webgpu::get_dawn_vulkan_handles(&handles)) {
    return {.message = "patched Dawn Vulkan handle query failed"};
  }
  if (!dawn_vulkan_handles_ready(handles)) {
    return {.message = "patched Dawn returned incomplete Vulkan handles or queue family metadata"};
  }

  VulkanClearFns vkFns{};
  std::string detail;
  if (!load_vulkan_clear_fns(handles, vkFns, detail)) {
    return {.message = detail};
  }

  PFN_xrVoidFunction proc = nullptr;
  if (!load_xr_proc(instance, "xrGetVulkanGraphicsRequirements2KHR", &proc, detail)) {
    return {.message = detail};
  }
  const auto getRequirements = reinterpret_cast<PFN_xrGetVulkanGraphicsRequirements2KHR>(proc);
  XrGraphicsRequirementsVulkanKHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR};
  XrResult xrResult = getRequirements(instance, systemId, &requirements);
  if (XR_FAILED(xrResult)) {
    return {.message = "xrGetVulkanGraphicsRequirements2KHR failed: " + result_string(xrResult)};
  }

  if (!load_xr_proc(instance, "xrGetVulkanGraphicsDevice2KHR", &proc, detail)) {
    return {.message = detail};
  }
  const auto getGraphicsDevice = reinterpret_cast<PFN_xrGetVulkanGraphicsDevice2KHR>(proc);
  XrVulkanGraphicsDeviceGetInfoKHR deviceGetInfo{XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};
  deviceGetInfo.systemId = systemId;
  deviceGetInfo.vulkanInstance = handles.instance;
  VkPhysicalDevice runtimePhysicalDevice = VK_NULL_HANDLE;
  xrResult = getGraphicsDevice(instance, &deviceGetInfo, &runtimePhysicalDevice);
  if (XR_FAILED(xrResult)) {
    return {.message = "xrGetVulkanGraphicsDevice2KHR failed for Dawn's Vulkan instance: " +
                       result_string(xrResult)};
  }
  if (runtimePhysicalDevice != handles.physicalDevice) {
    VkPhysicalDeviceProperties runtimeProperties{};
    VkPhysicalDeviceProperties dawnProperties{};
    if (runtimePhysicalDevice != VK_NULL_HANDLE) {
      vkFns.getPhysicalDeviceProperties(runtimePhysicalDevice, &runtimeProperties);
    }
    vkFns.getPhysicalDeviceProperties(handles.physicalDevice, &dawnProperties);
    return {.message = "OpenXR runtime selected a different Vulkan physical device (" +
                       std::string{runtimeProperties.deviceName} + ") than Dawn (" +
                       std::string{dawnProperties.deviceName} + ")"};
  }

  VkPhysicalDeviceProperties physicalDeviceProperties{};
  vkFns.getPhysicalDeviceProperties(handles.physicalDevice, &physicalDeviceProperties);
  const XrVersion dawnVulkanVersion =
      XR_MAKE_VERSION(VK_API_VERSION_MAJOR(physicalDeviceProperties.apiVersion),
                      VK_API_VERSION_MINOR(physicalDeviceProperties.apiVersion),
                      VK_API_VERSION_PATCH(physicalDeviceProperties.apiVersion));
  if (dawnVulkanVersion < requirements.minApiVersionSupported) {
    return {.message = "Dawn Vulkan device API version " + xr_version_string(dawnVulkanVersion) +
                       " is below the OpenXR runtime minimum " +
                       xr_version_string(requirements.minApiVersionSupported)};
  }
  if (requirements.maxApiVersionSupported != 0 && dawnVulkanVersion > requirements.maxApiVersionSupported) {
    return {.message = "Dawn Vulkan device API version " + xr_version_string(dawnVulkanVersion) +
                       " is above the OpenXR runtime maximum " +
                       xr_version_string(requirements.maxApiVersionSupported)};
  }

  XrGraphicsBindingVulkanKHR binding{XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR};
  binding.instance = handles.instance;
  binding.physicalDevice = handles.physicalDevice;
  binding.device = handles.device;
  binding.queueFamilyIndex = handles.queueFamilyIndex;
  binding.queueIndex = handles.queueIndex;

  XrSessionCreateInfo sessionCreateInfo{XR_TYPE_SESSION_CREATE_INFO};
  sessionCreateInfo.next = &binding;
  sessionCreateInfo.systemId = systemId;

  XrSession session = XR_NULL_HANDLE;
  xrResult = xrCreateSession(instance, &sessionCreateInfo, &session);
  if (XR_FAILED(xrResult)) {
    return {.message = "xrCreateSession with Dawn's Vulkan device failed: " + result_string(xrResult)};
  }

  auto destroySession = [&] {
    if (session != XR_NULL_HANDLE) {
      xrDestroySession(session);
      session = XR_NULL_HANDLE;
    }
  };

  uint32_t formatCount = 0;
  xrResult = xrEnumerateSwapchainFormats(session, 0, &formatCount, nullptr);
  if (XR_FAILED(xrResult) || formatCount == 0) {
    destroySession();
    return {.message = "xrEnumerateSwapchainFormats failed: " + result_string(xrResult)};
  }
  std::vector<int64_t> formats(formatCount);
  xrResult = xrEnumerateSwapchainFormats(session, formatCount, &formatCount, formats.data());
  if (XR_FAILED(xrResult)) {
    destroySession();
    return {.message = "failed to read OpenXR swapchain formats: " + result_string(xrResult)};
  }
  formats.resize(formatCount);

  const std::array preferredFormats{
      preferred_vk_format(),
      static_cast<int64_t>(VK_FORMAT_R8G8B8A8_UNORM),
      static_cast<int64_t>(VK_FORMAT_B8G8R8A8_UNORM),
  };
  int64_t selectedFormat = 0;
  for (const int64_t candidate : preferredFormats) {
    if (std::find(formats.begin(), formats.end(), candidate) != formats.end()) {
      selectedFormat = candidate;
      break;
    }
  }
  if (selectedFormat == 0) {
    destroySession();
    return {.message = "OpenXR runtime does not expose a BGRA8/RGBA8 Vulkan color swapchain format; format count=" +
                       std::to_string(formats.size())};
  }

  const uint32_t eyeCount = std::min<uint32_t>(2, static_cast<uint32_t>(xrViews.size()));
  if (eyeCount != 2) {
    destroySession();
    return {.message = "OpenXR primary stereo view configuration did not provide two eyes"};
  }

  for (uint32_t eye = 0; eye < eyeCount; ++eye) {
    if (xrViews[eye].recommendedImageRectWidth == 0 || xrViews[eye].recommendedImageRectHeight == 0 ||
        xrViews[eye].maxSwapchainSampleCount < ProofSwapchainSampleCount) {
      destroySession();
      return {.message = "OpenXR primary stereo view configuration returned invalid dimensions or unsupported proof "
                         "sample count"};
    }

    XrSwapchainCreateInfo swapchainCreateInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    swapchainCreateInfo.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainCreateInfo.format = selectedFormat;
    swapchainCreateInfo.sampleCount = ProofSwapchainSampleCount;
    swapchainCreateInfo.width = xrViews[eye].recommendedImageRectWidth;
    swapchainCreateInfo.height = xrViews[eye].recommendedImageRectHeight;
    swapchainCreateInfo.faceCount = 1;
    swapchainCreateInfo.arraySize = 1;
    swapchainCreateInfo.mipCount = 1;

    XrSwapchain swapchain = XR_NULL_HANDLE;
    xrResult = xrCreateSwapchain(session, &swapchainCreateInfo, &swapchain);
    if (XR_FAILED(xrResult)) {
      destroySession();
      return {.message = "xrCreateSwapchain failed for proof eye " + std::to_string(eye) + " (" +
                         std::to_string(swapchainCreateInfo.width) + "x" +
                         std::to_string(swapchainCreateInfo.height) + " samples=" +
                         std::to_string(swapchainCreateInfo.sampleCount) + " format=" +
                         vk_format_string(selectedFormat) + " recommendedSamples=" +
                         std::to_string(xrViews[eye].recommendedSwapchainSampleCount) + " maxSamples=" +
                         std::to_string(xrViews[eye].maxSwapchainSampleCount) + "): " +
                         result_string(xrResult)};
    }

    auto destroySwapchain = [&] {
      if (swapchain != XR_NULL_HANDLE) {
        xrDestroySwapchain(swapchain);
        swapchain = XR_NULL_HANDLE;
      }
    };

    uint32_t imageCount = 0;
    xrResult = xrEnumerateSwapchainImages(swapchain, 0, &imageCount, nullptr);
    if (XR_FAILED(xrResult) || imageCount == 0) {
      destroySwapchain();
      destroySession();
      return {.message = "xrEnumerateSwapchainImages failed for proof eye " + std::to_string(eye) + ": " +
                         result_string(xrResult)};
    }
    std::vector<XrSwapchainImageVulkanKHR> images(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
    xrResult = xrEnumerateSwapchainImages(swapchain, imageCount, &imageCount,
                                          reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()));
    if (XR_FAILED(xrResult)) {
      destroySwapchain();
      destroySession();
      return {.message = "failed to read Vulkan swapchain images for proof eye " + std::to_string(eye) + ": " +
                         result_string(xrResult)};
    }

    uint32_t imageIndex = 0;
    XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    xrResult = xrAcquireSwapchainImage(swapchain, &acquireInfo, &imageIndex);
    if (XR_FAILED(xrResult) || imageIndex >= images.size()) {
      destroySwapchain();
      destroySession();
      return {.message = "xrAcquireSwapchainImage failed for proof eye " + std::to_string(eye) + ": " +
                         result_string(xrResult)};
    }

    XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    waitInfo.timeout = XR_INFINITE_DURATION;
    xrResult = xrWaitSwapchainImage(swapchain, &waitInfo);
    if (XR_FAILED(xrResult)) {
      XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
      xrReleaseSwapchainImage(swapchain, &releaseInfo);
      destroySwapchain();
      destroySession();
      return {.message = "xrWaitSwapchainImage failed for proof eye " + std::to_string(eye) + ": " +
                         result_string(xrResult)};
    }

    if (!clear_openxr_swapchain_image(handles, vkFns, images[imageIndex].image, detail)) {
      XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
      xrReleaseSwapchainImage(swapchain, &releaseInfo);
      destroySwapchain();
      destroySession();
      return {.message = "Vulkan clear failed for proof eye " + std::to_string(eye) + ": " + detail};
    }

    const wgpu::TextureFormat wgpuFormat = wgpu_format_from_vk_format(selectedFormat);
    if (wgpuFormat == wgpu::TextureFormat::Undefined) {
      XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
      xrReleaseSwapchainImage(swapchain, &releaseInfo);
      destroySwapchain();
      destroySession();
      return {.message = "selected OpenXR Vulkan format cannot be mapped to a WebGPU texture format"};
    }
    const wgpu::TextureDescriptor wrapperDescriptor{
        .label = "OpenXR proof swapchain image",
        .usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopyDst,
        .dimension = wgpu::TextureDimension::e2D,
        .size =
            wgpu::Extent3D{
                .width = swapchainCreateInfo.width,
                .height = swapchainCreateInfo.height,
                .depthOrArrayLayers = 1,
        },
        .format = wgpuFormat,
        .mipLevelCount = 1,
        .sampleCount = swapchainCreateInfo.sampleCount,
    };
    if (webgpu::wrap_dawn_vulkan_swapchain_image(images[imageIndex].image, wrapperDescriptor) == nullptr) {
      XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
      xrReleaseSwapchainImage(swapchain, &releaseInfo);
      destroySwapchain();
      destroySession();
      return {.message = "Dawn failed to wrap OpenXR runtime-owned VkImage for proof eye " + std::to_string(eye) +
                         " (" + std::to_string(swapchainCreateInfo.width) + "x" +
                         std::to_string(swapchainCreateInfo.height) + " samples=" +
                         std::to_string(swapchainCreateInfo.sampleCount) + " format=" +
                         vk_format_string(selectedFormat) + " imageCount=" + std::to_string(images.size()) + ")"};
    }

    XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xrResult = xrReleaseSwapchainImage(swapchain, &releaseInfo);
    destroySwapchain();
    if (XR_FAILED(xrResult)) {
      destroySession();
      return {.message = "xrReleaseSwapchainImage failed for proof eye " + std::to_string(eye) + ": " +
                         result_string(xrResult)};
    }
  }

  destroySession();
  return {.succeeded = true,
          .message =
              "Dawn Vulkan device created an OpenXR Vulkan session and cleared both eye swapchain images successfully"
              " using " +
              vk_format_string(selectedFormat)};
}
#endif
} // namespace

ProbeResult probe_openxr(AuroraBackend selectedBackend) noexcept {
  if (selectedBackend != BACKEND_VULKAN) {
    return blocked("OpenXR desktop launch requires BACKEND_VULKAN");
  }

  uint32_t extensionCount = 0;
  XrResult result = xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr);
  if (XR_FAILED(result)) {
    return unavailable(append_dawn_interop("OpenXR runtime unavailable while enumerating instance extensions: " +
                                           result_string(result) + runtime_hint()));
  }

  std::vector<XrExtensionProperties> extensions(extensionCount, {XR_TYPE_EXTENSION_PROPERTIES});
  result = xrEnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount, extensions.data());
  if (XR_FAILED(result)) {
    return unavailable(append_dawn_interop("OpenXR runtime unavailable while reading instance extensions: " +
                                           result_string(result) + runtime_hint()));
  }
  extensions.resize(extensionCount);

  if (!extension_available(extensions, VulkanEnable2Extension)) {
    return blocked(
        append_dawn_interop("OpenXR runtime does not expose XR_KHR_vulkan_enable2, required for desktop Vulkan launch"));
  }

  const std::array enabledExtensions{VulkanEnable2Extension};
  XrInstanceCreateInfo createInfo{XR_TYPE_INSTANCE_CREATE_INFO};
  std::strncpy(createInfo.applicationInfo.applicationName, "Aurora", XR_MAX_APPLICATION_NAME_SIZE - 1);
  createInfo.applicationInfo.applicationVersion = 1;
  std::strncpy(createInfo.applicationInfo.engineName, "Aurora", XR_MAX_ENGINE_NAME_SIZE - 1);
  createInfo.applicationInfo.engineVersion = 1;
  createInfo.applicationInfo.apiVersion = RequestedOpenXRApiVersion;
  createInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size());
  createInfo.enabledExtensionNames = enabledExtensions.data();

  XrInstance instance = XR_NULL_HANDLE;
  result = xrCreateInstance(&createInfo, &instance);
  if (XR_FAILED(result)) {
    return unavailable(append_dawn_interop("OpenXR instance creation failed: " + result_string(result) + runtime_hint()));
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
    return unavailable(append_dawn_interop("No OpenXR head-mounted-display system available: " +
                                           result_string(result) + runtime_hint()));
  }

  uint32_t viewCount = 0;
  result = xrEnumerateViewConfigurationViews(instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount,
                                             nullptr);
  if (XR_FAILED(result) || viewCount == 0) {
    destroyInstance();
    return blocked(append_dawn_interop("OpenXR runtime does not expose a primary stereo view configuration: " +
                                       result_string(result)));
  }

  std::vector<XrViewConfigurationView> xrViews(viewCount, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
  result = xrEnumerateViewConfigurationViews(instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, viewCount,
                                             &viewCount, xrViews.data());
  if (XR_FAILED(result)) {
    destroyInstance();
    return blocked(append_dawn_interop("Failed to query OpenXR primary stereo view configuration: " +
                                       result_string(result)));
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

#if defined(AURORA_ENABLE_GX) && defined(AURORA_DAWN_OPENXR_HANDLES)
  const auto proof = run_dawn_openxr_vulkan_clear_proof(instance, systemId, xrViews);
  destroyInstance();
  if (proof.succeeded) {
    return blocked(append_dawn_interop(proof.message + "; Aurora EFB per-eye render target wiring is not implemented yet"),
                   std::move(views));
  }
  return blocked(append_dawn_interop(
                     "OpenXR runtime and stereo view configuration detected, but Dawn/OpenXR Vulkan proof is blocked: " +
                     proof.message),
                 std::move(views));
#else
  destroyInstance();
  return blocked(append_dawn_interop("OpenXR runtime and stereo view configuration detected, but XR rendering is blocked"),
                 std::move(views));
#endif
}
} // namespace aurora::xr
