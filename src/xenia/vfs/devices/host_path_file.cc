/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/devices/host_path_file.h"

#include "xenia/vfs/devices/host_path_entry.h"

namespace xe {
namespace vfs {

HostPathFile::HostPathFile(
    uint32_t file_access, HostPathEntry* entry,
    std::unique_ptr<xe::filesystem::FileHandle> file_handle)
    : File(file_access, entry), file_handle_(std::move(file_handle)) {}

HostPathFile::~HostPathFile() = default;

void HostPathFile::Destroy() { delete this; }

X_STATUS HostPathFile::ReadSync(void* buffer, size_t buffer_length,
                                size_t byte_offset, size_t* out_bytes_read) {
  if (!(file_access_ &
        (FileAccess::kGenericRead | FileAccess::kFileReadData))) {
    return X_STATUS_ACCESS_DENIED;
  }

  if (file_handle_->Read(byte_offset, buffer, buffer_length, out_bytes_read)) {
    return X_STATUS_SUCCESS;
  } else {
    return X_STATUS_END_OF_FILE;
  }
}

X_STATUS HostPathFile::WriteSync(const void* buffer, size_t buffer_length,
                                 size_t byte_offset,
                                 size_t* out_bytes_written) {
  if (!(file_access_ & (FileAccess::kGenericWrite | FileAccess::kFileWriteData |
                        FileAccess::kFileAppendData))) {
    return X_STATUS_ACCESS_DENIED;
  }

  if (file_handle_->Write(byte_offset, buffer, buffer_length,
                          out_bytes_written)) {
    return X_STATUS_SUCCESS;
  } else {
    return X_STATUS_END_OF_FILE;
  }
}

X_STATUS HostPathFile::SetLength(size_t length) {
  if (!(file_access_ &
        (FileAccess::kGenericWrite | FileAccess::kFileWriteData))) {
    return X_STATUS_ACCESS_DENIED;
  }

  if (file_handle_->SetLength(length)) {
    return X_STATUS_SUCCESS;
  } else {
    return X_STATUS_END_OF_FILE;
  }
}

X_STATUS HostPathFile::SetName(const std::string_view file_name) {
  if (file_handle_->Rename(file_name)) {
    return X_STATUS_SUCCESS;
  } else {
    return X_STATUS_NO_SUCH_FILE;
  }
}

}  // namespace vfs
}  // namespace xe
