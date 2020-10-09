/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/user_module.h"

#include <vector>

#include "xenia/base/byte_stream.h"
#include "xenia/base/logging.h"
#include "xenia/base/xxhash.h"
#include "xenia/cpu/elf_module.h"
#include "xenia/cpu/processor.h"
#include "xenia/cpu/xex_module.h"
#include "xenia/emulator.h"
#include "xenia/kernel/xfile.h"
#include "xenia/kernel/xthread.h"

DEFINE_bool(xex_apply_patches, true, "Apply XEX patches.", "Kernel");

namespace xe {
namespace kernel {

UserModule::UserModule(KernelState* kernel_state)
    : XModule(kernel_state, ModuleType::kUserModule) {}

UserModule::~UserModule() { Unload(); }

uint32_t UserModule::title_id() const {
  if (module_format_ != kModuleFormatXex) {
    return 0;
  }
  auto header = xex_header();
  for (uint32_t i = 0; i < header->header_count; i++) {
    auto& opt_header = header->headers[i];
    if (opt_header.key == XEX_HEADER_EXECUTION_INFO) {
      auto opt_header_ptr =
          reinterpret_cast<const uint8_t*>(header) + opt_header.offset;
      auto opt_exec_info =
          reinterpret_cast<const xex2_opt_execution_info*>(opt_header_ptr);
      return static_cast<uint32_t>(opt_exec_info->title_id);
    }
  }
  return 0;
}

X_STATUS UserModule::LoadFromFile(const std::string_view path) {
  X_STATUS result = X_STATUS_UNSUCCESSFUL;

  // Resolve the file to open.
  // TODO(benvanik): make this code shared?
  auto fs_entry = kernel_state()->file_system()->ResolvePath(path);
  if (!fs_entry) {
    XELOGE("File not found: {}", path);
    return X_STATUS_NO_SUCH_FILE;
  }

  path_ = fs_entry->absolute_path();
  name_ = utf8::find_base_name_from_guest_path(path_);

  // If the FS supports mapping, map the file in and load from that.
  if (fs_entry->can_map()) {
    // Map.
    auto mmap = fs_entry->OpenMapped(MappedMemory::Mode::kRead);
    if (!mmap) {
      return result;
    }

    // Load the module.
    result = LoadFromMemory(mmap->data(), mmap->size());
    if (XSUCCEEDED(result)) {
      XXH3_state_t hash_state;
      XXH3_64bits_reset(&hash_state);
      XXH3_64bits_update(&hash_state, mmap->data(), mmap->size());
      hash_ = XXH3_64bits_digest(&hash_state);
    }
  } else {
    std::vector<uint8_t> buffer(fs_entry->size());

    // Open file for reading.
    vfs::File* file = nullptr;
    result = fs_entry->Open(vfs::FileAccess::kGenericRead, &file);
    if (XFAILED(result)) {
      return result;
    }

    // Read entire file into memory.
    // Ugh.
    size_t bytes_read = 0;
    result = file->ReadSync(buffer.data(), buffer.size(), 0, &bytes_read);
    if (XFAILED(result)) {
      return result;
    }

    // Load the module.
    result = LoadFromMemory(buffer.data(), bytes_read);

    // Generate xex hash
    XXH3_state_t hash_state;
    XXH3_64bits_reset(&hash_state);
    XXH3_64bits_update(&hash_state, buffer.data(), bytes_read);
    hash_ = XXH3_64bits_digest(&hash_state);

    // Close the file.
    file->Destroy();
  }

  // Only XEX returns X_STATUS_PENDING
  if (result != X_STATUS_PENDING) {
    return result;
  }

  XELOGI("Module hash: {:08X} for {}", hash_, name_);

  if (cvars::xex_apply_patches) {
    // Search for xexp patch file
    auto patch_entry = kernel_state()->file_system()->ResolvePath(path_ + "p");

    if (patch_entry) {
      auto patch_path = patch_entry->absolute_path();

      XELOGI("Loading XEX patch from {}", patch_path);

      auto patch_module = object_ref<UserModule>(new UserModule(kernel_state_));
      result = patch_module->LoadFromFile(patch_path);
      if (!result) {
        result = patch_module->xex_module()->ApplyPatch(xex_module());
        if (result) {
          XELOGE("Failed to apply XEX patch, code: {}", result);
        }
      } else {
        XELOGE("Failed to load XEX patch, code: {}", result);
      }

      if (result) {
        return X_STATUS_UNSUCCESSFUL;
      }
    }
  }

  return LoadXexContinue();
}

X_STATUS UserModule::LoadFromMemory(const void* addr, const size_t length) {
  auto processor = kernel_state()->processor();

  be<fourcc_t> magic;
  magic.value = xe::load<fourcc_t>(addr);
  if (magic == xe::cpu::kXEX2Signature || magic == xe::cpu::kXEX1Signature) {
    module_format_ = kModuleFormatXex;
  } else if (magic == xe::cpu::kElfSignature) {
    module_format_ = kModuleFormatElf;
  } else {
    be<uint16_t> magic16;
    magic16.value = xe::load<uint16_t>(addr);
    if (magic16 == 0x4D5A) {
      XELOGE("XNA executables are not yet implemented");
      return X_STATUS_NOT_IMPLEMENTED;
    } else {
      XELOGE("Unknown module magic: {:08X}", magic);
      return X_STATUS_NOT_IMPLEMENTED;
    }
  }

  if (module_format_ == kModuleFormatXex) {
    // Prepare the module for execution.
    // Runtime takes ownership.
    auto xex_module =
        std::make_unique<cpu::XexModule>(processor, kernel_state());
    if (!xex_module->Load(name_, path_, addr, length)) {
      return X_STATUS_UNSUCCESSFUL;
    }
    processor_module_ = xex_module.get();
    if (!processor->AddModule(std::move(xex_module))) {
      return X_STATUS_UNSUCCESSFUL;
    }

    // Only XEX headers + image are loaded right now
    // Caller will have to call LoadXexContinue after they've loaded in a patch
    // (or after patch isn't found anywhere)
    // or if this is an XEXP being loaded return success since there's nothing
    // else to load
    return this->xex_module()->is_patch() ? X_STATUS_SUCCESS : X_STATUS_PENDING;

  } else if (module_format_ == kModuleFormatElf) {
    auto elf_module =
        std::make_unique<cpu::ElfModule>(processor, kernel_state());
    if (!elf_module->Load(name_, path_, addr, length)) {
      return X_STATUS_UNSUCCESSFUL;
    }

    entry_point_ = elf_module->entry_point();
    stack_size_ = 1024 * 1024;  // 1 MB
    is_dll_module_ = false;     // Hardcoded not a DLL (for now)

    processor_module_ = elf_module.get();
    if (!processor->AddModule(std::move(elf_module))) {
      return X_STATUS_UNSUCCESSFUL;
    }
  }

  OnLoad();

  return X_STATUS_SUCCESS;
}

X_STATUS UserModule::LoadXexContinue() {
  // LoadXexContinue: finishes loading XEX after a patch has been applied (or
  // patch wasn't found)

  if (!this->xex_module()) {
    return X_STATUS_UNSUCCESSFUL;
  }

  // If guest_xex_header is set we must have already loaded the XEX
  if (guest_xex_header_) {
    return X_STATUS_SUCCESS;
  }

  // Finish XexModule load (PE sections/imports/symbols...)
  if (!xex_module()->LoadContinue()) {
    return X_STATUS_UNSUCCESSFUL;
  }

  // Copy the xex2 header into guest memory.
  auto header = this->xex_module()->xex_header();
  auto security_header = this->xex_module()->xex_security_info();
  guest_xex_header_ = memory()->SystemHeapAlloc(header->header_size);

  uint8_t* xex_header_ptr = memory()->TranslateVirtual(guest_xex_header_);
  std::memcpy(xex_header_ptr, header, header->header_size);

  // Cache some commonly used headers...
  this->xex_module()->GetOptHeader(XEX_HEADER_ENTRY_POINT, &entry_point_);
  this->xex_module()->GetOptHeader(XEX_HEADER_DEFAULT_STACK_SIZE, &stack_size_);
  is_dll_module_ = !!(header->module_flags & XEX_MODULE_DLL_MODULE);

  // Setup the loader data entry
  auto ldr_data =
      memory()->TranslateVirtual<X_LDR_DATA_TABLE_ENTRY*>(hmodule_ptr_);

  ldr_data->dll_base = 0;  // GetProcAddress will read this.
  ldr_data->xex_header_base = guest_xex_header_;
  ldr_data->full_image_size = security_header->image_size;
  ldr_data->image_base = this->xex_module()->base_address();
  ldr_data->entry_point = entry_point_;

  OnLoad();

  return X_STATUS_SUCCESS;
}

X_STATUS UserModule::Unload() {
  if (module_format_ == kModuleFormatXex &&
      (!processor_module_ || !xex_module()->loaded())) {
    // Quick abort.
    return X_STATUS_SUCCESS;
  }

  if (module_format_ == kModuleFormatXex && processor_module_ &&
      xex_module()->Unload()) {
    OnUnload();
    return X_STATUS_SUCCESS;
  }

  return X_STATUS_UNSUCCESSFUL;
}

uint32_t UserModule::GetProcAddressByOrdinal(uint16_t ordinal) {
  return xex_module()->GetProcAddress(ordinal);
}

uint32_t UserModule::GetProcAddressByName(std::string_view name) {
  return xex_module()->GetProcAddress(name);
}

X_STATUS UserModule::GetSection(const std::string_view name,
                                uint32_t* out_section_data,
                                uint32_t* out_section_size) {
  xex2_opt_resource_info* resource_header = nullptr;
  if (!cpu::XexModule::GetOptHeader(xex_header(), XEX_HEADER_RESOURCE_INFO,
                                    &resource_header)) {
    // No resources.
    return X_STATUS_NOT_FOUND;
  }
  uint32_t count = (resource_header->size - 4) / sizeof(xex2_resource);
  for (uint32_t i = 0; i < count; i++) {
    auto& res = resource_header->resources[i];
    if (utf8::equal_z(name, std::string_view(res.name, 8))) {
      // Found!
      *out_section_data = res.address;
      *out_section_size = res.size;
      return X_STATUS_SUCCESS;
    }
  }

  return X_STATUS_NOT_FOUND;
}

X_STATUS UserModule::GetOptHeader(xex2_header_keys key, void** out_ptr) {
  assert_not_null(out_ptr);

  if (module_format_ == kModuleFormatElf) {
    // Quick die.
    return X_STATUS_UNSUCCESSFUL;
  }

  bool ret = xex_module()->GetOptHeader(key, out_ptr);
  if (!ret) {
    return X_STATUS_NOT_FOUND;
  }

  return X_STATUS_SUCCESS;
}

X_STATUS UserModule::GetOptHeader(xex2_header_keys key,
                                  uint32_t* out_header_guest_ptr) {
  if (module_format_ == kModuleFormatElf) {
    // Quick die.
    return X_STATUS_UNSUCCESSFUL;
  }

  auto header =
      memory()->TranslateVirtual<const xex2_header*>(guest_xex_header_);
  if (!header) {
    return X_STATUS_UNSUCCESSFUL;
  }
  return GetOptHeader(memory(), header, key, out_header_guest_ptr);
}

X_STATUS UserModule::GetOptHeader(const Memory* memory,
                                  const xex2_header* header,
                                  xex2_header_keys key,
                                  uint32_t* out_header_guest_ptr) {
  assert_not_null(out_header_guest_ptr);
  uint32_t field_value = 0;
  bool field_found = false;
  for (uint32_t i = 0; i < header->header_count; i++) {
    auto& opt_header = header->headers[i];
    if (opt_header.key != key) {
      continue;
    }
    field_found = true;
    switch (opt_header.key & 0xFF) {
      case 0x00:
        // Return data stored in header value.
        field_value = opt_header.value;
        break;
      case 0x01:
        // Return pointer to data stored in header value.
        field_value = memory->HostToGuestVirtual(&opt_header.value);
        break;
      default:
        // Data stored at offset to header.
        field_value = memory->HostToGuestVirtual(header) + opt_header.offset;
        break;
    }
    break;
  }
  *out_header_guest_ptr = field_value;
  if (!field_found) {
    return X_STATUS_NOT_FOUND;
  }
  return X_STATUS_SUCCESS;
}

bool UserModule::Save(ByteStream* stream) {
  if (!XModule::Save(stream)) {
    return false;
  }

  // A lot of the information stored on this class can be reconstructed at
  // runtime.

  return true;
}

object_ref<UserModule> UserModule::Restore(KernelState* kernel_state,
                                           ByteStream* stream,
                                           const std::string_view path) {
  auto module = new UserModule(kernel_state);

  // XModule::Save took care of this earlier...
  // TODO: Find a nicer way to represent that here.
  if (!module->RestoreObject(stream)) {
    return nullptr;
  }

  auto result = module->LoadFromFile(path);
  if (XFAILED(result)) {
    XELOGD("UserModule::Restore LoadFromFile({}) FAILED - code {:08X}", path,
           result);
    return nullptr;
  }

  if (!kernel_state->RegisterUserModule(retain_object(module))) {
    // Already loaded?
    assert_always();
  }

  return object_ref<UserModule>(module);
}

void UserModule::Dump() {
  if (module_format_ == kModuleFormatElf) {
    // Quick die.
    return;
  }

  StringBuffer sb;

  xe::cpu::ExportResolver* export_resolver =
      kernel_state_->emulator()->export_resolver();
  auto header = xex_header();

  // XEX header.
  sb.AppendFormat("Module {}:\n", path_);
  sb.AppendFormat("    Module Flags: {:08X}\n", (uint32_t)header->module_flags);

  // Security header
  auto security_info = xex_module()->xex_security_info();
  sb.Append("Security Header:\n");
  sb.AppendFormat("     Image Flags: {:08X}\n",
                  (uint32_t)security_info->image_flags);
  sb.AppendFormat("    Load Address: {:08X}\n",
                  (uint32_t)security_info->load_address);
  sb.AppendFormat("      Image Size: {:08X}\n",
                  (uint32_t)security_info->image_size);
  sb.AppendFormat("    Export Table: {:08X}\n",
                  (uint32_t)security_info->export_table);

  // Optional headers
  sb.AppendFormat("Optional Header Count: {}\n",
                  (uint32_t)header->header_count);

  for (uint32_t i = 0; i < header->header_count; i++) {
    auto& opt_header = header->headers[i];

    // Stash a pointer (although this isn't used in every case)
    auto opt_header_ptr =
        reinterpret_cast<const uint8_t*>(header) + opt_header.offset;
    switch (opt_header.key) {
      case XEX_HEADER_RESOURCE_INFO: {
        sb.Append("  XEX_HEADER_RESOURCE_INFO:\n");
        auto opt_resource_info =
            reinterpret_cast<const xex2_opt_resource_info*>(opt_header_ptr);

        uint32_t count = (opt_resource_info->size - 4) / 16;
        for (uint32_t j = 0; j < count; j++) {
          auto& res = opt_resource_info->resources[j];

          // Manually NULL-terminate the name.
          char name[9];
          std::memcpy(name, res.name, sizeof(res.name));
          name[8] = 0;

          sb.AppendFormat(
              "    {:<8} {:08X}-{:08X}, {}b\n", name, (uint32_t)res.address,
              (uint32_t)res.address + (uint32_t)res.size, (uint32_t)res.size);
        }
      } break;
      case XEX_HEADER_FILE_FORMAT_INFO: {
        sb.Append("  XEX_HEADER_FILE_FORMAT_INFO (TODO):\n");
      } break;
      case XEX_HEADER_DELTA_PATCH_DESCRIPTOR: {
        sb.Append("  XEX_HEADER_DELTA_PATCH_DESCRIPTOR (TODO):\n");
      } break;
      case XEX_HEADER_BOUNDING_PATH: {
        auto opt_bound_path =
            reinterpret_cast<const xex2_opt_bound_path*>(opt_header_ptr);
        sb.AppendFormat("  XEX_HEADER_BOUNDING_PATH: {}\n",
                        opt_bound_path->path);
      } break;
      case XEX_HEADER_ORIGINAL_BASE_ADDRESS: {
        sb.AppendFormat("  XEX_HEADER_ORIGINAL_BASE_ADDRESS: {:08X}\n",
                        (uint32_t)opt_header.value);
      } break;
      case XEX_HEADER_ENTRY_POINT: {
        sb.AppendFormat("  XEX_HEADER_ENTRY_POINT: {:08X}\n",
                        (uint32_t)opt_header.value);
      } break;
      case XEX_HEADER_IMAGE_BASE_ADDRESS: {
        sb.AppendFormat("  XEX_HEADER_IMAGE_BASE_ADDRESS: {:08X}\n",
                        (uint32_t)opt_header.value);
      } break;
      case XEX_HEADER_IMPORT_LIBRARIES: {
        sb.Append("  XEX_HEADER_IMPORT_LIBRARIES:\n");
        auto opt_import_libraries =
            reinterpret_cast<const xex2_opt_import_libraries*>(opt_header_ptr);

        // FIXME: Don't know if 32 is the actual limit, but haven't seen more
        // than 2.
        const char* string_table[32];
        std::memset(string_table, 0, sizeof(string_table));

        // Parse the string table
        for (size_t j = 0, o = 0; j < opt_import_libraries->string_table.size &&
                                  o < opt_import_libraries->string_table.count;
             o++) {
          assert_true(o < xe::countof(string_table));
          const char* str = &opt_import_libraries->string_table.data[j];

          string_table[o] = str;
          j += std::strlen(str) + 1;

          // Padding
          if ((j % 4) != 0) {
            j += 4 - (j % 4);
          }
        }

        auto library_data =
            reinterpret_cast<const uint8_t*>(opt_import_libraries);
        uint32_t library_offset = opt_import_libraries->string_table.size + 12;
        while (library_offset < opt_import_libraries->size) {
          auto library = reinterpret_cast<const xex2_import_library*>(
              library_data + library_offset);
          if (!library->size) {
            break;
          }
          auto name = string_table[library->name_index & 0xFF];
          assert_not_null(name);
          sb.AppendFormat("    {} - {} imports\n", name,
                          (uint16_t)library->count);

          xex2_version version, version_min;
          version = library->version();
          version_min = library->version_min();
          sb.AppendFormat("      Version: {}.{}.{}.{}\n", version.major,
                          version.minor, version.build, version.qfe);
          sb.AppendFormat("      Min Version: {}.{}.{}.{}\n", version_min.major,
                          version_min.minor, version_min.build,
                          version_min.qfe);

          library_offset += library->size;
        }
      } break;
      case XEX_HEADER_CHECKSUM_TIMESTAMP: {
        sb.Append("  XEX_HEADER_CHECKSUM_TIMESTAMP (TODO):\n");
      } break;
      case XEX_HEADER_ORIGINAL_PE_NAME: {
        auto opt_pe_name =
            reinterpret_cast<const xex2_opt_original_pe_name*>(opt_header_ptr);
        sb.AppendFormat("  XEX_HEADER_ORIGINAL_PE_NAME: {}\n",
                        opt_pe_name->name);
      } break;
      case XEX_HEADER_STATIC_LIBRARIES: {
        sb.Append("  XEX_HEADER_STATIC_LIBRARIES:\n");
        auto opt_static_libraries =
            reinterpret_cast<const xex2_opt_static_libraries*>(opt_header_ptr);

        uint32_t count = (opt_static_libraries->size - 4) / 0x10;
        for (uint32_t l = 0; l < count; l++) {
          auto& library = opt_static_libraries->libraries[l];
          sb.AppendFormat("    {:<8} : {}.{}.{}.{}\n", library.name,
                          static_cast<uint16_t>(library.version_major),
                          static_cast<uint16_t>(library.version_minor),
                          static_cast<uint16_t>(library.version_build),
                          static_cast<uint16_t>(library.version_qfe));
        }
      } break;
      case XEX_HEADER_TLS_INFO: {
        sb.Append("  XEX_HEADER_TLS_INFO:\n");
        auto opt_tls_info =
            reinterpret_cast<const xex2_opt_tls_info*>(opt_header_ptr);

        sb.AppendFormat("          Slot Count: {}\n",
                        static_cast<uint32_t>(opt_tls_info->slot_count));
        sb.AppendFormat("    Raw Data Address: {:08X}\n",
                        static_cast<uint32_t>(opt_tls_info->raw_data_address));
        sb.AppendFormat("           Data Size: {}\n",
                        static_cast<uint32_t>(opt_tls_info->data_size));
        sb.AppendFormat("       Raw Data Size: {}\n",
                        static_cast<uint32_t>(opt_tls_info->raw_data_size));
      } break;
      case XEX_HEADER_DEFAULT_STACK_SIZE: {
        sb.AppendFormat("  XEX_HEADER_DEFAULT_STACK_SIZE: {}\n",
                        static_cast<uint32_t>(opt_header.value));
      } break;
      case XEX_HEADER_DEFAULT_FILESYSTEM_CACHE_SIZE: {
        sb.AppendFormat("  XEX_HEADER_DEFAULT_FILESYSTEM_CACHE_SIZE: {}\n",
                        static_cast<uint32_t>(opt_header.value));
      } break;
      case XEX_HEADER_DEFAULT_HEAP_SIZE: {
        sb.AppendFormat("  XEX_HEADER_DEFAULT_HEAP_SIZE: {}\n",
                        static_cast<uint32_t>(opt_header.value));
      } break;
      case XEX_HEADER_PAGE_HEAP_SIZE_AND_FLAGS: {
        sb.Append("  XEX_HEADER_PAGE_HEAP_SIZE_AND_FLAGS (TODO):\n");
      } break;
      case XEX_HEADER_SYSTEM_FLAGS: {
        sb.AppendFormat("  XEX_HEADER_SYSTEM_FLAGS: {:08X}\n",
                        static_cast<uint32_t>(opt_header.value));
      } break;
      case XEX_HEADER_EXECUTION_INFO: {
        sb.Append("  XEX_HEADER_EXECUTION_INFO:\n");
        auto opt_exec_info =
            reinterpret_cast<const xex2_opt_execution_info*>(opt_header_ptr);

        sb.AppendFormat("       Media ID: {:08X}\n",
                        static_cast<uint32_t>(opt_exec_info->media_id));
        sb.AppendFormat("       Title ID: {:08X}\n",
                        static_cast<uint32_t>(opt_exec_info->title_id));
        sb.AppendFormat("    Savegame ID: {:08X}\n",
                        static_cast<uint32_t>(opt_exec_info->title_id));
        sb.AppendFormat("    Disc Number / Total: {} / {}\n",
                        opt_exec_info->disc_number, opt_exec_info->disc_count);
      } break;
      case XEX_HEADER_TITLE_WORKSPACE_SIZE: {
        sb.AppendFormat("  XEX_HEADER_TITLE_WORKSPACE_SIZE: {}\n",
                        uint32_t(opt_header.value));
      } break;
      case XEX_HEADER_GAME_RATINGS: {
        sb.Append("  XEX_HEADER_GAME_RATINGS (TODO):\n");
      } break;
      case XEX_HEADER_LAN_KEY: {
        sb.Append("  XEX_HEADER_LAN_KEY:");
        auto opt_lan_key =
            reinterpret_cast<const xex2_opt_lan_key*>(opt_header_ptr);

        for (int l = 0; l < 16; l++) {
          sb.AppendFormat(" {:02X}", opt_lan_key->key[l]);
        }
        sb.Append("\n");
      } break;
      case XEX_HEADER_XBOX360_LOGO: {
        sb.Append("  XEX_HEADER_XBOX360_LOGO (TODO):\n");
      } break;
      case XEX_HEADER_MULTIDISC_MEDIA_IDS: {
        sb.Append("  XEX_HEADER_MULTIDISC_MEDIA_IDS (TODO):\n");
      } break;
      case XEX_HEADER_ALTERNATE_TITLE_IDS: {
        sb.Append("  XEX_HEADER_ALTERNATE_TITLE_IDS (TODO):\n");
      } break;
      case XEX_HEADER_ADDITIONAL_TITLE_MEMORY: {
        sb.AppendFormat("  XEX_HEADER_ADDITIONAL_TITLE_MEMORY: {}\n",
                        uint32_t(opt_header.value));
      } break;
      case XEX_HEADER_EXPORTS_BY_NAME: {
        sb.Append("  XEX_HEADER_EXPORTS_BY_NAME:\n");
        auto dir =
            reinterpret_cast<const xex2_opt_data_directory*>(opt_header_ptr);

        auto exe_address = xex_module()->base_address();
        auto e = memory()->TranslateVirtual<const X_IMAGE_EXPORT_DIRECTORY*>(
            exe_address + dir->offset);
        auto e_base = reinterpret_cast<uintptr_t>(e);

        // e->AddressOfX RVAs are relative to the IMAGE_EXPORT_DIRECTORY!
        auto function_table =
            reinterpret_cast<const uint32_t*>(e_base + e->AddressOfFunctions);
        // Names relative to directory.
        auto name_table =
            reinterpret_cast<const uint32_t*>(e_base + e->AddressOfNames);
        // Table of ordinals (by name).
        auto ordinal_table = reinterpret_cast<const uint16_t*>(
            e_base + e->AddressOfNameOrdinals);
        for (uint32_t n = 0; n < e->NumberOfNames; n++) {
          auto name = reinterpret_cast<const char*>(e_base + name_table[n]);
          uint16_t ordinal = ordinal_table[n];
          uint32_t addr = exe_address + function_table[ordinal];
          sb.AppendFormat("    {:<28} - {:03X} - {:08X}\n", name, ordinal,
                          addr);
        }
      } break;
      default: {
        sb.AppendFormat("  Unknown Header {:08X}\n", (uint32_t)opt_header.key);
      } break;
    }
  }

  sb.Append("Sections:\n");
  for (uint32_t i = 0, page = 0; i < security_info->page_descriptor_count;
       i++) {
    // Manually byteswap the bitfield data.
    xex2_page_descriptor page_descriptor;
    page_descriptor.value =
        xe::byte_swap(security_info->page_descriptors[i].value);

    const char* type = "UNKNOWN";
    switch (page_descriptor.info) {
      case XEX_SECTION_CODE:
        type = "CODE   ";
        break;
      case XEX_SECTION_DATA:
        type = "RWDATA ";
        break;
      case XEX_SECTION_READONLY_DATA:
        type = "RODATA ";
        break;
    }

    const uint32_t page_size =
        xex_module()->base_address() < 0x90000000 ? 64 * 1024 : 4 * 1024;
    uint32_t start_address = xex_module()->base_address() + (page * page_size);
    uint32_t end_address =
        start_address + (page_descriptor.page_count * page_size);

    sb.AppendFormat("  {:3} {} {:3} pages    {:08X} - {:08X} ({} bytes)\n",
                    page, type, page_descriptor.page_count, start_address,
                    end_address, page_descriptor.page_count * page_size);
    page += page_descriptor.page_count;
  }

  // Print out imports.

  auto import_libs = xex_module()->import_libraries();

  sb.Append("Imports:\n");
  for (std::vector<cpu::XexModule::ImportLibrary>::const_iterator library =
           import_libs->begin();
       library != import_libs->end(); ++library) {
    if (library->imports.size() > 0) {
      sb.AppendFormat(" {} - {} imports\n", library->name,
                      library->imports.size());
      sb.AppendFormat("   Version: {}.{}.{}.{}\n", library->version.major,
                      library->version.minor, library->version.build,
                      library->version.qfe);
      sb.AppendFormat("   Min Version: {}.{}.{}.{}\n",
                      library->min_version.major, library->min_version.minor,
                      library->min_version.build, library->min_version.qfe);
      sb.Append("\n");

      // Counts.
      int known_count = 0;
      int unknown_count = 0;
      int impl_count = 0;
      int unimpl_count = 0;

      for (std::vector<cpu::XexModule::ImportLibraryFn>::const_iterator info =
               library->imports.begin();
           info != library->imports.end(); ++info) {
        if (kernel_state_->IsKernelModule(library->name)) {
          auto kernel_export =
              export_resolver->GetExportByOrdinal(library->name, info->ordinal);
          if (kernel_export) {
            known_count++;
            if (kernel_export->is_implemented()) {
              impl_count++;
            } else {
              unimpl_count++;
            }
          } else {
            unknown_count++;
            unimpl_count++;
          }
        } else {
          auto module = kernel_state_->GetModule(library->name);
          if (module) {
            uint32_t export_addr =
                module->GetProcAddressByOrdinal(info->ordinal);
            if (export_addr) {
              impl_count++;
              known_count++;
            } else {
              unimpl_count++;
              unknown_count++;
            }
          } else {
            unimpl_count++;
            unknown_count++;
          }
        }
      }
      float total_count = static_cast<float>(library->imports.size()) / 100.0f;
      sb.AppendFormat("         Total: {:4}\n", library->imports.size());
      sb.AppendFormat("         Known:  {:3}% ({} known, {} unknown)\n",
                      static_cast<int>(known_count / total_count), known_count,
                      unknown_count);
      sb.AppendFormat(
          "   Implemented:  {:3}% ({} implemented, {} unimplemented)\n",
          static_cast<int>(impl_count / total_count), impl_count, unimpl_count);
      sb.AppendFormat("\n");

      // Listing.
      for (std::vector<cpu::XexModule::ImportLibraryFn>::const_iterator info =
               library->imports.begin();
           info != library->imports.end(); ++info) {
        const char* name = "UNKNOWN";
        bool implemented = false;

        cpu::Export* kernel_export = nullptr;
        if (kernel_state_->IsKernelModule(library->name)) {
          kernel_export =
              export_resolver->GetExportByOrdinal(library->name, info->ordinal);
          if (kernel_export) {
            name = kernel_export->name;
            implemented = kernel_export->is_implemented();
          }
        } else {
          auto module = kernel_state_->GetModule(library->name);
          if (module && module->GetProcAddressByOrdinal(info->ordinal)) {
            // TODO(benvanik): name lookup.
            implemented = true;
          }
        }
        if (kernel_export &&
            kernel_export->type == cpu::Export::Type::kVariable) {
          sb.AppendFormat("   V {:08X}          {:03X} ({:4}) {} {}\n",
                          info->value_address, info->ordinal, info->ordinal,
                          implemented ? "  " : "!!", name);
        } else if (info->thunk_address) {
          sb.AppendFormat("   F {:08X} {:08X} {:03X} ({:4}) {} {}\n",
                          info->value_address, info->thunk_address,
                          info->ordinal, info->ordinal,
                          implemented ? "  " : "!!", name);
        }
      }
    }

    sb.Append("\n");
  }

  xe::logging::AppendLogLine(xe::LogLevel::Info, 'i', sb.to_string_view());
}

}  // namespace kernel
}  // namespace xe
