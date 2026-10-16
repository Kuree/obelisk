//===- RandModelCount.h - Exact finite model counts and sampling ----------===//

#pragma once

#include "obelisk/Runtime/Runtime.h"

#include "obelisk/Runtime/RandomSamplerPlan.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace obelisk::runtime {

/// Little-endian words with two stored inline. Every decision-diagram node
/// holds a count and ranking shifts them per input, while the counts of the
/// components a sampler meets rarely exceed 128 bits.
class CountWords {
public:
  size_t size() const { return count; }
  bool empty() const { return count == 0; }
  uint64_t *begin() { return data(); }
  uint64_t *end() { return data() + count; }
  uint64_t &operator[](size_t index) { return data()[index]; }
  uint64_t operator[](size_t index) const { return data()[index]; }
  uint64_t &back() { return data()[count - 1]; }
  uint64_t back() const { return data()[count - 1]; }
  void push_back(uint64_t value) {
    resize(count + 1);
    back() = value;
  }
  void pop_back() { resize(count - 1); }
  void assign(const uint64_t *first, const uint64_t *last) {
    size_t size = size_t(last - first);
    if (size <= inlineSize) {
      std::copy(first, last, inlineWords);
      heap.clear();
    } else
      heap.assign(first, last);
    count = size;
  }
  void resize(size_t size, uint64_t value = 0) {
    if (size <= inlineSize) {
      if (count > inlineSize) {
        std::copy(heap.begin(), heap.begin() + size, inlineWords);
        heap.clear();
      } else
        std::fill(inlineWords + std::min(count, size), inlineWords + size,
                  value);
    } else {
      if (count <= inlineSize)
        heap.assign(inlineWords, inlineWords + count);
      heap.resize(size, value);
    }
    count = size;
  }

private:
  static constexpr size_t inlineSize = 2;
  uint64_t *data() { return count > inlineSize ? heap.data() : inlineWords; }
  const uint64_t *data() const {
    return count > inlineSize ? heap.data() : inlineWords;
  }

  size_t count = 0;
  uint64_t inlineWords[inlineSize] = {};
  std::vector<uint64_t> heap;
};

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

  uint64_t word(size_t index) const {
    return index < words.size() ? words[index] : 0;
  }

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

  unsigned bitWidth() const {
    return zero() ? 0
                  : static_cast<unsigned>((words.size() - 1) * 64 + 64 -
                                          __builtin_clzll(words.back()));
  }

private:
  void normalize() {
    while (!words.empty() && words.back() == 0)
      words.pop_back();
  }

  CountWords words;
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
    circuits.insert(0, 0);
    circuits.insert(1, 1);
  }

  bool build(random::BooleanCircuit::ID predicate) {
    root = 1;
    return constrain(predicate);
  }

  /// Build an existential projection directly. Eliminated inputs contribute
  /// Boolean existence, so a satisfying branch can stop their search without
  /// constructing the diagram of every later completion. A nonzero budget
  /// tries retained inputs first; exhaustion leaves the caller free to retry
  /// with the compiler's variable order in a fresh counter.
  bool buildProjection(random::BooleanCircuit::ID predicate,
                       const std::vector<bool> &keep, size_t budget = 0) {
    if (keep.size() != variableCount || nodes.size() != 2)
      return false;
    constructionBudget = budget;
    if (!convert(predicate, root, budget, 0, &keep, budget != 0))
      return false;
    constructionBudget = 0;
    project(keep);
    return true;
  }

  /// Compiler-only preparation can stop optimizing at a resource budget and
  /// retain the exact Boolean plan. Runtime sampling never truncates a count.
  bool prepareDiagram(random::BooleanCircuit::ID predicate, size_t budget,
                      size_t circuitBudget) {
    constructionBudget = budget;
    bool complete = convert(predicate, root, budget, circuitBudget);
    constructionBudget = 0;
    return complete;
  }

  /// Re-encode a completed decision diagram using the original named inputs.
  /// This is an equivalence-preserving compiler optimization; dead arithmetic
  /// gates can then be omitted from the serialized sampling plan.
  random::BooleanCircuit::ID
  exportDiagram(random::BooleanCircuit &output,
                const std::vector<random::BooleanCircuit::ID> &inputs,
                bool projected = false) const {
    ID start = projected ? viewRoot : root;
    auto reached = reachable(start);
    std::vector<random::BooleanCircuit::ID> exported(nodes.size());
    exported[1] = 1;
    for (ID id = 2; id != reached.size(); ++id)
      if (reached[id]) {
        const Node &node = nodes[id];
        exported[id] = output.conditional(
            inputs[node.variable], exported[node.high], exported[node.low]);
      }
    return exported[start];
  }

  /// Exact, portable identity of the current projected relation. A fixed
  /// variable order and low-before-high postorder make this independent of
  /// graph allocation history. Equal bytes mean equal domains; no hash or
  /// probabilistic fingerprint participates in cyclic-history decisions.
  std::vector<uint8_t> domainSnapshot() const {
    std::vector<uint64_t> names(nodes.size(), UINT64_MAX);
    names[0] = 0;
    names[1] = 1;
    std::vector<ID> order;
    std::vector<std::pair<ID, bool>> pending{{viewRoot, false}};
    while (!pending.empty()) {
      auto [id, visited] = pending.back();
      pending.pop_back();
      if (names[id] != UINT64_MAX)
        continue;
      if (!visited) {
        pending.emplace_back(id, true);
        pending.emplace_back(nodes[id].high, false);
        pending.emplace_back(nodes[id].low, false);
      } else {
        names[id] = order.size() + 2;
        order.push_back(id);
      }
    }
    std::vector<uint8_t> result;
    auto append = [&](uint64_t value) {
      for (unsigned byte = 0; byte != 8; ++byte)
        result.push_back(static_cast<uint8_t>(value >> (byte * 8)));
    };
    append(1); // Snapshot version, independent of the compiler plan format.
    append(variableCount);
    append(names[viewRoot]);
    append(order.size());
    for (ID id : order) {
      append(nodes[id].variable);
      append(names[nodes[id].low]);
      append(names[nodes[id].high]);
    }
    return result;
  }

  /// Cofactor fixed inputs before constructing the relation's diagram. Only
  /// random inputs are counted; compiler parameters were already substituted.
  bool constrain(random::BooleanCircuit::ID predicate) {
    if (std::find(fixed.begin(), fixed.end(), true) != fixed.end())
      predicate = circuit->restrict(predicate, fixedValues, fixed);
    ID relation = 0;
    if (!convert(predicate, relation))
      return false;
    root = conjunction(root, relation);
    restrict(fixedValues, fixed);
    return true;
  }

  /// Conjoin `predicate` unless that empties the relation. A greedy pass over
  /// many preferences tests each against the relation retained so far
  /// without rebuilding it; it defers counting, so project before counting.
  bool conjoinIfSatisfiable(random::BooleanCircuit::ID predicate,
                            bool &retained) {
    if (std::find(fixed.begin(), fixed.end(), true) != fixed.end())
      predicate = circuit->restrict(predicate, fixedValues, fixed);
    ID relation = 0;
    if (!convert(predicate, relation))
      return false;
    ID conjoined = conjunction(root, relation);
    retained = conjoined != 0;
    if (retained)
      root = conjoined;
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

  /// Whether the projected relation contains a complete assignment.
  bool contains(const std::vector<bool> &value) const {
    ID current = viewRoot;
    while (current > 1)
      current = value[nodes[current].variable] ? nodes[current].high
                                               : nodes[current].low;
    return current == 1;
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
      random::IntegerHash mix;
      return mix(key.a ^ mix(key.b ^ mix(key.c)));
    }
  };

  ID makeNode(size_t variable, ID low, ID high) {
    if (low == high)
      return low;
    auto [found, inserted] =
        unique.insert(Key{variable, low, high}, nodes.size());
    if (inserted)
      nodes.push_back({variable, low, high});
    return *found;
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
    const ID *found = cache.find(key);
    if (!found)
      return false;
    result = *found;
    return true;
  }

  ID conditional(ID condition, ID yes, ID no) {
    Key initial{condition, yes, no};
    std::vector<Key> pending{initial};
    ID result = 0;
    while (!pending.empty()) {
      // Diagram products can expand inside one outer conversion step. Count
      // this walk too, so bounded compiler preparation and speculative runtime
      // projection cannot bypass their budget through a large conjunction.
      if (constructionExhausted ||
          (constructionBudget && --constructionBudget == 0)) {
        constructionExhausted = true;
        return 0;
      }
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
      cache.insert(key, result);
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
  bool extractLiterals(random::BooleanCircuit::ID predicate,
                       random::BooleanCircuit::ID &remaining, ID &literals,
                       size_t &budget, size_t circuitBudget,
                       const std::vector<bool> *keep = nullptr) {
    using CircuitID = random::BooleanCircuit::ID;
    remaining = predicate;
    literals = 1;
    if (!circuit->hasMandatoryLiteral(remaining))
      return true;
    std::vector<bool> selected(variableCount, false);
    std::vector<bool> values(variableCount, false);
    for (;;) {
      if (!circuit->hasMandatoryLiteral(remaining))
        break;
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
          remaining = 0;
          return true;
        }
        if (id == 1 || !circuit->hasMandatoryLiteral(id) || visited[id / 2])
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
            remaining = 0;
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
    for (size_t variable = variableCount; variable-- != 0;)
      if (selected[variable] && (!keep || (*keep)[variable]))
        literals = values[variable] ? makeNode(variable, 0, literals)
                                    : makeNode(variable, literals, 0);
    return true;
  }

  // Shannon-expand the final predicate, simplifying each cofactor in the
  // compiler's AND graph. Constructing arithmetic BDDs one gate at a time can
  // explode even when a modular product equality has only a handful of roots.
  // This walk prunes impossible prefixes before materializing their children.
  bool convert(random::BooleanCircuit::ID predicate, ID &result,
               size_t budget = 0, size_t circuitBudget = 0,
               const std::vector<bool> *keep = nullptr,
               bool retainedFirst = false) {
    using CircuitID = random::BooleanCircuit::ID;
    struct Frame {
      CircuitID predicate;
      CircuitID remaining = 0;
      ID literals = 1;
      bool propagated = false;
      bool decided = false;
      CircuitID low = 0, high = 0;
    };
    std::vector<Frame> pending{{predicate}};
    std::vector<size_t> firstKept;
    while (!pending.empty()) {
      if (constructionExhausted || (budget && --budget == 0) ||
          (circuitBudget && circuit->getNodes().size() > circuitBudget))
        return false;
      Frame &frame = pending.back();
      CircuitID id = frame.predicate;
      if (circuits.contains(id)) {
        pending.pop_back();
        continue;
      }
      // A branch can expose a long forced prefix that was hidden under an
      // OR, notably either sign of a real zero cell. Propagate that prefix
      // before cofactoring its arithmetic decoder one bit at a time. The
      // literal diagram restores every forced input for exact counts and
      // projections; this is simplification, not a restriction of the domain.
      // A decision node of a compiler-prepared diagram already names its
      // cofactors by its first input, which is what the walk would choose.
      if (!frame.propagated) {
        frame.propagated = true;
        frame.remaining = id;
        frame.decided =
            !retainedFirst && circuit->decision(id, circuit->firstVariable(id),
                                                frame.low, frame.high);
        if (!frame.decided &&
            !extractLiterals(id, frame.remaining, frame.literals, budget,
                             circuitBudget, keep))
          return false;
      }
      if (frame.remaining != id) {
        const ID *reduced = circuits.find(frame.remaining);
        if (!reduced) {
          pending.push_back({frame.remaining});
          continue;
        }
        ID conjoined = conjunction(*reduced, frame.literals);
        // An exhausted product returns a placeholder; never memoize it.
        if (constructionExhausted)
          return false;
        circuits.insert(id, conjoined);
        pending.pop_back();
        continue;
      }
      size_t variable = circuit->firstVariable(id);
      if (retainedFirst) {
        // Decide retained inputs first. Once their relevant prefix is fixed,
        // the eliminated suffix needs one witness rather than all completions.
        // Cache one minimum per circuit node, including new cofactors; no
        // variable-width dependency bitmap is built for each node.
        const auto &circuitNodes = circuit->getNodes();
        while (firstKept.size() != circuitNodes.size()) {
          size_t index = firstKept.size();
          const auto &node = circuitNodes[index];
          size_t first = variableCount;
          if (node.opcode == random::PlanOpcode::RandomBit) {
            if (node.b >= variableCount)
              return false;
            if ((*keep)[node.b])
              first = node.b;
          } else if (node.opcode == random::PlanOpcode::And)
            first = std::min(firstKept[node.a / 2], firstKept[node.b / 2]);
          firstKept.push_back(first);
        }
        if (firstKept[id / 2] != variableCount)
          variable = firstKept[id / 2];
      }
      if (variable >= variableCount)
        return false;
      CircuitID low =
          frame.decided ? frame.low : circuit->cofactor(id, variable, false);
      CircuitID high =
          frame.decided ? frame.high : circuit->cofactor(id, variable, true);
      if (!circuits.contains(low)) {
        pending.push_back({low});
        continue;
      }
      if (keep && !(*keep)[variable] && circuits.at(low) == 1) {
        circuits.insert(id, 1);
        pending.pop_back();
        continue;
      }
      if (!circuits.contains(high)) {
        pending.push_back({high});
        continue;
      }
      ID expanded =
          keep && !(*keep)[variable]
              ? disjunction(circuits.at(low), circuits.at(high))
              : makeNode(variable, circuits.at(low), circuits.at(high));
      if (constructionExhausted)
        return false;
      circuits.insert(id, expanded);
      pending.pop_back();
    }
    result = circuits.at(predicate);
    return !constructionExhausted;
  }

  random::BooleanCircuit *circuit;
  size_t variableCount;
  ID root = 0;
  ID viewRoot = 0;
  size_t constructionBudget = 0;
  bool constructionExhausted = false;
  std::vector<bool> fixed, fixedValues, sampled;
  std::vector<size_t> levels;
  std::vector<Node> nodes;
  std::vector<ModelCount> counts;
  random::FlatMap<Key, ID, Hash> unique, cache;
  random::FlatMap<random::BooleanCircuit::ID, ID> circuits;
};

} // namespace obelisk::runtime
