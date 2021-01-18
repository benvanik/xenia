/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_VFS_DEVICES_STFS_CONTAINER_ENTRY_H_
#define XENIA_VFS_DEVICES_STFS_CONTAINER_ENTRY_H_

#include <map>
#include <string>
#include <vector>

#include "xenia/vfs/entry.h"
#include "xenia/vfs/file.h"

namespace xe {
namespace vfs {
typedef std::map<size_t, FILE*> MultiFileHandles;

class StfsContainerDevice;

class StfsContainerEntry : public Entry {
 public:
  StfsContainerEntry(Device* device, Entry* parent, const std::string_view path,
                     MultiFileHandles* files);
  ~StfsContainerEntry() override;

  static std::unique_ptr<StfsContainerEntry> Create(Device* device,
                                                    Entry* parent,
                                                    const std::string_view name,
                                                    MultiFileHandles* files);

  MultiFileHandles* files() const { return files_; }
  size_t data_offset() const { return data_offset_; }
  size_t data_size() const { return data_size_; }
  size_t block() const { return block_; }

  void SetHostFileName(const std::string_view filename) override { return; }
  X_STATUS Open(uint32_t desired_access, File** out_file) override;

  struct BlockRecord {
    size_t file;
    size_t offset;
    size_t length;
  };
  const std::vector<BlockRecord>& block_list() const { return block_list_; }

 private:
  friend class StfsContainerDevice;

  MultiFileHandles* files_;
  size_t data_offset_;
  size_t data_size_;
  size_t block_;
  std::vector<BlockRecord> block_list_;
};

}  // namespace vfs
}  // namespace xe

#endif  // XENIA_VFS_DEVICES_STFS_CONTAINER_ENTRY_H_