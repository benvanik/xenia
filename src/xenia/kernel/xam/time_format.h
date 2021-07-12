/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_TIME_FORMAT_H_
#define XENIA_KERNEL_XAM_TIME_FORMAT_H_

#include <cstdint>

#include "xenia/kernel/locale_info.h"

namespace xe {
namespace kernel {
namespace xam {
namespace time_format {

enum class TimeFormat : uint32_t {
  HH_M = 0u,
  HH_M_S = 1u,
  H_M_S = 2u,
  H_M = 3u,
  HH_M_S_P = 4u,
  HH_M_P = 5u,
  H_M_S_P = 6u,
  H_M_P = 7u,
  P_H_M_S = 8u,
  P_H_M = 9u,
  P_HH_M_S = 10u,
  P_HH_M = 11u,
};

bool GetCountryTimeFormat(locale_info::Country country,
                          TimeFormat& time_format);

inline bool has_seconds(TimeFormat time_format) {
  switch (time_format) {
    case TimeFormat::HH_M_S:
    case TimeFormat::H_M_S:
    case TimeFormat::HH_M_S_P:
    case TimeFormat::H_M_S_P:
    case TimeFormat::P_H_M_S:
    case TimeFormat::P_HH_M_S:
      return true;
    default:
      return false;
  }
}

inline TimeFormat add_seconds(TimeFormat time_format) {
  switch (time_format) {
    case TimeFormat::HH_M:
      return TimeFormat::HH_M_S;
    case TimeFormat::H_M:
      return TimeFormat::H_M_S;
    case TimeFormat::HH_M_P:
      return TimeFormat::HH_M_S_P;
    case TimeFormat::H_M_P:
      return TimeFormat::H_M_S_P;
    case TimeFormat::P_H_M:
      return TimeFormat::P_H_M_S;
    case TimeFormat::P_HH_M:
      return TimeFormat::P_HH_M_S;
    default:
      return time_format;
  }
}

inline TimeFormat remove_seconds(TimeFormat time_format) {
  switch (time_format) {
    case TimeFormat::HH_M:
      return TimeFormat::HH_M_S;
    case TimeFormat::H_M:
      return TimeFormat::H_M_S;
    case TimeFormat::HH_M_P:
      return TimeFormat::HH_M_S_P;
    case TimeFormat::H_M_P:
      return TimeFormat::H_M_S_P;
    case TimeFormat::P_H_M:
      return TimeFormat::P_H_M_S;
    case TimeFormat::P_HH_M:
      return TimeFormat::P_HH_M_S;
    default:
      return time_format;
  }
}

inline bool has_period(TimeFormat time_format) {
  switch (time_format) {
    case TimeFormat::HH_M_S_P:
    case TimeFormat::HH_M_P:
    case TimeFormat::H_M_S_P:
    case TimeFormat::H_M_P:
    case TimeFormat::P_H_M_S:
    case TimeFormat::P_H_M:
    case TimeFormat::P_HH_M_S:
    case TimeFormat::P_HH_M:
      return true;
    default:
      return false;
  }
}

inline TimeFormat add_period_prefix(TimeFormat time_format) {
  switch (time_format) {
    case TimeFormat::HH_M:
    case TimeFormat::HH_M_P:
      return TimeFormat::P_HH_M;
    case TimeFormat::HH_M_S:
    case TimeFormat::HH_M_S_P:
      return TimeFormat::P_HH_M_S;
    case TimeFormat::H_M_S:
    case TimeFormat::H_M_S_P:
      return TimeFormat::P_H_M_S;
    case TimeFormat::H_M:
    case TimeFormat::H_M_P:
      return TimeFormat::P_H_M;
    default:
      return time_format;
  }
}

inline TimeFormat add_period_suffix(TimeFormat time_format) {
  switch (time_format) {
    case TimeFormat::HH_M:
    case TimeFormat::P_HH_M:
      return TimeFormat::HH_M_P;
    case TimeFormat::HH_M_S:
    case TimeFormat::P_HH_M_S:
      return TimeFormat::HH_M_S_P;
    case TimeFormat::H_M_S:
    case TimeFormat::P_H_M_S:
      return TimeFormat::H_M_S_P;
    case TimeFormat::H_M:
    case TimeFormat::P_H_M:
      return TimeFormat::H_M_P;
    default:
      return time_format;
  }
}

inline TimeFormat remove_period(TimeFormat time_format) {
  switch (time_format) {
    case TimeFormat::HH_M_S_P:
    case TimeFormat::P_HH_M_S:
      return TimeFormat::HH_M_S;
    case TimeFormat::HH_M_P:
    case TimeFormat::P_HH_M:
      return TimeFormat::HH_M;
    case TimeFormat::H_M_S_P:
    case TimeFormat::P_H_M_S:
      return TimeFormat::H_M_S;
    case TimeFormat::H_M_P:
    case TimeFormat::P_H_M:
      return TimeFormat::H_M;
    default:
      return time_format;
  }
}

}  // namespace time_format
}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif
