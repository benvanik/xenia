/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_VULKAN_VULKAN_H_
#define XENIA_UI_VULKAN_VULKAN_H_

#include <gflags/gflags.h>

#include "xenia/base/platform.h"

#if XE_PLATFORM_WIN32
#define VK_USE_PLATFORM_WIN32_KHR 1
#elif XE_PLATFORM_LINUX
#define VK_USE_PLATFORM_XCB_KHR 1
#else
#error Platform not yet supported.
#endif  // XE_PLATFORM_WIN32

// We are statically linked with the loader, so use function prototypes.
#define VK_PROTOTYPES
#if XE_PLATFORM_LINUX
#include <vulkan/vulkan.h>
#elif XE_PLATFORM_WIN32
#include "third_party/vulkan/vulkan.h"
#endif

// NOTE: header order matters here, unfortunately:
#include "third_party/vulkan/vk_lunarg_debug_marker.h"

#define XELOGVK XELOGI

DECLARE_bool(vulkan_validation);
DECLARE_bool(vulkan_primary_queue_only);

#endif  // XENIA_UI_VULKAN_VULKAN_H_
