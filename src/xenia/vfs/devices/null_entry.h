/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_VFS_DEVICES_NULL_ENTRY_H_
#define XENIA_VFS_DEVICES_NULL_ENTRY_H_

#include <string>

#include "xenia/base/filesystem.h"
#include "xenia/vfs/entry.h"

namespace xe {
namespace vfs {

class NullDevice;

class NullEntry : public Entry {
 public:
  NullEntry(Device* device, Entry* parent, std::string path);
  ~NullEntry() override;

  static NullEntry* Create(Device* device, Entry* parent,
                           const std::string& path);

  X_STATUS Open(uint32_t desired_access, File** out_file) override;

  bool can_map() const override { return false; }

 private:
  friend class NullDevice;
};

}  // namespace vfs
}  // namespace xe

#endif  // XENIA_VFS_DEVICES_NULL_ENTRY_H_
