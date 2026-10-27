//===- RandSamplePlan.cpp - Specialize and sample compiler-proved plans
//----===//

#include "RandSamplePlan.h"
#include "RandModelCount.h"
#include "RandSolveOrder.h"
#include "RandSolveWeighted.h"
#include "RealRandom.h"
#include "talos/Runtime/RandomDomain.h"
#include "talos/Runtime/RandomSamplerPlan.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <memory>
#include <numeric>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace talos::runtime {
namespace {
using random::BooleanCircuit;
using random::PlanOpcode;
using ID = BooleanCircuit::ID;
using Bits = std::vector<ID>;
constexpr ID missing = BooleanCircuit::missing;

uint16_t read16(const uint8_t *p) {
  return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}
uint32_t read32(const uint8_t *p) { return random::planRead32(p); }
uint64_t read64(const uint8_t *p) { return random::planRead64(p); }

/// Bits needed to index a permutation of `cardinality` ranks.
unsigned permutationWidth(uint64_t cardinality) {
  unsigned width = 0;
  for (uint64_t remaining = cardinality - 1; remaining; remaining >>= 1)
    ++width;
  return width;
}

uint64_t fieldValue(const std::vector<bool> &bits) {
  uint64_t value = 0;
  for (size_t bit = 0; bit != bits.size(); ++bit)
    value |= uint64_t(bits[bit]) << bit;
  return value;
}

/// The constraint set a cyclic history belongs to: the compiled program and
/// its constraint_mode mask. State values are not part of it; their effect is
/// the legal domain, which changes within one iteration.
uint64_t cyclicIdentity(const uint8_t *program, size_t size,
                        uint64_t constraintMask) {
  uint64_t hash = 0xcbf29ce484222325ull;
  auto mix = [&](uint8_t byte) {
    hash ^= byte;
    hash *= 0x100000001b3ull;
  };
  for (size_t index = 0; index != size; ++index)
    mix(program[index]);
  for (unsigned byte = 0; byte != 8; ++byte)
    mix(static_cast<uint8_t>(constraintMask >> (byte * 8)));
  return hash;
}

/// Persistent randc state between calls, opaque to the caller. The current
/// domain's permutation is (key, position) in the request; this holds the
/// domain it permutes, whether its pass completed, and the values returned
/// in this iteration under earlier domains.
struct CyclicHistory {
  bool present = false;
  bool complete = false;
  uint64_t identity = 0;
  std::vector<uint8_t> segment;
  std::vector<uint32_t> returned;

  static constexpr uint64_t version = 2;

  bool contains(uint64_t value) const {
    return std::binary_search(returned.begin(), returned.end(),
                              static_cast<uint32_t>(value));
  }

  void insert(uint64_t value) {
    auto at = std::lower_bound(returned.begin(), returned.end(),
                               static_cast<uint32_t>(value));
    if (at == returned.end() || *at != value)
      returned.insert(at, static_cast<uint32_t>(value));
  }

  bool read(const uint8_t *bytes, size_t size) {
    *this = CyclicHistory();
    if (size == 0)
      return true;
    size_t offset = 0;
    auto take = [&](uint64_t &value, unsigned width) {
      if (size - offset < width)
        return false;
      value = 0;
      for (unsigned byte = 0; byte != width; ++byte)
        value |= uint64_t(bytes[offset + byte]) << (byte * 8);
      offset += width;
      return true;
    };
    uint64_t tag, flag, segmentSize, count;
    if (!take(tag, 8) || tag != version || !take(identity, 8) ||
        !take(flag, 8) || flag > 1 || !take(segmentSize, 8) ||
        segmentSize > size - offset)
      return false;
    complete = flag;
    segment.assign(bytes + offset, bytes + offset + segmentSize);
    offset += segmentSize;
    if (!take(count, 8) || count > (size - offset) / 4)
      return false;
    returned.resize(count);
    for (uint32_t &value : returned) {
      uint64_t word = 0;
      take(word, 4);
      value = static_cast<uint32_t>(word);
    }
    present = true;
    return offset == size && std::is_sorted(returned.begin(), returned.end()) &&
           std::adjacent_find(returned.begin(), returned.end()) ==
               returned.end();
  }

  std::vector<uint8_t> write() const {
    std::vector<uint8_t> bytes;
    auto put = [&](uint64_t value, unsigned width) {
      for (unsigned byte = 0; byte != width; ++byte)
        bytes.push_back(static_cast<uint8_t>(value >> (byte * 8)));
    };
    put(version, 8);
    put(identity, 8);
    put(complete, 8);
    put(segment.size(), 8);
    bytes.insert(bytes.end(), segment.begin(), segment.end());
    put(returned.size(), 8);
    for (uint32_t value : returned)
      put(value, 4);
    return bytes;
  }

  /// Rebuild the stored domain's diagram from its ModelCounter snapshot.
  bool rebuild(BooleanCircuit &circuit, Bits &inputs, ID &root,
               unsigned width) const {
    size_t offset = 0;
    auto take = [&](uint64_t &value) {
      if (segment.size() - offset < 8)
        return false;
      value = 0;
      for (unsigned byte = 0; byte != 8; ++byte)
        value |= uint64_t(segment[offset + byte]) << (byte * 8);
      offset += 8;
      return true;
    };
    uint64_t tag, variables, rootName, count;
    if (!take(tag) || tag != 1 || !take(variables) || variables != width ||
        !take(rootName) || !take(count) || count > segment.size() / 24)
      return false;
    inputs.clear();
    for (unsigned bit = 0; bit != width; ++bit)
      inputs.push_back(circuit.input(PlanOpcode::RandomBit, bit, bit));
    std::vector<ID> names{0, 1};
    for (uint64_t node = 0; node != count; ++node) {
      uint64_t variable, low, high;
      if (!take(variable) || !take(low) || !take(high) || variable >= width ||
          low >= names.size() || high >= names.size())
        return false;
      names.push_back(
          circuit.conditional(inputs[variable], names[high], names[low]));
    }
    if (rootName >= names.size() || offset != segment.size())
      return false;
    root = names[rootName];
    return true;
  }
};

/// Exact weights enumerate the item combinations of coupled distributions.
/// Beyond this many combinations a solve reports exhausted resources.
constexpr size_t maxDistributionTerms = size_t{1} << 16;

/// Union-find over relation inputs. A circuit node's representative is one
/// input of its support; computing it unites that whole support. With
/// `within`, only the selected inputs count as support; the others connect
/// nothing, as if they were constants.
class InputPartition {
public:
  static constexpr size_t none = SIZE_MAX;

  InputPartition(const BooleanCircuit &circuit, size_t inputs,
                 const std::vector<bool> *within = nullptr)
      : circuit(circuit), within(within), parent(inputs),
        representatives(circuit.getNodes().size(), unknown) {
    std::iota(parent.begin(), parent.end(), size_t{0});
  }

  size_t find(size_t input) {
    while (parent[input] != input)
      input = parent[input] = parent[parent[input]];
    return input;
  }

  size_t unite(size_t a, size_t b) {
    if (a == none || b == none)
      return a == none ? b : a;
    a = find(a);
    b = find(b);
    parent[std::max(a, b)] = std::min(a, b);
    return std::min(a, b);
  }

  /// Unite the inputs `id` reads; returns one of them, or none for a
  /// constant.
  size_t join(ID id) {
    std::vector<size_t> pending{id / 2};
    while (!pending.empty()) {
      size_t node = pending.back();
      if (representatives[node] != unknown) {
        pending.pop_back();
        continue;
      }
      const auto &plan = circuit.getNodes()[node];
      if (plan.opcode == PlanOpcode::RandomBit)
        representatives[node] = !within || (*within)[plan.b] ? plan.b : none;
      else if (plan.opcode != PlanOpcode::And)
        representatives[node] = none;
      else if (representatives[plan.a / 2] == unknown)
        pending.push_back(plan.a / 2);
      else if (representatives[plan.b / 2] == unknown)
        pending.push_back(plan.b / 2);
      else
        representatives[node] =
            unite(representatives[plan.a / 2], representatives[plan.b / 2]);
    }
    return representatives[id / 2];
  }

private:
  static constexpr size_t unknown = SIZE_MAX - 1;
  const BooleanCircuit &circuit;
  const std::vector<bool> *within;
  std::vector<size_t> parent;
  std::vector<size_t> representatives;
};

/// The inputs, as ordered variable indices, that any of `roots` reads.
std::vector<size_t> circuitSupport(const BooleanCircuit &circuit,
                                   const std::vector<ID> &roots) {
  std::vector<size_t> support;
  std::vector<bool> seen(circuit.getNodes().size(), false);
  std::vector<ID> pending;
  for (ID root : roots)
    pending.push_back(root / 2);
  while (!pending.empty()) {
    ID node = pending.back();
    pending.pop_back();
    if (seen[node])
      continue;
    seen[node] = true;
    const auto &plan = circuit.getNodes()[node];
    if (plan.opcode == PlanOpcode::RandomBit)
      support.push_back(plan.b);
    else if (plan.opcode == PlanOpcode::And) {
      pending.push_back(plan.a / 2);
      pending.push_back(plan.b / 2);
    }
  }
  std::sort(support.begin(), support.end());
  return support;
}

/// Per-node scratch reused across walks of a growing circuit. A node is
/// marked in the current walk when its stamp equals the walk's generation,
/// so starting a walk costs nothing for the nodes it never visits.
class NodeScratch {
public:
  void begin(size_t nodes) {
    if (stamps.size() < nodes) {
      stamps.resize(nodes, 0);
      values.resize(nodes, 0);
    }
    if (++generation == 0) {
      std::fill(stamps.begin(), stamps.end(), 0);
      generation = 1;
    }
  }
  bool mark(size_t node) {
    if (stamps[node] == generation)
      return false;
    stamps[node] = generation;
    return true;
  }
  ID &value(size_t node) { return values[node]; }

private:
  std::vector<uint32_t> stamps;
  std::vector<ID> values;
  uint32_t generation = 0;
};

/// A relation copied into a circuit of its own whose inputs are its support
/// plus `extra`, numbered in their original order. Decision diagrams, counts
/// and ranks depend only on that order, so they are the original relation's;
/// sizing a ModelCounter by one component instead of every input keeps the
/// work for many independent components linear. Construction records only
/// the shape: the reached gates in postorder, with operands renamed to local
/// positions. Relations of one shape are the same relation over
/// renamed inputs, so a constraint iterated over an array needs one circuit
/// and one diagram for all of its elements. A specialized relation has only
/// random inputs and gates; anything else leaves the copy invalid.
struct LocalRelation {
  std::vector<size_t> originals;
  std::vector<uint32_t> shape;
  BooleanCircuit circuit;
  Bits inputs;
  ID root = 0;
  bool valid = true;

  LocalRelation(const BooleanCircuit &source, ID relation, NodeScratch &scratch,
                const std::vector<size_t> &extra = {}) {
    const auto &nodes = source.getNodes();
    scratch.begin(nodes.size());
    scratch.mark(0);
    scratch.value(0) = 0;
    // Postorder lists every operand before its gate.
    std::vector<ID> reached;
    std::vector<std::pair<ID, bool>> pending{{relation / 2, false}};
    while (!pending.empty()) {
      auto [node, expanded] = pending.back();
      pending.pop_back();
      if (expanded) {
        reached.push_back(node);
        continue;
      }
      if (!scratch.mark(node))
        continue;
      if (nodes[node].opcode == PlanOpcode::And) {
        pending.push_back({node, true});
        pending.push_back({nodes[node].b / 2, false});
        pending.push_back({nodes[node].a / 2, false});
        continue;
      }
      if (nodes[node].opcode == PlanOpcode::RandomBit)
        originals.push_back(nodes[node].b);
      else
        valid = false;
      reached.push_back(node);
    }
    originals.insert(originals.end(), extra.begin(), extra.end());
    std::sort(originals.begin(), originals.end());
    originals.erase(std::unique(originals.begin(), originals.end()),
                    originals.end());
    auto rename = [&](ID id) { return scratch.value(id / 2) ^ (id & 1); };
    shape.reserve(reached.size() * 3 + 2);
    shape.push_back(uint32_t(originals.size()));
    for (size_t position = 0; position != reached.size(); ++position) {
      const auto &plan = nodes[reached[position]];
      if (plan.opcode == PlanOpcode::RandomBit) {
        shape.push_back(0);
        shape.push_back(
            uint32_t(std::lower_bound(originals.begin(), originals.end(),
                                      size_t(plan.b)) -
                     originals.begin()));
      } else {
        shape.push_back(1);
        shape.push_back(rename(plan.a));
        shape.push_back(rename(plan.b));
      }
      scratch.value(reached[position]) = ID(position + 1) * 2;
    }
    shape.push_back(rename(relation));
  }

  /// Construct the local circuit from the shape.
  void build() {
    circuit.reserve(shape.size() / 3 + shape.front());
    for (uint32_t index = 0; index != shape.front(); ++index)
      inputs.push_back(circuit.input(PlanOpcode::RandomBit, 0, index));
    std::vector<ID> handles{0};
    auto handle = [&](uint32_t id) { return handles[id / 2] ^ (id & 1); };
    for (size_t at = 1; at + 1 != shape.size();) {
      if (shape[at] == 0) {
        handles.push_back(inputs[shape[at + 1]]);
        at += 2;
      } else {
        handles.push_back(
            circuit.conjunction(handle(shape[at + 1]), handle(shape[at + 2])));
        at += 3;
      }
    }
    root = handle(shape.back());
  }

  uint64_t hash() const {
    uint64_t value = shape.size();
    for (uint32_t word : shape)
      value = random::IntegerHash()(value ^ word);
    return value;
  }
};

/// The runtime handles Boolean cofactors and exact ranks only. Arithmetic
/// expressions and their validity predicates were lowered by compiler Z3.
class Formula {
public:
  Formula(BooleanCircuit &circuit, const Bits &bits)
      : circuit(circuit), bits(bits), fixed(bits.size(), false),
        fixedValues(bits.size(), false) {}

  ID predicate() {
    ID result = 1;
    for (ID requirement : requirements)
      result = circuit.conjunction(result, requirement);
    if (std::find(fixed.begin(), fixed.end(), true) != fixed.end())
      result = circuit.restrict(result, fixedValues, fixed);
    return result;
  }

  /// The relation's top-level conjuncts: requirements with the fixed inputs
  /// substituted, split at positive ANDs. Substitution can expose new ones,
  /// such as the body of a guard that an earlier solve-before layer decided.
  /// A contradiction is the single conjunct 0. Substitution adds gates to the
  /// shared circuit.
  std::vector<ID> conjuncts() {
    bool restricted =
        std::find(fixed.begin(), fixed.end(), true) != fixed.end();
    std::vector<ID> result;
    std::vector<ID> pending;
    for (auto requirement = requirements.rbegin();
         requirement != requirements.rend(); ++requirement)
      pending.push_back(restricted
                            ? circuit.restrict(*requirement, fixedValues, fixed)
                            : *requirement);
    while (!pending.empty()) {
      ID id = pending.back();
      pending.pop_back();
      if (id == 0)
        return {0};
      const auto &node = circuit.getNodes()[id / 2];
      if (id == 1)
        continue;
      if (!(id & 1) && node.opcode == PlanOpcode::And) {
        pending.push_back(node.b);
        pending.push_back(node.a);
      } else
        result.push_back(id);
    }
    return result;
  }

  void require(ID predicate) { requirements.push_back(predicate); }
  void push() { checkpoints.push_back(requirements.size()); }
  void pop() {
    requirements.resize(checkpoints.back());
    checkpoints.pop_back();
  }

  /// The relation's projection onto the unfixed inputs that `keep` selects,
  /// as independent relations: each reads only kept inputs, and no two read
  /// the same one. A decision diagram over interleaved independent fields
  /// multiplies their widths, so nothing here builds one over the whole
  /// relation. Conjuncts that share no eliminated input are projected apart,
  /// since existential quantification distributes over a conjunction of
  /// disjoint supports; the projections are then grouped by the kept inputs
  /// they share. A nonzero `retainedFirstBudget` first tries deciding kept
  /// inputs first in each projection (ModelCounter::buildProjection).
  SamplerResult projections(const std::vector<bool> &keep,
                            std::vector<ID> &result,
                            size_t retainedFirstBudget = 0) {
    result.clear();
    std::vector<ID> parts = conjuncts();
    if (parts.size() == 1 && parts.front() == 0)
      return SamplerResult::Unsatisfiable;
    std::vector<bool> kept(bits.size()), eliminated(bits.size());
    for (size_t input = 0; input != bits.size(); ++input) {
      kept[input] = keep[input] && !fixed[input];
      eliminated[input] = !keep[input] && !fixed[input];
    }
    // Each group conjoins its parts; a single part keeps its own handle.
    struct Group {
      ID relation = 1;
      size_t parts = 0;
    };
    std::map<size_t, Group> groups;
    std::vector<ID> projected;
    if (std::find(eliminated.begin(), eliminated.end(), true) ==
        eliminated.end()) {
      projected = std::move(parts);
    } else {
      InputPartition byEliminated(circuit, bits.size(), &eliminated);
      std::vector<size_t> partInputs;
      for (ID part : parts)
        partInputs.push_back(byEliminated.join(part));
      for (size_t index = 0; index != parts.size(); ++index) {
        if (partInputs[index] == InputPartition::none) {
          projected.push_back(parts[index]);
          continue;
        }
        Group &group = groups[byEliminated.find(partInputs[index])];
        group.relation = circuit.conjunction(group.relation, parts[index]);
        ++group.parts;
      }
    }
    for (const auto &entry : groups) {
      const Group &group = entry.second;
      if (group.parts == 1 && leavesFree(group.relation, eliminated))
        continue;
      LocalRelation local(circuit, group.relation, scratch);
      if (!local.valid)
        return SamplerResult::Invalid;
      local.build();
      std::vector<bool> localKept;
      Bits originals;
      for (size_t original : local.originals) {
        localKept.push_back(kept[original]);
        originals.push_back(bits[original]);
      }
      ModelCounter counter(local.circuit, local.inputs);
      if (!retainedFirstBudget ||
          !counter.buildProjection(local.root, localKept,
                                   retainedFirstBudget)) {
        counter = ModelCounter(local.circuit, local.inputs);
        if (!counter.buildProjection(local.root, localKept))
          return SamplerResult::Invalid;
      }
      if (counter.count().zero())
        return SamplerResult::Unsatisfiable;
      ID projection = counter.exportDiagram(circuit, originals, true);
      if (projection != 1)
        projected.push_back(projection);
    }
    InputPartition byKept(circuit, bits.size());
    std::vector<size_t> projectedInputs;
    for (ID part : projected)
      projectedInputs.push_back(byKept.join(part));
    std::map<size_t, ID> components;
    for (size_t index = 0; index != projected.size(); ++index) {
      ID &component =
          components.try_emplace(byKept.find(projectedInputs[index]), 1)
              .first->second;
      component = circuit.conjunction(component, projected[index]);
    }
    for (const auto &component : components)
      result.push_back(component.second);
    return SamplerResult::Satisfiable;
  }

  /// Whether eliminating `eliminated` from `part` leaves a true projection
  /// by a compiler-proved free field: every input of the field is a mutable
  /// input that is eliminated, and frozen inputs outside it are among the
  /// inputs the proof quantifies over.
  bool leavesFree(ID part, const std::vector<bool> &eliminated) const {
    const auto *first = freeFieldIndex.find(part);
    if (!first)
      return false;
    for (size_t index = *first;
         index != freeFields.size() && freeFields[index].node == part;
         ++index) {
      const FreeField &field = freeFields[index];
      bool free = true;
      for (uint32_t bit = field.offset;
           free && bit != field.offset + field.width; ++bit)
        free = ordinals[bit] != SIZE_MAX && eliminated[ordinals[bit]];
      if (free)
        return true;
    }
    return false;
  }

  /// Compiler-proved free fields by the handle their node specializes to,
  /// sorted by handle, and the variable index of each mutable physical bit.
  struct FreeField {
    ID node;
    uint32_t offset, width;
  };
  void setFreeFields(std::vector<FreeField> fields) {
    freeFields = std::move(fields);
    std::stable_sort(
        freeFields.begin(), freeFields.end(),
        [](const FreeField &a, const FreeField &b) { return a.node < b.node; });
    size_t size = 0;
    for (size_t index = 0; index != freeFields.size(); ++index) {
      freeFieldIndex.insert(freeFields[index].node, index);
      size = std::max(size, size_t(freeFields[index].offset) +
                                freeFields[index].width);
    }
    for (unsigned physical : physicalBits)
      size = std::max(size, size_t(physical) + 1);
    ordinals.assign(size, SIZE_MAX);
    for (size_t bit = 0; bit != physicalBits.size(); ++bit)
      ordinals[physicalBits[bit]] = bit;
  }

  /// Whether the relation has a solution, which is whether its projection
  /// onto `separators` is nonempty. Inputs that an earlier solve-before layer
  /// decides make good separators: a dynamic array's size guards the
  /// constraint on each element, which leaves the elements independent.
  SamplerResult check(const std::vector<bool> *separators = nullptr) {
    std::vector<ID> relations;
    SamplerResult status = projections(
        separators ? *separators : std::vector<bool>(bits.size(), false),
        relations);
    if (status != SamplerResult::Satisfiable)
      return status;
    for (ID relation : relations) {
      LocalRelation local(circuit, relation, scratch);
      if (!local.valid)
        return SamplerResult::Invalid;
      local.build();
      ModelCounter counter(local.circuit, local.inputs);
      if (!counter.build(local.root))
        return SamplerResult::Invalid;
      if (counter.count().zero())
        return SamplerResult::Unsatisfiable;
    }
    return SamplerResult::Satisfiable;
  }

  /// Fix the unfixed inputs that `keep` selects to a uniform choice from the
  /// relation's projection onto them. Independent projections are chosen
  /// one at a time: a uniform choice from their product is a uniform choice
  /// from each (IEEE 1800-2023 18.5.9 permits any solving order with the
  /// same outcome). Kept inputs that no constraint reads are chosen with the
  /// first projection, so a relation that does not separate consumes the
  /// same draws as a single decision diagram of it. With `satisfiable`, the
  /// relation is known to have a solution, so choosing no input needs no
  /// projection; otherwise the projection also decides that.
  SamplerResult choose(const std::vector<bool> &keep,
                       talos_rt_random_state_v1 &state,
                       bool satisfiable = false) {
    bool open = false;
    for (size_t input = 0; input != bits.size() && !open; ++input)
      open = keep[input] && !fixed[input];
    if (!open && satisfiable)
      return SamplerResult::Satisfiable;
    std::vector<ID> relations;
    SamplerResult status = projections(keep, relations);
    if (status != SamplerResult::Satisfiable)
      return status;
    std::vector<LocalRelation> locals;
    std::vector<bool> unconstrained(bits.size());
    for (size_t input = 0; input != bits.size(); ++input)
      unconstrained[input] = keep[input] && !fixed[input];
    for (ID relation : relations) {
      locals.emplace_back(circuit, relation, scratch);
      if (!locals.back().valid)
        return SamplerResult::Invalid;
      for (size_t input : locals.back().originals)
        unconstrained[input] = false;
    }
    std::vector<size_t> extra;
    for (size_t input = 0; input != bits.size(); ++input)
      if (unconstrained[input])
        extra.push_back(input);
    if (!extra.empty()) {
      if (locals.empty())
        locals.emplace_back(circuit, 1, scratch, extra);
      else
        locals.front() =
            LocalRelation(circuit, relations.front(), scratch, extra);
    }
    // Constraints iterated over an array give many components of one local
    // shape; each distinct relation is built and counted once.
    std::vector<std::pair<const LocalRelation *, ModelCounter>> counters;
    std::unordered_map<uint64_t, std::vector<size_t>> shapes;
    for (LocalRelation &local : locals) {
      std::vector<size_t> &candidates = shapes[local.hash()];
      auto same =
          std::find_if(candidates.begin(), candidates.end(), [&](size_t index) {
            return counters[index].first->shape == local.shape;
          });
      if (same == candidates.end()) {
        local.build();
        candidates.push_back(counters.size());
        counters.emplace_back(&local,
                              ModelCounter(local.circuit, local.inputs));
        if (!counters.back().second.build(local.root))
          return SamplerResult::Invalid;
        same = candidates.end() - 1;
      }
      const ModelCounter &counter = counters[*same].second;
      ModelCount count = counter.count();
      if (count.zero())
        return SamplerResult::Unsatisfiable;
      std::vector<bool> value =
          counter.assignment(ModelCount::randomBelow(count, state));
      for (size_t input = 0; input != local.originals.size(); ++input) {
        fixed[local.originals[input]] = true;
        fixedValues[local.originals[input]] = value[input];
      }
    }
    return SamplerResult::Satisfiable;
  }

  /// Choose every unfixed input, one solve-before layer at a time (IEEE
  /// 1800-2023 18.5.9), then the rest jointly. An auxiliary target bit is
  /// decided with the last of its inputs. A layer chosen from a projection
  /// leaves a satisfiable relation.
  SamplerResult layered(unsigned width, const uint64_t *masks,
                        size_t layerCount, talos_rt_random_state_v1 &state) {
    size_t words = (uint64_t(width) + 63) / 64;
    bool satisfiable = false;
    for (size_t layer = 0; layer != layerCount; ++layer) {
      std::vector<bool> keep(bits.size(), false);
      for (size_t bit = 0; bit != bits.size(); ++bit)
        keep[bit] = !fixed[bit] && physicalBits[bit] < width &&
                    ((masks[layer * words + physicalBits[bit] / 64] >>
                      (physicalBits[bit] % 64)) &
                     1);
      for (size_t bit = 0; bit != bits.size(); ++bit)
        if (physicalBits[bit] >= width)
          keep[bit] =
              !fixed[bit] &&
              std::all_of(
                  auxiliarySupport[bit].begin(), auxiliarySupport[bit].end(),
                  [&](size_t input) { return fixed[input] || keep[input]; });
      SamplerResult status = choose(keep, state, satisfiable);
      if (status != SamplerResult::Satisfiable)
        return status;
      satisfiable = true;
    }
    return choose(std::vector<bool>(bits.size(), true), state, satisfiable);
  }

  // Distribution metadata contains concrete, at-most-64-bit interval bounds.
  // These short Boolean comparisons specialize that metadata without any
  // bit-vector solver or arithmetic interpreter in the target runtime.
  ID atLeast(const Bits &field, uint64_t lower) {
    ID result = 1;
    for (size_t bit = 0; bit != field.size(); ++bit)
      result = ((lower >> bit) & 1) ? circuit.conjunction(field[bit], result)
                                    : circuit.disjunction(field[bit], result);
    return result;
  }
  ID atMost(const Bits &field, uint64_t upper) {
    ID result = 1;
    for (size_t bit = 0; bit != field.size(); ++bit)
      result = ((upper >> bit) & 1)
                   ? circuit.disjunction(field[bit] ^ 1, result)
                   : circuit.conjunction(field[bit] ^ 1, result);
    return result;
  }

  /// Cyclic sampling (IEEE 1800-2023 18.4.2). An iteration returns each
  /// legal value at most once and ends when none of the values it has not
  /// returned satisfies the constraints; a different constraint set (program
  /// or constraint_mode mask) starts a new one. While the legal domain is
  /// unchanged, values follow a keyed permutation of its ranks, so the
  /// history is constant size. When the domain changes, for example because
  /// a state variable or an earlier cyclic field changed, the values the
  /// permutation returned move to an explicit list and a new permutation of
  /// the new domain skips them.
  SamplerResult cyclic(CyclicSampleRequest &request, uint64_t identity,
                       unsigned width, const uint64_t *start,
                       const uint64_t *mutableMask,
                       talos_rt_random_state_v1 &state, uint64_t *output) {
    if (request.width == 0 || request.width > 32 || request.offset > width ||
        request.width > width - request.offset)
      return SamplerResult::Invalid;
    for (unsigned bit = 0; bit != request.width; ++bit)
      if (!((mutableMask[(request.offset + bit) / 64] >>
             ((request.offset + bit) % 64)) &
            1))
        return SamplerResult::Invalid;
    std::vector<bool> keep(bits.size(), false);
    for (size_t bit = 0; bit != bits.size(); ++bit)
      keep[bit] = physicalBits[bit] >= request.offset &&
                  physicalBits[bit] - request.offset < request.width;
    // Sparse domains often benefit from deciding the cyclic bits first.
    // Broad arithmetic projections may need the compiler's interleaving; a
    // bounded attempt falls back to constructing the exact relation there.
    // Components without a cyclic bit only need a solution.
    std::vector<ID> relations;
    SamplerResult status = projections(keep, relations, 4096);
    if (status != SamplerResult::Satisfiable)
      return status;
    ID cyclicRelation = 1;
    for (ID relation : relations)
      cyclicRelation = circuit.conjunction(cyclicRelation, relation);
    ModelCounter completions(circuit, bits);
    if (!completions.build(cyclicRelation))
      return SamplerResult::Invalid;
    completions.project(keep);
    if (completions.count().zero())
      return SamplerResult::Unsatisfiable;

    // Rebuild only the projected field in a fixed physical-bit order. The
    // compiler may choose a different input order for another plan; equal
    // legal domains must still have identical snapshots and rank maps.
    BooleanCircuit domainCircuit;
    Bits domainBits;
    for (unsigned bit = 0; bit != request.width; ++bit)
      domainBits.push_back(
          domainCircuit.input(PlanOpcode::RandomBit, bit, bit));
    Bits renamed(bits.size(), 0);
    for (size_t bit = 0; bit != bits.size(); ++bit)
      if (keep[bit])
        renamed[bit] = domainBits[physicalBits[bit] - request.offset];
    ID predicate = completions.exportDiagram(domainCircuit, renamed, true);
    ModelCounter domain(domainCircuit, domainBits);
    if (!domain.build(predicate))
      return SamplerResult::Invalid;
    uint64_t cardinality = domain.count().word(0);
    if (!cardinality || cardinality > (uint64_t{1} << request.width))
      return SamplerResult::Invalid;
    CyclicHistory history;
    if (!history.read(request.previousDomain, request.previousDomainSize))
      return SamplerResult::Invalid;
    std::vector<uint8_t> snapshot = domain.domainSnapshot();
    uint64_t key = request.key, position = request.position;
    talos_rt_random_state_v1 nextState = state;
    if (!history.present || history.identity != identity) {
      history = CyclicHistory();
      position = 0;
    } else if (history.segment != snapshot) {
      // Retire the values the previous domain's permutation returned: its
      // prefix, or all of its values once a pass completed.
      BooleanCircuit previousCircuit;
      Bits previousBits;
      ID previousRoot;
      if (!history.rebuild(previousCircuit, previousBits, previousRoot,
                           request.width))
        return SamplerResult::Invalid;
      ModelCounter previous(previousCircuit, previousBits);
      if (!previous.build(previousRoot))
        return SamplerResult::Invalid;
      uint64_t previousCardinality = previous.count().word(0);
      unsigned previousWidth = permutationWidth(previousCardinality);
      if (!previousCardinality || (position >> previousWidth) ||
          (history.complete && position))
        return SamplerResult::Invalid;
      uint64_t steps =
          history.complete ? uint64_t{1} << previousWidth : position;
      for (uint64_t step = 0; step != steps; ++step) {
        uint64_t next, rank = 0;
        if (previousWidth &&
            talos_rt_v1_random_cycle_next(key, step, previousWidth, &next,
                                            &rank) != TALOS_RT_OK)
          return SamplerResult::Invalid;
        if (rank < previousCardinality)
          history.insert(fieldValue(previous.assignment(ModelCount(rank))));
      }
      position = 0;
      history.complete = false;
    } else if (history.complete) {
      // Every legal value of the unchanged domain was returned.
      history.returned.clear();
      history.complete = false;
    }
    history.present = true;
    history.identity = identity;
    // The iteration ends when every legal value has been returned.
    if (!history.returned.empty()) {
      uint64_t returnedLegal = 0;
      std::vector<bool> bits(request.width);
      for (uint32_t value : history.returned) {
        for (unsigned bit = 0; bit != request.width; ++bit)
          bits[bit] = (value >> bit) & 1;
        returnedLegal += domain.contains(bits);
      }
      if (returnedLegal == cardinality)
        history.returned.clear();
    }
    unsigned permutation = permutationWidth(cardinality);
    if (position >> permutation)
      return SamplerResult::Invalid;
    // Continue the domain's permutation to the next value this iteration has
    // not returned. A pass that wraps without one ends the iteration.
    uint64_t value = 0;
    auto advance = [&](bool &found) {
      found = false;
      do {
        uint64_t rank = 0;
        if (permutation) {
          if (position == 0)
            key = talos_rt_v1_random_state_next64(&nextState);
          if (talos_rt_v1_random_cycle_next(key, position, permutation,
                                              &position,
                                              &rank) != TALOS_RT_OK)
            return false;
          if (rank >= cardinality)
            continue;
        }
        value = fieldValue(domain.assignment(ModelCount(rank)));
        found = !history.contains(value);
      } while (!found && position != 0);
      return true;
    };
    bool found;
    if (!advance(found))
      return SamplerResult::Invalid;
    if (!found) {
      history.returned.clear();
      if (!advance(found) || !found)
        return SamplerResult::Invalid;
    }
    history.complete = position == 0;
    size_t words = (uint64_t(width) + 63) / 64;
    std::copy_n(start, words, output);
    for (unsigned bit = 0; bit != request.width; ++bit) {
      unsigned physical = request.offset + bit;
      uint64_t mask = uint64_t{1} << (physical % 64);
      output[physical / 64] =
          (output[physical / 64] & ~mask) | (((value >> bit) & 1) ? mask : 0);
    }
    history.segment = std::move(snapshot);
    request.key = key;
    request.position = position;
    request.domain = history.write();
    state = nextState;
    return SamplerResult::Satisfiable;
  }

  SamplerResult
  weighted(unsigned width, const Bits &variable, const std::vector<ID> &bits,
           const std::vector<unsigned> &physicalBits,
           const std::vector<WeightedGroup> &groups,
           const std::vector<ID> &guards,
           const std::vector<Bits> &expressionWeights,
           const std::vector<bool> &retainedSoft,
           uint64_t constraintMask, const uint64_t *captures,
           const uint32_t *captureWidths,
           const std::vector<size_t> &captureOffsets, const uint64_t *start,
           const uint64_t *mutableMask, talos_rt_random_state_v1 &state,
           const uint64_t *masks, size_t layerCount, uint64_t *output) {
    std::vector<bool> active(groups.size(), false);
    for (size_t index = 0; index != groups.size(); ++index)
      active[index] =
          (groups[index].constraintBlock ==
               TALOS_RT_RANDOM_UNMASKED_CONSTRAINT_V1 ||
           !((constraintMask >> groups[index].constraintBlock) & 1)) &&
          (groups[index].softPriority == UINT32_MAX ||
           (groups[index].softPriority < retainedSoft.size() &&
            retainedSoft[groups[index].softPriority]));

    // Distributions over independent inputs factor: their joint measure is
    // the product of per-component measures. Sampling each component alone
    // combines items only across distributions that share a component, not
    // across every distribution in the relation.
    InputPartition partition(circuit, bits.size());
    std::vector<ID> parts = conjuncts();
    std::vector<size_t> partInputs;
    partInputs.reserve(parts.size());
    for (ID part : parts)
      partInputs.push_back(partition.join(part));
    std::vector<size_t> groupInputs(groups.size(), InputPartition::none);
    for (size_t index = 0; index != groups.size(); ++index) {
      if (!active[index])
        continue;
      const WeightedGroup &group = groups[index];
      size_t input = InputPartition::none;
      for (size_t bit = 0; bit != bits.size(); ++bit)
        if (physicalBits[bit] >= group.targetOffset &&
            physicalBits[bit] - group.targetOffset < group.width)
          input = partition.unite(input, bit);
      if (input == InputPartition::none)
        continue;
      if (index < guards.size() && guards[index] != missing)
        input = partition.unite(input, partition.join(guards[index]));
      for (const WeightedItem &item : group.items)
        if (item.expressionWeight)
          for (ID weightBit : expressionWeights[item.weightCapture])
            input = partition.unite(input, partition.join(weightBit));
      groupInputs[index] = input;
    }
    std::vector<size_t> components;
    for (size_t input : groupInputs)
      if (input != InputPartition::none)
        components.push_back(partition.find(input));
    std::sort(components.begin(), components.end());
    components.erase(std::unique(components.begin(), components.end()),
                     components.end());

    auto owner = [&](size_t input) {
      if (input == InputPartition::none)
        return components.size();
      size_t root = partition.find(input);
      auto found = std::lower_bound(components.begin(), components.end(), root);
      return found != components.end() && *found == root
                 ? size_t(found - components.begin())
                 : components.size();
    };
    bool remainder = false;
    for (size_t input : partInputs)
      remainder = remainder || owner(input) == components.size();

    talos_rt_random_state_v1 nextState = state;
    std::vector<bool> value;
    // Conjuncts are split before restriction; prioritized stages that fix
    // inputs never reach weighted sampling.
    if ((components.size() == 1 && !remainder) ||
        std::find(fixed.begin(), fixed.end(), true) != fixed.end()) {
      SamplerResult result = weightedComponent(
          predicate(), active, width, variable, bits, physicalBits, groups,
          guards, expressionWeights, captures, captureWidths, captureOffsets,
          masks, layerCount, nextState, value);
      if (result != SamplerResult::Satisfiable)
        return result;
    } else {
      // Components holding a distribution are sampled in input order, then
      // the remaining relation, which separates further on its own.
      value.assign(bits.size(), false);
      for (size_t component = 0; component != components.size(); ++component) {
        ID relation = 1;
        for (size_t index = 0; index != parts.size(); ++index)
          if (owner(partInputs[index]) == component)
            relation = circuit.conjunction(relation, parts[index]);
        std::vector<bool> included(groups.size(), false);
        for (size_t index = 0; index != groups.size(); ++index)
          included[index] = groupInputs[index] != InputPartition::none &&
                            owner(groupInputs[index]) == component;
        std::vector<bool> sampled;
        SamplerResult result = weightedComponent(
            relation, included, width, variable, bits, physicalBits, groups,
            guards, expressionWeights, captures, captureWidths, captureOffsets,
            masks, layerCount, nextState, sampled);
        if (result != SamplerResult::Satisfiable)
          return result;
        for (size_t bit = 0; bit != bits.size(); ++bit)
          if (owner(bit) == component)
            value[bit] = sampled[bit];
      }
      // Inputs of a distribution component are fixed in the remaining
      // relation, which never reads them, so they consume no draw there.
      Formula rest(circuit, bits);
      rest.physicalBits = physicalBits;
      rest.auxiliarySupport = auxiliarySupport;
      rest.setFreeFields(freeFields);
      for (size_t index = 0; index != parts.size(); ++index)
        if (owner(partInputs[index]) == components.size())
          rest.require(parts[index]);
      for (size_t bit = 0; bit != bits.size(); ++bit)
        rest.fixed[bit] = owner(bit) != components.size();
      SamplerResult result = rest.layered(width, masks, layerCount, nextState);
      if (result != SamplerResult::Satisfiable)
        return result == SamplerResult::Unsatisfiable ? SamplerResult::Invalid
                                                      : result;
      for (size_t bit = 0; bit != bits.size(); ++bit)
        if (owner(bit) == components.size())
          value[bit] = rest.fixedValues[bit];
    }
    size_t words = (uint64_t(width) + 63) / 64;
    for (size_t word = 0; word != words; ++word)
      output[word] = start[word] & ~mutableMask[word];
    for (size_t bit = 0; bit != value.size(); ++bit)
      if (value[bit] && physicalBits[bit] < width)
        output[physicalBits[bit] / 64] |= uint64_t{1}
                                          << (physicalBits[bit] % 64);
    if (width % 64)
      output[words - 1] &= (uint64_t{1} << (width % 64)) - 1;
    state = nextState;
    return SamplerResult::Satisfiable;
  }

  /// Sample the inputs of one relation under the included distributions.
  /// Inputs the relation does not read are sampled uniformly; a caller that
  /// partitions the relation keeps only its component's values.
  SamplerResult weightedComponent(
      ID predicate, const std::vector<bool> &included, unsigned width,
      const Bits &variable, const std::vector<ID> &bits,
      const std::vector<unsigned> &physicalBits,
      const std::vector<WeightedGroup> &groups, const std::vector<ID> &guards,
      const std::vector<Bits> &expressionWeights, const uint64_t *captures,
      const uint32_t *captureWidths, const std::vector<size_t> &captureOffsets,
      const uint64_t *masks, size_t layerCount,
      talos_rt_random_state_v1 &nextState, std::vector<bool> &value) {
    ModelCounter counter(circuit, bits);
    if (!counter.build(predicate))
      return SamplerResult::Invalid;
    size_t words = (uint64_t(width) + 63) / 64;
    std::vector<bool> accepted(bits.size(), false);
    struct Choice {
      ModelCounter region;
      ModelCount mass, count, coefficient;
    };
    struct Term {
      std::vector<size_t> choices;
      ModelCount mass;
    };
    for (size_t layer = 0; layer != std::max<size_t>(1, layerCount); ++layer) {
      std::vector<bool> keep(bits.size(), false);
      for (size_t bit = 0; bit != bits.size(); ++bit)
        keep[bit] =
            !accepted[bit] && physicalBits[bit] < width &&
            (!layerCount || ((masks[layer * words + physicalBits[bit] / 64] >>
                              (physicalBits[bit] % 64)) &
                             1));
      // An auxiliary target bit is decided with the last of its inputs.
      for (size_t bit = 0; bit != bits.size(); ++bit)
        if (physicalBits[bit] >= width)
          keep[bit] = !accepted[bit] &&
                      std::all_of(auxiliarySupport[bit].begin(),
                                  auxiliarySupport[bit].end(),
                                  [&](size_t input) {
                                    return accepted[input] || keep[input];
                                  });
      std::vector<bool> targets(bits.size(), false);
      std::vector<std::vector<Choice>> choices;
      for (size_t groupIndex = 0; groupIndex != groups.size(); ++groupIndex) {
        if (!included[groupIndex])
          continue;
        const WeightedGroup &group = groups[groupIndex];
        std::vector<bool> target(bits.size(), false);
        bool current = false, later = false;
        for (size_t bit = 0; bit != bits.size(); ++bit) {
          target[bit] = !accepted[bit] &&
                        physicalBits[bit] >= group.targetOffset &&
                        physicalBits[bit] - group.targetOffset < group.width;
          current |= target[bit] && keep[bit];
          later |= target[bit] && !keep[bit];
        }
        if (!current)
          continue;
        // The compiler requires each distributed target to belong to one
        // solve layer. Partially ordered target paths need a posterior measure
        // carried across their layers rather than fresh range normalization.
        if (later)
          return SamplerResult::Invalid;
        ID guard = groupIndex < guards.size() && guards[groupIndex] != missing
                       ? guards[groupIndex]
                       : 1;
        ModelCounter active = counter;
        ModelCounter inactive = counter;
        if (!active.constrain(guard) || !inactive.constrain(guard ^ 1))
          return SamplerResult::Invalid;
        if (active.count().zero())
          continue;
        // Activation is state or an already accepted ordering layer. A guard
        // that still varies needs a conditional, scale-invariant measure.
        if (!inactive.count().zero())
          return SamplerResult::Invalid;
        std::vector<Choice> eligible;
        ModelCount denominator(1);
        Bits field(variable.begin() + group.targetOffset,
                   variable.begin() + group.targetOffset + group.width);
        if (group.isSigned)
          field.back() ^= 1;
        for (const WeightedItem &item : group.items) {
          unsigned weightWidth =
              captureWidths ? captureWidths[item.weightCapture] : 64;
          size_t weightWords = (uint64_t(weightWidth) + 63) / 64;
          ModelCount mass;
          if (!item.expressionWeight) {
            std::vector<uint64_t> weight(
                captures + captureOffsets[item.weightCapture],
                captures + captureOffsets[item.weightCapture] + weightWords);
            if (weightWidth % 64)
              weight.back() &= (uint64_t{1} << (weightWidth % 64)) - 1;
            mass = ModelCount::fromWords(weight.data(), weight.size());
          }
          if ((!item.expressionWeight && mass.zero()) || item.intervals.empty())
            continue;
          ID region = 0;
          for (const WeightedInterval &interval : item.intervals) {
            ID match = 1;
            if (interval.cardinality != 0) {
              match = circuit.conjunction(
                  atLeast(field, interval.lower),
                  atMost(field, interval.lower + interval.cardinality - 1));
            }
            region = circuit.disjunction(region, match);
          }
          ModelCounter range = counter;
          if (!range.constrain(region))
            return SamplerResult::Invalid;
          range.project(target);
          ModelCount count = range.count();
          if (count.zero())
            continue;
          denominator = denominator.multiplied(count);
          auto originalMass = [&](ModelCount value) {
            if (!item.perRange) {
              uint64_t cardinality = item.intervals.front().cardinality;
              value =
                  value.multiplied(cardinality ? ModelCount(cardinality)
                                               : ModelCount::powerOfTwo(64));
            }
            return value;
          };
          if (item.expressionWeight) {
            const Bits &weight = expressionWeights[item.weightCapture];
            if (weight.size() != weightWidth)
              return SamplerResult::Invalid;
            // An unsigned expression is the sum of its set-bit masses.
            // Every bit uses the original item's legal-value denominator,
            // including values where that particular bit is zero.
            for (unsigned bit = 0; bit != weightWidth; ++bit) {
              if (weight[bit] == 0)
                continue;
              ModelCounter weightedRange = counter;
              if (!weightedRange.constrain(
                      circuit.conjunction(region, weight[bit])))
                return SamplerResult::Invalid;
              weightedRange.project(target);
              if (weightedRange.count().zero())
                continue;
              // Each weight bit must be determined by this target and the
              // accepted prefix. Reject unresolved companion-dependent roots
              // rather than assigning their existential projection a mass.
              ModelCounter zeroRange = counter;
              if (!zeroRange.constrain(
                      circuit.conjunction(region, weight[bit] ^ 1)))
                return SamplerResult::Invalid;
              zeroRange.project(target);
              ModelCount covered = weightedRange.count();
              covered.add(zeroRange.count());
              // Both projections cover the original range. Their counts add
              // to its count exactly when their target values are disjoint.
              if (covered.compare(count) != 0)
                return SamplerResult::Invalid;
              eligible.push_back({std::move(weightedRange),
                                  originalMass(ModelCount::powerOfTwo(bit)),
                                  count, ModelCount()});
            }
          } else
            eligible.push_back({std::move(range), originalMass(std::move(mass)),
                                count, ModelCount()});
        }
        if (eligible.empty())
          return SamplerResult::Invalid;
        // Each source item's original mass is shared by its legal target
        // values. A common denominator keeps the resulting ratios integral
        // without a fixed-width normalization or rounding step.
        for (Choice &choice : eligible)
          choice.coefficient =
              choice.mass.multiplied(denominator.divided(choice.count));
        choices.push_back(std::move(eligible));
        for (size_t bit = 0; bit != bits.size(); ++bit)
          targets[bit] = targets[bit] || target[bit];
      }
      if (!choices.empty()) {
        // Coupled distributions combine their items. Exact weights need every
        // combination's legal count, so bound that work and report it rather
        // than spinning inside randomize().
        size_t combinations = 1;
        for (const std::vector<Choice> &group : choices) {
          if (combinations > maxDistributionTerms / group.size())
            return SamplerResult::Exhausted;
          combinations *= group.size();
        }
        std::vector<size_t> indices(choices.size(), 0);
        std::vector<Term> terms;
        ModelCount total;
        for (;;) {
          ModelCounter branch = counter;
          ModelCount coefficient(1);
          for (size_t group = 0; group != choices.size(); ++group) {
            const Choice &choice = choices[group][indices[group]];
            if (!branch.intersect(choice.region))
              return SamplerResult::Invalid;
            coefficient = coefficient.multiplied(choice.coefficient);
          }
          branch.project(targets);
          ModelCount mass = coefficient.multiplied(branch.count());
          if (!mass.zero()) {
            total.add(mass);
            terms.push_back({indices, std::move(mass)});
          }
          size_t index = indices.size();
          while (index != 0) {
            --index;
            if (++indices[index] != choices[index].size())
              break;
            indices[index] = 0;
          }
          if (index == 0 && indices.front() == 0)
            break;
        }
        if (total.zero())
          return SamplerResult::Invalid;
        ModelCount rank = ModelCount::randomBelow(total, nextState);
        const Term *selected = nullptr;
        for (const Term &term : terms) {
          if (rank.compare(term.mass) < 0) {
            selected = &term;
            break;
          }
          rank.subtract(term.mass);
        }
        if (!selected)
          return SamplerResult::Invalid;
        ModelCounter branch = counter;
        for (size_t group = 0; group != choices.size(); ++group)
          if (!branch.intersect(
                  choices[group][selected->choices[group]].region))
            return SamplerResult::Invalid;
        branch.project(targets);
        auto selection = branch.assignment(
            ModelCount::randomBelow(branch.count(), nextState));
        counter.restrict(selection, targets);
      }
      counter.project(keep);
      ModelCount count = counter.count();
      if (count.zero())
        return SamplerResult::Invalid;
      auto chosen =
          counter.assignment(ModelCount::randomBelow(count, nextState));
      counter.restrict(chosen, keep);
      for (size_t bit = 0; bit != bits.size(); ++bit)
        accepted[bit] = accepted[bit] || keep[bit];
    }
    counter.project(std::vector<bool>(bits.size(), true));
    ModelCount count = counter.count();
    if (count.zero())
      return SamplerResult::Invalid;
    value = counter.assignment(ModelCount::randomBelow(count, nextState));
    return SamplerResult::Satisfiable;
  }

  /// The first layer's projection is empty exactly when the relation is, so
  /// an unsatisfiable relation is reported without a separate check.
  SamplerResult ordered(unsigned width, const uint64_t *start,
                        const uint64_t *mutableMask,
                        talos_rt_random_state_v1 &state,
                        const uint64_t *masks, size_t layerCount,
                        uint64_t *output) {
    // The unspecified remaining fields retain the ordinary joint distribution
    // conditional on the accepted earlier-layer values.
    talos_rt_random_state_v1 nextState = state;
    SamplerResult status = layered(width, masks, layerCount, nextState);
    if (status != SamplerResult::Satisfiable)
      return status;
    size_t words = (uint64_t(width) + 63) / 64;
    for (size_t index = 0; index != words; ++index)
      output[index] = start[index] & ~mutableMask[index];
    for (size_t bit = 0; bit != bits.size(); ++bit)
      if (fixedValues[bit])
        output[physicalBits[bit] / 64] |= uint64_t{1}
                                          << (physicalBits[bit] % 64);
    if (width % 64)
      output[words - 1] &= (uint64_t{1} << (width % 64)) - 1;
    state = nextState;
    return SamplerResult::Satisfiable;
  }

  bool projection(const OrderMask &mask, talos_rt_random_state_v1 &state,
                  OrderMask &output) {
    std::vector<bool> keep(bits.size(), false);
    for (size_t bit = 0; bit != bits.size(); ++bit)
      keep[bit] =
          !fixed[bit] &&
          ((mask[physicalBits[bit] / 64] >> (physicalBits[bit] % 64)) & 1);
    // Each stage checks its relation before choosing its layers.
    if (choose(keep, state, true) != SamplerResult::Satisfiable)
      return false;
    for (size_t index = 0; index != bits.size(); ++index) {
      if (!keep[index])
        continue;
      unsigned bit = physicalBits[index];
      uint64_t field = uint64_t{1} << (bit % 64);
      output[bit / 64] =
          (output[bit / 64] & ~field) | (fixedValues[index] ? field : 0);
    }
    return true;
  }

  SamplerResult
  prioritized(const Bits &bits, const std::vector<unsigned> &physicalBits,
              const std::vector<ID> &hard, const std::vector<ID> &soft,
              const std::vector<const PriorityInput *> &hardInputs,
              const std::vector<const PriorityInput *> &softInputs,
              const std::vector<ID> &domains,
              const std::vector<SolveOrderEdge> &edges, const uint64_t *start,
              const uint64_t *mutableMask, size_t wordCount,
              uint64_t disabledBlocks, talos_rt_random_state_v1 &state,
              uint64_t *output) {
    OrderMask arguments(wordCount, 0);
    for (const PriorityInput *input : hardInputs)
      if (input)
        orderUnite(arguments, input->arguments);
    for (const PriorityInput *input : softInputs)
      if (input)
        orderUnite(arguments, input->arguments);
    std::vector<OrderMask> stages;
    if (!buildSolveOrderLayers(edges,
                               OrderMask(mutableMask, mutableMask + wordCount),
                               disabledBlocks, true, stages, false, &arguments))
      return SamplerResult::Unavailable;
    std::vector<size_t> inputStages(bits.size(), 0);
    for (size_t bit = 0; bit != bits.size(); ++bit)
      for (size_t stage = 0; stage != stages.size(); ++stage)
        if ((stages[stage][physicalBits[bit] / 64] >>
             (physicalBits[bit] % 64)) &
            1)
          inputStages[bit] = stage;
    auto stageFor = [&](ID predicate, const PriorityInput *input,
                        size_t &stage) {
      stage = 0;
      if (input) {
        for (size_t bit = 0; bit != bits.size(); ++bit) {
          unsigned physical = physicalBits[bit];
          if ((input->references[physical / 64] >> (physical % 64)) & 1)
            stage = std::max(stage, inputStages[bit]);
          if ((input->arguments[physical / 64] >> (physical % 64)) & 1)
            stage = std::max(stage, inputStages[bit] + 1);
        }
      } else {
        std::unordered_set<ID> visited;
        std::vector<ID> pending{predicate};
        while (!pending.empty()) {
          ID id = pending.back() / 2;
          pending.pop_back();
          if (!visited.insert(id).second || !id)
            continue;
          const auto &node = circuit.getNodes()[id];
          if (node.opcode == PlanOpcode::RandomBit)
            stage = std::max(stage, inputStages[node.b]);
          else {
            pending.push_back(node.a);
            pending.push_back(node.b);
          }
        }
      }
      return stage < stages.size();
    };
    std::vector<size_t> hardStages(hard.size()), softStages(soft.size());
    for (size_t index = 0; index != hard.size(); ++index)
      if (!stageFor(hard[index], hardInputs[index], hardStages[index]))
        return SamplerResult::Unavailable;
    for (size_t index = 0; index != soft.size(); ++index)
      if (soft[index] != missing &&
          !stageFor(soft[index], softInputs[index], softStages[index]))
        return SamplerResult::Unavailable;
    talos_rt_random_state_v1 nextState = state;
    OrderMask assignment(start, start + wordCount);
    for (size_t stage = 0; stage != stages.size(); ++stage) {
      requirements.clear();
      checkpoints.clear();
      for (ID domain : domains)
        require(domain);
      for (size_t index = 0; index != hard.size(); ++index)
        if (hardStages[index] == stage)
          require(hard[index]);
      auto status = check();
      if (status != SamplerResult::Satisfiable)
        return status;
      for (size_t priority = soft.size(); priority-- != 0;) {
        if (soft[priority] == missing || softStages[priority] != stage)
          continue;
        push();
        require(soft[priority]);
        status = check();
        if (status == SamplerResult::Invalid)
          return status;
        if (status == SamplerResult::Unsatisfiable)
          pop();
      }
      std::vector<OrderMask> layers;
      if (!buildSolveOrderLayers(edges, stages[stage], disabledBlocks, false,
                                 layers, true))
        return SamplerResult::Unavailable;
      if (layers.empty())
        layers.push_back(stages[stage]);
      for (const OrderMask &layer : layers)
        if (!projection(layer, nextState, assignment))
          return SamplerResult::Invalid;
      for (size_t bit = 0; bit != bits.size(); ++bit)
        if ((stages[stage][physicalBits[bit] / 64] >>
             (physicalBits[bit] % 64)) &
            1) {
          fixed[bit] = true;
          fixedValues[bit] =
              (assignment[physicalBits[bit] / 64] >> (physicalBits[bit] % 64)) &
              1;
        }
    }
    std::copy(assignment.begin(), assignment.end(), output);
    state = nextState;
    return SamplerResult::Satisfiable;
  }

  BooleanCircuit &circuit;
  const Bits &bits;
  std::vector<unsigned> physicalBits;
  // Inputs that an auxiliary expression-target bit is defined over; empty for
  // assignment bits.
  std::vector<std::vector<size_t>> auxiliarySupport;
  std::vector<bool> fixed, fixedValues;
  std::vector<ID> requirements;
  std::vector<size_t> checkpoints;
  NodeScratch scratch;
  std::vector<FreeField> freeFields;
  random::FlatMap<ID, size_t> freeFieldIndex;
  std::vector<size_t> ordinals;
};

} // namespace

SamplerResult sampleRandomPlan(
    const uint8_t *program, size_t programSize, const uint64_t *start,
    const uint64_t *mutableMask, size_t assignmentWords,
    uint64_t constraintMask, const uint64_t *captures,
    const uint32_t *captureWidths, size_t captureCount, uint64_t *assignment,
    talos_rt_random_state_v1 *randomState, const uint64_t *orderedMasks,
    size_t orderedLayerCount, CyclicSampleRequest *cyclic) {
  if (programSize < TALOS_RT_RANDOM_PROGRAM_HEADER_SIZE ||
      read32(program + 16) != captureCount ||
      !(read32(program + 20) & TALOS_RT_RANDOM_PROGRAM_HAS_SAMPLER_PLAN))
    return SamplerResult::Unavailable;
  random::SamplerPlanView plan;
  if (!random::readSamplerPlan(program, programSize, captureWidths, plan))
    return SamplerResult::Invalid;
  size_t fullProgramSize = programSize;
  programSize = plan.coreSize;
  bool wide = read16(program + 4) == TALOS_RT_RANDOM_PROGRAM_VERSION_V2;
  unsigned header = wide ? TALOS_RT_RANDOM_PROGRAM_HEADER_SIZE_V2
                         : TALOS_RT_RANDOM_PROGRAM_HEADER_SIZE;
  unsigned width = read32(program + 8), instructions = read32(program + 12);
  unsigned flags = read32(program + 20);
  bool uniform = (flags & TALOS_RT_RANDOM_PROGRAM_EXACT_UNIFORM) != 0;
  bool weighted = (flags & TALOS_RT_RANDOM_PROGRAM_EXACT_WEIGHTED) != 0;
  bool exact = uniform || weighted;
  bool functionPriority =
      (flags & TALOS_RT_RANDOM_PROGRAM_HAS_FUNCTION_PRIORITY) != 0;
  bool dynamicMasks =
      (flags & TALOS_RT_RANDOM_PROGRAM_HAS_GUARD_INPUT_MASKS) != 0;
  if (assignmentWords != std::max<uint64_t>(1, (uint64_t(width) + 63) / 64) ||
      ((flags & TALOS_RT_RANDOM_PROGRAM_HAS_DIST) && !weighted) ||
      (weighted &&
       (!(flags & TALOS_RT_RANDOM_PROGRAM_HAS_DIST) || functionPriority)) ||
      !exact || !randomState || (orderedLayerCount && !orderedMasks))
    return SamplerResult::Unavailable;
  std::vector<size_t> captureOffsets(captureCount);
  for (size_t capture = 1; capture < captureCount; ++capture)
    captureOffsets[capture] =
        captureOffsets[capture - 1] +
        (captureWidths ? (uint64_t(captureWidths[capture - 1]) + 63) / 64 : 1);

  // The compiler chooses the variable order once. Binding modes may remove
  // inputs, but preserves that order for every conditional projection.
  std::vector<unsigned> orderedBits(width);
  for (unsigned index = 1; index != plan.nodeCount; ++index) {
    const uint8_t *p = plan.nodes + size_t(index) * random::samplerPlanNodeSize;
    if (static_cast<PlanOpcode>(read32(p)) == PlanOpcode::RandomBit)
      orderedBits[read32(p + 8)] = read32(p + 4);
  }
  BooleanCircuit circuit;
  // Specialization creates about one gate per plan node.
  circuit.reserve(plan.nodeCount);
  Bits variable(width), booleanBits;
  std::vector<unsigned> physicalBits;
  for (unsigned physical : orderedBits) {
    if ((mutableMask[physical / 64] >> (physical % 64)) & 1) {
      variable[physical] =
          circuit.input(PlanOpcode::RandomBit, physical, booleanBits.size());
      booleanBits.push_back(variable[physical]);
      physicalBits.push_back(physical);
    } else
      variable[physical] = (start[physical / 64] >> (physical % 64)) & 1;
  }
  std::vector<ID> specialized(plan.nodeCount);
  struct RealCell {
    std::array<uint64_t, TALOS_RT_REAL64_RANK_WORDS> first{}, last{};
  };
  std::unordered_map<uint64_t, RealCell> realCells;
  auto handle = [&](uint32_t id) { return specialized[id / 2] ^ (id & 1); };
  for (unsigned index = 1; index != plan.nodeCount; ++index) {
    const uint8_t *p = plan.nodes + size_t(index) * random::samplerPlanNodeSize;
    unsigned a = read32(p + 4), b = read32(p + 8);
    switch (static_cast<PlanOpcode>(read32(p))) {
    case PlanOpcode::RandomBit:
      specialized[index] = variable[a];
      break;
    case PlanOpcode::CaptureBit:
      specialized[index] =
          (captures[captureOffsets[a] + b / 64] >> (b % 64)) & 1;
      break;
    case PlanOpcode::ConstraintEnabled:
      specialized[index] = ((constraintMask >> a) & 1) == 0;
      break;
    case PlanOpcode::Real32FirstBit:
    case PlanOpcode::Real32LastBit:
    case PlanOpcode::Real64FirstBit:
    case PlanOpcode::Real64LastBit:
    case PlanOpcode::Real32From64FirstBit:
    case PlanOpcode::Real32From64LastBit: {
      unsigned precision =
          random::realCellPrecision(static_cast<PlanOpcode>(read32(p)));
      bool narrowed =
          random::realCellNarrowed(static_cast<PlanOpcode>(read32(p)));
      uint64_t key = (uint64_t(a) << 2) | (narrowed ? 2 : precision == 64);
      auto [entry, inserted] = realCells.try_emplace(key);
      if (inserted) {
        uint64_t payload = captures[captureOffsets[a]];
        if (precision == 32) {
          uint32_t word = narrowed ? narrowReal64Payload(payload)
                                   : static_cast<uint32_t>(payload);
          float value;
          std::memcpy(&value, &word, sizeof(value));
          RealCoordinates<float>::cell(value, entry->second.first.data(),
                                       entry->second.last.data());
        } else {
          double value;
          std::memcpy(&value, &payload, sizeof(value));
          RealCoordinates<double>::cell(value, entry->second.first.data(),
                                        entry->second.last.data());
        }
      }
      bool first =
          static_cast<PlanOpcode>(read32(p)) == PlanOpcode::Real32FirstBit ||
          static_cast<PlanOpcode>(read32(p)) ==
              PlanOpcode::Real32From64FirstBit ||
          static_cast<PlanOpcode>(read32(p)) == PlanOpcode::Real64FirstBit;
      const auto &words = first ? entry->second.first : entry->second.last;
      specialized[index] = (words[b / 64] >> (b % 64)) & 1;
      break;
    }
    case PlanOpcode::And:
      specialized[index] = circuit.conjunction(handle(a), handle(b));
      break;
    default:
      return SamplerResult::Invalid;
    }
  }
  std::vector<ID> roots(instructions, missing);
  for (unsigned index = 0; index != plan.rootCount; ++index) {
    const uint8_t *p = plan.roots + size_t(index) * random::samplerPlanRootSize;
    roots[read32(p)] = handle(read32(p + 4));
  }
  std::vector<ID> soft;
  std::vector<ID> priorityHard, priorityDomains;
  size_t literals = header + size_t(instructions) * 16;
  size_t domainsOffset =
      literals + (wide ? size_t(read32(program + 24)) * 8 : 0);
  if (flags & TALOS_RT_RANDOM_PROGRAM_HAS_SOLVE_BEFORE)
    domainsOffset += TALOS_RT_RANDOM_SOLVE_EDGE_HEADER_SIZE +
                     size_t(read32(program + domainsOffset)) *
                         (wide ? assignmentWords * 16 +
                                     TALOS_RT_RANDOM_SOLVE_EDGE_TAIL_SIZE_V2
                               : TALOS_RT_RANDOM_SOLVE_EDGE_SIZE);

  PriorityInputs sourceInputs;
  if (functionPriority &&
      !readPriorityInputs(program, programSize, header, instructions, width,
                          domainsOffset, sourceInputs, captureCount,
                          dynamicMasks))
    return SamplerResult::Unavailable;
  for (auto &entry : sourceInputs) {
    PriorityInput &input = entry.second;
    if (!input.maskCapture)
      continue;
    size_t capture = input.maskCapture - 1;
    for (OrderMask *row : {&input.references, &input.arguments}) {
      size_t captureOffset = wide ? captureOffsets[capture] : capture;
      unsigned captureWidth = wide ? captureWidths[capture] : 64;
      size_t captureWords = (uint64_t(captureWidth) + 63) / 64;
      for (size_t word = 0; word != assignmentWords; ++word) {
        uint64_t mask =
            word < captureWords ? captures[captureOffset + word] : 0;
        if (word + 1 == captureWords && captureWidth % 64)
          mask &= (uint64_t{1} << (captureWidth % 64)) - 1;
        (*row)[word] &= mask;
      }
      ++capture;
    }
    for (size_t word = 0; word != assignmentWords; ++word)
      if (input.arguments[word] & ~input.references[word])
        return SamplerResult::Unavailable;
  }
  std::vector<WeightedGroup> weightedGroups;
  if (weighted) {
    if (domainsOffset > programSize ||
        programSize - domainsOffset < TALOS_RT_RANDOM_DIST_HEADER_SIZE)
      return SamplerResult::Unavailable;
    size_t records = read32(program + domainsOffset + 4);
    if (records >
        (programSize - domainsOffset - TALOS_RT_RANDOM_DIST_HEADER_SIZE) /
            TALOS_RT_RANDOM_DIST_RECORD_SIZE)
      return SamplerResult::Unavailable;
    size_t footerSize = TALOS_RT_RANDOM_DIST_HEADER_SIZE +
                        records * TALOS_RT_RANDOM_DIST_RECORD_SIZE;
    if (!parseWeightedGroups(program + domainsOffset, footerSize, width,
                             captureCount, weightedGroups) ||
        !resolveWeightedIntervals(
            weightedGroups, [&](uint64_t index, uint64_t &value) {
              if (captureWidths && captureWidths[index] != 64)
                return false;
              value = captures[captureOffsets[index]];
              return true;
            }))
      return SamplerResult::Unavailable;
    domainsOffset += footerSize;
  }
  // IEEE 1800-2023 18.5.3 distributes the value of any integral expression.
  // An expression target gets one auxiliary variable per exported bit,
  // constrained equal to that bit. Auxiliaries are numbered after the
  // assignment, are never written back, and leave every model count unchanged.
  std::vector<Bits> expressionTargets(weightedGroups.size());
  for (unsigned index = 0; index != instructions; ++index) {
    const uint8_t *p = program + header + size_t(index) * 16;
    if (p[0] != TALOS_RT_RANDOM_END_DIST_TARGET_BIT_V1)
      continue;
    unsigned operand = read32(p + (wide ? 8 : 4));
    uint64_t auxiliary = wide ? read32(p + 12) : read64(p + 8);
    if (roots[index] == missing || operand >= weightedGroups.size() ||
        !weightedGroups[operand].expressionTarget ||
        auxiliary != expressionTargets[operand].size())
      return SamplerResult::Invalid;
    expressionTargets[operand].push_back(roots[index]);
  }
  std::vector<ID> auxiliaryEquivalences;
  std::vector<std::vector<size_t>> auxiliarySupport(booleanBits.size());
  for (size_t group = 0; group != weightedGroups.size(); ++group) {
    WeightedGroup &distribution = weightedGroups[group];
    if (expressionTargets[group].size() !=
        (distribution.expressionTarget ? distribution.width : 0))
      return SamplerResult::Invalid;
    if (!distribution.expressionTarget)
      continue;
    std::vector<size_t> support = circuitSupport(circuit,
                                                 expressionTargets[group]);
    distribution.targetOffset = static_cast<uint32_t>(variable.size());
    for (ID bit : expressionTargets[group]) {
      ID input = circuit.input(PlanOpcode::RandomBit,
                               static_cast<uint32_t>(variable.size()),
                               static_cast<uint32_t>(booleanBits.size()));
      physicalBits.push_back(static_cast<unsigned>(variable.size()));
      variable.push_back(input);
      booleanBits.push_back(input);
      auxiliarySupport.push_back(support);
      auxiliaryEquivalences.push_back(circuit.exclusiveOr(input, bit) ^ 1);
    }
  }
  Formula f(circuit, booleanBits);
  f.physicalBits = physicalBits;
  f.auxiliarySupport = std::move(auxiliarySupport);
  std::vector<Formula::FreeField> freeFields;
  for (uint32_t index = 0; index != plan.freeFieldCount; ++index) {
    const uint8_t *p =
        plan.freeFields + size_t(index) * random::samplerFreeFieldSize;
    freeFields.push_back({handle(read32(p)), read32(p + 4), read32(p + 8)});
  }
  f.setFreeFields(std::move(freeFields));
  for (ID equivalence : auxiliaryEquivalences)
    f.require(equivalence);
  std::vector<ID> distributionGuards(weightedGroups.size(), missing);
  std::vector<Bits> expressionWeights(captureCount);
  std::vector<const PriorityInput *> hardInputs;
  std::vector<const PriorityInput *> softInputs;
  for (unsigned index = 0; index != instructions; ++index) {
    const uint8_t *p = program + header + size_t(index) * 16;
    unsigned opcode = p[0];
    if (roots[index] == missing)
      continue;
    unsigned operand = read32(p + (wide ? 8 : 4));
    uint64_t auxiliary = wide ? read32(p + 12) : read64(p + 8);
    if (opcode == TALOS_RT_RANDOM_END_DIST_WEIGHT_BIT_V1) {
      if (!weighted || operand >= captureCount ||
          auxiliary != expressionWeights[operand].size())
        return SamplerResult::Invalid;
      expressionWeights[operand].push_back(roots[index]);
      continue;
    }
    if (opcode == TALOS_RT_RANDOM_END_DIST_TARGET_BIT_V1)
      continue;
    if (opcode == TALOS_RT_RANDOM_END_DIST_GUARD_V1) {
      if (!weighted || operand >= distributionGuards.size() ||
          distributionGuards[operand] != missing)
        return SamplerResult::Invalid;
      distributionGuards[operand] = roots[index];
      continue;
    }
    bool enabled = operand == TALOS_RT_RANDOM_UNMASKED_CONSTRAINT_V1 ||
                   (constraintMask & (uint64_t{1} << operand)) == 0;
    ID predicate = enabled ? roots[index] : 1;
    auto source = sourceInputs.find(index);
    const PriorityInput *input =
        enabled && source != sourceInputs.end() ? &source->second : nullptr;
    if (opcode == TALOS_RT_RANDOM_END_HARD_V1) {
      if (functionPriority) {
        priorityHard.push_back(predicate);
        hardInputs.push_back(input);
      } else
        f.require(predicate);
    } else {
      if (auxiliary >= instructions)
        return SamplerResult::Invalid;
      if (auxiliary >= soft.size()) {
        soft.resize(auxiliary + 1, missing);
        softInputs.resize(auxiliary + 1, nullptr);
      }
      soft[auxiliary] = predicate;
      softInputs[auxiliary] = input;
    }
  }
  for (const WeightedGroup &group : weightedGroups)
    for (const WeightedItem &item : group.items)
      if (item.expressionWeight &&
          expressionWeights[item.weightCapture].size() !=
              (captureWidths ? captureWidths[item.weightCapture] : 64))
        return SamplerResult::Invalid;
  if (flags & TALOS_RT_RANDOM_PROGRAM_HAS_DOMAINS) {
    size_t offset = domainsOffset;
    if (offset > programSize || programSize - offset < 8)
      return SamplerResult::Invalid;
    unsigned groups = read32(program + offset),
             records = read32(program + offset + 4);
    std::vector<ID> domains(groups, 0);
    std::vector<bool> active(groups, false);
    for (unsigned index = 0; index != records; ++index) {
      const uint8_t *p = program + offset + 8 + size_t(index) * 32;
      random::DomainRecord decoded;
      if (!random::readDomainRecord(p, width, wide, program + literals,
                                    wide ? read32(program + 24) : 0, nullptr,
                                    decoded) ||
          decoded.group >= groups)
        return SamplerResult::Invalid;
      unsigned group = decoded.group, target = decoded.targetOffset;
      ID match = 1;
      for (unsigned bit = 0; bit != decoded.width; ++bit) {
        active[group] =
            active[group] ||
            ((mutableMask[(target + bit) / 64] >> ((target + bit) % 64)) & 1) !=
                0;
        if ((decoded.maskWord(bit / 64) >> (bit % 64)) & 1)
          match = circuit.conjunction(
              match,
              variable[target + bit] ^
                  ID(!((decoded.valueWord(bit / 64) >> (bit % 64)) & 1)));
      }
      domains[group] = circuit.disjunction(domains[group], match);
    }
    for (unsigned group = 0; group != groups; ++group)
      if (active[group]) {
        if (functionPriority)
          priorityDomains.push_back(domains[group]);
        else
          f.require(domains[group]);
      }
  }
  if (cyclic) {
    if (!exact || functionPriority)
      return SamplerResult::Unavailable;
    // Only hard positive-weight support constrains a cyclic choice. Ordinary
    // soft preferences and probability masses are resolved after cyclic fields
    // are frozen. Support is a conjunction of groups, without a Cartesian
    // product of their weighted alternatives.
    for (size_t groupIndex = 0; groupIndex != weightedGroups.size();
         ++groupIndex) {
      const WeightedGroup &group = weightedGroups[groupIndex];
      if (group.softPriority != UINT32_MAX ||
          (group.constraintBlock != TALOS_RT_RANDOM_UNMASKED_CONSTRAINT_V1 &&
           ((constraintMask >> group.constraintBlock) & 1)))
        continue;
      Bits field(variable.begin() + group.targetOffset,
                 variable.begin() + group.targetOffset + group.width);
      if (group.isSigned)
        field.back() ^= 1;
      ID support = 0;
      for (const WeightedItem &item : group.items) {
        unsigned weightWidth =
            captureWidths ? captureWidths[item.weightCapture] : 64;
        ID positive = 0;
        if (item.expressionWeight) {
          if (expressionWeights[item.weightCapture].size() != weightWidth)
            return SamplerResult::Invalid;
          for (ID bit : expressionWeights[item.weightCapture])
            positive = circuit.disjunction(positive, bit);
        } else
          for (size_t word = 0; word != (uint64_t(weightWidth) + 63) / 64;
               ++word)
            if (captures[captureOffsets[item.weightCapture] + word] != 0)
              positive = 1;
        if (!positive)
          continue;
        for (const WeightedInterval &interval : item.intervals) {
          ID region = interval.cardinality == 0
                          ? 1
                          : circuit.conjunction(
                                f.atLeast(field, interval.lower),
                                f.atMost(field, interval.lower +
                                                    interval.cardinality - 1));
          support = circuit.disjunction(support,
                                        circuit.conjunction(region, positive));
        }
      }
      ID guard = distributionGuards[groupIndex] == missing
                     ? 1
                     : distributionGuards[groupIndex];
      f.require(circuit.disjunction(guard ^ 1, support));
    }
    return f.cyclic(*cyclic,
                    cyclicIdentity(program, fullProgramSize, constraintMask),
                    width, start, mutableMask, *randomState, assignment);
  }
  if (functionPriority) {
    size_t offset = literals + (wide ? size_t(read32(program + 24)) * 8 : 0);
    uint32_t count = read32(program + offset);
    const uint8_t *cursor =
        program + offset + TALOS_RT_RANDOM_SOLVE_EDGE_HEADER_SIZE;
    std::vector<SolveOrderEdge> edges;
    for (uint32_t index = 0; index != count; ++index) {
      SolveOrderEdge edge;
      for (OrderMask *mask : {&edge.before, &edge.after})
        for (size_t word = 0; word != assignmentWords; ++word) {
          mask->push_back(read64(cursor));
          cursor += 8;
        }
      edge.block = read32(cursor);
      edge.functionPriority =
          (read32(cursor + 4) &
           TALOS_RT_RANDOM_SOLVE_EDGE_FUNCTION_PRIORITY) != 0;
      cursor += 8;
      if (dynamicMasks && edge.functionPriority) {
        bool live = false;
        for (const auto &entry : sourceInputs) {
          const PriorityInput &input = entry.second;
          // The same enabled atom must retain both endpoints of an implicit
          // edge. A different call with the same argument does not activate
          // an edge whose guarded constraint was eliminated.
          uint32_t block = read32(program + header + size_t(entry.first) * 16 +
                                  (wide ? 8 : 4));
          if (block != TALOS_RT_RANDOM_UNMASKED_CONSTRAINT_V1 &&
              ((constraintMask >> block) & 1))
            continue;
          bool before = false, after = false;
          for (size_t word = 0; word != assignmentWords; ++word) {
            before |= (input.arguments[word] & edge.before[word]) != 0;
            after |= (input.references[word] & ~input.arguments[word] &
                      edge.after[word]) != 0;
          }
          live |= before && after;
        }
        if (!live)
          continue;
      }
      edges.push_back(std::move(edge));
    }
    return f.prioritized(booleanBits, physicalBits, priorityHard, soft,
                         hardInputs, softInputs, priorityDomains, edges, start,
                         mutableMask, assignmentWords, constraintMask,
                         *randomState, assignment);
  }
  bool hasSoft = std::any_of(soft.begin(), soft.end(),
                             [](ID predicate) { return predicate != missing; });
  // Preferences and distributions are decided against a satisfiable
  // relation; plain ordered sampling detects an empty one itself. The first
  // solve-before layer is decided first in any case; projecting onto it
  // lets the inputs it guards be checked apart.
  if (hasSoft || weighted) {
    std::vector<bool> separators(booleanBits.size(), false);
    if (orderedLayerCount)
      for (size_t bit = 0; bit != booleanBits.size(); ++bit)
        separators[bit] = physicalBits[bit] < width &&
                          ((orderedMasks[physicalBits[bit] / 64] >>
                            (physicalBits[bit] % 64)) &
                           1);
    auto result = f.check(&separators);
    if (result != SamplerResult::Satisfiable)
      return result;
  }
  // IEEE 1800-2023 18.5.13.1: retain each preference, highest priority
  // first, that stays satisfiable with the hard constraints and every
  // preference retained before it. Preferences over independent inputs are
  // decided in independent relations, one per input component.
  std::vector<bool> retainedSoft(soft.size(), false);
  if (hasSoft) {
    InputPartition partition(circuit, booleanBits.size());
    std::vector<ID> parts = f.conjuncts();
    std::vector<size_t> partInputs, softComponents(soft.size());
    for (ID part : parts)
      partInputs.push_back(partition.join(part));
    for (size_t priority = 0; priority != soft.size(); ++priority)
      softComponents[priority] = soft[priority] == missing
                                     ? InputPartition::none
                                     : partition.join(soft[priority]);
    std::unordered_map<size_t, ID> componentHard;
    for (size_t index = 0; index != parts.size(); ++index)
      if (partInputs[index] != InputPartition::none) {
        ID &hard = componentHard.try_emplace(partition.find(partInputs[index]),
                                             1)
                       .first->second;
        hard = circuit.conjunction(hard, parts[index]);
      }
    std::unordered_map<size_t, ModelCounter> relations;
    for (size_t priority = soft.size(); priority-- != 0;) {
      if (soft[priority] == missing)
        continue;
      bool retained = soft[priority] != 0;
      if (softComponents[priority] != InputPartition::none) {
        size_t component = partition.find(softComponents[priority]);
        auto [relation, created] =
            relations.try_emplace(component, circuit, booleanBits);
        auto hard = componentHard.find(component);
        if ((created &&
             !relation->second.build(
                 hard == componentHard.end() ? 1 : hard->second)) ||
            !relation->second.conjoinIfSatisfiable(soft[priority], retained))
          return SamplerResult::Invalid;
      }
      if (retained) {
        f.require(soft[priority]);
        retainedSoft[priority] = true;
      }
    }
  }
  if (weighted)
    return f.weighted(width, variable, booleanBits, physicalBits,
                      weightedGroups, distributionGuards, expressionWeights,
                      retainedSoft, constraintMask, captures, captureWidths,
                      captureOffsets, start, mutableMask, *randomState,
                      orderedMasks, orderedLayerCount, assignment);
  return f.ordered(width, start, mutableMask, *randomState, orderedMasks,
                   orderedLayerCount, assignment);
}

} // namespace talos::runtime
