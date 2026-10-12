//===- RandomSamplerZ3.cpp - Compile exact parameterized sampling plans ---===//

#include "../../runtime/lib/RandModelCount.h"
#include "obelisk/Runtime/RandomSamplerPlan.h"
#include "obelisk/Solver/ConstraintSolver.h"

#ifdef OBELISK_ENABLE_Z3
#include <algorithm>
#include <unordered_map>
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
};

class BitVectorFormula {
public:
  BitVectorFormula() {
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
      result = constant(0, width);
      Z3_ast lowerNonzero = Z3_mk_false(context);
      for (unsigned bit = 0; bit != input.width; ++bit) {
        Z3_ast selected =
            truth({Z3_mk_extract(context, bit, bit, input.value), 1});
        Z3_ast atBit =
            Z3_mk_ite(context, lowerNonzero, constant(uint64_t(bit) + 1, width),
                      constant(bit, width));
        result = Z3_mk_ite(context, selected, atBit, result);
        lowerNonzero = disjunction(lowerNonzero, selected);
      }
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

  Z3_context context = nullptr;
  std::unordered_map<Z3_ast, Bits> popcounts;
  struct CardinalityInputs {
    std::vector<Z3_ast> positive, negative;
    std::vector<int> coefficients;
  };
  std::unordered_map<Z3_ast, CardinalityInputs> cardinalityInputs;
};

inline bool bitBlastPredicate(Z3_context context, Z3_ast predicate,
                              std::vector<Z3_ast> &formulas) {
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
  Z3_tactic tactic = Z3_tactic_and_then(context, first, simplify);
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

/// Preserve named Boolean inputs through bit blasting. No auxiliary Boolean
/// is accepted: otherwise an existential witness could multiply a sample's
/// count, or be mistaken for a live state parameter.
class PlanBuilder {
public:
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
    if (!bitBlastPredicate(formula.context, predicate, formulas))
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
        for (unsigned index = 0; index != arity; ++index)
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
               const std::vector<unsigned> &boundaries) {
    // Each diagram attempt traverses the shared circuit. Bound the number of
    // attempts globally so many atoms cannot cause quadratic compiler work.
    // Skipping this optimization preserves the exact Boolean sampling plan.
    if (roots.size() > 32)
      return;
    auto nodes = circuit.getNodes();
    struct Input {
      uint32_t column, source;
    };
    std::vector<Input> inputs;
    std::vector<unsigned> columns(boundaries.back());
    for (size_t field = 1; field != boundaries.size(); ++field)
      for (unsigned bit = boundaries[field - 1]; bit != boundaries[field];
           ++bit)
        columns[bit] = bit - boundaries[field - 1];
    std::vector<std::vector<Input>> byColumn;
    for (uint32_t index = 1; index != nodes.size(); ++index) {
      const auto &node = nodes[index];
      if (node.opcode == PlanOpcode::And)
        continue;
      uint32_t column = 0;
      if (node.opcode == PlanOpcode::RandomBit)
        column = columns[node.a];
      else if (node.opcode == PlanOpcode::CaptureBit)
        column = node.b;
      if (byColumn.size() <= column)
        byColumn.resize(size_t(column) + 1);
      byColumn[column].push_back({column, index});
    }
    for (const auto &column : byColumn)
      inputs.insert(inputs.end(), column.begin(), column.end());
    // One bounded diagram per atom preserves independent function stages and
    // dynamic soft priorities. Joint satisfiability is handled after binding
    // the live parameters, without freezing one compiler-selected model.
    for (auto &root : roots) {
      std::vector<bool> reached(nodes.size(), false);
      std::vector<ID> pending{root.second};
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
      BooleanCircuit prepared;
      std::vector<ID> translated(nodes.size()), variables, original;
      for (const auto &input : inputs) {
        if (!reached[input.source])
          continue;
        translated[input.source] = prepared.input(
            PlanOpcode::RandomBit, input.source, variables.size());
        variables.push_back(translated[input.source]);
        original.push_back(input.source * 2);
      }
      auto handle = [&](ID id) { return translated[id / 2] ^ (id & 1); };
      for (uint32_t index = 1; index != nodes.size(); ++index)
        if (reached[index] && nodes[index].opcode == PlanOpcode::And)
          translated[index] = prepared.conjunction(handle(nodes[index].a),
                                                   handle(nodes[index].b));
      runtime::ModelCounter counter(prepared, variables);
      if (counter.prepareDiagram(handle(root.second), 4096, 32768))
        root.second = counter.exportDiagram(circuit, original);
    }
  }

private:
  int nextSymbol = 0;
  std::unordered_map<Z3_ast, ID> converted;
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
      unsigned bit = boundaries[field - 1] + column;
      randomBits[bit] = builder.input(PlanOpcode::RandomBit, bit, ordinal++);
      if (bit + 1 == boundaries[field])
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
  }
  std::unordered_map<unsigned, Z3_ast> enabled;
  std::vector<Bits> stack;
  std::vector<Bits> temporaries;
  std::vector<std::pair<unsigned, ID>> roots;
  for (unsigned index = 0; index != instructions; ++index) {
    const uint8_t *p = program + header + size_t(index) * 16;
    unsigned opcode = p[0], bits = wide ? read32(p + 4) : p[1];
    uint8_t instructionFlags = wide ? p[1] : p[2];
    bool sign = (instructionFlags & OBELISK_RT_RANDOM_INSTRUCTION_SIGNED) != 0;
    unsigned operand = read32(p + (wide ? 8 : 4));
    uint64_t auxiliary = wide ? read32(p + 12) : read64(p + 8);
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
      stack.push_back(
          {Z3_mk_ite(f.context, f.truth(condition), f.resize(yes, bits, sign),
                     f.resize(no, bits, sign)),
           bits});
    } else if ((opcode >= OBELISK_RT_RANDOM_CAST_V1 &&
                opcode <= OBELISK_RT_RANDOM_LOGICAL_NOT_V1) ||
               opcode == OBELISK_RT_RANDOM_COUNT_ONES_V1 ||
               opcode == OBELISK_RT_RANDOM_CLOG2_V1) {
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
  builder.prepare(roots, boundaries);
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
