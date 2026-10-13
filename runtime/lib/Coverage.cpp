//===- Coverage.cpp - Context-local functional coverage state ------------===//

#include "ContainerStorageInternal.h"
#include "RuntimeInternal.h"

#include "../../lib/Coverage/CoverageCountProjectionIndex.h"
#include "../../lib/Coverage/CoverageDecisionIndex.h"
#include "../../lib/Coverage/CoverageIndex.h"
#include "../../lib/Coverage/CoveragePredicateIndex.h"
#include "../../lib/Coverage/CoverageTransitionIndex.h"
#include "ProcessPacking.h"
#include "ProcessShared.h"
#include "obelisk/Runtime/StableHash.h"

#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <iterator>
#include <limits>
#include <numeric>
#include <set>
#include <stdexcept>
#include <thread>

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

// Sampling retains only the immutable label graph and numeric bin ordinals.
// Constructor query indexes and temporary value projections can be released.
struct FunctionalCoverageCrossSelection {
  using Graph = obelisk::coverage::detail::DecisionIndex;
  using Index = obelisk::coverage::detail::IndexMap<uint64_t, uint32_t>;
  std::shared_ptr<const Graph> graph;
  std::vector<Index> bins;
  std::vector<std::pair<uint32_t, uint32_t>> variables;
  bool matches(Graph::Root root, const std::vector<uint64_t> &tuple) const {
    if (tuple.size() != bins.size())
      return false;
    std::vector<uint32_t> ordinals(tuple.size());
    for (size_t axis = 0; axis != tuple.size(); ++axis) {
      auto bin = bins[axis].find(tuple[axis]);
      if (bin == bins[axis].end())
        return false;
      ordinals[axis] = bin->second;
    }
    while (root > 1) {
      const auto [axis, bit] = variables[graph->variable(root)];
      if (axis >= ordinals.size())
        return false;
      root = graph->child(root, (ordinals[axis] >> bit) & 1);
    }
    return root == 1;
  }
};

// Templates are partitioned once. A constructor then touches only its type's
// static rows and its newly resolved bundle, independent of earlier instances.
struct FunctionalCoverageConstructionSchemas {
  obelisk::coverage::Digest schemaFingerprint{};
  obelisk::coverage::detail::TypeHierarchyIndex hierarchy;
  obelisk::coverage::detail::IndexMap<
      uint64_t, std::shared_ptr<const obelisk::coverage::Database>>
      types;
  obelisk::coverage::detail::IndexMap<
      uint64_t, obelisk::coverage::FunctionalContainerType>
      containerTypes;
  obelisk::coverage::detail::IndexMap<
      uint64_t, obelisk::coverage::FunctionalAggregateType>
      aggregateTypes;
  obelisk::coverage::detail::IndexMap<uint64_t,
                                      obelisk::coverage::FunctionalClassType>
      classTypes;
  obelisk::coverage::detail::IndexMap<uint64_t, uint64_t> virtualInterfaceTypes;
  obelisk::coverage::detail::IndexMap<uint64_t, uint64_t>
      virtualInterfaceScopes;
  obelisk::coverage::detail::IndexMap<uint64_t, uint64_t> formalValueTypes;
  obelisk::coverage::detail::IndexSet<uint64_t> itemIDs, binIDs, valueSetIDs;
  obelisk::coverage::detail::IndexMap<uint64_t,
                                      obelisk::coverage::FunctionalItem>
      items;
  using TypeProfile = obelisk::coverage::detail::IndexMap<
      std::tuple<obelisk::coverage::FunctionalConfigurationOptionOwnerKind,
                 uint64_t,
                 obelisk::coverage::FunctionalConfigurationOptionKind>,
      std::tuple<obelisk::coverage::FunctionalConfigurationValueKind, uint64_t,
                 std::string>>;
  obelisk::coverage::detail::IndexMap<uint64_t, TypeProfile> loadedTypeProfiles;
};

struct FunctionalCoverageConfigurationSchemas {
  const obelisk::coverage::Database *source = nullptr;
  obelisk::coverage::detail::IndexMap<
      FunctionalCoverageTypeKey,
      std::shared_ptr<const obelisk::coverage::Database>>
      configurations;
};

struct FunctionalCoverageQueryMetadata {
  struct SampleTransition {
    const obelisk::coverage::ResolvedTransitionAlternative *resolved = nullptr;
    const obelisk::coverage::TransitionAlternative *definition = nullptr;
    size_t bin = 0;
    size_t activeSlot = 0;
  };
  struct SampleItem {
    uint64_t transitionLengthLimit = 0;
    size_t value = SIZE_MAX;
    size_t iff = SIZE_MAX;
    std::vector<SampleTransition> transitions;
  };
  size_t transitionSlotCount = 0;
  struct ConstructionOptions {
    uint64_t autoBinMax = 64;
    bool detectOverlap = false;
    bool crossRetainAutoBins = true;
    bool perInstance = false;
  };
  std::shared_ptr<const FunctionalCoverageConstructionSchemas> templates;
  ConstructionOptions groupOptions;
  std::vector<ConstructionOptions> itemOptions;
  std::vector<obelisk::coverage::ResolvedFunctionalItem> items;
  std::vector<obelisk::coverage::ResolvedFunctionalBin> bins;
  obelisk::coverage::detail::IndexMap<uint64_t, size_t> itemPositions;
  obelisk::coverage::detail::IndexMap<uint64_t, size_t> templateItemPositions;
  /// Same-name inherited selectors follow the effective aggregating item.
  /// SIZE_MAX marks ambiguity without a linear scan at every field read.
  obelisk::coverage::detail::IndexMap<std::pair<std::string, uint32_t>, size_t>
      inheritedItemPositions;
  obelisk::coverage::detail::IndexMap<uint64_t, size_t> binPositions;
  obelisk::coverage::detail::IndexMap<uint64_t, size_t> crossPositions;
  std::vector<std::vector<size_t>> itemBins;
  obelisk::coverage::detail::IndexSet<uint64_t> explicitAtLeastItems;
  /// Sample ordinals are frozen with the configuration. Optional defaults
  /// occupy their own slots even when omitted from a particular ABI batch.
  std::vector<const obelisk::coverage::FunctionalExpression *>
      sampleExpressions;
  std::vector<SampleItem> sampleItems;
  std::vector<size_t> binIffs;
  std::vector<const obelisk::coverage::ResolvedFunctionalValueSet *>
      binValueSets;
  obelisk::coverage::detail::IndexMap<
      uint64_t, const obelisk::coverage::ResolvedFunctionalValueSet *>
      valueSets;
};

struct FunctionalCoverageDiagnosticIndex {
  /// Live rows refer to the current run and instances, which enter the
  /// persisted schema only when a snapshot is assembled. Keeping them here
  /// leaves coverage_load's input database valid after an illegal-bin hit.
  std::vector<obelisk::coverage::IllegalBinDiagnostic> rows;
  size_t indexed = 0;
  obelisk::coverage::detail::IndexMap<
      std::tuple<obelisk::coverage::UUID, uint64_t, uint64_t, uint64_t>, size_t>
      positions;
};

struct FunctionalCoverageLoadedData {
  struct Instance {
    uint64_t typeID = 0;
    obelisk::coverage::Digest configuration{};
    obelisk::coverage::detail::IndexMap<uint64_t, uint64_t> counts;
    // Cross identities here are template IDs, shared by all configurations.
    obelisk::coverage::detail::IndexMap<
        uint64_t, obelisk::coverage::detail::IndexMap<
                      std::vector<uint64_t>, FunctionalCoverageCrossTupleState>>
        crossTuples;
    std::vector<obelisk::coverage::ResolvedInstanceOption> options;
    // The persisted profile is immutable until the next database load. Point
    // queries borrow this full profile using their original item ordinal.
    mutable std::shared_ptr<const FunctionalCoverageInstanceState> profile;
  };
  const obelisk::coverage::Database *source = nullptr;
  obelisk::coverage::detail::IndexMap<FunctionalCoverageTypeKey,
                                      std::vector<Instance>>
      configurations;
};

namespace {

bool isFunctionalRealValueKind(uint32_t kind) {
  return kind == OBELISK_RT_FUNCTIONAL_VALUE_REAL ||
         kind == OBELISK_RT_FUNCTIONAL_VALUE_SHORTREAL;
}

std::optional<double>
functionalRealValue(const obelisk_rt_functional_value_v1 &value) {
  if (!value.value || value.unknown)
    return std::nullopt;
  if (value.kind == OBELISK_RT_FUNCTIONAL_VALUE_REAL && value.bit_width == 64 &&
      value.value_size == sizeof(double)) {
    double real;
    std::memcpy(&real, value.value, sizeof(real));
    return real;
  }
  if (value.kind == OBELISK_RT_FUNCTIONAL_VALUE_SHORTREAL &&
      value.bit_width == 32 && value.value_size == sizeof(float)) {
    float real;
    std::memcpy(&real, value.value, sizeof(real));
    return static_cast<double>(real);
  }
  return std::nullopt;
}
using obelisk::coverage::detail::IndexMap;
using obelisk::coverage::detail::IndexSet;
using obelisk::coverage::detail::sortByKey;

int32_t saturateI32(uint64_t value) {
  return value > static_cast<uint64_t>(INT32_MAX) ? INT32_MAX
                                                  : static_cast<int32_t>(value);
}

uint64_t saturatingAdd(uint64_t left, uint64_t right) {
  return right > UINT64_MAX - left ? UINT64_MAX : left + right;
}

CoverageState *coverageState(obelisk_rt_context *context, bool create) {
  if (!context->coverage && create)
    context->coverage = std::make_unique<CoverageState>();
  return context->coverage.get();
}

bool saturatingIncrement(uint64_t &value) {
  if (value == UINT64_MAX)
    return true;
  ++value;
  return false;
}

bool atomicSaturatingIncrement(std::atomic<uint64_t> &value) {
  uint64_t current = value.load(std::memory_order_relaxed);
  while (current != UINT64_MAX &&
         !value.compare_exchange_weak(current, current + 1,
                                      std::memory_order_relaxed,
                                      std::memory_order_relaxed)) {
  }
  return current == UINT64_MAX;
}

struct CrossSelectorTerm {
  const obelisk::coverage::CrossSelectorNode *condition = nullptr;
  bool negated = false;
};

using CrossSelectorConjunction = std::vector<CrossSelectorTerm>;
using CrossSelectorDNF = std::vector<CrossSelectorConjunction>;

obelisk_rt_status
decodeCrossSelectorDNF(const obelisk::coverage::Database &schema,
                       const obelisk::coverage::CrossSelectorNode &root,
                       uint64_t alternativeLimit, CrossSelectorDNF &result,
                       bool substituteSetsWithFalse = false) {
  using namespace obelisk::coverage;
  result.clear();
  if (alternativeLimit == 0)
    return OBELISK_RT_OUT_OF_RESOURCES;
  std::unordered_map<uint64_t, const CrossSelectorNode *> nodes;
  for (const CrossSelectorNode &node : schema.crossSelectorNodes) {
    if (node.cross != root.cross)
      continue;
    if (!nodes.emplace(node.id, &node).second)
      return OBELISK_RT_INVALID_DESIGN;
  }

  std::vector<const CrossSelectorNode *> reachable;
  std::vector<const CrossSelectorNode *> pending{&root};
  std::unordered_set<uint64_t> visited;
  while (!pending.empty()) {
    const CrossSelectorNode *node = pending.back();
    pending.pop_back();
    if (!visited.insert(node->id).second)
      continue;
    reachable.push_back(node);
    for (uint32_t ordinal = 0; ordinal != node->operandCount; ++ordinal) {
      if (ordinal >= node->operandCount ||
          uint64_t{node->firstOperand} + ordinal >=
              schema.crossSelectorOperands.size())
        return OBELISK_RT_INVALID_DESIGN;
      const CrossSelectorOperand &operand =
          schema.crossSelectorOperands[node->firstOperand + ordinal];
      if (operand.node != node->id || operand.ordinal != ordinal)
        return OBELISK_RT_INVALID_DESIGN;
      auto child = nodes.find(operand.operand);
      if (child == nodes.end() || child->second->ordinal >= node->ordinal)
        return OBELISK_RT_INVALID_DESIGN;
      pending.push_back(child->second);
    }
  }
  std::sort(reachable.begin(), reachable.end(),
            [](const auto *left, const auto *right) {
              return left->ordinal < right->ordinal;
            });

  auto termLess = [](const CrossSelectorTerm &left,
                     const CrossSelectorTerm &right) {
    return std::tie(left.condition->id, left.negated) <
           std::tie(right.condition->id, right.negated);
  };
  auto conjunctionLess = [&](const CrossSelectorConjunction &left,
                             const CrossSelectorConjunction &right) {
    return std::lexicographical_compare(left.begin(), left.end(), right.begin(),
                                        right.end(), termLess);
  };
  auto normalize = [&](CrossSelectorDNF &dnf) {
    for (CrossSelectorConjunction &conjunction : dnf) {
      std::sort(conjunction.begin(), conjunction.end(), termLess);
      conjunction.erase(std::unique(conjunction.begin(), conjunction.end(),
                                    [](const CrossSelectorTerm &left,
                                       const CrossSelectorTerm &right) {
                                      return left.condition->id ==
                                                 right.condition->id &&
                                             left.negated == right.negated;
                                    }),
                        conjunction.end());
    }
    std::sort(dnf.begin(), dnf.end(), conjunctionLess);
    dnf.erase(std::unique(dnf.begin(), dnf.end(),
                          [](const CrossSelectorConjunction &left,
                             const CrossSelectorConjunction &right) {
                            if (left.size() != right.size())
                              return false;
                            return std::equal(
                                left.begin(), left.end(), right.begin(),
                                [](const CrossSelectorTerm &leftTerm,
                                   const CrossSelectorTerm &rightTerm) {
                                  return leftTerm.condition->id ==
                                             rightTerm.condition->id &&
                                         leftTerm.negated == rightTerm.negated;
                                });
                          }),
              dnf.end());
    // An empty conjunction is true. In a disjunction it absorbs every other
    // alternative, keeping `cross_identifier || expression` canonical and
    // preventing redundant full-product rectangle expansion.
    if (!dnf.empty() && dnf.front().empty())
      dnf.resize(1);
  };

  std::unordered_map<uint64_t, CrossSelectorDNF> decoded;
  uint64_t decodedAlternativeCount = 0;
  uint64_t decodedTermCount = 0;
  auto countTerms = [&](const CrossSelectorDNF &dnf, uint64_t &count) -> bool {
    count = 0;
    for (const CrossSelectorConjunction &conjunction : dnf) {
      if (conjunction.size() > alternativeLimit - count)
        return false;
      count += conjunction.size();
    }
    return true;
  };
  for (const CrossSelectorNode *node : reachable) {
    CrossSelectorDNF nodeDNF;
    if (node->kind == CrossSelectorKind::Binsof) {
      if (!node->target || node->operandCount || node->withExpression ||
          node->constructionExpression || node->tupleSet ||
          node->matchesExpression)
        return OBELISK_RT_INVALID_DESIGN;
      nodeDNF.push_back({{node, false}});
    } else if (node->kind == CrossSelectorKind::AllTuples) {
      if (node->target || node->bin || node->valueSet || node->withExpression ||
          node->constructionExpression || node->tupleSet ||
          node->matchesExpression || node->operandCount || node->flags ||
          node->matchesPolicy != CrossMatchesPolicy::None || node->matchesCount)
        return OBELISK_RT_INVALID_DESIGN;
      nodeDNF.emplace_back();
    } else if (node->kind == CrossSelectorKind::Not) {
      if (node->target || node->bin || node->valueSet || node->withExpression ||
          node->constructionExpression || node->tupleSet ||
          node->matchesExpression || node->operandCount != 1)
        return OBELISK_RT_INVALID_DESIGN;
      const CrossSelectorOperand &operand =
          schema.crossSelectorOperands[node->firstOperand];
      const CrossSelectorNode *child = nodes.at(operand.operand);
      if (!child || child->kind != CrossSelectorKind::Binsof ||
          !child->target || child->operandCount || child->withExpression ||
          child->constructionExpression || child->tupleSet ||
          child->matchesExpression)
        return OBELISK_RT_INVALID_DESIGN;
      nodeDNF.push_back({{child, true}});
    } else if (node->kind == CrossSelectorKind::And ||
               node->kind == CrossSelectorKind::Or) {
      if (node->target || node->bin || node->valueSet || node->withExpression ||
          node->constructionExpression || node->tupleSet ||
          node->matchesExpression || node->operandCount != 2)
        return OBELISK_RT_INVALID_DESIGN;
      const CrossSelectorOperand &leftOperand =
          schema.crossSelectorOperands[node->firstOperand];
      const CrossSelectorOperand &rightOperand =
          schema.crossSelectorOperands[node->firstOperand + 1];
      auto left = decoded.find(leftOperand.operand);
      auto right = decoded.find(rightOperand.operand);
      if (left == decoded.end() || right == decoded.end())
        return OBELISK_RT_INVALID_DESIGN;
      if (node->kind == CrossSelectorKind::Or) {
        uint64_t leftTermCount = 0;
        uint64_t rightTermCount = 0;
        if (left->second.size() > alternativeLimit ||
            right->second.size() > alternativeLimit - left->second.size() ||
            !countTerms(left->second, leftTermCount) ||
            !countTerms(right->second, rightTermCount) ||
            rightTermCount > alternativeLimit - leftTermCount)
          return OBELISK_RT_OUT_OF_RESOURCES;
        nodeDNF = left->second;
        nodeDNF.insert(nodeDNF.end(), right->second.begin(),
                       right->second.end());
      } else {
        if (!left->second.empty() &&
            right->second.size() > alternativeLimit / left->second.size())
          return OBELISK_RT_OUT_OF_RESOURCES;
        nodeDNF.reserve(left->second.size() * right->second.size());
        uint64_t nodeTermCount = 0;
        for (const CrossSelectorConjunction &leftTerms : left->second)
          for (const CrossSelectorConjunction &rightTerms : right->second) {
            if (leftTerms.size() > alternativeLimit ||
                rightTerms.size() > alternativeLimit - leftTerms.size())
              return OBELISK_RT_OUT_OF_RESOURCES;
            const uint64_t conjunctionSize =
                leftTerms.size() + rightTerms.size();
            if (conjunctionSize > alternativeLimit - nodeTermCount)
              return OBELISK_RT_OUT_OF_RESOURCES;
            nodeTermCount += conjunctionSize;
            CrossSelectorConjunction conjunction = leftTerms;
            conjunction.insert(conjunction.end(), rightTerms.begin(),
                               rightTerms.end());
            nodeDNF.push_back(std::move(conjunction));
          }
      }
    } else if (node->kind == CrossSelectorKind::Set &&
               substituteSetsWithFalse) {
      if (!node->constructionExpression || node->target || node->bin ||
          node->valueSet || node->withExpression || node->tupleSet ||
          node->operandCount)
        return OBELISK_RT_INVALID_DESIGN;
      // An empty DNF is false. Parents naturally implement false && X and
      // false || X without expanding any CrossVal value domain.
    } else {
      return OBELISK_RT_INVALID_DESIGN;
    }
    normalize(nodeDNF);
    if ((!substituteSetsWithFalse && nodeDNF.empty()) ||
        nodeDNF.size() > alternativeLimit)
      return OBELISK_RT_OUT_OF_RESOURCES;
    uint64_t nodeTermCount = 0;
    if (!countTerms(nodeDNF, nodeTermCount) ||
        nodeDNF.size() > alternativeLimit - decodedAlternativeCount ||
        nodeTermCount > alternativeLimit - decodedTermCount)
      return OBELISK_RT_OUT_OF_RESOURCES;
    decodedAlternativeCount += nodeDNF.size();
    decodedTermCount += nodeTermCount;
    decoded.emplace(node->id, std::move(nodeDNF));
  }
  auto rootDNF = decoded.find(root.id);
  if (rootDNF == decoded.end())
    return OBELISK_RT_INVALID_DESIGN;
  result = std::move(rootDNF->second);
  return OBELISK_RT_OK;
}

bool bit(const uint8_t *data, uint64_t index) {
  return (data[index / 8] >> (index % 8)) & 1;
}

void setBit(std::vector<uint8_t> &data, uint64_t index, bool value) {
  uint8_t mask = uint8_t{1} << (index % 8);
  if (value)
    data[index / 8] |= mask;
  else
    data[index / 8] &= uint8_t(~mask);
}

void recordToggleBit(CoverageState &coverage, uint64_t index, bool newValue,
                     bool newUnknown) {
  bool oldValue = bit(coverage.toggleValue.data(), index);
  bool oldUnknown = bit(coverage.toggleUnknown.data(), index);
  const bool collecting =
      coverage.toggleEnabled[index] && !coverage.toggleExcluded[index];
  if (collecting && !oldUnknown && !newUnknown && oldValue != newValue) {
    uint64_t counter = index * 4 + (oldValue ? 1 : 0);
    if (saturatingIncrement(coverage.toggleCounters[counter]))
      coverage.toggleOverflow[counter] = 1;
  } else if (collecting &&
             (oldUnknown != newUnknown ||
              (oldUnknown && newUnknown && oldValue != newValue))) {
    // Subindices 2 and 3 are diagnostic-only transitions into and out of the
    // X/Z domain. X<->Z also enters subindex 2; neither enters the denominator.
    uint64_t counter = index * 4 + (newUnknown ? 2 : 3);
    if (saturatingIncrement(coverage.toggleCounters[counter]))
      coverage.toggleOverflow[counter] = 1;
  }
  setBit(coverage.toggleValue, index, newValue);
  setBit(coverage.toggleUnknown, index, newUnknown);
}

bool schemaCounts(const obelisk::coverage::Database &schema,
                  uint64_t &lineCount, uint64_t &toggleBitCount) {
  lineCount = schema.linePoints.size();
  toggleBitCount = 0;
  for (const obelisk::coverage::ToggleObject &object : schema.toggleObjects) {
    if (object.bitWidth > UINT64_MAX - toggleBitCount)
      return false;
    toggleBitCount += object.bitWidth;
  }
  return true;
}

constexpr int32_t SVCovStart = 0;
constexpr int32_t SVCovStop = 1;
constexpr int32_t SVCovReset = 2;
constexpr int32_t SVCovCheck = 3;
constexpr int32_t SVCovModule = 10;
constexpr int32_t SVCovHierarchy = 11;
constexpr int32_t SVCovAssertion = 20;
constexpr int32_t SVCovFSM = 21;
constexpr int32_t SVCovStatement = 22;
constexpr int32_t SVCovToggle = 23;
constexpr int32_t SVCovOverflow = -2;
constexpr int32_t SVCovError = -1;
constexpr int32_t SVCovNoCoverage = 0;
constexpr int32_t SVCovOK = 1;
constexpr int32_t SVCovPartial = 2;

bool isValidSVControl(int32_t control) {
  return control >= SVCovStart && control <= SVCovCheck;
}

bool isValidSVCoverageType(int32_t coverageType) {
  return coverageType >= SVCovAssertion && coverageType <= SVCovToggle;
}

bool isValidSVScopeDefinition(int32_t scopeDefinition) {
  return scopeDefinition == SVCovModule || scopeDefinition == SVCovHierarchy;
}

uint64_t availableRunCoverage(const CoverageState &coverage) {
  using namespace obelisk::coverage;
  uint64_t flags = 0;
  for (uint64_t index = 0; index != coverage.lineCount; ++index)
    if (!coverage.lineExcluded[index]) {
      flags |= RunContainsLine;
      break;
    }
  for (uint64_t index = 0; index != coverage.toggleBitCount; ++index)
    if (!coverage.toggleExcluded[index]) {
      flags |= RunContainsToggle;
      break;
    }
  // A declared coverage model remains functional data even when it has no
  // bins or every bin is excluded. Its identities and options must survive a
  // save; contribution to the score is decided by the coverage calculation.
  if (coverage.schema && !coverage.schema->functionalTypes.empty())
    flags |= RunContainsFunctional;
  return flags;
}

uint64_t runFlagForMetric(obelisk::coverage::MetricKind metric) {
  using namespace obelisk::coverage;
  switch (metric) {
  case MetricKind::Line:
    return RunContainsLine;
  case MetricKind::Toggle:
    return RunContainsToggle;
  case MetricKind::Functional:
    return RunContainsFunctional;
  default:
    return 0;
  }
}

void retainPersistedRunData(obelisk::coverage::Database &database,
                            uint64_t persistedRunFlags) {
  using namespace obelisk::coverage;
  for (Run &run : database.runs)
    run.flags &= persistedRunFlags;
  database.runs.erase(
      std::remove_if(database.runs.begin(), database.runs.end(),
                     [](const Run &run) { return run.flags == 0; }),
      database.runs.end());
  std::set<UUID> retainedRuns;
  for (const Run &run : database.runs)
    retainedRuns.insert(run.uuid);
  database.counters.erase(
      std::remove_if(database.counters.begin(), database.counters.end(),
                     [&](const Counter &counter) {
                       return !(persistedRunFlags &
                                runFlagForMetric(counter.metric)) ||
                              !retainedRuns.count(counter.run);
                     }),
      database.counters.end());
  if (persistedRunFlags & RunContainsFunctional)
    return;

  // Resolved functional configurations and instances are execution data. The
  // static templates remain in the exact schema fingerprint so a line/toggle
  // snapshot can still be loaded and strictly merged by the same simulator.
  database.resolvedInstances.clear();
  database.resolvedInstanceOptions.clear();
  database.functionalConfigurations.clear();
  database.functionalConfigurationOptions.clear();
  database.resolvedFunctionalItems.clear();
  database.resolvedFunctionalBins.clear();
  database.resolvedTransitionAlternatives.clear();
  database.resolvedTransitionExpansionGroups.clear();
  database.resolvedTransitionSteps.clear();
  database.resolvedCrossPlans.clear();
  database.resolvedCrossAutomaticBinCountLimbs.clear();
  database.resolvedCrossAutomaticNodes.clear();
  database.resolvedCrossAutomaticEdges.clear();
  database.resolvedCrossSelectorBindings.clear();
  database.resolvedFunctionalValueSets.clear();
  database.resolvedFunctionalValueAtoms.clear();
  database.resolvedFunctionalValueLimbs.clear();
  database.resolvedFunctionalBinPlans.clear();
  database.resolvedFunctionalBinGroups.clear();
  database.resolvedFunctionalTupleSets.clear();
  database.resolvedFunctionalTupleSetTuples.clear();
  database.resolvedFunctionalTupleSetComponents.clear();
  database.sparseCrossTuples.clear();
  database.sparseCrossTupleComponents.clear();
  database.illegalBinDiagnostics.clear();
}

bool isScopeSelected(const CoverageState &coverage, uint64_t scope,
                     const std::vector<uint64_t> &roots,
                     bool includeDescendants) {
  for (size_t depth = 0; scope && depth <= coverage.scopeParents.size();
       ++depth) {
    if (std::binary_search(roots.begin(), roots.end(), scope))
      return true;
    if (!includeDescendants)
      return false;
    auto parent = coverage.scopeParents.find(scope);
    if (parent == coverage.scopeParents.end())
      return false;
    scope = parent->second;
  }
  return false;
}

bool loadedCounterHit(const CoverageState &coverage,
                      obelisk::coverage::MetricKind metric, uint64_t entity,
                      uint32_t subindex) {
  if (!coverage.schema)
    return false;
  return std::any_of(coverage.schema->counters.begin(),
                     coverage.schema->counters.end(),
                     [&](const obelisk::coverage::Counter &counter) {
                       return counter.metric == metric &&
                              counter.entity == entity &&
                              counter.subindex == subindex && counter.value;
                     });
}

int32_t controlSelectedCoverage(CoverageState &coverage, int32_t control,
                                int32_t coverageType, int32_t scopeDefinition,
                                const std::vector<uint64_t> &roots) {
  if (!isValidSVControl(control) || !isValidSVCoverageType(coverageType) ||
      !isValidSVScopeDefinition(scopeDefinition) || roots.empty())
    return SVCovError;

  // Clause 40 does not require assertion or FSM extraction. A valid stop or
  // reset remains a successful no-op; check/start report no available
  // obligations, per Table 40-1.
  if (coverageType == SVCovAssertion || coverageType == SVCovFSM)
    return control == SVCovStop || control == SVCovReset ? SVCovOK
                                                         : SVCovNoCoverage;

  bool includeDescendants = scopeDefinition == SVCovHierarchy;
  uint64_t available = 0;
  uint64_t unavailable = 0;
  std::unordered_set<uint64_t> resetEntities;

  if (coverageType == SVCovStatement) {
    for (uint64_t index = 0; index != coverage.lineCount; ++index) {
      const auto &point = coverage.schema->linePoints[index];
      if (!isScopeSelected(coverage, point.scope, roots, includeDescendants))
        continue;
      if (coverage.lineExcluded[index]) {
        unavailable = saturatingAdd(unavailable, 1);
        continue;
      }
      available = saturatingAdd(available, 1);
      if (control == SVCovStart)
        coverage.lineEnabled[index].store(1, std::memory_order_relaxed);
      else if (control == SVCovStop)
        coverage.lineEnabled[index].store(0, std::memory_order_relaxed);
      else if (control == SVCovReset) {
        coverage.lineCounters[index].store(0, std::memory_order_relaxed);
        coverage.lineOverflow[index].store(0, std::memory_order_relaxed);
        resetEntities.insert(point.id);
      }
    }
  } else {
    uint64_t base = 0;
    for (const auto &object : coverage.schema->toggleObjects) {
      bool selected =
          isScopeSelected(coverage, object.scope, roots, includeDescendants);
      for (uint64_t bitIndex = 0; bitIndex != object.bitWidth; ++bitIndex) {
        uint64_t index = base + bitIndex;
        if (!selected)
          continue;
        if (coverage.toggleExcluded[index]) {
          unavailable = saturatingAdd(unavailable, 2);
          continue;
        }
        available = saturatingAdd(available, 2);
        if (control == SVCovStart)
          coverage.toggleEnabled[index] = 1;
        else if (control == SVCovStop)
          coverage.toggleEnabled[index] = 0;
        else if (control == SVCovReset) {
          resetEntities.insert(object.id);
          std::fill_n(coverage.toggleCounters.begin() + index * 4, 4, 0);
          std::fill_n(coverage.toggleOverflow.begin() + index * 4, 4, 0);
        }
      }
      base += object.bitWidth;
    }
  }

  if (control == SVCovReset && coverage.schema) {
    obelisk::coverage::MetricKind metric =
        coverageType == SVCovStatement ? obelisk::coverage::MetricKind::Line
                                       : obelisk::coverage::MetricKind::Toggle;
    coverage.schema->counters.erase(
        std::remove_if(coverage.schema->counters.begin(),
                       coverage.schema->counters.end(),
                       [&](const obelisk::coverage::Counter &counter) {
                         return counter.metric == metric &&
                                resetEntities.count(counter.entity);
                       }),
        coverage.schema->counters.end());
  }

  if (control == SVCovStop || control == SVCovReset)
    return SVCovOK;
  if (!available)
    return SVCovNoCoverage;
  return unavailable ? SVCovPartial : SVCovOK;
}

int32_t querySelectedCoverage(const CoverageState &coverage,
                              int32_t coverageType, int32_t scopeDefinition,
                              const std::vector<uint64_t> &roots,
                              bool maximum) {
  if (!isValidSVCoverageType(coverageType) ||
      !isValidSVScopeDefinition(scopeDefinition) || roots.empty())
    return SVCovError;
  if (coverageType == SVCovAssertion || coverageType == SVCovFSM)
    return SVCovNoCoverage;

  bool includeDescendants = scopeDefinition == SVCovHierarchy;
  uint64_t covered = 0;
  uint64_t total = 0;
  if (coverageType == SVCovStatement) {
    for (uint64_t index = 0; index != coverage.lineCount; ++index) {
      const auto &point = coverage.schema->linePoints[index];
      if (coverage.lineExcluded[index] ||
          !isScopeSelected(coverage, point.scope, roots, includeDescendants))
        continue;
      total = saturatingAdd(total, 1);
      if (coverage.lineCounters[index].load(std::memory_order_relaxed) ||
          loadedCounterHit(coverage, obelisk::coverage::MetricKind::Line,
                           point.id, 0))
        covered = saturatingAdd(covered, 1);
    }
  } else {
    uint64_t base = 0;
    for (const auto &object : coverage.schema->toggleObjects) {
      bool selected =
          isScopeSelected(coverage, object.scope, roots, includeDescendants);
      for (uint64_t bitIndex = 0; bitIndex != object.bitWidth; ++bitIndex) {
        uint64_t index = base + bitIndex;
        if (!selected || coverage.toggleExcluded[index])
          continue;
        total = saturatingAdd(total, 2);
        for (uint32_t direction = 0; direction != 2; ++direction)
          if (coverage.toggleCounters[index * 4 + direction] ||
              loadedCounterHit(coverage, obelisk::coverage::MetricKind::Toggle,
                               object.id,
                               static_cast<uint32_t>(bitIndex * 4 + direction)))
            covered = saturatingAdd(covered, 1);
      }
      base += object.bitWidth;
    }
  }

  uint64_t value = maximum ? total : covered;
  return value > static_cast<uint64_t>(INT32_MAX) ? SVCovOverflow
                                                  : static_cast<int32_t>(value);
}

struct PersistentCoverageMetric {
  obelisk::coverage::MetricKind metric;
  uint64_t runFlag;
};

std::optional<PersistentCoverageMetric>
persistentCoverageMetric(int32_t coverageType) {
  using namespace obelisk::coverage;
  if (coverageType == SVCovStatement)
    return PersistentCoverageMetric{MetricKind::Line, RunContainsLine};
  if (coverageType == SVCovToggle)
    return PersistentCoverageMetric{MetricKind::Toggle, RunContainsToggle};
  return std::nullopt;
}

void retainPersistentMetric(obelisk::coverage::Database &database,
                            PersistentCoverageMetric selected) {
  using namespace obelisk::coverage;
  database.counters.erase(
      std::remove_if(database.counters.begin(), database.counters.end(),
                     [&](const Counter &counter) {
                       return counter.metric != selected.metric;
                     }),
      database.counters.end());
  database.runs.erase(std::remove_if(database.runs.begin(), database.runs.end(),
                                     [&](Run &run) {
                                       if (!(run.flags & selected.runFlag))
                                         return true;
                                       run.flags = selected.runFlag;
                                       return false;
                                     }),
                      database.runs.end());
  database.counters.erase(
      std::remove_if(database.counters.begin(), database.counters.end(),
                     [&](const Counter &counter) {
                       return std::none_of(database.runs.begin(),
                                           database.runs.end(),
                                           [&](const Run &run) {
                                             return run.uuid == counter.run;
                                           });
                     }),
      database.counters.end());
  // Statement and toggle persistence never smuggles functional per-run state
  // into a metric-specific Clause 40 database. The typed static functional
  // schema remains present because it participates in the strict fingerprint.
  database.resolvedInstances.clear();
  database.resolvedInstanceOptions.clear();
  database.functionalConfigurations.clear();
  database.functionalConfigurationOptions.clear();
  database.resolvedFunctionalItems.clear();
  database.resolvedFunctionalBins.clear();
  database.resolvedTransitionAlternatives.clear();
  database.resolvedTransitionExpansionGroups.clear();
  database.resolvedTransitionSteps.clear();
  database.resolvedCrossPlans.clear();
  database.resolvedCrossAutomaticBinCountLimbs.clear();
  database.resolvedCrossAutomaticNodes.clear();
  database.resolvedCrossAutomaticEdges.clear();
  database.resolvedCrossSelectorBindings.clear();
  database.resolvedFunctionalValueSets.clear();
  database.resolvedFunctionalValueAtoms.clear();
  database.resolvedFunctionalValueLimbs.clear();
  database.resolvedFunctionalBinPlans.clear();
  database.resolvedFunctionalBinGroups.clear();
  database.resolvedFunctionalTupleSets.clear();
  database.resolvedFunctionalTupleSetTuples.clear();
  database.resolvedFunctionalTupleSetComponents.clear();
  database.sparseCrossTuples.clear();
  database.sparseCrossTupleComponents.clear();
  database.illegalBinDiagnostics.clear();
}

obelisk::coverage::Status
mergePersistentMetricSlice(obelisk::coverage::Database &destination,
                           const obelisk::coverage::Database &source,
                           PersistentCoverageMetric selected) {
  using namespace obelisk::coverage;
  Database result = destination;
  Status status = validate(result);
  if (status != Status::Ok)
    return status;
  status = validate(source);
  if (status != Status::Ok)
    return status;
  if (computeSchemaFingerprint(result) != computeSchemaFingerprint(source))
    return Status::SchemaMismatch;

  IndexMap<UUID, size_t> runPositions;
  for (size_t index = 0; index != result.runs.size(); ++index)
    runPositions.emplace(result.runs[index].uuid, index);
  for (const Run &run : source.runs) {
    if (run.flags != selected.runFlag)
      return Status::InvalidDatabase;
    auto found = runPositions.find(run.uuid);
    if (found == runPositions.end()) {
      runPositions.emplace(run.uuid, result.runs.size());
      result.runs.push_back(run);
      continue;
    }
    auto *existing = &result.runs[found->second];

    // Clause 40 database operations select one metric at a time. Two slices
    // from the same simulation may therefore share a run UUID while carrying
    // disjoint metric contributions. This is deliberately narrower than the
    // native database merge operation, which always rejects duplicate UUIDs.
    if ((existing->flags & selected.runFlag) || existing->name != run.name ||
        existing->status != run.status || existing->seed != run.seed ||
        existing->tags != run.tags)
      return Status::DuplicateRun;
    existing->flags |= selected.runFlag;
    existing->simulationTime =
        std::max(existing->simulationTime, run.simulationTime);
    if (!existing->timestamp ||
        (run.timestamp && run.timestamp < existing->timestamp))
      existing->timestamp = run.timestamp;
  }
  for (const Counter &counter : source.counters) {
    if (counter.metric != selected.metric)
      return Status::InvalidDatabase;
    result.counters.push_back(counter);
  }
  sortByKey(result.counters.begin(), result.counters.end(),
            [](const auto &row) {
              return std::tie(row.run, row.metric, row.entity, row.instance,
                              row.subindex);
            });
  sortByKey(result.runs.begin(), result.runs.end(),
            [](const Run &run) { return run.uuid; });
  status = validate(result);
  if (status != Status::Ok)
    return status;
  destination = std::move(result);
  return Status::Ok;
}

struct FailedCoverageSaveCleanup {
  const std::string &path;
  bool armed = false;

  ~FailedCoverageSaveCleanup() {
    if (armed)
      std::remove(path.c_str());
  }
};

obelisk_rt_status copyManagedCoverageString(obelisk_rt_string_v1 value,
                                            std::string &result) {
  char scratch[8] = {};
  const char *bytes = nullptr;
  uint64_t size = 0;
  obelisk_rt_status status =
      obelisk_rt_v1_string_view(value, scratch, &bytes, &size);
  if (status != OBELISK_RT_OK)
    return status;
  if (size > SIZE_MAX)
    return OBELISK_RT_OUT_OF_RESOURCES;
  result = size ? std::string(bytes, static_cast<size_t>(size)) : std::string();
  return OBELISK_RT_OK;
}

uint64_t processID() {
#if defined(__unix__) || defined(__APPLE__)
  return static_cast<uint64_t>(getpid());
#else
  // Freestanding and wasm runtimes have a single process namespace. A
  // process-local static address supplies the collision-avoidance identity
  // needed by %p without introducing a host LLVM dependency or speculative
  // platform headers.
  static const uint8_t processMarker = 0;
  return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&processMarker));
#endif
}

std::string sanitizeTestName(const std::string &name) {
  std::string result;
  result.reserve(name.size());
  for (unsigned char character : name)
    result.push_back(std::isalnum(character) || character == '.' ||
                             character == '-' || character == '_'
                         ? static_cast<char>(character)
                         : '_');
  return result.empty() ? "test" : result;
}

std::string expandOutputPath(const CoverageState &coverage) {
  if (!coverage.outputExplicit)
    return coverage.outputPath;
  std::string result;
  const std::string process = std::to_string(processID());
  const std::string test = sanitizeTestName(coverage.testName);
  for (size_t index = 0; index != coverage.outputPath.size(); ++index) {
    if (coverage.outputPath[index] == '%' &&
        index + 1 < coverage.outputPath.size()) {
      char substitution = coverage.outputPath[index + 1];
      if (substitution == 'p' || substitution == 't') {
        result += substitution == 'p' ? process : test;
        ++index;
        continue;
      }
    }
    result.push_back(coverage.outputPath[index]);
  }
  return result;
}

obelisk::coverage::UUID makeRunUUID(obelisk_rt_context *context,
                                    CoverageState &coverage) {
  if (!std::all_of(coverage.runUUID.begin(), coverage.runUUID.end(),
                   [](uint8_t byte) { return byte == 0; }))
    return coverage.runUUID;
  std::array<uint64_t, 4> identity{
      static_cast<uint64_t>(
          std::chrono::high_resolution_clock::now().time_since_epoch().count()),
      processID(), static_cast<uint64_t>(reinterpret_cast<uintptr_t>(context)),
      static_cast<uint64_t>(
          std::hash<std::thread::id>{}(std::this_thread::get_id()))};
  obelisk::coverage::Digest digest = obelisk::coverage::sha256(
      reinterpret_cast<const uint8_t *>(identity.data()), sizeof(identity));
  std::copy_n(digest.begin(), coverage.runUUID.size(),
              coverage.runUUID.begin());
  if (std::all_of(coverage.runUUID.begin(), coverage.runUUID.end(),
                  [](uint8_t byte) { return byte == 0; }))
    coverage.runUUID.back() = 1;
  return coverage.runUUID;
}

obelisk_rt_status mapCoverageStatus(obelisk::coverage::Status status);

obelisk_rt_status makeSnapshot(obelisk_rt_context *context,
                               CoverageState &coverage, uint32_t runStatus,
                               uint64_t simulationTime,
                               obelisk::coverage::Database &result) {
  if (!coverage.schema)
    return OBELISK_RT_INVALID_LIFECYCLE;
  result = *coverage.schema;
  obelisk::coverage::Diagnostic canonicalDiagnostic;
  auto canonicalStatus =
      obelisk::coverage::canonicalizeFunctionalConfigurations(
          result, &canonicalDiagnostic);
  if (canonicalStatus != obelisk::coverage::Status::Ok)
    return mapCoverageStatus(canonicalStatus);
  retainPersistedRunData(result, coverage.persistedRunFlags);
  obelisk::coverage::Run run;
  run.uuid = makeRunUUID(context, coverage);
  run.name = coverage.testName.empty() ? "default" : coverage.testName;
  run.status = runStatus;
  run.simulationTime = simulationTime;
  run.timestamp = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
  run.seed = context->configuredSeed;
  run.flags = availableRunCoverage(coverage) & coverage.persistedRunFlags;
  run.tags = coverage.tags;
  sortByKey(run.tags.begin(), run.tags.end(),
            [](const auto &tag) { return tag; });
  if (std::any_of(
          result.runs.begin(), result.runs.end(),
          [&](const auto &existing) { return existing.uuid == run.uuid; }))
    return OBELISK_RT_INVALID_LIFECYCLE;
  result.runs.push_back(run);

  if (coverage.finalized && (run.flags & obelisk::coverage::RunContainsLine)) {
    for (uint64_t index = 0; index != coverage.lineCount; ++index)
      result.counters.push_back(
          {run.uuid, obelisk::coverage::MetricKind::Line,
           result.linePoints[index].id, 0, 0,
           coverage.lineOverflow[index].load(std::memory_order_relaxed) ? 1u
                                                                        : 0u,
           coverage.lineCounters[index].load(std::memory_order_relaxed)});
  }
  if (coverage.finalized &&
      (run.flags & obelisk::coverage::RunContainsToggle)) {
    uint64_t base = 0;
    for (const obelisk::coverage::ToggleObject &object : result.toggleObjects) {
      for (uint64_t bitIndex = 0; bitIndex != object.bitWidth; ++bitIndex)
        for (uint32_t direction = 0; direction != 4; ++direction)
          result.counters.push_back(
              {run.uuid, obelisk::coverage::MetricKind::Toggle, object.id, 0,
               static_cast<uint32_t>(bitIndex * 4 + direction),
               coverage.toggleOverflow[(base + bitIndex) * 4 + direction] ? 1u
                                                                          : 0u,
               coverage.toggleCounters[(base + bitIndex) * 4 + direction]});
      base += object.bitWidth;
    }
  }
  if (run.flags & obelisk::coverage::RunContainsFunctional) {
    if (coverage.diagnosticIndex)
      result.illegalBinDiagnostics.insert(
          result.illegalBinDiagnostics.end(),
          coverage.diagnosticIndex->rows.begin(),
          coverage.diagnosticIndex->rows.end());
    using Owner = obelisk::coverage::FunctionalConfigurationOptionOwnerKind;
    using Option = obelisk::coverage::FunctionalConfigurationOptionKind;
    using ValueKind = obelisk::coverage::FunctionalConfigurationValueKind;
    auto appendTypeUnsignedOption = [&](uint64_t owner, Owner ownerKind,
                                        Option option, uint64_t value) {
      result.resolvedInstanceOptions.push_back({run.uuid,
                                                0,
                                                owner,
                                                ownerKind,
                                                option,
                                                ValueKind::Unsigned,
                                                {},
                                                value,
                                                0});
    };
    for (const auto &[typeID, options] : coverage.typeOptions) {
      if (options.goal)
        appendTypeUnsignedOption(typeID, Owner::Group, Option::Goal,
                                 *options.goal);
      if (options.weight)
        appendTypeUnsignedOption(typeID, Owner::Group, Option::Weight,
                                 *options.weight);
      if (options.mergeInstances)
        appendTypeUnsignedOption(typeID, Owner::Group, Option::MergeInstances,
                                 *options.mergeInstances ? 1 : 0);
      if (options.distributeFirst)
        appendTypeUnsignedOption(typeID, Owner::Group, Option::DistributeFirst,
                                 *options.distributeFirst ? 1 : 0);
      for (const auto &[item, value] : options.itemGoals)
        appendTypeUnsignedOption(item, Owner::Item, Option::Goal, value);
      for (const auto &[item, value] : options.itemWeights)
        appendTypeUnsignedOption(item, Owner::Item, Option::Weight, value);
      for (const auto &[item, value] : options.comments)
        result.resolvedInstanceOptions.push_back(
            {run.uuid, 0, item ? item : typeID,
             item ? Owner::Item : Owner::Group, Option::Comment,
             ValueKind::String, value, 0, 0});
    }
    using ResolvedKey =
        std::tuple<uint64_t, obelisk::coverage::Digest, uint64_t>;
    IndexMap<ResolvedKey, const obelisk::coverage::ResolvedFunctionalItem *>
        itemsByID;
    for (const auto &item : result.resolvedFunctionalItems)
      itemsByID.emplace(ResolvedKey{item.type, item.configuration, item.id},
                        &item);
    IndexMap<ResolvedKey, uint64_t> crossTemplates;
    IndexMap<std::pair<uint64_t, obelisk::coverage::Digest>,
             const FunctionalCoverageTypeState *>
        typesByConfiguration;
    for (const auto &[key, type] : coverage.types) {
      typesByConfiguration.emplace(
          std::make_pair(key.typeID, key.configuration), &type);
      for (const auto &cross : type.crosses)
        crossTemplates.emplace(
            ResolvedKey{key.typeID, key.configuration, cross.id},
            cross.templateItem);
    }
    IndexMap<uint64_t, const FunctionalCoverageInstanceState *>
        instancesByHandle;
    for (const auto &[handle, instance] : coverage.instances)
      instancesByHandle.emplace(handle, &instance);
    for (const auto &[handle, state] : instancesByHandle) {
      const auto &instance = *state;
      auto type =
          typesByConfiguration.find({instance.typeID, instance.configuration});
      if (type == typesByConfiguration.end())
        continue;
      IndexMap<uint64_t, const std::string *> comments;
      for (const auto &[owner, comment] : instance.comments)
        comments.emplace(owner, &comment);
      IndexMap<uint64_t, uint64_t> printMissingByItem;
      for (const auto &[owner, value] : instance.crossNumPrintMissing)
        printMissingByItem.emplace(owner, value);
      IndexSet<uint64_t> explicitPrintMissingItems;
      for (uint64_t owner : instance.explicitCrossNumPrintMissingItems)
        explicitPrintMissingItems.insert(owner);
      result.resolvedInstances.push_back(
          {run.uuid, instance.typeID, handle, instance.name,
           instance.generatedName
               ? obelisk::coverage::ResolvedInstanceGeneratedName
               : 0,
           instance.configuration});
      auto appendUnsignedOption = [&](uint64_t owner, Owner ownerKind,
                                      Option option, uint64_t value) {
        result.resolvedInstanceOptions.push_back({run.uuid,
                                                  handle,
                                                  owner,
                                                  ownerKind,
                                                  option,
                                                  ValueKind::Unsigned,
                                                  {},
                                                  value,
                                                  0});
      };
      appendUnsignedOption(0, Owner::Group, Option::Goal,
                           instance.instanceGoal);
      appendUnsignedOption(0, Owner::Group, Option::Weight,
                           instance.instanceWeight);
      appendUnsignedOption(0, Owner::Group, Option::AtLeast,
                           instance.groupAtLeast);
      appendUnsignedOption(0, Owner::Group, Option::CrossNumPrintMissing,
                           instance.groupCrossNumPrintMissing);
      if (auto comment = comments.find(0); comment != comments.end())
        result.resolvedInstanceOptions.push_back(
            {run.uuid, handle, 0, Owner::Group, Option::Comment,
             ValueKind::String, *comment->second, 0, 0});
      if (instance.itemGoals.size() != type->second->items.size() ||
          instance.itemWeights.size() != type->second->items.size() ||
          instance.itemAtLeast.size() != type->second->items.size())
        return OBELISK_RT_INVALID_DESIGN;
      for (size_t itemIndex = 0; itemIndex != type->second->items.size();
           ++itemIndex) {
        uint64_t resolvedID = type->second->items[itemIndex];
        auto foundItem = itemsByID.find(
            {instance.typeID, instance.configuration, resolvedID});
        if (foundItem == itemsByID.end())
          return OBELISK_RT_INVALID_DESIGN;
        const auto *resolved = foundItem->second;
        appendUnsignedOption(resolved->templateItem, Owner::Item, Option::Goal,
                             instance.itemGoals[itemIndex]);
        appendUnsignedOption(resolved->templateItem, Owner::Item,
                             Option::Weight, instance.itemWeights[itemIndex]);
        appendUnsignedOption(resolved->templateItem, Owner::Item,
                             Option::AtLeast, instance.itemAtLeast[itemIndex]);
        if (explicitPrintMissingItems.count(resolved->templateItem)) {
          auto printMissing = printMissingByItem.find(resolved->templateItem);
          if (resolved->kind != obelisk::coverage::FunctionalItemKind::Cross ||
              printMissing == printMissingByItem.end())
            return OBELISK_RT_INVALID_DESIGN;
          appendUnsignedOption(resolved->templateItem, Owner::Item,
                               Option::CrossNumPrintMissing,
                               printMissing->second);
        }
        if (auto comment = comments.find(resolved->templateItem);
            comment != comments.end())
          result.resolvedInstanceOptions.push_back(
              {run.uuid, handle, resolved->templateItem, Owner::Item,
               Option::Comment, ValueKind::String, *comment->second, 0, 0});
      }
      if (instance.bins.size() != type->second->bins.size())
        return OBELISK_RT_INVALID_DESIGN;
      for (const FunctionalCoverageBinState &counter : instance.bins)
        result.counters.push_back(
            {run.uuid, obelisk::coverage::MetricKind::Functional, counter.id,
             handle, 0, counter.overflow ? 1u : 0u, counter.count});
      for (const auto &crossEntry : instance.crossTuples) {
        const uint64_t resolvedCross = crossEntry.first;
        const auto &tuples = crossEntry.second;
        auto cross = crossTemplates.find(
            {instance.typeID, instance.configuration, resolvedCross});
        if (cross == crossTemplates.end())
          return OBELISK_RT_INVALID_DESIGN;
        for (const auto &[components, counter] : tuples) {
          if (components.size() > UINT32_MAX ||
              result.sparseCrossTupleComponents.size() > UINT32_MAX ||
              components.size() >
                  UINT32_MAX - result.sparseCrossTupleComponents.size())
            return OBELISK_RT_OUT_OF_RESOURCES;
          const uint32_t first =
              static_cast<uint32_t>(result.sparseCrossTupleComponents.size());
          result.sparseCrossTupleComponents.insert(
              result.sparseCrossTupleComponents.end(), components.begin(),
              components.end());
          result.sparseCrossTuples.push_back(
              {run.uuid, handle, cross->second, first,
               static_cast<uint32_t>(components.size()), counter.count,
               counter.overflow ? obelisk::coverage::SparseCrossTupleOverflow
                                : 0});
        }
      }
    }
  }
  sortByKey(result.runs.begin(), result.runs.end(),
            [](const auto &row) { return row.uuid; });
  sortByKey(result.counters.begin(), result.counters.end(),
            [](const auto &row) {
              return std::tie(row.run, row.metric, row.entity, row.instance,
                              row.subindex);
            });
  sortByKey(result.resolvedInstances.begin(), result.resolvedInstances.end(),
            [](const auto &row) { return std::tie(row.run, row.id); });
  sortByKey(result.resolvedInstanceOptions.begin(),
            result.resolvedInstanceOptions.end(), [](const auto &row) {
              return std::tie(row.run, row.instance, row.ownerKind, row.owner,
                              row.option);
            });
  sortByKey(result.illegalBinDiagnostics.begin(),
            result.illegalBinDiagnostics.end(), [](const auto &row) {
              return std::tie(row.run, row.instance, row.bin,
                              row.simulationTime);
            });
  struct OrderedSparseTuple {
    obelisk::coverage::SparseCrossTuple tuple;
    std::vector<uint64_t> components;
  };
  std::vector<OrderedSparseTuple> sparse;
  sparse.reserve(result.sparseCrossTuples.size());
  for (const auto &tuple : result.sparseCrossTuples) {
    if (uint64_t{tuple.firstComponent} + tuple.componentCount >
        result.sparseCrossTupleComponents.size())
      return OBELISK_RT_INVALID_DESIGN;
    sparse.push_back(
        {tuple,
         std::vector<uint64_t>(
             result.sparseCrossTupleComponents.begin() + tuple.firstComponent,
             result.sparseCrossTupleComponents.begin() + tuple.firstComponent +
                 tuple.componentCount)});
  }
  sortByKey(sparse.begin(), sparse.end(), [](const auto &row) {
    return std::tie(row.tuple.run, row.tuple.instance, row.tuple.cross,
                    row.components);
  });
  result.sparseCrossTuples.clear();
  result.sparseCrossTupleComponents.clear();
  for (OrderedSparseTuple &entry : sparse) {
    entry.tuple.firstComponent =
        static_cast<uint32_t>(result.sparseCrossTupleComponents.size());
    result.sparseCrossTupleComponents.insert(
        result.sparseCrossTupleComponents.end(), entry.components.begin(),
        entry.components.end());
    result.sparseCrossTuples.push_back(entry.tuple);
  }
  result.schemaFingerprint =
      obelisk::coverage::computeSchemaFingerprint(result);
  return OBELISK_RT_OK;
}

obelisk_rt_status mapCoverageStatus(obelisk::coverage::Status status) {
  switch (status) {
  case obelisk::coverage::Status::Ok:
    return OBELISK_RT_OK;
  case obelisk::coverage::Status::OutOfMemory:
    return OBELISK_RT_OUT_OF_MEMORY;
  case obelisk::coverage::Status::IoError:
    return OBELISK_RT_IO_ERROR;
  default:
    return OBELISK_RT_INVALID_DESIGN;
  }
}

struct CoverageResult {
  double percentage = 0.0;
  uint64_t covered = 0;
  uint64_t total = 0;
  bool contributes = false;
};

struct FunctionalCoverageSetQueryIndex;
obelisk_rt_status
markStaticallyEmptyStateBins(obelisk::coverage::Database &schema,
                             uint64_t typeID,
                             const obelisk::coverage::Digest &configuration,
                             FunctionalCoverageSetQueryIndex &setQueries);
obelisk_rt_status
deriveTransitionExclusions(const obelisk::coverage::Database &schema,
                           uint64_t typeID,
                           const obelisk::coverage::Digest &configuration,
                           std::unordered_set<uint64_t> &suppressedAlternatives,
                           std::unordered_set<uint64_t> &emptyBins,
                           FunctionalCoverageSetQueryIndex &setQueries);
bool functionalBooleanTrue(const obelisk_rt_functional_value_v1 &value);

struct FunctionalIdentityReservations {
  const IndexSet<uint64_t> &templates;
  IndexSet<uint64_t> resolved;

  size_t count(uint64_t id) const {
    return templates.count(id) || resolved.count(id);
  }
  void insert(uint64_t id) { resolved.insert(id); }
};

template <typename Reservations>
uint64_t resolvedFunctionalID(const char *nameSpace, uint64_t typeID,
                              uint64_t templateID, uint32_t ordinal,
                              const Reservations &occupied) {
  uint64_t hash = OBELISK_STABLE_HASH_OFFSET_BASIS;
  hash = obelisk_stable_hash_append(hash, nameSpace, std::strlen(nameSpace));
  hash = obelisk_stable_hash_append_byte(hash, 0);
  hash = obelisk_stable_hash_append_uint_le(hash, typeID, 8);
  hash = obelisk_stable_hash_append_uint_le(hash, templateID, 8);
  hash = obelisk_stable_hash_append_uint_le(hash, ordinal, 4);
  // Stable identities are never repaired with a physical-table ordinal: a
  // collision is an invalid design, exactly like a compiler stable-ID
  // collision.  This keeps the identity invariant under unrelated insertion.
  return hash && !occupied.count(hash) ? hash : 0;
}

struct FunctionalInteger {
  bool negative = false;
  std::vector<uint64_t> magnitude;
};

void normalizeMagnitude(FunctionalInteger &value) {
  while (!value.magnitude.empty() && !value.magnitude.back())
    value.magnitude.pop_back();
  if (value.magnitude.empty())
    value.negative = false;
}

int compareMagnitude(const std::vector<uint64_t> &left,
                     const std::vector<uint64_t> &right) {
  if (left.size() != right.size())
    return left.size() < right.size() ? -1 : 1;
  for (size_t reverse = left.size(); reverse != 0; --reverse) {
    size_t index = reverse - 1;
    if (left[index] != right[index])
      return left[index] < right[index] ? -1 : 1;
  }
  return 0;
}

int compareFunctionalInteger(const FunctionalInteger &left,
                             const FunctionalInteger &right) {
  if (left.negative != right.negative)
    return left.negative ? -1 : 1;
  int magnitude = compareMagnitude(left.magnitude, right.magnitude);
  return left.negative ? -magnitude : magnitude;
}

uint64_t readLittleEndianLimb(const void *plane, uint64_t size,
                              uint32_t ordinal) {
  uint64_t offset = uint64_t{ordinal} * 8;
  if (!plane || offset >= size)
    return 0;
  const auto *bytes = static_cast<const uint8_t *>(plane);
  uint64_t value = 0;
  for (uint32_t byte = 0; byte != 8 && offset + byte < size; ++byte)
    value |= uint64_t{bytes[offset + byte]} << (byte * 8);
  return value;
}

uint64_t lowBitsMask(uint64_t bitCount) {
  return bitCount >= 64 ? UINT64_MAX : (uint64_t{1} << bitCount) - uint64_t{1};
}

uint32_t functionalLimbCount(uint32_t bitWidth) {
  return static_cast<uint32_t>((uint64_t{bitWidth} + 63) / 64);
}

FunctionalInteger
functionalIntegerFromBits(std::vector<uint64_t> bits, uint32_t bitWidth,
                          obelisk::coverage::CoverageSignedness signedness) {
  FunctionalInteger result;
  result.negative =
      signedness == obelisk::coverage::CoverageSignedness::Signed &&
      ((bits.back() >> ((bitWidth - 1) % 64)) & 1);
  result.magnitude = std::move(bits);
  if (result.negative) {
    uint64_t carry = 1;
    for (uint64_t &limb : result.magnitude) {
      limb = ~limb;
      uint64_t previous = limb;
      limb += carry;
      carry = carry && limb < previous;
    }
    if (bitWidth % 64)
      result.magnitude.back() &= lowBitsMask(bitWidth % 64);
  }
  normalizeMagnitude(result);
  return result;
}

bool decodeFunctionalInteger(const obelisk_rt_functional_value_v1 &descriptor,
                             obelisk::coverage::CoverageSignedness signedness,
                             FunctionalInteger &result, bool &hasUnknown) {
  if ((descriptor.kind != OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL &&
       descriptor.kind != OBELISK_RT_FUNCTIONAL_VALUE_FOUR_STATE) ||
      !descriptor.bit_width ||
      descriptor.value_size != (descriptor.bit_width + 7) / 8 ||
      !descriptor.value ||
      (descriptor.kind == OBELISK_RT_FUNCTIONAL_VALUE_FOUR_STATE) !=
          (descriptor.unknown != nullptr) ||
      (signedness != obelisk::coverage::CoverageSignedness::Signed &&
       signedness != obelisk::coverage::CoverageSignedness::Unsigned) ||
      descriptor.bit_width > UINT32_MAX)
    return false;

  uint32_t limbCount = static_cast<uint32_t>((descriptor.bit_width + 63) / 64);
  std::vector<uint64_t> bits(limbCount);
  hasUnknown = false;
  for (uint32_t limb = 0; limb != limbCount; ++limb) {
    uint64_t unknown =
        readLittleEndianLimb(descriptor.unknown, descriptor.value_size, limb);
    uint64_t encoded =
        readLittleEndianLimb(descriptor.value, descriptor.value_size, limb);
    bits[limb] = descriptor.kind == OBELISK_RT_FUNCTIONAL_VALUE_FOUR_STATE
                     ? encoded ^ unknown
                     : encoded;
    if (limb + 1 == limbCount && descriptor.bit_width % 64) {
      uint64_t mask = lowBitsMask(descriptor.bit_width % 64);
      bits[limb] &= mask;
      unknown &= mask;
    }
    hasUnknown |= unknown != 0;
  }

  result = functionalIntegerFromBits(
      std::move(bits), static_cast<uint32_t>(descriptor.bit_width), signedness);
  return true;
}

struct FunctionalWildcardCube {
  uint32_t bitWidth = 0;
  obelisk::coverage::CoverageSignedness signedness =
      obelisk::coverage::CoverageSignedness::NotApplicable;
  std::vector<uint64_t> aval;
  std::vector<uint64_t> wildcard;
};

bool cubeBit(const std::vector<uint64_t> &plane, uint32_t index) {
  return (plane[index / 64] >> (index % 64)) & 1;
}

void setCubeBit(std::vector<uint64_t> &plane, uint32_t index, bool value) {
  uint64_t mask = uint64_t{1} << (index % 64);
  if (value)
    plane[index / 64] |= mask;
  else
    plane[index / 64] &= ~mask;
}

bool decodeFunctionalWildcardCube(
    const obelisk_rt_functional_value_v1 &descriptor,
    obelisk::coverage::CoverageSignedness signedness,
    FunctionalWildcardCube &result) {
  if ((descriptor.kind != OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL &&
       descriptor.kind != OBELISK_RT_FUNCTIONAL_VALUE_FOUR_STATE) ||
      !descriptor.bit_width || descriptor.bit_width > UINT32_MAX ||
      descriptor.value_size != (descriptor.bit_width + 7) / 8 ||
      !descriptor.value ||
      (descriptor.kind == OBELISK_RT_FUNCTIONAL_VALUE_FOUR_STATE) !=
          (descriptor.unknown != nullptr) ||
      (signedness != obelisk::coverage::CoverageSignedness::Signed &&
       signedness != obelisk::coverage::CoverageSignedness::Unsigned))
    return false;
  result.bitWidth = static_cast<uint32_t>(descriptor.bit_width);
  result.signedness = signedness;
  uint32_t limbCount = (descriptor.bit_width + 63) / 64;
  result.aval.resize(limbCount);
  result.wildcard.resize(limbCount);
  for (uint32_t limb = 0; limb != limbCount; ++limb) {
    uint64_t unknown =
        readLittleEndianLimb(descriptor.unknown, descriptor.value_size, limb);
    uint64_t encoded =
        readLittleEndianLimb(descriptor.value, descriptor.value_size, limb);
    uint64_t aval = descriptor.kind == OBELISK_RT_FUNCTIONAL_VALUE_FOUR_STATE
                        ? encoded ^ unknown
                        : encoded;
    result.wildcard[limb] = unknown;
    result.aval[limb] = aval & ~unknown;
  }
  if (result.bitWidth % 64) {
    uint64_t mask = lowBitsMask(result.bitWidth % 64);
    result.aval.back() &= mask;
    result.wildcard.back() &= mask;
  }
  return true;
}

FunctionalInteger wildcardCubeExtreme(const FunctionalWildcardCube &cube,
                                      bool maximum) {
  std::vector<uint64_t> bits = cube.aval;
  for (uint32_t bit = 0; bit != cube.bitWidth; ++bit)
    if (cubeBit(cube.wildcard, bit))
      setCubeBit(bits, bit, maximum);
  if (cube.signedness == obelisk::coverage::CoverageSignedness::Signed) {
    uint32_t sign = cube.bitWidth - 1;
    const bool signCanZero =
        cubeBit(cube.wildcard, sign) || !cubeBit(cube.aval, sign);
    const bool signCanOne =
        cubeBit(cube.wildcard, sign) || cubeBit(cube.aval, sign);
    setCubeBit(bits, sign, maximum ? !signCanZero : signCanOne);
  }
  return functionalIntegerFromBits(std::move(bits), cube.bitWidth,
                                   cube.signedness);
}

bool functionalIntegerRepresentable(
    const FunctionalInteger &value, uint32_t bitWidth,
    obelisk::coverage::CoverageSignedness signedness);

std::vector<FunctionalWildcardCube> castFunctionalWildcardCube(
    const FunctionalWildcardCube &source, uint32_t targetWidth,
    obelisk::coverage::CoverageSignedness targetSignedness) {
  using obelisk::coverage::CoverageSignedness;
  auto constrain = [](FunctionalWildcardCube &cube, uint32_t begin,
                      uint32_t end, bool value) {
    for (uint32_t bit = begin; bit < end; ++bit) {
      if (cubeBit(cube.wildcard, bit)) {
        setCubeBit(cube.wildcard, bit, false);
        setCubeBit(cube.aval, bit, value);
      } else if (cubeBit(cube.aval, bit) != value) {
        return false;
      }
    }
    return true;
  };
  auto addConstrained = [&](std::vector<FunctionalWildcardCube> &candidates,
                            uint32_t begin, bool value) {
    FunctionalWildcardCube cube = source;
    if (begin >= source.bitWidth ||
        constrain(cube, begin, source.bitWidth, value))
      candidates.push_back(std::move(cube));
  };

  const bool sourceSigned = source.signedness == CoverageSignedness::Signed;
  const bool targetSigned = targetSignedness == CoverageSignedness::Signed;
  std::vector<FunctionalWildcardCube> candidates;
  if (sourceSigned && targetSigned && source.bitWidth > targetWidth) {
    // A narrowing signed conversion retains the representable positive and
    // negative sign-extension prefixes as two disjoint subcubes.
    addConstrained(candidates, targetWidth - 1, false);
    addConstrained(candidates, targetWidth - 1, true);
  } else if (sourceSigned && !targetSigned) {
    // Unsigned targets exclude every negative source value. Wider sources
    // must additionally have zero in every bit discarded by the conversion.
    addConstrained(candidates, std::min(targetWidth, source.bitWidth - 1),
                   false);
  } else if (!sourceSigned && targetSigned) {
    // The target sign bit and every wider source bit must be zero.
    addConstrained(candidates, std::min(targetWidth - 1, source.bitWidth),
                   false);
  } else {
    // Unsigned narrowing only retains values whose discarded prefix is zero.
    addConstrained(candidates, std::min(targetWidth, source.bitWidth), false);
  }

  if (sourceSigned && targetSigned && targetWidth > source.bitWidth &&
      cubeBit(source.wildcard, source.bitWidth - 1)) {
    std::vector<FunctionalWildcardCube> split;
    for (const FunctionalWildcardCube &candidate : candidates) {
      FunctionalWildcardCube positive = candidate;
      FunctionalWildcardCube negative = candidate;
      if (constrain(positive, source.bitWidth - 1, source.bitWidth, false))
        split.push_back(std::move(positive));
      if (constrain(negative, source.bitWidth - 1, source.bitWidth, true))
        split.push_back(std::move(negative));
    }
    candidates = std::move(split);
  }

  std::vector<FunctionalWildcardCube> result;
  result.reserve(candidates.size());
  for (const FunctionalWildcardCube &candidate : candidates) {
    FunctionalWildcardCube cube;
    cube.bitWidth = targetWidth;
    cube.signedness = targetSignedness;
    cube.aval.resize((uint64_t{targetWidth} + 63) / 64);
    cube.wildcard.resize(cube.aval.size());
    uint32_t copiedBits = std::min(source.bitWidth, targetWidth);
    for (uint32_t bit = 0; bit != copiedBits; ++bit) {
      setCubeBit(cube.aval, bit, cubeBit(candidate.aval, bit));
      setCubeBit(cube.wildcard, bit, cubeBit(candidate.wildcard, bit));
    }
    bool sign = false;
    if (sourceSigned)
      sign = cubeBit(candidate.aval, source.bitWidth - 1);
    for (uint32_t bit = source.bitWidth; bit < targetWidth; ++bit)
      setCubeBit(cube.aval, bit, sign);
    if (targetWidth % 64) {
      uint64_t mask = lowBitsMask(targetWidth % 64);
      cube.aval.back() &= mask;
      cube.wildcard.back() &= mask;
    }
    result.push_back(std::move(cube));
  }
  return result;
}

FunctionalInteger functionalPowerOfTwo(uint32_t exponent, bool negative) {
  FunctionalInteger result;
  result.negative = negative;
  result.magnitude.resize(exponent / 64 + 1);
  result.magnitude[exponent / 64] = uint64_t{1} << (exponent % 64);
  return result;
}

FunctionalInteger functionalMaximum(uint32_t bitWidth, bool isSigned) {
  uint32_t magnitudeBits = bitWidth - (isSigned ? 1 : 0);
  FunctionalInteger result;
  result.magnitude.resize((uint64_t{magnitudeBits} + 63) / 64, UINT64_MAX);
  if (magnitudeBits % 64)
    result.magnitude.back() = lowBitsMask(magnitudeBits % 64);
  normalizeMagnitude(result);
  return result;
}

bool functionalIntegerRepresentable(
    const FunctionalInteger &value, uint32_t bitWidth,
    obelisk::coverage::CoverageSignedness signedness) {
  bool isSigned = signedness == obelisk::coverage::CoverageSignedness::Signed;
  if (value.negative) {
    if (!isSigned)
      return false;
    FunctionalInteger minimum = functionalPowerOfTwo(bitWidth - 1, true);
    return compareFunctionalInteger(value, minimum) >= 0;
  }
  return compareFunctionalInteger(value,
                                  functionalMaximum(bitWidth, isSigned)) <= 0;
}

std::vector<uint64_t> encodeFunctionalInteger(const FunctionalInteger &value,
                                              uint32_t bitWidth) {
  uint32_t limbCount = (uint64_t{bitWidth} + 63) / 64;
  std::vector<uint64_t> result(limbCount);
  std::copy_n(value.magnitude.begin(),
              std::min<size_t>(value.magnitude.size(), result.size()),
              result.begin());
  if (value.negative) {
    uint64_t carry = 1;
    for (uint64_t &limb : result) {
      limb = ~limb;
      uint64_t previous = limb;
      limb += carry;
      carry = carry && limb < previous;
    }
  }
  if (bitWidth % 64)
    result.back() &= lowBitsMask(bitWidth % 64);
  return result;
}

bool functionalIntegerFromReal(double source, FunctionalInteger &result) {
  result = {};
  if (!std::isfinite(source))
    return false;
  const double rounded = std::round(source);
  // IEEE 1800-2023 19.5.7 drops a singleton when conversion changes its
  // value under normal equality. The rounding rule itself is from 6.12.1.
  if (rounded != source)
    return false;
  if (rounded == 0.0)
    return true;

  uint64_t encoding = 0;
  static_assert(sizeof(encoding) == sizeof(rounded));
  std::memcpy(&encoding, &rounded, sizeof(encoding));
  const uint64_t exponent = (encoding >> 52) & UINT64_C(0x7ff);
  if (!exponent || exponent == UINT64_C(0x7ff))
    return false;
  const int unbiasedExponent = static_cast<int>(exponent) - 1023;
  if (unbiasedExponent < 0)
    return false;
  uint64_t significand =
      (encoding & ((UINT64_C(1) << 52) - 1)) | (UINT64_C(1) << 52);
  const int shift = unbiasedExponent - 52;
  if (shift < 0)
    significand >>= static_cast<unsigned>(-shift);
  const uint32_t magnitudeBits = static_cast<uint32_t>(unbiasedExponent) + 1;
  result.magnitude.resize((uint64_t{magnitudeBits} + 63) / 64);
  if (shift >= 0) {
    const uint32_t limb = static_cast<uint32_t>(shift) / 64;
    const uint32_t offset = static_cast<uint32_t>(shift) % 64;
    result.magnitude[limb] |= significand << offset;
    if (offset && limb + 1 < result.magnitude.size())
      result.magnitude[limb + 1] |= significand >> (64 - offset);
  } else {
    result.magnitude.front() = significand;
  }
  result.negative = std::signbit(rounded);
  normalizeMagnitude(result);
  return true;
}

using FunctionalMagnitude = std::vector<uint64_t>;

void normalizeFunctionalMagnitude(FunctionalMagnitude &value) {
  while (!value.empty() && value.back() == 0)
    value.pop_back();
}

FunctionalMagnitude addFunctionalMagnitudes(const FunctionalMagnitude &left,
                                            const FunctionalMagnitude &right) {
  FunctionalMagnitude result(std::max(left.size(), right.size()));
  uint64_t carry = 0;
  for (size_t index = 0; index != result.size(); ++index) {
    uint64_t a = index < left.size() ? left[index] : 0;
    uint64_t b = index < right.size() ? right[index] : 0;
    uint64_t first = a + b;
    uint64_t firstCarry = first < a;
    uint64_t second = first + carry;
    uint64_t secondCarry = second < first;
    result[index] = second;
    carry = firstCarry | secondCarry;
  }
  if (carry)
    result.push_back(carry);
  return result;
}

bool multiplyFunctionalMagnitudes(const FunctionalMagnitude &left,
                                  const FunctionalMagnitude &right,
                                  FunctionalMagnitude &result) {
  result.clear();
  if (left.empty() || right.empty())
    return true;
  if (left.size() > SIZE_MAX - right.size())
    return false;
  result.assign(left.size() + right.size(), 0);
  for (size_t leftIndex = 0; leftIndex != left.size(); ++leftIndex) {
    uint64_t carry = 0;
    for (size_t rightIndex = 0; rightIndex != right.size(); ++rightIndex) {
      const size_t index = leftIndex + rightIndex;
      unsigned __int128 product =
          static_cast<unsigned __int128>(left[leftIndex]) * right[rightIndex] +
          result[index] + carry;
      result[index] = static_cast<uint64_t>(product);
      carry = static_cast<uint64_t>(product >> 64);
    }
    size_t index = leftIndex + right.size();
    while (carry) {
      unsigned __int128 sum =
          static_cast<unsigned __int128>(result[index]) + carry;
      result[index] = static_cast<uint64_t>(sum);
      carry = static_cast<uint64_t>(sum >> 64);
      ++index;
    }
  }
  normalizeFunctionalMagnitude(result);
  return true;
}

uint64_t saturatingFunctionalMagnitude(const FunctionalMagnitude &value) {
  return value.size() > 1 ? UINT64_MAX : value.empty() ? 0 : value.front();
}

long double floatingFunctionalMagnitude(const FunctionalMagnitude &value) {
  long double result = 0.0L;
  for (size_t index = value.size(); index != 0; --index)
    result =
        std::ldexp(result, 64) + static_cast<long double>(value[index - 1]);
  return result;
}

struct FunctionalCrossProfile {
  uint64_t atLeast = 1;
  uint64_t type = 0;
  obelisk::coverage::Digest configuration{};
  uint64_t cross = 0;
  uint64_t root = 0;
  uint32_t targetCount = 0;

  bool operator==(const FunctionalCrossProfile &other) const {
    return atLeast == other.atLeast && type == other.type &&
           configuration == other.configuration && cross == other.cross &&
           root == other.root && targetCount == other.targetCount;
  }
};

using FunctionalCrossNodeKey =
    std::tuple<uint64_t, obelisk::coverage::Digest, uint64_t, uint64_t>;
using FunctionalCrossBinKey =
    std::tuple<uint64_t, obelisk::coverage::Digest, uint64_t>;

/// Count the union of canonical automatic-cross graphs without enumerating
/// tuples. Each state identifies the profile nodes that still accept a prefix;
/// equal labels are merged and equal successor states carry a multiplicity.
bool countFunctionalCrossProfileUnion(
    const obelisk::coverage::Database &schema,
    const std::vector<FunctionalCrossProfile> &profiles,
    FunctionalMagnitude &result) {
  using namespace obelisk::coverage;
  result.clear();
  if (profiles.empty())
    return true;
  const uint32_t targetCount = profiles.front().targetCount;
  if (!targetCount)
    return false;
  for (const auto &profile : profiles)
    if (!profile.type || !profile.cross || !profile.root ||
        profile.targetCount != targetCount)
      return false;

  std::map<FunctionalCrossNodeKey, const ResolvedCrossAutomaticNode *> nodes;
  for (const auto &node : schema.resolvedCrossAutomaticNodes)
    nodes.emplace(FunctionalCrossNodeKey{node.type, node.configuration,
                                         node.cross, node.id},
                  &node);
  std::map<FunctionalCrossBinKey, std::string> binNames;
  for (const auto &bin : schema.resolvedFunctionalBins)
    binNames.emplace(FunctionalCrossBinKey{bin.type, bin.configuration, bin.id},
                     bin.name);

  using Active = std::pair<size_t, uint64_t>;
  using State = std::vector<Active>;
  std::map<State, FunctionalMagnitude> states;
  State initial;
  initial.reserve(profiles.size());
  for (size_t index = 0; index != profiles.size(); ++index)
    initial.emplace_back(index, profiles[index].root);
  states.emplace(std::move(initial), FunctionalMagnitude{1});
  for (uint32_t target = 0; target != targetCount; ++target) {
    std::map<State, FunctionalMagnitude> successors;
    for (const auto &[active, prefixes] : states) {
      std::map<std::string, State> labels;
      for (const auto &[profileIndex, nodeID] : active) {
        const FunctionalCrossProfile &profile = profiles[profileIndex];
        auto node = nodes.find(
            {profile.type, profile.configuration, profile.cross, nodeID});
        if (node == nodes.end() || node->second->targetOrdinal != target ||
            uint64_t{node->second->firstEdge} + node->second->edgeCount >
                schema.resolvedCrossAutomaticEdges.size())
          return false;
        for (uint32_t ordinal = 0; ordinal != node->second->edgeCount;
             ++ordinal) {
          const ResolvedCrossAutomaticEdge &edge =
              schema.resolvedCrossAutomaticEdges[node->second->firstEdge +
                                                 ordinal];
          auto name =
              binNames.find({profile.type, profile.configuration, edge.bin});
          const bool finalTarget = target + 1 == targetCount;
          if (name == binNames.end() || edge.node != nodeID ||
              (finalTarget != (edge.child == 0)))
            return false;
          labels[name->second].emplace_back(profileIndex, edge.child);
        }
      }
      std::map<State, uint64_t> multiplicities;
      for (auto &[name, next] : labels) {
        (void)name;
        std::sort(next.begin(), next.end());
        next.erase(std::unique(next.begin(), next.end()), next.end());
        uint64_t &multiplicity = multiplicities[next];
        if (multiplicity == UINT64_MAX)
          return false;
        ++multiplicity;
      }
      for (const auto &[next, multiplicity] : multiplicities) {
        FunctionalMagnitude contribution;
        if (!multiplyFunctionalMagnitudes(
                prefixes, FunctionalMagnitude{multiplicity}, contribution))
          return false;
        FunctionalMagnitude &count = successors[next];
        count = addFunctionalMagnitudes(count, contribution);
      }
    }
    states = std::move(successors);
  }
  for (const auto &[state, count] : states) {
    (void)state;
    result = addFunctionalMagnitudes(result, count);
  }
  return true;
}

bool functionalCrossProfileContains(
    const obelisk::coverage::Database &schema,
    const FunctionalCrossProfile &profile,
    const std::vector<std::string> &components) {
  using namespace obelisk::coverage;
  if (!profile.root || profile.targetCount != components.size())
    return false;
  uint64_t nodeID = profile.root;
  for (uint32_t target = 0; target != components.size(); ++target) {
    auto node = std::find_if(schema.resolvedCrossAutomaticNodes.begin(),
                             schema.resolvedCrossAutomaticNodes.end(),
                             [&](const ResolvedCrossAutomaticNode &candidate) {
                               return candidate.type == profile.type &&
                                      candidate.configuration ==
                                          profile.configuration &&
                                      candidate.cross == profile.cross &&
                                      candidate.id == nodeID;
                             });
    if (node == schema.resolvedCrossAutomaticNodes.end() ||
        node->targetOrdinal != target ||
        uint64_t{node->firstEdge} + node->edgeCount >
            schema.resolvedCrossAutomaticEdges.size())
      return false;
    const ResolvedCrossAutomaticEdge *selected = nullptr;
    for (uint32_t ordinal = 0; ordinal != node->edgeCount; ++ordinal) {
      const ResolvedCrossAutomaticEdge &edge =
          schema.resolvedCrossAutomaticEdges[node->firstEdge + ordinal];
      auto bin = std::find_if(schema.resolvedFunctionalBins.begin(),
                              schema.resolvedFunctionalBins.end(),
                              [&](const ResolvedFunctionalBin &candidate) {
                                return candidate.type == profile.type &&
                                       candidate.configuration ==
                                           profile.configuration &&
                                       candidate.id == edge.bin;
                              });
      if (bin != schema.resolvedFunctionalBins.end() &&
          bin->name == components[target]) {
        if (selected)
          return false;
        selected = &edge;
      }
    }
    if (!selected)
      return false;
    const bool finalTarget = target + 1 == components.size();
    if (finalTarget != (selected->child == 0))
      return false;
    nodeID = selected->child;
  }
  return true;
}

FunctionalMagnitude
subtractFunctionalMagnitudes(const FunctionalMagnitude &left,
                             const FunctionalMagnitude &right) {
  FunctionalMagnitude result = left;
  uint64_t borrow = 0;
  for (size_t index = 0; index != result.size(); ++index) {
    uint64_t subtrahend = index < right.size() ? right[index] : 0;
    uint64_t withBorrow = subtrahend + borrow;
    uint64_t carry = withBorrow < subtrahend;
    uint64_t previous = result[index];
    result[index] -= withBorrow;
    borrow = carry | (previous < withBorrow);
  }
  normalizeFunctionalMagnitude(result);
  return result;
}

FunctionalMagnitude divideFunctionalMagnitude(const FunctionalMagnitude &value,
                                              uint32_t divisor,
                                              uint32_t &remainder) {
  FunctionalMagnitude quotient(value.size());
  uint64_t carried = 0;
  for (size_t reverse = value.size(); reverse != 0; --reverse) {
    size_t index = reverse - 1;
    // Divide one 64-bit limb a bit at a time. This remains portable C++17 and
    // avoids a compiler-specific 128-bit integer in the runtime ABI library.
    uint64_t limbQuotient = 0;
    for (uint32_t bit = 64; bit != 0; --bit) {
      carried = (carried << 1) | ((value[index] >> (bit - 1)) & 1);
      if (carried >= divisor) {
        carried -= divisor;
        limbQuotient |= uint64_t{1} << (bit - 1);
      }
    }
    quotient[index] = limbQuotient;
  }
  normalizeFunctionalMagnitude(quotient);
  remainder = static_cast<uint32_t>(carried);
  return quotient;
}

bool functionalMagnitudeFitsU32(const FunctionalMagnitude &value) {
  return value.size() <= 1 && (value.empty() || value.front() <= UINT32_MAX);
}

uint32_t functionalMagnitudeAsU32(const FunctionalMagnitude &value) {
  return value.empty() ? 0 : static_cast<uint32_t>(value.front());
}

bool functionalMagnitudeBit(const FunctionalMagnitude &value, uint32_t index) {
  return index / 64 < value.size() && ((value[index / 64] >> (index % 64)) & 1);
}

uint32_t functionalMagnitudeBitWidth(const FunctionalMagnitude &value) {
  if (value.empty())
    return 0;
  uint64_t top = value.back();
  uint32_t bits = 0;
  while (top) {
    ++bits;
    top >>= 1;
  }
  return static_cast<uint32_t>((value.size() - 1) * 64 + bits);
}

uint32_t functionalMagnitudeTrailingZeros(const FunctionalMagnitude &value) {
  for (size_t index = 0; index != value.size(); ++index) {
    uint64_t limb = value[index];
    if (!limb)
      continue;
    uint32_t count = 0;
    while (!(limb & 1)) {
      ++count;
      limb >>= 1;
    }
    return static_cast<uint32_t>(index * 64 + count);
  }
  return UINT32_MAX;
}

FunctionalMagnitude functionalPowerOfTwoMagnitude(uint32_t exponent) {
  FunctionalMagnitude result(exponent / 64 + 1);
  result[exponent / 64] = uint64_t{1} << (exponent % 64);
  return result;
}

std::string functionalMagnitudeDecimal(FunctionalMagnitude value) {
  if (value.empty())
    return "0";
  std::string digits;
  while (!value.empty()) {
    uint32_t remainder = 0;
    value = divideFunctionalMagnitude(value, 10, remainder);
    digits.push_back(static_cast<char>('0' + remainder));
  }
  std::reverse(digits.begin(), digits.end());
  return digits;
}

void toggleFunctionalSignBias(FunctionalMagnitude &value, uint32_t bitWidth) {
  value[(bitWidth - 1) / 64] ^= uint64_t{1} << ((bitWidth - 1) % 64);
}

struct IntegralCoverageShape {
  bool interval = false;
  std::vector<uint64_t> low;
  std::vector<uint64_t> high;
  std::vector<uint64_t> value;
  std::vector<uint64_t> wildcard;
};

bool makeIntegralCoverageShape(
    const obelisk::coverage::Database &schema,
    const obelisk::coverage::ResolvedFunctionalValueAtom &atom,
    uint32_t bitWidth, obelisk::coverage::CoverageSignedness signedness,
    IntegralCoverageShape &shape);

bool coverageShapeIntersectsPrefix(const IntegralCoverageShape &shape,
                                   const std::vector<uint64_t> &prefix,
                                   int64_t remainingBit, uint32_t bitWidth);
bool coverageShapeContainsPrefix(const IntegralCoverageShape &shape,
                                 const std::vector<uint64_t> &prefix,
                                 int64_t remainingBit, uint32_t bitWidth);
int compareIntegralBitVectors(const std::vector<uint64_t> &left,
                              const std::vector<uint64_t> &right);

/// Enumerate the canonical union of signed-biased integral shapes.  Prefix
/// blocks make the cardinality check exact before any individual values are
/// materialized, including for overlapping wildcard cubes.
obelisk_rt_status
enumerateIntegralShapeUnion(const std::vector<IntegralCoverageShape> &shapes,
                            uint32_t bitWidth,
                            std::vector<FunctionalMagnitude> &values) {
  struct Block {
    FunctionalMagnitude prefix;
    int64_t remainingBit = -1;
  };
  struct Frame {
    FunctionalMagnitude prefix;
    int64_t remainingBit = -1;
    std::vector<size_t> candidates;
  };

  values.clear();
  if (shapes.empty())
    return OBELISK_RT_OK;
  const uint64_t limit = obelisk::coverage::ParseLimits{}.maxRecords;
  const uint32_t limbCount = functionalLimbCount(bitWidth);
  Frame root;
  root.prefix.resize(limbCount);
  root.remainingBit = static_cast<int64_t>(bitWidth) - 1;
  root.candidates.resize(shapes.size());
  std::iota(root.candidates.begin(), root.candidates.end(), size_t{0});

  std::vector<Block> blocks;
  std::vector<Frame> search;
  search.push_back(std::move(root));
  FunctionalMagnitude cardinality;
  uint64_t visited = 0;
  while (!search.empty()) {
    if (visited++ == limit)
      return OBELISK_RT_OUT_OF_RESOURCES;
    Frame frame = std::move(search.back());
    search.pop_back();
    std::vector<size_t> intersecting;
    intersecting.reserve(frame.candidates.size());
    bool contained = false;
    for (size_t index : frame.candidates) {
      const IntegralCoverageShape &shape = shapes[index];
      if (!coverageShapeIntersectsPrefix(shape, frame.prefix,
                                         frame.remainingBit, bitWidth))
        continue;
      if (coverageShapeContainsPrefix(shape, frame.prefix, frame.remainingBit,
                                      bitWidth)) {
        contained = true;
        break;
      }
      intersecting.push_back(index);
    }
    if (!contained && intersecting.empty())
      continue;
    if (contained || frame.remainingBit < 0) {
      FunctionalMagnitude blockCardinality =
          frame.remainingBit < 0
              ? FunctionalMagnitude{1}
              : functionalPowerOfTwoMagnitude(
                    static_cast<uint32_t>(frame.remainingBit) + 1);
      cardinality = addFunctionalMagnitudes(cardinality, blockCardinality);
      if (!functionalMagnitudeFitsU32(cardinality) ||
          functionalMagnitudeAsU32(cardinality) > limit ||
          blocks.size() == limit)
        return OBELISK_RT_OUT_OF_RESOURCES;
      blocks.push_back({std::move(frame.prefix), frame.remainingBit});
      continue;
    }
    if (search.size() > limit - 2)
      return OBELISK_RT_OUT_OF_RESOURCES;
    Frame one{frame.prefix, frame.remainingBit - 1, intersecting};
    setCubeBit(one.prefix, static_cast<uint32_t>(frame.remainingBit), true);
    search.push_back(std::move(one));
    search.push_back({std::move(frame.prefix), frame.remainingBit - 1,
                      std::move(intersecting)});
  }

  values.reserve(functionalMagnitudeAsU32(cardinality));
  for (const Block &block : blocks) {
    FunctionalMagnitude blockCardinality =
        block.remainingBit < 0
            ? FunctionalMagnitude{1}
            : functionalPowerOfTwoMagnitude(
                  static_cast<uint32_t>(block.remainingBit) + 1);
    const uint32_t count = functionalMagnitudeAsU32(blockCardinality);
    for (uint32_t offset = 0; offset != count; ++offset) {
      FunctionalMagnitude value =
          addFunctionalMagnitudes(block.prefix, FunctionalMagnitude{offset});
      value.resize(limbCount);
      values.push_back(std::move(value));
    }
  }
  return OBELISK_RT_OK;
}

struct StateCoverageInterval {
  // Most-significant limb first, so the radix key and numeric order agree.
  std::vector<uint64_t> low;
  std::vector<uint64_t> high;
  size_t bin = SIZE_MAX;
  bool empty = false;
};

bool advanceStateCoverageEndpoint(std::vector<uint64_t> &endpoint,
                                  bool increment) {
  for (size_t reverse = endpoint.size(); reverse != 0; --reverse) {
    uint64_t &limb = endpoint[reverse - 1];
    if (increment ? ++limb != 0 : limb-- != 0)
      return true;
  }
  return false;
}

template <typename Real>
bool makeRealStateCoverageInterval(
    const obelisk::coverage::ResolvedFunctionalValueAtom &atom,
    StateCoverageInterval &interval) {
  using namespace obelisk::coverage;
  double sourceLow, sourceHigh;
  std::memcpy(&sourceLow, &atom.realLowBits, sizeof(sourceLow));
  std::memcpy(&sourceHigh, &atom.realHighBits, sizeof(sourceHigh));
  bool lowerUnbounded = atom.flags & FunctionalValueAtomLowerUnbounded;
  bool upperUnbounded = atom.flags & FunctionalValueAtomUpperUnbounded;
  Real low = lowerUnbounded ? -std::numeric_limits<Real>::infinity()
                            : static_cast<Real>(sourceLow);
  Real high = upperUnbounded ? std::numeric_limits<Real>::infinity()
                             : static_cast<Real>(sourceHigh);
  if (std::isnan(low) || std::isnan(high))
    return false;
  if (!lowerUnbounded &&
      (low < sourceLow ||
       (low == sourceLow && !(atom.flags & FunctionalValueAtomLowerInclusive))))
    low = std::nextafter(low, std::numeric_limits<Real>::infinity());
  if (!upperUnbounded && (high > sourceHigh ||
                          (high == sourceHigh &&
                           !(atom.flags & FunctionalValueAtomUpperInclusive))))
    high = std::nextafter(high, -std::numeric_limits<Real>::infinity());
  interval.empty = low > high;
  auto rank = [](Real value) -> uint64_t {
    using Bits = std::conditional_t<sizeof(Real) == sizeof(uint32_t), uint32_t,
                                    uint64_t>;
    Bits bits;
    std::memcpy(&bits, &value, sizeof(bits));
    constexpr Bits sign = Bits{1} << (sizeof(Bits) * 8 - 1);
    // Collapse -0 and +0 into one rank. Adjacent representable values then
    // have adjacent ranks, including at zero and at the infinity boundaries.
    return bits & sign ? static_cast<Bits>(~bits)
                       : static_cast<Bits>((bits | sign) - 1);
  };
  interval.low = {rank(low)};
  interval.high = {rank(high)};
  return true;
}

bool makeStateCoverageInterval(
    const obelisk::coverage::Database &schema,
    const obelisk::coverage::ResolvedFunctionalValueSet &set,
    const obelisk::coverage::ResolvedFunctionalValueAtom &atom, bool shortReal,
    StateCoverageInterval &interval) {
  using namespace obelisk::coverage;
  if (set.kind == FunctionalValueSetKind::Real)
    return shortReal ? makeRealStateCoverageInterval<float>(atom, interval)
                     : makeRealStateCoverageInterval<double>(atom, interval);
  IntegralCoverageShape shape;
  if (!makeIntegralCoverageShape(schema, atom, set.bitWidth, set.signedness,
                                 shape))
    return false;
  if (shape.interval) {
    interval.low.assign(shape.low.rbegin(), shape.low.rend());
    interval.high.assign(shape.high.rbegin(), shape.high.rend());
    if (!(atom.flags & FunctionalValueAtomLowerInclusive) &&
        !advanceStateCoverageEndpoint(interval.low, true))
      interval.empty = true;
    if (!(atom.flags & FunctionalValueAtomUpperInclusive) &&
        !advanceStateCoverageEndpoint(interval.high, false))
      interval.empty = true;
  } else {
    // A prefix cube is one interval. Other cubes retain symbolic subtraction.
    bool fixed = false;
    for (size_t ordinal = 0; ordinal != shape.wildcard.size(); ++ordinal) {
      uint64_t wildcard = shape.wildcard[ordinal];
      uint64_t valid = ordinal + 1 == shape.wildcard.size() && set.bitWidth % 64
                           ? lowBitsMask(set.bitWidth % 64)
                           : UINT64_MAX;
      if ((fixed && wildcard) || (wildcard & (wildcard + 1)))
        return false;
      fixed |= wildcard != valid;
    }
    interval.low.assign(shape.value.rbegin(), shape.value.rend());
    interval.high = interval.low;
    for (size_t ordinal = 0; ordinal != shape.wildcard.size(); ++ordinal)
      interval.high[shape.wildcard.size() - ordinal - 1] |=
          shape.wildcard[ordinal];
  }
  interval.empty |= interval.high < interval.low;
  return true;
}

void mergeStateCoverageIntervals(
    std::vector<StateCoverageInterval> &intervals) {
  obelisk::coverage::detail::sortByKey(
      intervals.begin(), intervals.end(),
      [](const auto &interval) -> const auto & { return interval.low; });
  std::vector<StateCoverageInterval> merged;
  merged.reserve(intervals.size());
  for (auto &interval : intervals) {
    if (interval.empty)
      continue;
    bool adjacent = false;
    if (!merged.empty()) {
      adjacent = interval.low <= merged.back().high;
      if (!adjacent) {
        auto successor = merged.back().high;
        adjacent = advanceStateCoverageEndpoint(successor, true) &&
                   interval.low <= successor;
      }
    }
    if (adjacent) {
      if (merged.back().high < interval.high)
        merged.back().high = std::move(interval.high);
    } else
      merged.push_back(std::move(interval));
  }
  intervals = std::move(merged);
}

/// All references use ordinals: constructor staging may append schema rows.
/// Numeric selectors share a canonical exclusion union and endpoint indexes.
/// General cubes share decision graphs, grouped by their exact B plane so
/// wildcard 0/1 expansion cannot accidentally consume constrained X/Z values.
struct FunctionalCoverageSetQueryIndex {
  using Set = obelisk::coverage::ResolvedFunctionalValueSet;
  using Bin = obelisk::coverage::ResolvedFunctionalBin;
  using Graph = obelisk::coverage::detail::DecisionIndex;
  using Root = Graph::Root;
  using DomainKey = std::tuple<uint32_t, uint32_t, uint32_t, bool>;
  struct CompiledSet {
    DomainKey domain;
    Graph *graph = nullptr;
    std::vector<uint64_t> sources;
    bool numeric = true;
    bool prepared = false;
    std::vector<StateCoverageInterval> intervals;
    IndexMap<std::vector<uint64_t>, size_t> endpoints;
    IndexMap<std::vector<uint64_t>, Root> groups;
    const StateCoverageInterval *
    containing(const std::vector<uint64_t> &value) const {
      auto position = endpoints.upper_bound(value);
      if (position == endpoints.begin())
        return nullptr;
      const auto &interval = intervals[(--position)->second];
      return value <= interval.high ? &interval : nullptr;
    }
    void indexEndpoints() {
      for (size_t ordinal = 0; ordinal != intervals.size(); ++ordinal)
        endpoints.emplace(intervals[ordinal].low, ordinal);
    }
  };

  const obelisk::coverage::Database &schema;
  uint64_t type;
  obelisk::coverage::Digest configuration;
  obelisk_rt_status status = OBELISK_RT_OK;
  IndexMap<uint64_t, size_t> bins, sets, items, templateItems, plans, selectors,
      crossPlans, tupleSets;
  IndexMap<std::pair<uint64_t, uint64_t>, size_t> bindings, targetOrdinals;
  IndexMap<uint64_t, std::vector<size_t>> alternatives;
  IndexMap<uint64_t, std::vector<const obelisk::coverage::CrossPredicateNode *>>
      predicatePrograms;
  IndexMap<std::pair<uint64_t, uint32_t>, const Set *> predicateValues;
  IndexMap<uint64_t, FunctionalMagnitude> wideMatches;
  IndexSet<uint64_t> indexTargets;
  FunctionalMagnitude
  matchCount(const obelisk::coverage::ResolvedCrossSelectorBinding &binding) {
    if (!(binding.flags & obelisk::coverage::ResolvedCrossSelectorWideMatches))
      return binding.matchesCount ? FunctionalMagnitude{binding.matchesCount}
                                  : FunctionalMagnitude{};
    auto value = wideMatches.find(binding.node);
    if (value == wideMatches.end()) {
      status = OBELISK_RT_INVALID_DESIGN;
      return {};
    }
    return value->second;
  }
  IndexMap<uint64_t, std::vector<uint64_t>> exclusionSets;
  IndexSet<uint64_t> shortRealItems, transitionValueItems;
  IndexMap<DomainKey, std::unique_ptr<Graph>> graphs;
  IndexMap<std::pair<bool, std::vector<uint64_t>>, std::shared_ptr<CompiledSet>>
      compiled;
  IndexMap<uint64_t, std::shared_ptr<CompiledSet>> binSets, masks;
  IndexMap<std::pair<DomainKey, Root>, std::shared_ptr<CompiledSet>>
      transitionSets;
  IndexMap<std::tuple<uint64_t, uint64_t, bool>, std::shared_ptr<CompiledSet>>
      selections;

  FunctionalCoverageSetQueryIndex(
      const obelisk::coverage::Database &schema, uint64_t type,
      const obelisk::coverage::Digest &configuration)
      : schema(schema), type(type), configuration(configuration) {
    using namespace obelisk::coverage;
    auto indexRows = [&](const auto &rows, auto &index) {
      for (size_t ordinal = 0; ordinal != rows.size(); ++ordinal)
        if (rows[ordinal].type == type &&
            rows[ordinal].configuration == configuration)
          index.emplace(rows[ordinal].id, ordinal);
    };
    indexRows(schema.resolvedFunctionalBins, bins);
    indexRows(schema.resolvedFunctionalValueSets, sets);
    indexRows(schema.resolvedFunctionalItems, items);
    indexRows(schema.resolvedFunctionalTupleSets, tupleSets);
    IndexMap<uint64_t, const CrossPredicateNode *> predicateNodes;
    std::vector<uint64_t> pendingIndices;
    for (const auto &node : schema.crossPredicateNodes) {
      predicatePrograms[node.selector].push_back(&node);
      predicateNodes.emplace(node.id, &node);
      if (node.kind == CrossPredicateKind::ElementSelect)
        pendingIndices.push_back(node.right);
    }
    // Index operands precede data operands in the variable order. Otherwise
    // an ordinary variable bit-select can require an exponential Boolean
    // graph. Visit each index-subtree node once, including shared operands.
    IndexSet<uint64_t> visitedIndices;
    while (!pendingIndices.empty()) {
      uint64_t id = pendingIndices.back();
      pendingIndices.pop_back();
      if (!visitedIndices.insert(id).second)
        continue;
      auto found = predicateNodes.find(id);
      if (found == predicateNodes.end())
        continue;
      const auto &node = *found->second;
      if (node.kind == CrossPredicateKind::Target)
        indexTargets.insert(node.target);
      if (node.left)
        pendingIndices.push_back(node.left);
      if (node.right)
        pendingIndices.push_back(node.right);
    }
    for (const auto &[id, ordinal] : sets) {
      const auto &set = schema.resolvedFunctionalValueSets[ordinal];
      if (set.role == ResolvedFunctionalValueSetRole::PredicateValue)
        predicateValues.emplace(
            std::make_pair(set.ownerSelector, set.ownerOrdinal), &set);
      if (set.role == ResolvedFunctionalValueSetRole::MatchesCount) {
        const auto &atom = schema.resolvedFunctionalValueAtoms[set.firstAtom];
        FunctionalMagnitude count;
        for (uint32_t limb = 0; limb != atom.limbCount; ++limb)
          count.push_back(
              schema.resolvedFunctionalValueLimbs[atom.firstLimb + limb]
                  .lowAval);
        normalizeFunctionalMagnitude(count);
        wideMatches.emplace(set.ownerSelector, std::move(count));
      }
    }
    for (const auto &[id, ordinal] : items)
      templateItems.emplace(
          schema.resolvedFunctionalItems[ordinal].templateItem, ordinal);
    for (size_t ordinal = 0;
         ordinal != schema.resolvedFunctionalBinPlans.size(); ++ordinal) {
      const auto &plan = schema.resolvedFunctionalBinPlans[ordinal];
      if (plan.type == type && plan.configuration == configuration)
        plans.emplace(plan.bin, ordinal);
    }
    for (size_t ordinal = 0; ordinal != schema.crossSelectorNodes.size();
         ++ordinal)
      selectors.emplace(schema.crossSelectorNodes[ordinal].id, ordinal);
    for (size_t ordinal = 0; ordinal != schema.crossPlans.size(); ++ordinal)
      crossPlans.emplace(schema.crossPlans[ordinal].item, ordinal);
    for (const auto &target : schema.crossTargets) {
      targetOrdinals.emplace(std::make_pair(target.cross, target.target),
                             target.ordinal);
      auto item = templateItems.find(target.target);
      if (item != templateItems.end())
        transitionValueItems.insert(
            schema.resolvedFunctionalItems[item->second].id);
    }
    for (size_t ordinal = 0;
         ordinal != schema.resolvedCrossSelectorBindings.size(); ++ordinal) {
      const auto &binding = schema.resolvedCrossSelectorBindings[ordinal];
      if (binding.type == type && binding.configuration == configuration)
        bindings.emplace(std::make_pair(binding.cross, binding.node), ordinal);
    }
    for (size_t ordinal = 0;
         ordinal != schema.resolvedTransitionAlternatives.size(); ++ordinal) {
      const auto &alternative = schema.resolvedTransitionAlternatives[ordinal];
      if (alternative.type == type &&
          alternative.configuration == configuration)
        alternatives[alternative.bin].push_back(ordinal);
    }
    IndexSet<uint64_t> shortTemplates;
    for (const auto &expression : schema.functionalExpressions)
      if (expression.role == FunctionalExpressionRole::CoverpointSample &&
          expression.resultKind == FunctionalExpressionResultKind::ShortReal)
        shortTemplates.insert(expression.owner);
    for (const auto &[id, ordinal] : items)
      if (shortTemplates.count(
              schema.resolvedFunctionalItems[ordinal].templateItem))
        shortRealItems.insert(id);
    for (const auto &[id, ordinal] : bins) {
      const auto &bin = schema.resolvedFunctionalBins[ordinal];
      if (bin.kind != FunctionalBinKind::State ||
          !(bin.flags & (FunctionalBinIgnore | FunctionalBinIllegal)) ||
          (bin.flags & FunctionalBinEmpty))
        continue;
      auto plan = plans.find(id);
      if (plan != plans.end())
        exclusionSets[bin.item].push_back(
            schema.resolvedFunctionalBinPlans[plan->second].valueSet);
    }
  }

  template <typename Record>
  static const Record *lookup(const IndexMap<uint64_t, size_t> &index,
                              const std::vector<Record> &rows, uint64_t id) {
    auto found = index.find(id);
    return found == index.end() ? nullptr : &rows[found->second];
  }
  const Bin *bin(uint64_t id) const {
    return lookup(bins, schema.resolvedFunctionalBins, id);
  }
  const Set *valueSet(uint64_t id) const {
    return lookup(sets, schema.resolvedFunctionalValueSets, id);
  }
  const obelisk::coverage::ResolvedCrossSelectorBinding *
  binding(uint64_t cross, uint64_t node) const {
    auto found = bindings.find({cross, node});
    return found == bindings.end()
               ? nullptr
               : &schema.resolvedCrossSelectorBindings[found->second];
  }

  Graph &graphFor(const DomainKey &domain) {
    auto found = graphs.find(domain);
    if (found == graphs.end()) {
      bool real = std::get<0>(domain) ==
                  static_cast<uint32_t>(
                      obelisk::coverage::FunctionalValueSetKind::Real);
      uint32_t width =
          real ? (std::get<3>(domain) ? 32 : 64) : std::get<1>(domain);
      found = graphs.emplace(domain, std::make_unique<Graph>(width)).first;
    }
    return *found->second;
  }

  bool needsTransitionValues(uint64_t item) const {
    return transitionValueItems.count(item);
  }

  void setTransitionValues(uint64_t bin, uint32_t width,
                           obelisk::coverage::CoverageSignedness signedness,
                           Root root) {
    DomainKey domain = {
        static_cast<uint32_t>(
            obelisk::coverage::FunctionalValueSetKind::Integral),
        width, static_cast<uint32_t>(signedness), false};
    auto key = std::make_pair(domain, root);
    auto found = transitionSets.find(key);
    if (found == transitionSets.end()) {
      auto set = std::make_shared<CompiledSet>();
      set->domain = domain;
      set->graph = &graphFor(domain);
      set->numeric = false;
      set->prepared = true;
      if (root)
        set->groups.emplace(
            std::vector<uint64_t>(functionalLimbCount(width), 0), root);
      found = transitionSets.emplace(key, std::move(set)).first;
    }
    binSets[bin] = found->second;
  }

  std::shared_ptr<CompiledSet> compile(std::vector<uint64_t> sources,
                                       bool shortReal) {
    using namespace obelisk::coverage;
    detail::sortByKey(sources.begin(), sources.end(),
                      [](uint64_t id) { return id; });
    sources.erase(std::unique(sources.begin(), sources.end()), sources.end());
    auto key = std::make_pair(shortReal, sources);
    auto found = compiled.find(key);
    if (found != compiled.end())
      return found->second;
    auto result = std::make_shared<CompiledSet>();
    result->sources = std::move(sources);
    for (uint64_t id : result->sources) {
      const Set *set = valueSet(id);
      if (!set) {
        status = OBELISK_RT_INVALID_DESIGN;
        return nullptr;
      }
      DomainKey domain = {static_cast<uint32_t>(set->kind), set->bitWidth,
                          static_cast<uint32_t>(set->signedness), shortReal};
      if (!result->graph) {
        result->domain = domain;
        result->graph = &graphFor(domain);
      } else if (domain != result->domain) {
        status = OBELISK_RT_INVALID_DESIGN;
        return nullptr;
      }
      if (!result->numeric)
        continue;
      for (uint32_t ordinal = 0; ordinal != set->atomCount; ++ordinal) {
        StateCoverageInterval interval;
        if (!makeStateCoverageInterval(
                schema, *set,
                schema.resolvedFunctionalValueAtoms[set->firstAtom + ordinal],
                shortReal, interval)) {
          result->numeric = false;
          result->intervals.clear();
          break;
        }
        result->intervals.push_back(std::move(interval));
      }
    }
    if (result->numeric) {
      mergeStateCoverageIntervals(result->intervals);
      result->indexEndpoints();
    }
    compiled.emplace(std::move(key), result);
    return result;
  }

  bool prepare(CompiledSet &set) {
    using namespace obelisk::coverage;
    if (set.prepared)
      return status == OBELISK_RT_OK;
    set.prepared = true;
    if (!set.graph)
      return true;
    Graph &graph = *set.graph;
    bool real = std::get<0>(set.domain) ==
                static_cast<uint32_t>(FunctionalValueSetKind::Real);
    uint32_t width = std::get<1>(set.domain);
    bool shortReal = std::get<3>(set.domain);
    std::vector<uint64_t> known(real ? 0 : functionalLimbCount(width));
    auto add = [&](const std::vector<uint64_t> &bval, Root root) {
      auto &group = set.groups[bval];
      group = graph.unite(group, root);
    };
    if (set.numeric) {
      for (const auto &interval : set.intervals)
        add(known, graph.interval(interval.low, interval.high));
    } else {
      for (uint64_t id : set.sources) {
        const auto &source = *valueSet(id);
        for (uint32_t ordinal = 0; ordinal != source.atomCount; ++ordinal) {
          const auto &atom =
              schema.resolvedFunctionalValueAtoms[source.firstAtom + ordinal];
          StateCoverageInterval interval;
          if (makeStateCoverageInterval(schema, source, atom, shortReal,
                                        interval)) {
            if (!interval.empty)
              add(known, graph.interval(interval.low, interval.high));
            continue;
          }
          if (atom.kind != FunctionalValueAtomKind::IntegralValue) {
            status = OBELISK_RT_INVALID_DESIGN;
            return false;
          }
          std::vector<uint64_t> aval(atom.limbCount), bval(atom.limbCount),
              wildcard(atom.limbCount);
          for (uint32_t limbOrdinal = 0; limbOrdinal != atom.limbCount;
               ++limbOrdinal) {
            const auto &limb =
                schema
                    .resolvedFunctionalValueLimbs[atom.firstLimb + limbOrdinal];
            uint64_t valid = limbOrdinal + 1 == atom.limbCount && width % 64
                                 ? lowBitsMask(width % 64)
                                 : UINT64_MAX;
            wildcard[limbOrdinal] = limb.wildcardMask & valid;
            aval[limbOrdinal] = limb.lowAval & ~wildcard[limbOrdinal] & valid;
            bval[limbOrdinal] = limb.lowBval & ~wildcard[limbOrdinal] & valid;
          }
          if (source.signedness == CoverageSignedness::Signed) {
            uint64_t sign = uint64_t{1} << ((width - 1) % 64);
            aval[(width - 1) / 64] ^= sign & ~wildcard[(width - 1) / 64];
          }
          add(bval, graph.cube(aval, wildcard));
        }
      }
    }
    if (graph.exhausted())
      status = OBELISK_RT_OUT_OF_RESOURCES;
    return status == OBELISK_RT_OK;
  }

  std::shared_ptr<CompiledSet>
  subject(const Bin &bin,
          const std::unordered_set<uint64_t> &suppressedAlternatives) {
    using namespace obelisk::coverage;
    auto cached = binSets.find(bin.id);
    if (cached != binSets.end())
      return cached->second;
    std::vector<uint64_t> sources;
    if (bin.kind == FunctionalBinKind::State) {
      auto plan = plans.find(bin.id);
      if (plan == plans.end()) {
        status = OBELISK_RT_INVALID_DESIGN;
        return nullptr;
      }
      sources.push_back(
          schema.resolvedFunctionalBinPlans[plan->second].valueSet);
    } else if (bin.kind == FunctionalBinKind::Transition) {
      auto found = alternatives.find(bin.id);
      if (found == alternatives.end()) {
        status = OBELISK_RT_INVALID_DESIGN;
        return nullptr;
      }
      for (size_t ordinal : found->second) {
        const auto &alternative =
            schema.resolvedTransitionAlternatives[ordinal];
        if (suppressedAlternatives.count(alternative.id))
          continue;
        if (!alternative.stepCount ||
            uint64_t{alternative.firstStep} + alternative.stepCount >
                schema.resolvedTransitionSteps.size()) {
          status = OBELISK_RT_INVALID_DESIGN;
          return nullptr;
        }
        const auto &terminal =
            schema.resolvedTransitionSteps[alternative.firstStep +
                                           alternative.stepCount - 1];
        if (terminal.type != type || terminal.configuration != configuration ||
            terminal.bin != bin.id || terminal.alternative != alternative.id ||
            terminal.ordinal != alternative.stepCount - 1) {
          status = OBELISK_RT_INVALID_DESIGN;
          return nullptr;
        }
        sources.push_back(terminal.valueSet);
      }
    } else {
      status = OBELISK_RT_INVALID_DESIGN;
      return nullptr;
    }
    auto result = compile(std::move(sources), shortRealItems.count(bin.item));
    binSets.emplace(bin.id, result);
    return result;
  }

  std::shared_ptr<CompiledSet> mask(const Bin &bin) {
    using namespace obelisk::coverage;
    if (bin.kind != FunctionalBinKind::State)
      return nullptr; // State exclusions do not remove transition values.
    auto found = exclusionSets.find(bin.item);
    if (found == exclusionSets.end())
      return nullptr;
    auto cached = masks.find(bin.item);
    if (cached != masks.end())
      return cached->second;
    auto result = compile(found->second, shortRealItems.count(bin.item));
    masks.emplace(bin.item, result);
    return result;
  }

  bool intersects(uint64_t binID, uint64_t selectionID,
                  const std::unordered_set<uint64_t> &suppressedAlternatives) {
    const Bin *definition = bin(binID);
    if (!definition) {
      status = OBELISK_RT_INVALID_DESIGN;
      return false;
    }
    auto left = subject(*definition, suppressedAlternatives);
    auto right = compile({selectionID}, shortRealItems.count(definition->item));
    auto excluded = mask(*definition);
    if (left && excluded && left->domain != excluded->domain)
      status = OBELISK_RT_INVALID_DESIGN;
    if (!left || !right || status != OBELISK_RT_OK || !left->graph ||
        left->domain != right->domain)
      return false;
    if (left->numeric && right->numeric && (!excluded || excluded->numeric)) {
      auto key = std::make_tuple(
          definition->item, selectionID,
          definition->kind == obelisk::coverage::FunctionalBinKind::State);
      auto cached = selections.find(key);
      if (cached == selections.end()) {
        auto trimmed = std::make_shared<CompiledSet>();
        for (auto interval : right->intervals) {
          if (excluded) {
            if (const auto *range = excluded->containing(interval.low)) {
              interval.low = range->high;
              interval.empty |=
                  !advanceStateCoverageEndpoint(interval.low, true);
            }
            if (const auto *range = excluded->containing(interval.high)) {
              interval.high = range->low;
              interval.empty |=
                  !advanceStateCoverageEndpoint(interval.high, false);
            }
          }
          if (!interval.empty && interval.low <= interval.high)
            trimmed->intervals.push_back(std::move(interval));
        }
        trimmed->indexEndpoints();
        cached = selections.emplace(key, std::move(trimmed)).first;
      }
      const auto &selected = *cached->second;
      for (const auto &interval : left->intervals) {
        auto position = selected.endpoints.upper_bound(interval.low);
        if (position != selected.endpoints.begin()) {
          auto previous = std::prev(position);
          if (selected.intervals[previous->second].high >= interval.low)
            position = previous;
        }
        if (position == selected.endpoints.end())
          continue;
        const auto &candidate = selected.intervals[position->second];
        if (interval.high < candidate.low)
          continue;
        const auto &low = std::max(interval.low, candidate.low);
        const auto &high = std::min(interval.high, candidate.high);
        const auto *removed = excluded ? excluded->containing(low) : nullptr;
        if (!removed)
          return true;
        auto next = removed->high;
        if (advanceStateCoverageEndpoint(next, true) && next <= high)
          return true;
        // A trimmed interval ends in a legal value. If this overlap is
        // wholly excluded, the subject ends before the next selector hull.
      }
      return false;
    }
    if (!prepare(*left) || !prepare(*right) ||
        (excluded && !prepare(*excluded)))
      return false;
    Graph &graph = *left->graph;
    for (const auto &[bval, root] : left->groups) {
      auto selected = right->groups.find(bval);
      if (selected == right->groups.end())
        continue;
      Root remaining = root;
      if (excluded) {
        auto removed = excluded->groups.find(bval);
        if (removed != excluded->groups.end())
          remaining = graph.subtract(root, removed->second);
      }
      Root overlap = graph.intersect(remaining, selected->second);
      if (graph.exhausted()) {
        status = OBELISK_RT_OUT_OF_RESOURCES;
        return false;
      }
      if (overlap)
        return true;
    }
    return false;
  }

  uint64_t
  cardinality(uint64_t binID, uint64_t cap,
              const std::unordered_set<uint64_t> &suppressedAlternatives) {
    const Bin *definition = bin(binID);
    if (!definition || !cap) {
      status = !cap ? OBELISK_RT_INVALID_ARGUMENT : OBELISK_RT_INVALID_DESIGN;
      return 0;
    }
    auto values = subject(*definition, suppressedAlternatives);
    auto excluded = mask(*definition);
    if (values && excluded && values->domain != excluded->domain)
      status = OBELISK_RT_INVALID_DESIGN;
    if (!values || status != OBELISK_RT_OK || !values->graph ||
        !prepare(*values) || (excluded && !prepare(*excluded)))
      return 0;
    Graph &graph = *values->graph;
    uint64_t result = 0;
    for (const auto &[bval, root] : values->groups) {
      Root remaining = root;
      if (excluded) {
        auto removed = excluded->groups.find(bval);
        if (removed != excluded->groups.end())
          remaining = graph.subtract(root, removed->second);
      }
      uint64_t count = graph.cardinality(remaining, cap);
      if (graph.exhausted()) {
        status = OBELISK_RT_OUT_OF_RESOURCES;
        return 0;
      }
      if (count >= cap - result)
        return cap;
      result += count;
    }
    return result;
  }
};

struct PreparedCrossValueIndex;
std::atomic<uint64_t> nextCoveragePreparationStamp{1};
struct PreparedFunctionalConstruction {
  std::unique_ptr<obelisk::coverage::Database> schema;
  std::unique_ptr<FunctionalCoverageSetQueryIndex> queries;
  const obelisk::coverage::Database *borrowedSchema = nullptr;
  FunctionalCoverageSetQueryIndex *borrowedQueries = nullptr;
  const obelisk::coverage::Database &database() const {
    return schema ? *schema : *borrowedSchema;
  }
  FunctionalCoverageSetQueryIndex &query() const {
    return queries ? *queries : *borrowedQueries;
  }
  std::unordered_set<uint64_t> suppressedTransitions;
  IndexMap<uint64_t, std::vector<uint64_t>> binsByItem;
  FunctionalMagnitude maximumMatchCount{1};
  IndexMap<uint64_t, uint64_t> withMatches;
  IndexMap<std::vector<uint64_t>, std::shared_ptr<PreparedCrossValueIndex>>
      crosses;
};

/// A relation between bin labels and their associated values. Keeping labels
/// until after selection preserves correlations; existential value projection
/// deduplicates overlapping bins without enumerating their Cartesian product.
struct PreparedCrossValueIndex {
  using Graph = obelisk::coverage::detail::DecisionIndex;
  using Image = obelisk::coverage::detail::DecisionProjectionIndex;
  using CountImage = obelisk::coverage::detail::DecisionCountProjectionIndex;
  using Root = Graph::Root;
  using CompiledSet = FunctionalCoverageSetQueryIndex::CompiledSet;
  struct Axis {
    uint64_t item = 0;
    uint32_t width = 0, binFirst = 0, binWidth = 0;
    bool fourState = false, indexFirst = false;
    std::vector<uint32_t> avalVariables, bvalVariables, valueAvalVariables,
        valueBvalVariables;
    obelisk::coverage::CoverageSignedness signedness;
    Graph *source = nullptr;
    Root relation = 0;
    std::unique_ptr<Image> avalImage, bvalImage;
    IndexMap<uint64_t, uint32_t> binOrdinals;
    IndexMap<uint64_t, Root> templateBins;
    IndexMap<std::pair<uintptr_t, uintptr_t>, Root> associated;
  };

  PreparedFunctionalConstruction &prepared;
  std::vector<Axis> axes;
  std::unique_ptr<Graph> relation, values;
  std::unique_ptr<Image> binImage, valueImage, sampleImage;
  std::shared_ptr<Graph> sampleGraph;
  std::shared_ptr<FunctionalCoverageCrossSelection> sampleSelection;
  std::unique_ptr<CountImage> countImage;
  std::unique_ptr<obelisk::coverage::detail::PredicateDecisionIndex> predicates;
  IndexMap<uint64_t, size_t> targetAxes;
  IndexMap<uint64_t, Root> selectors;
  Root universe = 1;
  obelisk_rt_status status = OBELISK_RT_OK;

  explicit PreparedCrossValueIndex(PreparedFunctionalConstruction &prepared)
      : prepared(prepared) {}

  bool check() {
    if (prepared.query().status != OBELISK_RT_OK)
      status = prepared.query().status;
    if ((relation && relation->exhausted()) ||
        (values && values->exhausted()) ||
        (countImage && countImage->exhausted()) ||
        (valueImage && valueImage->exhausted()) ||
        (binImage && binImage->exhausted()) ||
        (sampleImage && sampleImage->exhausted()) ||
        (sampleGraph && sampleGraph->exhausted()) ||
        (predicates && predicates->exhausted()))
      status = OBELISK_RT_OUT_OF_RESOURCES;
    return status == OBELISK_RT_OK;
  }

  Root label(const Axis &axis, uint32_t ordinal) {
    Root root = 1;
    for (uint32_t bit = 0; bit != axis.binWidth; ++bit)
      root = (ordinal >> bit) & 1
                 ? relation->makeNode(axis.binFirst + axis.binWidth - bit - 1,
                                      0, root)
                 : relation->makeNode(axis.binFirst + axis.binWidth - bit - 1,
                                      root, 0);
    return root;
  }

  Root associated(Axis &axis, const std::shared_ptr<CompiledSet> &set,
                  const std::shared_ptr<CompiledSet> &mask = nullptr) {
    auto &queries = prepared.query();
    if (!set || set->graph != axis.source ||
        (mask && mask->domain != set->domain)) {
      status = queries.status == OBELISK_RT_OK ? OBELISK_RT_INVALID_DESIGN
                                               : queries.status;
      return 0;
    }
    auto key = std::make_pair(reinterpret_cast<uintptr_t>(set.get()),
                              reinterpret_cast<uintptr_t>(mask.get()));
    auto found = axis.associated.find(key);
    if (found != axis.associated.end())
      return found->second;
    if (!queries.prepare(*set) || (mask && !queries.prepare(*mask))) {
      status = queries.status;
      return 0;
    }
    Root result = 0;
    std::vector<uint64_t> fixed((uint64_t{axis.width} + 63) / 64);
    for (const auto &[bval, root] : set->groups) {
      Root remaining = root;
      if (mask) {
        auto excluded = mask->groups.find(bval);
        if (excluded != mask->groups.end())
          remaining = axis.source->subtract(remaining, excluded->second);
      }
      if (!remaining)
        continue;
      Root aval = axis.avalImage->project(remaining);
      Root bplane = axis.bvalImage->project(axis.source->cube(bval, fixed));
      result = relation->unite(result, relation->intersect(aval, bplane));
      if (axis.avalImage->exhausted() || axis.bvalImage->exhausted() ||
          !check()) {
        status = OBELISK_RT_OUT_OF_RESOURCES;
        return 0;
      }
    }
    axis.associated.emplace(key, result);
    return result;
  }

  bool build(const obelisk::coverage::CrossPlan &plan) {
    using namespace obelisk::coverage;
    const auto &schema = prepared.database();
    auto &queries = prepared.query();
    uint64_t relationWidth = 0, valueWidth = 0;
    for (uint32_t ordinal = 0; ordinal != plan.targetCount; ++ordinal) {
      const auto &target = schema.crossTargets[plan.firstTarget + ordinal];
      const auto *item = queries.lookup(
          queries.templateItems, schema.resolvedFunctionalItems, target.target);
      if (!item ||
          target.tupleResultKind != FunctionalExpressionResultKind::Integral ||
          !target.tupleBitWidth) {
        status = OBELISK_RT_INVALID_DESIGN;
        return false;
      }
      Axis axis;
      axis.item = item->id;
      axis.width = target.tupleBitWidth;
      axis.signedness = target.tupleSignedness;
      axis.fourState = target.tupleFlags & CrossTargetDomainFourState;
      axis.indexFirst = queries.indexTargets.count(target.target);
      auto bins = prepared.binsByItem.find(axis.item);
      uint64_t last = bins == prepared.binsByItem.end() || bins->second.empty()
                          ? 0
                          : bins->second.size() - 1;
      while (last) {
        ++axis.binWidth;
        last >>= 1;
      }
      if (relationWidth + axis.binWidth + uint64_t{axis.width} * 2 >
              UINT32_MAX ||
          valueWidth + uint64_t{axis.width} * 2 > UINT32_MAX) {
        status = OBELISK_RT_OUT_OF_RESOURCES;
        return false;
      }
      relationWidth += axis.binWidth + uint64_t{axis.width} * 2;
      valueWidth += uint64_t{axis.width} * 2;
      FunctionalCoverageSetQueryIndex::DomainKey domain = {
          static_cast<uint32_t>(FunctionalValueSetKind::Integral), axis.width,
          static_cast<uint32_t>(axis.signedness), false};
      axis.source = &queries.graphFor(domain);
      targetAxes.emplace(target.target, axes.size());
      axes.push_back(std::move(axis));
    }
    if (relationWidth > (uint64_t{1} << 18)) {
      status = OBELISK_RT_OUT_OF_RESOURCES;
      return false;
    }
    relation = std::make_unique<Graph>(relationWidth);
    values = std::make_unique<Graph>(valueWidth);
    uint32_t labelVariable = 0;
    std::vector<std::pair<uint32_t, uint32_t>> physicalBits;
    for (auto &axis : axes) {
      axis.binFirst = labelVariable;
      labelVariable += axis.binWidth;
      axis.avalVariables.resize(axis.width);
      axis.bvalVariables.resize(axis.width);
      axis.valueAvalVariables.resize(axis.width);
      axis.valueBvalVariables.resize(axis.width);
      for (uint32_t bit = 0; bit != axis.width; ++bit)
        physicalBits.emplace_back(&axis - axes.data(), bit);
    }
    detail::sortByKey(physicalBits.begin(), physicalBits.end(),
                      [&](const auto &bit) {
                        return std::make_tuple(!axes[bit.first].indexFirst,
                                               ~bit.second, bit.first);
                      });
    uint32_t relationVariable = labelVariable, valueVariable = 0;
    for (const auto &physical : physicalBits)
      for (uint32_t plane = 0; plane != 2; ++plane) {
        auto &axis = axes[physical.first];
        const uint32_t bit = physical.second;
        (plane ? axis.avalVariables : axis.bvalVariables)[bit] =
            relationVariable++;
        (plane ? axis.valueAvalVariables : axis.valueBvalVariables)[bit] =
            valueVariable++;
      }
    std::vector<uint32_t> binVariables(relationWidth, UINT32_MAX),
        valueVariables(relationWidth, UINT32_MAX);
    for (auto &axis : axes) {
      std::vector<uint32_t> avalVariables(axis.width),
          bvalVariables(axis.width);
      for (uint32_t bit = 0; bit != axis.width; ++bit) {
        avalVariables[axis.width - bit - 1] = axis.avalVariables[bit];
        bvalVariables[axis.width - bit - 1] = axis.bvalVariables[bit];
        valueVariables[axis.avalVariables[bit]] = axis.valueAvalVariables[bit];
        valueVariables[axis.bvalVariables[bit]] = axis.valueBvalVariables[bit];
      }
      for (uint32_t bit = 0; bit != axis.binWidth; ++bit)
        binVariables[axis.binFirst + bit] = axis.binFirst + bit;
      axis.avalImage = std::make_unique<Image>(*axis.source, *relation,
                                               std::move(avalVariables));
      axis.bvalImage = std::make_unique<Image>(*axis.source, *relation,
                                               std::move(bvalVariables));
      auto bins = prepared.binsByItem.find(axis.item);
      if (bins != prepared.binsByItem.end()) {
        for (uint32_t ordinal = 0; ordinal != bins->second.size(); ++ordinal) {
          const auto *bin = queries.bin(bins->second[ordinal]);
          axis.binOrdinals.emplace(bin->id, ordinal);
          Root binLabel = label(axis, ordinal);
          Root binValues = associated(
              axis, queries.subject(*bin, prepared.suppressedTransitions),
              queries.mask(*bin));
          axis.relation = relation->unite(
              axis.relation, relation->intersect(binLabel, binValues));
          auto &authored = axis.templateBins[bin->templateBin];
          authored = relation->unite(authored, binLabel);
          if (!check())
            return false;
        }
      }
      universe = relation->intersect(universe, axis.relation);
    }
    binImage = std::make_unique<Image>(*relation, *relation, binVariables);
    countImage = std::make_unique<CountImage>(*relation, *relation,
                                              std::move(binVariables),
                                              prepared.maximumMatchCount);
    valueImage =
        std::make_unique<Image>(*relation, *values, std::move(valueVariables));
    predicates =
        std::make_unique<obelisk::coverage::detail::PredicateDecisionIndex>(
            *relation);
    return check();
  }

  Root predicate(const obelisk::coverage::CrossSelectorNode &selector) {
    using namespace obelisk::coverage;
    using Predicate = detail::PredicateDecisionIndex;
    const auto &schema = prepared.database();
    auto &queries = prepared.query();
    auto program = queries.predicatePrograms.find(selector.id);
    if (program == queries.predicatePrograms.end()) {
      status = OBELISK_RT_INVALID_DESIGN;
      return 0;
    }
    return predicates->compile(
        selector.predicateRoot, program->second,
        [&](const CrossPredicateNode &node) {
          Predicate::Value value;
          if (node.kind == CrossPredicateKind::Target) {
            auto target = targetAxes.find(node.target);
            if (target == targetAxes.end()) {
              status = OBELISK_RT_INVALID_DESIGN;
              return value;
            }
            const auto &axis = axes[target->second];
            value.aval.resize(axis.width);
            value.bval.resize(axis.width);
            value.isSigned = axis.signedness == CoverageSignedness::Signed;
            for (uint32_t bit = 0; bit != axis.width; ++bit) {
              value.aval[bit] =
                  relation->makeNode(axis.avalVariables[bit], 0, 1);
              value.bval[bit] =
                  axis.fourState
                      ? relation->makeNode(axis.bvalVariables[bit], 0, 1)
                      : 0;
              if (value.isSigned && bit + 1 == axis.width)
                value.aval[bit] = relation->subtract(1, value.aval[bit]);
            }
            Predicate::resize(value, node.bitWidth, value.isSigned);
          } else {
            auto found =
                queries.predicateValues.find({node.selector, node.ordinal});
            if (found == queries.predicateValues.end()) {
              status = OBELISK_RT_INVALID_DESIGN;
              return value;
            }
            const auto &set = *found->second;
            if (set.atomCount != 1 || set.bitWidth != node.bitWidth) {
              status = OBELISK_RT_INVALID_DESIGN;
              return value;
            }
            const auto &atom =
                schema.resolvedFunctionalValueAtoms[set.firstAtom];
            value.aval.resize(node.bitWidth);
            value.bval.resize(node.bitWidth);
            for (uint32_t bit = 0; bit != node.bitWidth; ++bit) {
              const auto &limb =
                  schema
                      .resolvedFunctionalValueLimbs[atom.firstLimb + bit / 64];
              value.aval[bit] = (limb.lowAval >> (bit % 64)) & 1;
              value.bval[bit] = (limb.lowBval >> (bit % 64)) & 1;
            }
          }
          return value;
        });
  }

  Root
  matchingBins(Root subject, Root accepted,
               const obelisk::coverage::ResolvedCrossSelectorBinding &binding) {
    using namespace obelisk::coverage;
    if (binding.matchesPolicy == CrossMatchesPolicy::All)
      return relation->subtract(
          binImage->project(subject),
          binImage->project(relation->subtract(subject, accepted)));
    auto count = prepared.query().matchCount(binding);
    if (count.empty()) {
      status = OBELISK_RT_INVALID_DESIGN;
      return 0;
    }
    Root satisfying = relation->intersect(subject, accepted);
    return count.size() == 1 && count.front() == 1
               ? binImage->project(satisfying)
               : countImage->atLeast(satisfying, std::move(count));
  }

  Root tupleValues(const obelisk::coverage::ResolvedFunctionalTupleSet &set) {
    const auto &schema = prepared.database();
    auto &queries = prepared.query();
    Root result = 0;
    for (uint32_t ordinal = 0; ordinal != set.tupleCount; ++ordinal) {
      const auto &tuple =
          schema.resolvedFunctionalTupleSetTuples[set.firstTuple + ordinal];
      if (tuple.componentCount != axes.size()) {
        status = OBELISK_RT_INVALID_DESIGN;
        return 0;
      }
      Root value = 1;
      for (uint32_t index = 0; index != tuple.componentCount; ++index) {
        const auto &component =
            schema.resolvedFunctionalTupleSetComponents[tuple.firstComponent +
                                                        index];
        if (component.ordinal != index ||
            component.target != axes[index].item) {
          status = OBELISK_RT_INVALID_DESIGN;
          return 0;
        }
        value = relation->intersect(
            value, associated(axes[index],
                              queries.compile({component.valueSet}, false)));
      }
      result = relation->unite(result, value);
      if (!check())
        return 0;
    }
    return result;
  }

  Root select(const obelisk::coverage::CrossSelectorNode &root,
              uint64_t resolvedCross, uint64_t outerWith) {
    using namespace obelisk::coverage;
    const auto &schema = prepared.database();
    auto &queries = prepared.query();
    if (auto cached = selectors.find(root.id); cached != selectors.end())
      return cached->second;
    std::vector<const CrossSelectorNode *> pending{&root}, reachable;
    IndexSet<uint64_t> visited;
    while (!pending.empty()) {
      const auto *node = pending.back();
      pending.pop_back();
      if (selectors.count(node->id) || !visited.insert(node->id).second)
        continue;
      if (node->withExpression && node->id != outerWith &&
          !queries.binding(resolvedCross, node->id)) {
        status = OBELISK_RT_INVALID_DESIGN;
        return 0;
      }
      reachable.push_back(node);
      for (uint32_t ordinal = 0; ordinal != node->operandCount; ++ordinal) {
        const auto &operand =
            schema.crossSelectorOperands[node->firstOperand + ordinal];
        const auto *child = queries.lookup(
            queries.selectors, schema.crossSelectorNodes, operand.operand);
        if (!child || child->cross != root.cross ||
            child->ordinal >= node->ordinal || operand.node != node->id ||
            operand.ordinal != ordinal) {
          status = OBELISK_RT_INVALID_DESIGN;
          return 0;
        }
        pending.push_back(child);
      }
    }
    sortByKey(reachable.begin(), reachable.end(),
              [](const auto *node) { return node->ordinal; });
    Root outerSelection = 0;
    for (const auto *node : reachable) {
      Root selected = 0;
      auto operand = [&](uint32_t ordinal) {
        auto found = selectors.find(
            schema.crossSelectorOperands[node->firstOperand + ordinal].operand);
        return found == selectors.end() ? Root{0} : found->second;
      };
      if (node->kind == CrossSelectorKind::AllTuples) {
        selected = 1;
      } else if (node->kind == CrossSelectorKind::Binsof) {
        auto target = targetAxes.find(node->target);
        if (target == targetAxes.end()) {
          status = OBELISK_RT_INVALID_DESIGN;
          return 0;
        }
        auto &axis = axes[target->second];
        selected = 1;
        if (node->bin) {
          auto bins = axis.templateBins.find(node->bin);
          selected = bins == axis.templateBins.end() ? 0 : bins->second;
        }
        if (node->valueSet) {
          const auto *binding = queries.binding(resolvedCross, node->id);
          if (!binding || !binding->valueSet) {
            status = OBELISK_RT_INVALID_DESIGN;
            return 0;
          }
          Root selection =
              associated(axis, queries.compile({binding->valueSet}, false));
          selected = relation->intersect(
              selected,
              binImage->project(relation->intersect(axis.relation, selection)));
        }
      } else if (node->kind == CrossSelectorKind::Not) {
        selected = relation->subtract(1, operand(0));
      } else if (node->kind == CrossSelectorKind::And) {
        selected = relation->intersect(operand(0), operand(1));
      } else if (node->kind == CrossSelectorKind::Or) {
        selected = relation->unite(operand(0), operand(1));
      } else if (node->kind == CrossSelectorKind::With) {
        selected = operand(0);
      } else if (node->kind == CrossSelectorKind::Set) {
        const auto *binding = queries.binding(resolvedCross, node->id);
        const auto *set =
            binding ? queries.lookup(queries.tupleSets,
                                     schema.resolvedFunctionalTupleSets,
                                     binding->tupleSet)
                    : nullptr;
        if (!set) {
          status = OBELISK_RT_INVALID_DESIGN;
          return 0;
        }
        Root accepted = tupleValues(*set);
        selected = matchingBins(universe, accepted, *binding);
      } else {
        status = OBELISK_RT_INVALID_DESIGN;
        return 0;
      }
      if ((node->withExpression || node->predicateRoot) &&
          node->id != outerWith) {
        const auto *binding = queries.binding(resolvedCross, node->id);
        if (!binding) {
          status = OBELISK_RT_INVALID_DESIGN;
          return 0;
        }
        Root accepted = 0;
        if (binding->flags & ResolvedCrossSelectorSymbolicWith)
          accepted = predicate(*node);
        else if (binding->flags & ResolvedCrossSelectorScalarWith)
          accepted = (binding->flags & ResolvedCrossSelectorScalarTrue) ? 1 : 0;
        else {
          const auto *set = queries.lookup(queries.tupleSets,
                                           schema.resolvedFunctionalTupleSets,
                                           binding->tupleSet);
          if (!set) {
            status = OBELISK_RT_INVALID_DESIGN;
            return 0;
          }
          accepted = tupleValues(*set);
        }
        Root subject = relation->intersect(universe, selected);
        selected = matchingBins(subject, accepted, *binding);
      }
      if (!check())
        return 0;
      if (node->id == outerWith)
        outerSelection = selected;
      else
        selectors.emplace(node->id, selected);
    }
    return root.id == outerWith ? outerSelection
                                : selectors.find(root.id)->second;
  }

  // Import label roots once through a shared projection memo. Only this
  // compact graph survives initialization; associated values and count planes
  // belong to the temporary constructor index.
  Root sampleRoot(Root selected) {
    if (!sampleSelection) {
      uint32_t width = 0;
      for (const auto &axis : axes)
        width += axis.binWidth;
      sampleGraph = std::make_shared<Graph>(width);
      sampleSelection = std::make_shared<FunctionalCoverageCrossSelection>();
      sampleSelection->graph = sampleGraph;
      sampleSelection->bins.resize(axes.size());
      std::vector<uint32_t> mapping(relation->getWidth(), UINT32_MAX);
      uint32_t variable = 0;
      for (uint32_t ordinal = 0; ordinal != axes.size(); ++ordinal) {
        const auto &axis = axes[ordinal];
        for (const auto &[bin, label] : axis.binOrdinals)
          sampleSelection->bins[ordinal].emplace(bin, label);
        for (uint32_t bit = 0; bit != axis.binWidth; ++bit) {
          mapping[axis.binFirst + bit] = variable++;
          sampleSelection->variables.emplace_back(ordinal,
                                                  axis.binWidth - bit - 1);
        }
      }
      sampleImage =
          std::make_unique<Image>(*relation, *sampleGraph, std::move(mapping));
    }
    return sampleImage->project(selected);
  }

  obelisk_rt_status bind(const obelisk::coverage::CrossPlan &plan,
                         const obelisk::coverage::CrossSelectorNode &node,
                         uint64_t resolvedCross,
                         const CoverageTupleQueueSnapshot &snapshot) {
    using namespace obelisk::coverage;
    auto matches = prepared.withMatches.find(node.id);
    if (!node.withExpression || matches == prepared.withMatches.end() ||
        selectors.count(node.id) || plan.targetCount != axes.size())
      return OBELISK_RT_INVALID_DESIGN;
    Root subordinate = select(node, resolvedCross, node.id);
    if (!check())
      return status;
    std::vector<uint8_t> used(snapshot.valueSize);
    for (uint32_t ordinal = 0; ordinal != plan.targetCount; ++ordinal) {
      const auto &target =
          prepared.database().crossTargets[plan.firstTarget + ordinal];
      for (uint32_t bit = 0; bit != target.tupleBitWidth; ++bit)
        used[(target.tupleBitOffset + bit) / 8] |=
            uint8_t{1} << ((target.tupleBitOffset + bit) % 8);
    }
    Root accepted = 0;
    for (uint64_t ordinal = 0; ordinal != snapshot.count; ++ordinal) {
      const auto *aval = snapshot.value.data() + ordinal * snapshot.valueSize;
      const auto *bval = snapshot.fourState ? snapshot.unknown.data() +
                                                  ordinal * snapshot.valueSize
                                            : nullptr;
      for (uint64_t byte = 0; byte != snapshot.valueSize; ++byte)
        if ((aval[byte] & ~used[byte]) || (bval && (bval[byte] & ~used[byte])))
          return OBELISK_RT_INVALID_DESIGN;
      Root tuple = 1;
      std::vector<std::pair<uint32_t, bool>> assignments;
      for (size_t index = axes.size(); index != 0; --index) {
        const auto &axis = axes[index - 1];
        const auto &target =
            prepared.database().crossTargets[plan.firstTarget + index - 1];
        for (uint32_t plane = 0; plane != 2; ++plane)
          for (uint32_t bit = 0; bit != axis.width; ++bit) {
            uint64_t encodedBit = target.tupleBitOffset + bit;
            bool one =
                plane ? bval && ((bval[encodedBit / 8] >> (encodedBit % 8)) & 1)
                      : ((aval[encodedBit / 8] >> (encodedBit % 8)) & 1);
            if (plane && one &&
                !(target.tupleFlags & CrossTargetDomainFourState))
              return OBELISK_RT_INVALID_DESIGN;
            if (!plane && axis.signedness == CoverageSignedness::Signed &&
                bit == axis.width - 1)
              one = !one;
            uint32_t variable =
                (plane ? axis.bvalVariables : axis.avalVariables)[bit];
            assignments.emplace_back(variable, one);
          }
      }
      sortByKey(assignments.begin(), assignments.end(),
                [](const auto &entry) { return ~entry.first; });
      for (const auto &[variable, one] : assignments)
        tuple = one ? relation->makeNode(variable, 0, tuple)
                    : relation->makeNode(variable, tuple, 0);
      accepted = relation->unite(accepted, tuple);
      if (!check())
        return status;
    }
    Root subject = relation->intersect(universe, subordinate);
    Root selected =
        node.matchesPolicy == CrossMatchesPolicy::All
            ? relation->subtract(
                  binImage->project(subject),
                  binImage->project(relation->subtract(subject, accepted)))
        : matches->second == 1
            ? binImage->project(relation->intersect(subject, accepted))
            : countImage->atLeast(relation->intersect(subject, accepted),
                                  matches->second);
    if (!check())
      return status;
    selectors.emplace(node.id, selected);
    return OBELISK_RT_OK;
  }

  obelisk_rt_status candidates(const obelisk::coverage::CrossPlan &plan,
                               const obelisk::coverage::CrossSelectorNode &root,
                               uint64_t resolvedCross, uint32_t maximum,
                               CoverageTupleQueueSnapshot &output) {
    using namespace obelisk::coverage;
    Root selected =
        select(root, resolvedCross, root.withExpression ? root.id : 0);
    Root image = valueImage->project(relation->intersect(universe, selected));
    std::vector<std::vector<uint64_t>> tuples;
    if (!check())
      return status;
    if (!values->enumerate(image, maximum, tuples))
      return OBELISK_RT_OUT_OF_RESOURCES;
    CoverageTupleQueueSnapshot result;
    result.count = tuples.size();
    if (!plan.tupleProvenanceSpan || plan.tupleProvenanceSpan > UINT64_MAX - 7)
      return OBELISK_RT_INVALID_DESIGN;
    result.valueSize = (plan.tupleProvenanceSpan + 7) / 8;
    result.fourState = (plan.tupleFlags & CrossTupleFourState) != 0;
    uint64_t byteLimit = std::min<uint64_t>(ParseLimits{}.maxSectionBytes,
                                            std::numeric_limits<size_t>::max());
    if (result.valueSize > byteLimit ||
        (result.count && result.valueSize > byteLimit / result.count))
      return OBELISK_RT_OUT_OF_RESOURCES;
    result.value.resize(result.count * result.valueSize);
    if (result.fourState)
      result.unknown.resize(result.value.size());
    for (size_t index = 0; index != tuples.size(); ++index) {
      auto read = [&](uint32_t bit) {
        return (tuples[index][bit / 64] >> (bit % 64)) & 1;
      };
      auto write = [&](std::vector<uint8_t> &plane, uint64_t bit, bool one) {
        if (one)
          plane[index * result.valueSize + bit / 8] |= uint8_t{1} << (bit % 8);
      };
      for (size_t ordinal = 0; ordinal != axes.size(); ++ordinal) {
        const auto &axis = axes[ordinal];
        const auto &target =
            prepared.database().crossTargets[plan.firstTarget + ordinal];

        for (uint32_t bit = 0; bit != axis.width; ++bit) {
          bool aval =
              read(values->getWidth() - 1 - axis.valueAvalVariables[bit]);
          if (axis.signedness == CoverageSignedness::Signed &&
              bit == axis.width - 1)
            aval = !aval;
          write(result.value, target.tupleBitOffset + bit, aval);
          bool bval =
              read(values->getWidth() - 1 - axis.valueBvalVariables[bit]);
          if (bval && !result.fourState)
            return OBELISK_RT_INVALID_DESIGN;
          if (result.fourState)
            write(result.unknown, target.tupleBitOffset + bit, bval);
        }
      }
    }
    output = std::move(result);
    return OBELISK_RT_OK;
  }
};

// Index each physical bin once; reused crosses share their target geometry.
void initializeSelectionIndex(PreparedFunctionalConstruction &index,
                              const obelisk::coverage::Database &schema,
                              FunctionalCoverageSetQueryIndex &queries,
                              uint64_t type,
                              const obelisk::coverage::Digest &configuration,
                              const std::unordered_set<uint64_t> &suppressed) {
  using namespace obelisk::coverage;
  index.borrowedSchema = &schema;
  index.borrowedQueries = &queries;
  index.suppressedTransitions = suppressed;
  constexpr uint32_t omitted =
      FunctionalBinDefault | FunctionalBinDefaultSequence |
      FunctionalBinIgnore | FunctionalBinIllegal | FunctionalBinEmpty;
  for (const auto &bin : schema.resolvedFunctionalBins)
    if (bin.type == type && bin.configuration == configuration &&
        (bin.kind == FunctionalBinKind::State ||
         bin.kind == FunctionalBinKind::Transition) &&
        !(bin.flags & omitted))
      index.binsByItem[bin.item].push_back(bin.id);
  for (auto &[item, bins] : index.binsByItem) {
    (void)item;
    sortByKey(bins.begin(), bins.end(), [](uint64_t id) { return id; });
  }
  for (const auto &binding : schema.resolvedCrossSelectorBindings)
    if (binding.type == type && binding.configuration == configuration) {
      auto count = queries.matchCount(binding);
      if (detail::DecisionCountProjectionIndex::compareMagnitudes(
              count, index.maximumMatchCount) > 0)
        index.maximumMatchCount = std::move(count);
    }
}

std::shared_ptr<PreparedCrossValueIndex>
selectionKernel(PreparedFunctionalConstruction &index,
                const obelisk::coverage::CrossPlan &plan) {
  std::vector<uint64_t> targets;
  targets.reserve(plan.targetCount);
  for (uint32_t ordinal = 0; ordinal != plan.targetCount; ++ordinal)
    targets.push_back(
        index.database().crossTargets[plan.firstTarget + ordinal].target);
  auto cached = index.crosses.find(targets);
  if (cached != index.crosses.end())
    return cached->second;
  auto kernel = std::make_shared<PreparedCrossValueIndex>(index);
  kernel->build(plan);
  index.crosses.emplace(std::move(targets), kernel);
  return kernel;
}

obelisk_rt_status resolvedFunctionalBinIntersectsValueSet(
    const obelisk::coverage::Database &schema, uint64_t type,
    const obelisk::coverage::Digest &configuration, uint64_t bin,
    const obelisk::coverage::ResolvedFunctionalValueSet &selection,
    const std::unordered_set<uint64_t> &suppressedTransitionAlternatives,
    FunctionalCoverageSetQueryIndex &setQueries, bool &result);
bool crossSelectorUsesWith(const obelisk::coverage::Database &schema,
                           const obelisk::coverage::CrossSelectorNode &root,
                           FunctionalCoverageSetQueryIndex &setQueries);
bool crossSelectorUsesSet(const obelisk::coverage::Database &schema,
                          const obelisk::coverage::CrossSelectorNode &root,
                          FunctionalCoverageSetQueryIndex &setQueries);
obelisk_rt_status resolvedCrossSelectorSelectsBinTuple(
    const obelisk::coverage::Database &schema, uint64_t type,
    const obelisk::coverage::Digest &configuration, uint64_t resolvedCross,
    const obelisk::coverage::CrossSelectorNode &root,
    const std::vector<uint64_t> &binTuple,
    const std::unordered_set<uint64_t> &suppressedTransitionAlternatives,
    FunctionalCoverageSetQueryIndex &setQueries, bool &result);
obelisk_rt_status resolvedCrossSelectorSetCandidates(
    const obelisk::coverage::Database &schema, uint64_t type,
    const obelisk::coverage::Digest &configuration, uint64_t resolvedCross,
    const obelisk::coverage::CrossSelectorNode &root,
    const std::vector<std::vector<uint64_t>> &targetBins,
    const std::unordered_set<uint64_t> &suppressedTransitionAlternatives,
    FunctionalCoverageSetQueryIndex &setQueries,
    std::vector<std::vector<uint64_t>> &result);
obelisk_rt_status
emitFunctionalOverlapWarnings(const obelisk::coverage::Database &schema,
                              uint64_t type,
                              const obelisk::coverage::Digest &configuration);

IndexMap<uint64_t, FunctionalCoverageConstructionSchemas::TypeProfile>
staticFunctionalTypeProfiles(const obelisk::coverage::Database &schema) {
  using namespace obelisk::coverage;
  IndexMap<uint64_t, FunctionalCoverageConstructionSchemas::TypeProfile>
      profiles;
  IndexMap<std::tuple<uint64_t, Digest, uint64_t>, uint64_t> items;
  for (const auto &row : schema.resolvedFunctionalItems)
    items.emplace(std::make_tuple(row.type, row.configuration, row.id),
                  row.templateItem);
  for (const auto &row : schema.functionalConfigurations)
    profiles[row.type];
  for (const auto &row : schema.functionalConfigurationOptions) {
    if (row.scope != FunctionalOptionScopeKind::Type ||
        row.option == FunctionalConfigurationOptionKind::DistributeFirst)
      continue;
    uint64_t fallback =
        row.option == FunctionalConfigurationOptionKind::Goal     ? 100
        : row.option == FunctionalConfigurationOptionKind::Weight ? 1
                                                                  : 0;
    auto fallbackKind = row.option == FunctionalConfigurationOptionKind::Comment
                            ? FunctionalConfigurationValueKind::String
                            : FunctionalConfigurationValueKind::Unsigned;
    if (row.valueKind == fallbackKind && row.value == fallback &&
        row.stringValue.empty())
      continue;
    uint64_t owner =
        row.ownerKind == FunctionalConfigurationOptionOwnerKind::Group
            ? row.type
            : items.at({row.type, row.configuration, row.owner});
    profiles[row.type].emplace(
        std::make_tuple(row.ownerKind, owner, row.option),
        std::make_tuple(row.valueKind, row.value, row.stringValue));
  }
  return profiles;
}

obelisk_rt_status ensureFunctionalConstructionSchemas(CoverageState &coverage) {
  using namespace obelisk::coverage;
  if (!coverage.schema)
    return OBELISK_RT_INVALID_DESIGN;
  const auto &source = *coverage.schema;
  if (coverage.constructionSchemas &&
      coverage.constructionSchemas->schemaFingerprint ==
          source.schemaFingerprint)
    return OBELISK_RT_OK;

  auto prepared = std::make_shared<FunctionalCoverageConstructionSchemas>();
  if (!prepared->hierarchy.build(source.functionalTypes))
    return OBELISK_RT_INVALID_DESIGN;
  prepared->schemaFingerprint = source.schemaFingerprint;
  prepared->loadedTypeProfiles = staticFunctionalTypeProfiles(source);
  IndexMap<uint64_t, std::shared_ptr<Database>> types;
  IndexMap<uint64_t, const Scope *> scopes;
  for (const auto &scope : source.scopes)
    scopes.emplace(scope.id, &scope);
  for (const auto &type : source.functionalTypes) {
    auto scope = scopes.find(type.scope);
    if (scope == scopes.end())
      return OBELISK_RT_INVALID_DESIGN;
    auto schema = std::make_shared<Database>();
    schema->flags = source.flags;
    schema->producer = source.producer;
    schema->functionalTypes.push_back(type);
    // The shared hierarchy above validates ancestry once. A private
    // construction projection does not clone each type's ancestor closure.
    schema->functionalTypes.back().base = 0;
    schema->scopes.push_back(*scope->second);
    // This is a private validation projection, never a persisted hierarchy.
    schema->scopes.back().parent = 0;
    types.emplace(type.id, std::move(schema));
  }
  auto schemaFor = [&](uint64_t type) -> Database & { return *types.at(type); };
  IndexMap<uint64_t, uint64_t> itemTypes, binTypes;
  IndexMap<std::pair<FunctionalExpressionOwnerKind, uint64_t>, uint64_t> owners;
  using Owner = FunctionalExpressionOwnerKind;
  for (const auto &type : source.functionalTypes)
    owners.emplace(std::make_pair(Owner::Type, type.id), type.id);
  for (const auto &row : source.functionalItems) {
    schemaFor(row.type).functionalItems.push_back(row);
    itemTypes.emplace(row.id, row.type);
    owners.emplace(std::make_pair(Owner::Item, row.id), row.type);
    prepared->itemIDs.insert(row.id);
    prepared->items.emplace(row.id, row);
  }
  for (const auto &row : source.functionalBins) {
    uint64_t type = itemTypes.at(row.item);
    schemaFor(type).functionalBins.push_back(row);
    binTypes.emplace(row.id, type);
    owners.emplace(std::make_pair(Owner::Bin, row.id), type);
    prepared->binIDs.insert(row.id);
  }
  IndexMap<uint64_t, std::vector<uint64_t>> containerTypeUsers;
  IndexSet<std::pair<uint64_t, uint64_t>> containerTypeUses;
  IndexSet<std::pair<uint64_t, uint64_t>> nominalTypeUses;
  for (const auto &row : source.functionalFormals) {
    schemaFor(row.type).functionalFormals.push_back(row);
    owners.emplace(std::make_pair(Owner::Formal, row.id), row.type);
    if (row.valueType) {
      prepared->formalValueTypes.emplace(row.id, row.valueType);
      if (row.resultKind == FunctionalExpressionResultKind::Covergroup) {
        if (row.valueType != row.type &&
            nominalTypeUses.emplace(row.type, row.valueType).second) {
          auto &schema = schemaFor(row.type);
          std::string hierarchy = "type." + std::to_string(row.valueType);
          if (hierarchy == schema.functionalTypes.front().hierarchy)
            hierarchy += "_";
          // Borrow only the target's identity for relational validation.
          // Its plans, scopes, names and inheritance remain shared globally.
          schema.functionalTypes.push_back(
              {row.valueType, schema.scopes.front().id, "reference", 0,
               schemaFor(row.valueType).functionalTypes.front().languageVersion,
               std::move(hierarchy)});
        }
        continue;
      }
      if (containerTypeUses.emplace(row.valueType, row.type).second)
        containerTypeUsers[row.valueType].push_back(row.type);
    }
  }
  for (const auto &row : source.functionalContainerTypes) {
    prepared->containerTypes.emplace(row.id, row);
    for (uint64_t type : containerTypeUsers[row.id])
      schemaFor(type).functionalContainerTypes.push_back(row);
  }
  for (const auto &row : source.functionalAggregateTypes) {
    prepared->aggregateTypes.emplace(row.id, row);
    for (uint64_t type : containerTypeUsers[row.id])
      schemaFor(type).functionalAggregateTypes.push_back(row);
  }
  for (const auto &row : source.functionalClassTypes) {
    prepared->classTypes.emplace(row.id, row);
    for (uint64_t type : containerTypeUsers[row.id])
      schemaFor(type).functionalClassTypes.push_back(row);
  }
  for (const auto &row : source.functionalVirtualInterfaceTypes) {
    prepared->virtualInterfaceTypes.emplace(row.id, row.interfaceType);
    for (uint64_t type : containerTypeUsers[row.id])
      schemaFor(type).functionalVirtualInterfaceTypes.push_back(row);
  }
  for (const auto &row : source.functionalVirtualInterfaceScopes)
    prepared->virtualInterfaceScopes.emplace(row.id, row.interfaceType);
  auto appendSpan = [](auto &destination, const auto &rows, uint32_t first,
                       uint32_t count) {
    destination.insert(destination.end(), rows.begin() + first,
                       rows.begin() + first + count);
  };
  for (const auto &row : source.functionalValueSets) {
    uint64_t type = itemTypes.at(row.item);
    auto &schema = schemaFor(type);
    auto set = row;
    set.firstAtom = schema.functionalValueAtoms.size();
    for (uint32_t i = 0; i != row.atomCount; ++i) {
      auto atom = source.functionalValueAtoms[row.firstAtom + i];
      const uint32_t first = atom.firstLimb;
      atom.firstLimb = schema.functionalValueLimbs.size();
      appendSpan(schema.functionalValueLimbs, source.functionalValueLimbs,
                 first, atom.limbCount);
      schema.functionalValueAtoms.push_back(atom);
    }
    schema.functionalValueSets.push_back(set);
    owners.emplace(std::make_pair(Owner::ValueSet, row.id), type);
    prepared->valueSetIDs.insert(row.id);
  }
  for (const auto &row : source.exclusions)
    if (row.metric == MetricKind::Functional)
      schemaFor(binTypes.at(row.entity)).exclusions.push_back(row);
  for (const auto &row : source.functionalBinPlans)
    schemaFor(binTypes.at(row.bin)).functionalBinPlans.push_back(row);
  for (const auto &row : source.transitionPrograms) {
    auto &schema = schemaFor(itemTypes.at(row.item));
    auto program = row;
    program.firstAlternative = schema.transitionAlternatives.size();
    for (uint32_t i = 0; i != row.alternativeCount; ++i) {
      auto alternative =
          source.transitionAlternatives[row.firstAlternative + i];
      const uint32_t first = alternative.firstStep;
      alternative.firstStep = schema.transitionSteps.size();
      appendSpan(schema.transitionSteps, source.transitionSteps, first,
                 alternative.stepCount);
      schema.transitionAlternatives.push_back(alternative);
    }
    schema.transitionPrograms.push_back(program);
  }
  for (const auto &row : source.crossPlans) {
    auto &schema = schemaFor(itemTypes.at(row.item));
    auto plan = row;
    plan.firstTarget = schema.crossTargets.size();
    plan.firstBin = schema.crossBins.size();
    appendSpan(schema.crossTargets, source.crossTargets, row.firstTarget,
               row.targetCount);
    appendSpan(schema.crossBins, source.crossBins, row.firstBin, row.binCount);
    schema.crossPlans.push_back(plan);
  }
  for (const auto &row : source.crossSelectorNodes) {
    uint64_t type = itemTypes.at(row.cross);
    auto &schema = schemaFor(type);
    auto node = row;
    node.firstOperand = schema.crossSelectorOperands.size();
    appendSpan(schema.crossSelectorOperands, source.crossSelectorOperands,
               row.firstOperand, row.operandCount);
    schema.crossSelectorNodes.push_back(node);
    owners.emplace(std::make_pair(Owner::Selector, row.id), type);
  }
  for (const auto &row : source.crossPredicateNodes)
    schemaFor(owners.at(std::make_pair(Owner::Selector, row.selector)))
        .crossPredicateNodes.push_back(row);
  for (const auto &row : source.functionalTupleSets) {
    auto &schema = schemaFor(itemTypes.at(row.cross));
    auto set = row;
    set.firstTuple = schema.functionalTupleSetTuples.size();
    for (uint32_t i = 0; i != row.tupleCount; ++i) {
      auto tuple = source.functionalTupleSetTuples[row.firstTuple + i];
      const uint32_t first = tuple.firstComponent;
      tuple.firstComponent = schema.functionalTupleSetComponents.size();
      appendSpan(schema.functionalTupleSetComponents,
                 source.functionalTupleSetComponents, first,
                 tuple.componentCount);
      schema.functionalTupleSetTuples.push_back(tuple);
    }
    schema.functionalTupleSets.push_back(set);
  }
  for (const auto &row : source.functionalExpressions)
    schemaFor(owners.at({row.ownerKind, row.owner}))
        .functionalExpressions.push_back(row);
  for (const auto &row : source.functionalOptionPlans) {
    uint64_t type =
        row.ownerKind == FunctionalConfigurationOptionOwnerKind::Group
            ? row.owner
            : itemTypes.at(row.owner);
    schemaFor(type).functionalOptionPlans.push_back(row);
  }
  for (auto &[id, schema] : types) {
    sortByKey(schema->functionalTypes.begin(), schema->functionalTypes.end(),
              [](const auto &type) { return type.id; });
    schema->schemaFingerprint = computeSchemaFingerprint(*schema);
    prepared->types.emplace(id, std::move(schema));
  }
  coverage.constructionSchemas = std::move(prepared);
  return OBELISK_RT_OK;
}

obelisk_rt_status indexFunctionalConfigurations(CoverageState &coverage) {
  if (coverage.configurationIndexSchema != coverage.schema.get()) {
    IndexMap<uint64_t, std::vector<obelisk::coverage::Digest>> prepared;
    IndexSet<FunctionalCoverageTypeKey> keys;
    for (const auto &row : coverage.schema->functionalConfigurations) {
      prepared[row.type].push_back(row.configuration);
      keys.insert({row.type, row.configuration});
    }
    coverage.configurationsByType = std::move(prepared);
    coverage.configurationKeys = std::move(keys);
    coverage.payloadlessConfigurations = {};
    coverage.configurationIndexSchema = coverage.schema.get();
    coverage.indexedConfigurations =
        coverage.schema->functionalConfigurations.size();
  }
  // Incremental indexing visits each appended configuration exactly once.
  // Advance only after both insertions succeed, so allocation failure is
  // retryable.
  while (coverage.indexedConfigurations <
         coverage.schema->functionalConfigurations.size()) {
    const auto &row =
        coverage.schema
            ->functionalConfigurations[coverage.indexedConfigurations];
    coverage.configurationKeys.insert({row.type, row.configuration});
    auto &configurations = coverage.configurationsByType[row.type];
    if (configurations.empty() || configurations.back() != row.configuration)
      configurations.push_back(row.configuration);
    ++coverage.indexedConfigurations;
  }
  return OBELISK_RT_OK;
}

obelisk_rt_status resolveStaticFunctionalType(
    obelisk_rt_context *context, obelisk::coverage::Database &destination,
    uint64_t typeID, const obelisk_rt_functional_value_v1 *expressions,
    uint64_t expressionCount, obelisk::coverage::Digest &resolvedConfiguration,
    const FunctionalCoverageConstructionSchemas &templates,
    PreparedFunctionalConstruction *prepared = nullptr,
    const obelisk::coverage::Database *sourceSchema = nullptr) {
  using namespace obelisk::coverage;
  const Database &schema = sourceSchema ? *sourceSchema : destination;
  FunctionalIdentityReservations itemIDs{templates.itemIDs, {}},
      binIDs{templates.binIDs, {}}, valueSetIDs{templates.valueSetIDs, {}};

  std::vector<const FunctionalItem *> templateItems;
  for (const auto &item : schema.functionalItems)
    if (item.type == typeID)
      templateItems.push_back(&item);
  sortByKey(templateItems.begin(), templateItems.end(),
            [](const auto *item) { return std::tie(item->ordinal, item->id); });
  IndexMap<uint64_t, uint32_t> itemOrdinals;
  for (const auto *item : templateItems)
    itemOrdinals.emplace(item->id, item->ordinal);

  std::vector<ResolvedFunctionalItem> resolvedItemRows;
  IndexMap<uint64_t, size_t> resolvedItemPositions;
  IndexMap<uint64_t, uint64_t> resolvedItems;
  for (const FunctionalItem *item : templateItems) {
    uint64_t id = resolvedFunctionalID("functional.resolved.item", typeID,
                                       item->id, 0, itemIDs);
    if (!id)
      return OBELISK_RT_INVALID_DESIGN;
    itemIDs.insert(id);
    resolvedItemPositions.emplace(id, resolvedItemRows.size());
    resolvedItemRows.push_back({typeID,
                                {},
                                id,
                                item->id,
                                item->name,
                                item->kind,
                                item->flags,
                                item->goal,
                                item->weight,
                                item->ordinal,
                                item->hierarchy});
    resolvedItems.emplace(item->id, id);
  }

  std::vector<const FunctionalBin *> templateBins;
  for (const auto &bin : schema.functionalBins)
    if (resolvedItems.count(bin.item))
      templateBins.push_back(&bin);
  sortByKey(templateBins.begin(), templateBins.end(), [&](const auto *bin) {
    return std::make_tuple(itemOrdinals.at(bin->item), bin->ordinal, bin->id);
  });

  struct PendingSet {
    ResolvedFunctionalValueSet set;
    std::vector<ResolvedFunctionalValueAtom> atoms;
    std::vector<ResolvedFunctionalValueLimb> limbs;
  };
  struct PendingAtom {
    ResolvedFunctionalValueAtom atom;
    std::vector<ResolvedFunctionalValueLimb> limbs;
  };
  struct PendingDefaultArray {
    const FunctionalBin *bin = nullptr;
    const FunctionalBinPlan *plan = nullptr;
    uint64_t item = 0;
    uint64_t firstBinID = 0;
    uint32_t groupIndex = 0;
  };
  std::vector<ResolvedFunctionalBin> resolvedBinRows;
  std::vector<ResolvedFunctionalBin> resolvedCrossBinRows;
  std::vector<ResolvedFunctionalBinPlan> resolvedPlanRows;
  std::vector<ResolvedFunctionalBinGroup> resolvedGroupRows;
  std::vector<ResolvedTransitionStep> resolvedTransitionStepRows;
  std::vector<ResolvedTransitionAlternative> resolvedTransitionAlternativeRows;
  std::vector<ResolvedTransitionExpansionGroup>
      resolvedTransitionExpansionGroupRows;
  std::vector<PendingSet> pendingSets;
  std::vector<PendingDefaultArray> pendingDefaultArrays;
  IndexMap<uint64_t, uint32_t> nextResolvedBinOrdinal;
  IndexSet<uint64_t> transitionAlternativeIDs;
  IndexMap<uint64_t, const obelisk_rt_functional_value_v1 *> expressionValues;
  for (uint64_t i = 0; i != expressionCount; ++i)
    expressionValues.emplace(expressions[i].id, &expressions[i]);
  IndexMap<uint64_t, const FunctionalExpression *> expressionSchemas;
  IndexMap<uint64_t, const FunctionalExpression *> coverpointSamples;
  IndexMap<uint64_t, std::vector<const FunctionalExpression *>>
      binWithExpressions;
  for (const auto &row : schema.functionalExpressions) {
    expressionSchemas.emplace(row.id, &row);
    if (row.ownerKind == FunctionalExpressionOwnerKind::Item &&
        row.role == FunctionalExpressionRole::CoverpointSample)
      coverpointSamples.emplace(row.owner, &row);
    if (row.ownerKind == FunctionalExpressionOwnerKind::Bin &&
        row.role == FunctionalExpressionRole::BinWith)
      binWithExpressions[row.owner].push_back(&row);
  }
  for (auto &[bin, predicates] : binWithExpressions) {
    (void)bin;
    sortByKey(predicates.begin(), predicates.end(),
              [](const auto *row) { return row->ownerOrdinal; });
  }
  IndexMap<uint64_t, const CrossPlan *> templateCrossPlans;
  for (const auto &row : schema.crossPlans)
    templateCrossPlans.emplace(row.item, &row);
  IndexMap<uint64_t, const FunctionalBinPlan *> binPlans;
  for (const auto &row : schema.functionalBinPlans)
    binPlans.emplace(row.bin, &row);
  IndexMap<uint64_t, const TransitionProgram *> transitionPrograms;
  for (const auto &row : schema.transitionPrograms)
    transitionPrograms.emplace(row.bin, &row);
  IndexMap<uint64_t, const FunctionalValueSet *> valueSets;
  for (const auto &row : schema.functionalValueSets)
    valueSets.emplace(row.id, &row);
  IndexSet<uint64_t> suppressAutomaticBins;
  for (const auto *bin : templateBins)
    if ((bin->flags & FunctionalBinAutomatic) ||
        ((bin->kind == FunctionalBinKind::State ||
          bin->kind == FunctionalBinKind::Transition) &&
         !(bin->flags & (FunctionalBinIgnore | FunctionalBinIllegal))))
      suppressAutomaticBins.insert(bin->item);
  auto lookup = [](const auto &index, uint64_t id) {
    auto entry = index.find(id);
    return entry == index.end() ? nullptr : entry->second;
  };
  auto expressionValue =
      [&](uint64_t id) -> const obelisk_rt_functional_value_v1 * {
    auto entry = expressionValues.find(id);
    return entry == expressionValues.end() ? nullptr : entry->second;
  };
  auto expressionSchema = [&](uint64_t id) -> const FunctionalExpression * {
    auto entry = expressionSchemas.find(id);
    return entry == expressionSchemas.end() ? nullptr : entry->second;
  };
  auto decodeRealBound = [&](const obelisk_rt_functional_value_v1 &value,
                             uint64_t expressionID,
                             bool &hasUnknown) -> std::optional<double> {
    if (isFunctionalRealValueKind(value.kind))
      return functionalRealValue(value);
    const auto *expression = expressionSchema(expressionID);
    FunctionalInteger integer;
    bool unknown = false;
    if (!expression ||
        expression->resultKind != FunctionalExpressionResultKind::Integral ||
        !decodeFunctionalInteger(value, expression->signedness, integer,
                                 unknown))
      return std::nullopt;
    hasUnknown |= unknown;
    long double magnitude = floatingFunctionalMagnitude(integer.magnitude);
    return static_cast<double>(integer.negative ? -magnitude : magnitude);
  };
  std::vector<FunctionalConfigurationOption> configurationOptionRows;
  IndexMap<uint64_t, uint64_t> itemAtLeast;
  IndexMap<uint64_t, uint64_t> itemAutoBinMax;
  IndexMap<uint64_t, bool> itemCrossRetainAutoBins;
  IndexMap<uint64_t, double> itemRealInterval;
  std::optional<uint64_t> groupAtLeast;
  std::optional<uint64_t> groupAutoBinMax;
  std::optional<bool> groupCrossRetainAutoBins;
  bool groupDistributeFirst = false;
  std::optional<size_t> distributeFirstRow;
  double groupRealInterval = 1.0;
  for (const FunctionalOptionPlan &plan : schema.functionalOptionPlans) {
    const bool group =
        plan.ownerKind == FunctionalConfigurationOptionOwnerKind::Group;
    auto resolvedOwner = resolvedItems.end();
    if (group) {
      if (plan.owner != typeID)
        continue;
    } else {
      resolvedOwner = resolvedItems.find(plan.owner);
      if (resolvedOwner == resolvedItems.end())
        continue;
    }
    const FunctionalExpression *expression = expressionSchema(plan.expression);
    const obelisk_rt_functional_value_v1 *encoded =
        expressionValue(plan.expression);
    // An instance name is mutable metadata, not resolved schema. Validate its
    // constructor result here, but do not add it to the configuration rows or
    // fingerprint. covergroup_create copies the value into the live instance.
    if (plan.option == FunctionalConfigurationOptionKind::Name) {
      if (!group || !expression || !encoded ||
          expression->resultKind != FunctionalExpressionResultKind::String ||
          encoded->kind != OBELISK_RT_FUNCTIONAL_VALUE_STRING)
        return OBELISK_RT_INVALID_DESIGN;
      continue;
    }
    if (plan.option == FunctionalConfigurationOptionKind::Comment) {
      if (!expression || !encoded ||
          expression->resultKind != FunctionalExpressionResultKind::String ||
          encoded->kind != OBELISK_RT_FUNCTIONAL_VALUE_STRING)
        return OBELISK_RT_INVALID_DESIGN;
      std::string comment;
      obelisk_rt_status status =
          copyManagedCoverageString(encoded->payload, comment);
      if (status != OBELISK_RT_OK)
        return status;
      if (!obelisk::coverage::isValidUtf8(comment))
        return OBELISK_RT_INVALID_DESIGN;
      configurationOptionRows.push_back(
          {typeID,
           {},
           group ? typeID : resolvedOwner->second,
           plan.ownerKind,
           plan.scope,
           plan.option,
           FunctionalConfigurationValueKind::String,
           0,
           0,
           std::move(comment)});
      continue;
    }
    if (plan.option == FunctionalConfigurationOptionKind::RealInterval) {
      if (!expression || !encoded || !isRealResultKind(expression->resultKind))
        return OBELISK_RT_INVALID_DESIGN;
      auto real = functionalRealValue(*encoded);
      if (!real)
        return OBELISK_RT_INVALID_DESIGN;
      double interval = *real;
      uint64_t intervalBits = 0;
      std::memcpy(&intervalBits, &interval, sizeof(intervalBits));
      if (!std::isfinite(interval) || interval <= 0.0)
        return OBELISK_RT_INVALID_DESIGN;
      const uint64_t resolvedOwnerID = group ? typeID : resolvedOwner->second;
      configurationOptionRows.push_back(
          {typeID,
           {},
           resolvedOwnerID,
           plan.ownerKind,
           plan.scope,
           plan.option,
           FunctionalConfigurationValueKind::RealBits,
           0,
           intervalBits,
           {}});
      if (group)
        groupRealInterval = interval;
      else
        itemRealInterval.emplace(resolvedOwnerID, interval);
      continue;
    }
    const bool booleanOption =
        plan.option == FunctionalConfigurationOptionKind::DetectOverlap ||
        plan.option == FunctionalConfigurationOptionKind::PerInstance ||
        plan.option == FunctionalConfigurationOptionKind::GetInstCoverage ||
        plan.option == FunctionalConfigurationOptionKind::MergeInstances ||
        plan.option == FunctionalConfigurationOptionKind::DistributeFirst ||
        plan.option == FunctionalConfigurationOptionKind::Strobe ||
        plan.option == FunctionalConfigurationOptionKind::CrossRetainAutoBins;
    const FunctionalExpressionResultKind expectedResult =
        booleanOption ? FunctionalExpressionResultKind::Boolean
                      : FunctionalExpressionResultKind::Integral;
    if (!expression || !encoded || expression->resultKind != expectedResult)
      return OBELISK_RT_INVALID_DESIGN;
    uint64_t value = 0;
    FunctionalConfigurationValueKind valueKind =
        FunctionalConfigurationValueKind::Unsigned;
    if (booleanOption) {
      value = functionalBooleanTrue(*encoded) ? 1 : 0;
      valueKind = FunctionalConfigurationValueKind::Boolean;
    } else {
      FunctionalInteger integer;
      bool unknown = false;
      if (!decodeFunctionalInteger(*encoded, expression->signedness, integer,
                                   unknown) ||
          unknown || integer.negative || integer.magnitude.size() > 1)
        return OBELISK_RT_INVALID_DESIGN;
      value = integer.magnitude.empty() ? 0 : integer.magnitude.front();
    }
    switch (plan.option) {
    case FunctionalConfigurationOptionKind::Goal:
      if (value > 100)
        return OBELISK_RT_INVALID_DESIGN;
      break;
    case FunctionalConfigurationOptionKind::Weight:
      if (value > UINT32_MAX)
        return OBELISK_RT_OUT_OF_RESOURCES;
      break;
    case FunctionalConfigurationOptionKind::AutoBinMax:
      break;
    case FunctionalConfigurationOptionKind::CrossNumPrintMissing:
      break;
    case FunctionalConfigurationOptionKind::DetectOverlap:
      break;
    case FunctionalConfigurationOptionKind::AtLeast:
      break;
    case FunctionalConfigurationOptionKind::PerInstance:
    case FunctionalConfigurationOptionKind::GetInstCoverage:
    case FunctionalConfigurationOptionKind::MergeInstances:
    case FunctionalConfigurationOptionKind::DistributeFirst:
    case FunctionalConfigurationOptionKind::Strobe:
    case FunctionalConfigurationOptionKind::CrossRetainAutoBins:
      break;
    default:
      return OBELISK_RT_INVALID_DESIGN;
    }
    configurationOptionRows.push_back({typeID,
                                       {},
                                       group ? typeID : resolvedOwner->second,
                                       plan.ownerKind,
                                       plan.scope,
                                       plan.option,
                                       valueKind,
                                       0,
                                       value,
                                       {}});
    if (group &&
        plan.option == FunctionalConfigurationOptionKind::DistributeFirst) {
      if (plan.scope != FunctionalOptionScopeKind::Type)
        return OBELISK_RT_INVALID_DESIGN;
      groupDistributeFirst = value != 0;
      distributeFirstRow = configurationOptionRows.size() - 1;
    }
    if (group && plan.scope == FunctionalOptionScopeKind::Instance) {
      switch (plan.option) {
      case FunctionalConfigurationOptionKind::Goal:
      case FunctionalConfigurationOptionKind::Weight:
        break;
      case FunctionalConfigurationOptionKind::AtLeast:
        groupAtLeast = value;
        break;
      case FunctionalConfigurationOptionKind::AutoBinMax:
        groupAutoBinMax = value;
        break;
      case FunctionalConfigurationOptionKind::CrossNumPrintMissing:
        break;
      case FunctionalConfigurationOptionKind::DetectOverlap:
        break;
      case FunctionalConfigurationOptionKind::PerInstance:
      case FunctionalConfigurationOptionKind::GetInstCoverage:
        break;
      case FunctionalConfigurationOptionKind::CrossRetainAutoBins:
        groupCrossRetainAutoBins = value != 0;
        break;
      default:
        return OBELISK_RT_INVALID_DESIGN;
      }
      continue;
    }
    if (group)
      continue;
    // Type-scoped item options are retained as configuration rows and applied
    // only by cumulative merge calculations. They must not overwrite the
    // resolved instance-item profile used by get_inst_coverage().
    if (plan.scope == FunctionalOptionScopeKind::Type)
      continue;
    auto position = resolvedItemPositions.find(resolvedOwner->second);
    if (position == resolvedItemPositions.end())
      return OBELISK_RT_INVALID_DESIGN;
    auto *item = &resolvedItemRows[position->second];
    switch (plan.option) {
    case FunctionalConfigurationOptionKind::Goal:
      item->goal = static_cast<uint32_t>(value);
      break;
    case FunctionalConfigurationOptionKind::Weight:
      item->weight = static_cast<uint32_t>(value);
      break;
    case FunctionalConfigurationOptionKind::AtLeast:
      itemAtLeast.emplace(item->id, value);
      break;
    case FunctionalConfigurationOptionKind::AutoBinMax:
      itemAutoBinMax.emplace(item->id, value);
      break;
    case FunctionalConfigurationOptionKind::CrossNumPrintMissing:
      if (item->kind != FunctionalItemKind::Cross)
        return OBELISK_RT_INVALID_DESIGN;
      break;
    case FunctionalConfigurationOptionKind::DetectOverlap:
      if (item->kind != FunctionalItemKind::Coverpoint)
        return OBELISK_RT_INVALID_DESIGN;
      break;
    case FunctionalConfigurationOptionKind::CrossRetainAutoBins:
      if (item->kind != FunctionalItemKind::Cross)
        return OBELISK_RT_INVALID_DESIGN;
      itemCrossRetainAutoBins.emplace(item->id, value != 0);
      break;
    default:
      return OBELISK_RT_INVALID_DESIGN;
    }
  }
  // 19.5.1.1 captures the current distribution policy at construction. A
  // procedural update must override the elaborated definition, change the
  // configuration identity, and leave every previously resolved bin intact.
  auto overrides = context->coverage->typeOptions.find(typeID);
  if (overrides != context->coverage->typeOptions.end() &&
      overrides->second.distributeFirst)
    groupDistributeFirst = *overrides->second.distributeFirst;
  if (distributeFirstRow)
    configurationOptionRows[*distributeFirstRow].value = groupDistributeFirst;
  else
    configurationOptionRows.push_back(
        {typeID,
         {},
         typeID,
         FunctionalConfigurationOptionOwnerKind::Group,
         FunctionalOptionScopeKind::Type,
         FunctionalConfigurationOptionKind::DistributeFirst,
         FunctionalConfigurationValueKind::Boolean,
         0,
         groupDistributeFirst,
         {}});
  auto effectiveAtLeast = [&](uint64_t item) {
    auto found = itemAtLeast.find(item);
    if (found != itemAtLeast.end())
      return found->second;
    return groupAtLeast.value_or(1);
  };
  auto effectiveAutoBinMax = [&](uint64_t item) {
    auto found = itemAutoBinMax.find(item);
    if (found != itemAutoBinMax.end())
      return found->second;
    return groupAutoBinMax.value_or(64);
  };
  auto effectiveCrossRetainAutoBins = [&](uint64_t item) {
    auto found = itemCrossRetainAutoBins.find(item);
    if (found != itemCrossRetainAutoBins.end())
      return found->second;
    return groupCrossRetainAutoBins.value_or(true);
  };
  auto effectiveRealInterval = [&](uint64_t item) {
    auto found = itemRealInterval.find(item);
    return found == itemRealInterval.end() ? groupRealInterval : found->second;
  };
  auto unpackPendingAtoms = [](const PendingSet &pending) {
    std::vector<PendingAtom> result;
    result.reserve(pending.atoms.size());
    for (const auto &atom : pending.atoms) {
      PendingAtom unpacked;
      unpacked.atom = atom;
      unpacked.atom.firstLimb = 0;
      unpacked.limbs.insert(
          unpacked.limbs.end(), pending.limbs.begin() + atom.firstLimb,
          pending.limbs.begin() + atom.firstLimb + atom.limbCount);
      result.push_back(std::move(unpacked));
    }
    return result;
  };
  auto packPendingSet = [](const PendingSet &base,
                           const std::vector<PendingAtom> &atoms,
                           uint64_t setID, uint64_t binID,
                           uint32_t expansionOrdinal) {
    PendingSet result;
    result.set = base.set;
    result.set.id = setID;
    result.set.ownerBin = binID;
    result.set.ownerOrdinal = expansionOrdinal;
    result.set.firstAtom = 0;
    result.set.atomCount = static_cast<uint32_t>(atoms.size());
    for (uint32_t atomOrdinal = 0; atomOrdinal != atoms.size(); ++atomOrdinal) {
      PendingAtom unpacked = atoms[atomOrdinal];
      unpacked.atom.valueSet = setID;
      unpacked.atom.ordinal = atomOrdinal;
      unpacked.atom.firstLimb = static_cast<uint32_t>(result.limbs.size());
      for (uint32_t limbOrdinal = 0; limbOrdinal != unpacked.limbs.size();
           ++limbOrdinal) {
        unpacked.limbs[limbOrdinal].valueSet = setID;
        unpacked.limbs[limbOrdinal].atomOrdinal = atomOrdinal;
        unpacked.limbs[limbOrdinal].ordinal = limbOrdinal;
      }
      result.atoms.push_back(unpacked.atom);
      result.limbs.insert(result.limbs.end(), unpacked.limbs.begin(),
                          unpacked.limbs.end());
    }
    return result;
  };
  auto decodeTransitionBound = [&](uint64_t expressionID,
                                   uint64_t &bound) -> bool {
    const FunctionalExpression *boundExpression =
        expressionSchema(expressionID);
    const auto *boundValue = expressionValue(expressionID);
    FunctionalInteger integer;
    bool unknown = false;
    if (!boundExpression || !boundValue ||
        !decodeFunctionalInteger(*boundValue, boundExpression->signedness,
                                 integer, unknown) ||
        unknown || integer.negative || integer.magnitude.size() > 1)
      return false;
    bound = integer.magnitude.empty() ? 0 : integer.magnitude.front();
    return true;
  };
  auto resolveTransitionBounds = [&](const TransitionStep &step,
                                     uint64_t &lowerBound,
                                     uint64_t &upperBound) -> bool {
    const bool once = step.repetition == TransitionRepetitionKind::Once;
    if (once) {
      lowerBound = upperBound = 1;
      return !step.flags && !step.lowerExpression && !step.upperExpression &&
             step.lowerBound == 1 && step.upperBound == 1;
    }
    if (!step.flags) {
      if (step.lowerExpression || step.upperExpression)
        return false;
      lowerBound = step.lowerBound;
      upperBound = step.upperBound;
    } else if (step.flags == TransitionStepNeedsResolution) {
      if (!step.lowerExpression ||
          !decodeTransitionBound(step.lowerExpression, lowerBound))
        return false;
      if (step.upperExpression) {
        if (!decodeTransitionBound(step.upperExpression, upperBound))
          return false;
      } else
        upperBound = lowerBound;
    } else
      return false;
    return lowerBound && upperBound >= lowerBound &&
           (step.repetition != TransitionRepetitionKind::Consecutive ||
            upperBound != TransitionUnbounded);
  };
  auto resolveTransitionAtom =
      [&](const FunctionalValueAtom &atom, const FunctionalValueSet &set,
          bool wildcard,
          std::vector<PendingAtom> &result) -> obelisk_rt_status {
    if (atom.kind != FunctionalValueAtomKind::IntegralValue &&
        atom.kind != FunctionalValueAtomKind::IntegralRange)
      return OBELISK_RT_INVALID_DESIGN;
    const bool singleton = atom.kind == FunctionalValueAtomKind::IntegralValue;
    const bool lowerUnbounded = atom.flags & FunctionalValueAtomLowerUnbounded;
    const bool upperUnbounded = atom.flags & FunctionalValueAtomUpperUnbounded;
    if (singleton && (lowerUnbounded || upperUnbounded))
      return OBELISK_RT_INVALID_DESIGN;
    const auto *low =
        lowerUnbounded ? nullptr : expressionValue(atom.lowerExpression);
    const auto *high = upperUnbounded ? nullptr
                       : singleton    ? low
                                      : expressionValue(atom.upperExpression);
    const FunctionalExpression *lowExpression =
        lowerUnbounded ? nullptr : expressionSchema(atom.lowerExpression);
    const FunctionalExpression *highExpression =
        upperUnbounded ? nullptr
                       : expressionSchema(singleton ? atom.lowerExpression
                                                    : atom.upperExpression);
    if ((!lowerUnbounded && (!low || !lowExpression)) ||
        (!upperUnbounded && (!high || !highExpression)))
      return OBELISK_RT_INVALID_DESIGN;

    const bool targetSigned = set.signedness == CoverageSignedness::Signed;
    const FunctionalInteger targetMinimum =
        targetSigned ? functionalPowerOfTwo(set.bitWidth - 1, true)
                     : FunctionalInteger{};
    const FunctionalInteger targetMaximum =
        functionalMaximum(set.bitWidth, targetSigned);

    const uint32_t limbCount = functionalLimbCount(set.bitWidth);
    auto appendKnown = [&](const FunctionalInteger &lowValue,
                           const FunctionalInteger &highValue) {
      PendingAtom pending;
      pending.atom.kind = atom.kind;
      pending.atom.flags = atom.flags & (FunctionalValueAtomLowerInclusive |
                                         FunctionalValueAtomUpperInclusive);
      pending.atom.limbCount = limbCount;
      std::vector<uint64_t> lowLimbs =
          encodeFunctionalInteger(lowValue, set.bitWidth);
      std::vector<uint64_t> highLimbs =
          encodeFunctionalInteger(highValue, set.bitWidth);
      for (uint32_t limb = 0; limb != limbCount; ++limb)
        pending.limbs.push_back(
            {0, 0, limb, lowLimbs[limb], 0, highLimbs[limb], 0, 0});
      result.push_back(std::move(pending));
    };
    auto clipRange = [&](FunctionalInteger &lowValue,
                         FunctionalInteger &highValue,
                         const char *warningPrefix) {
      if (compareFunctionalInteger(lowValue, highValue) > 0) {
        std::fprintf(stderr,
                     "warning: %stransition bin range has reversed bounds "
                     "and is ignored\n",
                     warningPrefix);
        return false;
      }
      FunctionalInteger clippedLow =
          compareFunctionalInteger(lowValue, targetMinimum) < 0 ? targetMinimum
                                                                : lowValue;
      FunctionalInteger clippedHigh =
          compareFunctionalInteger(highValue, targetMaximum) > 0 ? targetMaximum
                                                                 : highValue;
      if (compareFunctionalInteger(clippedLow, clippedHigh) > 0) {
        std::fprintf(stderr,
                     "warning: %stransition bin range is outside the "
                     "effective coverpoint type and is ignored\n",
                     warningPrefix);
        return false;
      }
      if (compareFunctionalInteger(clippedLow, lowValue) != 0 ||
          compareFunctionalInteger(clippedHigh, highValue) != 0)
        std::fprintf(stderr,
                     "warning: %stransition bin range is clipped to the "
                     "effective coverpoint type\n",
                     warningPrefix);
      lowValue = std::move(clippedLow);
      highValue = std::move(clippedHigh);
      return true;
    };

    if (wildcard) {
      FunctionalWildcardCube lowCube, highCube;
      if ((!lowerUnbounded && !decodeFunctionalWildcardCube(
                                  *low, lowExpression->signedness, lowCube)) ||
          (!upperUnbounded && !decodeFunctionalWildcardCube(
                                  *high, highExpression->signedness, highCube)))
        return OBELISK_RT_INVALID_DESIGN;
      if (singleton) {
        std::vector<FunctionalWildcardCube> alternatives =
            castFunctionalWildcardCube(lowCube, set.bitWidth, set.signedness);
        if (alternatives.empty()) {
          std::fputs("warning: wildcard transition bin singleton is outside "
                     "the effective coverpoint type and is ignored\n",
                     stderr);
          return OBELISK_RT_OK;
        }
        if (alternatives.size() > ParseLimits{}.maxRecords - result.size())
          return OBELISK_RT_OUT_OF_RESOURCES;
        for (const FunctionalWildcardCube &cube : alternatives) {
          PendingAtom pending;
          pending.atom.kind = atom.kind;
          pending.atom.flags = atom.flags;
          pending.atom.limbCount = limbCount;
          for (uint32_t limb = 0; limb != limbCount; ++limb) {
            uint64_t mask = cube.wildcard[limb];
            uint64_t aval = cube.aval[limb] & ~mask;
            pending.limbs.push_back({0, 0, limb, aval, mask, aval, mask, mask});
          }
          result.push_back(std::move(pending));
        }
        return OBELISK_RT_OK;
      }
      FunctionalInteger lowValue =
          lowerUnbounded ? targetMinimum : wildcardCubeExtreme(lowCube, false);
      FunctionalInteger highValue =
          upperUnbounded ? targetMaximum : wildcardCubeExtreme(highCube, true);
      if (clipRange(lowValue, highValue, "wildcard "))
        appendKnown(lowValue, highValue);
      return OBELISK_RT_OK;
    }

    FunctionalInteger lowValue = targetMinimum;
    FunctionalInteger highValue = targetMaximum;
    bool lowUnknown = false, highUnknown = false;
    if ((!lowerUnbounded &&
         !decodeFunctionalInteger(*low, lowExpression->signedness, lowValue,
                                  lowUnknown)) ||
        (!upperUnbounded &&
         !decodeFunctionalInteger(*high, highExpression->signedness, highValue,
                                  highUnknown)))
      return OBELISK_RT_INVALID_DESIGN;
    if (lowUnknown || highUnknown) {
      std::fputs(singleton
                     ? "warning: transition bin singleton contains X or Z "
                       "and is ignored\n"
                     : "warning: transition bin range contains X or Z and is "
                       "ignored\n",
                 stderr);
      return OBELISK_RT_OK;
    }
    if (singleton) {
      if (!functionalIntegerRepresentable(lowValue, set.bitWidth,
                                          set.signedness)) {
        std::fputs("warning: transition bin singleton is outside the "
                   "effective coverpoint type and is ignored\n",
                   stderr);
        return OBELISK_RT_OK;
      }
      highValue = lowValue;
    } else if (!clipRange(lowValue, highValue, "")) {
      return OBELISK_RT_OK;
    }
    appendKnown(lowValue, highValue);
    return OBELISK_RT_OK;
  };
  for (const FunctionalBin *bin : templateBins) {
    auto item = resolvedItems.find(bin->item);
    if (item == resolvedItems.end())
      return OBELISK_RT_INVALID_DESIGN;
    const auto *sample = lookup(coverpointSamples, bin->item);
    const bool shortRealPoint =
        sample &&
        sample->resultKind == FunctionalExpressionResultKind::ShortReal;
    if (bin->kind == FunctionalBinKind::Cross) {
      uint64_t binID = resolvedFunctionalID("functional.resolved.bin", typeID,
                                            bin->id, 0, binIDs);
      if (!binID)
        return OBELISK_RT_INVALID_DESIGN;
      binIDs.insert(binID);
      resolvedCrossBinRows.push_back({typeID,
                                      {},
                                      binID,
                                      bin->id,
                                      item->second,
                                      bin->name,
                                      bin->kind,
                                      bin->flags,
                                      bin->ordinal,
                                      0,
                                      effectiveAtLeast(item->second),
                                      bin->hierarchy});
      continue;
    }
    if (bin->kind == FunctionalBinKind::Transition) {
      const auto *plan = lookup(binPlans, bin->id);
      const auto *program = lookup(transitionPrograms, bin->id);
      const bool defaultSequence =
          (bin->flags & FunctionalBinDefaultSequence) != 0;
      const bool scalar =
          plan && plan->arrayMode == FunctionalBinArrayMode::Scalar;
      const bool multiple =
          plan && plan->arrayMode == FunctionalBinArrayMode::Unsized;
      if (!plan || !program || plan->valueSet || (!scalar && !multiple) ||
          plan->cardinalityExpression || plan->arrayCardinality ||
          (scalar &&
           plan->distribution != FunctionalBinDistributionKind::None) ||
          (multiple &&
           plan->distribution != FunctionalBinDistributionKind::PerValue) ||
          (defaultSequence && !scalar) ||
          (defaultSequence ? program->alternativeCount != 0
                           : program->alternativeCount == 0) ||
          uint64_t{program->firstAlternative} + program->alternativeCount >
              schema.transitionAlternatives.size())
        return OBELISK_RT_INVALID_DESIGN;

      if (multiple) {
        struct ConcreteValue {
          PendingAtom atom;
          std::string label;
        };
        struct ConcreteStep {
          const FunctionalValueSet *templateSet = nullptr;
          std::vector<ConcreteValue> values;
          uint64_t lowerBound = 1;
          uint64_t upperBound = 1;
          TransitionRepetitionKind repetition = TransitionRepetitionKind::Once;
        };
        struct ConcreteSelection {
          uint32_t value = 0;
          uint64_t repetitions = 1;
        };
        struct ConcreteSequence {
          std::vector<ConcreteSelection> selections;
          std::string label;
        };

        const uint32_t firstBin = static_cast<uint32_t>(resolvedBinRows.size());
        const uint32_t firstPlan =
            static_cast<uint32_t>(resolvedPlanRows.size());
        uint32_t binExpansionOrdinal = 0;
        std::set<std::string> emittedSequenceLabels;
        for (uint32_t alternativeOrdinal = 0;
             alternativeOrdinal != program->alternativeCount;
             ++alternativeOrdinal) {
          const TransitionAlternative &templateAlternative =
              schema.transitionAlternatives[program->firstAlternative +
                                            alternativeOrdinal];
          if (templateAlternative.bin != bin->id ||
              templateAlternative.ordinal != alternativeOrdinal ||
              !templateAlternative.stepCount ||
              uint64_t{templateAlternative.firstStep} +
                      templateAlternative.stepCount >
                  schema.transitionSteps.size())
            return OBELISK_RT_INVALID_DESIGN;

          std::vector<ConcreteStep> concreteSteps;
          concreteSteps.reserve(templateAlternative.stepCount);
          bool alternativeSurvives = true;
          for (uint32_t stepOrdinal = 0;
               stepOrdinal != templateAlternative.stepCount; ++stepOrdinal) {
            const TransitionStep &templateStep =
                schema.transitionSteps[templateAlternative.firstStep +
                                       stepOrdinal];
            const bool once =
                templateStep.repetition == TransitionRepetitionKind::Once;
            const bool consecutive = templateStep.repetition ==
                                     TransitionRepetitionKind::Consecutive;
            uint64_t lowerBound = 1;
            uint64_t upperBound = 1;
            if (templateStep.bin != bin->id ||
                templateStep.alternativeOrdinal != alternativeOrdinal ||
                templateStep.ordinal != stepOrdinal ||
                (!once && !consecutive) ||
                !resolveTransitionBounds(templateStep, lowerBound, upperBound))
              return OBELISK_RT_INVALID_DESIGN;
            const auto *set = lookup(valueSets, templateStep.valueSet);
            if (!set || set->kind != FunctionalValueSetKind::Integral ||
                set->setExpression || !set->atomCount ||
                (set->flags & ~FunctionalValueSetNeedsResolution) ||
                uint64_t{set->firstAtom} + set->atomCount >
                    schema.functionalValueAtoms.size())
              return OBELISK_RT_INVALID_DESIGN;

            if (templateAlternative.stepCount == 1 && lowerBound == 1)
              return OBELISK_RT_INVALID_DESIGN;

            const bool targetSigned =
                set->signedness == CoverageSignedness::Signed;
            const uint32_t limbCount = functionalLimbCount(set->bitWidth);
            std::vector<PendingAtom> resolvedAtoms;
            for (uint32_t atomIndex = 0; atomIndex != set->atomCount;
                 ++atomIndex) {
              const FunctionalValueAtom &atom =
                  schema.functionalValueAtoms[set->firstAtom + atomIndex];
              obelisk_rt_status status = resolveTransitionAtom(
                  atom, *set, (bin->flags & FunctionalBinWildcard) != 0,
                  resolvedAtoms);
              if (status != OBELISK_RT_OK)
                return status;
            }
            std::vector<IntegralCoverageShape> shapes;
            shapes.reserve(resolvedAtoms.size());
            for (const PendingAtom &atom : resolvedAtoms) {
              if (atom.limbs.size() != limbCount)
                return OBELISK_RT_INVALID_DESIGN;
              IntegralCoverageShape shape;
              shape.interval =
                  atom.atom.kind == FunctionalValueAtomKind::IntegralRange;
              if (!shape.interval &&
                  atom.atom.kind != FunctionalValueAtomKind::IntegralValue)
                return OBELISK_RT_INVALID_DESIGN;
              if (shape.interval) {
                shape.low.resize(limbCount);
                shape.high.resize(limbCount);
              } else {
                shape.value.resize(limbCount);
                shape.wildcard.resize(limbCount);
              }
              for (uint32_t limb = 0; limb != limbCount; ++limb) {
                const auto &value = atom.limbs[limb];
                if (shape.interval) {
                  if (value.lowBval || value.highBval || value.wildcardMask)
                    return OBELISK_RT_INVALID_DESIGN;
                  shape.low[limb] = value.lowAval;
                  shape.high[limb] = value.highAval;
                } else {
                  if (value.lowBval & ~value.wildcardMask)
                    return OBELISK_RT_INVALID_DESIGN;
                  shape.value[limb] = value.lowAval & ~value.wildcardMask;
                  shape.wildcard[limb] = value.wildcardMask;
                }
              }
              if (targetSigned) {
                const uint32_t signBit = set->bitWidth - 1;
                const uint32_t signLimb = signBit / 64;
                const uint64_t signMask = uint64_t{1} << (signBit % 64);
                if (shape.interval) {
                  shape.low[signLimb] ^= signMask;
                  shape.high[signLimb] ^= signMask;
                } else {
                  shape.value[signLimb] ^= signMask;
                  shape.value[signLimb] &= ~shape.wildcard[signLimb];
                }
              }
              shapes.push_back(std::move(shape));
            }

            ConcreteStep concreteStep;
            concreteStep.templateSet = &*set;
            concreteStep.lowerBound = lowerBound;
            concreteStep.upperBound = upperBound;
            concreteStep.repetition = templateStep.repetition;
            std::vector<FunctionalMagnitude> values;
            obelisk_rt_status enumerationStatus =
                enumerateIntegralShapeUnion(shapes, set->bitWidth, values);
            if (enumerationStatus != OBELISK_RT_OK)
              return enumerationStatus;
            concreteStep.values.reserve(values.size());
            for (FunctionalMagnitude &orderedValue : values) {
              if (targetSigned)
                toggleFunctionalSignBias(orderedValue, set->bitWidth);
              PendingAtom concrete;
              concrete.atom.kind = FunctionalValueAtomKind::IntegralValue;
              concrete.atom.flags = FunctionalValueAtomLowerInclusive |
                                    FunctionalValueAtomUpperInclusive;
              concrete.atom.limbCount = limbCount;
              for (uint32_t limb = 0; limb != limbCount; ++limb)
                concrete.limbs.push_back({0, 0, limb, orderedValue[limb], 0,
                                          orderedValue[limb], 0, 0});
              FunctionalInteger integer = functionalIntegerFromBits(
                  orderedValue, set->bitWidth, set->signedness);
              std::string label = functionalMagnitudeDecimal(integer.magnitude);
              concreteStep.values.push_back(
                  {std::move(concrete),
                   integer.negative ? "-" + label : label});
            }
            const uint64_t repetitionDifference = upperBound - lowerBound;
            if (repetitionDifference >= ParseLimits{}.maxRecords)
              return OBELISK_RT_OUT_OF_RESOURCES;
            const uint64_t repetitionChoices = repetitionDifference + 1;
            if (!concreteStep.values.empty() &&
                concreteStep.values.size() >
                    ParseLimits{}.maxRecords / repetitionChoices)
              return OBELISK_RT_OUT_OF_RESOURCES;
            if (concreteStep.values.empty())
              alternativeSurvives = false;
            concreteSteps.push_back(std::move(concreteStep));
          }

          std::vector<ConcreteSequence> sequences(1);
          if (!alternativeSurvives)
            sequences.clear();
          for (const ConcreteStep &step : concreteSteps) {
            if (step.values.empty()) {
              sequences.clear();
              break;
            }
            const uint64_t repetitionChoices =
                step.upperBound - step.lowerBound + 1;
            const uint64_t stepChoices = step.values.size() * repetitionChoices;
            if (sequences.size() > ParseLimits{}.maxRecords / stepChoices)
              return OBELISK_RT_OUT_OF_RESOURCES;
            std::vector<ConcreteSequence> expanded;
            expanded.reserve(sequences.size() * stepChoices);
            // IEEE 1800-2023 19.5.2 presents the leftmost transition item as
            // the fastest-varying component (7=>11, 8=>11, ..., 10=>12).
            for (uint64_t repetitionOrdinal = 0;
                 repetitionOrdinal != repetitionChoices; ++repetitionOrdinal) {
              const uint64_t repetitions = step.lowerBound + repetitionOrdinal;
              for (uint32_t value = 0; value != step.values.size(); ++value) {
                std::string component = step.values[value].label;
                if (step.repetition == TransitionRepetitionKind::Consecutive) {
                  const std::string repetitionSuffix =
                      "[*" + std::to_string(repetitions) + "]";
                  if (component.size() >
                      ParseLimits{}.maxStringBytes - repetitionSuffix.size())
                    return OBELISK_RT_OUT_OF_RESOURCES;
                  component += repetitionSuffix;
                }
                for (const ConcreteSequence &prefix : sequences) {
                  ConcreteSequence sequence = prefix;
                  sequence.selections.push_back({value, repetitions});
                  const uint64_t separator = sequence.label.empty() ? 0 : 2;
                  if (component.size() > ParseLimits{}.maxStringBytes ||
                      sequence.label.size() >
                          ParseLimits{}.maxStringBytes - component.size() ||
                      separator > ParseLimits{}.maxStringBytes -
                                      sequence.label.size() - component.size())
                    return OBELISK_RT_OUT_OF_RESOURCES;
                  if (separator)
                    sequence.label += "=>";
                  sequence.label += component;
                  expanded.push_back(std::move(sequence));
                }
              }
            }
            sequences = std::move(expanded);
          }
          sequences.erase(
              std::remove_if(
                  sequences.begin(), sequences.end(),
                  [&](const ConcreteSequence &sequence) {
                    return !emittedSequenceLabels.insert(sequence.label).second;
                  }),
              sequences.end());
          if (sequences.size() > ParseLimits{}.maxRecords - binExpansionOrdinal)
            return OBELISK_RT_OUT_OF_RESOURCES;
          if (binExpansionOrdinal > UINT32_MAX - sequences.size())
            return OBELISK_RT_OUT_OF_RESOURCES;

          const uint32_t firstAlternative =
              static_cast<uint32_t>(resolvedTransitionAlternativeRows.size());
          for (uint32_t expansionOrdinal = 0;
               expansionOrdinal != sequences.size(); ++expansionOrdinal) {
            const ConcreteSequence &sequence = sequences[expansionOrdinal];
            const uint32_t binOrdinal = binExpansionOrdinal++;
            uint64_t binID = resolvedFunctionalID(
                "functional.resolved.bin", typeID, bin->id, binOrdinal, binIDs);
            if (!binID)
              return OBELISK_RT_INVALID_DESIGN;
            binIDs.insert(binID);
            if (sequence.label.size() > ParseLimits{}.maxStringBytes - 2)
              return OBELISK_RT_OUT_OF_RESOURCES;
            const std::string suffix = "[" + sequence.label + "]";
            if (bin->name.size() >
                    ParseLimits{}.maxStringBytes - suffix.size() ||
                bin->hierarchy.size() >
                    ParseLimits{}.maxStringBytes - suffix.size())
              return OBELISK_RT_OUT_OF_RESOURCES;
            resolvedBinRows.push_back({typeID,
                                       {},
                                       binID,
                                       bin->id,
                                       item->second,
                                       bin->name + suffix,
                                       bin->kind,
                                       bin->flags,
                                       nextResolvedBinOrdinal[item->second]++,
                                       binOrdinal,
                                       effectiveAtLeast(item->second),
                                       bin->hierarchy + suffix});
            resolvedPlanRows.push_back({typeID,
                                        {},
                                        binID,
                                        0,
                                        plan->iffExpression,
                                        0,
                                        0,
                                        FunctionalBinArrayMode::Unsized,
                                        FunctionalBinDistributionKind::PerValue,
                                        plan->flags});

            uint64_t alternativeID = resolvedFunctionalID(
                "functional.resolved.transition-alternative", typeID, binID, 0,
                transitionAlternativeIDs);
            if (!alternativeID)
              return OBELISK_RT_INVALID_DESIGN;
            transitionAlternativeIDs.insert(alternativeID);
            const uint32_t firstStep =
                static_cast<uint32_t>(resolvedTransitionStepRows.size());
            for (uint32_t stepOrdinal = 0; stepOrdinal != concreteSteps.size();
                 ++stepOrdinal) {
              const ConcreteStep &step = concreteSteps[stepOrdinal];
              const ConcreteSelection &selection =
                  sequence.selections[stepOrdinal];
              uint64_t setID = resolvedFunctionalID(
                  "functional.resolved.transition-value-set", typeID,
                  step.templateSet->id, binOrdinal, valueSetIDs);
              if (!setID)
                return OBELISK_RT_INVALID_DESIGN;
              valueSetIDs.insert(setID);
              PendingSet base;
              base.set = {typeID,
                          {},
                          0,
                          step.templateSet->id,
                          item->second,
                          0,
                          0,
                          step.templateSet->bitWidth,
                          step.templateSet->kind,
                          0,
                          step.templateSet->signedness,
                          binID,
                          0,
                          0,
                          stepOrdinal,
                          ResolvedFunctionalValueSetRole::TransitionStep};
              pendingSets.push_back(packPendingSet(
                  base, {step.values[selection.value].atom}, setID, binID, 0));
              resolvedTransitionStepRows.push_back({typeID,
                                                    {},
                                                    binID,
                                                    alternativeID,
                                                    setID,
                                                    selection.repetitions,
                                                    selection.repetitions,
                                                    stepOrdinal,
                                                    0});
            }
            resolvedTransitionAlternativeRows.push_back(
                {typeID,
                 {},
                 alternativeID,
                 binID,
                 alternativeOrdinal,
                 expansionOrdinal,
                 firstStep,
                 static_cast<uint32_t>(concreteSteps.size()),
                 0,
                 0});
          }
          resolvedTransitionExpansionGroupRows.push_back(
              {typeID,
               {},
               item->second,
               bin->id,
               alternativeOrdinal,
               firstAlternative,
               static_cast<uint32_t>(sequences.size()),
               0,
               sequences.size()});
        }
        for (uint32_t index = firstPlan; index != resolvedPlanRows.size();
             ++index)
          resolvedPlanRows[index].arrayCardinality = binExpansionOrdinal;
        resolvedGroupRows.push_back({typeID,
                                     {},
                                     item->second,
                                     bin->id,
                                     firstBin,
                                     binExpansionOrdinal,
                                     binExpansionOrdinal,
                                     FunctionalBinArrayMode::Unsized,
                                     FunctionalBinDistributionKind::PerValue,
                                     plan->flags,
                                     bin->kind});
        continue;
      }

      uint64_t binID = resolvedFunctionalID("functional.resolved.bin", typeID,
                                            bin->id, 0, binIDs);
      if (!binID)
        return OBELISK_RT_INVALID_DESIGN;
      binIDs.insert(binID);
      const uint32_t firstBin = static_cast<uint32_t>(resolvedBinRows.size());
      resolvedBinRows.push_back({typeID,
                                 {},
                                 binID,
                                 bin->id,
                                 item->second,
                                 bin->name,
                                 bin->kind,
                                 bin->flags,
                                 nextResolvedBinOrdinal[item->second]++,
                                 0,
                                 effectiveAtLeast(item->second),
                                 bin->hierarchy});
      resolvedPlanRows.push_back({typeID,
                                  {},
                                  binID,
                                  0,
                                  plan->iffExpression,
                                  0,
                                  0,
                                  FunctionalBinArrayMode::Scalar,
                                  FunctionalBinDistributionKind::None,
                                  plan->flags});
      resolvedGroupRows.push_back({typeID,
                                   {},
                                   item->second,
                                   bin->id,
                                   firstBin,
                                   1,
                                   0,
                                   FunctionalBinArrayMode::Scalar,
                                   FunctionalBinDistributionKind::None,
                                   plan->flags,
                                   bin->kind});

      if (defaultSequence)
        continue;

      uint32_t resolvedAlternativeOrdinal = 0;
      for (uint32_t alternativeOrdinal = 0;
           alternativeOrdinal != program->alternativeCount;
           ++alternativeOrdinal) {
        const TransitionAlternative &templateAlternative =
            schema.transitionAlternatives[program->firstAlternative +
                                          alternativeOrdinal];
        if (templateAlternative.bin != bin->id ||
            templateAlternative.ordinal != alternativeOrdinal ||
            !templateAlternative.stepCount ||
            uint64_t{templateAlternative.firstStep} +
                    templateAlternative.stepCount >
                schema.transitionSteps.size())
          return OBELISK_RT_INVALID_DESIGN;
        uint64_t alternativeID = resolvedFunctionalID(
            "functional.resolved.transition-alternative", typeID, bin->id,
            alternativeOrdinal, transitionAlternativeIDs);
        if (!alternativeID)
          return OBELISK_RT_INVALID_DESIGN;
        transitionAlternativeIDs.insert(alternativeID);
        const uint32_t firstStep =
            static_cast<uint32_t>(resolvedTransitionStepRows.size());
        std::vector<PendingSet> alternativeSets;
        std::vector<ResolvedTransitionStep> alternativeSteps;
        bool alternativeSurvives = true;
        for (uint32_t stepOrdinal = 0;
             stepOrdinal != templateAlternative.stepCount; ++stepOrdinal) {
          const TransitionStep &templateStep =
              schema
                  .transitionSteps[templateAlternative.firstStep + stepOrdinal];
          const bool once =
              templateStep.repetition == TransitionRepetitionKind::Once;
          const bool consecutive =
              templateStep.repetition == TransitionRepetitionKind::Consecutive;
          const bool gotoRepetition =
              templateStep.repetition == TransitionRepetitionKind::Goto;
          const bool nonconsecutive = templateStep.repetition ==
                                      TransitionRepetitionKind::Nonconsecutive;
          uint64_t lowerBound = 1;
          uint64_t upperBound = 1;
          if (templateStep.bin != bin->id ||
              templateStep.alternativeOrdinal != alternativeOrdinal ||
              templateStep.ordinal != stepOrdinal ||
              (!once && !consecutive && !gotoRepetition && !nonconsecutive) ||
              !resolveTransitionBounds(templateStep, lowerBound, upperBound))
            return OBELISK_RT_INVALID_DESIGN;
          const auto *set = lookup(valueSets, templateStep.valueSet);
          if (!set || set->kind != FunctionalValueSetKind::Integral ||
              set->setExpression || !set->atomCount ||
              (set->flags & ~FunctionalValueSetNeedsResolution))
            return OBELISK_RT_INVALID_DESIGN;
          uint64_t resolvedSetID =
              resolvedFunctionalID("functional.resolved.transition-value-set",
                                   typeID, set->id, 0, valueSetIDs);
          if (!resolvedSetID)
            return OBELISK_RT_INVALID_DESIGN;
          valueSetIDs.insert(resolvedSetID);
          PendingSet base;
          base.set = {typeID,
                      {},
                      resolvedSetID,
                      set->id,
                      item->second,
                      0,
                      0,
                      set->bitWidth,
                      set->kind,
                      0,
                      set->signedness,
                      binID,
                      0,
                      resolvedAlternativeOrdinal,
                      stepOrdinal,
                      ResolvedFunctionalValueSetRole::TransitionStep};
          std::vector<PendingAtom> resolvedAtoms;
          for (uint32_t atomIndex = 0; atomIndex != set->atomCount;
               ++atomIndex) {
            const FunctionalValueAtom &atom =
                schema.functionalValueAtoms[set->firstAtom + atomIndex];
            obelisk_rt_status status = resolveTransitionAtom(
                atom, *set, (bin->flags & FunctionalBinWildcard) != 0,
                resolvedAtoms);
            if (status != OBELISK_RT_OK)
              return status;
          }
          if (resolvedAtoms.empty()) {
            alternativeSurvives = false;
            break;
          }
          PendingSet pending =
              packPendingSet(base, resolvedAtoms, resolvedSetID, binID,
                             resolvedAlternativeOrdinal);
          alternativeSets.push_back(std::move(pending));
          // A single repeated item with a repetition-count-1 expansion has
          // no transition and is illegal under IEEE 1800-2023 19.5.2.  The
          // bounds can depend on constructor arguments, so enforce this while
          // resolving each instance as well as in database validation.
          if (templateAlternative.stepCount == 1 && lowerBound == 1)
            return OBELISK_RT_INVALID_DESIGN;
          alternativeSteps.push_back({typeID,
                                      {},
                                      binID,
                                      alternativeID,
                                      resolvedSetID,
                                      lowerBound,
                                      upperBound,
                                      stepOrdinal,
                                      0});
        }
        const uint32_t firstAlternative =
            static_cast<uint32_t>(resolvedTransitionAlternativeRows.size());
        if (!alternativeSurvives) {
          resolvedTransitionExpansionGroupRows.push_back({typeID,
                                                          {},
                                                          item->second,
                                                          bin->id,
                                                          alternativeOrdinal,
                                                          firstAlternative,
                                                          0,
                                                          0,
                                                          0});
          continue;
        }
        pendingSets.insert(pendingSets.end(),
                           std::make_move_iterator(alternativeSets.begin()),
                           std::make_move_iterator(alternativeSets.end()));
        resolvedTransitionStepRows.insert(resolvedTransitionStepRows.end(),
                                          alternativeSteps.begin(),
                                          alternativeSteps.end());
        resolvedTransitionAlternativeRows.push_back(
            {typeID,
             {},
             alternativeID,
             binID,
             alternativeOrdinal,
             0,
             firstStep,
             templateAlternative.stepCount,
             resolvedAlternativeOrdinal,
             0});
        resolvedTransitionExpansionGroupRows.push_back({typeID,
                                                        {},
                                                        item->second,
                                                        bin->id,
                                                        alternativeOrdinal,
                                                        firstAlternative,
                                                        1,
                                                        0,
                                                        1});
        ++resolvedAlternativeOrdinal;
      }
      if (!resolvedAlternativeOrdinal)
        resolvedBinRows[firstBin].flags |= FunctionalBinEmpty;
      continue;
    }
    if (bin->kind != FunctionalBinKind::State)
      return OBELISK_RT_INVALID_DESIGN;
    const auto *plan = lookup(binPlans, bin->id);
    const bool isDefault = (bin->flags & FunctionalBinDefault) != 0;
    if (!plan || (isDefault ? plan->valueSet != 0 : plan->valueSet == 0) ||
        (isDefault && plan->arrayMode == FunctionalBinArrayMode::Fixed) ||
        (plan->arrayMode == FunctionalBinArrayMode::Scalar &&
         (plan->arrayCardinality || plan->cardinalityExpression ||
          plan->distribution != FunctionalBinDistributionKind::None)) ||
        (plan->arrayMode == FunctionalBinArrayMode::Unsized &&
         (plan->arrayCardinality || plan->cardinalityExpression ||
          plan->distribution != FunctionalBinDistributionKind::PerValue)) ||
        (plan->arrayMode == FunctionalBinArrayMode::Fixed &&
         ((bool(plan->arrayCardinality) == bool(plan->cardinalityExpression)) ||
          plan->distribution != FunctionalBinDistributionKind::Uniform)))
      return OBELISK_RT_INVALID_DESIGN;
    uint64_t binID = resolvedFunctionalID("functional.resolved.bin", typeID,
                                          bin->id, 0, binIDs);
    if (!binID)
      return OBELISK_RT_INVALID_DESIGN;
    binIDs.insert(binID);
    uint64_t resolvedSetID = 0;
    std::optional<PendingSet> resolvedTemplateSet;
    static const std::vector<const FunctionalExpression *> noWithExpressions;
    auto withOwner = binWithExpressions.find(bin->id);
    const auto &withExpressions = withOwner == binWithExpressions.end()
                                      ? noWithExpressions
                                      : withOwner->second;
    if (withExpressions.size() > MaxFunctionalWithCandidates)
      return OBELISK_RT_OUT_OF_RESOURCES;
    std::vector<bool> withPredicateResults;
    withPredicateResults.reserve(withExpressions.size());
    for (uint32_t ordinal = 0; ordinal != withExpressions.size(); ++ordinal) {
      const FunctionalExpression &withExpression = *withExpressions[ordinal];
      const obelisk_rt_functional_value_v1 *predicate =
          expressionValue(withExpression.id);
      if (withExpression.ownerOrdinal != ordinal ||
          withExpression.ownerSubordinal != 0 ||
          withExpression.resultKind !=
              FunctionalExpressionResultKind::Boolean ||
          withExpression.evaluationPhase !=
              FunctionalExpressionEvaluationPhase::Constructor ||
          !predicate ||
          (predicate->kind != OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL &&
           predicate->kind != OBELISK_RT_FUNCTIONAL_VALUE_FOUR_STATE) ||
          predicate->bit_width != 1 || predicate->value_size != 1 ||
          !predicate->value || predicate->owner || predicate->payload ||
          predicate->argument_ref_kind ||
          (predicate->kind == OBELISK_RT_FUNCTIONAL_VALUE_FOUR_STATE) !=
              (predicate->unknown != nullptr))
        return OBELISK_RT_INVALID_DESIGN;
      withPredicateResults.push_back(functionalBooleanTrue(*predicate));
    }
    if (!isDefault) {
      const auto *set = lookup(valueSets, plan->valueSet);
      if (!set || (set->flags & ~FunctionalValueSetNeedsResolution))
        return OBELISK_RT_INVALID_DESIGN;
      resolvedSetID = resolvedFunctionalID("functional.resolved.value-set",
                                           typeID, set->id, 0, valueSetIDs);
      if (!resolvedSetID)
        return OBELISK_RT_INVALID_DESIGN;
      valueSetIDs.insert(resolvedSetID);
      PendingSet pending;
      pending.set = {typeID,
                     {},
                     resolvedSetID,
                     set->id,
                     item->second,
                     0,
                     set->atomCount,
                     set->bitWidth,
                     set->kind,
                     0,
                     set->signedness,
                     binID,
                     0,
                     0,
                     0,
                     ResolvedFunctionalValueSetRole::StateBin};
      if (set->setExpression) {
        const FunctionalExpression *setExpression =
            expressionSchema(set->setExpression);
        const obelisk_rt_functional_value_v1 *construction =
            expressionValue(set->setExpression);
        if (!setExpression || !construction ||
            setExpression->resultKind != FunctionalExpressionResultKind::Set ||
            construction->kind != OBELISK_RT_FUNCTIONAL_VALUE_MANAGED_CONTAINER)
          return OBELISK_RT_INVALID_DESIGN;
        const bool sourceReal =
            setExpression->signedness == CoverageSignedness::NotApplicable;
        const bool sourceFourState =
            setExpression->flags & FunctionalExpressionSetElementFourState;
        const uint32_t sourceKind = sourceReal        ? OBELISK_RT_ELEMENT_REAL
                                    : sourceFourState ? OBELISK_RT_ELEMENT_LOGIC
                                                      : OBELISK_RT_ELEMENT_BITS;
        CoverageSetSnapshot snapshot;
        obelisk_rt_status snapshotStatus = obelisk_rt_coverage_set_snapshot(
            context, construction->owner, setExpression->bitWidth, sourceKind,
            sourceFourState, ParseLimits{}.maxRecords, snapshot);
        if (snapshotStatus != OBELISK_RT_OK)
          return snapshotStatus;

        const bool wildcard = (bin->flags & FunctionalBinWildcard) != 0;
        const uint32_t targetLimbCount = functionalLimbCount(set->bitWidth);
        const ParseLimits limits;
        auto canAppend = [&](size_t current, uint64_t additional,
                             uint32_t recordSize) {
          const uint64_t maximum = std::min<uint64_t>(
              limits.maxRecords, limits.maxSectionBytes / recordSize);
          return current <= maximum && additional <= maximum - current;
        };
        auto elementDescriptor = [&](uint64_t ordinal) {
          obelisk_rt_functional_value_v1 value{};
          value.id = setExpression->id;
          value.bit_width = setExpression->bitWidth;
          value.value_size = snapshot.valueSize;
          value.value = snapshot.value.data() + ordinal * snapshot.valueSize;
          if (sourceFourState)
            value.unknown =
                snapshot.unknown.data() + ordinal * snapshot.valueSize;
          value.kind = sourceReal ? (setExpression->bitWidth == 32
                                         ? OBELISK_RT_FUNCTIONAL_VALUE_SHORTREAL
                                         : OBELISK_RT_FUNCTIONAL_VALUE_REAL)
                       : sourceFourState
                           ? OBELISK_RT_FUNCTIONAL_VALUE_FOUR_STATE
                           : OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL;
          return value;
        };
        auto decodeReal = [&](const obelisk_rt_functional_value_v1 &value,
                              double &result) {
          auto real = functionalRealValue(value);
          if (!real)
            return false;
          result = *real;
          return true;
        };
        auto appendInteger = [&](const FunctionalInteger &integer) {
          if (!functionalIntegerRepresentable(integer, set->bitWidth,
                                              set->signedness)) {
            std::fputs("warning: functional bin set element is outside the "
                       "effective coverpoint type and is ignored\n",
                       stderr);
            return OBELISK_RT_OK;
          }
          if (!canAppend(pending.atoms.size(), 1,
                         ResolvedFunctionalValueAtomRecord::Size) ||
              !canAppend(pending.limbs.size(), targetLimbCount,
                         ResolvedFunctionalValueLimbRecord::Size))
            return OBELISK_RT_OUT_OF_RESOURCES;
          const uint32_t atomOrdinal = pending.atoms.size();
          const uint32_t firstLimb = pending.limbs.size();
          std::vector<uint64_t> limbs =
              encodeFunctionalInteger(integer, set->bitWidth);
          for (uint32_t limb = 0; limb != targetLimbCount; ++limb)
            pending.limbs.push_back({resolvedSetID, atomOrdinal, limb,
                                     limbs[limb], 0, limbs[limb], 0, 0});
          pending.atoms.push_back({resolvedSetID, 0, 0, firstLimb,
                                   targetLimbCount, atomOrdinal,
                                   FunctionalValueAtomKind::IntegralValue,
                                   FunctionalValueAtomLowerInclusive |
                                       FunctionalValueAtomUpperInclusive});
          return OBELISK_RT_OK;
        };
        auto appendReal = [&](double value) {
          if (!std::isfinite(value)) {
            std::fputs("warning: functional bin set element is not a finite "
                       "real value and is ignored\n",
                       stderr);
            return OBELISK_RT_OK;
          }
          if (shortRealPoint &&
              (std::abs(value) > std::numeric_limits<float>::max() ||
               static_cast<double>(static_cast<float>(value)) != value)) {
            std::fputs("warning: functional bin set real element changes value "
                       "when converted to the effective coverpoint type and "
                       "is ignored\n",
                       stderr);
            return OBELISK_RT_OK;
          }
          if (!canAppend(pending.atoms.size(), 1,
                         ResolvedFunctionalValueAtomRecord::Size))
            return OBELISK_RT_OUT_OF_RESOURCES;
          uint64_t bits = 0;
          std::memcpy(&bits, &value, sizeof(bits));
          const uint32_t atomOrdinal = pending.atoms.size();
          pending.atoms.push_back({resolvedSetID, bits, bits, 0, 0, atomOrdinal,
                                   FunctionalValueAtomKind::RealInterval,
                                   FunctionalValueAtomLowerInclusive |
                                       FunctionalValueAtomUpperInclusive});
          return OBELISK_RT_OK;
        };

        for (uint64_t ordinal = 0; ordinal != snapshot.count; ++ordinal) {
          obelisk_rt_functional_value_v1 element = elementDescriptor(ordinal);
          if (set->kind == FunctionalValueSetKind::Real) {
            if (sourceReal) {
              double value = 0.0;
              if (!decodeReal(element, value))
                return OBELISK_RT_INVALID_DESIGN;
              if (obelisk_rt_status status = appendReal(value);
                  status != OBELISK_RT_OK)
                return status;
              continue;
            }
            FunctionalInteger integer;
            bool unknown = false;
            if (!decodeFunctionalInteger(element, setExpression->signedness,
                                         integer, unknown))
              return OBELISK_RT_INVALID_DESIGN;
            if (unknown) {
              std::fputs("warning: functional bin set element contains X or "
                         "Z and is ignored\n",
                         stderr);
              continue;
            }
            long double magnitude =
                floatingFunctionalMagnitude(integer.magnitude);
            double value =
                static_cast<double>(integer.negative ? -magnitude : magnitude);
            FunctionalInteger roundTrip;
            if (!functionalIntegerFromReal(value, roundTrip) ||
                roundTrip.negative != integer.negative ||
                roundTrip.magnitude != integer.magnitude) {
              std::fputs("warning: functional bin set integral element "
                         "changes value when converted to the effective "
                         "coverpoint type and is ignored\n",
                         stderr);
              continue;
            }
            if (obelisk_rt_status status = appendReal(value);
                status != OBELISK_RT_OK)
              return status;
            continue;
          }
          if (set->kind != FunctionalValueSetKind::Integral)
            return OBELISK_RT_INVALID_DESIGN;
          if (sourceReal) {
            double value = 0.0;
            if (!decodeReal(element, value))
              return OBELISK_RT_INVALID_DESIGN;
            FunctionalInteger integer;
            if (!functionalIntegerFromReal(value, integer)) {
              std::fputs("warning: functional bin set real element changes "
                         "value when converted to the effective coverpoint "
                         "type and is ignored\n",
                         stderr);
              continue;
            }
            if (obelisk_rt_status status = appendInteger(integer);
                status != OBELISK_RT_OK)
              return status;
            continue;
          }
          if (!wildcard) {
            FunctionalInteger integer;
            bool unknown = false;
            if (!decodeFunctionalInteger(element, setExpression->signedness,
                                         integer, unknown))
              return OBELISK_RT_INVALID_DESIGN;
            if (unknown) {
              std::fputs("warning: functional bin set element contains X or "
                         "Z and is ignored\n",
                         stderr);
              continue;
            }
            if (obelisk_rt_status status = appendInteger(integer);
                status != OBELISK_RT_OK)
              return status;
            continue;
          }
          FunctionalWildcardCube sourceCube;
          if (!decodeFunctionalWildcardCube(element, setExpression->signedness,
                                            sourceCube))
            return OBELISK_RT_INVALID_DESIGN;
          // Casting widens each candidate to the effective point width. Check
          // that even one physical result fits the exact-v1 database limits
          // before the helper allocates its target-width planes; the exact
          // candidate count is checked below after conversion.
          if (!canAppend(0, targetLimbCount,
                         ResolvedFunctionalValueLimbRecord::Size))
            return OBELISK_RT_OUT_OF_RESOURCES;
          std::vector<FunctionalWildcardCube> alternatives =
              castFunctionalWildcardCube(sourceCube, set->bitWidth,
                                         set->signedness);
          if (alternatives.empty()) {
            std::fputs("warning: wildcard functional bin set element is "
                       "outside the effective coverpoint type and is ignored\n",
                       stderr);
            continue;
          }
          if (!canAppend(pending.atoms.size(), alternatives.size(),
                         ResolvedFunctionalValueAtomRecord::Size) ||
              (targetLimbCount &&
               alternatives.size() > UINT64_MAX / uint64_t{targetLimbCount}) ||
              !canAppend(pending.limbs.size(),
                         alternatives.size() * uint64_t{targetLimbCount},
                         ResolvedFunctionalValueLimbRecord::Size))
            return OBELISK_RT_OUT_OF_RESOURCES;
          for (const FunctionalWildcardCube &cube : alternatives) {
            const uint32_t atomOrdinal = pending.atoms.size();
            const uint32_t firstLimb = pending.limbs.size();
            for (uint32_t limb = 0; limb != targetLimbCount; ++limb) {
              const uint64_t mask = cube.wildcard[limb];
              const uint64_t aval = cube.aval[limb] & ~mask;
              pending.limbs.push_back({resolvedSetID, atomOrdinal, limb, aval,
                                       mask, aval, mask, mask});
            }
            pending.atoms.push_back({resolvedSetID, 0, 0, firstLimb,
                                     targetLimbCount, atomOrdinal,
                                     FunctionalValueAtomKind::IntegralValue,
                                     FunctionalValueAtomLowerInclusive |
                                         FunctionalValueAtomUpperInclusive});
          }
        }
      } else {
        for (uint32_t atomIndex = 0; atomIndex != set->atomCount; ++atomIndex) {
          const auto &atom =
              schema.functionalValueAtoms[set->firstAtom + atomIndex];
          uint32_t firstLimb = pending.limbs.size();
          if (set->flags & FunctionalValueSetNeedsResolution) {
            const bool lowerUnbounded =
                atom.flags & FunctionalValueAtomLowerUnbounded;
            const bool upperUnbounded =
                atom.flags & FunctionalValueAtomUpperUnbounded;
            const auto *low = lowerUnbounded
                                  ? nullptr
                                  : expressionValue(atom.lowerExpression);
            const bool singleton =
                atom.kind == FunctionalValueAtomKind::IntegralValue ||
                (atom.kind == FunctionalValueAtomKind::RealInterval &&
                 !atom.upperExpression && !upperUnbounded);
            const auto *high = upperUnbounded ? nullptr
                               : singleton
                                   ? low
                                   : expressionValue(atom.upperExpression);
            if ((!lowerUnbounded && !low) || (!upperUnbounded && !high))
              return OBELISK_RT_INVALID_DESIGN;
            if (set->kind == FunctionalValueSetKind::Real) {
              if (atom.kind != FunctionalValueAtomKind::RealInterval)
                return OBELISK_RT_INVALID_DESIGN;
              uint64_t lowBits = 0, highBits = 0;
              bool hasUnknown = false;
              auto lower =
                  lowerUnbounded
                      ? std::optional<double>(0)
                      : decodeRealBound(*low, atom.lowerExpression, hasUnknown);
              auto upper = upperUnbounded ? std::optional<double>(0)
                           : singleton
                               ? lower
                               : decodeRealBound(*high, atom.upperExpression,
                                                 hasUnknown);
              if (!lower || !upper)
                return OBELISK_RT_INVALID_DESIGN;
              if (hasUnknown) {
                std::fputs("warning: functional bin real element contains X "
                           "or Z and is ignored\n",
                           stderr);
                continue;
              }
              double lowValue = *lower, highValue = *upper;
              if (shortRealPoint && singleton &&
                  (std::abs(lowValue) > std::numeric_limits<float>::max() ||
                   static_cast<double>(static_cast<float>(lowValue)) !=
                       lowValue)) {
                std::fputs("warning: functional bin real element changes value "
                           "when converted to the effective coverpoint type "
                           "and is ignored\n",
                           stderr);
                continue;
              }
              const uint32_t toleranceFlags =
                  atom.flags & (FunctionalValueAtomAbsoluteTolerance |
                                FunctionalValueAtomRelativeTolerance);
              if (toleranceFlags) {
                double delta = highValue;
                if (toleranceFlags & FunctionalValueAtomRelativeTolerance)
                  delta = lowValue * delta / 100.0;
                highValue = lowValue + delta;
                lowValue -= delta;
                if (highValue < lowValue)
                  std::swap(lowValue, highValue);
              }
              if ((!lowerUnbounded && !std::isfinite(lowValue)) ||
                  (!upperUnbounded && !std::isfinite(highValue)) ||
                  (!lowerUnbounded && !upperUnbounded && lowValue > highValue))
                return OBELISK_RT_INVALID_DESIGN;
              if (shortRealPoint && !singleton) {
                // Intersect a real range with the binary32 domain. Each end
                // moves at most one ULP after rounding; never enumerate values.
                constexpr double limit = std::numeric_limits<float>::max();
                double originalLow = lowValue, originalHigh = highValue;
                if ((!lowerUnbounded && lowValue > limit) ||
                    (!upperUnbounded && highValue < -limit)) {
                  std::fputs("warning: functional bin real range is outside "
                             "the effective coverpoint type and is ignored\n",
                             stderr);
                  continue;
                }
                if (!lowerUnbounded) {
                  float clipped =
                      static_cast<float>(std::max(lowValue, -limit));
                  if (static_cast<double>(clipped) < lowValue)
                    clipped = std::nextafter(
                        clipped, std::numeric_limits<float>::infinity());
                  lowValue = clipped;
                }
                if (!upperUnbounded) {
                  float clipped =
                      static_cast<float>(std::min(highValue, limit));
                  if (static_cast<double>(clipped) > highValue)
                    clipped = std::nextafter(
                        clipped, -std::numeric_limits<float>::infinity());
                  highValue = clipped;
                }
                if (!lowerUnbounded && !upperUnbounded &&
                    lowValue > highValue) {
                  std::fputs(
                      "warning: functional bin real range has no values "
                      "in the effective coverpoint type and is ignored\n",
                      stderr);
                  continue;
                }
                if ((!lowerUnbounded && lowValue != originalLow) ||
                    (!upperUnbounded && highValue != originalHigh))
                  std::fputs("warning: functional bin real range is clipped "
                             "to the effective coverpoint type\n",
                             stderr);
              }
              std::memcpy(&lowBits, &lowValue, sizeof(lowBits));
              std::memcpy(&highBits, &highValue, sizeof(highBits));
              uint32_t resolvedOrdinal = pending.atoms.size();
              pending.atoms.push_back(
                  {resolvedSetID, lowBits, highBits, firstLimb, 0,
                   resolvedOrdinal, atom.kind,
                   atom.flags & (FunctionalValueAtomLowerInclusive |
                                 FunctionalValueAtomUpperInclusive |
                                 FunctionalValueAtomLowerUnbounded |
                                 FunctionalValueAtomUpperUnbounded |
                                 FunctionalValueAtomRealRange)});
              continue;
            }
            if (set->kind != FunctionalValueSetKind::Integral ||
                (!lowerUnbounded && isFunctionalRealValueKind(low->kind)) ||
                (!upperUnbounded && isFunctionalRealValueKind(high->kind)))
              return OBELISK_RT_INVALID_DESIGN;
            uint32_t limbCount = (uint64_t{set->bitWidth} + 63) / 64;
            bool wildcard = (bin->flags & FunctionalBinWildcard) != 0;
            const bool targetSigned =
                set->signedness == CoverageSignedness::Signed;
            const FunctionalInteger targetMinimum =
                targetSigned ? functionalPowerOfTwo(set->bitWidth - 1, true)
                             : FunctionalInteger{};
            const FunctionalInteger targetMaximum =
                functionalMaximum(set->bitWidth, targetSigned);
            if (!wildcard) {
              const FunctionalExpression *lowExpression =
                  lowerUnbounded ? nullptr
                                 : expressionSchema(atom.lowerExpression);
              const FunctionalExpression *highExpression =
                  upperUnbounded
                      ? nullptr
                      : expressionSchema(
                            atom.kind == FunctionalValueAtomKind::IntegralValue
                                ? atom.lowerExpression
                                : atom.upperExpression);
              FunctionalInteger originalLow = targetMinimum;
              FunctionalInteger originalHigh = targetMaximum;
              bool lowUnknown = false, highUnknown = false;
              if ((!lowerUnbounded &&
                   (!lowExpression ||
                    !decodeFunctionalInteger(*low, lowExpression->signedness,
                                             originalLow, lowUnknown))) ||
                  (!upperUnbounded &&
                   (!highExpression ||
                    !decodeFunctionalInteger(*high, highExpression->signedness,
                                             originalHigh, highUnknown))))
                return OBELISK_RT_INVALID_DESIGN;

              if (lowUnknown || highUnknown) {
                std::fputs(atom.kind == FunctionalValueAtomKind::IntegralValue
                               ? "warning: functional bin singleton contains "
                                 "X or Z and is ignored\n"
                               : "warning: functional bin range contains X or "
                                 "Z and is ignored\n",
                           stderr);
                continue;
              }

              if (atom.kind == FunctionalValueAtomKind::IntegralValue) {
                if (!functionalIntegerRepresentable(originalLow, set->bitWidth,
                                                    set->signedness)) {
                  std::fputs("warning: functional bin singleton is outside the "
                             "effective coverpoint type and is ignored\n",
                             stderr);
                  continue;
                }
                originalHigh = originalLow;
              } else {
                // IEEE 1800-2017 11.4.13: an integral value range with a left
                // bound greater than its right bound is empty.
                if (compareFunctionalInteger(originalLow, originalHigh) > 0) {
                  std::fputs("warning: functional bin range has reversed "
                             "bounds and is ignored\n",
                             stderr);
                  continue;
                }
                FunctionalInteger clippedLow =
                    compareFunctionalInteger(originalLow, targetMinimum) < 0
                        ? targetMinimum
                        : originalLow;
                FunctionalInteger clippedHigh =
                    compareFunctionalInteger(originalHigh, targetMaximum) > 0
                        ? targetMaximum
                        : originalHigh;
                if (compareFunctionalInteger(clippedLow, clippedHigh) > 0) {
                  std::fputs("warning: functional bin range is outside the "
                             "effective coverpoint type and is ignored\n",
                             stderr);
                  continue;
                }
                if (compareFunctionalInteger(clippedLow, originalLow) != 0 ||
                    compareFunctionalInteger(clippedHigh, originalHigh) != 0)
                  std::fputs("warning: functional bin range is clipped to the "
                             "effective coverpoint type\n",
                             stderr);
                originalLow = std::move(clippedLow);
                originalHigh = std::move(clippedHigh);
              }

              std::vector<uint64_t> lowLimbs =
                  encodeFunctionalInteger(originalLow, set->bitWidth);
              std::vector<uint64_t> highLimbs =
                  encodeFunctionalInteger(originalHigh, set->bitWidth);
              uint32_t resolvedOrdinal = pending.atoms.size();
              for (uint32_t limbIndex = 0; limbIndex != limbCount; ++limbIndex)
                pending.limbs.push_back({resolvedSetID, resolvedOrdinal,
                                         limbIndex, lowLimbs[limbIndex], 0,
                                         highLimbs[limbIndex], 0, 0});
              pending.atoms.push_back(
                  {resolvedSetID, 0, 0, firstLimb, limbCount, resolvedOrdinal,
                   atom.kind,
                   atom.flags & (FunctionalValueAtomLowerInclusive |
                                 FunctionalValueAtomUpperInclusive)});
              continue;
            }
            const FunctionalExpression *lowExpression =
                lowerUnbounded ? nullptr
                               : expressionSchema(atom.lowerExpression);
            const FunctionalExpression *highExpression =
                upperUnbounded
                    ? nullptr
                    : expressionSchema(
                          atom.kind == FunctionalValueAtomKind::IntegralValue
                              ? atom.lowerExpression
                              : atom.upperExpression);
            FunctionalWildcardCube lowCube, highCube;
            if ((!lowerUnbounded &&
                 (!lowExpression ||
                  !decodeFunctionalWildcardCube(*low, lowExpression->signedness,
                                                lowCube))) ||
                (!upperUnbounded &&
                 (!highExpression ||
                  !decodeFunctionalWildcardCube(
                      *high, highExpression->signedness, highCube))))
              return OBELISK_RT_INVALID_DESIGN;
            if (atom.kind == FunctionalValueAtomKind::IntegralValue) {
              std::vector<FunctionalWildcardCube> alternatives =
                  castFunctionalWildcardCube(lowCube, set->bitWidth,
                                             set->signedness);
              if (alternatives.empty()) {
                std::fputs(
                    "warning: wildcard functional bin singleton is outside "
                    "the effective coverpoint type and is ignored\n",
                    stderr);
                continue;
              }
              for (const FunctionalWildcardCube &cube : alternatives) {
                uint32_t alternativeFirstLimb = pending.limbs.size();
                uint32_t resolvedOrdinal = pending.atoms.size();
                for (uint32_t limbIndex = 0; limbIndex != limbCount;
                     ++limbIndex) {
                  uint64_t wildcardMask = cube.wildcard[limbIndex];
                  uint64_t aval = cube.aval[limbIndex] & ~wildcardMask;
                  pending.limbs.push_back({resolvedSetID, resolvedOrdinal,
                                           limbIndex, aval, wildcardMask, aval,
                                           wildcardMask, wildcardMask});
                }
                pending.atoms.push_back(
                    {resolvedSetID, 0, 0, alternativeFirstLimb, limbCount,
                     resolvedOrdinal, atom.kind, atom.flags});
              }
              continue;
            }

            FunctionalInteger originalLow =
                lowerUnbounded ? targetMinimum
                               : wildcardCubeExtreme(lowCube, false);
            FunctionalInteger originalHigh =
                upperUnbounded ? targetMaximum
                               : wildcardCubeExtreme(highCube, true);
            if (compareFunctionalInteger(originalLow, originalHigh) > 0) {
              std::fputs("warning: wildcard functional bin range has reversed "
                         "bounds and is ignored\n",
                         stderr);
              continue;
            }
            FunctionalInteger clippedLow =
                compareFunctionalInteger(originalLow, targetMinimum) < 0
                    ? targetMinimum
                    : originalLow;
            FunctionalInteger clippedHigh =
                compareFunctionalInteger(originalHigh, targetMaximum) > 0
                    ? targetMaximum
                    : originalHigh;
            if (compareFunctionalInteger(clippedLow, clippedHigh) > 0) {
              std::fputs("warning: wildcard functional bin range is outside "
                         "the effective coverpoint type and is ignored\n",
                         stderr);
              continue;
            }
            if (compareFunctionalInteger(clippedLow, originalLow) != 0 ||
                compareFunctionalInteger(clippedHigh, originalHigh) != 0)
              std::fputs("warning: wildcard functional bin range is clipped to "
                         "the effective coverpoint type\n",
                         stderr);
            std::vector<uint64_t> lowLimbs =
                encodeFunctionalInteger(clippedLow, set->bitWidth);
            std::vector<uint64_t> highLimbs =
                encodeFunctionalInteger(clippedHigh, set->bitWidth);
            uint32_t resolvedOrdinal = pending.atoms.size();
            for (uint32_t limbIndex = 0; limbIndex != limbCount; ++limbIndex)
              pending.limbs.push_back({resolvedSetID, resolvedOrdinal,
                                       limbIndex, lowLimbs[limbIndex], 0,
                                       highLimbs[limbIndex], 0, 0});
            pending.atoms.push_back(
                {resolvedSetID, 0, 0, firstLimb, limbCount, resolvedOrdinal,
                 atom.kind,
                 atom.flags & (FunctionalValueAtomLowerInclusive |
                               FunctionalValueAtomUpperInclusive)});
            continue;
          }
          if (atom.lowerExpression || atom.upperExpression)
            return OBELISK_RT_INVALID_DESIGN;
          for (uint32_t limbIndex = 0; limbIndex != atom.limbCount;
               ++limbIndex) {
            const auto &limb =
                schema.functionalValueLimbs[atom.firstLimb + limbIndex];
            uint32_t resolvedOrdinal = pending.atoms.size();
            pending.limbs.push_back({resolvedSetID, resolvedOrdinal,
                                     limb.ordinal, limb.lowAval, limb.lowBval,
                                     limb.highAval, limb.highBval,
                                     limb.wildcardMask});
          }
          uint32_t resolvedOrdinal = pending.atoms.size();
          pending.atoms.push_back({resolvedSetID, atom.realLowBits,
                                   atom.realHighBits, firstLimb, atom.limbCount,
                                   resolvedOrdinal, atom.kind, atom.flags});
        }
      }
      pending.set.atomCount = static_cast<uint32_t>(pending.atoms.size());
      resolvedTemplateSet = std::move(pending);
    }

    if (isDefault && plan->arrayMode == FunctionalBinArrayMode::Unsized) {
      pendingDefaultArrays.push_back(
          {bin, plan, item->second, binID,
           static_cast<uint32_t>(resolvedGroupRows.size())});
      resolvedGroupRows.push_back(
          {typeID,
           {},
           item->second,
           bin->id,
           static_cast<uint32_t>(resolvedBinRows.size()),
           0,
           0,
           FunctionalBinArrayMode::Unsized,
           FunctionalBinDistributionKind::PerValue,
           plan->flags,
           bin->kind});
      continue;
    }

    std::vector<std::vector<PendingAtom>> expansionAtoms;
    std::vector<std::string> expansionLabels;
    bool expansionLabelsIncludeClosingBracket = false;
    uint32_t arrayCardinality = 0;
    if (plan->arrayMode == FunctionalBinArrayMode::Scalar) {
      arrayCardinality = 0;
      expansionAtoms.resize(1);
      if (resolvedTemplateSet)
        expansionAtoms.front() = unpackPendingAtoms(*resolvedTemplateSet);
      if (!withExpressions.empty()) {
        if (!resolvedTemplateSet ||
            resolvedTemplateSet->set.kind != FunctionalValueSetKind::Integral)
          return OBELISK_RT_INVALID_DESIGN;
        const uint32_t bitWidth = resolvedTemplateSet->set.bitWidth;
        const auto signedness = resolvedTemplateSet->set.signedness;
        const uint32_t limbCount = functionalLimbCount(bitWidth);
        std::vector<PendingAtom> selected;
        uint32_t candidateOrdinal = 0;
        for (const PendingAtom &source : expansionAtoms.front()) {
          if (source.limbs.size() != limbCount ||
              (source.atom.kind != FunctionalValueAtomKind::IntegralValue &&
               source.atom.kind != FunctionalValueAtomKind::IntegralRange) ||
              std::any_of(source.limbs.begin(), source.limbs.end(),
                          [](const auto &limb) {
                            return limb.lowBval || limb.highBval ||
                                   limb.wildcardMask;
                          }))
            return OBELISK_RT_INVALID_DESIGN;
          FunctionalMagnitude current(limbCount), high(limbCount);
          for (uint32_t limb = 0; limb != limbCount; ++limb) {
            current[limb] = source.limbs[limb].lowAval;
            high[limb] = source.limbs[limb].highAval;
          }
          if (signedness == CoverageSignedness::Signed) {
            toggleFunctionalSignBias(current, bitWidth);
            toggleFunctionalSignBias(high, bitWidth);
          }
          normalizeFunctionalMagnitude(current);
          normalizeFunctionalMagnitude(high);
          if (compareMagnitude(current, high) > 0)
            return OBELISK_RT_INVALID_DESIGN;
          while (true) {
            if (candidateOrdinal == withPredicateResults.size())
              return OBELISK_RT_INVALID_DESIGN;
            if (withPredicateResults[candidateOrdinal]) {
              FunctionalMagnitude encoded = current;
              encoded.resize(limbCount);
              if (signedness == CoverageSignedness::Signed)
                toggleFunctionalSignBias(encoded, bitWidth);
              PendingAtom atom;
              atom.atom.kind = FunctionalValueAtomKind::IntegralValue;
              atom.atom.flags = FunctionalValueAtomLowerInclusive |
                                FunctionalValueAtomUpperInclusive;
              atom.atom.limbCount = limbCount;
              for (uint32_t limb = 0; limb != limbCount; ++limb)
                atom.limbs.push_back(
                    {0, 0, limb, encoded[limb], 0, encoded[limb], 0, 0});
              selected.push_back(std::move(atom));
            }
            ++candidateOrdinal;
            if (compareMagnitude(current, high) == 0)
              break;
            current = addFunctionalMagnitudes(current, FunctionalMagnitude{1});
          }
        }
        if (candidateOrdinal != withPredicateResults.size())
          return OBELISK_RT_INVALID_DESIGN;
        expansionAtoms.front() = std::move(selected);
      }
    } else {
      if (!resolvedTemplateSet)
        return OBELISK_RT_INVALID_DESIGN;
      if (resolvedTemplateSet->set.kind == FunctionalValueSetKind::Real) {
        if (plan->arrayMode != FunctionalBinArrayMode::Unsized &&
            plan->arrayMode != FunctionalBinArrayMode::Fixed)
          return OBELISK_RT_INVALID_DESIGN;
        const double interval = effectiveRealInterval(item->second);
        if (!std::isfinite(interval) || interval <= 0.0)
          return OBELISK_RT_INVALID_DESIGN;

        auto decodeRealBits = [](uint64_t bits) {
          double value = 0.0;
          std::memcpy(&value, &bits, sizeof(value));
          return value;
        };
        auto encodeRealBits = [](double value) {
          uint64_t bits = 0;
          std::memcpy(&bits, &value, sizeof(bits));
          return bits;
        };
        auto makeRealAtom = [&](double low, double high, uint32_t flags) {
          PendingAtom result;
          result.atom.kind = FunctionalValueAtomKind::RealInterval;
          result.atom.realLowBits = encodeRealBits(low);
          result.atom.realHighBits = encodeRealBits(high);
          result.atom.flags = flags;
          return result;
        };

        std::vector<PendingAtom> realItems;
        for (const PendingAtom &source :
             unpackPendingAtoms(*resolvedTemplateSet)) {
          if (source.atom.kind != FunctionalValueAtomKind::RealInterval ||
              !source.limbs.empty())
            return OBELISK_RT_INVALID_DESIGN;
          const bool lowerUnbounded =
              source.atom.flags & FunctionalValueAtomLowerUnbounded;
          const bool upperUnbounded =
              source.atom.flags & FunctionalValueAtomUpperUnbounded;
          const bool range = source.atom.flags & FunctionalValueAtomRealRange;
          const double low = decodeRealBits(source.atom.realLowBits);
          const double high = decodeRealBits(source.atom.realHighBits);
          if (!range || lowerUnbounded || upperUnbounded ||
              high - low <= interval) {
            if (realItems.size() == ParseLimits{}.maxRecords)
              return OBELISK_RT_OUT_OF_RESOURCES;
            realItems.push_back(source);
            continue;
          }

          const double span = high - low;
          const double partitionCount = std::ceil(span / interval);
          const uint64_t remainingLimit =
              ParseLimits{}.maxRecords - realItems.size();
          if (!std::isfinite(span) || !std::isfinite(partitionCount) ||
              partitionCount > static_cast<double>(remainingLimit) + 1.0)
            return OBELISK_RT_OUT_OF_RESOURCES;
          uint64_t partitions = static_cast<uint64_t>(partitionCount);
          auto boundary = [&](uint64_t ordinal) {
            return low + interval * static_cast<double>(ordinal);
          };
          // Division and endpoint construction round independently in
          // binary64. Select the smallest ordinal whose constructed boundary
          // reaches the range high, so 0.75 + 3*0.01 ending at 0.78 does not
          // acquire a spurious sliver while nextafter(10,+inf) still does.
          while (partitions > 1 && boundary(partitions - 1) >= high)
            --partitions;
          while (boundary(partitions) < high) {
            if (partitions == remainingLimit)
              return OBELISK_RT_OUT_OF_RESOURCES;
            ++partitions;
          }
          if (partitions > remainingLimit)
            return OBELISK_RT_OUT_OF_RESOURCES;
          for (uint64_t partition = 0; partition != partitions; ++partition) {
            const bool last = partition + 1 == partitions;
            const double current = partition ? boundary(partition) : low;
            const double next = last ? high : boundary(partition + 1);
            if (!std::isfinite(current) || !std::isfinite(next) ||
                next <= current || (!last && next >= high))
              return OBELISK_RT_INVALID_DESIGN;
            if (realItems.size() == ParseLimits{}.maxRecords)
              return OBELISK_RT_OUT_OF_RESOURCES;
            realItems.push_back(makeRealAtom(
                current, next,
                FunctionalValueAtomLowerInclusive |
                    (last ? FunctionalValueAtomUpperInclusive : 0) |
                    FunctionalValueAtomRealRange));
          }
        }

        auto realItemKey = [&](const PendingAtom &entry) {
          constexpr uint32_t identityFlags = FunctionalValueAtomLowerInclusive |
                                             FunctionalValueAtomUpperInclusive |
                                             FunctionalValueAtomLowerUnbounded |
                                             FunctionalValueAtomUpperUnbounded |
                                             FunctionalValueAtomRealRange;
          const bool lowerUnbounded =
              entry.atom.flags & FunctionalValueAtomLowerUnbounded;
          const bool upperUnbounded =
              entry.atom.flags & FunctionalValueAtomUpperUnbounded;
          uint64_t lowBits = lowerUnbounded ? 0 : entry.atom.realLowBits;
          uint64_t highBits = upperUnbounded ? 0 : entry.atom.realHighBits;
          // IEEE 754 signed zeros compare equal and therefore describe the
          // same real item for duplicate merging.
          if (!lowerUnbounded && decodeRealBits(lowBits) == 0.0)
            lowBits = 0;
          if (!upperUnbounded && decodeRealBits(highBits) == 0.0)
            highBits = 0;
          return std::make_tuple(entry.atom.flags & identityFlags, lowBits,
                                 highBits);
        };
        auto formatReal = [](double value, std::string &result) {
          char buffer[128];
          auto formatted = std::to_chars(std::begin(buffer), std::end(buffer),
                                         value, std::chars_format::general);
          if (formatted.ec != std::errc{})
            return false;
          result.assign(buffer, formatted.ptr);
          if (result.find_first_of(".eE") == std::string::npos)
            result += ".0";
          return true;
        };
        auto realItemLabel = [&](const PendingAtom &entry,
                                 std::string &result) {
          const bool range = entry.atom.flags & FunctionalValueAtomRealRange;
          const bool lowerUnbounded =
              entry.atom.flags & FunctionalValueAtomLowerUnbounded;
          const bool upperUnbounded =
              entry.atom.flags & FunctionalValueAtomUpperUnbounded;
          std::string low;
          if (lowerUnbounded)
            low = "$";
          else if (!formatReal(decodeRealBits(entry.atom.realLowBits), low))
            return false;
          if (!range) {
            result = std::move(low) + "]";
            return true;
          }
          std::string high;
          if (upperUnbounded)
            high = "$";
          else if (!formatReal(decodeRealBits(entry.atom.realHighBits), high))
            return false;
          result =
              std::move(low) + ":" + high +
              ((entry.atom.flags & FunctionalValueAtomUpperInclusive) ? "]"
                                                                      : ")");
          return true;
        };

        if (plan->arrayMode == FunctionalBinArrayMode::Unsized) {
          std::vector<PendingAtom> uniqueItems;
          uniqueItems.reserve(realItems.size());
          std::set<std::tuple<uint32_t, uint64_t, uint64_t>> seenItems;
          for (PendingAtom &entry : realItems) {
            if (seenItems.insert(realItemKey(entry)).second)
              uniqueItems.push_back(std::move(entry));
          }
          if (uniqueItems.size() > UINT32_MAX)
            return OBELISK_RT_OUT_OF_RESOURCES;
          arrayCardinality = static_cast<uint32_t>(uniqueItems.size());
          expansionAtoms.reserve(uniqueItems.size());
          expansionLabels.reserve(uniqueItems.size());
          expansionLabelsIncludeClosingBracket = true;
          for (PendingAtom &entry : uniqueItems) {
            std::string label;
            if (!realItemLabel(entry, label))
              return OBELISK_RT_INVALID_DESIGN;
            expansionAtoms.push_back({std::move(entry)});
            expansionLabels.push_back(std::move(label));
          }
        } else {
          if (plan->arrayCardinality) {
            if (plan->arrayCardinality > UINT32_MAX)
              return OBELISK_RT_OUT_OF_RESOURCES;
            arrayCardinality = static_cast<uint32_t>(plan->arrayCardinality);
          } else {
            const FunctionalExpression *cardinalitySchema =
                expressionSchema(plan->cardinalityExpression);
            const obelisk_rt_functional_value_v1 *cardinalityValue =
                expressionValue(plan->cardinalityExpression);
            FunctionalInteger cardinality;
            bool unknown = false;
            if (!cardinalitySchema || !cardinalityValue ||
                !decodeFunctionalInteger(*cardinalityValue,
                                         cardinalitySchema->signedness,
                                         cardinality, unknown) ||
                unknown || cardinality.negative ||
                !functionalMagnitudeFitsU32(cardinality.magnitude))
              return OBELISK_RT_INVALID_DESIGN;
            arrayCardinality = functionalMagnitudeAsU32(cardinality.magnitude);
          }
          if (!arrayCardinality)
            return OBELISK_RT_INVALID_DESIGN;
          expansionAtoms.resize(arrayCardinality);
          expansionLabels.reserve(arrayCardinality);
          const size_t perBin =
              std::max<size_t>(1, realItems.size() / arrayCardinality);
          size_t cursor = 0;
          for (uint32_t expansion = 0; expansion != arrayCardinality;
               ++expansion) {
            expansionLabels.push_back(std::to_string(expansion));
            const size_t remaining = realItems.size() - cursor;
            const size_t count = expansion + 1 == arrayCardinality
                                     ? remaining
                                     : std::min(perBin, remaining);
            expansionAtoms[expansion].insert(
                expansionAtoms[expansion].end(),
                std::make_move_iterator(realItems.begin() + cursor),
                std::make_move_iterator(realItems.begin() + cursor + count));
            cursor += count;
          }
        }
      } else {
        if (resolvedTemplateSet->set.kind != FunctionalValueSetKind::Integral)
          return OBELISK_RT_INVALID_DESIGN;
        const uint32_t bitWidth = resolvedTemplateSet->set.bitWidth;
        const auto signedness = resolvedTemplateSet->set.signedness;
        const uint32_t limbCount = functionalLimbCount(bitWidth);
        std::vector<PendingAtom> sourceAtoms =
            unpackPendingAtoms(*resolvedTemplateSet);

        auto endpoint = [&](const PendingAtom &atom, bool high) {
          FunctionalMagnitude result(limbCount);
          for (uint32_t ordinal = 0; ordinal != limbCount; ++ordinal)
            result[ordinal] = high ? atom.limbs[ordinal].highAval
                                   : atom.limbs[ordinal].lowAval;
          if (signedness == CoverageSignedness::Signed)
            toggleFunctionalSignBias(result, bitWidth);
          normalizeFunctionalMagnitude(result);
          return result;
        };
        auto atomCardinality = [&](const PendingAtom &atom,
                                   FunctionalMagnitude &result) {
          if (atom.atom.kind == FunctionalValueAtomKind::IntegralRange) {
            FunctionalMagnitude low = endpoint(atom, false);
            FunctionalMagnitude high = endpoint(atom, true);
            if (compareMagnitude(low, high) > 0)
              return false;
            result = subtractFunctionalMagnitudes(high, low);
            result = addFunctionalMagnitudes(result, FunctionalMagnitude{1});
            return true;
          }
          if (atom.atom.kind != FunctionalValueAtomKind::IntegralValue)
            return false;
          uint32_t wildcardBits = 0;
          for (const auto &limb : atom.limbs) {
            uint64_t wildcard = limb.wildcardMask;
            while (wildcard) {
              wildcard &= wildcard - 1;
              ++wildcardBits;
            }
          }
          result = functionalPowerOfTwoMagnitude(wildcardBits);
          return true;
        };

        std::vector<FunctionalMagnitude> sourceCardinalities;
        FunctionalMagnitude total;
        for (const PendingAtom &atom : sourceAtoms) {
          FunctionalMagnitude count;
          if (atom.limbs.size() != limbCount || !atomCardinality(atom, count))
            return OBELISK_RT_INVALID_DESIGN;
          sourceCardinalities.push_back(count);
          total = addFunctionalMagnitudes(total, count);
        }

        struct ConcreteValue {
          FunctionalMagnitude aval;
          FunctionalMagnitude bval;
        };
        auto materializeConcreteValues =
            [&](const std::vector<PendingAtom> &atoms,
                std::vector<ConcreteValue> &values) -> obelisk_rt_status {
          for (const PendingAtom &atom : atoms) {
            FunctionalMagnitude count;
            if (atom.limbs.size() != limbCount ||
                !atomCardinality(atom, count) ||
                !functionalMagnitudeFitsU32(count))
              return OBELISK_RT_OUT_OF_RESOURCES;
            uint32_t concreteCount = functionalMagnitudeAsU32(count);
            if (concreteCount > ParseLimits{}.maxRecords - values.size())
              return OBELISK_RT_OUT_OF_RESOURCES;
            if (atom.atom.kind == FunctionalValueAtomKind::IntegralRange) {
              FunctionalMagnitude current = endpoint(atom, false);
              for (uint32_t ordinal = 0; ordinal != concreteCount; ++ordinal) {
                FunctionalMagnitude encoded = current;
                if (encoded.size() < limbCount)
                  encoded.resize(limbCount);
                if (signedness == CoverageSignedness::Signed)
                  toggleFunctionalSignBias(encoded, bitWidth);
                values.push_back(
                    {std::move(encoded), FunctionalMagnitude(limbCount)});
                current =
                    addFunctionalMagnitudes(current, FunctionalMagnitude{1});
              }
              continue;
            }
            FunctionalMagnitude base(limbCount), bval(limbCount), wildcard;
            wildcard.resize(limbCount);
            std::vector<uint32_t> wildcardPositions;
            for (uint32_t ordinal = 0; ordinal != limbCount; ++ordinal) {
              base[ordinal] = atom.limbs[ordinal].lowAval;
              wildcard[ordinal] = atom.limbs[ordinal].wildcardMask;
              bval[ordinal] = atom.limbs[ordinal].lowBval & ~wildcard[ordinal];
            }
            for (uint32_t bitIndex = 0; bitIndex != bitWidth; ++bitIndex)
              if (functionalMagnitudeBit(wildcard, bitIndex))
                wildcardPositions.push_back(bitIndex);
            for (uint32_t ordinal = 0; ordinal != concreteCount; ++ordinal) {
              FunctionalMagnitude concrete = base;
              for (uint32_t position = 0; position != wildcardPositions.size();
                   ++position) {
                uint32_t bitIndex = wildcardPositions[position];
                uint64_t mask = uint64_t{1} << (bitIndex % 64);
                if ((ordinal >> position) & 1)
                  concrete[bitIndex / 64] |= mask;
                else
                  concrete[bitIndex / 64] &= ~mask;
              }
              values.push_back({std::move(concrete), bval});
            }
          }
          return OBELISK_RT_OK;
        };
        auto makeConcreteAtom = [&](const ConcreteValue &value) {
          PendingAtom concrete;
          concrete.atom.kind = FunctionalValueAtomKind::IntegralValue;
          concrete.atom.flags = FunctionalValueAtomLowerInclusive |
                                FunctionalValueAtomUpperInclusive;
          concrete.atom.limbCount = limbCount;
          for (uint32_t ordinal = 0; ordinal != limbCount; ++ordinal)
            concrete.limbs.push_back({0, 0, ordinal, value.aval[ordinal],
                                      value.bval[ordinal], value.aval[ordinal],
                                      value.bval[ordinal], 0});
          return concrete;
        };
        auto concreteLabel = [&](const ConcreteValue &value) {
          if (std::any_of(value.bval.begin(), value.bval.end(),
                          [](uint64_t limb) { return limb != 0; }))
            return std::string("four-state");
          FunctionalInteger integer =
              functionalIntegerFromBits(value.aval, bitWidth, signedness);
          std::string label = functionalMagnitudeDecimal(integer.magnitude);
          return integer.negative ? "-" + label : label;
        };

        if (plan->arrayMode == FunctionalBinArrayMode::Unsized) {
          std::vector<ConcreteValue> values;
          obelisk_rt_status materialized =
              materializeConcreteValues(sourceAtoms, values);
          if (materialized != OBELISK_RT_OK)
            return materialized;
          if (values.size() > UINT32_MAX)
            return OBELISK_RT_OUT_OF_RESOURCES;
          arrayCardinality = static_cast<uint32_t>(values.size());
          expansionAtoms.reserve(values.size());
          expansionLabels.reserve(values.size());
          for (const ConcreteValue &value : values) {
            expansionAtoms.push_back({makeConcreteAtom(value)});
            expansionLabels.push_back(concreteLabel(value));
          }
        } else {
          if (plan->arrayMode != FunctionalBinArrayMode::Fixed)
            return OBELISK_RT_INVALID_DESIGN;
          if (plan->arrayCardinality) {
            if (plan->arrayCardinality > UINT32_MAX)
              return OBELISK_RT_OUT_OF_RESOURCES;
            arrayCardinality = static_cast<uint32_t>(plan->arrayCardinality);
          } else {
            const FunctionalExpression *cardinalitySchema =
                expressionSchema(plan->cardinalityExpression);
            const obelisk_rt_functional_value_v1 *cardinalityValue =
                expressionValue(plan->cardinalityExpression);
            FunctionalInteger cardinality;
            bool unknown = false;
            if (!cardinalitySchema || !cardinalityValue ||
                !decodeFunctionalInteger(*cardinalityValue,
                                         cardinalitySchema->signedness,
                                         cardinality, unknown) ||
                unknown || cardinality.negative ||
                !functionalMagnitudeFitsU32(cardinality.magnitude))
              return OBELISK_RT_INVALID_DESIGN;
            arrayCardinality = functionalMagnitudeAsU32(cardinality.magnitude);
          }
          if (!arrayCardinality)
            return OBELISK_RT_INVALID_DESIGN;
          expansionAtoms.resize(arrayCardinality);
          expansionLabels.reserve(arrayCardinality);
          uint32_t remainder = 0;
          FunctionalMagnitude quotient =
              divideFunctionalMagnitude(total, arrayCardinality, remainder);
          // IEEE 1800-2017/2023 19.5.1 defines B as the integer quotient,
          // but never less than one. When bins outnumber values, assign the
          // source occurrences to the leading bins and retain the remaining
          // bins as empty.
          if (quotient.empty()) {
            quotient = FunctionalMagnitude{1};
            remainder = 0;
          }
          size_t sourceIndex = 0;
          FunctionalMagnitude sourceOffset;

          auto appendSlice = [&](const PendingAtom &source,
                                 const FunctionalMagnitude &offset,
                                 const FunctionalMagnitude &length,
                                 std::vector<PendingAtom> &destination) {
            if (length.empty())
              return true;
            if (source.atom.kind == FunctionalValueAtomKind::IntegralRange) {
              FunctionalMagnitude low =
                  addFunctionalMagnitudes(endpoint(source, false), offset);
              FunctionalMagnitude high = addFunctionalMagnitudes(
                  low,
                  subtractFunctionalMagnitudes(length, FunctionalMagnitude{1}));
              low.resize(limbCount);
              high.resize(limbCount);
              if (signedness == CoverageSignedness::Signed) {
                toggleFunctionalSignBias(low, bitWidth);
                toggleFunctionalSignBias(high, bitWidth);
              }
              PendingAtom slice;
              slice.atom.kind = compareMagnitude(low, high) == 0
                                    ? FunctionalValueAtomKind::IntegralValue
                                    : FunctionalValueAtomKind::IntegralRange;
              slice.atom.flags = FunctionalValueAtomLowerInclusive |
                                 FunctionalValueAtomUpperInclusive;
              slice.atom.limbCount = limbCount;
              for (uint32_t ordinal = 0; ordinal != limbCount; ++ordinal)
                slice.limbs.push_back(
                    {0, 0, ordinal, low[ordinal], 0, high[ordinal], 0, 0});
              destination.push_back(std::move(slice));
              return true;
            }

            FunctionalMagnitude wildcard(limbCount), biasedValue(limbCount);
            std::vector<uint32_t> wildcardPositions;
            for (uint32_t ordinal = 0; ordinal != limbCount; ++ordinal) {
              wildcard[ordinal] = source.limbs[ordinal].wildcardMask;
              biasedValue[ordinal] = source.limbs[ordinal].lowAval;
            }
            if (signedness == CoverageSignedness::Signed) {
              uint32_t signBit = bitWidth - 1;
              if (!functionalMagnitudeBit(wildcard, signBit))
                toggleFunctionalSignBias(biasedValue, bitWidth);
            }
            for (uint32_t bitIndex = 0; bitIndex != bitWidth; ++bitIndex)
              if (functionalMagnitudeBit(wildcard, bitIndex))
                wildcardPositions.push_back(bitIndex);
            if (wildcardPositions.empty()) {
              destination.push_back(source);
              return length == FunctionalMagnitude{1};
            }

            FunctionalMagnitude current = offset;
            FunctionalMagnitude remaining = length;
            while (!remaining.empty()) {
              uint32_t alignment = functionalMagnitudeTrailingZeros(current);
              uint32_t lengthPower = functionalMagnitudeBitWidth(remaining) - 1;
              uint32_t blockPower =
                  std::min<uint32_t>(wildcardPositions.size(), lengthPower);
              if (alignment != UINT32_MAX)
                blockPower = std::min(blockPower, alignment);
              PendingAtom slice = source;
              for (uint32_t ordinal = 0; ordinal != limbCount; ++ordinal) {
                slice.limbs[ordinal].lowAval = biasedValue[ordinal];
                slice.limbs[ordinal].highAval = biasedValue[ordinal];
                slice.limbs[ordinal].lowBval = 0;
                slice.limbs[ordinal].highBval = 0;
                slice.limbs[ordinal].wildcardMask = 0;
              }
              for (uint32_t position = 0; position != wildcardPositions.size();
                   ++position) {
                uint32_t bitIndex = wildcardPositions[position];
                uint64_t mask = uint64_t{1} << (bitIndex % 64);
                auto &limb = slice.limbs[bitIndex / 64];
                if (position < blockPower) {
                  limb.lowAval &= ~mask;
                  limb.highAval &= ~mask;
                  limb.lowBval |= mask;
                  limb.highBval |= mask;
                  limb.wildcardMask |= mask;
                } else if (functionalMagnitudeBit(current, position)) {
                  limb.lowAval |= mask;
                  limb.highAval |= mask;
                } else {
                  limb.lowAval &= ~mask;
                  limb.highAval &= ~mask;
                }
              }
              if (signedness == CoverageSignedness::Signed) {
                uint32_t signBit = bitWidth - 1;
                if (!(slice.limbs[signBit / 64].wildcardMask &
                      (uint64_t{1} << (signBit % 64)))) {
                  slice.limbs[signBit / 64].lowAval ^= uint64_t{1}
                                                       << (signBit % 64);
                  slice.limbs[signBit / 64].highAval ^= uint64_t{1}
                                                        << (signBit % 64);
                }
              }
              destination.push_back(std::move(slice));
              FunctionalMagnitude block =
                  functionalPowerOfTwoMagnitude(blockPower);
              current = addFunctionalMagnitudes(current, block);
              remaining = subtractFunctionalMagnitudes(remaining, block);
            }
            return true;
          };

          for (uint32_t expansion = 0; expansion != arrayCardinality;
               ++expansion) {
            expansionLabels.push_back(std::to_string(expansion));
            FunctionalMagnitude required = quotient;
            if (expansion + 1 == arrayCardinality && remainder)
              required = addFunctionalMagnitudes(
                  required, FunctionalMagnitude{remainder});
            while (!required.empty()) {
              if (sourceIndex == sourceAtoms.size())
                break;
              FunctionalMagnitude available = subtractFunctionalMagnitudes(
                  sourceCardinalities[sourceIndex], sourceOffset);
              FunctionalMagnitude take =
                  compareMagnitude(available, required) < 0 ? available
                                                            : required;
              if (!appendSlice(sourceAtoms[sourceIndex], sourceOffset, take,
                               expansionAtoms[expansion]))
                return OBELISK_RT_INVALID_DESIGN;
              sourceOffset = addFunctionalMagnitudes(sourceOffset, take);
              required = subtractFunctionalMagnitudes(required, take);
              if (compareMagnitude(sourceOffset,
                                   sourceCardinalities[sourceIndex]) == 0) {
                ++sourceIndex;
                sourceOffset.clear();
              }
            }
          }
        }

        if (!withExpressions.empty()) {
          // IEEE 1800-2017/2023 19.5.1.1 applies the predicate to each
          // source occurrence, preserving duplicates and range-list order.
          // The compiler transports one Boolean result per occurrence; the
          // runtime independently reconstructs that stream and requires an
          // exact cardinality match before using it.
          std::vector<ConcreteValue> candidates;
          obelisk_rt_status materialized =
              materializeConcreteValues(sourceAtoms, candidates);
          if (materialized != OBELISK_RT_OK)
            return materialized;
          if (candidates.size() != withPredicateResults.size())
            return OBELISK_RT_INVALID_DESIGN;
          for (const ConcreteValue &candidate : candidates)
            if (std::any_of(candidate.bval.begin(), candidate.bval.end(),
                            [](uint64_t limb) { return limb != 0; }))
              return OBELISK_RT_INVALID_DESIGN;

          std::vector<uint32_t> occurrences;
          occurrences.reserve(candidates.size());
          for (uint32_t ordinal = 0; ordinal != candidates.size(); ++ordinal)
            if (groupDistributeFirst || withPredicateResults[ordinal])
              occurrences.push_back(ordinal);

          expansionAtoms.clear();
          expansionLabels.clear();
          expansionLabelsIncludeClosingBracket = false;
          if (plan->arrayMode == FunctionalBinArrayMode::Scalar) {
            expansionAtoms.resize(1);
            for (uint32_t ordinal : occurrences)
              if (withPredicateResults[ordinal])
                expansionAtoms.front().push_back(
                    makeConcreteAtom(candidates[ordinal]));
            arrayCardinality = 0;
          } else if (plan->arrayMode == FunctionalBinArrayMode::Unsized) {
            if (occurrences.size() > UINT32_MAX)
              return OBELISK_RT_OUT_OF_RESOURCES;
            arrayCardinality = static_cast<uint32_t>(occurrences.size());
            expansionAtoms.resize(arrayCardinality);
            expansionLabels.reserve(arrayCardinality);
            for (uint32_t expansion = 0; expansion != arrayCardinality;
                 ++expansion) {
              uint32_t ordinal = occurrences[expansion];
              expansionLabels.push_back(concreteLabel(candidates[ordinal]));
              if (withPredicateResults[ordinal])
                expansionAtoms[expansion].push_back(
                    makeConcreteAtom(candidates[ordinal]));
            }
          } else if (plan->arrayMode == FunctionalBinArrayMode::Fixed) {
            if (!arrayCardinality)
              return OBELISK_RT_INVALID_DESIGN;
            expansionAtoms.resize(arrayCardinality);
            expansionLabels.reserve(arrayCardinality);
            const size_t valuesPerBin =
                std::max<size_t>(1, occurrences.size() / arrayCardinality);
            size_t cursor = 0;
            for (uint32_t expansion = 0; expansion != arrayCardinality;
                 ++expansion) {
              expansionLabels.push_back(std::to_string(expansion));
              const size_t remaining = occurrences.size() - cursor;
              const size_t count = expansion + 1 == arrayCardinality
                                       ? remaining
                                       : std::min(valuesPerBin, remaining);
              for (size_t index = 0; index != count; ++index) {
                uint32_t ordinal = occurrences[cursor + index];
                if (withPredicateResults[ordinal])
                  expansionAtoms[expansion].push_back(
                      makeConcreteAtom(candidates[ordinal]));
              }
              cursor += count;
            }
          } else {
            return OBELISK_RT_INVALID_DESIGN;
          }
        }
      }
    }

    const uint32_t firstBin = static_cast<uint32_t>(resolvedBinRows.size());
    for (uint32_t expansion = 0; expansion != expansionAtoms.size();
         ++expansion) {
      uint64_t expansionBinID = binID;
      uint64_t expansionSetID = resolvedSetID;
      if (expansion) {
        expansionBinID = resolvedFunctionalID("functional.resolved.bin", typeID,
                                              bin->id, expansion, binIDs);
        expansionSetID =
            resolvedFunctionalID("functional.resolved.value-set", typeID,
                                 plan->valueSet, expansion, valueSetIDs);
        if (!expansionBinID || !expansionSetID)
          return OBELISK_RT_INVALID_DESIGN;
        binIDs.insert(expansionBinID);
        valueSetIDs.insert(expansionSetID);
      }
      std::string resolvedName = bin->name;
      std::string resolvedHierarchy = bin->hierarchy;
      if (plan->arrayMode != FunctionalBinArrayMode::Scalar) {
        const std::string suffix =
            "[" + expansionLabels[expansion] +
            (expansionLabelsIncludeClosingBracket ? "" : "]");
        resolvedName += suffix;
        resolvedHierarchy += suffix;
      }
      uint32_t flags = bin->flags;
      if (resolvedTemplateSet && expansionAtoms[expansion].empty())
        flags |= FunctionalBinEmpty;
      resolvedBinRows.push_back({typeID,
                                 {},
                                 expansionBinID,
                                 bin->id,
                                 item->second,
                                 std::move(resolvedName),
                                 bin->kind,
                                 flags,
                                 nextResolvedBinOrdinal[item->second]++,
                                 expansion,
                                 effectiveAtLeast(item->second),
                                 std::move(resolvedHierarchy)});
      if (resolvedTemplateSet)
        pendingSets.push_back(
            packPendingSet(*resolvedTemplateSet, expansionAtoms[expansion],
                           expansionSetID, expansionBinID, expansion));
      resolvedPlanRows.push_back({typeID,
                                  {},
                                  expansionBinID,
                                  expansionSetID,
                                  plan->iffExpression,
                                  plan->cardinalityExpression,
                                  arrayCardinality,
                                  plan->arrayMode,
                                  plan->distribution,
                                  plan->flags});
    }
    resolvedGroupRows.push_back({typeID,
                                 {},
                                 item->second,
                                 bin->id,
                                 firstBin,
                                 static_cast<uint32_t>(expansionAtoms.size()),
                                 arrayCardinality,
                                 plan->arrayMode,
                                 plan->distribution,
                                 plan->flags,
                                 bin->kind});
  }

  // Resolve each default-array domain from its owner's sets, without scanning
  // unrelated bins or sets. Numeric positions survive appending more rows.
  IndexMap<uint64_t, uint32_t> resolvedBinFlags;
  for (const auto &bin : resolvedBinRows)
    resolvedBinFlags.emplace(bin.id, bin.flags);
  std::vector<std::vector<size_t>> associatedSets(resolvedItemRows.size());
  for (size_t index = 0; index != pendingSets.size(); ++index) {
    const auto &set = pendingSets[index].set;
    auto owner = resolvedBinFlags.find(set.ownerBin);
    auto item = resolvedItemPositions.find(set.item);
    if (owner != resolvedBinFlags.end() &&
        !(owner->second & FunctionalBinDefault) &&
        set.kind == FunctionalValueSetKind::Integral &&
        item != resolvedItemPositions.end())
      associatedSets[item->second].push_back(index);
  }
  std::vector<std::optional<std::vector<FunctionalMagnitude>>> defaultDomains(
      resolvedItemRows.size());
  const uint64_t binRecordLimit =
      std::min<uint64_t>(UINT32_MAX, ParseLimits{}.maxRecords);
  for (const PendingDefaultArray &pendingDefault : pendingDefaultArrays) {
    auto position = resolvedItemPositions.find(pendingDefault.item);
    const ResolvedFunctionalItem *resolvedItem =
        position == resolvedItemPositions.end()
            ? nullptr
            : &resolvedItemRows[position->second];
    const FunctionalExpression *sampleExpression =
        resolvedItem ? lookup(coverpointSamples, resolvedItem->templateItem)
                     : nullptr;
    if (!resolvedItem || !sampleExpression ||
        isRealResultKind(sampleExpression->resultKind) ||
        !sampleExpression->bitWidth ||
        (sampleExpression->signedness != CoverageSignedness::Signed &&
         sampleExpression->signedness != CoverageSignedness::Unsigned))
      return OBELISK_RT_INVALID_DESIGN;

    const uint32_t bitWidth = sampleExpression->bitWidth;
    const uint32_t limbCount = functionalLimbCount(bitWidth);
    const auto signedness = sampleExpression->signedness;
    auto &domain = defaultDomains[position->second];
    if (!domain) {
      std::vector<IntegralCoverageShape> associatedShapes;
      for (size_t setIndex : associatedSets[position->second]) {
        const PendingSet &set = pendingSets[setIndex];
        if (set.set.bitWidth != bitWidth || set.set.signedness != signedness)
          continue;
        for (const auto &atom : set.atoms) {
          if (atom.limbCount != limbCount ||
              atom.firstLimb > set.limbs.size() ||
              limbCount > set.limbs.size() - atom.firstLimb)
            return OBELISK_RT_INVALID_DESIGN;
          IntegralCoverageShape shape;
          shape.interval = atom.kind == FunctionalValueAtomKind::IntegralRange;
          if (!shape.interval &&
              atom.kind != FunctionalValueAtomKind::IntegralValue)
            continue;
          if (shape.interval) {
            shape.low.resize(limbCount);
            shape.high.resize(limbCount);
          } else {
            shape.value.resize(limbCount);
            shape.wildcard.resize(limbCount);
          }
          bool coversKnownValues = true;
          for (uint32_t ordinal = 0; ordinal != limbCount; ++ordinal) {
            const auto &limb = set.limbs[atom.firstLimb + ordinal];
            uint64_t valid = ordinal + 1 == limbCount && bitWidth % 64
                                 ? lowBitsMask(bitWidth % 64)
                                 : UINT64_MAX;
            if (shape.interval) {
              if ((limb.lowBval | limb.highBval | limb.wildcardMask) & valid) {
                coversKnownValues = false;
                break;
              }
              shape.low[ordinal] = limb.lowAval & valid;
              shape.high[ordinal] = limb.highAval & valid;
            } else {
              if (limb.lowBval & ~limb.wildcardMask & valid) {
                coversKnownValues = false;
                break;
              }
              shape.wildcard[ordinal] = limb.wildcardMask & valid;
              shape.value[ordinal] = limb.lowAval & ~limb.wildcardMask & valid;
            }
          }
          if (!coversKnownValues)
            continue;
          if (signedness == CoverageSignedness::Signed) {
            const uint32_t signBit = bitWidth - 1;
            const uint32_t signLimb = signBit / 64;
            const uint64_t signMask = uint64_t{1} << (signBit % 64);
            if (shape.interval) {
              shape.low[signLimb] ^= signMask;
              shape.high[signLimb] ^= signMask;
            } else {
              shape.value[signLimb] ^= signMask;
              shape.value[signLimb] &= ~shape.wildcard[signLimb];
            }
          }
          associatedShapes.push_back(std::move(shape));
        }
      }

      struct MissingBlock {
        FunctionalMagnitude prefix;
        int64_t remainingBit = -1;
      };
      struct SearchFrame {
        FunctionalMagnitude prefix;
        int64_t remainingBit = -1;
        std::vector<size_t> candidates;
      };
      std::vector<MissingBlock> missingBlocks;
      FunctionalMagnitude missingCardinality;
      SearchFrame root;
      root.prefix.resize(limbCount);
      root.remainingBit = static_cast<int64_t>(bitWidth) - 1;
      root.candidates.resize(associatedShapes.size());
      for (size_t index = 0; index != associatedShapes.size(); ++index)
        root.candidates[index] = index;
      std::vector<SearchFrame> search;
      search.push_back(std::move(root));
      while (!search.empty()) {
        SearchFrame frame = std::move(search.back());
        search.pop_back();
        std::vector<size_t> intersecting;
        intersecting.reserve(frame.candidates.size());
        bool associated = false;
        for (size_t index : frame.candidates) {
          const IntegralCoverageShape &shape = associatedShapes[index];
          if (!coverageShapeIntersectsPrefix(shape, frame.prefix,
                                             frame.remainingBit, bitWidth))
            continue;
          if (coverageShapeContainsPrefix(shape, frame.prefix,
                                          frame.remainingBit, bitWidth)) {
            associated = true;
            break;
          }
          intersecting.push_back(index);
        }
        if (associated)
          continue;
        if (intersecting.empty() || frame.remainingBit < 0) {
          FunctionalMagnitude blockCardinality =
              frame.remainingBit < 0
                  ? FunctionalMagnitude{1}
                  : functionalPowerOfTwoMagnitude(
                        static_cast<uint32_t>(frame.remainingBit) + 1);
          missingCardinality =
              addFunctionalMagnitudes(missingCardinality, blockCardinality);
          if (!functionalMagnitudeFitsU32(missingCardinality) ||
              functionalMagnitudeAsU32(missingCardinality) > binRecordLimit)
            return OBELISK_RT_OUT_OF_RESOURCES;
          missingBlocks.push_back(
              {std::move(frame.prefix), frame.remainingBit});
          continue;
        }
        SearchFrame one{frame.prefix, frame.remainingBit - 1, intersecting};
        setCubeBit(one.prefix, static_cast<uint32_t>(frame.remainingBit), true);
        search.push_back(std::move(one));
        search.push_back({std::move(frame.prefix), frame.remainingBit - 1,
                          std::move(intersecting)});
      }

      std::vector<FunctionalMagnitude> values;
      values.reserve(functionalMagnitudeAsU32(missingCardinality));
      for (const MissingBlock &block : missingBlocks) {
        FunctionalMagnitude blockCardinality =
            block.remainingBit < 0
                ? FunctionalMagnitude{1}
                : functionalPowerOfTwoMagnitude(
                      static_cast<uint32_t>(block.remainingBit) + 1);
        const uint32_t count = functionalMagnitudeAsU32(blockCardinality);
        for (uint32_t offset = 0; offset != count; ++offset) {
          FunctionalMagnitude value = addFunctionalMagnitudes(
              block.prefix, FunctionalMagnitude{offset});
          value.resize(limbCount);
          if (signedness == CoverageSignedness::Signed)
            toggleFunctionalSignBias(value, bitWidth);
          values.push_back(std::move(value));
        }
      }
      domain = std::move(values);
    }
    const auto &values = *domain;
    const uint32_t cardinality = static_cast<uint32_t>(values.size());
    if (resolvedBinRows.size() > binRecordLimit ||
        cardinality > binRecordLimit - resolvedBinRows.size())
      return OBELISK_RT_OUT_OF_RESOURCES;
    auto &group = resolvedGroupRows[pendingDefault.groupIndex];
    group.firstBin = static_cast<uint32_t>(resolvedBinRows.size());
    group.binCount = cardinality;
    group.arrayCardinality = cardinality;
    PendingSet base;
    base.set = {typeID,
                {},
                0,
                0,
                pendingDefault.item,
                0,
                0,
                bitWidth,
                FunctionalValueSetKind::Integral,
                0,
                signedness,
                0,
                0,
                0,
                0,
                ResolvedFunctionalValueSetRole::StateBin};
    for (uint32_t expansion = 0; expansion != cardinality; ++expansion) {
      uint64_t binID = pendingDefault.firstBinID;
      if (expansion) {
        binID = resolvedFunctionalID("functional.resolved.bin", typeID,
                                     pendingDefault.bin->id, expansion, binIDs);
        if (!binID)
          return OBELISK_RT_INVALID_DESIGN;
        binIDs.insert(binID);
      }
      uint64_t setID =
          resolvedFunctionalID("functional.resolved.value-set", typeID,
                               pendingDefault.bin->id, expansion, valueSetIDs);
      if (!setID)
        return OBELISK_RT_INVALID_DESIGN;
      valueSetIDs.insert(setID);

      PendingAtom atom;
      atom.atom.kind = FunctionalValueAtomKind::IntegralValue;
      atom.atom.flags =
          FunctionalValueAtomLowerInclusive | FunctionalValueAtomUpperInclusive;
      atom.atom.limbCount = limbCount;
      for (uint32_t ordinal = 0; ordinal != limbCount; ++ordinal)
        atom.limbs.push_back({0, 0, ordinal, values[expansion][ordinal], 0,
                              values[expansion][ordinal], 0, 0});
      pendingSets.push_back(
          packPendingSet(base, {atom}, setID, binID, expansion));

      FunctionalInteger integer =
          functionalIntegerFromBits(values[expansion], bitWidth, signedness);
      std::string label = functionalMagnitudeDecimal(integer.magnitude);
      if (integer.negative)
        label.insert(label.begin(), '-');
      std::string name = pendingDefault.bin->name + "[" + label + "]";
      std::string hierarchy = pendingDefault.bin->hierarchy + "[" + label + "]";
      resolvedBinRows.push_back({typeID,
                                 {},
                                 binID,
                                 pendingDefault.bin->id,
                                 pendingDefault.item,
                                 std::move(name),
                                 pendingDefault.bin->kind,
                                 pendingDefault.bin->flags,
                                 expansion,
                                 expansion,
                                 effectiveAtLeast(pendingDefault.item),
                                 std::move(hierarchy)});
      resolvedPlanRows.push_back({typeID,
                                  {},
                                  binID,
                                  setID,
                                  pendingDefault.plan->iffExpression,
                                  0,
                                  cardinality,
                                  FunctionalBinArrayMode::Unsized,
                                  FunctionalBinDistributionKind::PerValue,
                                  pendingDefault.plan->flags});
    }
  }

  // A coverpoint with no user-defined ordinary bin receives the default set
  // of automatic integral bins (IEEE 1800-2023 19.5.3). Ignore and illegal
  // bins do not suppress creation; they are applied to the uniformly
  // partitioned bins after distribution.
  for (const ResolvedFunctionalItem &resolvedItem : resolvedItemRows) {
    if (resolvedItem.kind != FunctionalItemKind::Coverpoint)
      continue;
    if (suppressAutomaticBins.count(resolvedItem.templateItem))
      continue;

    const FunctionalExpression *sampleExpression =
        lookup(coverpointSamples, resolvedItem.templateItem);
    if (!sampleExpression || !sampleExpression->bitWidth ||
        sampleExpression->resultKind !=
            FunctionalExpressionResultKind::Integral ||
        (sampleExpression->signedness != CoverageSignedness::Signed &&
         sampleExpression->signedness != CoverageSignedness::Unsigned))
      return OBELISK_RT_INVALID_DESIGN;

    const uint32_t bitWidth = sampleExpression->bitWidth;
    const uint32_t limbCount = functionalLimbCount(bitWidth);
    const CoverageSignedness signedness = sampleExpression->signedness;
    const uint64_t autoBinMax = effectiveAutoBinMax(resolvedItem.id);
    const uint64_t binCount64 =
        bitWidth < 32 ? std::min(uint64_t{1} << bitWidth, autoBinMax)
                      : autoBinMax;
    if (binCount64 > binRecordLimit)
      return OBELISK_RT_OUT_OF_RESOURCES;
    const uint32_t binCount = static_cast<uint32_t>(binCount64);

    if (resolvedBinRows.size() > binRecordLimit ||
        binCount > binRecordLimit - resolvedBinRows.size())
      return OBELISK_RT_OUT_OF_RESOURCES;
    const uint32_t firstBin = static_cast<uint32_t>(resolvedBinRows.size());
    if (!binCount) {
      resolvedGroupRows.push_back({typeID,
                                   {},
                                   resolvedItem.id,
                                   0,
                                   firstBin,
                                   0,
                                   0,
                                   FunctionalBinArrayMode::Fixed,
                                   FunctionalBinDistributionKind::Uniform,
                                   0,
                                   FunctionalBinKind::State});
      continue;
    }
    FunctionalMagnitude domain = functionalPowerOfTwoMagnitude(bitWidth);
    uint32_t remainder = 0;
    FunctionalMagnitude uniformWidth =
        divideFunctionalMagnitude(domain, binCount, remainder);
    FunctionalMagnitude current;

    auto label = [](const FunctionalInteger &value) {
      std::string result = functionalMagnitudeDecimal(value.magnitude);
      if (value.negative)
        result.insert(result.begin(), '-');
      return result;
    };
    for (uint32_t expansion = 0; expansion != binCount; ++expansion) {
      FunctionalMagnitude binWidth = uniformWidth;
      if (expansion + 1 == binCount && remainder)
        binWidth = addFunctionalMagnitudes(
            binWidth, FunctionalMagnitude{static_cast<uint64_t>(remainder)});
      FunctionalMagnitude orderedLow = current;
      FunctionalMagnitude orderedHigh = addFunctionalMagnitudes(
          orderedLow,
          subtractFunctionalMagnitudes(binWidth, FunctionalMagnitude{1}));
      current = addFunctionalMagnitudes(current, binWidth);
      orderedLow.resize(limbCount);
      orderedHigh.resize(limbCount);
      FunctionalMagnitude encodedLow = orderedLow;
      FunctionalMagnitude encodedHigh = orderedHigh;
      if (signedness == CoverageSignedness::Signed) {
        toggleFunctionalSignBias(encodedLow, bitWidth);
        toggleFunctionalSignBias(encodedHigh, bitWidth);
      }

      uint64_t binID =
          resolvedFunctionalID("functional.resolved.automatic-bin", typeID,
                               resolvedItem.templateItem, expansion, binIDs);
      uint64_t setID = resolvedFunctionalID(
          "functional.resolved.automatic-value-set", typeID,
          resolvedItem.templateItem, expansion, valueSetIDs);
      if (!binID || !setID)
        return OBELISK_RT_INVALID_DESIGN;
      binIDs.insert(binID);
      valueSetIDs.insert(setID);

      FunctionalInteger low =
          functionalIntegerFromBits(encodedLow, bitWidth, signedness);
      FunctionalInteger high =
          functionalIntegerFromBits(encodedHigh, bitWidth, signedness);
      std::string binLabel = label(low);
      if (compareMagnitude(orderedLow, orderedHigh) != 0)
        binLabel += ":" + label(high);
      std::string name = "auto[" + binLabel + "]";
      std::string hierarchy = resolvedItem.hierarchy + "." + name;
      resolvedBinRows.push_back({typeID,
                                 {},
                                 binID,
                                 0,
                                 resolvedItem.id,
                                 std::move(name),
                                 FunctionalBinKind::State,
                                 FunctionalBinAutomatic,
                                 nextResolvedBinOrdinal[resolvedItem.id]++,
                                 expansion,
                                 effectiveAtLeast(resolvedItem.id),
                                 std::move(hierarchy)});

      PendingSet base;
      base.set = {typeID,
                  {},
                  0,
                  0,
                  resolvedItem.id,
                  0,
                  0,
                  bitWidth,
                  FunctionalValueSetKind::Integral,
                  0,
                  signedness,
                  0,
                  0,
                  0,
                  0,
                  ResolvedFunctionalValueSetRole::StateBin};
      PendingAtom atom;
      atom.atom.kind = compareMagnitude(orderedLow, orderedHigh) == 0
                           ? FunctionalValueAtomKind::IntegralValue
                           : FunctionalValueAtomKind::IntegralRange;
      atom.atom.flags =
          FunctionalValueAtomLowerInclusive | FunctionalValueAtomUpperInclusive;
      atom.atom.limbCount = limbCount;
      for (uint32_t ordinal = 0; ordinal != limbCount; ++ordinal)
        atom.limbs.push_back({0, 0, ordinal, encodedLow[ordinal], 0,
                              encodedHigh[ordinal], 0, 0});
      pendingSets.push_back(
          packPendingSet(base, {atom}, setID, binID, expansion));
      resolvedPlanRows.push_back({typeID,
                                  {},
                                  binID,
                                  setID,
                                  0,
                                  0,
                                  binCount,
                                  FunctionalBinArrayMode::Fixed,
                                  FunctionalBinDistributionKind::Uniform,
                                  0});
    }
    resolvedGroupRows.push_back({typeID,
                                 {},
                                 resolvedItem.id,
                                 0,
                                 firstBin,
                                 binCount,
                                 binCount,
                                 FunctionalBinArrayMode::Fixed,
                                 FunctionalBinDistributionKind::Uniform,
                                 0,
                                 FunctionalBinKind::State});
  }

  // Every point bin belongs to exactly one group. Pack the append-only spans
  // once, preserving item and declaration order, including empty groups.
  // Late default arrays therefore never shift subsequent rows or ordinals.
  std::vector<std::vector<size_t>> groupsByItem(resolvedItemRows.size());
  for (size_t index = 0; index != resolvedGroupRows.size(); ++index) {
    auto item = resolvedItemPositions.find(resolvedGroupRows[index].item);
    if (item == resolvedItemPositions.end())
      return OBELISK_RT_INVALID_DESIGN;
    groupsByItem[item->second].push_back(index);
  }
  std::vector<ResolvedFunctionalBin> packedBins;
  std::vector<ResolvedFunctionalBinGroup> packedGroups;
  packedBins.reserve(resolvedBinRows.size());
  packedGroups.reserve(resolvedGroupRows.size());
  for (size_t itemIndex = 0; itemIndex != resolvedItemRows.size();
       ++itemIndex) {
    uint32_t ordinal = 0;
    for (size_t groupIndex : groupsByItem[itemIndex]) {
      auto group = resolvedGroupRows[groupIndex];
      if (uint64_t{group.firstBin} + group.binCount > resolvedBinRows.size())
        return OBELISK_RT_INVALID_DESIGN;
      if (packedBins.size() > UINT32_MAX ||
          group.binCount > UINT32_MAX - packedBins.size())
        return OBELISK_RT_OUT_OF_RESOURCES;
      const uint32_t source = group.firstBin;
      group.firstBin = static_cast<uint32_t>(packedBins.size());
      for (uint32_t offset = 0; offset != group.binCount; ++offset) {
        auto &bin = resolvedBinRows[source + offset];
        if (bin.item != group.item)
          return OBELISK_RT_INVALID_DESIGN;
        bin.ordinal = ordinal++;
        packedBins.push_back(std::move(bin));
      }
      packedGroups.push_back(group);
    }
  }
  if (packedBins.size() != resolvedBinRows.size())
    return OBELISK_RT_INVALID_DESIGN;
  resolvedBinRows = std::move(packedBins);
  resolvedGroupRows = std::move(packedGroups);
  if (resolvedBinRows.size() > binRecordLimit ||
      resolvedCrossBinRows.size() > binRecordLimit - resolvedBinRows.size())
    return OBELISK_RT_OUT_OF_RESOURCES;
  resolvedBinRows.insert(resolvedBinRows.end(), resolvedCrossBinRows.begin(),
                         resolvedCrossBinRows.end());

  struct PendingTupleSet {
    ResolvedFunctionalTupleSet set;
    std::vector<ResolvedFunctionalTuple> tuples;
    std::vector<ResolvedFunctionalTupleComponent> components;
  };
  std::vector<PendingTupleSet> pendingTupleSets;
  std::unordered_set<uint64_t> tupleSetIDs;
  std::unordered_set<uint64_t> tupleIDs;
  for (const auto &set : schema.functionalTupleSets)
    tupleSetIDs.insert(set.id);
  for (const auto &tuple : schema.functionalTupleSetTuples)
    tupleIDs.insert(tuple.id);

  auto captureSelectorInteger =
      [&](uint64_t selector, uint64_t cross, uint32_t ordinal,
          const FunctionalExpression &descriptor,
          const obelisk_rt_functional_value_v1 &value,
          ResolvedFunctionalValueSetRole role) -> obelisk_rt_status {
    if (!descriptor.bitWidth || value.bit_width != descriptor.bitWidth ||
        !value.value ||
        (value.kind != OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL &&
         value.kind != OBELISK_RT_FUNCTIONAL_VALUE_FOUR_STATE))
      return OBELISK_RT_INVALID_DESIGN;
    uint64_t id = resolvedFunctionalID(
        role == ResolvedFunctionalValueSetRole::MatchesCount
            ? "functional.resolved.matches-count"
            : "functional.resolved.predicate-value",
        typeID, selector, ordinal, valueSetIDs);
    if (!id)
      return OBELISK_RT_INVALID_DESIGN;
    valueSetIDs.insert(id);
    PendingSet pending;
    pending.set.type = typeID;
    pending.set.id = id;
    pending.set.item = cross;
    pending.set.atomCount = 1;
    pending.set.bitWidth = descriptor.bitWidth;
    pending.set.kind = FunctionalValueSetKind::Integral;
    pending.set.signedness = descriptor.signedness;
    pending.set.ownerSelector = selector;
    pending.set.ownerOrdinal = ordinal;
    pending.set.role = role;
    uint32_t limbCount = (uint64_t{descriptor.bitWidth} + 63) / 64;
    const auto *aval = static_cast<const uint8_t *>(value.value);
    const auto *bval = static_cast<const uint8_t *>(value.unknown);
    for (uint32_t limb = 0; limb != limbCount; ++limb) {
      uint64_t a = 0, b = 0;
      for (uint32_t byte = 0;
           byte != 8 && uint64_t{limb} * 8 + byte < value.value_size; ++byte) {
        uint64_t offset = uint64_t{limb} * 8 + byte;
        a |= uint64_t{aval[offset]} << (byte * 8);
        if (bval)
          b |= uint64_t{bval[offset]} << (byte * 8);
      }
      // Simulation transports (aval ^ bval, bval); literal snapshots use
      // IEEE A/B planes, including the distinction between X and Z.
      a ^= b;
      if (limb + 1 == limbCount && descriptor.bitWidth % 64) {
        a &= lowBitsMask(descriptor.bitWidth % 64);
        b &= lowBitsMask(descriptor.bitWidth % 64);
      }
      pending.limbs.push_back({id, 0, limb, a, b, a, b, 0});
    }
    pending.atoms.push_back({id, 0, 0, 0, limbCount, 0,
                             FunctionalValueAtomKind::IntegralValue,
                             FunctionalValueAtomLowerInclusive |
                                 FunctionalValueAtomUpperInclusive});
    pendingSets.push_back(std::move(pending));
    return OBELISK_RT_OK;
  };

  std::vector<ResolvedCrossSelectorBinding> resolvedSelectorBindingRows;
  const auto programsBySelector =
      detail::groupRecords(schema.crossPredicateNodes,
                           [](const auto &node) { return node.selector; });
  for (const CrossSelectorNode &node : schema.crossSelectorNodes) {
    auto resolvedCross = resolvedItems.find(node.cross);
    if (resolvedCross == resolvedItems.end())
      continue;
    if (prepared && node.withExpression &&
        !(node.flags & CrossSelectorScalarWith))
      continue;
    if (node.tupleSet || (node.kind == CrossSelectorKind::Set) !=
                             (node.constructionExpression != 0))
      return OBELISK_RT_INVALID_DESIGN;
    if (!node.withExpression && (node.kind == CrossSelectorKind::Not ||
                                 node.kind == CrossSelectorKind::And ||
                                 node.kind == CrossSelectorKind::Or ||
                                 node.kind == CrossSelectorKind::AllTuples))
      continue;
    auto resolvedTarget = resolvedItems.find(node.target);
    if ((node.kind == CrossSelectorKind::Binsof &&
         resolvedTarget == resolvedItems.end()) ||
        (node.kind != CrossSelectorKind::Binsof &&
         (node.target || node.bin || node.valueSet)))
      return OBELISK_RT_INVALID_DESIGN;
    if (node.bin) {
      auto targetBin = std::find_if(
          schema.functionalBins.begin(), schema.functionalBins.end(),
          [&](const FunctionalBin &candidate) {
            return candidate.id == node.bin && candidate.item == node.target;
          });
      if (targetBin == schema.functionalBins.end())
        return OBELISK_RT_INVALID_DESIGN;
    }
    uint64_t resolvedSetID = 0;
    if (node.valueSet) {
      auto set = std::find_if(schema.functionalValueSets.begin(),
                              schema.functionalValueSets.end(),
                              [&](const FunctionalValueSet &entry) {
                                return entry.id == node.valueSet;
                              });
      if (set == schema.functionalValueSets.end() || set->item != node.target ||
          (set->kind != FunctionalValueSetKind::Integral &&
           set->kind != FunctionalValueSetKind::Real) ||
          !set->bitWidth || set->setExpression ||
          set->flags != FunctionalValueSetNeedsResolution ||
          uint64_t{set->firstAtom} + set->atomCount >
              schema.functionalValueAtoms.size())
        return OBELISK_RT_INVALID_DESIGN;

      resolvedSetID =
          resolvedFunctionalID("functional.resolved.selector-value-set", typeID,
                               set->id, 0, valueSetIDs);
      if (!resolvedSetID)
        return OBELISK_RT_INVALID_DESIGN;
      valueSetIDs.insert(resolvedSetID);
      PendingSet pending;
      pending.set = {typeID,
                     {},
                     resolvedSetID,
                     set->id,
                     resolvedTarget->second,
                     0,
                     0,
                     set->bitWidth,
                     set->kind,
                     0,
                     set->signedness,
                     0,
                     node.id,
                     0,
                     0,
                     ResolvedFunctionalValueSetRole::SelectorIntersection};
      const bool targetSigned = set->signedness == CoverageSignedness::Signed;
      const FunctionalInteger targetMinimum =
          targetSigned ? functionalPowerOfTwo(set->bitWidth - 1, true)
                       : FunctionalInteger{};
      const FunctionalInteger targetMaximum =
          functionalMaximum(set->bitWidth, targetSigned);
      const uint32_t limbCount = functionalLimbCount(set->bitWidth);
      for (uint32_t atomIndex = 0; atomIndex != set->atomCount; ++atomIndex) {
        const FunctionalValueAtom &atom =
            schema.functionalValueAtoms[set->firstAtom + atomIndex];
        if (atom.kind != FunctionalValueAtomKind::IntegralValue &&
            atom.kind != FunctionalValueAtomKind::IntegralRange &&
            atom.kind != FunctionalValueAtomKind::RealInterval)
          return OBELISK_RT_INVALID_DESIGN;
        const bool lowerUnbounded =
            atom.flags & FunctionalValueAtomLowerUnbounded;
        const bool upperUnbounded =
            atom.flags & FunctionalValueAtomUpperUnbounded;
        const auto *low =
            lowerUnbounded ? nullptr : expressionValue(atom.lowerExpression);
        const bool singleton =
            atom.kind == FunctionalValueAtomKind::IntegralValue ||
            (atom.kind == FunctionalValueAtomKind::RealInterval &&
             !atom.upperExpression && !upperUnbounded);
        const auto *high = upperUnbounded ? nullptr
                           : singleton ? low
                                       : expressionValue(atom.upperExpression);
        if ((!lowerUnbounded && !low) || (!upperUnbounded && !high))
          return OBELISK_RT_INVALID_DESIGN;
        if (set->kind == FunctionalValueSetKind::Real) {
          if (atom.kind != FunctionalValueAtomKind::RealInterval)
            return OBELISK_RT_INVALID_DESIGN;
          bool hasUnknown = false;
          auto lower =
              lowerUnbounded
                  ? std::optional<double>(0)
                  : decodeRealBound(*low, atom.lowerExpression, hasUnknown);
          auto upper =
              upperUnbounded ? std::optional<double>(0)
              : singleton
                  ? lower
                  : decodeRealBound(*high, atom.upperExpression, hasUnknown);
          if (!lower || !upper)
            return OBELISK_RT_INVALID_DESIGN;
          if (hasUnknown) {
            std::fputs(
                "warning: cross bin real intersect value contains X or Z "
                "and is ignored\n",
                stderr);
            continue;
          }
          if ((!lowerUnbounded && !std::isfinite(*lower)) ||
              (!upperUnbounded && !std::isfinite(*upper)))
            return OBELISK_RT_INVALID_DESIGN;
          if (!lowerUnbounded && !upperUnbounded && *lower > *upper) {
            std::fputs("warning: cross bin real intersect range has reversed "
                       "bounds and is ignored\n",
                       stderr);
            continue;
          }
          const auto *sample = lookup(coverpointSamples, node.target);
          bool shortReal =
              sample &&
              sample->resultKind == FunctionalExpressionResultKind::ShortReal;
          if (shortReal && singleton &&
              (std::abs(*lower) > std::numeric_limits<float>::max() ||
               static_cast<double>(static_cast<float>(*lower)) != *lower)) {
            std::fputs("warning: cross bin real intersect value changes value "
                       "when converted to the effective coverpoint type and "
                       "is ignored\n",
                       stderr);
            continue;
          }
          uint64_t lowBits = 0, highBits = 0;
          std::memcpy(&lowBits, &*lower, sizeof(lowBits));
          std::memcpy(&highBits, &*upper, sizeof(highBits));
          pending.atoms.push_back(
              {resolvedSetID, lowBits, highBits, 0, 0,
               static_cast<uint32_t>(pending.atoms.size()), atom.kind,
               atom.flags & (FunctionalValueAtomLowerInclusive |
                             FunctionalValueAtomUpperInclusive |
                             FunctionalValueAtomLowerUnbounded |
                             FunctionalValueAtomUpperUnbounded |
                             FunctionalValueAtomRealRange)});
          continue;
        }
        const FunctionalExpression *lowExpression =
            lowerUnbounded ? nullptr : expressionSchema(atom.lowerExpression);
        const FunctionalExpression *highExpression =
            upperUnbounded
                ? nullptr
                : expressionSchema(
                      atom.kind == FunctionalValueAtomKind::IntegralValue
                          ? atom.lowerExpression
                          : atom.upperExpression);
        FunctionalInteger originalLow = targetMinimum;
        FunctionalInteger originalHigh = targetMaximum;
        bool lowUnknown = false, highUnknown = false;
        if ((!lowerUnbounded &&
             (!low || !lowExpression ||
              !decodeFunctionalInteger(*low, lowExpression->signedness,
                                       originalLow, lowUnknown))) ||
            (!upperUnbounded &&
             (!high || !highExpression ||
              !decodeFunctionalInteger(*high, highExpression->signedness,
                                       originalHigh, highUnknown))))
          return OBELISK_RT_INVALID_DESIGN;
        if (lowUnknown || highUnknown) {
          std::fputs("warning: cross bin intersect value contains X or Z and "
                     "is ignored\n",
                     stderr);
          continue;
        }
        if (atom.kind == FunctionalValueAtomKind::IntegralValue) {
          if (!functionalIntegerRepresentable(originalLow, set->bitWidth,
                                              set->signedness)) {
            std::fputs("warning: cross bin intersect value is outside the "
                       "effective coverpoint type and is ignored\n",
                       stderr);
            continue;
          }
          originalHigh = originalLow;
        } else {
          if (compareFunctionalInteger(originalLow, originalHigh) > 0) {
            std::fputs("warning: cross bin intersect range has reversed bounds "
                       "and is ignored\n",
                       stderr);
            continue;
          }
          FunctionalInteger clippedLow =
              compareFunctionalInteger(originalLow, targetMinimum) < 0
                  ? targetMinimum
                  : originalLow;
          FunctionalInteger clippedHigh =
              compareFunctionalInteger(originalHigh, targetMaximum) > 0
                  ? targetMaximum
                  : originalHigh;
          if (compareFunctionalInteger(clippedLow, clippedHigh) > 0) {
            std::fputs("warning: cross bin intersect range is outside the "
                       "effective coverpoint type and is ignored\n",
                       stderr);
            continue;
          }
          originalLow = std::move(clippedLow);
          originalHigh = std::move(clippedHigh);
        }
        const uint32_t resolvedOrdinal = pending.atoms.size();
        const uint32_t firstLimb = pending.limbs.size();
        std::vector<uint64_t> lowLimbs =
            encodeFunctionalInteger(originalLow, set->bitWidth);
        std::vector<uint64_t> highLimbs =
            encodeFunctionalInteger(originalHigh, set->bitWidth);
        for (uint32_t limbIndex = 0; limbIndex != limbCount; ++limbIndex)
          pending.limbs.push_back({resolvedSetID, resolvedOrdinal, limbIndex,
                                   lowLimbs[limbIndex], 0, highLimbs[limbIndex],
                                   0, 0});
        pending.atoms.push_back(
            {resolvedSetID, 0, 0, firstLimb, limbCount, resolvedOrdinal,
             atom.kind,
             atom.flags & (FunctionalValueAtomLowerInclusive |
                           FunctionalValueAtomUpperInclusive)});
      }
      pending.set.atomCount = static_cast<uint32_t>(pending.atoms.size());
      pendingSets.push_back(std::move(pending));
    }

    uint64_t resolvedTupleSetID = 0;
    CrossMatchesPolicy resolvedMatchesPolicy = CrossMatchesPolicy::None;
    uint64_t resolvedMatchesCount = 0;
    uint32_t resolvedBindingFlags = 0;
    if (node.predicateRoot) {
      resolvedBindingFlags = ResolvedCrossSelectorSymbolicWith;
      resolvedMatchesPolicy = node.matchesPolicy;
      resolvedMatchesCount = node.matchesCount;
      for (const auto &operand :
           detail::lookupRows(programsBySelector, node.id)) {
        if (operand.kind != CrossPredicateKind::Value)
          continue;
        const auto *descriptor = expressionSchema(operand.expression);
        const auto *value = expressionValue(operand.expression);
        if (!descriptor || !value)
          return OBELISK_RT_INVALID_DESIGN;
        obelisk_rt_status captured = captureSelectorInteger(
            node.id, resolvedCross->second, operand.ordinal, *descriptor,
            *value, ResolvedFunctionalValueSetRole::PredicateValue);
        if (captured != OBELISK_RT_OK)
          return captured;
      }
    } else if (node.flags & CrossSelectorScalarWith) {
      const auto *predicate = expressionValue(node.withExpression);
      if (!predicate ||
          (predicate->kind != OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL &&
           predicate->kind != OBELISK_RT_FUNCTIONAL_VALUE_FOUR_STATE) ||
          predicate->bit_width != 1 || predicate->value_size != 1 ||
          !predicate->value)
        return OBELISK_RT_INVALID_DESIGN;
      bool truth = (*static_cast<const uint8_t *>(predicate->value) & 1) &&
                   (!predicate->unknown ||
                    !(*static_cast<const uint8_t *>(predicate->unknown) & 1));
      resolvedBindingFlags = ResolvedCrossSelectorScalarWith |
                             (truth ? ResolvedCrossSelectorScalarTrue : 0);
      resolvedMatchesPolicy = node.matchesPolicy;
      resolvedMatchesCount = node.matchesCount;

    } else if (node.constructionExpression || node.withExpression) {
      const auto *staticPlan = lookup(templateCrossPlans, node.cross);
      const auto *construction =
          expressionValue(node.withExpression ? node.withExpression
                                              : node.constructionExpression);
      if (!staticPlan || !construction ||
          construction->kind != OBELISK_RT_FUNCTIONAL_VALUE_MANAGED_CONTAINER ||
          uint64_t{staticPlan->firstTarget} + staticPlan->targetCount >
              schema.crossTargets.size())
        return OBELISK_RT_INVALID_DESIGN;

      CoverageTupleQueueSnapshot snapshot;
      obelisk_rt_status snapshotStatus =
          obelisk_rt_coverage_tuple_queue_snapshot(
              context, construction->owner, staticPlan->tupleElementType,
              staticPlan->tupleProvenanceSpan,
              (staticPlan->tupleFlags & CrossTupleFourState) != 0,
              node.withExpression ? MaxFunctionalCrossWithCandidates
                                  : ParseLimits{}.maxRecords,
              snapshot);
      if (snapshotStatus != OBELISK_RT_OK)
        return snapshotStatus;

      struct TupleComponentValue {
        const CrossTarget *target = nullptr;
        uint64_t resolvedItem = 0;
        std::vector<uint64_t> aval;
        std::vector<uint64_t> bval;
        uint64_t realBits = 0;
      };
      struct TupleValue {
        std::vector<TupleComponentValue> components;
        std::vector<uint8_t> key;
      };
      std::vector<TupleValue> values;
      IndexSet<std::vector<uint8_t>> uniqueValues;
      std::vector<uint8_t> usedBits(snapshot.valueSize);
      auto bitAt = [](const uint8_t *bytes, uint64_t bit) {
        return (bytes[bit / 8] >> (bit % 8)) & 1;
      };
      auto setBit = [](std::vector<uint8_t> &bytes, uint64_t bit, bool value) {
        if (value)
          bytes[bit / 8] |= uint8_t{1} << (bit % 8);
      };
      for (uint32_t ordinal = 0; ordinal != staticPlan->targetCount;
           ++ordinal) {
        const CrossTarget &target =
            schema.crossTargets[staticPlan->firstTarget + ordinal];
        if (target.cross != node.cross || target.ordinal != ordinal)
          return OBELISK_RT_INVALID_DESIGN;
        for (uint32_t bit = 0; bit != target.tupleBitWidth; ++bit)
          setBit(usedBits, target.tupleBitOffset + bit, true);
      }
      for (uint64_t tupleOrdinal = 0; tupleOrdinal != snapshot.count;
           ++tupleOrdinal) {
        const uint8_t *encoded =
            snapshot.value.data() + tupleOrdinal * snapshot.valueSize;
        const uint8_t *unknown =
            snapshot.fourState
                ? snapshot.unknown.data() + tupleOrdinal * snapshot.valueSize
                : nullptr;
        for (uint64_t byte = 0; byte != snapshot.valueSize; ++byte)
          if ((encoded[byte] & ~usedBits[byte]) ||
              (unknown && (unknown[byte] & ~usedBits[byte])))
            return OBELISK_RT_INVALID_DESIGN;

        TupleValue tuple;
        tuple.key.reserve(snapshot.valueSize * (snapshot.fourState ? 2 : 1));
        tuple.key.insert(tuple.key.end(), encoded,
                         encoded + snapshot.valueSize);
        if (unknown)
          tuple.key.insert(tuple.key.end(), unknown,
                           unknown + snapshot.valueSize);
        tuple.components.reserve(staticPlan->targetCount);
        bool usable = true;
        for (uint32_t ordinal = 0; ordinal != staticPlan->targetCount;
             ++ordinal) {
          const CrossTarget &target =
              schema.crossTargets[staticPlan->firstTarget + ordinal];
          auto resolvedTarget = resolvedItems.find(target.target);
          if (resolvedTarget == resolvedItems.end())
            return OBELISK_RT_INVALID_DESIGN;
          TupleComponentValue component;
          component.target = &target;
          component.resolvedItem = resolvedTarget->second;
          const uint32_t limbCount = functionalLimbCount(target.tupleBitWidth);
          component.aval.assign(limbCount, 0);
          component.bval.assign(limbCount, 0);
          for (uint32_t bit = 0; bit != target.tupleBitWidth; ++bit) {
            const uint64_t sourceBit = target.tupleBitOffset + bit;
            const bool bval = unknown && bitAt(unknown, sourceBit);
            const bool aval = bitAt(encoded, sourceBit) ^ bval;
            if (aval)
              component.aval[bit / 64] |= uint64_t{1} << (bit % 64);
            if (bval)
              component.bval[bit / 64] |= uint64_t{1} << (bit % 64);
          }
          if (!(target.tupleFlags & CrossTupleFieldFourState) &&
              std::any_of(component.bval.begin(), component.bval.end(),
                          [](uint64_t limb) { return limb != 0; }))
            return OBELISK_RT_INVALID_DESIGN;
          if (isRealResultKind(target.tupleResultKind)) {
            double real = 0.0;
            uint64_t encodedBits = component.aval.front();
            if (target.tupleResultKind ==
                FunctionalExpressionResultKind::ShortReal) {
              float shortReal;
              uint32_t bits = static_cast<uint32_t>(encodedBits);
              std::memcpy(&shortReal, &bits, sizeof(shortReal));
              real = shortReal;
            } else
              std::memcpy(&real, &encodedBits, sizeof(real));
            std::memcpy(&component.realBits, &real, sizeof(real));
            if (!std::isfinite(real)) {
              usable = false;
              break;
            }
            if (real == 0.0)
              component.realBits = 0;
          }
          tuple.components.push_back(std::move(component));
        }
        if (!usable)
          continue;
        // Numeric real equality treats -0.0 and +0.0 as the same CrossVal
        // component. Rebuild the key from canonical component planes.
        tuple.key.clear();
        for (const TupleComponentValue &component : tuple.components) {
          if (isRealResultKind(component.target->tupleResultKind)) {
            for (uint32_t byte = 0; byte != sizeof(uint64_t); ++byte)
              tuple.key.push_back(
                  static_cast<uint8_t>(component.realBits >> (byte * 8)));
            continue;
          }
          for (uint64_t limb : component.aval)
            for (uint32_t byte = 0; byte != sizeof(uint64_t); ++byte)
              tuple.key.push_back(static_cast<uint8_t>(limb >> (byte * 8)));
          for (uint64_t limb : component.bval)
            for (uint32_t byte = 0; byte != sizeof(uint64_t); ++byte)
              tuple.key.push_back(static_cast<uint8_t>(limb >> (byte * 8)));
        }
        if (uniqueValues.insert(tuple.key).second)
          values.push_back(std::move(tuple));
      }
      sortByKey(
          values.begin(), values.end(),
          [](const TupleValue &value) -> const auto & { return value.key; });

      resolvedMatchesPolicy = node.matchesPolicy;
      resolvedMatchesCount = node.matchesCount;

      resolvedTupleSetID =
          resolvedFunctionalID("functional.resolved.selector-set-tuples",
                               typeID, node.id, 0, tupleSetIDs);
      if (!resolvedTupleSetID)
        return OBELISK_RT_INVALID_DESIGN;
      tupleSetIDs.insert(resolvedTupleSetID);
      PendingTupleSet pendingTupleSet;
      pendingTupleSet.set = {typeID,
                             {},
                             resolvedTupleSetID,
                             0,
                             resolvedCross->second,
                             node.id,
                             0,
                             0,
                             FunctionalTupleElementMode::ValueTuple,
                             0};
      if (values.size() > UINT32_MAX)
        return OBELISK_RT_OUT_OF_RESOURCES;
      for (uint32_t tupleOrdinal = 0; tupleOrdinal != values.size();
           ++tupleOrdinal) {
        uint64_t tupleID =
            resolvedFunctionalID("functional.resolved.selector-set-tuple",
                                 typeID, node.id, tupleOrdinal, tupleIDs);
        if (!tupleID)
          return OBELISK_RT_INVALID_DESIGN;
        tupleIDs.insert(tupleID);
        if (values[tupleOrdinal].components.size() > UINT32_MAX ||
            pendingTupleSet.components.size() >
                UINT32_MAX - values[tupleOrdinal].components.size())
          return OBELISK_RT_OUT_OF_RESOURCES;
        const uint32_t firstComponent = pendingTupleSet.components.size();
        for (uint32_t componentOrdinal = 0;
             componentOrdinal != values[tupleOrdinal].components.size();
             ++componentOrdinal) {
          const TupleComponentValue &component =
              values[tupleOrdinal].components[componentOrdinal];
          const uint64_t wideStableOrdinal =
              uint64_t{tupleOrdinal} * staticPlan->targetCount +
              componentOrdinal;
          if (wideStableOrdinal > UINT32_MAX)
            return OBELISK_RT_OUT_OF_RESOURCES;
          const uint32_t stableOrdinal =
              static_cast<uint32_t>(wideStableOrdinal);
          uint64_t componentSetID =
              resolvedFunctionalID("functional.resolved.selector-set-value",
                                   typeID, node.id, stableOrdinal, valueSetIDs);
          if (!componentSetID)
            return OBELISK_RT_INVALID_DESIGN;
          valueSetIDs.insert(componentSetID);
          PendingSet base;
          base.set = {typeID,
                      {},
                      0,
                      0,
                      component.resolvedItem,
                      0,
                      0,
                      isRealResultKind(component.target->tupleResultKind)
                          ? 64
                          : component.target->tupleBitWidth,
                      isRealResultKind(component.target->tupleResultKind)
                          ? FunctionalValueSetKind::Real
                          : FunctionalValueSetKind::Integral,
                      0,
                      component.target->tupleSignedness,
                      0,
                      node.id,
                      tupleOrdinal,
                      componentOrdinal,
                      ResolvedFunctionalValueSetRole::TupleComponent};
          PendingAtom atom;
          atom.atom.kind = isRealResultKind(component.target->tupleResultKind)
                               ? FunctionalValueAtomKind::RealInterval
                               : FunctionalValueAtomKind::IntegralValue;
          atom.atom.flags = FunctionalValueAtomLowerInclusive |
                            FunctionalValueAtomUpperInclusive;
          if (atom.atom.kind == FunctionalValueAtomKind::RealInterval) {
            atom.atom.realLowBits = component.realBits;
            atom.atom.realHighBits = component.realBits;
          } else {
            atom.atom.limbCount = component.aval.size();
            for (uint32_t limb = 0; limb != component.aval.size(); ++limb)
              atom.limbs.push_back({0, 0, limb, component.aval[limb],
                                    component.bval[limb], component.aval[limb],
                                    component.bval[limb], 0});
          }
          pendingSets.push_back(
              packPendingSet(base, {atom}, componentSetID, 0, tupleOrdinal));
          pendingSets.back().set.ownerBin = 0;
          pendingSets.back().set.ownerOrdinal = tupleOrdinal;
          pendingSets.back().set.ownerSubordinal = componentOrdinal;
          pendingTupleSet.components.push_back({tupleID, component.resolvedItem,
                                                0, componentSetID,
                                                componentOrdinal, 0});
        }
        pendingTupleSet.tuples.push_back(
            {resolvedTupleSetID, tupleID, firstComponent,
             static_cast<uint32_t>(values[tupleOrdinal].components.size()),
             tupleOrdinal, 0});
      }
      pendingTupleSet.set.tupleCount = pendingTupleSet.tuples.size();
      pendingTupleSets.push_back(std::move(pendingTupleSet));
    }
    if (resolvedMatchesPolicy == CrossMatchesPolicy::Count) {
      if (node.matchesExpression) {
        const auto *descriptor = expressionSchema(node.matchesExpression);
        const auto *value = expressionValue(node.matchesExpression);
        FunctionalInteger integer;
        bool unknown = false;
        if (!descriptor || !value ||
            !decodeFunctionalInteger(*value, descriptor->signedness, integer,
                                     unknown) ||
            unknown || integer.negative || integer.magnitude.empty())
          return OBELISK_RT_INVALID_DESIGN;
        if (integer.magnitude.size() > 1) {
          obelisk_rt_status captured = captureSelectorInteger(
              node.id, resolvedCross->second, 0, *descriptor, *value,
              ResolvedFunctionalValueSetRole::MatchesCount);
          if (captured != OBELISK_RT_OK)
            return captured;
          resolvedBindingFlags |= ResolvedCrossSelectorWideMatches;
          resolvedMatchesCount = 0;
        } else
          resolvedMatchesCount = integer.magnitude.front();
      }
      if (!resolvedMatchesCount &&
          !(resolvedBindingFlags & ResolvedCrossSelectorWideMatches))
        return OBELISK_RT_INVALID_DESIGN;
    } else if (resolvedMatchesPolicy != CrossMatchesPolicy::None &&
               resolvedMatchesPolicy != CrossMatchesPolicy::All)
      return OBELISK_RT_INVALID_DESIGN;
    if (!resolvedSetID && !resolvedTupleSetID && !resolvedBindingFlags)
      continue;
    resolvedSelectorBindingRows.push_back({typeID,
                                           {},
                                           resolvedCross->second,
                                           node.id,
                                           resolvedSetID,
                                           node.withExpression,
                                           resolvedTupleSetID,
                                           resolvedMatchesPolicy,
                                           resolvedBindingFlags,
                                           resolvedMatchesCount});
  }
  sortByKey(pendingTupleSets.begin(), pendingTupleSets.end(),
            [](const auto &pending) { return pending.set.id; });
  std::vector<ResolvedFunctionalTupleSet> resolvedTupleSetRows;
  std::vector<ResolvedFunctionalTuple> resolvedTupleRows;
  std::vector<ResolvedFunctionalTupleComponent> resolvedTupleComponentRows;
  for (PendingTupleSet &pending : pendingTupleSets) {
    pending.set.firstTuple = resolvedTupleRows.size();
    for (ResolvedFunctionalTuple &tuple : pending.tuples) {
      tuple.firstComponent += resolvedTupleComponentRows.size();
      resolvedTupleRows.push_back(tuple);
    }
    resolvedTupleComponentRows.insert(resolvedTupleComponentRows.end(),
                                      pending.components.begin(),
                                      pending.components.end());
    resolvedTupleSetRows.push_back(pending.set);
  }
  sortByKey(resolvedSelectorBindingRows.begin(),
            resolvedSelectorBindingRows.end(),
            [](const auto &row) { return std::tie(row.cross, row.node); });
  IndexMap<uint64_t, const ResolvedFunctionalItem *> pendingItems;
  for (const auto &row : resolvedItemRows)
    pendingItems.emplace(row.id, &row);
  IndexMap<uint64_t, const ResolvedFunctionalBin *> pendingBins;
  for (const auto &row : resolvedBinRows)
    pendingBins.emplace(row.id, &row);
  sortByKey(
      pendingSets.begin(), pendingSets.end(), [&](const PendingSet &pending) {
        auto item = pendingItems.find(pending.set.item);
        auto bin = pendingBins.find(pending.set.ownerBin);
        return std::make_tuple(
            pending.set.templateValueSet,
            static_cast<uint32_t>(pending.set.role),
            item == pendingItems.end() ? uint64_t{0}
                                       : item->second->templateItem,
            bin == pendingBins.end() ? uint64_t{0} : bin->second->templateBin,
            bin == pendingBins.end() ? uint32_t{0}
                                     : bin->second->expansionOrdinal,
            pending.set.ownerSelector, pending.set.ownerOrdinal,
            pending.set.ownerSubordinal);
      });
  std::vector<ResolvedFunctionalValueSet> resolvedSetRows;
  std::vector<ResolvedFunctionalValueAtom> resolvedAtomRows;
  std::vector<ResolvedFunctionalValueLimb> resolvedLimbRows;
  const uint64_t valueRecordLimit =
      std::min<uint64_t>(UINT32_MAX, ParseLimits{}.maxRecords);
  if (pendingSets.size() > valueRecordLimit)
    return OBELISK_RT_OUT_OF_RESOURCES;
  for (PendingSet &pending : pendingSets) {
    if (pending.atoms.size() > valueRecordLimit - resolvedAtomRows.size() ||
        pending.limbs.size() > valueRecordLimit - resolvedLimbRows.size())
      return OBELISK_RT_OUT_OF_RESOURCES;
    pending.set.firstAtom = resolvedAtomRows.size();
    for (auto atom : pending.atoms) {
      atom.firstLimb += resolvedLimbRows.size();
      resolvedAtomRows.push_back(atom);
    }
    resolvedLimbRows.insert(resolvedLimbRows.end(), pending.limbs.begin(),
                            pending.limbs.end());
    resolvedSetRows.push_back(pending.set);
  }

  // Derive configuration identity over a temporary zero-key bundle.  The
  // digest deliberately excludes resolved IDs but includes resolved semantics.
  Database fingerprintLocal;
  std::unique_ptr<Database> fingerprintOwned;
  if (prepared)
    fingerprintOwned = std::make_unique<Database>(schema);
  else
    fingerprintLocal = schema;
  Database &fingerprintInput = prepared ? *fingerprintOwned : fingerprintLocal;
  uint32_t binBase = fingerprintInput.resolvedFunctionalBins.size();
  uint32_t atomBase = fingerprintInput.resolvedFunctionalValueAtoms.size();
  uint32_t limbBase = fingerprintInput.resolvedFunctionalValueLimbs.size();
  uint32_t transitionStepBase = fingerprintInput.resolvedTransitionSteps.size();
  uint32_t transitionAlternativeBase =
      fingerprintInput.resolvedTransitionAlternatives.size();
  uint32_t tupleBase = fingerprintInput.resolvedFunctionalTupleSetTuples.size();
  uint32_t tupleComponentBase =
      fingerprintInput.resolvedFunctionalTupleSetComponents.size();
  for (auto &group : resolvedGroupRows)
    group.firstBin += binBase;
  for (auto &set : resolvedSetRows)
    set.firstAtom += atomBase;
  for (auto &atom : resolvedAtomRows)
    atom.firstLimb += limbBase;
  for (auto &alternative : resolvedTransitionAlternativeRows)
    alternative.firstStep += transitionStepBase;
  for (auto &group : resolvedTransitionExpansionGroupRows)
    group.firstAlternative += transitionAlternativeBase;
  for (auto &set : resolvedTupleSetRows)
    set.firstTuple += tupleBase;
  for (auto &tuple : resolvedTupleRows)
    tuple.firstComponent += tupleComponentBase;
  fingerprintInput.resolvedFunctionalItems.insert(
      fingerprintInput.resolvedFunctionalItems.end(), resolvedItemRows.begin(),
      resolvedItemRows.end());
  fingerprintInput.resolvedFunctionalBins.insert(
      fingerprintInput.resolvedFunctionalBins.end(), resolvedBinRows.begin(),
      resolvedBinRows.end());
  fingerprintInput.resolvedFunctionalValueSets.insert(
      fingerprintInput.resolvedFunctionalValueSets.end(),
      resolvedSetRows.begin(), resolvedSetRows.end());
  fingerprintInput.resolvedFunctionalValueAtoms.insert(
      fingerprintInput.resolvedFunctionalValueAtoms.end(),
      resolvedAtomRows.begin(), resolvedAtomRows.end());
  fingerprintInput.resolvedFunctionalValueLimbs.insert(
      fingerprintInput.resolvedFunctionalValueLimbs.end(),
      resolvedLimbRows.begin(), resolvedLimbRows.end());
  fingerprintInput.resolvedFunctionalBinPlans.insert(
      fingerprintInput.resolvedFunctionalBinPlans.end(),
      resolvedPlanRows.begin(), resolvedPlanRows.end());
  fingerprintInput.resolvedFunctionalBinGroups.insert(
      fingerprintInput.resolvedFunctionalBinGroups.end(),
      resolvedGroupRows.begin(), resolvedGroupRows.end());
  fingerprintInput.resolvedTransitionSteps.insert(
      fingerprintInput.resolvedTransitionSteps.end(),
      resolvedTransitionStepRows.begin(), resolvedTransitionStepRows.end());
  fingerprintInput.resolvedTransitionAlternatives.insert(
      fingerprintInput.resolvedTransitionAlternatives.end(),
      resolvedTransitionAlternativeRows.begin(),
      resolvedTransitionAlternativeRows.end());
  fingerprintInput.resolvedTransitionExpansionGroups.insert(
      fingerprintInput.resolvedTransitionExpansionGroups.end(),
      resolvedTransitionExpansionGroupRows.begin(),
      resolvedTransitionExpansionGroupRows.end());
  fingerprintInput.functionalConfigurationOptions.insert(
      fingerprintInput.functionalConfigurationOptions.end(),
      configurationOptionRows.begin(), configurationOptionRows.end());
  fingerprintInput.resolvedCrossSelectorBindings.insert(
      fingerprintInput.resolvedCrossSelectorBindings.end(),
      resolvedSelectorBindingRows.begin(), resolvedSelectorBindingRows.end());
  fingerprintInput.resolvedFunctionalTupleSets.insert(
      fingerprintInput.resolvedFunctionalTupleSets.end(),
      resolvedTupleSetRows.begin(), resolvedTupleSetRows.end());
  fingerprintInput.resolvedFunctionalTupleSetTuples.insert(
      fingerprintInput.resolvedFunctionalTupleSetTuples.end(),
      resolvedTupleRows.begin(), resolvedTupleRows.end());
  fingerprintInput.resolvedFunctionalTupleSetComponents.insert(
      fingerprintInput.resolvedFunctionalTupleSetComponents.end(),
      resolvedTupleComponentRows.begin(), resolvedTupleComponentRows.end());
  auto queryStorage = std::make_unique<FunctionalCoverageSetQueryIndex>(
      fingerprintInput, typeID, Digest{});
  auto &setQueries = *queryStorage;
  obelisk_rt_status stateExclusionStatus = markStaticallyEmptyStateBins(
      fingerprintInput, typeID, Digest{}, setQueries);
  if (stateExclusionStatus != OBELISK_RT_OK)
    return stateExclusionStatus;
  std::unordered_set<uint64_t> suppressedTransitionAlternatives;
  std::unordered_set<uint64_t> emptyTransitionBins;
  obelisk_rt_status transitionExclusionStatus = deriveTransitionExclusions(
      fingerprintInput, typeID, Digest{}, suppressedTransitionAlternatives,
      emptyTransitionBins, setQueries);
  if (transitionExclusionStatus != OBELISK_RT_OK)
    return transitionExclusionStatus;
  for (auto &bin : fingerprintInput.resolvedFunctionalBins)
    if (bin.type == typeID && bin.configuration == Digest{} &&
        emptyTransitionBins.count(bin.id))
      bin.flags |= FunctionalBinEmpty;
  for (size_t index = 0; index != resolvedBinRows.size(); ++index) {
    auto &row = resolvedBinRows[index];
    const auto &resolved =
        fingerprintInput.resolvedFunctionalBins[binBase + index];
    if (resolved.type != typeID || resolved.configuration != Digest{} ||
        resolved.id != row.id)
      return OBELISK_RT_INVALID_DESIGN;
    row.flags = resolved.flags;
  }

  if (prepared) {
    prepared->schema = std::move(fingerprintOwned);
    prepared->queries = std::move(queryStorage);
    prepared->suppressedTransitions =
        std::move(suppressedTransitionAlternatives);
    constexpr uint32_t omitted =
        FunctionalBinDefault | FunctionalBinDefaultSequence |
        FunctionalBinIgnore | FunctionalBinIllegal | FunctionalBinEmpty;
    for (const auto &bin : prepared->schema->resolvedFunctionalBins)
      if (bin.type == typeID && bin.configuration == Digest{} &&
          (bin.kind == FunctionalBinKind::State ||
           bin.kind == FunctionalBinKind::Transition) &&
          !(bin.flags & omitted))
        prepared->binsByItem[bin.item].push_back(bin.id);
    for (auto &[item, bins] : prepared->binsByItem) {
      (void)item;
      sortByKey(bins.begin(), bins.end(), [](uint64_t id) { return id; });
    }
    for (const auto &binding :
         prepared->schema->resolvedCrossSelectorBindings) {
      auto count = setQueries.matchCount(binding);
      if (detail::DecisionCountProjectionIndex::compareMagnitudes(
              count, prepared->maximumMatchCount) > 0)
        prepared->maximumMatchCount = std::move(count);
    }
    for (const auto &node : prepared->schema->crossSelectorNodes) {
      if (!node.withExpression || (node.flags & CrossSelectorScalarWith))
        continue;
      uint64_t count = node.matchesCount;
      if (node.matchesPolicy == CrossMatchesPolicy::Count) {
        constexpr uint64_t cap = uint64_t{MaxFunctionalCrossWithCandidates} + 1;
        if (node.matchesExpression) {
          const auto *descriptor = expressionSchema(node.matchesExpression);
          const auto *value = expressionValue(node.matchesExpression);
          FunctionalInteger integer;
          bool unknown = false;
          if (!descriptor || !value ||
              !decodeFunctionalInteger(*value, descriptor->signedness, integer,
                                       unknown) ||
              unknown || integer.negative)
            return OBELISK_RT_INVALID_DESIGN;
          count = integer.magnitude.empty() ? 0
                  : integer.magnitude.size() > 1
                      ? cap
                      : std::min(integer.magnitude.front(), cap);
        } else
          count = std::min(count, cap);
        if (!count)
          return OBELISK_RT_INVALID_DESIGN;
      } else if (node.matchesPolicy != CrossMatchesPolicy::All)
        return OBELISK_RT_INVALID_DESIGN;
      prepared->withMatches.emplace(node.id, count);
      FunctionalMagnitude magnitude =
          count ? FunctionalMagnitude{count} : FunctionalMagnitude{};
      if (detail::DecisionCountProjectionIndex::compareMagnitudes(
              magnitude, prepared->maximumMatchCount) > 0)
        prepared->maximumMatchCount = std::move(magnitude);
    }
    return OBELISK_RT_OK;
  }

  PreparedFunctionalConstruction selectionIndex;
  initializeSelectionIndex(selectionIndex, fingerprintInput, setQueries, typeID,
                           Digest{}, suppressedTransitionAlternatives);
  IndexMap<std::pair<uint64_t, uint64_t>, size_t> resolvedBinTemplates;
  IndexMap<uint64_t, std::vector<const ResolvedFunctionalBin *>> orderedBins;
  for (size_t ordinal = 0; ordinal != resolvedBinRows.size(); ++ordinal) {
    const auto &bin = resolvedBinRows[ordinal];
    resolvedBinTemplates.emplace(std::make_pair(bin.item, bin.templateBin),
                                 ordinal);
    constexpr uint32_t omitted =
        FunctionalBinDefault | FunctionalBinDefaultSequence |
        FunctionalBinIgnore | FunctionalBinIllegal | FunctionalBinEmpty;
    if ((bin.kind == FunctionalBinKind::State ||
         bin.kind == FunctionalBinKind::Transition) &&
        !(bin.flags & omitted))
      orderedBins[bin.item].push_back(&bin);
  }
  for (auto &[item, bins] : orderedBins) {
    (void)item;
    sortByKey(bins.begin(), bins.end(),
              [](const auto *bin) { return std::tie(bin->name, bin->id); });
  }

  struct PendingCrossNode {
    ResolvedCrossAutomaticNode node;
    std::vector<ResolvedCrossAutomaticEdge> edges;
  };
  struct PendingCross {
    ResolvedCrossPlan plan;
    FunctionalMagnitude automaticBinCount;
    std::vector<PendingCrossNode> nodes;
  };
  std::unordered_set<uint64_t> crossNodeIDs;
  std::vector<const ResolvedFunctionalItem *> resolvedCrossItems;
  for (const auto &item : resolvedItemRows)
    if (item.kind == FunctionalItemKind::Cross)
      resolvedCrossItems.push_back(&item);
  sortByKey(resolvedCrossItems.begin(), resolvedCrossItems.end(),
            [](const auto *item) { return item->id; });
  const uint64_t recordLimit = ParseLimits{}.maxRecords;
  std::vector<PendingCross> pendingCrosses;
  uint64_t generatedCrossPlanCount = 0;
  uint64_t generatedCrossLimbCount = 0;
  uint64_t generatedCrossNodeCount = 0;
  uint64_t generatedCrossEdgeCount = 0;
  auto recordsFit = [&](size_t existing, uint64_t generated, size_t addition) {
    return existing <= recordLimit && generated <= recordLimit - existing &&
           addition <= recordLimit - existing - generated;
  };
  auto appendPendingCross = [&](PendingCross &&pending) {
    uint64_t edgeCount = 0;
    for (const PendingCrossNode &node : pending.nodes) {
      if (node.edges.size() > recordLimit - edgeCount)
        return false;
      edgeCount += node.edges.size();
    }
    if (!recordsFit(fingerprintInput.resolvedCrossPlans.size(),
                    generatedCrossPlanCount, 1) ||
        !recordsFit(fingerprintInput.resolvedCrossAutomaticBinCountLimbs.size(),
                    generatedCrossLimbCount,
                    pending.automaticBinCount.size()) ||
        !recordsFit(fingerprintInput.resolvedCrossAutomaticNodes.size(),
                    generatedCrossNodeCount, pending.nodes.size()) ||
        !recordsFit(fingerprintInput.resolvedCrossAutomaticEdges.size(),
                    generatedCrossEdgeCount, edgeCount))
      return false;
    ++generatedCrossPlanCount;
    generatedCrossLimbCount += pending.automaticBinCount.size();
    generatedCrossNodeCount += pending.nodes.size();
    generatedCrossEdgeCount += edgeCount;
    pendingCrosses.push_back(std::move(pending));
    return true;
  };
  for (const ResolvedFunctionalItem *cross : resolvedCrossItems) {
    const auto *staticPlan = lookup(templateCrossPlans, cross->templateItem);
    if (!staticPlan || staticPlan->targetCount < 2 ||
        uint64_t{staticPlan->firstTarget} + staticPlan->targetCount >
            schema.crossTargets.size() ||
        uint64_t{staticPlan->firstBin} + staticPlan->binCount >
            schema.crossBins.size())
      return OBELISK_RT_INVALID_DESIGN;

    PendingCross pending;
    CrossRetainAutoPolicy retain = staticPlan->retainAutoPolicy;
    if (retain == CrossRetainAutoPolicy::Deferred)
      retain = effectiveCrossRetainAutoBins(cross->id)
                   ? CrossRetainAutoPolicy::Retain
                   : CrossRetainAutoPolicy::Discard;
    if (retain != CrossRetainAutoPolicy::Retain &&
        retain != CrossRetainAutoPolicy::Discard)
      return OBELISK_RT_INVALID_DESIGN;
    pending.plan = {typeID, {}, cross->id, retain, 0, 0, 0, 0};

    std::vector<std::vector<const ResolvedFunctionalBin *>> targetBins;
    targetBins.reserve(staticPlan->targetCount);
    for (uint32_t ordinal = 0; ordinal != staticPlan->targetCount; ++ordinal) {
      const CrossTarget &staticTarget =
          schema.crossTargets[staticPlan->firstTarget + ordinal];
      if (staticTarget.cross != cross->templateItem ||
          staticTarget.ordinal != ordinal)
        return OBELISK_RT_INVALID_DESIGN;
      auto target = resolvedItems.find(staticTarget.target);
      if (target == resolvedItems.end())
        return OBELISK_RT_INVALID_DESIGN;
      const auto &bins = orderedBins[target->second];
      if (bins.size() > UINT32_MAX)
        return OBELISK_RT_OUT_OF_RESOURCES;
      targetBins.push_back(bins);
    }
    const auto &allTargetBins = targetBins;
    uint64_t fullRectangleBinEntryCount = 0;
    for (const auto &bins : allTargetBins) {
      if (bins.size() > recordLimit - fullRectangleBinEntryCount)
        return OBELISK_RT_OUT_OF_RESOURCES;
      fullRectangleBinEntryCount += bins.size();
    }
    std::unordered_map<uint64_t, const ResolvedFunctionalBin *> binsByID;
    for (const auto &bins : allTargetBins)
      for (const ResolvedFunctionalBin *bin : bins)
        binsByID.emplace(bin->id, bin);

    std::shared_ptr<PreparedCrossValueIndex> symbolic;
    for (uint32_t index = 0; index != staticPlan->binCount; ++index) {
      const auto &bin = schema.crossBins[staticPlan->firstBin + index];
      const auto *root = setQueries.lookup(setQueries.selectors,
                                           fingerprintInput.crossSelectorNodes,
                                           bin.rootSelector);
      if (!root)
        return OBELISK_RT_INVALID_DESIGN;
      if (crossSelectorUsesWith(schema, *root, setQueries)) {
        symbolic = selectionKernel(selectionIndex, *staticPlan);
        if (!symbolic->check())
          return symbolic->status;
        break;
      }
    }
    PreparedCrossValueIndex::Root authored = 0;

    struct SelectorRectangle {
      std::vector<std::vector<uint64_t>> allowedBins;
    };
    std::vector<SelectorRectangle> selectorRectangles;
    selectorRectangles.reserve(staticPlan->binCount);
    uint64_t selectorRectangleBinEntryCount = 0;

    // IEEE 1800-2023 19.6.1: AND intersects selected products and OR unions
    // them. Convert the Boolean selector to a bounded union of Cartesian
    // rectangles. A user-defined cross bin owns the resulting tuples
    // regardless of its iff guard, so every rectangle participates in
    // retained-auto subtraction, including ignore and illegal bins.
    for (uint32_t binIndex = 0; binIndex != staticPlan->binCount; ++binIndex) {
      const CrossBinPlan &crossBin =
          schema.crossBins[staticPlan->firstBin + binIndex];
      const auto *node = setQueries.lookup(setQueries.selectors,
                                           fingerprintInput.crossSelectorNodes,
                                           crossBin.rootSelector);
      CrossSelectorDNF alternatives;
      if (crossBin.cross != cross->templateItem || !node ||
          node->cross != cross->templateItem)
        return OBELISK_RT_INVALID_DESIGN;

      bool hasSignificantAlternative = false;
      if (symbolic) {
        auto selected = symbolic->select(*node, cross->id, 0);
        selected = symbolic->relation->intersect(
            selected, symbolic->binImage->project(symbolic->universe));
        authored = symbolic->relation->unite(authored, selected);
        if (!symbolic->check())
          return symbolic->status;
        hasSignificantAlternative = selected != 0;
      } else {
        const bool usesSet = crossSelectorUsesSet(schema, *node, setQueries);
        obelisk_rt_status selectorStatus = decodeCrossSelectorDNF(
            schema, *node, recordLimit, alternatives, usesSet);
        if (selectorStatus != OBELISK_RT_OK)
          return selectorStatus;
        for (const CrossSelectorConjunction &terms : alternatives) {
          // Bound both the retained rectangle payload and the full temporary
          // rectangle populated below. Filtering may shrink the latter, but it
          // must not permit an unbounded transient allocation first.
          if (fullRectangleBinEntryCount >
              recordLimit - selectorRectangleBinEntryCount)
            return OBELISK_RT_OUT_OF_RESOURCES;
          SelectorRectangle rectangle;
          rectangle.allowedBins.reserve(allTargetBins.size());
          for (const auto &bins : allTargetBins) {
            std::vector<uint64_t> ids;
            ids.reserve(bins.size());
            for (const ResolvedFunctionalBin *bin : bins)
              ids.push_back(bin->id);
            std::sort(ids.begin(), ids.end());
            rectangle.allowedBins.push_back(std::move(ids));
          }
          for (const CrossSelectorTerm &term : terms) {
            const CrossSelectorNode *condition = term.condition;
            auto staticTarget = std::find_if(
                schema.crossTargets.begin() + staticPlan->firstTarget,
                schema.crossTargets.begin() + staticPlan->firstTarget +
                    staticPlan->targetCount,
                [&](const CrossTarget &target) {
                  return target.target == condition->target;
                });
            if (staticTarget == schema.crossTargets.begin() +
                                    staticPlan->firstTarget +
                                    staticPlan->targetCount)
              return OBELISK_RT_INVALID_DESIGN;
            const ResolvedFunctionalValueSet *selectedSet = nullptr;
            if (condition->valueSet) {
              auto binding = std::find_if(
                  resolvedSelectorBindingRows.begin(),
                  resolvedSelectorBindingRows.end(),
                  [&](const ResolvedCrossSelectorBinding &candidate) {
                    return candidate.cross == cross->id &&
                           candidate.node == condition->id;
                  });
              auto set =
                  binding == resolvedSelectorBindingRows.end()
                      ? resolvedSetRows.end()
                      : std::find_if(
                            resolvedSetRows.begin(), resolvedSetRows.end(),
                            [&](const ResolvedFunctionalValueSet &candidate) {
                              return candidate.id == binding->valueSet;
                            });
              if (binding == resolvedSelectorBindingRows.end() ||
                  set == resolvedSetRows.end())
                return OBELISK_RT_INVALID_DESIGN;
              selectedSet = &*set;
            }
            obelisk_rt_status selectionStatus = OBELISK_RT_OK;
            auto isSelected = [&](uint64_t binID) {
              if (selectionStatus != OBELISK_RT_OK)
                return false;
              auto found = binsByID.find(binID);
              if (found == binsByID.end())
                return false;
              const ResolvedFunctionalBin *bin = found->second;
              bool selected =
                  !condition->bin || bin->templateBin == condition->bin;
              if (selected && selectedSet) {
                bool intersects = false;
                selectionStatus = resolvedFunctionalBinIntersectsValueSet(
                    fingerprintInput, typeID, Digest{}, bin->id, *selectedSet,
                    suppressedTransitionAlternatives, setQueries, intersects);
                selected = selectionStatus == OBELISK_RT_OK && intersects;
              }
              return term.negated ? !selected : selected;
            };
            auto &allowed = rectangle.allowedBins[staticTarget->ordinal];
            allowed.erase(
                std::remove_if(allowed.begin(), allowed.end(),
                               [&](uint64_t id) { return !isSelected(id); }),
                allowed.end());
            if (selectionStatus != OBELISK_RT_OK)
              return selectionStatus;
          }
          const bool significant = std::all_of(
              rectangle.allowedBins.begin(), rectangle.allowedBins.end(),
              [](const auto &bins) { return !bins.empty(); });
          if (!significant)
            continue;
          if (selectorRectangles.size() >= recordLimit)
            return OBELISK_RT_OUT_OF_RESOURCES;
          uint64_t rectangleBinEntryCount = 0;
          for (const auto &bins : rectangle.allowedBins) {
            if (bins.size() > recordLimit - rectangleBinEntryCount)
              return OBELISK_RT_OUT_OF_RESOURCES;
            rectangleBinEntryCount += bins.size();
          }
          if (rectangleBinEntryCount >
              recordLimit - selectorRectangleBinEntryCount)
            return OBELISK_RT_OUT_OF_RESOURCES;
          selectorRectangleBinEntryCount += rectangleBinEntryCount;
          hasSignificantAlternative = true;
          selectorRectangles.push_back(std::move(rectangle));
        }
        if (usesSet) {
          std::vector<std::vector<uint64_t>> targetBinIDs(allTargetBins.size());
          for (size_t ordinal = 0; ordinal != allTargetBins.size(); ++ordinal)
            for (const ResolvedFunctionalBin *bin : allTargetBins[ordinal])
              targetBinIDs[ordinal].push_back(bin->id);
          std::vector<std::vector<uint64_t>> candidates;
          selectorStatus = resolvedCrossSelectorSetCandidates(
              fingerprintInput, typeID, Digest{}, cross->id, *node,
              targetBinIDs, suppressedTransitionAlternatives, setQueries,
              candidates);
          if (selectorStatus != OBELISK_RT_OK)
            return selectorStatus;
          for (const auto &tuple : candidates) {
            if (selectorRectangles.size() >= recordLimit ||
                tuple.size() > recordLimit - selectorRectangleBinEntryCount)
              return OBELISK_RT_OUT_OF_RESOURCES;
            SelectorRectangle rectangle;
            rectangle.allowedBins.reserve(tuple.size());
            for (uint64_t bin : tuple)
              rectangle.allowedBins.push_back({bin});
            selectorRectangleBinEntryCount += tuple.size();
            selectorRectangles.push_back(std::move(rectangle));
            hasSignificantAlternative = true;
          }
        }
      }
      if (!hasSignificantAlternative) {
        auto mirror = resolvedBinTemplates.find({cross->id, crossBin.bin});
        if (mirror == resolvedBinTemplates.end())
          return OBELISK_RT_INVALID_DESIGN;
        auto &row = resolvedBinRows[mirror->second];
        row.flags |= FunctionalBinEmpty;
        fingerprintInput.resolvedFunctionalBins[binBase + mirror->second]
            .flags = row.flags;
        auto queryBin = setQueries.bins.find(row.id);
        if (queryBin == setQueries.bins.end())
          return OBELISK_RT_INVALID_DESIGN;
      }
    }
    std::sort(
        selectorRectangles.begin(), selectorRectangles.end(),
        [](const SelectorRectangle &left, const SelectorRectangle &right) {
          return left.allowedBins < right.allowedBins;
        });
    selectorRectangles.erase(
        std::unique(
            selectorRectangles.begin(), selectorRectangles.end(),
            [](const SelectorRectangle &left, const SelectorRectangle &right) {
              return left.allowedBins == right.allowedBins;
            }),
        selectorRectangles.end());

    // Discarding automatic bins does not bypass explicit selector resolution:
    // empty explicit bins must still be diagnosed and omitted from the
    // denominator. Only the retained automatic count and Cartesian DAG are
    // absent from this resolved configuration.
    if (retain == CrossRetainAutoPolicy::Discard) {
      if (!appendPendingCross(std::move(pending)))
        return OBELISK_RT_OUT_OF_RESOURCES;
      continue;
    }

    if (allTargetBins.empty() ||
        std::any_of(allTargetBins.begin(), allTargetBins.end(),
                    [](const auto &bins) { return bins.empty(); })) {
      if (!appendPendingCross(std::move(pending)))
        return OBELISK_RT_OUT_OF_RESOURCES;
      continue;
    }

    // Build a bounded reduced ordered multi-valued decision diagram for the
    // exact language U - union(rectangles). Forward states retain the explicit
    // rectangles that still match a prefix. Backward interning merges equal
    // suffix languages and omits dead edges, yielding the canonical v1 graph.
    using RectangleState = std::vector<uint32_t>;
    struct RawEdge {
      const ResolvedFunctionalBin *bin = nullptr;
      uint32_t successor = 0;
      bool accepts = false;
    };
    struct RawState {
      RectangleState active;
      std::vector<RawEdge> edges;
      PreparedCrossValueIndex::Root label = 0;
    };
    std::vector<std::vector<RawState>> levels(staticPlan->targetCount);
    if (symbolic) {
      using Root = PreparedCrossValueIndex::Root;
      auto &graph = *symbolic->relation;
      Root retained = graph.subtract(
          symbolic->binImage->project(symbolic->universe), authored);
      levels.front().push_back({{}, {}, retained});
      uint64_t rawStateCount = 1, rawEdgeCount = 0;
      for (uint32_t ordinal = 0; ordinal != staticPlan->targetCount;
           ++ordinal) {
        const auto &axis = symbolic->axes[ordinal];
        const bool finalTarget = ordinal + 1 == staticPlan->targetCount;
        std::vector<const ResolvedFunctionalBin *> byLabel(
            allTargetBins[ordinal].size());
        IndexMap<uint64_t, uint32_t> nameOrder;
        for (uint32_t order = 0; order != allTargetBins[ordinal].size();
             ++order) {
          const auto *bin = allTargetBins[ordinal][order];
          auto label = axis.binOrdinals.find(bin->id);
          if (label == axis.binOrdinals.end() ||
              label->second >= byLabel.size())
            return OBELISK_RT_INVALID_DESIGN;
          byLabel[label->second] = bin;
          nameOrder.emplace(bin->id, order);
        }
        IndexMap<Root, uint32_t> successors;
        for (RawState &state : levels[ordinal]) {
          struct Frame {
            Root root;
            uint32_t depth;
            uint64_t first;
          };
          std::vector<Frame> pending{{state.label, 0, 0}};
          while (!pending.empty()) {
            Frame frame = pending.back();
            pending.pop_back();
            if (!frame.root || frame.first >= byLabel.size())
              continue;
            const uint64_t span = uint64_t{1} << (axis.binWidth - frame.depth);
            const uint32_t variable = graph.variable(frame.root);
            if (variable >= axis.binFirst + axis.binWidth) {
              uint32_t successor = 0;
              if (finalTarget) {
                if (frame.root != 1)
                  return OBELISK_RT_INVALID_DESIGN;
              } else {
                auto [position, inserted] = successors.emplace(
                    frame.root,
                    static_cast<uint32_t>(levels[ordinal + 1].size()));
                if (inserted) {
                  if (++rawStateCount > recordLimit ||
                      levels[ordinal + 1].size() >= UINT32_MAX)
                    return OBELISK_RT_OUT_OF_RESOURCES;
                  levels[ordinal + 1].push_back({{}, {}, frame.root});
                }
                successor = position->second;
              }
              const uint64_t limit =
                  std::min<uint64_t>(byLabel.size(), frame.first + span);
              for (uint64_t code = frame.first; code != limit; ++code) {
                if (++rawEdgeCount > recordLimit)
                  return OBELISK_RT_OUT_OF_RESOURCES;
                state.edges.push_back({byLabel[code], successor, finalTarget});
              }
            } else {
              const uint32_t bit = axis.binFirst + frame.depth;
              if (span == 1 || variable < bit)
                return OBELISK_RT_INVALID_DESIGN;
              pending.push_back(
                  {variable == bit ? graph.child(frame.root, true) : frame.root,
                   frame.depth + 1, frame.first + span / 2});
              pending.push_back({variable == bit
                                     ? graph.child(frame.root, false)
                                     : frame.root,
                                 frame.depth + 1, frame.first});
            }
          }
          sortByKey(state.edges.begin(), state.edges.end(),
                    [&](const RawEdge &edge) {
                      return nameOrder.find(edge.bin->id)->second;
                    });
        }
      }
      if (!symbolic->check())
        return symbolic->status;
    } else {
      RectangleState initial(selectorRectangles.size());
      std::iota(initial.begin(), initial.end(), uint32_t{0});
      levels.front().push_back({std::move(initial), {}});
      uint64_t rawStateCount = 1;
      uint64_t rawEdgeCount = 0;
      for (uint32_t ordinal = 0; ordinal != staticPlan->targetCount;
           ++ordinal) {
        std::map<RectangleState, uint32_t> successorIndices;
        for (RawState &state : levels[ordinal]) {
          for (const ResolvedFunctionalBin *bin : allTargetBins[ordinal]) {
            RectangleState next;
            for (uint32_t rectangle : state.active)
              if (std::binary_search(
                      selectorRectangles[rectangle]
                          .allowedBins[ordinal]
                          .begin(),
                      selectorRectangles[rectangle].allowedBins[ordinal].end(),
                      bin->id))
                next.push_back(rectangle);
            const bool finalTarget = ordinal + 1 == staticPlan->targetCount;
            if (finalTarget && !next.empty())
              continue;
            if (++rawEdgeCount > recordLimit)
              return OBELISK_RT_OUT_OF_RESOURCES;
            RawEdge edge;
            edge.bin = bin;
            edge.accepts = finalTarget;
            if (!finalTarget) {
              auto [position, inserted] = successorIndices.emplace(
                  next, static_cast<uint32_t>(levels[ordinal + 1].size()));
              if (inserted) {
                if (++rawStateCount > recordLimit ||
                    levels[ordinal + 1].size() >= UINT32_MAX)
                  return OBELISK_RT_OUT_OF_RESOURCES;
                levels[ordinal + 1].push_back({next, {}});
              }
              edge.successor = position->second;
            }
            state.edges.push_back(edge);
          }
        }
      }
    }

    using NodeSignature = std::vector<std::pair<uint64_t, uint64_t>>;
    struct CanonicalNode {
      uint64_t id = 0;
      FunctionalMagnitude count;
    };
    std::vector<std::vector<uint64_t>> stateNodes(levels.size());
    std::vector<std::vector<FunctionalMagnitude>> stateCounts(levels.size());
    auto stableNodeID = [&](uint32_t ordinal, const NodeSignature &signature) {
      uint64_t hash = OBELISK_STABLE_HASH_OFFSET_BASIS;
      static constexpr char nameSpace[] =
          "functional.resolved.cross-automatic-node";
      hash = obelisk_stable_hash_append(hash, nameSpace, sizeof(nameSpace) - 1);
      hash = obelisk_stable_hash_append_byte(hash, 0);
      hash = obelisk_stable_hash_append_uint_le(hash, typeID, 8);
      hash = obelisk_stable_hash_append_uint_le(hash, cross->templateItem, 8);
      hash = obelisk_stable_hash_append_uint_le(hash, ordinal, 4);
      for (const auto &[bin, child] : signature) {
        hash = obelisk_stable_hash_append_uint_le(hash, bin, 8);
        hash = obelisk_stable_hash_append_uint_le(hash, child, 8);
      }
      return hash && !crossNodeIDs.count(hash) ? hash : uint64_t{0};
    };
    uint64_t pendingEdgeCount = 0;
    for (uint32_t reverseOrdinal = staticPlan->targetCount; reverseOrdinal != 0;
         --reverseOrdinal) {
      const uint32_t ordinal = reverseOrdinal - 1;
      stateNodes[ordinal].resize(levels[ordinal].size());
      stateCounts[ordinal].resize(levels[ordinal].size());
      IndexMap<NodeSignature, CanonicalNode> canonical;
      for (size_t stateIndex = 0; stateIndex != levels[ordinal].size();
           ++stateIndex) {
        NodeSignature signature;
        FunctionalMagnitude count;
        for (const RawEdge &raw : levels[ordinal][stateIndex].edges) {
          uint64_t child = 0;
          FunctionalMagnitude contribution{1};
          if (!raw.accepts) {
            child = stateNodes[ordinal + 1][raw.successor];
            if (!child)
              continue;
            contribution = stateCounts[ordinal + 1][raw.successor];
          }
          signature.emplace_back(raw.bin->id, child);
          count = addFunctionalMagnitudes(count, contribution);
        }
        if (signature.empty())
          continue;
        auto found = canonical.find(signature);
        if (found != canonical.end()) {
          stateNodes[ordinal][stateIndex] = found->second.id;
          stateCounts[ordinal][stateIndex] = found->second.count;
          continue;
        }
        if (!recordsFit(fingerprintInput.resolvedCrossAutomaticNodes.size(),
                        generatedCrossNodeCount, pending.nodes.size() + 1))
          return OBELISK_RT_OUT_OF_RESOURCES;
        if (!recordsFit(fingerprintInput.resolvedCrossAutomaticEdges.size(),
                        generatedCrossEdgeCount, pendingEdgeCount) ||
            !recordsFit(fingerprintInput.resolvedCrossAutomaticEdges.size(),
                        generatedCrossEdgeCount + pendingEdgeCount,
                        signature.size()))
          return OBELISK_RT_OUT_OF_RESOURCES;
        uint64_t nodeID = stableNodeID(ordinal, signature);
        if (!nodeID)
          return OBELISK_RT_INVALID_DESIGN;
        crossNodeIDs.insert(nodeID);
        PendingCrossNode node;
        node.node = {typeID,
                     {},
                     cross->id,
                     nodeID,
                     ordinal,
                     0,
                     static_cast<uint32_t>(signature.size()),
                     0};
        node.edges.reserve(signature.size());
        for (uint32_t edgeOrdinal = 0; edgeOrdinal != signature.size();
             ++edgeOrdinal)
          node.edges.push_back({nodeID, signature[edgeOrdinal].first,
                                signature[edgeOrdinal].second, edgeOrdinal, 0});
        pendingEdgeCount += signature.size();
        pending.nodes.push_back(std::move(node));
        canonical.emplace(signature, CanonicalNode{nodeID, count});
        stateNodes[ordinal][stateIndex] = nodeID;
        stateCounts[ordinal][stateIndex] = std::move(count);
      }
    }
    pending.plan.rootNode = stateNodes.front().front();
    pending.automaticBinCount = stateCounts.front().front();
    if (!appendPendingCross(std::move(pending)))
      return OBELISK_RT_OUT_OF_RESOURCES;
  }

  std::vector<ResolvedCrossPlan> resolvedCrossPlanRows;
  std::vector<uint64_t> resolvedCrossCountLimbs;
  std::vector<PendingCrossNode> pendingCrossNodes;
  for (PendingCross &pending : pendingCrosses) {
    if (resolvedCrossCountLimbs.size() > UINT32_MAX ||
        pending.automaticBinCount.size() >
            UINT32_MAX - resolvedCrossCountLimbs.size())
      return OBELISK_RT_OUT_OF_RESOURCES;
    pending.plan.firstAutomaticBinCountLimb =
        static_cast<uint32_t>(resolvedCrossCountLimbs.size());
    pending.plan.automaticBinCountLimbCount =
        static_cast<uint32_t>(pending.automaticBinCount.size());
    resolvedCrossCountLimbs.insert(resolvedCrossCountLimbs.end(),
                                   pending.automaticBinCount.begin(),
                                   pending.automaticBinCount.end());
    resolvedCrossPlanRows.push_back(pending.plan);
    pendingCrossNodes.insert(pendingCrossNodes.end(),
                             std::make_move_iterator(pending.nodes.begin()),
                             std::make_move_iterator(pending.nodes.end()));
  }
  sortByKey(
      pendingCrossNodes.begin(), pendingCrossNodes.end(),
      [](const auto &node) { return std::tie(node.node.cross, node.node.id); });
  std::vector<ResolvedCrossAutomaticNode> resolvedCrossNodeRows;
  std::vector<ResolvedCrossAutomaticEdge> resolvedCrossEdgeRows;
  for (PendingCrossNode &pending : pendingCrossNodes) {
    if (resolvedCrossEdgeRows.size() > UINT32_MAX ||
        pending.edges.size() > UINT32_MAX - resolvedCrossEdgeRows.size())
      return OBELISK_RT_OUT_OF_RESOURCES;
    pending.node.firstEdge =
        static_cast<uint32_t>(resolvedCrossEdgeRows.size());
    resolvedCrossNodeRows.push_back(pending.node);
    resolvedCrossEdgeRows.insert(resolvedCrossEdgeRows.end(),
                                 pending.edges.begin(), pending.edges.end());
  }

  const uint32_t crossCountBase =
      fingerprintInput.resolvedCrossAutomaticBinCountLimbs.size();
  const uint32_t crossEdgeBase =
      fingerprintInput.resolvedCrossAutomaticEdges.size();
  for (auto &plan : resolvedCrossPlanRows)
    plan.firstAutomaticBinCountLimb += crossCountBase;
  for (auto &node : resolvedCrossNodeRows)
    node.firstEdge += crossEdgeBase;
  fingerprintInput.resolvedCrossPlans.insert(
      fingerprintInput.resolvedCrossPlans.end(), resolvedCrossPlanRows.begin(),
      resolvedCrossPlanRows.end());
  fingerprintInput.resolvedCrossAutomaticBinCountLimbs.insert(
      fingerprintInput.resolvedCrossAutomaticBinCountLimbs.end(),
      resolvedCrossCountLimbs.begin(), resolvedCrossCountLimbs.end());
  fingerprintInput.resolvedCrossAutomaticNodes.insert(
      fingerprintInput.resolvedCrossAutomaticNodes.end(),
      resolvedCrossNodeRows.begin(), resolvedCrossNodeRows.end());
  fingerprintInput.resolvedCrossAutomaticEdges.insert(
      fingerprintInput.resolvedCrossAutomaticEdges.end(),
      resolvedCrossEdgeRows.begin(), resolvedCrossEdgeRows.end());
  Digest configuration = computeFunctionalConfigurationFingerprint(
      fingerprintInput, typeID, Digest{});
  resolvedConfiguration = configuration;
  if (std::any_of(
          schema.functionalConfigurations.begin(),
          schema.functionalConfigurations.end(), [&](const auto &entry) {
            return entry.type == typeID && entry.configuration == configuration;
          }))
    return OBELISK_RT_OK;

  for (auto &row : resolvedItemRows)
    row.configuration = configuration;
  for (auto &row : resolvedBinRows)
    row.configuration = configuration;
  for (auto &row : resolvedSetRows) {
    row.configuration = configuration;
    row.firstAtom -= atomBase;
  }
  for (auto &row : resolvedAtomRows)
    row.firstLimb -= limbBase;
  for (auto &row : resolvedTransitionAlternativeRows) {
    row.configuration = configuration;
    row.firstStep -= transitionStepBase;
  }
  for (auto &row : resolvedTransitionStepRows)
    row.configuration = configuration;
  for (auto &row : resolvedTransitionExpansionGroupRows) {
    row.configuration = configuration;
    row.firstAlternative -= transitionAlternativeBase;
  }
  for (auto &row : resolvedPlanRows)
    row.configuration = configuration;
  for (auto &row : resolvedGroupRows) {
    row.configuration = configuration;
    row.firstBin -= binBase;
  }
  for (auto &row : configurationOptionRows)
    row.configuration = configuration;
  for (auto &row : resolvedSelectorBindingRows)
    row.configuration = configuration;
  for (auto &row : resolvedTupleSetRows) {
    row.configuration = configuration;
    row.firstTuple -= tupleBase;
  }
  for (auto &row : resolvedTupleRows)
    row.firstComponent -= tupleComponentBase;
  for (auto &row : resolvedCrossPlanRows) {
    row.configuration = configuration;
    row.firstAutomaticBinCountLimb -= crossCountBase;
  }
  for (auto &row : resolvedCrossNodeRows) {
    row.configuration = configuration;
    row.firstEdge -= crossEdgeBase;
  }

  assert(schema.functionalConfigurations.empty() &&
         "resolution requires a static type projection");
  sortByKey(resolvedItemRows.begin(), resolvedItemRows.end(),
            [](const auto &row) { return row.id; });
  sortByKey(resolvedPlanRows.begin(), resolvedPlanRows.end(),
            [](const auto &row) { return row.bin; });
  sortByKey(configurationOptionRows.begin(), configurationOptionRows.end(),
            [](const auto &row) {
              return std::tie(row.ownerKind, row.owner, row.scope, row.option);
            });
  destination.functionalConfigurations.push_back({typeID, configuration, 0});
  destination.resolvedFunctionalItems = std::move(resolvedItemRows);
  destination.resolvedFunctionalBins = std::move(resolvedBinRows);
  destination.resolvedFunctionalValueSets = std::move(resolvedSetRows);
  destination.resolvedFunctionalValueAtoms = std::move(resolvedAtomRows);
  destination.resolvedFunctionalValueLimbs = std::move(resolvedLimbRows);
  destination.resolvedFunctionalBinPlans = std::move(resolvedPlanRows);
  destination.resolvedFunctionalBinGroups = std::move(resolvedGroupRows);
  destination.resolvedTransitionAlternatives =
      std::move(resolvedTransitionAlternativeRows);
  destination.resolvedTransitionSteps = std::move(resolvedTransitionStepRows);
  destination.resolvedTransitionExpansionGroups =
      std::move(resolvedTransitionExpansionGroupRows);
  destination.functionalConfigurationOptions =
      std::move(configurationOptionRows);
  destination.resolvedCrossPlans = std::move(resolvedCrossPlanRows);
  destination.resolvedCrossAutomaticBinCountLimbs =
      std::move(resolvedCrossCountLimbs);
  destination.resolvedCrossAutomaticNodes = std::move(resolvedCrossNodeRows);
  destination.resolvedCrossAutomaticEdges = std::move(resolvedCrossEdgeRows);
  destination.resolvedCrossSelectorBindings =
      std::move(resolvedSelectorBindingRows);
  destination.resolvedFunctionalTupleSets = std::move(resolvedTupleSetRows);
  destination.resolvedFunctionalTupleSetTuples = std::move(resolvedTupleRows);
  destination.resolvedFunctionalTupleSetComponents =
      std::move(resolvedTupleComponentRows);

  return OBELISK_RT_OK;
}

obelisk_rt_status validateFunctionalTypeOptionOwner(CoverageState &coverage,
                                                    uint64_t typeID,
                                                    uint64_t itemID) {
  obelisk_rt_status status = ensureFunctionalConstructionSchemas(coverage);
  if (status != OBELISK_RT_OK)
    return status;
  if (!coverage.constructionSchemas->types.count(typeID))
    return OBELISK_RT_INVALID_DESIGN;
  if (itemID) {
    auto item = coverage.constructionSchemas->items.find(itemID);
    if (item == coverage.constructionSchemas->items.end() ||
        item->second.type != typeID)
      return OBELISK_RT_INVALID_DESIGN;
  }
  return OBELISK_RT_OK;
}

obelisk_rt_status
applyFunctionalTypeOptions(CoverageState &coverage, uint64_t typeID,
                           FunctionalCoverageTypeState &type) {
  auto overrides = coverage.typeOptions.find(typeID);
  if (overrides == coverage.typeOptions.end())
    return OBELISK_RT_OK;
  const FunctionalCoverageTypeOptions &options = overrides->second;
  if (options.goal)
    type.typeGoal = *options.goal;
  if (options.weight)
    type.typeWeight = *options.weight;
  if (options.mergeInstances)
    type.mergeInstances = *options.mergeInstances;
  if (type.items.size() != type.typeItemGoals.size() ||
      type.items.size() != type.typeItemWeights.size() || !type.queryMetadata ||
      type.queryMetadata->items.size() != type.items.size())
    return OBELISK_RT_INVALID_DESIGN;
  for (size_t index = 0; index != type.items.size(); ++index) {
    const auto *resolved = &type.queryMetadata->items[index];
    if (auto goal = options.itemGoals.find(resolved->templateItem);
        goal != options.itemGoals.end())
      type.typeItemGoals[index] = goal->second;
    if (auto weight = options.itemWeights.find(resolved->templateItem);
        weight != options.itemWeights.end())
      type.typeItemWeights[index] = weight->second;
  }
  return OBELISK_RT_OK;
}

obelisk_rt_status
prepareFunctionalSampleMetadata(const obelisk::coverage::Database &schema,
                                uint64_t typeID,
                                const FunctionalCoverageTypeState &type,
                                FunctionalCoverageQueryMetadata &metadata);

obelisk_rt_status createResolvedFunctionalType(
    CoverageState &coverage, const obelisk::coverage::Database &schema,
    uint64_t typeID, const obelisk::coverage::Digest &configuration,
    FunctionalCoverageTypeState &result) {
  FunctionalCoverageTypeState type;
  type.configuration = configuration;
  auto metadata = std::make_shared<FunctionalCoverageQueryMetadata>();
  FunctionalCoverageSetQueryIndex setQueries(schema, typeID, configuration);
  metadata->templates = coverage.constructionSchemas;
  using namespace obelisk::coverage;
  const auto optionsByOwner = detail::groupRecords(
      schema.functionalConfigurationOptions, [](const auto &option) {
        return std::make_tuple(option.type, option.configuration,
                               option.ownerKind, option.owner);
      });
  auto optionsFor = [&](FunctionalConfigurationOptionOwnerKind ownerKind,
                        uint64_t owner) -> const auto & {
    return detail::lookupRows(
        optionsByOwner,
        std::make_tuple(typeID, configuration, ownerKind, owner));
  };
  const auto &groupOptions =
      optionsFor(FunctionalConfigurationOptionOwnerKind::Group, typeID);
  IndexMap<uint64_t, const ResolvedFunctionalBinPlan *> binPlans;
  for (const auto &plan : schema.resolvedFunctionalBinPlans)
    if (plan.type == typeID && plan.configuration == configuration)
      binPlans.emplace(plan.bin, &plan);
  IndexSet<uint64_t> excludedEntities;
  for (const auto &entry : schema.exclusions)
    if (entry.metric == MetricKind::Functional)
      excludedEntities.insert(entry.entity);
  std::unordered_set<uint64_t> emptyTransitionBins;
  obelisk_rt_status transitionExclusionStatus = deriveTransitionExclusions(
      schema, typeID, configuration, type.suppressedTransitionAlternatives,
      emptyTransitionBins, setQueries);
  if (transitionExclusionStatus != OBELISK_RT_OK)
    return transitionExclusionStatus;
  std::unordered_set<uint64_t> transitionBinsWithAlternatives;
  for (const auto &alternative : schema.resolvedTransitionAlternatives)
    if (alternative.type == typeID &&
        alternative.configuration == configuration)
      transitionBinsWithAlternatives.insert(alternative.bin);
  size_t verifiedEmptyTransitionBins = 0;
  for (const auto &bin : schema.resolvedFunctionalBins) {
    if (bin.type != typeID || bin.configuration != configuration ||
        bin.kind != obelisk::coverage::FunctionalBinKind::Transition ||
        (bin.flags & (obelisk::coverage::FunctionalBinIgnore |
                      obelisk::coverage::FunctionalBinIllegal)) ||
        !transitionBinsWithAlternatives.count(bin.id))
      continue;
    bool empty = emptyTransitionBins.count(bin.id);
    if (bool(bin.flags & obelisk::coverage::FunctionalBinEmpty) != empty)
      return OBELISK_RT_INVALID_DESIGN;
    verifiedEmptyTransitionBins += empty;
  }
  if (verifiedEmptyTransitionBins != emptyTransitionBins.size())
    return OBELISK_RT_INVALID_DESIGN;
  for (const auto &option : groupOptions) {
    if (option.type != typeID || option.configuration != configuration ||
        option.owner != typeID ||
        option.ownerKind !=
            obelisk::coverage::FunctionalConfigurationOptionOwnerKind::Group)
      continue;
    using Option = obelisk::coverage::FunctionalConfigurationOptionKind;
    using Scope = obelisk::coverage::FunctionalOptionScopeKind;
    if (option.scope == Scope::Instance && option.option == Option::Goal) {
      if (option.value > 100)
        return OBELISK_RT_INVALID_DESIGN;
      type.instanceGoal = static_cast<uint32_t>(option.value);
    } else if (option.scope == Scope::Instance &&
               option.option == Option::Weight) {
      if (option.value > UINT32_MAX)
        return OBELISK_RT_INVALID_DESIGN;
      type.instanceWeight = static_cast<uint32_t>(option.value);
    } else if (option.scope == Scope::Instance &&
               option.option == Option::GetInstCoverage) {
      if (option.value > 1)
        return OBELISK_RT_INVALID_DESIGN;
      type.getInstCoverage = option.value != 0;
    } else if (option.scope == Scope::Instance &&
               option.option == Option::PerInstance) {
      metadata->groupOptions.perInstance = option.value != 0;
    } else if (option.scope == Scope::Instance &&
               option.option == Option::AutoBinMax) {
      metadata->groupOptions.autoBinMax = option.value;
    } else if (option.scope == Scope::Instance &&
               option.option == Option::DetectOverlap) {
      metadata->groupOptions.detectOverlap = option.value != 0;
    } else if (option.scope == Scope::Instance &&
               option.option == Option::CrossRetainAutoBins) {
      metadata->groupOptions.crossRetainAutoBins = option.value != 0;
    } else if (option.scope == Scope::Instance &&
               option.option == Option::CrossNumPrintMissing) {
      type.groupCrossNumPrintMissing = option.value;
    } else if (option.scope == Scope::Type && option.option == Option::Goal) {
      if (option.value > 100)
        return OBELISK_RT_INVALID_DESIGN;
      type.typeGoal = static_cast<uint32_t>(option.value);
    } else if (option.scope == Scope::Type && option.option == Option::Weight) {
      if (option.value > UINT32_MAX)
        return OBELISK_RT_INVALID_DESIGN;
      type.typeWeight = static_cast<uint32_t>(option.value);
    } else if (option.scope == Scope::Type &&
               option.option == Option::MergeInstances) {
      if (option.value > 1)
        return OBELISK_RT_INVALID_DESIGN;
      type.mergeInstances = option.value != 0;
    } else if (option.scope == Scope::Type &&
               option.option == Option::DistributeFirst) {
      if (option.value > 1)
        return OBELISK_RT_INVALID_DESIGN;
      type.distributeFirst = option.value != 0;
    } else if (option.scope == Scope::Type && option.option == Option::Strobe) {
      if (option.value > 1)
        return OBELISK_RT_INVALID_DESIGN;
      type.strobe = option.value != 0;
    }
  }
  std::vector<const obelisk::coverage::ResolvedFunctionalItem *> items;
  for (const auto &item : schema.resolvedFunctionalItems)
    if (item.type == typeID && item.configuration == type.configuration)
      items.push_back(&item);
  sortByKey(items.begin(), items.end(),
            [](const auto *item) { return item->ordinal; });
  metadata->itemBins.resize(items.size());
  uint64_t groupAtLeast = 1;
  for (const auto &option : groupOptions)
    if (option.type == typeID && option.configuration == configuration &&
        option.owner == typeID &&
        option.ownerKind == obelisk::coverage::
                                FunctionalConfigurationOptionOwnerKind::Group &&
        option.scope ==
            obelisk::coverage::FunctionalOptionScopeKind::Instance &&
        option.option ==
            obelisk::coverage::FunctionalConfigurationOptionKind::AtLeast)
      groupAtLeast = option.value;
  for (const auto *item : items) {
    const auto &itemOptions =
        optionsFor(FunctionalConfigurationOptionOwnerKind::Item, item->id);
    const bool nonAggregating =
        (item->flags & obelisk::coverage::FunctionalItemNonAggregating) != 0;
    auto constructionOptions = metadata->groupOptions;
    for (const auto &option : itemOptions) {
      if (option.scope != FunctionalOptionScopeKind::Instance)
        continue;
      using Option = FunctionalConfigurationOptionKind;
      if (option.option == Option::AutoBinMax)
        constructionOptions.autoBinMax = option.value;
      else if (option.option == Option::DetectOverlap)
        constructionOptions.detectOverlap = option.value != 0;
      else if (option.option == Option::CrossRetainAutoBins)
        constructionOptions.crossRetainAutoBins = option.value != 0;
    }
    metadata->itemOptions.push_back(constructionOptions);
    if (!nonAggregating) {
      auto alias = metadata->inheritedItemPositions.emplace(
          std::make_pair(item->name, static_cast<uint32_t>(item->kind)),
          type.items.size());
      if (!alias.second)
        alias.first->second = SIZE_MAX;
    }
    metadata->itemPositions.emplace(item->id, type.items.size());
    metadata->templateItemPositions.emplace(item->templateItem,
                                            type.items.size());
    metadata->items.push_back(*item);
    type.items.push_back(item->id);
    type.itemAggregating.push_back(nonAggregating ? 0 : 1);
    type.itemGoals.push_back(item->goal);
    type.itemWeights.push_back(nonAggregating ? 0 : item->weight);
    uint64_t itemAtLeast = groupAtLeast;
    for (const auto &option : itemOptions)
      if (option.type == typeID && option.configuration == configuration &&
          option.owner == item->id &&
          option.ownerKind == obelisk::coverage::
                                  FunctionalConfigurationOptionOwnerKind::Item &&
          option.scope ==
              obelisk::coverage::FunctionalOptionScopeKind::Instance &&
          option.option ==
              obelisk::coverage::FunctionalConfigurationOptionKind::AtLeast)
        itemAtLeast = option.value;
    for (const auto &option : itemOptions)
      if (option.type == typeID && option.configuration == configuration &&
          option.owner == item->id &&
          option.ownerKind == obelisk::coverage::
                                  FunctionalConfigurationOptionOwnerKind::Item &&
          option.scope ==
              obelisk::coverage::FunctionalOptionScopeKind::Instance &&
          option.option == obelisk::coverage::
                               FunctionalConfigurationOptionKind::
                                   CrossNumPrintMissing) {
        if (item->kind != obelisk::coverage::FunctionalItemKind::Cross)
          return OBELISK_RT_INVALID_DESIGN;
        type.crossNumPrintMissing[item->templateItem] = option.value;
        type.explicitCrossNumPrintMissingItems.insert(item->templateItem);
      }
    type.itemAtLeast.push_back(itemAtLeast);
    uint32_t typeGoal = 100;
    uint32_t typeWeight = 1;
    for (const auto &option : itemOptions) {
      if (option.type != typeID || option.configuration != configuration ||
          option.owner != item->id ||
          option.ownerKind !=
              obelisk::coverage::FunctionalConfigurationOptionOwnerKind::Item ||
          option.scope != obelisk::coverage::FunctionalOptionScopeKind::Type)
        continue;
      if (option.option ==
          obelisk::coverage::FunctionalConfigurationOptionKind::Goal) {
        if (option.value > 100)
          return OBELISK_RT_INVALID_DESIGN;
        typeGoal = static_cast<uint32_t>(option.value);
      } else if (option.option ==
                 obelisk::coverage::FunctionalConfigurationOptionKind::Weight) {
        if (option.value > UINT32_MAX)
          return OBELISK_RT_INVALID_DESIGN;
        typeWeight = static_cast<uint32_t>(option.value);
      }
    }
    type.typeItemGoals.push_back(typeGoal);
    type.typeItemWeights.push_back(nonAggregating ? 0 : typeWeight);
  }
  for (const auto &bin : schema.resolvedFunctionalBins) {
    if (bin.type != typeID || bin.configuration != type.configuration)
      continue;
    auto plan = binPlans.find(bin.id);
    uint32_t flags = bin.flags;
    bool excluded = excludedEntities.count(bin.id) ||
                    excludedEntities.count(bin.templateBin);
    type.bins.push_back(
        {bin.id, bin.templateBin, bin.item,
         plan == binPlans.end() ? uint64_t{0} : plan->second->valueSet,
         bin.atLeast, bin.kind, flags, 0, false, excluded});
    auto position = metadata->itemPositions.find(bin.item);
    if (position == metadata->itemPositions.end())
      return OBELISK_RT_INVALID_DESIGN;
    metadata->itemBins[position->second].push_back(metadata->bins.size());
    metadata->binPositions.emplace(bin.id, metadata->bins.size());
    metadata->bins.push_back(bin);
  }
  PreparedFunctionalConstruction selectionIndex;
  initializeSelectionIndex(selectionIndex, schema, setQueries, typeID,
                           configuration,
                           type.suppressedTransitionAlternatives);
  const uint64_t selectorResolutionLimit =
      obelisk::coverage::ParseLimits{}.maxRecords;
  uint64_t explicitRectangleCount = 0;
  uint64_t explicitRectangleBinEntryCount = 0;
  IndexMap<uint64_t, const obelisk::coverage::CrossPlan *> staticCrossPlans;
  for (const auto &plan : schema.crossPlans)
    staticCrossPlans.emplace(plan.item, &plan);
  IndexMap<uint64_t, const obelisk::coverage::CrossSelectorNode *> selectors;
  for (const auto &selector : schema.crossSelectorNodes)
    selectors.emplace(selector.id, &selector);
  IndexMap<std::pair<uint64_t, uint64_t>, size_t> crossBins;
  for (size_t index = 0; index != metadata->bins.size(); ++index) {
    const auto &bin = metadata->bins[index];
    if (bin.kind == obelisk::coverage::FunctionalBinKind::Cross)
      crossBins.emplace(std::make_pair(bin.item, bin.templateBin), index);
  }
  IndexMap<std::pair<uint64_t, uint64_t>,
           const obelisk::coverage::ResolvedCrossSelectorBinding *>
      bindings;
  for (const auto &binding : schema.resolvedCrossSelectorBindings)
    if (binding.type == typeID && binding.configuration == configuration)
      bindings.emplace(std::make_pair(binding.cross, binding.node), &binding);
  for (const auto &set : schema.resolvedFunctionalValueSets)
    if (set.type == typeID && set.configuration == configuration)
      metadata->valueSets.emplace(set.id, &set);
  for (const auto &plan : schema.resolvedCrossPlans) {
    if (plan.type != typeID || plan.configuration != type.configuration)
      continue;
    auto itemPosition = metadata->itemPositions.find(plan.cross);
    if (itemPosition == metadata->itemPositions.end())
      return OBELISK_RT_INVALID_DESIGN;
    const auto *item = &metadata->items[itemPosition->second];
    auto staticPlanPosition = staticCrossPlans.find(item->templateItem);
    if (item->kind != obelisk::coverage::FunctionalItemKind::Cross ||
        staticPlanPosition == staticCrossPlans.end())
      return OBELISK_RT_INVALID_DESIGN;
    const auto *staticPlan = staticPlanPosition->second;
    if ((plan.retainAutoPolicy !=
             obelisk::coverage::CrossRetainAutoPolicy::Retain &&
         plan.retainAutoPolicy !=
             obelisk::coverage::CrossRetainAutoPolicy::Discard) ||
        uint64_t{staticPlan->firstTarget} + staticPlan->targetCount >
            schema.crossTargets.size() ||
        uint64_t{staticPlan->firstBin} + staticPlan->binCount >
            schema.crossBins.size() ||
        uint64_t{plan.firstAutomaticBinCountLimb} +
                plan.automaticBinCountLimbCount >
            schema.resolvedCrossAutomaticBinCountLimbs.size())
      return OBELISK_RT_INVALID_DESIGN;
    FunctionalCoverageCrossState cross;
    cross.id = item->id;
    cross.templateItem = item->templateItem;
    cross.automaticBinCount.insert(
        cross.automaticBinCount.end(),
        schema.resolvedCrossAutomaticBinCountLimbs.begin() +
            plan.firstAutomaticBinCountLimb,
        schema.resolvedCrossAutomaticBinCountLimbs.begin() +
            plan.firstAutomaticBinCountLimb + plan.automaticBinCountLimbCount);
    cross.automaticRoot = plan.rootNode;
    cross.atLeast = type.itemAtLeast[itemPosition->second];
    cross.excluded = excludedEntities.count(item->id) ||
                     excludedEntities.count(item->templateItem);
    IndexMap<uint64_t, uint32_t> targetOrdinals;
    for (uint32_t ordinal = 0; ordinal != staticPlan->targetCount; ++ordinal) {
      const auto &target =
          schema.crossTargets[staticPlan->firstTarget + ordinal];
      auto resolvedTarget = metadata->templateItemPositions.find(target.target);
      if (target.cross != item->templateItem || target.ordinal != ordinal ||
          resolvedTarget == metadata->templateItemPositions.end() ||
          metadata->items[resolvedTarget->second].kind !=
              obelisk::coverage::FunctionalItemKind::Coverpoint)
        return OBELISK_RT_INVALID_DESIGN;
      cross.targets.push_back(metadata->items[resolvedTarget->second].id);
      targetOrdinals.emplace(target.target, ordinal);
    }
    for (uint32_t binOrdinal = 0; binOrdinal != staticPlan->binCount;
         ++binOrdinal) {
      const auto &crossBin =
          schema.crossBins[staticPlan->firstBin + binOrdinal];
      auto selectorPosition = selectors.find(crossBin.rootSelector);
      auto resolvedBinPosition = crossBins.find({cross.id, crossBin.bin});
      CrossSelectorDNF alternatives;
      if (crossBin.cross != item->templateItem ||
          selectorPosition == selectors.end() ||
          selectorPosition->second->cross != item->templateItem ||
          resolvedBinPosition == crossBins.end())
        return OBELISK_RT_INVALID_DESIGN;
      const auto *selector = selectorPosition->second;
      const auto *resolvedBin = &metadata->bins[resolvedBinPosition->second];
      FunctionalCoverageCrossState::ExplicitBin explicitBin;
      explicitBin.bin = resolvedBin->id;
      if (crossSelectorUsesWith(schema, *selector, setQueries)) {
        auto kernel = selectionKernel(selectionIndex, *staticPlan);
        if (!kernel->check())
          return kernel->status;
        auto selected = kernel->select(*selector, cross.id, 0);
        selected = kernel->relation->intersect(
            selected, kernel->binImage->project(kernel->universe));
        if (!kernel->check())
          return kernel->status;
        explicitBin.selectionRoot = kernel->sampleRoot(selected);
        if (!kernel->check())
          return kernel->status;
        explicitBin.selection = kernel->sampleSelection;
      } else {
        const bool usesSet =
            crossSelectorUsesSet(schema, *selector, setQueries);
        obelisk_rt_status selectorStatus = decodeCrossSelectorDNF(
            schema, *selector, obelisk::coverage::ParseLimits{}.maxRecords,
            alternatives, usesSet);
        if (selectorStatus != OBELISK_RT_OK)
          return selectorStatus;
        for (const CrossSelectorConjunction &terms : alternatives) {
          FunctionalCoverageCrossState::Rectangle rectangle;
          IndexMap<uint32_t, std::vector<uint64_t>> constraints;
          uint64_t rectangleBinEntryCount = 0;
          for (const CrossSelectorTerm &term : terms) {
            const auto *condition = term.condition;
            auto targetOrdinal = targetOrdinals.find(condition->target);
            if (targetOrdinal == targetOrdinals.end())
              return OBELISK_RT_INVALID_DESIGN;
            const auto *staticTarget =
                &schema.crossTargets[staticPlan->firstTarget +
                                     targetOrdinal->second];

            const obelisk::coverage::ResolvedFunctionalValueSet *selectedSet =
                nullptr;
            if (condition->valueSet) {
              auto binding = bindings.find({cross.id, condition->id});
              if (binding == bindings.end())
                return OBELISK_RT_INVALID_DESIGN;
              auto set = metadata->valueSets.find(binding->second->valueSet);
              if (set == metadata->valueSets.end())
                return OBELISK_RT_INVALID_DESIGN;
              selectedSet = set->second;
            }

            std::vector<uint64_t> selectedBins;
            const uint64_t targetItem = cross.targets[staticTarget->ordinal];
            auto targetPosition = metadata->itemPositions.find(targetItem);
            for (size_t binIndex : metadata->itemBins[targetPosition->second]) {
              const auto &candidate = type.bins[binIndex];
              constexpr uint32_t omitted =
                  obelisk::coverage::FunctionalBinDefault |
                  obelisk::coverage::FunctionalBinDefaultSequence |
                  obelisk::coverage::FunctionalBinIgnore |
                  obelisk::coverage::FunctionalBinIllegal |
                  obelisk::coverage::FunctionalBinEmpty;
              if (candidate.flags & omitted)
                continue;
              bool selected =
                  !condition->bin || candidate.templateBin == condition->bin;
              if (selected && selectedSet) {
                bool intersects = false;
                obelisk_rt_status status =
                    resolvedFunctionalBinIntersectsValueSet(
                        schema, typeID, configuration, candidate.id,
                        *selectedSet, type.suppressedTransitionAlternatives,
                        setQueries, intersects);
                if (status != OBELISK_RT_OK)
                  return status;
                selected = intersects;
              }
              if (term.negated)
                selected = !selected;
              if (selected) {
                if (explicitRectangleBinEntryCount > selectorResolutionLimit ||
                    rectangleBinEntryCount >
                        selectorResolutionLimit -
                            explicitRectangleBinEntryCount ||
                    selectedBins.size() >= selectorResolutionLimit -
                                               explicitRectangleBinEntryCount -
                                               rectangleBinEntryCount)
                  return OBELISK_RT_OUT_OF_RESOURCES;
                selectedBins.push_back(candidate.id);
              }
            }
            sortByKey(selectedBins.begin(), selectedBins.end(),
                      [](uint64_t bin) { return bin; });
            auto position = constraints.find(staticTarget->ordinal);
            if (position != constraints.end()) {
              // Intersect in place so a third vector cannot evade the aggregate
              // payload budget while the existing and candidate vectors live.
              auto &existing = position->second;
              const size_t oldSize = existing.size();
              size_t left = 0;
              size_t right = 0;
              size_t output = 0;
              while (left != existing.size() && right != selectedBins.size()) {
                if (existing[left] < selectedBins[right]) {
                  ++left;
                } else if (selectedBins[right] < existing[left]) {
                  ++right;
                } else {
                  existing[output++] = existing[left++];
                  ++right;
                }
              }
              existing.resize(output);
              rectangleBinEntryCount -= oldSize - output;
            } else {
              rectangleBinEntryCount += selectedBins.size();
              constraints.emplace(staticTarget->ordinal,
                                  std::move(selectedBins));
            }
          }
          const bool significant = std::all_of(
              constraints.begin(), constraints.end(),
              [](const auto &entry) { return !entry.second.empty(); });
          if (!significant)
            continue;
          if (explicitRectangleCount >= selectorResolutionLimit ||
              rectangleBinEntryCount >
                  selectorResolutionLimit - explicitRectangleBinEntryCount)
            return OBELISK_RT_OUT_OF_RESOURCES;
          for (auto &[targetOrdinal, selectedBins] : constraints)
            rectangle.constraints.push_back(
                {targetOrdinal, std::move(selectedBins)});
          ++explicitRectangleCount;
          explicitRectangleBinEntryCount += rectangleBinEntryCount;
          explicitBin.alternatives.push_back(std::move(rectangle));
        }
        if (usesSet) {
          std::vector<std::vector<uint64_t>> targetBins(cross.targets.size());
          for (size_t targetOrdinal = 0; targetOrdinal != cross.targets.size();
               ++targetOrdinal) {
            auto targetPosition =
                metadata->itemPositions.find(cross.targets[targetOrdinal]);
            for (size_t binIndex : metadata->itemBins[targetPosition->second]) {
              const auto &candidate = type.bins[binIndex];
              constexpr uint32_t omitted =
                  obelisk::coverage::FunctionalBinDefault |
                  obelisk::coverage::FunctionalBinDefaultSequence |
                  obelisk::coverage::FunctionalBinIgnore |
                  obelisk::coverage::FunctionalBinIllegal |
                  obelisk::coverage::FunctionalBinEmpty;
              if (!(candidate.flags & omitted))
                targetBins[targetOrdinal].push_back(candidate.id);
            }
            sortByKey(targetBins[targetOrdinal].begin(),
                      targetBins[targetOrdinal].end(),
                      [](uint64_t bin) { return bin; });
          }
          std::vector<std::vector<uint64_t>> candidates;
          selectorStatus = resolvedCrossSelectorSetCandidates(
              schema, typeID, configuration, cross.id, *selector, targetBins,
              type.suppressedTransitionAlternatives, setQueries, candidates);
          if (selectorStatus != OBELISK_RT_OK)
            return selectorStatus;
          for (const auto &tuple : candidates) {
            if (explicitRectangleCount >= selectorResolutionLimit ||
                tuple.size() >
                    selectorResolutionLimit - explicitRectangleBinEntryCount)
              return OBELISK_RT_OUT_OF_RESOURCES;
            FunctionalCoverageCrossState::Rectangle rectangle;
            for (size_t ordinal = 0; ordinal != tuple.size(); ++ordinal)
              rectangle.constraints.push_back(
                  {static_cast<uint32_t>(ordinal), {tuple[ordinal]}});
            ++explicitRectangleCount;
            explicitRectangleBinEntryCount += tuple.size();
            explicitBin.alternatives.push_back(std::move(rectangle));
          }
        }
      }
      cross.explicitBins.push_back(std::move(explicitBin));
    }
    metadata->crossPositions.emplace(cross.id, type.crosses.size());
    type.crosses.push_back(std::move(cross));
  }
  for (const auto &option : schema.functionalConfigurationOptions)
    if (option.type == typeID && option.configuration == configuration &&
        option.ownerKind ==
            obelisk::coverage::FunctionalConfigurationOptionOwnerKind::Item &&
        option.scope ==
            obelisk::coverage::FunctionalOptionScopeKind::Instance &&
        option.option ==
            obelisk::coverage::FunctionalConfigurationOptionKind::AtLeast)
      metadata->explicitAtLeastItems.insert(option.owner);
  obelisk_rt_status sampleStatus =
      prepareFunctionalSampleMetadata(schema, typeID, type, *metadata);
  if (sampleStatus != OBELISK_RT_OK)
    return sampleStatus;
  type.queryMetadata = std::move(metadata);
  obelisk_rt_status typeOptionStatus =
      applyFunctionalTypeOptions(coverage, typeID, type);
  if (typeOptionStatus != OBELISK_RT_OK)
    return typeOptionStatus;
  obelisk_rt_status overlapStatus =
      emitFunctionalOverlapWarnings(schema, typeID, configuration);
  if (overlapStatus != OBELISK_RT_OK)
    return overlapStatus;
  result = std::move(type);
  return OBELISK_RT_OK;
}

obelisk_rt_status ensureFunctionalConfigurationSchemas(CoverageState &coverage);

obelisk_rt_status
bindResolvedFunctionalType(CoverageState &coverage, uint64_t typeID,
                           const obelisk::coverage::Digest &configuration,
                           FunctionalCoverageTypeState *&result) {
  FunctionalCoverageTypeKey key{typeID, configuration};
  auto found = coverage.types.find(key);
  if (found != coverage.types.end()) {
    result = &found->second;
    return OBELISK_RT_OK;
  }
  if (!coverage.schema)
    return OBELISK_RT_INVALID_DESIGN;
  indexFunctionalConfigurations(coverage);
  if (!coverage.configurationKeys.count(key))
    return OBELISK_RT_INVALID_DESIGN;
  obelisk_rt_status status = ensureFunctionalConfigurationSchemas(coverage);
  if (status != OBELISK_RT_OK)
    return status;
  auto bundle = coverage.loadedConfigurationSchemas->configurations.find(key);
  if (bundle == coverage.loadedConfigurationSchemas->configurations.end())
    return OBELISK_RT_INVALID_DESIGN;
  FunctionalCoverageTypeState type;
  status = createResolvedFunctionalType(coverage, *bundle->second, typeID,
                                        configuration, type);
  if (status != OBELISK_RT_OK)
    return status;
  type.configurationSchema = bundle->second;
  auto insertion = coverage.types.emplace(key, std::move(type));
  result = &insertion.first->second;
  return OBELISK_RT_OK;
}

// Prepare all allocation and rebasing before publishing either metadata or
// runtime cache state. Growing global columns geometrically gives amortized
// linear work across all constructors, including vector relocations.
template <typename T>
bool prepareConfigurationColumn(std::vector<T> &destination,
                                const std::vector<T> &bundle,
                                std::vector<T> &prepared) {
  const size_t limit = std::min<uint64_t>(
      UINT32_MAX, obelisk::coverage::ParseLimits{}.maxRecords);
  if (destination.size() > limit || bundle.size() > limit - destination.size())
    return false;
  prepared = bundle;
  const size_t required = destination.size() + bundle.size();
  if (required > destination.capacity())
    destination.reserve(
        std::max(required, std::min(limit, destination.capacity() * 2)));
  return true;
}

obelisk_rt_status
prepareConfigurationAppend(obelisk::coverage::Database &destination,
                           const obelisk::coverage::Database &bundle,
                           obelisk::coverage::Database &prepared) {
  if (!prepareConfigurationColumn(destination.functionalConfigurations,
                                  bundle.functionalConfigurations,
                                  prepared.functionalConfigurations) ||
      !prepareConfigurationColumn(destination.functionalConfigurationOptions,
                                  bundle.functionalConfigurationOptions,
                                  prepared.functionalConfigurationOptions) ||
      !prepareConfigurationColumn(destination.resolvedFunctionalItems,
                                  bundle.resolvedFunctionalItems,
                                  prepared.resolvedFunctionalItems) ||
      !prepareConfigurationColumn(destination.resolvedFunctionalBins,
                                  bundle.resolvedFunctionalBins,
                                  prepared.resolvedFunctionalBins) ||
      !prepareConfigurationColumn(destination.resolvedTransitionAlternatives,
                                  bundle.resolvedTransitionAlternatives,
                                  prepared.resolvedTransitionAlternatives) ||
      !prepareConfigurationColumn(destination.resolvedTransitionExpansionGroups,
                                  bundle.resolvedTransitionExpansionGroups,
                                  prepared.resolvedTransitionExpansionGroups) ||
      !prepareConfigurationColumn(destination.resolvedTransitionSteps,
                                  bundle.resolvedTransitionSteps,
                                  prepared.resolvedTransitionSteps) ||
      !prepareConfigurationColumn(destination.resolvedCrossPlans,
                                  bundle.resolvedCrossPlans,
                                  prepared.resolvedCrossPlans) ||
      !prepareConfigurationColumn(
          destination.resolvedCrossAutomaticBinCountLimbs,
          bundle.resolvedCrossAutomaticBinCountLimbs,
          prepared.resolvedCrossAutomaticBinCountLimbs) ||
      !prepareConfigurationColumn(destination.resolvedCrossAutomaticNodes,
                                  bundle.resolvedCrossAutomaticNodes,
                                  prepared.resolvedCrossAutomaticNodes) ||
      !prepareConfigurationColumn(destination.resolvedCrossAutomaticEdges,
                                  bundle.resolvedCrossAutomaticEdges,
                                  prepared.resolvedCrossAutomaticEdges) ||
      !prepareConfigurationColumn(destination.resolvedCrossSelectorBindings,
                                  bundle.resolvedCrossSelectorBindings,
                                  prepared.resolvedCrossSelectorBindings) ||
      !prepareConfigurationColumn(destination.resolvedFunctionalValueSets,
                                  bundle.resolvedFunctionalValueSets,
                                  prepared.resolvedFunctionalValueSets) ||
      !prepareConfigurationColumn(destination.resolvedFunctionalValueAtoms,
                                  bundle.resolvedFunctionalValueAtoms,
                                  prepared.resolvedFunctionalValueAtoms) ||
      !prepareConfigurationColumn(destination.resolvedFunctionalValueLimbs,
                                  bundle.resolvedFunctionalValueLimbs,
                                  prepared.resolvedFunctionalValueLimbs) ||
      !prepareConfigurationColumn(destination.resolvedFunctionalBinPlans,
                                  bundle.resolvedFunctionalBinPlans,
                                  prepared.resolvedFunctionalBinPlans) ||
      !prepareConfigurationColumn(destination.resolvedFunctionalBinGroups,
                                  bundle.resolvedFunctionalBinGroups,
                                  prepared.resolvedFunctionalBinGroups) ||
      !prepareConfigurationColumn(destination.resolvedFunctionalTupleSets,
                                  bundle.resolvedFunctionalTupleSets,
                                  prepared.resolvedFunctionalTupleSets) ||
      !prepareConfigurationColumn(destination.resolvedFunctionalTupleSetTuples,
                                  bundle.resolvedFunctionalTupleSetTuples,
                                  prepared.resolvedFunctionalTupleSetTuples) ||
      !prepareConfigurationColumn(
          destination.resolvedFunctionalTupleSetComponents,
          bundle.resolvedFunctionalTupleSetComponents,
          prepared.resolvedFunctionalTupleSetComponents))
    return OBELISK_RT_OUT_OF_RESOURCES;
  for (auto &row : prepared.resolvedFunctionalBinGroups)
    row.firstBin += destination.resolvedFunctionalBins.size();
  for (auto &row : prepared.resolvedFunctionalValueSets)
    row.firstAtom += destination.resolvedFunctionalValueAtoms.size();
  for (auto &row : prepared.resolvedFunctionalValueAtoms)
    row.firstLimb += destination.resolvedFunctionalValueLimbs.size();
  for (auto &row : prepared.resolvedTransitionAlternatives)
    row.firstStep += destination.resolvedTransitionSteps.size();
  for (auto &row : prepared.resolvedTransitionExpansionGroups)
    row.firstAlternative += destination.resolvedTransitionAlternatives.size();
  for (auto &row : prepared.resolvedCrossPlans)
    row.firstAutomaticBinCountLimb +=
        destination.resolvedCrossAutomaticBinCountLimbs.size();
  for (auto &row : prepared.resolvedCrossAutomaticNodes)
    row.firstEdge += destination.resolvedCrossAutomaticEdges.size();
  for (auto &row : prepared.resolvedFunctionalTupleSets)
    row.firstTuple += destination.resolvedFunctionalTupleSetTuples.size();
  for (auto &row : prepared.resolvedFunctionalTupleSetTuples)
    row.firstComponent +=
        destination.resolvedFunctionalTupleSetComponents.size();
  return OBELISK_RT_OK;
}

void appendPreparedConfiguration(obelisk::coverage::Database &destination,
                                 obelisk::coverage::Database &prepared) {
  auto append = [](auto &to, auto &from) {
    using Row = typename std::decay_t<decltype(to)>::value_type;
    static_assert(std::is_nothrow_move_constructible_v<Row>);
    if (to.empty()) {
      to.swap(from);
      return;
    }
    assert(to.capacity() - to.size() >= from.size());
    to.insert(to.end(), std::make_move_iterator(from.begin()),
              std::make_move_iterator(from.end()));
  };
  append(destination.functionalConfigurations,
         prepared.functionalConfigurations);
  append(destination.functionalConfigurationOptions,
         prepared.functionalConfigurationOptions);
  append(destination.resolvedFunctionalItems, prepared.resolvedFunctionalItems);
  append(destination.resolvedFunctionalBins, prepared.resolvedFunctionalBins);
  append(destination.resolvedTransitionAlternatives,
         prepared.resolvedTransitionAlternatives);
  append(destination.resolvedTransitionExpansionGroups,
         prepared.resolvedTransitionExpansionGroups);
  append(destination.resolvedTransitionSteps, prepared.resolvedTransitionSteps);
  append(destination.resolvedCrossPlans, prepared.resolvedCrossPlans);
  append(destination.resolvedCrossAutomaticBinCountLimbs,
         prepared.resolvedCrossAutomaticBinCountLimbs);
  append(destination.resolvedCrossAutomaticNodes,
         prepared.resolvedCrossAutomaticNodes);
  append(destination.resolvedCrossAutomaticEdges,
         prepared.resolvedCrossAutomaticEdges);
  append(destination.resolvedCrossSelectorBindings,
         prepared.resolvedCrossSelectorBindings);
  append(destination.resolvedFunctionalValueSets,
         prepared.resolvedFunctionalValueSets);
  append(destination.resolvedFunctionalValueAtoms,
         prepared.resolvedFunctionalValueAtoms);
  append(destination.resolvedFunctionalValueLimbs,
         prepared.resolvedFunctionalValueLimbs);
  append(destination.resolvedFunctionalBinPlans,
         prepared.resolvedFunctionalBinPlans);
  append(destination.resolvedFunctionalBinGroups,
         prepared.resolvedFunctionalBinGroups);
  append(destination.resolvedFunctionalTupleSets,
         prepared.resolvedFunctionalTupleSets);
  append(destination.resolvedFunctionalTupleSetTuples,
         prepared.resolvedFunctionalTupleSetTuples);
  append(destination.resolvedFunctionalTupleSetComponents,
         prepared.resolvedFunctionalTupleSetComponents);
}

obelisk_rt_status
ensureFunctionalConfigurationSchemas(CoverageState &coverage) {
  if (!coverage.schema)
    return OBELISK_RT_INVALID_DESIGN;
  if (coverage.loadedConfigurationSchemas &&
      coverage.loadedConfigurationSchemas->source == coverage.schema.get())
    return OBELISK_RT_OK;
  obelisk_rt_status status = ensureFunctionalConstructionSchemas(coverage);
  if (status != OBELISK_RT_OK)
    return status;
  std::vector<obelisk::coverage::Database> bundles;
  auto partitionStatus = obelisk::coverage::partitionFunctionalConfigurations(
      *coverage.schema, bundles);
  if (partitionStatus != obelisk::coverage::Status::Ok)
    return mapCoverageStatus(partitionStatus);
  auto prepared = std::make_shared<FunctionalCoverageConfigurationSchemas>();
  prepared->source = coverage.schema.get();
  for (auto &bundle : bundles) {
    const auto &row = bundle.functionalConfigurations.front();
    const FunctionalCoverageTypeKey key{row.type, row.configuration};
    auto source = coverage.constructionSchemas->types.find(row.type);
    if (source == coverage.constructionSchemas->types.end() ||
        !obelisk::coverage::isSchemaOnly(*source->second))
      return OBELISK_RT_INVALID_DESIGN;
    auto schema =
        std::make_shared<obelisk::coverage::Database>(*source->second);
    // Partitioning has already checked/rebased each owned payload. The static
    // projection has no resolved rows, so adopt its bundle without another
    // allocation, copy, or rebase of the twenty resolved columns.
    appendPreparedConfiguration(*schema, bundle);
    prepared->configurations.emplace(key, std::move(schema));
  }
  coverage.loadedConfigurationSchemas = std::move(prepared);
  return OBELISK_RT_OK;
}

obelisk_rt_status
bindFunctionalType(obelisk_rt_context *context, CoverageState &coverage,
                   uint64_t typeID, FunctionalCoverageTypeState *&result,
                   const obelisk_rt_functional_value_v1 *expressions = nullptr,
                   uint64_t expressionCount = 0) {
  if (!coverage.schema)
    return OBELISK_RT_INVALID_DESIGN;
  indexFunctionalConfigurations(coverage);
  const bool payloadless = !expressionCount;
  auto overrides = coverage.typeOptions.find(typeID);
  const std::optional<bool> distributeFirst =
      overrides == coverage.typeOptions.end()
          ? std::nullopt
          : overrides->second.distributeFirst;
  const std::pair<uint64_t, uint32_t> payloadlessKey{
      typeID, distributeFirst ? (*distributeFirst ? 1u : 0u) : 2u};
  auto cachePayloadless = [&](const obelisk::coverage::Digest &configuration) {
    if (payloadless)
      coverage.payloadlessConfigurations[payloadlessKey] = configuration;
  };
  if (payloadless) {
    auto cached = coverage.payloadlessConfigurations.find(payloadlessKey);
    if (cached != coverage.payloadlessConfigurations.end())
      return bindResolvedFunctionalType(coverage, typeID, cached->second,
                                        result);
    auto configurations = coverage.configurationsByType.find(typeID);
    if (configurations != coverage.configurationsByType.end() &&
        configurations->second.size() == 1) {
      auto status = bindResolvedFunctionalType(
          coverage, typeID, configurations->second.front(), result);
      if (status != OBELISK_RT_OK)
        return status;
      if (!distributeFirst || result->distributeFirst == *distributeFirst) {
        cachePayloadless(result->configuration);
        return OBELISK_RT_OK;
      }
    }
  }

  obelisk_rt_status status = ensureFunctionalConstructionSchemas(coverage);
  if (status != OBELISK_RT_OK)
    return status;
  auto templateSchema = coverage.constructionSchemas->types.find(typeID);
  if (templateSchema == coverage.constructionSchemas->types.end())
    return OBELISK_RT_INVALID_DESIGN;
  // Resolution and validation touch only this type and this configuration.
  auto resolved =
      std::make_shared<obelisk::coverage::Database>(*templateSchema->second);
  obelisk::coverage::Digest exact{};
  status = resolveStaticFunctionalType(context, *resolved, typeID, expressions,
                                       expressionCount, exact,
                                       *coverage.constructionSchemas);
  if (status != OBELISK_RT_OK)
    return status;
  FunctionalCoverageTypeKey key{typeID, exact};
  auto existing = coverage.types.find(key);
  if (existing != coverage.types.end()) {
    cachePayloadless(exact);
    result = &existing->second;
    return OBELISK_RT_OK;
  }
  obelisk::coverage::Diagnostic diagnostic;
  if (obelisk::coverage::validate(*resolved, &diagnostic) !=
      obelisk::coverage::Status::Ok) {
    std::fprintf(stderr,
                 "error: resolved functional coverage schema is "
                 "invalid%s%s%s%s\n",
                 diagnostic.field ? " in " : "",
                 diagnostic.field ? diagnostic.field : "",
                 diagnostic.detail.empty() ? "" : ": ",
                 diagnostic.detail.empty() ? "" : diagnostic.detail.c_str());
    return OBELISK_RT_INVALID_DESIGN;
  }
  const auto newProfiles = staticFunctionalTypeProfiles(*resolved);
  const auto &profile = newProfiles.at(typeID);
  auto loadedProfile =
      coverage.constructionSchemas->loadedTypeProfiles.find(typeID);
  if (loadedProfile != coverage.constructionSchemas->loadedTypeProfiles.end()) {
    if (loadedProfile->second != profile)
      return OBELISK_RT_INVALID_DESIGN;
  } else {
    auto configurations = coverage.configurationsByType.find(typeID);
    if (configurations != coverage.configurationsByType.end() &&
        !configurations->second.empty()) {
      auto previous =
          coverage.types.find({typeID, configurations->second.front()});
      if (previous == coverage.types.end() ||
          !previous->second.configurationSchema)
        return OBELISK_RT_INVALID_DESIGN;
      const auto previousProfiles =
          staticFunctionalTypeProfiles(*previous->second.configurationSchema);
      if (previousProfiles.at(typeID) != profile)
        return OBELISK_RT_INVALID_DESIGN;
    }
  }
  FunctionalCoverageTypeState type;
  status =
      createResolvedFunctionalType(coverage, *resolved, typeID, exact, type);
  if (status != OBELISK_RT_OK)
    return status;
  type.configurationSchema = resolved;
  obelisk::coverage::Database prepared;
  const bool append = !coverage.configurationKeys.count(key);
  if (append) {
    status = prepareConfigurationAppend(*coverage.schema, *resolved, prepared);
    if (status != OBELISK_RT_OK)
      return status;
  }
  // Index insertion has the strong exception guarantee. After it succeeds,
  // publishing the preallocated columns cannot fail.
  auto insertion = coverage.types.emplace(key, std::move(type));
  if (append)
    appendPreparedConfiguration(*coverage.schema, prepared);
  result = &insertion.first->second;
  cachePayloadless(exact);
  return OBELISK_RT_OK;
}

const obelisk::coverage::FunctionalContainerType *
functionalContainerType(const CoverageState &coverage, uint64_t valueType) {
  if (!valueType || !coverage.constructionSchemas)
    return nullptr;
  auto found = coverage.constructionSchemas->containerTypes.find(valueType);
  return found == coverage.constructionSchemas->containerTypes.end()
             ? nullptr
             : &found->second;
}

const obelisk::coverage::FunctionalContainerType *
functionalDefaultContainerType(const CoverageState &coverage,
                               uint64_t formalID) {
  if (!coverage.constructionSchemas)
    return nullptr;
  auto found = coverage.constructionSchemas->formalValueTypes.find(formalID);
  return found == coverage.constructionSchemas->formalValueTypes.end()
             ? nullptr
             : functionalContainerType(coverage, found->second);
}

const obelisk::coverage::FunctionalAggregateType *
functionalAggregateType(const CoverageState &coverage, uint64_t id) {
  if (!coverage.constructionSchemas)
    return nullptr;
  auto found = coverage.constructionSchemas->aggregateTypes.find(id);
  return found == coverage.constructionSchemas->aggregateTypes.end()
             ? nullptr
             : &found->second;
}

const obelisk::coverage::FunctionalAggregateType *
functionalDefaultAggregateType(const CoverageState &coverage, uint64_t formal) {
  if (!coverage.constructionSchemas)
    return nullptr;
  auto found = coverage.constructionSchemas->formalValueTypes.find(formal);
  return found == coverage.constructionSchemas->formalValueTypes.end()
             ? nullptr
             : functionalAggregateType(coverage, found->second);
}

const obelisk::coverage::FunctionalClassType *
functionalClassType(const CoverageState &coverage, uint64_t id) {
  if (!coverage.constructionSchemas)
    return nullptr;
  auto found = coverage.constructionSchemas->classTypes.find(id);
  return found == coverage.constructionSchemas->classTypes.end()
             ? nullptr
             : &found->second;
}

const obelisk::coverage::FunctionalClassType *
functionalDefaultClassType(const CoverageState &coverage, uint64_t formal) {
  if (!coverage.constructionSchemas)
    return nullptr;
  auto found = coverage.constructionSchemas->formalValueTypes.find(formal);
  return found == coverage.constructionSchemas->formalValueTypes.end()
             ? nullptr
             : functionalClassType(coverage, found->second);
}

bool validateClassFormal(obelisk_rt_context *context,
                         obelisk_rt_object_v1 *object,
                         const obelisk::coverage::FunctionalClassType *type) {
  if (!type)
    return false;
  const auto *descriptor =
      obelisk_rt_managed_class_lookup(context, type->classType);
  return descriptor &&
         (!object ||
          (obelisk_rt_managed_object_belongs_to(context, object) &&
           obelisk_rt_managed_object_kind(object) == OBELISK_RT_MANAGED_CLASS &&
           obelisk_rt_v1_object_is_instance(object, descriptor)));
}

uint64_t formalValueType(const CoverageState &coverage, uint64_t formal) {
  if (!coverage.constructionSchemas)
    return 0;
  auto found = coverage.constructionSchemas->formalValueTypes.find(formal);
  return found == coverage.constructionSchemas->formalValueTypes.end()
             ? 0
             : found->second;
}

bool validateProcessFormal(obelisk_rt_context *context, uint64_t handle) {
  if (!handle)
    return true;
  obelisk_rt_process_state state = OBELISK_RT_PROCESS_FINISHED;
  return obelisk_rt_v1_process_status(context, handle, &state) == OBELISK_RT_OK;
}

bool validateSemaphoreFormal(obelisk_rt_context *context,
                             obelisk_rt_object_v1 *object) {
  if (!object)
    return true;
  if (!obelisk_rt_managed_object_belongs_to(context, object))
    return false;
  // A zero-key readiness query checks the private bucket descriptor without
  // acquiring keys or disturbing queued waiters.
  bool ready = false;
  return obelisk_rt_semaphore_keys_ready(object, 0, ready) == OBELISK_RT_OK;
}

bool validateVirtualInterfaceFormal(obelisk_rt_context *context,
                                    uint64_t handle, uint64_t valueType) {
  if (!handle)
    return true;
  CoverageState *coverage = coverageState(context, false);
  if (!coverage || !coverage->constructionSchemas)
    return false;
  const auto &schemas = *coverage->constructionSchemas;
  auto type = schemas.virtualInterfaceTypes.find(valueType);
  auto scope = schemas.virtualInterfaceScopes.find(handle);
  return type != schemas.virtualInterfaceTypes.end() &&
         scope != schemas.virtualInterfaceScopes.end() &&
         type->second == scope->second;
}

bool validateCovergroupFormal(obelisk_rt_context *context, uint64_t handle,
                              uint64_t type) {
  CoverageState *coverage = coverageState(context, false);
  if (!coverage || !coverage->constructionSchemas ||
      !coverage->constructionSchemas->hierarchy.contains(type))
    return false;
  if (!handle)
    return true;
  auto found = coverage->instances.find(handle);
  return found != coverage->instances.end() &&
         coverage->constructionSchemas->hierarchy.isInstanceOf(
             found->second.typeID, type);
}

bool validateAggregateFormal(
    obelisk_rt_context *context, obelisk_rt_object_v1 *object,
    const obelisk::coverage::FunctionalAggregateType *type) {
  if (!type || !object ||
      !obelisk_rt_managed_object_belongs_to(context, object))
    return false;
  const obelisk_rt_element_type_v1 *element = nullptr;
  return obelisk_rt_reference_path_element(object, &element) == OBELISK_RT_OK &&
         element->kind == OBELISK_RT_ELEMENT_AGGREGATE &&
         element->type_id == type->elementType &&
         element->value_size == type->valueSize &&
         element->alignment == type->alignment && element->flags == type->flags;
}

bool validateContainerFormal(
    obelisk_rt_context *context, obelisk_rt_object_v1 *object,
    const obelisk::coverage::FunctionalContainerType *type) {
  if (!type)
    return false;
  if (!object)
    return true;
  if (!obelisk_rt_managed_object_belongs_to(context, object))
    return false;
  const auto *element =
      obelisk_rt_managed_element_type_lookup(context, type->elementType);
  if (!element)
    return false;
  struct Validation {
    const obelisk::coverage::FunctionalContainerType &type;
    const obelisk_rt_element_type_v1 *element;
  } validation{*type, element};
  return obelisk_rt_managed_object_access(
             object, OBELISK_RT_MANAGED_CONTAINER,
             [](void *environment, uint8_t *data,
                uint64_t extent) -> obelisk_rt_status {
               if (extent != sizeof(obelisk::runtime_detail::ContainerHeader))
                 return OBELISK_RT_INVALID_HANDLE;
               const auto &header = *reinterpret_cast<
                   const obelisk::runtime_detail::ContainerHeader *>(data);
               const auto &validation =
                   *static_cast<const Validation *>(environment);
               const auto &type = validation.type;
               bool mailbox =
                   type.kind ==
                   obelisk::coverage::FunctionalContainerKind::Mailbox;
               uint32_t kind = mailbox ? OBELISK_RT_CONTAINER_QUEUE
                                       : static_cast<uint32_t>(type.kind);
               return header.kind == kind &&
                              header.isMailbox ==
                                  static_cast<uint32_t>(mailbox) &&
                              header.element == validation.element &&
                              header.keyKind ==
                                  static_cast<uint32_t>(type.keyKind) &&
                              header.keyWidth == type.keyWidth &&
                              (mailbox || header.bound == type.bound)
                          ? OBELISK_RT_OK
                          : OBELISK_RT_INVALID_DESIGN;
             },
             &validation) == OBELISK_RT_OK;
}

bool validateValue(
    obelisk_rt_context *context, const obelisk_rt_functional_value_v1 &value,
    obelisk::coverage::FunctionalExpressionResultKind result, uint64_t width,
    bool reference,
    const obelisk::coverage::FunctionalContainerType *containerType = nullptr,
    const obelisk::coverage::FunctionalAggregateType *aggregateType = nullptr,
    const obelisk::coverage::FunctionalClassType *classType = nullptr,
    uint64_t valueType = 0) {
  using Result = obelisk::coverage::FunctionalExpressionResultKind;
  if (obelisk::coverage::isRealResultKind(result))
    width = obelisk::coverage::getRealResultBitWidth(result);
  // Boolean schema descriptors have no integral type width, but their helper
  // ABI representation is the canonical one-bit logic value.
  if (result == Result::Boolean)
    width = 1;
  else if ((result == Result::String || result == Result::Container ||
            result == Result::ClassHandle || result == Result::Chandle ||
            result == Result::Event || result == Result::Covergroup ||
            result == Result::Process || result == Result::VirtualInterface ||
            result == Result::Semaphore || result == Result::Mailbox) &&
           reference)
    width = sizeof(obelisk_rt_string_v1) * 8;
  if (!value.id)
    return false;
  if (reference) {
    if (!(value.kind == OBELISK_RT_FUNCTIONAL_VALUE_ARGUMENT_REF &&
          value.bit_width == width && value.value_size == (width + 7) / 8 &&
          !value.value && !value.unknown && value.argument_ref_kind <= 2 &&
          (value.argument_ref_kind == 0 || value.owner)))
      return false;
    if (result == Result::Aggregate)
      return aggregateType &&
             (value.argument_ref_kind != 2 ||
              validateAggregateFormal(context, value.owner, aggregateType));
    if (result != Result::Container && result != Result::ClassHandle &&
        result != Result::Covergroup && result != Result::Process &&
        result != Result::VirtualInterface && result != Result::Semaphore &&
        result != Result::Mailbox)
      return true;
    const uint8_t *stateValue =
        context->nativeStateValue
            ? context->nativeStateValue
            : reinterpret_cast<const uint8_t *>(context->stateValue.data());
    const uint8_t *stateUnknown =
        context->nativeStateValue
            ? context->nativeStateUnknown
            : reinterpret_cast<const uint8_t *>(context->stateUnknown.data());
    uint64_t stateBits = context->nativeStateValue
                             ? context->nativeStateBitCount
                             : context->stateValue.size() * uint64_t{64};
    uint64_t word = 0;
    if (obelisk_rt_v1_argument_ref_load(
            context, stateValue, stateUnknown, stateBits, value.owner,
            value.payload, value.argument_ref_kind, 64, 8, 0,
            result == Result::Covergroup || result == Result::Process ||
                    result == Result::VirtualInterface
                ? OBELISK_RT_ARGUMENT_VALUE_BITS
                : OBELISK_RT_ARGUMENT_VALUE_CLASS,
            &word, nullptr) != OBELISK_RT_OK)
      return false;
    if (result == Result::Covergroup)
      return validateCovergroupFormal(context, word, valueType);
    if (result == Result::Process)
      return validateProcessFormal(context, word);
    if (result == Result::VirtualInterface)
      return validateVirtualInterfaceFormal(context, word, valueType);
    auto *object = obelisk_rt_object_from_managed_word(word);
    return obelisk_rt_managed_word_from_object(object) == word &&
           (result == Result::Semaphore
                ? validateSemaphoreFormal(context, object)
            : result == Result::ClassHandle
                ? validateClassFormal(context, object, classType)
                : validateContainerFormal(context, object, containerType));
  }
  if (result == Result::Covergroup || result == Result::Process ||
      result == Result::VirtualInterface)
    return value.kind == (result == Result::VirtualInterface
                              ? OBELISK_RT_FUNCTIONAL_VALUE_VIRTUAL_INTERFACE
                          : result == Result::Process
                              ? OBELISK_RT_FUNCTIONAL_VALUE_PROCESS
                              : OBELISK_RT_FUNCTIONAL_VALUE_COVERGROUP) &&
           !value.bit_width && !value.value_size && !value.value &&
           !value.unknown && !value.owner && !value.argument_ref_kind &&
           (result == Result::VirtualInterface
                ? validateVirtualInterfaceFormal(context, value.payload,
                                                 valueType)
            : result == Result::Process
                ? validateProcessFormal(context, value.payload)
                : validateCovergroupFormal(context, value.payload, valueType));
  if (result == Result::Chandle || result == Result::Event)
    return value.kind == (result == Result::Event
                              ? OBELISK_RT_FUNCTIONAL_VALUE_EVENT
                              : OBELISK_RT_FUNCTIONAL_VALUE_CHANDLE) &&
           !value.bit_width && !value.value_size && !value.value &&
           !value.unknown && !value.owner && !value.argument_ref_kind;
  if (result == Result::Semaphore)
    return value.kind == OBELISK_RT_FUNCTIONAL_VALUE_SEMAPHORE &&
           !value.bit_width && !value.value_size && !value.value &&
           !value.unknown && !value.payload && !value.argument_ref_kind &&
           validateSemaphoreFormal(context, value.owner);
  if (result == Result::ClassHandle)
    return value.kind == OBELISK_RT_FUNCTIONAL_VALUE_CLASS_HANDLE &&
           !value.bit_width && !value.value_size && !value.value &&
           !value.unknown && !value.payload && !value.argument_ref_kind &&
           validateClassFormal(context, value.owner, classType);
  if (result == Result::Aggregate)
    return value.kind == OBELISK_RT_FUNCTIONAL_VALUE_MANAGED_AGGREGATE &&
           !value.bit_width && !value.value_size && !value.value &&
           !value.unknown && !value.payload && !value.argument_ref_kind &&
           validateAggregateFormal(context, value.owner, aggregateType);
  if (result == Result::Container || result == Result::Mailbox)
    return value.kind ==
               (result == Result::Mailbox
                    ? OBELISK_RT_FUNCTIONAL_VALUE_MAILBOX
                    : OBELISK_RT_FUNCTIONAL_VALUE_MANAGED_CONTAINER) &&
           !value.bit_width && !value.value_size && !value.value &&
           !value.unknown && !value.payload && !value.argument_ref_kind &&
           validateContainerFormal(context, value.owner, containerType);
  if (result == Result::String)
    return value.kind == OBELISK_RT_FUNCTIONAL_VALUE_STRING &&
           value.bit_width == 0 && value.value_size == 0 && !value.value &&
           !value.unknown && !value.owner && value.argument_ref_kind == 0 &&
           obelisk_rt_validate_string(context, value.payload) == OBELISK_RT_OK;
  if (result == Result::TupleQueue || result == Result::Set)
    return value.kind == OBELISK_RT_FUNCTIONAL_VALUE_MANAGED_CONTAINER &&
           value.bit_width == 0 && value.value_size == 0 && !value.value &&
           !value.unknown && !value.payload && !value.argument_ref_kind &&
           (!value.owner ||
            (obelisk_rt_managed_object_belongs_to(context, value.owner) &&
             obelisk_rt_managed_object_kind(value.owner) ==
                 OBELISK_RT_MANAGED_CONTAINER));
  if (value.owner || value.payload || value.argument_ref_kind || !value.value)
    return false;
  if (obelisk::coverage::isRealResultKind(result))
    return value.kind == (result == Result::ShortReal
                              ? OBELISK_RT_FUNCTIONAL_VALUE_SHORTREAL
                              : OBELISK_RT_FUNCTIONAL_VALUE_REAL) &&
           value.bit_width == width && value.value_size == width / 8 &&
           !value.unknown;
  if (result != Result::Boolean && result != Result::Integral)
    return false;
  if ((value.kind != OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL &&
       value.kind != OBELISK_RT_FUNCTIONAL_VALUE_FOUR_STATE) ||
      value.bit_width != width || !width ||
      value.value_size != (width + 7) / 8 ||
      (value.kind == OBELISK_RT_FUNCTIONAL_VALUE_FOUR_STATE) !=
          (value.unknown != nullptr))
    return false;
  unsigned tail = width % 8;
  if (!tail)
    return true;
  uint8_t padding = static_cast<uint8_t>(~((uint16_t{1} << tail) - 1));
  auto *aval = static_cast<const uint8_t *>(value.value);
  auto *bval = static_cast<const uint8_t *>(value.unknown);
  return !(aval[value.value_size - 1] & padding) &&
         (!bval || !(bval[value.value_size - 1] & padding));
}

void snapshotValue(const obelisk_rt_functional_value_v1 &source,
                   FunctionalCoverageValue &destination) {
  destination.id = source.id;
  destination.bitWidth = source.bit_width;
  destination.valueSize = source.value_size;
  destination.kind = source.kind;
  destination.argumentRefKind = source.argument_ref_kind;
  destination.owner = source.owner;
  destination.payload = source.payload;
  if (source.kind == OBELISK_RT_FUNCTIONAL_VALUE_ARGUMENT_REF ||
      source.kind == OBELISK_RT_FUNCTIONAL_VALUE_MANAGED_CONTAINER ||
      source.kind == OBELISK_RT_FUNCTIONAL_VALUE_MANAGED_AGGREGATE ||
      source.kind == OBELISK_RT_FUNCTIONAL_VALUE_CLASS_HANDLE ||
      source.kind == OBELISK_RT_FUNCTIONAL_VALUE_SEMAPHORE ||
      source.kind == OBELISK_RT_FUNCTIONAL_VALUE_MAILBOX ||
      source.kind == OBELISK_RT_FUNCTIONAL_VALUE_CHANDLE ||
      source.kind == OBELISK_RT_FUNCTIONAL_VALUE_EVENT ||
      source.kind == OBELISK_RT_FUNCTIONAL_VALUE_COVERGROUP ||
      source.kind == OBELISK_RT_FUNCTIONAL_VALUE_PROCESS ||
      source.kind == OBELISK_RT_FUNCTIONAL_VALUE_VIRTUAL_INTERFACE)
    return;
  // Strings are immutable values. The context's service-root enumerator
  // retains this captured managed word independently of the caller's storage.
  if (source.kind == OBELISK_RT_FUNCTIONAL_VALUE_STRING)
    return;
  auto *value = static_cast<const uint8_t *>(source.value);
  destination.value.assign(value, value + source.value_size);
  if (source.unknown) {
    auto *unknown = static_cast<const uint8_t *>(source.unknown);
    destination.unknown.assign(unknown, unknown + source.value_size);
  }
}

template <typename Entry>
std::vector<const Entry *> schemaOrdered(const std::vector<Entry> &entries,
                                         uint64_t typeID, bool constructor) {
  std::vector<const Entry *> result;
  // Rows are already owner-filtered in these construction/configuration
  // projections. Borrowed nominal identities have no expression rows.
  // Resolving each expression's owner again would make batching quadratic.
  for (const auto &entry : entries) {
    if constexpr (std::is_same_v<Entry, obelisk::coverage::FunctionalFormal>) {
      if (entry.type == typeID &&
          (entry.kind ==
           obelisk::coverage::FunctionalFormalKind::Constructor) == constructor)
        result.push_back(&entry);
    } else {
      using Phase = obelisk::coverage::FunctionalExpressionEvaluationPhase;
      const bool selected =
          constructor ? (entry.evaluationPhase == Phase::Constructor ||
                         entry.evaluationPhase == Phase::Option)
                      : entry.evaluationPhase == Phase::Sample;
      if (selected)
        result.push_back(&entry);
    }
  }
  sortByKey(result.begin(), result.end(), [](const auto *row) {
    if constexpr (std::is_same_v<Entry, obelisk::coverage::FunctionalFormal>)
      return row->ordinal;
    else
      return std::tie(row->evaluationPhase, row->resultOrdinal);
  });
  return result;
}

obelisk_rt_status
prepareFunctionalSampleMetadata(const obelisk::coverage::Database &schema,
                                uint64_t typeID,
                                const FunctionalCoverageTypeState &type,
                                FunctionalCoverageQueryMetadata &metadata) {
  using namespace obelisk::coverage;
  metadata.sampleExpressions =
      schemaOrdered(schema.functionalExpressions, typeID, false);
  metadata.sampleItems.resize(metadata.items.size());
  metadata.binIffs.assign(metadata.bins.size(), SIZE_MAX);
  metadata.binValueSets.resize(metadata.bins.size());
  IndexMap<uint64_t, size_t> binIffs;
  for (size_t index = 0; index != metadata.sampleExpressions.size(); ++index) {
    const auto &expression = *metadata.sampleExpressions[index];
    if (expression.role == FunctionalExpressionRole::BinIff) {
      binIffs.emplace(expression.owner, index);
      continue;
    }
    if (expression.role != FunctionalExpressionRole::CoverpointSample &&
        expression.role != FunctionalExpressionRole::CoverpointIff &&
        expression.role != FunctionalExpressionRole::CrossIff)
      continue;
    auto item = metadata.templateItemPositions.find(expression.owner);
    if (item == metadata.templateItemPositions.end())
      return OBELISK_RT_INVALID_DESIGN;
    auto &sample = metadata.sampleItems[item->second];
    if (expression.role == FunctionalExpressionRole::CoverpointSample)
      sample.value = index;
    else
      sample.iff = index;
  }
  for (size_t index = 0; index != type.bins.size(); ++index) {
    const auto &bin = type.bins[index];
    if (auto iff = binIffs.find(bin.templateBin); iff != binIffs.end())
      metadata.binIffs[index] = iff->second;
    if (bin.valueSet) {
      auto set = metadata.valueSets.find(bin.valueSet);
      if (set == metadata.valueSets.end())
        return OBELISK_RT_INVALID_DESIGN;
      metadata.binValueSets[index] = set->second;
    }
  }
  IndexMap<uint64_t, const TransitionProgram *> programs;
  for (const auto &program : schema.transitionPrograms)
    programs.emplace(program.bin, &program);
  for (const auto &alternative : schema.resolvedTransitionAlternatives) {
    if (alternative.type != typeID ||
        alternative.configuration != type.configuration)
      continue;
    auto position = metadata.binPositions.find(alternative.bin);
    if (position == metadata.binPositions.end())
      return OBELISK_RT_INVALID_DESIGN;
    const auto &bin = type.bins[position->second];
    auto item = metadata.itemPositions.find(bin.item);
    auto program = programs.find(bin.templateBin);
    if (item == metadata.itemPositions.end() || program == programs.end() ||
        bin.kind != FunctionalBinKind::Transition || !alternative.stepCount ||
        uint64_t{alternative.firstStep} + alternative.stepCount >
            schema.resolvedTransitionSteps.size() ||
        alternative.templateAlternativeOrdinal >=
            program->second->alternativeCount ||
        uint64_t{program->second->firstAlternative} +
                program->second->alternativeCount >
            schema.transitionAlternatives.size())
      return OBELISK_RT_INVALID_DESIGN;
    const auto &definition =
        schema.transitionAlternatives[program->second->firstAlternative +
                                      alternative.templateAlternativeOrdinal];
    if (definition.stepCount != alternative.stepCount ||
        uint64_t{definition.firstStep} + definition.stepCount >
            schema.transitionSteps.size())
      return OBELISK_RT_INVALID_DESIGN;
    for (uint32_t ordinal = 0; ordinal != alternative.stepCount; ++ordinal) {
      const auto &step =
          schema.resolvedTransitionSteps[alternative.firstStep + ordinal];
      if (step.alternative != alternative.id || step.ordinal != ordinal ||
          !metadata.valueSets.count(step.valueSet))
        return OBELISK_RT_INVALID_DESIGN;
    }
    auto &sampleItem = metadata.sampleItems[item->second];
    if (bin.flags & (FunctionalBinIgnore | FunctionalBinIllegal)) {
      uint64_t maximumLength = 0;
      for (uint32_t ordinal = 0; ordinal != alternative.stepCount; ++ordinal)
        maximumLength = saturatingAdd(
            maximumLength,
            schema.resolvedTransitionSteps[alternative.firstStep + ordinal]
                .upperBound);
      sampleItem.transitionLengthLimit =
          std::max(sampleItem.transitionLengthLimit, maximumLength);
    }
    sampleItem.transitions.push_back({&alternative, &definition,
                                      position->second,
                                      metadata.transitionSlotCount++});
  }
  return OBELISK_RT_OK;
}

FunctionalCoverageDiagnosticIndex &
indexFunctionalDiagnostics(CoverageState &coverage) {
  if (!coverage.diagnosticIndex)
    coverage.diagnosticIndex =
        std::make_shared<FunctionalCoverageDiagnosticIndex>();
  auto &index = *coverage.diagnosticIndex;
  const auto &rows = index.rows;
  for (; index.indexed != rows.size(); ++index.indexed) {
    const auto &row = rows[index.indexed];
    index.positions.emplace(
        std::make_tuple(row.run, row.bin, row.instance, row.simulationTime),
        index.indexed);
  }
  return index;
}

uint64_t readPlaneLimb(const void *plane, uint64_t size, uint32_t ordinal) {
  return readLittleEndianLimb(plane, size, ordinal);
}

bool functionalBooleanTrue(const obelisk_rt_functional_value_v1 &value) {
  uint64_t unknown = readPlaneLimb(value.unknown, value.value_size, 0);
  uint64_t encoded = readPlaneLimb(value.value, value.value_size, 0);
  // !simulation.logic transports (aval ^ bval, bval).  IEEE boolean
  // conversion accepts only a known one; X and Z are not true.
  uint64_t aval = value.kind == OBELISK_RT_FUNCTIONAL_VALUE_FOUR_STATE
                      ? encoded ^ unknown
                      : encoded;
  return !(unknown & 1) && (aval & 1);
}

int compareIntegral(
    const obelisk_rt_functional_value_v1 &value,
    const std::vector<obelisk::coverage::ResolvedFunctionalValueLimb> &limbs,
    const obelisk::coverage::ResolvedFunctionalValueAtom &atom, bool high,
    obelisk::coverage::CoverageSignedness signedness) {
  uint32_t count = static_cast<uint32_t>((value.bit_width + 63) / 64);
  for (uint32_t reverse = count; reverse != 0; --reverse) {
    uint32_t ordinal = reverse - 1;
    uint64_t actual = readPlaneLimb(value.value, value.value_size, ordinal);
    uint64_t bound = 0;
    if (ordinal < atom.limbCount) {
      const auto &limb = limbs[atom.firstLimb + ordinal];
      bound = high ? limb.highAval : limb.lowAval;
    }
    if (ordinal == count - 1 &&
        signedness == obelisk::coverage::CoverageSignedness::Signed) {
      uint64_t sign = uint64_t{1} << ((value.bit_width - 1) % 64);
      actual ^= sign;
      bound ^= sign;
    }
    if (actual != bound)
      return actual < bound ? -1 : 1;
  }
  return 0;
}

bool valueSetMatches(const obelisk::coverage::Database &schema,
                     const obelisk::coverage::ResolvedFunctionalValueSet *set,
                     const obelisk_rt_functional_value_v1 &value) {
  if (!set ||
      (set->kind == obelisk::coverage::FunctionalValueSetKind::Integral &&
       set->bitWidth != value.bit_width))
    return false;
  for (uint32_t atomIndex = 0; atomIndex != set->atomCount; ++atomIndex) {
    const auto &atom =
        schema.resolvedFunctionalValueAtoms[set->firstAtom + atomIndex];
    if (atom.kind == obelisk::coverage::FunctionalValueAtomKind::RealInterval) {
      auto real = functionalRealValue(value);
      if (!real)
        continue;
      double actual = *real, low = 0.0, high = 0.0;
      std::memcpy(&low, &atom.realLowBits, sizeof(low));
      std::memcpy(&high, &atom.realHighBits, sizeof(high));
      bool lower =
          (atom.flags & obelisk::coverage::FunctionalValueAtomLowerUnbounded) ||
          ((atom.flags & obelisk::coverage::FunctionalValueAtomLowerInclusive)
               ? actual >= low
               : actual > low);
      bool upper =
          (atom.flags & obelisk::coverage::FunctionalValueAtomUpperUnbounded) ||
          ((atom.flags & obelisk::coverage::FunctionalValueAtomUpperInclusive)
               ? actual <= high
               : actual < high);
      if (lower && upper)
        return true;
      continue;
    }
    if (isFunctionalRealValueKind(value.kind))
      continue;
    bool exact = true;
    for (uint32_t limbIndex = 0; limbIndex != atom.limbCount; ++limbIndex) {
      const auto &limb =
          schema.resolvedFunctionalValueLimbs[atom.firstLimb + limbIndex];
      uint64_t encoded =
          readPlaneLimb(value.value, value.value_size, limbIndex);
      uint64_t bval = readPlaneLimb(value.unknown, value.value_size, limbIndex);
      uint64_t aval = value.kind == OBELISK_RT_FUNCTIONAL_VALUE_FOUR_STATE
                          ? encoded ^ bval
                          : encoded;
      uint64_t mask = ~limb.wildcardMask;
      // A wildcard bin expands schema X/Z/? positions to 0 and 1 only.  A
      // sampled X or Z in such a position is never part of that expansion.
      exact &= !(bval & limb.wildcardMask) && !((aval ^ limb.lowAval) & mask) &&
               !((bval ^ limb.lowBval) & mask);
    }
    if (atom.kind ==
            obelisk::coverage::FunctionalValueAtomKind::IntegralValue &&
        exact)
      return true;
    bool known = true;
    for (uint32_t limbIndex = 0; limbIndex != (value.bit_width + 63) / 64;
         ++limbIndex)
      known &= !readPlaneLimb(value.unknown, value.value_size, limbIndex);
    if (atom.kind ==
            obelisk::coverage::FunctionalValueAtomKind::IntegralRange &&
        known) {
      int low = compareIntegral(value, schema.resolvedFunctionalValueLimbs,
                                atom, false, set->signedness);
      int high = compareIntegral(value, schema.resolvedFunctionalValueLimbs,
                                 atom, true, set->signedness);
      bool lower =
          (atom.flags & obelisk::coverage::FunctionalValueAtomLowerInclusive)
              ? low >= 0
              : low > 0;
      bool upper =
          (atom.flags & obelisk::coverage::FunctionalValueAtomUpperInclusive)
              ? high <= 0
              : high < 0;
      if (lower && upper)
        return true;
    }
  }
  return false;
}

const obelisk::coverage::ResolvedFunctionalValueSet *
findResolvedValueSet(const obelisk::coverage::Database &schema,
                     const obelisk::coverage::Digest &configuration,
                     uint64_t id) {
  auto found = std::find_if(
      schema.resolvedFunctionalValueSets.begin(),
      schema.resolvedFunctionalValueSets.end(), [&](const auto &entry) {
        return entry.id == id && entry.configuration == configuration;
      });
  return found == schema.resolvedFunctionalValueSets.end() ? nullptr : &*found;
}

int compareAtomEndpoint(
    const obelisk::coverage::Database &schema,
    const obelisk::coverage::ResolvedFunctionalValueAtom &left, bool leftHigh,
    const obelisk::coverage::ResolvedFunctionalValueAtom &right, bool rightHigh,
    uint32_t bitWidth, obelisk::coverage::CoverageSignedness signedness) {
  uint32_t count = functionalLimbCount(bitWidth);
  for (uint32_t reverse = count; reverse != 0; --reverse) {
    uint32_t ordinal = reverse - 1;
    auto endpoint = [&](const auto &atom, bool high) {
      if (ordinal >= atom.limbCount)
        return uint64_t{0};
      const auto &limb =
          schema.resolvedFunctionalValueLimbs[atom.firstLimb + ordinal];
      return high ? limb.highAval : limb.lowAval;
    };
    uint64_t a = endpoint(left, leftHigh);
    uint64_t b = endpoint(right, rightHigh);
    if (ordinal == count - 1 &&
        signedness == obelisk::coverage::CoverageSignedness::Signed) {
      uint64_t sign = uint64_t{1} << ((bitWidth - 1) % 64);
      a ^= sign;
      b ^= sign;
    }
    if (a != b)
      return a < b ? -1 : 1;
  }
  return 0;
}

bool cubeContains(
    const obelisk::coverage::Database &schema,
    const obelisk::coverage::ResolvedFunctionalValueAtom &container,
    const obelisk::coverage::ResolvedFunctionalValueAtom &subject) {
  if (container.limbCount != subject.limbCount)
    return false;
  for (uint32_t index = 0; index != container.limbCount; ++index) {
    const auto &outer =
        schema.resolvedFunctionalValueLimbs[container.firstLimb + index];
    const auto &inner =
        schema.resolvedFunctionalValueLimbs[subject.firstLimb + index];
    uint64_t outerWildcard = outer.wildcardMask;
    uint64_t innerWildcard = inner.wildcardMask;
    uint64_t constrained = ~outerWildcard;
    // A constrained outer bit cannot contain a subject wildcard. Conversely,
    // schema wildcards denote only {0,1}, never a sampled X/Z singleton.
    if ((innerWildcard & constrained) || (inner.lowBval & outerWildcard) ||
        ((outer.lowAval ^ inner.lowAval) & constrained) ||
        ((outer.lowBval ^ inner.lowBval) & constrained))
      return false;
  }
  return true;
}

uint64_t
atomEndpointLimb(const obelisk::coverage::Database &schema,
                 const obelisk::coverage::ResolvedFunctionalValueAtom &atom,
                 bool high, uint32_t ordinal) {
  if (ordinal >= atom.limbCount)
    return 0;
  const auto &limb =
      schema.resolvedFunctionalValueLimbs[atom.firstLimb + ordinal];
  return high ? limb.highAval : limb.lowAval;
}

int compareIntegralBitsToAtomEndpoint(
    const obelisk::coverage::Database &schema,
    const std::vector<uint64_t> &bits,
    const obelisk::coverage::ResolvedFunctionalValueAtom &atom, bool high,
    uint32_t bitWidth, obelisk::coverage::CoverageSignedness signedness) {
  uint32_t count = functionalLimbCount(bitWidth);
  for (uint32_t reverse = count; reverse != 0; --reverse) {
    uint32_t ordinal = reverse - 1;
    uint64_t a = bits[ordinal];
    uint64_t b = atomEndpointLimb(schema, atom, high, ordinal);
    if (ordinal == count - 1 &&
        signedness == obelisk::coverage::CoverageSignedness::Signed) {
      uint64_t sign = uint64_t{1} << ((bitWidth - 1) % 64);
      a ^= sign;
      b ^= sign;
    }
    if (a != b)
      return a < b ? -1 : 1;
  }
  return 0;
}

bool rangeContainsCube(
    const obelisk::coverage::Database &schema,
    const obelisk::coverage::ResolvedFunctionalValueAtom &range,
    const obelisk::coverage::ResolvedFunctionalValueAtom &cube,
    uint32_t bitWidth, obelisk::coverage::CoverageSignedness signedness) {
  uint32_t count = functionalLimbCount(bitWidth);
  std::vector<uint64_t> minimum(count), maximum(count);
  for (uint32_t ordinal = 0; ordinal != count; ++ordinal) {
    const auto &limb =
        schema.resolvedFunctionalValueLimbs[cube.firstLimb + ordinal];
    uint64_t valid = ordinal + 1 == count && bitWidth % 64
                         ? lowBitsMask(bitWidth % 64)
                         : UINT64_MAX;
    if (limb.lowBval & ~limb.wildcardMask & valid)
      return false;
    minimum[ordinal] = limb.lowAval & ~limb.wildcardMask & valid;
    maximum[ordinal] = (limb.lowAval | limb.wildcardMask) & valid;
  }
  if (signedness == obelisk::coverage::CoverageSignedness::Signed) {
    uint32_t signBit = bitWidth - 1;
    uint32_t signLimb = signBit / 64;
    uint64_t signMask = uint64_t{1} << (signBit % 64);
    const auto &limb =
        schema.resolvedFunctionalValueLimbs[cube.firstLimb + signLimb];
    const bool signWildcard = (limb.wildcardMask & signMask) != 0;
    const bool signOne = (limb.lowAval & signMask) != 0;
    if (signWildcard || signOne)
      minimum[signLimb] |= signMask;
    else
      minimum[signLimb] &= ~signMask;
    if (signWildcard || !signOne)
      maximum[signLimb] &= ~signMask;
    else
      maximum[signLimb] |= signMask;
  }
  return compareIntegralBitsToAtomEndpoint(schema, minimum, range, false,
                                           bitWidth, signedness) >= 0 &&
         compareIntegralBitsToAtomEndpoint(schema, maximum, range, true,
                                           bitWidth, signedness) <= 0;
}

bool cubeContainsRange(
    const obelisk::coverage::Database &schema,
    const obelisk::coverage::ResolvedFunctionalValueAtom &cube,
    const obelisk::coverage::ResolvedFunctionalValueAtom &range,
    uint32_t bitWidth) {
  uint32_t count = functionalLimbCount(bitWidth);
  int64_t highestDifference = -1;
  for (uint32_t ordinal = 0; ordinal != count; ++ordinal) {
    const auto &limb =
        schema.resolvedFunctionalValueLimbs[range.firstLimb + ordinal];
    uint64_t valid = ordinal + 1 == count && bitWidth % 64
                         ? lowBitsMask(bitWidth % 64)
                         : UINT64_MAX;
    if ((limb.lowBval | limb.highBval | limb.wildcardMask) & valid)
      return false;
    uint64_t difference = (limb.lowAval ^ limb.highAval) & valid;
    if (difference) {
      uint32_t bit = 0;
      while (difference >>= 1)
        ++bit;
      highestDifference = static_cast<int64_t>(ordinal) * 64 + bit;
    }
  }
  for (uint32_t ordinal = 0; ordinal != count; ++ordinal) {
    const auto &outer =
        schema.resolvedFunctionalValueLimbs[cube.firstLimb + ordinal];
    const auto &inner =
        schema.resolvedFunctionalValueLimbs[range.firstLimb + ordinal];
    uint64_t valid = ordinal + 1 == count && bitWidth % 64
                         ? lowBitsMask(bitWidth % 64)
                         : UINT64_MAX;
    uint64_t constrained = ~outer.wildcardMask & valid;
    if ((outer.lowBval & constrained) ||
        ((outer.lowAval ^ inner.lowAval) & constrained))
      return false;
    if (highestDifference >= 0) {
      uint64_t firstVariable = uint64_t{ordinal} * 64;
      uint64_t variable =
          highestDifference < static_cast<int64_t>(firstVariable) ? 0
          : highestDifference >= static_cast<int64_t>(firstVariable + 63)
              ? UINT64_MAX
              : lowBitsMask(static_cast<uint64_t>(highestDifference) -
                            firstVariable + 1);
      if (constrained & variable)
        return false;
    }
  }
  return true;
}

bool atomContains(
    const obelisk::coverage::Database &schema,
    const obelisk::coverage::ResolvedFunctionalValueSet &containerSet,
    const obelisk::coverage::ResolvedFunctionalValueAtom &container,
    const obelisk::coverage::ResolvedFunctionalValueSet &subjectSet,
    const obelisk::coverage::ResolvedFunctionalValueAtom &subject) {
  using AtomKind = obelisk::coverage::FunctionalValueAtomKind;
  if (containerSet.kind != subjectSet.kind ||
      containerSet.bitWidth != subjectSet.bitWidth ||
      containerSet.signedness != subjectSet.signedness)
    return false;
  if (container.kind == AtomKind::RealInterval ||
      subject.kind == AtomKind::RealInterval) {
    if (container.kind != AtomKind::RealInterval ||
        subject.kind != AtomKind::RealInterval)
      return false;
    double outerLow = 0.0, outerHigh = 0.0, innerLow = 0.0, innerHigh = 0.0;
    std::memcpy(&outerLow, &container.realLowBits, sizeof(outerLow));
    std::memcpy(&outerHigh, &container.realHighBits, sizeof(outerHigh));
    std::memcpy(&innerLow, &subject.realLowBits, sizeof(innerLow));
    std::memcpy(&innerHigh, &subject.realHighBits, sizeof(innerHigh));
    const bool outerLowerUnbounded =
        container.flags & obelisk::coverage::FunctionalValueAtomLowerUnbounded;
    const bool outerUpperUnbounded =
        container.flags & obelisk::coverage::FunctionalValueAtomUpperUnbounded;
    const bool innerLowerUnbounded =
        subject.flags & obelisk::coverage::FunctionalValueAtomLowerUnbounded;
    const bool innerUpperUnbounded =
        subject.flags & obelisk::coverage::FunctionalValueAtomUpperUnbounded;
    bool low = outerLowerUnbounded ||
               (!innerLowerUnbounded &&
                (outerLow < innerLow ||
                 (outerLow == innerLow &&
                  ((container.flags &
                    obelisk::coverage::FunctionalValueAtomLowerInclusive) ||
                   !(subject.flags &
                     obelisk::coverage::FunctionalValueAtomLowerInclusive)))));
    bool high = outerUpperUnbounded ||
                (!innerUpperUnbounded &&
                 (outerHigh > innerHigh ||
                  (outerHigh == innerHigh &&
                   ((container.flags &
                     obelisk::coverage::FunctionalValueAtomUpperInclusive) ||
                    !(subject.flags &
                      obelisk::coverage::FunctionalValueAtomUpperInclusive)))));
    return low && high;
  }

  bool containerRange = container.kind == AtomKind::IntegralRange;
  bool subjectRange = subject.kind == AtomKind::IntegralRange;
  if (!containerRange && !subjectRange)
    return cubeContains(schema, container, subject);
  if (!containerRange)
    return cubeContainsRange(schema, container, subject, subjectSet.bitWidth);
  if (!subjectRange)
    return rangeContainsCube(schema, container, subject, subjectSet.bitWidth,
                             subjectSet.signedness);
  return compareAtomEndpoint(schema, subject, false, container, false,
                             subjectSet.bitWidth, subjectSet.signedness) >= 0 &&
         compareAtomEndpoint(schema, subject, true, container, true,
                             subjectSet.bitWidth, subjectSet.signedness) <= 0;
}

bool makeIntegralCoverageShape(
    const obelisk::coverage::Database &schema,
    const obelisk::coverage::ResolvedFunctionalValueAtom &atom,
    uint32_t bitWidth, obelisk::coverage::CoverageSignedness signedness,
    IntegralCoverageShape &shape) {
  using AtomKind = obelisk::coverage::FunctionalValueAtomKind;
  if (atom.kind == AtomKind::RealInterval)
    return false;
  uint32_t count = functionalLimbCount(bitWidth);
  if (atom.limbCount != count)
    return false;
  shape = {};
  shape.interval = atom.kind == AtomKind::IntegralRange;
  if (shape.interval) {
    shape.low.resize(count);
    shape.high.resize(count);
  } else {
    shape.value.resize(count);
    shape.wildcard.resize(count);
  }
  for (uint32_t ordinal = 0; ordinal != count; ++ordinal) {
    const auto &limb =
        schema.resolvedFunctionalValueLimbs[atom.firstLimb + ordinal];
    uint64_t valid = ordinal + 1 == count && bitWidth % 64
                         ? lowBitsMask(bitWidth % 64)
                         : UINT64_MAX;
    if (shape.interval) {
      if ((limb.lowBval | limb.highBval | limb.wildcardMask) & valid)
        return false;
      shape.low[ordinal] = limb.lowAval & valid;
      shape.high[ordinal] = limb.highAval & valid;
    } else {
      if (limb.lowBval & ~limb.wildcardMask & valid)
        return false;
      shape.wildcard[ordinal] = limb.wildcardMask & valid;
      shape.value[ordinal] = limb.lowAval & ~limb.wildcardMask & valid;
    }
  }
  // Flipping the sign bit maps two's-complement signed order to ordinary
  // unsigned lexicographic order. This lets one prefix algorithm cover both
  // signed and unsigned values without target-sized arithmetic.
  if (signedness == obelisk::coverage::CoverageSignedness::Signed) {
    uint32_t signBit = bitWidth - 1;
    uint64_t signMask = uint64_t{1} << (signBit % 64);
    uint32_t signLimb = signBit / 64;
    if (shape.interval) {
      shape.low[signLimb] ^= signMask;
      shape.high[signLimb] ^= signMask;
    } else {
      shape.value[signLimb] ^= signMask;
      shape.value[signLimb] &= ~shape.wildcard[signLimb];
    }
  }
  return true;
}

/// Apply IEEE 1800-2023 19.6.1.1 value-set selection to one resolved
/// coverpoint bin. State bins subtract the union of ignore/illegal sets.
/// Transition bins use the
/// last step of every surviving transition alternative; the cross selects the
/// bin when any such terminal value set intersects the requested set.
obelisk_rt_status resolvedFunctionalBinIntersectsValueSet(
    const obelisk::coverage::Database &schema, uint64_t type,
    const obelisk::coverage::Digest &configuration, uint64_t binID,
    const obelisk::coverage::ResolvedFunctionalValueSet &selection,
    const std::unordered_set<uint64_t> &suppressedTransitionAlternatives,
    FunctionalCoverageSetQueryIndex &setQueries, bool &result) {
  result = false;
  if (&schema != &setQueries.schema || type != setQueries.type ||
      configuration != setQueries.configuration || selection.type != type ||
      selection.configuration != configuration)
    return OBELISK_RT_INVALID_DESIGN;
  result = setQueries.intersects(binID, selection.id,
                                 suppressedTransitionAlternatives);
  return setQueries.status;
}

bool crossSelectorUsesWith(const obelisk::coverage::Database &schema,
                           const obelisk::coverage::CrossSelectorNode &root,
                           FunctionalCoverageSetQueryIndex &setQueries) {
  std::vector<const obelisk::coverage::CrossSelectorNode *> worklist{&root};
  IndexSet<uint64_t> visited;
  while (!worklist.empty()) {
    const auto *node = worklist.back();
    worklist.pop_back();
    if (!visited.insert(node->id).second)
      continue;
    if (node->withExpression || node->predicateRoot)
      return true;
    for (uint32_t ordinal = 0; ordinal != node->operandCount; ++ordinal) {
      if (uint64_t{node->firstOperand} + ordinal >=
          schema.crossSelectorOperands.size())
        return false;
      uint64_t childID =
          schema.crossSelectorOperands[node->firstOperand + ordinal].operand;
      const auto *child = setQueries.lookup(setQueries.selectors,
                                            schema.crossSelectorNodes, childID);
      if (!child || child->cross != root.cross)
        return false;
      worklist.push_back(child);
    }
  }
  return false;
}

bool crossSelectorUsesSet(const obelisk::coverage::Database &schema,
                          const obelisk::coverage::CrossSelectorNode &root,
                          FunctionalCoverageSetQueryIndex &setQueries) {
  std::vector<const obelisk::coverage::CrossSelectorNode *> worklist{&root};
  IndexSet<uint64_t> visited;
  while (!worklist.empty()) {
    const auto *node = worklist.back();
    worklist.pop_back();
    if (!visited.insert(node->id).second)
      continue;
    if (node->kind == obelisk::coverage::CrossSelectorKind::Set)
      return true;
    for (uint32_t ordinal = 0; ordinal != node->operandCount; ++ordinal) {
      if (uint64_t{node->firstOperand} + ordinal >=
          schema.crossSelectorOperands.size())
        return false;
      uint64_t childID =
          schema.crossSelectorOperands[node->firstOperand + ordinal].operand;
      const auto *child = setQueries.lookup(setQueries.selectors,
                                            schema.crossSelectorNodes, childID);
      if (!child || child->cross != root.cross)
        return false;
      worklist.push_back(child);
    }
  }
  return false;
}

obelisk_rt_status resolvedFunctionalBinCardinalityCapped(
    const obelisk::coverage::Database &schema, uint64_t type,
    const obelisk::coverage::Digest &configuration, uint64_t binID,
    uint64_t cap,
    const std::unordered_set<uint64_t> &suppressedTransitionAlternatives,
    FunctionalCoverageSetQueryIndex &setQueries, uint64_t &result) {
  result = 0;
  if (&schema != &setQueries.schema || type != setQueries.type ||
      configuration != setQueries.configuration)
    return OBELISK_RT_INVALID_DESIGN;
  result = setQueries.cardinality(binID, cap, suppressedTransitionAlternatives);
  return setQueries.status;
}

obelisk_rt_status resolvedCrossSelectorSelectsBinTuple(
    const obelisk::coverage::Database &schema, uint64_t type,
    const obelisk::coverage::Digest &configuration, uint64_t resolvedCross,
    const obelisk::coverage::CrossSelectorNode &root,
    const std::vector<uint64_t> &binTuple,
    const std::unordered_set<uint64_t> &suppressedTransitionAlternatives,
    FunctionalCoverageSetQueryIndex &setQueries, bool &result) {
  using namespace obelisk::coverage;
  result = false;
  const auto *cross = setQueries.lookup(
      setQueries.items, schema.resolvedFunctionalItems, resolvedCross);
  if (!cross || cross->kind != FunctionalItemKind::Cross ||
      root.cross != cross->templateItem)
    return OBELISK_RT_INVALID_DESIGN;
  const auto *plan = setQueries.lookup(setQueries.crossPlans, schema.crossPlans,
                                       cross->templateItem);
  if (!plan || binTuple.size() != plan->targetCount ||
      uint64_t{plan->firstTarget} + plan->targetCount >
          schema.crossTargets.size())
    return OBELISK_RT_INVALID_DESIGN;

  std::vector<uint64_t> resolvedTargets;
  resolvedTargets.reserve(plan->targetCount);
  for (uint32_t ordinal = 0; ordinal != plan->targetCount; ++ordinal) {
    const CrossTarget &target =
        schema.crossTargets[plan->firstTarget + ordinal];
    const auto *resolvedTarget =
        setQueries.lookup(setQueries.templateItems,
                          schema.resolvedFunctionalItems, target.target);
    const auto *bin = setQueries.bin(binTuple[ordinal]);
    if (target.cross != root.cross || target.ordinal != ordinal ||
        !resolvedTarget ||
        resolvedTarget->kind != FunctionalItemKind::Coverpoint || !bin ||
        bin->item != resolvedTarget->id)
      return OBELISK_RT_INVALID_DESIGN;
    resolvedTargets.push_back(resolvedTarget->id);
  }

  auto bindingFor = [&](uint64_t node) {
    return setQueries.binding(resolvedCross, node);
  };
  IndexMap<uint64_t, bool> memo;
  IndexMap<uint64_t, bool> evaluating;
  std::function<obelisk_rt_status(const CrossSelectorNode &, bool &)> evaluate =
      [&](const CrossSelectorNode &node, bool &selected) -> obelisk_rt_status {
    auto memoized = memo.find(node.id);
    if (memoized != memo.end()) {
      selected = memoized->second;
      return OBELISK_RT_OK;
    }
    bool &active = evaluating[node.id];
    if (active)
      return OBELISK_RT_INVALID_DESIGN;
    active = true;
    auto finish = [&](bool value) {
      active = false;
      memo.emplace(node.id, value);
      selected = value;
      return OBELISK_RT_OK;
    };
    bool base = false;
    if (node.kind == CrossSelectorKind::Binsof) {
      auto target = setQueries.targetOrdinals.find({root.cross, node.target});
      if (target == setQueries.targetOrdinals.end() ||
          target->second >= binTuple.size())
        return OBELISK_RT_INVALID_DESIGN;
      const auto *bin = setQueries.bin(binTuple[target->second]);
      if (!bin)
        return OBELISK_RT_INVALID_DESIGN;
      base = !node.bin || bin->templateBin == node.bin;
      if (base && node.valueSet) {
        auto binding = bindingFor(node.id);
        if (!binding || !binding->valueSet)
          return OBELISK_RT_INVALID_DESIGN;
        const auto *set = setQueries.valueSet(binding->valueSet);
        if (!set)
          return OBELISK_RT_INVALID_DESIGN;
        obelisk_rt_status status = resolvedFunctionalBinIntersectsValueSet(
            schema, type, configuration, bin->id, *set,
            suppressedTransitionAlternatives, setQueries, base);
        if (status != OBELISK_RT_OK)
          return status;
      }
    } else if (node.kind == CrossSelectorKind::Set ||
               node.kind == CrossSelectorKind::AllTuples) {
      base = true;
    } else if (node.kind == CrossSelectorKind::Not ||
               node.kind == CrossSelectorKind::With ||
               node.kind == CrossSelectorKind::And ||
               node.kind == CrossSelectorKind::Or) {
      const uint32_t expected = (node.kind == CrossSelectorKind::Not ||
                                 node.kind == CrossSelectorKind::With)
                                    ? 1
                                    : 2;
      if (node.operandCount != expected ||
          uint64_t{node.firstOperand} + expected >
              schema.crossSelectorOperands.size())
        return OBELISK_RT_INVALID_DESIGN;
      std::array<bool, 2> operands{};
      for (uint32_t ordinal = 0; ordinal != expected; ++ordinal) {
        const auto &operand =
            schema.crossSelectorOperands[node.firstOperand + ordinal];
        const auto *child = setQueries.lookup(
            setQueries.selectors, schema.crossSelectorNodes, operand.operand);
        if (operand.node != node.id || operand.ordinal != ordinal ||
            (!child || child->cross != root.cross))
          return OBELISK_RT_INVALID_DESIGN;
        obelisk_rt_status status = evaluate(*child, operands[ordinal]);
        if (status != OBELISK_RT_OK)
          return status;
      }
      base = node.kind == CrossSelectorKind::Not    ? !operands[0]
             : node.kind == CrossSelectorKind::With ? operands[0]
             : node.kind == CrossSelectorKind::And  ? operands[0] && operands[1]
                                                   : operands[0] || operands[1];
    } else {
      return OBELISK_RT_INVALID_DESIGN;
    }
    if (!base || (!node.withExpression && !node.predicateRoot &&
                  !node.constructionExpression))
      return finish(base);

    auto binding = bindingFor(node.id);
    if (!binding || binding->withExpression != node.withExpression)
      return OBELISK_RT_INVALID_DESIGN;
    if ((binding->flags & ResolvedCrossSelectorSymbolicWith) ||
        ((binding->flags & ResolvedCrossSelectorScalarWith) &&
         (binding->flags & ResolvedCrossSelectorWideMatches))) {
      PreparedFunctionalConstruction index;
      initializeSelectionIndex(index, schema, setQueries, type, configuration,
                               suppressedTransitionAlternatives);
      auto kernel = selectionKernel(index, *plan);
      if (!kernel->check())
        return kernel->status;
      auto selected = kernel->select(node, resolvedCross, 0);
      auto root = kernel->sampleRoot(selected);
      if (!kernel->check())
        return kernel->status;
      return finish(kernel->sampleSelection->matches(root, binTuple));
    }
    if (binding->flags & ResolvedCrossSelectorScalarWith) {
      if (!(binding->flags & ResolvedCrossSelectorScalarTrue))
        return finish(false);
      if (binding->matchesPolicy == CrossMatchesPolicy::All)
        return finish(true);
      if (binding->matchesPolicy != CrossMatchesPolicy::Count ||
          !binding->matchesCount)
        return OBELISK_RT_INVALID_DESIGN;
      uint64_t volume = 1, threshold = binding->matchesCount;
      for (uint64_t bin : binTuple) {
        uint64_t count = setQueries.cardinality(
            bin, threshold, suppressedTransitionAlternatives);
        if (setQueries.status != OBELISK_RT_OK)
          return setQueries.status;
        if (!count)
          return finish(false);
        volume = volume >= threshold || count > (threshold - 1) / volume
                     ? threshold
                     : volume * count;
      }
      return finish(volume >= threshold);
    }
    if (!binding->tupleSet)
      return OBELISK_RT_INVALID_DESIGN;
    const auto *tupleSet = setQueries.lookup(setQueries.tupleSets,
                                             schema.resolvedFunctionalTupleSets,
                                             binding->tupleSet);
    if (!tupleSet || tupleSet->selector != node.id ||
        tupleSet->elementMode != FunctionalTupleElementMode::ValueTuple ||
        uint64_t{tupleSet->firstTuple} + tupleSet->tupleCount >
            schema.resolvedFunctionalTupleSetTuples.size())
      return OBELISK_RT_INVALID_DESIGN;
    uint64_t satisfying = 0;
    for (uint32_t tupleOrdinal = 0; tupleOrdinal != tupleSet->tupleCount;
         ++tupleOrdinal) {
      const auto &tuple =
          schema.resolvedFunctionalTupleSetTuples[tupleSet->firstTuple +
                                                  tupleOrdinal];
      if (tuple.componentCount != plan->targetCount ||
          uint64_t{tuple.firstComponent} + tuple.componentCount >
              schema.resolvedFunctionalTupleSetComponents.size())
        return OBELISK_RT_INVALID_DESIGN;
      bool contained = true;
      for (uint32_t componentOrdinal = 0;
           componentOrdinal != tuple.componentCount; ++componentOrdinal) {
        const auto &component =
            schema.resolvedFunctionalTupleSetComponents[tuple.firstComponent +
                                                        componentOrdinal];
        const auto *set = setQueries.valueSet(component.valueSet);
        if (component.ordinal != componentOrdinal ||
            component.target != resolvedTargets[componentOrdinal] || !set)
          return OBELISK_RT_INVALID_DESIGN;
        bool intersects = false;
        obelisk_rt_status status = resolvedFunctionalBinIntersectsValueSet(
            schema, type, configuration, binTuple[componentOrdinal], *set,
            suppressedTransitionAlternatives, setQueries, intersects);
        if (status != OBELISK_RT_OK)
          return status;
        if (!intersects) {
          contained = false;
          break;
        }
      }
      if (contained)
        ++satisfying;
    }
    if (binding->matchesPolicy == CrossMatchesPolicy::Count)
      return finish(!(binding->flags & ResolvedCrossSelectorWideMatches) &&
                    satisfying >= binding->matchesCount);
    if (binding->matchesPolicy != CrossMatchesPolicy::All ||
        binding->matchesCount)
      return OBELISK_RT_INVALID_DESIGN;

    const uint64_t cap = uint64_t{tupleSet->tupleCount} + 1;
    uint64_t total = 1;
    for (uint32_t ordinal = 0; ordinal != plan->targetCount; ++ordinal) {
      uint64_t containedValues = 0;
      obelisk_rt_status status = resolvedFunctionalBinCardinalityCapped(
          schema, type, configuration, binTuple[ordinal], cap,
          suppressedTransitionAlternatives, setQueries, containedValues);
      if (status != OBELISK_RT_OK)
        return status;
      if (!containedValues)
        return finish(false);
      total = containedValues >= cap || total > (cap - 1) / containedValues
                  ? cap
                  : total * containedValues;
    }
    return finish(satisfying == total);
  };
  return evaluate(root, result);
}

obelisk_rt_status resolvedCrossSelectorSetCandidates(
    const obelisk::coverage::Database &schema, uint64_t type,
    const obelisk::coverage::Digest &configuration, uint64_t resolvedCross,
    const obelisk::coverage::CrossSelectorNode &root,
    const std::vector<std::vector<uint64_t>> &targetBins,
    const std::unordered_set<uint64_t> &suppressedTransitionAlternatives,
    FunctionalCoverageSetQueryIndex &setQueries,
    std::vector<std::vector<uint64_t>> &result) {
  using namespace obelisk::coverage;
  result.clear();
  const uint64_t limit = ParseLimits{}.maxRecords;
  IndexSet<std::vector<uint64_t>> unique;
  std::vector<const CrossSelectorNode *> worklist{&root};
  IndexSet<uint64_t> visited;
  while (!worklist.empty()) {
    const CrossSelectorNode *node = worklist.back();
    worklist.pop_back();
    if (!visited.insert(node->id).second)
      continue;
    if (node->kind != CrossSelectorKind::Set) {
      for (uint32_t ordinal = 0; ordinal != node->operandCount; ++ordinal) {
        if (uint64_t{node->firstOperand} + ordinal >=
            schema.crossSelectorOperands.size())
          return OBELISK_RT_INVALID_DESIGN;
        uint64_t childID =
            schema.crossSelectorOperands[node->firstOperand + ordinal].operand;
        const auto *child = setQueries.lookup(
            setQueries.selectors, schema.crossSelectorNodes, childID);
        if (!child || child->cross != root.cross)
          return OBELISK_RT_INVALID_DESIGN;
        worklist.push_back(child);
      }
      continue;
    }
    const auto *binding = setQueries.binding(resolvedCross, node->id);
    const auto *tupleSet =
        binding ? setQueries.lookup(setQueries.tupleSets,
                                    schema.resolvedFunctionalTupleSets,
                                    binding->tupleSet)
                : nullptr;
    if (!tupleSet || tupleSet->selector != node->id || targetBins.empty() ||
        uint64_t{tupleSet->firstTuple} + tupleSet->tupleCount >
            schema.resolvedFunctionalTupleSetTuples.size())
      return OBELISK_RT_INVALID_DESIGN;
    for (uint32_t tupleOrdinal = 0; tupleOrdinal != tupleSet->tupleCount;
         ++tupleOrdinal) {
      const auto &valueTuple =
          schema.resolvedFunctionalTupleSetTuples[tupleSet->firstTuple +
                                                  tupleOrdinal];
      if (valueTuple.componentCount != targetBins.size() ||
          uint64_t{valueTuple.firstComponent} + valueTuple.componentCount >
              schema.resolvedFunctionalTupleSetComponents.size())
        return OBELISK_RT_INVALID_DESIGN;
      std::vector<std::vector<uint64_t>> memberships(targetBins.size());
      uint64_t product = 1;
      for (uint32_t targetOrdinal = 0;
           targetOrdinal != valueTuple.componentCount; ++targetOrdinal) {
        const auto &component = schema.resolvedFunctionalTupleSetComponents
                                    [valueTuple.firstComponent + targetOrdinal];
        const auto *selection = setQueries.valueSet(component.valueSet);
        if (!selection || selection->type != type ||
            component.ordinal != targetOrdinal)
          return OBELISK_RT_INVALID_DESIGN;
        for (uint64_t bin : targetBins[targetOrdinal]) {
          bool intersects = false;
          obelisk_rt_status status = resolvedFunctionalBinIntersectsValueSet(
              schema, type, configuration, bin, *selection,
              suppressedTransitionAlternatives, setQueries, intersects);
          if (status != OBELISK_RT_OK)
            return status;
          if (intersects)
            memberships[targetOrdinal].push_back(bin);
        }
        if (memberships[targetOrdinal].empty()) {
          product = 0;
          break;
        }
        if (product > limit / memberships[targetOrdinal].size())
          return OBELISK_RT_OUT_OF_RESOURCES;
        product *= memberships[targetOrdinal].size();
      }
      std::vector<uint64_t> candidate(targetBins.size());
      for (uint64_t encoded = 0; encoded != product; ++encoded) {
        uint64_t remainder = encoded;
        for (size_t reverse = memberships.size(); reverse != 0; --reverse) {
          const size_t ordinal = reverse - 1;
          candidate[ordinal] =
              memberships[ordinal][remainder % memberships[ordinal].size()];
          remainder /= memberships[ordinal].size();
        }
        bool selected = false;
        obelisk_rt_status status = resolvedCrossSelectorSelectsBinTuple(
            schema, type, configuration, resolvedCross, root, candidate,
            suppressedTransitionAlternatives, setQueries, selected);
        if (status != OBELISK_RT_OK)
          return status;
        if (selected && unique.insert(candidate).second) {
          if (unique.size() > limit)
            return OBELISK_RT_OUT_OF_RESOURCES;
          result.push_back(candidate);
        }
      }
    }
  }
  detail::sortByKey(result.begin(), result.end(),
                    [](const auto &tuple) -> const auto & { return tuple; });
  return OBELISK_RT_OK;
}

obelisk_rt_status
emitFunctionalOverlapWarnings(const obelisk::coverage::Database &schema,
                              uint64_t type,
                              const obelisk::coverage::Digest &configuration) {
  using namespace obelisk::coverage;
  auto optionValue = [&](uint64_t owner,
                         FunctionalConfigurationOptionOwnerKind ownerKind,
                         bool fallback) {
    for (const auto &option : schema.functionalConfigurationOptions)
      if (option.type == type && option.configuration == configuration &&
          option.owner == owner && option.ownerKind == ownerKind &&
          option.scope == FunctionalOptionScopeKind::Instance &&
          option.option == FunctionalConfigurationOptionKind::DetectOverlap)
        return option.value != 0;
    return fallback;
  };
  const bool groupDefault =
      optionValue(type, FunctionalConfigurationOptionOwnerKind::Group, false);

  std::map<uint64_t, const ResolvedFunctionalBinPlan *> plans;
  for (const auto &plan : schema.resolvedFunctionalBinPlans)
    if (plan.type == type && plan.configuration == configuration)
      plans.emplace(plan.bin, &plan);
  std::map<uint64_t, const ResolvedFunctionalValueSet *> valueSets;
  for (const auto &valueSet : schema.resolvedFunctionalValueSets)
    if (valueSet.type == type && valueSet.configuration == configuration)
      valueSets.emplace(valueSet.id, &valueSet);

  struct ShapeEntry {
    IntegralCoverageShape shape;
    const ResolvedFunctionalBin *bin = nullptr;
  };
  struct RealShapeEntry {
    const ResolvedFunctionalValueAtom *atom = nullptr;
    const ResolvedFunctionalBin *bin = nullptr;
    uint64_t valueSet = 0;
    uint32_t atomOrdinal = 0;
  };
  enum class SearchResult { None, Found, Limit };
  auto findOverlap = [&](const std::vector<ShapeEntry> &shapes,
                         uint32_t bitWidth, const ResolvedFunctionalBin *&left,
                         const ResolvedFunctionalBin *&right) {
    struct Frame {
      std::vector<uint64_t> prefix;
      int64_t remainingBit = -1;
      std::vector<size_t> candidates;
    };
    Frame root;
    root.prefix.resize((uint64_t{bitWidth} + 63) / 64);
    root.remainingBit = static_cast<int64_t>(bitWidth) - 1;
    root.candidates.resize(shapes.size());
    std::iota(root.candidates.begin(), root.candidates.end(), size_t{0});
    std::vector<Frame> worklist;
    worklist.push_back(std::move(root));
    uint64_t visits = 0;
    while (!worklist.empty()) {
      Frame frame = std::move(worklist.back());
      worklist.pop_back();
      std::vector<size_t> intersecting;
      intersecting.reserve(frame.candidates.size());
      const ShapeEntry *containing = nullptr;
      const ResolvedFunctionalBin *firstBin = nullptr;
      bool distinctBins = false;
      for (size_t index : frame.candidates) {
        if (visits == ParseLimits{}.maxRecords)
          return SearchResult::Limit;
        ++visits;
        const ShapeEntry &candidate = shapes[index];
        if (!coverageShapeIntersectsPrefix(candidate.shape, frame.prefix,
                                           frame.remainingBit, bitWidth))
          continue;
        intersecting.push_back(index);
        if (!firstBin)
          firstBin = candidate.bin;
        else if (firstBin->id != candidate.bin->id)
          distinctBins = true;
        if (!containing &&
            coverageShapeContainsPrefix(candidate.shape, frame.prefix,
                                        frame.remainingBit, bitWidth))
          containing = &candidate;
      }
      if (!distinctBins)
        continue;
      if (containing) {
        auto other = std::find_if(
            intersecting.begin(), intersecting.end(), [&](size_t index) {
              return shapes[index].bin->id != containing->bin->id;
            });
        if (other == intersecting.end())
          return SearchResult::Limit;
        left = containing->bin;
        right = shapes[*other].bin;
        return SearchResult::Found;
      }
      if (frame.remainingBit < 0)
        return SearchResult::Limit;
      Frame one{frame.prefix, frame.remainingBit - 1, intersecting};
      const uint32_t decisionBit = static_cast<uint32_t>(frame.remainingBit);
      one.prefix[decisionBit / 64] |= uint64_t{1} << (decisionBit % 64);
      worklist.push_back(std::move(one));
      worklist.push_back({std::move(frame.prefix), frame.remainingBit - 1,
                          std::move(intersecting)});
      if (worklist.size() > ParseLimits{}.maxRecords)
        return SearchResult::Limit;
    }
    return SearchResult::None;
  };
  auto findRealOverlap = [&](std::vector<RealShapeEntry> shapes,
                             const ResolvedFunctionalBin *&left,
                             const ResolvedFunctionalBin *&right) {
    if (shapes.size() > ParseLimits{}.maxRecords)
      return SearchResult::Limit;
    auto realEndpoint = [](uint64_t bits) {
      double value = 0.0;
      std::memcpy(&value, &bits, sizeof(value));
      return value;
    };
    std::sort(shapes.begin(), shapes.end(),
              [&](const RealShapeEntry &lhs, const RealShapeEntry &rhs) {
                const bool lhsUnbounded =
                    lhs.atom->flags & FunctionalValueAtomLowerUnbounded;
                const bool rhsUnbounded =
                    rhs.atom->flags & FunctionalValueAtomLowerUnbounded;
                if (lhsUnbounded != rhsUnbounded)
                  return lhsUnbounded;
                if (!lhsUnbounded) {
                  const double lhsLow = realEndpoint(lhs.atom->realLowBits);
                  const double rhsLow = realEndpoint(rhs.atom->realLowBits);
                  if (lhsLow != rhsLow)
                    return lhsLow < rhsLow;
                }
                return std::tie(lhs.bin->id, lhs.valueSet, lhs.atomOrdinal) <
                       std::tie(rhs.bin->id, rhs.valueSet, rhs.atomOrdinal);
              });
    auto upperLess = [&](size_t lhsIndex, size_t rhsIndex) {
      const RealShapeEntry &lhs = shapes[lhsIndex];
      const RealShapeEntry &rhs = shapes[rhsIndex];
      const bool lhsUnbounded =
          lhs.atom->flags & FunctionalValueAtomUpperUnbounded;
      const bool rhsUnbounded =
          rhs.atom->flags & FunctionalValueAtomUpperUnbounded;
      if (lhsUnbounded != rhsUnbounded)
        return !lhsUnbounded;
      if (!lhsUnbounded) {
        const double lhsHigh = realEndpoint(lhs.atom->realHighBits);
        const double rhsHigh = realEndpoint(rhs.atom->realHighBits);
        if (lhsHigh != rhsHigh)
          return lhsHigh < rhsHigh;
      }
      return std::tie(lhs.bin->id, lhs.valueSet, lhs.atomOrdinal) <
             std::tie(rhs.bin->id, rhs.valueSet, rhs.atomOrdinal);
    };
    std::set<size_t, decltype(upperLess)> active(upperLess);
    std::map<uint64_t, std::pair<const ResolvedFunctionalBin *, uint32_t>>
        activeBins;
    for (size_t index = 0; index != shapes.size(); ++index) {
      const RealShapeEntry &current = shapes[index];
      const bool currentLowerUnbounded =
          current.atom->flags & FunctionalValueAtomLowerUnbounded;
      while (!active.empty() && !currentLowerUnbounded) {
        const size_t candidateIndex = *active.begin();
        const RealShapeEntry &candidate = shapes[candidateIndex];
        if (candidate.atom->flags & FunctionalValueAtomUpperUnbounded)
          break;
        const double candidateHigh = realEndpoint(candidate.atom->realHighBits);
        const double currentLow = realEndpoint(current.atom->realLowBits);
        const bool disjoint =
            candidateHigh < currentLow ||
            (candidateHigh == currentLow &&
             (!(candidate.atom->flags & FunctionalValueAtomUpperInclusive) ||
              !(current.atom->flags & FunctionalValueAtomLowerInclusive)));
        if (!disjoint)
          break;
        active.erase(active.begin());
        auto bin = activeBins.find(candidate.bin->id);
        if (--bin->second.second == 0)
          activeBins.erase(bin);
      }
      auto other = activeBins.begin();
      if (other != activeBins.end() && other->first == current.bin->id)
        ++other;
      if (other != activeBins.end()) {
        left = other->second.first;
        right = current.bin;
        return SearchResult::Found;
      }
      active.insert(index);
      auto bin = activeBins
                     .try_emplace(current.bin->id,
                                  std::make_pair(current.bin, uint32_t{0}))
                     .first;
      ++bin->second.second;
    }
    return SearchResult::None;
  };

  for (const auto &item : schema.resolvedFunctionalItems) {
    if (item.type != type || item.configuration != configuration ||
        item.kind != FunctionalItemKind::Coverpoint ||
        !optionValue(item.id, FunctionalConfigurationOptionOwnerKind::Item,
                     groupDefault))
      continue;
    std::vector<ShapeEntry> shapes;
    std::vector<RealShapeEntry> realShapes;
    uint32_t bitWidth = 0;
    for (const auto &bin : schema.resolvedFunctionalBins) {
      constexpr uint32_t omitted = FunctionalBinDefault |
                                   FunctionalBinDefaultSequence |
                                   FunctionalBinAutomatic | FunctionalBinEmpty;
      if (bin.type != type || bin.configuration != configuration ||
          bin.item != item.id || bin.kind != FunctionalBinKind::State ||
          (bin.flags & omitted))
        continue;
      auto plan = plans.find(bin.id);
      auto valueSet = plan == plans.end()
                          ? valueSets.end()
                          : valueSets.find(plan->second->valueSet);
      if (valueSet == valueSets.end())
        continue;
      if (valueSet->second->kind == FunctionalValueSetKind::Integral)
        bitWidth = valueSet->second->bitWidth;
      for (uint32_t atomIndex = 0; atomIndex != valueSet->second->atomCount;
           ++atomIndex) {
        const auto &atom =
            schema.resolvedFunctionalValueAtoms[valueSet->second->firstAtom +
                                                atomIndex];
        if (valueSet->second->kind == FunctionalValueSetKind::Real) {
          realShapes.push_back({&atom, &bin, valueSet->second->id, atomIndex});
          continue;
        }
        IntegralCoverageShape shape;
        if (makeIntegralCoverageShape(schema, atom, valueSet->second->bitWidth,
                                      valueSet->second->signedness, shape))
          shapes.push_back({std::move(shape), &bin});
      }
    }
    const ResolvedFunctionalBin *left = nullptr;
    const ResolvedFunctionalBin *right = nullptr;
    SearchResult result = shapes.empty()
                              ? SearchResult::None
                              : findOverlap(shapes, bitWidth, left, right);
    if (result == SearchResult::None && !realShapes.empty())
      result = findRealOverlap(std::move(realShapes), left, right);
    if (result == SearchResult::Limit) {
      std::fprintf(stderr,
                   "error: detect_overlap traversal limit exceeded for "
                   "functional coverpoint '%s'\n",
                   item.hierarchy.c_str());
      return OBELISK_RT_OUT_OF_RESOURCES;
    }
    if (result == SearchResult::Found)
      std::fprintf(stderr,
                   "warning: functional coverpoint '%s' bins '%s' and '%s' "
                   "overlap\n",
                   item.hierarchy.c_str(), left->name.c_str(),
                   right->name.c_str());
  }
  return OBELISK_RT_OK;
}

int compareIntegralBitVectors(const std::vector<uint64_t> &left,
                              const std::vector<uint64_t> &right) {
  for (size_t reverse = left.size(); reverse != 0; --reverse) {
    size_t ordinal = reverse - 1;
    if (left[ordinal] != right[ordinal])
      return left[ordinal] < right[ordinal] ? -1 : 1;
  }
  return 0;
}

std::vector<uint64_t> coveragePrefixHigh(const std::vector<uint64_t> &prefix,
                                         int64_t remainingBit,
                                         uint32_t bitWidth) {
  std::vector<uint64_t> high = prefix;
  if (remainingBit >= 0) {
    uint32_t limb = static_cast<uint32_t>(remainingBit) / 64;
    for (uint32_t ordinal = 0; ordinal < limb; ++ordinal)
      high[ordinal] = UINT64_MAX;
    high[limb] |= lowBitsMask(static_cast<uint32_t>(remainingBit) % 64 + 1);
  }
  if (bitWidth % 64)
    high.back() &= lowBitsMask(bitWidth % 64);
  return high;
}

bool coverageShapeIntersectsPrefix(const IntegralCoverageShape &shape,
                                   const std::vector<uint64_t> &prefix,
                                   int64_t remainingBit, uint32_t bitWidth) {
  if (shape.interval) {
    std::vector<uint64_t> high =
        coveragePrefixHigh(prefix, remainingBit, bitWidth);
    return compareIntegralBitVectors(shape.high, prefix) >= 0 &&
           compareIntegralBitVectors(shape.low, high) <= 0;
  }
  uint32_t freeLimb =
      remainingBit < 0 ? 0 : static_cast<uint32_t>(remainingBit) / 64;
  for (uint32_t ordinal = 0; ordinal != shape.value.size(); ++ordinal) {
    uint64_t assigned = UINT64_MAX;
    if (remainingBit >= 0) {
      if (ordinal < freeLimb)
        assigned = 0;
      else if (ordinal == freeLimb)
        assigned = ~lowBitsMask(static_cast<uint32_t>(remainingBit) % 64 + 1);
    }
    if ((shape.value[ordinal] ^ prefix[ordinal]) & ~shape.wildcard[ordinal] &
        assigned)
      return false;
  }
  return true;
}

bool coverageShapeContainsPrefix(const IntegralCoverageShape &shape,
                                 const std::vector<uint64_t> &prefix,
                                 int64_t remainingBit, uint32_t bitWidth) {
  if (!coverageShapeIntersectsPrefix(shape, prefix, remainingBit, bitWidth))
    return false;
  if (shape.interval) {
    std::vector<uint64_t> high =
        coveragePrefixHigh(prefix, remainingBit, bitWidth);
    return compareIntegralBitVectors(shape.low, prefix) <= 0 &&
           compareIntegralBitVectors(shape.high, high) >= 0;
  }
  if (remainingBit < 0)
    return true;
  uint32_t lastFreeLimb = static_cast<uint32_t>(remainingBit) / 64;
  for (uint32_t ordinal = 0; ordinal <= lastFreeLimb; ++ordinal) {
    uint64_t free =
        ordinal < lastFreeLimb
            ? UINT64_MAX
            : lowBitsMask(static_cast<uint32_t>(remainingBit) % 64 + 1);
    if (~shape.wildcard[ordinal] & free)
      return false;
  }
  return true;
}

/// IEEE 1800-2023 19.5.5-.6 removes complete ordinary sequences containing
/// a finite excluded subsequence. Ordinary goto/nonconsecutive gaps are a
/// regular language, not a finite list of words. Share exclusion prefix and
/// decision graphs per point and prove emptiness by product reachability.
/// Derived graphs are private and do not enter the database identity.
obelisk_rt_status
deriveTransitionExclusions(const obelisk::coverage::Database &schema,
                           uint64_t typeID,
                           const obelisk::coverage::Digest &configuration,
                           std::unordered_set<uint64_t> &suppressedAlternatives,
                           std::unordered_set<uint64_t> &emptyBins,
                           FunctionalCoverageSetQueryIndex &setQueries) {
  using namespace obelisk::coverage;
  using Graph = detail::DecisionIndex;
  using Language = detail::TransitionIndex;
  constexpr uint64_t maxDerivationWork = uint64_t{1} << 14;
  uint64_t derivationWork = 0;
  auto consumeWork = [&](uint64_t amount) {
    if (amount > maxDerivationWork - derivationWork)
      return false;
    derivationWork += amount;
    return true;
  };
  suppressedAlternatives.clear();
  emptyBins.clear();
  IndexSet<uint64_t> affectedItems, fourStateTemplates, fourStateItems;
  IndexMap<uint64_t, const ResolvedFunctionalBin *> bins;
  for (const auto &bin : schema.resolvedFunctionalBins) {
    if (bin.type != typeID || bin.configuration != configuration)
      continue;
    if (!bins.emplace(bin.id, &bin).second)
      return OBELISK_RT_INVALID_DESIGN;
    if (bin.kind == FunctionalBinKind::Transition &&
        (bin.flags & (FunctionalBinIgnore | FunctionalBinIllegal)))
      affectedItems.insert(bin.item);
  }
  if (affectedItems.empty())
    return OBELISK_RT_OK;
  for (const auto &expression : schema.functionalExpressions)
    if (expression.role == FunctionalExpressionRole::CoverpointSample &&
        (expression.flags & FunctionalExpressionCoverpointFourState))
      fourStateTemplates.insert(expression.owner);
  for (const auto &item : schema.resolvedFunctionalItems)
    if (item.type == typeID && item.configuration == configuration &&
        fourStateTemplates.count(item.templateItem))
      fourStateItems.insert(item.id);
  IndexMap<uint64_t, const ResolvedFunctionalValueSet *> valueSets;
  for (const auto &set : schema.resolvedFunctionalValueSets)
    if (set.type == typeID && set.configuration == configuration)
      valueSets.emplace(set.id, &set);
  IndexMap<uint64_t, const TransitionProgram *> programs;
  for (const auto &program : schema.transitionPrograms)
    programs.emplace(program.bin, &program);
  struct Ordinary {
    uint64_t alternative, bin;
    std::vector<Language::Step> steps;
  };
  struct Point {
    uint32_t width = 0;
    CoverageSignedness signedness = CoverageSignedness::NotApplicable;
    std::unique_ptr<Graph> values;
    std::unique_ptr<Language> language;
    IndexMap<uint64_t, Graph::Root> sets;
    IndexMap<uint64_t, Graph::Root> binTerminals;
    std::vector<Ordinary> ordinary;
  };
  IndexMap<uint64_t, Point> points;
  IndexMap<uint64_t, uint64_t> ordinaryCounts, suppressedCounts;
  auto labelFor = [&](Point &point, uint64_t item,
                      const ResolvedFunctionalValueSet &set,
                      Graph::Root &label) -> obelisk_rt_status {
    if (set.kind != FunctionalValueSetKind::Integral || !set.bitWidth ||
        uint64_t{set.firstAtom} + set.atomCount >
            schema.resolvedFunctionalValueAtoms.size())
      return OBELISK_RT_INVALID_DESIGN;
    if (!point.values) {
      if (set.bitWidth == UINT32_MAX)
        return OBELISK_RT_OUT_OF_RESOURCES;
      point.width = set.bitWidth;
      point.signedness = set.signedness;
      // Known transition sets reject every X/Z sample, so one extra decision
      // bit represents the entire unknown class. It is a legal gap only for
      // a four-state coverpoint. This preserves the original sample domain.
      uint32_t width = point.width + 1;
      point.values = std::make_unique<Graph>(width);
      std::vector<uint64_t> zero(functionalLimbCount(width), 0);
      std::vector<uint64_t> knownWildcard = zero;
      for (uint32_t ordinal = 0; ordinal != point.width / 64; ++ordinal)
        knownWildcard[ordinal] = UINT64_MAX;
      if (point.width % 64)
        knownWildcard[point.width / 64] = lowBitsMask(point.width % 64);
      Graph::Root domain = fourStateItems.count(item)
                               ? 1
                               : point.values->cube(zero, knownWildcard);
      point.language = std::make_unique<Language>(*point.values, width, domain);
    } else if (point.width != set.bitWidth ||
               point.signedness != set.signedness)
      return OBELISK_RT_INVALID_DESIGN;
    if (auto found = point.sets.find(set.id); found != point.sets.end()) {
      label = found->second;
      return OBELISK_RT_OK;
    }
    label = 0;
    const uint32_t limbs = functionalLimbCount(point.width + 1);
    for (uint32_t ordinal = 0; ordinal != set.atomCount; ++ordinal) {
      if (!consumeWork(uint64_t{set.bitWidth} + 2))
        return OBELISK_RT_OUT_OF_RESOURCES;
      const auto &atom =
          schema.resolvedFunctionalValueAtoms[set.firstAtom + ordinal];
      if (atom.valueSet != set.id || atom.ordinal != ordinal)
        return OBELISK_RT_INVALID_DESIGN;
      StateCoverageInterval interval;
      Graph::Root root;
      if (makeStateCoverageInterval(schema, set, atom, false, interval)) {
        if (interval.empty)
          continue;
        interval.low.insert(interval.low.begin(), limbs - interval.low.size(),
                            0);
        interval.high.insert(interval.high.begin(),
                             limbs - interval.high.size(), 0);
        root = point.values->interval(interval.low, interval.high);
      } else {
        IntegralCoverageShape shape;
        if (!makeIntegralCoverageShape(schema, atom, set.bitWidth,
                                       set.signedness, shape) ||
            shape.interval)
          return OBELISK_RT_INVALID_DESIGN;
        shape.value.resize(limbs, 0);
        shape.wildcard.resize(limbs, 0);
        root = point.values->cube(shape.value, shape.wildcard);
      }
      label = point.values->unite(label, root);
      if (point.values->exhausted())
        return OBELISK_RT_OUT_OF_RESOURCES;
    }
    point.sets.emplace(set.id, label);
    return OBELISK_RT_OK;
  };
  for (const auto &alternative : schema.resolvedTransitionAlternatives) {
    if (alternative.type != typeID ||
        alternative.configuration != configuration)
      continue;
    auto bin = bins.find(alternative.bin);
    if (bin == bins.end() || bin->second->kind != FunctionalBinKind::Transition)
      return OBELISK_RT_INVALID_DESIGN;
    if (!affectedItems.count(bin->second->item))
      continue;
    if (!alternative.stepCount ||
        uint64_t{alternative.firstStep} + alternative.stepCount >
            schema.resolvedTransitionSteps.size())
      return OBELISK_RT_INVALID_DESIGN;
    if (!consumeWork(alternative.stepCount))
      return OBELISK_RT_OUT_OF_RESOURCES;
    auto program = programs.find(bin->second->templateBin);
    if (program == programs.end() ||
        alternative.templateAlternativeOrdinal >=
            program->second->alternativeCount ||
        uint64_t{program->second->firstAlternative} +
                program->second->alternativeCount >
            schema.transitionAlternatives.size())
      return OBELISK_RT_INVALID_DESIGN;
    const auto &definition =
        schema.transitionAlternatives[program->second->firstAlternative +
                                      alternative.templateAlternativeOrdinal];
    if (definition.stepCount != alternative.stepCount ||
        uint64_t{definition.firstStep} + definition.stepCount >
            schema.transitionSteps.size())
      return OBELISK_RT_INVALID_DESIGN;
    bool exclusion =
        bin->second->flags & (FunctionalBinIgnore | FunctionalBinIllegal);
    auto &point = points.emplace(bin->second->item).first->second;
    std::vector<Language::Step> steps;
    steps.reserve(alternative.stepCount);
    uint64_t maximumLength = 0, wordCount = 1;
    for (uint32_t ordinal = 0; ordinal != alternative.stepCount; ++ordinal) {
      const auto &step =
          schema.resolvedTransitionSteps[alternative.firstStep + ordinal];
      const auto repetition =
          schema.transitionSteps[definition.firstStep + ordinal].repetition;
      auto set = valueSets.find(step.valueSet);
      bool bounded = repetition == TransitionRepetitionKind::Once ||
                     repetition == TransitionRepetitionKind::Consecutive;
      if (step.alternative != alternative.id || step.ordinal != ordinal ||
          (!bounded &&
           (exclusion ||
            (repetition != TransitionRepetitionKind::Goto &&
             repetition != TransitionRepetitionKind::Nonconsecutive))) ||
          !step.lowerBound || step.upperBound < step.lowerBound ||
          step.upperBound == TransitionUnbounded ||
          (repetition == TransitionRepetitionKind::Once &&
           (step.lowerBound != 1 || step.upperBound != 1)) ||
          set == valueSets.end())
        return OBELISK_RT_INVALID_DESIGN;
      if (step.upperBound > maxDerivationWork - maximumLength)
        return OBELISK_RT_OUT_OF_RESOURCES;
      maximumLength += step.upperBound;
      Graph::Root label;
      obelisk_rt_status status =
          labelFor(point, bin->second->item, *set->second, label);
      if (status != OBELISK_RT_OK)
        return status;
      steps.push_back({label, step.lowerBound, step.upperBound, repetition});
      if (exclusion) {
        uint64_t choices = step.upperBound - step.lowerBound + 1;
        if (wordCount > maxDerivationWork / choices)
          return OBELISK_RT_OUT_OF_RESOURCES;
        wordCount *= choices;
      }
    }
    if (!exclusion) {
      ++ordinaryCounts[bin->second->id];
      point.ordinary.push_back(
          {alternative.id, alternative.bin, std::move(steps)});
      continue;
    }
    std::vector<uint64_t> repetitions(steps.size());
    for (uint64_t encoded = 0; encoded != wordCount; ++encoded) {
      uint64_t remainder = encoded, length = 0;
      for (size_t reverse = steps.size(); reverse != 0; --reverse) {
        size_t ordinal = reverse - 1;
        const auto &step = steps[ordinal];
        uint64_t choices = step.upper - step.lower + 1;
        repetitions[ordinal] = step.lower + remainder % choices;
        remainder /= choices;
        length += repetitions[ordinal];
      }
      if (!consumeWork(length))
        return OBELISK_RT_OUT_OF_RESOURCES;
      std::vector<Graph::Root> word;
      word.reserve(length);
      for (size_t ordinal = 0; ordinal != steps.size(); ++ordinal)
        word.insert(word.end(), repetitions[ordinal], steps[ordinal].values);
      point.language->addExclusion(word);
      if (point.language->exhausted())
        return OBELISK_RT_OUT_OF_RESOURCES;
    }
  }
  for (auto &[item, point] : points) {
    bool projectTerminals = setQueries.needsTransitionValues(item);
    Graph *target = nullptr;
    std::unique_ptr<obelisk::coverage::detail::DecisionProjectionIndex> image;
    if (projectTerminals) {
      FunctionalCoverageSetQueryIndex::DomainKey domain = {
          static_cast<uint32_t>(FunctionalValueSetKind::Integral), point.width,
          static_cast<uint32_t>(point.signedness), false};
      target = &setQueries.graphFor(domain);
      std::vector<uint32_t> variables(uint64_t{point.width} + 1, UINT32_MAX);
      for (uint32_t variable = 1; variable <= point.width; ++variable)
        variables[variable] = variable - 1;
      image =
          std::make_unique<obelisk::coverage::detail::DecisionProjectionIndex>(
              *point.values, *target, std::move(variables));
    }
    for (const auto &ordinary : point.ordinary) {
      Graph::Root terminals =
          projectTerminals
              ? point.language->terminalValues(ordinary.steps)
              : (point.language->hasAcceptedWord(ordinary.steps) ? 1 : 0);
      if (point.language->exhausted())
        return OBELISK_RT_OUT_OF_RESOURCES;
      if (!terminals) {
        suppressedAlternatives.insert(ordinary.alternative);
        ++suppressedCounts[ordinary.bin];
      }
      if (!projectTerminals)
        continue;
      // Cross selectors use the final values of surviving sequences
      // (19.6.1.1), not the untrimmed authored terminal set. Drop the known
      // sample sentinel and import each shared graph node once per point.
      if (terminals) {
        if (point.values->variable(terminals) != 0 ||
            point.values->child(terminals, true) != 0)
          return OBELISK_RT_INVALID_DESIGN;
        terminals = point.values->child(terminals, false);
      }
      auto &combined = point.binTerminals[ordinary.bin];
      combined = target->unite(combined, image->project(terminals));
      if (image->exhausted())
        return OBELISK_RT_OUT_OF_RESOURCES;
    }
    for (auto [bin, root] : point.binTerminals)
      setQueries.setTransitionValues(bin, point.width, point.signedness, root);
  }
  for (auto [bin, count] : ordinaryCounts)
    if (suppressedCounts[bin] == count)
      emptyBins.insert(bin);
  return OBELISK_RT_OK;
}

obelisk_rt_status
markStaticallyEmptyStateBins(obelisk::coverage::Database &schema,
                             uint64_t typeID,
                             const obelisk::coverage::Digest &configuration,
                             FunctionalCoverageSetQueryIndex &setQueries) {
  using namespace obelisk::coverage;
  IndexMap<uint64_t, const ResolvedFunctionalBinPlan *> plans;
  for (const auto &plan : schema.resolvedFunctionalBinPlans)
    if (plan.type == typeID && plan.configuration == configuration)
      plans.emplace(plan.bin, &plan);
  IndexMap<uint64_t, const ResolvedFunctionalValueSet *> sets;
  for (const auto &set : schema.resolvedFunctionalValueSets)
    if (set.type == typeID && set.configuration == configuration)
      sets.emplace(set.id, &set);
  auto setFor = [&](uint64_t bin) -> const ResolvedFunctionalValueSet * {
    auto plan = plans.find(bin);
    if (plan == plans.end())
      return nullptr;
    auto set = sets.find(plan->second->valueSet);
    return set == sets.end() ? nullptr : set->second;
  };
  IndexSet<uint64_t> shortRealTemplates;
  for (const auto &expression : schema.functionalExpressions)
    if (expression.role == FunctionalExpressionRole::CoverpointSample &&
        expression.resultKind == FunctionalExpressionResultKind::ShortReal)
      shortRealTemplates.insert(expression.owner);
  IndexSet<uint64_t> shortRealItems;
  for (const auto &item : schema.resolvedFunctionalItems)
    if (item.type == typeID && item.configuration == configuration &&
        shortRealTemplates.count(item.templateItem))
      shortRealItems.insert(item.id);
  struct ItemExclusions {
    const ResolvedFunctionalValueSet *type = nullptr;
    bool numeric = true;
    std::vector<const ResolvedFunctionalValueSet *> sets;
    std::vector<StateCoverageInterval> intervals;
    std::vector<StateCoverageInterval> queries;
  };
  IndexMap<uint64_t, ItemExclusions> exclusionsByItem;
  for (const auto &bin : schema.resolvedFunctionalBins) {
    if (bin.type != typeID || bin.configuration != configuration ||
        !(bin.flags & (FunctionalBinIgnore | FunctionalBinIllegal)) ||
        (bin.flags & FunctionalBinEmpty))
      continue;
    const auto *set = setFor(bin.id);
    // iff gates sampling, not the values removed by an exclusion (19.5.1,
    // 19.5.5-.6). Include guarded exclusions in the coverage denominator.
    if (set)
      exclusionsByItem[bin.item].sets.push_back(set);
  }
  auto sameDomain = [](const auto &left, const auto &right) {
    return left.kind == right.kind && left.bitWidth == right.bitWidth &&
           left.signedness == right.signedness;
  };
  for (auto &[item, exclusions] : exclusionsByItem) {
    exclusions.type = exclusions.sets.front();
    for (const auto *set : exclusions.sets) {
      if (!sameDomain(*set, *exclusions.type)) {
        exclusions.numeric = false;
        break;
      }
      for (uint32_t ordinal = 0; ordinal != set->atomCount; ++ordinal) {
        StateCoverageInterval interval;
        if (!makeStateCoverageInterval(
                schema, *set,
                schema.resolvedFunctionalValueAtoms[set->firstAtom + ordinal],
                shortRealItems.count(item), interval)) {
          exclusions.numeric = false;
          break;
        }
        exclusions.intervals.push_back(std::move(interval));
      }
      if (!exclusions.numeric)
        break;
    }
    if (exclusions.numeric)
      mergeStateCoverageIntervals(exclusions.intervals);
    else
      exclusions.intervals.clear();
  }
  std::vector<size_t> remaining(schema.resolvedFunctionalBins.size(), SIZE_MAX);
  for (size_t binIndex = 0; binIndex != schema.resolvedFunctionalBins.size();
       ++binIndex) {
    auto &bin = schema.resolvedFunctionalBins[binIndex];
    constexpr uint32_t nonContributing =
        FunctionalBinDefault | FunctionalBinIgnore | FunctionalBinIllegal |
        FunctionalBinEmpty;
    if (bin.type != typeID || bin.configuration != configuration ||
        bin.kind != FunctionalBinKind::State || (bin.flags & nonContributing))
      continue;
    const auto *subject = setFor(bin.id);
    if (!subject)
      continue;
    if (!subject->atomCount) {
      bin.flags |= FunctionalBinEmpty;
      continue;
    }
    auto exclusions = exclusionsByItem.find(bin.item);
    if (exclusions == exclusionsByItem.end()) {
      // Real array partitions may contain no values in the sample precision,
      // even without an ignore/illegal declaration (19.5.1, 19.11.1). Scan
      // endpoints once, stopping at the first nonempty interval; no value
      // enumeration or persistent query index is needed.
      if (subject->kind == FunctionalValueSetKind::Real) {
        bool empty = true;
        bool shortReal = shortRealItems.count(bin.item);
        for (uint32_t ordinal = 0; ordinal != subject->atomCount; ++ordinal) {
          StateCoverageInterval interval;
          if (!makeStateCoverageInterval(
                  schema, *subject,
                  schema.resolvedFunctionalValueAtoms[subject->firstAtom +
                                                      ordinal],
                  shortReal, interval) ||
              !interval.empty) {
            empty = false;
            break;
          }
        }
        if (empty)
          bin.flags |= FunctionalBinEmpty;
      }
      continue;
    }
    auto &group = exclusions->second;
    if (group.numeric && sameDomain(*subject, *group.type)) {
      remaining[binIndex] = subject->atomCount;
      for (uint32_t ordinal = 0; ordinal != subject->atomCount; ++ordinal) {
        StateCoverageInterval query;
        if (!makeStateCoverageInterval(
                schema, *subject,
                schema
                    .resolvedFunctionalValueAtoms[subject->firstAtom + ordinal],
                shortRealItems.count(bin.item), query)) {
          remaining[binIndex] = SIZE_MAX;
          break;
        }
        if (query.empty)
          --remaining[binIndex];
        else {
          query.bin = binIndex;
          group.queries.push_back(std::move(query));
        }
      }
      if (remaining[binIndex] != SIZE_MAX)
        continue;
    }
    // Reuse frozen symbolic unions for general cubes. Do not rebuild or
    // rescan all exclusion atoms for each ordinary bin.
    uint64_t cardinality = setQueries.cardinality(bin.id, 1, {});
    if (setQueries.status != OBELISK_RT_OK)
      return setQueries.status;
    if (!cardinality)
      bin.flags |= FunctionalBinEmpty;
  }
  // Sort all queries once and sweep the canonical union. Each atom and
  // endpoint is visited a bounded number of times, O(total numeric key bytes).
  for (auto &[item, group] : exclusionsByItem) {
    if (!group.numeric)
      continue;
    detail::sortByKey(
        group.queries.begin(), group.queries.end(),
        [](const auto &query) -> const auto & { return query.low; });
    size_t position = 0;
    for (const auto &query : group.queries) {
      while (position != group.intervals.size() &&
             group.intervals[position].high < query.low)
        ++position;
      if (remaining[query.bin] != SIZE_MAX &&
          position != group.intervals.size() &&
          group.intervals[position].low <= query.low &&
          query.high <= group.intervals[position].high)
        --remaining[query.bin];
    }
  }
  for (size_t bin = 0; bin != remaining.size(); ++bin)
    if (remaining[bin] == 0)
      schema.resolvedFunctionalBins[bin].flags |= FunctionalBinEmpty;
  return OBELISK_RT_OK;
}

const FunctionalCoverageCrossState *
findFunctionalCross(const FunctionalCoverageTypeState &type, uint64_t item) {
  if (type.queryMetadata) {
    auto found = type.queryMetadata->crossPositions.find(item);
    return found == type.queryMetadata->crossPositions.end()
               ? nullptr
               : &type.crosses[found->second];
  }
  auto found =
      std::find_if(type.crosses.begin(), type.crosses.end(),
                   [&](const auto &cross) { return cross.id == item; });
  return found == type.crosses.end() ? nullptr : &*found;
}

const obelisk::coverage::ResolvedFunctionalItem *
queryItemMetadata(const FunctionalCoverageTypeState &type, size_t itemIndex) {
  const size_t index =
      type.projectedItemIndex == SIZE_MAX ? itemIndex : type.projectedItemIndex;
  if (!type.queryMetadata || index >= type.queryMetadata->items.size() ||
      itemIndex >= type.items.size() ||
      type.queryMetadata->items[index].id != type.items[itemIndex])
    return nullptr;
  return &type.queryMetadata->items[index];
}

const std::vector<size_t> &
queryItemBinOrdinals(const FunctionalCoverageTypeState &type, uint64_t item) {
  static const std::vector<size_t> empty;
  if (!type.queryMetadata)
    return empty;
  auto position = type.queryMetadata->itemPositions.find(item);
  return position == type.queryMetadata->itemPositions.end()
             ? empty
             : type.queryMetadata->itemBins[position->second];
}

CoverageResult
queryInstanceItem(const FunctionalCoverageInstanceState &instance,
                  const FunctionalCoverageTypeState &type, size_t itemIndex,
                  bool typeProfile = false,
                  size_t instanceProfileIndex = SIZE_MAX,
                  std::optional<uint64_t> cumulativeAtLeast = std::nullopt) {
  CoverageResult result;
  if (itemIndex >= type.items.size() || itemIndex >= type.itemGoals.size() ||
      itemIndex >= type.itemWeights.size() ||
      itemIndex >= type.typeItemGoals.size() ||
      itemIndex >= type.typeItemWeights.size())
    return result;
  const uint64_t item = type.items[itemIndex];
  if (instanceProfileIndex == SIZE_MAX)
    instanceProfileIndex = itemIndex;
  const uint32_t goal =
      typeProfile ? type.typeItemGoals[itemIndex]
                  : instanceProfileIndex < instance.itemGoals.size()
                        ? instance.itemGoals[instanceProfileIndex]
                        : type.itemGoals[itemIndex];
  const uint32_t weight = typeProfile ? type.typeItemWeights[itemIndex]
                                      : instanceProfileIndex <
                                                instance.itemWeights.size()
                                            ? instance.itemWeights[
                                                  instanceProfileIndex]
                                            : type.itemWeights[itemIndex];
  const FunctionalCoverageCrossState *cross = findFunctionalCross(type, item);
  if (cross) {
    if (cross->excluded)
      return result;
    result.total = saturatingFunctionalMagnitude(cross->automaticBinCount);
    uint64_t crossAtLeast = cross->atLeast;
    if (auto found = instance.crossAtLeast.find(item);
        found != instance.crossAtLeast.end())
      crossAtLeast = found->second;
    if (cumulativeAtLeast)
      crossAtLeast = std::max(crossAtLeast, *cumulativeAtLeast);
    if (!crossAtLeast)
      result.covered = result.total;
    else {
      auto tuples = instance.crossTuples.find(item);
      if (tuples != instance.crossTuples.end())
        for (const auto &[components, state] : tuples->second) {
          (void)components;
          result.covered = saturatingAdd(result.covered,
                                         state.count >= crossAtLeast ? 1 : 0);
        }
    }
    constexpr uint32_t omitted = obelisk::coverage::FunctionalBinIgnore |
                                 obelisk::coverage::FunctionalBinIllegal |
                                 obelisk::coverage::FunctionalBinEmpty;
    uint64_t explicitTotal = 0;
    for (size_t ordinal : queryItemBinOrdinals(type, item)) {
      const auto &bin = instance.bins[ordinal];
      if (bin.item != item || (bin.flags & omitted) || bin.excluded)
        continue;
      explicitTotal = saturatingAdd(explicitTotal, 1);
      const uint64_t atLeast = cumulativeAtLeast
                                   ? std::max(bin.atLeast, *cumulativeAtLeast)
                                   : bin.atLeast;
      if (bin.count >= atLeast)
        result.covered = saturatingAdd(result.covered, 1);
    }
    result.total = saturatingAdd(result.total, explicitTotal);
    const long double denominator =
        floatingFunctionalMagnitude(cross->automaticBinCount) +
        static_cast<long double>(explicitTotal);
    if (denominator != 0.0L) {
      const long double raw =
          100.0L * static_cast<long double>(result.covered) / denominator;
      result.percentage =
          goal ? std::min(100.0, static_cast<double>(raw) * 100.0 / goal)
               : 100.0;
      result.contributes = true;
    } else {
      result.percentage = weight ? 0.0 : 100.0;
    }
    return result;
  }
  if (instance.bins.size() != type.bins.size())
    return result;
  constexpr uint32_t omitted = obelisk::coverage::FunctionalBinIgnore |
                               obelisk::coverage::FunctionalBinIllegal |
                               obelisk::coverage::FunctionalBinEmpty |
                               obelisk::coverage::FunctionalBinDefault |
                               obelisk::coverage::FunctionalBinDefaultSequence;
  for (size_t ordinal : queryItemBinOrdinals(type, item)) {
    const auto &bin = instance.bins[ordinal];
    if (bin.item != item || (bin.flags & omitted) || bin.excluded)
      continue;
    const uint64_t atLeast = cumulativeAtLeast
                                 ? std::max(bin.atLeast, *cumulativeAtLeast)
                                 : bin.atLeast;
    result.covered =
        saturatingAdd(result.covered, bin.count >= atLeast ? 1 : 0);
    result.total = saturatingAdd(result.total, 1);
  }
  if (result.total) {
    double raw = 100.0 * static_cast<double>(result.covered) /
                 static_cast<double>(result.total);
    result.percentage = goal ? std::min(100.0, raw * 100.0 / goal) : 100.0;
    result.contributes = true;
  } else {
    result.percentage = weight ? 0.0 : 100.0;
  }
  return result;
}

CoverageResult queryInstance(
    const FunctionalCoverageInstanceState &instance,
    const FunctionalCoverageTypeState &type,
    const std::vector<std::optional<uint64_t>> *cumulativeAtLeast = nullptr) {
  CoverageResult result;
  if (instance.bins.size() != type.bins.size() ||
      type.items.size() != type.itemAggregating.size())
    return result;
  double weighted = 0.0;
  uint64_t totalWeight = 0;
  for (size_t itemIndex = 0; itemIndex != type.items.size(); ++itemIndex) {
    const std::optional<uint64_t> atLeast =
        cumulativeAtLeast && itemIndex < cumulativeAtLeast->size()
            ? (*cumulativeAtLeast)[itemIndex]
            : std::nullopt;
    CoverageResult item =
        queryInstanceItem(instance, type, itemIndex, false, SIZE_MAX, atLeast);
    if (type.itemAggregating[itemIndex]) {
      result.covered = saturatingAdd(result.covered, item.covered);
      result.total = saturatingAdd(result.total, item.total);
    }
    uint64_t weight = itemIndex < instance.itemWeights.size()
                          ? instance.itemWeights[itemIndex]
                          : type.itemWeights[itemIndex];
    if (item.contributes && weight) {
      weighted += item.percentage * static_cast<double>(weight);
      totalWeight = saturatingAdd(totalWeight, weight);
    }
  }
  if (totalWeight) {
    result.percentage =
        instance.instanceGoal
            ? std::min(100.0, weighted / static_cast<double>(totalWeight) *
                                  100.0 / instance.instanceGoal)
            : 100.0;
    result.contributes = true;
  } else {
    result.percentage = instance.instanceWeight ? 0.0 : 100.0;
    result.covered = 0;
    result.total = 0;
  }
  return result;
}

using LoadedFunctionalInstance = FunctionalCoverageLoadedData::Instance;

void initializeFunctionalInstanceProfile(
    FunctionalCoverageInstanceState &instance,
    const FunctionalCoverageTypeState &type) {
  instance.instanceGoal = type.instanceGoal;
  instance.instanceWeight = type.instanceWeight;
  instance.itemGoals = type.itemGoals;
  instance.itemWeights = type.itemWeights;
  instance.itemAtLeast = type.itemAtLeast;
  instance.groupCrossNumPrintMissing = type.groupCrossNumPrintMissing;
  instance.crossNumPrintMissing = type.crossNumPrintMissing;
  instance.explicitCrossNumPrintMissingItems =
      type.explicitCrossNumPrintMissingItems;
  for (const FunctionalCoverageCrossState &cross : type.crosses)
    instance.crossAtLeast.emplace(cross.id, cross.atLeast);
}

const std::vector<LoadedFunctionalInstance> &
collectLoadedFunctionalInstances(const CoverageState &coverage, uint64_t typeID,
                                 const FunctionalCoverageTypeState &type) {
  using namespace obelisk::coverage;
  static const std::vector<LoadedFunctionalInstance> empty;
  if (!coverage.schema)
    return empty;
  if (!coverage.loadedData ||
      coverage.loadedData->source != coverage.schema.get()) {
    auto prepared = std::make_shared<FunctionalCoverageLoadedData>();
    prepared->source = coverage.schema.get();
    struct Location {
      FunctionalCoverageTypeKey configuration;
      size_t ordinal;
    };
    IndexMap<std::pair<UUID, uint64_t>, Location> instances;
    IndexMap<std::tuple<uint64_t, Digest, std::string>, size_t> named;
    // Generated/anonymous instances remain separate; explicit names merge only
    // within one configuration. Publish the cache after every row is indexed.
    for (const auto &instance : coverage.schema->resolvedInstances) {
      FunctionalCoverageTypeKey key{instance.type, instance.configuration};
      auto &groups = prepared->configurations[key];
      size_t ordinal = groups.size();
      if (!instance.name.empty() &&
          !(instance.flags & ResolvedInstanceGeneratedName)) {
        auto insertion =
            named.emplace(std::make_tuple(instance.type, instance.configuration,
                                          instance.name),
                          ordinal);
        ordinal = insertion.first->second;
      }
      if (ordinal == groups.size()) {
        groups.emplace_back();
        groups.back().typeID = instance.type;
        groups.back().configuration = instance.configuration;
      }
      instances.emplace(std::make_pair(instance.run, instance.id),
                        Location{key, ordinal});
    }
    auto groupFor = [&](const UUID &run,
                        uint64_t instance) -> LoadedFunctionalInstance * {
      auto found = instances.find({run, instance});
      if (found == instances.end())
        return nullptr;
      return &prepared->configurations.at(
          found->second.configuration)[found->second.ordinal];
    };
    for (const auto &option : coverage.schema->resolvedInstanceOptions)
      if (auto *group = groupFor(option.run, option.instance))
        group->options.push_back(option);
    for (const auto &counter : coverage.schema->counters) {
      if (counter.metric != MetricKind::Functional)
        continue;
      if (auto *group = groupFor(counter.run, counter.instance)) {
        uint64_t &count = group->counts[counter.entity];
        count = saturatingAdd(count, counter.value);
      }
    }
    for (const auto &tuple : coverage.schema->sparseCrossTuples) {
      auto *group = groupFor(tuple.run, tuple.instance);
      if (!group || uint64_t{tuple.firstComponent} + tuple.componentCount >
                        coverage.schema->sparseCrossTupleComponents.size())
        continue;
      std::vector<uint64_t> components(
          coverage.schema->sparseCrossTupleComponents.begin() +
              tuple.firstComponent,
          coverage.schema->sparseCrossTupleComponents.begin() +
              tuple.firstComponent + tuple.componentCount);
      auto &state = group->crossTuples[tuple.cross][components];
      const uint64_t previous = state.count;
      state.count = saturatingAdd(state.count, tuple.hits);
      state.overflow = state.overflow ||
                       (tuple.flags & SparseCrossTupleOverflow) ||
                       UINT64_MAX - previous < tuple.hits;
    }
    coverage.loadedData = std::move(prepared);
  }
  auto found =
      coverage.loadedData->configurations.find({typeID, type.configuration});
  return found == coverage.loadedData->configurations.end() ? empty
                                                            : found->second;
}

obelisk_rt_status
buildLoadedFunctionalInstance(const CoverageState &coverage,
                              const LoadedFunctionalInstance &loaded,
                              const FunctionalCoverageTypeState &type,
                              FunctionalCoverageInstanceState &instance) {
  using Owner = obelisk::coverage::FunctionalConfigurationOptionOwnerKind;
  using Option = obelisk::coverage::FunctionalConfigurationOptionKind;
  if (!coverage.schema || !type.queryMetadata ||
      type.projectedItemIndex != SIZE_MAX)
    return OBELISK_RT_INVALID_DESIGN;
  const auto &metadata = *type.queryMetadata;
  initializeFunctionalInstanceProfile(instance, type);
  instance.bins = type.bins;
  for (auto &bin : instance.bins) {
    auto count = loaded.counts.find(bin.id);
    bin.count = count == loaded.counts.end() ? 0 : count->second;
  }
  for (const auto &[templateItem, tuples] : loaded.crossTuples) {
    auto position = metadata.templateItemPositions.find(templateItem);
    if (position == metadata.templateItemPositions.end() ||
        metadata.items[position->second].kind !=
            obelisk::coverage::FunctionalItemKind::Cross)
      continue;
    auto &destination =
        instance.crossTuples[metadata.items[position->second].id];
    // Both the persisted profile and live counters use bounded-depth indexes.
    for (const auto &[components, state] : tuples)
      destination.emplace(components, state);
  }
  bool hasGroupGoal = false;
  bool hasGroupWeight = false;
  IndexSet<uint64_t> itemGoals, itemWeights;
  std::optional<uint64_t> groupAtLeast;
  IndexMap<uint64_t, uint64_t> itemAtLeast;
  std::optional<uint64_t> groupCrossNumPrintMissing;
  IndexMap<uint64_t, uint64_t> itemCrossNumPrintMissing;
  for (const auto &option : loaded.options) {
    if (option.ownerKind == Owner::Group) {
      if (option.option == Option::Goal) {
        instance.instanceGoal =
            hasGroupGoal ? std::max(instance.instanceGoal,
                                    static_cast<uint32_t>(option.value))
                         : static_cast<uint32_t>(option.value);
        hasGroupGoal = true;
      } else if (option.option == Option::Weight) {
        instance.instanceWeight =
            hasGroupWeight
                ? std::max(instance.instanceWeight,
                           static_cast<uint32_t>(option.value))
                : static_cast<uint32_t>(option.value);
        hasGroupWeight = true;
      } else if (option.option == Option::AtLeast) {
        groupAtLeast = groupAtLeast ? std::max(*groupAtLeast, option.value)
                                    : option.value;
      } else if (option.option == Option::CrossNumPrintMissing) {
        groupCrossNumPrintMissing =
            groupCrossNumPrintMissing
                ? std::max(*groupCrossNumPrintMissing, option.value)
                : option.value;
      }
      continue;
    }
    auto position = metadata.templateItemPositions.find(option.owner);
    if (position == metadata.templateItemPositions.end())
      return OBELISK_RT_INVALID_DESIGN;
    const size_t index = position->second;
    if (index >= type.items.size())
      return OBELISK_RT_INVALID_DESIGN;
    const auto &resolved = metadata.items[position->second];
    if (option.option == Option::Goal) {
      const bool seen = !itemGoals.insert(resolved.templateItem).second;
      instance.itemGoals[index] =
          seen ? std::max(instance.itemGoals[index],
                          static_cast<uint32_t>(option.value))
               : static_cast<uint32_t>(option.value);
    } else if (option.option == Option::Weight) {
      const bool seen = !itemWeights.insert(resolved.templateItem).second;
      instance.itemWeights[index] =
          seen ? std::max(instance.itemWeights[index],
                          static_cast<uint32_t>(option.value))
               : static_cast<uint32_t>(option.value);
    } else if (option.option == Option::AtLeast) {
      itemAtLeast[resolved.templateItem] =
          std::max(itemAtLeast[resolved.templateItem], option.value);
    } else if (option.option == Option::CrossNumPrintMissing) {
      if (resolved.kind != obelisk::coverage::FunctionalItemKind::Cross)
        return OBELISK_RT_INVALID_DESIGN;
      itemCrossNumPrintMissing[resolved.templateItem] = std::max(
          itemCrossNumPrintMissing[resolved.templateItem], option.value);
    }
  }
  if (groupAtLeast)
    instance.groupAtLeast = *groupAtLeast;
  if (groupCrossNumPrintMissing)
    instance.groupCrossNumPrintMissing = *groupCrossNumPrintMissing;
  for (const auto &[item, value] : itemCrossNumPrintMissing) {
    instance.crossNumPrintMissing[item] = value;
    instance.explicitCrossNumPrintMissingItems.insert(item);
  }
  IndexMap<uint64_t, uint64_t> effectiveAtLeast;
  for (size_t index = 0; index != type.items.size(); ++index) {
    if (index >= metadata.items.size())
      return OBELISK_RT_INVALID_DESIGN;
    const auto &resolved = metadata.items[index];
    auto threshold = itemAtLeast.find(resolved.templateItem);
    if (threshold != itemAtLeast.end()) {
      effectiveAtLeast.emplace(resolved.id, threshold->second);
    } else if (groupAtLeast &&
               !metadata.explicitAtLeastItems.count(resolved.id)) {
      effectiveAtLeast.emplace(resolved.id, *groupAtLeast);
    } else {
      continue;
    }
    instance.itemAtLeast[index] = effectiveAtLeast.at(resolved.id);
  }
  for (auto &bin : instance.bins)
    if (auto threshold = effectiveAtLeast.find(bin.item);
        threshold != effectiveAtLeast.end())
      bin.atLeast = threshold->second;
  for (auto &[cross, value] : instance.crossAtLeast)
    if (auto threshold = effectiveAtLeast.find(cross);
        threshold != effectiveAtLeast.end())
      value = threshold->second;
  return OBELISK_RT_OK;
}

obelisk_rt_status materializeLoadedFunctionalInstance(
    const CoverageState &coverage, const LoadedFunctionalInstance &loaded,
    const FunctionalCoverageTypeState &type,
    const FunctionalCoverageInstanceState *&result) {
  if (!loaded.profile) {
    auto fullType = coverage.types.find({loaded.typeID, loaded.configuration});
    if (fullType == coverage.types.end() ||
        fullType->second.queryMetadata != type.queryMetadata)
      return OBELISK_RT_INVALID_DESIGN;
    auto prepared = std::make_shared<FunctionalCoverageInstanceState>();
    obelisk_rt_status status = buildLoadedFunctionalInstance(
        coverage, loaded, fullType->second, *prepared);
    if (status != OBELISK_RT_OK)
      return status;
    loaded.profile = std::move(prepared);
  }
  result = loaded.profile.get();
  return OBELISK_RT_OK;
}

obelisk_rt_status collectCumulativeAtLeast(
    const CoverageState &coverage, uint64_t typeID,
    const std::vector<const FunctionalCoverageTypeState *> &types,
    IndexMap<uint64_t, uint64_t> &thresholds) {
  if (!coverage.schema)
    return OBELISK_RT_INVALID_DESIGN;
  thresholds = {};
  for (const FunctionalCoverageTypeState *type : types) {
    if (!type)
      return OBELISK_RT_INVALID_DESIGN;
    std::vector<uint64_t> templateItems;
    templateItems.reserve(type->items.size());
    for (size_t index = 0; index != type->items.size(); ++index) {
      const auto *resolved = queryItemMetadata(*type, index);
      if (!resolved)
        return OBELISK_RT_INVALID_DESIGN;
      templateItems.push_back(resolved->templateItem);
    }
    auto accumulate = [&](const FunctionalCoverageInstanceState &instance,
                          bool projectedLiveInstance) {
      const bool projected = projectedLiveInstance &&
                             type->projectedItemIndex != SIZE_MAX;
      if ((!projected && instance.itemAtLeast.size() != type->items.size()) ||
          (projected &&
           type->projectedItemIndex >= instance.itemAtLeast.size()))
        return false;
      for (size_t itemIndex = 0; itemIndex != type->items.size(); ++itemIndex) {
        const size_t profileIndex = projected ? type->projectedItemIndex
                                              : itemIndex;
        thresholds[templateItems[itemIndex]] = std::max(
            thresholds[templateItems[itemIndex]],
            instance.itemAtLeast[profileIndex]);
      }
      return true;
    };
    for (uint64_t handle : type->instances) {
      auto instance = coverage.instances.find(handle);
      if (instance != coverage.instances.end() &&
          !accumulate(instance->second, true))
        return OBELISK_RT_INVALID_DESIGN;
    }
    for (const LoadedFunctionalInstance &loaded :
         collectLoadedFunctionalInstances(coverage, typeID, *type)) {
      const FunctionalCoverageInstanceState *instance = nullptr;
      obelisk_rt_status status =
          materializeLoadedFunctionalInstance(coverage, loaded, *type, instance);
      if (status != OBELISK_RT_OK)
        return status;
      if (!accumulate(*instance, true))
        return OBELISK_RT_INVALID_DESIGN;
    }
  }
  return OBELISK_RT_OK;
}

obelisk_rt_status queryMergedType(
    const CoverageState &coverage, uint64_t typeID,
    const std::vector<const FunctionalCoverageTypeState *> &configurations,
    CoverageResult &result, uint64_t &instanceCount) {
  using namespace obelisk::coverage;
  if (!coverage.schema || configurations.empty())
    return OBELISK_RT_INVALID_DESIGN;

  struct MergedBin {
    uint64_t count = 0;
  };
  struct MergedItem {
    uint32_t goal = 100;
    uint32_t weight = 1;
    uint64_t atLeast = 0;
    bool hasProfile = false;
    bool cross = false;
    std::vector<FunctionalCrossProfile> automaticProfiles;
    IndexMap<std::string, MergedBin> bins;
    IndexMap<std::vector<std::string>, uint64_t> tuples;
    IndexSet<
        std::tuple<uint64_t, uint64_t, Digest, uint64_t, uint64_t, uint32_t>>
        profileKeys;
  };
  IndexMap<uint64_t, MergedItem> items;
  IndexMap<uint64_t, uint64_t> cumulativeAtLeast;
  result = {};
  instanceCount = 0;
  obelisk_rt_status thresholdStatus = collectCumulativeAtLeast(
      coverage, typeID, configurations, cumulativeAtLeast);
  if (thresholdStatus != OBELISK_RT_OK)
    return thresholdStatus;

  for (const FunctionalCoverageTypeState *type : configurations) {
    if (!type || type->items.size() != type->typeItemGoals.size() ||
        type->items.size() != type->typeItemWeights.size() ||
        type->items.size() != type->itemAggregating.size())
      return OBELISK_RT_INVALID_DESIGN;
    auto accumulate = [&](const FunctionalCoverageInstanceState &instance)
        -> obelisk_rt_status {
      for (size_t itemIndex = 0; itemIndex != type->items.size(); ++itemIndex) {
        if (!type->itemAggregating[itemIndex])
          continue;
        const uint64_t resolvedItemID = type->items[itemIndex];
        const auto *resolvedItem = queryItemMetadata(*type, itemIndex);
        if (!resolvedItem || !type->queryMetadata)
          return OBELISK_RT_INVALID_DESIGN;
        MergedItem &item = items[resolvedItem->templateItem];
        auto threshold = cumulativeAtLeast.find(resolvedItem->templateItem);
        if (threshold == cumulativeAtLeast.end())
          return OBELISK_RT_INVALID_DESIGN;
        item.atLeast = threshold->second;
        if (!item.hasProfile) {
          item.goal = type->typeItemGoals[itemIndex];
          item.weight = type->typeItemWeights[itemIndex];
          item.hasProfile = true;
        } else if (item.goal != type->typeItemGoals[itemIndex] ||
                   item.weight != type->typeItemWeights[itemIndex]) {
          return OBELISK_RT_INVALID_DESIGN;
        }
        const FunctionalCoverageCrossState *cross =
            findFunctionalCross(*type, resolvedItemID);
        if (cross) {
          if (!item.cross)
            item.cross = true;
          if (cross->automaticBinCount.empty() != (cross->automaticRoot == 0))
            return OBELISK_RT_INVALID_DESIGN;
          if (cross->automaticRoot) {
            if (cross->targets.size() > UINT32_MAX)
              return OBELISK_RT_OUT_OF_RESOURCES;
            FunctionalCrossProfile profile{
                item.atLeast,
                typeID,
                type->configuration,
                cross->id,
                cross->automaticRoot,
                static_cast<uint32_t>(cross->targets.size())};
            if (item.profileKeys
                    .insert(std::make_tuple(
                        profile.atLeast, profile.type, profile.configuration,
                        profile.cross, profile.root, profile.targetCount))
                    .second)
              item.automaticProfiles.push_back(std::move(profile));
          }
          auto tuples = instance.crossTuples.find(resolvedItemID);
          if (tuples != instance.crossTuples.end()) {
            for (const auto &[components, state] : tuples->second) {
              if (components.size() != cross->targets.size())
                return OBELISK_RT_INVALID_DESIGN;
              std::vector<std::string> names;
              names.reserve(components.size());
              for (uint64_t binID : components) {
                auto bin = type->queryMetadata->binPositions.find(binID);
                if (bin == type->queryMetadata->binPositions.end())
                  return OBELISK_RT_INVALID_DESIGN;
                names.push_back(type->queryMetadata->bins[bin->second].name);
              }
              item.tuples[names] =
                  saturatingAdd(item.tuples[names], state.count);
            }
          }
          for (size_t ordinal : queryItemBinOrdinals(*type, resolvedItemID)) {
            const auto &bin = instance.bins[ordinal];
            constexpr uint32_t omitted =
                FunctionalBinIgnore | FunctionalBinIllegal | FunctionalBinEmpty;
            if (bin.item != resolvedItemID || (bin.flags & omitted) ||
                bin.excluded)
              continue;
            const auto &resolvedBin = type->queryMetadata->bins[ordinal];
            if (resolvedBin.kind != FunctionalBinKind::Cross)
              return OBELISK_RT_INVALID_DESIGN;
            MergedBin &merged = item.bins[resolvedBin.name];
            merged.count = saturatingAdd(merged.count, bin.count);
          }
          continue;
        }
        if (item.cross)
          return OBELISK_RT_INVALID_DESIGN;
        for (size_t ordinal : queryItemBinOrdinals(*type, resolvedItemID)) {
          const auto &bin = instance.bins[ordinal];
          constexpr uint32_t omitted =
              FunctionalBinIgnore | FunctionalBinIllegal | FunctionalBinEmpty |
              FunctionalBinDefault | FunctionalBinDefaultSequence;
          if (bin.item != resolvedItemID || (bin.flags & omitted) ||
              bin.excluded)
            continue;
          MergedBin &merged =
              item.bins[type->queryMetadata->bins[ordinal].name];
          merged.count = saturatingAdd(merged.count, bin.count);
        }
      }
      return OBELISK_RT_OK;
    };

    for (uint64_t handle : type->instances) {
      auto instance = coverage.instances.find(handle);
      if (instance == coverage.instances.end())
        continue;
      obelisk_rt_status status = accumulate(instance->second);
      if (status != OBELISK_RT_OK)
        return status;
      ++instanceCount;
    }
    for (const LoadedFunctionalInstance &instance :
         collectLoadedFunctionalInstances(coverage, typeID, *type)) {
      const FunctionalCoverageInstanceState *materialized = nullptr;
      obelisk_rt_status materialize = materializeLoadedFunctionalInstance(
          coverage, instance, *type, materialized);
      if (materialize != OBELISK_RT_OK)
        return materialize;
      obelisk_rt_status status = accumulate(*materialized);
      if (status != OBELISK_RT_OK)
        return status;
      ++instanceCount;
    }
  }

  double weighted = 0.0;
  uint64_t totalWeight = 0;
  for (const auto &[templateItem, item] : items) {
    (void)templateItem;
    FunctionalMagnitude totalMagnitude;
    FunctionalMagnitude positiveThresholdMagnitude;
    FunctionalMagnitude coveredMagnitude;
    if (item.cross) {
      if (!countFunctionalCrossProfileUnion(
              *coverage.schema, item.automaticProfiles, totalMagnitude))
        return OBELISK_RT_INVALID_DESIGN;
      std::vector<FunctionalCrossProfile> positiveProfiles;
      if (item.atLeast)
        positiveProfiles = item.automaticProfiles;
      if (!countFunctionalCrossProfileUnion(*coverage.schema, positiveProfiles,
                                            positiveThresholdMagnitude))
        return OBELISK_RT_INVALID_DESIGN;
    }
    const uint64_t total =
        item.cross
            ? saturatingAdd(saturatingFunctionalMagnitude(totalMagnitude),
                            item.bins.size())
            : item.bins.size();
    uint64_t covered = 0;
    if (item.cross) {
      coveredMagnitude = subtractFunctionalMagnitudes(
          totalMagnitude, positiveThresholdMagnitude);
      uint64_t sparseCovered = 0;
      for (const auto &[components, hits] : item.tuples) {
        bool contained = false;
        for (const auto &profile : item.automaticProfiles)
          if (functionalCrossProfileContains(*coverage.schema, profile,
                                             components)) {
            contained = true;
          }
        if (!contained)
          return OBELISK_RT_INVALID_DESIGN;
        if (item.atLeast && hits >= item.atLeast)
          sparseCovered = saturatingAdd(sparseCovered, 1);
      }
      coveredMagnitude = addFunctionalMagnitudes(
          coveredMagnitude, FunctionalMagnitude{sparseCovered});
      covered = saturatingFunctionalMagnitude(coveredMagnitude);
      for (const auto &[name, bin] : item.bins) {
        (void)name;
        covered = saturatingAdd(covered, bin.count >= item.atLeast ? 1 : 0);
      }
    } else
      for (const auto &[name, bin] : item.bins) {
        (void)name;
        covered += bin.count >= item.atLeast;
      }
    result.covered = saturatingAdd(result.covered, covered);
    result.total = saturatingAdd(result.total, total);
    if (!total || !item.weight)
      continue;
    const long double denominator =
        item.cross ? floatingFunctionalMagnitude(totalMagnitude) +
                         static_cast<long double>(item.bins.size())
                   : static_cast<long double>(total);
    long double numerator = item.cross
                                ? floatingFunctionalMagnitude(coveredMagnitude)
                                : static_cast<long double>(covered);
    if (item.cross)
      for (const auto &[name, bin] : item.bins) {
        (void)name;
        numerator += bin.count >= item.atLeast ? 1.0L : 0.0L;
      }
    double percentage = static_cast<double>(100.0L * numerator / denominator);
    percentage =
        item.goal ? std::min(100.0, percentage * 100.0 / item.goal) : 100.0;
    weighted += percentage * static_cast<double>(item.weight);
    totalWeight = saturatingAdd(totalWeight, item.weight);
  }
  const FunctionalCoverageTypeState &profile = *configurations.front();
  if (totalWeight) {
    result.percentage =
        profile.typeGoal
            ? std::min(100.0, weighted / static_cast<double>(totalWeight) *
                                  100.0 / profile.typeGoal)
            : 100.0;
    result.contributes = true;
  } else {
    result.percentage = profile.typeWeight ? 0.0 : 100.0;
    result.covered = 0;
    result.total = 0;
  }
  return OBELISK_RT_OK;
}

CoverageResult
queryType(const CoverageState &coverage, uint64_t typeID,
          const FunctionalCoverageTypeState &type,
          uint64_t *outInstanceCount = nullptr,
          uint64_t *outTotalWeight = nullptr,
          const IndexMap<uint64_t, uint64_t> *cumulativeAtLeast = nullptr) {
  CoverageResult result;
  uint64_t instanceCount = 0;
  uint64_t totalWeight = 0;
  double weightedPercentage = 0.0;
  std::vector<std::optional<uint64_t>> itemThresholds(type.items.size());
  if (cumulativeAtLeast)
    for (size_t itemIndex = 0; itemIndex != type.items.size(); ++itemIndex)
      if (const auto *resolved = queryItemMetadata(type, itemIndex))
        if (auto threshold = cumulativeAtLeast->find(resolved->templateItem);
            threshold != cumulativeAtLeast->end())
          itemThresholds[itemIndex] = threshold->second;
  for (uint64_t handle : type.instances) {
    auto instance = coverage.instances.find(handle);
    if (instance == coverage.instances.end())
      continue;
    CoverageResult current = queryInstance(
        instance->second, type, cumulativeAtLeast ? &itemThresholds : nullptr);
    if (current.contributes && instance->second.instanceWeight) {
      weightedPercentage +=
          current.percentage *
          static_cast<double>(instance->second.instanceWeight);
      totalWeight =
          saturatingAdd(totalWeight, instance->second.instanceWeight);
    }
    result.covered = saturatingAdd(result.covered, current.covered);
    result.total = saturatingAdd(result.total, current.total);
    ++instanceCount;
  }
  for (const auto &instance :
       collectLoadedFunctionalInstances(coverage, typeID, type)) {
    const FunctionalCoverageInstanceState *materialized = nullptr;
    if (materializeLoadedFunctionalInstance(coverage, instance, type,
                                            materialized) != OBELISK_RT_OK)
      continue;
    CoverageResult current = queryInstance(
        *materialized, type, cumulativeAtLeast ? &itemThresholds : nullptr);
    if (current.contributes && materialized->instanceWeight) {
      weightedPercentage += current.percentage *
                            static_cast<double>(materialized->instanceWeight);
      totalWeight = saturatingAdd(totalWeight, materialized->instanceWeight);
    }
    result.covered = saturatingAdd(result.covered, current.covered);
    result.total = saturatingAdd(result.total, current.total);
    ++instanceCount;
  }
  if (totalWeight) {
    result.percentage = weightedPercentage / static_cast<double>(totalWeight);
    result.contributes = true;
  } else {
    result.percentage = type.typeWeight ? 0.0 : 100.0;
    result.covered = 0;
    result.total = 0;
  }
  if (outInstanceCount)
    *outInstanceCount = instanceCount;
  if (outTotalWeight)
    *outTotalWeight = totalWeight;
  return result;
}

obelisk_rt_status
queryAllTypeConfigurations(CoverageState &coverage, uint64_t typeID,
                           CoverageResult &result, uint64_t &instanceCount,
                           uint64_t *outTypeWeight = nullptr) {
  if (!coverage.schema)
    return OBELISK_RT_INVALID_DESIGN;
  indexFunctionalConfigurations(coverage);
  auto configurationRows = coverage.configurationsByType.find(typeID);
  if (configurationRows == coverage.configurationsByType.end() ||
      configurationRows->second.empty()) {
    obelisk_rt_status status = ensureFunctionalConstructionSchemas(coverage);
    if (status != OBELISK_RT_OK)
      return status;
    if (!coverage.constructionSchemas->types.count(typeID))
      return OBELISK_RT_INVALID_DESIGN;

    // A type query aggregates constructed instances.  Before the first
    // construction there is no constructor-resolved configuration to bind and
    // no bins contributing to the type result.  In particular, do not attempt
    // to evaluate constructor-dependent options or bin expressions without
    // their constructor values. IEEE 1800-2023 19.11 makes an empty aggregate
    // 0 percent with positive type weight and 100 percent with zero weight;
    // both optional counts are zero and it never contributes globally.
    result = {};
    auto options = coverage.typeOptions.find(typeID);
    const uint32_t weight = options != coverage.typeOptions.end()
                                ? options->second.weight.value_or(1)
                                : 1;
    result.percentage = weight ? 0.0 : 100.0;
    instanceCount = 0;
    if (outTypeWeight)
      *outTypeWeight = 0;
    return OBELISK_RT_OK;
  }

  const auto &configurations = configurationRows->second;
  std::vector<const FunctionalCoverageTypeState *> types;
  types.reserve(configurations.size());
  for (const auto &configuration : configurations) {
    FunctionalCoverageTypeState *type = nullptr;
    obelisk_rt_status status =
        bindResolvedFunctionalType(coverage, typeID, configuration, type);
    if (status != OBELISK_RT_OK)
      return status;
    types.push_back(type);
  }
  const bool mergeInstances = types.front()->mergeInstances;
  for (const FunctionalCoverageTypeState *type : types)
    if (type->mergeInstances != mergeInstances ||
        type->typeGoal != types.front()->typeGoal ||
        type->typeWeight != types.front()->typeWeight)
      return OBELISK_RT_INVALID_DESIGN;
  if (outTypeWeight)
    *outTypeWeight = types.front()->typeWeight;

  result = {};
  instanceCount = 0;
  if (mergeInstances) {
    obelisk_rt_status status =
        queryMergedType(coverage, typeID, types, result, instanceCount);
    if (status != OBELISK_RT_OK)
      return status;
  } else {
    IndexMap<uint64_t, uint64_t> cumulativeAtLeast;
    obelisk_rt_status thresholdStatus =
        collectCumulativeAtLeast(coverage, typeID, types, cumulativeAtLeast);
    if (thresholdStatus != OBELISK_RT_OK)
      return thresholdStatus;
    double weightedPercentage = 0.0;
    uint64_t totalWeight = 0;
    for (const FunctionalCoverageTypeState *type : types) {
      uint64_t configurationInstances = 0;
      uint64_t configurationWeight = 0;
      CoverageResult current =
          queryType(coverage, typeID, *type, &configurationInstances,
                    &configurationWeight, &cumulativeAtLeast);
      if (current.contributes && configurationWeight) {
        weightedPercentage +=
            current.percentage * static_cast<double>(configurationWeight);
        totalWeight = saturatingAdd(totalWeight, configurationWeight);
      }
      result.covered = saturatingAdd(result.covered, current.covered);
      result.total = saturatingAdd(result.total, current.total);
      instanceCount = saturatingAdd(instanceCount, configurationInstances);
    }
    if (totalWeight) {
      result.percentage = weightedPercentage / static_cast<double>(totalWeight);
      result.contributes = true;
    } else {
      result.percentage = types.front()->typeWeight ? 0.0 : 100.0;
      result.covered = 0;
      result.total = 0;
    }
  }
  const double rawPercentage =
      result.total ? 100.0 * static_cast<double>(result.covered) /
                         static_cast<double>(result.total)
                   : 0.0;
  if (result.total && std::fabs(rawPercentage - result.percentage) > 1.0e-9) {
    // Constructor-resolved configurations can have unequal bin counts, so
    // their raw sum is not a numerator/denominator for the equally weighted
    // instance average required by IEEE 1800-2017 19.11.3. Return a bounded
    // reduced rational for that weighted type result.
    constexpr uint64_t denominator = 1000000000;
    result.covered = static_cast<uint64_t>(std::llround(
        result.percentage * static_cast<double>(denominator) / 100.0));
    result.total = denominator;
    uint64_t left = result.covered;
    uint64_t right = result.total;
    while (right) {
      uint64_t remainder = left % right;
      left = right;
      right = remainder;
    }
    if (left) {
      result.covered /= left;
      result.total /= left;
    }
  }
  return OBELISK_RT_OK;
}

const obelisk::coverage::ResolvedFunctionalItem *
findResolvedFunctionalItem(const CoverageState &coverage, uint64_t typeID,
                           const obelisk::coverage::Digest &configuration,
                           uint64_t templateItem) {
  if (!coverage.schema || !templateItem)
    return nullptr;
  auto bound = coverage.types.find({typeID, configuration});
  if (bound == coverage.types.end() || !bound->second.queryMetadata)
    return nullptr;
  const auto &metadata = *bound->second.queryMetadata;
  auto exact = metadata.templateItemPositions.find(templateItem);
  if (exact != metadata.templateItemPositions.end())
    return &metadata.items[exact->second];

  if (!metadata.templates)
    return nullptr;
  auto source = metadata.templates->items.find(templateItem);
  if (source == metadata.templates->items.end())
    return nullptr;
  auto alias = metadata.inheritedItemPositions.find(std::make_pair(
      source->second.name, static_cast<uint32_t>(source->second.kind)));
  return alias == metadata.inheritedItemPositions.end() ||
                 alias->second >= metadata.items.size()
             ? nullptr
             : &metadata.items[alias->second];
}

bool selectFunctionalItem(const CoverageState &coverage, uint64_t typeID,
                          uint64_t templateItem,
                          const FunctionalCoverageTypeState &type,
                          FunctionalCoverageTypeState &selected,
                          uint64_t *effectiveTemplateItem = nullptr) {
  const obelisk::coverage::ResolvedFunctionalItem *resolved =
      findResolvedFunctionalItem(coverage, typeID, type.configuration,
                                 templateItem);
  if (!resolved)
    return false;
  auto item = type.queryMetadata->itemPositions.find(resolved->id);
  if (item == type.queryMetadata->itemPositions.end())
    return false;
  const size_t index = item->second;
  if (index >= type.itemAggregating.size() || index >= type.itemGoals.size() ||
      index >= type.itemWeights.size() || index >= type.itemAtLeast.size() ||
      index >= type.typeItemGoals.size() ||
      index >= type.typeItemWeights.size())
    return false;

  selected = type;
  selected.items.assign(1, type.items[index]);
  selected.itemAggregating.assign(1, type.itemAggregating[index]);
  selected.itemGoals.assign(1, type.itemGoals[index]);
  selected.itemWeights.assign(1, type.itemWeights[index]);
  selected.itemAtLeast.assign(1, type.itemAtLeast[index]);
  selected.typeItemGoals.assign(1, type.typeItemGoals[index]);
  selected.typeItemWeights.assign(1, type.typeItemWeights[index]);
  selected.projectedItemIndex = index;
  // queryInstance applies the enclosing group goal after its item aggregate.
  // A point query has no enclosing-group normalization, so use the identity
  // goal and the point's own weight for its zero-denominator behavior.
  selected.instanceGoal = 100;
  selected.instanceWeight = type.itemWeights[index];
  // queryMergedType applies both item and group type goals. The selected point
  // already carries its type goal as the sole item; suppress the group layer.
  selected.typeGoal = 100;
  selected.typeWeight = type.typeItemWeights[index];
  if (effectiveTemplateItem)
    *effectiveTemplateItem = resolved->templateItem;
  return true;
}

CoverageResult
queryFunctionalItemInstance(const FunctionalCoverageInstanceState &instance,
                            const FunctionalCoverageTypeState &type,
                            std::optional<uint64_t> cumulativeAtLeast =
                                std::nullopt) {
  if (type.items.size() != 1 || type.itemGoals.size() != 1 ||
      type.itemWeights.size() != 1 || instance.bins.size() != type.bins.size())
    return {};
  return queryInstanceItem(instance, type, 0, false, type.projectedItemIndex,
                           cumulativeAtLeast);
}

obelisk_rt_status
queryFunctionalItemType(const CoverageState &coverage, uint64_t typeID,
                        const FunctionalCoverageTypeState &type,
                        CoverageResult &result, uint64_t *outTotalWeight,
                        std::optional<uint64_t> cumulativeAtLeast =
                            std::nullopt) {
  result = {};
  uint64_t totalWeight = 0;
  double weightedPercentage = 0.0;
  auto accumulate = [&](const FunctionalCoverageInstanceState &instance)
      -> obelisk_rt_status {
    if (type.projectedItemIndex >= instance.itemWeights.size())
      return OBELISK_RT_INVALID_DESIGN;
    // IEEE 19.11.3 weights each point/cross instance by its own option.weight,
    // including procedural changes. The enclosing group and the constructor's
    // default item profile do not determine this item's averaging weight.
    const uint32_t weight = instance.itemWeights[type.projectedItemIndex];
    CoverageResult current =
        queryFunctionalItemInstance(instance, type, cumulativeAtLeast);
    if (current.contributes && weight) {
      weightedPercentage += current.percentage * static_cast<double>(weight);
      totalWeight = saturatingAdd(totalWeight, weight);
    }
    result.covered = saturatingAdd(result.covered, current.covered);
    result.total = saturatingAdd(result.total, current.total);
    return OBELISK_RT_OK;
  };
  for (uint64_t handle : type.instances) {
    auto instance = coverage.instances.find(handle);
    if (instance != coverage.instances.end()) {
      obelisk_rt_status status = accumulate(instance->second);
      if (status != OBELISK_RT_OK)
        return status;
    }
  }
  for (const auto &loaded :
       collectLoadedFunctionalInstances(coverage, typeID, type)) {
    const FunctionalCoverageInstanceState *instance = nullptr;
    obelisk_rt_status materialize =
        materializeLoadedFunctionalInstance(coverage, loaded, type, instance);
    if (materialize != OBELISK_RT_OK)
      return materialize;
    obelisk_rt_status status = accumulate(*instance);
    if (status != OBELISK_RT_OK)
      return status;
  }
  if (totalWeight) {
    result.percentage = weightedPercentage / static_cast<double>(totalWeight);
    result.contributes = true;
  }
  if (outTotalWeight)
    *outTotalWeight = totalWeight;
  return OBELISK_RT_OK;
}

obelisk_rt_status
queryMergedFunctionalItem(const CoverageState &coverage, uint64_t typeID,
                          const std::vector<FunctionalCoverageTypeState> &types,
                          CoverageResult &result) {
  if (!coverage.schema || types.empty())
    return OBELISK_RT_INVALID_DESIGN;
  std::vector<const FunctionalCoverageTypeState *> selected;
  selected.reserve(types.size());
  for (const auto &type : types) {
    if (type.items.size() != 1 || type.typeItemGoals.size() != 1 ||
        type.typeItemWeights.size() != 1)
      return OBELISK_RT_INVALID_DESIGN;
    selected.push_back(&type);
  }
  uint64_t ignoredInstanceCount = 0;
  return queryMergedType(coverage, typeID, selected, result,
                         ignoredInstanceCount);
}

void makeFunctionalCountsConsistent(CoverageResult &result) {
  const double rawPercentage =
      result.total ? 100.0 * static_cast<double>(result.covered) /
                         static_cast<double>(result.total)
                   : 0.0;
  if (!result.total || std::fabs(rawPercentage - result.percentage) <= 1.0e-9)
    return;
  constexpr uint64_t denominator = 1000000000;
  result.covered = static_cast<uint64_t>(std::llround(
      result.percentage * static_cast<double>(denominator) / 100.0));
  result.total = denominator;
  uint64_t left = result.covered;
  uint64_t right = result.total;
  while (right) {
    uint64_t remainder = left % right;
    left = right;
    right = remainder;
  }
  if (left) {
    result.covered /= left;
    result.total /= left;
  }
}

obelisk_rt_status queryAllItemConfigurations(CoverageState &coverage,
                                             uint64_t typeID,
                                             uint64_t templateItem,
                                             CoverageResult &result) {
  if (!coverage.schema || !templateItem)
    return OBELISK_RT_INVALID_DESIGN;
  obelisk_rt_status templateStatus =
      ensureFunctionalConstructionSchemas(coverage);
  if (templateStatus != OBELISK_RT_OK)
    return templateStatus;
  auto templateSchema = coverage.constructionSchemas->items.find(templateItem);
  if (templateSchema == coverage.constructionSchemas->items.end() ||
      templateSchema->second.type != typeID)
    return OBELISK_RT_INVALID_DESIGN;

  std::vector<FunctionalCoverageTypeState> selectedTypes;
  indexFunctionalConfigurations(coverage);
  auto configurations = coverage.configurationsByType.find(typeID);
  if (configurations != coverage.configurationsByType.end()) {
    for (const auto &configuration : configurations->second) {
      FunctionalCoverageTypeState *type = nullptr;
      obelisk_rt_status status =
          bindResolvedFunctionalType(coverage, typeID, configuration, type);
      if (status != OBELISK_RT_OK)
        return status;
      selectedTypes.emplace_back();
      if (!selectFunctionalItem(coverage, typeID, templateItem, *type,
                                selectedTypes.back()))
        return OBELISK_RT_INVALID_DESIGN;
    }
  }
  if (selectedTypes.empty()) {
    // Like the enclosing type query, a static item query has no contributing
    // bins until an instance establishes a constructor-resolved
    // configuration.  Return the empty aggregate without speculatively
    // resolving constructor-time expressions.
    result = {};
    auto options = coverage.typeOptions.find(typeID);
    if (options != coverage.typeOptions.end()) {
      auto weight = options->second.itemWeights.find(templateItem);
      if (weight != options->second.itemWeights.end() && !weight->second)
        result.percentage = 100.0;
    }
    return OBELISK_RT_OK;
  }

  const bool mergeInstances = selectedTypes.front().mergeInstances;
  const uint32_t typeGoal = selectedTypes.front().typeItemGoals.front();
  const uint32_t typeWeight = selectedTypes.front().typeItemWeights.front();
  for (const auto &type : selectedTypes)
    if (type.mergeInstances != mergeInstances ||
        type.typeItemGoals.front() != typeGoal ||
        type.typeItemWeights.front() != typeWeight)
      return OBELISK_RT_INVALID_DESIGN;

  result = {};
  if (mergeInstances) {
    obelisk_rt_status status =
        queryMergedFunctionalItem(coverage, typeID, selectedTypes, result);
    if (status != OBELISK_RT_OK)
      return status;
  } else {
    std::vector<const FunctionalCoverageTypeState *> projectedTypes;
    projectedTypes.reserve(selectedTypes.size());
    for (const auto &type : selectedTypes)
      projectedTypes.push_back(&type);
    IndexMap<uint64_t, uint64_t> cumulativeAtLeast;
    obelisk_rt_status thresholdStatus = collectCumulativeAtLeast(
        coverage, typeID, projectedTypes, cumulativeAtLeast);
    if (thresholdStatus != OBELISK_RT_OK)
      return thresholdStatus;
    const std::optional<uint64_t> itemAtLeast =
        cumulativeAtLeast.count(templateItem)
            ? std::optional<uint64_t>(cumulativeAtLeast.at(templateItem))
            : std::nullopt;
    double weightedPercentage = 0.0;
    uint64_t totalWeight = 0;
    for (const auto &type : selectedTypes) {
      uint64_t instanceWeight = 0;
      CoverageResult current;
      obelisk_rt_status status = queryFunctionalItemType(
          coverage, typeID, type, current, &instanceWeight, itemAtLeast);
      if (status != OBELISK_RT_OK)
        return status;
      if (current.contributes && instanceWeight) {
        weightedPercentage +=
            current.percentage * static_cast<double>(instanceWeight);
        totalWeight = saturatingAdd(totalWeight, instanceWeight);
      }
      result.covered = saturatingAdd(result.covered, current.covered);
      result.total = saturatingAdd(result.total, current.total);
    }
    if (totalWeight) {
      double raw = weightedPercentage / static_cast<double>(totalWeight);
      result.percentage =
          typeGoal ? std::min(100.0, raw * 100.0 / typeGoal) : 100.0;
      result.contributes = true;
    } else {
      result.percentage = typeWeight ? 0.0 : 100.0;
      result.covered = 0;
      result.total = 0;
    }
  }
  makeFunctionalCountsConsistent(result);
  return OBELISK_RT_OK;
}

obelisk_rt_status writeResult(const CoverageResult &result,
                              double *outPercentage, int32_t *outCovered,
                              int32_t *outTotal) {
  if (!outPercentage || !outCovered || !outTotal)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outPercentage = std::max(0.0, std::min(100.0, result.percentage));
  *outCovered = saturateI32(result.covered);
  *outTotal = saturateI32(result.total);
  return OBELISK_RT_OK;
}

} // namespace

struct FunctionalCoveragePreparedConstruction {
  uint32_t stamp = 0;
  std::unique_ptr<PreparedFunctionalConstruction> state;
};

obelisk_rt_status obelisk_rt_coverage_prepare_construction(
    obelisk_rt_context *context, uint64_t typeID,
    const obelisk_rt_functional_value_v1 *expressions, uint64_t expressionCount,
    uint64_t *outToken) {
  using namespace obelisk::coverage;
  if (!context || !typeID || !outToken || (expressionCount && !expressions))
    return OBELISK_RT_INVALID_ARGUMENT;
  OBELISK_RT_TRY {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    CoverageState *coverage = coverageState(context, true);
    if (!coverage->schema)
      return OBELISK_RT_INVALID_DESIGN;
    auto status = ensureFunctionalConstructionSchemas(*coverage);
    if (status != OBELISK_RT_OK)
      return status;
    auto construction = coverage->constructionSchemas->types.find(typeID);
    if (construction == coverage->constructionSchemas->types.end())
      return OBELISK_RT_INVALID_DESIGN;
    const auto &schema = *construction->second;
    size_t supplied = 0;
    for (const auto *expression :
         schemaOrdered(schema.functionalExpressions, typeID, true)) {
      if (expression->role == FunctionalExpressionRole::SelectorWith &&
          expression->resultKind == FunctionalExpressionResultKind::TupleQueue)
        continue;
      if (expression->role == FunctionalExpressionRole::FormalDefault &&
          (supplied == expressionCount ||
           expressions[supplied].id != expression->id))
        continue;
      if (supplied == expressionCount ||
          expressions[supplied].id != expression->id ||
          !validateValue(
              context, expressions[supplied], expression->resultKind,
              expression->resultKind == FunctionalExpressionResultKind::Real
                  ? 64
                  : expression->bitWidth,
              false,
              functionalDefaultContainerType(*coverage, expression->owner),
              functionalDefaultAggregateType(*coverage, expression->owner),
              functionalDefaultClassType(*coverage, expression->owner),
              formalValueType(*coverage, expression->owner)))
        return OBELISK_RT_INVALID_DESIGN;
      ++supplied;
    }
    if (supplied != expressionCount)
      return OBELISK_RT_INVALID_DESIGN;
    auto prepared = std::make_shared<FunctionalCoveragePreparedConstruction>();
    prepared->state = std::make_unique<PreparedFunctionalConstruction>();
    Database pending;
    Digest unused{};
    status = resolveStaticFunctionalType(
        context, pending, typeID, expressions, expressionCount, unused,
        *coverage->constructionSchemas, prepared->state.get(), &schema);
    if (status != OBELISK_RT_OK)
      return status;
    uint64_t stamp =
        nextCoveragePreparationStamp.load(std::memory_order_relaxed);
    while (stamp <= UINT32_MAX &&
           !nextCoveragePreparationStamp.compare_exchange_weak(
               stamp, stamp + 1, std::memory_order_relaxed)) {
    }
    if (stamp > UINT32_MAX)
      return OBELISK_RT_OUT_OF_RESOURCES;
    prepared->stamp = stamp;
    uint32_t slot = 0;
    if (!coverage->freePreparations.empty()) {
      slot = coverage->freePreparations.back();
      coverage->preparations[slot] = std::move(prepared);
      coverage->freePreparations.pop_back();
    } else {
      if (coverage->preparations.size() >= UINT32_MAX)
        return OBELISK_RT_OUT_OF_RESOURCES;
      slot = coverage->preparations.size();
      coverage->preparations.push_back(std::move(prepared));
    }
    *outToken = (stamp << 32) | (uint64_t{slot} + 1);
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH(const std::length_error &) {
    return OBELISK_RT_OUT_OF_RESOURCES;
  }
}

obelisk_rt_status obelisk_rt_coverage_prepared_candidates(
    obelisk_rt_context *context, uint64_t token, uint64_t selector,
    uint32_t maximumCandidates, CoverageTupleQueueSnapshot &output) {
  if (!context || !token || !static_cast<uint32_t>(token) || !selector ||
      maximumCandidates > obelisk::coverage::MaxFunctionalCrossWithCandidates)
    return OBELISK_RT_INVALID_ARGUMENT;
  OBELISK_RT_TRY {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    CoverageState *coverage = coverageState(context, false);
    uint32_t slot = static_cast<uint32_t>(token) - 1;
    if (!coverage || slot >= coverage->preparations.size() ||
        !coverage->preparations[slot] ||
        coverage->preparations[slot]->stamp != token >> 32)
      return OBELISK_RT_INVALID_HANDLE;
    auto &prepared = *coverage->preparations[slot]->state;
    auto &queries = prepared.query();
    const auto &schema = prepared.database();
    const auto *root =
        queries.lookup(queries.selectors, schema.crossSelectorNodes, selector);
    const auto *plan = root ? queries.lookup(queries.crossPlans,
                                             schema.crossPlans, root->cross)
                            : nullptr;
    const auto *cross =
        root ? queries.lookup(queries.templateItems,
                              schema.resolvedFunctionalItems, root->cross)
             : nullptr;
    if (!root || !plan || !cross)
      return OBELISK_RT_INVALID_DESIGN;
    std::vector<uint64_t> targets;
    for (uint32_t ordinal = 0; ordinal != plan->targetCount; ++ordinal)
      targets.push_back(
          schema.crossTargets[plan->firstTarget + ordinal].target);
    auto found = prepared.crosses.find(targets);
    if (found == prepared.crosses.end()) {
      auto index = std::make_shared<PreparedCrossValueIndex>(prepared);
      if (!index->build(*plan))
        return index->status;
      found =
          prepared.crosses.emplace(std::move(targets), std::move(index)).first;
    }
    return found->second->candidates(*plan, *root, cross->id, maximumCandidates,
                                     output);
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH(const std::length_error &) {
    return OBELISK_RT_OUT_OF_RESOURCES;
  }
}

obelisk_rt_status
obelisk_rt_coverage_release_preparation(obelisk_rt_context *context,
                                        uint64_t token) {
  if (!context || !token || !static_cast<uint32_t>(token))
    return OBELISK_RT_INVALID_ARGUMENT;
  OBELISK_RT_TRY {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    CoverageState *coverage = coverageState(context, false);
    uint32_t slot = static_cast<uint32_t>(token) - 1;
    if (!coverage || slot >= coverage->preparations.size() ||
        !coverage->preparations[slot] ||
        coverage->preparations[slot]->stamp != token >> 32)
      return OBELISK_RT_INVALID_HANDLE;
    coverage->freePreparations.push_back(slot);
    coverage->preparations[slot].reset();
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH(const std::length_error &) {
    return OBELISK_RT_OUT_OF_RESOURCES;
  }
}

extern "C" obelisk_rt_status obelisk_rt_v1_covergroup_prepare(
    obelisk_rt_context *context, uint64_t typeID,
    const obelisk_rt_functional_value_v1 *expressions, uint64_t expressionCount,
    uint64_t *outToken) {
  return obelisk_rt_coverage_prepare_construction(context, typeID, expressions,
                                                  expressionCount, outToken);
}

extern "C" obelisk_rt_status
obelisk_rt_v1_covergroup_bind_with(obelisk_rt_context *context, uint64_t token,
                                   uint64_t selector,
                                   obelisk_rt_object_v1 *queue) {
  using namespace obelisk::coverage;
  if (!context || !token || !static_cast<uint32_t>(token) || !selector ||
      !queue)
    return OBELISK_RT_INVALID_ARGUMENT;
  OBELISK_RT_TRY {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    CoverageState *coverage = coverageState(context, false);
    uint32_t slot = static_cast<uint32_t>(token) - 1;
    if (!coverage || slot >= coverage->preparations.size() ||
        !coverage->preparations[slot] ||
        coverage->preparations[slot]->stamp != token >> 32)
      return OBELISK_RT_INVALID_HANDLE;
    auto &prepared = *coverage->preparations[slot]->state;
    auto &queries = prepared.query();
    const auto &schema = prepared.database();
    const auto *root =
        queries.lookup(queries.selectors, schema.crossSelectorNodes, selector);
    const auto *plan = root ? queries.lookup(queries.crossPlans,
                                             schema.crossPlans, root->cross)
                            : nullptr;
    const auto *cross =
        root ? queries.lookup(queries.templateItems,
                              schema.resolvedFunctionalItems, root->cross)
             : nullptr;
    if (!root || !plan || !cross || !root->withExpression)
      return OBELISK_RT_INVALID_DESIGN;
    CoverageTupleQueueSnapshot snapshot;
    auto status = obelisk_rt_coverage_tuple_queue_snapshot(
        context, queue, plan->tupleElementType, plan->tupleProvenanceSpan,
        (plan->tupleFlags & CrossTupleFourState) != 0,
        MaxFunctionalCrossWithCandidates, snapshot);
    if (status != OBELISK_RT_OK)
      return status;
    std::vector<uint64_t> targets;
    for (uint32_t ordinal = 0; ordinal != plan->targetCount; ++ordinal)
      targets.push_back(
          schema.crossTargets[plan->firstTarget + ordinal].target);
    auto found = prepared.crosses.find(targets);
    if (found == prepared.crosses.end()) {
      auto index = std::make_shared<PreparedCrossValueIndex>(prepared);
      if (!index->build(*plan))
        return index->status;
      found =
          prepared.crosses.emplace(std::move(targets), std::move(index)).first;
    }
    return found->second->bind(*plan, *root, cross->id, snapshot);
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH(const std::length_error &) {
    return OBELISK_RT_OUT_OF_RESOURCES;
  }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_covergroup_release_preparation(obelisk_rt_context *context,
                                             uint64_t token) {
  return obelisk_rt_coverage_release_preparation(context, token);
}

extern "C" obelisk_rt_status obelisk_rt_v1_covergroup_candidates(
    obelisk_rt_context *context, uint64_t token, uint64_t selector,
    obelisk_rt_object_v1 *queue, uint32_t maximumCandidates) {
  if (!context || !token || !static_cast<uint32_t>(token) || !selector ||
      !queue ||
      maximumCandidates > obelisk::coverage::MaxFunctionalCrossWithCandidates)
    return OBELISK_RT_INVALID_ARGUMENT;
  OBELISK_RT_TRY {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    CoverageState *coverage = coverageState(context, false);
    uint32_t slot = static_cast<uint32_t>(token) - 1;
    if (!coverage || slot >= coverage->preparations.size() ||
        !coverage->preparations[slot] ||
        coverage->preparations[slot]->stamp != token >> 32)
      return OBELISK_RT_INVALID_HANDLE;
    auto &prepared = *coverage->preparations[slot]->state;
    auto &queries = prepared.query();
    const auto &schema = prepared.database();
    const auto *root =
        queries.lookup(queries.selectors, schema.crossSelectorNodes, selector);
    const auto *plan = root ? queries.lookup(queries.crossPlans,
                                             schema.crossPlans, root->cross)
                            : nullptr;
    if (!plan)
      return OBELISK_RT_INVALID_DESIGN;
    CoverageTupleQueueSnapshot empty;
    obelisk_rt_status status = obelisk_rt_coverage_tuple_queue_snapshot(
        context, queue, plan->tupleElementType, plan->tupleProvenanceSpan,
        (plan->tupleFlags & obelisk::coverage::CrossTupleFourState) != 0, 0,
        empty);
    if (status != OBELISK_RT_OK)
      return status;
    CoverageTupleQueueSnapshot values;
    status = obelisk_rt_coverage_prepared_candidates(context, token, selector,
                                                     maximumCandidates, values);
    if (status != OBELISK_RT_OK)
      return status;
    ManagedExecutionScope execution(context);
    if (execution.getStatus() != OBELISK_RT_OK)
      return execution.getStatus();
    obelisk_rt_gc_root_v1 rootGuard{};
    status =
        obelisk_rt_v1_gc_root_push(execution.getLane(), &rootGuard, &queue);
    if (status != OBELISK_RT_OK)
      return status;
    for (uint64_t ordinal = 0; ordinal != values.count; ++ordinal) {
      status = obelisk_rt_v1_container_write(
          execution.getLane(), queue, ordinal,
          values.value.data() + ordinal * values.valueSize,
          values.fourState ? values.unknown.data() + ordinal * values.valueSize
                           : nullptr);
      if (status != OBELISK_RT_OK)
        break;
    }
    obelisk_rt_status pop =
        obelisk_rt_v1_gc_root_pop(execution.getLane(), &rootGuard);
    return status == OBELISK_RT_OK ? pop : status;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH(const std::length_error &) {
    return OBELISK_RT_OUT_OF_RESOURCES;
  }
}

bool obelisk_rt_coverage_tracks_static_state_unlocked(
    const obelisk_rt_context *context, uint32_t staticID) {
  if (!context || !context->coverage || !context->coverage->finalized)
    return false;
  auto found = context->coverage->toggleBindings.find(staticID);
  return found != context->coverage->toggleBindings.end() &&
         !found->second.empty();
}

void obelisk_rt_coverage_record_transition_unlocked(obelisk_rt_context *context,
                                                    uint64_t stableID,
                                                    uint64_t bitWidth,
                                                    const uint8_t *changed,
                                                    const uint8_t *newValue,
                                                    const uint8_t *newUnknown) {
  if (!context || !changed || bitWidth == 0)
    return;
  CoverageState *coverage = coverageState(context, false);
  if (!coverage || !coverage->finalized || coverage->toggleBindings.empty())
    return;

  uint32_t staticID = 0;
  int64_t signedLow = 0;
  if (!decodeNativeStatic(stableID, staticID, signedLow) || signedLow < 0)
    return;
  auto found = coverage->toggleBindings.find(staticID);
  if (found == coverage->toggleBindings.end())
    return;
  uint64_t publishedLow = static_cast<uint64_t>(signedLow);
  if (publishedLow > UINT64_MAX - bitWidth)
    return;
  uint64_t publishedEnd = publishedLow + bitWidth;
  const NativeStaticState *state = findNativeStaticState(context, staticID);

  for (const CoverageToggleBinding &binding : found->second) {
    uint64_t bindingEnd = binding.stateLow + binding.bitWidth;
    uint64_t overlapLow = std::max(publishedLow, binding.stateLow);
    uint64_t overlapEnd = std::min(publishedEnd, bindingEnd);
    if (overlapLow >= overlapEnd)
      continue;
    for (uint64_t stateBit = overlapLow; stateBit != overlapEnd; ++stateBit) {
      uint64_t publishedBit = stateBit - publishedLow;
      if (!bit(changed, publishedBit))
        continue;
      bool value = false;
      bool unknown = false;
      if (newValue) {
        value = bit(newValue, publishedBit);
        unknown = newUnknown && bit(newUnknown, publishedBit);
      } else {
        if (!state || stateBit >= state->bitWidth)
          continue;
        uint64_t absolute = state->bitOffset + stateBit;
        if (absolute / 64 >= context->stateValue.size() ||
            absolute / 64 >= context->stateUnknown.size())
          continue;
        uint64_t mask = uint64_t{1} << (absolute % 64);
        value = (context->stateValue[absolute / 64] & mask) != 0;
        unknown = (context->stateUnknown[absolute / 64] & mask) != 0;
      }
      recordToggleBit(*coverage,
                      binding.coverageBase + stateBit - binding.stateLow, value,
                      unknown);
    }
  }
}

extern "C" obelisk_rt_status obelisk_rt_v1_covergroup_create(
    obelisk_rt_context *context, uint64_t typeID,
    const obelisk_rt_functional_value_v1 *formals, uint64_t formalCount,
    const obelisk_rt_functional_value_v1 *expressions, uint64_t expressionCount,
    obelisk_rt_covergroup_v1 *outHandle) {
  if (!context || !typeID || !outHandle || (formalCount && !formals) ||
      (expressionCount && !expressions))
    return OBELISK_RT_INVALID_ARGUMENT;
  OBELISK_RT_TRY {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    CoverageState *coverage = coverageState(context, true);
    if (!coverage->schema)
      return OBELISK_RT_INVALID_DESIGN;
    obelisk_rt_status templateStatus =
        ensureFunctionalConstructionSchemas(*coverage);
    if (templateStatus != OBELISK_RT_OK)
      return templateStatus;
    auto construction = coverage->constructionSchemas->types.find(typeID);
    if (construction == coverage->constructionSchemas->types.end())
      return OBELISK_RT_INVALID_DESIGN;
    const auto &templateSchema = *construction->second;
    auto expectedFormals =
        schemaOrdered(templateSchema.functionalFormals, typeID, true);
    auto expectedExpressions =
        schemaOrdered(templateSchema.functionalExpressions, typeID, true);
    if (formalCount != expectedFormals.size())
      return OBELISK_RT_INVALID_DESIGN;
    for (size_t index = 0; index != expectedFormals.size(); ++index) {
      const auto &formal = *expectedFormals[index];
      bool reference =
          formal.direction == obelisk::coverage::FunctionalFormalDirection::Ref;
      if (formals[index].id != formal.id ||
          !validateValue(
              context, formals[index], formal.resultKind,
              formal.resultKind ==
                      obelisk::coverage::FunctionalExpressionResultKind::Real
                  ? 64
                  : formal.bitWidth,
              reference, functionalContainerType(*coverage, formal.valueType),
              functionalAggregateType(*coverage, formal.valueType),
              functionalClassType(*coverage, formal.valueType),
              formal.valueType))
        return OBELISK_RT_INVALID_DESIGN;
      if (reference && formals[index].argument_ref_kind != 0) {
        if (obelisk_rt_managed_object_context(formals[index].owner) != context)
          return OBELISK_RT_INVALID_HANDLE;
        obelisk_rt_managed_kind_v1 expectedKind =
            formals[index].argument_ref_kind == 2
                ? OBELISK_RT_MANAGED_REFERENCE_PATH
                : OBELISK_RT_MANAGED_CLASS;
        if (obelisk_rt_managed_object_kind(formals[index].owner) !=
            expectedKind)
          return OBELISK_RT_INVALID_HANDLE;
      }
    }
    size_t supplied = 0;
    for (const auto *expression : expectedExpressions) {
      bool isDefault =
          expression->role ==
          obelisk::coverage::FunctionalExpressionRole::FormalDefault;
      if (isDefault && (supplied == expressionCount ||
                        expressions[supplied].id != expression->id))
        continue;
      if (supplied == expressionCount ||
          expressions[supplied].id != expression->id ||
          !validateValue(
              context, expressions[supplied], expression->resultKind,
              expression->resultKind ==
                      obelisk::coverage::FunctionalExpressionResultKind::Real
                  ? 64
                  : expression->bitWidth,
              false,
              functionalDefaultContainerType(*coverage, expression->owner),
              functionalDefaultAggregateType(*coverage, expression->owner),
              functionalDefaultClassType(*coverage, expression->owner),
              formalValueType(*coverage, expression->owner)))
        return OBELISK_RT_INVALID_DESIGN;
      ++supplied;
    }
    if (supplied != expressionCount)
      return OBELISK_RT_INVALID_DESIGN;
    FunctionalCoverageTypeState *type = nullptr;
    obelisk_rt_status status = bindFunctionalType(
        context, *coverage, typeID, type, expressions, expressionCount);
    if (status != OBELISK_RT_OK)
      return status;
    const auto &schema = type->configurationSchema ? *type->configurationSchema
                                                   : *coverage->schema;
    uint64_t handle = coverage->nextInstance;
    if (!handle)
      return OBELISK_RT_OUT_OF_RESOURCES;
    FunctionalCoverageInstanceState instance;
    instance.typeID = typeID;
    instance.configuration = type->configuration;
    instance.strobe = type->strobe;
    instance.instanceGoal = type->instanceGoal;
    instance.instanceWeight = type->instanceWeight;
    instance.itemGoals = type->itemGoals;
    instance.itemWeights = type->itemWeights;
    instance.itemAtLeast = type->itemAtLeast;
    instance.bins = type->bins;
    instance.transitionActive.resize(type->queryMetadata->transitionSlotCount);
    for (const FunctionalCoverageCrossState &cross : type->crosses)
      instance.crossAtLeast.emplace(cross.id, cross.atLeast);
    for (const auto &option : schema.functionalConfigurationOptions) {
      using Owner = obelisk::coverage::FunctionalConfigurationOptionOwnerKind;
      using Option = obelisk::coverage::FunctionalConfigurationOptionKind;
      using Scope = obelisk::coverage::FunctionalOptionScopeKind;
      if (option.type != typeID ||
          option.configuration != type->configuration ||
          option.scope != Scope::Instance)
        continue;
      if (option.ownerKind == Owner::Group && option.owner == typeID) {
        if (option.option == Option::AtLeast)
          instance.groupAtLeast = option.value;
        else if (option.option == Option::CrossNumPrintMissing)
          instance.groupCrossNumPrintMissing = option.value;
        else if (option.option == Option::Comment)
          instance.comments[0] = option.stringValue;
        continue;
      }
      if (option.ownerKind != Owner::Item)
        continue;
      const auto &metadata = *type->queryMetadata;
      auto position = metadata.itemPositions.find(option.owner);
      if (position == metadata.itemPositions.end())
        return OBELISK_RT_INVALID_DESIGN;
      const auto *resolved = &metadata.items[position->second];
      if (option.option == Option::AtLeast)
        instance.explicitAtLeastItems.insert(resolved->templateItem);
      else if (option.option == Option::CrossNumPrintMissing) {
        if (resolved->kind != obelisk::coverage::FunctionalItemKind::Cross)
          return OBELISK_RT_INVALID_DESIGN;
        instance.crossNumPrintMissing[resolved->templateItem] = option.value;
        instance.explicitCrossNumPrintMissingItems.insert(
            resolved->templateItem);
      } else if (option.option == Option::Comment)
        instance.comments[resolved->templateItem] = option.stringValue;
    }
    bool hasExplicitName = false;
    for (const auto &plan : schema.functionalOptionPlans) {
      if (plan.owner != typeID ||
          plan.ownerKind != obelisk::coverage::
                                FunctionalConfigurationOptionOwnerKind::Group ||
          plan.option !=
              obelisk::coverage::FunctionalConfigurationOptionKind::Name)
        continue;
      auto value = std::find_if(
          expressions, expressions + expressionCount,
          [&](const auto &entry) { return entry.id == plan.expression; });
      if (value == expressions + expressionCount ||
          value->kind != OBELISK_RT_FUNCTIONAL_VALUE_STRING)
        return OBELISK_RT_INVALID_DESIGN;
      status = copyManagedCoverageString(value->payload, instance.name);
      if (status != OBELISK_RT_OK)
        return status;
      if (!obelisk::coverage::isValidUtf8(instance.name))
        return OBELISK_RT_INVALID_DESIGN;
      hasExplicitName = true;
      break;
    }
    if (!hasExplicitName) {
      instance.name = "$auto$" + std::to_string(handle);
      instance.generatedName = true;
    }
    for (size_t index = 0; index != expectedFormals.size(); ++index) {
      FunctionalCoverageValue value;
      snapshotValue(formals[index], value);
      value.resultKind = expectedFormals[index]->resultKind;
      instance.formals.emplace(value.id, std::move(value));
    }
    if (type->instances.size() == type->instances.max_size())
      return OBELISK_RT_OUT_OF_RESOURCES;
    if (type->instances.size() == type->instances.capacity())
      type->instances.reserve(
          std::max(type->instances.size() + 1,
                   std::min(type->instances.max_size(),
                            type->instances.capacity() * 2)));
    std::vector<uint64_t> retainedAutomaticRefs;
    retainedAutomaticRefs.reserve(instance.formals.size());
    auto insertion = coverage->instances.emplace(handle, std::move(instance));
    if (!insertion.second)
      return OBELISK_RT_OUT_OF_RESOURCES;
    for (const auto &[id, formal] : insertion.first->second.formals) {
      (void)id;
      if (formal.kind != OBELISK_RT_FUNCTIONAL_VALUE_ARGUMENT_REF ||
          formal.argumentRefKind != 0)
        continue;
      status = obelisk_rt_v1_native_state_retain(context, formal.payload);
      if (status != OBELISK_RT_OK) {
        for (uint64_t payload : retainedAutomaticRefs)
          (void)obelisk_rt_v1_native_state_release(context, payload, 0);
        coverage->instances.erase(insertion.first);
        return status;
      }
      retainedAutomaticRefs.push_back(formal.payload);
    }
    // The reserve above makes this final publication non-throwing. From this
    // point onward the instance is reachable through both context indexes.
    type->instances.push_back(handle);
    ++coverage->nextInstance;
    *outHandle = handle;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH(const std::length_error &) {
    return OBELISK_RT_OUT_OF_RESOURCES;
  }
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_covergroup_set_enabled(obelisk_rt_context *context,
                                     obelisk_rt_covergroup_v1 handle,
                                     uint64_t itemID, uint32_t enabled) {
  if (!context || enabled > 1)
    return OBELISK_RT_INVALID_ARGUMENT;
  OBELISK_RT_TRY {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    CoverageState *coverage = coverageState(context, false);
    if (!coverage)
      return OBELISK_RT_INVALID_HANDLE;
    auto found = coverage->instances.find(handle);
    if (!handle || found == coverage->instances.end())
      return OBELISK_RT_INVALID_HANDLE;
    if (!itemID) {
      found->second.enabled = enabled != 0;
      return OBELISK_RT_OK;
    }
    auto type = coverage->types.find(
        {found->second.typeID, found->second.configuration});
    if (type == coverage->types.end() || !coverage->schema)
      return OBELISK_RT_INVALID_DESIGN;
    const obelisk::coverage::ResolvedFunctionalItem *resolved =
        findResolvedFunctionalItem(*coverage, found->second.typeID,
                                   found->second.configuration, itemID);
    if (!resolved ||
        std::find(type->second.items.begin(), type->second.items.end(),
                  resolved->id) == type->second.items.end())
      return OBELISK_RT_INVALID_DESIGN;
    if (enabled)
      found->second.disabledItems.erase(resolved->templateItem);
    else
      found->second.disabledItems.insert(resolved->templateItem);
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH(const std::length_error &) {
    return OBELISK_RT_OUT_OF_RESOURCES;
  }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_covergroup_set_name(obelisk_rt_context *context,
                                  obelisk_rt_covergroup_v1 handle,
                                  obelisk_rt_string_v1 name) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  obelisk_rt_status ownership = obelisk_rt_validate_string(context, name);
  if (ownership != OBELISK_RT_OK)
    return ownership;
  OBELISK_RT_TRY {
    std::string copied;
    obelisk_rt_status status = copyManagedCoverageString(name, copied);
    if (status != OBELISK_RT_OK)
      return status;
    if (!obelisk::coverage::isValidUtf8(copied))
      return OBELISK_RT_INVALID_ARGUMENT;
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    CoverageState *coverage = coverageState(context, false);
    if (!coverage)
      return OBELISK_RT_INVALID_HANDLE;
    auto found = coverage->instances.find(handle);
    if (!handle || found == coverage->instances.end())
      return OBELISK_RT_INVALID_HANDLE;
    found->second.name = std::move(copied);
    found->second.generatedName = false;
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH(const std::length_error &) {
    return OBELISK_RT_OUT_OF_RESOURCES;
  }
}

obelisk_rt_status obelisk_rt_covergroup_validate_option_reference(
    obelisk_rt_context *context, uint64_t handle, uint64_t typeID,
    uint64_t itemID, uint32_t option) {
  if (!context || ((handle == 0) == (typeID == 0)))
    return OBELISK_RT_INVALID_ARGUMENT;
  OBELISK_RT_TRY {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    CoverageState *coverage = coverageState(context, false);
    if (!coverage || !coverage->schema)
      return OBELISK_RT_INVALID_HANDLE;
    if (typeID) {
      if (option != OBELISK_RT_COVERGROUP_OPTION_WEIGHT &&
          option != OBELISK_RT_COVERGROUP_OPTION_GOAL &&
          option != OBELISK_RT_COVERGROUP_OPTION_COMMENT &&
          ((option != OBELISK_RT_COVERGROUP_OPTION_MERGE_INSTANCES &&
            option != OBELISK_RT_COVERGROUP_OPTION_DISTRIBUTE_FIRST) ||
           itemID))
        return OBELISK_RT_INVALID_ARGUMENT;
      return validateFunctionalTypeOptionOwner(*coverage, typeID, itemID);
    }
    auto instance = coverage->instances.find(handle);
    if (instance == coverage->instances.end())
      return OBELISK_RT_INVALID_HANDLE;
    const auto *item =
        itemID
            ? findResolvedFunctionalItem(*coverage, instance->second.typeID,
                                         instance->second.configuration, itemID)
            : nullptr;
    if (itemID && !item)
      return OBELISK_RT_INVALID_DESIGN;
    if (option == OBELISK_RT_COVERGROUP_OPTION_WEIGHT ||
        option == OBELISK_RT_COVERGROUP_OPTION_GOAL ||
        option == OBELISK_RT_COVERGROUP_OPTION_AT_LEAST ||
        option == OBELISK_RT_COVERGROUP_OPTION_COMMENT ||
        (option == OBELISK_RT_COVERGROUP_OPTION_NAME && !item) ||
        (option == OBELISK_RT_COVERGROUP_OPTION_CROSS_NUM_PRINT_MISSING &&
         (!item || item->kind == obelisk::coverage::FunctionalItemKind::Cross)))
      return OBELISK_RT_OK;
    return OBELISK_RT_INVALID_ARGUMENT;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH(const std::length_error &) {
    return OBELISK_RT_OUT_OF_RESOURCES;
  }
}

extern "C" obelisk_rt_status obelisk_rt_v1_covergroup_set_integer_option(
    obelisk_rt_context *context, obelisk_rt_covergroup_v1 handle,
    uint64_t itemID, obelisk_rt_covergroup_instance_option_v1 option,
    int64_t value) {
  if (!context || value < 0 ||
      (option != OBELISK_RT_COVERGROUP_OPTION_WEIGHT &&
       option != OBELISK_RT_COVERGROUP_OPTION_GOAL &&
       option != OBELISK_RT_COVERGROUP_OPTION_AT_LEAST &&
       option != OBELISK_RT_COVERGROUP_OPTION_CROSS_NUM_PRINT_MISSING) ||
      (option == OBELISK_RT_COVERGROUP_OPTION_GOAL && value > 100) ||
      (option == OBELISK_RT_COVERGROUP_OPTION_WEIGHT &&
       static_cast<uint64_t>(value) > UINT32_MAX) ||
      (option == OBELISK_RT_COVERGROUP_OPTION_CROSS_NUM_PRINT_MISSING &&
       value > INT32_MAX))
    return OBELISK_RT_INVALID_ARGUMENT;
  OBELISK_RT_TRY {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    CoverageState *coverage = coverageState(context, false);
    if (!coverage || !coverage->schema)
      return OBELISK_RT_INVALID_HANDLE;
    auto found = coverage->instances.find(handle);
    if (!handle || found == coverage->instances.end())
      return OBELISK_RT_INVALID_HANDLE;
    FunctionalCoverageInstanceState &instance = found->second;
    auto type = coverage->types.find({instance.typeID, instance.configuration});
    if (type == coverage->types.end() || !type->second.queryMetadata)
      return OBELISK_RT_INVALID_DESIGN;
    const auto &metadata = *type->second.queryMetadata;
    if (instance.itemAtLeast.size() != metadata.items.size() ||
        instance.itemGoals.size() != metadata.items.size() ||
        instance.itemWeights.size() != metadata.items.size() ||
        instance.bins.size() != metadata.bins.size())
      return OBELISK_RT_INVALID_DESIGN;

    const obelisk::coverage::ResolvedFunctionalItem *resolved = nullptr;
    size_t itemIndex = 0;
    if (itemID) {
      resolved = findResolvedFunctionalItem(*coverage, instance.typeID,
                                            instance.configuration, itemID);
      if (!resolved)
        return OBELISK_RT_INVALID_DESIGN;
      auto position = metadata.itemPositions.find(resolved->id);
      if (position == metadata.itemPositions.end())
        return OBELISK_RT_INVALID_DESIGN;
      itemIndex = position->second;
      if (itemIndex >= instance.itemGoals.size() ||
          itemIndex >= instance.itemWeights.size() ||
          itemIndex >= instance.itemAtLeast.size())
        return OBELISK_RT_INVALID_DESIGN;
    }

    const uint64_t unsignedValue = static_cast<uint64_t>(value);
    if (option == OBELISK_RT_COVERGROUP_OPTION_CROSS_NUM_PRINT_MISSING) {
      if (resolved) {
        if (resolved->kind != obelisk::coverage::FunctionalItemKind::Cross)
          return OBELISK_RT_INVALID_ARGUMENT;
        instance.crossNumPrintMissing[resolved->templateItem] = unsignedValue;
        instance.explicitCrossNumPrintMissingItems.insert(
            resolved->templateItem);
      } else {
        instance.groupCrossNumPrintMissing = unsignedValue;
      }
      return OBELISK_RT_OK;
    }
    if (option == OBELISK_RT_COVERGROUP_OPTION_WEIGHT) {
      if (resolved)
        instance.itemWeights[itemIndex] = static_cast<uint32_t>(unsignedValue);
      else
        instance.instanceWeight = static_cast<uint32_t>(unsignedValue);
      return OBELISK_RT_OK;
    }
    if (option == OBELISK_RT_COVERGROUP_OPTION_GOAL) {
      if (resolved)
        instance.itemGoals[itemIndex] = static_cast<uint32_t>(unsignedValue);
      else
        instance.instanceGoal = static_cast<uint32_t>(unsignedValue);
      return OBELISK_RT_OK;
    }

    auto applyAtLeast = [&](size_t index, uint64_t threshold) {
      for (size_t bin : metadata.itemBins[index])
        instance.bins[bin].atLeast = threshold;
      auto cross = instance.crossAtLeast.find(metadata.items[index].id);
      if (cross != instance.crossAtLeast.end())
        cross->second = threshold;
    };
    if (resolved) {
      instance.explicitAtLeastItems.insert(resolved->templateItem);
      instance.itemAtLeast[itemIndex] = unsignedValue;
      applyAtLeast(itemIndex, unsignedValue);
      return OBELISK_RT_OK;
    }
    instance.groupAtLeast = unsignedValue;
    // Item bin lists partition the instance bins. Updating a group default
    // visits each inheriting item and bin once and preserves local overrides.
    for (size_t itemIndex = 0; itemIndex != metadata.items.size();
         ++itemIndex) {
      const auto &item = metadata.items[itemIndex];
      if (!instance.explicitAtLeastItems.count(item.templateItem)) {
        instance.itemAtLeast[itemIndex] = unsignedValue;
        applyAtLeast(itemIndex, unsignedValue);
      }
    }
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH(const std::length_error &) {
    return OBELISK_RT_OUT_OF_RESOURCES;
  }
}

extern "C" obelisk_rt_status obelisk_rt_v1_covergroup_get_integer_option(
    obelisk_rt_context *context, obelisk_rt_covergroup_v1 handle,
    uint64_t itemID, obelisk_rt_covergroup_instance_option_v1 option,
    int64_t *outValue) {
  if (!context || !outValue ||
      (option != OBELISK_RT_COVERGROUP_OPTION_WEIGHT &&
       option != OBELISK_RT_COVERGROUP_OPTION_GOAL &&
       option != OBELISK_RT_COVERGROUP_OPTION_AT_LEAST &&
       option != OBELISK_RT_COVERGROUP_OPTION_CROSS_NUM_PRINT_MISSING &&
       !(option >= OBELISK_RT_COVERGROUP_OPTION_AUTO_BIN_MAX &&
         option <= OBELISK_RT_COVERGROUP_OPTION_CROSS_RETAIN_AUTO_BINS)))
    return OBELISK_RT_INVALID_ARGUMENT;
  std::lock_guard<std::recursive_mutex> lock(context->mutex);
  CoverageState *coverage = coverageState(context, false);
  if (!coverage || !coverage->schema)
    return OBELISK_RT_INVALID_HANDLE;
  auto found = coverage->instances.find(handle);
  if (!handle || found == coverage->instances.end())
    return OBELISK_RT_INVALID_HANDLE;
  FunctionalCoverageInstanceState &instance = found->second;
  auto type = coverage->types.find({instance.typeID, instance.configuration});
  if (type == coverage->types.end() || !type->second.queryMetadata)
    return OBELISK_RT_INVALID_DESIGN;

  const obelisk::coverage::ResolvedFunctionalItem *resolved = nullptr;
  size_t itemIndex = 0;
  if (itemID) {
    resolved = findResolvedFunctionalItem(*coverage, instance.typeID,
                                          instance.configuration, itemID);
    if (!resolved)
      return OBELISK_RT_INVALID_DESIGN;
    auto position =
        type->second.queryMetadata->itemPositions.find(resolved->id);
    if (position == type->second.queryMetadata->itemPositions.end())
      return OBELISK_RT_INVALID_DESIGN;
    itemIndex = position->second;
  }

  uint64_t value = 0;
  if (option == OBELISK_RT_COVERGROUP_OPTION_WEIGHT) {
    if (resolved) {
      if (itemIndex >= instance.itemWeights.size())
        return OBELISK_RT_INVALID_DESIGN;
      value = instance.itemWeights[itemIndex];
    } else
      value = instance.instanceWeight;
  } else if (option == OBELISK_RT_COVERGROUP_OPTION_GOAL) {
    if (resolved) {
      if (itemIndex >= instance.itemGoals.size())
        return OBELISK_RT_INVALID_DESIGN;
      value = instance.itemGoals[itemIndex];
    } else
      value = instance.instanceGoal;
  } else if (option == OBELISK_RT_COVERGROUP_OPTION_AT_LEAST) {
    if (resolved) {
      if (itemIndex >= instance.itemAtLeast.size())
        return OBELISK_RT_INVALID_DESIGN;
      value = instance.itemAtLeast[itemIndex];
    } else
      value = instance.groupAtLeast;
  } else if (option == OBELISK_RT_COVERGROUP_OPTION_CROSS_NUM_PRINT_MISSING) {
    if (resolved) {
      if (resolved->kind != obelisk::coverage::FunctionalItemKind::Cross)
        return OBELISK_RT_INVALID_ARGUMENT;
      auto explicitValue =
          instance.crossNumPrintMissing.find(resolved->templateItem);
      value = explicitValue == instance.crossNumPrintMissing.end()
                  ? instance.groupCrossNumPrintMissing
                  : explicitValue->second;
    } else
      value = instance.groupCrossNumPrintMissing;
  } else {
    const auto &metadata = *type->second.queryMetadata;
    if (resolved && itemIndex >= metadata.itemOptions.size())
      return OBELISK_RT_INVALID_DESIGN;
    const auto &construction =
        resolved ? metadata.itemOptions[itemIndex] : metadata.groupOptions;
    const bool point =
        resolved &&
        resolved->kind == obelisk::coverage::FunctionalItemKind::Coverpoint;
    const bool cross =
        resolved &&
        resolved->kind == obelisk::coverage::FunctionalItemKind::Cross;
    if (option == OBELISK_RT_COVERGROUP_OPTION_AUTO_BIN_MAX) {
      if (cross)
        return OBELISK_RT_INVALID_ARGUMENT;
      value = construction.autoBinMax;
    } else if (option == OBELISK_RT_COVERGROUP_OPTION_DETECT_OVERLAP) {
      if (cross)
        return OBELISK_RT_INVALID_ARGUMENT;
      value = construction.detectOverlap;
    } else if (option == OBELISK_RT_COVERGROUP_OPTION_CROSS_RETAIN_AUTO_BINS) {
      if (point)
        return OBELISK_RT_INVALID_ARGUMENT;
      value = construction.crossRetainAutoBins;
    } else {
      if (resolved)
        return OBELISK_RT_INVALID_ARGUMENT;
      value = option == OBELISK_RT_COVERGROUP_OPTION_PER_INSTANCE
                  ? construction.perInstance
                  : type->second.getInstCoverage;
    }
  }
  if (value > static_cast<uint64_t>(INT64_MAX))
    return OBELISK_RT_INVALID_DESIGN;
  *outValue = static_cast<int64_t>(value);
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status obelisk_rt_v1_covergroup_set_string_option(
    obelisk_rt_context *context, obelisk_rt_covergroup_v1 handle,
    uint64_t itemID, obelisk_rt_covergroup_instance_option_v1 option,
    obelisk_rt_string_v1 value) {
  if (!context || option != OBELISK_RT_COVERGROUP_OPTION_COMMENT)
    return OBELISK_RT_INVALID_ARGUMENT;
  obelisk_rt_status ownership = obelisk_rt_validate_string(context, value);
  if (ownership != OBELISK_RT_OK)
    return ownership;
  OBELISK_RT_TRY {
    std::string copied;
    obelisk_rt_status status = copyManagedCoverageString(value, copied);
    if (status != OBELISK_RT_OK)
      return status;
    if (!obelisk::coverage::isValidUtf8(copied))
      return OBELISK_RT_INVALID_ARGUMENT;
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    CoverageState *coverage = coverageState(context, false);
    if (!coverage || !coverage->schema)
      return OBELISK_RT_INVALID_HANDLE;
    auto found = coverage->instances.find(handle);
    if (!handle || found == coverage->instances.end())
      return OBELISK_RT_INVALID_HANDLE;
    if (itemID) {
      const obelisk::coverage::ResolvedFunctionalItem *resolved =
          findResolvedFunctionalItem(*coverage, found->second.typeID,
                                     found->second.configuration, itemID);
      if (!resolved)
        return OBELISK_RT_INVALID_DESIGN;
      found->second.comments[resolved->templateItem] = std::move(copied);
    } else
      found->second.comments[0] = std::move(copied);
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH(const std::length_error &) {
    return OBELISK_RT_OUT_OF_RESOURCES;
  }
}

extern "C" obelisk_rt_status obelisk_rt_v1_covergroup_get_string_option(
    obelisk_rt_gc_lane_v1 *lane, obelisk_rt_covergroup_v1 handle,
    uint64_t itemID, obelisk_rt_covergroup_instance_option_v1 option,
    obelisk_rt_string_v1 *outValue) {
  obelisk_rt_context *context = obelisk_rt_managed_lane_context(lane);
  if (!context || !outValue ||
      (option != OBELISK_RT_COVERGROUP_OPTION_COMMENT &&
       (option != OBELISK_RT_COVERGROUP_OPTION_NAME || itemID)))
    return OBELISK_RT_INVALID_ARGUMENT;
  OBELISK_RT_TRY {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    CoverageState *coverage = coverageState(context, false);
    if (!coverage || !coverage->schema)
      return OBELISK_RT_INVALID_HANDLE;
    auto found = coverage->instances.find(handle);
    if (!handle || found == coverage->instances.end())
      return OBELISK_RT_INVALID_HANDLE;
    uint64_t owner = 0;
    if (itemID) {
      const auto *item = findResolvedFunctionalItem(
          *coverage, found->second.typeID, found->second.configuration, itemID);
      if (!item)
        return OBELISK_RT_INVALID_DESIGN;
      owner = item->templateItem;
    }
    const std::string *text = &found->second.name;
    const std::string empty;
    if (option == OBELISK_RT_COVERGROUP_OPTION_COMMENT) {
      auto comment = found->second.comments.find(owner);
      text =
          comment == found->second.comments.end() ? &empty : &comment->second;
    }
    return obelisk_rt_v1_string_create(lane, text->data(), text->size(),
                                       outValue);
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH(const std::length_error &) {
    return OBELISK_RT_OUT_OF_RESOURCES;
  }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_covergroup_set_type_integer_option(
    obelisk_rt_context *context, uint64_t typeID, uint64_t itemID,
    obelisk_rt_covergroup_instance_option_v1 option, int64_t value) {
  if (!context || !typeID || value < 0 ||
      (option != OBELISK_RT_COVERGROUP_OPTION_WEIGHT &&
       option != OBELISK_RT_COVERGROUP_OPTION_GOAL &&
       option != OBELISK_RT_COVERGROUP_OPTION_MERGE_INSTANCES &&
       option != OBELISK_RT_COVERGROUP_OPTION_DISTRIBUTE_FIRST) ||
      (option == OBELISK_RT_COVERGROUP_OPTION_GOAL && value > 100) ||
      (option == OBELISK_RT_COVERGROUP_OPTION_WEIGHT &&
       static_cast<uint64_t>(value) > UINT32_MAX) ||
      ((option == OBELISK_RT_COVERGROUP_OPTION_MERGE_INSTANCES ||
        option == OBELISK_RT_COVERGROUP_OPTION_DISTRIBUTE_FIRST) &&
       (itemID || value > 1)))
    return OBELISK_RT_INVALID_ARGUMENT;
  OBELISK_RT_TRY {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    CoverageState *coverage = coverageState(context, false);
    if (!coverage || !coverage->schema)
      return OBELISK_RT_INVALID_HANDLE;
    obelisk_rt_status status =
        validateFunctionalTypeOptionOwner(*coverage, typeID, itemID);
    if (status != OBELISK_RT_OK)
      return status;

    FunctionalCoverageTypeOptions &overrides = coverage->typeOptions[typeID];
    if (option == OBELISK_RT_COVERGROUP_OPTION_DISTRIBUTE_FIRST) {
      overrides.distributeFirst = value != 0;
      return OBELISK_RT_OK;
    }
    const uint32_t unsignedValue = static_cast<uint32_t>(value);
    if (option == OBELISK_RT_COVERGROUP_OPTION_GOAL) {
      if (itemID)
        overrides.itemGoals[itemID] = unsignedValue;
      else
        overrides.goal = unsignedValue;
    } else if (option == OBELISK_RT_COVERGROUP_OPTION_WEIGHT) {
      if (itemID)
        overrides.itemWeights[itemID] = unsignedValue;
      else
        overrides.weight = unsignedValue;
    } else
      overrides.mergeInstances = value != 0;

    // Update only this owner in this type's bound configurations. Reapplying
    // all item overrides after every assignment makes initialization or a
    // sequence of item updates quadratic in the number of items.
    indexFunctionalConfigurations(*coverage);
    auto configurations = coverage->configurationsByType.find(typeID);
    if (configurations == coverage->configurationsByType.end())
      return OBELISK_RT_OK;
    for (const auto &configuration : configurations->second) {
      auto bound = coverage->types.find({typeID, configuration});
      if (bound == coverage->types.end())
        continue;
      auto &type = bound->second;
      if (!itemID) {
        if (option == OBELISK_RT_COVERGROUP_OPTION_GOAL)
          type.typeGoal = unsignedValue;
        else if (option == OBELISK_RT_COVERGROUP_OPTION_WEIGHT)
          type.typeWeight = unsignedValue;
        else
          type.mergeInstances = value != 0;
        continue;
      }
      if (!type.queryMetadata)
        return OBELISK_RT_INVALID_DESIGN;
      auto position = type.queryMetadata->templateItemPositions.find(itemID);
      if (position == type.queryMetadata->templateItemPositions.end() ||
          position->second >= type.typeItemGoals.size() ||
          position->second >= type.typeItemWeights.size())
        return OBELISK_RT_INVALID_DESIGN;
      if (option == OBELISK_RT_COVERGROUP_OPTION_GOAL)
        type.typeItemGoals[position->second] = unsignedValue;
      else
        type.typeItemWeights[position->second] = unsignedValue;
    }
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH(const std::length_error &) {
    return OBELISK_RT_OUT_OF_RESOURCES;
  }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_covergroup_set_type_string_option(
    obelisk_rt_context *context, uint64_t typeID, uint64_t itemID,
    obelisk_rt_covergroup_instance_option_v1 option,
    obelisk_rt_string_v1 value) {
  if (!context || !typeID || option != OBELISK_RT_COVERGROUP_OPTION_COMMENT)
    return OBELISK_RT_INVALID_ARGUMENT;
  obelisk_rt_status ownership = obelisk_rt_validate_string(context, value);
  if (ownership != OBELISK_RT_OK)
    return ownership;
  OBELISK_RT_TRY {
    std::string copied;
    obelisk_rt_status status = copyManagedCoverageString(value, copied);
    if (status != OBELISK_RT_OK)
      return status;
    if (!obelisk::coverage::isValidUtf8(copied))
      return OBELISK_RT_INVALID_ARGUMENT;
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    CoverageState *coverage = coverageState(context, false);
    if (!coverage || !coverage->schema)
      return OBELISK_RT_INVALID_HANDLE;
    status = validateFunctionalTypeOptionOwner(*coverage, typeID, itemID);
    if (status != OBELISK_RT_OK)
      return status;
    coverage->typeOptions[typeID].comments[itemID] = std::move(copied);
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH(const std::length_error &) {
    return OBELISK_RT_OUT_OF_RESOURCES;
  }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_covergroup_sample_enabled(obelisk_rt_context *context,
                                        obelisk_rt_covergroup_v1 handle,
                                        uint32_t *outEnabled) {
  if (!context || !outEnabled)
    return OBELISK_RT_INVALID_ARGUMENT;
  std::lock_guard<std::recursive_mutex> lock(context->mutex);
  CoverageState *coverage = coverageState(context, false);
  if (!coverage)
    return OBELISK_RT_INVALID_HANDLE;
  auto found = coverage->instances.find(handle);
  if (!handle || found == coverage->instances.end())
    return OBELISK_RT_INVALID_HANDLE;
  *outEnabled = found->second.enabled;
  return OBELISK_RT_OK;
}

obelisk_rt_status
obelisk_rt_covergroup_strobe_unlocked(obelisk_rt_context *context,
                                      uint64_t handle, bool &strobe) {
  if (!context || !handle)
    return OBELISK_RT_INVALID_ARGUMENT;
  CoverageState *coverage = coverageState(context, false);
  if (!coverage)
    return OBELISK_RT_INVALID_HANDLE;
  auto instance = coverage->instances.find(handle);
  if (instance == coverage->instances.end())
    return OBELISK_RT_INVALID_HANDLE;
  strobe = instance->second.strobe;
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status obelisk_rt_v1_covergroup_sample(
    obelisk_rt_context *context, obelisk_rt_covergroup_v1 handle,
    const obelisk_rt_functional_value_v1 *expressions,
    uint64_t expressionCount) {
  if (!context || (expressionCount && !expressions))
    return OBELISK_RT_INVALID_ARGUMENT;
  OBELISK_RT_TRY {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    CoverageState *coverage = coverageState(context, false);
    if (!coverage || !coverage->schema)
      return OBELISK_RT_INVALID_HANDLE;
    auto found = coverage->instances.find(handle);
    if (!handle || found == coverage->instances.end())
      return OBELISK_RT_INVALID_HANDLE;
    FunctionalCoverageInstanceState &instance = found->second;
    auto type = coverage->types.find({instance.typeID, instance.configuration});
    if (type == coverage->types.end())
      return OBELISK_RT_INVALID_DESIGN;
    if (!type->second.configurationSchema || !type->second.queryMetadata)
      return OBELISK_RT_INVALID_DESIGN;
    const auto &schema = *type->second.configurationSchema;
    const auto &metadata = *type->second.queryMetadata;
    if (metadata.sampleItems.size() != metadata.items.size() ||
        metadata.binIffs.size() != instance.bins.size() ||
        metadata.binValueSets.size() != instance.bins.size())
      return OBELISK_RT_INVALID_DESIGN;
    std::vector<const obelisk_rt_functional_value_v1 *> sampleExpressions(
        metadata.sampleExpressions.size());
    size_t supplied = 0;
    for (size_t index = 0; index != metadata.sampleExpressions.size();
         ++index) {
      const auto *entry = metadata.sampleExpressions[index];
      bool isDefault =
          entry->role ==
          obelisk::coverage::FunctionalExpressionRole::FormalDefault;
      if (isDefault && (supplied == expressionCount ||
                        expressions[supplied].id != entry->id))
        continue;
      if (supplied == expressionCount ||
          expressions[supplied].id != entry->id ||
          !validateValue(
              context, expressions[supplied], entry->resultKind,
              entry->resultKind ==
                      obelisk::coverage::FunctionalExpressionResultKind::Real
                  ? 64
                  : entry->bitWidth,
              false, functionalDefaultContainerType(*coverage, entry->owner),
              functionalDefaultAggregateType(*coverage, entry->owner),
              functionalDefaultClassType(*coverage, entry->owner),
              formalValueType(*coverage, entry->owner)))
        return OBELISK_RT_INVALID_DESIGN;
      sampleExpressions[index] = &expressions[supplied];
      ++supplied;
    }
    if (supplied != expressionCount)
      return OBELISK_RT_INVALID_DESIGN;
    if (!instance.enabled)
      return OBELISK_RT_OK;
    if (instance.transitionActive.size() != metadata.transitionSlotCount)
      return OBELISK_RT_INVALID_DESIGN;

    std::vector<size_t> pendingBinIncrements;
    IndexSet<size_t> pendingExplicitBinIncrements;
    IndexMap<uint64_t, uint64_t> pendingIllegalIncrements;
    IndexMap<uint64_t, IndexMap<std::vector<uint64_t>, uint64_t>>
        pendingCrossIncrements;
    IndexMap<uint64_t, std::vector<uint64_t>> sampledBins;
    auto pendingTransitionActive = instance.transitionActive;
    auto pendingTransitionSampledItems = instance.transitionSampledItems;
    auto binIsEnabled = [&](size_t index) {
      size_t iff = metadata.binIffs[index];
      return iff == SIZE_MAX || functionalBooleanTrue(*sampleExpressions[iff]);
    };
    for (size_t itemIndex = 0; itemIndex != metadata.items.size();
         ++itemIndex) {
      const auto *item = &metadata.items[itemIndex];
      const uint64_t resolvedItem = item->id;
      if (item->kind != obelisk::coverage::FunctionalItemKind::Coverpoint)
        continue;
      if (instance.disabledItems.count(item->templateItem))
        continue;
      const auto &sampleItem = metadata.sampleItems[itemIndex];
      const auto *sample = sampleItem.value == SIZE_MAX
                               ? nullptr
                               : sampleExpressions[sampleItem.value];
      bool itemEnabled =
          sampleItem.iff == SIZE_MAX ||
          functionalBooleanTrue(*sampleExpressions[sampleItem.iff]);
      if (!sample || !itemEnabled)
        continue;
      // Bin arrays and transition alternatives may share a large value set.
      // Interpret it only once for this coverpoint's sample value.
      IndexMap<uint64_t, bool> matchedSets;
      auto matchesSet =
          [&](const obelisk::coverage::ResolvedFunctionalValueSet *set) {
            if (!set)
              return false;
            auto cached = matchedSets.find(set->id);
            if (cached != matchedSets.end())
              return cached->second;
            bool matched = valueSetMatches(schema, set, *sample);
            matchedSets.emplace(set->id, matched);
            return matched;
          };
      const bool hadPreviousTransitionSample =
          pendingTransitionSampledItems.count(resolvedItem) != 0;
      IndexSet<uint64_t> illegalBins;
      bool excluded = false;
      bool matchedNonDefault = false;
      std::vector<size_t> ordinary;
      std::vector<size_t> defaults;
      for (size_t binIndex : metadata.itemBins[itemIndex]) {
        auto &bin = instance.bins[binIndex];
        if (bin.kind == obelisk::coverage::FunctionalBinKind::Transition ||
            (bin.flags & obelisk::coverage::FunctionalBinEmpty))
          continue;
        bool binEnabled = binIsEnabled(binIndex);
        if (bin.flags & obelisk::coverage::FunctionalBinDefault) {
          if (binEnabled &&
              (!bin.valueSet || matchesSet(metadata.binValueSets[binIndex])))
            defaults.push_back(binIndex);
          continue;
        }
        if (!bin.valueSet || !matchesSet(metadata.binValueSets[binIndex]))
          continue;
        // The iff is a sampling guard, not part of the bin's value set.  A
        // matching defined bin therefore prevents fallback to default even when
        // its iff is false; only the counter update is disabled (IEEE 1800-2017
        // 19.5).
        matchedNonDefault = true;
        if (bin.flags & obelisk::coverage::FunctionalBinIllegal) {
          excluded = true;
          if (binEnabled)
            illegalBins.insert(bin.id);
        } else if (bin.flags & obelisk::coverage::FunctionalBinIgnore)
          excluded = true;
        else if (binEnabled)
          ordinary.push_back(binIndex);
      }
      // IEEE 1800-2017 19.5: default catches exactly the values associated with
      // none of the defined value bins. It is sampled only after all nondefault
      // bins, including ignore/illegal bins, have failed to match.
      if (!matchedNonDefault) {
        for (size_t binIndex : defaults) {
          auto &bin = instance.bins[binIndex];
          if (bin.flags & obelisk::coverage::FunctionalBinIllegal)
            illegalBins.insert(bin.id);
          else if (bin.flags & obelisk::coverage::FunctionalBinIgnore)
            excluded = true;
          else
            ordinary.push_back(binIndex);
        }
      }
      if (!illegalBins.empty()) {
        for (uint64_t illegalBin : illegalBins) {
          uint64_t &increment = pendingIllegalIncrements[illegalBin];
          increment = saturatingAdd(increment, 1);
        }
      } else if (!excluded) {
        for (size_t binIndex : ordinary) {
          const auto &bin = instance.bins[binIndex];
          pendingBinIncrements.push_back(binIndex);
          constexpr uint32_t omittedFromCross =
              obelisk::coverage::FunctionalBinDefault |
              obelisk::coverage::FunctionalBinDefaultSequence |
              obelisk::coverage::FunctionalBinIgnore |
              obelisk::coverage::FunctionalBinIllegal |
              obelisk::coverage::FunctionalBinEmpty;
          if (!(bin.flags & omittedFromCross))
            sampledBins[resolvedItem].push_back(bin.id);
        }
      }

      // State exclusions affect only state bins. IEEE 1800-2023 19.5.5 and
      // 19.5.6 explicitly require the same sampled value to continue through
      // transition matching.

      // Each active ordinal denotes the next step expected by one overlapping
      // match of a resolved finite sequence. A new match begins on every
      // enabled sample, including a sample that also advances another match.
      // Counter publication remains deferred with the rest of the sample.
      IndexSet<size_t> transitionHits;
      struct OrdinaryTransitionHit {
        size_t binIndex = 0;
        uint64_t shortestLength = UINT64_MAX;
        bool unexcluded = false;
      };
      std::vector<OrdinaryTransitionHit> ordinaryTransitionHits;
      std::vector<uint64_t> exclusionHitLengths;
      std::vector<size_t> ordinaryTransitionSlots;
      bool previousPendingRemains = false;
      for (const auto &binding : sampleItem.transitions) {
        const auto &alternative = *binding.resolved;
        const size_t binIndex = binding.bin;
        const auto *bin = &instance.bins[binIndex];
        if (bin->flags & obelisk::coverage::FunctionalBinEmpty)
          continue;
        if (!(bin->flags & (obelisk::coverage::FunctionalBinIgnore |
                            obelisk::coverage::FunctionalBinIllegal)) &&
            type->second.suppressedTransitionAlternatives.count(alternative.id))
          continue;
        bool binEnabled = binIsEnabled(binIndex);
        auto &active = pendingTransitionActive[binding.activeSlot];
        const bool exclusion =
            bin->flags & (obelisk::coverage::FunctionalBinIgnore |
                          obelisk::coverage::FunctionalBinIllegal);
        if (!binEnabled && !exclusion) {
          if (active)
            previousPendingRemains = true;
          continue;
        }
        // Exclusion membership is independent of iff (IEEE 1800-2023
        // 19.5.1, 19.5.5-.6). Keep matching ignored and illegal sequences even
        // when their diagnostic counter is disabled.
        const auto &templateAlternative = *binding.definition;
        auto matchesStep = [&](uint32_t ordinal) {
          const auto &step =
              schema.resolvedTransitionSteps[alternative.firstStep + ordinal];
          return matchesSet(metadata.valueSets.find(step.valueSet)->second);
        };
        bool hit = false;
        bool unexcludedHit = false;
        uint64_t shortestHit = UINT64_MAX;
        uint64_t shortestUnexcludedHit = UINT64_MAX;
        using ActiveState =
            FunctionalCoverageInstanceState::TransitionActiveState;
        using TransitionStates =
            FunctionalCoverageInstanceState::TransitionStates;
        std::shared_ptr<TransitionStates> next;
        auto retain = [&](const ActiveState &state, bool fromPrevious) {
          if (!next)
            next = std::make_shared<TransitionStates>();
          next->insert(state);
          previousPendingRemains |= fromPrevious;
        };
        auto advance = [&](const ActiveState &activeState,
                           bool fromPrevious) -> bool {
          ActiveState sampledState = activeState;
          if (sampleItem.transitionLengthLimit)
            sampledState.samplesConsumed =
                std::min(sampleItem.transitionLengthLimit,
                         saturatingAdd(sampledState.samplesConsumed, 1));
          auto recordHit = [&] {
            hit = true;
            shortestHit = std::min(shortestHit, sampledState.samplesConsumed);
            if (!sampledState.excluded) {
              unexcludedHit = true;
              shortestUnexcludedHit =
                  std::min(shortestUnexcludedHit, sampledState.samplesConsumed);
            }
          };
          const uint32_t ordinal = activeState.ordinal;
          const uint64_t repetitionsConsumed = activeState.repetitionsConsumed;
          // A terminal nonconsecutive repetition remains a complete match on
          // every later sample until its repeated value occurs again. Model
          // that trailing !value[*0:$] state with the terminal ordinal and
          // the repeated step as its gap-forbidden ordinal.
          if (ordinal == alternative.stepCount) {
            if (activeState.gapForbiddenOrdinal >= alternative.stepCount ||
                repetitionsConsumed ||
                schema.transitionSteps[templateAlternative.firstStep +
                                       activeState.gapForbiddenOrdinal]
                        .repetition !=
                    obelisk::coverage::TransitionRepetitionKind::Nonconsecutive)
              return false;
            if (!matchesStep(activeState.gapForbiddenOrdinal)) {
              recordHit();
              retain(sampledState, fromPrevious);
            }
            return true;
          }
          if (ordinal >= alternative.stepCount)
            return false;
          const auto &step =
              schema.resolvedTransitionSteps[alternative.firstStep + ordinal];
          const auto repetition =
              schema.transitionSteps[templateAlternative.firstStep + ordinal]
                  .repetition;
          if (repetition != obelisk::coverage::TransitionRepetitionKind::Once &&
              repetition !=
                  obelisk::coverage::TransitionRepetitionKind::Consecutive &&
              repetition != obelisk::coverage::TransitionRepetitionKind::Goto &&
              repetition !=
                  obelisk::coverage::TransitionRepetitionKind::Nonconsecutive)
            return false;
          if (!step.lowerBound || step.upperBound < step.lowerBound ||
              repetitionsConsumed >= step.upperBound)
            return false;
          bool matched = matchesStep(ordinal);
          if (activeState.gapForbiddenOrdinal != UINT32_MAX) {
            if (activeState.gapForbiddenOrdinal >= alternative.stepCount)
              return false;
            // A nonconsecutive suffix may wait through values that also
            // match its following step. Preserve both choices (19.5.2).
            if (!matchesStep(activeState.gapForbiddenOrdinal))
              retain(sampledState, fromPrevious);
          } else if (!matched &&
                     (repetition ==
                          obelisk::coverage::TransitionRepetitionKind::Goto ||
                      repetition ==
                          obelisk::coverage::TransitionRepetitionKind::
                              Nonconsecutive)) {
            retain(sampledState, fromPrevious);
          }
          if (!matched)
            return true;
          const uint64_t consumed = repetitionsConsumed + 1;
          if (consumed >= step.lowerBound) {
            if (ordinal + 1 == alternative.stepCount) {
              recordHit();
              if (repetition ==
                  obelisk::coverage::TransitionRepetitionKind::Nonconsecutive)
                retain({alternative.stepCount, ordinal, 0, activeState.excluded,
                        sampledState.samplesConsumed},
                       fromPrevious);
            } else
              retain({ordinal + 1,
                      repetition == obelisk::coverage::
                                        TransitionRepetitionKind::Nonconsecutive
                          ? ordinal
                          : UINT32_MAX,
                      0, activeState.excluded, sampledState.samplesConsumed},
                     fromPrevious);
          }
          if (consumed < step.upperBound)
            retain({ordinal, UINT32_MAX, consumed, activeState.excluded,
                    sampledState.samplesConsumed},
                   fromPrevious);
          return true;
        };
        if (active)
          for (const ActiveState &state : *active)
            if (!advance(state, true))
              return OBELISK_RT_INVALID_DESIGN;
        if (!advance({0, UINT32_MAX, 0, false}, false))
          return OBELISK_RT_INVALID_DESIGN;
        active = std::move(next);
        if (!(bin->flags & (obelisk::coverage::FunctionalBinIgnore |
                            obelisk::coverage::FunctionalBinIllegal)))
          ordinaryTransitionSlots.push_back(binding.activeSlot);
        if (hit) {
          if (bin->flags & (obelisk::coverage::FunctionalBinIgnore |
                            obelisk::coverage::FunctionalBinIllegal)) {
            transitionHits.insert(binIndex);
            exclusionHitLengths.push_back(shortestHit);
          } else {
            ordinaryTransitionHits.push_back(
                {binIndex, shortestUnexcludedHit, unexcludedHit});
          }
        }
      }
      const uint64_t shortestExclusion =
          exclusionHitLengths.empty()
              ? UINT64_MAX
              : *std::min_element(exclusionHitLengths.begin(),
                                  exclusionHitLengths.end());
      if (!exclusionHitLengths.empty()) {
        // An exclusion word that ends on this sample taints every live
        // ordinary prefix containing that suffix. Keep the taint with the
        // overlapping matcher state so a longer ordinary word cannot be
        // counted after the ignored or illegal subsequence has completed.
        for (size_t slot : ordinaryTransitionSlots) {
          auto &active = pendingTransitionActive[slot];
          if (!active || !std::any_of(active->begin(), active->end(),
                                      [&](const auto &state) {
                                        return !state.excluded &&
                                               state.samplesConsumed >=
                                                   shortestExclusion;
                                      }))
            continue;
          auto tainted = std::make_shared<
              FunctionalCoverageInstanceState::TransitionStates>();
          for (auto state : *active) {
            if (state.samplesConsumed >= shortestExclusion)
              state.excluded = true;
            tainted->insert(std::move(state));
          }
          active = std::move(tainted);
        }
      }
      for (const OrdinaryTransitionHit &hit : ordinaryTransitionHits) {
        bool excluded = !hit.unexcluded;
        excluded |= !exclusionHitLengths.empty() &&
                    shortestExclusion <= hit.shortestLength;
        if (!excluded)
          transitionHits.insert(hit.binIndex);
      }
      if (hadPreviousTransitionSample && transitionHits.empty() &&
          !previousPendingRemains) {
        for (size_t binIndex : metadata.itemBins[itemIndex]) {
          const auto &bin = instance.bins[binIndex];
          if (bin.kind != obelisk::coverage::FunctionalBinKind::Transition ||
              !(bin.flags & obelisk::coverage::FunctionalBinDefaultSequence) ||
              (bin.flags & obelisk::coverage::FunctionalBinEmpty))
            continue;
          if (binIsEnabled(binIndex))
            pendingBinIncrements.push_back(binIndex);
        }
      }
      pendingTransitionSampledItems.insert(resolvedItem);
      for (size_t binIndex : transitionHits) {
        const auto &bin = instance.bins[binIndex];
        if (!binIsEnabled(binIndex))
          continue;
        if (bin.flags & obelisk::coverage::FunctionalBinIllegal) {
          uint64_t &increment = pendingIllegalIncrements[bin.id];
          increment = saturatingAdd(increment, 1);
        } else if (!(bin.flags & obelisk::coverage::FunctionalBinIgnore)) {
          pendingBinIncrements.push_back(binIndex);
          sampledBins[resolvedItem].push_back(bin.id);
        }
      }
    }
    for (const FunctionalCoverageCrossState &cross : type->second.crosses) {
      if (instance.disabledItems.count(cross.templateItem) ||
          (cross.automaticBinCount.empty() && cross.explicitBins.empty()))
        continue;
      auto crossItem = metadata.itemPositions.find(cross.id);
      if (crossItem == metadata.itemPositions.end())
        return OBELISK_RT_INVALID_DESIGN;
      size_t crossIff = metadata.sampleItems[crossItem->second].iff;
      bool crossEnabled = crossIff == SIZE_MAX ||
                          functionalBooleanTrue(*sampleExpressions[crossIff]);
      if (!crossEnabled)
        continue;

      std::vector<const std::vector<uint64_t> *> targetHits;
      targetHits.reserve(cross.targets.size());
      for (uint64_t target : cross.targets) {
        auto hits = sampledBins.find(target);
        if (hits == sampledBins.end() || hits->second.empty())
          break;
        targetHits.push_back(&hits->second);
      }
      // A missing target makes the entire Cartesian product empty, even when
      // an earlier prefix would exceed the resource limit. Check that first.
      if (targetHits.size() != cross.targets.size())
        continue;
      size_t tupleCount = 1;
      for (const auto *hits : targetHits) {
        if (tupleCount > SIZE_MAX / hits->size())
          return OBELISK_RT_OUT_OF_RESOURCES;
        tupleCount *= hits->size();
      }
      // Decode each complete tuple directly. Growing and copying every
      // intermediate prefix was quadratic in arity for a single tuple.
      std::vector<uint64_t> tuple(cross.targets.size());
      for (size_t encodedTuple = 0; encodedTuple != tupleCount;
           ++encodedTuple) {
        size_t remainder = encodedTuple;
        for (size_t reverse = targetHits.size(); reverse != 0; --reverse) {
          size_t ordinal = reverse - 1;
          tuple[ordinal] =
              (*targetHits[ordinal])[remainder % targetHits[ordinal]->size()];
          remainder /= targetHits[ordinal]->size();
        }
        bool selectedByExplicitBin = false;
        bool ignored = false;
        IndexSet<uint64_t> illegalBins;
        IndexSet<size_t> ordinaryBins;
        for (const auto &explicitBin : cross.explicitBins) {
          const bool selected =
              explicitBin.selection
                  ? explicitBin.selection->matches(explicitBin.selectionRoot,
                                                   tuple)
                  : std::any_of(
                        explicitBin.alternatives.begin(),
                        explicitBin.alternatives.end(),
                        [&](const auto &rectangle) {
                          return std::all_of(
                              rectangle.constraints.begin(),
                              rectangle.constraints.end(),
                              [&](const auto &constraint) {
                                return constraint.targetOrdinal <
                                           tuple.size() &&
                                       std::binary_search(
                                           constraint.selectedTargetBins
                                               .begin(),
                                           constraint.selectedTargetBins.end(),
                                           tuple[constraint.targetOrdinal]);
                              });
                        });
          if (!selected)
            continue;
          selectedByExplicitBin = true;
          auto binPosition = metadata.binPositions.find(explicitBin.bin);
          if (binPosition == metadata.binPositions.end())
            return OBELISK_RT_INVALID_DESIGN;
          size_t binIndex = binPosition->second;
          const auto *bin = &instance.bins[binIndex];
          bool binEnabled = binIsEnabled(binIndex);
          // The iff guards this bin's counter, not its tuple selection. A
          // selected tuple therefore remains owned by the explicit bin and
          // cannot fall through to an automatic bin when the guard is false
          // (IEEE 1800-2023 19.6).
          if (!binEnabled)
            continue;
          if (bin->flags & obelisk::coverage::FunctionalBinIllegal) {
            illegalBins.insert(bin->id);
          } else if (bin->flags & obelisk::coverage::FunctionalBinIgnore) {
            ignored = true;
          } else {
            ordinaryBins.insert(binIndex);
          }
        }
        // IEEE 1800-2023 19.6.2-19.6.3: illegal selections take precedence
        // over ignored and ordinary cross bins, and ignored selections take
        // precedence over ordinary bins. Both kinds are outside the coverage
        // denominator but continue to own their selected tuples.
        if (!illegalBins.empty()) {
          // One cross bin is hit at most once per sample even when overlapping
          // coverpoint bins produce more than one selected tuple.
          for (uint64_t illegalBin : illegalBins)
            pendingIllegalIncrements.try_emplace(illegalBin, 1);
        } else if (ignored) {
          continue;
        } else if (selectedByExplicitBin) {
          for (size_t binIndex : ordinaryBins)
            pendingExplicitBinIncrements.insert(binIndex);
        } else if (!cross.automaticBinCount.empty()) {
          auto &counter = pendingCrossIncrements[cross.id][tuple];
          counter = saturatingAdd(counter, 1);
        }
      }
    }

    pendingBinIncrements.insert(pendingBinIncrements.end(),
                                pendingExplicitBinIncrements.begin(),
                                pendingExplicitBinIncrements.end());

    // Build every allocation-bearing update before publishing any counter. A
    // failed sample is therefore recoverable and cannot leave coverpoints,
    // sparse cross tuples, and illegal-bin diagnostics describing different
    // subsets of the same atomic sample.
    decltype(instance.crossTuples)::InsertionBatch stagedCrosses(
        instance.crossTuples);
    // Inner batches must roll back before the outer batch removes newly
    // created cross tables. Their leaves keep existing counter addresses
    // stable.
    std::vector<FunctionalCoverageCrossTupleCounters::InsertionBatch>
        stagedTuples;
    stagedTuples.reserve(pendingCrossIncrements.size());
    std::vector<std::pair<FunctionalCoverageCrossTupleState *, uint64_t>>
        pendingTupleIncrements;
    for (const auto &[crossID, increments] : pendingCrossIncrements) {
      auto cross = stagedCrosses.emplace(crossID).first;
      stagedTuples.emplace_back(cross->second);
      for (const auto &[tuple, increment] : increments) {
        auto counter = stagedTuples.back().emplace(tuple).first;
        pendingTupleIncrements.emplace_back(&counter->second, increment);
      }
    }

    std::vector<obelisk::coverage::IllegalBinDiagnostic> pendingNewDiagnostics;
    std::vector<std::pair<size_t, uint64_t>> pendingDiagnosticIncrements;
    std::vector<std::string> pendingIllegalHierarchies;
    pendingIllegalHierarchies.reserve(pendingIllegalIncrements.size());
    const obelisk::coverage::UUID currentRun = makeRunUUID(context, *coverage);
    auto *diagnostics = pendingIllegalIncrements.empty()
                            ? nullptr
                            : &indexFunctionalDiagnostics(*coverage);
    for (const auto &[bin, increment] : pendingIllegalIncrements) {
      auto resolvedBin = metadata.binPositions.find(bin);
      if (resolvedBin == metadata.binPositions.end())
        return OBELISK_RT_INVALID_DESIGN;
      pendingIllegalHierarchies.push_back(
          metadata.bins[resolvedBin->second].hierarchy);
      auto diagnostic = diagnostics->positions.find(
          std::make_tuple(currentRun, bin, handle, context->schedulerTime));
      if (diagnostic == diagnostics->positions.end())
        pendingNewDiagnostics.push_back({currentRun, bin, handle,
                                         context->schedulerTime, increment,
                                         "illegal bin sampled", 0});
      else
        pendingDiagnosticIncrements.emplace_back(diagnostic->second, increment);
    }
    if (diagnostics) {
      auto &rows = diagnostics->rows;
      if (pendingNewDiagnostics.size() > rows.max_size() - rows.size())
        return OBELISK_RT_OUT_OF_RESOURCES;
      const size_t required = rows.size() + pendingNewDiagnostics.size();
      if (required > rows.capacity()) {
        const size_t grown =
            rows.capacity() +
            std::min(rows.capacity(), rows.max_size() - rows.capacity());
        rows.reserve(std::max(required, grown));
      }
    }

    for (auto &batch : stagedTuples)
      batch.commit();
    stagedCrosses.commit();
    for (const auto &[counter, increment] : pendingTupleIncrements) {
      if (increment > UINT64_MAX - counter->count) {
        counter->count = UINT64_MAX;
        counter->overflow = true;
      } else
        counter->count += increment;
    }
    instance.transitionActive.swap(pendingTransitionActive);
    instance.transitionSampledItems.swap(pendingTransitionSampledItems);
    for (size_t binIndex : pendingBinIncrements) {
      auto &bin = instance.bins[binIndex];
      if (bin.count == UINT64_MAX)
        bin.overflow = true;
      else
        ++bin.count;
    }
    for (const auto &[position, increment] : pendingDiagnosticIncrements) {
      auto &diagnostic = diagnostics->rows[position];
      if (increment > UINT64_MAX - diagnostic.count) {
        diagnostic.count = UINT64_MAX;
        diagnostic.flags |= obelisk::coverage::IllegalBinDiagnosticOverflow;
      } else
        diagnostic.count += increment;
    }
    for (auto &diagnostic : pendingNewDiagnostics)
      diagnostics->rows.push_back(std::move(diagnostic));
    for (const std::string &hierarchy : pendingIllegalHierarchies)
      std::fprintf(stderr,
                   "ERROR: functional coverage illegal bin '%s' sampled at "
                   "simulation time %llu\n",
                   hierarchy.c_str(),
                   static_cast<unsigned long long>(context->schedulerTime));
    if (!pendingIllegalHierarchies.empty())
      latchSchedulerErrorUnlocked(context);
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH(const std::length_error &) {
    return OBELISK_RT_OUT_OF_RESOURCES;
  }
}

extern "C" obelisk_rt_status obelisk_rt_v1_covergroup_formal_read(
    obelisk_rt_context *context, obelisk_rt_covergroup_v1 handle,
    uint64_t formalID, obelisk_rt_functional_value_kind_v1 kind,
    uint64_t bitWidth, uint64_t valueSize, const uint8_t *globalValue,
    const uint8_t *globalUnknown, uint64_t globalBitCount, void *outValue,
    void *outUnknown) {
  if (!context || !formalID || !outValue)
    return OBELISK_RT_INVALID_ARGUMENT;
  OBELISK_RT_TRY {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    CoverageState *coverage = coverageState(context, false);
    if (!coverage || !coverage->schema)
      return OBELISK_RT_INVALID_HANDLE;
    auto found = coverage->instances.find(handle);
    if (!handle || found == coverage->instances.end())
      return OBELISK_RT_INVALID_HANDLE;
    auto position = found->second.formals.find(formalID);
    if (position == found->second.formals.end())
      return OBELISK_RT_INVALID_DESIGN;
    const FunctionalCoverageValue &formal = position->second;
    bool covergroup =
        formal.resultKind ==
        obelisk::coverage::FunctionalExpressionResultKind::Covergroup;
    bool process = formal.resultKind ==
                   obelisk::coverage::FunctionalExpressionResultKind::Process;
    bool virtualInterface =
        formal.resultKind ==
        obelisk::coverage::FunctionalExpressionResultKind::VirtualInterface;
    bool semaphore =
        formal.resultKind ==
        obelisk::coverage::FunctionalExpressionResultKind::Semaphore;
    bool mailbox = formal.resultKind ==
                   obelisk::coverage::FunctionalExpressionResultKind::Mailbox;
    auto validHandle = [&](uint64_t word) {
      if (semaphore || mailbox) {
        auto *object = obelisk_rt_object_from_managed_word(word);
        return obelisk_rt_managed_word_from_object(object) == word &&
               (semaphore
                    ? validateSemaphoreFormal(context, object)
                    : validateContainerFormal(
                          context, object,
                          functionalDefaultContainerType(*coverage, formalID)));
      }
      return process ? validateProcessFormal(context, word)
             : virtualInterface
                 ? validateVirtualInterfaceFormal(
                       context, word, formalValueType(*coverage, formalID))
                 : validateCovergroupFormal(
                       context, word, formalValueType(*coverage, formalID));
    };
    auto loadReference = [&](uint64_t width, uint64_t size, uint32_t fourState,
                             uint32_t valueKind, void *value, void *unknown) {
      const uint8_t *stateValue =
          globalValue ? globalValue
          : context->nativeStateValue
              ? context->nativeStateValue
              : reinterpret_cast<const uint8_t *>(context->stateValue.data());
      const uint8_t *stateUnknown =
          globalValue ? globalUnknown
          : context->nativeStateUnknown
              ? context->nativeStateUnknown
              : reinterpret_cast<const uint8_t *>(context->stateUnknown.data());
      uint64_t stateBits = globalValue ? globalBitCount
                           : context->nativeStateValue
                               ? context->nativeStateBitCount
                               : context->stateValue.size() * uint64_t{64};
      return obelisk_rt_v1_argument_ref_load(
          context, stateValue, stateUnknown, stateBits, formal.owner,
          formal.payload, formal.argumentRefKind, width, size, fourState,
          valueKind, value, unknown);
    };
    if (kind == OBELISK_RT_FUNCTIONAL_VALUE_ARGUMENT_REF) {
      if ((formal.kind != OBELISK_RT_FUNCTIONAL_VALUE_ARGUMENT_REF &&
           formal.kind != OBELISK_RT_FUNCTIONAL_VALUE_MANAGED_AGGREGATE) ||
          bitWidth != 192 || valueSize != 24 || outUnknown)
        return OBELISK_RT_INVALID_DESIGN;
      if (covergroup || process || virtualInterface || semaphore || mailbox) {
        uint64_t word = 0;
        obelisk_rt_status status =
            loadReference(64, 8, 0,
                          semaphore || mailbox ? OBELISK_RT_ARGUMENT_VALUE_CLASS
                                               : OBELISK_RT_ARGUMENT_VALUE_BITS,
                          &word, nullptr);
        if (status != OBELISK_RT_OK)
          return status;
        if (!validHandle(word))
          return OBELISK_RT_INVALID_HANDLE;
      }
      std::array<uint64_t, 3> reference{
          obelisk_rt_managed_word_from_object(formal.owner), formal.payload,
          formal.kind == OBELISK_RT_FUNCTIONAL_VALUE_MANAGED_AGGREGATE
              ? uint64_t{2}
              : formal.argumentRefKind};
      std::memcpy(outValue, reference.data(), sizeof(reference));
      return OBELISK_RT_OK;
    }
    bool string = formal.resultKind ==
                  obelisk::coverage::FunctionalExpressionResultKind::String;
    bool container =
        formal.resultKind ==
        obelisk::coverage::FunctionalExpressionResultKind::Container;
    bool classHandle =
        formal.resultKind ==
        obelisk::coverage::FunctionalExpressionResultKind::ClassHandle;
    bool chandle = formal.resultKind ==
                   obelisk::coverage::FunctionalExpressionResultKind::Chandle;
    bool event = formal.resultKind ==
                 obelisk::coverage::FunctionalExpressionResultKind::Event;
    if (obelisk::coverage::isRealResultKind(formal.resultKind)) {
      uint32_t expected =
          formal.resultKind ==
                  obelisk::coverage::FunctionalExpressionResultKind::ShortReal
              ? OBELISK_RT_FUNCTIONAL_VALUE_SHORTREAL
              : OBELISK_RT_FUNCTIONAL_VALUE_REAL;
      uint32_t width =
          obelisk::coverage::getRealResultBitWidth(formal.resultKind);
      if (kind != expected || bitWidth != width || valueSize != width / 8 ||
          outUnknown)
        return OBELISK_RT_INVALID_DESIGN;
    } else if (chandle || event || covergroup || process || virtualInterface) {
      if (kind != (virtualInterface
                       ? OBELISK_RT_FUNCTIONAL_VALUE_VIRTUAL_INTERFACE
                   : process    ? OBELISK_RT_FUNCTIONAL_VALUE_PROCESS
                   : covergroup ? OBELISK_RT_FUNCTIONAL_VALUE_COVERGROUP
                   : event      ? OBELISK_RT_FUNCTIONAL_VALUE_EVENT
                                : OBELISK_RT_FUNCTIONAL_VALUE_CHANDLE) ||
          bitWidth != 64 || valueSize != 8 || outUnknown)
        return OBELISK_RT_INVALID_DESIGN;
    } else if (container || classHandle || semaphore || mailbox) {
      uint32_t expected = mailbox     ? OBELISK_RT_FUNCTIONAL_VALUE_MAILBOX
                          : semaphore ? OBELISK_RT_FUNCTIONAL_VALUE_SEMAPHORE
                          : classHandle
                              ? OBELISK_RT_FUNCTIONAL_VALUE_CLASS_HANDLE
                              : OBELISK_RT_FUNCTIONAL_VALUE_MANAGED_CONTAINER;
      if (kind != expected || bitWidth != 64 || valueSize != 8 || outUnknown)
        return OBELISK_RT_INVALID_DESIGN;
    } else if (string) {
      if (kind != OBELISK_RT_FUNCTIONAL_VALUE_STRING ||
          bitWidth != sizeof(obelisk_rt_string_v1) * 8 ||
          valueSize != sizeof(obelisk_rt_string_v1) || outUnknown)
        return OBELISK_RT_INVALID_DESIGN;
    } else if (isFunctionalRealValueKind(kind) ||
               kind == OBELISK_RT_FUNCTIONAL_VALUE_STRING ||
               kind == OBELISK_RT_FUNCTIONAL_VALUE_MANAGED_CONTAINER ||
               kind == OBELISK_RT_FUNCTIONAL_VALUE_CLASS_HANDLE ||
               kind == OBELISK_RT_FUNCTIONAL_VALUE_SEMAPHORE ||
               kind == OBELISK_RT_FUNCTIONAL_VALUE_MAILBOX ||
               kind == OBELISK_RT_FUNCTIONAL_VALUE_CHANDLE ||
               kind == OBELISK_RT_FUNCTIONAL_VALUE_EVENT ||
               kind == OBELISK_RT_FUNCTIONAL_VALUE_COVERGROUP ||
               kind == OBELISK_RT_FUNCTIONAL_VALUE_PROCESS ||
               kind == OBELISK_RT_FUNCTIONAL_VALUE_VIRTUAL_INTERFACE ||
               formal.bitWidth != bitWidth || formal.valueSize != valueSize)
      return OBELISK_RT_INVALID_DESIGN;
    uint32_t storedKind = formal.kind;
    if (storedKind == OBELISK_RT_FUNCTIONAL_VALUE_ARGUMENT_REF) {
      uint32_t fourState = kind == OBELISK_RT_FUNCTIONAL_VALUE_FOUR_STATE;
      if (kind != OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL && !fourState &&
          !isFunctionalRealValueKind(kind) && !string && !container &&
          !classHandle && !chandle && !event && !covergroup && !process &&
          !virtualInterface && !semaphore && !mailbox)
        return OBELISK_RT_INVALID_ARGUMENT;
      if (covergroup || process || virtualInterface || semaphore || mailbox) {
        uint64_t word = 0;
        obelisk_rt_status status =
            loadReference(64, 8, 0,
                          semaphore || mailbox ? OBELISK_RT_ARGUMENT_VALUE_CLASS
                                               : OBELISK_RT_ARGUMENT_VALUE_BITS,
                          &word, nullptr);
        if (status != OBELISK_RT_OK)
          return status;
        if (!validHandle(word))
          return OBELISK_RT_INVALID_HANDLE;
        std::memcpy(outValue, &word, sizeof(word));
        return OBELISK_RT_OK;
      }
      return loadReference(bitWidth, valueSize, fourState,
                           string ? OBELISK_RT_ARGUMENT_VALUE_STRING
                           : container || classHandle
                               ? OBELISK_RT_ARGUMENT_VALUE_CLASS
                               : OBELISK_RT_ARGUMENT_VALUE_BITS,
                           outValue, outUnknown);
    }
    if (storedKind != kind ||
        (kind == OBELISK_RT_FUNCTIONAL_VALUE_FOUR_STATE && !outUnknown))
      return OBELISK_RT_INVALID_DESIGN;
    if (container || classHandle || semaphore || mailbox) {
      uint64_t word = obelisk_rt_managed_word_from_object(formal.owner);
      std::memcpy(outValue, &word, sizeof(word));
    } else if (string) {
      obelisk_rt_status status =
          obelisk_rt_validate_string(context, formal.payload);
      if (status != OBELISK_RT_OK)
        return status;
      std::memcpy(outValue, &formal.payload, sizeof(formal.payload));
    } else if (chandle || event || covergroup || process || virtualInterface)
      std::memcpy(outValue, &formal.payload, sizeof(formal.payload));
    else
      std::memcpy(outValue, formal.value.data(),
                  static_cast<size_t>(valueSize));
    if (outUnknown) {
      std::memset(outUnknown, 0, static_cast<size_t>(valueSize));
      if (!formal.unknown.empty())
        std::memcpy(outUnknown, formal.unknown.data(),
                    static_cast<size_t>(valueSize));
    }
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH(const std::length_error &) {
    return OBELISK_RT_OUT_OF_RESOURCES;
  }
}

extern "C" obelisk_rt_status obelisk_rt_v1_covergroup_instance_query(
    obelisk_rt_context *context, obelisk_rt_covergroup_v1 handle,
    uint64_t itemID, double *outPercentage, int32_t *outCovered,
    int32_t *outTotal) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  OBELISK_RT_TRY {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    CoverageState *coverage = coverageState(context, false);
    if (!coverage)
      return OBELISK_RT_INVALID_HANDLE;
    auto found = coverage->instances.find(handle);
    if (!handle || found == coverage->instances.end())
      return OBELISK_RT_INVALID_HANDLE;
    auto type = coverage->types.find(
        {found->second.typeID, found->second.configuration});
    if (type == coverage->types.end())
      return OBELISK_RT_INVALID_DESIGN;
    if (itemID) {
      FunctionalCoverageTypeState selected;
      uint64_t effectiveTemplateItem = 0;
      if (!selectFunctionalItem(*coverage, found->second.typeID, itemID,
                                type->second, selected, &effectiveTemplateItem))
        return OBELISK_RT_INVALID_DESIGN;
      if (type->second.mergeInstances && !type->second.getInstCoverage) {
        CoverageResult result;
        obelisk_rt_status status = queryAllItemConfigurations(
            *coverage, found->second.typeID, effectiveTemplateItem, result);
        if (status != OBELISK_RT_OK)
          return status;
        return writeResult(result, outPercentage, outCovered, outTotal);
      }
      return writeResult(queryFunctionalItemInstance(found->second, selected),
                         outPercentage, outCovered, outTotal);
    }
    // IEEE 1800-2017 Table 19-1: get_inst_coverage only changes behavior when
    // merge_instances is enabled. Without per-instance tracking, the instance
    // method returns the same cumulative union as get_coverage().
    if (type->second.mergeInstances && !type->second.getInstCoverage) {
      CoverageResult result;
      uint64_t instanceCount = 0;
      obelisk_rt_status status = queryAllTypeConfigurations(
          *coverage, found->second.typeID, result, instanceCount);
      if (status != OBELISK_RT_OK)
        return status;
      return writeResult(result, outPercentage, outCovered, outTotal);
    }
    return writeResult(queryInstance(found->second, type->second),
                       outPercentage, outCovered, outTotal);
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH(const std::length_error &) {
    return OBELISK_RT_OUT_OF_RESOURCES;
  }
}

extern "C" obelisk_rt_status obelisk_rt_v1_covergroup_type_query(
    obelisk_rt_context *context, uint64_t typeID, uint64_t itemID,
    double *outPercentage, int32_t *outCovered, int32_t *outTotal) {
  if (!context || !typeID || !outPercentage || !outCovered || !outTotal)
    return OBELISK_RT_INVALID_ARGUMENT;
  OBELISK_RT_TRY {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    CoverageState *coverage = coverageState(context, true);
    CoverageResult result;
    if (itemID) {
      obelisk_rt_status status =
          queryAllItemConfigurations(*coverage, typeID, itemID, result);
      return status == OBELISK_RT_OK
                 ? writeResult(result, outPercentage, outCovered, outTotal)
                 : status;
    }
    uint64_t instanceCount = 0;
    obelisk_rt_status status =
        queryAllTypeConfigurations(*coverage, typeID, result, instanceCount);
    if (status != OBELISK_RT_OK)
      return status;
    return writeResult(result, outPercentage, outCovered, outTotal);
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH(const std::length_error &) {
    return OBELISK_RT_OUT_OF_RESOURCES;
  }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_functional_coverage_get(obelisk_rt_context *context,
                                      double *outPercentage) {
  if (!context || !outPercentage)
    return OBELISK_RT_INVALID_ARGUMENT;
  OBELISK_RT_TRY {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    CoverageState *coverage = coverageState(context, true);
    std::set<uint64_t> typeIDs;
    for (const auto &[key, type] : coverage->types) {
      (void)type;
      typeIDs.insert(key.typeID);
    }
    if (coverage->schema)
      for (const auto &instance : coverage->schema->resolvedInstances)
        typeIDs.insert(instance.type);

    double weighted = 0.0;
    uint64_t totalWeight = 0;
    for (uint64_t typeID : typeIDs) {
      uint64_t instanceCount = 0;
      uint64_t typeWeight = 0;
      CoverageResult result;
      obelisk_rt_status status = queryAllTypeConfigurations(
          *coverage, typeID, result, instanceCount, &typeWeight);
      if (status != OBELISK_RT_OK)
        return status;
      if (!instanceCount || !result.contributes || !typeWeight)
        continue;
      weighted += result.percentage * static_cast<double>(typeWeight);
      totalWeight = saturatingAdd(totalWeight, typeWeight);
    }
    // IEEE 1800-2017 Tables 19-3 and 19-11: each covergroup type contributes
    // with type_option.weight. The global query is 100 when there are no
    // contributing nonzero-weight types.
    *outPercentage =
        totalWeight ? weighted / static_cast<double>(totalWeight) : 100.0;
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH(const std::length_error &) {
    return OBELISK_RT_OUT_OF_RESOURCES;
  }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_functional_coverage_set_db_name(obelisk_rt_context *context,
                                              obelisk_rt_string_v1 name) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  char scratch[8] = {};
  const char *bytes = nullptr;
  uint64_t size = 0;
  obelisk_rt_status status =
      obelisk_rt_v1_string_view(name, scratch, &bytes, &size);
  if (status != OBELISK_RT_OK)
    return status;
  if (size > SIZE_MAX)
    return OBELISK_RT_OUT_OF_RESOURCES;
  OBELISK_RT_TRY {
    std::string path(bytes, static_cast<size_t>(size));
    if (path.empty())
      return OBELISK_RT_INVALID_ARGUMENT;
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    CoverageState *coverage = coverageState(context, true);
    coverage->outputPath = std::move(path);
    coverage->outputExplicit = true;
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH(const std::length_error &) {
    return OBELISK_RT_OUT_OF_RESOURCES;
  }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_functional_coverage_load_db(obelisk_rt_context *context,
                                          obelisk_rt_string_v1 name) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  char scratch[8] = {};
  const char *bytes = nullptr;
  uint64_t size = 0;
  obelisk_rt_status status =
      obelisk_rt_v1_string_view(name, scratch, &bytes, &size);
  if (status != OBELISK_RT_OK)
    return status;
  if (size > SIZE_MAX)
    return OBELISK_RT_OUT_OF_RESOURCES;
  return obelisk_rt_v1_coverage_load(context, bytes, size);
}

extern "C" obelisk_rt_status obelisk_rt_v1_coverage_finalize(
    obelisk_rt_context *context, uint64_t lineCount, uint64_t toggleBitCount,
    const uint8_t *initialValue, const uint8_t *initialUnknown,
    uint32_t persistenceMask) {
  constexpr uint64_t maxToggleBits =
      static_cast<uint64_t>(SIZE_MAX / sizeof(uint64_t) / 4);
  if (!context || (toggleBitCount && (!initialValue || !initialUnknown)) ||
      (persistenceMask & ~OBELISK_RT_COVERAGE_PERSIST_ALL) != 0 ||
      lineCount >
          static_cast<uint64_t>(SIZE_MAX / sizeof(std::atomic<uint64_t>)) ||
      toggleBitCount > (UINT64_MAX - 7) / 8 || toggleBitCount > maxToggleBits)
    return OBELISK_RT_INVALID_ARGUMENT;
  OBELISK_RT_TRY {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    CoverageState *coverage = coverageState(context, true);
    if (coverage->finalized)
      return OBELISK_RT_INVALID_LIFECYCLE;
    static_assert(OBELISK_RT_COVERAGE_PERSIST_LINE ==
                  obelisk::coverage::RunContainsLine);
    static_assert(OBELISK_RT_COVERAGE_PERSIST_TOGGLE ==
                  obelisk::coverage::RunContainsToggle);
    static_assert(OBELISK_RT_COVERAGE_PERSIST_FUNCTIONAL ==
                  obelisk::coverage::RunContainsFunctional);
    coverage->persistedRunFlags = persistenceMask;
    if (coverage->schema) {
      uint64_t schemaToggleBits = 0;
      for (const obelisk::coverage::ToggleObject &object :
           coverage->schema->toggleObjects) {
        if (object.bitWidth > UINT64_MAX - schemaToggleBits)
          return OBELISK_RT_INVALID_DESIGN;
        schemaToggleBits += object.bitWidth;
      }
      if (lineCount != coverage->schema->linePoints.size() ||
          toggleBitCount != schemaToggleBits)
        return OBELISK_RT_INVALID_DESIGN;
    }
    if (lineCount) {
      coverage->lineCounters =
          std::make_unique<std::atomic<uint64_t>[]>(lineCount);
      coverage->lineOverflow =
          std::make_unique<std::atomic<uint8_t>[]>(lineCount);
      coverage->lineEnabled =
          std::make_unique<std::atomic<uint8_t>[]>(lineCount);
      for (uint64_t index = 0; index != lineCount; ++index) {
        coverage->lineCounters[index].store(0, std::memory_order_relaxed);
        coverage->lineOverflow[index].store(0, std::memory_order_relaxed);
        coverage->lineEnabled[index].store(1, std::memory_order_relaxed);
      }
    }
    coverage->lineCount = lineCount;
    coverage->lineExcluded.assign(static_cast<size_t>(lineCount), 0);
    coverage->toggleBitCount = toggleBitCount;
    coverage->toggleCounters.assign(static_cast<size_t>(toggleBitCount * 4), 0);
    coverage->toggleOverflow.assign(static_cast<size_t>(toggleBitCount * 4), 0);
    coverage->toggleEnabled.assign(static_cast<size_t>(toggleBitCount), 1);
    coverage->toggleBound.assign(static_cast<size_t>(toggleBitCount), 0);
    coverage->toggleExcluded.assign(static_cast<size_t>(toggleBitCount), 0);
    coverage->toggleBindings.clear();
    coverage->toggleBindingsSealed = false;
    uint64_t bytes = (toggleBitCount + 7) / 8;
    if (bytes) {
      coverage->toggleValue.assign(static_cast<size_t>(bytes), 0);
      coverage->toggleUnknown.assign(static_cast<size_t>(bytes), 0);
      std::copy(initialValue, initialValue + bytes,
                coverage->toggleValue.begin());
      std::copy(initialUnknown, initialUnknown + bytes,
                coverage->toggleUnknown.begin());
    }
    if (coverage->schema) {
      std::unordered_set<uint64_t> excludedLines;
      std::unordered_set<uint64_t> excludedToggles;
      for (const obelisk::coverage::Exclusion &exclusion :
           coverage->schema->exclusions) {
        if (exclusion.metric == obelisk::coverage::MetricKind::Line)
          excludedLines.insert(exclusion.entity);
        else if (exclusion.metric == obelisk::coverage::MetricKind::Toggle)
          excludedToggles.insert(exclusion.entity);
      }
      for (uint64_t index = 0; index != lineCount; ++index)
        coverage->lineExcluded[index] =
            excludedLines.count(coverage->schema->linePoints[index].id) != 0;
      uint64_t base = 0;
      for (const obelisk::coverage::ToggleObject &object :
           coverage->schema->toggleObjects) {
        if (excludedToggles.count(object.id))
          std::fill(coverage->toggleExcluded.begin() + base,
                    coverage->toggleExcluded.begin() + base + object.bitWidth,
                    1);
        base += object.bitWidth;
      }
      coverage->scopeParents.clear();
      coverage->definitionScopes.clear();
      for (const obelisk::coverage::Scope &scope : coverage->schema->scopes) {
        coverage->scopeParents.emplace(scope.id, scope.parent);
        if (!scope.definition.empty())
          coverage->definitionScopes[scope.definition].push_back(scope.id);
      }
      for (auto &entry : coverage->definitionScopes)
        std::sort(entry.second.begin(), entry.second.end());
    }
    coverage->finalized = true;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH(const std::length_error &) {
    return OBELISK_RT_OUT_OF_RESOURCES;
  }
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_coverage_point_hit(obelisk_rt_context *context, uint64_t point,
                                 uint32_t enabled) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  CoverageState *coverage = context->coverage.get();
  if (!coverage || !coverage->finalized)
    return OBELISK_RT_INVALID_LIFECYCLE;
  if (point >= coverage->lineCount)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (!enabled ||
      !coverage->lineEnabled[point].load(std::memory_order_relaxed) ||
      coverage->lineExcluded[point])
    return OBELISK_RT_OK;
  if (atomicSaturatingIncrement(coverage->lineCounters[point]))
    coverage->lineOverflow[point].store(1, std::memory_order_relaxed);
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status obelisk_rt_v1_coverage_toggle_transition(
    obelisk_rt_context *context, uint64_t firstBit, uint64_t bitCount,
    const uint8_t *value, const uint8_t *unknown) {
  if (!context || (bitCount && (!value || !unknown)))
    return OBELISK_RT_INVALID_ARGUMENT;
  std::lock_guard<std::recursive_mutex> lock(context->mutex);
  CoverageState *coverage = coverageState(context, false);
  if (!coverage || !coverage->finalized)
    return OBELISK_RT_INVALID_LIFECYCLE;
  if (firstBit > coverage->toggleBitCount ||
      bitCount > coverage->toggleBitCount - firstBit)
    return OBELISK_RT_INVALID_ARGUMENT;
  for (uint64_t local = 0; local != bitCount; ++local) {
    uint64_t index = firstBit + local;
    bool newValue = bit(value, local);
    bool newUnknown = bit(unknown, local);
    recordToggleBit(*coverage, index, newValue, newUnknown);
  }
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_coverage_toggle_seal(obelisk_rt_context *context) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  std::lock_guard<std::recursive_mutex> lock(context->mutex);
  CoverageState *coverage = coverageState(context, false);
  if (!coverage || !coverage->finalized || coverage->toggleBindingsSealed)
    return OBELISK_RT_INVALID_LIFECYCLE;
  for (uint64_t index = 0; index != coverage->toggleBitCount; ++index)
    if (!coverage->toggleExcluded[index] && !coverage->toggleBound[index])
      return OBELISK_RT_INVALID_DESIGN;
  coverage->toggleBindingsSealed = true;
  coverage->toggleBound.clear();
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_coverage_toggle_bind(obelisk_rt_context *context,
                                   uint64_t firstBit, uint64_t bitCount,
                                   uint64_t stateHandle) {
  if (!context || bitCount == 0)
    return OBELISK_RT_INVALID_ARGUMENT;
  std::lock_guard<std::recursive_mutex> lock(context->mutex);
  CoverageState *coverage = coverageState(context, false);
  if (!coverage || !coverage->finalized)
    return OBELISK_RT_INVALID_LIFECYCLE;
  if (coverage->toggleBindingsSealed)
    return OBELISK_RT_INVALID_LIFECYCLE;
  if (firstBit > coverage->toggleBitCount ||
      bitCount > coverage->toggleBitCount - firstBit)
    return OBELISK_RT_INVALID_ARGUMENT;
  uint32_t staticID = 0;
  int64_t signedLow = 0;
  if (!decodeNativeStatic(stateHandle, staticID, signedLow) || signedLow < 0)
    return OBELISK_RT_INVALID_HANDLE;
  const NativeStaticState *state = findNativeStaticState(context, staticID);
  uint64_t stateLow = static_cast<uint64_t>(signedLow);
  if (!state || stateLow > state->bitWidth ||
      bitCount > state->bitWidth - stateLow)
    return OBELISK_RT_INVALID_HANDLE;
  for (uint64_t index = firstBit; index != firstBit + bitCount; ++index)
    if (coverage->toggleBound[index])
      return OBELISK_RT_INVALID_DESIGN;
  std::fill(coverage->toggleBound.begin() + firstBit,
            coverage->toggleBound.begin() + firstBit + bitCount, 1);
  coverage->toggleBindings[staticID].push_back({firstBit, stateLow, bitCount});
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_coverage_reset(obelisk_rt_context *context, uint32_t metric,
                             const uint8_t *currentValue,
                             const uint8_t *currentUnknown) {
  if (!context || (metric != OBELISK_RT_COVERAGE_STATEMENT &&
                   metric != OBELISK_RT_COVERAGE_TOGGLE))
    return OBELISK_RT_INVALID_ARGUMENT;
  std::lock_guard<std::recursive_mutex> lock(context->mutex);
  CoverageState *coverage = coverageState(context, false);
  if (!coverage || !coverage->finalized)
    return OBELISK_RT_INVALID_LIFECYCLE;
  if (metric == OBELISK_RT_COVERAGE_STATEMENT) {
    for (uint64_t index = 0; index != coverage->lineCount; ++index) {
      coverage->lineCounters[index].store(0, std::memory_order_relaxed);
      coverage->lineOverflow[index].store(0, std::memory_order_relaxed);
    }
  } else {
    if (coverage->toggleBitCount && (!currentValue || !currentUnknown))
      return OBELISK_RT_INVALID_ARGUMENT;
    std::fill(coverage->toggleCounters.begin(), coverage->toggleCounters.end(),
              0);
    std::fill(coverage->toggleOverflow.begin(), coverage->toggleOverflow.end(),
              0);
    uint64_t bytes = (coverage->toggleBitCount + 7) / 8;
    if (bytes) {
      std::copy(currentValue, currentValue + bytes,
                coverage->toggleValue.begin());
      std::copy(currentUnknown, currentUnknown + bytes,
                coverage->toggleUnknown.begin());
    }
  }
  if (coverage->schema)
    coverage->schema->counters.erase(
        std::remove_if(coverage->schema->counters.begin(),
                       coverage->schema->counters.end(),
                       [&](const obelisk::coverage::Counter &counter) {
                         return static_cast<uint32_t>(counter.metric) == metric;
                       }),
        coverage->schema->counters.end());
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_coverage_query(obelisk_rt_context *context, uint32_t metric,
                             uint64_t *outCovered, uint64_t *outTotal,
                             double *outPercentage) {
  if (!context || !outCovered || !outTotal || !outPercentage ||
      (metric != OBELISK_RT_COVERAGE_STATEMENT &&
       metric != OBELISK_RT_COVERAGE_TOGGLE))
    return OBELISK_RT_INVALID_ARGUMENT;
  std::lock_guard<std::recursive_mutex> lock(context->mutex);
  CoverageState *coverage = coverageState(context, false);
  if (!coverage || !coverage->finalized)
    return OBELISK_RT_INVALID_LIFECYCLE;
  uint64_t covered = 0, total = 0;
  auto loaded = [&](obelisk::coverage::MetricKind loadedMetric, uint64_t entity,
                    uint32_t subindex) {
    uint64_t value = 0;
    if (!coverage->schema)
      return value;
    for (const obelisk::coverage::Counter &counter : coverage->schema->counters)
      if (counter.metric == loadedMetric && counter.entity == entity &&
          counter.subindex == subindex)
        value = saturatingAdd(value, counter.value);
    return value;
  };
  if (metric == OBELISK_RT_COVERAGE_STATEMENT) {
    for (uint64_t index = 0; index != coverage->lineCount; ++index) {
      if (coverage->lineExcluded[index])
        continue;
      ++total;
      uint64_t entity =
          coverage->schema ? coverage->schema->linePoints[index].id : index;
      covered +=
          coverage->lineCounters[index].load(std::memory_order_relaxed) != 0 ||
          loaded(obelisk::coverage::MetricKind::Line, entity, 0) != 0;
    }
  } else {
    if (!coverage->schema) {
      for (uint64_t index = 0; index != coverage->toggleBitCount; ++index) {
        total += 2;
        covered += coverage->toggleCounters[index * 4] != 0;
        covered += coverage->toggleCounters[index * 4 + 1] != 0;
      }
      *outCovered = covered;
      *outTotal = total;
      *outPercentage = total ? 100.0 * static_cast<double>(covered) /
                                   static_cast<double>(total)
                             : 0.0;
      return OBELISK_RT_OK;
    }
    uint64_t base = 0;
    for (const obelisk::coverage::ToggleObject &object :
         coverage->schema->toggleObjects) {
      for (uint64_t bitIndex = 0; bitIndex != object.bitWidth; ++bitIndex) {
        if (coverage->toggleExcluded[base + bitIndex])
          continue;
        total += 2;
        covered += coverage->toggleCounters[(base + bitIndex) * 4] != 0 ||
                   loaded(obelisk::coverage::MetricKind::Toggle, object.id,
                          static_cast<uint32_t>(bitIndex * 4)) != 0;
        covered += coverage->toggleCounters[(base + bitIndex) * 4 + 1] != 0 ||
                   loaded(obelisk::coverage::MetricKind::Toggle, object.id,
                          static_cast<uint32_t>(bitIndex * 4 + 1)) != 0;
      }
      base += object.bitWidth;
    }
  }
  *outCovered = covered;
  *outTotal = total;
  *outPercentage =
      total ? 100.0 * static_cast<double>(covered) / static_cast<double>(total)
            : 0.0;
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status obelisk_rt_v1_coverage_control_definition(
    obelisk_rt_context *context, int32_t control, int32_t coverageType,
    int32_t scopeDefinition, obelisk_rt_string_v1 definition,
    int32_t *outStatus) {
  if (!context || !outStatus)
    return OBELISK_RT_INVALID_ARGUMENT;
  char scratch[8] = {};
  const char *bytes = nullptr;
  uint64_t size = 0;
  obelisk_rt_status status =
      obelisk_rt_v1_string_view(definition, scratch, &bytes, &size);
  if (status != OBELISK_RT_OK)
    return status;
  if (size > SIZE_MAX)
    return OBELISK_RT_OUT_OF_RESOURCES;
  OBELISK_RT_TRY {
    std::string name =
        size ? std::string(bytes, static_cast<size_t>(size)) : std::string();
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    CoverageState *coverage = coverageState(context, false);
    if (!coverage || !coverage->finalized)
      return OBELISK_RT_INVALID_LIFECYCLE;
    auto found = coverage->definitionScopes.find(name);
    if (found == coverage->definitionScopes.end()) {
      *outStatus = SVCovError;
      return OBELISK_RT_OK;
    }
    *outStatus = controlSelectedCoverage(*coverage, control, coverageType,
                                         scopeDefinition, found->second);
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH(const std::length_error &) {
    return OBELISK_RT_OUT_OF_RESOURCES;
  }
}

extern "C" obelisk_rt_status obelisk_rt_v1_coverage_control_instance(
    obelisk_rt_context *context, int32_t control, int32_t coverageType,
    int32_t scopeDefinition, uint64_t coverageScopeID, int32_t *outStatus) {
  if (!context || !outStatus || !coverageScopeID)
    return OBELISK_RT_INVALID_ARGUMENT;
  OBELISK_RT_TRY {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    CoverageState *coverage = coverageState(context, false);
    if (!coverage || !coverage->finalized)
      return OBELISK_RT_INVALID_LIFECYCLE;
    if (!coverage->scopeParents.count(coverageScopeID)) {
      *outStatus = SVCovError;
      return OBELISK_RT_OK;
    }
    std::vector<uint64_t> roots{coverageScopeID};
    *outStatus = controlSelectedCoverage(*coverage, control, coverageType,
                                         scopeDefinition, roots);
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH(const std::length_error &) {
    return OBELISK_RT_OUT_OF_RESOURCES;
  }
}

extern "C" obelisk_rt_status obelisk_rt_v1_coverage_query_definition(
    obelisk_rt_context *context, int32_t coverageType, int32_t scopeDefinition,
    obelisk_rt_string_v1 definition, uint32_t maximum, int32_t *outValue) {
  if (!context || !outValue || maximum > 1)
    return OBELISK_RT_INVALID_ARGUMENT;
  char scratch[8] = {};
  const char *bytes = nullptr;
  uint64_t size = 0;
  obelisk_rt_status status =
      obelisk_rt_v1_string_view(definition, scratch, &bytes, &size);
  if (status != OBELISK_RT_OK)
    return status;
  if (size > SIZE_MAX)
    return OBELISK_RT_OUT_OF_RESOURCES;
  OBELISK_RT_TRY {
    std::string name =
        size ? std::string(bytes, static_cast<size_t>(size)) : std::string();
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    CoverageState *coverage = coverageState(context, false);
    if (!coverage || !coverage->finalized)
      return OBELISK_RT_INVALID_LIFECYCLE;
    auto found = coverage->definitionScopes.find(name);
    if (found == coverage->definitionScopes.end()) {
      *outValue = SVCovError;
      return OBELISK_RT_OK;
    }
    *outValue = querySelectedCoverage(*coverage, coverageType, scopeDefinition,
                                      found->second, maximum != 0);
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH(const std::length_error &) {
    return OBELISK_RT_OUT_OF_RESOURCES;
  }
}

extern "C" obelisk_rt_status obelisk_rt_v1_coverage_query_instance(
    obelisk_rt_context *context, int32_t coverageType, int32_t scopeDefinition,
    uint64_t coverageScopeID, uint32_t maximum, int32_t *outValue) {
  if (!context || !outValue || !coverageScopeID || maximum > 1)
    return OBELISK_RT_INVALID_ARGUMENT;
  OBELISK_RT_TRY {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    CoverageState *coverage = coverageState(context, false);
    if (!coverage || !coverage->finalized)
      return OBELISK_RT_INVALID_LIFECYCLE;
    if (!coverage->scopeParents.count(coverageScopeID)) {
      *outValue = SVCovError;
      return OBELISK_RT_OK;
    }
    std::vector<uint64_t> roots{coverageScopeID};
    *outValue = querySelectedCoverage(*coverage, coverageType, scopeDefinition,
                                      roots, maximum != 0);
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH(const std::length_error &) {
    return OBELISK_RT_OUT_OF_RESOURCES;
  }
}

extern "C" obelisk_rt_status obelisk_rt_v1_coverage_database_save(
    obelisk_rt_context *context, int32_t coverageType,
    obelisk_rt_string_v1 name, int32_t *outStatus) {
  if (!context || !outStatus)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outStatus = SVCovError;
  std::string path;
  FailedCoverageSaveCleanup cleanup{path};
  OBELISK_RT_TRY {
    obelisk_rt_status status = copyManagedCoverageString(name, path);
    if (status != OBELISK_RT_OK)
      return status;
    if (path.empty())
      return OBELISK_RT_OK;
    if (!isValidSVCoverageType(coverageType)) {
      cleanup.armed = true;
      return OBELISK_RT_OK;
    }
    std::optional<PersistentCoverageMetric> selected =
        persistentCoverageMetric(coverageType);
    if (!selected) {
      *outStatus = SVCovNoCoverage;
      return OBELISK_RT_OK;
    }
    cleanup.armed = true;

    obelisk::coverage::Database snapshot;
    {
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      CoverageState *coverage = coverageState(context, false);
      if (!coverage || !coverage->finalized)
        return OBELISK_RT_INVALID_LIFECYCLE;
      if (!(availableRunCoverage(*coverage) & selected->runFlag)) {
        cleanup.armed = false;
        *outStatus = SVCovNoCoverage;
        return OBELISK_RT_OK;
      }
      status =
          makeSnapshot(context, *coverage, 0, context->schedulerTime, snapshot);
      if (status != OBELISK_RT_OK)
        return status;
      retainPersistentMetric(snapshot, *selected);
    }
    obelisk::coverage::Diagnostic diagnostic;
    obelisk::coverage::Status writeStatus =
        obelisk::coverage::writeFileAtomically(path, snapshot, &diagnostic);
    if (writeStatus == obelisk::coverage::Status::OutOfMemory)
      return OBELISK_RT_OUT_OF_MEMORY;
    *outStatus =
        writeStatus == obelisk::coverage::Status::Ok ? SVCovOK : SVCovError;
    cleanup.armed = writeStatus != obelisk::coverage::Status::Ok;
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH(const std::length_error &) {
    return OBELISK_RT_OUT_OF_RESOURCES;
  }
}

extern "C" obelisk_rt_status obelisk_rt_v1_coverage_database_merge(
    obelisk_rt_context *context, int32_t coverageType,
    obelisk_rt_string_v1 name, int32_t *outStatus) {
  if (!context || !outStatus)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outStatus = SVCovError;
  OBELISK_RT_TRY {
    if (!isValidSVCoverageType(coverageType))
      return OBELISK_RT_OK;
    std::string path;
    obelisk_rt_status status = copyManagedCoverageString(name, path);
    if (status != OBELISK_RT_OK)
      return status;
    if (path.empty())
      return OBELISK_RT_OK;

    obelisk::coverage::Database incoming;
    obelisk::coverage::Diagnostic diagnostic;
    obelisk::coverage::Status readStatus =
        obelisk::coverage::readFile(path, incoming, {}, &diagnostic);
    if (readStatus == obelisk::coverage::Status::OutOfMemory)
      return OBELISK_RT_OUT_OF_MEMORY;
    if (readStatus != obelisk::coverage::Status::Ok)
      return OBELISK_RT_OK;

    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    CoverageState *coverage = coverageState(context, false);
    if (!coverage || !coverage->schema || !coverage->finalized)
      return OBELISK_RT_INVALID_LIFECYCLE;
    if (incoming.schemaFingerprint != coverage->schema->schemaFingerprint)
      return OBELISK_RT_OK;
    std::optional<PersistentCoverageMetric> selected =
        persistentCoverageMetric(coverageType);
    if (!selected || !(availableRunCoverage(*coverage) & selected->runFlag)) {
      *outStatus = SVCovNoCoverage;
      return OBELISK_RT_OK;
    }
    bool containsMetric =
        std::any_of(incoming.runs.begin(), incoming.runs.end(),
                    [&](const obelisk::coverage::Run &run) {
                      return (run.flags & selected->runFlag) != 0;
                    });
    if (!containsMetric) {
      *outStatus = SVCovNoCoverage;
      return OBELISK_RT_OK;
    }
    bool currentRunInitialized =
        std::any_of(coverage->runUUID.begin(), coverage->runUUID.end(),
                    [](uint8_t byte) { return byte != 0; });
    if (currentRunInitialized &&
        std::any_of(incoming.runs.begin(), incoming.runs.end(),
                    [&](const obelisk::coverage::Run &run) {
                      return run.uuid == coverage->runUUID &&
                             (run.flags & selected->runFlag) != 0;
                    }))
      return OBELISK_RT_OK;
    retainPersistentMetric(incoming, *selected);
    obelisk::coverage::Database combined = *coverage->schema;
    obelisk::coverage::Status canonicalStatus =
        obelisk::coverage::canonicalizeFunctionalConfigurations(combined,
                                                                &diagnostic);
    if (canonicalStatus != obelisk::coverage::Status::Ok)
      return mapCoverageStatus(canonicalStatus);
    obelisk::coverage::Status mergeStatus =
        mergePersistentMetricSlice(combined, incoming, *selected);
    if (mergeStatus == obelisk::coverage::Status::OutOfMemory)
      return OBELISK_RT_OUT_OF_MEMORY;
    if (mergeStatus != obelisk::coverage::Status::Ok)
      return OBELISK_RT_OK;
    coverage->schema =
        std::make_unique<obelisk::coverage::Database>(std::move(combined));
    coverage->constructionSchemas.reset();
    coverage->loadedConfigurationSchemas.reset();
    coverage->loadedData.reset();
    coverage->configurationIndexSchema = nullptr;
    *outStatus = SVCovOK;
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH(const std::length_error &) {
    return OBELISK_RT_OUT_OF_RESOURCES;
  }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_coverage_load(obelisk_rt_context *context, const char *path,
                            uint64_t pathSize) {
  if (!context || !path || !pathSize || pathSize > SIZE_MAX)
    return OBELISK_RT_INVALID_ARGUMENT;
  OBELISK_RT_TRY {
    std::string pathText(path, static_cast<size_t>(pathSize));
    obelisk::coverage::Database incoming;
    obelisk::coverage::Diagnostic diagnostic;
    obelisk::coverage::Status status =
        obelisk::coverage::readFile(pathText, incoming, {}, &diagnostic);
    if (status != obelisk::coverage::Status::Ok)
      return mapCoverageStatus(status);
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    CoverageState *coverage = coverageState(context, true);
    if (coverage->finalized) {
      uint64_t incomingLines = 0, incomingToggleBits = 0;
      if (!schemaCounts(incoming, incomingLines, incomingToggleBits) ||
          incomingLines != coverage->lineCount ||
          incomingToggleBits != coverage->toggleBitCount)
        return OBELISK_RT_INVALID_DESIGN;
    }
    obelisk::coverage::Database combined;
    if (coverage->schema) {
      combined = *coverage->schema;
      status = obelisk::coverage::canonicalizeFunctionalConfigurations(
          combined, &diagnostic);
      if (status != obelisk::coverage::Status::Ok)
        return mapCoverageStatus(status);
      status = obelisk::coverage::merge(combined, incoming, false, &diagnostic);
      if (status != obelisk::coverage::Status::Ok) {
        std::fprintf(stderr, "error: coverage load failed%s%s%s%s\n",
                     diagnostic.field ? " in " : "",
                     diagnostic.field ? diagnostic.field : "",
                     diagnostic.detail.empty() ? "" : ": ",
                     diagnostic.detail.empty() ? ""
                                               : diagnostic.detail.c_str());
        return mapCoverageStatus(status);
      }
    } else {
      combined = std::move(incoming);
    }
    // Live instances are bound to an exact resolved configuration. Loading a
    // database while they exist may add runs but must not replace that schema.
    IndexSet<FunctionalCoverageTypeKey> retainedConfigurations;
    for (const auto &row : combined.functionalConfigurations)
      retainedConfigurations.insert({row.type, row.configuration});
    for (const auto &boundType : coverage->types) {
      if (!retainedConfigurations.count(boundType.first))
        return OBELISK_RT_INVALID_DESIGN;
    }
    auto committedSchema =
        std::make_unique<obelisk::coverage::Database>(std::move(combined));
    coverage->schema = std::move(committedSchema);
    coverage->constructionSchemas.reset();
    coverage->loadedConfigurationSchemas.reset();
    coverage->loadedData.reset();
    coverage->configurationIndexSchema = nullptr;
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH(const std::length_error &) {
    return OBELISK_RT_OUT_OF_RESOURCES;
  }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_coverage_save(obelisk_rt_context *context, const char *path,
                            uint64_t pathSize, uint32_t runStatus,
                            uint64_t simulationTime) {
  if (!context || !path || !pathSize || pathSize > SIZE_MAX)
    return OBELISK_RT_INVALID_ARGUMENT;
  OBELISK_RT_TRY {
    std::string pathText(path, static_cast<size_t>(pathSize));
    obelisk::coverage::Database snapshot;
    {
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      CoverageState *coverage = coverageState(context, false);
      if (!coverage) {
        setLastErrorUnlocked(
            context, "failed to create coverage snapshot for '" + pathText +
                         "': " +
                         obelisk_rt_v1_status_string(
                             OBELISK_RT_INVALID_LIFECYCLE));
        return OBELISK_RT_INVALID_LIFECYCLE;
      }
      obelisk_rt_status status =
          makeSnapshot(context, *coverage, runStatus, simulationTime, snapshot);
      if (status != OBELISK_RT_OK) {
        setLastErrorUnlocked(context,
                             "failed to create coverage snapshot for '" +
                                 pathText + "': " +
                                 obelisk_rt_v1_status_string(status));
        return status;
      }
    }
    obelisk::coverage::Diagnostic diagnostic;
    obelisk::coverage::Status writeStatus =
        obelisk::coverage::writeFileAtomically(pathText, snapshot, &diagnostic);
    if (writeStatus != obelisk::coverage::Status::Ok) {
      std::string message = "failed to write coverage database '" + pathText +
                            "': " +
                            obelisk::coverage::statusName(writeStatus);
      if (diagnostic.field) {
        message += " (";
        message += diagnostic.field;
        if (!diagnostic.detail.empty()) {
          message += ": ";
          message += diagnostic.detail;
        }
        message += ')';
      }
      setLastError(context, std::move(message));
    }
    return mapCoverageStatus(writeStatus);
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) {
    OBELISK_RT_TRY {
      setLastError(context,
                   "failed to create coverage snapshot: runtime allocation "
                   "failed");
    }
    OBELISK_RT_CATCH_ALL {}
    return OBELISK_RT_OUT_OF_MEMORY;
  }
  OBELISK_RT_CATCH(const std::length_error &) {
    OBELISK_RT_TRY {
      setLastError(context,
                   "failed to create coverage snapshot: resource limit "
                   "exceeded");
    }
    OBELISK_RT_CATCH_ALL {}
    return OBELISK_RT_OUT_OF_RESOURCES;
  }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_coverage_snapshot(obelisk_rt_context *context, uint32_t runStatus,
                                uint64_t simulationTime) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  OBELISK_RT_TRY {
    std::string output;
    {
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      CoverageState *coverage = coverageState(context, false);
      if (!coverage || coverage->dumpSuppressed)
        return OBELISK_RT_OK;
      output = expandOutputPath(*coverage);
    }
    return obelisk_rt_v1_coverage_save(context, output.data(), output.size(),
                                       runStatus, simulationTime);
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) {
    OBELISK_RT_TRY {
      setLastError(context,
                   "failed to prepare coverage output path: runtime "
                   "allocation failed");
    }
    OBELISK_RT_CATCH_ALL {}
    return OBELISK_RT_OUT_OF_MEMORY;
  }
  OBELISK_RT_CATCH(const std::length_error &) {
    OBELISK_RT_TRY {
      setLastError(context,
                   "failed to prepare coverage output path: resource limit "
                   "exceeded");
    }
    OBELISK_RT_CATCH_ALL {}
    return OBELISK_RT_OUT_OF_RESOURCES;
  }
}
