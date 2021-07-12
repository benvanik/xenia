/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/util/xdbf_utils.h"

namespace xe {
namespace kernel {
namespace util {

constexpr fourcc_t kXdbfSignatureXdbf = make_fourcc("XDBF");
constexpr fourcc_t kXdbfSignatureXstc = make_fourcc("XSTC");
constexpr fourcc_t kXdbfSignatureXstr = make_fourcc("XSTR");

using Language = locale_info::Language;

XdbfWrapper::XdbfWrapper(const uint8_t* data, size_t data_size)
    : data_(data), data_size_(data_size) {
  if (!data || data_size <= sizeof(XbdfHeader)) {
    data_ = nullptr;
    return;
  }

  const uint8_t* ptr = data_;

  header_ = reinterpret_cast<const XbdfHeader*>(ptr);
  ptr += sizeof(XbdfHeader);
  if (header_->magic != kXdbfSignatureXdbf) {
    data_ = nullptr;
    return;
  }

  entries_ = reinterpret_cast<const XbdfEntry*>(ptr);
  ptr += sizeof(XbdfEntry) * header_->entry_count;

  files_ = reinterpret_cast<const XbdfFileLoc*>(ptr);
  ptr += sizeof(XbdfFileLoc) * header_->free_count;

  content_offset_ = ptr;
}

XdbfBlock XdbfWrapper::GetEntry(XdbfSection section, uint64_t id) const {
  for (uint32_t i = 0; i < header_->entry_used; ++i) {
    auto& entry = entries_[i];
    if (entry.section == static_cast<uint16_t>(section) && entry.id == id) {
      XdbfBlock block;
      block.buffer = content_offset_ + entry.offset;
      block.size = entry.size;
      return block;
    }
  }
  return {0};
}

std::string XdbfWrapper::GetStringTableEntry(Language language,
                                             uint16_t string_id) const {
  auto language_block =
      GetEntry(XdbfSection::kStringTable, static_cast<uint64_t>(language));
  if (!language_block) {
    return "";
  }

  auto xstr_head =
      reinterpret_cast<const XdbfXstrHeader*>(language_block.buffer);
  assert_true(xstr_head->magic == kXdbfSignatureXstr);
  assert_true(xstr_head->version == 1);

  const uint8_t* ptr = language_block.buffer + sizeof(XdbfXstrHeader);
  for (uint16_t i = 0; i < xstr_head->string_count; ++i) {
    auto entry = reinterpret_cast<const XdbfStringTableEntry*>(ptr);
    ptr += sizeof(XdbfStringTableEntry);
    if (entry->id == string_id) {
      return std::string(reinterpret_cast<const char*>(ptr),
                         entry->string_length);
    }
    ptr += entry->string_length;
  }
  return "";
}

constexpr uint64_t kXdbfIdTitle = 0x8000;
constexpr uint64_t kXdbfIdXstc = 0x58535443;

XdbfBlock XdbfGameData::icon() const {
  return GetEntry(XdbfSection::kImage, kXdbfIdTitle);
}

Language XdbfGameData::default_language() const {
  auto block = GetEntry(XdbfSection::kMetadata, kXdbfIdXstc);
  if (!block.buffer) {
    return Language::kEnglish;
  }
  auto xstc = reinterpret_cast<const XdbfXstc*>(block.buffer);
  assert_true(xstc->magic == kXdbfSignatureXstc);
  return static_cast<Language>(static_cast<uint8_t>(xstc->default_language));
}

std::string XdbfGameData::title() const {
  return GetStringTableEntry(default_language(), kXdbfIdTitle);
}

std::string XdbfGameData::title(Language language) const {
  return GetStringTableEntry(language, kXdbfIdTitle);
}

}  // namespace util
}  // namespace kernel
}  // namespace xe
