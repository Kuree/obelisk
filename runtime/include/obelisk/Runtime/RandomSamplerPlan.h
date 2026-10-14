//===- RandomSamplerPlan.h - Compiler-generated Boolean sampling plans ----===//

#pragma once

#include "obelisk/Runtime/Runtime.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace obelisk::random {

inline bool isRealInstruction(unsigned opcode) {
  return opcode >= OBELISK_RT_RANDOM_REAL32_BITS_V1 &&
         opcode <= OBELISK_RT_RANDOM_REAL_IS_FINITE_V1;
}

/// The host and both target program readers share the scalar/rank wire shape.
/// Typed operand provenance is additionally checked while compiling the plan.
inline bool validRealInstruction(unsigned opcode, unsigned width,
                                 unsigned flags, unsigned operand,
                                 uint64_t auxiliary, unsigned inputWidth) {
  if (!isRealInstruction(opcode) || operand || auxiliary || !inputWidth ||
      (flags && (opcode != OBELISK_RT_RANDOM_REAL_CAST_V1 ||
                 flags != OBELISK_RT_RANDOM_INSTRUCTION_SIGNED)))
    return false;
  switch (opcode) {
  case OBELISK_RT_RANDOM_REAL32_BITS_V1:
    return width == 32 && inputWidth == 32;
  case OBELISK_RT_RANDOM_REAL64_BITS_V1:
    return width == 64 && inputWidth == 64;
  case OBELISK_RT_RANDOM_REAL32_FROM_RANK_V1:
    return width == 32 && inputWidth == OBELISK_RT_REAL32_RANK_BITS;
  case OBELISK_RT_RANDOM_REAL64_FROM_RANK_V1:
    return width == 64 && inputWidth == OBELISK_RT_REAL64_RANK_BITS;
  case OBELISK_RT_RANDOM_REAL_RANK_VALID_V1:
    return width == 1 && (inputWidth == OBELISK_RT_REAL32_RANK_BITS ||
                          inputWidth == OBELISK_RT_REAL64_RANK_BITS);
  case OBELISK_RT_RANDOM_REAL_TO_BITS_V1:
    return (width == 32 || width == 64) && inputWidth == width;
  case OBELISK_RT_RANDOM_REAL_CAST_V1:
    return width == 32 || width == 64;
  case OBELISK_RT_RANDOM_REAL_TO_INTEGER_V1:
  case OBELISK_RT_RANDOM_REAL_TRUNCATE_INTEGER_V1:
    return width && (inputWidth == 32 || inputWidth == 64);
  case OBELISK_RT_RANDOM_REAL_IS_FINITE_V1:
    return width == 1 && (inputWidth == 32 || inputWidth == 64);
  }
  return false;
}

/// A complemented-edge AND graph. Handles 0 and 1 are false and true; the low
/// bit of every other handle negates its node. The compiler lowers constraint
/// arithmetic before writing this pointer-free, little-endian plan. IEEE cell
/// inputs bind deterministic numeric conversions of live state payloads.
enum class PlanOpcode : uint32_t {
  Constant = 0,
  RandomBit = 1,
  CaptureBit = 2,
  ConstraintEnabled = 3,
  And = 4,
  // a is the capture index and b is a signed-coordinate bit. The first/last
  // pair is the complete finite IEEE decoder cell, before property clipping.
  // Nonfinite captures have zero placeholders and retain their IEEE guards.
  Real32FirstBit = 5,
  Real32LastBit = 6,
  Real64FirstBit = 7,
  Real64LastBit = 8,
  // Binary64 state rounded to binary32, for promoted shortreal comparisons.
  Real32From64FirstBit = 9,
  Real32From64LastBit = 10,
};

inline unsigned realCellPrecision(PlanOpcode opcode) {
  switch (opcode) {
  case PlanOpcode::Real32FirstBit:
  case PlanOpcode::Real32LastBit:
  case PlanOpcode::Real32From64FirstBit:
  case PlanOpcode::Real32From64LastBit:
    return 32;
  case PlanOpcode::Real64FirstBit:
  case PlanOpcode::Real64LastBit:
    return 64;
  default:
    return 0;
  }
}

inline bool realCellNarrowed(PlanOpcode opcode) {
  return opcode == PlanOpcode::Real32From64FirstBit ||
         opcode == PlanOpcode::Real32From64LastBit;
}

struct PlanNode {
  PlanOpcode opcode;
  uint32_t a, b;
};

class BooleanCircuit {
public:
  using ID = uint32_t;
  static constexpr uint32_t noVariable = UINT32_MAX;
  static constexpr ID missing = UINT32_MAX;

  BooleanCircuit() {
    nodes.push_back({PlanOpcode::Constant, 0, 0});
    firstVariables.push_back(noVariable);
    mandatoryLiterals.push_back(false);
  }

  ID input(PlanOpcode opcode, uint32_t a, uint32_t b = 0) {
    ID id = static_cast<ID>(nodes.size() * 2);
    nodes.push_back({opcode, a, b});
    firstVariables.push_back(opcode == PlanOpcode::RandomBit ? b : noVariable);
    mandatoryLiterals.push_back(opcode == PlanOpcode::RandomBit);
    return id;
  }

  ID conjunction(ID a, ID b) {
    if (!a || !b || a == (b ^ 1))
      return 0;
    if (a == 1 || a == b)
      return b;
    if (b == 1)
      return a;
    // Absorption and a contradictory literal inside an AND are common after
    // substituting a live capture into a compiler-bit-blasted carry chain.
    auto absorb = [&](ID outer, ID inner, ID &result) {
      if ((inner & 1) || nodes[inner / 2].opcode != PlanOpcode::And)
        return false;
      const PlanNode &node = nodes[inner / 2];
      if (outer == node.a || outer == node.b) {
        result = inner;
        return true;
      }
      if (outer == (node.a ^ 1) || outer == (node.b ^ 1)) {
        result = 0;
        return true;
      }
      return false;
    };
    ID result;
    if (absorb(a, b, result) || absorb(b, a, result))
      return result;
    if (a > b)
      std::swap(a, b);
    uint64_t key = (uint64_t(a) << 32) | b;
    auto found = unique.find(key);
    if (found != unique.end())
      return found->second;
    result = static_cast<ID>(nodes.size() * 2);
    nodes.push_back({PlanOpcode::And, a, b});
    firstVariables.push_back(std::min(firstVariable(a), firstVariable(b)));
    mandatoryLiterals.push_back(hasMandatoryLiteral(a) ||
                                hasMandatoryLiteral(b));
    unique.emplace(key, result);
    return result;
  }

  ID disjunction(ID a, ID b) { return conjunction(a ^ 1, b ^ 1) ^ 1; }
  ID exclusiveOr(ID a, ID b) {
    return disjunction(conjunction(a, b ^ 1), conjunction(a ^ 1, b));
  }
  ID conditional(ID condition, ID yes, ID no) {
    if (yes == no)
      return yes;
    if (yes == 1)
      return disjunction(condition, no);
    if (no == 0)
      return conjunction(condition, yes);
    if (yes == 0)
      return conjunction(condition ^ 1, no);
    if (no == 1)
      return disjunction(condition ^ 1, yes);
    return disjunction(conjunction(condition, yes),
                       conjunction(condition ^ 1, no));
  }

  uint32_t firstVariable(ID id) const { return firstVariables[id / 2]; }
  /// Whether traversing positive ANDs reaches a mandatory random literal.
  /// Negating an AND stops that traversal; negating a random bit retains it.
  /// Each gate computes this flag once, so arithmetic without such literals
  /// can skip the propagation walk and its temporary graph inventory.
  bool hasMandatoryLiteral(ID id) const {
    return mandatoryLiterals[id / 2] &&
           (!(id & 1) || nodes[id / 2].opcode == PlanOpcode::RandomBit);
  }
  const std::vector<PlanNode> &getNodes() const { return nodes; }

  /// Restrict one variable. Hash-consing the simplified
  /// cofactors lets the sampler construct only the final relation's decision
  /// diagram, without constructing diagrams for intermediate arithmetic.
  ID cofactor(ID root, uint32_t variable, bool high) {
    auto known = [&](ID id, ID &result) {
      if (firstVariable(id) > variable ||
          (nodes[id / 2].opcode == PlanOpcode::RandomBit &&
           nodes[id / 2].b != variable)) {
        result = id;
        return true;
      }
      uint64_t key = (uint64_t(id) << 32) | variable;
      auto found = cofactors.find(key);
      if (found == cofactors.end())
        return false;
      result = high ? found->second.second : found->second.first;
      return result != missing;
    };
    std::vector<ID> pending{root};
    ID result = 0;
    while (!pending.empty()) {
      ID id = pending.back();
      if (known(id, result)) {
        pending.pop_back();
        continue;
      }
      PlanNode node = nodes[id / 2];
      if (node.opcode == PlanOpcode::RandomBit)
        result = ID(high) ^ (id & 1);
      else {
        ID a = 0, b = 0;
        if (!known(node.a, a)) {
          pending.push_back(node.a);
          continue;
        }
        if (!known(node.b, b)) {
          pending.push_back(node.b);
          continue;
        }
        result = conjunction(a, b) ^ (id & 1);
      }
      uint64_t key = (uint64_t(id) << 32) | variable;
      auto found =
          cofactors.emplace(key, std::make_pair(missing, missing)).first;
      (high ? found->second.second : found->second.first) = result;
      pending.pop_back();
    }
    return result;
  }

  /// Substitute arbitrary accepted stage inputs. Only reachable nodes are
  /// visited; fixed bits remain constants and never multiply a model count.
  ID restrict(ID root, const std::vector<bool> &values,
              const std::vector<bool> &selected) {
    std::unordered_map<ID, ID> reduced;
    reduced.emplace(0, 0);
    auto known = [&](ID id) { return reduced.count(id & ~ID(1)); };
    auto value = [&](ID id) { return reduced.at(id & ~ID(1)) ^ (id & 1); };
    std::vector<ID> pending{root & ~ID(1)};
    while (!pending.empty()) {
      ID id = pending.back();
      if (known(id)) {
        pending.pop_back();
        continue;
      }
      PlanNode node = nodes[id / 2];
      if (node.opcode == PlanOpcode::RandomBit)
        reduced.emplace(id, selected[node.b] ? ID(values[node.b]) : id);
      else {
        if (!known(node.a)) {
          pending.push_back(node.a & ~ID(1));
          continue;
        }
        if (!known(node.b)) {
          pending.push_back(node.b & ~ID(1));
          continue;
        }
        reduced.emplace(id, conjunction(value(node.a), value(node.b)));
      }
      pending.pop_back();
    }
    return value(root);
  }

private:
  std::vector<PlanNode> nodes;
  std::vector<uint32_t> firstVariables;
  std::vector<bool> mandatoryLiterals;
  std::unordered_map<uint64_t, ID> unique;
  std::unordered_map<uint64_t, std::pair<ID, ID>> cofactors;
};

constexpr uint32_t samplerPlanMagic = 0x504d4153;    // SAMP
constexpr uint32_t samplerTrailerMagic = 0x4e414c50; // PLAN
constexpr uint32_t samplerPlanVersion = 1;
constexpr size_t samplerPlanHeaderSize = 32;
constexpr size_t samplerPlanNodeSize = 16;
constexpr size_t samplerPlanRootSize = 8;
constexpr size_t samplerPlanTrailerSize = 16;

inline uint32_t planRead32(const uint8_t *bytes) {
  uint32_t value = 0;
  for (unsigned index = 0; index != 4; ++index)
    value |= uint32_t(bytes[index]) << (index * 8);
  return value;
}

inline uint64_t planRead64(const uint8_t *bytes) {
  return uint64_t(planRead32(bytes)) | (uint64_t(planRead32(bytes + 4)) << 32);
}

struct SamplerPlanView {
  const uint8_t *nodes = nullptr, *roots = nullptr;
  uint32_t nodeCount = 0, rootCount = 0;
  size_t coreSize = 0;
};

/// END_SOFT priorities are dense from zero and larger values win. Each one
/// has its own instruction, so the instruction count bounds the priorities
/// that one program can name.
class SoftPrioritySet {
public:
  bool insert(uint64_t priority, uint64_t instructions) {
    if (priority >= instructions)
      return false;
    if (priority >= seen.size())
      seen.resize(priority + 1, false);
    seen[priority] = true;
    return true;
  }
  bool contains(uint64_t priority) const {
    return priority < seen.size() && seen[priority];
  }
  bool dense() const {
    return std::find(seen.begin(), seen.end(), false) == seen.end();
  }
  size_t size() const { return seen.size(); }

private:
  std::vector<bool> seen;
};

inline bool isDistExportInstruction(unsigned opcode) {
  return opcode == OBELISK_RT_RANDOM_END_DIST_WEIGHT_BIT_V1 ||
         opcode == OBELISK_RT_RANDOM_END_DIST_TARGET_BIT_V1;
}

/// Weight and target roots are one contiguous, ascending bit sequence per
/// weight capture or distribution group. Keep validation linear without
/// accepting partial or repeated definitions. A reader that has not parsed
/// the distribution footer passes no group count.
class DistBitSequence {
public:
  DistBitSequence(uint32_t captures, std::optional<uint32_t> groups,
                  bool weighted)
      : weightSeen(captures, false), groups(groups), weighted(weighted) {}
  bool visit(unsigned opcode, unsigned width, unsigned flags, unsigned operand,
             uint64_t bit) {
    if (!isDistExportInstruction(opcode))
      return next == 0;
    bool weight = opcode == OBELISK_RT_RANDOM_END_DIST_WEIGHT_BIT_V1;
    if (!weighted || !width || flags ||
        operand >= (weight ? weightSeen.size() : groups.value_or(UINT32_MAX)))
      return false;
    if (next == 0) {
      bool repeated = weight ? weightSeen[operand]
                             : !targetSeen.insert(operand).second;
      if (bit || repeated)
        return false;
      if (weight)
        weightSeen[operand] = true;
      active = opcode;
      owner = operand;
      bits = width;
    }
    if (active != opcode || owner != operand || bits != width || bit != next)
      return false;
    if (++next == bits)
      next = 0;
    return true;
  }
  bool complete() const { return next == 0; }

private:
  std::vector<bool> weightSeen;
  std::unordered_set<uint32_t> targetSeen;
  std::optional<uint32_t> groups;
  bool weighted;
  unsigned active = 0, owner = 0, bits = 0, next = 0;
};

/// Validate the bundle independently of the residual interpreter. All handles
/// are topological, all parameter indices are bounded, and roots correspond
/// one-to-one to end instructions and weight-bit exports. The residual's own
/// validator checks its arithmetic and optional metadata after using coreSize
/// as its byte bound.
inline bool readSamplerPlan(const uint8_t *program, size_t size,
                            const uint32_t *captureWidths,
                            SamplerPlanView &view) {
  if (size < 24 + samplerPlanHeaderSize + samplerPlanTrailerSize)
    return false;
  const uint8_t *trailer = program + size - samplerPlanTrailerSize;
  if (planRead32(trailer) != samplerTrailerMagic ||
      planRead32(trailer + 4) != samplerPlanVersion)
    return false;
  uint64_t planSize = planRead64(trailer + 8);
  if (planSize < samplerPlanHeaderSize ||
      planSize > size - samplerPlanTrailerSize - 24)
    return false;
  size_t coreSize = size - samplerPlanTrailerSize - size_t(planSize);
  const uint8_t *header = program + coreSize;
  uint32_t nodes = planRead32(header + 8), roots = planRead32(header + 12);
  uint32_t width = planRead32(program + 8), captures = planRead32(program + 16);
  if (planRead32(header) != samplerPlanMagic ||
      planRead32(header + 4) != samplerPlanVersion || !nodes ||
      nodes >= (UINT32_MAX / 2) || planRead32(header + 16) != width ||
      planRead32(header + 20) != captures ||
      planRead64(header + 24) != coreSize ||
      planSize != samplerPlanHeaderSize +
                      uint64_t(nodes) * samplerPlanNodeSize +
                      uint64_t(roots) * samplerPlanRootSize)
    return false;
  bool wide = program[4] == 2 && program[5] == 0;
  size_t instructionHeader = wide ? 32 : 24;
  uint32_t instructions = planRead32(program + 12);
  if (coreSize < instructionHeader ||
      instructions > (coreSize - instructionHeader) / 16)
    return false;
  const uint8_t *nodeBytes = header + samplerPlanHeaderSize;
  std::vector<bool> physical(width, false), order(width, false);
  for (uint32_t index = 0; index != nodes; ++index) {
    const uint8_t *p = nodeBytes + size_t(index) * samplerPlanNodeSize;
    auto opcode = static_cast<PlanOpcode>(planRead32(p));
    uint32_t a = planRead32(p + 4), b = planRead32(p + 8);
    if (planRead32(p + 12))
      return false;
    switch (opcode) {
    case PlanOpcode::Constant:
      if (index || a || b)
        return false;
      break;
    case PlanOpcode::RandomBit:
      if (!index || a >= width || b >= width || physical[a] || order[b])
        return false;
      physical[a] = order[b] = true;
      break;
    case PlanOpcode::CaptureBit:
      if (!index || a >= captures ||
          b >= (captureWidths ? captureWidths[a] : 64))
        return false;
      break;
    case PlanOpcode::Real32FirstBit:
    case PlanOpcode::Real32LastBit:
    case PlanOpcode::Real64FirstBit:
    case PlanOpcode::Real64LastBit:
    case PlanOpcode::Real32From64FirstBit:
    case PlanOpcode::Real32From64LastBit: {
      unsigned precision = realCellPrecision(opcode);
      unsigned coordinateWidth = precision == 32 ? OBELISK_RT_REAL32_RANK_BITS
                                                 : OBELISK_RT_REAL64_RANK_BITS;
      // These are deterministic state parameters, never sampled witnesses.
      // The low IEEE payload bits of the declared capture name their cell.
      if (!index || a >= captures || b >= coordinateWidth ||
          (captureWidths ? captureWidths[a] : 64) <
              (realCellNarrowed(opcode) ? 64u : precision) ||
          !(planRead32(program + 20) & OBELISK_RT_RANDOM_PROGRAM_HAS_REAL))
        return false;
      break;
    }
    case PlanOpcode::ConstraintEnabled:
      if (!index || a >= 64 || b)
        return false;
      break;
    case PlanOpcode::And:
      if (!index || a / 2 >= index || b / 2 >= index)
        return false;
      break;
    default:
      return false;
    }
  }
  if (std::find(physical.begin(), physical.end(), false) != physical.end())
    return false;
  const uint8_t *rootBytes = nodeBytes + size_t(nodes) * samplerPlanNodeSize;
  uint32_t previous = 0, expected = 0;
  DistBitSequence distBits(captures, std::nullopt,
                           planRead32(program + 20) &
                               OBELISK_RT_RANDOM_PROGRAM_EXACT_WEIGHTED);
  for (uint32_t index = 0; index != instructions; ++index) {
    const uint8_t *p = program + instructionHeader + size_t(index) * 16;
    uint8_t opcode = p[0];
    unsigned bits = wide ? planRead32(p + 4) : p[1];
    unsigned operand = planRead32(p + (wide ? 8 : 4));
    if (!distBits.visit(opcode, bits, wide ? p[1] : p[2], operand,
                        wide ? planRead32(p + 12) : planRead64(p + 8)))
      return false;
    if (opcode == OBELISK_RT_RANDOM_END_DIST_WEIGHT_BIT_V1 &&
        bits != (captureWidths ? captureWidths[operand] : 64))
      return false;
    // END_HARD, END_SOFT and END_DIST_GUARD are fixed C ABI opcodes.
    if (opcode == OBELISK_RT_RANDOM_END_HARD_V1 ||
        opcode == OBELISK_RT_RANDOM_END_SOFT_V1 ||
        opcode == OBELISK_RT_RANDOM_END_DIST_GUARD_V1 ||
        isDistExportInstruction(opcode))
      ++expected;
  }
  if (roots != expected || !distBits.complete())
    return false;
  for (uint32_t index = 0; index != roots; ++index) {
    const uint8_t *p = rootBytes + size_t(index) * samplerPlanRootSize;
    uint32_t instruction = planRead32(p), handle = planRead32(p + 4);
    if (instruction >= instructions || (index && instruction <= previous) ||
        handle / 2 >= nodes)
      return false;
    uint8_t opcode = program[instructionHeader + size_t(instruction) * 16];
    if (opcode != OBELISK_RT_RANDOM_END_HARD_V1 &&
        opcode != OBELISK_RT_RANDOM_END_SOFT_V1 &&
        opcode != OBELISK_RT_RANDOM_END_DIST_GUARD_V1 &&
        !isDistExportInstruction(opcode))
      return false;
    previous = instruction;
  }
  view = {nodeBytes, rootBytes, nodes, roots, coreSize};
  return true;
}

} // namespace obelisk::random
