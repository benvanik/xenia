/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/vulkan/vulkan_swap_chain.h"

#include <gflags/gflags.h>

#include <mutex>
#include <string>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/ui/vulkan/vulkan.h"
#include "xenia/ui/vulkan/vulkan_device.h"
#include "xenia/ui/vulkan/vulkan_instance.h"
#include "xenia/ui/vulkan/vulkan_util.h"

DEFINE_bool(vulkan_random_clear_color, false,
            "Randomizes framebuffer clear color.");

namespace xe {
namespace ui {
namespace vulkan {

VulkanSwapChain::VulkanSwapChain(VulkanInstance* instance, VulkanDevice* device)
    : instance_(instance), device_(device) {}

VulkanSwapChain::~VulkanSwapChain() { Shutdown(); }

VkResult VulkanSwapChain::Initialize(VkSurfaceKHR surface) {
  surface_ = surface;
  VkResult status;

  // Find a queue family that supports presentation.
  VkBool32 surface_supported = false;
  uint32_t queue_family_index = -1;
  for (uint32_t i = 0;
       i < device_->device_info().queue_family_properties.size(); i++) {
    const VkQueueFamilyProperties& family_props =
        device_->device_info().queue_family_properties[i];
    if (!(family_props.queueFlags & VK_QUEUE_GRAPHICS_BIT) ||
        !(family_props.queueFlags & VK_QUEUE_TRANSFER_BIT)) {
      continue;
    }

    status = vkGetPhysicalDeviceSurfaceSupportKHR(*device_, i, surface,
                                                  &surface_supported);
    if (status == VK_SUCCESS && surface_supported == VK_TRUE) {
      queue_family_index = i;
      break;
    }
  }

  if (!surface_supported) {
    XELOGE(
        "Physical device does not have a queue that supports "
        "graphics/transfer/presentation!");
    return VK_ERROR_INCOMPATIBLE_DRIVER;
  }

  presentation_queue_ = device_->AcquireQueue(queue_family_index);
  presentation_queue_family_ = queue_family_index;
  if (!presentation_queue_) {
    // That's okay, use the primary queue.
    presentation_queue_ = device_->primary_queue();
    presentation_queue_mutex_ = &device_->primary_queue_mutex();
    presentation_queue_family_ = device_->queue_family_index();

    if (!presentation_queue_) {
      XELOGE("Failed to acquire swap chain presentation queue!");
      return VK_ERROR_INITIALIZATION_FAILED;
    }
  }

  // Query supported target formats.
  uint32_t count = 0;
  status =
      vkGetPhysicalDeviceSurfaceFormatsKHR(*device_, surface_, &count, nullptr);
  CheckResult(status, "vkGetPhysicalDeviceSurfaceFormatsKHR");
  std::vector<VkSurfaceFormatKHR> surface_formats;
  surface_formats.resize(count);
  status = vkGetPhysicalDeviceSurfaceFormatsKHR(*device_, surface_, &count,
                                                surface_formats.data());
  CheckResult(status, "vkGetPhysicalDeviceSurfaceFormatsKHR");
  if (status != VK_SUCCESS) {
    return status;
  }

  // If the format list includes just one entry of VK_FORMAT_UNDEFINED the
  // surface has no preferred format.
  // Otherwise, at least one supported format will be returned.
  assert_true(surface_formats.size() >= 1);
  if (surface_formats.size() == 1 &&
      surface_formats[0].format == VK_FORMAT_UNDEFINED) {
    // Fallback to common RGBA.
    surface_format_ = VK_FORMAT_R8G8B8A8_UNORM;
  } else {
    // Use first defined format.
    surface_format_ = surface_formats[0].format;
  }

  // Query surface min/max/caps.
  VkSurfaceCapabilitiesKHR surface_caps;
  status = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(*device_, surface_,
                                                     &surface_caps);
  CheckResult(status, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
  if (status != VK_SUCCESS) {
    return status;
  }

  // Query surface properties so we can configure ourselves within bounds.
  std::vector<VkPresentModeKHR> present_modes;
  status = vkGetPhysicalDeviceSurfacePresentModesKHR(*device_, surface_, &count,
                                                     nullptr);
  CheckResult(status, "vkGetPhysicalDeviceSurfacePresentModesKHR");
  if (status != VK_SUCCESS) {
    return status;
  }

  present_modes.resize(count);
  status = vkGetPhysicalDeviceSurfacePresentModesKHR(*device_, surface_, &count,
                                                     present_modes.data());
  CheckResult(status, "vkGetPhysicalDeviceSurfacePresentModesKHR");
  if (status != VK_SUCCESS) {
    return status;
  }

  // Calculate swapchain target dimensions.
  VkExtent2D extent = surface_caps.currentExtent;
  if (surface_caps.currentExtent.width == -1) {
    assert_true(surface_caps.currentExtent.height == -1);
    // Undefined extents, so we need to pick something.
    XELOGI("Swap chain target surface extents undefined; guessing value");
    extent.width = 1280;
    extent.height = 720;
  }
  surface_width_ = extent.width;
  surface_height_ = extent.height;

  // Always prefer mailbox mode (non-tearing, low-latency).
  // If it's not available we'll use immediate (tearing, low-latency).
  // If not even that we fall back to FIFO, which sucks.
  VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
  for (size_t i = 0; i < present_modes.size(); ++i) {
    if (present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
      // This is the best, so early-out.
      present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
      break;
    } else if (present_modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) {
      present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    }
  }

  // Determine the number of images (1 + number queued).
  uint32_t image_count = surface_caps.minImageCount + 1;
  if (surface_caps.maxImageCount > 0 &&
      image_count > surface_caps.maxImageCount) {
    // Too many requested - use whatever we can.
    XELOGI("Requested number of swapchain images (%d) exceeds maximum (%d)",
           image_count, surface_caps.maxImageCount);
    image_count = surface_caps.maxImageCount;
  }

  // Always pass through whatever transform the surface started with (so long
  // as it's supported).
  VkSurfaceTransformFlagBitsKHR pre_transform = surface_caps.currentTransform;

  VkSwapchainCreateInfoKHR create_info;
  create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  create_info.pNext = nullptr;
  create_info.flags = 0;
  create_info.surface = surface_;
  create_info.minImageCount = image_count;
  create_info.imageFormat = surface_format_;
  create_info.imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
  create_info.imageExtent.width = extent.width;
  create_info.imageExtent.height = extent.height;
  create_info.imageArrayLayers = 1;
  create_info.imageUsage =
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  create_info.queueFamilyIndexCount = 0;
  create_info.pQueueFamilyIndices = nullptr;
  create_info.preTransform = pre_transform;
  create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  create_info.presentMode = present_mode;
  create_info.clipped = VK_TRUE;
  create_info.oldSwapchain = nullptr;

  XELOGVK("Creating swap chain:");
  XELOGVK("  minImageCount    = %u", create_info.minImageCount);
  XELOGVK("  imageFormat      = %s", to_string(create_info.imageFormat));
  XELOGVK("  imageExtent      = %d x %d", create_info.imageExtent.width,
          create_info.imageExtent.height);
  auto pre_transform_str = to_flags_string(create_info.preTransform);
  XELOGVK("  preTransform     = %s", pre_transform_str.c_str());
  XELOGVK("  imageArrayLayers = %u", create_info.imageArrayLayers);
  XELOGVK("  presentMode      = %s", to_string(create_info.presentMode));
  XELOGVK("  clipped          = %s", create_info.clipped ? "true" : "false");
  XELOGVK("  imageColorSpace  = %s", to_string(create_info.imageColorSpace));
  auto image_usage_flags_str = to_flags_string(
      static_cast<VkImageUsageFlagBits>(create_info.imageUsage));
  XELOGVK("  imageUsageFlags  = %s", image_usage_flags_str.c_str());
  XELOGVK("  imageSharingMode = %s", to_string(create_info.imageSharingMode));
  XELOGVK("  queueFamilyCount = %u", create_info.queueFamilyIndexCount);

  status = vkCreateSwapchainKHR(*device_, &create_info, nullptr, &handle);
  if (status != VK_SUCCESS) {
    XELOGE("Failed to create swapchain: %s", to_string(status));
    return status;
  }

  // Create the pool used for transient buffers, so we can reset them all at
  // once.
  VkCommandPoolCreateInfo cmd_pool_info;
  cmd_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  cmd_pool_info.pNext = nullptr;
  cmd_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  cmd_pool_info.queueFamilyIndex = presentation_queue_family_;
  status = vkCreateCommandPool(*device_, &cmd_pool_info, nullptr, &cmd_pool_);
  CheckResult(status, "vkCreateCommandPool");
  if (status != VK_SUCCESS) {
    return status;
  }

  // Primary command buffer
  VkCommandBufferAllocateInfo cmd_buffer_info;
  cmd_buffer_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cmd_buffer_info.pNext = nullptr;
  cmd_buffer_info.commandPool = cmd_pool_;
  cmd_buffer_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cmd_buffer_info.commandBufferCount = 2;
  status = vkAllocateCommandBuffers(*device_, &cmd_buffer_info, &cmd_buffer_);
  CheckResult(status, "vkCreateCommandBuffer");
  if (status != VK_SUCCESS) {
    return status;
  }

  // Make two command buffers we'll do all our primary rendering from.
  VkCommandBuffer command_buffers[2];
  cmd_buffer_info.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
  cmd_buffer_info.commandBufferCount = 2;
  status =
      vkAllocateCommandBuffers(*device_, &cmd_buffer_info, command_buffers);
  CheckResult(status, "vkCreateCommandBuffer");
  if (status != VK_SUCCESS) {
    return status;
  }

  render_cmd_buffer_ = command_buffers[0];
  copy_cmd_buffer_ = command_buffers[1];

  // Create the render pass used to draw to the swap chain.
  // The actual framebuffer attached will depend on which image we are drawing
  // into.
  VkAttachmentDescription color_attachment;
  color_attachment.flags = 0;
  color_attachment.format = surface_format_;
  color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;  // CLEAR;
  color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  color_attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  color_attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  VkAttachmentReference color_reference;
  color_reference.attachment = 0;
  color_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  VkAttachmentReference depth_reference;
  depth_reference.attachment = VK_ATTACHMENT_UNUSED;
  depth_reference.layout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkSubpassDescription render_subpass;
  render_subpass.flags = 0;
  render_subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  render_subpass.inputAttachmentCount = 0;
  render_subpass.pInputAttachments = nullptr;
  render_subpass.colorAttachmentCount = 1;
  render_subpass.pColorAttachments = &color_reference;
  render_subpass.pResolveAttachments = nullptr;
  render_subpass.pDepthStencilAttachment = &depth_reference;
  render_subpass.preserveAttachmentCount = 0,
  render_subpass.pPreserveAttachments = nullptr;
  VkRenderPassCreateInfo render_pass_info;
  render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  render_pass_info.pNext = nullptr;
  render_pass_info.flags = 0;
  render_pass_info.attachmentCount = 1;
  render_pass_info.pAttachments = &color_attachment;
  render_pass_info.subpassCount = 1;
  render_pass_info.pSubpasses = &render_subpass;
  render_pass_info.dependencyCount = 0;
  render_pass_info.pDependencies = nullptr;
  status =
      vkCreateRenderPass(*device_, &render_pass_info, nullptr, &render_pass_);
  CheckResult(status, "vkCreateRenderPass");
  if (status != VK_SUCCESS) {
    return status;
  }

  // Create a semaphore we'll use to synchronize with the swapchain.
  VkSemaphoreCreateInfo semaphore_info;
  semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  semaphore_info.pNext = nullptr;
  semaphore_info.flags = 0;
  status = vkCreateSemaphore(*device_, &semaphore_info, nullptr,
                             &image_available_semaphore_);
  CheckResult(status, "vkCreateSemaphore");
  if (status != VK_SUCCESS) {
    return status;
  }

  // Create another semaphore used to synchronize writes to the swap image.
  status = vkCreateSemaphore(*device_, &semaphore_info, nullptr,
                             &image_usage_semaphore_);
  CheckResult(status, "vkCreateSemaphore");
  if (status != VK_SUCCESS) {
    return status;
  }

  // Get images we will be presenting to.
  // Note that this may differ from our requested amount.
  uint32_t actual_image_count = 0;
  std::vector<VkImage> images;
  status =
      vkGetSwapchainImagesKHR(*device_, handle, &actual_image_count, nullptr);
  CheckResult(status, "vkGetSwapchainImagesKHR");
  if (status != VK_SUCCESS) {
    return status;
  }

  images.resize(actual_image_count);
  status = vkGetSwapchainImagesKHR(*device_, handle, &actual_image_count,
                                   images.data());
  CheckResult(status, "vkGetSwapchainImagesKHR");
  if (status != VK_SUCCESS) {
    return status;
  }

  // Create all buffers.
  buffers_.resize(images.size());
  for (size_t i = 0; i < buffers_.size(); ++i) {
    status = InitializeBuffer(&buffers_[i], images[i]);
    if (status != VK_SUCCESS) {
      XELOGE("Failed to initialize a swapchain buffer");
      return status;
    }

    buffers_[i].image_layout = VK_IMAGE_LAYOUT_UNDEFINED;
  }

  // Create a fence we'll use to wait for commands to finish.
  VkFenceCreateInfo fence_create_info = {
      VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      nullptr,
      VK_FENCE_CREATE_SIGNALED_BIT,
  };
  status = vkCreateFence(*device_, &fence_create_info, nullptr,
                         &synchronization_fence_);
  CheckResult(status, "vkGetSwapchainImagesKHR");
  if (status != VK_SUCCESS) {
    return status;
  }

  XELOGVK("Swap chain initialized successfully!");
  return VK_SUCCESS;
}

VkResult VulkanSwapChain::InitializeBuffer(Buffer* buffer,
                                           VkImage target_image) {
  DestroyBuffer(buffer);
  buffer->image = target_image;

  VkResult status;

  // Create an image view for the presentation image.
  // This will be used as a framebuffer attachment.
  VkImageViewCreateInfo image_view_info;
  image_view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  image_view_info.pNext = nullptr;
  image_view_info.flags = 0;
  image_view_info.image = buffer->image;
  image_view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  image_view_info.format = surface_format_;
  image_view_info.components.r = VK_COMPONENT_SWIZZLE_R;
  image_view_info.components.g = VK_COMPONENT_SWIZZLE_G;
  image_view_info.components.b = VK_COMPONENT_SWIZZLE_B;
  image_view_info.components.a = VK_COMPONENT_SWIZZLE_A;
  image_view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  image_view_info.subresourceRange.baseMipLevel = 0;
  image_view_info.subresourceRange.levelCount = 1;
  image_view_info.subresourceRange.baseArrayLayer = 0;
  image_view_info.subresourceRange.layerCount = 1;
  status = vkCreateImageView(*device_, &image_view_info, nullptr,
                             &buffer->image_view);
  CheckResult(status, "vkCreateImageView");
  if (status != VK_SUCCESS) {
    return status;
  }

  // Create the framebuffer used to render into this image.
  VkImageView attachments[] = {buffer->image_view};
  VkFramebufferCreateInfo framebuffer_info;
  framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  framebuffer_info.pNext = nullptr;
  framebuffer_info.flags = 0;
  framebuffer_info.renderPass = render_pass_;
  framebuffer_info.attachmentCount =
      static_cast<uint32_t>(xe::countof(attachments));
  framebuffer_info.pAttachments = attachments;
  framebuffer_info.width = surface_width_;
  framebuffer_info.height = surface_height_;
  framebuffer_info.layers = 1;
  status = vkCreateFramebuffer(*device_, &framebuffer_info, nullptr,
                               &buffer->framebuffer);
  CheckResult(status, "vkCreateFramebuffer");
  if (status != VK_SUCCESS) {
    return status;
  }

  return VK_SUCCESS;
}

void VulkanSwapChain::DestroyBuffer(Buffer* buffer) {
  if (buffer->framebuffer) {
    vkDestroyFramebuffer(*device_, buffer->framebuffer, nullptr);
    buffer->framebuffer = nullptr;
  }
  if (buffer->image_view) {
    vkDestroyImageView(*device_, buffer->image_view, nullptr);
    buffer->image_view = nullptr;
  }
  // Image is taken care of by the presentation engine.
  buffer->image = nullptr;
}

VkResult VulkanSwapChain::Reinitialize() {
  // Hacky, but stash the surface so we can reuse it.
  auto surface = surface_;
  surface_ = nullptr;
  Shutdown();
  return Initialize(surface);
}

void VulkanSwapChain::WaitAndSignalSemaphore(VkSemaphore sem) {
  wait_and_signal_semaphores_.push_back(sem);
}

void VulkanSwapChain::Shutdown() {
  // TODO(benvanik): properly wait for a clean state.
  for (auto& buffer : buffers_) {
    DestroyBuffer(&buffer);
  }
  buffers_.clear();

  VK_SAFE_DESTROY(vkDestroySemaphore, *device_, image_available_semaphore_,
                  nullptr);
  VK_SAFE_DESTROY(vkDestroyRenderPass, *device_, render_pass_, nullptr);

  if (copy_cmd_buffer_) {
    vkFreeCommandBuffers(*device_, cmd_pool_, 1, &copy_cmd_buffer_);
    copy_cmd_buffer_ = nullptr;
  }
  if (render_cmd_buffer_) {
    vkFreeCommandBuffers(*device_, cmd_pool_, 1, &render_cmd_buffer_);
    render_cmd_buffer_ = nullptr;
  }
  VK_SAFE_DESTROY(vkDestroyCommandPool, *device_, cmd_pool_, nullptr);

  if (presentation_queue_) {
    if (!presentation_queue_mutex_) {
      // We own the queue and need to release it.
      device_->ReleaseQueue(presentation_queue_, presentation_queue_family_);
    }
    presentation_queue_ = nullptr;
    presentation_queue_mutex_ = nullptr;
    presentation_queue_family_ = -1;
  }

  VK_SAFE_DESTROY(vkDestroyFence, *device_, synchronization_fence_, nullptr);

  // images_ doesn't need to be cleaned up as the swapchain does it implicitly.
  VK_SAFE_DESTROY(vkDestroySwapchainKHR, *device_, handle, nullptr);
  VK_SAFE_DESTROY(vkDestroySurfaceKHR, *instance_, surface_, nullptr);
}

VkResult VulkanSwapChain::Begin() {
  wait_and_signal_semaphores_.clear();

  VkResult status;

  // Wait for the last swap to finish.
  status = vkWaitForFences(*device_, 1, &synchronization_fence_, VK_TRUE, -1);
  if (status != VK_SUCCESS) {
    return status;
  }

  status = vkResetFences(*device_, 1, &synchronization_fence_);
  if (status != VK_SUCCESS) {
    return status;
  }

  // Get the index of the next available swapchain image.
  status =
      vkAcquireNextImageKHR(*device_, handle, 0, image_available_semaphore_,
                            nullptr, &current_buffer_index_);
  if (status != VK_SUCCESS) {
    return status;
  }

  // Wait for the acquire semaphore to be signaled so that the following
  // operations know they can start modifying the image.
  VkSubmitInfo wait_submit_info;
  wait_submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  wait_submit_info.pNext = nullptr;

  VkPipelineStageFlags wait_dst_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  wait_submit_info.waitSemaphoreCount = 1;
  wait_submit_info.pWaitSemaphores = &image_available_semaphore_;
  wait_submit_info.pWaitDstStageMask = &wait_dst_stage;

  wait_submit_info.commandBufferCount = 0;
  wait_submit_info.pCommandBuffers = nullptr;
  wait_submit_info.signalSemaphoreCount = 1;
  wait_submit_info.pSignalSemaphores = &image_usage_semaphore_;
  if (presentation_queue_mutex_) {
    presentation_queue_mutex_->lock();
  }
  status = vkQueueSubmit(presentation_queue_, 1, &wait_submit_info, nullptr);
  if (presentation_queue_mutex_) {
    presentation_queue_mutex_->unlock();
  }
  if (status != VK_SUCCESS) {
    return status;
  }

  // Reset all command buffers.
  vkResetCommandBuffer(render_cmd_buffer_, 0);
  vkResetCommandBuffer(copy_cmd_buffer_, 0);
  auto& current_buffer = buffers_[current_buffer_index_];

  // Build the command buffer that will execute all queued rendering buffers.
  VkCommandBufferInheritanceInfo inherit_info;
  inherit_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
  inherit_info.pNext = nullptr;
  inherit_info.renderPass = render_pass_;
  inherit_info.subpass = 0;
  inherit_info.framebuffer = current_buffer.framebuffer;
  inherit_info.occlusionQueryEnable = VK_FALSE;
  inherit_info.queryFlags = 0;
  inherit_info.pipelineStatistics = 0;

  VkCommandBufferBeginInfo begin_info;
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.pNext = nullptr;
  begin_info.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT |
                     VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  begin_info.pInheritanceInfo = &inherit_info;
  status = vkBeginCommandBuffer(render_cmd_buffer_, &begin_info);
  CheckResult(status, "vkBeginCommandBuffer");
  if (status != VK_SUCCESS) {
    return status;
  }

  // Start recording the copy command buffer as well.
  begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  status = vkBeginCommandBuffer(copy_cmd_buffer_, &begin_info);
  CheckResult(status, "vkBeginCommandBuffer");
  if (status != VK_SUCCESS) {
    return status;
  }

  // First: Issue a command to clear the render target.
  VkImageSubresourceRange clear_range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  VkClearColorValue clear_color;
  clear_color.float32[0] = 238 / 255.0f;
  clear_color.float32[1] = 238 / 255.0f;
  clear_color.float32[2] = 238 / 255.0f;
  clear_color.float32[3] = 1.0f;
  if (FLAGS_vulkan_random_clear_color) {
    clear_color.float32[0] =
        rand() / static_cast<float>(RAND_MAX);  // NOLINT(runtime/threadsafe_fn)
    clear_color.float32[1] = 1.0f;
    clear_color.float32[2] = 0.0f;
  }
  vkCmdClearColorImage(copy_cmd_buffer_, current_buffer.image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_color, 1,
                       &clear_range);

  return VK_SUCCESS;
}

VkResult VulkanSwapChain::End() {
  auto& current_buffer = buffers_[current_buffer_index_];
  VkResult status;

  status = vkEndCommandBuffer(render_cmd_buffer_);
  CheckResult(status, "vkEndCommandBuffer");
  if (status != VK_SUCCESS) {
    return status;
  }

  status = vkEndCommandBuffer(copy_cmd_buffer_);
  CheckResult(status, "vkEndCommandBuffer");
  if (status != VK_SUCCESS) {
    return status;
  }

  // Build primary command buffer.
  status = vkResetCommandBuffer(cmd_buffer_, 0);
  CheckResult(status, "vkResetCommandBuffer");
  if (status != VK_SUCCESS) {
    return status;
  }

  VkCommandBufferBeginInfo begin_info;
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.pNext = nullptr;
  begin_info.flags = 0;
  begin_info.pInheritanceInfo = nullptr;
  status = vkBeginCommandBuffer(cmd_buffer_, &begin_info);
  CheckResult(status, "vkBeginCommandBuffer");
  if (status != VK_SUCCESS) {
    return status;
  }

  // Transition the image to a format we can copy to.
  VkImageMemoryBarrier pre_image_copy_barrier;
  pre_image_copy_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  pre_image_copy_barrier.pNext = nullptr;
  pre_image_copy_barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
  pre_image_copy_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  pre_image_copy_barrier.oldLayout = current_buffer.image_layout;
  pre_image_copy_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  pre_image_copy_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  pre_image_copy_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  pre_image_copy_barrier.image = current_buffer.image;
  pre_image_copy_barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0,
                                             1};
  vkCmdPipelineBarrier(cmd_buffer_, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &pre_image_copy_barrier);

  current_buffer.image_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

  // Execute copy commands
  vkCmdExecuteCommands(cmd_buffer_, 1, &copy_cmd_buffer_);

  // Transition the image to a color attachment target for drawing.
  VkImageMemoryBarrier pre_image_memory_barrier;
  pre_image_memory_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  pre_image_memory_barrier.pNext = nullptr;
  pre_image_memory_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  pre_image_memory_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  pre_image_memory_barrier.image = current_buffer.image;
  pre_image_memory_barrier.subresourceRange.aspectMask =
      VK_IMAGE_ASPECT_COLOR_BIT;
  pre_image_memory_barrier.subresourceRange.baseMipLevel = 0;
  pre_image_memory_barrier.subresourceRange.levelCount = 1;
  pre_image_memory_barrier.subresourceRange.baseArrayLayer = 0;
  pre_image_memory_barrier.subresourceRange.layerCount = 1;

  pre_image_memory_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  pre_image_memory_barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  pre_image_memory_barrier.oldLayout = current_buffer.image_layout;
  pre_image_memory_barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  vkCmdPipelineBarrier(cmd_buffer_, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0,
                       nullptr, 0, nullptr, 1, &pre_image_memory_barrier);

  current_buffer.image_layout = pre_image_memory_barrier.newLayout;

  // Begin render pass.
  VkRenderPassBeginInfo render_pass_begin_info;
  render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  render_pass_begin_info.pNext = nullptr;
  render_pass_begin_info.renderPass = render_pass_;
  render_pass_begin_info.framebuffer = current_buffer.framebuffer;
  render_pass_begin_info.renderArea.offset.x = 0;
  render_pass_begin_info.renderArea.offset.y = 0;
  render_pass_begin_info.renderArea.extent.width = surface_width_;
  render_pass_begin_info.renderArea.extent.height = surface_height_;
  render_pass_begin_info.clearValueCount = 0;
  render_pass_begin_info.pClearValues = nullptr;
  vkCmdBeginRenderPass(cmd_buffer_, &render_pass_begin_info,
                       VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);

  // Render commands.
  vkCmdExecuteCommands(cmd_buffer_, 1, &render_cmd_buffer_);

  // End render pass.
  vkCmdEndRenderPass(cmd_buffer_);

  // Transition the image to a format the presentation engine can source from.
  // FIXME: Do we need more synchronization here between the copy buffer?
  VkImageMemoryBarrier post_image_memory_barrier;
  post_image_memory_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  post_image_memory_barrier.pNext = nullptr;
  post_image_memory_barrier.srcAccessMask =
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  post_image_memory_barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
  post_image_memory_barrier.oldLayout = current_buffer.image_layout;
  post_image_memory_barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  post_image_memory_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  post_image_memory_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  post_image_memory_barrier.image = current_buffer.image;
  post_image_memory_barrier.subresourceRange.aspectMask =
      VK_IMAGE_ASPECT_COLOR_BIT;
  post_image_memory_barrier.subresourceRange.baseMipLevel = 0;
  post_image_memory_barrier.subresourceRange.levelCount = 1;
  post_image_memory_barrier.subresourceRange.baseArrayLayer = 0;
  post_image_memory_barrier.subresourceRange.layerCount = 1;
  vkCmdPipelineBarrier(cmd_buffer_,
                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                       VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &post_image_memory_barrier);

  current_buffer.image_layout = post_image_memory_barrier.newLayout;

  status = vkEndCommandBuffer(cmd_buffer_);
  CheckResult(status, "vkEndCommandBuffer");
  if (status != VK_SUCCESS) {
    return status;
  }

  VkPipelineStageFlags wait_dst_stage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
  std::vector<VkSemaphore> semaphores;
  for (size_t i = 0; i < wait_and_signal_semaphores_.size(); i++) {
    semaphores.push_back(wait_and_signal_semaphores_[i]);
  }
  semaphores.push_back(image_usage_semaphore_);

  // Submit commands.
  // Wait on the image usage semaphore (signaled when an image is available)
  VkSubmitInfo render_submit_info;
  render_submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  render_submit_info.pNext = nullptr;
  render_submit_info.waitSemaphoreCount = uint32_t(semaphores.size());
  render_submit_info.pWaitSemaphores = semaphores.data();
  render_submit_info.pWaitDstStageMask = &wait_dst_stage;
  render_submit_info.commandBufferCount = 1;
  render_submit_info.pCommandBuffers = &cmd_buffer_;
  render_submit_info.signalSemaphoreCount = uint32_t(semaphores.size()) - 1;
  render_submit_info.pSignalSemaphores = semaphores.data();
  if (presentation_queue_mutex_) {
    presentation_queue_mutex_->lock();
  }
  status = vkQueueSubmit(presentation_queue_, 1, &render_submit_info,
                         synchronization_fence_);
  if (presentation_queue_mutex_) {
    presentation_queue_mutex_->unlock();
  }

  if (status != VK_SUCCESS) {
    return status;
  }

  // Queue the present of our current image.
  const VkSwapchainKHR swap_chains[] = {handle};
  const uint32_t swap_chain_image_indices[] = {current_buffer_index_};
  VkPresentInfoKHR present_info;
  present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  present_info.pNext = nullptr;
  present_info.waitSemaphoreCount = 0;
  present_info.pWaitSemaphores = nullptr;
  present_info.swapchainCount = static_cast<uint32_t>(xe::countof(swap_chains));
  present_info.pSwapchains = swap_chains;
  present_info.pImageIndices = swap_chain_image_indices;
  present_info.pResults = nullptr;
  if (presentation_queue_mutex_) {
    presentation_queue_mutex_->lock();
  }
  status = vkQueuePresentKHR(presentation_queue_, &present_info);
  if (presentation_queue_mutex_) {
    presentation_queue_mutex_->unlock();
  }

  switch (status) {
    case VK_SUCCESS:
      break;
    case VK_SUBOPTIMAL_KHR:
      // We are not rendering at the right size - but the presentation engine
      // will scale the output for us.
      status = VK_SUCCESS;
      break;
    case VK_ERROR_OUT_OF_DATE_KHR:
      // Lost presentation ability; need to recreate the swapchain.
      // TODO(benvanik): recreate swapchain.
      Reinitialize();
      // assert_always("Swapchain recreation not implemented");
      break;
    case VK_ERROR_DEVICE_LOST:
      // Fatal. Device lost.
      break;
    default:
      XELOGE("Failed to queue present: %s", to_string(status));
      assert_always("Unexpected queue present failure");
  }

  return status;
}

}  // namespace vulkan
}  // namespace ui
}  // namespace xe
