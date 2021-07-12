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

#ifndef XE_LANGUAGE
#define XE_LANGUAGE(label, id, name)
#endif

#ifndef XE_LANGUAGE_ID
#define XE_LANGUAGE_ID(id, name)
#endif

#ifndef XE_LANGUAGE_MAXIMUM
#define XE_LANGUAGE_MAXIMUM(name, stfs)
#endif

// clang-format off

XE_LANGUAGE(zz,  0, Invalid)
XE_LANGUAGE(en,  1, English)
XE_LANGUAGE(ja,  2, Japanese)
XE_LANGUAGE(de,  3, German)
XE_LANGUAGE(fr,  4, French)
XE_LANGUAGE(es,  5, Spanish)
XE_LANGUAGE(it,  6, Italian)
XE_LANGUAGE(ko,  7, Korean)
XE_LANGUAGE(zh,  8, TChinese)
XE_LANGUAGE(pt,  9, Portuguese)
XE_LANGUAGE_ID(10, SChinseObsolete)
XE_LANGUAGE(pl, 11, Polish)
XE_LANGUAGE(ru, 12, Russian)
XE_LANGUAGE(sv, 13, Swedish)
XE_LANGUAGE(tr, 14, Turkish)
XE_LANGUAGE(nb, 15, Norwegian)
XE_LANGUAGE(nl, 16, Dutch)
XE_LANGUAGE(zh, 17, SChinese)
XE_LANGUAGE_MAXIMUM(SChinese, Swedish)

// clang-format on

#undef XE_LANGUAGE_MAXIMUM
#undef XE_LANGUAGE_ID
#undef XE_LANGUAGE
