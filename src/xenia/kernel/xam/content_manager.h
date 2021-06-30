/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_CONTENT_MANAGER_H_
#define XENIA_KERNEL_XAM_CONTENT_MANAGER_H_

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "xenia/base/memory.h"
#include "xenia/base/mutex.h"
#include "xenia/base/string_key.h"
#include "xenia/base/string_util.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
class KernelState;
}  // namespace kernel
}  // namespace xe

namespace xe {
namespace kernel {
namespace xam {

// If set in XCONTENT_AGGREGATE_DATA, will be substituted with the running
// titles ID
// TODO: check if actual x360 kernel/xam has a value similar to this
constexpr uint32_t kCurrentlyRunningTitleId = 0xFFFFFFFF;

struct XCONTENT_DATA {
  be<uint32_t> device_id;
  be<XContentType> content_type;
  union {
    // this should be be<uint16_t>, but that stops copy constructor being
    // generated...
    uint16_t uint[128];
    char16_t chars[128];
  } display_name_raw;

  char file_name_raw[42];

  // Some games use this padding field as a null-terminator, as eg.
  // DLC packages usually fill the entire file_name_raw array
  // Not every game sets it to 0 though, so make sure any file_name_raw reads
  // only go up to 42 chars!
  uint8_t padding[2];

  bool operator==(const XCONTENT_DATA& other) const {
    // Package is located via device_id/content_type/file_name, so only need to
    // compare those
    return device_id == other.device_id && content_type == other.content_type &&
           file_name() == other.file_name();
  }

  std::u16string display_name() const {
    return load_and_swap<std::u16string>(display_name_raw.uint);
  }

  std::string file_name() const {
    std::string value;
    value.assign(file_name_raw,
                 std::min(strlen(file_name_raw), countof(file_name_raw)));
    return value;
  }

  void set_display_name(const std::u16string_view value) {
    // Some games (eg Goldeneye XBLA) require multiple null-terminators for it
    // to read the string properly, blanking the array should take care of that

    std::fill_n(display_name_raw.chars, countof(display_name_raw.chars), 0);
    string_util::copy_and_swap_truncating(display_name_raw.chars, value,
                                          countof(display_name_raw.chars));
  }

  void set_file_name(const std::string_view value) {
    std::fill_n(file_name_raw, countof(file_name_raw), 0);
    string_util::copy_maybe_truncating<string_util::Safety::IKnowWhatIAmDoing>(
        file_name_raw, value, xe::countof(file_name_raw));

    // Some games rely on padding field acting as a null-terminator...
    padding[0] = padding[1] = 0;
  }
};
static_assert_size(XCONTENT_DATA, 0x134);

struct XCONTENT_AGGREGATE_DATA : XCONTENT_DATA {
  be<uint64_t> unk134;  // some titles store XUID here?
  be<uint32_t> title_id;

  XCONTENT_AGGREGATE_DATA() = default;
  XCONTENT_AGGREGATE_DATA(const XCONTENT_DATA& other) {
    device_id = other.device_id;
    content_type = other.content_type;
    set_display_name(other.display_name());
    set_file_name(other.file_name());
    padding[0] = padding[1] = 0;
    unk134 = 0;
    title_id = kCurrentlyRunningTitleId;
  }

  bool operator==(const XCONTENT_AGGREGATE_DATA& other) const {
    // Package is located via device_id/title_id/content_type/file_name, so only
    // need to compare those
    return device_id == other.device_id && title_id == other.title_id &&
           content_type == other.content_type &&
           file_name() == other.file_name();
  }
};
static_assert_size(XCONTENT_AGGREGATE_DATA, 0x148);

class ContentPackage {
 public:
  ContentPackage(KernelState* kernel_state, const std::string_view root_name,
                 const XCONTENT_AGGREGATE_DATA& data,
                 const std::filesystem::path& package_path);
  ~ContentPackage();

  const XCONTENT_AGGREGATE_DATA& GetPackageContentData() const {
    return content_data_;
  }

 private:
  KernelState* kernel_state_;
  std::string root_name_;
  std::string device_path_;
  XCONTENT_AGGREGATE_DATA content_data_;
};

class ContentManager {
 public:
  ContentManager(KernelState* kernel_state,
                 const std::filesystem::path& root_path);
  ~ContentManager();

  std::vector<XCONTENT_AGGREGATE_DATA> ListContent(uint32_t device_id,
                                                   XContentType content_type,
                                                   uint32_t title_id = -1);

  std::unique_ptr<ContentPackage> ResolvePackage(
      const std::string_view root_name, const XCONTENT_AGGREGATE_DATA& data);

  bool ContentExists(const XCONTENT_AGGREGATE_DATA& data);
  X_RESULT WriteContentHeaderFile(const std::string file_name,
                                  const XCONTENT_DATA* data_raw);
  X_RESULT ReadContentHeaderFile(const std::string_view file_name,
                                 XContentType content_type,
                                 XCONTENT_DATA& data);
  X_RESULT CreateContent(const std::string_view root_name,
                         const XCONTENT_AGGREGATE_DATA& data);
  X_RESULT OpenContent(const std::string_view root_name,
                       const XCONTENT_AGGREGATE_DATA& data);
  X_RESULT CloseContent(const std::string_view root_name);
  X_RESULT GetContentThumbnail(const XCONTENT_AGGREGATE_DATA& data,
                               std::vector<uint8_t>* buffer);
  X_RESULT SetContentThumbnail(const XCONTENT_AGGREGATE_DATA& data,
                               std::vector<uint8_t> buffer);
  X_RESULT DeleteContent(const XCONTENT_AGGREGATE_DATA& data);
  std::filesystem::path ResolveGameUserContentPath();
  bool IsContentOpen(const XCONTENT_AGGREGATE_DATA& data) const;
  void CloseOpenedFilesFromContent(const std::string_view root_name);

 private:
  std::filesystem::path ResolvePackageRoot(XContentType content_type,
                                           uint32_t title_id = -1);
  std::filesystem::path ResolvePackagePath(const XCONTENT_AGGREGATE_DATA& data);

  KernelState* kernel_state_;
  std::filesystem::path root_path_;

  // TODO(benvanik): remove use of global lock, it's bad here!
  xe::global_critical_region global_critical_region_;
  std::unordered_map<string_key, ContentPackage*> open_packages_;
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_CONTENT_MANAGER_H_
