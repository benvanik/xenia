/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/vulkan/vulkan_provider.h"

#include <algorithm>

#include "xenia/base/logging.h"
#include "xenia/ui/vulkan/vulkan_context.h"
#include "xenia/ui/vulkan/vulkan_device.h"
#include "xenia/ui/vulkan/vulkan_instance.h"
#include "xenia/ui/vulkan/vulkan_util.h"

DEFINE_uint64(vulkan_device_index, 0, "Index of the physical device to use.",
              "Vulkan");

namespace xe {
namespace ui {
namespace vulkan {

std::unique_ptr<VulkanProvider> VulkanProvider::Create(Window* main_window) {
  std::unique_ptr<VulkanProvider> provider(new VulkanProvider(main_window));
  if (!provider->Initialize()) {
    xe::FatalError(
        "Unable to initialize Vulkan graphics subsystem.\n"
        "\n"
        "Ensure you have the latest drivers for your GPU and that it "
        "supports Vulkan.\n"
        "\n"
        "See https://xenia.jp/faq/ for more information and a list of "
        "supported GPUs.");
    return nullptr;
  }
  return provider;
}

VulkanProvider::VulkanProvider(Window* main_window)
    : GraphicsProvider(main_window) {}

VulkanProvider::~VulkanProvider() {
  device_.reset();
  instance_.reset();
}

bool VulkanProvider::Initialize() {
  instance_ = std::make_unique<VulkanInstance>();

  // Always enable the swapchain.
  instance_->DeclareRequiredExtension(VK_KHR_SURFACE_EXTENSION_NAME,
                                      Version::Make(0, 0, 0), false);
#if XE_PLATFORM_WIN32
  instance_->DeclareRequiredExtension(VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
                                      Version::Make(0, 0, 0), false);
#elif XE_PLATFORM_LINUX
  instance_->DeclareRequiredExtension(VK_KHR_XCB_SURFACE_EXTENSION_NAME,
                                      Version::Make(0, 0, 0), false);
#endif

  // Attempt initialization and device query.
  if (!instance_->Initialize()) {
    XELOGE("Failed to initialize vulkan instance");
    return false;
  }

  // Pick the device to use.
  auto available_devices = instance_->available_devices();
  if (available_devices.empty()) {
    XELOGE("No devices available for use");
    return false;
  }
  size_t device_index =
      std::min(available_devices.size(), cvars::vulkan_device_index);
  auto& device_info = available_devices[device_index];

  // Create the device.
  device_ = std::make_unique<VulkanDevice>(instance_.get());
  device_->DeclareRequiredExtension("VK_KHR_swapchain", Version::Make(0, 0, 0),
                                    false);
  if (!device_->Initialize(device_info)) {
    XELOGE("Unable to initialize device");
    return false;
  }

  return true;
}

std::unique_ptr<GraphicsContext> VulkanProvider::CreateContext(
    Window* target_window) {
  auto new_context =
      std::unique_ptr<VulkanContext>(new VulkanContext(this, target_window));
  if (!new_context->Initialize()) {
    return nullptr;
  }
  return std::unique_ptr<GraphicsContext>(new_context.release());
}

std::unique_ptr<GraphicsContext> VulkanProvider::CreateOffscreenContext() {
  auto new_context =
      std::unique_ptr<VulkanContext>(new VulkanContext(this, nullptr));
  if (!new_context->Initialize()) {
    return nullptr;
  }
  return std::unique_ptr<GraphicsContext>(new_context.release());
}

}  // namespace vulkan
}  // namespace ui
}  // namespace xe
