/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <gflags/gflags.h>

#include <string>

// NOTE: this must be included before microprofile as macro expansion needs
// XELOGI.
#include "xenia/base/logging.h"

// NOTE: microprofile must be setup first, before profiling.h is included.
#define MICROPROFILE_ENABLED 1
#define MICROPROFILEUI_ENABLED 1
#define MICROPROFILE_IMPL 1
#define MICROPROFILEUI_IMPL 1
#define MICROPROFILE_PER_THREAD_BUFFER_SIZE (1024 * 1024 * 10)
#define MICROPROFILE_USE_THREAD_NAME_CALLBACK 1
#define MICROPROFILE_WEBSERVER_MAXFRAMES 3
#define MICROPROFILE_PRINTF XELOGI
#define MICROPROFILE_WEBSERVER 0
#define MICROPROFILE_DEBUG 0
#define MICROPROFILE_MAX_THREADS 128
#include "third_party/microprofile/microprofile.h"

#include "xenia/base/assert.h"
#include "xenia/base/profiling.h"
#include "xenia/ui/window.h"

#if XE_OPTION_PROFILING
#include "third_party/microprofile/microprofileui.h"
#endif  // XE_OPTION_PROFILING

#if XE_OPTION_PROFILING_UI
#undef DrawText
#include "xenia/ui/microprofile_drawer.h"
#endif  // XE_OPTION_PROFILING_UI

DEFINE_bool(show_profiler, false, "Show profiling UI by default.");

namespace xe {

#if XE_OPTION_PROFILING_UI
ui::Window* Profiler::window_ = nullptr;
std::unique_ptr<ui::MicroprofileDrawer> Profiler::drawer_ = nullptr;
#endif  // XE_OPTION_PROFILING_UI

#if XE_OPTION_PROFILING

bool Profiler::is_enabled() { return true; }

bool Profiler::is_visible() { return is_enabled() && MicroProfileIsDrawing(); }

void Profiler::Initialize() {
  // Custom groups.
  MicroProfileSetEnableAllGroups(false);
  MicroProfileForceEnableGroup("apu", MicroProfileTokenTypeCpu);
  MicroProfileForceEnableGroup("cpu", MicroProfileTokenTypeCpu);
  MicroProfileForceEnableGroup("gpu", MicroProfileTokenTypeCpu);
  MicroProfileForceEnableGroup("internal", MicroProfileTokenTypeCpu);
  g_MicroProfile.nGroupMask = g_MicroProfile.nForceGroup;
  g_MicroProfile.nActiveGroup = g_MicroProfile.nActiveGroupWanted =
      g_MicroProfile.nGroupMask;

  // Custom timers: time, average.
  g_MicroProfile.nBars |= 0x1 | 0x2;
  g_MicroProfile.nActiveBars |= 0x1 | 0x2;

#if XE_OPTION_PROFILING_UI
  MicroProfileInitUI();
  g_MicroProfileUI.bShowSpikes = true;
  g_MicroProfileUI.nOpacityBackground = 0x40u << 24;
  g_MicroProfileUI.nOpacityForeground = 0xc0u << 24;
  if (FLAGS_show_profiler) {
    MicroProfileSetDisplayMode(1);
  }
#else
  MicroProfileSetForceEnable(true);
  MicroProfileSetEnableAllGroups(true);
  MicroProfileSetForceMetaCounters(false);
#endif  // XE_OPTION_PROFILING_UI
}

void Profiler::Dump() {
#if XE_OPTION_PROFILING_UI
  MicroProfileDumpTimers();
#endif  // XE_OPTION_PROFILING_UI
  MicroProfileDumpHtml("profile.html");
  MicroProfileDumpHtmlToFile();
}

void Profiler::Shutdown() {
  drawer_.reset();
  window_ = nullptr;
  MicroProfileShutdown();
}

uint32_t Profiler::GetColor(const char* str) {
  std::hash<std::string> fn;
  size_t value = fn(str);
  return value & 0xFFFFFF;
}

void Profiler::ThreadEnter(const char* name) {
  MicroProfileOnThreadCreate(name);
}

void Profiler::ThreadExit() { MicroProfileOnThreadExit(); }

bool Profiler::OnKeyDown(ui::KeyEvent* e) {
  switch (e->key()) {
    case xe::ui::KeyEvent::Key::kTick:  // `
      MicroProfileTogglePause();
      return true;
#if XE_OPTION_PROFILING_UI
    case xe::ui::KeyEvent::Key::kTab:  // VK_TAB:
      MicroProfileToggleDisplayMode();
      return true;
    case xe::ui::KeyEvent::Key::k1:  // 1
      MicroProfileModKey(1);
      return true;
#endif  // XE_OPTION_PROFILING_UI
    default:
      return false;
  }
  return false;
}

bool Profiler::OnKeyUp(ui::KeyEvent* e) {
  switch (e->key()) {
#if XE_OPTION_PROFILING_UI
    case xe::ui::KeyEvent::Key::k1:  // 1
      MicroProfileModKey(0);
      return true;
#endif  // XE_OPTION_PROFILING_UI
    default:
      return false;
  }
  return false;
}

#if XE_OPTION_PROFILING_UI

void Profiler::OnMouseDown(bool left_button, bool right_button) {
  MicroProfileMouseButton(left_button, right_button);
}

void Profiler::OnMouseUp() { MicroProfileMouseButton(0, 0); }

void Profiler::OnMouseMove(int x, int y) { MicroProfileMousePosition(x, y, 0); }

void Profiler::OnMouseWheel(int x, int y, int dy) {
  MicroProfileMousePosition(x, y, dy);
}

void Profiler::ToggleDisplay() { MicroProfileToggleDisplayMode(); }

void Profiler::TogglePause() { MicroProfileTogglePause(); }

#else

void Profiler::OnMouseDown(bool left_button, bool right_button) {}

void Profiler::OnMouseUp() {}

void Profiler::OnMouseMove(int x, int y) {}

void Profiler::OnMouseWheel(int x, int y, int dy) {}

void Profiler::ToggleDisplay() {}

void Profiler::TogglePause() {}

#endif  // XE_OPTION_PROFILING_UI

void Profiler::set_window(ui::Window* window) {
  assert_null(window_);
  if (!window) {
    return;
  }
  window_ = window;
  drawer_ = std::make_unique<ui::MicroprofileDrawer>(window);

  window_->on_painted.AddListener([](ui::UIEvent* e) { Profiler::Present(); });

  // Pass through mouse events.
  window_->on_mouse_down.AddListener([](ui::MouseEvent* e) {
    if (Profiler::is_visible()) {
      Profiler::OnMouseDown(e->button() == ui::MouseEvent::Button::kLeft,
                            e->button() == ui::MouseEvent::Button::kRight);
      e->set_handled(true);
      window_->Invalidate();
    }
  });
  window_->on_mouse_up.AddListener([](ui::MouseEvent* e) {
    if (Profiler::is_visible()) {
      Profiler::OnMouseUp();
      e->set_handled(true);
      window_->Invalidate();
    }
  });
  window_->on_mouse_move.AddListener([](ui::MouseEvent* e) {
    if (Profiler::is_visible()) {
      Profiler::OnMouseMove(e->x(), e->y());
      e->set_handled(true);
      window_->Invalidate();
    }
  });
  window_->on_mouse_wheel.AddListener([](ui::MouseEvent* e) {
    if (Profiler::is_visible()) {
      Profiler::OnMouseWheel(e->x(), e->y(), -e->dy());
      e->set_handled(true);
      window_->Invalidate();
    }
  });

  // Watch for toggle/mode keys and such.
  window_->on_key_down.AddListener([](ui::KeyEvent* e) {
    if (Profiler::is_visible()) {
      Profiler::OnKeyDown(e);
      e->set_handled(true);
      window_->Invalidate();
    }
  });
  window_->on_key_up.AddListener([](ui::KeyEvent* e) {
    if (Profiler::is_visible()) {
      Profiler::OnKeyUp(e);
      e->set_handled(true);
      window_->Invalidate();
    }
  });
}

void Profiler::Present() {
  SCOPE_profile_cpu_f("internal");
#if XE_OPTION_PROFILING_UI
  if (!window_ || !drawer_) {
    return;
  }
  drawer_->Begin();
  MicroProfileDraw(window_->scaled_width(), window_->scaled_height());
  drawer_->End();
#endif  // XE_OPTION_PROFILING_UI
}

void Profiler::Flip() { MicroProfileFlip(); }

#else

bool Profiler::is_enabled() { return false; }
bool Profiler::is_visible() { return false; }
void Profiler::Initialize() {}
void Profiler::Dump() {}
void Profiler::Shutdown() {}
uint32_t Profiler::GetColor(const char* str) { return 0; }
void Profiler::ThreadEnter(const char* name) {}
void Profiler::ThreadExit() {}
bool Profiler::OnKeyDown(ui::KeyEvent* e) { return false; }
bool Profiler::OnKeyUp(ui::KeyEvent* e) { return false; }
void Profiler::OnMouseDown(bool left_button, bool right_button) {}
void Profiler::OnMouseUp() {}
void Profiler::OnMouseMove(int x, int y) {}
void Profiler::OnMouseWheel(int x, int y, int dy) {}
void Profiler::ToggleDisplay() {}
void Profiler::TogglePause() {}
void Profiler::set_window(ui::Window* window) {}
void Profiler::Present() {}
void Profiler::Flip() {}

#endif  // XE_OPTION_PROFILING

}  // namespace xe

uint32_t MicroProfileGpuInsertTimeStamp() { return 0; }

uint64_t MicroProfileGpuGetTimeStamp(uint32_t nKey) { return 0; }

uint64_t MicroProfileTicksPerSecondGpu() { return 0; }

const char* MicroProfileGetThreadName() { return "TODO: get thread name!"; }

#if XE_OPTION_PROFILING
#if XE_OPTION_PROFILING_UI

void MicroProfileDrawBox(int nX, int nY, int nX1, int nY1, uint32_t nColor,
                         MicroProfileBoxType type) {
  auto drawer = xe::Profiler::drawer();
  if (!drawer) {
    return;
  }
  drawer->DrawBox(nX, nY, nX1, nY1, nColor,
                  static_cast<xe::ui::MicroprofileDrawer::BoxType>(type));
}

void MicroProfileDrawLine2D(uint32_t nVertices, float* pVertices,
                            uint32_t nColor) {
  auto drawer = xe::Profiler::drawer();
  if (!drawer) {
    return;
  }
  drawer->DrawLine2D(nVertices, pVertices, nColor);
}

void MicroProfileDrawText(int nX, int nY, uint32_t nColor, const char* pText,
                          uint32_t nLen) {
  auto drawer = xe::Profiler::drawer();
  if (!drawer) {
    return;
  }
  drawer->DrawText(nX, nY, nColor, pText, nLen);
}

#endif  // XE_OPTION_PROFILING_UI

#endif  // XE_OPTION_PROFILING
