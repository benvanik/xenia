/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_XAM_MODULE_H_
#define XENIA_KERNEL_XAM_XAM_MODULE_H_

#include <string>

#include "xenia/cpu/export_resolver.h"
#include "xenia/kernel/kernel_module.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xam/xam_ordinals.h"

namespace xe {
namespace kernel {
namespace xam {

bool xeXamIsUIActive();

class XamModule : public KernelModule {
 public:
  XamModule(Emulator* emulator, KernelState* kernel_state);
  virtual ~XamModule();

  static void RegisterExportTable(xe::cpu::ExportResolver* export_resolver);

  struct LoaderData {
    bool launch_data_present = false;
    std::vector<uint8_t> launch_data;
    uint32_t launch_flags = 0;
    std::string launch_path;  // Full path to next xex
  };

  const LoaderData& loader_data() const { return loader_data_; }
  LoaderData& loader_data() { return loader_data_; }

  uint32_t llf_data_address() const { return llf_data_address_; }
  const std::vector<uint32_t>& llf_entry_addresses() const {
    return llf_entry_addresses_;
  }

 private:
  LoaderData loader_data_;
  uint32_t llf_data_address_;
  std::vector<uint32_t> llf_entry_addresses_;
};

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_XAM_MODULE_H_
