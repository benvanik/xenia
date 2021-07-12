/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/xam_module.h"

#include <vector>

#include "xenia/base/math.h"
#include "xenia/base/string_util.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xam/xam_private.h"

namespace xe {
namespace kernel {
namespace xam {

std::atomic<int> xam_dialogs_shown_ = {0};

bool xeXamIsUIActive() { return xam_dialogs_shown_ > 0; }

void AllocateStringTable(Memory* memory,
                         std::initializer_list<const std::u16string_view> list,
                         uint32_t& data_address,
                         std::vector<uint32_t>& entry_addresses) {
  size_t size = 0;

  std::vector<std::tuple<size_t, const std::u16string_view>> entries;

  for (const auto& entry : list) {
    if (!entry.data()) {
      // since 0 would point to the pointer table, we use it to signify the
      // entry is invalid/null.
      entries.push_back({0, {}});
      continue;
    }

    size_t entry_size = (entry.length() + 1) * sizeof(char16_t);

    entries.push_back({size, entry});

    size += entry_size;
    size = align(size, size_t(4) /* hello I'm Linux and I'm a doo-doo head */);
  }

  data_address = memory->SystemHeapAlloc(uint32_t(size));
  assert_not_zero(data_address);

  auto buffer = memory->TranslateVirtual(data_address);

  entry_addresses.resize(entries.size());
  for (int i = 0; i < entries.size(); ++i) {
    auto& [offset, entry] = entries[i];
    if (!entry.data()) {
      entry_addresses[i] = 0;
      continue;
    }
    entry_addresses[i] = uint32_t(data_address + offset);
    // we don't need to null terminate because SystemHeapAlloc implies zeroing
    auto entry_buffer = reinterpret_cast<char16_t*>(&buffer[offset]);
    store_and_swap(entry_buffer, entry);
  }
}

XamModule::XamModule(Emulator* emulator, KernelState* kernel_state)
    : KernelModule(kernel_state, "xe:\\xam.xex"), loader_data_() {
  RegisterExportTable(export_resolver_);

#define XE_MODULE_EXPORT_GROUP(m, n) \
  Register##n##Exports(export_resolver_, kernel_state_);
#include "xam_module_export_groups.inc"
#undef XE_MODULE_EXPORT_GROUP

  // Language locale fallback strings, for XamGetLanguageLocaleFallbackString.
  std::initializer_list<const std::u16string_view> llf_entries = {
      {},       {},       u"ja-JP", u"de-DE", u"fr-FR",  u"es-ES",
      u"it-IT", u"ko-KR", u"zh-TW", u"pt-BR", u"zh-CHS", u"pl-PL",
      u"ru-RU", u"sv-SE", u"tr-TR", u"nb-NO", u"nl-NL",  u"zh-CHS",
  };
  AllocateStringTable(memory_, llf_entries, llf_data_address_,
                      llf_entry_addresses_);
}

std::vector<xe::cpu::Export*> xam_exports(4096);

xe::cpu::Export* RegisterExport_xam(xe::cpu::Export* export_entry) {
  assert_true(export_entry->ordinal < xam_exports.size());
  xam_exports[export_entry->ordinal] = export_entry;
  return export_entry;
}

void XamModule::RegisterExportTable(xe::cpu::ExportResolver* export_resolver) {
  assert_not_null(export_resolver);

// Build the export table used for resolution.
#include "xenia/kernel/util/export_table_pre.inc"
  static xe::cpu::Export xam_export_table[] = {
#include "xenia/kernel/xam/xam_table.inc"
  };
#include "xenia/kernel/util/export_table_post.inc"
  for (size_t i = 0; i < xe::countof(xam_export_table); ++i) {
    auto& export_entry = xam_export_table[i];
    assert_true(export_entry.ordinal < xam_exports.size());
    if (!xam_exports[export_entry.ordinal]) {
      xam_exports[export_entry.ordinal] = &export_entry;
    }
  }
  export_resolver->RegisterTable("xam.xex", &xam_exports);
}

XamModule::~XamModule() {}

}  // namespace xam
}  // namespace kernel
}  // namespace xe
