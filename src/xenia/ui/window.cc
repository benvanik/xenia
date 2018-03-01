/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/window.h"

#include <algorithm>

#include "third_party/imgui/imgui.h"
#include "xenia/base/assert.h"
#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/ui/imgui_drawer.h"

namespace xe {
namespace ui {

constexpr bool kContinuousRepaint = false;
constexpr bool kShowPresentFps = kContinuousRepaint;

// Enables long press behaviors (context menu, etc).
constexpr bool kTouch = false;

constexpr uint64_t kDoubleClickDelayMillis = 600;
constexpr double kDoubleClickDistance = 5;

constexpr int32_t kMouseWheelDetent = 120;

Window::Window(Loop* loop, const std::wstring& title)
    : loop_(loop), title_(title) {}

Window::~Window() {
  // Context must have been cleaned up already in OnDestroy.
  assert_null(context_.get());
}

void Window::AttachListener(WindowListener* listener) {
  if (in_listener_loop_) {
    pending_listener_attaches_.push_back(listener);
    return;
  }
  auto it = std::find(listeners_.begin(), listeners_.end(), listener);
  if (it != listeners_.end()) {
    return;
  }
  listeners_.push_back(listener);
  Invalidate();
}

void Window::DetachListener(WindowListener* listener) {
  if (in_listener_loop_) {
    pending_listener_detaches_.push_back(listener);
    return;
  }
  auto it = std::find(listeners_.begin(), listeners_.end(), listener);
  if (it == listeners_.end()) {
    return;
  }
  listeners_.erase(it);
}

void Window::ForEachListener(std::function<void(WindowListener*)> fn) {
  assert_false(in_listener_loop_);
  in_listener_loop_ = true;
  for (auto listener : listeners_) {
    fn(listener);
  }
  in_listener_loop_ = false;
  while (!pending_listener_attaches_.empty()) {
    auto listener = pending_listener_attaches_.back();
    pending_listener_attaches_.pop_back();
    AttachListener(listener);
  }
  while (!pending_listener_detaches_.empty()) {
    auto listener = pending_listener_detaches_.back();
    pending_listener_detaches_.pop_back();
    DetachListener(listener);
  }
}

void Window::TryForEachListener(std::function<bool(WindowListener*)> fn) {
  assert_false(in_listener_loop_);
  in_listener_loop_ = true;
  for (auto listener : listeners_) {
    if (fn(listener)) {
      break;
    }
  }
  in_listener_loop_ = false;
  while (!pending_listener_attaches_.empty()) {
    auto listener = pending_listener_attaches_.back();
    pending_listener_attaches_.pop_back();
    AttachListener(listener);
  }
  while (!pending_listener_detaches_.empty()) {
    auto listener = pending_listener_detaches_.back();
    pending_listener_detaches_.pop_back();
    DetachListener(listener);
  }
}

void Window::set_imgui_input_enabled(bool value) {
  if (value == is_imgui_input_enabled_) {
    return;
  }
  is_imgui_input_enabled_ = value;
  if (!value) {
    DetachListener(imgui_drawer_.get());
  } else {
    AttachListener(imgui_drawer_.get());
  }
}

bool Window::OnCreate() { return true; }

bool Window::MakeReady() {
  imgui_drawer_ = std::make_unique<xe::ui::ImGuiDrawer>(this);

  return true;
}

void Window::OnMainMenuChange() {
  ForEachListener([](auto listener) { listener->OnMainMenuChange(); });
}

void Window::OnClose() {
  UIEvent e(this);
  ForEachListener([&e](auto listener) { listener->OnClosing(&e); });
  on_closing(&e);
  ForEachListener([&e](auto listener) { listener->OnClosed(&e); });
  on_closed(&e);
}

void Window::OnDestroy() {
  if (!context_) {
    return;
  }

  imgui_drawer_.reset();

  // Context must go last.
  context_.reset();
}

void Window::Layout() {
  UIEvent e(this);
  OnLayout(&e);
}

void Window::Invalidate() {}

void Window::OnDpiChanged(UIEvent* e) {
  // TODO(DrChat): Notify listeners.
}

void Window::OnResize(UIEvent* e) {
  ForEachListener([e](auto listener) { listener->OnResize(e); });
}

void Window::OnLayout(UIEvent* e) {
  ForEachListener([e](auto listener) { listener->OnLayout(e); });
}

void Window::OnPaint(UIEvent* e) {
  if (!context_) {
    return;
  }
  ++frame_count_;
  ++fps_frame_count_;
  uint64_t now_ns = xe::Clock::QueryHostSystemTime();
  if (now_ns > fps_update_time_ns_ + 1000 * 10000) {
    fps_ = static_cast<uint32_t>(
        fps_frame_count_ /
        (static_cast<double>(now_ns - fps_update_time_ns_) / 10000000.0));
    fps_update_time_ns_ = now_ns;
    fps_frame_count_ = 0;
  }

  GraphicsContextLock context_lock(context_.get());

  // Prepare ImGui for use this frame.
  auto& io = imgui_drawer_->GetIO();
  if (!last_paint_time_ns_) {
    io.DeltaTime = 0.0f;
    last_paint_time_ns_ = now_ns;
  } else {
    io.DeltaTime = (now_ns - last_paint_time_ns_) / 10000000.0f;
    last_paint_time_ns_ = now_ns;
  }
  io.DisplaySize = ImVec2(static_cast<float>(scaled_width()),
                          static_cast<float>(scaled_height()));
  ImGui::NewFrame();

  context_->BeginSwap();
  if (context_->WasLost()) {
    on_context_lost(e);
    return;
  }

  ForEachListener([e](auto listener) { listener->OnPainting(e); });
  on_painting(e);
  ForEachListener([e](auto listener) { listener->OnPaint(e); });
  on_paint(e);

  // Flush ImGui buffers before we swap.
  ImGui::Render();

  ForEachListener([e](auto listener) { listener->OnPainted(e); });
  on_painted(e);

  context_->EndSwap();

  // If animations are running, reinvalidate immediately.
  if (kContinuousRepaint) {
    Invalidate();
  }
}

void Window::OnFileDrop(FileDropEvent* e) {
  on_file_drop(e);
  ForEachListener([e](auto listener) { listener->OnFileDrop(e); });
}

void Window::OnVisible(UIEvent* e) {
  ForEachListener([e](auto listener) { listener->OnVisible(e); });
}

void Window::OnHidden(UIEvent* e) {
  ForEachListener([e](auto listener) { listener->OnHidden(e); });
}

void Window::OnGotFocus(UIEvent* e) {
  ForEachListener([e](auto listener) { listener->OnGotFocus(e); });
}

void Window::OnLostFocus(UIEvent* e) {
  modifier_shift_pressed_ = false;
  modifier_cntrl_pressed_ = false;
  modifier_alt_pressed_ = false;
  modifier_super_pressed_ = false;
  ForEachListener([e](auto listener) { listener->OnLostFocus(e); });
}

void Window::OnKeyPress(KeyEvent* e, bool is_down, bool is_char) {
  if (!is_char) {
    switch (e->key()) {
      case ui::KeyEvent::Key::kLeftShift:
      case ui::KeyEvent::Key::kRightShift:
        modifier_shift_pressed_ = is_down;
        break;
      case ui::KeyEvent::Key::kLeftControl:
      case ui::KeyEvent::Key::kRightControl:
        modifier_cntrl_pressed_ = is_down;
        break;
      // case xx:
      //  // alt ??
      //  modifier_alt_pressed_ = is_down;
      //  break;
      case ui::KeyEvent::Key::kSuper:
        modifier_super_pressed_ = is_down;
        break;
      default:
        break;  // do nothing
    }
  }
}

void Window::OnKeyDown(KeyEvent* e) {
  on_key_down(e);
  if (e->is_handled()) {
    return;
  }
  TryForEachListener([e](auto listener) {
    listener->OnKeyDown(e);
    return e->is_handled();
  });
  OnKeyPress(e, true, false);
}

void Window::OnKeyUp(KeyEvent* e) {
  on_key_up(e);
  if (e->is_handled()) {
    return;
  }
  TryForEachListener([e](auto listener) {
    listener->OnKeyUp(e);
    return e->is_handled();
  });
  OnKeyPress(e, false, false);
}

void Window::OnKeyChar(KeyEvent* e) {
  OnKeyPress(e, true, true);
  on_key_char(e);
  ForEachListener([e](auto listener) { listener->OnKeyChar(e); });
  OnKeyPress(e, false, true);
}

void Window::OnMouseDown(MouseEvent* e) {
  on_mouse_down(e);
  if (e->is_handled()) {
    return;
  }
  TryForEachListener([e](auto listener) {
    listener->OnMouseDown(e);
    return e->is_handled();
  });
}

void Window::OnMouseMove(MouseEvent* e) {
  on_mouse_move(e);
  if (e->is_handled()) {
    return;
  }
  TryForEachListener([e](auto listener) {
    listener->OnMouseMove(e);
    return e->is_handled();
  });
}

void Window::OnMouseUp(MouseEvent* e) {
  on_mouse_up(e);
  if (e->is_handled()) {
    return;
  }
  TryForEachListener([e](auto listener) {
    listener->OnMouseUp(e);
    return e->is_handled();
  });
}

void Window::OnMouseWheel(MouseEvent* e) {
  on_mouse_wheel(e);
  if (e->is_handled()) {
    return;
  }
  TryForEachListener([e](auto listener) {
    listener->OnMouseWheel(e);
    return e->is_handled();
  });
}

}  // namespace ui
}  // namespace xe
