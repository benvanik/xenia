/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/vulkan/vulkan_graphics_system.h"

#include <algorithm>
#include <cstring>

#include "xenia/base/logging.h"
#include "xenia/base/profiling.h"
#include "xenia/cpu/processor.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/gpu/vulkan/vulkan_command_processor.h"
#include "xenia/gpu/vulkan/vulkan_gpu_flags.h"
#include "xenia/ui/vulkan/vulkan_provider.h"
#include "xenia/ui/vulkan/vulkan_swap_chain.h"
#include "xenia/ui/vulkan/vulkan_util.h"
#include "xenia/ui/window.h"

namespace xe {
namespace gpu {
namespace vulkan {

using xe::ui::RawImage;
using xe::ui::vulkan::CheckResult;

VulkanGraphicsSystem::VulkanGraphicsSystem() {}
VulkanGraphicsSystem::~VulkanGraphicsSystem() = default;

X_STATUS VulkanGraphicsSystem::Setup(cpu::Processor* processor,
                                     kernel::KernelState* kernel_state,
                                     ui::Window* target_window) {
  // Must create the provider so we can create contexts.
  auto provider = xe::ui::vulkan::VulkanProvider::Create(target_window);
  device_ = provider->device();
  provider_ = std::move(provider);

  auto result = GraphicsSystem::Setup(processor, kernel_state, target_window);
  if (result) {
    return result;
  }

  if (target_window) {
    display_context_ = reinterpret_cast<xe::ui::vulkan::VulkanContext*>(
        target_window->context());
  }

  // Create our own command pool we can use for captures.
  VkCommandPoolCreateInfo create_info = {
      VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      nullptr,
      VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |
          VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
      device_->queue_family_index(),
  };
  auto status =
      vkCreateCommandPool(*device_, &create_info, nullptr, &command_pool_);
  CheckResult(status, "vkCreateCommandPool");

  return X_STATUS_SUCCESS;
}

void VulkanGraphicsSystem::Shutdown() {
  GraphicsSystem::Shutdown();

  vkDestroyCommandPool(*device_, command_pool_, nullptr);
}

std::unique_ptr<RawImage> VulkanGraphicsSystem::Capture() {
  auto& swap_state = command_processor_->swap_state();
  std::lock_guard<std::mutex> lock(swap_state.mutex);
  if (!swap_state.front_buffer_texture) {
    return nullptr;
  }

  VkResult status = VK_SUCCESS;

  VkCommandBufferAllocateInfo alloc_info = {
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      nullptr,
      command_pool_,
      VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      1,
  };

  VkCommandBuffer cmd = nullptr;
  status = vkAllocateCommandBuffers(*device_, &alloc_info, &cmd);
  CheckResult(status, "vkAllocateCommandBuffers");
  if (status != VK_SUCCESS) {
    return nullptr;
  }

  VkCommandBufferBeginInfo begin_info = {
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      nullptr,
      VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
      nullptr,
  };
  vkBeginCommandBuffer(cmd, &begin_info);

  auto front_buffer =
      reinterpret_cast<VkImage>(swap_state.front_buffer_texture);

  status = CreateCaptureBuffer(cmd, {swap_state.width, swap_state.height});
  if (status != VK_SUCCESS) {
    vkFreeCommandBuffers(*device_, command_pool_, 1, &cmd);
    return nullptr;
  }

  VkImageMemoryBarrier barrier;
  std::memset(&barrier, 0, sizeof(VkImageMemoryBarrier));
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
  barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = front_buffer;
  barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &barrier);

  // Copy front buffer into capture image.
  VkBufferImageCopy region = {
      0,         0,
      0,         {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
      {0, 0, 0}, {swap_state.width, swap_state.height, 1},
  };

  vkCmdCopyImageToBuffer(cmd, front_buffer, VK_IMAGE_LAYOUT_GENERAL,
                         capture_buffer_, 1, &region);

  VkBufferMemoryBarrier memory_barrier = {
      VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      nullptr,
      VK_ACCESS_TRANSFER_WRITE_BIT,
      VK_ACCESS_HOST_READ_BIT | VK_ACCESS_MEMORY_READ_BIT,
      VK_QUEUE_FAMILY_IGNORED,
      VK_QUEUE_FAMILY_IGNORED,
      capture_buffer_,
      0,
      VK_WHOLE_SIZE,
  };
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                       VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 1,
                       &memory_barrier, 0, nullptr);

  status = vkEndCommandBuffer(cmd);

  // Submit commands and wait.
  if (status == VK_SUCCESS) {
    std::lock_guard<std::mutex> lock(device_->primary_queue_mutex());
    VkSubmitInfo submit_info = {
        VK_STRUCTURE_TYPE_SUBMIT_INFO,
        nullptr,
        0,
        nullptr,
        nullptr,
        1,
        &cmd,
        0,
        nullptr,
    };
    status = vkQueueSubmit(device_->primary_queue(), 1, &submit_info, nullptr);
    CheckResult(status, "vkQueueSubmit");

    if (status == VK_SUCCESS) {
      status = vkQueueWaitIdle(device_->primary_queue());
      CheckResult(status, "vkQueueWaitIdle");
    }
  }

  vkFreeCommandBuffers(*device_, command_pool_, 1, &cmd);

  void* data;
  if (status == VK_SUCCESS) {
    status = vkMapMemory(*device_, capture_buffer_memory_, 0, VK_WHOLE_SIZE, 0,
                         &data);
    CheckResult(status, "vkMapMemory");
  }

  if (status == VK_SUCCESS) {
    std::unique_ptr<RawImage> raw_image(new RawImage());
    raw_image->width = swap_state.width;
    raw_image->height = swap_state.height;
    raw_image->stride = swap_state.width * 4;
    raw_image->data.resize(raw_image->stride * raw_image->height);

    std::memcpy(raw_image->data.data(), data,
                raw_image->stride * raw_image->height);

    vkUnmapMemory(*device_, capture_buffer_memory_);
    DestroyCaptureBuffer();
    return raw_image;
  }

  DestroyCaptureBuffer();
  return nullptr;
}

VkResult VulkanGraphicsSystem::CreateCaptureBuffer(VkCommandBuffer cmd,
                                                   VkExtent2D extents) {
  VkResult status = VK_SUCCESS;

  VkBufferCreateInfo buffer_info = {
      VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      nullptr,
      0,
      extents.width * extents.height * 4,
      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      VK_SHARING_MODE_EXCLUSIVE,
      0,
      nullptr,
  };
  status = vkCreateBuffer(*device_, &buffer_info, nullptr, &capture_buffer_);
  if (status != VK_SUCCESS) {
    return status;
  }

  capture_buffer_size_ = extents.width * extents.height * 4;

  // Bind memory to buffer.
  VkMemoryRequirements mem_requirements;
  vkGetBufferMemoryRequirements(*device_, capture_buffer_, &mem_requirements);
  capture_buffer_memory_ = device_->AllocateMemory(
      mem_requirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  assert_not_null(capture_buffer_memory_);

  status =
      vkBindBufferMemory(*device_, capture_buffer_, capture_buffer_memory_, 0);
  CheckResult(status, "vkBindImageMemory");
  if (status != VK_SUCCESS) {
    vkDestroyBuffer(*device_, capture_buffer_, nullptr);
    return status;
  }

  return status;
}

void VulkanGraphicsSystem::DestroyCaptureBuffer() {
  vkDestroyBuffer(*device_, capture_buffer_, nullptr);
  vkFreeMemory(*device_, capture_buffer_memory_, nullptr);
  capture_buffer_ = nullptr;
  capture_buffer_memory_ = nullptr;
  capture_buffer_size_ = 0;
}

std::unique_ptr<CommandProcessor>
VulkanGraphicsSystem::CreateCommandProcessor() {
  return std::make_unique<VulkanCommandProcessor>(this, kernel_state_);
}

void VulkanGraphicsSystem::Swap(xe::ui::UIEvent* e) {
  if (!command_processor_ || !display_context_) {
    return;
  }

  // Check for pending swap.
  auto& swap_state = command_processor_->swap_state();
  if (display_context_->WasLost()) {
    // We're crashing. Cheese it.
    swap_state.pending = false;
    return;
  }

  {
    std::lock_guard<std::mutex> lock(swap_state.mutex);
    if (!swap_state.pending) {
      // return;
    }

    swap_state.pending = false;
  }

  if (!swap_state.front_buffer_texture) {
    // Not yet ready.
    return;
  }

  auto swap_chain = display_context_->swap_chain();
  auto copy_cmd_buffer = swap_chain->copy_cmd_buffer();
  auto front_buffer =
      reinterpret_cast<VkImage>(swap_state.front_buffer_texture);

  VkImageMemoryBarrier barrier;
  std::memset(&barrier, 0, sizeof(VkImageMemoryBarrier));
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
  barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = front_buffer;
  barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vkCmdPipelineBarrier(copy_cmd_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &barrier);

  VkImageBlit region;
  region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.srcOffsets[0] = {0, 0, 0};
  region.srcOffsets[1] = {static_cast<int32_t>(swap_state.width),
                          static_cast<int32_t>(swap_state.height), 1};

  region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.dstOffsets[0] = {0, 0, 0};
  region.dstOffsets[1] = {static_cast<int32_t>(swap_chain->surface_width()),
                          static_cast<int32_t>(swap_chain->surface_height()),
                          1};
  vkCmdBlitImage(copy_cmd_buffer, front_buffer, VK_IMAGE_LAYOUT_GENERAL,
                 swap_chain->surface_image(),
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region,
                 VK_FILTER_LINEAR);
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe
