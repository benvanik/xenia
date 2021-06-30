/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
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

#if XE_PLATFORM_LINUX
#include "xenia/ui/window_gtk.h"

#include <X11/Xlib-xcb.h>
#endif

namespace xe {
namespace ui {
namespace vulkan {

VulkanContext::VulkanContext(VulkanProvider* provider, Window* target_window)
    : GraphicsContext(provider, target_window) {}

VulkanContext::~VulkanContext() {
  VkResult status;
  auto provider = static_cast<VulkanProvider*>(provider_);
  auto device = provider->device();
  {
    std::lock_guard<std::mutex> queue_lock(device->primary_queue_mutex());
    status = vkQueueWaitIdle(device->primary_queue());
  }
  immediate_drawer_.reset();
  swap_chain_.reset();
}

bool VulkanContext::Initialize() {
  auto provider = static_cast<VulkanProvider*>(provider_);

  if (target_window_) {
    // Create swap chain used to present to the window.
    VkResult status = VK_ERROR_FEATURE_NOT_PRESENT;
    VkSurfaceKHR surface = nullptr;
#if XE_PLATFORM_WIN32
    VkWin32SurfaceCreateInfoKHR create_info;
    create_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    create_info.pNext = nullptr;
    create_info.flags = 0;
    create_info.hinstance =
        static_cast<HINSTANCE>(target_window_->native_platform_handle());
    create_info.hwnd = static_cast<HWND>(target_window_->native_handle());
    status = vkCreateWin32SurfaceKHR(*provider->instance(), &create_info,
                                     nullptr, &surface);
    CheckResult(status, "vkCreateWin32SurfaceKHR");
#elif XE_PLATFORM_LINUX
#ifdef GDK_WINDOWING_X11
    GtkWidget* window_handle =
        dynamic_cast<GTKWindow*>(target_window_)->native_window_handle();
    xcb_window_t window =
        gdk_x11_window_get_xid(gtk_widget_get_window(window_handle));
    VkXcbSurfaceCreateInfoKHR create_info;
    create_info.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
    create_info.pNext = nullptr;
    create_info.flags = 0;
    create_info.connection = static_cast<xcb_connection_t*>(
        target_window_->native_platform_handle());
    create_info.window = static_cast<xcb_window_t>(window);
    status = vkCreateXcbSurfaceKHR(*provider->instance(), &create_info, nullptr,
                                   &surface);
    CheckResult(status, "vkCreateXcbSurfaceKHR");
#else
#error Unsupported GDK Backend on Linux.
#endif  // GDK_WINDOWING_X11
#else
#error Platform not yet implemented.
#endif  // XE_PLATFORM_WIN32
    if (status != VK_SUCCESS) {
      XELOGE("Failed to create presentation surface");
      return false;
    }

    swap_chain_ = std::make_unique<VulkanSwapChain>(provider->instance(),
                                                    provider->device());
    if (swap_chain_->Initialize(surface) != VK_SUCCESS) {
      XELOGE("Unable to initialize swap chain");
      return false;
    }

    // Only initialize immediate mode drawer if we are not an offscreen context.
    immediate_drawer_ = std::make_unique<VulkanImmediateDrawer>(this);
    status = immediate_drawer_->Initialize();
    if (status != VK_SUCCESS) {
      XELOGE("Failed to initialize the immediate mode drawer");
      immediate_drawer_.reset();
      return false;
    }
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

bool VulkanContext::BeginSwap() {
  SCOPE_profile_cpu_f("gpu");
  auto provider = static_cast<VulkanProvider*>(provider_);
  auto device = provider->device();

  VkResult status;

  // If we have a window see if it's been resized since we last swapped.
  // If it has been, we'll need to reinitialize the swap chain before we
  // start touching it.
  if (target_window_) {
    if (target_window_->scaled_width() != swap_chain_->surface_width() ||
        target_window_->scaled_height() != swap_chain_->surface_height()) {
      // Resized!
      swap_chain_->Reinitialize();
    }
  }

  if (!context_lost_) {
    // Acquire the next image and set it up for use.
    status = swap_chain_->Begin();
    if (status == VK_ERROR_DEVICE_LOST) {
      context_lost_ = true;
    }
  }

  // TODO(benvanik): use a fence instead? May not be possible with target image.
  std::lock_guard<std::mutex> queue_lock(device->primary_queue_mutex());
  status = vkQueueWaitIdle(device->primary_queue());

  return true;
}

void VulkanContext::EndSwap() {
  SCOPE_profile_cpu_f("gpu");
  auto provider = static_cast<VulkanProvider*>(provider_);
  auto device = provider->device();

  VkResult status;

  if (!context_lost_) {
    // Notify the presentation engine the image is ready.
    // The contents must be in a coherent state.
    status = swap_chain_->End();
    if (status == VK_ERROR_DEVICE_LOST) {
      context_lost_ = true;
    }
  }

  // Wait until the queue is idle.
  // TODO(benvanik): is this required?
  std::lock_guard<std::mutex> queue_lock(device->primary_queue_mutex());
  status = vkQueueWaitIdle(device->primary_queue());
}

std::unique_ptr<RawImage> VulkanContext::Capture() {
  // TODO(benvanik): read back swap chain front buffer.
  return nullptr;
}

}  // namespace vulkan
}  // namespace ui
}  // namespace xe
