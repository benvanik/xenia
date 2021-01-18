/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_VFS_DEVICES_HOST_PATH_ENTRY_H_
#define XENIA_VFS_DEVICES_HOST_PATH_ENTRY_H_

#include <string>

#include "xenia/base/filesystem.h"
#include "xenia/vfs/entry.h"

namespace xe {
namespace vfs {

class HostPathDevice;

class HostPathEntry : public Entry {
 public:
  HostPathEntry(Device* device, Entry* parent, const std::string_view path,
                const std::filesystem::path& host_path);
  ~HostPathEntry() override;

  static HostPathEntry* Create(Device* device, Entry* parent,
                               const std::filesystem::path& full_path,
                               xe::filesystem::FileInfo file_info);

  const std::filesystem::path& host_path() { return host_path_; }
  void SetHostFileName(const std::string_view filename) override;

  X_STATUS Open(uint32_t desired_access, File** out_file) override;

  bool can_map() const override { return true; }
  std::unique_ptr<MappedMemory> OpenMapped(MappedMemory::Mode mode,
                                           size_t offset,
                                           size_t length) override;
  void update() override;

 private:
  friend class HostPathDevice;

  std::unique_ptr<Entry> CreateEntryInternal(const std::string_view name,
                                             uint32_t attributes) override;
  bool DeleteEntryInternal(Entry* entry) override;

  std::filesystem::path host_path_;
};

}  // namespace vfs
}  // namespace xe

#endif  // XENIA_VFS_DEVICES_HOST_PATH_ENTRY_H_
