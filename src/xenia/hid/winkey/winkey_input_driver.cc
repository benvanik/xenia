/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/winkey/winkey_input_driver.h"

#include "xenia/base/platform_win.h"
#include "xenia/hid/hid_flags.h"
#include "xenia/hid/input_system.h"
#include "xenia/ui/window.h"

namespace xe {
namespace hid {
namespace winkey {

WinKeyInputDriver::WinKeyInputDriver(xe::ui::Window* window)
    : InputDriver(window), packet_number_(1) {
  // Register a key listener.
  window_->on_key_down.AddListener([this](ui::KeyEvent* evt) {
    auto global_lock = global_critical_region_.Acquire();

    KeyEvent key;
    key.vkey = evt->native_key_code();
    key.transition = true;
    key.prev_state = evt->prev_state();
    key.repeat_count = evt->repeat_count();
    key_events_.push(key);
  });
  window_->on_key_up.AddListener([this](ui::KeyEvent* evt) {
    auto global_lock = global_critical_region_.Acquire();

    KeyEvent key;
    key.vkey = evt->native_key_code();
    key.transition = false;
    key.prev_state = evt->prev_state();
    key.repeat_count = evt->repeat_count();
    key_events_.push(key);
  });
}

WinKeyInputDriver::~WinKeyInputDriver() = default;

X_STATUS WinKeyInputDriver::Setup() { return X_STATUS_SUCCESS; }

X_RESULT WinKeyInputDriver::GetCapabilities(uint32_t user_index, uint32_t flags,
                                            X_INPUT_CAPABILITIES* out_caps) {
  if (user_index != 0) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  // TODO(benvanik): confirm with a real XInput controller.
  out_caps->type = 0x01;      // XINPUT_DEVTYPE_GAMEPAD
  out_caps->sub_type = 0x01;  // XINPUT_DEVSUBTYPE_GAMEPAD
  out_caps->flags = 0;
  out_caps->gamepad.buttons = 0xFFFF;
  out_caps->gamepad.left_trigger = 0xFF;
  out_caps->gamepad.right_trigger = 0xFF;
  out_caps->gamepad.thumb_lx = (int16_t)0xFFFFu;
  out_caps->gamepad.thumb_ly = (int16_t)0xFFFFu;
  out_caps->gamepad.thumb_rx = (int16_t)0xFFFFu;
  out_caps->gamepad.thumb_ry = (int16_t)0xFFFFu;
  out_caps->vibration.left_motor_speed = 0;
  out_caps->vibration.right_motor_speed = 0;
  return X_ERROR_SUCCESS;
}

#define IS_KEY_TOGGLED(key) ((GetKeyState(key) & 0x1) == 0x1)
#define IS_KEY_DOWN(key) ((GetAsyncKeyState(key) & 0x8000) == 0x8000)

X_RESULT WinKeyInputDriver::GetState(uint32_t user_index,
                                     X_INPUT_STATE* out_state) {
  if (user_index != 0) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  packet_number_++;

  uint16_t buttons = 0;
  uint8_t left_trigger = 0;
  uint8_t right_trigger = 0;
  int16_t thumb_lx = 0;
  int16_t thumb_ly = 0;
  int16_t thumb_rx = 0;
  int16_t thumb_ry = 0;

  if (window_->has_focus()) {
    if (IS_KEY_TOGGLED(VK_CAPITAL)) {
      // dpad toggled
      if (IS_KEY_DOWN(0x41)) {
        // A
        buttons |= 0x0004;  // XINPUT_GAMEPAD_DPAD_LEFT
      }
      if (IS_KEY_DOWN(0x44)) {
        // D
        buttons |= 0x0008;  // XINPUT_GAMEPAD_DPAD_RIGHT
      }
      if (IS_KEY_DOWN(0x53)) {
        // S
        buttons |= 0x0002;  // XINPUT_GAMEPAD_DPAD_DOWN
      }
      if (IS_KEY_DOWN(0x57)) {
        // W
        buttons |= 0x0001;  // XINPUT_GAMEPAD_DPAD_UP
      }
    } else {
      // left stick
      if (IS_KEY_DOWN(0x41)) {
        // A
        thumb_lx += SHRT_MIN;
      }
      if (IS_KEY_DOWN(0x44)) {
        // D
        thumb_lx += SHRT_MAX;
      }
      if (IS_KEY_DOWN(0x53)) {
        // S
        thumb_ly += SHRT_MIN;
      }
      if (IS_KEY_DOWN(0x57)) {
        // W
        thumb_ly += SHRT_MAX;
      }
    }

    if (IS_KEY_DOWN(0x46)) {
      // F
      buttons |= 0x0040;  // XINPUT_GAMEPAD_LEFT_THUMB
    }

    // Right stick
    if (IS_KEY_DOWN(0x26)) {
      // Up
      thumb_ry += SHRT_MAX;
    }
    if (IS_KEY_DOWN(0x28)) {
      // Down
      thumb_ry += SHRT_MIN;
    }
    if (IS_KEY_DOWN(0x27)) {
      // Right
      thumb_rx += SHRT_MAX;
    }
    if (IS_KEY_DOWN(0x25)) {
      // Left
      thumb_rx += SHRT_MIN;
    }

    if (IS_KEY_DOWN(0x4C)) {
      // L
      buttons |= 0x4000;  // XINPUT_GAMEPAD_X
    }
    if (IS_KEY_DOWN(VK_OEM_7)) {
      // '
      buttons |= 0x2000;  // XINPUT_GAMEPAD_B
    }
    if (IS_KEY_DOWN(VK_OEM_1)) {
      // ;
      buttons |= 0x1000;  // XINPUT_GAMEPAD_A
    }
    if (IS_KEY_DOWN(0x50)) {
      // P
      buttons |= 0x8000;  // XINPUT_GAMEPAD_Y
    }

    if (IS_KEY_DOWN(0x4B)) {
      // K
      buttons |= 0x0080;  // XINPUT_GAMEPAD_RIGHT_THUMB
    }

    if (IS_KEY_DOWN(0x51) || IS_KEY_DOWN(0x49)) {
      // Q / I
      left_trigger = 0xFF;
    }

    if (IS_KEY_DOWN(0x45) || IS_KEY_DOWN(0x4F)) {
      // E / O
      right_trigger = 0xFF;
    }

    if (IS_KEY_DOWN(0x5A)) {
      // Z
      buttons |= 0x0020;  // XINPUT_GAMEPAD_BACK
    }
    if (IS_KEY_DOWN(0x58)) {
      // X
      buttons |= 0x0010;  // XINPUT_GAMEPAD_START
    }
    if (IS_KEY_DOWN(0x31)) {
      // 1
      buttons |= 0x0100;  // XINPUT_GAMEPAD_LEFT_SHOULDER
    }
    if (IS_KEY_DOWN(0x33)) {
      // 3
      buttons |= 0x0200;  // XINPUT_GAMEPAD_RIGHT_SHOULDER
    }
  }

  out_state->packet_number = packet_number_;
  out_state->gamepad.buttons = buttons;
  out_state->gamepad.left_trigger = left_trigger;
  out_state->gamepad.right_trigger = right_trigger;
  out_state->gamepad.thumb_lx = thumb_lx;
  out_state->gamepad.thumb_ly = thumb_ly;
  out_state->gamepad.thumb_rx = thumb_rx;
  out_state->gamepad.thumb_ry = thumb_ry;

  return X_ERROR_SUCCESS;
}

X_RESULT WinKeyInputDriver::SetState(uint32_t user_index,
                                     X_INPUT_VIBRATION* vibration) {
  if (user_index != 0) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  return X_ERROR_SUCCESS;
}

X_RESULT WinKeyInputDriver::GetKeystroke(uint32_t user_index, uint32_t flags,
                                         X_INPUT_KEYSTROKE* out_keystroke) {
  if (user_index != 0) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  X_RESULT result = X_ERROR_EMPTY;

  uint16_t virtual_key = 0;
  uint16_t unicode = 0;
  uint16_t keystroke_flags = 0;
  uint8_t hid_code = 0;

  // Pop from the queue.
  KeyEvent evt;
  {
    auto global_lock = global_critical_region_.Acquire();
    if (key_events_.empty()) {
      // No keys!
      return X_ERROR_EMPTY;
    }
    evt = key_events_.front();
    key_events_.pop();
  }

  // TODO(DrChat): Some other way to toggle this...
  if (IS_KEY_TOGGLED(VK_CAPITAL)) {
    // dpad toggled
    if (evt.vkey == (0x41)) {
      // A
      virtual_key = 0x5812;  // VK_PAD_DPAD_LEFT
    } else if (evt.vkey == (0x44)) {
      // D
      virtual_key = 0x5813;  // VK_PAD_DPAD_RIGHT
    } else if (evt.vkey == (0x53)) {
      // S
      virtual_key = 0x5811;  // VK_PAD_DPAD_DOWN
    } else if (evt.vkey == (0x57)) {
      // W
      virtual_key = 0x5810;  // VK_PAD_DPAD_UP
    }
  } else {
    // left stick
    if (evt.vkey == (0x57)) {
      // W
      virtual_key = 0x5820;  // VK_PAD_LTHUMB_UP
    }
    if (evt.vkey == (0x53)) {
      // S
      virtual_key = 0x5821;  // VK_PAD_LTHUMB_DOWN
    }
    if (evt.vkey == (0x44)) {
      // D
      virtual_key = 0x5822;  // VK_PAD_LTHUMB_RIGHT
    }
    if (evt.vkey == (0x41)) {
      // A
      virtual_key = 0x5823;  // VK_PAD_LTHUMB_LEFT
    }
  }

  // Right stick
  if (evt.vkey == (0x26)) {
    // Up
    virtual_key = 0x5830;
  }
  if (evt.vkey == (0x28)) {
    // Down
    virtual_key = 0x5831;
  }
  if (evt.vkey == (0x27)) {
    // Right
    virtual_key = 0x5832;
  }
  if (evt.vkey == (0x25)) {
    // Left
    virtual_key = 0x5833;
  }

  if (evt.vkey == (0x4C)) {
    // L
    virtual_key = 0x5802;  // VK_PAD_X
  } else if (evt.vkey == (VK_OEM_7)) {
    // '
    virtual_key = 0x5801;  // VK_PAD_B
  } else if (evt.vkey == (VK_OEM_1)) {
    // ;
    virtual_key = 0x5800;  // VK_PAD_A
  } else if (evt.vkey == (0x50)) {
    // P
    virtual_key = 0x5803;  // VK_PAD_Y
  }

  if (evt.vkey == (0x58)) {
    // X
    virtual_key = 0x5814;  // VK_PAD_START
  }
  if (evt.vkey == (0x5A)) {
    // Z
    virtual_key = 0x5815;  // VK_PAD_BACK
  }
  if (evt.vkey == (0x51) || evt.vkey == (0x49)) {
    // Q / I
    virtual_key = 0x5806;  // VK_PAD_LTRIGGER
  }
  if (evt.vkey == (0x45) || evt.vkey == (0x4F)) {
    // E / O
    virtual_key = 0x5807;  // VK_PAD_RTRIGGER
  }
  if (evt.vkey == (0x31)) {
    // 1
    virtual_key = 0x5805;  // VK_PAD_LSHOULDER
  }
  if (evt.vkey == (0x33)) {
    // 3
    virtual_key = 0x5804;  // VK_PAD_RSHOULDER
  }

  if (virtual_key != 0) {
    if (evt.transition == true) {
      keystroke_flags |= 0x0001;  // XINPUT_KEYSTROKE_KEYDOWN
    } else if (evt.transition == false) {
      keystroke_flags |= 0x0002;  // XINPUT_KEYSTROKE_KEYUP
    }

    if (evt.prev_state == evt.transition) {
      keystroke_flags |= 0x0004;  // XINPUT_KEYSTROKE_REPEAT
    }

    result = X_ERROR_SUCCESS;
  }

  out_keystroke->virtual_key = virtual_key;
  out_keystroke->unicode = unicode;
  out_keystroke->flags = keystroke_flags;
  out_keystroke->user_index = 0;
  out_keystroke->hid_code = hid_code;

  // X_ERROR_EMPTY if no new keys
  // X_ERROR_DEVICE_NOT_CONNECTED if no device
  // X_ERROR_SUCCESS if key
  return result;
}

}  // namespace winkey
}  // namespace hid
}  // namespace xe
