/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <cstring>

#include "xenia/kernel/locale_info.h"

#include "xenia/base/logging.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/date_format.h"
#include "xenia/kernel/xam/time_format.h"
#include "xenia/kernel/xam/xam_module.h"
#include "xenia/kernel/xam/xam_private.h"
#include "xenia/xbox.h"

DECLARE_int32(user_country);
DECLARE_int32(user_language);

// TODO(gibbed): put these forward decls in a header somewhere.

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

// TODO(gibbed): *REQUIRED BEFORE MERGING* "auto" setting to pick appropriate
// country from user's system.
Country xeXamGetCountry() {
  return static_cast<Country>(static_cast<uint8_t>(cvars::user_country));
}

// TODO(gibbed): *REQUIRED BEFORE MERGING* "auto" setting to pick appropriate
// language from user's system.
Language xeXamGetLanguage() {
  auto language =
      static_cast<Language>(static_cast<uint8_t>(cvars::user_language));
  if (language != Language::kInvalid && language < Language::kMaximum) {
    return language;
  }
  uint32_t game_region = xeXGetGameRegion();
  if ((game_region & 0x0000FF00) == 0x100) {  // asia
    switch (game_region) {
      case 0x101:  // japan
        return Language::kJapanese;
      case 0x102:  // china
        return Language::kSChinese;
      default:
        return Language::kKorean;
    }
  }
  return Language::kEnglish;
}

Country xeXamValidateCountry(Country country) {
  if (country != Country::kZZ && country < Country::kMaximum) {
    return country;
  }
  uint32_t game_region = xeXGetGameRegion();
  if ((game_region & 0x0000FF00) == 0x100) {  // asia
    switch (game_region) {
      case 0x101:
        return Country::kJP;
      case 0x102:
        return Country::kCN;
      default:
        return Country::kKR;
    }
  } else if ((game_region & 0x0000FF00) == 0x200) {  // europe
    if (game_region == 0x201) {
      return Country::kAU;
    }
    switch (xeXamGetLanguage()) {
      case Language::kDutch:
        return Country::kDE;
      case Language::kFrench:
        return Country::kFR;
      case Language::kSpanish:
        return Country::kES;
      case Language::kItalian:
        return Country::kIT;
      case Language::kPortuguese:
        return Country::kPT;
      case Language::kPolish:
        return Country::kPL;
      case Language::kRussian:
        return Country::kRU;
      default:
        return Country::kGB;
    }
  }
  return Country::kUS;
}

// XGetLanguage has subtly different results than XamGetLanguage when defaulting
// based on the game's region. Probably before newer supported languages were
// added on the Xbox 360.
Language xeXGetLanguage() {
  auto language =
      static_cast<Language>(static_cast<uint8_t>(cvars::user_language));
  if (language != Language::kInvalid && language <= Language::kPortuguese) {
    return language;
  }

  uint32_t game_region = xeXGetGameRegion();
  if ((game_region & 0x0000FF00) == 0x100) {  // asia
    switch (game_region) {
      case 0x101:  // japan
        return Language::kJapanese;
      case 0x102:  // china
        break;
      default:
        return Language::kKorean;
    }
  }
  return Language::kEnglish;
}

Locale xeXamGetLocaleEx(Country last_country, Locale last_locale) {
  auto country = xeXamGetCountry();
  if (country != Country::kZZ && country <= last_country) {
    auto locale = locale_info::GetLocaleFromCountry(country);
    if (locale != Locale::kZZ && locale <= last_locale) {
      return locale;
    }
  }
  // couldn't find locale, fallback from game region.
  auto game_region = xeXGetGameRegion();
  if ((game_region & 0xFF00) == 0x100) {  // asia
    return game_region == 0x101 ? Locale::kJP : Locale::kKR;
  } else if ((game_region & 0xFF) == 0x200) {
    return Locale::kGB;
  }
  return Locale::kUS;
}

Locale xeXamGetLocale() {
  return xeXamGetLocaleEx(Country::kLast, Locale::kLast);
}

// Exports.

dword_result_t XamValidateCountry(dword_t country_id) {
  auto country = static_cast<Country>(static_cast<uint8_t>(country_id));
  country = xeXamValidateCountry(country);
  return static_cast<uint8_t>(country);
}
DECLARE_XAM_EXPORT1(XamValidateCountry, kLocale, kImplemented);

dword_result_t XamGetLanguage() {
  return static_cast<uint8_t>(xeXamGetLanguage());
}
DECLARE_XAM_EXPORT1(XamGetLanguage, kLocale, kImplemented);

dword_result_t XGetLanguage() { return static_cast<uint8_t>(xeXGetLanguage()); }
DECLARE_XAM_EXPORT1(XGetLanguage, kLocale, kImplemented);

dword_result_t XamGetCountry() {
  return static_cast<uint8_t>(xeXamGetCountry());
}
DECLARE_XAM_EXPORT1(XamGetCountry, kLocale, kImplemented);

dword_result_t XamGetLocale() { return static_cast<uint8_t>(xeXamGetLocale()); }
DECLARE_XAM_EXPORT1(XamGetLocale, kLocale, kImplemented);

dword_result_t XamGetOnlineCountryFromLocale(dword_t id) {
  auto locale = static_cast<Locale>(static_cast<uint8_t>(id));
  auto online_country = locale_info::GetOnlineCountryFromLocale(locale);
  return static_cast<uint8_t>(online_country);
}
DECLARE_XAM_EXPORT1(XamGetOnlineCountryFromLocale, kLocale, kImplemented);

dword_result_t XamGetOnlineCountryString(dword_t id, dword_t buffer_length,
                                         lpu16string_t buffer) {
  if (buffer_length >= 0x80000000u) {
    return X_E_INVALIDARG;
  }

  auto online_country = static_cast<OnlineCountry>(static_cast<uint8_t>(id));
  auto str = locale_info::GetOnlineCountryString(online_country);
  if (!str) {
    return X_E_NOTFOUND;
  }

  auto value = std::u16string_view(str);
  if (value.size() + 1 > buffer_length) {
    return X_E_BUFFER_TOO_SMALL;
  }

  string_util::copy_and_swap_truncating(buffer, value, buffer_length);
  return X_E_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamGetOnlineCountryString, kLocale, kImplemented);

dword_result_t XamGetCountryString(dword_t id, dword_t buffer_length,
                                   lpu16string_t buffer) {
  if (buffer_length >= 0x80000000u) {
    return X_E_INVALIDARG;
  }

  auto country = static_cast<Country>(static_cast<uint8_t>(id));
  auto str = locale_info::GetCountryString(country);
  if (!str) {
    return X_E_NOTFOUND;
  }

  auto value = std::u16string_view(str);
  if (value.size() + 1 > buffer_length) {
    return X_E_BUFFER_TOO_SMALL;
  }

  string_util::copy_and_swap_truncating(buffer, value, buffer_length);
  return X_E_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamGetCountryString, kLocale, kImplemented);

dword_result_t XamGetLanguageString(dword_t id, dword_t buffer_length,
                                    lpu16string_t buffer) {
  if (buffer_length >= 0x80000000u) {
    return X_E_INVALIDARG;
  }

  auto language = static_cast<Language>(static_cast<uint8_t>(id));
  auto str = locale_info::GetLanguageString(language);
  if (!str) {
    return X_E_NOTFOUND;
  }

  const auto value = std::u16string_view(str);
  if (value.size() + 1 > buffer_length) {
    return X_E_BUFFER_TOO_SMALL;
  }

  string_util::copy_and_swap_truncating(buffer, value, buffer_length);
  return X_E_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamGetLanguageString, kLocale, kImplemented);

dword_result_t XamGetLanguageLocaleString(dword_t language_id,
                                          dword_t locale_id,
                                          dword_t buffer_length,
                                          lpu16string_t buffer) {
  if (buffer_length >= 0x80000000u) {
    return X_E_INVALIDARG;
  }

  auto language = static_cast<Language>(static_cast<uint8_t>(language_id));
  auto language_str = locale_info::GetLanguageString(language);
  if (!language_str) {
    return X_E_NOTFOUND;
  }

  auto locale = static_cast<Locale>(static_cast<uint8_t>(locale_id));
  auto locale_str = locale_info::GetLocaleString(locale);
  if (!locale_str) {
    return X_E_NOTFOUND;
  }

  auto value = fmt::format(u"{}-{}", language_str, locale_str);
  if (value.size() + 1 > buffer_length) {
    return X_E_BUFFER_TOO_SMALL;
  }

  string_util::copy_and_swap_truncating(buffer, value, buffer_length);
  return X_E_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamGetLanguageLocaleString, kLocale, kImplemented);

dword_result_t XamGetOnlineLanguageAndCountryString(dword_t online_language_id,
                                                    dword_t online_country_id,
                                                    dword_t buffer_length,
                                                    lpu16string_t buffer) {
  if (buffer_length >= 0x80000000u) {
    return X_E_INVALIDARG;
  }

  auto online_language =
      static_cast<OnlineLanguage>(static_cast<uint8_t>(online_language_id));
  auto online_language_str =
      locale_info::GetOnlineLanguageString(online_language);
  if (!online_language_str) {
    return X_E_NOTFOUND;
  }

  auto online_country =
      static_cast<OnlineCountry>(static_cast<uint8_t>(online_country_id));
  auto online_country_str = locale_info::GetOnlineCountryString(online_country);
  if (!online_country_str) {
    return X_E_NOTFOUND;
  }

  auto value = fmt::format(u"{}-{}", online_language_str, online_country_str);
  if (value.size() + 1 > buffer_length) {
    return X_E_BUFFER_TOO_SMALL;
  }

  string_util::copy_and_swap_truncating(buffer, value, buffer_length);
  return X_E_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamGetOnlineLanguageAndCountryString, kLocale,
                    kImplemented);

dword_result_t XamGetLanguageLocaleFallbackString(dword_t id) {
  auto xam = kernel_state()->GetKernelModule<XamModule>("xam.xex");
  auto entry_addresses = xam->llf_entry_addresses();
  assert_true(id < entry_addresses.size());
  return entry_addresses[id];
}
DECLARE_XAM_EXPORT1(XamGetLanguageLocaleFallbackString, kLocale, kImplemented);

dword_result_t XamGetLocaleString(dword_t id, dword_t buffer_length,
                                  lpu16string_t buffer) {
  if (buffer_length >= 0x80000000u) {
    return X_E_INVALIDARG;
  }

  auto locale = static_cast<Locale>(static_cast<uint8_t>(id));
  auto str = locale_info::GetLocaleString(locale);
  if (!str) {
    return X_E_NOTFOUND;
  }

  const auto value = std::u16string_view(str);
  if (value.size() + 1 > buffer_length) {
    return X_E_BUFFER_TOO_SMALL;
  }

  string_util::copy_truncating(buffer, value, buffer_length);
  return X_E_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamGetLocaleString, kLocale, kImplemented);

dword_result_t XamGetLocaleFromOnlineCountry(dword_t id) {
  auto online_country =
      static_cast<locale_info::OnlineCountry>(static_cast<uint8_t>(id));
  auto locale = locale_info::GetLocaleFromOnlineCountry(online_country);
  return static_cast<uint8_t>(locale);
}
DECLARE_XAM_EXPORT1(XamGetLocaleFromOnlineCountry, kLocale, kImplemented);

dword_result_t XamGetLanguageFromOnlineLanguage(dword_t id) {
  auto online_language = locale_info::OnlineLanguage(static_cast<uint8_t>(id));
  auto language = locale_info::GetLanguageFromOnlineLanguage(online_language);
  return static_cast<uint8_t>(language);
}
DECLARE_XAM_EXPORT1(XamGetLanguageFromOnlineLanguage, kLocale, kImplemented);

dword_result_t XamGetOnlineLanguageString(dword_t id, dword_t buffer_length,
                                          lpu16string_t buffer) {
  if (buffer_length >= 0x80000000u) {
    return X_E_INVALIDARG;
  }

  auto online_language = locale_info::OnlineLanguage(uint8_t(id));
  auto str = locale_info::GetOnlineLanguageString(online_language);
  if (!str) {
    return X_E_NOTFOUND;
  }

  auto value = std::u16string_view(str);
  if (value.size() + 1 > buffer_length) {
    return X_E_BUFFER_TOO_SMALL;
  }

  string_util::copy_and_swap_truncating(buffer, value, buffer_length);
  return X_E_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamGetOnlineLanguageString, kLocale, kImplemented);

dword_result_t XamGetCountryFromOnlineCountry(dword_t id) {
  auto online_country = locale_info::OnlineCountry(static_cast<uint8_t>(id));
  auto country = locale_info::GetCountryFromOnlineCountry(online_country);
  return static_cast<uint8_t>(country);
}
DECLARE_XAM_EXPORT1(XamGetCountryFromOnlineCountry, kLocale, kImplemented);

dword_result_t XamGetLocaleEx(dword_t max_country_id, dword_t max_locale_id) {
  auto max_country = static_cast<Country>(static_cast<uint8_t>(max_country_id));
  auto max_locale = static_cast<Locale>(static_cast<uint8_t>(max_locale_id));
  auto locale = xeXamGetLocaleEx(max_country, max_locale);
  return static_cast<uint8_t>(locale);
}
DECLARE_XAM_EXPORT1(XamGetLocaleEx, kLocale, kImplemented);

dword_result_t XamGetLanguageTypeface(dword_t language_id, dword_t path_count,
                                      lpvoid_t path_buffer) {
  auto language = static_cast<Language>(static_cast<uint32_t>(language_id));

  if (language == Language::kInvalid || language >= Language::kMaximum) {
    XELOGE("XamGetLanguageTypeface called with invalid language %u!",
           language_id);
    language = xeXamGetLanguage();
  }

  std::u16string_view typeface_path;
  switch (language) {
    case Language::kTChinese:
      typeface_path = u"file://media:/XenonCLatin.xtt";
      break;
    case Language::kSChinese:
      typeface_path = u"file://media:/XenonSCLatin.xtt";
      break;
    default:
      typeface_path = u"file://media:/XenonJKLatin.xtt";
      break;
  }

  if (typeface_path.size() + 1 > path_count) {
    return X_E_BUFFER_TOO_SMALL;
  }

  string_util::copy_and_swap_truncating(path_buffer.as<char16_t*>(),
                                        typeface_path, path_count);
  return X_E_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamGetLanguageTypeface, kLocale, kImplemented);

dword_result_t XamGetLanguageTypefacePatch(dword_t language_id) {
  // TODO(gibbed): returns a pointer to a static string... yay...
  return 0;
}
DECLARE_XAM_EXPORT1(XamGetLanguageTypefacePatch, kLocale, kStub);

}  // namespace xam
}  // namespace kernel
}  // namespace xe

void xe::kernel::xam::RegisterLocaleExports(
    xe::cpu::ExportResolver* export_resolver,
    xe::kernel::KernelState* kernel_state) {}
