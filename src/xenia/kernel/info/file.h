/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_INFO_FILE_H_
#define XENIA_KERNEL_INFO_FILE_H_

#include "xenia/xbox.h"

namespace xe {
namespace kernel {

// https://github.com/oukiar/vdash/blob/master/vdash/include/kernel.h
enum X_FILE_INFORMATION_CLASS {
  XFileDirectoryInformation = 1,
  XFileFullDirectoryInformation,
  XFileBothDirectoryInformation,
  XFileBasicInformation,
  XFileStandardInformation,
  XFileInternalInformation,
  XFileEaInformation,
  XFileAccessInformation,
  XFileNameInformation,
  XFileRenameInformation,
  XFileLinkInformation,
  XFileNamesInformation,
  XFileDispositionInformation,
  XFilePositionInformation,
  XFileFullEaInformation,
  XFileModeInformation,
  XFileAlignmentInformation,
  XFileAllInformation,
  XFileAllocationInformation,
  XFileEndOfFileInformation,
  XFileAlternateNameInformation,
  XFileStreamInformation,
  XFileMountPartitionInformation,
  XFileMountPartitionsInformation,
  XFilePipeRemoteInformation,
  XFileSectorInformation,
  XFileXctdCompressionInformation,
  XFileCompressionInformation,
  XFileObjectIdInformation,
  XFileCompletionInformation,
  XFileMoveClusterInformation,
  XFileIoPriorityInformation,
  XFileReparsePointInformation,
  XFileNetworkOpenInformation,
  XFileAttributeTagInformation,
  XFileTrackingInformation,
  XFileMaximumInformation
};

#pragma pack(push, 1)

// https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_internal_information
struct X_FILE_INTERNAL_INFORMATION {
  be<uint64_t> index_number;
};
static_assert_size(X_FILE_INTERNAL_INFORMATION, 8);

// https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/ntddk/ns-ntddk-_file_disposition_information
struct X_FILE_DISPOSITION_INFORMATION {
  uint8_t delete_file;
};
static_assert_size(X_FILE_DISPOSITION_INFORMATION, 1);

// https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/ns-wdm-_file_position_information
struct X_FILE_POSITION_INFORMATION {
  be<uint64_t> current_byte_offset;
};
static_assert_size(X_FILE_POSITION_INFORMATION, 8);

// https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/ntddk/ns-ntddk-_file_end_of_file_information
struct X_FILE_END_OF_FILE_INFORMATION {
  be<uint64_t> end_of_file;
};
static_assert_size(X_FILE_END_OF_FILE_INFORMATION, 8);

struct X_FILE_XCTD_COMPRESSION_INFORMATION {
  be<uint32_t> unknown;
};
static_assert_size(X_FILE_XCTD_COMPRESSION_INFORMATION, 4);

// https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_completion_information
struct X_FILE_COMPLETION_INFORMATION {
  be<uint32_t> handle;
  be<uint32_t> key;
};
static_assert_size(X_FILE_COMPLETION_INFORMATION, 8);

// https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/ns-wdm-_file_network_open_information
struct X_FILE_NETWORK_OPEN_INFORMATION {
  be<uint64_t> creation_time;
  be<uint64_t> last_access_time;
  be<uint64_t> last_write_time;
  be<uint64_t> change_time;
  be<uint64_t> allocation_size;
  be<uint64_t> end_of_file;  // size in bytes
  be<uint32_t> attributes;
  be<uint32_t> pad;
};
static_assert_size(X_FILE_NETWORK_OPEN_INFORMATION, 56);

struct X_FILE_RENAME_INFORMATION {
  be<uint32_t> rename;
  be<uint32_t> root_dir_handle;
  X_ANSI_STRING ansi_string;
};
static_assert_size(X_FILE_RENAME_INFORMATION, 16);

#pragma pack(pop)

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_INFO_FILE_H_
