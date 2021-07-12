/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// This is a partial file designed to be included by other files when
// constructing various tables.

#ifndef XE_LOCALE
#define XE_LOCALE(name, id, online_country, country)
#endif

#ifndef XE_LOCALE_SUFFIX
#define XE_LOCALE_SUFFIX(name, id, suffix, online_country, country)
#endif

#ifndef XE_LOCALE_MAXIMUM
#define XE_LOCALE_MAXIMUM(name)
#endif

// clang-format off

XE_LOCALE(ZZ,  0, ZZ, ZZ)
XE_LOCALE(AU,  1, AU, AU)
XE_LOCALE(AT,  2, AT, AT)
XE_LOCALE(BE,  3, BE, BE)
XE_LOCALE(BR,  4, BR, BR)
XE_LOCALE(CA,  5, CA, CA)
XE_LOCALE(CL,  6, CL, CL)
XE_LOCALE(CN,  7, CN, CN)
XE_LOCALE(CO,  8, CO, CO)
XE_LOCALE(CZ,  9, CZ, CZ)
XE_LOCALE(DK, 10, DK, DK)
XE_LOCALE(FI, 11, FI, FI)
XE_LOCALE(FR, 12, FR, FR)
XE_LOCALE(DE, 13, DE, DE)
XE_LOCALE(GR, 14, GR, GR)
XE_LOCALE(HK, 15, HK, HK)
XE_LOCALE(HU, 16, HU, HU)
XE_LOCALE(IN, 17, IN, IN)
XE_LOCALE(IE, 18, IE, IE)
XE_LOCALE(IT, 19, IT, IT)
XE_LOCALE(JP, 20, JP, JP)
XE_LOCALE(KR, 21, KR, KR)
XE_LOCALE(MX, 22, MX, MX)
XE_LOCALE(NL, 23, NL, NL)
XE_LOCALE(NZ, 24, NZ, NZ)
XE_LOCALE(NO, 25, NO, NO)
XE_LOCALE(PL, 26, PL, PL)
XE_LOCALE(PT, 27, PT, PT)
XE_LOCALE(SG, 28, SG, SG)
XE_LOCALE(SK, 29, SK, SK)
XE_LOCALE(ZA, 30, ZA, ZA)
XE_LOCALE(ES, 31, ES, ES)
XE_LOCALE(SE, 32, SE, SE)
XE_LOCALE(CH, 33, CH, CH)
XE_LOCALE(TW, 34, TW, TW)
XE_LOCALE(GB, 35, GB, GB)
XE_LOCALE(US, 36, US, US)
XE_LOCALE(RU, 37, RU, RU)
XE_LOCALE_SUFFIX(ZZ, 38, duplicate, ZZ_duplicate, ZZ_duplicate)
XE_LOCALE(TR, 39, TR, TR)
XE_LOCALE(AR, 40, AR, AR)
XE_LOCALE(SA, 41, SA, SA)
XE_LOCALE(IL, 42, IL, IL)
XE_LOCALE(AE, 43, AE, AE)
XE_LOCALE_MAXIMUM(AE)

// clang-format on

#undef XE_LOCALE_MAXIMUM
#undef XE_LOCALE_SUFFIX
#undef XE_LOCALE
