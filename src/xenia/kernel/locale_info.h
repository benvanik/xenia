/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_LOCALE_INFO_H_
#define XENIA_KERNEL_LOCALE_INFO_H_

#include <cstdint>

namespace xe {
namespace kernel {
namespace locale_info {

enum class Language : uint8_t {
#define XE_LANGUAGE(label, id, name) k##name = id,
#define XE_LANGUAGE_ID(id, name) k##name = id,
#define XE_LANGUAGE_MAXIMUM(name, stfs) \
  kLast = k##name, kMaximum = k##name + 1, kMaximumSTFS = k##stfs,
#include "locale_info/language_table.inc"
};

enum class OnlineLanguage : uint8_t {
#define XE_ONLINE_LANGUAGE(name, id, language) k_##name = id,
#define XE_ONLINE_LANGUAGE_SUFFIX(name, id, suffix, language) \
  k_##name##_##suffix = id,
#define XE_ONLINE_LANGUAGE_MAXIMUM(name) \
  kLast = k_##name, kMaximum = k_##name + 1,
#include "locale_info/language_online_table.inc"
};

enum class Country : uint8_t {
#define XE_COUNTRY(name, id, locale) k##name = id,
#define XE_COUNTRY_SUFFIX(name, id, suffix, locale) k##name##_##suffix = id,
#define XE_COUNTRY_MAXIMUM(name) kLast = k##name, kMaximum = k##name + 1,
#include "locale_info/country_table.inc"
};

enum class OnlineCountry : uint8_t {
#define XE_ONLINE_COUNTRY(name, id, country, locale) k##name = id,
#define XE_ONLINE_COUNTRY_SUFFIX(name, id, suffix, country, locale) \
  k##name##_##suffix = id,
#define XE_ONLINE_COUNTRY_MAXIMUM(name) kLast = k##name, kMaximum = k##name + 1,
#include "locale_info/country_online_table.inc"
};

enum class Locale : uint8_t {
#define XE_LOCALE(name, id, online_country, country) k##name = id,
#define XE_LOCALE_SUFFIX(name, id, suffix, online_country, country) \
  k##name##_##suffix = id,
#define XE_LOCALE_MAXIMUM(name) kLast = k##name, kMaximum = k##name + 1,
#include "locale_info/locale_table.inc"
};

Language GetLanguageFromOnlineLanguage(OnlineLanguage online_language);

Country GetCountryFromOnlineCountry(OnlineCountry online_country);
Locale GetLocaleFromOnlineCountry(OnlineCountry online_country);

Locale GetLocaleFromCountry(Country country);

OnlineCountry GetOnlineCountryFromLocale(Locale locale);
Country GetCountryFromLocale(Locale locale);

const char16_t* GetLanguageString(Language language);
const char16_t* GetOnlineLanguageString(OnlineLanguage online_language);

const char16_t* GetCountryString(Country country);
const char16_t* GetOnlineCountryString(OnlineCountry online_country);

const char16_t* GetLocaleString(Locale locale);

}  // namespace locale_info
}  // namespace kernel
}  // namespace xe

#endif
