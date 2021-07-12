/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/logging.h"
#include "xenia/cpu/processor.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/user_module.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_private.h"
#include "xenia/xbox.h"

#if XE_PLATFORM_WIN32
#include "xenia/base/platform_win.h"
#endif

DEFINE_int32(user_language, 1,
             "User language ID.\n"
             "  1=en  2=ja  3=de  4=fr  5=es  6=it  7=ko  8=zh\n"
             "  9=pt 11=pl 12=ru 13=sv 14=tr 15=nb 16=nl 17=zh",
             "XConfig");

DEFINE_int32(user_country, 103,
             "User country ID.\n"
             "   1=AE   2=AL   3=AM   4=AR   5=AT   6=AU   7=AZ   8=BE   9=BG\n"
             "  10=BH  11=BN  12=BO  13=BR  14=BY  15=BZ  16=CA  18=CH  19=CL\n"
             "  20=CN  21=CO  22=CR  23=CZ  24=DE  25=DK  26=DO  27=DZ  28=EC\n"
             "  29=EE  30=EG  31=ES  32=FI  33=FO  34=FR  35=GB  36=GE  37=GR\n"
             "  38=GT  39=HK  40=HN  41=HR  42=HU  43=ID  44=IE  45=IL  46=IN\n"
             "  47=IQ  48=IR  49=IS  50=IT  51=JM  52=JO  53=JP  54=KE  55=KG\n"
             "  56=KR  57=KW  58=KZ  59=LB  60=LI  61=LT  62=LU  63=LV  64=LY\n"
             "  65=MA  66=MC  67=MK  68=MN  69=MO  70=MV  71=MX  72=MY  73=NI\n"
             "  74=NL  75=NO  76=NZ  77=OM  78=PA  79=PE  80=PH  81=PK  82=PL\n"
             "  83=PR  84=PT  85=PY  86=QA  87=RO  88=RU  89=SA  90=SE  91=SG\n"
             "  92=SI  93=SK  95=SV  96=SY  97=TH  98=TN  99=TR 100=TT 101=TW\n"
             " 102=UA 103=US 104=UY 105=UZ 106=VE 107=VN 108=YE 109=ZA\n",
             "XConfig");

namespace xe {
namespace kernel {
namespace xboxkrnl {

X_STATUS xeExGetXConfigSetting(uint16_t category, uint16_t setting,
                               void* buffer, uint16_t buffer_size,
                               uint16_t* required_size) {
  uint16_t setting_size = 0;
  alignas(uint32_t) uint8_t value[4];

  // TODO(benvanik): have real structs here that just get copied from.
  // https://free60project.github.io/wiki/XConfig.html
  // https://github.com/oukiar/freestyledash/blob/master/Freestyle/Tools/Generic/ExConfig.h
  switch (category) {
    case 0x0002:
      // XCONFIG_SECURED_CATEGORY
      switch (setting) {
        case 0x0002:  // XCONFIG_SECURED_AV_REGION
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, 0x00001000);  // USA/Canada
          break;
        default:
          assert_unhandled_case(setting);
          return X_STATUS_INVALID_PARAMETER_2;
      }
      break;
    case 0x0003:
      // XCONFIG_USER_CATEGORY
      switch (setting) {
#if XE_PLATFORM_WIN32
          // TODO(gibbed): *REQUIRED BEFORE MERGING* all of the time zone stuff
          // needs to get appropriate value from user settings / system.
        case 0x0001: {  // XCONFIG_USER_TIME_ZONE_BIAS
          TIME_ZONE_INFORMATION tzi;
          auto id = GetTimeZoneInformation(&tzi);
          setting_size = 4;
          xe::store_and_swap<uint32_t>(
              value, id != TIME_ZONE_ID_INVALID ? tzi.Bias : 0);
          break;
        }
        case 0x0002: {  // XCONFIG_USER_TIME_ZONE_STD_NAME
          TIME_ZONE_INFORMATION tzi;
          auto id = GetTimeZoneInformation(&tzi);
          setting_size = 4;
          std::memcpy(value, "CST\0", 4);
          break;
        }
        case 0x0003: {  // XCONFIG_USER_TIME_ZONE_DLT_NAME
          TIME_ZONE_INFORMATION tzi;
          auto id = GetTimeZoneInformation(&tzi);
          setting_size = 4;
          std::memcpy(value, "CDT\0", 4);
          break;
        }
        case 0x0004: {  // XCONFIG_USER_TIME_ZONE_STD_DATE
          TIME_ZONE_INFORMATION tzi;
          auto id = GetTimeZoneInformation(&tzi);
          value[0] = static_cast<uint8_t>(tzi.StandardDate.wMonth);
          value[1] = static_cast<uint8_t>(tzi.StandardDate.wDay);
          value[2] = static_cast<uint8_t>(tzi.StandardDate.wDayOfWeek);
          value[3] = static_cast<uint8_t>(tzi.StandardDate.wHour);
          setting_size = 4;
          break;
        }
        case 0x0005: {  // XCONFIG_USER_TIME_ZONE_DLT_DATE
          TIME_ZONE_INFORMATION tzi;
          auto id = GetTimeZoneInformation(&tzi);
          value[0] = static_cast<uint8_t>(tzi.DaylightDate.wMonth);
          value[1] = static_cast<uint8_t>(tzi.DaylightDate.wDay);
          value[2] = static_cast<uint8_t>(tzi.DaylightDate.wDayOfWeek);
          value[3] = static_cast<uint8_t>(tzi.DaylightDate.wHour);
          setting_size = 4;
          break;
        }
        case 0x0006: {  // XCONFIG_USER_TIME_ZONE_STD_BIAS
          TIME_ZONE_INFORMATION tzi;
          auto id = GetTimeZoneInformation(&tzi);
          setting_size = 4;
          xe::store_and_swap<uint32_t>(
              value, id != TIME_ZONE_ID_INVALID ? tzi.StandardBias : 0);
          break;
        }
        case 0x0007: {  // XCONFIG_USER_TIME_ZONE_DLT_BIAS
          TIME_ZONE_INFORMATION tzi;
          auto id = GetTimeZoneInformation(&tzi);
          setting_size = 4;
          xe::store_and_swap<uint32_t>(
              value, id != TIME_ZONE_ID_INVALID ? tzi.DaylightBias : 0);
          break;
        }
#else
        case 0x0001:  // XCONFIG_USER_TIME_ZONE_BIAS
        case 0x0002:  // XCONFIG_USER_TIME_ZONE_STD_NAME
        case 0x0003:  // XCONFIG_USER_TIME_ZONE_DLT_NAME
        case 0x0004:  // XCONFIG_USER_TIME_ZONE_STD_DATE
        case 0x0005:  // XCONFIG_USER_TIME_ZONE_DLT_DATE
        case 0x0006:  // XCONFIG_USER_TIME_ZONE_STD_BIAS
        case 0x0007:  // XCONFIG_USER_TIME_ZONE_DLT_BIAS
          setting_size = 4;
          // TODO(benvanik): get this value.
          xe::store_and_swap<uint32_t>(value, 0);
          break;
#endif
        case 0x0009:  // XCONFIG_USER_LANGUAGE
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, cvars::user_language);
          break;
        case 0x000A:  // XCONFIG_USER_VIDEO_FLAGS
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, 0x00040000);
          break;
        case 0x000C: {  // XCONFIG_USER_RETAIL_FLAGS
          // TODO(benvanik): get this value.
          uint32_t flags = 0;
#if XE_PLATFORM_WIN32
          TIME_ZONE_INFORMATION tzi;
          auto id = GetTimeZoneInformation(&tzi);
          if (id != TIME_ZONE_ID_DAYLIGHT) {
            flags |= 0x02;  // DST off
          }
#endif
          flags |= 0x04;  // network initialized?
          // flags |= 0x08;  // 24-hour clock
          flags |= 0x40;  // dashboard initial setup complete
          setting_size = 4;
          xe::store_and_swap<uint32_t>(value, flags);
          break;
        }
        case 0x000E:  // XCONFIG_USER_COUNTRY
          setting_size = 1;
          value[0] = static_cast<uint8_t>(cvars::user_country);
          break;
        default:
          assert_unhandled_case(setting);
          return X_STATUS_INVALID_PARAMETER_2;
      }
      break;
    default:
      assert_unhandled_case(category);
      return X_STATUS_INVALID_PARAMETER_1;
  }

  if (buffer) {
    if (buffer_size < setting_size) {
      return X_STATUS_BUFFER_TOO_SMALL;
    }
    std::memcpy(buffer, value, setting_size);
  } else {
    if (buffer_size) {
      return X_STATUS_INVALID_PARAMETER_3;
    }
  }

  if (required_size) {
    *required_size = setting_size;
  }

  return X_STATUS_SUCCESS;
}

dword_result_t ExGetXConfigSetting(word_t category, word_t setting,
                                   lpvoid_t buffer_ptr, word_t buffer_size,
                                   lpword_t required_size_ptr) {
  uint16_t required_size = 0;
  X_STATUS result = xeExGetXConfigSetting(category, setting, buffer_ptr,
                                          buffer_size, &required_size);

  if (required_size_ptr) {
    *required_size_ptr = required_size;
  }

  return result;
}
DECLARE_XBOXKRNL_EXPORT1(ExGetXConfigSetting, kModules, kImplemented);

dword_result_t ExSetXConfigSetting(word_t category, word_t setting,
                                   lpvoid_t buffer_ptr, word_t buffer_size) {
  return 0;
}
DECLARE_XBOXKRNL_EXPORT1(ExSetXConfigSetting, kModules, kStub);

void RegisterXConfigExports(xe::cpu::ExportResolver* export_resolver,
                            KernelState* kernel_state) {}

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe
