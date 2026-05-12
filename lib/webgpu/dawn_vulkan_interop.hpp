#pragma once

#include <string>

#include <webgpu/webgpu_cpp.h>

#if defined(AURORA_DAWN_OPENXR_HANDLES)
#include <dawn/native/VulkanBackend.h>
#endif

namespace aurora::webgpu {
struct DawnVulkanInteropStatus {
  bool backendVulkan = false;
  bool hasVkInstance = false;
  bool hasDeviceHandles = false;
  bool hasSwapchainImageWrapper = false;
  std::string message;
};

DawnVulkanInteropStatus probe_dawn_vulkan_interop() noexcept;

#if defined(AURORA_DAWN_OPENXR_HANDLES)
bool get_dawn_vulkan_handles(dawn::native::vulkan::VulkanDeviceHandles* outHandles) noexcept;
PFN_vkVoidFunction get_dawn_vulkan_instance_proc_addr(const char* name) noexcept;
bool wrap_dawn_vulkan_swapchain_image(VkImage image, const wgpu::TextureDescriptor& textureDescriptor) noexcept;
#endif
} // namespace aurora::webgpu
