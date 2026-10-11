//===- RandSamplePlan.cpp - Specialize and sample compiler-proved plans
//----===//

#include "RandSamplePlan.h"
#include "RandModelCount.h"
#include "RandSolveOrder.h"
#include "RandSolveWeighted.h"
#include "obelisk/Runtime/RandomDomain.h"
#include "obelisk/Runtime/RandomSamplerPlan.h"

#include <algorithm>
#include <array>
#include <memory>
#include <unordered_set>
#include <vector>

namespace obelisk::runtime {
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

  void require(ID predicate) { requirements.push_back(predicate); }
  void push() { checkpoints.push_back(requirements.size()); }
  void pop() {
    requirements.resize(checkpoints.back());
    checkpoints.pop_back();
  }

  SamplerResult check() {
    ModelCounter counter(circuit, bits);
    if (!counter.build(predicate()))
      return SamplerResult::Invalid;
    return counter.count().zero() ? SamplerResult::Unsatisfiable
                                  : SamplerResult::Satisfiable;
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

  bool minimum(const Bits &variable, unsigned width, const uint64_t *start,
               const uint64_t *mutableMask, uint64_t *output) {
    ID lowerBound = 1;
    for (unsigned bit = 0; bit != width; ++bit)
      lowerBound = ((start[bit / 64] >> (bit % 64)) & 1)
                       ? circuit.conjunction(variable[bit], lowerBound)
                       : circuit.disjunction(variable[bit], lowerBound);
    push();
    require(lowerBound);
    auto status = check();
    if (status == SamplerResult::Invalid)
      return false;
    if (status == SamplerResult::Unsatisfiable)
      pop();
    for (unsigned bit = width; bit-- != 0;) {
      push();
      require(variable[bit] ^ 1);
      status = check();
      if (status == SamplerResult::Invalid)
        return false;
      if (status == SamplerResult::Unsatisfiable) {
        pop();
        require(variable[bit]);
      }
    }
    ModelCounter counter(circuit, bits);
    if (!counter.build(predicate()) || counter.count().zero())
      return false;
    write(counter.assignment(ModelCount()), width, start, mutableMask, output);
    return true;
  }

  void write(const std::vector<bool> &value, unsigned width,
             const uint64_t *start, const uint64_t *mutableMask,
             uint64_t *output) {
    size_t words = (uint64_t(width) + 63) / 64;
    for (size_t word = 0; word != words; ++word)
      output[word] = start[word] & ~mutableMask[word];
    for (size_t bit = 0; bit != value.size(); ++bit)
      if (value[bit])
        output[physicalBits[bit] / 64] |= uint64_t{1}
                                          << (physicalBits[bit] % 64);
    if (width % 64)
      output[words - 1] &= (uint64_t{1} << (width % 64)) - 1;
  }

  bool weighted(unsigned width, const Bits &variable,
                const std::vector<ID> &bits,
                const std::vector<unsigned> &physicalBits,
                const std::vector<WeightedGroup> &groups,
                const std::vector<ID> &guards, uint64_t retainedSoft,
                uint64_t constraintMask, const uint64_t *captures,
                const uint32_t *captureWidths,
                const std::vector<size_t> &captureOffsets,
                const uint64_t *start, const uint64_t *mutableMask,
                obelisk_rt_random_state_v1 &state, const uint64_t *masks,
                size_t layerCount, uint64_t *output) {
    ModelCounter counter(circuit, bits);
    if (!counter.build(predicate()))
      return false;
    obelisk_rt_random_state_v1 nextState = state;
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
            !accepted[bit] &&
            (!layerCount || ((masks[layer * words + physicalBits[bit] / 64] >>
                              (physicalBits[bit] % 64)) &
                             1));
      std::vector<bool> targets(bits.size(), false);
      std::vector<std::vector<Choice>> choices;
      for (size_t groupIndex = 0; groupIndex != groups.size(); ++groupIndex) {
        const WeightedGroup &group = groups[groupIndex];
        if ((group.constraintBlock !=
                 OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1 &&
             ((constraintMask >> group.constraintBlock) & 1)) ||
            (group.softPriority != UINT32_MAX &&
             !((retainedSoft >> group.softPriority) & 1)))
          continue;
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
          return false;
        ID guard = groupIndex < guards.size() && guards[groupIndex] != missing
                       ? guards[groupIndex]
                       : 1;
        ModelCounter active = counter;
        ModelCounter inactive = counter;
        if (!active.constrain(guard) || !inactive.constrain(guard ^ 1))
          return false;
        if (active.count().zero())
          continue;
        // Activation is state or an already accepted ordering layer. A guard
        // that still varies needs a conditional, scale-invariant measure.
        if (!inactive.count().zero())
          return false;
        std::vector<Choice> eligible;
        Bits field(variable.begin() + group.targetOffset,
                   variable.begin() + group.targetOffset + group.width);
        if (group.isSigned)
          field.back() ^= 1;
        for (const WeightedItem &item : group.items) {
          unsigned weightWidth =
              captureWidths ? captureWidths[item.weightCapture] : 64;
          size_t weightWords = (uint64_t(weightWidth) + 63) / 64;
          std::vector<uint64_t> weight(
              captures + captureOffsets[item.weightCapture],
              captures + captureOffsets[item.weightCapture] + weightWords);
          if (weightWidth % 64)
            weight.back() &= (uint64_t{1} << (weightWidth % 64)) - 1;
          ModelCount mass = ModelCount::fromWords(weight.data(), weight.size());
          if (mass.zero() || item.intervals.empty())
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
          if (!item.perRange) {
            uint64_t cardinality = item.intervals.front().cardinality;
            mass = mass.multiplied(cardinality ? ModelCount(cardinality)
                                               : ModelCount::powerOfTwo(64));
          }
          ModelCounter range = counter;
          if (!range.constrain(region))
            return false;
          range.project(target);
          ModelCount count = range.count();
          if (!count.zero())
            eligible.push_back({std::move(range), std::move(mass),
                                std::move(count), ModelCount()});
        }
        if (eligible.empty())
          return false;
        // Each source item's original mass is shared by its legal target
        // values. A common denominator keeps the resulting ratios integral
        // without a fixed-width normalization or rounding step.
        ModelCount denominator(1);
        for (const Choice &choice : eligible)
          denominator = denominator.multiplied(choice.count);
        for (Choice &choice : eligible)
          choice.coefficient =
              choice.mass.multiplied(denominator.divided(choice.count));
        choices.push_back(std::move(eligible));
        for (size_t bit = 0; bit != bits.size(); ++bit)
          targets[bit] = targets[bit] || target[bit];
      }
      if (!choices.empty()) {
        std::vector<size_t> indices(choices.size(), 0);
        std::vector<Term> terms;
        ModelCount total;
        for (;;) {
          ModelCounter branch = counter;
          ModelCount coefficient(1);
          for (size_t group = 0; group != choices.size(); ++group) {
            const Choice &choice = choices[group][indices[group]];
            if (!branch.intersect(choice.region))
              return false;
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
          return false;
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
          return false;
        ModelCounter branch = counter;
        for (size_t group = 0; group != choices.size(); ++group)
          if (!branch.intersect(
                  choices[group][selected->choices[group]].region))
            return false;
        branch.project(targets);
        auto value = branch.assignment(
            ModelCount::randomBelow(branch.count(), nextState));
        counter.restrict(value, targets);
      }
      counter.project(keep);
      ModelCount count = counter.count();
      if (count.zero())
        return false;
      auto value =
          counter.assignment(ModelCount::randomBelow(count, nextState));
      counter.restrict(value, keep);
      for (size_t bit = 0; bit != bits.size(); ++bit)
        accepted[bit] = accepted[bit] || keep[bit];
    }
    counter.project(std::vector<bool>(bits.size(), true));
    ModelCount count = counter.count();
    if (count.zero())
      return false;
    auto value = counter.assignment(ModelCount::randomBelow(count, nextState));
    for (size_t word = 0; word != words; ++word)
      output[word] = start[word] & ~mutableMask[word];
    for (size_t bit = 0; bit != value.size(); ++bit)
      if (value[bit])
        output[physicalBits[bit] / 64] |= uint64_t{1}
                                          << (physicalBits[bit] % 64);
    if (width % 64)
      output[words - 1] &= (uint64_t{1} << (width % 64)) - 1;
    state = nextState;
    return true;
  }

  bool ordered(unsigned width, const std::vector<ID> &bits,
               const std::vector<unsigned> &physicalBits, const uint64_t *start,
               const uint64_t *mutableMask, obelisk_rt_random_state_v1 &state,
               const uint64_t *masks, size_t layerCount, uint64_t *output) {
    ModelCounter counter(circuit, bits);
    if (!counter.build(predicate()))
      return false;
    obelisk_rt_random_state_v1 nextState = state;
    size_t words = (uint64_t(width) + 63) / 64;
    for (size_t layer = 0; layer != layerCount; ++layer) {
      std::vector<bool> keep(bits.size(), false);
      for (size_t bit = 0; bit != bits.size(); ++bit)
        keep[bit] = (masks[layer * words + physicalBits[bit] / 64] >>
                     (physicalBits[bit] % 64)) &
                    1;
      counter.project(keep);
      ModelCount count = counter.count();
      if (count.zero())
        return false;
      auto value =
          counter.assignment(ModelCount::randomBelow(count, nextState));
      counter.restrict(value, keep);
    }
    // The unspecified remaining fields retain the ordinary joint distribution
    // conditional on the accepted earlier-layer values.
    counter.project(std::vector<bool>(bits.size(), true));
    ModelCount count = counter.count();
    if (count.zero())
      return false;
    auto value = counter.assignment(ModelCount::randomBelow(count, nextState));
    for (size_t index = 0; index != words; ++index)
      output[index] = start[index] & ~mutableMask[index];
    for (size_t bit = 0; bit != value.size(); ++bit)
      if (value[bit])
        output[physicalBits[bit] / 64] |= uint64_t{1}
                                          << (physicalBits[bit] % 64);
    if (width % 64)
      output[words - 1] &= (uint64_t{1} << (width % 64)) - 1;
    state = nextState;
    return true;
  }

  bool projection(const Bits &bits, const std::vector<unsigned> &physicalBits,
                  const OrderMask &mask, obelisk_rt_random_state_v1 &state,
                  OrderMask &output) {
    std::vector<bool> keep(bits.size(), false);
    for (size_t bit = 0; bit != bits.size(); ++bit)
      keep[bit] =
          (mask[physicalBits[bit] / 64] >> (physicalBits[bit] % 64)) & 1;
    ModelCounter counter(circuit, bits);
    if (!counter.build(predicate()))
      return false;
    counter.project(keep);
    ModelCount count = counter.count();
    if (count.zero())
      return false;
    auto selected = counter.assignment(ModelCount::randomBelow(count, state));
    for (size_t index = 0; index != bits.size(); ++index) {
      if (!keep[index])
        continue;
      unsigned bit = physicalBits[index];
      uint64_t field = uint64_t{1} << (bit % 64);
      output[bit / 64] =
          (output[bit / 64] & ~field) | (selected[index] ? field : 0);
      require(selected[index] ? bits[index] : bits[index] ^ 1);
    }
    return true;
  }

  SamplerResult
  prioritized(const Bits &bits, const std::vector<unsigned> &physicalBits,
              const std::vector<ID> &hard, const std::array<ID, 64> &soft,
              const std::vector<const PriorityInput *> &hardInputs,
              const std::array<const PriorityInput *, 64> &softInputs,
              const std::vector<ID> &domains,
              const std::vector<SolveOrderEdge> &edges, const uint64_t *start,
              const uint64_t *mutableMask, size_t wordCount,
              uint64_t disabledBlocks, obelisk_rt_random_state_v1 &state,
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
    obelisk_rt_random_state_v1 nextState = state;
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
        if (!projection(bits, physicalBits, layer, nextState, assignment))
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
  std::vector<bool> fixed, fixedValues;
  std::vector<ID> requirements;
  std::vector<size_t> checkpoints;
};

} // namespace

SamplerResult
sampleRandomPlan(const uint8_t *program, size_t programSize,
                 const uint64_t *start, const uint64_t *mutableMask,
                 size_t assignmentWords, uint64_t constraintMask,
                 const uint64_t *captures, const uint32_t *captureWidths,
                 size_t captureCount, uint64_t *assignment,
                 obelisk_rt_random_state_v1 *randomState,
                 const uint64_t *orderedMasks, size_t orderedLayerCount) {
  if (programSize < OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE ||
      read32(program + 16) != captureCount ||
      !(read32(program + 20) & OBELISK_RT_RANDOM_PROGRAM_HAS_SAMPLER_PLAN))
    return SamplerResult::Unavailable;
  random::SamplerPlanView plan;
  if (!random::readSamplerPlan(program, programSize, captureWidths, plan))
    return SamplerResult::Invalid;
  programSize = plan.coreSize;
  bool wide = read16(program + 4) == OBELISK_RT_RANDOM_PROGRAM_VERSION_V2;
  unsigned header = wide ? OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE_V2
                         : OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE;
  unsigned width = read32(program + 8), instructions = read32(program + 12);
  unsigned flags = read32(program + 20);
  bool uniform = (flags & OBELISK_RT_RANDOM_PROGRAM_SMT_UNIFORM) != 0;
  bool weighted = (flags & OBELISK_RT_RANDOM_PROGRAM_SMT_WEIGHTED) != 0;
  bool exact = uniform || weighted;
  bool functionPriority =
      (flags & OBELISK_RT_RANDOM_PROGRAM_HAS_FUNCTION_PRIORITY) != 0;
  bool dynamicMasks =
      (flags & OBELISK_RT_RANDOM_PROGRAM_HAS_GUARD_INPUT_MASKS) != 0;
  if (assignmentWords != std::max<uint64_t>(1, (uint64_t(width) + 63) / 64) ||
      ((flags & OBELISK_RT_RANDOM_PROGRAM_HAS_DIST) && !weighted) ||
      (weighted &&
       (!(flags & OBELISK_RT_RANDOM_PROGRAM_HAS_DIST) || functionPriority)) ||
      ((flags & OBELISK_RT_RANDOM_PROGRAM_HAS_SOLVE_BEFORE) && !exact) ||
      (exact && !randomState) || (orderedLayerCount && !orderedMasks))
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
  Formula f(circuit, booleanBits);
  f.physicalBits = physicalBits;
  std::array<ID, 64> soft;
  soft.fill(missing);
  std::vector<ID> priorityHard, priorityDomains;
  size_t literals = header + size_t(instructions) * 16;
  size_t domainsOffset =
      literals + (wide ? size_t(read32(program + 24)) * 8 : 0);
  if (flags & OBELISK_RT_RANDOM_PROGRAM_HAS_SOLVE_BEFORE)
    domainsOffset += OBELISK_RT_RANDOM_SOLVE_EDGE_HEADER_SIZE +
                     size_t(read32(program + domainsOffset)) *
                         (wide ? assignmentWords * 16 +
                                     OBELISK_RT_RANDOM_SOLVE_EDGE_TAIL_SIZE_V2
                               : OBELISK_RT_RANDOM_SOLVE_EDGE_SIZE);

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
        programSize - domainsOffset < OBELISK_RT_RANDOM_DIST_HEADER_SIZE)
      return SamplerResult::Unavailable;
    size_t records = read32(program + domainsOffset + 4);
    if (records >
        (programSize - domainsOffset - OBELISK_RT_RANDOM_DIST_HEADER_SIZE) /
            OBELISK_RT_RANDOM_DIST_RECORD_SIZE)
      return SamplerResult::Unavailable;
    size_t footerSize = OBELISK_RT_RANDOM_DIST_HEADER_SIZE +
                        records * OBELISK_RT_RANDOM_DIST_RECORD_SIZE;
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
  std::vector<ID> distributionGuards(weightedGroups.size(), missing);
  std::vector<const PriorityInput *> hardInputs;
  std::array<const PriorityInput *, 64> softInputs{};
  for (unsigned index = 0; index != instructions; ++index) {
    const uint8_t *p = program + header + size_t(index) * 16;
    unsigned opcode = p[0];
    if (roots[index] == missing)
      continue;
    unsigned operand = read32(p + (wide ? 8 : 4));
    uint64_t auxiliary = wide ? read32(p + 12) : read64(p + 8);
    if (opcode == OBELISK_RT_RANDOM_END_DIST_GUARD_V1) {
      if (!weighted || operand >= distributionGuards.size() ||
          distributionGuards[operand] != missing)
        return SamplerResult::Invalid;
      distributionGuards[operand] = roots[index];
      continue;
    }
    bool enabled = operand == OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1 ||
                   (constraintMask & (uint64_t{1} << operand)) == 0;
    ID predicate = enabled ? roots[index] : 1;
    auto source = sourceInputs.find(index);
    const PriorityInput *input =
        enabled && source != sourceInputs.end() ? &source->second : nullptr;
    if (opcode == OBELISK_RT_RANDOM_END_HARD_V1) {
      if (functionPriority) {
        priorityHard.push_back(predicate);
        hardInputs.push_back(input);
      } else
        f.require(predicate);
    } else {
      soft[auxiliary] = predicate;
      softInputs[auxiliary] = input;
    }
  }
  if (flags & OBELISK_RT_RANDOM_PROGRAM_HAS_DOMAINS) {
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
  if (functionPriority) {
    size_t offset = literals + (wide ? size_t(read32(program + 24)) * 8 : 0);
    uint32_t count = read32(program + offset);
    const uint8_t *cursor =
        program + offset + OBELISK_RT_RANDOM_SOLVE_EDGE_HEADER_SIZE;
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
           OBELISK_RT_RANDOM_SOLVE_EDGE_FUNCTION_PRIORITY) != 0;
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
          if (block != OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1 &&
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
  auto result = f.check();
  if (result != SamplerResult::Satisfiable)
    return result;
  uint64_t retainedSoft = 0;
  for (size_t priority = soft.size(); priority-- != 0;) {
    if (soft[priority] == missing)
      continue;
    f.push();
    f.require(soft[priority]);
    result = f.check();
    if (result == SamplerResult::Invalid)
      return result;
    if (result == SamplerResult::Unsatisfiable)
      f.pop();
    else
      retainedSoft |= uint64_t{1} << priority;
  }
  if (weighted)
    return f.weighted(width, variable, booleanBits, physicalBits,
                      weightedGroups, distributionGuards, retainedSoft,
                      constraintMask, captures, captureWidths, captureOffsets,
                      start, mutableMask, *randomState, orderedMasks,
                      orderedLayerCount, assignment)
               ? SamplerResult::Satisfiable
               : SamplerResult::Invalid;
  if (uniform)
    return f.ordered(width, booleanBits, physicalBits, start, mutableMask,
                     *randomState, orderedMasks, orderedLayerCount, assignment)
               ? SamplerResult::Satisfiable
               : SamplerResult::Invalid;
  return f.minimum(variable, width, start, mutableMask, assignment)
             ? SamplerResult::Satisfiable
             : SamplerResult::Invalid;
}

} // namespace obelisk::runtime
