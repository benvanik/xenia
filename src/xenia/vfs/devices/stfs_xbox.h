/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_VFS_DEVICES_STFS_XBOX_H_
#define XENIA_VFS_DEVICES_STFS_XBOX_H_

#include "xenia/base/string_util.h"
#include "xenia/kernel/locale_info.h"
#include "xenia/kernel/util/xex2_info.h"

namespace xe {
namespace vfs {

// Structs used for interchange between Xenia and actual Xbox360 kernel/XAM

inline uint32_t load_uint24_be(const uint8_t* p) {
  return (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | uint32_t(p[2]);
}
inline uint32_t load_uint24_le(const uint8_t* p) {
  return (uint32_t(p[2]) << 16) | (uint32_t(p[1]) << 8) | uint32_t(p[0]);
}
inline void store_uint24_le(uint8_t* p, uint32_t value) {
  p[2] = uint8_t((value >> 16) & 0xFF);
  p[1] = uint8_t((value >> 8) & 0xFF);
  p[0] = uint8_t(value & 0xFF);
}

enum class XContentPackageType : uint32_t {
  kCon = 0x434F4E20,
  kPirs = 0x50495253,
  kLive = 0x4C495645,
};

enum class XContentVolumeType : uint32_t {
  kStfs = 0,
  kSvod = 1,
};

/* STFS structures */
#pragma pack(push, 1)
struct StfsVolumeDescriptor {
  uint8_t descriptor_length;
  uint8_t version;
  union {
    struct {
      uint8_t read_only_format : 1;  // if set, only uses a single backing-block
                                     // per hash table (no resiliency),
                                     // otherwise uses two
      uint8_t root_active_index : 1;  // if set, uses secondary backing-block
                                      // for the highest-level hash table

      uint8_t directory_overallocated : 1;
      uint8_t directory_index_bounds_valid : 1;
    } bits;
    uint8_t as_byte;
  } flags;
  uint16_t file_table_block_count;
  uint8_t file_table_block_number_raw[3];
  uint8_t top_hash_table_hash[0x14];
  be<uint32_t> total_block_count;
  be<uint32_t> free_block_count;

  uint32_t file_table_block_number() const {
    return load_uint24_le(file_table_block_number_raw);
  }

  void set_file_table_block_number(uint32_t value) {
    store_uint24_le(file_table_block_number_raw, value);
  }

  bool is_valid() const {
    return descriptor_length == sizeof(StfsVolumeDescriptor);
  }
};
static_assert_size(StfsVolumeDescriptor, 0x24);
#pragma pack(pop)

enum class StfsHashState : uint8_t {
  kFree = 0,   // unallocated but doesn't exist in package (needs to expand)?
  kFree2 = 1,  // unallocated but exists in package?
  kInUse = 2,
};

struct StfsHashEntry {
  uint8_t sha1[0x14];

  xe::be<uint32_t> info_raw;

  uint32_t level0_next_block() const { return info_raw & 0xFFFFFF; }
  void set_level0_next_block(uint32_t value) {
    info_raw = (info_raw & ~0xFFFFFF) | (value & 0xFFFFFF);
  }

  StfsHashState level0_allocation_state() const {
    return StfsHashState(uint8_t(((info_raw & 0xC0000000) >> 30) & 0xFF));
  }
  void set_level0_allocation_state(StfsHashState value) {
    info_raw = (info_raw & ~0xC0000000) | (uint32_t(value) << 30);
  }

  uint32_t levelN_num_blocks_free() const { return info_raw & 0x7FFF; }
  void set_levelN_num_blocks_free(uint32_t value) {
    info_raw = (info_raw & ~0x7FFF) | (value & 0x7FFF);
  }

  uint32_t levelN_num_blocks_unk() const {
    return ((info_raw & 0x3FFF8000) >> 15) & 0x7FFF;
  }
  void set_levelN_num_blocks_unk(uint32_t value) {
    info_raw = (info_raw & ~0x3FFF8000) | ((value & 0x7FFF) << 15);
  }

  bool levelN_active_index() const { return (info_raw & 0x40000000) != 0; }
  void set_levelN_active_index(bool value) {
    info_raw = (info_raw & ~0x40000000) | (value ? 0x40000000 : 0);
  }

  bool levelN_writeable() const { return (info_raw & 0x80000000) != 0; }
  void set_levelN_writeable(bool value) {
    info_raw = (info_raw & ~0x80000000) | (value ? 0x80000000 : 0);
  }
};
static_assert_size(StfsHashEntry, 0x18);

struct StfsHashTable {
  StfsHashEntry entries[170];
  xe::be<uint32_t> num_blocks;  // num L0 blocks covered by this table?
  uint8_t padding[12];
};
static_assert_size(StfsHashTable, 0x1000);

struct StfsDirectoryEntry {
  char name[40];

  struct {
    uint8_t name_length : 6;
    uint8_t contiguous : 1;
    uint8_t directory : 1;
  } flags;

  uint8_t valid_data_blocks_raw[3];
  uint8_t allocated_data_blocks_raw[3];
  uint8_t start_block_number_raw[3];

  be<uint16_t> directory_index;

  be<uint32_t> length;

  be<uint16_t> create_date;
  be<uint16_t> create_time;
  be<uint16_t> modified_date;
  be<uint16_t> modified_time;

  uint32_t valid_data_blocks() const {
    return load_uint24_le(valid_data_blocks_raw);
  }

  void set_valid_data_blocks(uint32_t value) {
    store_uint24_le(valid_data_blocks_raw, value);
  }

  uint32_t allocated_data_blocks() const {
    return load_uint24_le(allocated_data_blocks_raw);
  }

  void set_allocated_data_blocks(uint32_t value) {
    store_uint24_le(allocated_data_blocks_raw, value);
  }

  uint32_t start_block_number() const {
    return load_uint24_le(start_block_number_raw);
  }

  void set_start_block_number(uint32_t value) {
    store_uint24_le(start_block_number_raw, value);
  }
};
static_assert_size(StfsDirectoryEntry, 0x40);

struct StfsDirectoryBlock {
  StfsDirectoryEntry entries[0x40];
};
static_assert_size(StfsDirectoryBlock, 0x1000);

/* SVOD structures */
struct SvodDeviceDescriptor {
  uint8_t descriptor_length;
  uint8_t block_cache_element_count;
  uint8_t worker_thread_processor;
  uint8_t worker_thread_priority;
  uint8_t first_fragment_hash_entry[0x14];
  union {
    struct {
      uint8_t must_be_zero_for_future_usage : 6;
      uint8_t enhanced_gdf_layout : 1;
      uint8_t zero_for_downlevel_clients : 1;
    } bits;
    uint8_t as_byte;
  } features;
  uint8_t num_data_blocks_raw[3];
  uint8_t start_data_block_raw[3];
  uint8_t reserved[5];

  uint32_t num_data_blocks() { return load_uint24_le(num_data_blocks_raw); }

  uint32_t start_data_block() { return load_uint24_le(start_data_block_raw); }
};
static_assert_size(SvodDeviceDescriptor, 0x24);

/* XContent structures */
struct XContentLicense {
  be<uint64_t> licensee_id;
  be<uint32_t> license_bits;
  be<uint32_t> license_flags;
};
static_assert_size(XContentLicense, 0x10);

struct XContentMediaData {
  uint8_t series_id[0x10];
  uint8_t season_id[0x10];
  be<uint16_t> season_number;
  be<uint16_t> episode_number;
};
static_assert_size(XContentMediaData, 0x24);

struct XContentAvatarAssetData {
  be<uint32_t> sub_category;
  be<uint32_t> colorizable;
  uint8_t asset_id[0x10];
  uint8_t skeleton_version_mask;
  uint8_t reserved[0xB];
};
static_assert_size(XContentAvatarAssetData, 0x24);

struct XContentAttributes {
  uint8_t profile_transfer : 1;
  uint8_t device_transfer : 1;
  uint8_t move_only_transfer : 1;
  uint8_t kinect_enabled : 1;
  uint8_t disable_network_storage : 1;
  uint8_t deep_link_supported : 1;
  uint8_t reserved : 2;
};
static_assert_size(XContentAttributes, 1);

#pragma pack(push, 1)
struct XContentMetadata {
  static const uint32_t kThumbLengthV1 = 0x4000;
  static const uint32_t kThumbLengthV2 = 0x3D00;

  static const uint32_t kNumLanguagesV1 = 9;
  // metadata_version 2 adds 3 languages inside thumbnail/title_thumbnail space
  static const uint32_t kNumLanguagesV2 = 12;

  be<XContentType> content_type;
  be<uint32_t> metadata_version;
  be<uint64_t> content_size;
  xex2_opt_execution_info execution_info;
  uint8_t console_id[5];
  be<uint64_t> profile_id;
  union {
    StfsVolumeDescriptor stfs;
    SvodDeviceDescriptor svod;
  } volume_descriptor;
  be<uint32_t> data_file_count;
  be<uint64_t> data_file_size;
  be<XContentVolumeType> volume_type;
  be<uint64_t> online_creator;
  be<uint32_t> category;
  uint8_t reserved2[0x20];
  union {
    XContentMediaData media_data;
    XContentAvatarAssetData avatar_asset_data;
  } metadata_v2;
  uint8_t device_id[0x14];
  union {
    be<uint16_t> uint[kNumLanguagesV1][128];
    char16_t chars[kNumLanguagesV1][128];
  } display_name_raw;
  union {
    be<uint16_t> uint[kNumLanguagesV1][128];
    char16_t chars[kNumLanguagesV1][128];
  } description_raw;
  union {
    be<uint16_t> uint[64];
    char16_t chars[64];
  } publisher_raw;
  union {
    be<uint16_t> uint[64];
    char16_t chars[64];
  } title_name_raw;
  union {
    XContentAttributes bits;
    uint8_t as_byte;
  } flags;
  be<uint32_t> thumbnail_size;
  be<uint32_t> title_thumbnail_size;
  uint8_t thumbnail[kThumbLengthV2];
  union {
    be<uint16_t> uint[kNumLanguagesV2 - kNumLanguagesV1][128];
    char16_t chars[kNumLanguagesV2 - kNumLanguagesV1][128];
  } display_name_ex_raw;
  uint8_t title_thumbnail[kThumbLengthV2];
  union {
    be<uint16_t> uint[kNumLanguagesV2 - kNumLanguagesV1][128];
    char16_t chars[kNumLanguagesV2 - kNumLanguagesV1][128];
  } description_ex_raw;

  using Language = kernel::locale_info::Language;

  std::u16string display_name(Language language) const {
    uint32_t lang_id = static_cast<uint32_t>(language) - 1;

    if (lang_id >= kNumLanguagesV2) {
      assert_always();
      // no room for this lang, read from english slot..
      lang_id = static_cast<uint32_t>(Language::kEnglish) - 1;
    }

    const be<uint16_t>* str = 0;
    if (lang_id >= 0 && lang_id < kNumLanguagesV1) {
      str = display_name_raw.uint[lang_id];
    } else if (lang_id >= kNumLanguagesV1 && lang_id < kNumLanguagesV2 &&
               metadata_version >= 2) {
      str = display_name_ex_raw.uint[lang_id - kNumLanguagesV1];
    }

    if (!str) {
      // Invalid language ID?
      assert_always();
      return u"";
    }

    return load_and_swap<std::u16string>(str);
  }

  std::u16string description(Language language) const {
    uint32_t lang_id = uint32_t(language) - 1;

    if (lang_id >= kNumLanguagesV2) {
      assert_always();
      // no room for this lang, read from english slot..
      lang_id = uint32_t(Language::kEnglish) - 1;
    }

    const be<uint16_t>* str = 0;
    if (lang_id >= 0 && lang_id < kNumLanguagesV1) {
      str = description_raw.uint[lang_id];
    } else if (lang_id >= kNumLanguagesV1 && lang_id < kNumLanguagesV2 &&
               metadata_version >= 2) {
      str = description_ex_raw.uint[lang_id - kNumLanguagesV1];
    }

    if (!str) {
      // Invalid language ID?
      assert_always();
      return u"";
    }

    return load_and_swap<std::u16string>(str);
  }

  std::u16string publisher() const {
    return load_and_swap<std::u16string>(publisher_raw.uint);
  }

  std::u16string title_name() const {
    return load_and_swap<std::u16string>(title_name_raw.uint);
  }

  bool set_display_name(Language language, const std::u16string_view value) {
    uint32_t lang_id = static_cast<uint32_t>(language) - 1;

    if (lang_id >= kNumLanguagesV2) {
      assert_always();
      // no room for this lang, store in english slot..
      lang_id = static_cast<uint32_t>(Language::kEnglish) - 1;
    }

    char16_t* str = 0;
    if (lang_id >= 0 && lang_id < kNumLanguagesV1) {
      str = display_name_raw.chars[lang_id];
    } else if (lang_id >= kNumLanguagesV1 && lang_id < kNumLanguagesV2 &&
               metadata_version >= 2) {
      str = display_name_ex_raw.chars[lang_id - kNumLanguagesV1];
    }

    if (!str) {
      // Invalid language ID?
      assert_always();
      return false;
    }

    string_util::copy_and_swap_truncating(str, value,
                                          countof(display_name_raw.chars[0]));
    return true;
  }

  bool set_description(Language language, const std::u16string_view value) {
    uint32_t lang_id = uint32_t(language) - 1;

    if (lang_id >= kNumLanguagesV2) {
      assert_always();
      // no room for this lang, store in english slot..
      lang_id = static_cast<uint32_t>(Language::kEnglish) - 1;
    }

    char16_t* str = 0;
    if (lang_id >= 0 && lang_id < kNumLanguagesV1) {
      str = description_raw.chars[lang_id];
    } else if (lang_id >= kNumLanguagesV1 && lang_id < kNumLanguagesV2 &&
               metadata_version >= 2) {
      str = description_ex_raw.chars[lang_id - kNumLanguagesV1];
    }

    if (!str) {
      // Invalid language ID?
      assert_always();
      return false;
    }

    string_util::copy_and_swap_truncating(str, value,
                                          countof(description_raw.chars[0]));
    return true;
  }

  void set_publisher(const std::u16string_view value) {
    string_util::copy_and_swap_truncating(publisher_raw.chars, value,
                                          countof(publisher_raw.chars));
  }

  void set_title_name(const std::u16string_view value) {
    string_util::copy_and_swap_truncating(title_name_raw.chars, value,
                                          countof(title_name_raw.chars));
  }
};
static_assert_size(XContentMetadata, 0x93D6);

struct XContentHeader {
  be<XContentPackageType> magic;
  uint8_t signature[0x228];
  XContentLicense licenses[0x10];
  uint8_t content_id[0x14];
  be<uint32_t> header_size;

  bool is_magic_valid() const {
    return magic == XContentPackageType::kCon ||
           magic == XContentPackageType::kLive ||
           magic == XContentPackageType::kPirs;
  }
};
static_assert_size(XContentHeader, 0x344);
#pragma pack(pop)

struct StfsHeader {
  XContentHeader header;
  XContentMetadata metadata;
  // TODO: title/system updates contain more data after XContentMetadata, seems
  // to affect header.header_size
};
static_assert_size(StfsHeader, 0x971A);

}  // namespace vfs
}  // namespace xe

#endif  // XENIA_VFS_DEVICES_STFS_XBOX_H_
