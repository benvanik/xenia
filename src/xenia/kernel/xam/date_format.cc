/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/date_format.h"

namespace xe {
namespace kernel {
namespace xam {
namespace date_format {

using Country = locale_info::Country;

// TODO(gibbed): include this in the country table?
bool GetCountryDateFormat(Country country, DateFormat& date_format) {
  // This isn't intended to be what's *correct*, but how the 360 behaves.
  switch (country) {
    case Country::kBR:
    case Country::kGR:
    case Country::kHK:
    case Country::kNL:
    case Country::kSG:
      date_format = DateFormat::D_M_Y;
      return true;
    case Country::kAU:
    case Country::kBE:
    case Country::kCL:
    case Country::kCZ:
    case Country::kFI:
    case Country::kIE:
    case Country::kNZ:
    case Country::kSK:
      date_format = DateFormat::D_MM_Y;
      return true;
    case Country::kAE:
    case Country::kAR:
    case Country::kAT:
    case Country::kCA:
    case Country::kCH:
    case Country::kCO:
    case Country::kDE:
    case Country::kDK:
    case Country::kES:
    case Country::kFR:
    case Country::kGB:
    case Country::kIL:
    case Country::kIN:
    case Country::kIT:
    case Country::kMX:
    case Country::kNO:
    case Country::kPT:
    case Country::kRU:
    case Country::kSA:
    case Country::kTR:
    case Country::kZZ_duplicate:
      date_format = DateFormat::DD_MM_Y;
      return true;
    case Country::kUS:
      date_format = DateFormat::M_D_Y;
      return true;
    case Country::kCN:
    case Country::kTW:
      date_format = DateFormat::Y_M_D;
      return true;
    case Country::kHU:
    case Country::kJP:
    case Country::kKR:
    case Country::kPL:
    case Country::kSE:
    case Country::kZA:
      date_format = DateFormat::Y_MM_DD;
      return true;
    default:
      return false;
  }
}

}  // namespace date_format
}  // namespace xam
}  // namespace kernel
}  // namespace xe
