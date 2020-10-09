/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */
#include "xenia/patcher/patcher.h"

#include "xenia/base/cvar.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"

DEFINE_bool(apply_patches, true, "Enables patching functionality", "General");

namespace xe {
namespace patcher {

PatchingSystem::PatchingSystem() {
  is_any_patch_applied_ = false;
  LoadPatches();
}

PatchingSystem::~PatchingSystem() {}

void PatchingSystem::LoadPatches() {
  if (!cvars::apply_patches) {
    return;
  }

  const std::filesystem::path patches_directory =
      filesystem::GetExecutableFolder() / "patches";

  const std::vector<xe::filesystem::FileInfo>& patch_files =
      filesystem::ListFiles(patches_directory);
  std::regex file_name_regex_match = std::regex("^[A-Fa-f0-9]{8}.*\\.patch$");

  for (const xe::filesystem::FileInfo& patch_file : patch_files) {
    // Skip files that doesn't have only title_id as name and .patch as
    // extension
    if (!std::regex_match(patch_file.name.u8string(), file_name_regex_match)) {
      XELOGE(
          "PatchingSystem: Skipped loading file {} due to incorrect filename",
          patch_file.name.u8string());
      continue;
    }

    const PatchFileEntry loaded_title_patches =
        ReadPatchFile(patch_file.path / patch_file.name);
    if (loaded_title_patches.title_id != -1) {
      loaded_patch_files.push_back(loaded_title_patches);
    }
  }
  XELOGI("Patching System: Loaded patches for {} titles",
         loaded_patch_files.size());
}

void PatchingSystem::ApplyPatchesForTitle(Memory* memory,
                                          const uint32_t title_id,
                                          const uint64_t hash) {
  for (const PatchFileEntry& patchFile : loaded_patch_files) {
    if (patchFile.title_id != title_id) {
      continue;
    }
    if (patchFile.hash != 0 && patchFile.hash != hash) {
      continue;
    }

    for (const PatchInfoEntry& patchEntry : patchFile.patch_info) {
      if (!patchEntry.is_enabled) {
        continue;
      }
      XELOGE("Applying patch for: {}({:08X}) - {}", patchFile.title_name,
             patchFile.title_id, patchEntry.patch_name);
      ApplyPatch(memory, &patchEntry);
    }
  }
}

void PatchingSystem::ApplyPatch(Memory* memory, const PatchInfoEntry* patch) {
  for (const PatchDataEntry& patch_data_entry : patch->patch_data) {
    uint32_t old_address_protect = 0;
    auto address = memory->TranslateVirtual(patch_data_entry.memory_address_);
    auto heap = memory->LookupHeap(patch_data_entry.memory_address_);
    if (!heap) {
      continue;
    }

    heap->QueryProtect(patch_data_entry.memory_address_, &old_address_protect);

    heap->Protect(patch_data_entry.memory_address_,
                  patch_data_entry.alloc_size_,
                  kMemoryProtectRead | kMemoryProtectWrite);

    switch (patch_data_entry.alloc_size_) {
      case 1:
        xe::store_and_swap(address, uint8_t(patch_data_entry.new_value_));
        break;
      case 2:
        xe::store_and_swap(address, uint16_t(patch_data_entry.new_value_));
        break;
      case 4:
        xe::store_and_swap(address, uint32_t(patch_data_entry.new_value_));
        break;
      case 8:
        xe::store_and_swap(address, uint64_t(patch_data_entry.new_value_));
        break;
      default:
        XELOGE("Unsupported patch allocation size");
        break;
    }
    // Restore previous protection
    heap->Protect(patch_data_entry.memory_address_,
                  patch_data_entry.alloc_size_, old_address_protect);

    if (!is_any_patch_applied_) {
      is_any_patch_applied_ = true;
    }
  }
}

PatchFileEntry PatchingSystem::ReadPatchFile(
    const std::filesystem::path& file_path) {
  PatchFileEntry patchFile;
  std::shared_ptr<cpptoml::table> patch_toml_fields;

  try {
    patch_toml_fields = cpptoml::parse_file(file_path.u8string());
  } catch (...) {
    XELOGE("Cannot load patch file: {}", file_path.u8string());
    patchFile.title_id = -1;
    return patchFile;
  };

  auto title_name = patch_toml_fields->get_as<std::string>("title_name");
  auto title_id = patch_toml_fields->get_as<std::string>("title_id");
  auto title_hash = patch_toml_fields->get_as<std::string>("hash");

  patchFile.title_id = strtoul((*title_id).c_str(), NULL, 16);
  patchFile.hash = strtoull((*title_hash).c_str(), NULL, 16);
  patchFile.title_name = *title_name;

  auto tarr = patch_toml_fields->get_table_array("patch");

  for (auto table : *tarr) {
    PatchInfoEntry patch = PatchInfoEntry();
    auto patch_name = *table->get_as<std::string>("name");
    auto patch_desc = *table->get_as<std::string>("desc");
    auto patch_author = *table->get_as<std::string>("author");
    auto is_enabled = *table->get_as<bool>("is_enabled");

    patch.id = 0;  // ToDo: Implement id for future GUI stuff
    patch.patch_name = patch_name;
    patch.patch_desc = patch_desc;
    patch.patch_author = patch_author;
    patch.is_enabled = is_enabled;

    const std::string data_types[] = {"be64", "be32", "be16", "be8"};

    // Iterate through all available data sizes
    for (const std::string& type : data_types) {
      auto entries = ReadPatchData(type, table);

      for (const PatchDataEntry& entry : *entries) {
        patch.patch_data.push_back(entry);
      }
    }
    patchFile.patch_info.push_back(patch);
  }
  return patchFile;
}

std::vector<PatchDataEntry>* PatchingSystem::ReadPatchData(
    const std::string size_type,
    const std::shared_ptr<cpptoml::table>& patch_table) {
  std::vector<PatchDataEntry>* patch_data = new std::vector<PatchDataEntry>();
  auto patch_data_tarr = patch_table->get_table_array(size_type);

  if (!patch_data_tarr) {
    return patch_data;
  }

  for (const auto& patch_data_table : *patch_data_tarr) {
    auto address = patch_data_table->get_as<std::uint32_t>("address");
    // Todo: How to handle different sizes
    auto value = patch_data_table->get_as<std::uint64_t>("value");

    PatchDataEntry patchData = {GetAllocSize(size_type), *address, *value};
    patch_data->push_back(patchData);
  }
  return patch_data;
}

uint8_t PatchingSystem::GetAllocSize(const std::string provided_size) {
  uint8_t alloc_size = sizeof(uint32_t);

  if (provided_size == "be64") {
    alloc_size = sizeof(uint64_t);
  }

  else if (provided_size == "be32") {
    alloc_size = sizeof(uint32_t);
  }

  else if (provided_size == "be16") {
    alloc_size = sizeof(uint16_t);
  }

  else if (provided_size == "be8") {
    alloc_size = sizeof(uint8_t);
  }
  return alloc_size;
}

}  // namespace patcher
}  // namespace xe
