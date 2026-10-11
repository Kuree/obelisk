//===- RandModelCount.h - Exact finite model counts and sampling ----------===//

#pragma once

#include "obelisk/Runtime/Runtime.h"

#include "obelisk/Runtime/RandomSamplerPlan.h"

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace obelisk::runtime {

/// Nonnegative arbitrary-precision cardinalities and integral mass factors.
/// Counts include 2^N for a full N-bit domain, so a fixed-width assignment
/// integer is insufficient.
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

  static ModelCount fromWords(const uint64_t *value, size_t count) {
    ModelCount result;
    if (count)
      result.words.assign(value, value + count);
    result.normalize();
    return result;
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

  ModelCount multiplied(const ModelCount &other) const {
    if (zero() || other.zero())
      return ModelCount();
    ModelCount result;
    result.words.resize(words.size() + other.words.size(), 0);
    for (size_t lhs = 0; lhs != words.size(); ++lhs) {
      uint64_t carry = 0;
      for (size_t rhs = 0; rhs != other.words.size(); ++rhs) {
        unsigned __int128 product =
            static_cast<unsigned __int128>(words[lhs]) * other.words[rhs] +
            result.words[lhs + rhs] + carry;
        result.words[lhs + rhs] = static_cast<uint64_t>(product);
        carry = static_cast<uint64_t>(product >> 64);
      }
      result.words[lhs + other.words.size()] = carry;
    }
    result.normalize();
    return result;
  }

  /// Integer quotient. The caller establishes that the divisor is nonzero.
  /// Distribution normalization uses this only for exact integral factors.
  ModelCount divided(const ModelCount &other) const {
    if (compare(other) < 0)
      return ModelCount();
    unsigned shift = bitWidth() - other.bitWidth();
    ModelCount divisor = other.shifted(shift);
    ModelCount remainder = *this;
    ModelCount quotient;
    quotient.words.resize(size_t(shift) / 64 + 1, 0);
    for (unsigned bit = shift;; --bit) {
      if (remainder.compare(divisor) >= 0) {
        remainder.subtract(divisor);
        quotient.words[bit / 64] |= uint64_t{1} << (bit % 64);
      }
      if (bit == 0)
        break;
      uint64_t carry = 0;
      for (size_t word = divisor.words.size(); word-- != 0;) {
        uint64_t nextCarry = divisor.words[word] << 63;
        divisor.words[word] = (divisor.words[word] >> 1) | carry;
        carry = nextCarry;
      }
      divisor.normalize();
    }
    quotient.normalize();
    return quotient;
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
  unsigned bitWidth() const {
    return zero() ? 0
                  : static_cast<unsigned>((words.size() - 1) * 64 + 64 -
                                          __builtin_clzll(words.back()));
  }

  void normalize() {
    while (!words.empty() && words.back() == 0)
      words.pop_back();
  }

  std::vector<uint64_t> words;
};

/// Exact counts, projections and ranks for a compiler-produced Boolean plan.
/// Shared with bounded compiler preparation; target counting has no budget.
/// Iterative graph walks also avoid a bit-width-dependent WASM call stack.
class ModelCounter {
public:
  using ID = size_t;

  ModelCounter(random::BooleanCircuit &circuit,
               const std::vector<random::BooleanCircuit::ID> &variables)
      : circuit(&circuit), variableCount(variables.size()),
        fixed(variableCount, false), fixedValues(variableCount, false) {
    nodes.push_back({variableCount, 0, 0});
    nodes.push_back({variableCount, 1, 1});
    circuits.emplace(0, 0);
    circuits.emplace(1, 1);
  }

  bool build(random::BooleanCircuit::ID predicate) {
    root = 1;
    return constrain(predicate);
  }

  /// Compiler-only preparation can stop optimizing at a resource budget and
  /// retain the exact Boolean plan. Runtime sampling never truncates a count.
  bool prepareDiagram(random::BooleanCircuit::ID predicate, size_t budget,
                      size_t circuitBudget) {
    return convertConstrained(predicate, root, budget, circuitBudget);
  }

  /// Re-encode a completed decision diagram using the original named inputs.
  /// This is an equivalence-preserving compiler optimization; dead arithmetic
  /// gates can then be omitted from the serialized sampling plan.
  random::BooleanCircuit::ID
  exportDiagram(random::BooleanCircuit &output,
                const std::vector<random::BooleanCircuit::ID> &inputs) const {
    auto reached = reachable(root);
    std::vector<random::BooleanCircuit::ID> exported(nodes.size());
    exported[1] = 1;
    for (ID id = 2; id != reached.size(); ++id)
      if (reached[id]) {
        const Node &node = nodes[id];
        exported[id] = output.conditional(
            inputs[node.variable], exported[node.high], exported[node.low]);
      }
    return exported[root];
  }

  /// Cofactor fixed inputs before constructing the relation's diagram. Only
  /// random inputs are counted; compiler parameters were already substituted.
  bool constrain(random::BooleanCircuit::ID predicate) {
    if (std::find(fixed.begin(), fixed.end(), true) != fixed.end())
      predicate = circuit->restrict(predicate, fixedValues, fixed);
    ID relation = 0;
    if (!convertConstrained(predicate, relation))
      return false;
    root = conjunction(root, relation);
    restrict(fixedValues, fixed);
    return true;
  }

  /// Reuse a relation from the same compiled sampling plan. Weighted item
  /// combinations share the same input inventory and accepted prefix; importing
  /// their diagrams avoids constructing them again for every Cartesian term.
  bool intersect(const ModelCounter &other) {
    if (circuit != other.circuit || variableCount != other.variableCount ||
        fixed != other.fixed || fixedValues != other.fixedValues)
      return false;
    auto reached = other.reachable(other.root);
    std::vector<ID> imported(other.nodes.size());
    imported[1] = 1;
    for (ID id = 2; id != reached.size(); ++id)
      if (reached[id]) {
        const Node &node = other.nodes[id];
        imported[id] =
            makeNode(node.variable, imported[node.low], imported[node.high]);
      }
    root = conjunction(root, imported[other.root]);
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

  /// Substitute mandatory literals before branching through arithmetic gates.
  /// A positive AND forces both children; a negated AND does not. Restore the
  /// literals in the diagram so counting, projection and weighted intersections
  /// retain exactly the original relation and the same input inventory.
  bool convertConstrained(random::BooleanCircuit::ID predicate, ID &result,
                          size_t budget = 0, size_t circuitBudget = 0) {
    using CircuitID = random::BooleanCircuit::ID;
    auto found = circuits.find(predicate);
    if (found != circuits.end()) {
      result = found->second;
      return true;
    }
    std::vector<bool> selected(variableCount, false);
    std::vector<bool> values(variableCount, false);
    CircuitID remaining = predicate;
    for (;;) {
      bool changed = false;
      std::vector<bool> visited(circuit->getNodes().size(), false);
      std::vector<CircuitID> pending{remaining};
      while (!pending.empty()) {
        if ((budget && --budget == 0) ||
            (circuitBudget && circuit->getNodes().size() > circuitBudget))
          return false;
        CircuitID id = pending.back();
        pending.pop_back();
        if (id == 0) {
          result = 0;
          circuits.emplace(predicate, 0);
          return true;
        }
        if (id == 1 || visited[id / 2])
          continue;
        auto node = circuit->getNodes()[id / 2];
        // Opposite polarities of an AND have different implications. Mark
        // only the positive occurrence, which is the one we can decompose.
        if (node.opcode == random::PlanOpcode::And && !(id & 1)) {
          visited[id / 2] = true;
          pending.push_back(node.a);
          pending.push_back(node.b);
        } else if (node.opcode == random::PlanOpcode::RandomBit) {
          if (node.b >= variableCount)
            return false;
          bool value = !(id & 1);
          if (selected[node.b] && values[node.b] != value) {
            result = 0;
            circuits.emplace(predicate, 0);
            return true;
          }
          changed |= !selected[node.b];
          selected[node.b] = true;
          values[node.b] = value;
        }
      }
      if (!changed)
        break;
      remaining = circuit->restrict(remaining, values, selected);
    }
    if (!convert(remaining, result, budget, circuitBudget))
      return false;
    ID literals = 1;
    for (size_t variable = variableCount; variable-- != 0;)
      if (selected[variable])
        literals = values[variable] ? makeNode(variable, 0, literals)
                                    : makeNode(variable, literals, 0);
    result = conjunction(result, literals);
    circuits.emplace(predicate, result);
    return true;
  }

  // Shannon-expand the final predicate, simplifying each cofactor in the
  // compiler's AND graph. Constructing arithmetic BDDs one gate at a time can
  // explode even when a modular product equality has only a handful of roots.
  // This walk prunes impossible prefixes before materializing their children.
  bool convert(random::BooleanCircuit::ID predicate, ID &result,
               size_t budget = 0, size_t circuitBudget = 0) {
    using CircuitID = random::BooleanCircuit::ID;
    std::vector<CircuitID> pending{predicate};
    while (!pending.empty()) {
      if ((budget && --budget == 0) ||
          (circuitBudget && circuit->getNodes().size() > circuitBudget))
        return false;
      CircuitID id = pending.back();
      auto found = circuits.find(id);
      if (found != circuits.end()) {
        pending.pop_back();
        continue;
      }
      size_t variable = circuit->firstVariable(id);
      if (variable >= variableCount)
        return false;
      CircuitID low = circuit->cofactor(id, variable, false);
      CircuitID high = circuit->cofactor(id, variable, true);
      if (!circuits.count(low)) {
        pending.push_back(low);
        continue;
      }
      if (!circuits.count(high)) {
        pending.push_back(high);
        continue;
      }
      circuits.emplace(id,
                       makeNode(variable, circuits.at(low), circuits.at(high)));
      pending.pop_back();
    }
    result = circuits.at(predicate);
    return true;
  }

  random::BooleanCircuit *circuit;
  size_t variableCount;
  ID root = 0;
  ID viewRoot = 0;
  std::vector<bool> fixed, fixedValues, sampled;
  std::vector<size_t> levels;
  std::vector<Node> nodes;
  std::vector<ModelCount> counts;
  std::unordered_map<Key, ID, Hash> unique, cache;
  std::unordered_map<random::BooleanCircuit::ID, ID> circuits;
};

} // namespace obelisk::runtime
