/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/logging.h"
#include "xenia/base/memory.h"
#include "xenia/base/mutex.h"
#include "xenia/cpu/processor.h"
#include "xenia/kernel/info/file.h"
#include "xenia/kernel/info/volume.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_private.h"
#include "xenia/kernel/xevent.h"
#include "xenia/kernel/xfile.h"
#include "xenia/kernel/xiocompletion.h"
#include "xenia/kernel/xsymboliclink.h"
#include "xenia/kernel/xthread.h"
#include "xenia/vfs/device.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xboxkrnl {

uint32_t GetQueryFileInfoMinimumLength(uint32_t info_class) {
  switch (info_class) {
    case XFileInternalInformation:
      return sizeof(X_FILE_INTERNAL_INFORMATION);
    case XFilePositionInformation:
      return sizeof(X_FILE_POSITION_INFORMATION);
    case XFileXctdCompressionInformation:
      return sizeof(X_FILE_XCTD_COMPRESSION_INFORMATION);
    case XFileNetworkOpenInformation:
      return sizeof(X_FILE_NETWORK_OPEN_INFORMATION);
    // TODO(gibbed): structures to get the size of.
    case XFileModeInformation:
    case XFileAlignmentInformation:
    case XFileSectorInformation:
    case XFileIoPriorityInformation:
      return 4;
    case XFileNameInformation:
    case XFileAllocationInformation:
      return 8;
    case XFileBasicInformation:
      return 40;
    default:
      return 0;
  }
}

dword_result_t NtQueryInformationFile(
    dword_t file_handle, pointer_t<X_IO_STATUS_BLOCK> io_status_block_ptr,
    lpvoid_t info_ptr, dword_t info_length, dword_t info_class) {
  uint32_t minimum_length = GetQueryFileInfoMinimumLength(info_class);
  if (!minimum_length) {
    return X_STATUS_INVALID_INFO_CLASS;
  }

  if (info_length < minimum_length) {
    return X_STATUS_INFO_LENGTH_MISMATCH;
  }

  auto file = kernel_state()->object_table()->LookupObject<XFile>(file_handle);
  if (!file) {
    return X_STATUS_INVALID_HANDLE;
  }

  info_ptr.Zero(info_length);

  X_STATUS status = X_STATUS_SUCCESS;
  uint32_t out_length;

  switch (info_class) {
    case XFileInternalInformation: {
      // Internal unique file pointer. Not sure why anyone would want this.
      // TODO(benvanik): use pointer to fs::entry?
      auto info = info_ptr.as<X_FILE_INTERNAL_INFORMATION*>();
      info->index_number = xe::memory::hash_combine(0, file->path());
      out_length = sizeof(*info);
      break;
    }
    case XFilePositionInformation: {
      auto info = info_ptr.as<X_FILE_POSITION_INFORMATION*>();
      info->current_byte_offset = file->position();
      out_length = sizeof(*info);
      break;
    }
    case XFileSectorInformation: {
      // TODO(benvanik): return sector this file's on.
      XELOGE("NtQueryInformationFile(XFileSectorInformation) unimplemented");
      status = X_STATUS_INVALID_PARAMETER;
      out_length = 0;
      break;
    }
    case XFileXctdCompressionInformation: {
      XELOGE(
          "NtQueryInformationFile(XFileXctdCompressionInformation) "
          "unimplemented");
      // Files that are XCTD compressed begin with the magic 0x0FF512ED but we
      // shouldn't detect this that way. There's probably a flag somewhere
      // (attributes?) that defines if it's compressed or not.
      status = X_STATUS_INVALID_PARAMETER;
      out_length = 0;
      break;
    };
    case XFileNetworkOpenInformation: {
      // Make sure we're working with up-to-date information, just in case the
      // file size has changed via something other than NtSetInfoFile
      // (eg. seems NtWriteFile might extend the file in some cases)
      file->entry()->update();

      auto info = info_ptr.as<X_FILE_NETWORK_OPEN_INFORMATION*>();
      info->creation_time = file->entry()->create_timestamp();
      info->last_access_time = file->entry()->access_timestamp();
      info->last_write_time = file->entry()->write_timestamp();
      info->change_time = file->entry()->write_timestamp();
      info->allocation_size = file->entry()->allocation_size();
      info->end_of_file = file->entry()->size();
      info->attributes = file->entry()->attributes();
      out_length = sizeof(*info);
      break;
    }
    case XFileAlignmentInformation: {
      // Requested by XMountUtilityDrive XAM-task
      auto info = info_ptr.as<uint32_t*>();
      *info = 0;  // FILE_BYTE_ALIGNMENT?
      out_length = sizeof(*info);
      break;
    }
    default: {
      // Unsupported, for now.
      assert_always();
      status = X_STATUS_INVALID_PARAMETER;
      out_length = 0;
      break;
    }
  }

  if (io_status_block_ptr) {
    io_status_block_ptr->status = status;
    io_status_block_ptr->information = out_length;
  }

  return status;
}
DECLARE_XBOXKRNL_EXPORT1(NtQueryInformationFile, kFileSystem, kImplemented);

uint32_t GetSetFileInfoMinimumLength(uint32_t info_class) {
  switch (info_class) {
    case XFileDispositionInformation:
      return sizeof(X_FILE_DISPOSITION_INFORMATION);
    case XFilePositionInformation:
      return sizeof(X_FILE_POSITION_INFORMATION);
    case XFileCompletionInformation:
      return sizeof(X_FILE_COMPLETION_INFORMATION);
    // TODO(gibbed): structures to get the size of.
    case XFileModeInformation:
    case XFileIoPriorityInformation:
      return 4;
    case XFileAllocationInformation:
    case XFileEndOfFileInformation:
    case XFileMountPartitionInformation:
      return 8;
    case XFileRenameInformation:
    case XFileLinkInformation:
      return 16;
    case XFileBasicInformation:
      return 40;
    case XFileMountPartitionsInformation:
      return 152;
    default:
      return 0;
  }
}

dword_result_t NtSetInformationFile(
    dword_t file_handle, pointer_t<X_IO_STATUS_BLOCK> io_status_block,
    lpvoid_t info_ptr, dword_t info_length, dword_t info_class) {
  uint32_t minimum_length = GetSetFileInfoMinimumLength(info_class);
  if (!minimum_length) {
    return X_STATUS_INVALID_INFO_CLASS;
  }

  if (info_length < minimum_length) {
    return X_STATUS_INFO_LENGTH_MISMATCH;
  }

  auto file = kernel_state()->object_table()->LookupObject<XFile>(file_handle);
  if (!file) {
    return X_STATUS_INVALID_HANDLE;
  }

  X_STATUS result = X_STATUS_SUCCESS;
  uint32_t out_length;

  switch (info_class) {
    case XFileDispositionInformation: {
      // Used to set deletion flag. Which we don't support. Probably?
      auto info = info_ptr.as<X_FILE_DISPOSITION_INFORMATION*>();
      bool delete_on_close = info->delete_file ? true : false;
      out_length = 0;
      XELOGW("NtSetInformationFile ignoring delete on close: {}",
             delete_on_close);
      break;
    }
    case XFilePositionInformation: {
      auto info = info_ptr.as<X_FILE_POSITION_INFORMATION*>();
      file->set_position(info->current_byte_offset);
      out_length = sizeof(*info);
      break;
    }
    case XFileAllocationInformation: {
      XELOGW("NtSetInformationFile ignoring alloc");
      out_length = 8;
      break;
    }
    case XFileEndOfFileInformation: {
      auto info = info_ptr.as<X_FILE_END_OF_FILE_INFORMATION*>();
      result = file->SetLength(info->end_of_file);
      out_length = sizeof(*info);

      // Update the files vfs::Entry information
      file->entry()->update();
      break;
    }
    case XFileCompletionInformation: {
      // Info contains IO Completion handle and completion key
      auto info = info_ptr.as<X_FILE_COMPLETION_INFORMATION*>();
      auto handle = uint32_t(info->handle);
      auto key = uint32_t(info->key);
      out_length = sizeof(*info);
      auto port =
          kernel_state()->object_table()->LookupObject<XIOCompletion>(handle);
      if (!port) {
        result = X_STATUS_INVALID_HANDLE;
      } else {
        file->RegisterIOCompletionPort(key, port);
      }
      break;
    }
    default:
      // Unsupported, for now.
      assert_always();
      out_length = 0;
      break;
  }

  if (io_status_block) {
    io_status_block->status = result;
    io_status_block->information = out_length;
  }

  return result;
}
DECLARE_XBOXKRNL_EXPORT2(NtSetInformationFile, kFileSystem, kImplemented,
                         kHighFrequency);

uint32_t GetQueryVolumeInfoMinimumLength(uint32_t info_class) {
  switch (info_class) {
    case XFileFsVolumeInformation:
      return sizeof(X_FILE_FS_VOLUME_INFORMATION);
    case XFileFsSizeInformation:
      return sizeof(X_FILE_FS_SIZE_INFORMATION);
    case XFileFsAttributeInformation:
      return sizeof(X_FILE_FS_ATTRIBUTE_INFORMATION);
    // TODO(gibbed): structures to get the size of.
    case XFileFsDeviceInformation:
      return 8;
    default:
      return 0;
  }
}

dword_result_t NtQueryVolumeInformationFile(
    dword_t file_handle, pointer_t<X_IO_STATUS_BLOCK> io_status_block_ptr,
    lpvoid_t info_ptr, dword_t info_length, dword_t info_class) {
  uint32_t minimum_length = GetQueryVolumeInfoMinimumLength(info_class);
  if (!minimum_length) {
    return X_STATUS_INVALID_INFO_CLASS;
  }

  if (info_length < minimum_length) {
    return X_STATUS_INFO_LENGTH_MISMATCH;
  }

  auto file = kernel_state()->object_table()->LookupObject<XFile>(file_handle);
  if (!file) {
    return X_STATUS_INVALID_HANDLE;
  }

  info_ptr.Zero(info_length);

  X_STATUS status = X_STATUS_SUCCESS;
  uint32_t out_length;

  switch (info_class) {
    case XFileFsVolumeInformation: {
      auto info = info_ptr.as<X_FILE_FS_VOLUME_INFORMATION*>();
      info->creation_time = 0;
      info->serial_number = 0;  // set for FATX, but we don't do that currently
      info->supports_objects = 0;
      info->label_length = 0;
      out_length = offsetof(X_FILE_FS_VOLUME_INFORMATION, label);
      break;
    }
    case XFileFsSizeInformation: {
      auto device = file->device();
      auto info = info_ptr.as<X_FILE_FS_SIZE_INFORMATION*>();
      info->total_allocation_units = device->total_allocation_units();
      info->available_allocation_units = device->available_allocation_units();
      info->sectors_per_allocation_unit = device->sectors_per_allocation_unit();
      info->bytes_per_sector = device->bytes_per_sector();
      // TODO(gibbed): sanity check, XCTD userland code seems to require this.
      assert_true(info->bytes_per_sector == 0x200);
      out_length = sizeof(*info);
      break;
    }
    case XFileFsAttributeInformation: {
      auto device = file->device();
      const auto& name = device->name();
      auto info = info_ptr.as<X_FILE_FS_ATTRIBUTE_INFORMATION*>();
      info->attributes = device->attributes();
      info->component_name_max_length = device->component_name_max_length();
      info->name_length = uint32_t(name.size());
      if (info_length >= 12 + name.size()) {
        std::memcpy(info->name, name.data(), name.size());
        out_length =
            offsetof(X_FILE_FS_ATTRIBUTE_INFORMATION, name) + info->name_length;
      } else {
        status = X_STATUS_BUFFER_OVERFLOW;
        out_length = offsetof(X_FILE_FS_ATTRIBUTE_INFORMATION, name);
      }
      break;
    }
    case XFileFsDeviceInformation:
    default: {
      // Unsupported, for now.
      assert_always();
      out_length = 0;
      break;
    }
  }

  if (io_status_block_ptr) {
    io_status_block_ptr->status = status;
    io_status_block_ptr->information = out_length;
  }

  return status;
}
DECLARE_XBOXKRNL_EXPORT1(NtQueryVolumeInformationFile, kFileSystem,
                         kImplemented);

void RegisterIoInfoExports(xe::cpu::ExportResolver* export_resolver,
                           KernelState* kernel_state) {}

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe
