/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/content_manager.h"

#include <array>
#include <string>

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/string.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xfile.h"
#include "xenia/kernel/xobject.h"
#include "xenia/vfs/devices/host_path_device.h"

namespace xe {
namespace kernel {
namespace xam {

static const char* kThumbnailFileName = "__thumbnail.png";

static const char* kGameUserContentDirName = "profile";
static const char* kGameContentHeaderDirName = "Headers";

static int content_device_id_ = 0;

ContentPackage::ContentPackage(KernelState* kernel_state,
                               const std::string_view root_name,
                               const XCONTENT_AGGREGATE_DATA& data,
                               const std::filesystem::path& package_path)
    : kernel_state_(kernel_state), root_name_(root_name) {
  device_path_ = fmt::format("\\Device\\Content\\{0}\\", ++content_device_id_);
  content_data_ = data;

  auto fs = kernel_state_->file_system();
  auto device =
      std::make_unique<vfs::HostPathDevice>(device_path_, package_path, false);
  device->Initialize();
  fs->RegisterDevice(std::move(device));
  fs->RegisterSymbolicLink(root_name_ + ":", device_path_);
}

ContentPackage::~ContentPackage() {
  auto fs = kernel_state_->file_system();
  fs->UnregisterSymbolicLink(root_name_ + ":");
  fs->UnregisterDevice(device_path_);
}

ContentManager::ContentManager(KernelState* kernel_state,
                               const std::filesystem::path& root_path)
    : kernel_state_(kernel_state), root_path_(root_path) {}

ContentManager::~ContentManager() = default;

std::filesystem::path ContentManager::ResolvePackageRoot(
    XContentType content_type, uint32_t title_id) {
  if (title_id == kCurrentlyRunningTitleId) {
    title_id = kernel_state_->title_id();
  }
  auto title_id_str = fmt::format("{:08X}", title_id);
  auto content_type_str = fmt::format("{:08X}", uint32_t(content_type));

  // Package root path:
  // content_root/title_id/content_type/
  return root_path_ / title_id_str / content_type_str;
}

std::filesystem::path ContentManager::ResolvePackagePath(
    const XCONTENT_AGGREGATE_DATA& data) {
  // Content path:
  // content_root/title_id/content_type/data_file_name/
  auto package_root = ResolvePackageRoot(data.content_type, data.title_id);
  return package_root / xe::to_path(data.file_name());
}

std::vector<XCONTENT_AGGREGATE_DATA> ContentManager::ListContent(
    uint32_t device_id, XContentType content_type, uint32_t title_id) {
  std::vector<XCONTENT_AGGREGATE_DATA> result;

  if (title_id == kCurrentlyRunningTitleId) {
    title_id = kernel_state_->title_id();
  }

  // Search path:
  // content_root/title_id/type_name/*
  auto package_root = ResolvePackageRoot(content_type, title_id);
  auto file_infos = xe::filesystem::ListFiles(package_root);
  for (const auto& file_info : file_infos) {
    if (file_info.type != xe::filesystem::FileInfo::Type::kDirectory) {
      // Directories only.
      continue;
    }

    XCONTENT_AGGREGATE_DATA content_data;
    if (XSUCCEEDED(
            ReadContentHeaderFile(xe::path_to_utf8(file_info.name) + ".header",
                                  content_type, content_data))) {
      result.emplace_back(std::move(content_data));
    } else {
      content_data.device_id = device_id;
      content_data.content_type = content_type;
      content_data.set_display_name(xe::path_to_utf16(file_info.name));
      content_data.set_file_name(xe::path_to_utf8(file_info.name));
      content_data.title_id = title_id;
      result.emplace_back(std::move(content_data));
    }
  }
  return result;
}

std::unique_ptr<ContentPackage> ContentManager::ResolvePackage(
    const std::string_view root_name, const XCONTENT_AGGREGATE_DATA& data) {
  auto package_path = ResolvePackagePath(data);
  if (!std::filesystem::exists(package_path)) {
    return nullptr;
  }

  auto global_lock = global_critical_region_.Acquire();

  auto package = std::make_unique<ContentPackage>(kernel_state_, root_name,
                                                  data, package_path);
  return package;
}

bool ContentManager::ContentExists(const XCONTENT_AGGREGATE_DATA& data) {
  auto path = ResolvePackagePath(data);
  return std::filesystem::exists(path);
}

X_RESULT ContentManager::WriteContentHeaderFile(const std::string file_name,
                                                const XCONTENT_DATA* data) {
  auto title_id = fmt::format("{:8X}", kernel_state_->title_id());
  auto content_type =
      fmt::format("{:08X}", load_and_swap<uint32_t>(&data->content_type));
  auto header_path =
      root_path_ / title_id / kGameContentHeaderDirName / content_type;

  if (!std::filesystem::exists(header_path)) {
    if (!std::filesystem::create_directories(header_path)) {
      return X_STATUS_ACCESS_DENIED;
    }
  }
  auto header_filename = file_name + ".header";

  xe::filesystem::CreateFile(header_path / header_filename);

  if (std::filesystem::exists(header_path / header_filename)) {
    auto file = xe::filesystem::OpenFile(header_path / header_filename, "wb");
    fwrite(data, 1, sizeof(XCONTENT_DATA), file);
    fclose(file);
    return X_STATUS_SUCCESS;
  }
  return X_STATUS_NO_SUCH_FILE;
}

X_RESULT ContentManager::ReadContentHeaderFile(const std::string_view file_name,
                                               XContentType content_type,
                                               XCONTENT_DATA& data) {
  auto title_id = fmt::format("{:8X}", kernel_state_->title_id());
  auto content_type_directory = fmt::format("{:08X}", content_type);
  auto header_file_path = root_path_ / title_id / kGameContentHeaderDirName /
                          content_type_directory / file_name;
  constexpr uint32_t header_size = sizeof(XCONTENT_DATA);

  if (std::filesystem::exists(header_file_path)) {
    auto file = xe::filesystem::OpenFile(header_file_path, "rb");

    std::array<uint8_t, header_size> buffer;

    auto fize_size = std::filesystem::file_size(header_file_path);
    if (fize_size != header_size) {
      fclose(file);
      return X_STATUS_END_OF_FILE;
    }

    size_t result = fread(buffer.data(), 1, header_size, file);
    if (result != header_size) {
      fclose(file);
      return X_STATUS_END_OF_FILE;
    }
    fclose(file);
    std::memcpy(&data, buffer.data(), buffer.size());
    return X_STATUS_SUCCESS;
  }
  return X_STATUS_NO_SUCH_FILE;
}

X_RESULT ContentManager::CreateContent(const std::string_view root_name,
                                       const XCONTENT_AGGREGATE_DATA& data) {
  auto global_lock = global_critical_region_.Acquire();

  if (open_packages_.count(string_key(root_name))) {
    // Already content open with this root name.
    return X_ERROR_ALREADY_EXISTS;
  }

  auto package_path = ResolvePackagePath(data);
  if (std::filesystem::exists(package_path)) {
    // Exists, must not!
    return X_ERROR_ALREADY_EXISTS;
  }

  if (!std::filesystem::create_directories(package_path)) {
    return X_ERROR_ACCESS_DENIED;
  }

  auto package = ResolvePackage(root_name, data);
  assert_not_null(package);

  open_packages_.insert({string_key::create(root_name), package.release()});

  return X_ERROR_SUCCESS;
}

X_RESULT ContentManager::OpenContent(const std::string_view root_name,
                                     const XCONTENT_AGGREGATE_DATA& data) {
  auto global_lock = global_critical_region_.Acquire();

  if (open_packages_.count(string_key(root_name))) {
    // Already content open with this root name.
    return X_ERROR_ALREADY_EXISTS;
  }

  auto package_path = ResolvePackagePath(data);
  if (!std::filesystem::exists(package_path)) {
    // Does not exist, must be created.
    return X_ERROR_FILE_NOT_FOUND;
  }

  // Open package.
  auto package = ResolvePackage(root_name, data);
  assert_not_null(package);

  open_packages_.insert({string_key::create(root_name), package.release()});

  return X_ERROR_SUCCESS;
}

X_RESULT ContentManager::CloseContent(const std::string_view root_name) {
  auto global_lock = global_critical_region_.Acquire();

  auto it = open_packages_.find(string_key(root_name));
  if (it == open_packages_.end()) {
    return X_ERROR_FILE_NOT_FOUND;
  }
  CloseOpenedFilesFromContent(root_name);

  auto package = it->second;
  open_packages_.erase(it);
  delete package;

  return X_ERROR_SUCCESS;
}

X_RESULT ContentManager::GetContentThumbnail(
    const XCONTENT_AGGREGATE_DATA& data, std::vector<uint8_t>* buffer) {
  auto global_lock = global_critical_region_.Acquire();
  auto package_path = ResolvePackagePath(data);
  auto thumb_path = package_path / kThumbnailFileName;
  if (std::filesystem::exists(thumb_path)) {
    auto file = xe::filesystem::OpenFile(thumb_path, "rb");
    fseek(file, 0, SEEK_END);
    size_t file_len = ftell(file);
    fseek(file, 0, SEEK_SET);
    buffer->resize(file_len);
    fread(const_cast<uint8_t*>(buffer->data()), 1, buffer->size(), file);
    fclose(file);
    return X_ERROR_SUCCESS;
  } else {
    return X_ERROR_FILE_NOT_FOUND;
  }
}

X_RESULT ContentManager::SetContentThumbnail(
    const XCONTENT_AGGREGATE_DATA& data, std::vector<uint8_t> buffer) {
  auto global_lock = global_critical_region_.Acquire();
  auto package_path = ResolvePackagePath(data);
  std::filesystem::create_directories(package_path);
  if (std::filesystem::exists(package_path)) {
    auto thumb_path = package_path / kThumbnailFileName;
    auto file = xe::filesystem::OpenFile(thumb_path, "wb");
    fwrite(buffer.data(), 1, buffer.size(), file);
    fclose(file);
    return X_ERROR_SUCCESS;
  } else {
    return X_ERROR_FILE_NOT_FOUND;
  }
}

X_RESULT ContentManager::DeleteContent(const XCONTENT_AGGREGATE_DATA& data) {
  auto global_lock = global_critical_region_.Acquire();

  if (IsContentOpen(data)) {
    // TODO(Gliniak): Get real error code for this case.
    return X_ERROR_ACCESS_DENIED;
  }

  auto package_path = ResolvePackagePath(data);
  if (std::filesystem::remove_all(package_path) > 0) {
    return X_ERROR_SUCCESS;
  } else {
    return X_ERROR_FILE_NOT_FOUND;
  }
}

std::filesystem::path ContentManager::ResolveGameUserContentPath() {
  auto title_id = fmt::format("{:8X}", kernel_state_->title_id());
  auto user_name = xe::to_path(kernel_state_->user_profile()->name());

  // Per-game per-profile data location:
  // content_root/title_id/profile/user_name
  return root_path_ / title_id / kGameUserContentDirName / user_name;
}

bool ContentManager::IsContentOpen(const XCONTENT_AGGREGATE_DATA& data) const {
  return std::any_of(open_packages_.cbegin(), open_packages_.cend(),
                     [data](std::pair<string_key, ContentPackage*> content) {
                       return data == content.second->GetPackageContentData();
                     });
}

void ContentManager::CloseOpenedFilesFromContent(
    const std::string_view root_name) {
  // TODO(Gliniak): Cleanup this code to care only about handles
  // related to provided content
  const std::vector<object_ref<XFile>> all_files_handles =
      kernel_state_->object_table()->GetObjectsByType<XFile>(
          XObject::Type::File);

  std::string resolved_path = "";
  kernel_state_->file_system()->FindSymbolicLink(std::string(root_name) + ':',
                                                 resolved_path);

  for (const object_ref<XFile>& file : all_files_handles) {
    std::string file_path = file->entry()->absolute_path();
    bool is_file_inside_content = utf8::starts_with(file_path, resolved_path);

    if (is_file_inside_content) {
      file->ReleaseHandle();
    }
  }
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe
