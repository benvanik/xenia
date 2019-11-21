/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// A note about vectors:
// Xenia represents vectors as xyzw pairs, with indices 0123.
// XMM registers are xyzw pairs with indices 3210, making them more like wzyx.
// This makes things somewhat confusing. It'd be nice to just shuffle the
// registers around on load/store, however certain operations require that
// data be in the right offset.
// Basically, this identity must hold:
//   shuffle(vec, b00011011) -> {x,y,z,w} => {x,y,z,w}
// All indices and operations must respect that.
//
// Memory (big endian):
// [00 01 02 03] [04 05 06 07] [08 09 0A 0B] [0C 0D 0E 0F] (x, y, z, w)
// load into xmm register:
// [0F 0E 0D 0C] [0B 0A 09 08] [07 06 05 04] [03 02 01 00] (w, z, y, x)

#include "xenia/cpu/backend/x64/x64_sequences.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>

#include "xenia/base/assert.h"
#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/base/threading.h"
#include "xenia/cpu/backend/x64/x64_emitter.h"
#include "xenia/cpu/backend/x64/x64_op.h"
#include "xenia/cpu/backend/x64/x64_tracers.h"
#include "xenia/cpu/hir/hir_builder.h"
#include "xenia/cpu/processor.h"

namespace xe {
namespace cpu {
namespace backend {
namespace x64 {

using namespace Xbyak;

// TODO(benvanik): direct usings.
using namespace xe::cpu;
using namespace xe::cpu::hir;

using xe::cpu::hir::Instr;

typedef bool (*SequenceSelectFn)(X64Emitter&, const Instr*);
std::unordered_map<uint32_t, SequenceSelectFn> sequence_table;

// ============================================================================
// OPCODE_COMMENT
// ============================================================================
struct COMMENT : Sequence<COMMENT, I<OPCODE_COMMENT, VoidOp, OffsetOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (IsTracingInstr()) {
      auto str = reinterpret_cast<const char*>(i.src1.value);
      // TODO(benvanik): pass through.
      // TODO(benvanik): don't just leak this memory.
      auto str_copy = strdup(str);
      e.mov(e.rdx, reinterpret_cast<uint64_t>(str_copy));
      e.CallNative(reinterpret_cast<void*>(TraceString));
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_COMMENT, COMMENT);

// ============================================================================
// OPCODE_NOP
// ============================================================================
struct NOP : Sequence<NOP, I<OPCODE_NOP, VoidOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) { e.nop(); }
};
EMITTER_OPCODE_TABLE(OPCODE_NOP, NOP);

// ============================================================================
// OPCODE_SOURCE_OFFSET
// ============================================================================
struct SOURCE_OFFSET
    : Sequence<SOURCE_OFFSET, I<OPCODE_SOURCE_OFFSET, VoidOp, OffsetOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.MarkSourceOffset(i.instr);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SOURCE_OFFSET, SOURCE_OFFSET);

// ============================================================================
// OPCODE_ASSIGN
// ============================================================================
struct ASSIGN_I8 : Sequence<ASSIGN_I8, I<OPCODE_ASSIGN, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.mov(i.dest, i.src1);
  }
};
struct ASSIGN_I16 : Sequence<ASSIGN_I16, I<OPCODE_ASSIGN, I16Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.mov(i.dest, i.src1);
  }
};
struct ASSIGN_I32 : Sequence<ASSIGN_I32, I<OPCODE_ASSIGN, I32Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.mov(i.dest, i.src1);
  }
};
struct ASSIGN_I64 : Sequence<ASSIGN_I64, I<OPCODE_ASSIGN, I64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.mov(i.dest, i.src1);
  }
};
struct ASSIGN_F32 : Sequence<ASSIGN_F32, I<OPCODE_ASSIGN, F32Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vmovaps(i.dest, i.src1);
  }
};
struct ASSIGN_F64 : Sequence<ASSIGN_F64, I<OPCODE_ASSIGN, F64Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vmovaps(i.dest, i.src1);
  }
};
struct ASSIGN_V128 : Sequence<ASSIGN_V128, I<OPCODE_ASSIGN, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vmovaps(i.dest, i.src1);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_ASSIGN, ASSIGN_I8, ASSIGN_I16, ASSIGN_I32,
                     ASSIGN_I64, ASSIGN_F32, ASSIGN_F64, ASSIGN_V128);

// ============================================================================
// OPCODE_CAST
// ============================================================================
struct CAST_I32_F32 : Sequence<CAST_I32_F32, I<OPCODE_CAST, I32Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vmovd(i.dest, i.src1);
  }
};
struct CAST_I64_F64 : Sequence<CAST_I64_F64, I<OPCODE_CAST, I64Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vmovq(i.dest, i.src1);
  }
};
struct CAST_F32_I32 : Sequence<CAST_F32_I32, I<OPCODE_CAST, F32Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vmovd(i.dest, i.src1);
  }
};
struct CAST_F64_I64 : Sequence<CAST_F64_I64, I<OPCODE_CAST, F64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vmovq(i.dest, i.src1);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_CAST, CAST_I32_F32, CAST_I64_F64, CAST_F32_I32,
                     CAST_F64_I64);

// ============================================================================
// OPCODE_ZERO_EXTEND
// ============================================================================
struct ZERO_EXTEND_I16_I8
    : Sequence<ZERO_EXTEND_I16_I8, I<OPCODE_ZERO_EXTEND, I16Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movzx(i.dest, i.src1);
  }
};
struct ZERO_EXTEND_I32_I8
    : Sequence<ZERO_EXTEND_I32_I8, I<OPCODE_ZERO_EXTEND, I32Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movzx(i.dest, i.src1);
  }
};
struct ZERO_EXTEND_I64_I8
    : Sequence<ZERO_EXTEND_I64_I8, I<OPCODE_ZERO_EXTEND, I64Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movzx(i.dest, i.src1);
  }
};
struct ZERO_EXTEND_I32_I16
    : Sequence<ZERO_EXTEND_I32_I16, I<OPCODE_ZERO_EXTEND, I32Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movzx(i.dest, i.src1);
  }
};
struct ZERO_EXTEND_I64_I16
    : Sequence<ZERO_EXTEND_I64_I16, I<OPCODE_ZERO_EXTEND, I64Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movzx(i.dest, i.src1);
  }
};
struct ZERO_EXTEND_I64_I32
    : Sequence<ZERO_EXTEND_I64_I32, I<OPCODE_ZERO_EXTEND, I64Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.mov(i.dest.reg().cvt32(), i.src1);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_ZERO_EXTEND, ZERO_EXTEND_I16_I8, ZERO_EXTEND_I32_I8,
                     ZERO_EXTEND_I64_I8, ZERO_EXTEND_I32_I16,
                     ZERO_EXTEND_I64_I16, ZERO_EXTEND_I64_I32);

// ============================================================================
// OPCODE_SIGN_EXTEND
// ============================================================================
struct SIGN_EXTEND_I16_I8
    : Sequence<SIGN_EXTEND_I16_I8, I<OPCODE_SIGN_EXTEND, I16Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movsx(i.dest, i.src1);
  }
};
struct SIGN_EXTEND_I32_I8
    : Sequence<SIGN_EXTEND_I32_I8, I<OPCODE_SIGN_EXTEND, I32Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movsx(i.dest, i.src1);
  }
};
struct SIGN_EXTEND_I64_I8
    : Sequence<SIGN_EXTEND_I64_I8, I<OPCODE_SIGN_EXTEND, I64Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movsx(i.dest, i.src1);
  }
};
struct SIGN_EXTEND_I32_I16
    : Sequence<SIGN_EXTEND_I32_I16, I<OPCODE_SIGN_EXTEND, I32Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movsx(i.dest, i.src1);
  }
};
struct SIGN_EXTEND_I64_I16
    : Sequence<SIGN_EXTEND_I64_I16, I<OPCODE_SIGN_EXTEND, I64Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movsx(i.dest, i.src1);
  }
};
struct SIGN_EXTEND_I64_I32
    : Sequence<SIGN_EXTEND_I64_I32, I<OPCODE_SIGN_EXTEND, I64Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movsxd(i.dest, i.src1);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SIGN_EXTEND, SIGN_EXTEND_I16_I8, SIGN_EXTEND_I32_I8,
                     SIGN_EXTEND_I64_I8, SIGN_EXTEND_I32_I16,
                     SIGN_EXTEND_I64_I16, SIGN_EXTEND_I64_I32);

// ============================================================================
// OPCODE_TRUNCATE
// ============================================================================
struct TRUNCATE_I8_I16
    : Sequence<TRUNCATE_I8_I16, I<OPCODE_TRUNCATE, I8Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movzx(i.dest.reg().cvt32(), i.src1.reg().cvt8());
  }
};
struct TRUNCATE_I8_I32
    : Sequence<TRUNCATE_I8_I32, I<OPCODE_TRUNCATE, I8Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movzx(i.dest.reg().cvt32(), i.src1.reg().cvt8());
  }
};
struct TRUNCATE_I8_I64
    : Sequence<TRUNCATE_I8_I64, I<OPCODE_TRUNCATE, I8Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movzx(i.dest.reg().cvt32(), i.src1.reg().cvt8());
  }
};
struct TRUNCATE_I16_I32
    : Sequence<TRUNCATE_I16_I32, I<OPCODE_TRUNCATE, I16Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movzx(i.dest.reg().cvt32(), i.src1.reg().cvt16());
  }
};
struct TRUNCATE_I16_I64
    : Sequence<TRUNCATE_I16_I64, I<OPCODE_TRUNCATE, I16Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movzx(i.dest.reg().cvt32(), i.src1.reg().cvt16());
  }
};
struct TRUNCATE_I32_I64
    : Sequence<TRUNCATE_I32_I64, I<OPCODE_TRUNCATE, I32Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.mov(i.dest, i.src1.reg().cvt32());
  }
};
EMITTER_OPCODE_TABLE(OPCODE_TRUNCATE, TRUNCATE_I8_I16, TRUNCATE_I8_I32,
                     TRUNCATE_I8_I64, TRUNCATE_I16_I32, TRUNCATE_I16_I64,
                     TRUNCATE_I32_I64);

// ============================================================================
// OPCODE_CONVERT
// ============================================================================
struct CONVERT_I32_F32
    : Sequence<CONVERT_I32_F32, I<OPCODE_CONVERT, I32Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // TODO(benvanik): saturation check? cvtt* (trunc?)
    if (i.instr->flags == ROUND_TO_ZERO) {
      e.vcvttss2si(i.dest, i.src1);
    } else {
      e.vcvtss2si(i.dest, i.src1);
    }
  }
};
struct CONVERT_I32_F64
    : Sequence<CONVERT_I32_F64, I<OPCODE_CONVERT, I32Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // Intel returns 0x80000000 if the double value does not fit within an int32
    // PPC saturates the value instead.
    // So, we can clamp the double value to (double)0x7FFFFFFF.
    e.vminsd(e.xmm0, i.src1, e.GetXmmConstPtr(XMMIntMaxPD));
    if (i.instr->flags == ROUND_TO_ZERO) {
      e.vcvttsd2si(i.dest, e.xmm0);
    } else {
      e.vcvtsd2si(i.dest, e.xmm0);
    }
  }
};
struct CONVERT_I64_F64
    : Sequence<CONVERT_I64_F64, I<OPCODE_CONVERT, I64Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // Copy src1.
    e.movq(e.rcx, i.src1);

    // TODO(benvanik): saturation check? cvtt* (trunc?)
    if (i.instr->flags == ROUND_TO_ZERO) {
      e.vcvttsd2si(i.dest, i.src1);
    } else {
      e.vcvtsd2si(i.dest, i.src1);
    }

    // 0x8000000000000000
    e.mov(e.rax, 0x1);
    e.shl(e.rax, 63);

    // Saturate positive overflow
    // TODO(DrChat): Find a shorter equivalent sequence.
    // if (result ind. && src1 >= 0)
    //   result = 0x7FFFFFFFFFFFFFFF;
    e.cmp(e.rax, i.dest);
    e.sete(e.al);
    e.movzx(e.rax, e.al);
    e.shr(e.rcx, 63);
    e.xor_(e.rcx, 0x01);
    e.and_(e.rax, e.rcx);

    e.sub(i.dest, e.rax);
  }
};
struct CONVERT_F32_I32
    : Sequence<CONVERT_F32_I32, I<OPCODE_CONVERT, F32Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // TODO(benvanik): saturation check? cvtt* (trunc?)
    e.vcvtsi2ss(i.dest, i.src1);
  }
};
struct CONVERT_F32_F64
    : Sequence<CONVERT_F32_F64, I<OPCODE_CONVERT, F32Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // TODO(benvanik): saturation check? cvtt* (trunc?)
    e.vcvtsd2ss(i.dest, i.src1);
  }
};
struct CONVERT_F64_I64
    : Sequence<CONVERT_F64_I64, I<OPCODE_CONVERT, F64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // TODO(benvanik): saturation check? cvtt* (trunc?)
    e.vcvtsi2sd(i.dest, i.src1);
  }
};
struct CONVERT_F64_F32
    : Sequence<CONVERT_F64_F32, I<OPCODE_CONVERT, F64Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vcvtss2sd(i.dest, i.src1);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_CONVERT, CONVERT_I32_F32, CONVERT_I32_F64,
                     CONVERT_I64_F64, CONVERT_F32_I32, CONVERT_F32_F64,
                     CONVERT_F64_I64, CONVERT_F64_F32);

// ============================================================================
// OPCODE_ROUND
// ============================================================================
struct ROUND_F32 : Sequence<ROUND_F32, I<OPCODE_ROUND, F32Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    switch (i.instr->flags) {
      case ROUND_TO_ZERO:
        e.vroundss(i.dest, i.src1, 0b00000011);
        break;
      case ROUND_TO_NEAREST:
        e.vroundss(i.dest, i.src1, 0b00000000);
        break;
      case ROUND_TO_MINUS_INFINITY:
        e.vroundss(i.dest, i.src1, 0b00000001);
        break;
      case ROUND_TO_POSITIVE_INFINITY:
        e.vroundss(i.dest, i.src1, 0b00000010);
        break;
    }
  }
};
struct ROUND_F64 : Sequence<ROUND_F64, I<OPCODE_ROUND, F64Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    switch (i.instr->flags) {
      case ROUND_TO_ZERO:
        e.vroundsd(i.dest, i.src1, 0b00000011);
        break;
      case ROUND_TO_NEAREST:
        e.vroundsd(i.dest, i.src1, 0b00000000);
        break;
      case ROUND_TO_MINUS_INFINITY:
        e.vroundsd(i.dest, i.src1, 0b00000001);
        break;
      case ROUND_TO_POSITIVE_INFINITY:
        e.vroundsd(i.dest, i.src1, 0b00000010);
        break;
    }
  }
};
struct ROUND_V128 : Sequence<ROUND_V128, I<OPCODE_ROUND, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    switch (i.instr->flags) {
      case ROUND_TO_ZERO:
        e.vroundps(i.dest, i.src1, 0b00000011);
        break;
      case ROUND_TO_NEAREST:
        e.vroundps(i.dest, i.src1, 0b00000000);
        break;
      case ROUND_TO_MINUS_INFINITY:
        e.vroundps(i.dest, i.src1, 0b00000001);
        break;
      case ROUND_TO_POSITIVE_INFINITY:
        e.vroundps(i.dest, i.src1, 0b00000010);
        break;
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_ROUND, ROUND_F32, ROUND_F64, ROUND_V128);

// ============================================================================
// OPCODE_LOAD_CLOCK
// ============================================================================
struct LOAD_CLOCK : Sequence<LOAD_CLOCK, I<OPCODE_LOAD_CLOCK, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // It'd be cool to call QueryPerformanceCounter directly, but w/e.
    e.CallNative(LoadClock);
    e.mov(i.dest, e.rax);
  }
  static uint64_t LoadClock(void* raw_context) {
    return Clock::QueryGuestTickCount();
  }
};
EMITTER_OPCODE_TABLE(OPCODE_LOAD_CLOCK, LOAD_CLOCK);

// ============================================================================
// OPCODE_CONTEXT_BARRIER
// ============================================================================
struct CONTEXT_BARRIER
    : Sequence<CONTEXT_BARRIER, I<OPCODE_CONTEXT_BARRIER, VoidOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {}
};
EMITTER_OPCODE_TABLE(OPCODE_CONTEXT_BARRIER, CONTEXT_BARRIER);

// ============================================================================
// OPCODE_MAX
// ============================================================================
struct MAX_F32 : Sequence<MAX_F32, I<OPCODE_MAX, F32Op, F32Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(e, i,
                               [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
                                 e.vmaxss(dest, src1, src2);
                               });
  }
};
struct MAX_F64 : Sequence<MAX_F64, I<OPCODE_MAX, F64Op, F64Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(e, i,
                               [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
                                 e.vmaxsd(dest, src1, src2);
                               });
  }
};
struct MAX_V128 : Sequence<MAX_V128, I<OPCODE_MAX, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(e, i,
                               [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
                                 e.vmaxps(dest, src1, src2);
                               });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_MAX, MAX_F32, MAX_F64, MAX_V128);

// ============================================================================
// OPCODE_MIN
// ============================================================================
struct MIN_I8 : Sequence<MIN_I8, I<OPCODE_MIN, I8Op, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryOp(
        e, i,
        [](X64Emitter& e, const Reg8& dest_src, const Reg8& src) {
          e.cmp(dest_src, src);
          e.cmovg(dest_src.cvt32(), src.cvt32());
        },
        [](X64Emitter& e, const Reg8& dest_src, int32_t constant) {
          e.mov(e.al, constant);
          e.cmp(dest_src, e.al);
          e.cmovg(dest_src.cvt32(), e.eax);
        });
  }
};
struct MIN_I16 : Sequence<MIN_I16, I<OPCODE_MIN, I16Op, I16Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryOp(
        e, i,
        [](X64Emitter& e, const Reg16& dest_src, const Reg16& src) {
          e.cmp(dest_src, src);
          e.cmovg(dest_src.cvt32(), src.cvt32());
        },
        [](X64Emitter& e, const Reg16& dest_src, int32_t constant) {
          e.mov(e.ax, constant);
          e.cmp(dest_src, e.ax);
          e.cmovg(dest_src.cvt32(), e.eax);
        });
  }
};
struct MIN_I32 : Sequence<MIN_I32, I<OPCODE_MIN, I32Op, I32Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryOp(
        e, i,
        [](X64Emitter& e, const Reg32& dest_src, const Reg32& src) {
          e.cmp(dest_src, src);
          e.cmovg(dest_src, src);
        },
        [](X64Emitter& e, const Reg32& dest_src, int32_t constant) {
          e.mov(e.eax, constant);
          e.cmp(dest_src, e.eax);
          e.cmovg(dest_src, e.eax);
        });
  }
};
struct MIN_I64 : Sequence<MIN_I64, I<OPCODE_MIN, I64Op, I64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryOp(
        e, i,
        [](X64Emitter& e, const Reg64& dest_src, const Reg64& src) {
          e.cmp(dest_src, src);
          e.cmovg(dest_src, src);
        },
        [](X64Emitter& e, const Reg64& dest_src, int64_t constant) {
          e.mov(e.rax, constant);
          e.cmp(dest_src, e.rax);
          e.cmovg(dest_src, e.rax);
        });
  }
};
struct MIN_F32 : Sequence<MIN_F32, I<OPCODE_MIN, F32Op, F32Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(e, i,
                               [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
                                 e.vminss(dest, src1, src2);
                               });
  }
};
struct MIN_F64 : Sequence<MIN_F64, I<OPCODE_MIN, F64Op, F64Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(e, i,
                               [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
                                 e.vminsd(dest, src1, src2);
                               });
  }
};
struct MIN_V128 : Sequence<MIN_V128, I<OPCODE_MIN, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(e, i,
                               [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
                                 e.vminps(dest, src1, src2);
                               });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_MIN, MIN_I8, MIN_I16, MIN_I32, MIN_I64, MIN_F32,
                     MIN_F64, MIN_V128);

// ============================================================================
// OPCODE_SELECT
// ============================================================================
// dest = src1 ? src2 : src3
// TODO(benvanik): match compare + select sequences, as often it's something
//     like SELECT(VECTOR_COMPARE_SGE(a, b), a, b)
struct SELECT_I8
    : Sequence<SELECT_I8, I<OPCODE_SELECT, I8Op, I8Op, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    Reg8 src2;
    if (i.src2.is_constant) {
      src2 = e.al;
      e.mov(src2, i.src2.constant());
    } else {
      src2 = i.src2;
    }
    e.test(i.src1, i.src1);
    e.cmovnz(i.dest.reg().cvt32(), src2.cvt32());
    e.cmovz(i.dest.reg().cvt32(), i.src3.reg().cvt32());
  }
};
struct SELECT_I16
    : Sequence<SELECT_I16, I<OPCODE_SELECT, I16Op, I8Op, I16Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    Reg16 src2;
    if (i.src2.is_constant) {
      src2 = e.ax;
      e.mov(src2, i.src2.constant());
    } else {
      src2 = i.src2;
    }
    e.test(i.src1, i.src1);
    e.cmovnz(i.dest.reg().cvt32(), src2.cvt32());
    e.cmovz(i.dest.reg().cvt32(), i.src3.reg().cvt32());
  }
};
struct SELECT_I32
    : Sequence<SELECT_I32, I<OPCODE_SELECT, I32Op, I8Op, I32Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    Reg32 src2;
    if (i.src2.is_constant) {
      src2 = e.eax;
      e.mov(src2, i.src2.constant());
    } else {
      src2 = i.src2;
    }
    e.test(i.src1, i.src1);
    e.cmovnz(i.dest, src2);
    e.cmovz(i.dest, i.src3);
  }
};
struct SELECT_I64
    : Sequence<SELECT_I64, I<OPCODE_SELECT, I64Op, I8Op, I64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    Reg64 src2;
    if (i.src2.is_constant) {
      src2 = e.rax;
      e.mov(src2, i.src2.constant());
    } else {
      src2 = i.src2;
    }
    e.test(i.src1, i.src1);
    e.cmovnz(i.dest, src2);
    e.cmovz(i.dest, i.src3);
  }
};
struct SELECT_F32
    : Sequence<SELECT_F32, I<OPCODE_SELECT, F32Op, I8Op, F32Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // TODO(benvanik): find a shorter sequence.
    // dest = src1 != 0 ? src2 : src3
    e.movzx(e.eax, i.src1);
    e.vmovd(e.xmm1, e.eax);
    e.vxorps(e.xmm0, e.xmm0);
    e.vpcmpeqd(e.xmm0, e.xmm1);

    Xmm src2 = i.src2.is_constant ? e.xmm2 : i.src2;
    if (i.src2.is_constant) {
      e.LoadConstantXmm(src2, i.src2.constant());
    }
    e.vpandn(e.xmm1, e.xmm0, src2);

    Xmm src3 = i.src3.is_constant ? e.xmm2 : i.src3;
    if (i.src3.is_constant) {
      e.LoadConstantXmm(src3, i.src3.constant());
    }
    e.vpand(i.dest, e.xmm0, src3);
    e.vpor(i.dest, e.xmm1);
  }
};
struct SELECT_F64
    : Sequence<SELECT_F64, I<OPCODE_SELECT, F64Op, I8Op, F64Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // dest = src1 != 0 ? src2 : src3
    e.movzx(e.eax, i.src1);
    e.vmovd(e.xmm1, e.eax);
    e.vpxor(e.xmm0, e.xmm0);
    e.vpcmpeqq(e.xmm0, e.xmm1);

    Xmm src2 = i.src2.is_constant ? e.xmm2 : i.src2;
    if (i.src2.is_constant) {
      e.LoadConstantXmm(src2, i.src2.constant());
    }
    e.vpandn(e.xmm1, e.xmm0, src2);

    Xmm src3 = i.src3.is_constant ? e.xmm2 : i.src3;
    if (i.src3.is_constant) {
      e.LoadConstantXmm(src3, i.src3.constant());
    }
    e.vpand(i.dest, e.xmm0, src3);
    e.vpor(i.dest, e.xmm1);
  }
};
struct SELECT_V128_I8
    : Sequence<SELECT_V128_I8, I<OPCODE_SELECT, V128Op, I8Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // TODO(benvanik): find a shorter sequence.
    // dest = src1 != 0 ? src2 : src3
    e.movzx(e.eax, i.src1);
    e.vmovd(e.xmm1, e.eax);
    e.vpbroadcastd(e.xmm1, e.xmm1);
    e.vxorps(e.xmm0, e.xmm0);
    e.vpcmpeqd(e.xmm0, e.xmm1);

    Xmm src2 = i.src2.is_constant ? e.xmm2 : i.src2;
    if (i.src2.is_constant) {
      e.LoadConstantXmm(src2, i.src2.constant());
    }
    e.vpandn(e.xmm1, e.xmm0, src2);

    Xmm src3 = i.src3.is_constant ? e.xmm2 : i.src3;
    if (i.src3.is_constant) {
      e.LoadConstantXmm(src3, i.src3.constant());
    }
    e.vpand(i.dest, e.xmm0, src3);
    e.vpor(i.dest, e.xmm1);
  }
};
struct SELECT_V128_V128
    : Sequence<SELECT_V128_V128,
               I<OPCODE_SELECT, V128Op, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    Xmm src1 = i.src1.is_constant ? e.xmm0 : i.src1;
    if (i.src1.is_constant) {
      e.LoadConstantXmm(src1, i.src1.constant());
    }

    Xmm src2 = i.src2.is_constant ? e.xmm1 : i.src2;
    if (i.src2.is_constant) {
      e.LoadConstantXmm(src2, i.src2.constant());
    }

    Xmm src3 = i.src3.is_constant ? e.xmm2 : i.src3;
    if (i.src3.is_constant) {
      e.LoadConstantXmm(src3, i.src3.constant());
    }

    // src1 ? src2 : src3;
    e.vpandn(e.xmm3, src1, src2);
    e.vpand(i.dest, src1, src3);
    e.vpor(i.dest, i.dest, e.xmm3);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SELECT, SELECT_I8, SELECT_I16, SELECT_I32,
                     SELECT_I64, SELECT_F32, SELECT_F64, SELECT_V128_I8,
                     SELECT_V128_V128);

// ============================================================================
// OPCODE_IS_TRUE
// ============================================================================
struct IS_TRUE_I8 : Sequence<IS_TRUE_I8, I<OPCODE_IS_TRUE, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.setnz(i.dest);
  }
};
struct IS_TRUE_I16 : Sequence<IS_TRUE_I16, I<OPCODE_IS_TRUE, I8Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.setnz(i.dest);
  }
};
struct IS_TRUE_I32 : Sequence<IS_TRUE_I32, I<OPCODE_IS_TRUE, I8Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.setnz(i.dest);
  }
};
struct IS_TRUE_I64 : Sequence<IS_TRUE_I64, I<OPCODE_IS_TRUE, I8Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.setnz(i.dest);
  }
};
struct IS_TRUE_F32 : Sequence<IS_TRUE_F32, I<OPCODE_IS_TRUE, I8Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vptest(i.src1, i.src1);
    e.setnz(i.dest);
  }
};
struct IS_TRUE_F64 : Sequence<IS_TRUE_F64, I<OPCODE_IS_TRUE, I8Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vptest(i.src1, i.src1);
    e.setnz(i.dest);
  }
};
struct IS_TRUE_V128 : Sequence<IS_TRUE_V128, I<OPCODE_IS_TRUE, I8Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vptest(i.src1, i.src1);
    e.setnz(i.dest);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_IS_TRUE, IS_TRUE_I8, IS_TRUE_I16, IS_TRUE_I32,
                     IS_TRUE_I64, IS_TRUE_F32, IS_TRUE_F64, IS_TRUE_V128);

// ============================================================================
// OPCODE_IS_FALSE
// ============================================================================
struct IS_FALSE_I8 : Sequence<IS_FALSE_I8, I<OPCODE_IS_FALSE, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.setz(i.dest);
  }
};
struct IS_FALSE_I16 : Sequence<IS_FALSE_I16, I<OPCODE_IS_FALSE, I8Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.setz(i.dest);
  }
};
struct IS_FALSE_I32 : Sequence<IS_FALSE_I32, I<OPCODE_IS_FALSE, I8Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.setz(i.dest);
  }
};
struct IS_FALSE_I64 : Sequence<IS_FALSE_I64, I<OPCODE_IS_FALSE, I8Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.setz(i.dest);
  }
};
struct IS_FALSE_F32 : Sequence<IS_FALSE_F32, I<OPCODE_IS_FALSE, I8Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vptest(i.src1, i.src1);
    e.setz(i.dest);
  }
};
struct IS_FALSE_F64 : Sequence<IS_FALSE_F64, I<OPCODE_IS_FALSE, I8Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vptest(i.src1, i.src1);
    e.setz(i.dest);
  }
};
struct IS_FALSE_V128
    : Sequence<IS_FALSE_V128, I<OPCODE_IS_FALSE, I8Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vptest(i.src1, i.src1);
    e.setz(i.dest);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_IS_FALSE, IS_FALSE_I8, IS_FALSE_I16, IS_FALSE_I32,
                     IS_FALSE_I64, IS_FALSE_F32, IS_FALSE_F64, IS_FALSE_V128);

// ============================================================================
// OPCODE_IS_NAN
// ============================================================================
struct IS_NAN_F32 : Sequence<IS_NAN_F32, I<OPCODE_IS_NAN, I8Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vucomiss(i.src1, i.src1);
    e.setp(i.dest);
  }
};

struct IS_NAN_F64 : Sequence<IS_NAN_F64, I<OPCODE_IS_NAN, I8Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vucomisd(i.src1, i.src1);
    e.setp(i.dest);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_IS_NAN, IS_NAN_F32, IS_NAN_F64);

// ============================================================================
// OPCODE_COMPARE_EQ
// ============================================================================
struct COMPARE_EQ_I8
    : Sequence<COMPARE_EQ_I8, I<OPCODE_COMPARE_EQ, I8Op, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeCompareOp(e, i,
                             [](X64Emitter& e, const Reg8& src1,
                                const Reg8& src2) { e.cmp(src1, src2); },
                             [](X64Emitter& e, const Reg8& src1,
                                int32_t constant) { e.cmp(src1, constant); });
    e.sete(i.dest);
  }
};
struct COMPARE_EQ_I16
    : Sequence<COMPARE_EQ_I16, I<OPCODE_COMPARE_EQ, I8Op, I16Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeCompareOp(e, i,
                             [](X64Emitter& e, const Reg16& src1,
                                const Reg16& src2) { e.cmp(src1, src2); },
                             [](X64Emitter& e, const Reg16& src1,
                                int32_t constant) { e.cmp(src1, constant); });
    e.sete(i.dest);
  }
};
struct COMPARE_EQ_I32
    : Sequence<COMPARE_EQ_I32, I<OPCODE_COMPARE_EQ, I8Op, I32Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeCompareOp(e, i,
                             [](X64Emitter& e, const Reg32& src1,
                                const Reg32& src2) { e.cmp(src1, src2); },
                             [](X64Emitter& e, const Reg32& src1,
                                int32_t constant) { e.cmp(src1, constant); });
    e.sete(i.dest);
  }
};
struct COMPARE_EQ_I64
    : Sequence<COMPARE_EQ_I64, I<OPCODE_COMPARE_EQ, I8Op, I64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeCompareOp(e, i,
                             [](X64Emitter& e, const Reg64& src1,
                                const Reg64& src2) { e.cmp(src1, src2); },
                             [](X64Emitter& e, const Reg64& src1,
                                int32_t constant) { e.cmp(src1, constant); });
    e.sete(i.dest);
  }
};
struct COMPARE_EQ_F32
    : Sequence<COMPARE_EQ_F32, I<OPCODE_COMPARE_EQ, I8Op, F32Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(
        e, i, [&i](X64Emitter& e, I8Op dest, const Xmm& src1, const Xmm& src2) {
          e.vcomiss(src1, src2);
        });
    e.sete(i.dest);
  }
};
struct COMPARE_EQ_F64
    : Sequence<COMPARE_EQ_F64, I<OPCODE_COMPARE_EQ, I8Op, F64Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(
        e, i, [&i](X64Emitter& e, I8Op dest, const Xmm& src1, const Xmm& src2) {
          e.vcomisd(src1, src2);
        });
    e.sete(i.dest);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_COMPARE_EQ, COMPARE_EQ_I8, COMPARE_EQ_I16,
                     COMPARE_EQ_I32, COMPARE_EQ_I64, COMPARE_EQ_F32,
                     COMPARE_EQ_F64);

// ============================================================================
// OPCODE_COMPARE_NE
// ============================================================================
struct COMPARE_NE_I8
    : Sequence<COMPARE_NE_I8, I<OPCODE_COMPARE_NE, I8Op, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeCompareOp(e, i,
                             [](X64Emitter& e, const Reg8& src1,
                                const Reg8& src2) { e.cmp(src1, src2); },
                             [](X64Emitter& e, const Reg8& src1,
                                int32_t constant) { e.cmp(src1, constant); });
    e.setne(i.dest);
  }
};
struct COMPARE_NE_I16
    : Sequence<COMPARE_NE_I16, I<OPCODE_COMPARE_NE, I8Op, I16Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeCompareOp(e, i,
                             [](X64Emitter& e, const Reg16& src1,
                                const Reg16& src2) { e.cmp(src1, src2); },
                             [](X64Emitter& e, const Reg16& src1,
                                int32_t constant) { e.cmp(src1, constant); });
    e.setne(i.dest);
  }
};
struct COMPARE_NE_I32
    : Sequence<COMPARE_NE_I32, I<OPCODE_COMPARE_NE, I8Op, I32Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeCompareOp(e, i,
                             [](X64Emitter& e, const Reg32& src1,
                                const Reg32& src2) { e.cmp(src1, src2); },
                             [](X64Emitter& e, const Reg32& src1,
                                int32_t constant) { e.cmp(src1, constant); });
    e.setne(i.dest);
  }
};
struct COMPARE_NE_I64
    : Sequence<COMPARE_NE_I64, I<OPCODE_COMPARE_NE, I8Op, I64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeCompareOp(e, i,
                             [](X64Emitter& e, const Reg64& src1,
                                const Reg64& src2) { e.cmp(src1, src2); },
                             [](X64Emitter& e, const Reg64& src1,
                                int32_t constant) { e.cmp(src1, constant); });
    e.setne(i.dest);
  }
};
struct COMPARE_NE_F32
    : Sequence<COMPARE_NE_F32, I<OPCODE_COMPARE_NE, I8Op, F32Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vcomiss(i.src1, i.src2);
    e.setne(i.dest);
  }
};
struct COMPARE_NE_F64
    : Sequence<COMPARE_NE_F64, I<OPCODE_COMPARE_NE, I8Op, F64Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vcomisd(i.src1, i.src2);
    e.setne(i.dest);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_COMPARE_NE, COMPARE_NE_I8, COMPARE_NE_I16,
                     COMPARE_NE_I32, COMPARE_NE_I64, COMPARE_NE_F32,
                     COMPARE_NE_F64);

// ============================================================================
// OPCODE_COMPARE_*
// ============================================================================
#define EMITTER_ASSOCIATIVE_COMPARE_INT(op, instr, inverse_instr, type, \
                                        reg_type)                       \
  struct COMPARE_##op##_##type                                          \
      : Sequence<COMPARE_##op##_##type,                                 \
                 I<OPCODE_COMPARE_##op, I8Op, type, type>> {            \
    static void Emit(X64Emitter& e, const EmitArgType& i) {             \
      EmitAssociativeCompareOp(                                         \
          e, i,                                                         \
          [](X64Emitter& e, const Reg8& dest, const reg_type& src1,     \
             const reg_type& src2, bool inverse) {                      \
            e.cmp(src1, src2);                                          \
            if (!inverse) {                                             \
              e.instr(dest);                                            \
            } else {                                                    \
              e.inverse_instr(dest);                                    \
            }                                                           \
          },                                                            \
          [](X64Emitter& e, const Reg8& dest, const reg_type& src1,     \
             int32_t constant, bool inverse) {                          \
            e.cmp(src1, constant);                                      \
            if (!inverse) {                                             \
              e.instr(dest);                                            \
            } else {                                                    \
              e.inverse_instr(dest);                                    \
            }                                                           \
          });                                                           \
    }                                                                   \
  };
#define EMITTER_ASSOCIATIVE_COMPARE_XX(op, instr, inverse_instr)           \
  EMITTER_ASSOCIATIVE_COMPARE_INT(op, instr, inverse_instr, I8Op, Reg8);   \
  EMITTER_ASSOCIATIVE_COMPARE_INT(op, instr, inverse_instr, I16Op, Reg16); \
  EMITTER_ASSOCIATIVE_COMPARE_INT(op, instr, inverse_instr, I32Op, Reg32); \
  EMITTER_ASSOCIATIVE_COMPARE_INT(op, instr, inverse_instr, I64Op, Reg64); \
  EMITTER_OPCODE_TABLE(OPCODE_COMPARE_##op, COMPARE_##op##_I8Op,           \
                       COMPARE_##op##_I16Op, COMPARE_##op##_I32Op,         \
                       COMPARE_##op##_I64Op);
EMITTER_ASSOCIATIVE_COMPARE_XX(SLT, setl, setg);
EMITTER_ASSOCIATIVE_COMPARE_XX(SLE, setle, setge);
EMITTER_ASSOCIATIVE_COMPARE_XX(SGT, setg, setl);
EMITTER_ASSOCIATIVE_COMPARE_XX(SGE, setge, setle);
EMITTER_ASSOCIATIVE_COMPARE_XX(ULT, setb, seta);
EMITTER_ASSOCIATIVE_COMPARE_XX(ULE, setbe, setae);
EMITTER_ASSOCIATIVE_COMPARE_XX(UGT, seta, setb);
EMITTER_ASSOCIATIVE_COMPARE_XX(UGE, setae, setbe);

// https://web.archive.org/web/20171129015931/https://x86.renejeschke.de/html/file_module_x86_id_288.html
// Original link: https://x86.renejeschke.de/html/file_module_x86_id_288.html
#define EMITTER_ASSOCIATIVE_COMPARE_FLT_XX(op, instr)                 \
  struct COMPARE_##op##_F32                                           \
      : Sequence<COMPARE_##op##_F32,                                  \
                 I<OPCODE_COMPARE_##op, I8Op, F32Op, F32Op>> {        \
    static void Emit(X64Emitter& e, const EmitArgType& i) {           \
      e.vcomiss(i.src1, i.src2);                                      \
      e.instr(i.dest);                                                \
    }                                                                 \
  };                                                                  \
  struct COMPARE_##op##_F64                                           \
      : Sequence<COMPARE_##op##_F64,                                  \
                 I<OPCODE_COMPARE_##op, I8Op, F64Op, F64Op>> {        \
    static void Emit(X64Emitter& e, const EmitArgType& i) {           \
      if (i.src1.is_constant) {                                       \
        e.LoadConstantXmm(e.xmm0, i.src1.constant());                 \
        e.vcomisd(e.xmm0, i.src2);                                    \
      } else if (i.src2.is_constant) {                                \
        e.LoadConstantXmm(e.xmm0, i.src2.constant());                 \
        e.vcomisd(i.src1, e.xmm0);                                    \
      } else {                                                        \
        e.vcomisd(i.src1, i.src2);                                    \
      }                                                               \
      e.instr(i.dest);                                                \
    }                                                                 \
  };                                                                  \
  EMITTER_OPCODE_TABLE(OPCODE_COMPARE_##op##_FLT, COMPARE_##op##_F32, \
                       COMPARE_##op##_F64);
EMITTER_ASSOCIATIVE_COMPARE_FLT_XX(SLT, setb);
EMITTER_ASSOCIATIVE_COMPARE_FLT_XX(SLE, setbe);
EMITTER_ASSOCIATIVE_COMPARE_FLT_XX(SGT, seta);
EMITTER_ASSOCIATIVE_COMPARE_FLT_XX(SGE, setae);
EMITTER_ASSOCIATIVE_COMPARE_FLT_XX(ULT, setb);
EMITTER_ASSOCIATIVE_COMPARE_FLT_XX(ULE, setbe);
EMITTER_ASSOCIATIVE_COMPARE_FLT_XX(UGT, seta);
EMITTER_ASSOCIATIVE_COMPARE_FLT_XX(UGE, setae);

// ============================================================================
// OPCODE_DID_SATURATE
// ============================================================================
struct DID_SATURATE
    : Sequence<DID_SATURATE, I<OPCODE_DID_SATURATE, I8Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // TODO(benvanik): implement saturation check (VECTOR_ADD, etc).
    e.xor_(i.dest, i.dest);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_DID_SATURATE, DID_SATURATE);

// ============================================================================
// OPCODE_ADD
// ============================================================================
// TODO(benvanik): put dest/src1|2 together.
template <typename SEQ, typename REG, typename ARGS>
void EmitAddXX(X64Emitter& e, const ARGS& i) {
  SEQ::EmitCommutativeBinaryOp(
      e, i,
      [](X64Emitter& e, const REG& dest_src, const REG& src) {
        e.add(dest_src, src);
      },
      [](X64Emitter& e, const REG& dest_src, int32_t constant) {
        e.add(dest_src, constant);
      });
}
struct ADD_I8 : Sequence<ADD_I8, I<OPCODE_ADD, I8Op, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant && i.dest.reg() == i.src1.reg() &&
        i.src2.value->constant.u8 == 1) {
      e.inc(i.dest.reg());
    } else {
      EmitAddXX<ADD_I8, Reg8>(e, i);
    }
  }
};
struct ADD_I16 : Sequence<ADD_I16, I<OPCODE_ADD, I16Op, I16Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant && i.dest.reg() == i.src1.reg() &&
        i.src2.value->constant.u16 == 1) {
      e.inc(i.dest.reg());
    } else {
      EmitAddXX<ADD_I16, Reg16>(e, i);
    }
  }
};
struct ADD_I32 : Sequence<ADD_I32, I<OPCODE_ADD, I32Op, I32Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant && i.dest.reg() == i.src1.reg() &&
        i.src2.value->constant.u32 == 1) {
      e.inc(i.dest.reg());

    } else {
      EmitAddXX<ADD_I32, Reg32>(e, i);
    }
  }
};
struct ADD_I64 : Sequence<ADD_I64, I<OPCODE_ADD, I64Op, I64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant && i.dest.reg() == i.src1.reg() &&
        i.src2.value->constant.u64 == 1) {
      e.inc(i.dest.reg());
    } else {
      EmitAddXX<ADD_I64, Reg64>(e, i);
    }
  }
};
struct ADD_F32 : Sequence<ADD_F32, I<OPCODE_ADD, F32Op, F32Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(e, i,
                               [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
                                 e.vaddss(dest, src1, src2);
                               });
  }
};
struct ADD_F64 : Sequence<ADD_F64, I<OPCODE_ADD, F64Op, F64Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(e, i,
                               [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
                                 e.vaddsd(dest, src1, src2);
                               });
  }
};
struct ADD_V128 : Sequence<ADD_V128, I<OPCODE_ADD, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(e, i,
                               [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
                                 e.vaddps(dest, src1, src2);
                               });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_ADD, ADD_I8, ADD_I16, ADD_I32, ADD_I64, ADD_F32,
                     ADD_F64, ADD_V128);

// ============================================================================
// OPCODE_ADD_CARRY
// ============================================================================
// TODO(benvanik): put dest/src1|2 together.
template <typename SEQ, typename REG, typename ARGS>
void EmitAddCarryXX(X64Emitter& e, const ARGS& i) {
  // TODO(benvanik): faster setting? we could probably do some fun math tricks
  // here to get the carry flag set.
  if (i.src3.is_constant) {
    if (i.src3.constant()) {
      e.stc();
    } else {
      e.clc();
    }
  } else {
    if (i.src3.reg().getIdx() <= 4) {
      // Can move from A/B/C/DX to AH.
      e.mov(e.ah, i.src3.reg().cvt8());
    } else {
      e.mov(e.al, i.src3);
      e.mov(e.ah, e.al);
    }
    e.sahf();
  }
  SEQ::EmitCommutativeBinaryOp(
      e, i,
      [](X64Emitter& e, const REG& dest_src, const REG& src) {
        e.adc(dest_src, src);
      },
      [](X64Emitter& e, const REG& dest_src, int32_t constant) {
        e.adc(dest_src, constant);
      });
}
struct ADD_CARRY_I8
    : Sequence<ADD_CARRY_I8, I<OPCODE_ADD_CARRY, I8Op, I8Op, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitAddCarryXX<ADD_CARRY_I8, Reg8>(e, i);
  }
};
struct ADD_CARRY_I16
    : Sequence<ADD_CARRY_I16, I<OPCODE_ADD_CARRY, I16Op, I16Op, I16Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitAddCarryXX<ADD_CARRY_I16, Reg16>(e, i);
  }
};
struct ADD_CARRY_I32
    : Sequence<ADD_CARRY_I32, I<OPCODE_ADD_CARRY, I32Op, I32Op, I32Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitAddCarryXX<ADD_CARRY_I32, Reg32>(e, i);
  }
};
struct ADD_CARRY_I64
    : Sequence<ADD_CARRY_I64, I<OPCODE_ADD_CARRY, I64Op, I64Op, I64Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitAddCarryXX<ADD_CARRY_I64, Reg64>(e, i);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_ADD_CARRY, ADD_CARRY_I8, ADD_CARRY_I16,
                     ADD_CARRY_I32, ADD_CARRY_I64);

// ============================================================================
// OPCODE_SUB
// ============================================================================
// TODO(benvanik): put dest/src1|2 together.
template <typename SEQ, typename REG, typename ARGS>
void EmitSubXX(X64Emitter& e, const ARGS& i) {
  SEQ::EmitAssociativeBinaryOp(
      e, i,
      [](X64Emitter& e, const REG& dest_src, const REG& src) {
        e.sub(dest_src, src);
      },
      [](X64Emitter& e, const REG& dest_src, int32_t constant) {
        e.sub(dest_src, constant);
      });
}
struct SUB_I8 : Sequence<SUB_I8, I<OPCODE_SUB, I8Op, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant && i.dest.reg() == i.src1.reg() &&
        i.src2.value->constant.u8 == 1) {
      e.dec(i.dest.reg());
    } else {
      EmitSubXX<SUB_I8, Reg8>(e, i);
    }
  }
};
struct SUB_I16 : Sequence<SUB_I16, I<OPCODE_SUB, I16Op, I16Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant && i.dest.reg() == i.src1.reg() &&
        i.src2.value->constant.u16 == 1) {
      e.dec(i.dest.reg());
    } else {
      EmitSubXX<SUB_I16, Reg16>(e, i);
    }
  }
};
struct SUB_I32 : Sequence<SUB_I32, I<OPCODE_SUB, I32Op, I32Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant && i.dest.reg() == i.src1.reg() &&
        i.src2.value->constant.u32 == 1) {
      e.dec(i.dest.reg());
    } else {
      EmitSubXX<SUB_I32, Reg32>(e, i);
    }
  }
};
struct SUB_I64 : Sequence<SUB_I64, I<OPCODE_SUB, I64Op, I64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant && i.dest.reg() == i.src1.reg() &&
        i.src2.value->constant.u64 == 1) {
      e.dec(i.dest.reg());
    } else {
      EmitSubXX<SUB_I64, Reg64>(e, i);
    }
  }
};
struct SUB_F32 : Sequence<SUB_F32, I<OPCODE_SUB, F32Op, F32Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    assert_true(!i.instr->flags);
    EmitAssociativeBinaryXmmOp(e, i,
                               [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
                                 e.vsubss(dest, src1, src2);
                               });
  }
};
struct SUB_F64 : Sequence<SUB_F64, I<OPCODE_SUB, F64Op, F64Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    assert_true(!i.instr->flags);
    EmitAssociativeBinaryXmmOp(e, i,
                               [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
                                 e.vsubsd(dest, src1, src2);
                               });
  }
};
struct SUB_V128 : Sequence<SUB_V128, I<OPCODE_SUB, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    assert_true(!i.instr->flags);
    EmitAssociativeBinaryXmmOp(e, i,
                               [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
                                 e.vsubps(dest, src1, src2);
                               });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SUB, SUB_I8, SUB_I16, SUB_I32, SUB_I64, SUB_F32,
                     SUB_F64, SUB_V128);

// ============================================================================
// OPCODE_MUL
// ============================================================================
// Sign doesn't matter here, as we don't use the high bits.
// We exploit mulx here to avoid creating too much register pressure.
struct MUL_I8 : Sequence<MUL_I8, I<OPCODE_MUL, I8Op, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (e.IsFeatureEnabled(kX64EmitBMI2)) {
      // mulx: $1:$2 = EDX * $3

      // TODO(benvanik): place src2 in edx?
      if (i.src1.is_constant) {
        assert_true(!i.src2.is_constant);
        e.movzx(e.edx, i.src2);
        e.mov(e.eax, static_cast<uint8_t>(i.src1.constant()));
        e.mulx(e.edx, i.dest.reg().cvt32(), e.eax);
      } else if (i.src2.is_constant) {
        e.movzx(e.edx, i.src1);
        e.mov(e.eax, static_cast<uint8_t>(i.src2.constant()));
        e.mulx(e.edx, i.dest.reg().cvt32(), e.eax);
      } else {
        e.movzx(e.edx, i.src2);
        e.mulx(e.edx, i.dest.reg().cvt32(), i.src1.reg().cvt32());
      }
    } else {
      // x86 mul instruction
      // AH:AL = AL * $1;

      if (i.src1.is_constant) {
        assert_true(!i.src2.is_constant);
        e.mov(e.al, i.src1.constant());
        e.mul(i.src2);
        e.mov(i.dest, e.al);
      } else if (i.src2.is_constant) {
        assert_true(!i.src1.is_constant);
        e.mov(e.al, i.src2.constant());
        e.mul(i.src1);
        e.mov(i.dest, e.al);
      } else {
        e.movzx(e.al, i.src1);
        e.mul(i.src2);
        e.mov(i.dest, e.al);
      }
    }
  }
};
struct MUL_I16 : Sequence<MUL_I16, I<OPCODE_MUL, I16Op, I16Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (e.IsFeatureEnabled(kX64EmitBMI2)) {
      // mulx: $1:$2 = EDX * $3

      // TODO(benvanik): place src2 in edx?
      if (i.src1.is_constant) {
        assert_true(!i.src2.is_constant);
        e.movzx(e.edx, i.src2);
        e.mov(e.ax, static_cast<uint16_t>(i.src1.constant()));
        e.mulx(e.edx, i.dest.reg().cvt32(), e.eax);
      } else if (i.src2.is_constant) {
        e.movzx(e.edx, i.src1);
        e.mov(e.ax, static_cast<uint16_t>(i.src2.constant()));
        e.mulx(e.edx, i.dest.reg().cvt32(), e.eax);
      } else {
        e.movzx(e.edx, i.src2);
        e.mulx(e.edx, i.dest.reg().cvt32(), i.src1.reg().cvt32());
      }
    } else {
      // x86 mul instruction
      // DX:AX = AX * $1;

      if (i.src1.is_constant) {
        assert_true(!i.src2.is_constant);
        e.mov(e.ax, i.src1.constant());
        e.mul(i.src2);
        e.movzx(i.dest, e.ax);
      } else if (i.src2.is_constant) {
        assert_true(!i.src1.is_constant);
        e.mov(e.ax, i.src2.constant());
        e.mul(i.src1);
        e.movzx(i.dest, e.ax);
      } else {
        e.movzx(e.ax, i.src1);
        e.mul(i.src2);
        e.movzx(i.dest, e.ax);
      }
    }
  }
};
struct MUL_I32 : Sequence<MUL_I32, I<OPCODE_MUL, I32Op, I32Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      uint32_t multiplier = i.src2.value->constant.u32;
      if (multiplier == 3 || multiplier == 5 || multiplier == 9) {
        e.lea(i.dest, e.ptr[i.src1.reg() * (multiplier - 1) + i.src1.reg()]);
        return;
      }
    }
    if (e.IsFeatureEnabled(kX64EmitBMI2)) {
      // mulx: $1:$2 = EDX * $3

      // TODO(benvanik): place src2 in edx?
      if (i.src1.is_constant) {
        assert_true(!i.src2.is_constant);
        e.mov(e.edx, i.src2);
        e.mov(e.eax, i.src1.constant());
        e.mulx(e.edx, i.dest, e.eax);
      } else if (i.src2.is_constant) {
        e.mov(e.edx, i.src1);
        e.mov(e.eax, i.src2.constant());
        e.mulx(e.edx, i.dest, e.eax);
      } else {
        e.mov(e.edx, i.src2);
        e.mulx(e.edx, i.dest, i.src1);
      }
    } else {
      // x86 mul instruction
      // EDX:EAX = EAX * $1;

      // is_constant AKA not a register
      if (i.src1.is_constant) {
        assert_true(!i.src2.is_constant);  // can't multiply 2 constants
        e.mov(e.eax, i.src1.constant());
        e.mul(i.src2);
        e.mov(i.dest, e.eax);
      } else if (i.src2.is_constant) {
        assert_true(!i.src1.is_constant);  // can't multiply 2 constants
        e.mov(e.eax, i.src2.constant());
        e.mul(i.src1);
        e.mov(i.dest, e.eax);
      } else {
        e.mov(e.eax, i.src1);
        e.mul(i.src2);
        e.mov(i.dest, e.eax);
      }
    }
  }
};
struct MUL_I64 : Sequence<MUL_I64, I<OPCODE_MUL, I64Op, I64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      uint64_t multiplier = i.src2.value->constant.u64;
      if (multiplier == 3 || multiplier == 5 || multiplier == 9) {
        e.lea(i.dest, e.ptr[i.src1.reg() * ((int)multiplier - 1) + i.src1.reg()]);
        return;
      }
    }
    if (e.IsFeatureEnabled(kX64EmitBMI2)) {
      // mulx: $1:$2 = RDX * $3

      // TODO(benvanik): place src2 in edx?
      if (i.src1.is_constant) {
        assert_true(!i.src2.is_constant);
        e.mov(e.rdx, i.src2);
        e.mov(e.rax, i.src1.constant());
        e.mulx(e.rdx, i.dest, e.rax);
      } else if (i.src2.is_constant) {
        e.mov(e.rdx, i.src1);
        e.mov(e.rax, i.src2.constant());
        e.mulx(e.rdx, i.dest, e.rax);
      } else {
        e.mov(e.rdx, i.src2);
        e.mulx(e.rdx, i.dest, i.src1);
      }
    } else {
      // x86 mul instruction
      // RDX:RAX = RAX * $1;

      if (i.src1.is_constant) {
        assert_true(!i.src2.is_constant);  // can't multiply 2 constants
        e.mov(e.rax, i.src1.constant());
        e.mul(i.src2);
        e.mov(i.dest, e.rax);
      } else if (i.src2.is_constant) {
        assert_true(!i.src1.is_constant);  // can't multiply 2 constants
        e.mov(e.rax, i.src2.constant());
        e.mul(i.src1);
        e.mov(i.dest, e.rax);
      } else {
        e.mov(e.rax, i.src1);
        e.mul(i.src2);
        e.mov(i.dest, e.rax);
      }
    }
  }
};
struct MUL_F32 : Sequence<MUL_F32, I<OPCODE_MUL, F32Op, F32Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    assert_true(!i.instr->flags);
    EmitCommutativeBinaryXmmOp(e, i,
                               [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
                                 e.vmulss(dest, src1, src2);
                               });
  }
};
struct MUL_F64 : Sequence<MUL_F64, I<OPCODE_MUL, F64Op, F64Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    assert_true(!i.instr->flags);
    EmitCommutativeBinaryXmmOp(e, i,
                               [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
                                 e.vmulsd(dest, src1, src2);
                               });
  }
};
struct MUL_V128 : Sequence<MUL_V128, I<OPCODE_MUL, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    assert_true(!i.instr->flags);
    EmitCommutativeBinaryXmmOp(e, i,
                               [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
                                 e.vmulps(dest, src1, src2);
                               });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_MUL, MUL_I8, MUL_I16, MUL_I32, MUL_I64, MUL_F32,
                     MUL_F64, MUL_V128);

// ============================================================================
// OPCODE_MUL_HI
// ============================================================================
struct MUL_HI_I8 : Sequence<MUL_HI_I8, I<OPCODE_MUL_HI, I8Op, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.instr->flags & ARITHMETIC_UNSIGNED) {
      // mulx: $1:$2 = EDX * $3

      if (e.IsFeatureEnabled(kX64EmitBMI2)) {
        // TODO(benvanik): place src1 in eax? still need to sign extend
        e.movzx(e.edx, i.src1);
        e.mulx(i.dest.reg().cvt32(), e.eax, i.src2.reg().cvt32());
      } else {
        // x86 mul instruction
        // AH:AL = AL * $1;
        if (i.src1.is_constant) {
          assert_true(!i.src2.is_constant);  // can't multiply 2 constants
          e.mov(e.al, i.src1.constant());
          e.mul(i.src2);
          e.mov(i.dest, e.ah);
        } else if (i.src2.is_constant) {
          assert_true(!i.src1.is_constant);  // can't multiply 2 constants
          e.mov(e.al, i.src2.constant());
          e.mul(i.src1);
          e.mov(i.dest, e.ah);
        } else {
          e.mov(e.al, i.src1);
          e.mul(i.src2);
          e.mov(i.dest, e.ah);
        }
      }
    } else {
      if (i.src1.is_constant) {
        e.mov(e.al, i.src1.constant());
      } else {
        e.mov(e.al, i.src1);
      }
      if (i.src2.is_constant) {
        e.mov(e.al, i.src2.constant());
        e.imul(e.al);
      } else {
        e.imul(i.src2);
      }
      e.mov(i.dest, e.ah);
    }
  }
};
struct MUL_HI_I16
    : Sequence<MUL_HI_I16, I<OPCODE_MUL_HI, I16Op, I16Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.instr->flags & ARITHMETIC_UNSIGNED) {
      if (e.IsFeatureEnabled(kX64EmitBMI2)) {
        // TODO(benvanik): place src1 in eax? still need to sign extend
        e.movzx(e.edx, i.src1);
        e.mulx(i.dest.reg().cvt32(), e.eax, i.src2.reg().cvt32());
      } else {
        // x86 mul instruction
        // DX:AX = AX * $1;
        if (i.src1.is_constant) {
          assert_true(!i.src2.is_constant);  // can't multiply 2 constants
          e.mov(e.ax, i.src1.constant());
          e.mul(i.src2);
          e.mov(i.dest, e.dx);
        } else if (i.src2.is_constant) {
          assert_true(!i.src1.is_constant);  // can't multiply 2 constants
          e.mov(e.ax, i.src2.constant());
          e.mul(i.src1);
          e.mov(i.dest, e.dx);
        } else {
          e.mov(e.ax, i.src1);
          e.mul(i.src2);
          e.mov(i.dest, e.dx);
        }
      }
    } else {
      if (i.src1.is_constant) {
        e.mov(e.ax, i.src1.constant());
      } else {
        e.mov(e.ax, i.src1);
      }
      if (i.src2.is_constant) {
        e.mov(e.dx, i.src2.constant());
        e.imul(e.dx);
      } else {
        e.imul(i.src2);
      }
      e.mov(i.dest, e.dx);
    }
  }
};
struct MUL_HI_I32
    : Sequence<MUL_HI_I32, I<OPCODE_MUL_HI, I32Op, I32Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.instr->flags & ARITHMETIC_UNSIGNED) {
      if (e.IsFeatureEnabled(kX64EmitBMI2)) {
        // TODO(benvanik): place src1 in eax? still need to sign extend
        e.mov(e.edx, i.src1);
        if (i.src2.is_constant) {
          e.mov(e.eax, i.src2.constant());
          e.mulx(i.dest, e.edx, e.eax);
        } else {
          e.mulx(i.dest, e.edx, i.src2);
        }
      } else {
        // x86 mul instruction
        // EDX:EAX = EAX * $1;
        if (i.src1.is_constant) {
          assert_true(!i.src2.is_constant);  // can't multiply 2 constants
          e.mov(e.eax, i.src1.constant());
          e.mul(i.src2);
          e.mov(i.dest, e.edx);
        } else if (i.src2.is_constant) {
          assert_true(!i.src1.is_constant);  // can't multiply 2 constants
          e.mov(e.eax, i.src2.constant());
          e.mul(i.src1);
          e.mov(i.dest, e.edx);
        } else {
          e.mov(e.eax, i.src1);
          e.mul(i.src2);
          e.mov(i.dest, e.edx);
        }
      }
    } else {
      if (i.src1.is_constant) {
        e.mov(e.eax, i.src1.constant());
      } else {
        e.mov(e.eax, i.src1);
      }
      if (i.src2.is_constant) {
        e.mov(e.edx, i.src2.constant());
        e.imul(e.edx);
      } else {
        e.imul(i.src2);
      }
      e.mov(i.dest, e.edx);
    }
  }
};
struct MUL_HI_I64
    : Sequence<MUL_HI_I64, I<OPCODE_MUL_HI, I64Op, I64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.instr->flags & ARITHMETIC_UNSIGNED) {
      if (e.IsFeatureEnabled(kX64EmitBMI2)) {
        // TODO(benvanik): place src1 in eax? still need to sign extend
        e.mov(e.rdx, i.src1);
        if (i.src2.is_constant) {
          e.mov(e.rax, i.src2.constant());
          e.mulx(i.dest, e.rdx, e.rax);
        } else {
          e.mulx(i.dest, e.rax, i.src2);
        }
      } else {
        // x86 mul instruction
        // RDX:RAX < RAX * REG(op1);
        if (i.src1.is_constant) {
          assert_true(!i.src2.is_constant);  // can't multiply 2 constants
          e.mov(e.rax, i.src1.constant());
          e.mul(i.src2);
          e.mov(i.dest, e.rdx);
        } else if (i.src2.is_constant) {
          assert_true(!i.src1.is_constant);  // can't multiply 2 constants
          e.mov(e.rax, i.src2.constant());
          e.mul(i.src1);
          e.mov(i.dest, e.rdx);
        } else {
          e.mov(e.rax, i.src1);
          e.mul(i.src2);
          e.mov(i.dest, e.rdx);
        }
      }
    } else {
      if (i.src1.is_constant) {
        e.mov(e.rax, i.src1.constant());
      } else {
        e.mov(e.rax, i.src1);
      }
      if (i.src2.is_constant) {
        e.mov(e.rdx, i.src2.constant());
        e.imul(e.rdx);
      } else {
        e.imul(i.src2);
      }
      e.mov(i.dest, e.rdx);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_MUL_HI, MUL_HI_I8, MUL_HI_I16, MUL_HI_I32,
                     MUL_HI_I64);
/*  from Hackers Delight - by Henry S. Warren Jr. Calculate magic number for
 * unsigned division */
template <typename T>
auto magicu(T d) {
  constexpr unsigned NBITS = sizeof(T) * CHAR_BIT;
  constexpr unsigned NBITS_M1 = NBITS - 1;
  constexpr T SIGNBIT = T(1) << NBITS_M1;

  constexpr T POSMASK = ~SIGNBIT;

  struct mu {
    T M;    // Magic number,
    int a;  // "add" indicator,
    int s;
  };  // and shift amount.

  // Must have 1 <= d <= 2**32-1.
  int p, gt = 0;
  T nc, delta, q1, r1, q2, r2;
  struct mu magu;

  magu.a = 0;  // Initialize "add" indicator.
  nc = -1 - ((T) - (std::make_signed_t<T>)d) % d;  // Unsigned arithmetic here.
  p = NBITS_M1;                                    // Init. p.
  q1 = SIGNBIT / nc;                               // Init. q1 = 2**p/nc.
  r1 = SIGNBIT - q1 * nc;                          // Init. r1 = rem(2**p, nc).
  q2 = POSMASK / d;                                // Init. q2 = (2**p - 1)/d.
  r2 = POSMASK - q2 * d;  // Init. r2 = rem(2**p - 1, d).
  do {
    p = p + 1;
    if (q1 >= SIGNBIT) gt = 1;  // Means q1 > delta.
    if (r1 >= nc - r1) {
      q1 = 2 * q1 + 1;  // Update q1.
      r1 = 2 * r1 - nc;
    }  // Update r1.
    else {
      q1 = 2 * q1;
      r1 = 2 * r1;
    }
    if (r2 + 1 >= d - r2) {
      if (q2 >= POSMASK) magu.a = 1;
      q2 = 2 * q2 + 1;  // Update q2.
      r2 = 2 * r2 + 1 - d;
    }  // Update r2.
    else {
      if (q2 >= SIGNBIT) magu.a = 1;
      q2 = 2 * q2;
      r2 = 2 * r2 + 1;
    }
    delta = d - 1 - r2;
  } while (gt == 0 && (q1 < delta || (q1 == delta && r1 == 0)));

  magu.M = q2 + 1;     // Magic number
  magu.s = p - NBITS;  // and shift amount to return
  return magu;         // (magu.a was set above).
}
// ============================================================================
// OPCODE_DIV
// ============================================================================
// TODO(benvanik): optimize common constant cases.
// TODO(benvanik): simplify code!
struct DIV_I8 : Sequence<DIV_I8, I<OPCODE_DIV, I8Op, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    Xbyak::Label skip;
    e.inLocalLabel();

    if (i.src2.is_constant) {
      assert_true(!i.src1.is_constant);
      e.mov(e.cl, i.src2.constant());
      if (i.instr->flags & ARITHMETIC_UNSIGNED) {
        e.movzx(e.ax, i.src1);
        e.div(e.cl);
      } else {
        e.movsx(e.ax, i.src1);
        e.idiv(e.cl);
      }
    } else {
      // Skip if src2 is zero.
      e.test(i.src2, i.src2);
      e.jz(skip, CodeGenerator::T_SHORT);

      if (i.instr->flags & ARITHMETIC_UNSIGNED) {
        if (i.src1.is_constant) {
          e.mov(e.ax, static_cast<int16_t>(i.src1.constant()));
        } else {
          e.movzx(e.ax, i.src1);
        }
        e.div(i.src2);
      } else {
        if (i.src1.is_constant) {
          e.mov(e.ax, static_cast<int16_t>(i.src1.constant()));
        } else {
          e.movsx(e.ax, i.src1);
        }
        e.idiv(i.src2);
      }
    }

    e.L(skip);
    e.outLocalLabel();
    e.mov(i.dest, e.al);
  }
};
struct DIV_I16 : Sequence<DIV_I16, I<OPCODE_DIV, I16Op, I16Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    Xbyak::Label skip;
    e.inLocalLabel();

    if (i.src2.is_constant) {
      assert_true(!i.src1.is_constant);
      e.mov(e.cx, i.src2.constant());
      if (i.instr->flags & ARITHMETIC_UNSIGNED) {
        e.mov(e.ax, i.src1);
        // Zero upper bits.
        e.xor_(e.dx, e.dx);
        e.div(e.cx);
      } else {
        e.mov(e.ax, i.src1);
        e.cwd();  // dx:ax = sign-extend ax
        e.idiv(e.cx);
      }
    } else {
      // Skip if src2 is zero.
      e.test(i.src2, i.src2);
      e.jz(skip, CodeGenerator::T_SHORT);

      if (i.instr->flags & ARITHMETIC_UNSIGNED) {
        if (i.src1.is_constant) {
          e.mov(e.ax, i.src1.constant());
        } else {
          e.mov(e.ax, i.src1);
        }
        // Zero upper bits.
        e.xor_(e.dx, e.dx);
        e.div(i.src2);
      } else {
        if (i.src1.is_constant) {
          e.mov(e.ax, i.src1.constant());
        } else {
          e.mov(e.ax, i.src1);
        }
        e.cwd();  // dx:ax = sign-extend ax
        e.idiv(i.src2);
      }
    }

    e.L(skip);
    e.outLocalLabel();
    e.mov(i.dest, e.ax);
  }
};
struct DIV_I32 : Sequence<DIV_I32, I<OPCODE_DIV, I32Op, I32Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    Xbyak::Label skip;
    e.inLocalLabel();

    if (i.src2.is_constant) {
      assert_true(!i.src1.is_constant);
      
      if (i.instr->flags & ARITHMETIC_UNSIGNED) {
        auto div_shift_info = magicu<unsigned>(i.src2.value->constant.u32);
		//disabling addflag for now. the code for handling it is incorrect
        if (!div_shift_info.a) {
          e.mov(i.dest, i.src1);
          if (e.IsFeatureEnabled(kX64EmitAVX2)) {
            e.mov(e.edx, div_shift_info.M);
            e.mulx(i.dest, i.dest, i.dest);
          } else {
            e.mov(e.eax, div_shift_info.M);
            e.mul(i.dest);
            e.mov(i.dest, e.edx);
          }
          if (div_shift_info.a) {
            // pg 228, hackers delight, 2nd edition. add initial input to
            // product, result may carry we need to handle this carry, treat
            // result as 33 bits with carry as bit 33
            e.add(i.dest, i.src1);
            e.rcr(i.dest, 1);
          }
          if (div_shift_info.s != 0) {
            e.shr(i.dest, div_shift_info.s);
          }
          return;
        } else {
          e.mov(e.ecx, i.src2.constant());
          e.mov(e.eax, i.src1);
          // Zero upper bits.
          e.xor_(e.edx, e.edx);
          e.div(e.ecx);
		}
      } else {
        e.mov(e.ecx, i.src2.constant());
        e.mov(e.eax, i.src1);
        e.cdq();  // edx:eax = sign-extend eax
        e.idiv(e.ecx);
      }
    } else {
      // Skip if src2 is zero.
      e.test(i.src2, i.src2);
      e.jz(skip, CodeGenerator::T_SHORT);

      if (i.instr->flags & ARITHMETIC_UNSIGNED) {
        if (i.src1.is_constant) {
          e.mov(e.eax, i.src1.constant());
        } else {
          e.mov(e.eax, i.src1);
        }
        // Zero upper bits.
        e.xor_(e.edx, e.edx);
        e.div(i.src2);
      } else {
        if (i.src1.is_constant) {
          e.mov(e.eax, i.src1.constant());
        } else {
          e.mov(e.eax, i.src1);
        }
        e.cdq();  // edx:eax = sign-extend eax
        e.idiv(i.src2);
      }
    }

    e.L(skip);
    e.outLocalLabel();
    e.mov(i.dest, e.eax);
  }
};
struct DIV_I64 : Sequence<DIV_I64, I<OPCODE_DIV, I64Op, I64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    Xbyak::Label skip;
    e.inLocalLabel();

    if (i.src2.is_constant) {
      assert_true(!i.src1.is_constant);
      if (i.instr->flags & ARITHMETIC_UNSIGNED) {
        auto div_shift_info = magicu<uint64_t>(i.src2.value->constant.u64);
		//disabled addflag for now
        if (!div_shift_info.a) {
          if (e.IsFeatureEnabled(kX64EmitAVX2)) {
            e.mov(e.rdx, div_shift_info.M);
            e.mov(i.dest, i.src1);
            e.mulx(i.dest, i.dest, i.dest);
          } else {
            e.mov(e.rax, div_shift_info.M);
            e.mul(i.src1);
            e.mov(i.dest, e.rdx);
          }
          if (div_shift_info.a) {
            // pg 228, hackers delight, 2nd edition. add initial input to
            // product, result may carry we need to handle this carry, treat
            // result as 65 bits with carry as bit 65
            e.add(i.dest, i.src1);
            e.rcr(i.dest, 1);
          }
          if (div_shift_info.s) {
            e.shr(i.dest, div_shift_info.s);
          }
          return;
        } else {
          e.mov(e.rcx, i.src2.constant());
          e.mov(e.rax, i.src1);
          // Zero upper bits.
          e.xor_(e.rdx, e.rdx);
          e.div(e.rcx);
		}
      } else {
        e.mov(e.rcx, i.src2.constant());
        e.mov(e.rax, i.src1);
        e.cqo();  // rdx:rax = sign-extend rax
        e.idiv(e.rcx);
      }
    } else {
      // Skip if src2 is zero.
      e.test(i.src2, i.src2);
      e.jz(skip, CodeGenerator::T_SHORT);

      if (i.instr->flags & ARITHMETIC_UNSIGNED) {
        if (i.src1.is_constant) {
          e.mov(e.rax, i.src1.constant());
        } else {
          e.mov(e.rax, i.src1);
        }
        // Zero upper bits.
        e.xor_(e.rdx, e.rdx);
        e.div(i.src2);
      } else {
        if (i.src1.is_constant) {
          e.mov(e.rax, i.src1.constant());
        } else {
          e.mov(e.rax, i.src1);
        }
        e.cqo();  // rdx:rax = sign-extend rax
        e.idiv(i.src2);
      }
    }

    e.L(skip);
    e.outLocalLabel();
    e.mov(i.dest, e.rax);
  }
};
struct DIV_F32 : Sequence<DIV_F32, I<OPCODE_DIV, F32Op, F32Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    assert_true(!i.instr->flags);
    EmitAssociativeBinaryXmmOp(e, i,
                               [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
                                 e.vdivss(dest, src1, src2);
                               });
  }
};
struct DIV_F64 : Sequence<DIV_F64, I<OPCODE_DIV, F64Op, F64Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    assert_true(!i.instr->flags);
    EmitAssociativeBinaryXmmOp(e, i,
                               [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
                                 e.vdivsd(dest, src1, src2);
                               });
  }
};
struct DIV_V128 : Sequence<DIV_V128, I<OPCODE_DIV, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    assert_true(!i.instr->flags);
    EmitAssociativeBinaryXmmOp(e, i,
                               [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
                                 e.vdivps(dest, src1, src2);
                               });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_DIV, DIV_I8, DIV_I16, DIV_I32, DIV_I64, DIV_F32,
                     DIV_F64, DIV_V128);

// ============================================================================
// OPCODE_MUL_ADD
// ============================================================================
// d = 1 * 2 + 3
// $0 = $1x$0 + $2
// Forms of vfmadd/vfmsub:
// - 132 -> $1 = $1 * $3 + $2
// - 213 -> $1 = $2 * $1 + $3
// - 231 -> $1 = $2 * $3 + $1
struct MUL_ADD_F32
    : Sequence<MUL_ADD_F32, I<OPCODE_MUL_ADD, F32Op, F32Op, F32Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // FMA extension
    if (e.IsFeatureEnabled(kX64EmitFMA)) {
      EmitCommutativeBinaryXmmOp(e, i,
                                 [&i](X64Emitter& e, const Xmm& dest,
                                      const Xmm& src1, const Xmm& src2) {
                                   Xmm src3 =
                                       i.src3.is_constant ? e.xmm1 : i.src3;
                                   if (i.src3.is_constant) {
                                     e.LoadConstantXmm(src3, i.src3.constant());
                                   }
                                   if (i.dest == src1) {
                                     e.vfmadd213ss(i.dest, src2, src3);
                                   } else if (i.dest == src2) {
                                     e.vfmadd213ss(i.dest, src1, src3);
                                   } else if (i.dest == i.src3) {
                                     e.vfmadd231ss(i.dest, src1, src2);
                                   } else {
                                     // Dest not equal to anything
                                     e.vmovss(i.dest, src1);
                                     e.vfmadd213ss(i.dest, src2, src3);
                                   }
                                 });
    } else {
      Xmm src3;
      if (i.src3.is_constant) {
        src3 = e.xmm1;
        e.LoadConstantXmm(src3, i.src3.constant());
      } else {
        // If i.dest == i.src3, back up i.src3 so we don't overwrite it.
        src3 = i.src3;
        if (i.dest == i.src3) {
          e.vmovss(e.xmm1, i.src3);
          src3 = e.xmm1;
        }
      }

      // Multiply operation is commutative.
      EmitCommutativeBinaryXmmOp(
          e, i, [&i](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
            e.vmulss(dest, src1, src2);  // $0 = $1 * $2
          });

      e.vaddss(i.dest, i.dest, src3);  // $0 = $1 + $2
    }
  }
};
struct MUL_ADD_F64
    : Sequence<MUL_ADD_F64, I<OPCODE_MUL_ADD, F64Op, F64Op, F64Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // FMA extension
    if (e.IsFeatureEnabled(kX64EmitFMA)) {
      EmitCommutativeBinaryXmmOp(e, i,
                                 [&i](X64Emitter& e, const Xmm& dest,
                                      const Xmm& src1, const Xmm& src2) {
                                   Xmm src3 =
                                       i.src3.is_constant ? e.xmm1 : i.src3;
                                   if (i.src3.is_constant) {
                                     e.LoadConstantXmm(src3, i.src3.constant());
                                   }
                                   if (i.dest == src1) {
                                     e.vfmadd213sd(i.dest, src2, src3);
                                   } else if (i.dest == src2) {
                                     e.vfmadd213sd(i.dest, src1, src3);
                                   } else if (i.dest == i.src3) {
                                     e.vfmadd231sd(i.dest, src1, src2);
                                   } else {
                                     // Dest not equal to anything
                                     e.vmovsd(i.dest, src1);
                                     e.vfmadd213sd(i.dest, src2, src3);
                                   }
                                 });
    } else {
      Xmm src3;
      if (i.src3.is_constant) {
        src3 = e.xmm1;
        e.LoadConstantXmm(src3, i.src3.constant());
      } else {
        // If i.dest == i.src3, back up i.src3 so we don't overwrite it.
        src3 = i.src3;
        if (i.dest == i.src3) {
          e.vmovsd(e.xmm1, i.src3);
          src3 = e.xmm1;
        }
      }

      // Multiply operation is commutative.
      EmitCommutativeBinaryXmmOp(
          e, i, [&i](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
            e.vmulsd(dest, src1, src2);  // $0 = $1 * $2
          });

      e.vaddsd(i.dest, i.dest, src3);  // $0 = $1 + $2
    }
  }
};
struct MUL_ADD_V128
    : Sequence<MUL_ADD_V128,
               I<OPCODE_MUL_ADD, V128Op, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // TODO(benvanik): the vfmadd sequence produces slightly different results
    // than vmul+vadd and it'd be nice to know why. Until we know, it's
    // disabled so tests pass.
    if (false && e.IsFeatureEnabled(kX64EmitFMA)) {
      EmitCommutativeBinaryXmmOp(e, i,
                                 [&i](X64Emitter& e, const Xmm& dest,
                                      const Xmm& src1, const Xmm& src2) {
                                   Xmm src3 =
                                       i.src3.is_constant ? e.xmm1 : i.src3;
                                   if (i.src3.is_constant) {
                                     e.LoadConstantXmm(src3, i.src3.constant());
                                   }
                                   if (i.dest == src1) {
                                     e.vfmadd213ps(i.dest, src2, src3);
                                   } else if (i.dest == src2) {
                                     e.vfmadd213ps(i.dest, src1, src3);
                                   } else if (i.dest == i.src3) {
                                     e.vfmadd231ps(i.dest, src1, src2);
                                   } else {
                                     // Dest not equal to anything
                                     e.vmovdqa(i.dest, src1);
                                     e.vfmadd213ps(i.dest, src2, src3);
                                   }
                                 });
    } else {
      Xmm src3;
      if (i.src3.is_constant) {
        src3 = e.xmm1;
        e.LoadConstantXmm(src3, i.src3.constant());
      } else {
        // If i.dest == i.src3, back up i.src3 so we don't overwrite it.
        src3 = i.src3;
        if (i.dest == i.src3) {
          e.vmovdqa(e.xmm1, i.src3);
          src3 = e.xmm1;
        }
      }

      // Multiply operation is commutative.
      EmitCommutativeBinaryXmmOp(
          e, i, [&i](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
            e.vmulps(dest, src1, src2);  // $0 = $1 * $2
          });

      e.vaddps(i.dest, i.dest, src3);  // $0 = $1 + $2
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_MUL_ADD, MUL_ADD_F32, MUL_ADD_F64, MUL_ADD_V128);

// ============================================================================
// OPCODE_MUL_SUB
// ============================================================================
// d = 1 * 2 - 3
// $0 = $2x$0 - $3
// TODO(benvanik): use other forms (132/213/etc) to avoid register shuffling.
// dest could be src2 or src3 - need to ensure it's not before overwriting dest
// perhaps use other 132/213/etc
// Forms:
// - 132 -> $1 = $1 * $3 - $2
// - 213 -> $1 = $2 * $1 - $3
// - 231 -> $1 = $2 * $3 - $1
struct MUL_SUB_F32
    : Sequence<MUL_SUB_F32, I<OPCODE_MUL_SUB, F32Op, F32Op, F32Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // FMA extension
    if (e.IsFeatureEnabled(kX64EmitFMA)) {
      EmitCommutativeBinaryXmmOp(e, i,
                                 [&i](X64Emitter& e, const Xmm& dest,
                                      const Xmm& src1, const Xmm& src2) {
                                   Xmm src3 =
                                       i.src3.is_constant ? e.xmm1 : i.src3;
                                   if (i.src3.is_constant) {
                                     e.LoadConstantXmm(src3, i.src3.constant());
                                   }
                                   if (i.dest == src1) {
                                     e.vfmsub213ss(i.dest, src2, src3);
                                   } else if (i.dest == src2) {
                                     e.vfmsub213ss(i.dest, src1, src3);
                                   } else if (i.dest == i.src3) {
                                     e.vfmsub231ss(i.dest, src1, src2);
                                   } else {
                                     // Dest not equal to anything
                                     e.vmovss(i.dest, src1);
                                     e.vfmsub213ss(i.dest, src2, src3);
                                   }
                                 });
    } else {
      Xmm src3;
      if (i.src3.is_constant) {
        src3 = e.xmm1;
        e.LoadConstantXmm(src3, i.src3.constant());
      } else {
        // If i.dest == i.src3, back up i.src3 so we don't overwrite it.
        src3 = i.src3;
        if (i.dest == i.src3) {
          e.vmovss(e.xmm1, i.src3);
          src3 = e.xmm1;
        }
      }

      // Multiply operation is commutative.
      EmitCommutativeBinaryXmmOp(
          e, i, [&i](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
            e.vmulss(dest, src1, src2);  // $0 = $1 * $2
          });

      e.vsubss(i.dest, i.dest, src3);  // $0 = $1 - $2
    }
  }
};
struct MUL_SUB_F64
    : Sequence<MUL_SUB_F64, I<OPCODE_MUL_SUB, F64Op, F64Op, F64Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // FMA extension
    if (e.IsFeatureEnabled(kX64EmitFMA)) {
      EmitCommutativeBinaryXmmOp(e, i,
                                 [&i](X64Emitter& e, const Xmm& dest,
                                      const Xmm& src1, const Xmm& src2) {
                                   Xmm src3 =
                                       i.src3.is_constant ? e.xmm1 : i.src3;
                                   if (i.src3.is_constant) {
                                     e.LoadConstantXmm(src3, i.src3.constant());
                                   }
                                   if (i.dest == src1) {
                                     e.vfmsub213sd(i.dest, src2, src3);
                                   } else if (i.dest == src2) {
                                     e.vfmsub213sd(i.dest, src1, src3);
                                   } else if (i.dest == i.src3) {
                                     e.vfmsub231sd(i.dest, src1, src2);
                                   } else {
                                     // Dest not equal to anything
                                     e.vmovsd(i.dest, src1);
                                     e.vfmsub213sd(i.dest, src2, src3);
                                   }
                                 });
    } else {
      Xmm src3;
      if (i.src3.is_constant) {
        src3 = e.xmm1;
        e.LoadConstantXmm(src3, i.src3.constant());
      } else {
        // If i.dest == i.src3, back up i.src3 so we don't overwrite it.
        src3 = i.src3;
        if (i.dest == i.src3) {
          e.vmovsd(e.xmm1, i.src3);
          src3 = e.xmm1;
        }
      }

      // Multiply operation is commutative.
      EmitCommutativeBinaryXmmOp(
          e, i, [&i](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
            e.vmulsd(dest, src1, src2);  // $0 = $1 * $2
          });

      e.vsubsd(i.dest, i.dest, src3);  // $0 = $1 - $2
    }
  }
};
struct MUL_SUB_V128
    : Sequence<MUL_SUB_V128,
               I<OPCODE_MUL_SUB, V128Op, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // FMA extension
    if (e.IsFeatureEnabled(kX64EmitFMA)) {
      EmitCommutativeBinaryXmmOp(e, i,
                                 [&i](X64Emitter& e, const Xmm& dest,
                                      const Xmm& src1, const Xmm& src2) {
                                   Xmm src3 =
                                       i.src3.is_constant ? e.xmm1 : i.src3;
                                   if (i.src3.is_constant) {
                                     e.LoadConstantXmm(src3, i.src3.constant());
                                   }
                                   if (i.dest == src1) {
                                     e.vfmsub213ps(i.dest, src2, src3);
                                   } else if (i.dest == src2) {
                                     e.vfmsub213ps(i.dest, src1, src3);
                                   } else if (i.dest == i.src3) {
                                     e.vfmsub231ps(i.dest, src1, src2);
                                   } else {
                                     // Dest not equal to anything
                                     e.vmovdqa(i.dest, src1);
                                     e.vfmsub213ps(i.dest, src2, src3);
                                   }
                                 });
    } else {
      Xmm src3;
      if (i.src3.is_constant) {
        src3 = e.xmm1;
        e.LoadConstantXmm(src3, i.src3.constant());
      } else {
        // If i.dest == i.src3, back up i.src3 so we don't overwrite it.
        src3 = i.src3;
        if (i.dest == i.src3) {
          e.vmovdqa(e.xmm1, i.src3);
          src3 = e.xmm1;
        }
      }

      // Multiply operation is commutative.
      EmitCommutativeBinaryXmmOp(
          e, i, [&i](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
            e.vmulps(dest, src1, src2);  // $0 = $1 * $2
          });

      e.vsubps(i.dest, i.dest, src3);  // $0 = $1 - $2
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_MUL_SUB, MUL_SUB_F32, MUL_SUB_F64, MUL_SUB_V128);

// ============================================================================
// OPCODE_NEG
// ============================================================================
// TODO(benvanik): put dest/src1 together.
template <typename SEQ, typename REG, typename ARGS>
void EmitNegXX(X64Emitter& e, const ARGS& i) {
  SEQ::EmitUnaryOp(e, i,
                   [](X64Emitter& e, const REG& dest_src) { e.neg(dest_src); });
}
struct NEG_I8 : Sequence<NEG_I8, I<OPCODE_NEG, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitNegXX<NEG_I8, Reg8>(e, i);
  }
};
struct NEG_I16 : Sequence<NEG_I16, I<OPCODE_NEG, I16Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitNegXX<NEG_I16, Reg16>(e, i);
  }
};
struct NEG_I32 : Sequence<NEG_I32, I<OPCODE_NEG, I32Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitNegXX<NEG_I32, Reg32>(e, i);
  }
};
struct NEG_I64 : Sequence<NEG_I64, I<OPCODE_NEG, I64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitNegXX<NEG_I64, Reg64>(e, i);
  }
};
struct NEG_F32 : Sequence<NEG_F32, I<OPCODE_NEG, F32Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vxorps(i.dest, i.src1, e.GetXmmConstPtr(XMMSignMaskPS));
  }
};
struct NEG_F64 : Sequence<NEG_F64, I<OPCODE_NEG, F64Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vxorpd(i.dest, i.src1, e.GetXmmConstPtr(XMMSignMaskPD));
  }
};
struct NEG_V128 : Sequence<NEG_V128, I<OPCODE_NEG, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    assert_true(!i.instr->flags);
    e.vxorps(i.dest, i.src1, e.GetXmmConstPtr(XMMSignMaskPS));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_NEG, NEG_I8, NEG_I16, NEG_I32, NEG_I64, NEG_F32,
                     NEG_F64, NEG_V128);

// ============================================================================
// OPCODE_ABS
// ============================================================================
struct ABS_F32 : Sequence<ABS_F32, I<OPCODE_ABS, F32Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vpand(i.dest, i.src1, e.GetXmmConstPtr(XMMAbsMaskPS));
  }
};
struct ABS_F64 : Sequence<ABS_F64, I<OPCODE_ABS, F64Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vpand(i.dest, i.src1, e.GetXmmConstPtr(XMMAbsMaskPD));
  }
};
struct ABS_V128 : Sequence<ABS_V128, I<OPCODE_ABS, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vpand(i.dest, i.src1, e.GetXmmConstPtr(XMMAbsMaskPS));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_ABS, ABS_F32, ABS_F64, ABS_V128);

// ============================================================================
// OPCODE_SQRT
// ============================================================================
struct SQRT_F32 : Sequence<SQRT_F32, I<OPCODE_SQRT, F32Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vsqrtss(i.dest, i.src1);
  }
};
struct SQRT_F64 : Sequence<SQRT_F64, I<OPCODE_SQRT, F64Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vsqrtsd(i.dest, i.src1);
  }
};
struct SQRT_V128 : Sequence<SQRT_V128, I<OPCODE_SQRT, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vsqrtps(i.dest, i.src1);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SQRT, SQRT_F32, SQRT_F64, SQRT_V128);

// ============================================================================
// OPCODE_RSQRT
// ============================================================================
struct RSQRT_F32 : Sequence<RSQRT_F32, I<OPCODE_RSQRT, F32Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vrsqrtss(i.dest, i.src1);
  }
};
struct RSQRT_F64 : Sequence<RSQRT_F64, I<OPCODE_RSQRT, F64Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vcvtsd2ss(i.dest, i.src1);
    e.vrsqrtss(i.dest, i.dest);
    e.vcvtss2sd(i.dest, i.dest);
  }
};
struct RSQRT_V128 : Sequence<RSQRT_V128, I<OPCODE_RSQRT, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vrsqrtps(i.dest, i.src1);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_RSQRT, RSQRT_F32, RSQRT_F64, RSQRT_V128);

// ============================================================================
// OPCODE_RECIP
// ============================================================================
struct RECIP_F32 : Sequence<RECIP_F32, I<OPCODE_RECIP, F32Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vrcpss(i.dest, i.src1);
  }
};
struct RECIP_F64 : Sequence<RECIP_F64, I<OPCODE_RECIP, F64Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vcvtsd2ss(i.dest, i.src1);
    e.vrcpss(i.dest, i.dest);
    e.vcvtss2sd(i.dest, i.dest);
  }
};
struct RECIP_V128 : Sequence<RECIP_V128, I<OPCODE_RECIP, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vrcpps(i.dest, i.src1);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_RECIP, RECIP_F32, RECIP_F64, RECIP_V128);

// ============================================================================
// OPCODE_POW2
// ============================================================================
// TODO(benvanik): use approx here:
//     https://jrfonseca.blogspot.com/2008/09/fast-sse2-pow-tables-or-polynomials.html
struct POW2_F32 : Sequence<POW2_F32, I<OPCODE_POW2, F32Op, F32Op>> {
  static __m128 EmulatePow2(void*, __m128 src) {
    float src_value;
    _mm_store_ss(&src_value, src);
    float result = std::exp2(src_value);
    return _mm_load_ss(&result);
  }
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    assert_always();
    e.lea(e.GetNativeParam(0), e.StashXmm(0, i.src1));
    e.CallNativeSafe(reinterpret_cast<void*>(EmulatePow2));
    e.vmovaps(i.dest, e.xmm0);
  }
};
struct POW2_F64 : Sequence<POW2_F64, I<OPCODE_POW2, F64Op, F64Op>> {
  static __m128d EmulatePow2(void*, __m128d src) {
    double src_value;
    _mm_store_sd(&src_value, src);
    double result = std::exp2(src_value);
    return _mm_load_sd(&result);
  }
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    assert_always();
    e.lea(e.GetNativeParam(0), e.StashXmm(0, i.src1));
    e.CallNativeSafe(reinterpret_cast<void*>(EmulatePow2));
    e.vmovaps(i.dest, e.xmm0);
  }
};
struct POW2_V128 : Sequence<POW2_V128, I<OPCODE_POW2, V128Op, V128Op>> {
  static __m128 EmulatePow2(void*, __m128 src) {
    alignas(16) float values[4];
    _mm_store_ps(values, src);
    for (size_t i = 0; i < 4; ++i) {
      values[i] = std::exp2(values[i]);
    }
    return _mm_load_ps(values);
  }
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.lea(e.GetNativeParam(0), e.StashXmm(0, i.src1));
    e.CallNativeSafe(reinterpret_cast<void*>(EmulatePow2));
    e.vmovaps(i.dest, e.xmm0);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_POW2, POW2_F32, POW2_F64, POW2_V128);

// ============================================================================
// OPCODE_LOG2
// ============================================================================
// TODO(benvanik): use approx here:
//     https://jrfonseca.blogspot.com/2008/09/fast-sse2-pow-tables-or-polynomials.html
// TODO(benvanik): this emulated fn destroys all xmm registers! don't do it!
struct LOG2_F32 : Sequence<LOG2_F32, I<OPCODE_LOG2, F32Op, F32Op>> {
  static __m128 EmulateLog2(void*, __m128 src) {
    float src_value;
    _mm_store_ss(&src_value, src);
    float result = std::log2(src_value);
    return _mm_load_ss(&result);
  }
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    assert_always();
    e.lea(e.GetNativeParam(0), e.StashXmm(0, i.src1));
    e.CallNativeSafe(reinterpret_cast<void*>(EmulateLog2));
    e.vmovaps(i.dest, e.xmm0);
  }
};
struct LOG2_F64 : Sequence<LOG2_F64, I<OPCODE_LOG2, F64Op, F64Op>> {
  static __m128d EmulateLog2(void*, __m128d src) {
    double src_value;
    _mm_store_sd(&src_value, src);
    double result = std::log2(src_value);
    return _mm_load_sd(&result);
  }
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    assert_always();
    e.lea(e.GetNativeParam(0), e.StashXmm(0, i.src1));
    e.CallNativeSafe(reinterpret_cast<void*>(EmulateLog2));
    e.vmovaps(i.dest, e.xmm0);
  }
};
struct LOG2_V128 : Sequence<LOG2_V128, I<OPCODE_LOG2, V128Op, V128Op>> {
  static __m128 EmulateLog2(void*, __m128 src) {
    alignas(16) float values[4];
    _mm_store_ps(values, src);
    for (size_t i = 0; i < 4; ++i) {
      values[i] = std::log2(values[i]);
    }
    return _mm_load_ps(values);
  }
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.lea(e.GetNativeParam(0), e.StashXmm(0, i.src1));
    e.CallNativeSafe(reinterpret_cast<void*>(EmulateLog2));
    e.vmovaps(i.dest, e.xmm0);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_LOG2, LOG2_F32, LOG2_F64, LOG2_V128);

// ============================================================================
// OPCODE_DOT_PRODUCT_3
// ============================================================================
struct DOT_PRODUCT_3_V128
    : Sequence<DOT_PRODUCT_3_V128,
               I<OPCODE_DOT_PRODUCT_3, F32Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // https://msdn.microsoft.com/en-us/library/bb514054(v=vs.90).aspx
    EmitCommutativeBinaryXmmOp(e, i,
                               [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
                                 // TODO(benvanik): apparently this is very slow
                                 // - find alternative?
                                 e.vdpps(dest, src1, src2, 0b01110001);
                               });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_DOT_PRODUCT_3, DOT_PRODUCT_3_V128);

// ============================================================================
// OPCODE_DOT_PRODUCT_4
// ============================================================================
struct DOT_PRODUCT_4_V128
    : Sequence<DOT_PRODUCT_4_V128,
               I<OPCODE_DOT_PRODUCT_4, F32Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // https://msdn.microsoft.com/en-us/library/bb514054(v=vs.90).aspx
    EmitCommutativeBinaryXmmOp(e, i,
                               [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
                                 // TODO(benvanik): apparently this is very slow
                                 // - find alternative?
                                 e.vdpps(dest, src1, src2, 0b11110001);
                               });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_DOT_PRODUCT_4, DOT_PRODUCT_4_V128);

// ============================================================================
// OPCODE_AND
// ============================================================================
// TODO(benvanik): put dest/src1|2 together.
template <typename SEQ, typename REG, typename ARGS>
void EmitAndXX(X64Emitter& e, const ARGS& i) {
  SEQ::EmitCommutativeBinaryOp(
      e, i,
      [](X64Emitter& e, const REG& dest_src, const REG& src) {
        e.and_(dest_src, src);
      },
      [](X64Emitter& e, const REG& dest_src, int32_t constant) {
        e.and_(dest_src, constant);
      });
}
struct AND_I8 : Sequence<AND_I8, I<OPCODE_AND, I8Op, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitAndXX<AND_I8, Reg8>(e, i);
  }
};
struct AND_I16 : Sequence<AND_I16, I<OPCODE_AND, I16Op, I16Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitAndXX<AND_I16, Reg16>(e, i);
  }
};
struct AND_I32 : Sequence<AND_I32, I<OPCODE_AND, I32Op, I32Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitAndXX<AND_I32, Reg32>(e, i);
  }
};
struct AND_I64 : Sequence<AND_I64, I<OPCODE_AND, I64Op, I64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitAndXX<AND_I64, Reg64>(e, i);
  }
};
struct AND_V128 : Sequence<AND_V128, I<OPCODE_AND, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(e, i,
                               [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
                                 e.vpand(dest, src1, src2);
                               });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_AND, AND_I8, AND_I16, AND_I32, AND_I64, AND_V128);

// ============================================================================
// OPCODE_OR
// ============================================================================
// TODO(benvanik): put dest/src1|2 together.
template <typename SEQ, typename REG, typename ARGS>
void EmitOrXX(X64Emitter& e, const ARGS& i) {
  SEQ::EmitCommutativeBinaryOp(
      e, i,
      [](X64Emitter& e, const REG& dest_src, const REG& src) {
        e.or_(dest_src, src);
      },
      [](X64Emitter& e, const REG& dest_src, int32_t constant) {
        e.or_(dest_src, constant);
      });
}
struct OR_I8 : Sequence<OR_I8, I<OPCODE_OR, I8Op, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitOrXX<OR_I8, Reg8>(e, i);
  }
};
struct OR_I16 : Sequence<OR_I16, I<OPCODE_OR, I16Op, I16Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitOrXX<OR_I16, Reg16>(e, i);
  }
};
struct OR_I32 : Sequence<OR_I32, I<OPCODE_OR, I32Op, I32Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitOrXX<OR_I32, Reg32>(e, i);
  }
};
struct OR_I64 : Sequence<OR_I64, I<OPCODE_OR, I64Op, I64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitOrXX<OR_I64, Reg64>(e, i);
  }
};
struct OR_V128 : Sequence<OR_V128, I<OPCODE_OR, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(e, i,
                               [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
                                 e.vpor(dest, src1, src2);
                               });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_OR, OR_I8, OR_I16, OR_I32, OR_I64, OR_V128);

// ============================================================================
// OPCODE_XOR
// ============================================================================
// TODO(benvanik): put dest/src1|2 together.
template <typename SEQ, typename REG, typename ARGS>
void EmitXorXX(X64Emitter& e, const ARGS& i) {
  SEQ::EmitCommutativeBinaryOp(
      e, i,
      [](X64Emitter& e, const REG& dest_src, const REG& src) {
        e.xor_(dest_src, src);
      },
      [](X64Emitter& e, const REG& dest_src, int32_t constant) {
        e.xor_(dest_src, constant);
      });
}
struct XOR_I8 : Sequence<XOR_I8, I<OPCODE_XOR, I8Op, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitXorXX<XOR_I8, Reg8>(e, i);
  }
};
struct XOR_I16 : Sequence<XOR_I16, I<OPCODE_XOR, I16Op, I16Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitXorXX<XOR_I16, Reg16>(e, i);
  }
};
struct XOR_I32 : Sequence<XOR_I32, I<OPCODE_XOR, I32Op, I32Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitXorXX<XOR_I32, Reg32>(e, i);
  }
};
struct XOR_I64 : Sequence<XOR_I64, I<OPCODE_XOR, I64Op, I64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitXorXX<XOR_I64, Reg64>(e, i);
  }
};
struct XOR_V128 : Sequence<XOR_V128, I<OPCODE_XOR, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(e, i,
                               [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
                                 e.vpxor(dest, src1, src2);
                               });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_XOR, XOR_I8, XOR_I16, XOR_I32, XOR_I64, XOR_V128);

// ============================================================================
// OPCODE_NOT
// ============================================================================
// TODO(benvanik): put dest/src1 together.
template <typename SEQ, typename REG, typename ARGS>
void EmitNotXX(X64Emitter& e, const ARGS& i) {
  SEQ::EmitUnaryOp(
      e, i, [](X64Emitter& e, const REG& dest_src) { e.not_(dest_src); });
}
struct NOT_I8 : Sequence<NOT_I8, I<OPCODE_NOT, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitNotXX<NOT_I8, Reg8>(e, i);
  }
};
struct NOT_I16 : Sequence<NOT_I16, I<OPCODE_NOT, I16Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitNotXX<NOT_I16, Reg16>(e, i);
  }
};
struct NOT_I32 : Sequence<NOT_I32, I<OPCODE_NOT, I32Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitNotXX<NOT_I32, Reg32>(e, i);
  }
};
struct NOT_I64 : Sequence<NOT_I64, I<OPCODE_NOT, I64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitNotXX<NOT_I64, Reg64>(e, i);
  }
};
struct NOT_V128 : Sequence<NOT_V128, I<OPCODE_NOT, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // dest = src ^ 0xFFFF...
    e.vpxor(i.dest, i.src1, e.GetXmmConstPtr(XMMFFFF /* FF... */));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_NOT, NOT_I8, NOT_I16, NOT_I32, NOT_I64, NOT_V128);

// ============================================================================
// OPCODE_SHL
// ============================================================================
// TODO(benvanik): optimize common shifts.
template <typename SEQ, typename REG, typename ARGS>
void EmitShlXX(X64Emitter& e, const ARGS& i) {
  SEQ::EmitAssociativeBinaryOp(
      e, i,
      [](X64Emitter& e, const REG& dest_src, const Reg8& src) {
        // shlx: $1 = $2 << $3
        // shl: $1 = $1 << $2
        if (e.IsFeatureEnabled(kX64EmitBMI2)) {
          if (dest_src.getBit() == 64) {
            e.shlx(dest_src.cvt64(), dest_src.cvt64(), src.cvt64());
          } else {
            e.shlx(dest_src.cvt32(), dest_src.cvt32(), src.cvt32());
          }
        } else {
          e.mov(e.cl, src);
          e.shl(dest_src, e.cl);
        }
      },
      [](X64Emitter& e, const REG& dest_src, int8_t constant) {
        e.shl(dest_src, constant);
      });
}
struct SHL_I8 : Sequence<SHL_I8, I<OPCODE_SHL, I8Op, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitShlXX<SHL_I8, Reg8>(e, i);
  }
};
struct SHL_I16 : Sequence<SHL_I16, I<OPCODE_SHL, I16Op, I16Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitShlXX<SHL_I16, Reg16>(e, i);
  }
};
struct SHL_I32 : Sequence<SHL_I32, I<OPCODE_SHL, I32Op, I32Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitShlXX<SHL_I32, Reg32>(e, i);
  }
};
struct SHL_I64 : Sequence<SHL_I64, I<OPCODE_SHL, I64Op, I64Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitShlXX<SHL_I64, Reg64>(e, i);
  }
};
struct SHL_V128 : Sequence<SHL_V128, I<OPCODE_SHL, V128Op, V128Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // TODO(benvanik): native version (with shift magic).
    if (i.src2.is_constant) {
      e.mov(e.GetNativeParam(1), i.src2.constant());
    } else {
      e.mov(e.GetNativeParam(1), i.src2);
    }
    e.lea(e.GetNativeParam(0), e.StashXmm(0, i.src1));
    e.CallNativeSafe(reinterpret_cast<void*>(EmulateShlV128));
    e.vmovaps(i.dest, e.xmm0);
  }
  static __m128i EmulateShlV128(void*, __m128i src1, uint8_t src2) {
    // Almost all instances are shamt = 1, but non-constant.
    // shamt is [0,7]
    uint8_t shamt = src2 & 0x7;
    alignas(16) vec128_t value;
    _mm_store_si128(reinterpret_cast<__m128i*>(&value), src1);
    for (int i = 0; i < 15; ++i) {
      value.u8[i ^ 0x3] = (value.u8[i ^ 0x3] << shamt) |
                          (value.u8[(i + 1) ^ 0x3] >> (8 - shamt));
    }
    value.u8[15 ^ 0x3] = value.u8[15 ^ 0x3] << shamt;
    return _mm_load_si128(reinterpret_cast<__m128i*>(&value));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SHL, SHL_I8, SHL_I16, SHL_I32, SHL_I64, SHL_V128);

// ============================================================================
// OPCODE_SHR
// ============================================================================
// TODO(benvanik): optimize common shifts.
template <typename SEQ, typename REG, typename ARGS>
void EmitShrXX(X64Emitter& e, const ARGS& i) {
  SEQ::EmitAssociativeBinaryOp(
      e, i,
      [](X64Emitter& e, const REG& dest_src, const Reg8& src) {
        // shrx: op1 dest, op2 src, op3 count
        // shr: op1 src/dest, op2 count
        if (e.IsFeatureEnabled(kX64EmitBMI2)) {
          if (dest_src.getBit() == 64) {
            e.shrx(dest_src.cvt64(), dest_src.cvt64(), src.cvt64());
          } else if (dest_src.getBit() == 32) {
            e.shrx(dest_src.cvt32(), dest_src.cvt32(), src.cvt32());
          } else {
            e.movzx(dest_src.cvt32(), dest_src);
            e.shrx(dest_src.cvt32(), dest_src.cvt32(), src.cvt32());
          }
        } else {
          e.mov(e.cl, src);
          e.shr(dest_src, e.cl);
        }
      },
      [](X64Emitter& e, const REG& dest_src, int8_t constant) {
        e.shr(dest_src, constant);
      });
}
struct SHR_I8 : Sequence<SHR_I8, I<OPCODE_SHR, I8Op, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitShrXX<SHR_I8, Reg8>(e, i);
  }
};
struct SHR_I16 : Sequence<SHR_I16, I<OPCODE_SHR, I16Op, I16Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitShrXX<SHR_I16, Reg16>(e, i);
  }
};
struct SHR_I32 : Sequence<SHR_I32, I<OPCODE_SHR, I32Op, I32Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitShrXX<SHR_I32, Reg32>(e, i);
  }
};
struct SHR_I64 : Sequence<SHR_I64, I<OPCODE_SHR, I64Op, I64Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitShrXX<SHR_I64, Reg64>(e, i);
  }
};
struct SHR_V128 : Sequence<SHR_V128, I<OPCODE_SHR, V128Op, V128Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // TODO(benvanik): native version (with shift magic).
    if (i.src2.is_constant) {
      e.mov(e.GetNativeParam(1), i.src2.constant());
    } else {
      e.mov(e.GetNativeParam(1), i.src2);
    }
    e.lea(e.GetNativeParam(0), e.StashXmm(0, i.src1));
    e.CallNativeSafe(reinterpret_cast<void*>(EmulateShrV128));
    e.vmovaps(i.dest, e.xmm0);
  }
  static __m128i EmulateShrV128(void*, __m128i src1, uint8_t src2) {
    // Almost all instances are shamt = 1, but non-constant.
    // shamt is [0,7]
    uint8_t shamt = src2 & 0x7;
    alignas(16) vec128_t value;
    _mm_store_si128(reinterpret_cast<__m128i*>(&value), src1);
    for (int i = 15; i > 0; --i) {
      value.u8[i ^ 0x3] = (value.u8[i ^ 0x3] >> shamt) |
                          (value.u8[(i - 1) ^ 0x3] << (8 - shamt));
    }
    value.u8[0 ^ 0x3] = value.u8[0 ^ 0x3] >> shamt;
    return _mm_load_si128(reinterpret_cast<__m128i*>(&value));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SHR, SHR_I8, SHR_I16, SHR_I32, SHR_I64, SHR_V128);

// ============================================================================
// OPCODE_SHA
// ============================================================================
// TODO(benvanik): optimize common shifts.
template <typename SEQ, typename REG, typename ARGS>
void EmitSarXX(X64Emitter& e, const ARGS& i) {
  SEQ::EmitAssociativeBinaryOp(
      e, i,
      [](X64Emitter& e, const REG& dest_src, const Reg8& src) {
        if (e.IsFeatureEnabled(kX64EmitBMI2)) {
          if (dest_src.getBit() == 64) {
            e.sarx(dest_src.cvt64(), dest_src.cvt64(), src.cvt64());
          } else if (dest_src.getBit() == 32) {
            e.sarx(dest_src.cvt32(), dest_src.cvt32(), src.cvt32());
          } else {
            e.movsx(dest_src.cvt32(), dest_src);
            e.sarx(dest_src.cvt32(), dest_src.cvt32(), src.cvt32());
          }
        } else {
          e.mov(e.cl, src);
          e.sar(dest_src, e.cl);
        }
      },
      [](X64Emitter& e, const REG& dest_src, int8_t constant) {
        e.sar(dest_src, constant);
      });
}
struct SHA_I8 : Sequence<SHA_I8, I<OPCODE_SHA, I8Op, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitSarXX<SHA_I8, Reg8>(e, i);
  }
};
struct SHA_I16 : Sequence<SHA_I16, I<OPCODE_SHA, I16Op, I16Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitSarXX<SHA_I16, Reg16>(e, i);
  }
};
struct SHA_I32 : Sequence<SHA_I32, I<OPCODE_SHA, I32Op, I32Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitSarXX<SHA_I32, Reg32>(e, i);
  }
};
struct SHA_I64 : Sequence<SHA_I64, I<OPCODE_SHA, I64Op, I64Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitSarXX<SHA_I64, Reg64>(e, i);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SHA, SHA_I8, SHA_I16, SHA_I32, SHA_I64);

// ============================================================================
// OPCODE_ROTATE_LEFT
// ============================================================================
// TODO(benvanik): put dest/src1 together, src2 in cl.
template <typename SEQ, typename REG, typename ARGS>
void EmitRotateLeftXX(X64Emitter& e, const ARGS& i) {
  if (i.src2.is_constant) {
    // Constant rotate.
    if (i.dest != i.src1) {
      if (i.src1.is_constant) {
        e.mov(i.dest, i.src1.constant());
      } else {
        e.mov(i.dest, i.src1);
      }
    }
    e.rol(i.dest, i.src2.constant());
  } else {
    // Variable rotate.
    if (i.src2.reg().getIdx() != e.cl.getIdx()) {
      e.mov(e.cl, i.src2);
    }
    if (i.dest != i.src1) {
      if (i.src1.is_constant) {
        e.mov(i.dest, i.src1.constant());
      } else {
        e.mov(i.dest, i.src1);
      }
    }
    e.rol(i.dest, e.cl);
  }
}
struct ROTATE_LEFT_I8
    : Sequence<ROTATE_LEFT_I8, I<OPCODE_ROTATE_LEFT, I8Op, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitRotateLeftXX<ROTATE_LEFT_I8, Reg8>(e, i);
  }
};
struct ROTATE_LEFT_I16
    : Sequence<ROTATE_LEFT_I16, I<OPCODE_ROTATE_LEFT, I16Op, I16Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitRotateLeftXX<ROTATE_LEFT_I16, Reg16>(e, i);
  }
};
struct ROTATE_LEFT_I32
    : Sequence<ROTATE_LEFT_I32, I<OPCODE_ROTATE_LEFT, I32Op, I32Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitRotateLeftXX<ROTATE_LEFT_I32, Reg32>(e, i);
  }
};
struct ROTATE_LEFT_I64
    : Sequence<ROTATE_LEFT_I64, I<OPCODE_ROTATE_LEFT, I64Op, I64Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitRotateLeftXX<ROTATE_LEFT_I64, Reg64>(e, i);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_ROTATE_LEFT, ROTATE_LEFT_I8, ROTATE_LEFT_I16,
                     ROTATE_LEFT_I32, ROTATE_LEFT_I64);

// ============================================================================
// OPCODE_BYTE_SWAP
// ============================================================================
// TODO(benvanik): put dest/src1 together.
struct BYTE_SWAP_I16
    : Sequence<BYTE_SWAP_I16, I<OPCODE_BYTE_SWAP, I16Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitUnaryOp(
        e, i, [](X64Emitter& e, const Reg16& dest_src) { e.ror(dest_src, 8); });
  }
};
struct BYTE_SWAP_I32
    : Sequence<BYTE_SWAP_I32, I<OPCODE_BYTE_SWAP, I32Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitUnaryOp(
        e, i, [](X64Emitter& e, const Reg32& dest_src) { e.bswap(dest_src); });
  }
};
struct BYTE_SWAP_I64
    : Sequence<BYTE_SWAP_I64, I<OPCODE_BYTE_SWAP, I64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitUnaryOp(
        e, i, [](X64Emitter& e, const Reg64& dest_src) { e.bswap(dest_src); });
  }
};
struct BYTE_SWAP_V128
    : Sequence<BYTE_SWAP_V128, I<OPCODE_BYTE_SWAP, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // TODO(benvanik): find a way to do this without the memory load.
    e.vpshufb(i.dest, i.src1, e.GetXmmConstPtr(XMMByteSwapMask));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_BYTE_SWAP, BYTE_SWAP_I16, BYTE_SWAP_I32,
                     BYTE_SWAP_I64, BYTE_SWAP_V128);

// ============================================================================
// OPCODE_CNTLZ
// Count leading zeroes
// ============================================================================
struct CNTLZ_I8 : Sequence<CNTLZ_I8, I<OPCODE_CNTLZ, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (e.IsFeatureEnabled(kX64EmitLZCNT)) {
      // No 8bit lzcnt, so do 16 and sub 8.
      e.movzx(i.dest.reg().cvt16(), i.src1);
      e.lzcnt(i.dest.reg().cvt16(), i.dest.reg().cvt16());
      e.sub(i.dest, 8);
    } else {
      Xbyak::Label end;
      e.inLocalLabel();

      e.bsr(e.rax, i.src1);  // ZF set if i.src1 is 0
      e.mov(i.dest, 0x8);
      e.jz(end);

      e.xor_(e.rax, 0x7);
      e.mov(i.dest, e.rax);

      e.L(end);
      e.outLocalLabel();
    }
  }
};
struct CNTLZ_I16 : Sequence<CNTLZ_I16, I<OPCODE_CNTLZ, I8Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (e.IsFeatureEnabled(kX64EmitLZCNT)) {
      // LZCNT: searches $2 until MSB 1 found, stores idx (from last bit) in $1
      e.lzcnt(i.dest.reg().cvt32(), i.src1);
    } else {
      Xbyak::Label end;
      e.inLocalLabel();

      e.bsr(e.rax, i.src1);  // ZF set if i.src1 is 0
      e.mov(i.dest, 0x10);
      e.jz(end);

      e.xor_(e.rax, 0x0F);
      e.mov(i.dest, e.rax);

      e.L(end);
      e.outLocalLabel();
    }
  }
};
struct CNTLZ_I32 : Sequence<CNTLZ_I32, I<OPCODE_CNTLZ, I8Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (e.IsFeatureEnabled(kX64EmitLZCNT)) {
      e.lzcnt(i.dest.reg().cvt32(), i.src1);
    } else {
      Xbyak::Label end;
      e.inLocalLabel();

      e.bsr(e.rax, i.src1);  // ZF set if i.src1 is 0
      e.mov(i.dest, 0x20);
      e.jz(end);

      e.xor_(e.rax, 0x1F);
      e.mov(i.dest, e.rax);

      e.L(end);
      e.outLocalLabel();
    }
  }
};
struct CNTLZ_I64 : Sequence<CNTLZ_I64, I<OPCODE_CNTLZ, I8Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (e.IsFeatureEnabled(kX64EmitLZCNT)) {
      e.lzcnt(i.dest.reg().cvt64(), i.src1);
    } else {
      Xbyak::Label end;
      e.inLocalLabel();

      e.bsr(e.rax, i.src1);  // ZF set if i.src1 is 0
      e.mov(i.dest, 0x40);
      e.jz(end);

      e.xor_(e.rax, 0x3F);
      e.mov(i.dest, e.rax);

      e.L(end);
      e.outLocalLabel();
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_CNTLZ, CNTLZ_I8, CNTLZ_I16, CNTLZ_I32, CNTLZ_I64);

// ============================================================================
// OPCODE_SET_ROUNDING_MODE
// ============================================================================
// Input: FPSCR (PPC format)
static const uint32_t mxcsr_table[] = {
    0x1F80, 0x7F80, 0x5F80, 0x3F80, 0x9F80, 0xFF80, 0xDF80, 0xBF80,
};
struct SET_ROUNDING_MODE_I32
    : Sequence<SET_ROUNDING_MODE_I32,
               I<OPCODE_SET_ROUNDING_MODE, VoidOp, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.mov(e.rcx, i.src1);
    e.and_(e.rcx, 0x7);
    e.mov(e.rax, uintptr_t(mxcsr_table));
    e.vldmxcsr(e.ptr[e.rax + e.rcx * 4]);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SET_ROUNDING_MODE, SET_ROUNDING_MODE_I32);

// Include anchors to other sequence sources so they get included in the build.
extern volatile int anchor_control;
static int anchor_control_dest = anchor_control;

extern volatile int anchor_memory;
static int anchor_memory_dest = anchor_memory;

extern volatile int anchor_vector;
static int anchor_vector_dest = anchor_vector;

bool SelectSequence(X64Emitter* e, const Instr* i, const Instr** new_tail) {
  const InstrKey key(i);
  auto it = sequence_table.find(key);
  if (it != sequence_table.end()) {
    if (it->second(*e, i)) {
      *new_tail = i->next;
      return true;
    }
  }
  XELOGE("No sequence match for variant %s", i->opcode->name);
  return false;
}

}  // namespace x64
}  // namespace backend
}  // namespace cpu
}  // namespace xe
