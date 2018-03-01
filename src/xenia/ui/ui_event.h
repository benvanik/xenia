/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_UI_EVENT_H_
#define XENIA_UI_UI_EVENT_H_

namespace xe {
namespace ui {

class Window;

class UIEvent {
 public:
  explicit UIEvent(Window* target = nullptr) : target_(target) {}
  virtual ~UIEvent() = default;

  Window* target() const { return target_; }

 private:
  Window* target_ = nullptr;
};

class FileDropEvent : public UIEvent {
 public:
  FileDropEvent(Window* target, wchar_t* filename)
      : UIEvent(target), filename_(filename) {}
  ~FileDropEvent() override = default;

  wchar_t* filename() const { return filename_; }

 private:
  wchar_t* filename_ = nullptr;
};

class KeyEvent : public UIEvent {
 public:
  // TODO(dougvj) The list of keycodes is probably incomplete but may be
  // sufficient. Maybe there is a better way?
  enum class Key {
    kNone = 0,
    kEsc,
    kF1,
    kF2,
    kF3,
    kF4,
    kF5,
    kF6,
    kF7,
    kF8,
    kF9,
    kF10,
    kF11,
    kF12,
    kTick,
    k1,
    k2,
    k3,
    k4,
    k5,
    k6,
    k7,
    k8,
    k9,
    k0,
    kMinus,
    kEquals,
    kBackspace,
    kTab,
    kQ,
    kW,
    kE,
    kR,
    kT,
    kY,
    kU,
    kI,
    kO,
    kP,
    kLeftBracket,
    kRightBracket,
    kBackSlash,
    kCapsLock,
    kA,
    kS,
    kD,
    kF,
    kG,
    kH,
    kJ,
    kK,
    kL,
    kSemiColon,
    kQuote,
    kEnter,
    kLeftShift,
    kZ,
    kX,
    kC,
    kV,
    kB,
    kN,
    kM,
    kComma,
    kPeriod,
    kSlash,
    kRightShift,
    kLeftControl,
    kSuper,
    kLeftAlt,
    kSpace,
    kRightAlt,
    kRightControl,
    kUp,
    kDown,
    kLeft,
    kRight,
    kInsert,
    kHome,
    kPageUp,
    kDelete,
    kEnd,
    kPageDown,
    kNpStar,
    kNpMinus,
    kNpPlus,
    kPause
  };

 public:
  KeyEvent(Window* target, Key key, int native_key_code, uint32_t key_char,
           int repeat_count, bool prev_state, bool modifier_shift_pressed,
           bool modifier_ctrl_pressed, bool modifier_alt_pressed,
           bool modifier_super_pressed)
      : UIEvent(target),
        key_(key),
        native_key_code_(native_key_code),
        key_char_(key_char),
        repeat_count_(repeat_count),
        prev_state_(prev_state),
        modifier_shift_pressed_(modifier_shift_pressed),
        modifier_ctrl_pressed_(modifier_ctrl_pressed),
        modifier_alt_pressed_(modifier_alt_pressed),
        modifier_super_pressed_(modifier_super_pressed) {}
  ~KeyEvent() override = default;

  bool is_handled() const { return handled_; }
  void set_handled(bool value) { handled_ = value; }

  Key key() const { return key_; }
  uint32_t key_char() const { return key_char_; }
  int native_key_code() const { return native_key_code_; }

  int repeat_count() const { return repeat_count_; }
  bool prev_state() const { return prev_state_; }

  bool is_shift_pressed() const { return modifier_shift_pressed_; }
  bool is_ctrl_pressed() const { return modifier_ctrl_pressed_; }
  bool is_alt_pressed() const { return modifier_alt_pressed_; }
  bool is_super_pressed() const { return modifier_super_pressed_; }

 private:
  bool handled_ = false;
  Key key_;
  uint32_t key_char_;
  int native_key_code_;

  int repeat_count_ = 0;
  bool prev_state_ = false;  // Key previously down(true) or up(false)

  bool modifier_shift_pressed_ = false;
  bool modifier_ctrl_pressed_ = false;
  bool modifier_alt_pressed_ = false;
  bool modifier_super_pressed_ = false;
};

class MouseEvent : public UIEvent {
 public:
  enum class Button {
    kNone = 0,
    kLeft,
    kRight,
    kMiddle,
    kX1,
    kX2,
  };

 public:
  MouseEvent(Window* target, Button button, int32_t x, int32_t y,
             int32_t dx = 0, int32_t dy = 0)
      : UIEvent(target), button_(button), x_(x), y_(y), dx_(dx), dy_(dy) {}
  ~MouseEvent() override = default;

  bool is_handled() const { return handled_; }
  void set_handled(bool value) { handled_ = value; }

  Button button() const { return button_; }
  int32_t x() const { return x_; }
  int32_t y() const { return y_; }
  int32_t dx() const { return dx_; }
  int32_t dy() const { return dy_; }

 private:
  bool handled_ = false;
  Button button_;
  int32_t x_ = 0;
  int32_t y_ = 0;
  int32_t dx_ = 0;
  int32_t dy_ = 0;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_UI_EVENT_H_
