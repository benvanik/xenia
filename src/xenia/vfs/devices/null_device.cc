/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/devices/null_device.h"

#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/kernel/xfile.h"
#include "xenia/vfs/devices/null_entry.h"

namespace xe {
namespace vfs {

NullDevice::NullDevice(const std::string& mount_path,
                       const std::initializer_list<std::string>& null_paths)
    : Device(mount_path), null_paths_(null_paths), name_("NullDevice") {}

NullDevice::~NullDevice() = default;

bool NullDevice::Initialize() {
  auto root_entry = new NullEntry(this, nullptr, mount_path_);
  root_entry->attributes_ = kFileAttributeDirectory;
  root_entry_ = std::unique_ptr<Entry>(root_entry);

  for (auto path : null_paths_) {
    auto child = NullEntry::Create(this, root_entry, path);
    root_entry->children_.push_back(std::unique_ptr<Entry>(child));
  }
  return true;
}

void NullDevice::Dump(StringBuffer* string_buffer) {
  auto global_lock = global_critical_region_.Acquire();
  root_entry_->Dump(string_buffer, 0);
}

Entry* NullDevice::ResolvePath(const std::string_view path) {
  XELOGFS("NullDevice::ResolvePath({})", path);

  auto root = root_entry_.get();
  if (path.empty()) {
    return root_entry_.get();
  }

  for (auto& child : root->children()) {
    if (!strcasecmp(child->path().c_str(), path.data())) {
      return child.get();
    }
  }

  return nullptr;
}

}  // namespace vfs
}  // namespace xe
