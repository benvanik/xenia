/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_DATE_FORMAT_H_
#define XENIA_KERNEL_XAM_DATE_FORMAT_H_

#include <cstdint>

#include "xenia/kernel/locale_info.h"

namespace xe {
namespace kernel {
namespace xam {
namespace date_format {

enum class DateFormat : uint32_t {
  D_M_Y = 0,
  D_MM_Y = 1,
  DD_MM_Y = 2,
  M_D_Y = 3,
  Y_M_D = 4,
  Y_MM_DD = 5,
};

bool GetCountryDateFormat(locale_info::Country country,
                          DateFormat& date_format);

}  // namespace date_format
}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif
