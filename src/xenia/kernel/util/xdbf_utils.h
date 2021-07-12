/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_UTIL_XDBF_UTILS_H_
#define XENIA_KERNEL_UTIL_XDBF_UTILS_H_

#include <string>
#include <vector>

#include "xenia/base/memory.h"
#include "xenia/kernel/locale_info.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace util {

// https://github.com/oukiar/freestyledash/blob/master/Freestyle/Tools/XEX/SPA.h
// https://github.com/oukiar/freestyledash/blob/master/Freestyle/Tools/XEX/SPA.cpp

enum class XdbfSection : uint16_t {
  kMetadata = 0x0001,
  kImage = 0x0002,
  kStringTable = 0x0003,
};

struct XdbfBlock {
  const uint8_t* buffer;
  size_t size;

  operator bool() const { return buffer != nullptr; }
};

// Wraps an XBDF (XboxDataBaseFormat) in-memory database.
// https://free60project.github.io/wiki/XDBF.html
class XdbfWrapper {
 public:
  XdbfWrapper(const uint8_t* data, size_t data_size);

  // True if the target memory contains a valid XDBF instance.
  bool is_valid() const { return data_ != nullptr; }

  // Gets an entry in the given section.
  // If the entry is not found the returned block will be nullptr.
  XdbfBlock GetEntry(XdbfSection section, uint64_t id) const;

  // Gets a string from the string table in the given language.
  // Returns the empty string if the entry is not found.
  std::string GetStringTableEntry(locale_info::Language language,
                                  uint16_t string_id) const;

 protected:
#pragma pack(push, 1)
  struct XbdfHeader {
    xe::be<uint32_t> magic;
    xe::be<uint32_t> version;
    xe::be<uint32_t> entry_count;
    xe::be<uint32_t> entry_used;
    xe::be<uint32_t> free_count;
    xe::be<uint32_t> free_used;
  };
  static_assert_size(XbdfHeader, 24);

  struct XbdfEntry {
    xe::be<uint16_t> section;
    xe::be<uint64_t> id;
    xe::be<uint32_t> offset;
    xe::be<uint32_t> size;
  };
  static_assert_size(XbdfEntry, 18);

  struct XbdfFileLoc {
    xe::be<uint32_t> offset;
    xe::be<uint32_t> size;
  };
  static_assert_size(XbdfFileLoc, 8);

  struct XdbfXstc {
    xe::be<uint32_t> magic;
    xe::be<uint32_t> version;
    xe::be<uint32_t> size;
    xe::be<uint32_t> default_language;
  };
  static_assert_size(XdbfXstc, 16);

  struct XdbfXstrHeader {
    xe::be<uint32_t> magic;
    xe::be<uint32_t> version;
    xe::be<uint32_t> size;
    xe::be<uint16_t> string_count;
  };
  static_assert_size(XdbfXstrHeader, 14);

  struct XdbfStringTableEntry {
    xe::be<uint16_t> id;
    xe::be<uint16_t> string_length;
  };
  static_assert_size(XdbfStringTableEntry, 4);
#pragma pack(pop)

 private:
  const uint8_t* data_ = nullptr;
  size_t data_size_ = 0;
  const uint8_t* content_offset_ = nullptr;

  const XbdfHeader* header_ = nullptr;
  const XbdfEntry* entries_ = nullptr;
  const XbdfFileLoc* files_ = nullptr;
};

class XdbfGameData : public XdbfWrapper {
 public:
  XdbfGameData(const uint8_t* data, size_t data_size)
      : XdbfWrapper(data, data_size) {}

  // The game icon image, if found.
  XdbfBlock icon() const;

  // The game's default language.
  locale_info::Language default_language() const;

  // The game's title in its default language.
  std::string title() const;

  std::string title(locale_info::Language language) const;
};

}  // namespace util
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_UTIL_XDBF_UTILS_H_
