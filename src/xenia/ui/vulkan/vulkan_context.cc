/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/vulkan/vulkan_context.h"

#include <mutex>
#include <string>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/profiling.h"
#include "xenia/ui/vulkan/vulkan.h"
#include "xenia/ui/vulkan/vulkan_device.h"
#include "xenia/ui/vulkan/vulkan_immediate_drawer.h"
#include "xenia/ui/vulkan/vulkan_instance.h"
#include "xenia/ui/vulkan/vulkan_provider.h"
#include "xenia/ui/vulkan/vulkan_swap_chain.h"
#include "xenia/ui/vulkan/vulkan_util.h"
#include "xenia/ui/window.h"

namespace xe {
namespace ui {
namespace vulkan {

VulkanContext::VulkanContext(VulkanProvider* provider, Window* target_window)
    : GraphicsContext(provider, target_window) {}

VulkanContext::~VulkanContext() {
  auto provider = static_cast<VulkanProvider*>(provider_);
  auto device = provider->device();
  vkQueueWaitIdle(device->primary_queue());
  immediate_drawer_.reset();
  swap_chain_.reset();
  if (cmd_pool_) {
    vkDestroyCommandPool(*device, cmd_pool_, nullptr);
  }
}

bool VulkanContext::Initialize() {
  auto provider = static_cast<VulkanProvider*>(provider_);
  auto device = provider->device();

  // All context-specific commands will be allocated from this.
  // We may want to have additional pools for different rendering subsystems.
  VkCommandPoolCreateInfo cmd_pool_info;
  cmd_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  cmd_pool_info.pNext = nullptr;
  cmd_pool_info.queueFamilyIndex = device->queue_family_index();
  cmd_pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  auto err = vkCreateCommandPool(*device, &cmd_pool_info, nullptr, &cmd_pool_);
  CheckResult(err, "vkCreateCommandPool");

  if (target_window_) {
    // Create swap chain used to present to the window.
    VkSurfaceKHR surface = nullptr;
#if XE_PLATFORM_WIN32
    VkWin32SurfaceCreateInfoKHR create_info;
    create_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    create_info.pNext = nullptr;
    create_info.flags = 0;
    create_info.hinstance =
        static_cast<HINSTANCE>(target_window_->native_platform_handle());
    create_info.hwnd = static_cast<HWND>(target_window_->native_handle());
    err = vkCreateWin32SurfaceKHR(*provider->instance(), &create_info, nullptr,
                                  &surface);
    CheckResult(err, "vkCreateWin32SurfaceKHR");
#else
#error Platform not yet implemented.
#endif  // XE_PLATFORM_WIN32
    swap_chain_ = std::make_unique<VulkanSwapChain>(provider->instance(),
                                                    provider->device());
    if (!swap_chain_->Initialize(surface)) {
      XELOGE("Unable to initialize swap chain");
      vkDestroySurfaceKHR(*provider->instance(), surface, nullptr);
      return false;
    }

    // Only initialize immediate mode drawer if we are not an offscreen context.
    immediate_drawer_ = std::make_unique<VulkanImmediateDrawer>(this);
  }

  return true;
}

ImmediateDrawer* VulkanContext::immediate_drawer() {
  return immediate_drawer_.get();
}

VulkanInstance* VulkanContext::instance() const {
  return static_cast<VulkanProvider*>(provider_)->instance();
}

VulkanDevice* VulkanContext::device() const {
  return static_cast<VulkanProvider*>(provider_)->device();
}

bool VulkanContext::is_current() { return false; }

bool VulkanContext::MakeCurrent() {
  SCOPE_profile_cpu_f("gpu");
  return true;
}

void VulkanContext::ClearCurrent() {}

void VulkanContext::BeginSwap() {
  SCOPE_profile_cpu_f("gpu");
  auto provider = static_cast<VulkanProvider*>(provider_);
  auto device = provider->device();

  // Acquire the next image and set it up for use.
  swap_chain_->Begin();

  // TODO(benvanik): use a fence instead? May not be possible with target image.
  auto err = vkQueueWaitIdle(device->primary_queue());
  CheckResult(err, "vkQueueWaitIdle");
}

void VulkanContext::EndSwap() {
  SCOPE_profile_cpu_f("gpu");
  auto provider = static_cast<VulkanProvider*>(provider_);
  auto device = provider->device();

  // Notify the presentation engine the image is ready.
  // The contents must be in a coherent state.
  swap_chain_->End();

  // Wait until the queue is idle.
  // TODO(benvanik): is this required?
  auto err = vkQueueWaitIdle(device->primary_queue());
  CheckResult(err, "vkQueueWaitIdle");
}

std::unique_ptr<RawImage> VulkanContext::Capture() {
  assert_always();
  return nullptr;
}

}  // namespace vulkan
}  // namespace ui
}  // namespace xe
