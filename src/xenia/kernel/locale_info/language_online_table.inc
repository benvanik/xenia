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

#ifndef XE_ONLINE_LANGUAGE
#define XE_ONLINE_LANGUAGE(name, id, language)
#endif

#ifndef XE_ONLINE_LANGUAGE_SUFFIX
#define XE_ONLINE_LANGUAGE_SUFFIX(name, id, suffix, language)
#endif

#ifndef XE_ONLINE_LANGUAGE_MAXIMUM
#define XE_ONLINE_LANGUAGE_MAXIMUM(name)
#endif

// clang-format off

XE_ONLINE_LANGUAGE(zz,  0, Invalid)
XE_ONLINE_LANGUAGE(en,  1, English)
XE_ONLINE_LANGUAGE(ja,  2, Japanese)
XE_ONLINE_LANGUAGE(de,  3, German)
XE_ONLINE_LANGUAGE(fr,  4, French)
XE_ONLINE_LANGUAGE(es,  5, Spanish)
XE_ONLINE_LANGUAGE(it,  6, Italian)
XE_ONLINE_LANGUAGE(ko,  7, Korean)
XE_ONLINE_LANGUAGE_SUFFIX(zh, 8, traditional, TChinese)
XE_ONLINE_LANGUAGE(pt,  9, Portuguese)
XE_ONLINE_LANGUAGE_SUFFIX(zh, 10, simplified, SChinese)
XE_ONLINE_LANGUAGE(pl, 11, Polish)
XE_ONLINE_LANGUAGE(ru, 12, Russian)
XE_ONLINE_LANGUAGE(da, 13, English)
XE_ONLINE_LANGUAGE(fi, 14, English)
XE_ONLINE_LANGUAGE(nb, 15, Norwegian)
XE_ONLINE_LANGUAGE(nl, 16, Dutch)
XE_ONLINE_LANGUAGE(sv, 17, Swedish)
XE_ONLINE_LANGUAGE(cs, 18, English)
XE_ONLINE_LANGUAGE(el, 19, English)
XE_ONLINE_LANGUAGE(hu, 20, English)
XE_ONLINE_LANGUAGE(sk, 21, English)
XE_ONLINE_LANGUAGE(id, 22, English)
XE_ONLINE_LANGUAGE(ms, 23, English)
XE_ONLINE_LANGUAGE(ar, 24, English)
XE_ONLINE_LANGUAGE(bg, 25, English)
XE_ONLINE_LANGUAGE(et, 26, English)
XE_ONLINE_LANGUAGE(hr, 27, English)
XE_ONLINE_LANGUAGE(he, 28, English)
XE_ONLINE_LANGUAGE(is, 29, English)
XE_ONLINE_LANGUAGE(kk, 30, English)
XE_ONLINE_LANGUAGE(lt, 31, English)
XE_ONLINE_LANGUAGE(lv, 32, English)
XE_ONLINE_LANGUAGE(ro, 33, English)
XE_ONLINE_LANGUAGE(sl, 34, English)
XE_ONLINE_LANGUAGE(th, 35, English)
XE_ONLINE_LANGUAGE(tr, 36, Turkish)
XE_ONLINE_LANGUAGE(uk, 37, English)
XE_ONLINE_LANGUAGE(vi, 38, English)
XE_ONLINE_LANGUAGE(ps, 39, English)
XE_ONLINE_LANGUAGE(sq, 40, English)
XE_ONLINE_LANGUAGE(hy, 41, English)
XE_ONLINE_LANGUAGE(bn, 42, English)
XE_ONLINE_LANGUAGE(be, 43, English)
XE_ONLINE_LANGUAGE(km, 44, English)
XE_ONLINE_LANGUAGE(am, 45, English)
XE_ONLINE_LANGUAGE(fo, 46, English)
XE_ONLINE_LANGUAGE(ka, 47, English)
XE_ONLINE_LANGUAGE(kl, 48, English)
XE_ONLINE_LANGUAGE(sw, 49, English)
XE_ONLINE_LANGUAGE(ky, 50, English)
XE_ONLINE_LANGUAGE(lb, 51, English)
XE_ONLINE_LANGUAGE(mk, 52, English)
XE_ONLINE_LANGUAGE(mt, 53, English)
XE_ONLINE_LANGUAGE(mn, 54, English)
XE_ONLINE_LANGUAGE(ne, 55, English)
XE_ONLINE_LANGUAGE(ur, 56, English)
XE_ONLINE_LANGUAGE(rw, 57, English)
XE_ONLINE_LANGUAGE(wo, 58, English)
XE_ONLINE_LANGUAGE(si, 59, English)
XE_ONLINE_LANGUAGE(tk, 60, English)
XE_ONLINE_LANGUAGE_MAXIMUM(tk)

// clang-format on

#undef XE_ONLINE_LANGUAGE_MAXIMUM
#undef XE_ONLINE_LANGUAGE_SUFFIX
#undef XE_ONLINE_LANGUAGE
