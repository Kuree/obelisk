//===- RandomSamplerZ3.cpp - Compile exact parameterized sampling plans ---===//

#include "../../runtime/lib/RandModelCount.h"
#include "obelisk/Runtime/RandomSamplerPlan.h"
#include "obelisk/Solver/ConstraintSolver.h"

#ifdef OBELISK_ENABLE_Z3
#include "RandomRealZ3.h"
#include <algorithm>
#include <functional>
#include <map>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include <z3.h>

namespace obelisk::solver {
namespace {
using random::BooleanCircuit;
using random::PlanOpcode;
using ID = BooleanCircuit::ID;

uint16_t read16(const uint8_t *p) {
  return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}

uint32_t read32(const uint8_t *p) {
  uint32_t value = 0;
  for (unsigned byte = 0; byte != 4; ++byte)
    value |= uint32_t(p[byte]) << (byte * 8);
  return value;
}

uint64_t read64(const uint8_t *p) {
  uint64_t value = 0;
  for (unsigned byte = 0; byte != 8; ++byte)
    value |= uint64_t(p[byte]) << (byte * 8);
  return value;
}

struct Bits {
  Z3_ast value;
  unsigned width;
  bool real = false;
  Z3_ast representation = nullptr;
  Z3_ast coordinate = nullptr;
  // A mixed active/frozen selection retains coordinates only on this guard.
  // Its other arm is IEEE state and must never decode a NaN/Inf placeholder.
  Z3_ast coordinateGuard = nullptr;
  Z3_ast coordinateState = nullptr;
  // Exact IEEE widening preserves the numeric value used by integral casts.
  // Retain its original payload without assigning a promoted NaN payload.
  Z3_ast numericPayload = nullptr;
  unsigned numericWidth = 0;
};

class BitVectorFormula {
public:
  BitVectorFormula() {
    // Numeric casts extract IEEE bits only after sanitizing nonfinite inputs,
    // so undefined NaN payloads cannot be observed. Z3's fpa2bv tactic reads
    // hi_fp_unspecified from the rewriter module parameters but does not
    // accept it as a tactic parameter; set it before any sampler context
    // exists so the unspecified completion branches stay deterministic and do
    // not become extra random inputs.
    static std::once_flag hardwareUnspecified;
    std::call_once(hardwareUnspecified, [] {
      Z3_global_param_set("rewriter.hi_fp_unspecified", "true");
    });
    Z3_config config = Z3_mk_config();
    context = Z3_mk_context(config);
    Z3_del_config(config);
    Z3_set_error_handler(context, [](Z3_context, Z3_error_code) {});
  }

  ~BitVectorFormula() { Z3_del_context(context); }

  BitVectorFormula(const BitVectorFormula &) = delete;
  BitVectorFormula &operator=(const BitVectorFormula &) = delete;

  Z3_ast constant(uint64_t value, unsigned width) {
    return Z3_mk_unsigned_int64(context, value, Z3_mk_bv_sort(context, width));
  }

  Z3_ast words(const uint64_t *value, unsigned width) {
    unsigned count = (width - 1) / 64 + 1;
    unsigned highWidth = width - (count - 1) * 64;
    Z3_ast result = constant(value[count - 1], highWidth);
    while (--count != 0)
      result = Z3_mk_concat(context, result, constant(value[count - 1], 64));
    return result;
  }

  Z3_ast resize(Bits input, unsigned width, bool signedValue = false) {
    if (input.real) {
      if (width != 32 && width != 64)
        return nullptr;
      return input.width == width
                 ? input.value
                 : Z3_mk_fpa_to_fp_float(context, rounding(), input.value,
                                         realSort(width));
    }
    if (input.width == width)
      return input.value;
    Z3_ast result =
        input.width > width ? Z3_mk_extract(context, width - 1, 0, input.value)
        : signedValue
            ? Z3_mk_sign_ext(context, width - input.width, input.value)
            : Z3_mk_zero_ext(context, width - input.width, input.value);
    auto count = popcounts.find(input.value);
    if (count != popcounts.end()) {
      unsigned needed = 32 - __builtin_clz(count->second.width);
      if (width >= needed && (!signedValue || input.width > needed))
        popcounts.emplace(result, count->second);
    }
    return result;
  }

  Z3_ast truth(Bits input) {
    if (input.real)
      return Z3_mk_not(context, Z3_mk_fpa_is_zero(context, input.value));
    return Z3_mk_not(context,
                     Z3_mk_eq(context, input.value, constant(0, input.width)));
  }

  Bits boolean(Z3_ast predicate, unsigned width = 1) {
    return {
        Z3_mk_ite(context, predicate, constant(1, width), constant(0, width)),
        width};
  }

  Z3_ast conjunction(Z3_ast a, Z3_ast b) {
    Z3_ast values[] = {a, b};
    return Z3_mk_and(context, 2, values);
  }

  Z3_ast disjunction(Z3_ast a, Z3_ast b) {
    Z3_ast values[] = {a, b};
    return Z3_mk_or(context, 2, values);
  }

  Bits unary(uint8_t opcode, Bits input, unsigned width, bool sign) {
    if (random::isRealInstruction(opcode))
      return realUnary(opcode, input, width, sign);
    if (input.real) {
      if (opcode == OBELISK_RT_RANDOM_LOGICAL_NOT_V1)
        return boolean(Z3_mk_not(context, truth(input)), width);
      if ((opcode == OBELISK_RT_RANDOM_POS_V1 ||
           opcode == OBELISK_RT_RANDOM_NEG_V1) &&
          (width == 32 || width == 64)) {
        Z3_ast value = resize(input, width);
        Z3_ast representation =
            input.width == width ? input.representation : nullptr;
        if (opcode == OBELISK_RT_RANDOM_NEG_V1)
          value = Z3_mk_fpa_neg(context, value);
        if (representation && opcode == OBELISK_RT_RANDOM_NEG_V1)
          representation =
              Z3_mk_bvxor(context, representation,
                          constant(uint64_t{1} << (width - 1), width));
        Bits result{value,
                    width,
                    true,
                    representation,
                    opcode == OBELISK_RT_RANDOM_POS_V1 && input.width == width
                        ? input.coordinate
                        : nullptr,
                    opcode == OBELISK_RT_RANDOM_POS_V1 && input.width == width
                        ? input.coordinateGuard
                        : nullptr,
                    opcode == OBELISK_RT_RANDOM_POS_V1 && input.width == width
                        ? input.coordinateState
                        : nullptr};
        if (input.width == width && input.numericPayload) {
          result.numericPayload = input.numericPayload;
          result.numericWidth = input.numericWidth;
          if (opcode == OBELISK_RT_RANDOM_NEG_V1)
            result.numericPayload =
                Z3_mk_bvxor(context, result.numericPayload,
                            constant(uint64_t{1} << (result.numericWidth - 1),
                                     result.numericWidth));
        }
        return result;
      }
      return {nullptr, width};
    }
    Z3_ast result = nullptr;
    switch (opcode) {
    case OBELISK_RT_RANDOM_CAST_V1:
    case OBELISK_RT_RANDOM_POS_V1:
      result = resize(input, width, sign);
      break;
    case OBELISK_RT_RANDOM_NEG_V1:
      result = Z3_mk_bvneg(context, resize(input, width, sign));
      break;
    case OBELISK_RT_RANDOM_BIT_NOT_V1:
      result = Z3_mk_bvnot(context, resize(input, width, sign));
      break;
    case OBELISK_RT_RANDOM_REDUCE_AND_V1:
    case OBELISK_RT_RANDOM_REDUCE_NAND_V1: {
      Z3_ast ones = Z3_mk_bvnot(context, constant(0, input.width));
      Z3_ast predicate = Z3_mk_eq(context, input.value, ones);
      if (opcode == OBELISK_RT_RANDOM_REDUCE_NAND_V1)
        predicate = Z3_mk_not(context, predicate);
      return boolean(predicate, width);
    }
    case OBELISK_RT_RANDOM_REDUCE_OR_V1:
      return boolean(truth(input), width);
    case OBELISK_RT_RANDOM_REDUCE_NOR_V1:
    case OBELISK_RT_RANDOM_LOGICAL_NOT_V1:
      return boolean(Z3_mk_not(context, truth(input)), width);
    case OBELISK_RT_RANDOM_REDUCE_XOR_V1:
    case OBELISK_RT_RANDOM_REDUCE_XNOR_V1: {
      Z3_ast parity = Z3_mk_false(context);
      for (unsigned bit = 0; bit != input.width; ++bit) {
        Bits selected{Z3_mk_extract(context, bit, bit, input.value), 1};
        parity = Z3_mk_xor(context, parity, truth(selected));
      }
      if (opcode == OBELISK_RT_RANDOM_REDUCE_XNOR_V1)
        parity = Z3_mk_not(context, parity);
      return boolean(parity, width);
    }
    case OBELISK_RT_RANDOM_COUNT_ONES_V1: {
      unsigned countWidth = std::min(width, 32u - __builtin_clz(input.width));
      // A shared prefix sum takes linear construction work. Its low-to-high
      // order also lets equal partial counts share the same cofactor after
      // binding state and sampling a prefix of the input bits. The count
      // precision is at most the 32-bit width of the count system functions.
      result = constant(0, countWidth);
      for (unsigned bit = 0; bit != input.width; ++bit) {
        Bits selected{Z3_mk_extract(context, bit, bit, input.value), 1};
        result = Z3_mk_bvadd(context, result, resize(selected, countWidth));
      }
      result = resize({result, countWidth}, width);
      break;
    }
    case OBELISK_RT_RANDOM_CLOG2_V1: {
      // $clog2 is at most the input width, so select among indices only as
      // wide as that bound and zero-extend once, as for $countones.
      unsigned indexWidth = std::min(width, 32u - __builtin_clz(input.width));
      result = constant(0, indexWidth);
      Z3_ast lowerNonzero = Z3_mk_false(context);
      for (unsigned bit = 0; bit != input.width; ++bit) {
        Z3_ast selected =
            truth({Z3_mk_extract(context, bit, bit, input.value), 1});
        Z3_ast atBit = Z3_mk_ite(context, lowerNonzero,
                                 constant(uint64_t(bit) + 1, indexWidth),
                                 constant(bit, indexWidth));
        result = Z3_mk_ite(context, selected, atBit, result);
        lowerNonzero = disjunction(lowerNonzero, selected);
      }
      result = resize({result, indexWidth}, width);
      break;
    }
    default:
      break;
    }
    if (opcode == OBELISK_RT_RANDOM_COUNT_ONES_V1 &&
        width >= 32u - __builtin_clz(input.width))
      popcounts.emplace(result, input);
    return {result, width};
  }

  /// Generate fixed sparse counts directly in linear time. Delegate general
  /// cardinality construction to Z3 rather than building a quadratic table
  /// in the host. Unate lowering preserves the prefix structure that a target
  /// sampler can simplify after binding the live count.
  Z3_ast countEqual(Bits input, unsigned count) {
    bool complement = count > input.width / 2;
    if (complement)
      count = input.width - count;
    if (count > 8) {
      auto [found, inserted] = cardinalityInputs.try_emplace(input.value);
      auto &args = found->second;
      if (inserted) {
        args.positive.reserve(input.width);
        args.negative.reserve(input.width);
        args.coefficients.assign(input.width, 1);
        for (unsigned bit = 0; bit != input.width; ++bit) {
          Z3_ast selected =
              truth({Z3_mk_extract(context, bit, bit, input.value), 1});
          args.positive.push_back(selected);
          args.negative.push_back(Z3_mk_not(context, selected));
        }
      }
      // Complementing caps the API's signed bound at UINT_MAX / 2.
      return Z3_mk_pbeq(context, input.width,
                        (complement ? args.negative : args.positive).data(),
                        args.coefficients.data(), static_cast<int>(count));
    }
    std::vector<Z3_ast> exact(count + 1, Z3_mk_false(context));
    exact[0] = Z3_mk_true(context);
    for (unsigned bit = 0; bit != input.width; ++bit) {
      Z3_ast selected =
          truth({Z3_mk_extract(context, bit, bit, input.value), 1});
      if (complement)
        selected = Z3_mk_not(context, selected);
      for (unsigned k = count; k != 0; --k)
        exact[k] = Z3_mk_ite(context, selected, exact[k - 1], exact[k]);
      exact[0] = conjunction(Z3_mk_not(context, selected), exact[0]);
    }
    return exact[count];
  }

  Bits binary(uint8_t opcode, Bits lhs, Bits rhs, unsigned width, bool sign) {
    if (lhs.real || rhs.real)
      return realBinary(opcode, lhs, rhs, width);
    unsigned commonWidth = std::max(lhs.width, rhs.width);
    Z3_ast a = resize(lhs, commonWidth, sign);
    Z3_ast b = resize(rhs, commonWidth, sign);
    if (opcode == OBELISK_RT_RANDOM_EQ_V1 ||
        opcode == OBELISK_RT_RANDOM_NE_V1) {
      for (auto pair : {std::make_pair(a, b), std::make_pair(b, a)}) {
        auto count = popcounts.find(pair.first);
        if (count == popcounts.end())
          continue;
        // Negative signed constants cannot equal a nonnegative count. Do not
        // apply the recurrence to a signed representation that can overflow.
        unsigned needed = 32 - __builtin_clz(count->second.width);
        if (sign && commonWidth <= needed)
          continue;
        uint64_t value;
        Z3_ast simplified = Z3_simplify(context, pair.second);
        Z3_ast match;
        if (Z3_get_ast_kind(context, simplified) == Z3_NUMERAL_AST &&
            Z3_get_numeral_uint64(context, simplified, &value)) {
          if (value > count->second.width)
            match = Z3_mk_false(context);
          else
            match = countEqual(count->second, static_cast<unsigned>(value));
        } else {
          // Reuse the complete sum for live and candidate targets. Building
          // a width-sized predicate for every possible count is quadratic.
          match = Z3_mk_eq(context, pair.first, pair.second);
        }
        if (opcode == OBELISK_RT_RANDOM_NE_V1)
          match = Z3_mk_not(context, match);
        return boolean(match, width);
      }
    }
    Z3_ast predicate = nullptr;
    switch (opcode) {
    case OBELISK_RT_RANDOM_EQ_V1:
      predicate = Z3_mk_eq(context, a, b);
      break;
    case OBELISK_RT_RANDOM_NE_V1:
      predicate = Z3_mk_not(context, Z3_mk_eq(context, a, b));
      break;
    case OBELISK_RT_RANDOM_GE_V1:
      predicate =
          sign ? Z3_mk_bvsge(context, a, b) : Z3_mk_bvuge(context, a, b);
      break;
    case OBELISK_RT_RANDOM_GT_V1:
      predicate =
          sign ? Z3_mk_bvsgt(context, a, b) : Z3_mk_bvugt(context, a, b);
      break;
    case OBELISK_RT_RANDOM_LE_V1:
      predicate =
          sign ? Z3_mk_bvsle(context, a, b) : Z3_mk_bvule(context, a, b);
      break;
    case OBELISK_RT_RANDOM_LT_V1:
      predicate =
          sign ? Z3_mk_bvslt(context, a, b) : Z3_mk_bvult(context, a, b);
      break;
    case OBELISK_RT_RANDOM_LOGICAL_AND_V1:
      predicate = conjunction(truth(lhs), truth(rhs));
      break;
    case OBELISK_RT_RANDOM_LOGICAL_OR_V1:
      predicate = disjunction(truth(lhs), truth(rhs));
      break;
    case OBELISK_RT_RANDOM_LOGICAL_IMPLIES_V1:
      predicate = Z3_mk_implies(context, truth(lhs), truth(rhs));
      break;
    case OBELISK_RT_RANDOM_LOGICAL_EQUIV_V1:
      predicate = Z3_mk_eq(context, truth(lhs), truth(rhs));
      break;
    default:
      break;
    }
    if (predicate)
      return boolean(predicate, width);
    a = resize(lhs, width, sign);
    b = resize(rhs, width, sign);
    Z3_ast result = nullptr;
    switch (opcode) {
    case OBELISK_RT_RANDOM_ADD_V1:
      result = Z3_mk_bvadd(context, a, b);
      break;
    case OBELISK_RT_RANDOM_SUB_V1:
      result = Z3_mk_bvsub(context, a, b);
      break;
    case OBELISK_RT_RANDOM_MUL_V1:
      result = Z3_mk_bvmul(context, a, b);
      break;
    case OBELISK_RT_RANDOM_BIT_AND_V1:
      result = Z3_mk_bvand(context, a, b);
      break;
    case OBELISK_RT_RANDOM_BIT_OR_V1:
      result = Z3_mk_bvor(context, a, b);
      break;
    case OBELISK_RT_RANDOM_BIT_XOR_V1:
      result = Z3_mk_bvxor(context, a, b);
      break;
    case OBELISK_RT_RANDOM_BIT_XNOR_V1:
      result = Z3_mk_bvnot(context, Z3_mk_bvxor(context, a, b));
      break;
    case OBELISK_RT_RANDOM_DIV_V1:
      result = sign ? Z3_mk_bvsdiv(context, a, b) : Z3_mk_bvudiv(context, a, b);
      break;
    case OBELISK_RT_RANDOM_MOD_V1:
      result = sign ? Z3_mk_bvsrem(context, a, b) : Z3_mk_bvurem(context, a, b);
      break;
    case OBELISK_RT_RANDOM_SHIFT_LEFT_V1:
    case OBELISK_RT_RANDOM_SHIFT_RIGHT_V1:
    case OBELISK_RT_RANDOM_SHIFT_RIGHT_ARITH_V1: {
      unsigned operationWidth = std::max(width, rhs.width);
      bool arithmetic = opcode == OBELISK_RT_RANDOM_SHIFT_RIGHT_ARITH_V1;
      a = resize({resize(lhs, width, sign), width}, operationWidth, arithmetic);
      b = resize(rhs, operationWidth);
      result = opcode == OBELISK_RT_RANDOM_SHIFT_LEFT_V1
                   ? Z3_mk_bvshl(context, a, b)
               : arithmetic ? Z3_mk_bvashr(context, a, b)
                            : Z3_mk_bvlshr(context, a, b);
      return {resize({result, operationWidth}, width), width};
    }
    case OBELISK_RT_RANDOM_POWER_V1: {
      result = constant(1, width);
      for (unsigned bit = 0; bit != rhs.width; ++bit) {
        Bits selected{Z3_mk_extract(context, bit, bit, rhs.value), 1};
        result = Z3_mk_ite(context, truth(selected),
                           Z3_mk_bvmul(context, result, a), result);
        if (bit + 1 != rhs.width)
          a = Z3_mk_bvmul(context, a, a);
      }
      break;
    }
    default:
      break;
    }
    return {result, width};
  }

  Z3_sort realSort(unsigned width) {
    return Z3_mk_fpa_sort(context, width == 32 ? 8 : 11, width == 32 ? 24 : 53);
  }

  Z3_ast rounding() { return Z3_mk_fpa_round_nearest_ties_to_even(context); }

  Z3_ast rankValid(Bits input) {
    bool shortReal = input.width == OBELISK_RT_REAL32_RANK_BITS;
    unsigned fraction = shortReal ? 23 : 52;
    unsigned exponent = shortReal ? 254 : 2046;
    Z3_ast maximum = Z3_mk_bvshl(
        context, constant((uint64_t{1} << (fraction + 1)) - 1, input.width),
        constant(exponent, input.width));
    return conjunction(
        Z3_mk_not(context,
                  coordinateOrder(input.value, Z3_mk_bvneg(context, maximum),
                                  input.width, false)),
        coordinateOrder(input.value, maximum, input.width, true));
  }

  Bits realUnary(uint8_t opcode, Bits input, unsigned width, bool sign) {
    bool format = width == 32 || width == 64;
    if (opcode == OBELISK_RT_RANDOM_REAL_IS_FINITE_V1) {
      if (!input.real || sign || width != 1)
        return {nullptr, width};
      return boolean(conjunction(
          Z3_mk_not(context, Z3_mk_fpa_is_nan(context, input.value)),
          Z3_mk_not(context, Z3_mk_fpa_is_infinite(context, input.value))));
    }
    if (opcode == OBELISK_RT_RANDOM_REAL_TO_INTEGER_V1 ||
        opcode == OBELISK_RT_RANDOM_REAL_TRUNCATE_INTEGER_V1) {
      if (!input.real || sign || !width)
        return {nullptr, width};
      hasReal = true;
      Z3_ast payload =
          input.representation ? input.representation : input.numericPayload;
      if (!payload) {
        // Numeric conversion observes no NaN payload. Sanitize nonfinite
        // arithmetic before obtaining IEEE bits, then use the same proven
        // bit-vector conversion as direct payloads. This avoids a wide signed
        // conversion and its range-completion witnesses.
        Z3_ast finite = conjunction(
            Z3_mk_not(context, Z3_mk_fpa_is_nan(context, input.value)),
            Z3_mk_not(context, Z3_mk_fpa_is_infinite(context, input.value)));
        Z3_ast safe =
            Z3_mk_ite(context, finite, input.value,
                      Z3_mk_fpa_zero(context, realSort(input.width), false));
        payload = Z3_mk_fpa_to_ieee_bv(context, safe);
        return {detail::realIntegerBits(
                    context, payload, input.width, width,
                    opcode == OBELISK_RT_RANDOM_REAL_TRUNCATE_INTEGER_V1),
                width};
      }
      return {detail::realIntegerBits(
                  context, payload,
                  input.representation ? input.width : input.numericWidth,
                  width, opcode == OBELISK_RT_RANDOM_REAL_TRUNCATE_INTEGER_V1),
              width};
    }
    if (opcode == OBELISK_RT_RANDOM_REAL_RANK_VALID_V1) {
      if (input.real || sign || width != 1 ||
          (input.width != OBELISK_RT_REAL32_RANK_BITS &&
           input.width != OBELISK_RT_REAL64_RANK_BITS))
        return {nullptr, width};
      return boolean(rankValid(input));
    }
    if (!format)
      return {nullptr, width};
    hasReal = true;
    if (opcode == OBELISK_RT_RANDOM_REAL_CAST_V1) {
      bool retainsCoordinate =
          input.real &&
          (input.width == width || (input.width == 32 && width == 64));
      Z3_ast value =
          input.real ? resize(input, width)
          : sign ? Z3_mk_fpa_to_fp_signed(context, rounding(), input.value,
                                          realSort(width))
                 : Z3_mk_fpa_to_fp_unsigned(context, rounding(), input.value,
                                            realSort(width));
      Bits result{
          value,
          width,
          true,
          input.real && input.width == width ? input.representation : nullptr,
          retainsCoordinate ? input.coordinate : nullptr,
          retainsCoordinate ? input.coordinateGuard : nullptr,
          retainsCoordinate && input.coordinateState
              ? resize({input.coordinateState, input.width, true}, width)
              : nullptr};
      if (retainsCoordinate) {
        result.numericPayload =
            input.representation ? input.representation : input.numericPayload;
        result.numericWidth =
            input.representation ? input.width : input.numericWidth;
      }
      return result;
    }
    if (opcode == OBELISK_RT_RANDOM_REAL_TO_BITS_V1) {
      if (!input.real || input.width != width || sign)
        return {nullptr, width};
      // Preserve payloads of bitcasts and rank decoders. Floating arithmetic
      // can produce platform-dependent NaN payloads; do not invent a model.
      return {input.representation, width};
    }
    if (input.real || sign)
      return {nullptr, width};
    if (opcode == OBELISK_RT_RANDOM_REAL32_BITS_V1 ||
        opcode == OBELISK_RT_RANDOM_REAL64_BITS_V1) {
      if (input.width != width ||
          width != (opcode == OBELISK_RT_RANDOM_REAL32_BITS_V1 ? 32u : 64u))
        return {nullptr, width};
      return {Z3_mk_fpa_to_fp_bv(context, input.value, realSort(width)), width,
              true, input.value};
    }
    unsigned rankWidth =
        width == 32 ? OBELISK_RT_REAL32_RANK_BITS : OBELISK_RT_REAL64_RANK_BITS;
    if ((opcode != OBELISK_RT_RANDOM_REAL32_FROM_RANK_V1 &&
         opcode != OBELISK_RT_RANDOM_REAL64_FROM_RANK_V1) ||
        input.width != rankWidth ||
        width != (opcode == OBELISK_RT_RANDOM_REAL32_FROM_RANK_V1 ? 32u : 64u))
      return {nullptr, width};
    unsigned fraction = width == 32 ? 23 : 52;
    unsigned exponent = width == 32 ? 8 : 11;
    Z3_ast signBit =
        Z3_mk_extract(context, rankWidth - 1, rankWidth - 1, input.value);
    Z3_ast negative = Z3_mk_eq(context, signBit, constant(1, 1));
    Z3_ast magnitude = Z3_mk_ite(
        context, negative, Z3_mk_bvneg(context, input.value), input.value);
    // A balanced highest-set-bit tree visits every coordinate bit once and
    // avoids a width-dependent compiler or WASM recursion depth.
    struct Prefix {
      Z3_ast present, highest;
    };
    std::vector<Prefix> prefixes;
    for (unsigned bit = 0; bit != rankWidth; ++bit)
      prefixes.push_back(
          {Z3_mk_eq(context, Z3_mk_extract(context, bit, bit, magnitude),
                    constant(1, 1)),
           constant(bit + 1, exponent + 1)});
    while (prefixes.size() > 1) {
      size_t next = 0;
      for (size_t i = 0; i < prefixes.size(); i += 2) {
        Prefix value = prefixes[i];
        if (i + 1 != prefixes.size()) {
          Prefix high = prefixes[i + 1];
          value = {
              disjunction(value.present, high.present),
              Z3_mk_ite(context, high.present, high.highest, value.highest)};
        }
        prefixes[next++] = value;
      }
      prefixes.resize(next);
    }
    Z3_ast highest =
        Z3_mk_ite(context, prefixes.front().present, prefixes.front().highest,
                  constant(0, exponent + 1));
    Z3_ast normal =
        Z3_mk_bvugt(context, highest, constant(fraction + 1, exponent + 1));
    Z3_ast shift = Z3_mk_ite(
        context, normal,
        Z3_mk_bvsub(context, highest, constant(fraction + 1, exponent + 1)),
        constant(1, exponent + 1));
    Z3_ast shifted = Z3_mk_bvlshr(context, magnitude,
                                  resize({shift, exponent + 1}, rankWidth));
    Z3_ast storedExponent =
        Z3_mk_ite(context, normal, resize({shift, exponent + 1}, exponent),
                  constant(0, exponent));
    Z3_ast payload = Z3_mk_concat(
        context, signBit,
        Z3_mk_concat(context, storedExponent,
                     Z3_mk_extract(context, fraction - 1, 0, shifted)));
    return {Z3_mk_fpa_to_fp_bv(context, payload, realSort(width)), width, true,
            payload, input.value};
  }

  /// Retain the lexicographic comparator as a linear Boolean prefix. Binding
  /// a state endpoint then gives a literal AND/OR chain rather than leaving
  /// bit-blasted subtraction/carry expressions for the sampler to rediscover.
  Z3_ast coordinateOrder(Z3_ast lhs, Z3_ast rhs, unsigned width,
                         bool inclusive) {
    Z3_ast result = inclusive ? Z3_mk_true(context) : Z3_mk_false(context);
    for (unsigned bit = 0; bit != width; ++bit) {
      Z3_ast a = Z3_mk_eq(context, Z3_mk_extract(context, bit, bit, lhs),
                          constant(1, 1));
      Z3_ast b = Z3_mk_eq(context, Z3_mk_extract(context, bit, bit, rhs),
                          constant(1, 1));
      if (bit + 1 == width) {
        a = Z3_mk_not(context, a);
        b = Z3_mk_not(context, b);
      }
      Z3_ast notA = Z3_mk_not(context, a);
      result = Z3_mk_ite(context, b, disjunction(notA, result),
                         conjunction(notA, result));
    }
    return result;
  }

  /// IEEE ordering is monotone in the signed numeric coordinate. Comparing a
  /// decoded property to a retained IEEE payload therefore only needs the
  /// payload's complete preimage cell, rather than the property's barrel shift
  /// and highest-bit decoder. Each endpoint takes a fixed number of Z3 nodes.
  Z3_ast compareCoordinate(uint8_t opcode, Bits rank, Z3_ast payload,
                           unsigned precision,
                           std::pair<Z3_ast, Z3_ast> capturedCell = {nullptr,
                                                                     nullptr}) {
    if (rank.width == OBELISK_RT_REAL32_RANK_BITS && precision == 64) {
      // Widening shortreal is exact. Its order against a real endpoint is
      // determined by that endpoint's nearest shortreal and which side the
      // endpoint lies on. No candidate coordinate decoder is needed.
      Z3_ast target = Z3_mk_fpa_to_fp_bv(context, payload, realSort(64));
      Z3_ast nearest =
          Z3_mk_fpa_to_fp_float(context, rounding(), target, realSort(32));
      Z3_ast widened =
          Z3_mk_fpa_to_fp_float(context, rounding(), nearest, realSort(64));
      auto cell = captureCell ? captureCell(payload, 32)
                              : std::pair<Z3_ast, Z3_ast>{nullptr, nullptr};
      Z3_ast nearestPayload;
      if (cell.first) {
        // Cell parameters supply the numeric endpoints. Only classification
        // and sign remain in this payload, avoiding arbitrary NaN witnesses.
        Z3_ast sign =
            Z3_mk_ite(context,
                      Z3_mk_eq(context, Z3_mk_extract(context, 63, 63, payload),
                               constant(1, 1)),
                      constant(UINT64_C(0x80000000), 32), constant(0, 32));
        nearestPayload = Z3_mk_ite(
            context, Z3_mk_fpa_is_nan(context, nearest),
            constant(UINT64_C(0x7fc00000), 32),
            Z3_mk_bvor(context, sign,
                       Z3_mk_ite(context,
                                 Z3_mk_fpa_is_infinite(context, nearest),
                                 constant(UINT64_C(0x7f800000), 32),
                                 constant(1, 32))));
      } else {
        nearestPayload = Z3_mk_ite(context, Z3_mk_fpa_is_nan(context, nearest),
                                   constant(UINT64_C(0x7fc00000), 32),
                                   Z3_mk_fpa_to_ieee_bv(context, nearest));
      }
      auto compare = [&](uint8_t kind) {
        return compareCoordinate(kind, rank, nearestPayload, 32, cell);
      };
      switch (opcode) {
      case OBELISK_RT_RANDOM_EQ_V1:
      case OBELISK_RT_RANDOM_NE_V1: {
        Z3_ast equal = conjunction(Z3_mk_fpa_eq(context, widened, target),
                                   compare(OBELISK_RT_RANDOM_EQ_V1));
        return opcode == OBELISK_RT_RANDOM_EQ_V1 ? equal
                                                 : Z3_mk_not(context, equal);
      }
      case OBELISK_RT_RANDOM_LT_V1:
        return Z3_mk_ite(context, Z3_mk_fpa_lt(context, widened, target),
                         compare(OBELISK_RT_RANDOM_LE_V1),
                         compare(OBELISK_RT_RANDOM_LT_V1));
      case OBELISK_RT_RANDOM_LE_V1:
        return Z3_mk_ite(context, Z3_mk_fpa_leq(context, widened, target),
                         compare(OBELISK_RT_RANDOM_LE_V1),
                         compare(OBELISK_RT_RANDOM_LT_V1));
      case OBELISK_RT_RANDOM_GT_V1:
        return Z3_mk_ite(context, Z3_mk_fpa_gt(context, widened, target),
                         compare(OBELISK_RT_RANDOM_GE_V1),
                         compare(OBELISK_RT_RANDOM_GT_V1));
      case OBELISK_RT_RANDOM_GE_V1:
        return Z3_mk_ite(context, Z3_mk_fpa_geq(context, widened, target),
                         compare(OBELISK_RT_RANDOM_GE_V1),
                         compare(OBELISK_RT_RANDOM_GT_V1));
      default:
        return nullptr;
      }
    }
    unsigned fraction = precision == 32 ? 23 : 52;
    unsigned exponent = precision == 32 ? 8 : 11;
    Z3_ast exponentBits =
        Z3_mk_extract(context, precision - 2, fraction, payload);
    Z3_ast fractionBits = Z3_mk_extract(context, fraction - 1, 0, payload);
    Z3_ast negative = Z3_mk_eq(
        context, Z3_mk_extract(context, precision - 1, precision - 1, payload),
        constant(1, 1));
    Z3_ast normal = Z3_mk_not(
        context, Z3_mk_eq(context, exponentBits, constant(0, exponent)));
    Z3_ast significand =
        Z3_mk_bvor(context, resize({fractionBits, fraction}, rank.width),
                   Z3_mk_ite(context, normal,
                             constant(uint64_t{1} << fraction, rank.width),
                             constant(0, rank.width)));
    Z3_ast shift =
        Z3_mk_ite(context, normal, resize({exponentBits, exponent}, rank.width),
                  constant(1, rank.width));
    Z3_ast magnitude = Z3_mk_bvshl(context, significand, shift);
    Z3_ast lastMagnitude = Z3_mk_bvsub(
        context,
        Z3_mk_bvadd(context, magnitude,
                    Z3_mk_bvshl(context, constant(1, rank.width), shift)),
        constant(1, rank.width));
    Z3_ast zero =
        Z3_mk_eq(context, Z3_mk_extract(context, precision - 2, 0, payload),
                 constant(0, precision - 1));
    // IEEE equality combines both zero encodings and all three zero ranks.
    Z3_ast first =
        Z3_mk_ite(context, zero, Z3_mk_bvneg(context, constant(1, rank.width)),
                  Z3_mk_ite(context, negative,
                            Z3_mk_bvneg(context, lastMagnitude), magnitude));
    Z3_ast last =
        Z3_mk_ite(context, zero, constant(1, rank.width),
                  Z3_mk_ite(context, negative, Z3_mk_bvneg(context, magnitude),
                            lastMagnitude));
    if (capturedCell.first || captureCell) {
      auto cell =
          capturedCell.first ? capturedCell : captureCell(payload, precision);
      if (cell.first) {
        first = cell.first;
        last = cell.second;
      }
    }
    // The one coordinate outside the decoder's finite cells is the signed
    // minimum: its magnitude's highest bit yields negative infinity. Keeping
    // that case makes this rewrite exact even without a separate domain root.
    Z3_ast minimum = Z3_mk_bvshl(context, constant(1, rank.width),
                                 constant(rank.width - 1, rank.width));
    Z3_ast minimumRank = Z3_mk_eq(context, rank.value, minimum);
    Z3_ast finite = nullptr, infinite = nullptr;
    switch (opcode) {
    case OBELISK_RT_RANDOM_EQ_V1:
    case OBELISK_RT_RANDOM_NE_V1:
      finite =
          conjunction(Z3_mk_not(context, coordinateOrder(rank.value, first,
                                                         rank.width, false)),
                      coordinateOrder(rank.value, last, rank.width, true));
      infinite = conjunction(negative, minimumRank);
      if (opcode == OBELISK_RT_RANDOM_NE_V1) {
        finite = Z3_mk_not(context, finite);
        infinite = Z3_mk_not(context, infinite);
      }
      break;
    case OBELISK_RT_RANDOM_LT_V1:
      finite = coordinateOrder(rank.value, first, rank.width, false);
      infinite = Z3_mk_not(context, negative);
      break;
    case OBELISK_RT_RANDOM_LE_V1:
      finite = coordinateOrder(rank.value, last, rank.width, true);
      infinite = disjunction(Z3_mk_not(context, negative), minimumRank);
      break;
    case OBELISK_RT_RANDOM_GT_V1:
      finite = Z3_mk_not(context,
                         coordinateOrder(rank.value, last, rank.width, true));
      infinite = conjunction(negative, Z3_mk_not(context, minimumRank));
      break;
    case OBELISK_RT_RANDOM_GE_V1:
      finite = Z3_mk_not(context,
                         coordinateOrder(rank.value, first, rank.width, false));
      infinite = negative;
      break;
    default:
      return nullptr;
    }
    Z3_ast special =
        Z3_mk_eq(context, exponentBits,
                 constant((uint64_t{1} << exponent) - 1, exponent));
    Z3_ast nan = conjunction(
        special, Z3_mk_not(context, Z3_mk_eq(context, fractionBits,
                                             constant(0, fraction))));
    return Z3_mk_ite(context, nan,
                     opcode == OBELISK_RT_RANDOM_NE_V1 ? Z3_mk_true(context)
                                                       : Z3_mk_false(context),
                     Z3_mk_ite(context, special, infinite, finite));
  }

  Bits realBinary(uint8_t opcode, Bits lhs, Bits rhs, unsigned width) {
    switch (opcode) {
    case OBELISK_RT_RANDOM_LOGICAL_AND_V1:
      return boolean(conjunction(truth(lhs), truth(rhs)), width);
    case OBELISK_RT_RANDOM_LOGICAL_OR_V1:
      return boolean(disjunction(truth(lhs), truth(rhs)), width);
    case OBELISK_RT_RANDOM_LOGICAL_IMPLIES_V1:
      return boolean(Z3_mk_implies(context, truth(lhs), truth(rhs)), width);
    case OBELISK_RT_RANDOM_LOGICAL_EQUIV_V1:
      return boolean(Z3_mk_eq(context, truth(lhs), truth(rhs)), width);
    }
    if (!lhs.real || !rhs.real)
      return {nullptr, width};
    if (lhs.width == rhs.width && opcode >= OBELISK_RT_RANDOM_EQ_V1 &&
        opcode <= OBELISK_RT_RANDOM_LT_V1) {
      if (lhs.coordinate && rhs.representation && !rhs.coordinate) {
        unsigned rankWidth =
            Z3_get_bv_sort_size(context, Z3_get_sort(context, lhs.coordinate));
        if (Z3_ast predicate =
                compareCoordinate(opcode, {lhs.coordinate, rankWidth},
                                  rhs.representation, lhs.width)) {
          if (lhs.coordinateGuard) {
            Bits state = lhs;
            state.value = lhs.coordinateState;
            state.coordinate = state.coordinateGuard = state.coordinateState =
                nullptr;
            Bits frozen = realBinary(opcode, state, rhs, width);
            if (!frozen.value)
              return {nullptr, width};
            predicate = Z3_mk_ite(context, lhs.coordinateGuard, predicate,
                                  truth(frozen));
          }
          return boolean(predicate, width);
        }
      } else if (rhs.coordinate && lhs.representation && !lhs.coordinate) {
        unsigned rankWidth =
            Z3_get_bv_sort_size(context, Z3_get_sort(context, rhs.coordinate));
        uint8_t reversed = opcode;
        switch (opcode) {
        case OBELISK_RT_RANDOM_GE_V1:
          reversed = OBELISK_RT_RANDOM_LE_V1;
          break;
        case OBELISK_RT_RANDOM_GT_V1:
          reversed = OBELISK_RT_RANDOM_LT_V1;
          break;
        case OBELISK_RT_RANDOM_LE_V1:
          reversed = OBELISK_RT_RANDOM_GE_V1;
          break;
        case OBELISK_RT_RANDOM_LT_V1:
          reversed = OBELISK_RT_RANDOM_GT_V1;
          break;
        }
        if (Z3_ast predicate =
                compareCoordinate(reversed, {rhs.coordinate, rankWidth},
                                  lhs.representation, lhs.width)) {
          if (rhs.coordinateGuard) {
            Bits state = rhs;
            state.value = rhs.coordinateState;
            state.coordinate = state.coordinateGuard = state.coordinateState =
                nullptr;
            Bits frozen = realBinary(opcode, lhs, state, width);
            if (!frozen.value)
              return {nullptr, width};
            predicate = Z3_mk_ite(context, rhs.coordinateGuard, predicate,
                                  truth(frozen));
          }
          return boolean(predicate, width);
        }
      }
    }
    unsigned precision = std::max(lhs.width, rhs.width);
    Z3_ast a = resize(lhs, precision), b = resize(rhs, precision);
    Z3_ast predicate = nullptr;
    switch (opcode) {
    case OBELISK_RT_RANDOM_EQ_V1:
      predicate = Z3_mk_fpa_eq(context, a, b);
      break;
    case OBELISK_RT_RANDOM_NE_V1:
      predicate = Z3_mk_not(context, Z3_mk_fpa_eq(context, a, b));
      break;
    case OBELISK_RT_RANDOM_GE_V1:
      predicate = Z3_mk_fpa_geq(context, a, b);
      break;
    case OBELISK_RT_RANDOM_GT_V1:
      predicate = Z3_mk_fpa_gt(context, a, b);
      break;
    case OBELISK_RT_RANDOM_LE_V1:
      predicate = Z3_mk_fpa_leq(context, a, b);
      break;
    case OBELISK_RT_RANDOM_LT_V1:
      predicate = Z3_mk_fpa_lt(context, a, b);
      break;
    }
    if (predicate)
      return boolean(predicate, width);
    if (width != 32 && width != 64)
      return {nullptr, width};
    a = resize(lhs, width);
    b = resize(rhs, width);
    Z3_ast value = nullptr;
    switch (opcode) {
    case OBELISK_RT_RANDOM_ADD_V1:
      value = Z3_mk_fpa_add(context, rounding(), a, b);
      break;
    case OBELISK_RT_RANDOM_SUB_V1:
      value = Z3_mk_fpa_sub(context, rounding(), a, b);
      break;
    case OBELISK_RT_RANDOM_MUL_V1:
      value = Z3_mk_fpa_mul(context, rounding(), a, b);
      break;
    case OBELISK_RT_RANDOM_DIV_V1:
      value = Z3_mk_fpa_div(context, rounding(), a, b);
      break;
    }
    return {value, width, true};
  }

  Z3_context context = nullptr;
  bool hasReal = false;
  std::function<std::pair<Z3_ast, Z3_ast>(Z3_ast, unsigned)> captureCell;
  std::unordered_map<Z3_ast, Bits> popcounts;
  struct CardinalityInputs {
    std::vector<Z3_ast> positive, negative;
    std::vector<int> coefficients;
  };
  std::unordered_map<Z3_ast, CardinalityInputs> cardinalityInputs;
};

/// Rewriting steps allowed to one plan's simplification.
constexpr unsigned maxRewriteSteps = 5'000'000;

inline bool bitBlastPredicate(Z3_context context, Z3_ast predicate,
                              std::vector<Z3_ast> &formulas, bool real) {
  Z3_tactic floating = nullptr;
  if (real) {
    // hi_fp_unspecified is set globally by BitVectorFormula.
    floating = Z3_mk_tactic(context, "fpa2bv");
    if (!floating || Z3_get_error_code(context) != Z3_OK)
      return false;
    Z3_tactic_inc_ref(context, floating);
  }
  Z3_goal goal = Z3_mk_goal(context, false, false, false);
  Z3_goal_inc_ref(context, goal);
  Z3_goal_assert(context, goal, predicate);
  Z3_tactic simplify = Z3_mk_tactic(context, "simplify");
  Z3_tactic_inc_ref(context, simplify);
  Z3_tactic blast = Z3_mk_tactic(context, "bit-blast");
  Z3_tactic_inc_ref(context, blast);
  Z3_tactic cardinality = Z3_mk_tactic(context, "card2bv");
  Z3_tactic_inc_ref(context, cardinality);
  Z3_params parameters = Z3_mk_params(context);
  Z3_params_inc_ref(context, parameters);
  Z3_params_set_bool(
      context, parameters,
      Z3_mk_string_symbol(context, "keep_cardinality_constraints"), false);
  Z3_params_set_symbol(context, parameters,
                       Z3_mk_string_symbol(context, "cardinality.encoding"),
                       Z3_mk_string_symbol(context, "unate"));
  Z3_tactic unate = Z3_tactic_using_params(context, cardinality, parameters);
  Z3_tactic_inc_ref(context, unate);
  Z3_tactic cardFirst = Z3_tactic_and_then(context, simplify, unate);
  Z3_tactic_inc_ref(context, cardFirst);
  Z3_tactic first = Z3_tactic_and_then(context, cardFirst, blast);
  Z3_tactic_inc_ref(context, first);
  Z3_tactic floatingSimplified = nullptr, floatingFirst = nullptr;
  if (real) {
    floatingSimplified = Z3_tactic_and_then(context, simplify, floating);
    Z3_tactic_inc_ref(context, floatingSimplified);
    floatingFirst = Z3_tactic_and_then(context, floatingSimplified, first);
    Z3_tactic_inc_ref(context, floatingFirst);
  }
  Z3_tactic unbounded =
      Z3_tactic_and_then(context, real ? floatingFirst : first, simplify);
  Z3_tactic_inc_ref(context, unbounded);
  // Tactics ignore the context's resource limit. Keep products nested rather
  // than flattening them into sorted monomials, which for a chain of squarings
  // (a ** with a random exponent) grows exponentially within one rewrite, and
  // bound the rewriter's steps deterministically. Exhaustion fails the tactic
  // and is diagnosed as a plan that cannot be compiled.
  Z3_params limits = Z3_mk_params(context);
  Z3_params_inc_ref(context, limits);
  Z3_params_set_uint(context, limits, Z3_mk_string_symbol(context, "max_steps"),
                     maxRewriteSteps);
  Z3_params_set_bool(context, limits, Z3_mk_string_symbol(context, "flat"),
                     false);
  Z3_tactic tactic = Z3_tactic_using_params(context, unbounded, limits);
  Z3_tactic_inc_ref(context, tactic);
  Z3_apply_result result = Z3_tactic_apply(context, tactic, goal);
  bool valid = result && Z3_get_error_code(context) == Z3_OK;
  if (result)
    Z3_apply_result_inc_ref(context, result);
  valid = valid && Z3_apply_result_get_num_subgoals(context, result) == 1;
  formulas.clear();
  if (valid) {
    Z3_goal output = Z3_apply_result_get_subgoal(context, result, 0);
    for (unsigned index = 0; index != Z3_goal_size(context, output); ++index)
      formulas.push_back(Z3_goal_formula(context, output, index));
  }
  if (result)
    Z3_apply_result_dec_ref(context, result);
  Z3_tactic_dec_ref(context, tactic);
  Z3_params_dec_ref(context, limits);
  Z3_tactic_dec_ref(context, unbounded);
  if (real) {
    Z3_tactic_dec_ref(context, floatingFirst);
    Z3_tactic_dec_ref(context, floatingSimplified);
    Z3_tactic_dec_ref(context, floating);
  }
  Z3_tactic_dec_ref(context, first);
  Z3_tactic_dec_ref(context, cardFirst);
  Z3_tactic_dec_ref(context, unate);
  Z3_params_dec_ref(context, parameters);
  Z3_tactic_dec_ref(context, cardinality);
  Z3_tactic_dec_ref(context, blast);
  Z3_tactic_dec_ref(context, simplify);
  Z3_goal_dec_ref(context, goal);
  return valid;
}

/// Preserve named Boolean inputs through bit blasting. No unbound auxiliary
/// Boolean is accepted: otherwise an existential witness could multiply a
/// sample's count. Named IEEE cell parameters are deterministic conversions
/// of declared state captures.
class PlanBuilder {
public:
  PlanBuilder() {
    formula.captureCell = [this](Z3_ast payload, unsigned precision) {
      auto source = realCaptureSources.find(payload);
      if (source == realCaptureSources.end() ||
          (source->second.second != precision &&
           !(precision == 32 && source->second.second == 64)))
        return std::pair<Z3_ast, Z3_ast>{nullptr, nullptr};
      unsigned capture = source->second.first;
      bool narrowed = precision == 32 && source->second.second == 64;
      uint64_t key =
          (uint64_t(capture) << 2) | (narrowed ? 2 : precision == 64);
      auto found = realCaptureCells.find(key);
      if (found != realCaptureCells.end())
        return found->second;
      unsigned width = precision == 32 ? OBELISK_RT_REAL32_RANK_BITS
                                       : OBELISK_RT_REAL64_RANK_BITS;
      PlanOpcode first = narrowed          ? PlanOpcode::Real32From64FirstBit
                         : precision == 32 ? PlanOpcode::Real32FirstBit
                                           : PlanOpcode::Real64FirstBit;
      PlanOpcode last = narrowed          ? PlanOpcode::Real32From64LastBit
                        : precision == 32 ? PlanOpcode::Real32LastBit
                                          : PlanOpcode::Real64LastBit;
      std::vector<Z3_ast> firstBits, lastBits;
      for (unsigned bit = 0; bit != width; ++bit) {
        firstBits.push_back(input(first, capture, bit));
        lastBits.push_back(input(last, capture, bit));
      }
      auto endpoints = std::make_pair(pack(firstBits), pack(lastBits));
      realCaptureCells.emplace(key, endpoints);
      return endpoints;
    };
  }

  void registerRealCapture(Bits capture, unsigned index) {
    for (unsigned precision : {32u, 64u})
      if (capture.width >= precision)
        realCaptureSources.emplace(formula.resize(capture, precision),
                                   std::make_pair(index, precision));
  }

  Z3_ast input(PlanOpcode opcode, uint32_t a, uint32_t b = 0) {
    ID id = circuit.input(opcode, a, b);
    Z3_ast ast = Z3_mk_const(formula.context,
                             Z3_mk_int_symbol(formula.context, nextSymbol++),
                             Z3_mk_bool_sort(formula.context));
    converted.emplace(ast, id);
    return ast;
  }

  Z3_ast pack(const std::vector<Z3_ast> &bits) {
    std::vector<Z3_ast> pieces;
    for (Z3_ast bit : bits)
      pieces.push_back(Z3_mk_ite(formula.context, bit, formula.constant(1, 1),
                                 formula.constant(0, 1)));
    while (pieces.size() > 1) {
      size_t next = 0;
      for (size_t index = 0; index < pieces.size(); index += 2)
        pieces[next++] = index + 1 == pieces.size()
                             ? pieces[index]
                             : Z3_mk_concat(formula.context, pieces[index + 1],
                                            pieces[index]);
      pieces.resize(next);
    }
    return pieces.front();
  }

  bool lower(Z3_ast predicate, ID &result) {
    std::vector<Z3_ast> formulas;
    if (!bitBlastPredicate(formula.context, predicate, formulas,
                           formula.hasReal))
      return false;
    result = 1;
    for (Z3_ast atom : formulas) {
      ID value;
      if (!convert(atom, value))
        return false;
      result = circuit.conjunction(result, value);
    }
    return circuit.getNodes().size() < UINT32_MAX / 2;
  }

  BitVectorFormula formula;
  BooleanCircuit circuit;

private:
  bool convert(Z3_ast predicate, ID &result) {
    Z3_context context = formula.context;
    struct Frame {
      Z3_ast value;
      unsigned nextChild = 0;
    };
    std::vector<Frame> pending{{predicate}};
    while (!pending.empty()) {
      Z3_ast value = pending.back().value;
      if (converted.count(value)) {
        pending.pop_back();
        continue;
      }
      if (Z3_get_ast_kind(context, value) != Z3_APP_AST)
        return false;
      Z3_app app = Z3_to_app(context, value);
      unsigned arity = Z3_get_app_num_args(context, app);
      bool ready = true;
      while (pending.back().nextChild != arity) {
        Z3_ast child = Z3_get_app_arg(context, app, pending.back().nextChild++);
        if (!converted.count(child)) {
          pending.push_back({child});
          ready = false;
          break;
        }
      }
      if (!ready)
        continue;
      auto argument = [&](unsigned index) {
        return converted.at(Z3_get_app_arg(context, app, index));
      };
      Z3_decl_kind kind =
          Z3_get_decl_kind(context, Z3_get_app_decl(context, app));
      ID id = 0;
      switch (kind) {
      case Z3_OP_TRUE:
        id = 1;
        break;
      case Z3_OP_FALSE:
        break;
      case Z3_OP_AND:
      case Z3_OP_OR:
      case Z3_OP_XOR:
      case Z3_OP_XOR3:
        id = kind == Z3_OP_AND ? 1 : 0;
        // Z3 flattens literal prefixes in their input order. Put the first
        // term at the outside of the chain, so binding its earliest variable
        // need not walk the complete remaining prefix at every bit.
        for (unsigned index = arity; index-- != 0;)
          id = kind == Z3_OP_AND  ? circuit.conjunction(id, argument(index))
               : kind == Z3_OP_OR ? circuit.disjunction(id, argument(index))
                                  : circuit.exclusiveOr(id, argument(index));
        break;
      case Z3_OP_NOT:
        id = argument(0) ^ 1;
        break;
      case Z3_OP_EQ:
      case Z3_OP_IFF:
        if (arity != 2)
          return false;
        id = circuit.exclusiveOr(argument(0), argument(1)) ^ 1;
        break;
      case Z3_OP_DISTINCT:
        if (arity != 2)
          return false;
        id = circuit.exclusiveOr(argument(0), argument(1));
        break;
      case Z3_OP_IMPLIES:
        id = circuit.disjunction(argument(0) ^ 1, argument(1));
        break;
      case Z3_OP_ITE:
        id = circuit.conditional(argument(0), argument(1), argument(2));
        break;
      case Z3_OP_CARRY:
        id = circuit.disjunction(
            circuit.conjunction(argument(0), argument(1)),
            circuit.conjunction(argument(2),
                                circuit.disjunction(argument(0), argument(1))));
        break;
      default:
        return false;
      }
      converted.emplace(value, id);
      pending.pop_back();
    }
    result = converted.at(predicate);
    return true;
  }

public:
  /// Move bounded Boolean cofactoring into the compiler as well as bit
  /// blasting. Parameters and equally significant random bits share a prefix;
  /// broad or nonlinear relations retain their exact circuit when this
  /// optimization budget is exhausted.
  void prepare(std::vector<std::pair<unsigned, ID>> &roots,
               const std::vector<unsigned> &boundaries,
               const std::vector<bool> &descending) {
    auto nodes = circuit.getNodes();
    std::vector<unsigned> bitColumns(boundaries.back());
    for (size_t field = 1; field != boundaries.size(); ++field)
      for (unsigned bit = boundaries[field - 1]; bit != boundaries[field];
           ++bit)
        bitColumns[bit] = descending[field] ? boundaries[field] - 1 - bit
                                            : bit - boundaries[field - 1];
    std::vector<uint32_t> columns(nodes.size(), 0);
    for (uint32_t index = 1; index != nodes.size(); ++index) {
      const auto &node = nodes[index];
      if (node.opcode == PlanOpcode::RandomBit)
        columns[index] = bitColumns[node.a];
      else if (node.opcode == PlanOpcode::CaptureBit)
        columns[index] = node.b;
      else if (unsigned precision = random::realCellPrecision(node.opcode))
        columns[index] = (precision == 32 ? OBELISK_RT_REAL32_RANK_BITS
                                          : OBELISK_RT_REAL64_RANK_BITS) -
                         1 - node.b;
    }
    // Each attempt traverses only its own subcircuit, and a failed one also
    // spends its construction budget. This bounds their total, including
    // subcircuits that atoms share. Skipping preparation preserves the exact
    // Boolean sampling plan.
    int64_t work = int64_t{1} << 24;
    // The declared random field holding a physical bit.
    auto fieldOf = [&](uint32_t bit) {
      return size_t(
                 std::upper_bound(boundaries.begin(), boundaries.end(), bit) -
                 boundaries.begin()) -
             1;
    };
    auto attempt = [&](const std::vector<ID> &conjuncts, ID &result) {
      std::vector<uint32_t> reached;
      std::unordered_set<uint32_t> seen{0};
      std::vector<ID> pending(conjuncts.begin(), conjuncts.end());
      while (!pending.empty()) {
        uint32_t index = pending.back() / 2;
        pending.pop_back();
        if (!seen.insert(index).second)
          continue;
        reached.push_back(index);
        if (nodes[index].opcode == PlanOpcode::And) {
          pending.push_back(nodes[index].a);
          pending.push_back(nodes[index].b);
        }
      }
      work -= int64_t(reached.size());
      if (work < 0)
        return false;
      // Parameters and equally significant random bits share a prefix.
      std::sort(reached.begin(), reached.end());
      std::vector<uint32_t> inputs;
      for (uint32_t index : reached)
        if (nodes[index].opcode != PlanOpcode::And)
          inputs.push_back(index);
      std::stable_sort(inputs.begin(), inputs.end(),
                       [&](uint32_t lhs, uint32_t rhs) {
                         return columns[lhs] < columns[rhs];
                       });
      BooleanCircuit prepared;
      std::unordered_map<uint32_t, ID> translated{{0, 0}};
      std::vector<ID> variables, original;
      for (uint32_t index : inputs) {
        ID input =
            prepared.input(PlanOpcode::RandomBit, index, variables.size());
        translated.emplace(index, input);
        variables.push_back(input);
        original.push_back(index * 2);
      }
      auto handle = [&](ID id) { return translated.at(id / 2) ^ (id & 1); };
      for (uint32_t index : reached)
        if (nodes[index].opcode == PlanOpcode::And)
          translated.emplace(index,
                             prepared.conjunction(handle(nodes[index].a),
                                                  handle(nodes[index].b)));
      ID relation = 1;
      for (ID conjunct : conjuncts)
        relation = prepared.conjunction(relation, handle(conjunct));
      runtime::ModelCounter counter(prepared, variables);
      if (!counter.prepareDiagram(relation, 4096, 32768)) {
        work -= 4096;
        return false;
      }
      result = counter.exportDiagram(circuit, original);
      // Record each random field that the diagram leaves free: eliminating
      // it alone leaves the projection true for every other input, state
      // and mode parameters included. Over a single field, that projection
      // is a constant the target decides cheaply. The target splits top-level
      // ANDs, so only a diagram that stays one conjunct is found by its
      // handle.
      const auto &exported = circuit.getNodes()[result / 2];
      if (result / 2 == 0 ||
          (!(result & 1) && exported.opcode == PlanOpcode::And))
        return true;
      std::vector<size_t> fields(variables.size(), SIZE_MAX);
      for (size_t variable = 0; variable != variables.size(); ++variable)
        if (nodes[inputs[variable]].opcode == PlanOpcode::RandomBit)
          fields[variable] = fieldOf(nodes[inputs[variable]].a);
      std::vector<size_t> distinct(fields.begin(), fields.end());
      std::sort(distinct.begin(), distinct.end());
      distinct.erase(std::unique(distinct.begin(), distinct.end()),
                     distinct.end());
      if (std::count_if(distinct.begin(), distinct.end(),
                        [](size_t field) { return field != SIZE_MAX; }) < 2)
        return true;
      for (size_t field : distinct) {
        if (field == SIZE_MAX)
          continue;
        std::vector<bool> keep(variables.size());
        unsigned kept = 0;
        for (size_t variable = 0; variable != variables.size(); ++variable)
          kept += keep[variable] = fields[variable] != field;
        counter.project(keep);
        work -= int64_t(variables.size());
        if (counter.count().compare(runtime::ModelCount::powerOfTwo(kept)) == 0)
          freeFields.push_back({result, boundaries[field],
                                boundaries[field + 1] - boundaries[field]});
      }
      return true;
    };
    // One bounded diagram per atom preserves independent function stages and
    // dynamic soft priorities. Joint satisfiability is handled after binding
    // the live parameters, without freezing one compiler-selected model.
    // Within an atom, top-level conjuncts get one diagram per set of random
    // fields they read. The target samples conjuncts over disjoint inputs
    // apart and splits them again once an earlier solve-before layer decides
    // what joins them, such as a container size guarding each element; one
    // diagram over interleaved independent fields would multiply their widths
    // and hide that structure. Conjuncts over the same fields, such as both
    // bounds of a range, share a diagram, or each gets its own when together
    // they exceed the budget.
    auto fieldsOf = [&](ID conjunct) {
      std::vector<size_t> fields;
      std::unordered_set<uint32_t> visited{0};
      std::vector<ID> walk{conjunct};
      while (!walk.empty()) {
        uint32_t index = walk.back() / 2;
        walk.pop_back();
        if (!visited.insert(index).second)
          continue;
        if (nodes[index].opcode == PlanOpcode::And) {
          walk.push_back(nodes[index].a);
          walk.push_back(nodes[index].b);
        } else if (nodes[index].opcode == PlanOpcode::RandomBit) {
          fields.push_back(fieldOf(nodes[index].a));
        }
      }
      work -= int64_t(visited.size());
      std::sort(fields.begin(), fields.end());
      fields.erase(std::unique(fields.begin(), fields.end()), fields.end());
      return fields;
    };
    for (auto &root : roots) {
      if (work < 0)
        break;
      std::vector<ID> conjuncts;
      std::unordered_set<ID> unique;
      std::vector<ID> pending{root.second};
      while (!pending.empty()) {
        ID id = pending.back();
        pending.pop_back();
        if (id == 1 || !unique.insert(id).second)
          continue;
        if (!(id & 1) && nodes[id / 2].opcode == PlanOpcode::And) {
          pending.push_back(nodes[id / 2].b);
          pending.push_back(nodes[id / 2].a);
        } else
          conjuncts.push_back(id);
      }
      std::vector<std::vector<ID>> groups;
      std::map<std::vector<size_t>, size_t> bySupport;
      for (ID conjunct : conjuncts) {
        auto [group, inserted] =
            bySupport.try_emplace(fieldsOf(conjunct), groups.size());
        if (inserted)
          groups.emplace_back();
        groups[group->second].push_back(conjunct);
      }
      ID result = conjuncts.empty() ? root.second : 1;
      for (const auto &group : groups) {
        ID prepared;
        if (work >= 0 && attempt(group, prepared)) {
          result = circuit.conjunction(result, prepared);
          continue;
        }
        for (ID conjunct : group) {
          if (group.size() == 1 || work < 0 || !attempt({conjunct}, prepared))
            prepared = conjunct;
          result = circuit.conjunction(result, prepared);
        }
      }
      root.second = result;
    }
  }

  struct FreeField {
    ID node;
    uint32_t offset, width;
  };
  std::vector<FreeField> freeFields;

private:
  int nextSymbol = 0;
  std::unordered_map<Z3_ast, ID> converted;
  std::unordered_map<Z3_ast, std::pair<unsigned, unsigned>> realCaptureSources;
  std::unordered_map<uint64_t, std::pair<Z3_ast, Z3_ast>> realCaptureCells;
};
} // namespace

std::optional<std::vector<uint8_t>>
compileRandomSamplerPlan(const uint8_t *program, size_t programSize,
                         const std::vector<uint32_t> &captureWidths) {
  if (programSize < OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE ||
      read32(program) != OBELISK_RT_RANDOM_PROGRAM_MAGIC)
    return std::nullopt;
  bool wide = read16(program + 4) == OBELISK_RT_RANDOM_PROGRAM_VERSION_V2;
  size_t header = wide ? OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE_V2
                       : OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE;
  unsigned width = read32(program + 8), instructions = read32(program + 12);
  if (programSize < header || instructions > (programSize - header) / 16 ||
      captureWidths.size() != read32(program + 16))
    return std::nullopt;
  size_t literals = header + size_t(instructions) * 16;
  unsigned literalWords = wide ? read32(program + 24) : 0;
  if (literalWords > (programSize - literals) / 8)
    return std::nullopt;
  PlanBuilder builder;
  auto &f = builder.formula;
  std::vector<bool> boundary(size_t(width) + 1, false);
  boundary[0] = boundary[width] = true;
  for (unsigned index = 0; index != instructions; ++index) {
    const uint8_t *p = program + header + size_t(index) * 16;
    if (p[0] != OBELISK_RT_RANDOM_PUSH_VARIABLE_V1)
      continue;
    unsigned bits = wide ? read32(p + 4) : p[1];
    unsigned offset = read32(p + (wide ? 8 : 4));
    if (!bits || offset > width || bits > width - offset)
      return std::nullopt;
    boundary[offset] = boundary[offset + bits] = true;
  }
  std::vector<unsigned> boundaries;
  for (size_t bit = 0; bit != boundary.size(); ++bit)
    if (boundary[bit])
      boundaries.push_back(static_cast<unsigned>(bit));
  std::vector<Z3_ast> randomBits(width);
  // Numeric real intervals prune high prefixes first. Integral fields retain
  // their low-prefix order, which is useful for modular arithmetic and counts.
  std::vector<bool> descending(boundaries.size(), false);
  if (read32(program + 20) & OBELISK_RT_RANDOM_PROGRAM_HAS_REAL)
    for (size_t field = 1; field != boundaries.size(); ++field) {
      unsigned fieldWidth = boundaries[field] - boundaries[field - 1];
      descending[field] = fieldWidth == OBELISK_RT_REAL32_RANK_BITS ||
                          fieldWidth == OBELISK_RT_REAL64_RANK_BITS;
    }
  unsigned ordinal = 0;
  // Keep only unfinished fields in the column traversal. Each declared bit
  // is visited once even when one wide field follows many narrow fields.
  std::vector<size_t> next(boundaries.size());
  for (size_t field = 0; field != next.size(); ++field)
    next[field] = field + 1;
  size_t first = 1;
  for (unsigned column = 0; first != boundaries.size(); ++column) {
    size_t *link = &first;
    while (*link != boundaries.size()) {
      size_t field = *link;
      unsigned bit = descending[field] ? boundaries[field] - 1 - column
                                       : boundaries[field - 1] + column;
      randomBits[bit] = builder.input(PlanOpcode::RandomBit, bit, ordinal++);
      if (column + 1 == boundaries[field] - boundaries[field - 1])
        *link = next[field];
      else
        link = &next[field];
    }
  }
  // An empty random argument set is a singleton assignment space. Its
  // constraints still have state/mode inputs, but no zero-width Z3 bit-vector.
  Z3_ast variable = width ? builder.pack(randomBits) : nullptr;
  std::vector<Bits> captures;
  for (unsigned index = 0; index != captureWidths.size(); ++index) {
    if (!captureWidths[index])
      return std::nullopt;
    std::vector<Z3_ast> bits;
    for (unsigned bit = 0; bit != captureWidths[index]; ++bit)
      bits.push_back(builder.input(PlanOpcode::CaptureBit, index, bit));
    captures.push_back({builder.pack(bits), captureWidths[index]});
    builder.registerRealCapture(captures.back(), index);
  }
  std::unordered_map<unsigned, Z3_ast> enabled;
  std::vector<Bits> stack;
  std::vector<Bits> temporaries;
  std::vector<std::pair<unsigned, ID>> roots;
  random::DistBitSequence distBits(
      captureWidths.size(), std::nullopt,
      read32(program + 20) & OBELISK_RT_RANDOM_PROGRAM_EXACT_WEIGHTED);
  for (unsigned index = 0; index != instructions; ++index) {
    const uint8_t *p = program + header + size_t(index) * 16;
    unsigned opcode = p[0], bits = wide ? read32(p + 4) : p[1];
    uint8_t instructionFlags = wide ? p[1] : p[2];
    bool sign = (instructionFlags & OBELISK_RT_RANDOM_INSTRUCTION_SIGNED) != 0;
    unsigned operand = read32(p + (wide ? 8 : 4));
    uint64_t auxiliary = wide ? read32(p + 12) : read64(p + 8);
    if (!distBits.visit(opcode, bits, instructionFlags, operand, auxiliary))
      return std::nullopt;
    if (random::isRealInstruction(opcode)) {
      if (!(read32(program + 20) & OBELISK_RT_RANDOM_PROGRAM_HAS_REAL) ||
          stack.empty() ||
          !random::validRealInstruction(opcode, bits, instructionFlags, operand,
                                        auxiliary, stack.back().width))
        return std::nullopt;
    }
    if (!bits)
      return std::nullopt;
    if (opcode == OBELISK_RT_RANDOM_PUSH_VARIABLE_V1) {
      if (operand > width || bits > width - operand)
        return std::nullopt;
      stack.push_back(
          {Z3_mk_extract(f.context, operand + bits - 1, operand, variable),
           bits});
    } else if (opcode == OBELISK_RT_RANDOM_PUSH_CAPTURE_V1) {
      if (operand >= captures.size())
        return std::nullopt;
      stack.push_back({f.resize(captures[operand], bits), bits});
    } else if (opcode == OBELISK_RT_RANDOM_PUSH_LITERAL_V1) {
      if (wide) {
        size_t count = (uint64_t(bits) + 63) / 64;
        if (auxiliary > literalWords || count > literalWords - auxiliary)
          return std::nullopt;
        std::vector<uint64_t> words(count);
        for (size_t word = 0; word != count; ++word)
          words[word] = read64(program + literals + (auxiliary + word) * 8);
        stack.push_back({f.words(words.data(), bits), bits});
      } else {
        if (bits > 64)
          return std::nullopt;
        stack.push_back({f.constant(auxiliary, bits), bits});
      }
    } else if (opcode == OBELISK_RT_RANDOM_STORE_VALUE_V1) {
      if (stack.empty() || instructionFlags != 0 || auxiliary != 0 ||
          operand != temporaries.size() || bits != stack.back().width)
        return std::nullopt;
      temporaries.push_back(stack.back());
    } else if (opcode == OBELISK_RT_RANDOM_LOAD_VALUE_V1) {
      if (instructionFlags != 0 || auxiliary != 0 ||
          operand >= temporaries.size() || bits != temporaries[operand].width)
        return std::nullopt;
      stack.push_back(temporaries[operand]);
    } else if (opcode == OBELISK_RT_RANDOM_PUSH_CONSTRAINT_ENABLED_V1) {
      if (operand >= 64)
        return std::nullopt;
      auto found = enabled.find(operand);
      if (found == enabled.end())
        found =
            enabled
                .emplace(operand,
                         builder.input(PlanOpcode::ConstraintEnabled, operand))
                .first;
      stack.push_back(f.boolean(found->second, bits));
    } else if (random::isDistExportInstruction(opcode)) {
      if (stack.size() != 1 || stack.back().real ||
          stack.back().width != bits ||
          (opcode == OBELISK_RT_RANDOM_END_DIST_WEIGHT_BIT_V1 &&
           bits != captureWidths[operand]))
        return std::nullopt;
      Bits selected{
          Z3_mk_extract(f.context, auxiliary, auxiliary, stack.back().value),
          1};
      ID root;
      if (!builder.lower(f.truth(selected), root))
        return std::nullopt;
      roots.emplace_back(index, root);
      if (auxiliary + 1 == bits)
        stack.clear();
    } else if (opcode == OBELISK_RT_RANDOM_END_HARD_V1 ||
               opcode == OBELISK_RT_RANDOM_END_SOFT_V1 ||
               opcode == OBELISK_RT_RANDOM_END_DIST_GUARD_V1) {
      if (stack.size() != 1)
        return std::nullopt;
      ID root;
      if (!builder.lower(f.truth(stack.back()), root))
        return std::nullopt;
      roots.emplace_back(index, root);
      stack.clear();
    } else if (opcode == OBELISK_RT_RANDOM_SELECT_V1) {
      if (stack.size() < 3)
        return std::nullopt;
      Bits no = stack.back();
      stack.pop_back();
      Bits yes = stack.back();
      stack.pop_back();
      Bits condition = stack.back();
      stack.pop_back();
      if (yes.real != no.real || (yes.real && bits != 32 && bits != 64))
        return std::nullopt;
      Z3_ast yesValue = f.resize(yes, bits, sign);
      Z3_ast noValue = f.resize(no, bits, sign);
      if (!yesValue || !noValue)
        return std::nullopt;
      Z3_ast representation =
          yes.real && yes.representation && no.representation &&
                  yes.width == bits && no.width == bits
              ? Z3_mk_ite(f.context, f.truth(condition), yes.representation,
                          no.representation)
              : nullptr;
      bool sameCoordinates =
          yes.coordinate && no.coordinate &&
          Z3_is_eq_sort(f.context, Z3_get_sort(f.context, yes.coordinate),
                        Z3_get_sort(f.context, no.coordinate));
      Z3_ast coordinate =
          yes.real && sameCoordinates && yes.width == bits && no.width == bits
              ? Z3_mk_ite(f.context, f.truth(condition), yes.coordinate,
                          no.coordinate)
              : nullptr;
      Z3_ast coordinateGuard = nullptr, coordinateState = nullptr;
      if (yes.real && yes.width == bits && no.width == bits &&
          (yes.coordinate || no.coordinate) &&
          (!yes.coordinate || !no.coordinate || sameCoordinates) &&
          (!coordinate || yes.coordinateGuard || no.coordinateGuard)) {
        Z3_ast conditionValue = f.truth(condition);
        unsigned rankWidth = Z3_get_bv_sort_size(
            f.context, Z3_get_sort(f.context, yes.coordinate ? yes.coordinate
                                                             : no.coordinate));
        coordinate = Z3_mk_ite(
            f.context, conditionValue,
            yes.coordinate ? yes.coordinate : f.constant(0, rankWidth),
            no.coordinate ? no.coordinate : f.constant(0, rankWidth));
        auto guard = [&](Bits input) {
          return input.coordinateGuard ? input.coordinateGuard
                 : input.coordinate    ? Z3_mk_true(f.context)
                                       : Z3_mk_false(f.context);
        };
        auto state = [&](Bits input) {
          return input.coordinateState ? input.coordinateState
                 : input.coordinate
                     ? Z3_mk_fpa_zero(f.context, f.realSort(bits), false)
                     : input.value;
        };
        coordinateGuard =
            Z3_mk_ite(f.context, conditionValue, guard(yes), guard(no));
        coordinateState =
            Z3_mk_ite(f.context, conditionValue, state(yes), state(no));
      }
      Bits result{Z3_mk_ite(f.context, f.truth(condition), yesValue, noValue),
                  bits,
                  yes.real,
                  representation,
                  coordinate,
                  coordinateGuard,
                  coordinateState};
      if (yes.real && yes.numericPayload && no.numericPayload &&
          yes.width == bits && no.width == bits &&
          yes.numericWidth == no.numericWidth) {
        result.numericPayload =
            Z3_mk_ite(f.context, f.truth(condition), yes.numericPayload,
                      no.numericPayload);
        result.numericWidth = yes.numericWidth;
      }
      stack.push_back(result);
    } else if ((opcode >= OBELISK_RT_RANDOM_CAST_V1 &&
                opcode <= OBELISK_RT_RANDOM_LOGICAL_NOT_V1) ||
               opcode == OBELISK_RT_RANDOM_COUNT_ONES_V1 ||
               opcode == OBELISK_RT_RANDOM_CLOG2_V1 ||
               random::isRealInstruction(opcode)) {
      if (stack.empty())
        return std::nullopt;
      Bits value = stack.back();
      stack.pop_back();
      Bits result = f.unary(opcode, value, bits, sign);
      if (!result.value)
        return std::nullopt;
      stack.push_back(result);
    } else {
      if (stack.size() < 2)
        return std::nullopt;
      Bits rhs = stack.back();
      stack.pop_back();
      Bits lhs = stack.back();
      stack.pop_back();
      Bits result = f.binary(opcode, lhs, rhs, bits, sign);
      if (!result.value)
        return std::nullopt;
      stack.push_back(result);
    }
  }
  if (!stack.empty())
    return std::nullopt;
  builder.prepare(roots, boundaries, descending);
  // Retain all random inputs, including unconstrained fields. Everything else
  // is reachable from an atom root; unused captures and replaced arithmetic
  // gates consume no target bytes or specialization work.
  auto nodes = builder.circuit.getNodes();
  std::vector<bool> reached(nodes.size(), false);
  std::vector<ID> pending;
  for (auto root : roots)
    pending.push_back(root.second);
  while (!pending.empty()) {
    ID index = pending.back() / 2;
    pending.pop_back();
    if (reached[index])
      continue;
    reached[index] = true;
    if (nodes[index].opcode == PlanOpcode::And) {
      pending.push_back(nodes[index].a);
      pending.push_back(nodes[index].b);
    }
  }
  BooleanCircuit compact;
  std::vector<ID> rewritten(nodes.size());
  auto handle = [&](ID id) { return rewritten[id / 2] ^ (id & 1); };
  for (uint32_t index = 1; index != nodes.size(); ++index) {
    const auto &node = nodes[index];
    if (!reached[index] && node.opcode != PlanOpcode::RandomBit)
      continue;
    rewritten[index] = node.opcode == PlanOpcode::And
                           ? compact.conjunction(handle(node.a), handle(node.b))
                           : compact.input(node.opcode, node.a, node.b);
  }
  for (auto &root : roots)
    root.second = handle(root.second);
  std::vector<uint8_t> plan;
  auto append32 = [&](uint32_t value) {
    for (unsigned byte = 0; byte != 4; ++byte)
      plan.push_back(static_cast<uint8_t>(value >> (byte * 8)));
  };
  auto append64 = [&](uint64_t value) {
    for (unsigned byte = 0; byte != 8; ++byte)
      plan.push_back(static_cast<uint8_t>(value >> (byte * 8)));
  };
  append32(random::samplerPlanMagic);
  append32(random::samplerPlanVersion);
  append32(static_cast<uint32_t>(compact.getNodes().size()));
  append32(static_cast<uint32_t>(roots.size()));
  append32(width);
  append32(static_cast<uint32_t>(captures.size()));
  append64(programSize);
  for (const auto &node : compact.getNodes()) {
    append32(static_cast<uint32_t>(node.opcode));
    append32(node.a);
    append32(node.b);
    append32(0);
  }
  for (auto [instruction, root] : roots) {
    append32(instruction);
    append32(root);
  }
  std::vector<PlanBuilder::FreeField> freeFields;
  for (const auto &free : builder.freeFields)
    if (reached[free.node / 2])
      freeFields.push_back({handle(free.node), free.offset, free.width});
  if (!freeFields.empty()) {
    append32(random::samplerFreeFieldMagic);
    append32(static_cast<uint32_t>(freeFields.size()));
    for (const auto &free : freeFields) {
      append32(free.node);
      append32(free.offset);
      append32(free.width);
      append32(0);
    }
  }
  size_t planSize = plan.size();
  append32(random::samplerTrailerMagic);
  append32(random::samplerPlanVersion);
  append64(planSize);
  return plan;
}
} // namespace obelisk::solver
#else
namespace obelisk::solver {
std::optional<std::vector<uint8_t>>
compileRandomSamplerPlan(const uint8_t *, size_t,
                         const std::vector<uint32_t> &) {
  return std::nullopt;
}
} // namespace obelisk::solver
#endif
