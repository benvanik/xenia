/*
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/ppc/ppc_emit-private.h"

#include "xenia/base/assert.h"
#include "xenia/cpu/cpu_flags.h"
#include "xenia/cpu/ppc/ppc_context.h"
#include "xenia/cpu/ppc/ppc_frontend.h"
#include "xenia/cpu/ppc/ppc_hir_builder.h"

#include <stddef.h>

namespace xe {
namespace cpu {
namespace ppc {

// TODO(benvanik): remove when enums redefined.
using namespace xe::cpu::hir;

using xe::cpu::hir::Label;
using xe::cpu::hir::Value;

int InstrEmit_branch(PPCHIRBuilder& f, const char* src, uint64_t cia,
                     Value* nia, bool lk, Value* cond = NULL,
                     bool expect_true = true, bool nia_is_lr = false) {
  uint32_t call_flags = 0;

  // TODO(benvanik): this may be wrong and overwrite LRs when not desired!
  // The docs say always, though...
  // Note that we do the update before we branch/call as we need it to
  // be correct for returns.
  if (lk) {
    Value* return_address = f.LoadConstantUint64(cia + 4);
    f.SetReturnAddress(return_address);
    f.StoreLR(return_address);
  }

  if (!lk) {
    // If LR is not set this call will never return here.
    call_flags |= CALL_TAIL;
  }

  // TODO(benvanik): set CALL_TAIL if !lk and the last block in the fn.
  //                 This is almost always a jump to restore gpr.

  if (nia->IsConstant()) {
    // Direct branch to address.
    // If it's a block inside of ourself, setup a fast jump.
    // Unless it's to ourselves directly, in which case it's
    // recursion.
    uint32_t nia_value = nia->AsUint64() & 0xFFFFFFFF;
    bool is_recursion = false;
    if (nia_value == f.function()->address() && lk) {
      is_recursion = true;
    }
    Label* label = is_recursion ? NULL : f.LookupLabel(nia_value);
    if (label) {
      // Branch to label.
      uint32_t branch_flags = 0;
      if (cond) {
        if (expect_true) {
          f.BranchTrue(cond, label, branch_flags);
        } else {
          f.BranchFalse(cond, label, branch_flags);
        }
      } else {
        f.Branch(label, branch_flags);
      }
    } else {
      // Call function.
      auto function = f.LookupFunction(nia_value);
      if (cond) {
        if (!expect_true) {
          cond = f.IsFalse(cond);
        }
        f.CallTrue(cond, function, call_flags);
      } else {
        f.Call(function, call_flags);
      }
    }
  } else {
// Indirect branch to pointer.

// TODO(benvanik): runtime recursion detection?

// TODO(benvanik): run a DFA pass to see if we can detect whether this is
//     a normal function return that is pulling the LR from the stack that
//     it set in the prolog. If so, we can omit the dynamic check!

//// Dynamic test when branching to LR, which is usually used for the return.
//// We only do this if LK=0 as returns wouldn't set LR.
//// Ideally it's a return and we can just do a simple ret and be done.
//// If it's not, we fall through to the full indirection logic.
// if (!lk && reg == kXEPPCRegLR) {
//  // The return block will spill registers for us.
//  // TODO(benvanik): 'lr_mismatch' debug info.
//  // Note: we need to test on *only* the 32-bit target, as the target ptr may
//  //     have garbage in the upper 32 bits.
//  c.cmp(target.r32(), c.getGpArg(1).r32());
//  // TODO(benvanik): evaluate hint here.
//  c.je(e.GetReturnLabel(), kCondHintLikely);
//}
#if 0
    // This breaks longjump, as that uses blr with a non-return lr.
    // It'd be nice to move SET_RETURN_ADDRESS semantics up into context
    // so that we can just use this.
    if (!lk && nia_is_lr) {
      // Return (most likely).
      // TODO(benvanik): test? ReturnCheck()?
      if (cond) {
        if (!expect_true) {
          cond = f.IsFalse(cond);
        }
        f.ReturnTrue(cond);
      } else {
        f.Return();
      }
    } else {
#else
    {
#endif
    // Jump to pointer.
    bool likely_return = !lk && nia_is_lr;
    if (likely_return) {
      call_flags |= CALL_POSSIBLE_RETURN;
    }
    if (cond) {
      if (!expect_true) {
        cond = f.IsFalse(cond);
      }
      f.CallIndirectTrue(cond, nia, call_flags);
    } else {
      f.CallIndirect(nia, call_flags);
    }
  }
}

return 0;
}  // namespace ppc

int InstrEmit_bx(PPCHIRBuilder& f, const InstrData& i) {
  // if AA then
  //   NIA <- EXTS(LI || 0b00)
  // else
  //   NIA <- CIA + EXTS(LI || 0b00)
  // if LK then
  //   LR <- CIA + 4

  uint32_t nia;
  if (i.I.AA) {
    nia = (uint32_t)XEEXTS26(i.I.LI << 2);
  } else {
    nia = (uint32_t)(i.address + XEEXTS26(i.I.LI << 2));
  }

  return InstrEmit_branch(f, "bx", i.address, f.LoadConstantUint32(nia),
                          i.I.LK);
}

int InstrEmit_bcx(PPCHIRBuilder& f, const InstrData& i) {
  // if ¬BO[2] then
  //   CTR <- CTR - 1
  // ctr_ok <- BO[2] | ((CTR[0:63] != 0) XOR BO[3])
  // cond_ok <- BO[0] | (CR[BI+32] ≡ BO[1])
  // if ctr_ok & cond_ok then
  //   if AA then
  //     NIA <- EXTS(BD || 0b00)
  //   else
  //     NIA <- CIA + EXTS(BD || 0b00)
  // if LK then
  //   LR <- CIA + 4

  // NOTE: the condition bits are reversed!
  // 01234 (docs)
  // 43210 (real)

  Value* ctr_ok = NULL;
  if (select_bits(i.B.BO, 2, 2)) {
    // Ignore ctr.
  } else {
    // Decrement counter.
    Value* ctr = f.LoadCTR();
    ctr = f.Sub(ctr, f.LoadConstantUint64(1));
    f.StoreCTR(ctr);
    // Ctr check.
    ctr = f.Truncate(ctr, INT32_TYPE);
    // TODO(benvanik): could do something similar to cond and avoid the
    // is_true/branch_true pairing.
    if (select_bits(i.B.BO, 1, 1)) {
      ctr_ok = f.IsFalse(ctr);
    } else {
      ctr_ok = f.IsTrue(ctr);
    }
  }

  Value* cond_ok = NULL;
  bool not_cond_ok = false;
  if (select_bits(i.B.BO, 4, 4)) {
    // Ignore cond.
  } else {
    Value* cr = f.LoadCRField(i.B.BI >> 2, i.B.BI & 3);
    cond_ok = cr;
    if (select_bits(i.B.BO, 3, 3)) {
      // Expect true.
      not_cond_ok = false;
    } else {
      // Expect false.
      not_cond_ok = true;
    }
  }

  // We do a bit of optimization here to make the llvm assembly easier to read.
  Value* ok = NULL;
  bool expect_true = true;
  if (ctr_ok && cond_ok) {
    if (not_cond_ok) {
      cond_ok = f.IsFalse(cond_ok);
    }
    ok = f.And(ctr_ok, cond_ok);
  } else if (ctr_ok) {
    ok = ctr_ok;
  } else if (cond_ok) {
    ok = cond_ok;
    expect_true = !not_cond_ok;
  }

  uint32_t nia;
  if (i.B.AA) {
    nia = (uint32_t)XEEXTS16(i.B.BD << 2);
  } else {
    nia = (uint32_t)(i.address + XEEXTS16(i.B.BD << 2));
  }
  return InstrEmit_branch(f, "bcx", i.address, f.LoadConstantUint32(nia),
                          i.B.LK, ok, expect_true);
}

int InstrEmit_bcctrx(PPCHIRBuilder& f, const InstrData& i) {
  // cond_ok <- BO[0] | (CR[BI+32] ≡ BO[1])
  // if cond_ok then
  //   NIA <- CTR[0:61] || 0b00
  // if LK then
  //   LR <- CIA + 4

  // NOTE: the condition bits are reversed!
  // 01234 (docs)
  // 43210 (real)

  Value* cond_ok = NULL;
  bool not_cond_ok = false;
  if (select_bits(i.XL.BO, 4, 4)) {
    // Ignore cond.
  } else {
    Value* cr = f.LoadCRField(i.XL.BI >> 2, i.XL.BI & 3);
    cond_ok = cr;
    if (select_bits(i.XL.BO, 3, 3)) {
      // Expect true.
      not_cond_ok = false;
    } else {
      // Expect false.
      not_cond_ok = true;
    }
  }

  bool expect_true = !not_cond_ok;
  return InstrEmit_branch(f, "bcctrx", i.address, f.LoadCTR(), i.XL.LK, cond_ok,
                          expect_true);
}

int InstrEmit_bclrx(PPCHIRBuilder& f, const InstrData& i) {
  // if ¬BO[2] then
  //   CTR <- CTR - 1
  // ctr_ok <- BO[2] | ((CTR[0:63] != 0) XOR BO[3]
  // cond_ok <- BO[0] | (CR[BI+32] ≡ BO[1])
  // if ctr_ok & cond_ok then
  //   NIA <- LR[0:61] || 0b00
  // if LK then
  //   LR <- CIA + 4

  // NOTE: the condition bits are reversed!
  // 01234 (docs)
  // 43210 (real)

  Value* ctr_ok = NULL;
  if (select_bits(i.XL.BO, 2, 2)) {
    // Ignore ctr.
  } else {
    // Decrement counter.
    Value* ctr = f.LoadCTR();
    ctr = f.Sub(ctr, f.LoadConstantUint64(1));
    f.StoreCTR(ctr);
    // Ctr check.
    ctr = f.Truncate(ctr, INT32_TYPE);
    // TODO(benvanik): could do something similar to cond and avoid the
    // is_true/branch_true pairing.
    if (select_bits(i.XL.BO, 1, 1)) {
      ctr_ok = f.IsFalse(ctr);
    } else {
      ctr_ok = f.IsTrue(ctr);
    }
  }

  Value* cond_ok = NULL;
  bool not_cond_ok = false;
  if (select_bits(i.XL.BO, 4, 4)) {
    // Ignore cond.
  } else {
    Value* cr = f.LoadCRField(i.XL.BI >> 2, i.XL.BI & 3);
    cond_ok = cr;
    if (select_bits(i.XL.BO, 3, 3)) {
      // Expect true.
      not_cond_ok = false;
    } else {
      // Expect false.
      not_cond_ok = true;
    }
  }

  // We do a bit of optimization here to make the llvm assembly easier to read.
  Value* ok = NULL;
  bool expect_true = true;
  if (ctr_ok && cond_ok) {
    if (not_cond_ok) {
      cond_ok = f.IsFalse(cond_ok);
    }
    ok = f.And(ctr_ok, cond_ok);
  } else if (ctr_ok) {
    ok = ctr_ok;
  } else if (cond_ok) {
    ok = cond_ok;
    expect_true = !not_cond_ok;
  }

  return InstrEmit_branch(f, "bclrx", i.address, f.LoadLR(), i.XL.LK, ok,
                          expect_true, true);
}

// Condition register logical (A-23)

int InstrEmit_crand(PPCHIRBuilder& f, const InstrData& i) {
  // CR[bt] <- CR[ba] & CR[bb]   bt=bo, ba=bi, bb=bb
  Value* ba = f.LoadCRField(i.XL.BI >> 2, i.XL.BI & 3);
  Value* bb = f.LoadCRField(i.XL.BB >> 2, i.XL.BB & 3);
  Value* bt = f.And(ba, bb);
  f.StoreCRField(i.XL.BO >> 2, i.XL.BO & 3, bt);
  return 0;
}

int InstrEmit_crandc(PPCHIRBuilder& f, const InstrData& i) {
  // CR[bt] <- CR[ba] & ¬CR[bb]   bt=bo, ba=bi, bb=bb
  Value* ba = f.LoadCRField(i.XL.BI >> 2, i.XL.BI & 3);
  Value* bb = f.LoadCRField(i.XL.BB >> 2, i.XL.BB & 3);
  Value* bt = f.And(ba, f.And(f.Not(bb), f.LoadConstantInt8(0x01)));
  f.StoreCRField(i.XL.BO >> 2, i.XL.BO & 3, bt);
  return 0;
}

int InstrEmit_creqv(PPCHIRBuilder& f, const InstrData& i) {
  // CR[bt] <- CR[ba] == CR[bb]   bt=bo, ba=bi, bb=bb
  Value* ba = f.LoadCRField(i.XL.BI >> 2, i.XL.BI & 3);
  Value* bb = f.LoadCRField(i.XL.BB >> 2, i.XL.BB & 3);
  Value* bt = f.CompareEQ(ba, bb);
  f.StoreCRField(i.XL.BO >> 2, i.XL.BO & 3, bt);
  return 0;
}

int InstrEmit_crnand(PPCHIRBuilder& f, const InstrData& i) {
  // CR[bt] <- ¬(CR[ba] & CR[bb])   bt=bo, ba=bi, bb=bb
  Value* ba = f.LoadCRField(i.XL.BI >> 2, i.XL.BI & 3);
  Value* bb = f.LoadCRField(i.XL.BB >> 2, i.XL.BB & 3);
  Value* bt = f.And(f.Not(f.And(ba, bb)), f.LoadConstantInt8(0x01));
  f.StoreCRField(i.XL.BO >> 2, i.XL.BO & 3, bt);
  return 0;
}

int InstrEmit_crnor(PPCHIRBuilder& f, const InstrData& i) {
  // CR[bt] <- ¬(CR[ba] | CR[bb])   bt=bo, ba=bi, bb=bb
  Value* ba = f.LoadCRField(i.XL.BI >> 2, i.XL.BI & 3);
  Value* bb = f.LoadCRField(i.XL.BB >> 2, i.XL.BB & 3);
  Value* bt = f.And(f.Not(f.Or(ba, bb)), f.LoadConstantInt8(0x01));
  f.StoreCRField(i.XL.BO >> 2, i.XL.BO & 3, bt);
  return 0;
}

int InstrEmit_cror(PPCHIRBuilder& f, const InstrData& i) {
  // CR[bt] <- CR[ba] | CR[bb]   bt=bo, ba=bi, bb=bb
  Value* ba = f.LoadCRField(i.XL.BI >> 2, i.XL.BI & 3);
  Value* bb = f.LoadCRField(i.XL.BB >> 2, i.XL.BB & 3);
  Value* bt = f.Or(ba, bb);
  f.StoreCRField(i.XL.BO >> 2, i.XL.BO & 3, bt);
  return 0;
}

int InstrEmit_crorc(PPCHIRBuilder& f, const InstrData& i) {
  // CR[bt] <- CR[ba] | ¬CR[bb]   bt=bo, ba=bi, bb=bb
  Value* ba = f.LoadCRField(i.XL.BI >> 2, i.XL.BI & 3);
  Value* bb = f.LoadCRField(i.XL.BB >> 2, i.XL.BB & 3);
  Value* bt = f.Or(ba, f.And(f.Not(bb), f.LoadConstantInt8(0x01)));
  f.StoreCRField(i.XL.BO >> 2, i.XL.BO & 3, bt);
  return 0;
}

int InstrEmit_crxor(PPCHIRBuilder& f, const InstrData& i) {
  // CR[bt] <- CR[ba] xor CR[bb]   bt=bo, ba=bi, bb=bb
  Value* ba = f.LoadCRField(i.XL.BI >> 2, i.XL.BI & 3);
  Value* bb = f.LoadCRField(i.XL.BB >> 2, i.XL.BB & 3);
  Value* bt = f.Xor(ba, bb);
  f.StoreCRField(i.XL.BO >> 2, i.XL.BO & 3, bt);
  return 0;
}

int InstrEmit_mcrf(PPCHIRBuilder& f, const InstrData& i) {
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

// System linkage (A-24)

int InstrEmit_sc(PPCHIRBuilder& f, const InstrData& i) {
  // Game code should only ever use LEV=0.
  // LEV=2 is to signify 'call import' from Xenia.
  // TODO(gibbed): syscalls!
  if (i.SC.LEV == 0) {
    f.CallExtern(f.builtins()->syscall_handler);
    return 0;
  }
  if (i.SC.LEV == 2) {
    f.CallExtern(f.function());
    return 0;
  }
  XEINSTRNOTIMPLEMENTED();
  return 1;
}

// Trap (A-25)

int InstrEmit_trap(PPCHIRBuilder& f, const InstrData& i, Value* va, Value* vb,
                   uint32_t TO) {
  // if (a < b) & TO[0] then TRAP
  // if (a > b) & TO[1] then TRAP
  // if (a = b) & TO[2] then TRAP
  // if (a <u b) & TO[3] then TRAP
  // if (a >u b) & TO[4] then TRAP
  // Bits swapped:
  // 01234
  // 43210
  if (!TO) {
    return 0;
  }
  Value* v = nullptr;
  if (TO & (1 << 4)) {
    // a < b
    auto cmp = f.CompareSLT(va, vb);
    v = v ? f.Or(v, cmp) : cmp;
  }
  if (TO & (1 << 3)) {
    // a > b
    auto cmp = f.CompareSGT(va, vb);
    v = v ? f.Or(v, cmp) : cmp;
  }
  if (TO & (1 << 2)) {
    // a = b
    auto cmp = f.CompareEQ(va, vb);
    v = v ? f.Or(v, cmp) : cmp;
  }
  if (TO & (1 << 1)) {
    // a <u b
    auto cmp = f.CompareULT(va, vb);
    v = v ? f.Or(v, cmp) : cmp;
  }
  if (TO & (1 << 0)) {
    // a >u b
    auto cmp = f.CompareUGT(va, vb);
    v = v ? f.Or(v, cmp) : cmp;
  }
  if (v) {
    f.TrapTrue(v);
  }
  return 0;
}

int InstrEmit_td(PPCHIRBuilder& f, const InstrData& i) {
  // a <- (RA)
  // b <- (RB)
  // if (a < b) & TO[0] then TRAP
  // if (a > b) & TO[1] then TRAP
  // if (a = b) & TO[2] then TRAP
  // if (a <u b) & TO[3] then TRAP
  // if (a >u b) & TO[4] then TRAP
  Value* ra = f.LoadGPR(i.X.RA);
  Value* rb = f.LoadGPR(i.X.RB);
  return InstrEmit_trap(f, i, ra, rb, i.X.RT);
}

int InstrEmit_tdi(PPCHIRBuilder& f, const InstrData& i) {
  // a <- (RA)
  // if (a < EXTS(SI)) & TO[0] then TRAP
  // if (a > EXTS(SI)) & TO[1] then TRAP
  // if (a = EXTS(SI)) & TO[2] then TRAP
  // if (a <u EXTS(SI)) & TO[3] then TRAP
  // if (a >u EXTS(SI)) & TO[4] then TRAP
  Value* ra = f.LoadGPR(i.D.RA);
  Value* rb = f.LoadConstantInt64(XEEXTS16(i.D.DS));
  return InstrEmit_trap(f, i, ra, rb, i.D.RT);
}

int InstrEmit_tw(PPCHIRBuilder& f, const InstrData& i) {
  // a <- EXTS((RA)[32:63])
  // b <- EXTS((RB)[32:63])
  // if (a < b) & TO[0] then TRAP
  // if (a > b) & TO[1] then TRAP
  // if (a = b) & TO[2] then TRAP
  // if (a <u b) & TO[3] then TRAP
  // if (a >u b) & TO[4] then TRAP
  Value* ra =
      f.SignExtend(f.Truncate(f.LoadGPR(i.X.RA), INT32_TYPE), INT64_TYPE);
  Value* rb =
      f.SignExtend(f.Truncate(f.LoadGPR(i.X.RB), INT32_TYPE), INT64_TYPE);
  return InstrEmit_trap(f, i, ra, rb, i.X.RT);
}

int InstrEmit_twi(PPCHIRBuilder& f, const InstrData& i) {
  // a <- EXTS((RA)[32:63])
  // if (a < EXTS(SI)) & TO[0] then TRAP
  // if (a > EXTS(SI)) & TO[1] then TRAP
  // if (a = EXTS(SI)) & TO[2] then TRAP
  // if (a <u EXTS(SI)) & TO[3] then TRAP
  // if (a >u EXTS(SI)) & TO[4] then TRAP
  if (i.D.RA == 0 && i.D.RT == 0x1F) {
    // This is a special trap. Probably.
    uint16_t type = (uint16_t)XEEXTS16(i.D.DS);
    f.Trap(type);
    return 0;
  }
  Value* ra =
      f.SignExtend(f.Truncate(f.LoadGPR(i.D.RA), INT32_TYPE), INT64_TYPE);
  Value* rb = f.LoadConstantInt64(XEEXTS16(i.D.DS));
  return InstrEmit_trap(f, i, ra, rb, i.D.RT);
}

// Processor control (A-26)

int InstrEmit_mfcr(PPCHIRBuilder& f, const InstrData& i) {
  // mfocrf RT,FXM
  // RT <- undefined
  // count <- 0
  // do i = 0 to 7
  // if FXMi = 1 then
  //   n <- i
  //   count <- count + 1
  // if count = 1 then
  //   RT4un + 32:4un + 35 <- CR4un + 32 : 4un + 35

  // TODO(benvanik): optimize mfcr sequences.
  // Often look something like this:
  //   mfocrf  r11, cr6
  //   not r10, r11
  //   extrwi    r3, r10, 1, 26
  // Could recognize this and only load the appropriate CR bit.

  Value* v;
  if (i.XFX.spr & (1 << 9)) {
    uint32_t bits = (i.XFX.spr & 0x1FF) >> 1;
    int count = 0;
    int cri = 0;
    for (int b = 0; b <= 7; ++b) {
      if (bits & (1 << b)) {
        cri = 7 - b;
        ++count;
      }
    }
    if (count == 1) {
      v = f.LoadCR(cri);
    } else {
      v = f.LoadZeroInt64();
    }
  } else {
    v = f.LoadCR();
  }
  f.StoreGPR(i.XFX.RT, v);
  return 0;
}

int InstrEmit_mfspr(PPCHIRBuilder& f, const InstrData& i) {
  // n <- spr[5:9] || spr[0:4]
  // if length(SPR(n)) = 64 then
  //   RT <- SPR(n)
  // else
  //   RT <- i32.0 || SPR(n)
  Value* v;
  const uint32_t n = ((i.XFX.spr & 0x1F) << 5) | ((i.XFX.spr >> 5) & 0x1F);
  switch (n) {
    case 1:
      // XER
      v = f.LoadXER();
      break;
    case 8:
      // LR
      v = f.LoadLR();
      break;
    case 9:
      // CTR
      v = f.LoadCTR();
      break;
    case 256:
      // VRSAVE
      v = f.LoadZeroInt64();
      break;
    case 268:
      // TB
      v = f.LoadClock();
      break;
    case 269:
      // TBU
      v = f.Shr(f.LoadClock(), 32);
      break;
    case 287:
      // [ Processor Version Register (PVR) ]
      // PVR is a 32 bit, read-only register within the supervisor level.
      // Bits 0 to 15 are the version number.
      // Bits 16 to 31 are the revision number.
      // Known Values: 0x710600?, 0x710700, 0x710800 (Corona?);
      // Note: Some XEXs (such as mfgbootlauncher.xex) may check for a value
      // that's less than 0x710700.
      v = f.LoadConstantUint64(cvars::pvr);
      break;
    default:
      XEINSTRNOTIMPLEMENTED();
      return 1;
  }
  f.StoreGPR(i.XFX.RT, v);
  return 0;
}

int InstrEmit_mftb(PPCHIRBuilder& f, const InstrData& i) {
  Value* time = f.LoadClock();
  const uint32_t n = ((i.XFX.spr & 0x1F) << 5) | ((i.XFX.spr >> 5) & 0x1F);
  if (n == 268) {
    // TB - full bits.
  } else {
    // TBU - upper bits only.
    time = f.Shr(time, 32);
  }
  f.StoreGPR(i.XFX.RT, time);
  return 0;
}

int InstrEmit_mtcrf(PPCHIRBuilder& f, const InstrData& i) {
  // mtocrf FXM,RS
  // count <- 0
  // do i = 0 to 7
  //   if FXMi = 1 then
  //     n <- i
  //     count <- count + 1
  // if count = 1 then
  //   CR4un + 32 : 4un + 35 <- RS4un + 32:4un + 35

  Value* v = f.LoadGPR(i.XFX.RT);
  if (i.XFX.spr & (1 << 9)) {
    uint32_t bits = (i.XFX.spr & 0x1FF) >> 1;
    int count = 0;
    int cri = 0;
    for (int b = 0; b <= 7; ++b) {
      if (bits & (1 << b)) {
        cri = 7 - b;
        ++count;
      }
    }
    if (count == 1) {
      f.StoreCR(cri, v);
    } else {
      // Invalid; store zero to CR.
      f.StoreCR(f.LoadZeroInt64());
    }
  } else {
    uint32_t bits = (i.XFX.spr & 0x1FF) >> 1;
    for (int b = 0; b <= 7; ++b) {
      if (bits & (1 << b)) {
        int cri = 7 - b;
        f.StoreCR(cri, v);
      }
    }
  }
  return 0;
}

int InstrEmit_mtspr(PPCHIRBuilder& f, const InstrData& i) {
  // n <- spr[5:9] || spr[0:4]
  // if length(SPR(n)) = 64 then
  //   SPR(n) <- (RS)
  // else
  //   SPR(n) <- (RS)[32:63]

  Value* rt = f.LoadGPR(i.XFX.RT);

  const uint32_t n = ((i.XFX.spr & 0x1F) << 5) | ((i.XFX.spr >> 5) & 0x1F);
  switch (n) {
    case 1:
      // XER
      f.StoreXER(rt);
      break;
    case 8:
      // LR
      f.StoreLR(rt);
      break;
    case 9:
      // CTR
      f.StoreCTR(rt);
      break;
    case 256:
      // VRSAVE
      break;
    default:
      XEINSTRNOTIMPLEMENTED();
      return 1;
  }

  return 0;
}

// MSR is used for toggling interrupts (among other things).
// We track it here for taking a global processor lock, as lots of lockfree
// code requires it. Sequences of mtmsr/lwar/stcw/mtmsr come up a lot, and
// without the lock here threads can livelock.

int InstrEmit_mfmsr(PPCHIRBuilder& f, const InstrData& i) {
  // bit 48 = EE; interrupt enabled
  // bit 62 = RI; recoverable interrupt
  // return 8000h if unlocked (interrupts enabled), else 0
  f.MemoryBarrier();
  f.CallExtern(f.builtins()->check_global_lock);
  f.StoreGPR(i.X.RT, f.LoadContext(offsetof(PPCContext, scratch), INT64_TYPE));
  return 0;
}

int InstrEmit_mtmsr(PPCHIRBuilder& f, const InstrData& i) {
  if (i.X.RA & 0x01) {
    // L = 1
    // iff storing from r13
    f.MemoryBarrier();
    f.StoreContext(
        offsetof(PPCContext, scratch),
        f.ZeroExtend(f.ZeroExtend(f.LoadGPR(i.X.RT), INT64_TYPE), INT64_TYPE));
    if (i.X.RT == 13) {
      // iff storing from r13 we are taking a lock (disable interrupts).
      if (!cvars::disable_global_lock) {
        f.CallExtern(f.builtins()->enter_global_lock);
      }
    } else {
      // Otherwise we are restoring interrupts (probably).
      if (!cvars::disable_global_lock) {
        f.CallExtern(f.builtins()->leave_global_lock);
      }
    }
    return 0;
  } else {
    // L = 0
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }
}

int InstrEmit_mtmsrd(PPCHIRBuilder& f, const InstrData& i) {
  if (i.X.RA & 0x01) {
    // L = 1
    f.MemoryBarrier();
    f.StoreContext(offsetof(PPCContext, scratch),
                   f.ZeroExtend(f.LoadGPR(i.X.RT), INT64_TYPE));
    if (i.X.RT == 13) {
      // iff storing from r13 we are taking a lock (disable interrupts).
      if (!cvars::disable_global_lock) {
        f.CallExtern(f.builtins()->enter_global_lock);
      }
    } else {
      // Otherwise we are restoring interrupts (probably).
      if (!cvars::disable_global_lock) {
        f.CallExtern(f.builtins()->leave_global_lock);
      }
    }
    return 0;
  } else {
    // L = 0
    XEINSTRNOTIMPLEMENTED();
    return 1;
  }
}

void RegisterEmitCategoryControl() {
  XEREGISTERINSTR(bx);
  XEREGISTERINSTR(bcx);
  XEREGISTERINSTR(bcctrx);
  XEREGISTERINSTR(bclrx);
  XEREGISTERINSTR(crand);
  XEREGISTERINSTR(crandc);
  XEREGISTERINSTR(creqv);
  XEREGISTERINSTR(crnand);
  XEREGISTERINSTR(crnor);
  XEREGISTERINSTR(cror);
  XEREGISTERINSTR(crorc);
  XEREGISTERINSTR(crxor);
  XEREGISTERINSTR(mcrf);
  XEREGISTERINSTR(sc);
  XEREGISTERINSTR(td);
  XEREGISTERINSTR(tdi);
  XEREGISTERINSTR(tw);
  XEREGISTERINSTR(twi);
  XEREGISTERINSTR(mfcr);
  XEREGISTERINSTR(mfspr);
  XEREGISTERINSTR(mftb);
  XEREGISTERINSTR(mtcrf);
  XEREGISTERINSTR(mtspr);
  XEREGISTERINSTR(mfmsr);
  XEREGISTERINSTR(mtmsr);
  XEREGISTERINSTR(mtmsrd);
}

}  // namespace ppc
}  // namespace cpu
}  // namespace xe
