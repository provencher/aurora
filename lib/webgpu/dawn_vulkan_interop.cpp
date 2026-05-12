#include "dawn_vulkan_interop.hpp"

#include "gpu.hpp"

#include <webgpu/webgpu_cpp.h>

#if defined(WEBGPU_DAWN) && defined(AURORA_DAWN_OPENXR_HANDLES)
#include <dawn/native/VulkanBackend.h>
#endif

#include <limits>

namespace aurora::webgpu {
#ifdef AURORA_DAWN_OPENXR_HANDLES
bool get_dawn_vulkan_handles(dawn::native::vulkan::VulkanDeviceHandles* outHandles) noexcept {
  if (outHandles == nullptr || !g_device || g_backendType != wgpu::BackendType::Vulkan) {
    return false;
  }
  return dawn::native::vulkan::GetDeviceHandles(g_device.Get(), outHandles);
}

PFN_vkVoidFunction get_dawn_vulkan_instance_proc_addr(const char* name) noexcept {
  if (name == nullptr || !g_device || g_backendType != wgpu::BackendType::Vulkan) {
    return nullptr;
  }
  return dawn::native::vulkan::GetInstanceProcAddr(g_device.Get(), name);
}

wgpu::Texture wrap_dawn_vulkan_swapchain_image(VkImage image,
                                               const wgpu::TextureDescriptor& textureDescriptor) noexcept {
  if (image == VK_NULL_HANDLE || !g_device || g_backendType != wgpu::BackendType::Vulkan) {
    return {};
  }

  const WGPUTextureDescriptor& nativeDescriptor = textureDescriptor;
  const dawn::native::vulkan::VulkanSwapchainImageDescriptor descriptor{
      .textureDescriptor = &nativeDescriptor,
      .image = image,
  };
  return wgpu::Texture::Acquire(dawn::native::vulkan::WrapVulkanSwapchainImage(g_device.Get(), &descriptor));
}
#endif

DawnVulkanInteropStatus probe_dawn_vulkan_interop() noexcept {
  DawnVulkanInteropStatus status{};

  if (g_backendType != wgpu::BackendType::Vulkan) {
    status.message = "Dawn backend is not Vulkan";
    return status;
  }
  status.backendVulkan = true;

  if (!g_device) {
    status.message = "Dawn Vulkan device is not initialized";
    return status;
  }

#ifdef AURORA_DAWN_OPENXR_HANDLES
  dawn::native::vulkan::VulkanDeviceHandles handles{};
  if (!get_dawn_vulkan_handles(&handles)) {
    status.message = "patched Dawn Vulkan handle query failed";
    return status;
  }
  status.hasVkInstance = handles.instance != VK_NULL_HANDLE;
  status.hasDeviceHandles = status.hasVkInstance && handles.physicalDevice != VK_NULL_HANDLE &&
                            handles.device != VK_NULL_HANDLE && handles.queue != VK_NULL_HANDLE &&
                            handles.queueFamilyIndex != std::numeric_limits<uint32_t>::max();
  status.hasSwapchainImageWrapper = &dawn::native::vulkan::WrapVulkanSwapchainImage != nullptr;
  status.message =
      status.hasDeviceHandles && status.hasSwapchainImageWrapper
          ? "patched Dawn exposes Vulkan instance, physical device, device, queue family, queue, and runtime-owned VkImage wrapping"
          : "patched Dawn returned incomplete Vulkan OpenXR interop support";
#else
  status.message =
      "current Dawn package does not expose VkPhysicalDevice, VkDevice, VkQueue, or queue family handles; use AURORA_DAWN_PROVIDER=vendor, AURORA_DAWN_LINKAGE=static, and AURORA_DAWN_APPLY_OPENXR_PATCH=ON for the OpenXR device/queue proof";
#endif

  return status;
}
} // namespace aurora::webgpu
