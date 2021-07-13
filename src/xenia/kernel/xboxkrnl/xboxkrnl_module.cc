/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xboxkrnl/xboxkrnl_module.h"

#include <vector>

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/base/clock.h"
#include "xenia/base/debugging.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/cpu/ppc/ppc_context.h"
#include "xenia/cpu/processor.h"
#include "xenia/emulator.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/user_module.h"
#include "xenia/kernel/xboxkrnl/cert_monitor.h"
#include "xenia/kernel/xboxkrnl/debug_monitor.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_private.h"
#include "xenia/kernel/xthread.h"

DEFINE_string(cl, "", "Specify additional command-line provided to guest.",
              "Kernel");

DEFINE_bool(kernel_debug_monitor, false, "Enable debug monitor.", "Kernel");
DEFINE_bool(kernel_cert_monitor, false, "Enable cert monitor.", "Kernel");
DEFINE_bool(kernel_pix, false, "Enable PIX.", "Kernel");

namespace xe {
namespace kernel {
namespace xboxkrnl {

bool XboxkrnlModule::SendPIXCommand(const char* cmd) {
  if (!cvars::kernel_pix) {
    return false;
  }

  auto global_lock = global_critical_region_.Acquire();
  if (!XThread::IsInThread()) {
    assert_always();
    return false;
  }

  uint32_t scratch_size = 260 + 260;
  auto scratch_ptr = memory_->SystemHeapAlloc(scratch_size);
  auto scratch = memory_->TranslateVirtual(scratch_ptr);
  std::memset(scratch, 0, scratch_size);

  auto response = reinterpret_cast<char*>(scratch + 0);
  auto command = reinterpret_cast<char*>(scratch + 260);

  fmt::format_to_n(command, 259, "PIX!m!{}", cmd);

  global_lock.unlock();
  uint64_t args[] = {scratch_ptr + 260, scratch_ptr, 260};
  auto result = kernel_state_->processor()->Execute(
      XThread::GetCurrentThread()->thread_state(), pix_function_, args,
      xe::countof(args));
  global_lock.lock();

  XELOGD("PIX(command): {}", cmd);
  XELOGD("PIX(response): {}", response);

  memory_->SystemHeapFree(scratch_ptr);

  if (XSUCCEEDED(result)) {
    return true;
  }

  return false;
}

XboxkrnlModule::XboxkrnlModule(Emulator* emulator, KernelState* kernel_state)
    : KernelModule(kernel_state, "xe:\\xboxkrnl.exe"),
      timestamp_timer_(nullptr) {
  RegisterExportTable(export_resolver_);

  // Register all exported functions.
  RegisterAudioExports(export_resolver_, kernel_state_);
  RegisterAudioXmaExports(export_resolver_, kernel_state_);
  RegisterCryptExports(export_resolver_, kernel_state_);
  RegisterDebugExports(export_resolver_, kernel_state_);
  RegisterErrorExports(export_resolver_, kernel_state_);
  RegisterHalExports(export_resolver_, kernel_state_);
  RegisterHidExports(export_resolver_, kernel_state_);
  RegisterIoExports(export_resolver_, kernel_state_);
  RegisterIoInfoExports(export_resolver_, kernel_state_);
  RegisterMemoryExports(export_resolver_, kernel_state_);
  RegisterMiscExports(export_resolver_, kernel_state_);
  RegisterModuleExports(export_resolver_, kernel_state_);
  RegisterObExports(export_resolver_, kernel_state_);
  RegisterRtlExports(export_resolver_, kernel_state_);
  RegisterStringExports(export_resolver_, kernel_state_);
  RegisterThreadingExports(export_resolver_, kernel_state_);
  RegisterUsbcamExports(export_resolver_, kernel_state_);
  RegisterVideoExports(export_resolver_, kernel_state_);
  RegisterXConfigExports(export_resolver_, kernel_state_);

  // KeDebugMonitorData (?*)
  // Set to a valid value when a remote debugger is attached.
  // Offset 0x18 is a 4b pointer to a handler function that seems to take two
  // arguments. If we wanted to see what would happen we could fake that.
  uint32_t pKeDebugMonitorData;
  if (!cvars::kernel_debug_monitor) {
    pKeDebugMonitorData = memory_->SystemHeapAlloc(4);
    auto lpKeDebugMonitorData = memory_->TranslateVirtual(pKeDebugMonitorData);
    xe::store_and_swap<uint32_t>(lpKeDebugMonitorData, 0);
  } else {
    pKeDebugMonitorData =
        memory_->SystemHeapAlloc(4 + sizeof(X_KEDEBUGMONITORDATA));
    xe::store_and_swap<uint32_t>(memory_->TranslateVirtual(pKeDebugMonitorData),
                                 pKeDebugMonitorData + 4);
    auto lpKeDebugMonitorData =
        memory_->TranslateVirtual<X_KEDEBUGMONITORDATA*>(pKeDebugMonitorData +
                                                         4);
    std::memset(lpKeDebugMonitorData, 0, sizeof(X_KEDEBUGMONITORDATA));
    lpKeDebugMonitorData->callback_fn =
        GenerateTrampoline("KeDebugMonitorCallback", KeDebugMonitorCallback);
  }
  export_resolver_->SetVariableMapping(
      "xboxkrnl.exe", ordinals::KeDebugMonitorData, pKeDebugMonitorData);

  // KeCertMonitorData (?*)
  // Always set to zero, ignored.
  uint32_t pKeCertMonitorData;
  if (!cvars::kernel_cert_monitor) {
    pKeCertMonitorData = memory_->SystemHeapAlloc(4);
    auto lpKeCertMonitorData = memory_->TranslateVirtual(pKeCertMonitorData);
    xe::store_and_swap<uint32_t>(lpKeCertMonitorData, 0);
  } else {
    pKeCertMonitorData =
        memory_->SystemHeapAlloc(4 + sizeof(X_KECERTMONITORDATA));
    xe::store_and_swap<uint32_t>(memory_->TranslateVirtual(pKeCertMonitorData),
                                 pKeCertMonitorData + 4);
    auto lpKeCertMonitorData =
        memory_->TranslateVirtual<X_KECERTMONITORDATA*>(pKeCertMonitorData + 4);
    std::memset(lpKeCertMonitorData, 0, sizeof(X_KECERTMONITORDATA));
    lpKeCertMonitorData->callback_fn =
        GenerateTrampoline("KeCertMonitorCallback", KeCertMonitorCallback);
  }
  export_resolver_->SetVariableMapping(
      "xboxkrnl.exe", ordinals::KeCertMonitorData, pKeCertMonitorData);

  // XboxHardwareInfo (XboxHardwareInfo_t, 16b)
  // flags       cpu#  ?     ?     ?     ?           ?       ?
  // 0x00000000, 0x06, 0x00, 0x00, 0x00, 0x00000000, 0x0000, 0x0000
  // Games seem to check if bit 26 (0x20) is set, which at least for xbox1
  // was whether an HDD was present. Not sure what the other flags are.
  //
  // aomega08 says the value is 0x02000817, bit 27: debug mode on.
  // When that is set, though, allocs crash in weird ways.
  //
  // From kernel dissasembly, after storage is initialized
  // XboxHardwareInfo flags is set with flag 5 (0x20).
  uint32_t pXboxHardwareInfo = memory_->SystemHeapAlloc(16);
  auto lpXboxHardwareInfo = memory_->TranslateVirtual(pXboxHardwareInfo);
  export_resolver_->SetVariableMapping(
      "xboxkrnl.exe", ordinals::XboxHardwareInfo, pXboxHardwareInfo);
  xe::store_and_swap<uint32_t>(lpXboxHardwareInfo + 0, 0x20);  // flags
  xe::store_and_swap<uint8_t>(lpXboxHardwareInfo + 4, 0x06);   // cpu count
  // Remaining 11b are zeroes?

  // ExConsoleGameRegion, probably same values as keyvault region uses?
  // Just return all 0xFF, should satisfy anything that checks it
  uint32_t pExConsoleGameRegion = memory_->SystemHeapAlloc(4);
  auto lpExConsoleGameRegion = memory_->TranslateVirtual(pExConsoleGameRegion);
  export_resolver_->SetVariableMapping(
      "xboxkrnl.exe", ordinals::ExConsoleGameRegion, pExConsoleGameRegion);
  xe::store<uint32_t>(lpExConsoleGameRegion, 0xFFFFFFFF);

  // XexExecutableModuleHandle (?**)
  // Games try to dereference this to get a pointer to some module struct.
  // So far it seems like it's just in loader code, and only used to look up
  // the XexHeaderBase for use by RtlImageXexHeaderField.
  // We fake it so that the address passed to that looks legit.
  // 0x80100FFC <- pointer to structure
  // 0x80101000 <- our module structure
  // 0x80101058 <- pointer to xex header
  // 0x80101100 <- xex header base
  uint32_t ppXexExecutableModuleHandle = memory_->SystemHeapAlloc(4);
  export_resolver_->SetVariableMapping("xboxkrnl.exe",
                                       ordinals::XexExecutableModuleHandle,
                                       ppXexExecutableModuleHandle);

  // ExLoadedImageName (char*)
  // The full path to loaded image/xex including its name.
  // Used usually in custom dashboards (Aurora)
  // Todo(Gliniak): Confirm that official kernel always allocate space for this
  // variable.
  uint32_t ppExLoadedImageName =
      memory_->SystemHeapAlloc(kExLoadedImageNameSize);
  export_resolver_->SetVariableMapping(
      "xboxkrnl.exe", ordinals::ExLoadedImageName, ppExLoadedImageName);

  // ExLoadedCommandLine (char*)
  // The name of the xex. Not sure this is ever really used on real devices.
  // Perhaps it's how swap disc/etc data is sent?
  // Always set to "default.xex" (with quotes) for now.
  // TODO(gibbed): set this to the actual module name.
  std::string command_line("\"default.xex\"");
  if (cvars::cl.length()) {
    command_line += " " + cvars::cl;
  }
  uint32_t command_line_length =
      xe::align(static_cast<uint32_t>(command_line.length()) + 1, 1024u);
  uint32_t pExLoadedCommandLine = memory_->SystemHeapAlloc(command_line_length);
  auto lpExLoadedCommandLine = memory_->TranslateVirtual(pExLoadedCommandLine);
  export_resolver_->SetVariableMapping(
      "xboxkrnl.exe", ordinals::ExLoadedCommandLine, pExLoadedCommandLine);
  std::memset(lpExLoadedCommandLine, 0, command_line_length);
  std::memcpy(lpExLoadedCommandLine, command_line.c_str(),
              command_line.length());

  // XboxKrnlVersion (8b)
  // Kernel version, looks like 2b.2b.2b.2b.
  // I've only seen games check >=, so we just fake something here.
  uint32_t pXboxKrnlVersion = memory_->SystemHeapAlloc(8);
  auto lpXboxKrnlVersion = memory_->TranslateVirtual(pXboxKrnlVersion);
  export_resolver_->SetVariableMapping(
      "xboxkrnl.exe", ordinals::XboxKrnlVersion, pXboxKrnlVersion);
  xe::store_and_swap<uint16_t>(lpXboxKrnlVersion + 0, 2);
  xe::store_and_swap<uint16_t>(lpXboxKrnlVersion + 2, 0xFFFF);
  xe::store_and_swap<uint16_t>(lpXboxKrnlVersion + 4, 0xFFFF);
  xe::store_and_swap<uint8_t>(lpXboxKrnlVersion + 6, 0x80);
  xe::store_and_swap<uint8_t>(lpXboxKrnlVersion + 7, 0x00);

  // KeTimeStampBundle (ad)
  // This must be updated during execution, at 1ms intevals.
  // We setup a system timer here to do that.
  uint32_t pKeTimeStampBundle = memory_->SystemHeapAlloc(24);
  auto lpKeTimeStampBundle = memory_->TranslateVirtual(pKeTimeStampBundle);
  export_resolver_->SetVariableMapping(
      "xboxkrnl.exe", ordinals::KeTimeStampBundle, pKeTimeStampBundle);
  xe::store_and_swap<uint64_t>(lpKeTimeStampBundle + 0, 0);
  xe::store_and_swap<uint64_t>(lpKeTimeStampBundle + 8, 0);
  xe::store_and_swap<uint32_t>(lpKeTimeStampBundle + 16,
                               Clock::QueryGuestUptimeMillis());
  xe::store_and_swap<uint32_t>(lpKeTimeStampBundle + 20, 0);
  timestamp_timer_ = xe::threading::HighResolutionTimer::CreateRepeating(
      std::chrono::milliseconds(1), [lpKeTimeStampBundle]() {
        xe::store_and_swap<uint32_t>(lpKeTimeStampBundle + 16,
                                     Clock::QueryGuestUptimeMillis());
      });
}

static auto& get_xboxkrnl_exports() {
  static std::vector<xe::cpu::Export*> xboxkrnl_exports(4096);
  return xboxkrnl_exports;
}

xe::cpu::Export* RegisterExport_xboxkrnl(xe::cpu::Export* export_entry) {
  auto& xboxkrnl_exports = get_xboxkrnl_exports();
  assert_true(export_entry->ordinal < xboxkrnl_exports.size());
  xboxkrnl_exports[export_entry->ordinal] = export_entry;
  return export_entry;
}

void XboxkrnlModule::RegisterExportTable(
    xe::cpu::ExportResolver* export_resolver) {
  assert_not_null(export_resolver);

// Build the export table used for resolution.
#include "xenia/kernel/util/export_table_pre.inc"
  static xe::cpu::Export xboxkrnl_export_table[] = {
#include "xenia/kernel/xboxkrnl/xboxkrnl_table.inc"
  };
#include "xenia/kernel/util/export_table_post.inc"
  auto& xboxkrnl_exports = get_xboxkrnl_exports();
  for (size_t i = 0; i < xe::countof(xboxkrnl_export_table); ++i) {
    auto& export_entry = xboxkrnl_export_table[i];
    assert_true(export_entry.ordinal < xboxkrnl_exports.size());
    if (!xboxkrnl_exports[export_entry.ordinal]) {
      xboxkrnl_exports[export_entry.ordinal] = &export_entry;
    }
  }
  export_resolver->RegisterTable("xboxkrnl.exe", &xboxkrnl_exports);
}

XboxkrnlModule::~XboxkrnlModule() = default;

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe
