/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/time_format.h"

namespace xe {
namespace kernel {
namespace xam {
namespace time_format {

using Country = locale_info::Country;

// TODO(gibbed): include this in the country table?
bool GetCountryTimeFormat(Country country, TimeFormat& time_format) {
  switch (country) {
    case Country::kAE:
    case Country::kAR:
    case Country::kAT:
    case Country::kBR:
    case Country::kCH:
    case Country::kCL:
    case Country::kCZ:
    case Country::kDE:
    case Country::kDK:
    case Country::kFR:
    case Country::kGB:
    case Country::kIE:
    case Country::kIL:
    case Country::kNO:
    case Country::kPL:
    case Country::kSA:
    case Country::kSE:
    case Country::kTR:
    case Country::kZZ_duplicate:
      time_format = TimeFormat::HH_M;
      return true;
    case Country::kAU:
    case Country::kCO:
    case Country::kMX:
      time_format = TimeFormat::HH_M_P;
      return true;
    case Country::kBE:
    case Country::kES:
    case Country::kFI:
    case Country::kHK:
    case Country::kHU:
    case Country::kIN:
    case Country::kIT:
    case Country::kJP:
    case Country::kNL:
    case Country::kPT:
    case Country::kRU:
    case Country::kSK:
      time_format = TimeFormat::H_M;
      return true;
    case Country::kCA:
    case Country::kGR:
    case Country::kNZ:
    case Country::kUS:
    case Country::kZA:
      time_format = TimeFormat::H_M_P;
      return true;
    case Country::kCN:
    case Country::kKR:
    case Country::kSG:
      time_format = TimeFormat::P_H_M;
      return true;
    case Country::kTW:
      time_format = TimeFormat::P_HH_M;
      return true;
    default:
      return false;
  }
}

}  // namespace time_format
}  // namespace xam
}  // namespace kernel
}  // namespace xe
