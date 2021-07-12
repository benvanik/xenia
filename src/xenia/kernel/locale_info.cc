/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/locale_info.h"

namespace xe {
namespace kernel {
namespace locale_info {

Language GetLanguageFromOnlineLanguage(OnlineLanguage online_language) {
  switch (online_language) {
#define XE_ONLINE_LANGUAGE(name, id, language) \
  case OnlineLanguage::k_##name:               \
    return Language::k##language;
#include "locale_info/language_online_table.inc"
    default:
      return Language::kInvalid;
  }
}

Country GetCountryFromOnlineCountry(OnlineCountry online_country) {
  switch (online_country) {
#define XE_ONLINE_COUNTRY(name, id, country, locale) \
  case OnlineCountry::k##name:                       \
    return Country::k##country;
#define XE_ONLINE_COUNTRY_SUFFIX(name, id, suffix, country, locale) \
  case OnlineCountry::k##name##_##suffix:                           \
    return Country::k##country;
#include "locale_info/country_online_table.inc"
    default:
      return Country::kZZ;
  }
}

Locale GetLocaleFromOnlineCountry(OnlineCountry online_country) {
  switch (online_country) {
#define XE_ONLINE_COUNTRY(name, id, country, locale) \
  case OnlineCountry::k##name:                       \
    return Locale::k##locale;
#define XE_ONLINE_COUNTRY_SUFFIX(name, id, suffix, country, locale) \
  case OnlineCountry::k##name##_##suffix:                           \
    return Locale::k##locale;
#include "locale_info/country_online_table.inc"
    default:
      return Locale::kZZ;
  }
}

Locale GetLocaleFromCountry(Country online_country) {
  switch (online_country) {
#define XE_COUNTRY(name, id, locale) \
  case Country::k##name:             \
    return Locale::k##locale;
#define XE_COUNTRY_SUFFIX(name, id, suffix, locale) \
  case Country::k##name##_##suffix:                 \
    return Locale::k##locale;
#include "locale_info/country_table.inc"
    default:
      return Locale::kZZ;
  }
}

OnlineCountry GetOnlineCountryFromLocale(Locale locale) {
  switch (locale) {
#define XE_LOCALE(name, id, online_country, country) \
  case Locale::k##name:                              \
    return OnlineCountry::k##online_country;
#define XE_LOCALE_SUFFIX(name, id, suffix, online_country, country) \
  case Locale::k##name##_##suffix:                                  \
    return OnlineCountry::k##online_country;
#include "locale_info/locale_table.inc"
    default:
      return OnlineCountry::kZZ;
  }
}

Country GeCountryFromLocale(Locale locale) {
  switch (locale) {
#define XE_LOCALE(name, id, online_country, country) \
  case Locale::k##name:                              \
    return Country::k##country;
#define XE_LOCALE_SUFFIX(name, id, suffix, online_country, country) \
  case Locale::k##name##_##suffix:                                  \
    return Country::k##country;
#include "locale_info/locale_table.inc"
    default:
      return Country::kZZ;
  }
}

const char16_t* GetLanguageString(Language language) {
  switch (language) {
#define XE_LANGUAGE(label, id, name) \
  case Language::k##name:            \
    return u## #label;
#include "locale_info/language_table.inc"
    default:
      return nullptr;
  }
}

const char16_t* GetOnlineLanguageString(OnlineLanguage online_language) {
  switch (online_language) {
#define XE_ONLINE_LANGUAGE(name, id, language) \
  case OnlineLanguage::k_##name:               \
    return u## #name;
#include "locale_info/language_online_table.inc"
    default:
      return nullptr;
  }
}

const char16_t* GetCountryString(Country country) {
  switch (country) {
#define XE_COUNTRY(name, id, locale) \
  case Country::k##name:             \
    return u## #name;
#define XE_COUNTRY_SUFFIX(name, id, suffix, locale) \
  case Country::k##name##_##suffix:                 \
    return u## #name;
#include "locale_info/country_table.inc"
    default:
      return nullptr;
  }
}

const char16_t* GetOnlineCountryString(OnlineCountry online_country) {
  switch (online_country) {
#define XE_ONLINE_COUNTRY(name, id, country, locale) \
  case OnlineCountry::k##name:                       \
    return u## #name;
#define XE_ONLINE_COUNTRY_SUFFIX(name, id, suffix, country, locale) \
  case OnlineCountry::k##name##_##suffix:                           \
    return u## #name;
#include "locale_info/country_online_table.inc"
    default:
      return nullptr;
  }
}

const char16_t* GetLocaleString(Locale locale) {
  switch (locale) {
#define XE_LOCALE(name, id, online_country, country) \
  case Locale::k##name:                              \
    return u## #name;
#define XE_LOCALE_SUFFIX(name, id, suffix, online_country, country) \
  case Locale::k##name##_##suffix:                                  \
    return u## #name;
#include "locale_info/locale_table.inc"
    default:
      return nullptr;
  }
}

}  // namespace locale_info
}  // namespace kernel
}  // namespace xe
