/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/logging.h"
#include "xenia/base/string_util.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/locale_info.h"
#include "xenia/kernel/user_module.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/date_format.h"
#include "xenia/kernel/xam/time_format.h"
#include "xenia/kernel/xam/xam_module.h"
#include "xenia/kernel/xam/xam_private.h"
#include "xenia/xbox.h"

#if XE_PLATFORM_WIN32
#include "xenia/base/platform_win.h"
#endif

#include "third_party/fmt/include/fmt/format.h"

namespace xe {
namespace kernel {
namespace xam {

uint32_t xeXGetGameRegion();

using Language = locale_info::Language;
using OnlineLanguage = locale_info::OnlineLanguage;
using Country = locale_info::Country;
using OnlineCountry = locale_info::OnlineCountry;
using Locale = locale_info::Locale;
using DateFormat = date_format::DateFormat;
using TimeFormat = time_format::TimeFormat;

Country xeXamGetCountry();
Country xeXamValidateCountry(Country country);

DateFormat xeXamGetLocaleDateFormat(Country country) {
  DateFormat date_format;
  if (date_format::GetCountryDateFormat(country, date_format)) {
    return date_format;
  }
  switch (xeXGetGameRegion()) {
    case 0xFF:
      return DateFormat::M_D_Y;
    case 0x102:
      return DateFormat::Y_M_D;
    case 0x101:
      return DateFormat::Y_MM_DD;
    default:
      return DateFormat::DD_MM_Y;
  }
}

TimeFormat xeXamGetLocaleTimeFormat(bool want_seconds) {
  auto country = xeXamValidateCountry(xeXamGetCountry());
  TimeFormat time_format;
  if (time_format::GetCountryTimeFormat(country, time_format)) {
    if (want_seconds) {
      time_format = time_format::add_seconds(time_format);
    }
  } else {
    time_format = TimeFormat::H_M_S_P;
  }
  // TODO(gibbed): get from config
  bool use_24_hour_clock = false;
  if (use_24_hour_clock) {
    time_format = time_format::remove_period(time_format);
  } else if (!time_format::has_period(time_format)) {
    time_format = time_format::add_period_suffix(time_format);
  }
  return time_format;
}

std::u16string xeXamFormatSystemDateString(uint32_t day, uint32_t month,
                                           uint32_t year) {
  auto country = xeXamValidateCountry(xeXamGetCountry());
  auto format = xeXamGetLocaleDateFormat(country);
  const char16_t* format_str;
  switch (format) {
    case DateFormat::D_M_Y:
    default:
      format_str = u"{day}/{month}/{year:04d}";
      break;
    case DateFormat::D_MM_Y:
      format_str = u"{day}/{month:02d}/{year:04d}";
      break;
    case DateFormat::DD_MM_Y:
      format_str = u"{day:02d}/{month:02d}/{year:04d}";
      break;
    case DateFormat::M_D_Y:
      format_str = u"{month}/{day}/{year:04d}";
      break;
    case DateFormat::Y_M_D:
      format_str = u"{year:04d}/{month}/{day}";
    case DateFormat::Y_MM_DD:
      format_str = u"{year:04d}/{month:02d}/{day:02d}";
      break;
  }
  return fmt::format(format_str, fmt::arg(u"year", year),
                     fmt::arg(u"month", month), fmt::arg(u"day", day));
}

std::u16string xeXamFormatSystemTimeString(uint32_t hour, uint32_t minute,
                                           uint32_t second) {
  auto format = xeXamGetLocaleTimeFormat(false);
  const char16_t* period;
  if (!time_format::has_period(format)) {
    period = u"";
  } else {
    bool is_before_midday = hour < 12u;
    if (hour == 0 || hour == 12) {
      hour = 12u;
    } else if (!is_before_midday) {
      hour = hour - 12u;
    }
    period = is_before_midday ? u"AM" : u"PM";
  }
  const char16_t* format_str;
  switch (format) {
    case TimeFormat::HH_M:
    default:
      format_str = u"{hour:02d}:{minute:02d}";
      break;
    case TimeFormat::HH_M_S:
      format_str = u"{hour:02d}:{minute:02d}:{second:02d}";
      break;
    case TimeFormat::H_M_S:
      format_str = u"{hour}:{minute:02d}:{second:02d}";
      break;
    case TimeFormat::H_M:
      format_str = u"{hour}:{minute:02d}";
      break;
    case TimeFormat::HH_M_S_P:
      format_str = u"{hour:02d}:{minute:02d}:{second:02d} {period}";
      break;
    case TimeFormat::HH_M_P:
      format_str = u"{hour:02d}:{minute:02d} {period}";
      break;
    case TimeFormat::H_M_S_P:
      format_str = u"{hour}:{minute:02d}:{second:02d} {period}";
      break;
    case TimeFormat::H_M_P:
      format_str = u"{hour}:{minute:02d} {period}";
      break;
    case TimeFormat::P_H_M_S:
      format_str = u"{period} {hour}:{minute:02d}:{second:02d}";
      break;
    case TimeFormat::P_H_M:
      format_str = u"{period} {hour}:{minute:02d}";
      break;
    case TimeFormat::P_HH_M_S:
      format_str = u"{period} {hour:02d}:{minute:02d}:{second:02d}";
      break;
    case TimeFormat::P_HH_M:
      format_str = u"{period} {hour:02d}:{minute:02d}";
      break;
  }
  return fmt::format(format_str, fmt::arg(u"hour", hour),
                     fmt::arg(u"minute", minute), fmt::arg(u"second", second),
                     fmt::arg(u"period", period));
}

#if XE_PLATFORM_WIN32
static SYSTEMTIME xeFileTimeToSystemTime(uint64_t filetime) {
  // TODO(gibbed): use our implementation of RtlTimeToTimeFields. Need to stick
  // X_TIME_FIELDS somewhere global.
  FILETIME t;
  t.dwHighDateTime = filetime >> 32;
  t.dwLowDateTime = (uint32_t)filetime;
  SYSTEMTIME st;
  FileTimeToSystemTime(&t, &st);
  return st;
}
#endif

std::u16string xeXamFormatDateString(uint64_t filetime) {
// TODO(gibbed): implement this for other platforms
#if XE_PLATFORM_WIN32
  auto st = xeFileTimeToSystemTime(filetime);
  return xeXamFormatSystemDateString(st.wDay, st.wMonth, st.wYear);
#else
  assert_always();
  return u"";
#endif
}

std::u16string xeXamFormatTimeString(uint64_t filetime) {
// TODO(gibbed): implement this for other platforms
#if XE_PLATFORM_WIN32
  auto st = xeFileTimeToSystemTime(filetime);
  return xeXamFormatSystemTimeString(st.wHour, st.wMinute, st.wSecond);
#else
  assert_always();
  return u"";
#endif
}

dword_result_t XamGetLocaleDateFormat(dword_t country_id) {
  auto country = static_cast<Country>(static_cast<uint8_t>(country_id));
  auto date_format = xeXamGetLocaleDateFormat(country);
  return static_cast<uint32_t>(date_format);
}
DECLARE_XAM_EXPORT1(XamGetLocaleDateFormat, kNone, kImplemented);

dword_result_t XamGetLocaleTimeFormat(dword_t seconds) {
  auto time_format = xeXamGetLocaleTimeFormat(seconds != 0);
  return static_cast<uint32_t>(time_format);
}
DECLARE_XAM_EXPORT1(XamGetLocaleTimeFormat, kNone, kImplemented);

void XamFormatDateString(dword_t unk, qword_t filetime, lpvoid_t output_buffer,
                         dword_t output_count) {
  assert_true(unk == 0xFF);
  auto str = xeXamFormatDateString(filetime);
  string_util::copy_and_swap_truncating(output_buffer.as<char16_t*>(), str,
                                        output_count);
}
DECLARE_XAM_EXPORT1(XamFormatDateString, kNone, kImplemented);

void XamFormatTimeString(dword_t unk, qword_t filetime, lpvoid_t output_buffer,
                         dword_t output_count) {
  assert_true(unk == 0xFF);
  auto str = xeXamFormatTimeString(filetime);
  string_util::copy_and_swap_truncating(output_buffer.as<char16_t*>(), str,
                                        output_count);
}
DECLARE_XAM_EXPORT1(XamFormatTimeString, kNone, kImplemented);

}  // namespace xam
}  // namespace kernel
}  // namespace xe

void xe::kernel::xam::RegisterDateTimeExports(
    xe::cpu::ExportResolver* export_resolver,
    xe::kernel::KernelState* kernel_state) {}
