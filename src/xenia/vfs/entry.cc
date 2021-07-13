/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/entry.h"

#include "xenia/base/filesystem.h"
#include "xenia/base/string.h"
#include "xenia/vfs/device.h"

namespace xe {
namespace vfs {

Entry::Entry(Device* device, Entry* parent, const std::string_view path)
    : device_(device),
      parent_(parent),
      path_(path),
      attributes_(0),
      size_(0),
      allocation_size_(0),
      create_timestamp_(0),
      access_timestamp_(0),
      write_timestamp_(0) {
  assert_not_null(device);
  absolute_path_ = xe::utf8::join_guest_paths(device->mount_path(), path);
  name_ = xe::utf8::find_name_from_guest_path(path);
}

Entry::~Entry() = default;

void Entry::Dump(xe::StringBuffer* string_buffer, int indent) {
  for (int i = 0; i < indent; ++i) {
    string_buffer->Append(' ');
  }
  string_buffer->Append(name());
  string_buffer->Append('\n');
  for (auto& child : children_) {
    child->Dump(string_buffer, indent + 2);
  }
}

void Entry::set_name(const std::string_view path) {
  absolute_path_ = xe::utf8::join_guest_paths(device_->mount_path(), path);
  path_ = xe::utf8::find_name_from_guest_path(path);
  name_ = xe::utf8::find_name_from_guest_path(path);

  SetHostFileName(name_);
}

bool Entry::is_read_only() const { return device_->is_read_only(); }

Entry* Entry::GetChild(const std::string_view name) {
  auto global_lock = global_critical_region_.Acquire();
  auto it = std::find_if(children_.cbegin(), children_.cend(),
                         [&](const auto& child) {
                           return xe::utf8::equal_case(child->name(), name);
                         });
  if (it == children_.cend()) {
    return nullptr;
  }
  return (*it).get();
}

Entry* Entry::ResolvePath(const std::string_view path) {
  // Walk the path, one separator at a time.
  Entry* entry = this;
  for (auto& part : xe::utf8::split_path(path)) {
    entry = entry->GetChild(part);
    if (!entry) {
      // Not found.
      return nullptr;
    }
  }
  return entry;
}

Entry* Entry::IterateChildren(const xe::filesystem::WildcardEngine& engine,
                              size_t* current_index) {
  auto global_lock = global_critical_region_.Acquire();
  while (*current_index < children_.size()) {
    auto& child = children_[*current_index];
    *current_index = *current_index + 1;
    if (engine.Match(child->name())) {
      return child.get();
    }
  }
  return nullptr;
}

Entry* Entry::CreateEntry(const std::string_view name, uint32_t attributes) {
  auto global_lock = global_critical_region_.Acquire();
  if (is_read_only()) {
    return nullptr;
  }
  if (GetChild(name)) {
    // Already exists.
    return nullptr;
  }
  auto entry = CreateEntryInternal(name, attributes);
  if (!entry) {
    return nullptr;
  }
  children_.push_back(std::move(entry));
  // TODO(benvanik): resort? would break iteration?
  Touch();
  return children_.back().get();
}

bool Entry::Delete(Entry* entry) {
  auto global_lock = global_critical_region_.Acquire();
  if (is_read_only()) {
    return false;
  }
  if (entry->parent() != this) {
    return false;
  }
  if (!DeleteEntryInternal(entry)) {
    return false;
  }
  for (auto it = children_.begin(); it != children_.end(); ++it) {
    if (it->get() == entry) {
      children_.erase(it);
      break;
    }
  }
  Touch();
  return true;
}

bool Entry::Delete() {
  assert_not_null(parent_);
  return parent_->Delete(this);
}

void Entry::Touch() {
  // TODO(benvanik): update timestamps.
}

}  // namespace vfs
}  // namespace xe
