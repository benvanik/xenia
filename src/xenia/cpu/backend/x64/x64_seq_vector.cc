/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Xenia Developers. All rights reserved.                      *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/x64/x64_sequences.h"

#include <algorithm>
#include <cstring>

#include "xenia/cpu/backend/x64/x64_op.h"

// For OPCODE_PACK/OPCODE_UNPACK
#include "third_party/half/include/half.hpp"

namespace xe {
namespace cpu {
namespace backend {
namespace x64 {

volatile int anchor_vector = 0;

// ============================================================================
// OPCODE_VECTOR_CONVERT_I2F
// ============================================================================
struct VECTOR_CONVERT_I2F
    : Sequence<VECTOR_CONVERT_I2F,
               I<OPCODE_VECTOR_CONVERT_I2F, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // flags = ARITHMETIC_UNSIGNED
    if (i.instr->flags & ARITHMETIC_UNSIGNED) {
      // xmm0 = mask of positive values
      e.vpcmpgtd(e.xmm0, i.src1, e.GetXmmConstPtr(XMMFFFF));

      // scale any values >= (unsigned)INT_MIN back to [0, INT_MAX]
      e.vpsubd(e.xmm1, i.src1, e.GetXmmConstPtr(XMMSignMaskI32));
      e.vblendvps(e.xmm1, e.xmm1, i.src1, e.xmm0);

      // xmm1 = [0, INT_MAX]
      e.vcvtdq2ps(i.dest, e.xmm1);

      // scale values back above [INT_MIN, UINT_MAX]
      e.vpandn(e.xmm0, e.xmm0, e.GetXmmConstPtr(XMMPosIntMinPS));
      e.vaddps(i.dest, i.dest, e.xmm0);
    } else {
      e.vcvtdq2ps(i.dest, i.src1);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_CONVERT_I2F, VECTOR_CONVERT_I2F);

// ============================================================================
// OPCODE_VECTOR_CONVERT_F2I
// ============================================================================
struct VECTOR_CONVERT_F2I
    : Sequence<VECTOR_CONVERT_F2I,
               I<OPCODE_VECTOR_CONVERT_F2I, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.instr->flags & ARITHMETIC_UNSIGNED) {
      // clamp to min 0
      e.vmaxps(e.xmm0, i.src1, e.GetXmmConstPtr(XMMZero));

      // xmm1 = mask of values >= (unsigned)INT_MIN
      e.vcmpgeps(e.xmm1, e.xmm0, e.GetXmmConstPtr(XMMPosIntMinPS));

      // scale any values >= (unsigned)INT_MIN back to [0, ...]
      e.vsubps(e.xmm2, e.xmm0, e.GetXmmConstPtr(XMMPosIntMinPS));
      e.vblendvps(e.xmm0, e.xmm0, e.xmm2, e.xmm1);

      // xmm0 = [0, INT_MAX]
      // this may still contain values > INT_MAX (if src has vals > UINT_MAX)
      e.vcvttps2dq(i.dest, e.xmm0);

      // xmm0 = mask of values that need saturation
      e.vpcmpeqd(e.xmm0, i.dest, e.GetXmmConstPtr(XMMIntMin));

      // scale values back above [INT_MIN, UINT_MAX]
      e.vpand(e.xmm1, e.xmm1, e.GetXmmConstPtr(XMMIntMin));
      e.vpaddd(i.dest, i.dest, e.xmm1);

      // saturate values > UINT_MAX
      e.vpor(i.dest, i.dest, e.xmm0);
    } else {
      // xmm2 = NaN mask
      e.vcmpunordps(e.xmm2, i.src1, i.src1);

      // convert packed floats to packed dwords
      e.vcvttps2dq(e.xmm0, i.src1);

      // (high bit) xmm1 = dest is indeterminate and i.src1 >= 0
      e.vpcmpeqd(e.xmm1, e.xmm0, e.GetXmmConstPtr(XMMIntMin));
      e.vpandn(e.xmm1, i.src1, e.xmm1);

      // saturate positive values
      e.vblendvps(i.dest, e.xmm0, e.GetXmmConstPtr(XMMIntMax), e.xmm1);

      // mask NaNs
      e.vpandn(i.dest, e.xmm2, i.dest);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_CONVERT_F2I, VECTOR_CONVERT_F2I);

// ============================================================================
// OPCODE_LOAD_VECTOR_SHL
// ============================================================================
static const vec128_t lvsl_table[16] = {
    vec128b(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15),
    vec128b(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16),
    vec128b(2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17),
    vec128b(3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18),
    vec128b(4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19),
    vec128b(5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20),
    vec128b(6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21),
    vec128b(7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22),
    vec128b(8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23),
    vec128b(9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24),
    vec128b(10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25),
    vec128b(11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26),
    vec128b(12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27),
    vec128b(13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28),
    vec128b(14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29),
    vec128b(15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30),
};
struct LOAD_VECTOR_SHL_I8
    : Sequence<LOAD_VECTOR_SHL_I8, I<OPCODE_LOAD_VECTOR_SHL, V128Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      auto sh = i.src1.constant();
      assert_true(sh < xe::countof(lvsl_table));
      e.mov(e.rax, (uintptr_t)&lvsl_table[sh]);
      e.vmovaps(i.dest, e.ptr[e.rax]);
    } else {
      // TODO(benvanik): find a cheaper way of doing this.
      e.movzx(e.rdx, i.src1);
      e.and_(e.dx, 0xF);
      e.shl(e.dx, 4);
      e.mov(e.rax, (uintptr_t)lvsl_table);
      e.vmovaps(i.dest, e.ptr[e.rax + e.rdx]);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_LOAD_VECTOR_SHL, LOAD_VECTOR_SHL_I8);

// ============================================================================
// OPCODE_LOAD_VECTOR_SHR
// ============================================================================
static const vec128_t lvsr_table[16] = {
    vec128b(16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31),
    vec128b(15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30),
    vec128b(14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29),
    vec128b(13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28),
    vec128b(12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27),
    vec128b(11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26),
    vec128b(10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25),
    vec128b(9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24),
    vec128b(8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23),
    vec128b(7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22),
    vec128b(6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21),
    vec128b(5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20),
    vec128b(4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19),
    vec128b(3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18),
    vec128b(2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17),
    vec128b(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16),
};
struct LOAD_VECTOR_SHR_I8
    : Sequence<LOAD_VECTOR_SHR_I8, I<OPCODE_LOAD_VECTOR_SHR, V128Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      auto sh = i.src1.constant();
      assert_true(sh < xe::countof(lvsr_table));
      e.mov(e.rax, (uintptr_t)&lvsr_table[sh]);
      e.vmovaps(i.dest, e.ptr[e.rax]);
    } else {
      // TODO(benvanik): find a cheaper way of doing this.
      e.movzx(e.rdx, i.src1);
      e.and_(e.dx, 0xF);
      e.shl(e.dx, 4);
      e.mov(e.rax, (uintptr_t)lvsr_table);
      e.vmovaps(i.dest, e.ptr[e.rax + e.rdx]);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_LOAD_VECTOR_SHR, LOAD_VECTOR_SHR_I8);

// ============================================================================
// OPCODE_VECTOR_MAX
// ============================================================================
struct VECTOR_MAX
    : Sequence<VECTOR_MAX, I<OPCODE_VECTOR_MAX, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(
        e, i, [&i](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
          uint32_t part_type = i.instr->flags >> 8;
          if (i.instr->flags & ARITHMETIC_UNSIGNED) {
            switch (part_type) {
              case INT8_TYPE:
                e.vpmaxub(dest, src1, src2);
                break;
              case INT16_TYPE:
                e.vpmaxuw(dest, src1, src2);
                break;
              case INT32_TYPE:
                e.vpmaxud(dest, src1, src2);
                break;
              default:
                assert_unhandled_case(part_type);
                break;
            }
          } else {
            switch (part_type) {
              case INT8_TYPE:
                e.vpmaxsb(dest, src1, src2);
                break;
              case INT16_TYPE:
                e.vpmaxsw(dest, src1, src2);
                break;
              case INT32_TYPE:
                e.vpmaxsd(dest, src1, src2);
                break;
              default:
                assert_unhandled_case(part_type);
                break;
            }
          }
        });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_MAX, VECTOR_MAX);

// ============================================================================
// OPCODE_VECTOR_MIN
// ============================================================================
struct VECTOR_MIN
    : Sequence<VECTOR_MIN, I<OPCODE_VECTOR_MIN, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(
        e, i, [&i](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
          uint32_t part_type = i.instr->flags >> 8;
          if (i.instr->flags & ARITHMETIC_UNSIGNED) {
            switch (part_type) {
              case INT8_TYPE:
                e.vpminub(dest, src1, src2);
                break;
              case INT16_TYPE:
                e.vpminuw(dest, src1, src2);
                break;
              case INT32_TYPE:
                e.vpminud(dest, src1, src2);
                break;
              default:
                assert_unhandled_case(part_type);
                break;
            }
          } else {
            switch (part_type) {
              case INT8_TYPE:
                e.vpminsb(dest, src1, src2);
                break;
              case INT16_TYPE:
                e.vpminsw(dest, src1, src2);
                break;
              case INT32_TYPE:
                e.vpminsd(dest, src1, src2);
                break;
              default:
                assert_unhandled_case(part_type);
                break;
            }
          }
        });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_MIN, VECTOR_MIN);

// ============================================================================
// OPCODE_VECTOR_COMPARE_EQ
// ============================================================================
struct VECTOR_COMPARE_EQ_V128
    : Sequence<VECTOR_COMPARE_EQ_V128,
               I<OPCODE_VECTOR_COMPARE_EQ, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(
        e, i, [&i](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
          switch (i.instr->flags) {
            case INT8_TYPE:
              e.vpcmpeqb(dest, src1, src2);
              break;
            case INT16_TYPE:
              e.vpcmpeqw(dest, src1, src2);
              break;
            case INT32_TYPE:
              e.vpcmpeqd(dest, src1, src2);
              break;
            case FLOAT32_TYPE:
              e.vcmpeqps(dest, src1, src2);
              break;
          }
        });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_COMPARE_EQ, VECTOR_COMPARE_EQ_V128);

// ============================================================================
// OPCODE_VECTOR_COMPARE_SGT
// ============================================================================
struct VECTOR_COMPARE_SGT_V128
    : Sequence<VECTOR_COMPARE_SGT_V128,
               I<OPCODE_VECTOR_COMPARE_SGT, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitAssociativeBinaryXmmOp(
        e, i, [&i](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
          switch (i.instr->flags) {
            case INT8_TYPE:
              e.vpcmpgtb(dest, src1, src2);
              break;
            case INT16_TYPE:
              e.vpcmpgtw(dest, src1, src2);
              break;
            case INT32_TYPE:
              e.vpcmpgtd(dest, src1, src2);
              break;
            case FLOAT32_TYPE:
              e.vcmpgtps(dest, src1, src2);
              break;
          }
        });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_COMPARE_SGT, VECTOR_COMPARE_SGT_V128);

// ============================================================================
// OPCODE_VECTOR_COMPARE_SGE
// ============================================================================
struct VECTOR_COMPARE_SGE_V128
    : Sequence<VECTOR_COMPARE_SGE_V128,
               I<OPCODE_VECTOR_COMPARE_SGE, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitAssociativeBinaryXmmOp(
        e, i, [&i](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
          switch (i.instr->flags) {
            case INT8_TYPE:
              e.vpcmpeqb(e.xmm0, src1, src2);
              e.vpcmpgtb(dest, src1, src2);
              e.vpor(dest, e.xmm0);
              break;
            case INT16_TYPE:
              e.vpcmpeqw(e.xmm0, src1, src2);
              e.vpcmpgtw(dest, src1, src2);
              e.vpor(dest, e.xmm0);
              break;
            case INT32_TYPE:
              e.vpcmpeqd(e.xmm0, src1, src2);
              e.vpcmpgtd(dest, src1, src2);
              e.vpor(dest, e.xmm0);
              break;
            case FLOAT32_TYPE:
              e.vcmpgeps(dest, src1, src2);
              break;
          }
        });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_COMPARE_SGE, VECTOR_COMPARE_SGE_V128);

// ============================================================================
// OPCODE_VECTOR_COMPARE_UGT
// ============================================================================
struct VECTOR_COMPARE_UGT_V128
    : Sequence<VECTOR_COMPARE_UGT_V128,
               I<OPCODE_VECTOR_COMPARE_UGT, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    Xbyak::Address sign_addr = e.ptr[e.rax];  // dummy
    switch (i.instr->flags) {
      case INT8_TYPE:
        sign_addr = e.GetXmmConstPtr(XMMSignMaskI8);
        break;
      case INT16_TYPE:
        sign_addr = e.GetXmmConstPtr(XMMSignMaskI16);
        break;
      case INT32_TYPE:
        sign_addr = e.GetXmmConstPtr(XMMSignMaskI32);
        break;
      case FLOAT32_TYPE:
        sign_addr = e.GetXmmConstPtr(XMMSignMaskF32);
        break;
      default:
        assert_always();
        break;
    }
    if (i.src1.is_constant) {
      // TODO(benvanik): make this constant.
      e.LoadConstantXmm(e.xmm0, i.src1.constant());
      e.vpxor(e.xmm0, sign_addr);
    } else {
      e.vpxor(e.xmm0, i.src1, sign_addr);
    }
    if (i.src2.is_constant) {
      // TODO(benvanik): make this constant.
      e.LoadConstantXmm(e.xmm1, i.src2.constant());
      e.vpxor(e.xmm1, sign_addr);
    } else {
      e.vpxor(e.xmm1, i.src2, sign_addr);
    }
    switch (i.instr->flags) {
      case INT8_TYPE:
        e.vpcmpgtb(i.dest, e.xmm0, e.xmm1);
        break;
      case INT16_TYPE:
        e.vpcmpgtw(i.dest, e.xmm0, e.xmm1);
        break;
      case INT32_TYPE:
        e.vpcmpgtd(i.dest, e.xmm0, e.xmm1);
        break;
      case FLOAT32_TYPE:
        e.vcmpgtps(i.dest, e.xmm0, e.xmm1);
        break;
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_COMPARE_UGT, VECTOR_COMPARE_UGT_V128);

// ============================================================================
// OPCODE_VECTOR_COMPARE_UGE
// ============================================================================
struct VECTOR_COMPARE_UGE_V128
    : Sequence<VECTOR_COMPARE_UGE_V128,
               I<OPCODE_VECTOR_COMPARE_UGE, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    Xbyak::Address sign_addr = e.ptr[e.rax];  // dummy
    switch (i.instr->flags) {
      case INT8_TYPE:
        sign_addr = e.GetXmmConstPtr(XMMSignMaskI8);
        break;
      case INT16_TYPE:
        sign_addr = e.GetXmmConstPtr(XMMSignMaskI16);
        break;
      case INT32_TYPE:
        sign_addr = e.GetXmmConstPtr(XMMSignMaskI32);
        break;
      case FLOAT32_TYPE:
        sign_addr = e.GetXmmConstPtr(XMMSignMaskF32);
        break;
    }
    if (i.src1.is_constant) {
      // TODO(benvanik): make this constant.
      e.LoadConstantXmm(e.xmm0, i.src1.constant());
      e.vpxor(e.xmm0, sign_addr);
    } else {
      e.vpxor(e.xmm0, i.src1, sign_addr);
    }
    if (i.src2.is_constant) {
      // TODO(benvanik): make this constant.
      e.LoadConstantXmm(e.xmm1, i.src2.constant());
      e.vpxor(e.xmm1, sign_addr);
    } else {
      e.vpxor(e.xmm1, i.src2, sign_addr);
    }
    switch (i.instr->flags) {
      case INT8_TYPE:
        e.vpcmpeqb(e.xmm2, e.xmm0, e.xmm1);
        e.vpcmpgtb(i.dest, e.xmm0, e.xmm1);
        e.vpor(i.dest, e.xmm2);
        break;
      case INT16_TYPE:
        e.vpcmpeqw(e.xmm2, e.xmm0, e.xmm1);
        e.vpcmpgtw(i.dest, e.xmm0, e.xmm1);
        e.vpor(i.dest, e.xmm2);
        break;
      case INT32_TYPE:
        e.vpcmpeqd(e.xmm2, e.xmm0, e.xmm1);
        e.vpcmpgtd(i.dest, e.xmm0, e.xmm1);
        e.vpor(i.dest, e.xmm2);
        break;
      case FLOAT32_TYPE:
        e.vcmpgeps(i.dest, e.xmm0, e.xmm1);
        break;
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_COMPARE_UGE, VECTOR_COMPARE_UGE_V128);

// ============================================================================
// OPCODE_VECTOR_ADD
// ============================================================================
struct VECTOR_ADD
    : Sequence<VECTOR_ADD, I<OPCODE_VECTOR_ADD, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(
        e, i, [&i](X64Emitter& e, const Xmm& dest, Xmm src1, Xmm src2) {
          const TypeName part_type =
              static_cast<TypeName>(i.instr->flags & 0xFF);
          const uint32_t arithmetic_flags = i.instr->flags >> 8;
          bool is_unsigned = !!(arithmetic_flags & ARITHMETIC_UNSIGNED);
          bool saturate = !!(arithmetic_flags & ARITHMETIC_SATURATE);
          switch (part_type) {
            case INT8_TYPE:
              if (saturate) {
                // TODO(benvanik): trace DID_SATURATE
                if (is_unsigned) {
                  e.vpaddusb(dest, src1, src2);
                } else {
                  e.vpaddsb(dest, src1, src2);
                }
              } else {
                e.vpaddb(dest, src1, src2);
              }
              break;
            case INT16_TYPE:
              if (saturate) {
                // TODO(benvanik): trace DID_SATURATE
                if (is_unsigned) {
                  e.vpaddusw(dest, src1, src2);
                } else {
                  e.vpaddsw(dest, src1, src2);
                }
              } else {
                e.vpaddw(dest, src1, src2);
              }
              break;
            case INT32_TYPE:
              if (saturate) {
                if (is_unsigned) {
                  // xmm0 is the only temp register that can be used by
                  // src1/src2.
                  e.vpaddd(e.xmm1, src1, src2);

                  // If result is smaller than either of the inputs, we've
                  // overflowed (only need to check one input)
                  // if (src1 > res) then overflowed
                  // http://locklessinc.com/articles/sat_arithmetic/
                  e.vpxor(e.xmm2, src1, e.GetXmmConstPtr(XMMSignMaskI32));
                  e.vpxor(e.xmm0, e.xmm1, e.GetXmmConstPtr(XMMSignMaskI32));
                  e.vpcmpgtd(e.xmm0, e.xmm2, e.xmm0);
                  e.vpor(dest, e.xmm1, e.xmm0);
                } else {
                  e.vpaddd(e.xmm1, src1, src2);

                  // Overflow results if two inputs are the same sign and the
                  // result isn't the same sign. if ((s32b)(~(src1 ^ src2) &
                  // (src1 ^ res)) < 0) then overflowed
                  // http://locklessinc.com/articles/sat_arithmetic/
                  e.vpxor(e.xmm2, src1, src2);
                  e.vpxor(e.xmm3, src1, e.xmm1);
                  e.vpandn(e.xmm2, e.xmm2, e.xmm3);

                  // Set any negative overflowed elements of src1 to INT_MIN
                  e.vpand(e.xmm3, src1, e.xmm2);
                  e.vblendvps(e.xmm1, e.xmm1, e.GetXmmConstPtr(XMMSignMaskI32),
                              e.xmm3);

                  // Set any positive overflowed elements of src1 to INT_MAX
                  e.vpandn(e.xmm3, src1, e.xmm2);
                  e.vblendvps(dest, e.xmm1, e.GetXmmConstPtr(XMMAbsMaskPS),
                              e.xmm3);
                }
              } else {
                e.vpaddd(dest, src1, src2);
              }
              break;
            case FLOAT32_TYPE:
              assert_false(is_unsigned);
              assert_false(saturate);
              e.vaddps(dest, src1, src2);
              break;
            default:
              assert_unhandled_case(part_type);
              break;
          }
        });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_ADD, VECTOR_ADD);

// ============================================================================
// OPCODE_VECTOR_SUB
// ============================================================================
struct VECTOR_SUB
    : Sequence<VECTOR_SUB, I<OPCODE_VECTOR_SUB, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(
        e, i, [&i](X64Emitter& e, const Xmm& dest, Xmm src1, Xmm src2) {
          const TypeName part_type =
              static_cast<TypeName>(i.instr->flags & 0xFF);
          const uint32_t arithmetic_flags = i.instr->flags >> 8;
          bool is_unsigned = !!(arithmetic_flags & ARITHMETIC_UNSIGNED);
          bool saturate = !!(arithmetic_flags & ARITHMETIC_SATURATE);
          switch (part_type) {
            case INT8_TYPE:
              if (saturate) {
                // TODO(benvanik): trace DID_SATURATE
                if (is_unsigned) {
                  e.vpsubusb(dest, src1, src2);
                } else {
                  e.vpsubsb(dest, src1, src2);
                }
              } else {
                e.vpsubb(dest, src1, src2);
              }
              break;
            case INT16_TYPE:
              if (saturate) {
                // TODO(benvanik): trace DID_SATURATE
                if (is_unsigned) {
                  e.vpsubusw(dest, src1, src2);
                } else {
                  e.vpsubsw(dest, src1, src2);
                }
              } else {
                e.vpsubw(dest, src1, src2);
              }
              break;
            case INT32_TYPE:
              if (saturate) {
                if (is_unsigned) {
                  // xmm0 is the only temp register that can be used by
                  // src1/src2.
                  e.vpsubd(e.xmm1, src1, src2);

                  // If result is greater than either of the inputs, we've
                  // underflowed (only need to check one input)
                  // if (res > src1) then underflowed
                  // http://locklessinc.com/articles/sat_arithmetic/
                  e.vpxor(e.xmm2, src1, e.GetXmmConstPtr(XMMSignMaskI32));
                  e.vpxor(e.xmm0, e.xmm1, e.GetXmmConstPtr(XMMSignMaskI32));
                  e.vpcmpgtd(e.xmm0, e.xmm0, e.xmm2);
                  e.vpandn(dest, e.xmm0, e.xmm1);
                } else {
                  e.vpsubd(e.xmm1, src1, src2);

                  // We can only overflow if the signs of the operands are
                  // opposite. If signs are opposite and result sign isn't the
                  // same as src1's sign, we've overflowed. if ((s32b)((src1 ^
                  // src2) & (src1 ^ res)) < 0) then overflowed
                  // http://locklessinc.com/articles/sat_arithmetic/
                  e.vpxor(e.xmm2, src1, src2);
                  e.vpxor(e.xmm3, src1, e.xmm1);
                  e.vpand(e.xmm2, e.xmm2, e.xmm3);

                  // Set any negative overflowed elements of src1 to INT_MIN
                  e.vpand(e.xmm3, src1, e.xmm2);
                  e.vblendvps(e.xmm1, e.xmm1, e.GetXmmConstPtr(XMMSignMaskI32),
                              e.xmm3);

                  // Set any positive overflowed elements of src1 to INT_MAX
                  e.vpandn(e.xmm3, src1, e.xmm2);
                  e.vblendvps(dest, e.xmm1, e.GetXmmConstPtr(XMMAbsMaskPS),
                              e.xmm3);
                }
              } else {
                e.vpsubd(dest, src1, src2);
              }
              break;
            case FLOAT32_TYPE:
              e.vsubps(dest, src1, src2);
              break;
            default:
              assert_unhandled_case(part_type);
              break;
          }
        });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_SUB, VECTOR_SUB);

// ============================================================================
// OPCODE_VECTOR_SHL
// ============================================================================
template <typename T, std::enable_if_t<std::is_integral<T>::value, int> = 0>
static __m128i EmulateVectorShl(void*, __m128i src1, __m128i src2) {
  alignas(16) T value[16 / sizeof(T)];
  alignas(16) T shamt[16 / sizeof(T)];

  // Load SSE registers into a C array.
  _mm_store_si128(reinterpret_cast<__m128i*>(value), src1);
  _mm_store_si128(reinterpret_cast<__m128i*>(shamt), src2);

  for (size_t i = 0; i < (16 / sizeof(T)); ++i) {
    value[i] = value[i] << (shamt[i] & ((sizeof(T) * 8) - 1));
  }

  // Store result and return it.
  return _mm_load_si128(reinterpret_cast<__m128i*>(value));
}

struct VECTOR_SHL_V128
    : Sequence<VECTOR_SHL_V128, I<OPCODE_VECTOR_SHL, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    switch (i.instr->flags) {
      case INT8_TYPE:
        EmitInt8(e, i);
        break;
      case INT16_TYPE:
        EmitInt16(e, i);
        break;
      case INT32_TYPE:
        EmitInt32(e, i);
        break;
      default:
        assert_always();
        break;
    }
  }

  static void EmitInt8(X64Emitter& e, const EmitArgType& i) {
    // TODO(benvanik): native version (with shift magic).
    if (i.src2.is_constant) {
      unsigned char n = i.src2.constant().u8[0];
      bool all = true;
      for (unsigned j = 1; j < 16; ++j) {
        if (i.src2.constant().u8[j] != n) {
          all = false;
          break;
        }
      }
      if (all) {
        // we build a mask to eliminate the bits that will be shifted out of the
        // bytes so that they do not enter the adjacent bytes
        uint8_t mask = ~(uint8_t)(((uint16_t)0x100) >> n);
        vec128_t shiftout_mask;
        for (unsigned i = 0; i < 16; ++i) {
          shiftout_mask.u8[i] = mask;
        }
        e.LoadConstantXmm(e.xmm0, shiftout_mask);
        e.vpand(e.xmm0, e.xmm0, i.src1);
        e.vpsllw(i.dest, e.xmm0, n);
        return;
      }
    }
    if (i.src2.is_constant) {
      e.LoadConstantXmm(e.xmm0, i.src2.constant());
      e.lea(e.GetNativeParam(1), e.StashXmm(1, e.xmm0));
    } else {
      e.lea(e.GetNativeParam(1), e.StashXmm(1, i.src2));
    }
    e.lea(e.GetNativeParam(0), e.StashXmm(0, i.src1));
    e.CallNativeSafe(reinterpret_cast<void*>(EmulateVectorShl<uint8_t>));
    e.vmovaps(i.dest, e.xmm0);
  }

  static void EmitInt16(X64Emitter& e, const EmitArgType& i) {
    Xmm src1;
    if (i.src1.is_constant) {
      src1 = e.xmm2;
      e.LoadConstantXmm(src1, i.src1.constant());
    } else {
      src1 = i.src1;
    }

    if (i.src2.is_constant) {
      const auto& shamt = i.src2.constant();
      bool all_same = true;
      for (size_t n = 0; n < 8 - n; ++n) {
        if (shamt.u16[n] != shamt.u16[n + 1]) {
          all_same = false;
          break;
        }
      }
      if (all_same) {
        // Every count is the same, so we can use vpsllw.
        e.vpsllw(i.dest, src1, shamt.u16[0] & 0xF);
        return;
      }
    }

    // Shift 8 words in src1 by amount specified in src2.
    Xbyak::Label emu, end;

    // Only bother with this check if shift amt isn't constant.
    if (!i.src2.is_constant) {
      // See if the shift is equal first for a shortcut.
      e.vpshuflw(e.xmm0, i.src2, 0b00000000);
      e.vpshufd(e.xmm0, e.xmm0, 0b00000000);
      e.vpxor(e.xmm1, e.xmm0, i.src2);
      e.vptest(e.xmm1, e.xmm1);
      e.jnz(emu);

      // Equal. Shift using vpsllw.
      e.mov(e.rax, 0xF);
      e.vmovq(e.xmm1, e.rax);
      e.vpand(e.xmm0, e.xmm0, e.xmm1);
      e.vpsllw(i.dest, src1, e.xmm0);
      e.jmp(end);
    }

    // TODO(benvanik): native version (with shift magic).
    e.L(emu);
    if (i.src2.is_constant) {
      e.LoadConstantXmm(e.xmm0, i.src2.constant());
      e.lea(e.GetNativeParam(1), e.StashXmm(1, e.xmm0));
    } else {
      e.lea(e.GetNativeParam(1), e.StashXmm(1, i.src2));
    }
    e.lea(e.GetNativeParam(0), e.StashXmm(0, src1));
    e.CallNativeSafe(reinterpret_cast<void*>(EmulateVectorShl<uint16_t>));
    e.vmovaps(i.dest, e.xmm0);

    e.L(end);
  }

  static void EmitInt32(X64Emitter& e, const EmitArgType& i) {
    Xmm src1;
    if (i.src1.is_constant) {
      src1 = e.xmm2;
      e.LoadConstantXmm(src1, i.src1.constant());
    } else {
      src1 = i.src1;
    }

    if (i.src2.is_constant) {
      const auto& shamt = i.src2.constant();
      bool all_same = true;
      for (size_t n = 0; n < 4 - n; ++n) {
        if (shamt.u32[n] != shamt.u32[n + 1]) {
          all_same = false;
          break;
        }
      }
      if (all_same) {
        // Every count is the same, so we can use vpslld.
        e.vpslld(i.dest, src1, shamt.u8[0] & 0x1F);
        return;
      }
    }

    if (e.IsFeatureEnabled(kX64EmitAVX2)) {
      if (i.src2.is_constant) {
        const auto& shamt = i.src2.constant();
        // Counts differ, so pre-mask and load constant.
        vec128_t masked = i.src2.constant();
        for (size_t n = 0; n < 4; ++n) {
          masked.u32[n] &= 0x1F;
        }
        e.LoadConstantXmm(e.xmm0, masked);
        e.vpsllvd(i.dest, src1, e.xmm0);
      } else {
        // Fully variable shift.
        // src shift mask may have values >31, and x86 sets to zero when
        // that happens so we mask.
        e.vandps(e.xmm0, i.src2, e.GetXmmConstPtr(XMMShiftMaskPS));
        e.vpsllvd(i.dest, src1, e.xmm0);
      }
    } else {
      // Shift 4 words in src1 by amount specified in src2.
      Xbyak::Label emu, end;

      // See if the shift is equal first for a shortcut.
      // Only bother with this check if shift amt isn't constant.
      if (!i.src2.is_constant) {
        e.vpshufd(e.xmm0, i.src2, 0b00000000);
        e.vpxor(e.xmm1, e.xmm0, i.src2);
        e.vptest(e.xmm1, e.xmm1);
        e.jnz(emu);

        // Equal. Shift using vpsrad.
        e.mov(e.rax, 0x1F);
        e.vmovq(e.xmm1, e.rax);
        e.vpand(e.xmm0, e.xmm0, e.xmm1);
        e.vpslld(i.dest, src1, e.xmm0);
        e.jmp(end);
      }

      // TODO(benvanik): native version (with shift magic).
      e.L(emu);
      if (i.src2.is_constant) {
        e.LoadConstantXmm(e.xmm0, i.src2.constant());
        e.lea(e.GetNativeParam(1), e.StashXmm(1, e.xmm0));
      } else {
        e.lea(e.GetNativeParam(1), e.StashXmm(1, i.src2));
      }
      e.lea(e.GetNativeParam(0), e.StashXmm(0, src1));
      e.CallNativeSafe(reinterpret_cast<void*>(EmulateVectorShl<uint32_t>));
      e.vmovaps(i.dest, e.xmm0);

      e.L(end);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_SHL, VECTOR_SHL_V128);

// ============================================================================
// OPCODE_VECTOR_SHR
// ============================================================================
template <typename T, std::enable_if_t<std::is_integral<T>::value, int> = 0>
static __m128i EmulateVectorShr(void*, __m128i src1, __m128i src2) {
  alignas(16) T value[16 / sizeof(T)];
  alignas(16) T shamt[16 / sizeof(T)];

  // Load SSE registers into a C array.
  _mm_store_si128(reinterpret_cast<__m128i*>(value), src1);
  _mm_store_si128(reinterpret_cast<__m128i*>(shamt), src2);

  for (size_t i = 0; i < (16 / sizeof(T)); ++i) {
    value[i] = value[i] >> (shamt[i] & ((sizeof(T) * 8) - 1));
  }

  // Store result and return it.
  return _mm_load_si128(reinterpret_cast<__m128i*>(value));
}

struct VECTOR_SHR_V128
    : Sequence<VECTOR_SHR_V128, I<OPCODE_VECTOR_SHR, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    switch (i.instr->flags) {
      case INT8_TYPE:
        EmitInt8(e, i);
        break;
      case INT16_TYPE:
        EmitInt16(e, i);
        break;
      case INT32_TYPE:
        EmitInt32(e, i);
        break;
      default:
        assert_always();
        break;
    }
  }

  static void EmitInt8(X64Emitter& e, const EmitArgType& i) {
    // TODO(benvanik): native version (with shift magic).
    if (i.src2.is_constant) {
      e.LoadConstantXmm(e.xmm0, i.src2.constant());
      e.lea(e.GetNativeParam(1), e.StashXmm(1, e.xmm0));
    } else {
      e.lea(e.GetNativeParam(1), e.StashXmm(1, i.src2));
    }
    e.lea(e.GetNativeParam(0), e.StashXmm(0, i.src1));
    e.CallNativeSafe(reinterpret_cast<void*>(EmulateVectorShr<uint8_t>));
    e.vmovaps(i.dest, e.xmm0);
  }

  static void EmitInt16(X64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      const auto& shamt = i.src2.constant();
      bool all_same = true;
      for (size_t n = 0; n < 8 - n; ++n) {
        if (shamt.u16[n] != shamt.u16[n + 1]) {
          all_same = false;
          break;
        }
      }
      if (all_same) {
        // Every count is the same, so we can use vpsllw.
        e.vpsrlw(i.dest, i.src1, shamt.u16[0] & 0xF);
        return;
      }
    }

    // Shift 8 words in src1 by amount specified in src2.
    Xbyak::Label emu, end;

    // See if the shift is equal first for a shortcut.
    // Only bother with this check if shift amt isn't constant.
    if (!i.src2.is_constant) {
      e.vpshuflw(e.xmm0, i.src2, 0b00000000);
      e.vpshufd(e.xmm0, e.xmm0, 0b00000000);
      e.vpxor(e.xmm1, e.xmm0, i.src2);
      e.vptest(e.xmm1, e.xmm1);
      e.jnz(emu);

      // Equal. Shift using vpsrlw.
      e.mov(e.rax, 0xF);
      e.vmovq(e.xmm1, e.rax);
      e.vpand(e.xmm0, e.xmm0, e.xmm1);
      e.vpsrlw(i.dest, i.src1, e.xmm0);
      e.jmp(end);
    }

    // TODO(benvanik): native version (with shift magic).
    e.L(emu);
    if (i.src2.is_constant) {
      e.LoadConstantXmm(e.xmm0, i.src2.constant());
      e.lea(e.GetNativeParam(1), e.StashXmm(1, e.xmm0));
    } else {
      e.lea(e.GetNativeParam(1), e.StashXmm(1, i.src2));
    }
    e.lea(e.GetNativeParam(0), e.StashXmm(0, i.src1));
    e.CallNativeSafe(reinterpret_cast<void*>(EmulateVectorShr<uint16_t>));
    e.vmovaps(i.dest, e.xmm0);

    e.L(end);
  }

  static void EmitInt32(X64Emitter& e, const EmitArgType& i) {
    Xmm src1;
    if (i.src1.is_constant) {
      src1 = e.xmm2;
      e.LoadConstantXmm(src1, i.src1.constant());
    } else {
      src1 = i.src1;
    }

    if (i.src2.is_constant) {
      const auto& shamt = i.src2.constant();
      bool all_same = true;
      for (size_t n = 0; n < 4 - n; ++n) {
        if (shamt.u32[n] != shamt.u32[n + 1]) {
          all_same = false;
          break;
        }
      }
      if (all_same) {
        // Every count is the same, so we can use vpsrld.
        e.vpsrld(i.dest, src1, shamt.u8[0] & 0x1F);
        return;
      } else {
        if (e.IsFeatureEnabled(kX64EmitAVX2)) {
          // Counts differ, so pre-mask and load constant.
          vec128_t masked = i.src2.constant();
          for (size_t n = 0; n < 4; ++n) {
            masked.u32[n] &= 0x1F;
          }
          e.LoadConstantXmm(e.xmm0, masked);
          e.vpsrlvd(i.dest, src1, e.xmm0);
          return;
        }
      }
    }

    if (e.IsFeatureEnabled(kX64EmitAVX2)) {
      // Fully variable shift.
      // src shift mask may have values >31, and x86 sets to zero when
      // that happens so we mask.
      e.vandps(e.xmm0, i.src2, e.GetXmmConstPtr(XMMShiftMaskPS));
      e.vpsrlvd(i.dest, src1, e.xmm0);
    } else {
      // Shift 4 words in src1 by amount specified in src2.
      Xbyak::Label emu, end;

      // See if the shift is equal first for a shortcut.
      // Only bother with this check if shift amt isn't constant.
      if (!i.src2.is_constant) {
        e.vpshufd(e.xmm0, i.src2, 0b00000000);
        e.vpxor(e.xmm1, e.xmm0, i.src2);
        e.vptest(e.xmm1, e.xmm1);
        e.jnz(emu);

        // Equal. Shift using vpsrld.
        e.mov(e.rax, 0x1F);
        e.vmovq(e.xmm1, e.rax);
        e.vpand(e.xmm0, e.xmm0, e.xmm1);
        e.vpsrld(i.dest, src1, e.xmm0);
        e.jmp(end);
      }

      // TODO(benvanik): native version.
      e.L(emu);
      if (i.src2.is_constant) {
        e.LoadConstantXmm(e.xmm0, i.src2.constant());
        e.lea(e.GetNativeParam(1), e.StashXmm(1, e.xmm0));
      } else {
        e.lea(e.GetNativeParam(1), e.StashXmm(1, i.src2));
      }
      e.lea(e.GetNativeParam(0), e.StashXmm(0, src1));
      e.CallNativeSafe(reinterpret_cast<void*>(EmulateVectorShr<uint32_t>));
      e.vmovaps(i.dest, e.xmm0);

      e.L(end);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_SHR, VECTOR_SHR_V128);

// ============================================================================
// OPCODE_VECTOR_SHA
// ============================================================================
struct VECTOR_SHA_V128
    : Sequence<VECTOR_SHA_V128, I<OPCODE_VECTOR_SHA, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    switch (i.instr->flags) {
      case INT8_TYPE:
        EmitInt8(e, i);
        break;
      case INT16_TYPE:
        EmitInt16(e, i);
        break;
      case INT32_TYPE:
        EmitInt32(e, i);
        break;
      default:
        assert_always();
        break;
    }
  }

  static void EmitInt8(X64Emitter& e, const EmitArgType& i) {
    // TODO(benvanik): native version (with shift magic).
    if (i.src2.is_constant) {
      e.LoadConstantXmm(e.xmm0, i.src2.constant());
      e.lea(e.GetNativeParam(1), e.StashXmm(1, e.xmm0));
    } else {
      e.lea(e.GetNativeParam(1), e.StashXmm(1, i.src2));
    }
    e.lea(e.GetNativeParam(0), e.StashXmm(0, i.src1));
    e.CallNativeSafe(reinterpret_cast<void*>(EmulateVectorShr<int8_t>));
    e.vmovaps(i.dest, e.xmm0);
  }

  static void EmitInt16(X64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      const auto& shamt = i.src2.constant();
      bool all_same = true;
      for (size_t n = 0; n < 8 - n; ++n) {
        if (shamt.u16[n] != shamt.u16[n + 1]) {
          all_same = false;
          break;
        }
      }
      if (all_same) {
        // Every count is the same, so we can use vpsraw.
        e.vpsraw(i.dest, i.src1, shamt.u16[0] & 0xF);
        return;
      }
    }

    // Shift 8 words in src1 by amount specified in src2.
    Xbyak::Label emu, end;

    // See if the shift is equal first for a shortcut.
    // Only bother with this check if shift amt isn't constant.
    if (!i.src2.is_constant) {
      e.vpshuflw(e.xmm0, i.src2, 0b00000000);
      e.vpshufd(e.xmm0, e.xmm0, 0b00000000);
      e.vpxor(e.xmm1, e.xmm0, i.src2);
      e.vptest(e.xmm1, e.xmm1);
      e.jnz(emu);

      // Equal. Shift using vpsraw.
      e.mov(e.rax, 0xF);
      e.vmovq(e.xmm1, e.rax);
      e.vpand(e.xmm0, e.xmm0, e.xmm1);
      e.vpsraw(i.dest, i.src1, e.xmm0);
      e.jmp(end);
    }

    // TODO(benvanik): native version (with shift magic).
    e.L(emu);
    if (i.src2.is_constant) {
      e.LoadConstantXmm(e.xmm0, i.src2.constant());
      e.lea(e.GetNativeParam(1), e.StashXmm(1, e.xmm0));
    } else {
      e.lea(e.GetNativeParam(1), e.StashXmm(1, i.src2));
    }
    e.lea(e.GetNativeParam(0), e.StashXmm(0, i.src1));
    e.CallNativeSafe(reinterpret_cast<void*>(EmulateVectorShr<int16_t>));
    e.vmovaps(i.dest, e.xmm0);

    e.L(end);
  }

  static void EmitInt32(X64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      const auto& shamt = i.src2.constant();
      bool all_same = true;
      for (size_t n = 0; n < 4 - n; ++n) {
        if (shamt.u32[n] != shamt.u32[n + 1]) {
          all_same = false;
          break;
        }
      }
      if (all_same) {
        // Every count is the same, so we can use vpsrad.
        e.vpsrad(i.dest, i.src1, shamt.u32[0] & 0x1F);
        return;
      }
    }

    if (e.IsFeatureEnabled(kX64EmitAVX2)) {
      // src shift mask may have values >31, and x86 sets to zero when
      // that happens so we mask.
      if (i.src2.is_constant) {
        e.LoadConstantXmm(e.xmm0, i.src2.constant());
        e.vandps(e.xmm0, e.GetXmmConstPtr(XMMShiftMaskPS));
      } else {
        e.vandps(e.xmm0, i.src2, e.GetXmmConstPtr(XMMShiftMaskPS));
      }
      e.vpsravd(i.dest, i.src1, e.xmm0);
    } else {
      // Shift 4 words in src1 by amount specified in src2.
      Xbyak::Label emu, end;

      // See if the shift is equal first for a shortcut.
      // Only bother with this check if shift amt isn't constant.
      if (!i.src2.is_constant) {
        e.vpshufd(e.xmm0, i.src2, 0b00000000);
        e.vpxor(e.xmm1, e.xmm0, i.src2);
        e.vptest(e.xmm1, e.xmm1);
        e.jnz(emu);

        // Equal. Shift using vpsrad.
        e.mov(e.rax, 0x1F);
        e.vmovq(e.xmm1, e.rax);
        e.vpand(e.xmm0, e.xmm0, e.xmm1);
        e.vpsrad(i.dest, i.src1, e.xmm0);
        e.jmp(end);
      }

      // TODO(benvanik): native version.
      e.L(emu);
      if (i.src2.is_constant) {
        e.LoadConstantXmm(e.xmm0, i.src2.constant());
        e.lea(e.GetNativeParam(1), e.StashXmm(1, e.xmm0));
      } else {
        e.lea(e.GetNativeParam(1), e.StashXmm(1, i.src2));
      }
      e.lea(e.GetNativeParam(0), e.StashXmm(0, i.src1));
      e.CallNativeSafe(reinterpret_cast<void*>(EmulateVectorShr<int32_t>));
      e.vmovaps(i.dest, e.xmm0);

      e.L(end);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_SHA, VECTOR_SHA_V128);

// ============================================================================
// OPCODE_VECTOR_ROTATE_LEFT
// ============================================================================
template <typename T, std::enable_if_t<std::is_integral<T>::value, int> = 0>
static __m128i EmulateVectorRotateLeft(void*, __m128i src1, __m128i src2) {
  alignas(16) T value[16 / sizeof(T)];
  alignas(16) T shamt[16 / sizeof(T)];

  // Load SSE registers into a C array.
  _mm_store_si128(reinterpret_cast<__m128i*>(value), src1);
  _mm_store_si128(reinterpret_cast<__m128i*>(shamt), src2);

  for (size_t i = 0; i < (16 / sizeof(T)); ++i) {
    value[i] = xe::rotate_left<T>(value[i], shamt[i] & ((sizeof(T) * 8) - 1));
  }

  // Store result and return it.
  return _mm_load_si128(reinterpret_cast<__m128i*>(value));
}

// TODO(benvanik): AVX512 has a native variable rotate (rolv).
struct VECTOR_ROTATE_LEFT_V128
    : Sequence<VECTOR_ROTATE_LEFT_V128,
               I<OPCODE_VECTOR_ROTATE_LEFT, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    switch (i.instr->flags) {
      case INT8_TYPE:
        // TODO(benvanik): native version (with shift magic).
        e.lea(e.GetNativeParam(0), e.StashXmm(0, i.src1));
        if (i.src2.is_constant) {
          e.LoadConstantXmm(e.xmm0, i.src2.constant());
          e.lea(e.GetNativeParam(1), e.StashXmm(1, e.xmm0));
        } else {
          e.lea(e.GetNativeParam(1), e.StashXmm(1, i.src2));
        }
        e.CallNativeSafe(
            reinterpret_cast<void*>(EmulateVectorRotateLeft<uint8_t>));
        e.vmovaps(i.dest, e.xmm0);
        break;
      case INT16_TYPE:
        // TODO(benvanik): native version (with shift magic).
        e.lea(e.GetNativeParam(0), e.StashXmm(0, i.src1));
        if (i.src2.is_constant) {
          e.LoadConstantXmm(e.xmm0, i.src2.constant());
          e.lea(e.GetNativeParam(1), e.StashXmm(1, e.xmm0));
        } else {
          e.lea(e.GetNativeParam(1), e.StashXmm(1, i.src2));
        }
        e.CallNativeSafe(
            reinterpret_cast<void*>(EmulateVectorRotateLeft<uint16_t>));
        e.vmovaps(i.dest, e.xmm0);
        break;
      case INT32_TYPE: {
        if (e.IsFeatureEnabled(kX64EmitAVX2)) {
          Xmm temp = i.dest;
          if (i.dest == i.src1 || i.dest == i.src2) {
            temp = e.xmm2;
          }
          // Shift left (to get high bits):
          e.vpand(e.xmm0, i.src2, e.GetXmmConstPtr(XMMShiftMaskPS));
          e.vpsllvd(e.xmm1, i.src1, e.xmm0);
          // Shift right (to get low bits):
          e.vmovaps(temp, e.GetXmmConstPtr(XMMPI32));
          e.vpsubd(temp, e.xmm0);
          e.vpsrlvd(i.dest, i.src1, temp);
          // Merge:
          e.vpor(i.dest, e.xmm1);
        } else {
          // TODO(benvanik): non-AVX2 native version.
          e.lea(e.GetNativeParam(0), e.StashXmm(0, i.src1));
          if (i.src2.is_constant) {
            e.LoadConstantXmm(e.xmm0, i.src2.constant());
            e.lea(e.GetNativeParam(1), e.StashXmm(1, e.xmm0));
          } else {
            e.lea(e.GetNativeParam(1), e.StashXmm(1, i.src2));
          }
          e.CallNativeSafe(
              reinterpret_cast<void*>(EmulateVectorRotateLeft<uint32_t>));
          e.vmovaps(i.dest, e.xmm0);
        }
        break;
      }
      default:
        assert_always();
        break;
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_ROTATE_LEFT, VECTOR_ROTATE_LEFT_V128);

// ============================================================================
// OPCODE_VECTOR_AVERAGE
// ============================================================================
template <typename T, std::enable_if_t<std::is_integral<T>::value, int> = 0>
static __m128i EmulateVectorAverage(void*, __m128i src1, __m128i src2) {
  alignas(16) T src1v[16 / sizeof(T)];
  alignas(16) T src2v[16 / sizeof(T)];
  alignas(16) T value[16 / sizeof(T)];

  // Load SSE registers into a C array.
  _mm_store_si128(reinterpret_cast<__m128i*>(src1v), src1);
  _mm_store_si128(reinterpret_cast<__m128i*>(src2v), src2);

  for (size_t i = 0; i < (16 / sizeof(T)); ++i) {
    auto t = (uint64_t(src1v[i]) + uint64_t(src2v[i]) + 1) / 2;
    value[i] = T(t);
  }

  // Store result and return it.
  return _mm_load_si128(reinterpret_cast<__m128i*>(value));
}

struct VECTOR_AVERAGE
    : Sequence<VECTOR_AVERAGE,
               I<OPCODE_VECTOR_AVERAGE, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(
        e, i,
        [&i](X64Emitter& e, const Xmm& dest, const Xmm& src1, const Xmm& src2) {
          const TypeName part_type =
              static_cast<TypeName>(i.instr->flags & 0xFF);
          const uint32_t arithmetic_flags = i.instr->flags >> 8;
          bool is_unsigned = !!(arithmetic_flags & ARITHMETIC_UNSIGNED);
          switch (part_type) {
            case INT8_TYPE:
              if (is_unsigned) {
                e.vpavgb(dest, src1, src2);
              } else {
                assert_always();
              }
              break;
            case INT16_TYPE:
              if (is_unsigned) {
                e.vpavgw(dest, src1, src2);
              } else {
                assert_always();
              }
              break;
            case INT32_TYPE:
              // No 32bit averages in AVX.
              if (is_unsigned) {
                if (i.src2.is_constant) {
                  e.LoadConstantXmm(e.xmm0, i.src2.constant());
                  e.lea(e.GetNativeParam(1), e.StashXmm(1, e.xmm0));
                } else {
                  e.lea(e.GetNativeParam(1), e.StashXmm(1, i.src2));
                }
                e.lea(e.GetNativeParam(0), e.StashXmm(0, i.src1));
                e.CallNativeSafe(
                    reinterpret_cast<void*>(EmulateVectorAverage<uint32_t>));
                e.vmovaps(i.dest, e.xmm0);
              } else {
                if (i.src2.is_constant) {
                  e.LoadConstantXmm(e.xmm0, i.src2.constant());
                  e.lea(e.GetNativeParam(1), e.StashXmm(1, e.xmm0));
                } else {
                  e.lea(e.GetNativeParam(1), e.StashXmm(1, i.src2));
                }
                e.lea(e.GetNativeParam(0), e.StashXmm(0, i.src1));
                e.CallNativeSafe(
                    reinterpret_cast<void*>(EmulateVectorAverage<int32_t>));
                e.vmovaps(i.dest, e.xmm0);
              }
              break;
            default:
              assert_unhandled_case(part_type);
              break;
          }
        });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_AVERAGE, VECTOR_AVERAGE);

// ============================================================================
// OPCODE_INSERT
// ============================================================================
struct INSERT_I8
    : Sequence<INSERT_I8, I<OPCODE_INSERT, V128Op, V128Op, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    assert_true(i.src2.is_constant);
    e.vpinsrb(i.dest, i.src3.reg().cvt32(), i.src2.constant() ^ 0x3);
  }
};
struct INSERT_I16
    : Sequence<INSERT_I16, I<OPCODE_INSERT, V128Op, V128Op, I8Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    assert_true(i.src2.is_constant);
    e.vpinsrw(i.dest, i.src3.reg().cvt32(), i.src2.constant() ^ 0x1);
  }
};
struct INSERT_I32
    : Sequence<INSERT_I32, I<OPCODE_INSERT, V128Op, V128Op, I8Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    assert_true(i.src2.is_constant);
    e.vpinsrd(i.dest, i.src3, i.src2.constant());
  }
};
EMITTER_OPCODE_TABLE(OPCODE_INSERT, INSERT_I8, INSERT_I16, INSERT_I32);

// ============================================================================
// OPCODE_EXTRACT
// ============================================================================
// TODO(benvanik): sequence extract/splat:
//  v0.i32 = extract v0.v128, 0
//  v0.v128 = splat v0.i32
// This can be a single broadcast.
struct EXTRACT_I8
    : Sequence<EXTRACT_I8, I<OPCODE_EXTRACT, I8Op, V128Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      e.vpextrb(i.dest.reg().cvt32(), i.src1, VEC128_B(i.src2.constant()));
    } else {
      e.mov(e.eax, 0x00000003);
      e.xor_(e.al, i.src2);
      e.and_(e.al, 0x1F);
      e.vmovd(e.xmm0, e.eax);
      e.vpshufb(e.xmm0, i.src1, e.xmm0);
      e.vmovd(i.dest.reg().cvt32(), e.xmm0);
      e.and_(i.dest, uint8_t(0xFF));
    }
  }
};
struct EXTRACT_I16
    : Sequence<EXTRACT_I16, I<OPCODE_EXTRACT, I16Op, V128Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      e.vpextrw(i.dest.reg().cvt32(), i.src1, VEC128_W(i.src2.constant()));
    } else {
      e.mov(e.al, i.src2);
      e.xor_(e.al, 0x01);
      e.shl(e.al, 1);
      e.mov(e.ah, e.al);
      e.add(e.ah, 1);
      e.vmovd(e.xmm0, e.eax);
      e.vpshufb(e.xmm0, i.src1, e.xmm0);
      e.vmovd(i.dest.reg().cvt32(), e.xmm0);
      e.and_(i.dest.reg().cvt32(), 0xFFFFu);
    }
  }
};
struct EXTRACT_I32
    : Sequence<EXTRACT_I32, I<OPCODE_EXTRACT, I32Op, V128Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    static const vec128_t extract_table_32[4] = {
        vec128b(3, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
        vec128b(7, 6, 5, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
        vec128b(11, 10, 9, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
        vec128b(15, 14, 13, 12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
    };
    if (i.src2.is_constant) {
      // TODO(gibbed): add support to constant propagation pass for
      // OPCODE_EXTRACT.
      Xmm src1;
      if (i.src1.is_constant) {
        src1 = e.xmm0;
        e.LoadConstantXmm(src1, i.src1.constant());
      } else {
        src1 = i.src1;
      }
      if (i.src2.constant() == 0) {
        e.vmovd(i.dest, src1);
      } else {
        e.vpextrd(i.dest, src1, VEC128_D(i.src2.constant()));
      }
    } else {
      // TODO(benvanik): try out hlide's version:
      // e.mov(e.eax, 3);
      // e.and_(e.al, i.src2);       // eax = [(i&3), 0, 0, 0]
      // e.imul(e.eax, 0x04040404); // [(i&3)*4, (i&3)*4, (i&3)*4, (i&3)*4]
      // e.add(e.eax, 0x00010203);  // [((i&3)*4)+3, ((i&3)*4)+2, ((i&3)*4)+1,
      // ((i&3)*4)+0]
      // e.vmovd(e.xmm0, e.eax);
      // e.vpshufb(e.xmm0, i.src1, e.xmm0);
      // e.vmovd(i.dest.reg().cvt32(), e.xmm0);
      // Get the desired word in xmm0, then extract that.
      Xmm src1;
      if (i.src1.is_constant) {
        src1 = e.xmm1;
        e.LoadConstantXmm(src1, i.src1.constant());
      } else {
        src1 = i.src1.reg();
      }

      e.xor_(e.rax, e.rax);
      e.mov(e.al, i.src2);
      e.and_(e.al, 0x03);
      e.shl(e.al, 4);
      e.mov(e.rdx, reinterpret_cast<uint64_t>(extract_table_32));
      e.vmovaps(e.xmm0, e.ptr[e.rdx + e.rax]);
      e.vpshufb(e.xmm0, src1, e.xmm0);
      e.vpextrd(i.dest, e.xmm0, 0);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_EXTRACT, EXTRACT_I8, EXTRACT_I16, EXTRACT_I32);

// ============================================================================
// OPCODE_SPLAT
// ============================================================================
// Copy a value into all elements of a vector
struct SPLAT_I8 : Sequence<SPLAT_I8, I<OPCODE_SPLAT, V128Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      // TODO(benvanik): faster constant splats.
      e.mov(e.eax, i.src1.constant());
      e.vmovd(e.xmm0, e.eax);
    } else {
      e.vmovd(e.xmm0, i.src1.reg().cvt32());
    }

    if (e.IsFeatureEnabled(kX64EmitAVX2)) {
      e.vpbroadcastb(i.dest, e.xmm0);
    } else {
      e.vpunpcklbw(e.xmm0, e.xmm0);
      e.vpunpcklwd(e.xmm0, e.xmm0);
      e.vpshufd(i.dest, e.xmm0, 0);
    }
  }
};
struct SPLAT_I16 : Sequence<SPLAT_I16, I<OPCODE_SPLAT, V128Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      // TODO(benvanik): faster constant splats.
      e.mov(e.eax, i.src1.constant());
      e.vmovd(e.xmm0, e.eax);
    } else {
      e.vmovd(e.xmm0, i.src1.reg().cvt32());
    }

    if (e.IsFeatureEnabled(kX64EmitAVX2)) {
      e.vpbroadcastw(i.dest, e.xmm0);
    } else {
      e.vpunpcklwd(e.xmm0, e.xmm0);  // unpack low word data
      e.vpshufd(i.dest, e.xmm0, 0);
    }
  }
};
struct SPLAT_I32 : Sequence<SPLAT_I32, I<OPCODE_SPLAT, V128Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      // TODO(benvanik): faster constant splats.
      e.mov(e.eax, i.src1.constant());
      e.vmovd(e.xmm0, e.eax);
    } else {
      e.vmovd(e.xmm0, i.src1);
    }

    if (e.IsFeatureEnabled(kX64EmitAVX2)) {
      e.vpbroadcastd(i.dest, e.xmm0);
    } else {
      e.vpshufd(i.dest, e.xmm0, 0);
    }
  }
};
struct SPLAT_F32 : Sequence<SPLAT_F32, I<OPCODE_SPLAT, V128Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (e.IsFeatureEnabled(kX64EmitAVX2)) {
      if (i.src1.is_constant) {
        // TODO(benvanik): faster constant splats.
        e.mov(e.eax, i.src1.value->constant.i32);
        e.vmovd(e.xmm0, e.eax);
        e.vbroadcastss(i.dest, e.xmm0);
      } else {
        e.vbroadcastss(i.dest, i.src1);
      }
    } else {
      if (i.src1.is_constant) {
        e.mov(e.eax, i.src1.value->constant.i32);
        e.vmovd(i.dest, e.eax);
        e.vshufps(i.dest, i.dest, i.dest, 0);
      } else {
        e.vshufps(i.dest, i.src1, i.src1, 0);
      }
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SPLAT, SPLAT_I8, SPLAT_I16, SPLAT_I32, SPLAT_F32);

// ============================================================================
// OPCODE_PERMUTE
// ============================================================================
struct PERMUTE_I32
    : Sequence<PERMUTE_I32, I<OPCODE_PERMUTE, V128Op, I32Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    assert_true(i.instr->flags == INT32_TYPE);
    // Permute words between src2 and src3.
    // TODO(benvanik): check src3 for zero. if 0, we can use pshufb.
    if (i.src1.is_constant) {
      uint32_t control = i.src1.constant();
      // Shuffle things into the right places in dest & xmm0,
      // then we blend them together.
      uint32_t src_control =
          (((control >> 24) & 0x3) << 6) | (((control >> 16) & 0x3) << 4) |
          (((control >> 8) & 0x3) << 2) | (((control >> 0) & 0x3) << 0);

      uint32_t blend_control = 0;
      if (e.IsFeatureEnabled(kX64EmitAVX2)) {
        // Blender for vpblendd
        blend_control =
            (((control >> 26) & 0x1) << 3) | (((control >> 18) & 0x1) << 2) |
            (((control >> 10) & 0x1) << 1) | (((control >> 2) & 0x1) << 0);
      } else {
        // Blender for vpblendw
        blend_control =
            (((control >> 26) & 0x1) << 6) | (((control >> 18) & 0x1) << 4) |
            (((control >> 10) & 0x1) << 2) | (((control >> 2) & 0x1) << 0);
        blend_control |= blend_control << 1;
      }

      // TODO(benvanik): if src2/src3 are constants, shuffle now!
      Xmm src2;
      if (i.src2.is_constant) {
        src2 = e.xmm1;
        e.LoadConstantXmm(src2, i.src2.constant());
      } else {
        src2 = i.src2;
      }
      Xmm src3;
      if (i.src3.is_constant) {
        src3 = e.xmm2;
        e.LoadConstantXmm(src3, i.src3.constant());
      } else {
        src3 = i.src3;
      }
      if (i.dest != src3) {
        e.vpshufd(i.dest, src2, src_control);
        e.vpshufd(e.xmm0, src3, src_control);
      } else {
        e.vmovaps(e.xmm0, src3);
        e.vpshufd(i.dest, src2, src_control);
        e.vpshufd(e.xmm0, e.xmm0, src_control);
      }

      if (e.IsFeatureEnabled(kX64EmitAVX2)) {
        e.vpblendd(i.dest, e.xmm0, blend_control);  // $0 = $1 <blend> $2
      } else {
        e.vpblendw(i.dest, e.xmm0, blend_control);  // $0 = $1 <blend> $2
      }
    } else {
      // Permute by non-constant.
      assert_always();
    }
  }
};
struct PERMUTE_V128
    : Sequence<PERMUTE_V128,
               I<OPCODE_PERMUTE, V128Op, V128Op, V128Op, V128Op>> {
  static void EmitByInt8(X64Emitter& e, const EmitArgType& i) {
    // TODO(benvanik): find out how to do this with only one temp register!
    // Permute bytes between src2 and src3.
    // src1 is an array of indices corresponding to positions within src2 and
    // src3.
    if (i.src3.value->IsConstantZero()) {
      // Permuting with src2/zero, so just shuffle/mask.
      if (i.src2.value->IsConstantZero()) {
        // src2 & src3 are zero, so result will always be zero.
        e.vpxor(i.dest, i.dest);
      } else {
        // Control mask needs to be shuffled.
        if (i.src1.is_constant) {
          e.LoadConstantXmm(e.xmm0, i.src1.constant());
          e.vxorps(e.xmm0, e.xmm0, e.GetXmmConstPtr(XMMSwapWordMask));
        } else {
          e.vxorps(e.xmm0, i.src1, e.GetXmmConstPtr(XMMSwapWordMask));
        }
        e.vpand(e.xmm0, e.GetXmmConstPtr(XMMPermuteByteMask));
        if (i.src2.is_constant) {
          e.LoadConstantXmm(i.dest, i.src2.constant());
          e.vpshufb(i.dest, i.dest, e.xmm0);
        } else {
          e.vpshufb(i.dest, i.src2, e.xmm0);
        }
        // Build a mask with values in src2 having 0 and values in src3 having
        // 1.
        e.vpcmpgtb(e.xmm0, e.xmm0, e.GetXmmConstPtr(XMMPermuteControl15));
        e.vpandn(i.dest, e.xmm0, i.dest);
      }
    } else {
      // General permute.
      // Control mask needs to be shuffled.
      // TODO(benvanik): do constants here instead of in generated code.
      if (i.src1.is_constant) {
        e.LoadConstantXmm(e.xmm2, i.src1.constant());
        e.vxorps(e.xmm2, e.xmm2, e.GetXmmConstPtr(XMMSwapWordMask));
      } else {
        e.vxorps(e.xmm2, i.src1, e.GetXmmConstPtr(XMMSwapWordMask));
      }
      e.vpand(e.xmm2, e.GetXmmConstPtr(XMMPermuteByteMask));
      Xmm src2_shuf = e.xmm0;
      if (i.src2.value->IsConstantZero()) {
        e.vpxor(src2_shuf, src2_shuf);
      } else if (i.src2.is_constant) {
        e.LoadConstantXmm(src2_shuf, i.src2.constant());
        e.vpshufb(src2_shuf, src2_shuf, e.xmm2);
      } else {
        e.vpshufb(src2_shuf, i.src2, e.xmm2);
      }
      Xmm src3_shuf = e.xmm1;
      if (i.src3.value->IsConstantZero()) {
        e.vpxor(src3_shuf, src3_shuf);
      } else if (i.src3.is_constant) {
        e.LoadConstantXmm(src3_shuf, i.src3.constant());
        e.vpshufb(src3_shuf, src3_shuf, e.xmm2);
      } else {
        e.vpshufb(src3_shuf, i.src3, e.xmm2);
      }
      // Build a mask with values in src2 having 0 and values in src3 having 1.
      e.vpcmpgtb(i.dest, e.xmm2, e.GetXmmConstPtr(XMMPermuteControl15));
      e.vpblendvb(i.dest, src2_shuf, src3_shuf, i.dest);
    }
  }

  static void EmitByInt16(X64Emitter& e, const EmitArgType& i) {
    // src1 is an array of indices corresponding to positions within src2 and
    // src3.
    assert_true(i.src1.is_constant);
    vec128_t perm = (i.src1.constant() & vec128s(0xF)) ^ vec128s(0x1);
    vec128_t perm_ctrl = vec128b(0);
    for (int i = 0; i < 8; i++) {
      perm_ctrl.i16[i] = perm.i16[i] > 7 ? -1 : 0;

      auto v = uint8_t(perm.u16[i]);
      perm.u8[i * 2] = v * 2;
      perm.u8[i * 2 + 1] = v * 2 + 1;
    }
    e.LoadConstantXmm(e.xmm0, perm);

    if (i.src2.is_constant) {
      e.LoadConstantXmm(e.xmm1, i.src2.constant());
    } else {
      e.vmovdqa(e.xmm1, i.src2);
    }
    if (i.src3.is_constant) {
      e.LoadConstantXmm(e.xmm2, i.src3.constant());
    } else {
      e.vmovdqa(e.xmm2, i.src3);
    }

    e.vpshufb(e.xmm1, e.xmm1, e.xmm0);
    e.vpshufb(e.xmm2, e.xmm2, e.xmm0);

    uint8_t mask = 0;
    for (int i = 0; i < 8; i++) {
      if (perm_ctrl.i16[i] == 0) {
        mask |= 1 << (7 - i);
      }
    }
    e.vpblendw(i.dest, e.xmm1, e.xmm2, mask);
  }

  static void EmitByInt32(X64Emitter& e, const EmitArgType& i) {
    assert_always();
  }

  static void Emit(X64Emitter& e, const EmitArgType& i) {
    switch (i.instr->flags) {
      case INT8_TYPE:
        EmitByInt8(e, i);
        break;
      case INT16_TYPE:
        EmitByInt16(e, i);
        break;
      case INT32_TYPE:
        EmitByInt32(e, i);
        break;
      default:
        assert_unhandled_case(i.instr->flags);
        return;
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_PERMUTE, PERMUTE_I32, PERMUTE_V128);

// ============================================================================
// OPCODE_SWIZZLE
// ============================================================================
struct SWIZZLE
    : Sequence<SWIZZLE, I<OPCODE_SWIZZLE, V128Op, V128Op, OffsetOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto element_type = i.instr->flags;
    if (element_type == INT8_TYPE) {
      assert_always();
    } else if (element_type == INT16_TYPE) {
      assert_always();
    } else if (element_type == INT32_TYPE || element_type == FLOAT32_TYPE) {
      uint8_t swizzle_mask = static_cast<uint8_t>(i.src2.value);
      Xmm src1;
      if (i.src1.is_constant) {
        src1 = e.xmm0;
        e.LoadConstantXmm(src1, i.src1.constant());
      } else {
        src1 = i.src1;
      }
      e.vpshufd(i.dest, src1, swizzle_mask);
    } else if (element_type == INT64_TYPE || element_type == FLOAT64_TYPE) {
      assert_always();
    } else {
      assert_always();
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SWIZZLE, SWIZZLE);

// ============================================================================
// OPCODE_PACK
// ============================================================================
struct PACK : Sequence<PACK, I<OPCODE_PACK, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    switch (i.instr->flags & PACK_TYPE_MODE) {
      case PACK_TYPE_D3DCOLOR:
        EmitD3DCOLOR(e, i);
        break;
      case PACK_TYPE_FLOAT16_2:
        EmitFLOAT16_2(e, i);
        break;
      case PACK_TYPE_FLOAT16_4:
        EmitFLOAT16_4(e, i);
        break;
      case PACK_TYPE_SHORT_2:
        EmitSHORT_2(e, i);
        break;
      case PACK_TYPE_SHORT_4:
        EmitSHORT_4(e, i);
        break;
      case PACK_TYPE_UINT_2101010:
        EmitUINT_2101010(e, i);
        break;
      case PACK_TYPE_ULONG_4202020:
        EmitULONG_4202020(e, i);
        break;
      case PACK_TYPE_8_IN_16:
        Emit8_IN_16(e, i, i.instr->flags);
        break;
      case PACK_TYPE_16_IN_32:
        Emit16_IN_32(e, i, i.instr->flags);
        break;
      default:
        assert_unhandled_case(i.instr->flags);
        break;
    }
  }
  static void EmitD3DCOLOR(X64Emitter& e, const EmitArgType& i) {
    assert_true(i.src2.value->IsConstantZero());
    Xmm src;
    if (i.src1.is_constant) {
      src = i.dest;
      e.LoadConstantXmm(src, i.src1.constant());
    } else {
      src = i.src1;
    }
    // Saturate to [3,3....] so that only values between 3...[00] and 3...[FF]
    // are valid - max before min to pack NaN as zero (Red Dead Redemption is
    // heavily affected by the order - packs 0xFFFFFFFF in matrix code to get 0
    // constant).
    e.vmaxps(i.dest, src, e.GetXmmConstPtr(XMM3333));
    e.vminps(i.dest, i.dest, e.GetXmmConstPtr(XMMPackD3DCOLORSat));
    // Extract bytes.
    // RGBA (XYZW) -> ARGB (WXYZ)
    // w = ((src1.uw & 0xFF) << 24) | ((src1.ux & 0xFF) << 16) |
    //     ((src1.uy & 0xFF) << 8) | (src1.uz & 0xFF)
    e.vpshufb(i.dest, i.dest, e.GetXmmConstPtr(XMMPackD3DCOLOR));
  }
  static __m128i EmulateFLOAT16_2(void*, __m128 src1) {
    alignas(16) float a[4];
    alignas(16) uint16_t b[8];
    _mm_store_ps(a, src1);
    std::memset(b, 0, sizeof(b));

    for (int i = 0; i < 2; i++) {
      b[7 - i] = half_float::detail::float2half<std::round_toward_zero>(a[i]);
    }

    return _mm_load_si128(reinterpret_cast<__m128i*>(b));
  }
  static void EmitFLOAT16_2(X64Emitter& e, const EmitArgType& i) {
    assert_true(i.src2.value->IsConstantZero());
    // http://blogs.msdn.com/b/chuckw/archive/2012/09/11/directxmath-f16c-and-fma.aspx
    // dest = [(src1.x | src1.y), 0, 0, 0]

    Xmm src;
    if (e.IsFeatureEnabled(kX64EmitF16C)) {
      if (i.src1.is_constant) {
        src = i.dest;
        e.LoadConstantXmm(src, i.src1.constant());
      } else {
        src = i.src1;
      }
      // 0|0|0|0|W|Z|Y|X
      e.vcvtps2ph(i.dest, src, 0b00000011);
      // Shuffle to X|Y|0|0|0|0|0|0
      e.vpshufb(i.dest, i.dest, e.GetXmmConstPtr(XMMPackFLOAT16_2));
    } else {
      if (i.src1.is_constant) {
        src = e.xmm0;
        e.LoadConstantXmm(src, i.src1.constant());
      } else {
        src = i.src1;
      }
      e.lea(e.GetNativeParam(0), e.StashXmm(0, src));
      e.CallNativeSafe(reinterpret_cast<void*>(EmulateFLOAT16_2));
      e.vmovaps(i.dest, e.xmm0);
    }
  }
  static __m128i EmulateFLOAT16_4(void*, __m128 src1) {
    alignas(16) float a[4];
    alignas(16) uint16_t b[8];
    _mm_store_ps(a, src1);
    std::memset(b, 0, sizeof(b));

    for (int i = 0; i < 4; i++) {
      b[7 - (i ^ 2)] =
          half_float::detail::float2half<std::round_toward_zero>(a[i]);
    }

    return _mm_load_si128(reinterpret_cast<__m128i*>(b));
  }
  static void EmitFLOAT16_4(X64Emitter& e, const EmitArgType& i) {
    assert_true(i.src2.value->IsConstantZero());
    // dest = [(src1.z | src1.w), (src1.x | src1.y), 0, 0]

    Xmm src;
    if (e.IsFeatureEnabled(kX64EmitF16C)) {
      if (i.src1.is_constant) {
        src = i.dest;
        e.LoadConstantXmm(src, i.src1.constant());
      } else {
        src = i.src1;
      }
      // 0|0|0|0|W|Z|Y|X
      e.vcvtps2ph(i.dest, src, 0b00000011);
      // Shuffle to Z|W|X|Y|0|0|0|0
      e.vpshufb(i.dest, i.dest, e.GetXmmConstPtr(XMMPackFLOAT16_4));
    } else {
      if (i.src1.is_constant) {
        src = e.xmm0;
        e.LoadConstantXmm(src, i.src1.constant());
      } else {
        src = i.src1;
      }
      e.lea(e.GetNativeParam(0), e.StashXmm(0, src));
      e.CallNativeSafe(reinterpret_cast<void*>(EmulateFLOAT16_4));
      e.vmovaps(i.dest, e.xmm0);
    }
  }
  static void EmitSHORT_2(X64Emitter& e, const EmitArgType& i) {
    assert_true(i.src2.value->IsConstantZero());
    Xmm src;
    if (i.src1.is_constant) {
      src = i.dest;
      e.LoadConstantXmm(src, i.src1.constant());
    } else {
      src = i.src1;
    }
    // Saturate.
    e.vmaxps(i.dest, src, e.GetXmmConstPtr(XMMPackSHORT_Min));
    e.vminps(i.dest, i.dest, e.GetXmmConstPtr(XMMPackSHORT_Max));
    // Pack.
    e.vpshufb(i.dest, i.dest, e.GetXmmConstPtr(XMMPackSHORT_2));
  }
  static void EmitSHORT_4(X64Emitter& e, const EmitArgType& i) {
    assert_true(i.src2.value->IsConstantZero());
    Xmm src;
    if (i.src1.is_constant) {
      src = i.dest;
      e.LoadConstantXmm(src, i.src1.constant());
    } else {
      src = i.src1;
    }
    // Saturate.
    e.vmaxps(i.dest, src, e.GetXmmConstPtr(XMMPackSHORT_Min));
    e.vminps(i.dest, i.dest, e.GetXmmConstPtr(XMMPackSHORT_Max));
    // Pack.
    e.vpshufb(i.dest, i.dest, e.GetXmmConstPtr(XMMPackSHORT_4));
  }
  static void EmitUINT_2101010(X64Emitter& e, const EmitArgType& i) {
    // https://www.opengl.org/registry/specs/ARB/vertex_type_2_10_10_10_rev.txt
    // XYZ are 10 bits, signed and saturated.
    // W is 2 bits, unsigned and saturated.
    Xmm src;
    if (i.src1.is_constant) {
      src = i.dest;
      e.LoadConstantXmm(src, i.src1.constant());
    } else {
      src = i.src1;
    }
    // Saturate.
    e.vmaxps(i.dest, src, e.GetXmmConstPtr(XMMPackUINT_2101010_MinUnpacked));
    e.vminps(i.dest, i.dest, e.GetXmmConstPtr(XMMPackUINT_2101010_MaxUnpacked));
    // Remove the unneeded bits of the floats.
    e.vpand(i.dest, e.GetXmmConstPtr(XMMPackUINT_2101010_MaskUnpacked));
    if (e.IsFeatureEnabled(kX64EmitAVX2)) {
      // Shift the components up.
      e.vpsllvd(i.dest, i.dest, e.GetXmmConstPtr(XMMPackUINT_2101010_Shift));
    } else {
      // Duplicate all the components into bits 10-19.
      e.vpslld(e.xmm0, i.dest, 10);
      e.vpor(i.dest, e.xmm0);
      // Duplicate all the components into bits 20-39
      // (so alpha will be in 30-31).
      e.vpslld(e.xmm0, i.dest, 20);
      e.vpor(i.dest, e.xmm0);
      // Leave only the needed components.
      e.vpand(i.dest, e.GetXmmConstPtr(XMMPackUINT_2101010_MaskPacked));
    }
    // Combine the components.
    e.vshufps(e.xmm0, i.dest, i.dest, _MM_SHUFFLE(2, 3, 0, 1));
    e.vorps(i.dest, e.xmm0);
    e.vshufps(e.xmm0, i.dest, i.dest, _MM_SHUFFLE(1, 0, 3, 2));
    e.vorps(i.dest, e.xmm0);
  }
  static void EmitULONG_4202020(X64Emitter& e, const EmitArgType& i) {
    // XYZ are 20 bits, signed and saturated.
    // W is 4 bits, unsigned and saturated.
    Xmm src;
    if (i.src1.is_constant) {
      src = i.dest;
      e.LoadConstantXmm(src, i.src1.constant());
    } else {
      src = i.src1;
    }
    // Saturate.
    e.vmaxps(i.dest, src, e.GetXmmConstPtr(XMMPackULONG_4202020_MinUnpacked));
    e.vminps(i.dest, i.dest,
             e.GetXmmConstPtr(XMMPackULONG_4202020_MaxUnpacked));
    // Remove the unneeded bits of the floats (so excess nibbles will also be
    // cleared).
    e.vpand(i.dest, e.GetXmmConstPtr(XMMPackULONG_4202020_MaskUnpacked));
    // Store Y and W shifted left by 4 so vpshufb can be used with them.
    e.vpslld(e.xmm0, i.dest, 4);
    // Place XZ where they're supposed to be.
    e.vpshufb(i.dest, i.dest, e.GetXmmConstPtr(XMMPackULONG_4202020_PermuteXZ));
    // Place YW.
    e.vpshufb(e.xmm0, e.xmm0, e.GetXmmConstPtr(XMMPackULONG_4202020_PermuteYW));
    // Merge XZ and YW.
    e.vorps(i.dest, e.xmm0);
  }
  static __m128i EmulatePack8_IN_16_UN_UN_SAT(void*, __m128i src1,
                                              __m128i src2) {
    alignas(16) uint16_t a[8];
    alignas(16) uint16_t b[8];
    alignas(16) uint8_t c[16];
    _mm_store_si128(reinterpret_cast<__m128i*>(a), src1);
    _mm_store_si128(reinterpret_cast<__m128i*>(b), src2);
    for (int i = 0; i < 8; ++i) {
      c[i] = uint8_t(std::max(uint16_t(0), std::min(uint16_t(255), a[i])));
      c[i + 8] = uint8_t(std::max(uint16_t(0), std::min(uint16_t(255), b[i])));
    }
    return _mm_load_si128(reinterpret_cast<__m128i*>(c));
  }
  static __m128i EmulatePack8_IN_16_UN_UN(void*, __m128i src1, __m128i src2) {
    alignas(16) uint8_t a[16];
    alignas(16) uint8_t b[16];
    alignas(16) uint8_t c[16];
    _mm_store_si128(reinterpret_cast<__m128i*>(a), src1);
    _mm_store_si128(reinterpret_cast<__m128i*>(b), src2);
    for (int i = 0; i < 8; ++i) {
      c[i] = a[i * 2];
      c[i + 8] = b[i * 2];
    }
    return _mm_load_si128(reinterpret_cast<__m128i*>(c));
  }
  static void Emit8_IN_16(X64Emitter& e, const EmitArgType& i, uint32_t flags) {
    // TODO(benvanik): handle src2 (or src1) being constant zero
    if (IsPackInUnsigned(flags)) {
      if (IsPackOutUnsigned(flags)) {
        if (IsPackOutSaturate(flags)) {
          // unsigned -> unsigned + saturate
          if (i.src2.is_constant) {
            e.LoadConstantXmm(e.xmm0, i.src2.constant());
            e.lea(e.GetNativeParam(1), e.StashXmm(1, e.xmm0));
          } else {
            e.lea(e.GetNativeParam(1), e.StashXmm(1, i.src2));
          }
          e.lea(e.GetNativeParam(0), e.StashXmm(0, i.src1));
          e.CallNativeSafe(
              reinterpret_cast<void*>(EmulatePack8_IN_16_UN_UN_SAT));
          e.vmovaps(i.dest, e.xmm0);
          e.vpshufb(i.dest, i.dest, e.GetXmmConstPtr(XMMByteOrderMask));
        } else {
          // unsigned -> unsigned
          e.lea(e.GetNativeParam(1), e.StashXmm(1, i.src2));
          e.lea(e.GetNativeParam(0), e.StashXmm(0, i.src1));
          e.CallNativeSafe(reinterpret_cast<void*>(EmulatePack8_IN_16_UN_UN));
          e.vmovaps(i.dest, e.xmm0);
          e.vpshufb(i.dest, i.dest, e.GetXmmConstPtr(XMMByteOrderMask));
        }
      } else {
        if (IsPackOutSaturate(flags)) {
          // unsigned -> signed + saturate
          assert_always();
        } else {
          // unsigned -> signed
          assert_always();
        }
      }
    } else {
      if (IsPackOutUnsigned(flags)) {
        if (IsPackOutSaturate(flags)) {
          // signed -> unsigned + saturate
          // PACKUSWB / SaturateSignedWordToUnsignedByte
          Xbyak::Xmm src2 = i.src2.is_constant ? e.xmm0 : i.src2;
          if (i.src2.is_constant) {
            e.LoadConstantXmm(src2, i.src2.constant());
          }

          e.vpackuswb(i.dest, i.src1, src2);
          e.vpshufb(i.dest, i.dest, e.GetXmmConstPtr(XMMByteOrderMask));
        } else {
          // signed -> unsigned
          assert_always();
        }
      } else {
        if (IsPackOutSaturate(flags)) {
          // signed -> signed + saturate
          // PACKSSWB / SaturateSignedWordToSignedByte
          e.vpacksswb(i.dest, i.src1, i.src2);
          e.vpshufb(i.dest, i.dest, e.GetXmmConstPtr(XMMByteOrderMask));
        } else {
          // signed -> signed
          assert_always();
        }
      }
    }
  }
  // Pack 2 32-bit vectors into a 16-bit vector.
  static void Emit16_IN_32(X64Emitter& e, const EmitArgType& i,
                           uint32_t flags) {
    // TODO(benvanik): handle src2 (or src1) being constant zero
    if (IsPackInUnsigned(flags)) {
      if (IsPackOutUnsigned(flags)) {
        if (IsPackOutSaturate(flags)) {
          // unsigned -> unsigned + saturate
          // Construct a saturation max value
          e.mov(e.eax, 0xFFFFu);
          e.vmovd(e.xmm0, e.eax);
          e.vpshufd(e.xmm0, e.xmm0, 0b00000000);

          if (!i.src1.is_constant) {
            e.vpminud(e.xmm1, i.src1, e.xmm0);  // Saturate src1
            e.vpshuflw(e.xmm1, e.xmm1, 0b00100010);
            e.vpshufhw(e.xmm1, e.xmm1, 0b00100010);
            e.vpshufd(e.xmm1, e.xmm1, 0b00001000);
          } else {
            // TODO(DrChat): Non-zero constants
            assert_true(i.src1.constant().u64[0] == 0 &&
                        i.src1.constant().u64[1] == 0);
            e.vpxor(e.xmm1, e.xmm1);
          }

          if (!i.src2.is_constant) {
            e.vpminud(i.dest, i.src2, e.xmm0);  // Saturate src2
            e.vpshuflw(i.dest, i.dest, 0b00100010);
            e.vpshufhw(i.dest, i.dest, 0b00100010);
            e.vpshufd(i.dest, i.dest, 0b10000000);
          } else {
            // TODO(DrChat): Non-zero constants
            assert_true(i.src2.constant().u64[0] == 0 &&
                        i.src2.constant().u64[1] == 0);
            e.vpxor(i.dest, i.dest);
          }

          e.vpblendw(i.dest, i.dest, e.xmm1, 0b00001111);
        } else {
          // unsigned -> unsigned
          e.vmovaps(e.xmm0, i.src1);
          e.vpshuflw(e.xmm0, e.xmm0, 0b00100010);
          e.vpshufhw(e.xmm0, e.xmm0, 0b00100010);
          e.vpshufd(e.xmm0, e.xmm0, 0b00001000);

          e.vmovaps(i.dest, i.src2);
          e.vpshuflw(i.dest, i.dest, 0b00100010);
          e.vpshufhw(i.dest, i.dest, 0b00100010);
          e.vpshufd(i.dest, i.dest, 0b10000000);

          e.vpblendw(i.dest, i.dest, e.xmm0, 0b00001111);
        }
      } else {
        if (IsPackOutSaturate(flags)) {
          // unsigned -> signed + saturate
          assert_always();
        } else {
          // unsigned -> signed
          assert_always();
        }
      }
    } else {
      if (IsPackOutUnsigned(flags)) {
        if (IsPackOutSaturate(flags)) {
          // signed -> unsigned + saturate
          // PACKUSDW
          // TMP[15:0] <- (DEST[31:0] < 0) ? 0 : DEST[15:0];
          // DEST[15:0] <- (DEST[31:0] > FFFFH) ? FFFFH : TMP[15:0];
          e.vpackusdw(i.dest, i.src1, i.src2);
          e.vpshuflw(i.dest, i.dest, 0b10110001);
          e.vpshufhw(i.dest, i.dest, 0b10110001);
        } else {
          // signed -> unsigned
          assert_always();
        }
      } else {
        if (IsPackOutSaturate(flags)) {
          // signed -> signed + saturate
          // PACKSSDW / SaturateSignedDwordToSignedWord
          Xmm src2;
          if (!i.src2.is_constant) {
            src2 = i.src2;
          } else {
            assert_false(i.src1 == e.xmm0);
            src2 = e.xmm0;
            e.LoadConstantXmm(src2, i.src2.constant());
          }
          e.vpackssdw(i.dest, i.src1, src2);
          e.vpshuflw(i.dest, i.dest, 0b10110001);
          e.vpshufhw(i.dest, i.dest, 0b10110001);
        } else {
          // signed -> signed
          assert_always();
        }
      }
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_PACK, PACK);

// ============================================================================
// OPCODE_UNPACK
// ============================================================================
struct UNPACK : Sequence<UNPACK, I<OPCODE_UNPACK, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    switch (i.instr->flags & PACK_TYPE_MODE) {
      case PACK_TYPE_D3DCOLOR:
        EmitD3DCOLOR(e, i);
        break;
      case PACK_TYPE_FLOAT16_2:
        EmitFLOAT16_2(e, i);
        break;
      case PACK_TYPE_FLOAT16_4:
        EmitFLOAT16_4(e, i);
        break;
      case PACK_TYPE_SHORT_2:
        EmitSHORT_2(e, i);
        break;
      case PACK_TYPE_SHORT_4:
        EmitSHORT_4(e, i);
        break;
      case PACK_TYPE_UINT_2101010:
        EmitUINT_2101010(e, i);
        break;
      case PACK_TYPE_ULONG_4202020:
        EmitULONG_4202020(e, i);
        break;
      case PACK_TYPE_8_IN_16:
        Emit8_IN_16(e, i, i.instr->flags);
        break;
      case PACK_TYPE_16_IN_32:
        Emit16_IN_32(e, i, i.instr->flags);
        break;
      default:
        assert_unhandled_case(i.instr->flags);
        break;
    }
  }
  static void EmitD3DCOLOR(X64Emitter& e, const EmitArgType& i) {
    // ARGB (WXYZ) -> RGBA (XYZW)
    Xmm src;
    if (i.src1.is_constant) {
      if (i.src1.value->IsConstantZero()) {
        e.vmovaps(i.dest, e.GetXmmConstPtr(XMMOne));
        return;
      }
      src = i.dest;
      e.LoadConstantXmm(src, i.src1.constant());
    } else {
      src = i.src1;
    }
    // src = ZZYYXXWW
    // Unpack to 000000ZZ,000000YY,000000XX,000000WW
    e.vpshufb(i.dest, src, e.GetXmmConstPtr(XMMUnpackD3DCOLOR));
    // Add 1.0f to each.
    e.vpor(i.dest, e.GetXmmConstPtr(XMMOne));
    // To convert to 0 to 1, games multiply by 0x47008081 and add 0xC7008081.
  }
  static __m128 EmulateFLOAT16_2(void*, __m128i src1) {
    alignas(16) uint16_t a[8];
    alignas(16) float b[4];
    _mm_store_si128(reinterpret_cast<__m128i*>(a), src1);

    for (int i = 0; i < 2; i++) {
      b[i] = half_float::detail::half2float(a[VEC128_W(6 + i)]);
    }

    // Constants, or something
    b[2] = 0.f;
    b[3] = 1.f;

    return _mm_load_ps(b);
  }
  static void EmitFLOAT16_2(X64Emitter& e, const EmitArgType& i) {
    // 1 bit sign, 5 bit exponent, 10 bit mantissa
    // D3D10 half float format
    // TODO(benvanik):
    // http://blogs.msdn.com/b/chuckw/archive/2012/09/11/directxmath-f16c-and-fma.aspx
    // Use _mm_cvtph_ps -- requires very modern processors (SSE5+)
    // Unpacking half floats:
    // http://fgiesen.wordpress.com/2012/03/28/half-to-float-done-quic/
    // Packing half floats: https://gist.github.com/rygorous/2156668
    // Load source, move from tight pack of X16Y16.... to X16...Y16...
    // Also zero out the high end.
    // TODO(benvanik): special case constant unpacks that just get 0/1/etc.

    Xmm src;
    if (e.IsFeatureEnabled(kX64EmitF16C)) {
      if (i.src1.is_constant) {
        src = i.dest;
        e.LoadConstantXmm(src, i.src1.constant());
      } else {
        src = i.src1;
      }
      // sx = src.iw >> 16;
      // sy = src.iw & 0xFFFF;
      // dest = { XMConvertHalfToFloat(sx),
      //          XMConvertHalfToFloat(sy),
      //          0.0,
      //          1.0 };
      // Shuffle to 0|0|0|0|0|0|Y|X
      e.vpshufb(i.dest, src, e.GetXmmConstPtr(XMMUnpackFLOAT16_2));
      e.vcvtph2ps(i.dest, i.dest);
      e.vpshufd(i.dest, i.dest, 0b10100100);
      e.vpor(i.dest, e.GetXmmConstPtr(XMM0001));
    } else {
      if (i.src1.is_constant) {
        src = e.xmm0;
        e.LoadConstantXmm(src, i.src1.constant());
      } else {
        src = i.src1;
      }
      e.lea(e.GetNativeParam(0), e.StashXmm(0, src));
      e.CallNativeSafe(reinterpret_cast<void*>(EmulateFLOAT16_2));
      e.vmovaps(i.dest, e.xmm0);
    }
  }
  static __m128 EmulateFLOAT16_4(void*, __m128i src1) {
    alignas(16) uint16_t a[8];
    alignas(16) float b[4];
    _mm_store_si128(reinterpret_cast<__m128i*>(a), src1);

    for (int i = 0; i < 4; i++) {
      b[i] = half_float::detail::half2float(a[VEC128_W(4 + i)]);
    }

    return _mm_load_ps(b);
  }
  static void EmitFLOAT16_4(X64Emitter& e, const EmitArgType& i) {
    // src = [(dest.x | dest.y), (dest.z | dest.w), 0, 0]
    Xmm src;
    if (e.IsFeatureEnabled(kX64EmitF16C)) {
      if (i.src1.is_constant) {
        src = i.dest;
        e.LoadConstantXmm(src, i.src1.constant());
      } else {
        src = i.src1;
      }
      // Shuffle to 0|0|0|0|W|Z|Y|X
      e.vpshufb(i.dest, src, e.GetXmmConstPtr(XMMUnpackFLOAT16_4));
      e.vcvtph2ps(i.dest, i.dest);
    } else {
      if (i.src1.is_constant) {
        src = e.xmm0;
        e.LoadConstantXmm(src, i.src1.constant());
      } else {
        src = i.src1;
      }
      e.lea(e.GetNativeParam(0), e.StashXmm(0, src));
      e.CallNativeSafe(reinterpret_cast<void*>(EmulateFLOAT16_4));
      e.vmovaps(i.dest, e.xmm0);
    }
  }
  static void EmitSHORT_2(X64Emitter& e, const EmitArgType& i) {
    // (VD.x) = 3.0 + (VB.x>>16)*2^-22
    // (VD.y) = 3.0 + (VB.x)*2^-22
    // (VD.z) = 0.0
    // (VD.w) = 1.0 (games splat W after unpacking to get vectors of 1.0f)
    // src is (xx,xx,xx,VALUE)
    Xmm src;
    if (i.src1.is_constant) {
      if (i.src1.value->IsConstantZero()) {
        e.vmovdqa(i.dest, e.GetXmmConstPtr(XMM3301));
        return;
      }
      // TODO(benvanik): check other common constants/perform shuffle/or here.
      src = i.dest;
      e.LoadConstantXmm(src, i.src1.constant());
    } else {
      src = i.src1;
    }
    // Shuffle bytes.
    e.vpshufb(i.dest, src, e.GetXmmConstPtr(XMMUnpackSHORT_2));
    // If negative, make smaller than 3 - sign extend before adding.
    e.vpslld(i.dest, 16);
    e.vpsrad(i.dest, 16);
    // Add 3,3,0,1.
    e.vpaddd(i.dest, e.GetXmmConstPtr(XMM3301));
    // Return quiet NaNs in case of negative overflow.
    e.vcmpeqps(e.xmm0, i.dest, e.GetXmmConstPtr(XMMUnpackSHORT_Overflow));
    e.vblendvps(i.dest, i.dest, e.GetXmmConstPtr(XMMQNaN), e.xmm0);
  }
  static void EmitSHORT_4(X64Emitter& e, const EmitArgType& i) {
    // (VD.x) = 3.0 + (VB.x>>16)*2^-22
    // (VD.y) = 3.0 + (VB.x)*2^-22
    // (VD.z) = 3.0 + (VB.y>>16)*2^-22
    // (VD.w) = 3.0 + (VB.y)*2^-22
    // src is (xx,xx,VALUE,VALUE)
    Xmm src;
    if (i.src1.is_constant) {
      if (i.src1.value->IsConstantZero()) {
        e.vmovdqa(i.dest, e.GetXmmConstPtr(XMM3333));
        return;
      }
      // TODO(benvanik): check other common constants/perform shuffle/or here.
      src = i.dest;
      e.LoadConstantXmm(src, i.src1.constant());
    } else {
      src = i.src1;
    }
    // Shuffle bytes.
    e.vpshufb(i.dest, src, e.GetXmmConstPtr(XMMUnpackSHORT_4));
    // If negative, make smaller than 3 - sign extend before adding.
    e.vpslld(i.dest, 16);
    e.vpsrad(i.dest, 16);
    // Add 3,3,3,3.
    e.vpaddd(i.dest, e.GetXmmConstPtr(XMM3333));
    // Return quiet NaNs in case of negative overflow.
    e.vcmpeqps(e.xmm0, i.dest, e.GetXmmConstPtr(XMMUnpackSHORT_Overflow));
    e.vblendvps(i.dest, i.dest, e.GetXmmConstPtr(XMMQNaN), e.xmm0);
  }
  static void EmitUINT_2101010(X64Emitter& e, const EmitArgType& i) {
    Xmm src;
    if (i.src1.is_constant) {
      if (i.src1.value->IsConstantZero()) {
        e.vmovdqa(i.dest, e.GetXmmConstPtr(XMM3331));
        return;
      }
      src = i.dest;
      e.LoadConstantXmm(src, i.src1.constant());
    } else {
      src = i.src1;
    }
    // Splat W.
    e.vshufps(i.dest, src, src, _MM_SHUFFLE(3, 3, 3, 3));
    // Keep only the needed components.
    // Red in 0-9 now, green in 10-19, blue in 20-29, alpha in 30-31.
    e.vpand(i.dest, e.GetXmmConstPtr(XMMPackUINT_2101010_MaskPacked));
    if (e.IsFeatureEnabled(kX64EmitAVX2)) {
      // Shift the components down.
      e.vpsrlvd(i.dest, i.dest, e.GetXmmConstPtr(XMMPackUINT_2101010_Shift));
    } else {
      // Duplicate green in 0-9 and alpha in 20-21.
      e.vpsrld(e.xmm0, i.dest, 10);
      e.vpor(i.dest, e.xmm0);
      // Duplicate blue in 0-9 and alpha in 0-1.
      e.vpsrld(e.xmm0, i.dest, 20);
      e.vpor(i.dest, e.xmm0);
      // Remove higher duplicate components.
      e.vpand(i.dest, e.GetXmmConstPtr(XMMPackUINT_2101010_MaskUnpacked));
    }
    // If XYZ are negative, make smaller than 3 - sign extend XYZ before adding.
    // W is unsigned.
    e.vpslld(i.dest, 22);
    e.vpsrad(i.dest, 22);
    // Add 3,3,3,1.
    e.vpaddd(i.dest, e.GetXmmConstPtr(XMM3331));
    // Return quiet NaNs in case of negative overflow.
    e.vcmpeqps(e.xmm0, i.dest,
               e.GetXmmConstPtr(XMMUnpackUINT_2101010_Overflow));
    e.vblendvps(i.dest, i.dest, e.GetXmmConstPtr(XMMQNaN), e.xmm0);
    // To convert XYZ to -1 to 1, games multiply by 0x46004020 & sub 0x46C06030.
    // For W to 0 to 1, they multiply by and subtract 0x4A2AAAAB.
  }
  static void EmitULONG_4202020(X64Emitter& e, const EmitArgType& i) {
    Xmm src;
    if (i.src1.is_constant) {
      if (i.src1.value->IsConstantZero()) {
        e.vmovdqa(i.dest, e.GetXmmConstPtr(XMM3331));
        return;
      }
      src = i.dest;
      e.LoadConstantXmm(src, i.src1.constant());
    } else {
      src = i.src1;
    }
    // Extract pairs of nibbles to XZYW. XZ will have excess 4 upper bits, YW
    // will have excess 4 lower bits.
    e.vpshufb(i.dest, src, e.GetXmmConstPtr(XMMUnpackULONG_4202020_Permute));
    // Drop the excess nibble of YW.
    e.vpsrld(e.xmm0, i.dest, 4);
    // Merge XZ and YW now both starting at offset 0.
    e.vshufps(i.dest, i.dest, e.xmm0, _MM_SHUFFLE(3, 2, 1, 0));
    // Reorder as XYZW.
    e.vshufps(i.dest, i.dest, _MM_SHUFFLE(3, 1, 2, 0));
    // Drop the excess upper nibble in XZ and sign-extend XYZ.
    e.vpslld(i.dest, 12);
    e.vpsrad(i.dest, 12);
    // Add 3,3,3,1.
    e.vpaddd(i.dest, e.GetXmmConstPtr(XMM3331));
    // Return quiet NaNs in case of negative overflow.
    e.vcmpeqps(e.xmm0, i.dest,
               e.GetXmmConstPtr(XMMUnpackULONG_4202020_Overflow));
    e.vblendvps(i.dest, i.dest, e.GetXmmConstPtr(XMMQNaN), e.xmm0);
  }
  static void Emit8_IN_16(X64Emitter& e, const EmitArgType& i, uint32_t flags) {
    assert_false(IsPackOutSaturate(flags));
    Xmm src;
    if (i.src1.is_constant) {
      src = i.dest;
      e.LoadConstantXmm(src, i.src1.constant());
    } else {
      src = i.src1;
    }
    if (IsPackToLo(flags)) {
      // Unpack to LO.
      if (IsPackInUnsigned(flags)) {
        if (IsPackOutUnsigned(flags)) {
          // unsigned -> unsigned
          assert_always();
        } else {
          // unsigned -> signed
          assert_always();
        }
      } else {
        if (IsPackOutUnsigned(flags)) {
          // signed -> unsigned
          assert_always();
        } else {
          // signed -> signed
          e.vpshufb(i.dest, src, e.GetXmmConstPtr(XMMByteOrderMask));
          e.vpunpckhbw(i.dest, i.dest, i.dest);
          e.vpsraw(i.dest, 8);
        }
      }
    } else {
      // Unpack to HI.
      if (IsPackInUnsigned(flags)) {
        if (IsPackOutUnsigned(flags)) {
          // unsigned -> unsigned
          assert_always();
        } else {
          // unsigned -> signed
          assert_always();
        }
      } else {
        if (IsPackOutUnsigned(flags)) {
          // signed -> unsigned
          assert_always();
        } else {
          // signed -> signed
          e.vpshufb(i.dest, src, e.GetXmmConstPtr(XMMByteOrderMask));
          e.vpunpcklbw(i.dest, i.dest, i.dest);
          e.vpsraw(i.dest, 8);
        }
      }
    }
  }
  static void Emit16_IN_32(X64Emitter& e, const EmitArgType& i,
                           uint32_t flags) {
    assert_false(IsPackOutSaturate(flags));
    Xmm src;
    if (i.src1.is_constant) {
      src = i.dest;
      e.LoadConstantXmm(src, i.src1.constant());
    } else {
      src = i.src1;
    }
    if (IsPackToLo(flags)) {
      // Unpack to LO.
      if (IsPackInUnsigned(flags)) {
        if (IsPackOutUnsigned(flags)) {
          // unsigned -> unsigned
          assert_always();
        } else {
          // unsigned -> signed
          assert_always();
        }
      } else {
        if (IsPackOutUnsigned(flags)) {
          // signed -> unsigned
          assert_always();
        } else {
          // signed -> signed
          e.vpunpckhwd(i.dest, src, src);
          e.vpsrad(i.dest, 16);
        }
      }
    } else {
      // Unpack to HI.
      if (IsPackInUnsigned(flags)) {
        if (IsPackOutUnsigned(flags)) {
          // unsigned -> unsigned
          assert_always();
        } else {
          // unsigned -> signed
          assert_always();
        }
      } else {
        if (IsPackOutUnsigned(flags)) {
          // signed -> unsigned
          assert_always();
        } else {
          // signed -> signed
          e.vpunpcklwd(i.dest, src, src);
          e.vpsrad(i.dest, 16);
        }
      }
    }
    e.vpshufd(i.dest, i.dest, 0xB1);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_UNPACK, UNPACK);

}  // namespace x64
}  // namespace backend
}  // namespace cpu
}  // namespace xe