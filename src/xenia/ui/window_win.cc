/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <ShellScalingApi.h>
#include <string>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform_win.h"
#include "xenia/ui/window_win.h"

namespace xe {
namespace ui {

std::unique_ptr<Window> Window::Create(Loop* loop, const std::wstring& title) {
  return std::make_unique<Win32Window>(loop, title);
}

Win32Window::Win32Window(Loop* loop, const std::wstring& title)
    : Window(loop, title) {}

Win32Window::~Win32Window() {
  OnDestroy();
  if (hwnd_) {
    SetWindowLongPtr(hwnd_, GWLP_USERDATA, 0);
    CloseWindow(hwnd_);
    hwnd_ = nullptr;
  }
  if (icon_) {
    DestroyIcon(icon_);
    icon_ = nullptr;
  }
}

NativePlatformHandle Win32Window::native_platform_handle() const {
  return ::GetModuleHandle(nullptr);
}

bool Win32Window::Initialize() { return OnCreate(); }

bool Win32Window::OnCreate() {
  HINSTANCE hInstance = GetModuleHandle(nullptr);

  static bool has_registered_class = false;
  if (!has_registered_class) {
    // Tell Windows that we're DPI aware.
    auto spda = (decltype(&SetProcessDpiAwareness))GetProcAddress(
        GetModuleHandle(L"shcore.dll"), "SetProcessDpiAwareness");
    if (spda) {
      auto res = spda(PROCESS_PER_MONITOR_DPI_AWARE);
      if (res != S_OK) {
        XELOGE("Failed to set process DPI awareness. (code = 0x%.8X)", res);
      }
    }

    WNDCLASSEX wcex;
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wcex.lpfnWndProc = Win32Window::WndProcThunk;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, L"MAINICON");
    wcex.hIconSm = NULL;  // LoadIcon(hInstance, L"MAINICON");
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = nullptr;
    wcex.lpszClassName = L"XeniaWindowClass";
    if (!RegisterClassEx(&wcex)) {
      XELOGE("RegisterClassEx failed");
      return false;
    }
    has_registered_class = true;
  }

  // Setup initial size.
  DWORD window_style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
  DWORD window_ex_style = WS_EX_APPWINDOW | WS_EX_CONTROLPARENT;
  RECT rc = {0, 0, width_, height_};
  AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

  // Create window.
  hwnd_ = CreateWindowEx(window_ex_style, L"XeniaWindowClass", title_.c_str(),
                         window_style, rc.left, rc.top, rc.right - rc.left,
                         rc.bottom - rc.top, nullptr, nullptr, hInstance, this);
  if (!hwnd_) {
    XELOGE("CreateWindow failed");
    return false;
  }

  // Disable flicks.
  ATOM atom = GlobalAddAtom(L"MicrosoftTabletPenServiceProperty");
  const DWORD_PTR dwHwndTabletProperty =
      TABLET_DISABLE_PRESSANDHOLD |    // disables press and hold (right-click)
                                       // gesture
      TABLET_DISABLE_PENTAPFEEDBACK |  // disables UI feedback on pen up (waves)
      TABLET_DISABLE_PENBARRELFEEDBACK |  // disables UI feedback on pen button
                                          // down (circle)
      TABLET_DISABLE_FLICKS |  // disables pen flicks (back, forward, drag down,
                               // drag up)
      TABLET_DISABLE_TOUCHSWITCH | TABLET_DISABLE_SMOOTHSCROLLING |
      TABLET_DISABLE_TOUCHUIFORCEON | TABLET_ENABLE_MULTITOUCHDATA;
  SetProp(hwnd_, L"MicrosoftTabletPenServiceProperty",
          reinterpret_cast<HANDLE>(dwHwndTabletProperty));
  GlobalDeleteAtom(atom);

  // Enable DWM elevation.
  EnableMMCSS();
  // Enable file dragging from external sources
  DragAcceptFiles(hwnd_, true);

  ShowWindow(hwnd_, SW_SHOWNORMAL);
  UpdateWindow(hwnd_);

  arrow_cursor_ = LoadCursor(nullptr, IDC_ARROW);

  // Initial state.
  if (!is_cursor_visible_) {
    ShowCursor(FALSE);
  }
  if (has_focus_) {
    SetFocus(hwnd_);
  }

  return super::OnCreate();
}

void Win32Window::EnableMMCSS() {
  HMODULE hLibrary = LoadLibrary(L"DWMAPI.DLL");
  if (!hLibrary) {
    return;
  }

  typedef HRESULT(__stdcall * PDwmEnableMMCSS)(BOOL);
  PDwmEnableMMCSS pDwmEnableMMCSS =
      (PDwmEnableMMCSS)GetProcAddress(hLibrary, "DwmEnableMMCSS");
  if (pDwmEnableMMCSS) {
    pDwmEnableMMCSS(TRUE);
  }

  typedef HRESULT(__stdcall * PDwmSetPresentParameters)(
      HWND, DWM_PRESENT_PARAMETERS*);
  PDwmSetPresentParameters pDwmSetPresentParameters =
      (PDwmSetPresentParameters)GetProcAddress(hLibrary,
                                               "DwmSetPresentParameters");
  if (pDwmSetPresentParameters) {
    DWM_PRESENT_PARAMETERS pp;
    std::memset(&pp, 0, sizeof(DWM_PRESENT_PARAMETERS));
    pp.cbSize = sizeof(DWM_PRESENT_PARAMETERS);
    pp.fQueue = FALSE;
    pp.cBuffer = 2;
    pp.fUseSourceRate = FALSE;
    pp.cRefreshesPerFrame = 1;
    pp.eSampling = DWM_SOURCE_FRAME_SAMPLING_POINT;
    pDwmSetPresentParameters(hwnd_, &pp);
  }

  FreeLibrary(hLibrary);
}

void Win32Window::OnDestroy() { super::OnDestroy(); }

void Win32Window::OnClose() {
  if (!closing_ && hwnd_) {
    closing_ = true;
  }
  super::OnClose();
}

void Win32Window::EnableMainMenu() {
  if (main_menu_) {
    main_menu_->EnableMenuItem(*this);
  }
}

void Win32Window::DisableMainMenu() {
  if (main_menu_) {
    main_menu_->DisableMenuItem(*this);
  }
}

bool Win32Window::set_title(const std::wstring& title) {
  if (!super::set_title(title)) {
    return false;
  }
  SetWindowText(hwnd_, title.c_str());
  return true;
}

bool Win32Window::SetIcon(const void* buffer, size_t size) {
  if (icon_ != nullptr) {
    DestroyIcon(icon_);
    icon_ = nullptr;
  }

  // Reset icon to default.
  auto default_icon = LoadIcon(GetModuleHandle(nullptr), L"MAINICON");
  SendMessage(hwnd_, WM_SETICON, ICON_BIG,
              reinterpret_cast<LPARAM>(default_icon));
  SendMessage(hwnd_, WM_SETICON, ICON_SMALL,
              reinterpret_cast<LPARAM>(default_icon));
  if (!buffer || !size) {
    return true;
  }

  // Create icon and set on window (if it's valid).
  icon_ = CreateIconFromResourceEx(
      reinterpret_cast<PBYTE>(const_cast<void*>(buffer)),
      static_cast<DWORD>(size), TRUE, 0x00030000, 0, 0,
      LR_DEFAULTCOLOR | LR_DEFAULTSIZE);
  if (icon_) {
    SendMessage(hwnd_, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon_));
    SendMessage(hwnd_, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon_));
  }

  return false;
}

bool Win32Window::is_fullscreen() const { return fullscreen_; }

// https://blogs.msdn.microsoft.com/oldnewthing/20131017-00/?p=2903
BOOL UnadjustWindowRect(LPRECT prc, DWORD dwStyle, BOOL fMenu) {
  RECT rc;
  SetRectEmpty(&rc);
  BOOL fRc = AdjustWindowRect(&rc, dwStyle, fMenu);
  if (fRc) {
    prc->left -= rc.left;
    prc->top -= rc.top;
    prc->right -= rc.right;
    prc->bottom -= rc.bottom;
  }
  return fRc;
}

void Win32Window::ToggleFullscreen(bool fullscreen) {
  if (fullscreen == is_fullscreen()) {
    return;
  }

  DWORD style = GetWindowLong(hwnd_, GWL_STYLE);
  if (fullscreen) {
    // http://blogs.msdn.com/b/oldnewthing/archive/2010/04/12/9994016.aspx
    MONITORINFO mi = {sizeof(mi)};
    if (GetWindowPlacement(hwnd_, &windowed_pos_) &&
        GetMonitorInfo(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY),
                       &mi)) {
      // Remove the menubar and borders.
      SetWindowLong(hwnd_, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
      ::SetMenu(hwnd_, NULL);

      // Resize the window to fullscreen.
      auto& rc = mi.rcMonitor;
      AdjustWindowRect(&rc, GetWindowLong(hwnd_, GWL_STYLE), false);
      MoveWindow(hwnd_, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                 TRUE);

      width_ = rc.right - rc.left;
      height_ = rc.bottom - rc.top;
    }
  } else {
    // Reinstate borders, resize to 1280x720
    SetWindowLong(hwnd_, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
    SetWindowPlacement(hwnd_, &windowed_pos_);

    auto main_menu = reinterpret_cast<Win32MenuItem*>(main_menu_.get());
    if (main_menu) {
      ::SetMenu(hwnd_, main_menu->handle());
    }

    auto& rc = windowed_pos_.rcNormalPosition;
    bool has_menu = main_menu_ ? true : false;
    UnadjustWindowRect(&rc, GetWindowLong(hwnd_, GWL_STYLE), has_menu);
    width_ = rc.right - rc.left;
    height_ = rc.bottom - rc.top;
  }

  fullscreen_ = fullscreen;
}

bool Win32Window::is_bordered() const {
  DWORD style = GetWindowLong(hwnd_, GWL_STYLE);
  return (style & WS_OVERLAPPEDWINDOW) == WS_OVERLAPPEDWINDOW;
}

void Win32Window::set_bordered(bool enabled) {
  if (is_fullscreen()) {
    // Don't screw with the borders if we're fullscreen.
    return;
  }

  DWORD style = GetWindowLong(hwnd_, GWL_STYLE);
  if (enabled) {
    SetWindowLong(hwnd_, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
  } else {
    SetWindowLong(hwnd_, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
  }
}

int Win32Window::get_dpi() const {
  HMONITOR monitor = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY);

  // According to msdn, x and y are identical...
  UINT dpi_x, dpi_y;
  GetDpiForMonitor(monitor, MDT_DEFAULT, &dpi_x, &dpi_y);
  return dpi_x;
}

void Win32Window::set_cursor_visible(bool value) {
  if (is_cursor_visible_ == value) {
    return;
  }
  is_cursor_visible_ = value;

  if (value) {
    ShowCursor(TRUE);
  } else {
    ShowCursor(FALSE);
  }
}

void Win32Window::set_focus(bool value) {
  if (has_focus_ == value) {
    return;
  }
  if (hwnd_) {
    if (value) {
      SetFocus(hwnd_);
    } else {
      SetFocus(nullptr);
    }
  } else {
    has_focus_ = value;
  }
}

void Win32Window::Resize(int32_t width, int32_t height) {
  if (is_fullscreen()) {
    // Cannot resize while in fullscreen.
    return;
  }

  // Scale width and height
  int32_t scaled_width, scaled_height;
  float dpi_scale = get_dpi_scale();
  scaled_width = int32_t(width * dpi_scale);
  scaled_height = int32_t(height * dpi_scale);

  RECT rc = {0, 0, 0, 0};
  GetWindowRect(hwnd_, &rc);
  if (rc.top < 0) {
    rc.top = 100;
  }
  if (rc.left < 0) {
    rc.left = 100;
  }

  rc.right = rc.left + scaled_width;
  rc.bottom = rc.top + scaled_height;

  bool has_menu = !is_fullscreen() && (main_menu_ ? true : false);
  AdjustWindowRect(&rc, GetWindowLong(hwnd_, GWL_STYLE), has_menu);
  MoveWindow(hwnd_, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
             TRUE);

  super::Resize(width, height);
}

void Win32Window::Resize(int32_t left, int32_t top, int32_t right,
                         int32_t bottom) {
  if (is_fullscreen()) {
    // Cannot resize while in fullscreen.
    return;
  }

  RECT rc = {left, top, right, bottom};

  // Scale width and height
  float dpi_scale = get_dpi_scale();
  rc.right = int32_t((right - left) * dpi_scale) + left;
  rc.bottom = int32_t((bottom - top) * dpi_scale) + top;

  bool has_menu = !is_fullscreen() && (main_menu_ ? true : false);
  AdjustWindowRect(&rc, GetWindowLong(hwnd_, GWL_STYLE), has_menu);
  MoveWindow(hwnd_, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
             TRUE);

  super::Resize(left, top, right, bottom);
}

void Win32Window::RawReposition(const RECT& rc) {
  MoveWindow(hwnd_, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
             TRUE);
}

void Win32Window::OnResize(UIEvent* e) {
  RECT client_rect;
  GetClientRect(hwnd_, &client_rect);
  int32_t width = client_rect.right - client_rect.left;
  int32_t height = client_rect.bottom - client_rect.top;

  // Rescale to base DPI.
  float dpi_scale = get_dpi_scale();
  width = int32_t(width / dpi_scale);
  height = int32_t(height / dpi_scale);

  if (width != width_ || height != height_) {
    width_ = width;
    height_ = height;
    Layout();
  }
  super::OnResize(e);
}

void Win32Window::Invalidate() {
  super::Invalidate();
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void Win32Window::Close() {
  if (closing_) {
    return;
  }
  closing_ = true;
  OnClose();
  DestroyWindow(hwnd_);
}

void Win32Window::OnMainMenuChange() {
  auto main_menu = reinterpret_cast<Win32MenuItem*>(main_menu_.get());
  // Don't actually set the menu if we're fullscreen. We'll do that later.
  if (main_menu && !is_fullscreen()) {
    ::SetMenu(hwnd_, main_menu->handle());
  }
}

LRESULT CALLBACK Win32Window::WndProcThunk(HWND hWnd, UINT message,
                                           WPARAM wParam, LPARAM lParam) {
  Win32Window* window = nullptr;
  if (message == WM_NCCREATE) {
    auto create_struct = reinterpret_cast<LPCREATESTRUCT>(lParam);
    window = reinterpret_cast<Win32Window*>(create_struct->lpCreateParams);
    SetWindowLongPtr(hWnd, GWLP_USERDATA, (__int3264)(LONG_PTR)window);
  } else {
    window =
        reinterpret_cast<Win32Window*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
  }
  if (window) {
    return window->WndProc(hWnd, message, wParam, lParam);
  } else {
    return DefWindowProc(hWnd, message, wParam, lParam);
  }
}

LRESULT Win32Window::WndProc(HWND hWnd, UINT message, WPARAM wParam,
                             LPARAM lParam) {
  if (hwnd_ != nullptr && hWnd != hwnd_) {
    return DefWindowProc(hWnd, message, wParam, lParam);
  }

  if (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) {
    if (HandleMouse(message, wParam, lParam)) {
      return 0;
    } else {
      return DefWindowProc(hWnd, message, wParam, lParam);
    }
  } else if (message >= WM_KEYFIRST && message <= WM_KEYLAST) {
    if (HandleKeyboard(message, wParam, lParam)) {
      return 0;
    } else {
      return DefWindowProc(hWnd, message, wParam, lParam);
    }
  }

  switch (message) {
    case WM_DROPFILES: {
      TCHAR lpszFile[MAX_PATH] = {0};
      UINT uFiles = 0;
      HDROP hDrop = (HDROP)wParam;
      // Get number of files dropped
      uFiles = DragQueryFile(hDrop, -1, NULL, NULL);

      // Only getting first file dropped (other files ignored)
      if (DragQueryFile(hDrop, 0, lpszFile, MAX_PATH)) {
        auto e = FileDropEvent(this, lpszFile);
        OnFileDrop(&e);
      }

      DragFinish(hDrop);
    } break;
    case WM_NCCREATE: {
      // Tell Windows to automatically scale non-client areas on different DPIs.
      auto en = (BOOL(WINAPI*)(HWND hwnd))GetProcAddress(
          GetModuleHandle(L"user32.dll"), "EnableNonClientDpiScaling");
      if (en) {
        en(hWnd);
      }
    } break;
    case WM_CREATE:
      break;
    case WM_DESTROY:
      OnDestroy();
      break;
    case WM_CLOSE:
      OnClose();
      break;

    case WM_MOVING:
      break;
    case WM_MOVE:
      break;
    case WM_SIZING:
      break;
    case WM_SIZE: {
      auto e = UIEvent(this);
      OnResize(&e);
    } break;

    case WM_PAINT: {
      ValidateRect(hwnd_, nullptr);
      static bool in_paint = false;
      if (!in_paint) {
        in_paint = true;
        auto e = UIEvent(this);
        OnPaint(&e);
        in_paint = false;
      }
      return 0;  // Ignored because of custom paint.
    }
    case WM_ERASEBKGND:
      return 0;  // Ignored because of custom paint.
    case WM_DISPLAYCHANGE:
      break;
    case WM_DPICHANGED: {
      LPRECT rect = (LPRECT)lParam;
      if (rect) {
        RawReposition(*rect);
      }

      auto e = UIEvent(this);
      OnDpiChanged(&e);
    } break;

    case WM_ACTIVATEAPP:
    case WM_SHOWWINDOW: {
      if (wParam == TRUE) {
        auto e = UIEvent(this);
        OnVisible(&e);
      } else {
        auto e = UIEvent(this);
        OnHidden(&e);
      }
      break;
    }

    case WM_KILLFOCUS: {
      has_focus_ = false;
      auto e = UIEvent(this);
      OnLostFocus(&e);
      break;
    }
    case WM_SETFOCUS: {
      has_focus_ = true;
      auto e = UIEvent(this);
      OnGotFocus(&e);
      break;
    }

    case WM_TABLET_QUERYSYSTEMGESTURESTATUS:
      return
          // disables press and hold (right-click) gesture
          TABLET_DISABLE_PRESSANDHOLD |
          // disables UI feedback on pen up (waves)
          TABLET_DISABLE_PENTAPFEEDBACK |
          // disables UI feedback on pen button down (circle)
          TABLET_DISABLE_PENBARRELFEEDBACK |
          // disables pen flicks (back, forward, drag down, drag up)
          TABLET_DISABLE_FLICKS | TABLET_DISABLE_TOUCHSWITCH |
          TABLET_DISABLE_SMOOTHSCROLLING | TABLET_DISABLE_TOUCHUIFORCEON |
          TABLET_ENABLE_MULTITOUCHDATA;

    case WM_MENUCOMMAND: {
      // TODO(benvanik): Redirect this to MenuItem's on_selected delegate.
      MENUINFO menu_info = {0};
      menu_info.cbSize = sizeof(menu_info);
      menu_info.fMask = MIM_MENUDATA;
      GetMenuInfo(HMENU(lParam), &menu_info);
      auto parent_item = reinterpret_cast<Win32MenuItem*>(menu_info.dwMenuData);
      auto child_item =
          reinterpret_cast<Win32MenuItem*>(parent_item->child(wParam));
      assert_not_null(child_item);
      UIEvent e(this);
      child_item->OnSelected(&e);
    } break;
  }

  return DefWindowProc(hWnd, message, wParam, lParam);
}

bool Win32Window::HandleMouse(UINT message, WPARAM wParam, LPARAM lParam) {
  int32_t x = GET_X_LPARAM(lParam);
  int32_t y = GET_Y_LPARAM(lParam);
  if (message == WM_MOUSEWHEEL) {
    POINT pt = {x, y};
    ScreenToClient(hwnd_, &pt);
    x = pt.x;
    y = pt.y;
  }

  MouseEvent::Button button = MouseEvent::Button::kNone;
  int32_t dx = x - last_mouse_pos_.x;
  int32_t dy = y - last_mouse_pos_.y;
  switch (message) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
      button = MouseEvent::Button::kLeft;
      break;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
      button = MouseEvent::Button::kRight;
      break;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
      button = MouseEvent::Button::kMiddle;
      break;
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
      switch (GET_XBUTTON_WPARAM(wParam)) {
        case XBUTTON1:
          button = MouseEvent::Button::kX1;
          break;
        case XBUTTON2:
          button = MouseEvent::Button::kX2;
          break;
        default:
          return false;
      }
      break;
    case WM_MOUSEMOVE:
      button = MouseEvent::Button::kNone;
      break;
    case WM_MOUSEWHEEL:
      button = MouseEvent::Button::kNone;
      dx = 0;  // ?
      dy = GET_WHEEL_DELTA_WPARAM(wParam);
      break;
    default:
      // Double click/etc?
      return true;
  }

  last_mouse_pos_ = {x, y};

  auto e = MouseEvent(this, button, x, y, dx, dy);
  switch (message) {
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_XBUTTONDOWN:
      OnMouseDown(&e);
      break;
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
    case WM_XBUTTONUP:
      OnMouseUp(&e);
      break;
    case WM_MOUSEMOVE:
      OnMouseMove(&e);
      break;
    case WM_MOUSEWHEEL:
      OnMouseWheel(&e);
      break;
  }
  return e.is_handled();
}

static KeyEvent::Key MapVkKeyToKeyEventKey(LPARAM key) {
  switch (key) {
    case VK_ESCAPE:
      return KeyEvent::Key::kEsc;
    case VK_F1:
      return KeyEvent::Key::kF1;
    case VK_F2:
      return KeyEvent::Key::kF2;
    case VK_F3:
      return KeyEvent::Key::kF3;
    case VK_F4:
      return KeyEvent::Key::kF4;
    case VK_F5:
      return KeyEvent::Key::kF5;
    case VK_F6:
      return KeyEvent::Key::kF6;
    case VK_F7:
      return KeyEvent::Key::kF7;
    case VK_F8:
      return KeyEvent::Key::kF8;
    case VK_F9:
      return KeyEvent::Key::kF9;
    case VK_F10:
      return KeyEvent::Key::kF10;
    case VK_F11:
      return KeyEvent::Key::kF11;
    case VK_F12:
      return KeyEvent::Key::kF12;
    case VK_OEM_3:
      return KeyEvent::Key::kTick;
    // 0-9 and A-Z match ASCII
    case '1':
      return KeyEvent::Key::k1;
    case '2':
      return KeyEvent::Key::k2;
    case '3':
      return KeyEvent::Key::k3;
    case '4':
      return KeyEvent::Key::k4;
    case '5':
      return KeyEvent::Key::k5;
    case '6':
      return KeyEvent::Key::k6;
    case '7':
      return KeyEvent::Key::k7;
    case '8':
      return KeyEvent::Key::k8;
    case '9':
      return KeyEvent::Key::k9;
    case '0':
      return KeyEvent::Key::k0;
    case VK_OEM_MINUS:
      return KeyEvent::Key::kMinus;
    case VK_OEM_PLUS:
      return KeyEvent::Key::kEquals;
    case VK_BACK:
      return KeyEvent::Key::kBackspace;
    case VK_TAB:
      return KeyEvent::Key::kTab;
    case 'Q':
      return KeyEvent::Key::kQ;
    case 'W':
      return KeyEvent::Key::kW;
    case 'E':
      return KeyEvent::Key::kE;
    case 'R':
      return KeyEvent::Key::kR;
    case 'T':
      return KeyEvent::Key::kT;
    case 'Y':
      return KeyEvent::Key::kY;
    case 'U':
      return KeyEvent::Key::kU;
    case 'I':
      return KeyEvent::Key::kI;
    case 'O':
      return KeyEvent::Key::kO;
    case 'P':
      return KeyEvent::Key::kP;
    case VK_OEM_4:
      return KeyEvent::Key::kLeftBracket;
    case VK_OEM_6:
      return KeyEvent::Key::kRightBracket;
    case VK_OEM_5:
      return KeyEvent::Key::kBackSlash;
    case VK_CAPITAL:
      return KeyEvent::Key::kCapsLock;
    case 'A':
      return KeyEvent::Key::kA;
    case 'S':
      return KeyEvent::Key::kS;
    case 'D':
      return KeyEvent::Key::kD;
    case 'F':
      return KeyEvent::Key::kF;
    case 'G':
      return KeyEvent::Key::kG;
    case 'H':
      return KeyEvent::Key::kH;
    case 'J':
      return KeyEvent::Key::kJ;
    case 'K':
      return KeyEvent::Key::kK;
    case 'L':
      return KeyEvent::Key::kL;
    case VK_OEM_1:
      return KeyEvent::Key::kSemiColon;
    case VK_OEM_7:
      return KeyEvent::Key::kQuote;
    case VK_RETURN:
      return KeyEvent::Key::kEnter;
    case VK_LSHIFT:
      return KeyEvent::Key::kLeftShift;
    case 'Z':
      return KeyEvent::Key::kZ;
    case 'X':
      return KeyEvent::Key::kX;
    case 'C':
      return KeyEvent::Key::kC;
    case 'V':
      return KeyEvent::Key::kV;
    case 'B':
      return KeyEvent::Key::kB;
    case 'N':
      return KeyEvent::Key::kN;
    case 'M':
      return KeyEvent::Key::kM;
    case VK_OEM_COMMA:
      return KeyEvent::Key::kComma;
    case VK_OEM_PERIOD:
      return KeyEvent::Key::kPeriod;
    case VK_OEM_2:
      return KeyEvent::Key::kSlash;
    case VK_RSHIFT:
      return KeyEvent::Key::kRightShift;
    case VK_LCONTROL:
      return KeyEvent::Key::kLeftControl;
    case VK_LWIN:
    case VK_RWIN:
      return KeyEvent::Key::kSuper;
    case VK_LMENU:
      return KeyEvent::Key::kLeftAlt;
    case VK_SPACE:
      return KeyEvent::Key::kSpace;
    case VK_RMENU:
      return KeyEvent::Key::kRightAlt;
    case VK_RCONTROL:
      return KeyEvent::Key::kRightControl;
    case VK_UP:
      return KeyEvent::Key::kUp;
    case VK_DOWN:
      return KeyEvent::Key::kDown;
    case VK_LEFT:
      return KeyEvent::Key::kLeft;
    case VK_RIGHT:
      return KeyEvent::Key::kRight;
    case VK_INSERT:
      return KeyEvent::Key::kInsert;
    case VK_DELETE:
      return KeyEvent::Key::kDelete;
    case VK_HOME:
      return KeyEvent::Key::kHome;
    case VK_END:
      return KeyEvent::Key::kEnd;
    case VK_PRIOR:
      return KeyEvent::Key::kPageUp;
    case VK_NEXT:
      return KeyEvent::Key::kPageDown;
    case VK_MULTIPLY:
      return KeyEvent::Key::kNpStar;
    case VK_SUBTRACT:
      return KeyEvent::Key::kNpMinus;
    case VK_ADD:
      return KeyEvent::Key::kNpPlus;
    case VK_PAUSE:
      return KeyEvent::Key::kPause;
  }
  return KeyEvent::Key::kNone;
}

bool Win32Window::HandleKeyboard(UINT message, WPARAM wParam, LPARAM lParam) {
  int repeat_count = static_cast<int>(lParam & 0xFFFF);
  int key_code = static_cast<int>(wParam);
  KeyEvent::Key key;
  int key_char;
  if (message != WM_CHAR) {
    key_char = MapVirtualKey(key_code, MAPVK_VK_TO_CHAR) & 0xFFFF;
    key = MapVkKeyToKeyEventKey(key_code);
  } else {
    key_char = key_code;
    key = KeyEvent::Key::kNone;
  }
  auto e = KeyEvent(
      this, key, key_code, key_char, repeat_count, !!(lParam & 0x40000000),
      !!(GetKeyState(VK_SHIFT) & 0x80), !!(GetKeyState(VK_CONTROL) & 0x80),
      !!(GetKeyState(VK_MENU) & 0x80), !!(GetKeyState(VK_LWIN) & 0x80));
  switch (message) {
    case WM_KEYDOWN:
      OnKeyDown(&e);
      break;
    case WM_KEYUP:
      OnKeyUp(&e);
      break;
    case WM_CHAR:
      OnKeyChar(&e);
      break;
  }
  return e.is_handled();
}

std::unique_ptr<ui::MenuItem> MenuItem::Create(Type type,
                                               const std::wstring& text,
                                               const std::wstring& hotkey,
                                               std::function<void()> callback) {
  return std::make_unique<Win32MenuItem>(type, text, hotkey, callback);
}

Win32MenuItem::Win32MenuItem(Type type, const std::wstring& text,
                             const std::wstring& hotkey,
                             std::function<void()> callback)
    : MenuItem(type, text, hotkey, std::move(callback)) {
  switch (type) {
    case MenuItem::Type::kNormal:
      handle_ = CreateMenu();
      break;
    case MenuItem::Type::kPopup:
      handle_ = CreatePopupMenu();
      break;
    default:
      // May just be a placeholder.
      break;
  }
  if (handle_) {
    MENUINFO menu_info = {0};
    menu_info.cbSize = sizeof(menu_info);
    menu_info.fMask = MIM_MENUDATA | MIM_STYLE;
    menu_info.dwMenuData = ULONG_PTR(this);
    menu_info.dwStyle = MNS_NOTIFYBYPOS;
    SetMenuInfo(handle_, &menu_info);
  }
}

Win32MenuItem::~Win32MenuItem() {
  if (handle_) {
    DestroyMenu(handle_);
  }
}

void Win32MenuItem::EnableMenuItem(Window& window) {
  int i = 0;
  for (auto iter = children_.begin(); iter != children_.end(); ++iter, i++) {
    ::EnableMenuItem(handle_, i, MF_BYPOSITION | MF_ENABLED);
  }
  DrawMenuBar((HWND)window.native_handle());
}

void Win32MenuItem::DisableMenuItem(Window& window) {
  int i = 0;
  for (auto iter = children_.begin(); iter != children_.end(); ++iter, i++) {
    ::EnableMenuItem(handle_, i, MF_BYPOSITION | MF_GRAYED);
  }
  DrawMenuBar((HWND)window.native_handle());
}

void Win32MenuItem::OnChildAdded(MenuItem* generic_child_item) {
  auto child_item = static_cast<Win32MenuItem*>(generic_child_item);

  switch (child_item->type()) {
    case MenuItem::Type::kNormal:
      // Nothing special.
      break;
    case MenuItem::Type::kPopup:
      AppendMenuW(handle_, MF_POPUP,
                  reinterpret_cast<UINT_PTR>(child_item->handle()),
                  child_item->text().c_str());
      break;
    case MenuItem::Type::kSeparator:
      AppendMenuW(handle_, MF_SEPARATOR, UINT_PTR(child_item->handle_), 0);
      break;
    case MenuItem::Type::kString:
      auto full_name = child_item->text();
      if (!child_item->hotkey().empty()) {
        full_name += L"\t" + child_item->hotkey();
      }
      AppendMenuW(handle_, MF_STRING, UINT_PTR(child_item->handle_),
                  full_name.c_str());
      break;
  }
}

void Win32MenuItem::OnChildRemoved(MenuItem* generic_child_item) {}

}  // namespace ui
}  // namespace xe
