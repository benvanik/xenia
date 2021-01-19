/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/string_util.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/xam_content_device.h"
#include "xenia/kernel/xam/xam_private.h"
#include "xenia/kernel/xenumerator.h"
#include "xenia/xbox.h"

DEFINE_int32(
    license_mask, 0,
    "Set license mask for activated content.\n"
    " 0 = No licenses enabled.\n"
    " 1 = First license enabled. Generally the full version license in\n"
    "     Xbox Live Arcade titles.\n"
    " -1 or 0xFFFFFFFF = All possible licenses enabled. Generally a\n"
    "                    bad idea, could lead to undefined behavior.",
    "Content");

namespace xe {
namespace kernel {
namespace xam {

dword_result_t XamContentGetLicenseMask(lpdword_t mask_ptr,
                                        lpunknown_t overlapped_ptr) {
  // Each bit in the mask represents a granted license. Available licenses
  // seems to vary from game to game, but most appear to use bit 0 to indicate
  // if the game is purchased or not.
  *mask_ptr = static_cast<uint32_t>(cvars::license_mask);

  if (overlapped_ptr) {
    kernel_state()->CompleteOverlappedImmediate(overlapped_ptr,
                                                X_ERROR_SUCCESS);
    return X_ERROR_IO_PENDING;
  } else {
    return X_ERROR_SUCCESS;
  }
}
DECLARE_XAM_EXPORT2(XamContentGetLicenseMask, kContent, kStub, kHighFrequency);

dword_result_t XamContentResolve(dword_t user_index, lpvoid_t content_data_ptr,
                                 lpunknown_t buffer_ptr, dword_t buffer_size,
                                 dword_t unk1, dword_t unk2, dword_t unk3) {
  auto content_data = content_data_ptr.as<XCONTENT_DATA*>();

  // Result of buffer_ptr is sent to RtlInitAnsiString.
  // buffer_size is usually 260 (max path).
  // Games expect zero if resolve was successful.
  assert_always();
  XELOGW("XamContentResolve unimplemented!");
  return X_ERROR_NOT_FOUND;
}
DECLARE_XAM_EXPORT1(XamContentResolve, kContent, kStub);

// https://github.com/MrColdbird/gameservice/blob/master/ContentManager.cpp
// https://github.com/LestaD/SourceEngine2007/blob/master/se2007/engine/xboxsystem.cpp#L499
dword_result_t XamContentCreateEnumerator(dword_t user_index, dword_t device_id,
                                          dword_t content_type,
                                          dword_t content_flags,
                                          dword_t items_per_enumerate,
                                          lpdword_t buffer_size_ptr,
                                          lpdword_t handle_out) {
  assert_not_null(handle_out);

  auto device_info = device_id == 0 ? nullptr : GetDummyDeviceInfo(device_id);
  if ((device_id && device_info == nullptr) || !handle_out) {
    if (buffer_size_ptr) {
      *buffer_size_ptr = 0;
    }

    // TODO(benvanik): memset 0 the data?
    return X_E_INVALIDARG;
  }

  if (buffer_size_ptr) {
    *buffer_size_ptr = sizeof(XCONTENT_DATA) * items_per_enumerate;
  }

  auto e = object_ref<XStaticEnumerator>(new XStaticEnumerator(
      kernel_state(), items_per_enumerate, sizeof(XCONTENT_DATA)));
  auto result = e->Initialize(0xFF, 0xFE, 0x20005, 0x20007, 0);
  if (XFAILED(result)) {
    return result;
  }

  if (!device_info || device_info->device_id == DummyDeviceId::HDD) {
    // Get all content data.
    auto content_datas = kernel_state()->content_manager()->ListContent(
        static_cast<uint32_t>(DummyDeviceId::HDD),
        XContentType(uint32_t(content_type)));
    for (const auto& content_data : content_datas) {
      auto item = reinterpret_cast<XCONTENT_DATA*>(e->AppendItem());
      assert_not_null(item);
      *item = content_data;
    }
  }

  if (!device_info || device_info->device_id == DummyDeviceId::ODD) {
    // TODO(gibbed): disc drive content
  }

  XELOGD("XamContentCreateEnumerator: added {} items to enumerator",
         e->item_count());

  *handle_out = e->handle();
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamContentCreateEnumerator, kContent, kImplemented);

dword_result_t xeXamContentCreate(dword_t user_index, lpstring_t root_name,
                                  lpvoid_t content_data_ptr,
                                  dword_t content_data_size, dword_t flags,
                                  lpdword_t disposition_ptr,
                                  lpdword_t license_mask_ptr,
                                  dword_t cache_size, qword_t content_size,
                                  lpvoid_t overlapped_ptr) {
  X_RESULT result = X_ERROR_INVALID_PARAMETER;
  XCONTENT_AGGREGATE_DATA content_data;
  if (content_data_size == sizeof(XCONTENT_DATA)) {
    content_data = *content_data_ptr.as<XCONTENT_DATA*>();
  } else if (content_data_size == sizeof(XCONTENT_AGGREGATE_DATA)) {
    content_data = *content_data_ptr.as<XCONTENT_AGGREGATE_DATA*>();
  } else {
    assert_always();
    return result;
  }

  auto content_manager = kernel_state()->content_manager();
  bool create = false;
  bool open = false;
  switch (flags & 0xF) {
    case 1:  // CREATE_NEW
             // Fail if exists.
      if (content_manager->ContentExists(content_data)) {
        result = X_ERROR_ALREADY_EXISTS;
      } else {
        create = true;
      }
      break;
    case 2:  // CREATE_ALWAYS
             // Overwrite existing, if any.
      if (content_manager->ContentExists(content_data)) {
        content_manager->DeleteContent(content_data);
        create = true;
      } else {
        create = true;
      }
      break;
    case 3:  // OPEN_EXISTING
             // Open only if exists.
      if (!content_manager->ContentExists(content_data)) {
        result = X_ERROR_PATH_NOT_FOUND;
      } else {
        open = true;
      }
      break;
    case 4:  // OPEN_ALWAYS
             // Create if needed.
      if (!content_manager->ContentExists(content_data)) {
        create = true;
      } else {
        open = true;
      }
      break;
    case 5:  // TRUNCATE_EXISTING
             // Fail if doesn't exist, if does exist delete and recreate.
      if (!content_manager->ContentExists(content_data)) {
        result = X_ERROR_PATH_NOT_FOUND;
      } else {
        content_manager->DeleteContent(content_data);
        create = true;
      }
      break;
    default:
      assert_unhandled_case(flags & 0xF);
      break;
  }

  // creation result
  // 0 = ?
  // 1 = created
  // 2 = opened
  uint32_t disposition = create ? 1 : 2;
  if (disposition_ptr) {
    // In case when overlapped_ptr exist we should clear disposition_ptr first
    // however we're executing it immediately, so it's not required
    *disposition_ptr = disposition;
  }

  if (create) {
    result = content_manager->CreateContent(root_name.value(), content_data);
    if (XSUCCEEDED(result)) {
      const XCONTENT_DATA* content_data_raw =
          content_data_ptr.as<XCONTENT_DATA*>();
      content_manager->WriteContentHeaderFile(content_data.file_name(),
                                              content_data_raw);
    }
  } else if (open) {
    result = content_manager->OpenContent(root_name.value(), content_data);
  }

  if (license_mask_ptr && XSUCCEEDED(result)) {
    *license_mask_ptr = 0;  // Stub!
  }

  if (overlapped_ptr) {
    X_RESULT extended_error = X_HRESULT_FROM_WIN32(result);
    if (int32_t(extended_error) < 0) {
      result = X_ERROR_FUNCTION_FAILED;
    }
    kernel_state()->CompleteOverlappedImmediateEx(overlapped_ptr, result,
                                                  extended_error, disposition);
    return X_ERROR_IO_PENDING;
  } else {
    return result;
  }
}

dword_result_t XamContentCreateEx(dword_t user_index, lpstring_t root_name,
                                  lpvoid_t content_data_ptr, dword_t flags,
                                  lpdword_t disposition_ptr,
                                  lpdword_t license_mask_ptr,
                                  dword_t cache_size, qword_t content_size,
                                  lpvoid_t overlapped_ptr) {
  return xeXamContentCreate(user_index, root_name, content_data_ptr,
                            sizeof(XCONTENT_DATA), flags, disposition_ptr,
                            license_mask_ptr, cache_size, content_size,
                            overlapped_ptr);
}
DECLARE_XAM_EXPORT1(XamContentCreateEx, kContent, kImplemented);

dword_result_t XamContentCreate(dword_t user_index, lpstring_t root_name,
                                lpvoid_t content_data_ptr, dword_t flags,
                                lpdword_t disposition_ptr,
                                lpdword_t license_mask_ptr,
                                lpvoid_t overlapped_ptr) {
  return xeXamContentCreate(user_index, root_name, content_data_ptr,
                            sizeof(XCONTENT_DATA), flags, disposition_ptr,
                            license_mask_ptr, 0, 0, overlapped_ptr);
}
DECLARE_XAM_EXPORT1(XamContentCreate, kContent, kImplemented);

dword_result_t XamContentCreateInternal(
    lpstring_t root_name, lpvoid_t content_data_ptr, dword_t flags,
    lpdword_t disposition_ptr, lpdword_t license_mask_ptr, dword_t cache_size,
    qword_t content_size, lpvoid_t overlapped_ptr) {
  return xeXamContentCreate(0xFE, root_name, content_data_ptr,
                            sizeof(XCONTENT_AGGREGATE_DATA), flags,
                            disposition_ptr, license_mask_ptr, cache_size,
                            content_size, overlapped_ptr);
}
DECLARE_XAM_EXPORT1(XamContentCreateInternal, kContent, kImplemented);

dword_result_t XamContentOpenFile(dword_t user_index, lpstring_t root_name,
                                  lpstring_t path, dword_t flags,
                                  lpdword_t disposition_ptr,
                                  lpdword_t license_mask_ptr,
                                  lpvoid_t overlapped_ptr) {
  // TODO(gibbed): arguments assumed based on XamContentCreate.
  return X_ERROR_FILE_NOT_FOUND;
}
DECLARE_XAM_EXPORT1(XamContentOpenFile, kContent, kStub);

dword_result_t XamContentFlush(lpstring_t root_name,
                               lpunknown_t overlapped_ptr) {
  X_RESULT result = X_ERROR_SUCCESS;
  if (overlapped_ptr) {
    kernel_state()->CompleteOverlappedImmediate(overlapped_ptr, result);
    return X_ERROR_IO_PENDING;
  } else {
    return result;
  }
}
DECLARE_XAM_EXPORT1(XamContentFlush, kContent, kStub);

dword_result_t XamContentClose(lpstring_t root_name,
                               lpunknown_t overlapped_ptr) {
  // Closes a previously opened root from XamContentCreate*.
  auto result =
      kernel_state()->content_manager()->CloseContent(root_name.value());

  if (overlapped_ptr) {
    kernel_state()->CompleteOverlappedImmediate(overlapped_ptr, result);
    return X_ERROR_IO_PENDING;
  } else {
    return result;
  }
}
DECLARE_XAM_EXPORT1(XamContentClose, kContent, kImplemented);

dword_result_t XamContentGetCreator(dword_t user_index,
                                    lpvoid_t content_data_ptr,
                                    lpdword_t is_creator_ptr,
                                    lpqword_t creator_xuid_ptr,
                                    lpunknown_t overlapped_ptr) {
  auto result = X_ERROR_SUCCESS;

  XCONTENT_AGGREGATE_DATA content_data = *content_data_ptr.as<XCONTENT_DATA*>();

  bool content_exists =
      kernel_state()->content_manager()->ContentExists(content_data);

  if (content_exists) {
    if (content_data.content_type == XContentType::kSavedGame) {
      // User always creates saves.
      *is_creator_ptr = 1;
      if (creator_xuid_ptr) {
        *creator_xuid_ptr = kernel_state()->user_profile()->xuid();
      }
    } else {
      *is_creator_ptr = 0;
      if (creator_xuid_ptr) {
        *creator_xuid_ptr = 0;
      }
    }
  } else {
    result = X_ERROR_PATH_NOT_FOUND;
  }

  if (overlapped_ptr) {
    kernel_state()->CompleteOverlappedImmediate(overlapped_ptr, result);
    return X_ERROR_IO_PENDING;
  } else {
    return result;
  }
}
DECLARE_XAM_EXPORT1(XamContentGetCreator, kContent, kImplemented);

dword_result_t XamContentGetThumbnail(dword_t user_index,
                                      lpvoid_t content_data_ptr,
                                      lpvoid_t buffer_ptr,
                                      lpdword_t buffer_size_ptr,
                                      lpunknown_t overlapped_ptr) {
  assert_not_null(buffer_size_ptr);
  uint32_t buffer_size = *buffer_size_ptr;
  XCONTENT_AGGREGATE_DATA content_data = *content_data_ptr.as<XCONTENT_DATA*>();

  // Get thumbnail (if it exists).
  std::vector<uint8_t> buffer;
  auto result = kernel_state()->content_manager()->GetContentThumbnail(
      content_data, &buffer);

  *buffer_size_ptr = uint32_t(buffer.size());

  if (XSUCCEEDED(result)) {
    // Write data, if we were given a pointer.
    // This may have just been a size query.
    if (buffer_ptr) {
      if (buffer_size < buffer.size()) {
        // Dest buffer too small.
        result = X_ERROR_INSUFFICIENT_BUFFER;
      } else {
        // Copy data.
        std::memcpy((uint8_t*)buffer_ptr, buffer.data(), buffer.size());
      }
    }
  }

  if (overlapped_ptr) {
    kernel_state()->CompleteOverlappedImmediate(overlapped_ptr, result);
    return X_ERROR_IO_PENDING;
  } else {
    return result;
  }
}
DECLARE_XAM_EXPORT1(XamContentGetThumbnail, kContent, kImplemented);

dword_result_t XamContentSetThumbnail(dword_t user_index,
                                      lpvoid_t content_data_ptr,
                                      lpvoid_t buffer_ptr, dword_t buffer_size,
                                      lpunknown_t overlapped_ptr) {
  XCONTENT_AGGREGATE_DATA content_data = *content_data_ptr.as<XCONTENT_DATA*>();

  // Buffer is PNG data.
  auto buffer = std::vector<uint8_t>((uint8_t*)buffer_ptr,
                                     (uint8_t*)buffer_ptr + buffer_size);
  auto result = kernel_state()->content_manager()->SetContentThumbnail(
      content_data, std::move(buffer));

  if (overlapped_ptr) {
    kernel_state()->CompleteOverlappedImmediate(overlapped_ptr, result);
    return X_ERROR_IO_PENDING;
  } else {
    return result;
  }
}
DECLARE_XAM_EXPORT1(XamContentSetThumbnail, kContent, kImplemented);

dword_result_t XamContentDelete(dword_t user_index, lpvoid_t content_data_ptr,
                                lpunknown_t overlapped_ptr) {
  XCONTENT_AGGREGATE_DATA content_data = *content_data_ptr.as<XCONTENT_DATA*>();

  auto result = kernel_state()->content_manager()->DeleteContent(content_data);

  if (overlapped_ptr) {
    kernel_state()->CompleteOverlappedImmediate(overlapped_ptr, result);
    return X_ERROR_IO_PENDING;
  } else {
    return result;
  }
}
DECLARE_XAM_EXPORT1(XamContentDelete, kContent, kImplemented);

dword_result_t XamContentDeleteInternal(lpvoid_t content_data_ptr,
                                        lpunknown_t overlapped_ptr) {
  // INFO: Analysis of xam.xex shows that "internal" functions are wrappers with
  // 0xFE as user_index
  return XamContentDelete(0xFE, content_data_ptr, overlapped_ptr);
}
DECLARE_XAM_EXPORT1(XamContentDeleteInternal, kContent, kImplemented);

void RegisterContentExports(xe::cpu::ExportResolver* export_resolver,
                            KernelState* kernel_state) {}

}  // namespace xam
}  // namespace kernel
}  // namespace xe
