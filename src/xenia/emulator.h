/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_EMULATOR_H_
#define XENIA_EMULATOR_H_

#include <functional>
#include <optional>
#include <string>

#include "xenia/base/delegate.h"
#include "xenia/base/exception_handler.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/memory.h"
#include "xenia/patcher/patcher.h"
#include "xenia/vfs/virtual_file_system.h"
#include "xenia/xbox.h"

namespace xe {
namespace apu {
class AudioSystem;
}  // namespace apu
namespace cpu {
class ExportResolver;
class Processor;
class ThreadState;
}  // namespace cpu
namespace gpu {
class GraphicsSystem;
}  // namespace gpu
namespace hid {
class InputDriver;
class InputSystem;
}  // namespace hid
namespace ui {
class Window;
}  // namespace ui
}  // namespace xe

namespace xe {

constexpr fourcc_t kEmulatorSaveSignature = make_fourcc("XSAV");

// The main type that runs the whole emulator.
// This is responsible for initializing and managing all the various subsystems.
class Emulator {
 public:
  explicit Emulator(const std::filesystem::path& command_line,
                    const std::filesystem::path& storage_root,
                    const std::filesystem::path& content_root,
                    const std::filesystem::path& cache_root);
  ~Emulator();

  // Full command line used when launching the process.
  const std::filesystem::path& command_line() const { return command_line_; }

  // Folder persistent internal emulator data is stored in.
  const std::filesystem::path& storage_root() const { return storage_root_; }

  // Folder guest content is stored in.
  const std::filesystem::path& content_root() const { return content_root_; }

  // Folder files safe to remove without significant side effects are stored in.
  const std::filesystem::path& cache_root() const { return cache_root_; }

  // Name of the title in the default language.
  const std::string& title_name() const { return title_name_; }

  // Version of the title as a string.
  const std::string& title_version() const { return title_version_; }

  // Currently running title ID
  uint32_t title_id() const {
    return !title_id_.has_value() ? 0 : title_id_.value();
  }

  // Are we currently running a title?
  bool is_title_open() const { return title_id_.has_value(); }

  // Window used for displaying graphical output.
  ui::Window* display_window() const { return display_window_; }

  // Guest memory system modelling the RAM (both virtual and physical) of the
  // system.
  Memory* memory() const { return memory_.get(); }

  // Virtualized processor that can execute PPC code.
  cpu::Processor* processor() const { return processor_.get(); }

  // Audio hardware emulation for decoding and playback.
  apu::AudioSystem* audio_system() const { return audio_system_.get(); }

  // GPU emulation for command list processing.
  gpu::GraphicsSystem* graphics_system() const {
    return graphics_system_.get();
  }

  // Human-interface Device (HID) adapters for controllers.
  hid::InputSystem* input_system() const { return input_system_.get(); }

  // Kernel function export table used to resolve exports when JITing code.
  cpu::ExportResolver* export_resolver() const {
    return export_resolver_.get();
  }

  // File systems mapped to disc images, folders, etc for games and save data.
  vfs::VirtualFileSystem* file_system() const { return file_system_.get(); }

  // The 'kernel', tracking all kernel objects and other state.
  // This is effectively the guest operating system.
  kernel::KernelState* kernel_state() const { return kernel_state_.get(); }

  patcher::PatchingSystem* patching_system() const {
    return patching_system_.get();
  }

  // Initializes the emulator and configures all components.
  // The given window is used for display and the provided functions are used
  // to create subsystems as required.
  // Once this function returns a game can be launched using one of the Launch
  // functions.
  X_STATUS Setup(
      ui::Window* display_window,
      std::function<std::unique_ptr<apu::AudioSystem>(cpu::Processor*)>
          audio_system_factory,
      std::function<std::unique_ptr<gpu::GraphicsSystem>()>
          graphics_system_factory,
      std::function<std::vector<std::unique_ptr<hid::InputDriver>>(ui::Window*)>
          input_driver_factory);

  // Terminates the currently running title.
  X_STATUS TerminateTitle();

  // Launches a game from the given file path.
  // This will attempt to infer the type of the given file (such as an iso, etc)
  // using heuristics.
  X_STATUS LaunchPath(const std::filesystem::path& path);

  // Launches a game from a .xex file by mounting the containing folder as if it
  // was an extracted STFS container.
  X_STATUS LaunchXexFile(const std::filesystem::path& path);

  // Launches a game from a disc image file (.iso, etc).
  X_STATUS LaunchDiscImage(const std::filesystem::path& path);

  // Launches a game from an STFS container file.
  X_STATUS LaunchStfsContainer(const std::filesystem::path& path);

  void Pause();
  void Resume();
  bool is_paused() const { return paused_; }

  bool SaveToFile(const std::filesystem::path& path);
  bool RestoreFromFile(const std::filesystem::path& path);

  // The game can request another title to be loaded.
  bool TitleRequested();
  void LaunchNextTitle();

  void WaitUntilExit();

 public:
  xe::Delegate<uint32_t, const std::string_view> on_launch;
  xe::Delegate<bool> on_shader_storage_initialization;
  xe::Delegate<> on_terminate;
  xe::Delegate<> on_exit;

 private:
  static bool ExceptionCallbackThunk(Exception* ex, void* data);
  bool ExceptionCallback(Exception* ex);

  std::string FindLaunchModule();

  X_STATUS CompleteLaunch(const std::filesystem::path& path,
                          const std::string_view module_path);

  std::filesystem::path command_line_;
  std::filesystem::path storage_root_;
  std::filesystem::path content_root_;
  std::filesystem::path cache_root_;

  std::string title_name_;
  std::string title_version_;

  ui::Window* display_window_;

  std::unique_ptr<Memory> memory_;

  std::unique_ptr<cpu::Processor> processor_;
  std::unique_ptr<apu::AudioSystem> audio_system_;
  std::unique_ptr<gpu::GraphicsSystem> graphics_system_;
  std::unique_ptr<hid::InputSystem> input_system_;

  std::unique_ptr<cpu::ExportResolver> export_resolver_;
  std::unique_ptr<vfs::VirtualFileSystem> file_system_;
  std::unique_ptr<patcher::PatchingSystem> patching_system_;

  std::unique_ptr<kernel::KernelState> kernel_state_;
  kernel::object_ref<kernel::XThread> main_thread_;
  std::optional<uint32_t> title_id_;  // Currently running title ID

  bool paused_;
  bool restoring_;
  threading::Fence restore_fence_;  // Fired on restore finish.
};

}  // namespace xe

#endif  // XENIA_EMULATOR_H_
