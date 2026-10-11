//===- RandModelCount.h - Exact finite model counts and sampling ----------===//

#pragma once

#include "obelisk/Runtime/Runtime.h"

#include <z3.h>

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace obelisk::runtime {

/// Nonnegative arbitrary-precision cardinalities. Counts include 2^N for a
/// full N-bit domain, so a fixed-width assignment integer is insufficient.
class ModelCount {
public:
  explicit ModelCount(uint64_t value = 0) {
    if (value)
      words.push_back(value);
  }

  static ModelCount powerOfTwo(unsigned bit) {
    ModelCount value;
    value.words.resize(size_t(bit) / 64 + 1, 0);
    value.words[bit / 64] = uint64_t{1} << (bit % 64);
    return value;
  }

  bool zero() const { return words.empty(); }

  int compare(const ModelCount &other) const {
    if (words.size() != other.words.size())
      return words.size() < other.words.size() ? -1 : 1;
    for (size_t index = words.size(); index-- != 0;)
      if (words[index] != other.words[index])
        return words[index] < other.words[index] ? -1 : 1;
    return 0;
  }

  ModelCount shifted(unsigned bits) const {
    if (zero())
      return ModelCount();
    ModelCount result;
    size_t offset = bits / 64;
    unsigned shift = bits % 64;
    result.words.resize(words.size() + offset + (shift != 0), 0);
    for (size_t index = 0; index != words.size(); ++index) {
      result.words[index + offset] |= words[index] << shift;
      if (shift)
        result.words[index + offset + 1] |= words[index] >> (64 - shift);
    }
    result.normalize();
    return result;
  }

  void add(const ModelCount &other) {
    words.resize(std::max(words.size(), other.words.size()), 0);
    uint64_t carry = 0;
    for (size_t index = 0; index != words.size(); ++index) {
      uint64_t rhs = index < other.words.size() ? other.words[index] : 0;
      uint64_t first = words[index] + rhs;
      uint64_t next = first + carry;
      carry = (first < words[index]) || (next < first);
      words[index] = next;
    }
    if (carry)
      words.push_back(carry);
  }

  /// The caller establishes *this >= other.
  void subtract(const ModelCount &other) {
    uint64_t borrow = 0;
    for (size_t index = 0; index != words.size(); ++index) {
      uint64_t rhs = index < other.words.size() ? other.words[index] : 0;
      uint64_t first = words[index] - rhs;
      uint64_t next = first - borrow;
      borrow = (words[index] < rhs) || (first < borrow);
      words[index] = next;
    }
    normalize();
  }

  /// Rejection of ranks outside the exact cardinality avoids modulo bias.
  /// A singleton needs no draw. Rejection probability is always below 1/2.
  static ModelCount randomBelow(const ModelCount &bound,
                                obelisk_rt_random_state_v1 &state) {
    ModelCount maximum = bound;
    maximum.subtract(ModelCount(1));
    if (maximum.zero())
      return ModelCount();
    unsigned highBits = 64 - __builtin_clzll(maximum.words.back());
    ModelCount rank;
    do {
      rank.words.resize(maximum.words.size());
      for (uint64_t &word : rank.words)
        word = obelisk_rt_v1_random_state_next64(&state);
      if (highBits != 64)
        rank.words.back() &= (uint64_t{1} << highBits) - 1;
      rank.normalize();
    } while (rank.compare(bound) >= 0);
    return rank;
  }

private:
  void normalize() {
    while (!words.empty() && words.back() == 0)
      words.pop_back();
  }

  std::vector<uint64_t> words;
};

/// Count a Z3 Boolean circuit with a reduced ordered decision diagram. Z3
/// provides the bit-vector semantics and bit blasting; the diagram supplies
/// exact counts, which a satisfiability model alone cannot provide. Only the
/// caller's active assignment bits are variables, so auxiliary bits and frozen
/// state never multiply the count. Iterative graph walks avoid WASM stack
/// limits.
class ModelCounter {
public:
  using ID = size_t;

  ModelCounter(Z3_context context, const std::vector<Z3_ast> &variables)
      : context(context), variableCount(variables.size()),
        fixed(variableCount, false), fixedValues(variableCount, false) {
    nodes.push_back({variableCount, 0, 0});
    nodes.push_back({variableCount, 1, 1});
    for (size_t index = 0; index != variables.size(); ++index)
      inputs.emplace(variables[index], makeNode(index, 0, 1));
  }

  bool build(Z3_ast predicate) {
    // The bit-blast tactic preserves the existing Boolean input symbols and
    // does not introduce Tseitin variables requiring projected counting.
    Z3_goal goal = Z3_mk_goal(context, false, false, false);
    Z3_goal_inc_ref(context, goal);
    Z3_goal_assert(context, goal, predicate);
    Z3_tactic simplify = Z3_mk_tactic(context, "simplify");
    Z3_tactic_inc_ref(context, simplify);
    Z3_tactic blast = Z3_mk_tactic(context, "bit-blast");
    Z3_tactic_inc_ref(context, blast);
    Z3_tactic first = Z3_tactic_and_then(context, simplify, blast);
    Z3_tactic_inc_ref(context, first);
    Z3_tactic tactic = Z3_tactic_and_then(context, first, simplify);
    Z3_tactic_inc_ref(context, tactic);
    Z3_apply_result result = Z3_tactic_apply(context, tactic, goal);
    bool valid = result && Z3_get_error_code(context) == Z3_OK;
    if (result)
      Z3_apply_result_inc_ref(context, result);
    valid = valid && Z3_apply_result_get_num_subgoals(context, result) == 1;
    root = 1;
    if (valid) {
      Z3_goal output = Z3_apply_result_get_subgoal(context, result, 0);
      for (unsigned index = 0; valid && index != Z3_goal_size(context, output);
           ++index) {
        ID circuit = 0;
        valid = convert(Z3_goal_formula(context, output, index), circuit);
        if (valid)
          root = conjunction(root, circuit);
      }
    }
    if (result)
      Z3_apply_result_dec_ref(context, result);
    Z3_tactic_dec_ref(context, tactic);
    Z3_tactic_dec_ref(context, first);
    Z3_tactic_dec_ref(context, blast);
    Z3_tactic_dec_ref(context, simplify);
    Z3_goal_dec_ref(context, goal);
    if (!valid)
      return false;

    project(std::vector<bool>(variableCount, true));
    return true;
  }

  ModelCount count() const {
    return counts[viewRoot].shifted(levels[nodes[viewRoot].variable]);
  }

  /// Existentially remove other fields before counting a solve-order layer.
  /// A legal projected value contributes one choice regardless of how many
  /// completions it has. Previous choices remain fixed in the original graph.
  void project(const std::vector<bool> &keep) {
    sampled.resize(variableCount);
    levels.assign(variableCount + 1, 0);
    for (size_t index = 0; index != variableCount; ++index) {
      sampled[index] = keep[index] && !fixed[index];
      levels[index + 1] = levels[index] + sampled[index];
    }
    auto reached = reachable(root);
    std::vector<ID> projected(nodes.size());
    projected[1] = 1;
    for (ID id = 2; id != reached.size(); ++id) {
      if (!reached[id])
        continue;
      Node node = nodes[id];
      projected[id] =
          sampled[node.variable]
              ? makeNode(node.variable, projected[node.low],
                         projected[node.high])
              : disjunction(projected[node.low], projected[node.high]);
    }
    viewRoot = projected[root];
    recount();
  }

  /// Cofactor the original relation by an accepted layer, preserving its
  /// constraints on later fields. Projecting this relation again yields the
  /// next conditional distribution rather than the previous projection.
  void restrict(const std::vector<bool> &value,
                const std::vector<bool> &selected) {
    auto reached = reachable(root);
    std::vector<ID> reduced(nodes.size());
    reduced[1] = 1;
    for (ID id = 2; id != reached.size(); ++id) {
      if (!reached[id])
        continue;
      Node node = nodes[id];
      reduced[id] =
          selected[node.variable]
              ? reduced[value[node.variable] ? node.high : node.low]
              : makeNode(node.variable, reduced[node.low], reduced[node.high]);
    }
    root = reduced[root];
    for (size_t index = 0; index != variableCount; ++index)
      if (selected[index]) {
        fixed[index] = true;
        fixedValues[index] = value[index];
      }
    project(std::vector<bool>(variableCount, true));
  }

  /// Each rank names one legal assignment exactly once. Skipped variables
  /// denote a full cube, whose cardinality is included rather than enumerated.
  std::vector<bool> assignment(ModelCount rank) const {
    std::vector<bool> value = fixedValues;
    ID current = viewRoot;
    for (size_t variable = 0; variable != variableCount; ++variable) {
      if (!sampled[variable])
        continue;
      const Node &node = nodes[current];
      bool skipped = variable < node.variable;
      ID low = skipped ? current : node.low;
      ModelCount lowCount = counts[low].shifted(levels[nodes[low].variable] -
                                                levels[variable] - 1);
      bool high = rank.compare(lowCount) >= 0;
      if (high) {
        rank.subtract(lowCount);
        value[variable] = true;
      }
      if (!skipped)
        current = high ? node.high : node.low;
    }
    return value;
  }

private:
  struct Node {
    size_t variable;
    ID low, high;
  };

  std::vector<bool> reachable(ID start) const {
    std::vector<bool> reached(nodes.size(), false);
    std::vector<ID> pending{start};
    while (!pending.empty()) {
      ID id = pending.back();
      pending.pop_back();
      if (reached[id])
        continue;
      reached[id] = true;
      if (id > 1) {
        pending.push_back(nodes[id].low);
        pending.push_back(nodes[id].high);
      }
    }
    return reached;
  }

  void recount() {
    auto reached = reachable(viewRoot);
    counts.resize(nodes.size());
    counts[0] = ModelCount();
    counts[1] = ModelCount(1);
    // A node is created after its children. Reduced nodes preserve that order.
    for (ID id = 2; id != nodes.size(); ++id) {
      if (!reached[id])
        continue;
      const Node &node = nodes[id];
      counts[id] = counts[node.low].shifted(levels[nodes[node.low].variable] -
                                            levels[node.variable] - 1);
      counts[id].add(counts[node.high].shifted(
          levels[nodes[node.high].variable] - levels[node.variable] - 1));
    }
  }

  struct Key {
    size_t a, b, c;
    bool operator==(const Key &other) const {
      return a == other.a && b == other.b && c == other.c;
    }
  };

  struct Hash {
    size_t operator()(const Key &key) const {
      size_t hash = std::hash<size_t>{}(key.a);
      for (size_t value : {key.b, key.c})
        hash ^= std::hash<size_t>{}(value) + size_t(0x9e3779b9) + (hash << 6) +
                (hash >> 2);
      return hash;
    }
  };

  ID makeNode(size_t variable, ID low, ID high) {
    if (low == high)
      return low;
    auto [found, inserted] =
        unique.emplace(Key{variable, low, high}, nodes.size());
    if (inserted)
      nodes.push_back({variable, low, high});
    return found->second;
  }

  bool resolved(Key key, ID &result) const {
    if (key.a <= 1 || key.b == key.c) {
      result = key.a == 0 ? key.c : key.b;
      return true;
    }
    if (key.b == 1 && key.c == 0) {
      result = key.a;
      return true;
    }
    auto found = cache.find(key);
    if (found == cache.end())
      return false;
    result = found->second;
    return true;
  }

  ID conditional(ID condition, ID yes, ID no) {
    Key initial{condition, yes, no};
    std::vector<Key> pending{initial};
    ID result = 0;
    while (!pending.empty()) {
      Key key = pending.back();
      if (resolved(key, result)) {
        pending.pop_back();
        continue;
      }
      size_t variable = std::min({nodes[key.a].variable, nodes[key.b].variable,
                                  nodes[key.c].variable});
      auto cofactor = [&](ID id, bool high) {
        const Node &node = nodes[id];
        return node.variable == variable ? (high ? node.high : node.low) : id;
      };
      Key low{cofactor(key.a, false), cofactor(key.b, false),
              cofactor(key.c, false)};
      Key high{cofactor(key.a, true), cofactor(key.b, true),
               cofactor(key.c, true)};
      ID lowResult = 0, highResult = 0;
      if (!resolved(low, lowResult)) {
        pending.push_back(low);
        continue;
      }
      if (!resolved(high, highResult)) {
        pending.push_back(high);
        continue;
      }
      result = makeNode(variable, lowResult, highResult);
      cache.emplace(key, result);
      pending.pop_back();
    }
    return result;
  }

  ID conjunction(ID a, ID b) { return conditional(a, b, 0); }
  ID disjunction(ID a, ID b) { return conditional(a, 1, b); }
  ID negate(ID a) { return conditional(a, 0, 1); }
  ID exclusiveOr(ID a, ID b) { return conditional(a, negate(b), b); }

  bool convert(Z3_ast predicate, ID &result) {
    std::vector<Z3_ast> pending{predicate};
    while (!pending.empty()) {
      Z3_ast value = pending.back();
      if (circuits.count(value)) {
        pending.pop_back();
        continue;
      }
      auto input = inputs.find(value);
      if (input != inputs.end()) {
        circuits.emplace(value, input->second);
        pending.pop_back();
        continue;
      }
      if (Z3_get_ast_kind(context, value) != Z3_APP_AST)
        return false;
      Z3_app app = Z3_to_app(context, value);
      unsigned arity = Z3_get_app_num_args(context, app);
      bool ready = true;
      for (unsigned index = 0; index != arity; ++index) {
        Z3_ast child = Z3_get_app_arg(context, app, index);
        if (!circuits.count(child)) {
          pending.push_back(child);
          ready = false;
          break;
        }
      }
      if (!ready)
        continue;
      auto argument = [&](unsigned index) {
        return circuits.at(Z3_get_app_arg(context, app, index));
      };
      Z3_decl_kind kind =
          Z3_get_decl_kind(context, Z3_get_app_decl(context, app));
      ID circuit = 0;
      switch (kind) {
      case Z3_OP_TRUE:
        circuit = 1;
        break;
      case Z3_OP_FALSE:
        break;
      case Z3_OP_AND:
      case Z3_OP_OR:
      case Z3_OP_XOR:
      case Z3_OP_XOR3:
        circuit = kind == Z3_OP_AND ? 1 : 0;
        for (unsigned index = 0; index != arity; ++index)
          circuit = kind == Z3_OP_AND  ? conjunction(circuit, argument(index))
                    : kind == Z3_OP_OR ? disjunction(circuit, argument(index))
                                       : exclusiveOr(circuit, argument(index));
        break;
      case Z3_OP_NOT:
        circuit = negate(argument(0));
        break;
      case Z3_OP_EQ:
      case Z3_OP_IFF:
        circuit = negate(exclusiveOr(argument(0), argument(1)));
        break;
      case Z3_OP_DISTINCT:
        if (arity != 2)
          return false;
        circuit = exclusiveOr(argument(0), argument(1));
        break;
      case Z3_OP_IMPLIES:
        circuit = conditional(argument(0), argument(1), 1);
        break;
      case Z3_OP_ITE:
        circuit = conditional(argument(0), argument(1), argument(2));
        break;
      case Z3_OP_CARRY:
        circuit = disjunction(
            conjunction(argument(0), argument(1)),
            conjunction(argument(2), disjunction(argument(0), argument(1))));
        break;
      default:
        return false;
      }
      circuits.emplace(value, circuit);
      pending.pop_back();
    }
    result = circuits.at(predicate);
    return true;
  }

  Z3_context context;
  size_t variableCount;
  ID root = 0;
  ID viewRoot = 0;
  std::vector<bool> fixed, fixedValues, sampled;
  std::vector<size_t> levels;
  std::vector<Node> nodes;
  std::vector<ModelCount> counts;
  std::unordered_map<Key, ID, Hash> unique, cache;
  std::unordered_map<Z3_ast, ID> inputs, circuits;
};

} // namespace obelisk::runtime
