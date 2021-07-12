/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/logging.h"
#include "xenia/base/string_util.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/user_module.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/xam_module.h"
#include "xenia/kernel/xam/xam_private.h"
#include "xenia/kernel/xenumerator.h"
#include "xenia/kernel/xthread.h"
#include "xenia/xbox.h"

#if XE_PLATFORM_WIN32
#include "xenia/base/platform_win.h"
#endif

#include "third_party/fmt/include/fmt/format.h"

namespace xe {
namespace kernel {
namespace xam {

dword_result_t XamFeatureEnabled(dword_t unk) { return 0; }
DECLARE_XAM_EXPORT1(XamFeatureEnabled, kNone, kStub);

// Empty stub schema binary.
uint8_t schema_bin[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x2C, 0x00, 0x00, 0x00, 0x2C, 0x00, 0x00,
    0x00, 0x2C, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x2C, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x2C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18,
};

dword_result_t XamGetOnlineSchema() {
  static uint32_t schema_guest = 0;

  if (!schema_guest) {
    schema_guest =
        kernel_state()->memory()->SystemHeapAlloc(8 + sizeof(schema_bin));
    auto schema = kernel_state()->memory()->TranslateVirtual(schema_guest);
    std::memcpy(schema + 8, schema_bin, sizeof(schema_bin));
    xe::store_and_swap<uint32_t>(schema + 0, schema_guest + 8);
    xe::store_and_swap<uint32_t>(schema + 4, sizeof(schema_bin));
  }

  // return pointer to the schema ptr/schema size struct
  return schema_guest;
}
DECLARE_XAM_EXPORT1(XamGetOnlineSchema, kNone, kImplemented);

dword_result_t keXamBuildResourceLocator(uint64_t module,
                                         const std::u16string& container,
                                         const std::u16string& resource,
                                         lpvoid_t buffer_ptr,
                                         uint32_t buffer_count) {
  std::u16string path;
  if (!module) {
    path = fmt::format(u"file://media:/{}.xzp#{}", container, resource);
    XELOGD(
        "XamBuildResourceLocator({0}) returning locator to local file {0}.xzp",
        xe::to_utf8(container));
  } else {
    path = fmt::format(u"section://{:X},{}#{}", (uint32_t)module, container,
                       resource);
  }
  xe::string_util::copy_and_swap_truncating(buffer_ptr.as<char16_t*>(), path,
                                            buffer_count);
  return 0;
}

dword_result_t XamBuildResourceLocator(qword_t module, lpu16string_t container,
                                       lpu16string_t resource,
                                       lpvoid_t buffer_ptr,
                                       dword_t buffer_count) {
  return keXamBuildResourceLocator(module, container.value(), resource.value(),
                                   buffer_ptr, buffer_count);
}
DECLARE_XAM_EXPORT1(XamBuildResourceLocator, kNone, kImplemented);

dword_result_t XamBuildGamercardResourceLocator(lpu16string_t filename,
                                                lpvoid_t buffer_ptr,
                                                dword_t buffer_count) {
  // On an actual xbox these funcs would return a locator to xam.xex resources,
  // but for Xenia we can return a locator to the resources as local files. (big
  // thanks to MS for letting XamBuildResourceLocator return local file
  // locators!)

  // If you're running an app that'll need them, make sure to extract xam.xex
  // resources with xextool ("xextool -d . xam.xex") and add a .xzp extension.

  return keXamBuildResourceLocator(0, u"gamercrd", filename.value(), buffer_ptr,
                                   buffer_count);
}
DECLARE_XAM_EXPORT1(XamBuildGamercardResourceLocator, kNone, kImplemented);

dword_result_t XamBuildSharedSystemResourceLocator(lpu16string_t filename,
                                                   lpvoid_t buffer_ptr,
                                                   dword_t buffer_count) {
  // see notes inside XamBuildGamercardResourceLocator above
  return keXamBuildResourceLocator(0, u"shrdres", filename.value(), buffer_ptr,
                                   buffer_count);
}
DECLARE_XAM_EXPORT1(XamBuildSharedSystemResourceLocator, kNone, kImplemented);

dword_result_t XamBuildLegacySystemResourceLocator(lpu16string_t filename,
                                                   lpvoid_t buffer_ptr,
                                                   dword_t buffer_count) {
  return XamBuildSharedSystemResourceLocator(filename, buffer_ptr,
                                             buffer_count);
}
DECLARE_XAM_EXPORT1(XamBuildLegacySystemResourceLocator, kNone, kImplemented);

dword_result_t XamBuildXamResourceLocator(lpu16string_t filename,
                                          lpvoid_t buffer_ptr,
                                          dword_t buffer_count) {
  return keXamBuildResourceLocator(0, u"xam", filename.value(), buffer_ptr,
                                   buffer_count);
}
DECLARE_XAM_EXPORT1(XamBuildXamResourceLocator, kNone, kImplemented);

dword_result_t XamGetSystemVersion() {
  // eh, just picking one. If we go too low we may break new games, but
  // this value seems to be used for conditionally loading symbols and if
  // we pretend to be old we have less to worry with implementing.
  // 0x200A3200
  // 0x20096B00
  return 0;
}
DECLARE_XAM_EXPORT1(XamGetSystemVersion, kNone, kStub);

void XCustomRegisterDynamicActions() {
  // ???
}
DECLARE_XAM_EXPORT1(XCustomRegisterDynamicActions, kNone, kStub);

dword_result_t XGetAVPack() {
  // DWORD
  // Not sure what the values are for this, but 6 is VGA.
  // Other likely values are 3/4/8 for HDMI or something.
  // Games seem to use this as a PAL check - if the result is not 3/4/6/8
  // they explode with errors if not in PAL mode.
  return 6;
}
DECLARE_XAM_EXPORT1(XGetAVPack, kNone, kStub);

uint32_t xeXGetGameRegion() { return 0xFFFFu; }

dword_result_t XGetGameRegion() { return xeXGetGameRegion(); }
DECLARE_XAM_EXPORT1(XGetGameRegion, kNone, kStub);

dword_result_t XamGetExecutionId(lpdword_t info_ptr) {
  auto module = kernel_state()->GetExecutableModule();
  assert_not_null(module);

  uint32_t guest_hdr_ptr;
  X_STATUS result =
      module->GetOptHeader(XEX_HEADER_EXECUTION_INFO, &guest_hdr_ptr);

  if (XFAILED(result)) {
    return result;
  }

  *info_ptr = guest_hdr_ptr;
  return X_STATUS_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamGetExecutionId, kNone, kImplemented);

dword_result_t XamLoaderSetLaunchData(lpvoid_t data, dword_t size) {
  auto xam = kernel_state()->GetKernelModule<XamModule>("xam.xex");
  auto& loader_data = xam->loader_data();
  loader_data.launch_data_present = size ? true : false;
  loader_data.launch_data.resize(size);
  std::memcpy(loader_data.launch_data.data(), data, size);
  return 0;
}
DECLARE_XAM_EXPORT1(XamLoaderSetLaunchData, kNone, kSketchy);

dword_result_t XamLoaderGetLaunchDataSize(lpdword_t size_ptr) {
  if (!size_ptr) {
    return X_ERROR_INVALID_PARAMETER;
  }

  auto xam = kernel_state()->GetKernelModule<XamModule>("xam.xex");
  auto& loader_data = xam->loader_data();
  if (!loader_data.launch_data_present) {
    *size_ptr = 0;
    return X_ERROR_NOT_FOUND;
  }

  *size_ptr = uint32_t(xam->loader_data().launch_data.size());
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamLoaderGetLaunchDataSize, kNone, kSketchy);

dword_result_t XamLoaderGetLaunchData(lpvoid_t buffer_ptr,
                                      dword_t buffer_size) {
  auto xam = kernel_state()->GetKernelModule<XamModule>("xam.xex");
  auto& loader_data = xam->loader_data();
  if (!loader_data.launch_data_present) {
    return X_ERROR_NOT_FOUND;
  }

  uint32_t copy_size =
      std::min(uint32_t(loader_data.launch_data.size()), uint32_t(buffer_size));
  std::memcpy(buffer_ptr, loader_data.launch_data.data(), copy_size);
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamLoaderGetLaunchData, kNone, kSketchy);

void XamLoaderLaunchTitle(lpstring_t raw_name_ptr, dword_t flags) {
  auto xam = kernel_state()->GetKernelModule<XamModule>("xam.xex");

  auto& loader_data = xam->loader_data();
  loader_data.launch_flags = flags;

  // Translate the launch path to a full path.
  if (raw_name_ptr) {
    auto path = raw_name_ptr.value();
    if (path.empty()) {
      loader_data.launch_path = "game:\\default.xex";
    } else {
      if (xe::utf8::find_name_from_guest_path(path) == path) {
        path = xe::utf8::join_guest_paths(
            xe::utf8::find_base_guest_path(
                kernel_state()->GetExecutableModule()->path()),
            path);
      }
      loader_data.launch_path = path;
    }
  } else {
    assert_always("Game requested exit to dashboard via XamLoaderLaunchTitle");
  }

  // This function does not return.
  kernel_state()->TerminateTitle();
}
DECLARE_XAM_EXPORT1(XamLoaderLaunchTitle, kNone, kSketchy);

void XamLoaderTerminateTitle() {
  // This function does not return.
  kernel_state()->TerminateTitle();
}
DECLARE_XAM_EXPORT1(XamLoaderTerminateTitle, kNone, kSketchy);

dword_result_t XamAlloc(dword_t unk, dword_t size, lpdword_t out_ptr) {
  assert_true(unk == 0);

  // Allocate from the heap. Not sure why XAM does this specially, perhaps
  // it keeps stuff in a separate heap?
  uint32_t ptr = kernel_state()->memory()->SystemHeapAlloc(size);
  *out_ptr = ptr;

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamAlloc, kMemory, kImplemented);

dword_result_t XamFree(lpdword_t ptr) {
  kernel_state()->memory()->SystemHeapFree(ptr.guest_address());

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamFree, kMemory, kImplemented);

dword_result_t XamQueryLiveHiveW(lpu16string_t name, lpvoid_t out_buf,
                                 dword_t out_size, dword_t type /* guess */) {
  return X_STATUS_INVALID_PARAMETER_1;
}
DECLARE_XAM_EXPORT1(XamQueryLiveHiveW, kNone, kStub);

void RegisterInfoExports(xe::cpu::ExportResolver* export_resolver,
                         KernelState* kernel_state) {}

}  // namespace xam
}  // namespace kernel
}  // namespace xe
