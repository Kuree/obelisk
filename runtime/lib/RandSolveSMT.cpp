//===- RandSolveSMT.cpp - Runtime Z3 constraint completion
//------------------===//

#include "RandSolveSMT.h"
#include "obelisk/Runtime/Runtime.h"

#ifdef OBELISK_RUNTIME_Z3
#include "RandModelCount.h"
#include <z3.h>

#include <algorithm>
#include <array>
#include <unordered_set>
#include <vector>

namespace obelisk::runtime {
namespace {

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
};

class Formula {
public:
  Formula() {
    Z3_config config = Z3_mk_config();
    context = Z3_mk_context(config);
    Z3_del_config(config);
    Z3_set_error_handler(context, [](Z3_context, Z3_error_code) {});
    solver = Z3_mk_solver(context);
    Z3_solver_inc_ref(context, solver);
  }

  ~Formula() {
    Z3_solver_dec_ref(context, solver);
    Z3_del_context(context);
  }

  Formula(const Formula &) = delete;
  Formula &operator=(const Formula &) = delete;

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
    if (input.width == width)
      return input.value;
    if (input.width > width)
      return Z3_mk_extract(context, width - 1, 0, input.value);
    return signedValue
               ? Z3_mk_sign_ext(context, width - input.width, input.value)
               : Z3_mk_zero_ext(context, width - input.width, input.value);
  }

  Z3_ast truth(Bits input) {
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

  void require(Z3_ast predicate) {
    Z3_solver_assert(context, solver, predicate);
  }

  Z3_lbool check() {
    Z3_lbool result = Z3_solver_check(context, solver);
    return Z3_get_error_code(context) == Z3_OK ? result : Z3_L_UNDEF;
  }

  Bits unary(uint8_t opcode, Bits input, unsigned width, bool sign) {
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
    case OBELISK_RT_RANDOM_COUNT_ONES_V1:
      result = constant(0, width);
      for (unsigned bit = 0; bit != input.width; ++bit) {
        Bits selected{Z3_mk_extract(context, bit, bit, input.value), 1};
        result = Z3_mk_bvadd(context, result, resize(selected, width));
      }
      break;
    case OBELISK_RT_RANDOM_CLOG2_V1:
      result = constant(0, width);
      for (unsigned bit = 0; bit != input.width; ++bit) {
        std::vector<uint64_t> threshold((input.width - 1) / 64 + 1, 0);
        threshold[bit / 64] = uint64_t{1} << (bit % 64);
        result = Z3_mk_ite(context,
                           Z3_mk_bvugt(context, input.value,
                                       words(threshold.data(), input.width)),
                           constant(bit + 1, width), result);
      }
      break;
    default:
      break;
    }
    return {result, width};
  }

  Bits binary(uint8_t opcode, Bits lhs, Bits rhs, unsigned width, bool sign) {
    unsigned commonWidth = std::max(lhs.width, rhs.width);
    Z3_ast a = resize(lhs, commonWidth, sign);
    Z3_ast b = resize(rhs, commonWidth, sign);
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

  /// Select the first unsigned value in cyclic order from the start. For
  /// fixed inactive bits this is the same order as compressed-mask traversal.
  bool minimum(Z3_ast variable, unsigned width, uint64_t *output) {
    for (unsigned bit = width; bit-- != 0;) {
      Z3_ast selected = Z3_mk_extract(context, bit, bit, variable);
      Z3_ast zero = Z3_mk_eq(context, selected, constant(0, 1));
      Z3_solver_push(context, solver);
      require(zero);
      Z3_lbool result = check();
      if (result == Z3_L_UNDEF)
        return false;
      if (result == Z3_L_FALSE) {
        Z3_solver_pop(context, solver, 1);
        require(Z3_mk_not(context, zero));
      }
    }
    if (check() != Z3_L_TRUE)
      return false;
    return model(variable, width, output);
  }

  bool model(Z3_ast variable, unsigned width, uint64_t *output) {
    Z3_model model = Z3_solver_get_model(context, solver);
    Z3_model_inc_ref(context, model);
    bool valid = true;
    for (unsigned offset = 0; valid && offset < width; offset += 64) {
      unsigned count = std::min(64u, width - offset);
      Z3_ast selected =
          Z3_mk_extract(context, offset + count - 1, offset, variable);
      Z3_ast value = nullptr;
      valid = Z3_model_eval(context, model, selected, true, &value) &&
              Z3_get_numeral_uint64(context, value, output + offset / 64);
    }
    Z3_model_dec_ref(context, model);
    return valid;
  }

  Z3_ast packBits(const std::vector<Z3_ast> &bits) {
    std::vector<Z3_ast> pieces;
    for (Z3_ast bit : bits)
      pieces.push_back(Z3_mk_ite(context, bit, constant(1, 1), constant(0, 1)));
    while (pieces.size() != 1) {
      size_t next = 0;
      for (size_t index = 0; index < pieces.size(); index += 2)
        pieces[next++] =
            index + 1 == pieces.size()
                ? pieces[index]
                : Z3_mk_concat(context, pieces[index + 1], pieces[index]);
      pieces.resize(next);
    }
    return pieces.front();
  }

  bool sampleComponent(const std::vector<Z3_ast> &bits, Z3_ast predicate,
                       obelisk_rt_random_state_v1 &state,
                       std::vector<bool> &selected) {
    unsigned width = bits.size();
    Z3_ast variable = packBits(bits);
    // Sparse nonlinear components need not construct a potentially large
    // decision diagram. Projection blocks only these bits, so unrelated free
    // fields cannot turn four roots into an apparent unbounded solution set.
    std::vector<std::vector<uint64_t>> assignments;
    Z3_solver_push(context, solver);
    bool complete = false;
    bool valid = true;
    for (unsigned index = 0; index != 17; ++index) {
      Z3_lbool result = check();
      if (result == Z3_L_FALSE) {
        complete = true;
        break;
      }
      std::vector<uint64_t> value((uint64_t(width) + 63) / 64, 0);
      if (result != Z3_L_TRUE || !model(variable, width, value.data())) {
        valid = false;
        break;
      }
      require(Z3_mk_not(
          context, Z3_mk_eq(context, variable, words(value.data(), width))));
      assignments.push_back(std::move(value));
    }
    Z3_solver_pop(context, solver, 1);
    if (!valid)
      return false;
    if (complete) {
      if (assignments.empty())
        return false;
      std::sort(assignments.begin(), assignments.end(),
                [](const auto &a, const auto &b) {
                  return std::lexicographical_compare(a.rbegin(), a.rend(),
                                                      b.rbegin(), b.rend());
                });
      uint64_t index = 0;
      if (assignments.size() != 1 &&
          obelisk_rt_v1_random_state_bounded(&state, assignments.size(),
                                             &index) != OBELISK_RT_OK)
        return false;
      selected.resize(width);
      for (unsigned bit = 0; bit != width; ++bit)
        selected[bit] = (assignments[index][bit / 64] >> (bit % 64)) & 1;
      return true;
    }
    ModelCounter counter(context, bits);
    if (!counter.build(predicate))
      return false;
    ModelCount count = counter.count();
    if (count.zero())
      return false;
    selected = counter.assignment(ModelCount::randomBelow(count, state));
    return true;
  }

  bool uniform(unsigned width, const std::vector<Z3_ast> &bits,
               const std::vector<unsigned> &physicalBits, const uint64_t *start,
               const uint64_t *mutableMask, obelisk_rt_random_state_v1 &state,
               uint64_t *output) {
    Z3_ast_vector assertions = Z3_solver_get_assertions(context, solver);
    Z3_ast_vector_inc_ref(context, assertions);
    std::vector<Z3_ast> pending;
    for (unsigned index = 0; index != Z3_ast_vector_size(context, assertions);
         ++index)
      pending.push_back(
          Z3_simplify(context, Z3_ast_vector_get(context, assertions, index)));
    Z3_ast_vector_dec_ref(context, assertions);

    // The legal set of disjoint components is their Cartesian product.
    // Sampling each component uniformly therefore gives an exact joint draw.
    std::unordered_map<Z3_ast, size_t> input;
    std::vector<size_t> parents(bits.size());
    for (size_t index = 0; index != bits.size(); ++index) {
      input.emplace(bits[index], index);
      parents[index] = index;
    }
    auto root = [&](size_t index) {
      size_t result = index;
      while (parents[result] != result)
        result = parents[result];
      while (parents[index] != index) {
        size_t next = parents[index];
        parents[index] = result;
        index = next;
      }
      return result;
    };
    struct Predicate {
      Z3_ast value;
      std::vector<size_t> support;
    };
    std::vector<Predicate> predicates;
    while (!pending.empty()) {
      Z3_ast value = pending.back();
      pending.pop_back();
      Z3_app app = Z3_to_app(context, value);
      unsigned arity = Z3_get_app_num_args(context, app);
      if (Z3_get_decl_kind(context, Z3_get_app_decl(context, app)) ==
          Z3_OP_AND) {
        for (unsigned index = 0; index != arity; ++index)
          pending.push_back(Z3_get_app_arg(context, app, index));
        continue;
      }
      std::vector<size_t> support;
      std::unordered_set<Z3_ast> visited;
      std::vector<Z3_ast> walk{value};
      while (!walk.empty()) {
        Z3_ast next = walk.back();
        walk.pop_back();
        if (!visited.insert(next).second)
          continue;
        auto found = input.find(next);
        if (found != input.end()) {
          support.push_back(found->second);
          continue;
        }
        if (Z3_get_ast_kind(context, next) == Z3_NUMERAL_AST)
          continue;
        if (Z3_get_ast_kind(context, next) != Z3_APP_AST)
          return false;
        Z3_app nested = Z3_to_app(context, next);
        for (unsigned index = 0; index != Z3_get_app_num_args(context, nested);
             ++index)
          walk.push_back(Z3_get_app_arg(context, nested, index));
      }
      if (support.empty())
        continue;
      for (size_t index = 1; index != support.size(); ++index) {
        size_t a = root(support.front()), b = root(support[index]);
        parents[std::max(a, b)] = std::min(a, b);
      }
      predicates.push_back({value, std::move(support)});
    }
    std::vector<std::vector<size_t>> components(bits.size());
    std::vector<std::vector<Z3_ast>> constraints(bits.size());
    for (const Predicate &predicate : predicates)
      constraints[root(predicate.support.front())].push_back(predicate.value);
    std::vector<size_t> freeBits;
    for (size_t index = 0; index != bits.size(); ++index) {
      size_t component = root(index);
      if (constraints[component].empty())
        freeBits.push_back(index);
      else
        components[component].push_back(index);
    }
    obelisk_rt_random_state_v1 nextState = state;
    std::vector<bool> selected(bits.size(), false);
    for (size_t index = 0; index != components.size(); ++index) {
      if (components[index].empty())
        continue;
      std::vector<Z3_ast> inputs;
      for (size_t bit : components[index])
        inputs.push_back(bits[bit]);
      Z3_ast predicate = Z3_mk_and(context, constraints[index].size(),
                                   constraints[index].data());
      std::vector<bool> local;
      if (!sampleComponent(inputs, predicate, nextState, local))
        return false;
      for (size_t bit = 0; bit != local.size(); ++bit)
        selected[components[index][bit]] = local[bit];
    }
    uint64_t draw = 0;
    for (size_t index = 0; index != freeBits.size(); ++index) {
      if (index % 64 == 0)
        draw = obelisk_rt_v1_random_state_next64(&nextState);
      selected[freeBits[index]] = (draw >> (index % 64)) & 1;
    }
    for (size_t index = 0; index != (uint64_t(width) + 63) / 64; ++index)
      output[index] = start[index] & ~mutableMask[index];
    for (size_t index = 0; index != selected.size(); ++index)
      if (selected[index])
        output[physicalBits[index] / 64] |= uint64_t{1}
                                            << (physicalBits[index] % 64);
    if (width % 64)
      output[(width - 1) / 64] &= (uint64_t{1} << (width % 64)) - 1;
    state = nextState;
    return true;
  }

  Z3_context context = nullptr;
  Z3_solver solver = nullptr;
};

} // namespace

SMTCompletion
completeRandomProgram(const uint8_t *program, size_t programSize,
                      const uint64_t *start, const uint64_t *mutableMask,
                      size_t assignmentWords, uint64_t constraintMask,
                      const uint64_t *captures, const uint32_t *captureWidths,
                      size_t captureCount, uint64_t *assignment,
                      obelisk_rt_random_state_v1 *randomState) {
  bool wide = read16(program + 4) == OBELISK_RT_RANDOM_PROGRAM_VERSION_V2;
  unsigned header = wide ? OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE_V2
                         : OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE;
  unsigned width = read32(program + 8);
  unsigned instructions = read32(program + 12);
  unsigned flags = read32(program + 20);
  // Ordered and weighted sampling need their own conditional projection
  // plans; an arbitrary satisfiable model would change those distributions.
  if (flags & (OBELISK_RT_RANDOM_PROGRAM_HAS_SOLVE_BEFORE |
               OBELISK_RT_RANDOM_PROGRAM_HAS_DIST))
    return SMTCompletion::Unavailable;
  if (width == 0 || assignmentWords != (uint64_t(width) + 63) / 64)
    return SMTCompletion::Unavailable;
  bool uniform = (flags & OBELISK_RT_RANDOM_PROGRAM_SMT_UNIFORM) != 0;
  if (uniform && !randomState)
    return SMTCompletion::Unavailable;
  Formula f;
  Z3_ast variable = nullptr;
  std::vector<Z3_ast> booleanBits;
  std::vector<unsigned> physicalBits;
  if (uniform) {
    // Interleave fields from their least significant bits. Carry chains and
    // relations between equally sized fields then share prefixes instead of
    // expanding every value of one field before visiting another field.
    std::vector<unsigned> boundaries{0, width};
    for (unsigned index = 0; index != instructions; ++index) {
      const uint8_t *p = program + header + size_t(index) * 16;
      if (p[0] != OBELISK_RT_RANDOM_PUSH_VARIABLE_V1)
        continue;
      unsigned bits = wide ? read32(p + 4) : p[1];
      unsigned offset = read32(p + (wide ? 8 : 4));
      boundaries.push_back(offset);
      boundaries.push_back(offset + bits);
    }
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()),
                     boundaries.end());
    std::vector<Z3_ast> pieces(width);
    unsigned maximum = 0;
    for (size_t field = 1; field != boundaries.size(); ++field)
      maximum = std::max(maximum, boundaries[field] - boundaries[field - 1]);
    for (unsigned column = 0; column != maximum; ++column)
      for (size_t field = 1; field != boundaries.size(); ++field) {
        unsigned bit = boundaries[field - 1] + column;
        if (bit >= boundaries[field])
          continue;
        if (((mutableMask[bit / 64] >> (bit % 64)) & 1) == 0) {
          pieces[bit] = f.constant((start[bit / 64] >> (bit % 64)) & 1, 1);
          continue;
        }
        Z3_ast input = Z3_mk_const(f.context, Z3_mk_int_symbol(f.context, bit),
                                   Z3_mk_bool_sort(f.context));
        booleanBits.push_back(input);
        physicalBits.push_back(bit);
        pieces[bit] =
            Z3_mk_ite(f.context, input, f.constant(1, 1), f.constant(0, 1));
      }
    // A balanced concatenation also avoids a width-deep Z3 expression stack.
    while (pieces.size() != 1) {
      size_t next = 0;
      for (size_t index = 0; index < pieces.size(); index += 2)
        pieces[next++] =
            index + 1 == pieces.size()
                ? pieces[index]
                : Z3_mk_concat(f.context, pieces[index + 1], pieces[index]);
      pieces.resize(next);
    }
    variable = pieces.front();
  } else {
    variable =
        Z3_mk_const(f.context, Z3_mk_string_symbol(f.context, "assignment"),
                    Z3_mk_bv_sort(f.context, width));
  }
  Z3_ast fixedMask = Z3_mk_bvnot(f.context, f.words(mutableMask, width));
  f.require(Z3_mk_eq(f.context, Z3_mk_bvand(f.context, variable, fixedMask),
                     Z3_mk_bvand(f.context, f.words(start, width), fixedMask)));
  std::vector<size_t> captureOffsets(captureCount);
  for (size_t capture = 1; capture < captureCount; ++capture)
    captureOffsets[capture] =
        captureOffsets[capture - 1] +
        (captureWidths ? (uint64_t(captureWidths[capture - 1]) + 63) / 64 : 1);
  std::vector<Bits> stack;
  std::array<Z3_ast, 64> soft{};
  size_t literals = header + size_t(instructions) * 16;
  for (unsigned index = 0; index != instructions; ++index) {
    const uint8_t *p = program + header + size_t(index) * 16;
    unsigned opcode = p[0];
    unsigned bits = wide ? read32(p + 4) : p[1];
    bool sign =
        ((wide ? p[1] : p[2]) & OBELISK_RT_RANDOM_INSTRUCTION_SIGNED) != 0;
    unsigned operand = read32(p + (wide ? 8 : 4));
    uint64_t auxiliary = wide ? read32(p + 12) : read64(p + 8);
    if (opcode == OBELISK_RT_RANDOM_PUSH_VARIABLE_V1) {
      stack.push_back(
          {Z3_mk_extract(f.context, operand + bits - 1, operand, variable),
           bits});
    } else if (opcode == OBELISK_RT_RANDOM_PUSH_CAPTURE_V1) {
      if (operand >= captureCount)
        return SMTCompletion::Unavailable;
      unsigned captureWidth = captureWidths ? captureWidths[operand] : 64;
      stack.push_back(
          {f.resize(
               {f.words(captures + (wide ? captureOffsets[operand] : operand),
                        captureWidth),
                captureWidth},
               bits),
           bits});
    } else if (opcode == OBELISK_RT_RANDOM_PUSH_LITERAL_V1) {
      if (wide) {
        std::vector<uint64_t> words((uint64_t(bits) + 63) / 64);
        for (size_t word = 0; word != words.size(); ++word)
          words[word] = read64(program + literals + (auxiliary + word) * 8);
        stack.push_back({f.words(words.data(), bits), bits});
      } else {
        stack.push_back({f.constant(auxiliary, bits), bits});
      }
    } else if (opcode == OBELISK_RT_RANDOM_PUSH_CONSTRAINT_ENABLED_V1) {
      stack.push_back(
          {f.constant(((constraintMask >> operand) & 1) == 0, 1), 1});
    } else if (opcode == OBELISK_RT_RANDOM_END_HARD_V1 ||
               opcode == OBELISK_RT_RANDOM_END_SOFT_V1) {
      if (stack.size() != 1)
        return SMTCompletion::Unavailable;
      bool enabled = operand == OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1 ||
                     (constraintMask & (uint64_t{1} << operand)) == 0;
      Z3_ast predicate =
          enabled ? f.truth(stack.back()) : Z3_mk_true(f.context);
      if (opcode == OBELISK_RT_RANDOM_END_HARD_V1)
        f.require(predicate);
      else
        soft[auxiliary] = predicate;
      stack.clear();
    } else if (opcode == OBELISK_RT_RANDOM_SELECT_V1) {
      if (stack.size() < 3)
        return SMTCompletion::Unavailable;
      Bits otherwise = stack.back();
      stack.pop_back();
      Bits selected = stack.back();
      stack.pop_back();
      Bits condition = stack.back();
      stack.pop_back();
      stack.push_back({Z3_mk_ite(f.context, f.truth(condition),
                                 f.resize(selected, bits, sign),
                                 f.resize(otherwise, bits, sign)),
                       bits});
    } else if ((opcode >= OBELISK_RT_RANDOM_CAST_V1 &&
                opcode <= OBELISK_RT_RANDOM_LOGICAL_NOT_V1) ||
               opcode == OBELISK_RT_RANDOM_COUNT_ONES_V1 ||
               opcode == OBELISK_RT_RANDOM_CLOG2_V1) {
      if (stack.empty())
        return SMTCompletion::Unavailable;
      Bits input = stack.back();
      stack.pop_back();
      Bits result = f.unary(opcode, input, bits, sign);
      if (!result.value)
        return SMTCompletion::Unavailable;
      stack.push_back(result);
    } else {
      if (stack.size() < 2)
        return SMTCompletion::Unavailable;
      Bits rhs = stack.back();
      stack.pop_back();
      Bits lhs = stack.back();
      stack.pop_back();
      Bits result = f.binary(opcode, lhs, rhs, bits, sign);
      if (!result.value)
        return SMTCompletion::Unavailable;
      stack.push_back(result);
    }
  }
  if (flags & OBELISK_RT_RANDOM_PROGRAM_HAS_DOMAINS) {
    size_t offset = literals + (wide ? size_t(read32(program + 24)) * 8 : 0);
    if (offset + 8 > programSize)
      return SMTCompletion::Unavailable;
    unsigned groups = read32(program + offset);
    unsigned records = read32(program + offset + 4);
    std::vector<Z3_ast> domains(groups, Z3_mk_false(f.context));
    std::vector<bool> active(groups, false);
    for (unsigned index = 0; index != records; ++index) {
      const uint8_t *p = program + offset + 8 + size_t(index) * 32;
      unsigned group = read32(p);
      unsigned target = read32(p + 4);
      unsigned bits = read16(p + 8);
      for (unsigned bit = target; bit != target + bits; ++bit)
        active[group] =
            active[group] || ((mutableMask[bit / 64] >> (bit % 64)) & 1) != 0;
      Z3_ast field =
          Z3_mk_extract(f.context, target + bits - 1, target, variable);
      Z3_ast match = Z3_mk_eq(
          f.context,
          Z3_mk_bvand(f.context, field, f.constant(read64(p + 16), bits)),
          f.constant(read64(p + 24), bits));
      domains[group] = f.disjunction(domains[group], match);
    }
    for (unsigned group = 0; group != groups; ++group)
      if (active[group])
        f.require(domains[group]);
  }
  Z3_lbool result = f.check();
  if (result != Z3_L_TRUE)
    return result == Z3_L_FALSE ? SMTCompletion::Unsatisfiable
                                : SMTCompletion::Unknown;
  for (size_t priority = soft.size(); priority-- != 0;) {
    if (!soft[priority])
      continue;
    Z3_solver_push(f.context, f.solver);
    f.require(soft[priority]);
    result = f.check();
    if (result == Z3_L_UNDEF)
      return SMTCompletion::Unknown;
    if (result == Z3_L_FALSE)
      Z3_solver_pop(f.context, f.solver, 1);
  }
  if (uniform)
    return f.uniform(width, booleanBits, physicalBits, start, mutableMask,
                     *randomState, assignment)
               ? SMTCompletion::Satisfiable
               : SMTCompletion::Unknown;
  Z3_solver_push(f.context, f.solver);
  f.require(Z3_mk_bvuge(f.context, variable, f.words(start, width)));
  result = f.check();
  if (result == Z3_L_UNDEF)
    return SMTCompletion::Unknown;
  if (result == Z3_L_FALSE)
    Z3_solver_pop(f.context, f.solver, 1);
  return f.minimum(variable, width, assignment) ? SMTCompletion::Satisfiable
                                                : SMTCompletion::Unknown;
}

} // namespace obelisk::runtime

#else

namespace obelisk::runtime {
SMTCompletion completeRandomProgram(const uint8_t *, size_t, const uint64_t *,
                                    const uint64_t *, size_t, uint64_t,
                                    const uint64_t *, const uint32_t *, size_t,
                                    uint64_t *, obelisk_rt_random_state_v1 *) {
  return SMTCompletion::Unavailable;
}
} // namespace obelisk::runtime

#endif
