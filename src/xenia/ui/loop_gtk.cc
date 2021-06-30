/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/loop_gtk.h"

#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include "xenia/base/assert.h"

namespace xe {
namespace ui {

std::unique_ptr<Loop> Loop::Create() { return std::make_unique<GTKLoop>(); }

GTKLoop::GTKLoop() : thread_id_() {
  gtk_init(nullptr, nullptr);
  xe::threading::Fence init_fence;
  thread_ = std::thread([&init_fence, this]() {
    xe::threading::set_name("GTK Loop");

    thread_id_ = std::this_thread::get_id();
    init_fence.Signal();

    ThreadMain();

    quit_fence_.Signal();
  });
  init_fence.Wait();
}

GTKLoop::~GTKLoop() {
  Quit();
  thread_.join();
}

void GTKLoop::ThreadMain() { gtk_main(); }

bool GTKLoop::is_on_loop_thread() {
  return thread_id_ == std::this_thread::get_id();
}

gboolean _posted_fn_thunk(gpointer posted_fn) {
  // convert gpointer back to std::function, call it, then free std::function
  std::function<void()>* f =
      reinterpret_cast<std::function<void()>*>(posted_fn);
  std::function<void()>& func = *f;
  func();
  delete f;
  // Tells GDK we don't want to run this again
  return G_SOURCE_REMOVE;
}

void GTKLoop::Post(std::function<void()> fn) {
  assert_true(thread_id_ != std::thread::id());
  // converting std::function to a generic pointer for gdk
  gdk_threads_add_idle(_posted_fn_thunk, reinterpret_cast<gpointer>(
                                             new std::function<void()>(fn)));
}

void GTKLoop::PostDelayed(std::function<void()> fn, uint64_t delay_millis) {
  gdk_threads_add_timeout(
      delay_millis, _posted_fn_thunk,
      reinterpret_cast<gpointer>(new std::function<void()>(fn)));
}

void GTKLoop::Quit() {
  assert_true(thread_id_ != std::thread::id());
  Post([]() { gtk_main_quit(); });
}

void GTKLoop::AwaitQuit() { quit_fence_.Wait(); }

}  // namespace ui
}  // namespace xe
