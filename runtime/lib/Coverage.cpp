//===- Coverage.cpp - Context-local functional coverage state ------------===//

#include "RuntimeInternal.h"

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

namespace {

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
  if (coverage.schema) {
    for (const FunctionalBin &bin : coverage.schema->functionalBins) {
      bool excluded = std::any_of(
          coverage.schema->exclusions.begin(),
          coverage.schema->exclusions.end(), [&](const Exclusion &entry) {
            return entry.metric == MetricKind::Functional &&
                   entry.entity == bin.id;
          });
      if (!excluded) {
        flags |= RunContainsFunctional;
        break;
      }
    }
    for (const ResolvedFunctionalBin &bin :
         coverage.schema->resolvedFunctionalBins) {
      bool excluded = std::any_of(
          coverage.schema->exclusions.begin(),
          coverage.schema->exclusions.end(), [&](const Exclusion &entry) {
            return entry.metric == MetricKind::Functional &&
                   (entry.entity == bin.id ||
                    (bin.templateBin && entry.entity == bin.templateBin));
          });
      if (!excluded) {
        flags |= RunContainsFunctional;
        break;
      }
    }
  }
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

  for (const Run &run : source.runs) {
    if (run.flags != selected.runFlag)
      return Status::InvalidDatabase;
    auto existing =
        std::lower_bound(result.runs.begin(), result.runs.end(), run.uuid,
                         [](const Run &candidate, const UUID &uuid) {
                           return candidate.uuid < uuid;
                         });
    if (existing == result.runs.end() || existing->uuid != run.uuid) {
      result.runs.insert(existing, run);
      continue;
    }

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
  std::sort(result.counters.begin(), result.counters.end(),
            [](const Counter &left, const Counter &right) {
              return std::tie(left.run, left.metric, left.entity, left.instance,
                              left.subindex) <
                     std::tie(right.run, right.metric, right.entity,
                              right.instance, right.subindex);
            });
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

obelisk_rt_status makeSnapshot(obelisk_rt_context *context,
                               CoverageState &coverage, uint32_t runStatus,
                               uint64_t simulationTime,
                               obelisk::coverage::Database &result) {
  if (!coverage.schema)
    return OBELISK_RT_INVALID_LIFECYCLE;
  result = *coverage.schema;
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
  std::sort(run.tags.begin(), run.tags.end());
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
    using Owner =
        obelisk::coverage::FunctionalConfigurationOptionOwnerKind;
    using Option = obelisk::coverage::FunctionalConfigurationOptionKind;
    using ValueKind = obelisk::coverage::FunctionalConfigurationValueKind;
    auto appendTypeUnsignedOption = [&](uint64_t owner, Owner ownerKind,
                                        Option option, uint64_t value) {
      result.resolvedInstanceOptions.push_back(
          {run.uuid, 0, owner, ownerKind, option, ValueKind::Unsigned, {},
           value, 0});
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
      if (auto comment = options.comments.find(0);
          comment != options.comments.end())
        result.resolvedInstanceOptions.push_back(
            {run.uuid, 0, typeID, Owner::Group, Option::Comment,
             ValueKind::String, comment->second, 0, 0});
      for (const auto &[item, value] : options.itemGoals)
        appendTypeUnsignedOption(item, Owner::Item, Option::Goal, value);
      for (const auto &[item, value] : options.itemWeights)
        appendTypeUnsignedOption(item, Owner::Item, Option::Weight, value);
      for (const auto &[item, value] : options.comments)
        if (item)
          result.resolvedInstanceOptions.push_back(
              {run.uuid, 0, item, Owner::Item, Option::Comment,
               ValueKind::String, value, 0, 0});
    }
    std::vector<uint64_t> instanceHandles;
    instanceHandles.reserve(coverage.instances.size());
    for (const auto &entry : coverage.instances)
      instanceHandles.push_back(entry.first);
    std::sort(instanceHandles.begin(), instanceHandles.end());
    for (uint64_t handle : instanceHandles) {
      const FunctionalCoverageInstanceState &instance =
          coverage.instances.at(handle);
      auto type =
          coverage.types.find({instance.typeID, instance.configuration});
      if (type == coverage.types.end())
        continue;
      result.resolvedInstances.push_back(
          {run.uuid, instance.typeID, handle, instance.name,
           instance.generatedName
               ? obelisk::coverage::ResolvedInstanceGeneratedName
               : 0,
           instance.configuration});
      auto appendUnsignedOption = [&](uint64_t owner, Owner ownerKind,
                                      Option option, uint64_t value) {
        result.resolvedInstanceOptions.push_back(
            {run.uuid, handle, owner, ownerKind, option, ValueKind::Unsigned,
             {}, value, 0});
      };
      appendUnsignedOption(0, Owner::Group, Option::Goal,
                           instance.instanceGoal);
      appendUnsignedOption(0, Owner::Group, Option::Weight,
                           instance.instanceWeight);
      appendUnsignedOption(0, Owner::Group, Option::AtLeast,
                           instance.groupAtLeast);
      appendUnsignedOption(0, Owner::Group, Option::CrossNumPrintMissing,
                           instance.groupCrossNumPrintMissing);
      if (auto comment = instance.comments.find(0);
          comment != instance.comments.end())
        result.resolvedInstanceOptions.push_back(
            {run.uuid, handle, 0, Owner::Group, Option::Comment,
             ValueKind::String, comment->second, 0, 0});
      if (instance.itemGoals.size() != type->second.items.size() ||
          instance.itemWeights.size() != type->second.items.size() ||
          instance.itemAtLeast.size() != type->second.items.size())
        return OBELISK_RT_INVALID_DESIGN;
      for (size_t itemIndex = 0; itemIndex != type->second.items.size();
           ++itemIndex) {
        uint64_t resolvedID = type->second.items[itemIndex];
        auto resolved = std::find_if(
            result.resolvedFunctionalItems.begin(),
            result.resolvedFunctionalItems.end(), [&](const auto &item) {
              return item.type == instance.typeID &&
                     item.configuration == instance.configuration &&
                     item.id == resolvedID;
            });
        if (resolved == result.resolvedFunctionalItems.end())
          return OBELISK_RT_INVALID_DESIGN;
        appendUnsignedOption(resolved->templateItem, Owner::Item, Option::Goal,
                             instance.itemGoals[itemIndex]);
        appendUnsignedOption(resolved->templateItem, Owner::Item,
                             Option::Weight, instance.itemWeights[itemIndex]);
        appendUnsignedOption(resolved->templateItem, Owner::Item,
                             Option::AtLeast,
                             instance.itemAtLeast[itemIndex]);
        if (instance.explicitCrossNumPrintMissingItems.count(
                resolved->templateItem)) {
          auto printMissing =
              instance.crossNumPrintMissing.find(resolved->templateItem);
          if (resolved->kind !=
                  obelisk::coverage::FunctionalItemKind::Cross ||
              printMissing == instance.crossNumPrintMissing.end())
            return OBELISK_RT_INVALID_DESIGN;
          appendUnsignedOption(resolved->templateItem, Owner::Item,
                               Option::CrossNumPrintMissing,
                               printMissing->second);
        }
        if (auto comment = instance.comments.find(resolved->templateItem);
            comment != instance.comments.end())
          result.resolvedInstanceOptions.push_back(
              {run.uuid, handle, resolved->templateItem, Owner::Item,
               Option::Comment, ValueKind::String, comment->second, 0, 0});
      }
      if (instance.bins.size() != type->second.bins.size())
        return OBELISK_RT_INVALID_DESIGN;
      for (const FunctionalCoverageBinState &counter : instance.bins)
        result.counters.push_back(
            {run.uuid, obelisk::coverage::MetricKind::Functional, counter.id,
             handle, 0, counter.overflow ? 1u : 0u, counter.count});
      for (const auto &crossEntry : instance.crossTuples) {
        const uint64_t resolvedCross = crossEntry.first;
        const auto &tuples = crossEntry.second;
        auto cross = std::find_if(
            type->second.crosses.begin(), type->second.crosses.end(),
            [&](const auto &entry) { return entry.id == resolvedCross; });
        if (cross == type->second.crosses.end())
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
              {run.uuid, handle, cross->templateItem, first,
               static_cast<uint32_t>(components.size()), counter.count,
               counter.overflow ? obelisk::coverage::SparseCrossTupleOverflow
                                : 0});
        }
      }
    }
  }
  std::sort(result.runs.begin(), result.runs.end(),
            [](const auto &left, const auto &right) {
              return left.uuid < right.uuid;
            });
  std::sort(result.counters.begin(), result.counters.end(),
            [](const auto &left, const auto &right) {
              return std::tie(left.run, left.metric, left.entity, left.instance,
                              left.subindex) <
                     std::tie(right.run, right.metric, right.entity,
                              right.instance, right.subindex);
            });
  std::sort(result.resolvedInstances.begin(), result.resolvedInstances.end(),
            [](const auto &left, const auto &right) {
              return std::tie(left.run, left.id) <
                     std::tie(right.run, right.id);
            });
  std::sort(result.resolvedInstanceOptions.begin(),
            result.resolvedInstanceOptions.end(),
            [](const auto &left, const auto &right) {
              return std::tie(left.run, left.instance, left.ownerKind,
                              left.owner, left.option) <
                     std::tie(right.run, right.instance, right.ownerKind,
                              right.owner, right.option);
            });
  std::sort(result.illegalBinDiagnostics.begin(),
            result.illegalBinDiagnostics.end(),
            [](const auto &left, const auto &right) {
              return std::tie(left.run, left.instance, left.bin,
                              left.simulationTime) <
                     std::tie(right.run, right.instance, right.bin,
                              right.simulationTime);
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
  std::sort(sparse.begin(), sparse.end(),
            [](const auto &left, const auto &right) {
              return std::tie(left.tuple.run, left.tuple.instance,
                              left.tuple.cross, left.components) <
                     std::tie(right.tuple.run, right.tuple.instance,
                              right.tuple.cross, right.components);
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

uint64_t ownerType(const obelisk::coverage::Database &schema,
                   const obelisk::coverage::FunctionalExpression &expression);
void markStaticallyEmptyStateBins(
    obelisk::coverage::Database &schema, uint64_t typeID,
    const obelisk::coverage::Digest &configuration);
obelisk_rt_status
deriveTransitionExclusions(const obelisk::coverage::Database &schema,
                           uint64_t typeID,
                           const obelisk::coverage::Digest &configuration,
                           std::unordered_set<uint64_t> &suppressedAlternatives,
                           std::unordered_set<uint64_t> &emptyBins);
bool functionalBooleanTrue(const obelisk_rt_functional_value_v1 &value);

uint64_t resolvedFunctionalID(const char *nameSpace, uint64_t typeID,
                              uint64_t templateID, uint32_t ordinal,
                              const std::unordered_set<uint64_t> &occupied) {
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

/// Compute an exact union cardinality, saturating only after the prefix proof
/// establishes that at least `cap` values exist. The search budget may still
/// reject an adversarial fragmented union, but resource exhaustion is never
/// guessed to mean that the threshold was reached.
obelisk_rt_status integralShapeUnionCardinalityCapped(
    const std::vector<IntegralCoverageShape> &shapes, uint32_t bitWidth,
    uint64_t cap, uint64_t &result) {
  struct Frame {
    FunctionalMagnitude prefix;
    int64_t remainingBit = -1;
    std::vector<size_t> candidates;
  };
  result = 0;
  if (!cap || !bitWidth)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (shapes.empty())
    return OBELISK_RT_OK;
  const uint64_t limit = obelisk::coverage::ParseLimits{}.maxRecords;
  Frame root;
  root.prefix.resize(functionalLimbCount(bitWidth));
  root.remainingBit = static_cast<int64_t>(bitWidth) - 1;
  root.candidates.resize(shapes.size());
  std::iota(root.candidates.begin(), root.candidates.end(), size_t{0});
  std::vector<Frame> search;
  search.push_back(std::move(root));
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
      const uint64_t bits = frame.remainingBit < 0
                                ? 0
                                : static_cast<uint64_t>(frame.remainingBit) + 1;
      if (bits >= 64 || (uint64_t{1} << bits) >= cap - result) {
        result = cap;
        return OBELISK_RT_OK;
      }
      result += uint64_t{1} << bits;
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
  return OBELISK_RT_OK;
}

bool resolvedFunctionalValueSetsIntersect(
    const obelisk::coverage::Database &schema,
    const obelisk::coverage::ResolvedFunctionalValueSet &left,
    const obelisk::coverage::ResolvedFunctionalValueSet &right);
obelisk_rt_status resolvedFunctionalBinIntersectsValueSet(
    const obelisk::coverage::Database &schema, uint64_t type,
    const obelisk::coverage::Digest &configuration, uint64_t bin,
    const obelisk::coverage::ResolvedFunctionalValueSet &selection,
    const std::unordered_set<uint64_t> &suppressedTransitionAlternatives,
    bool &result);
bool crossSelectorUsesWith(const obelisk::coverage::Database &schema,
                           const obelisk::coverage::CrossSelectorNode &root);
bool crossSelectorIsWithWrappedSet(
    const obelisk::coverage::Database &schema,
    const obelisk::coverage::CrossSelectorNode &root);
bool crossSelectorUsesSet(const obelisk::coverage::Database &schema,
                          const obelisk::coverage::CrossSelectorNode &root);
obelisk_rt_status resolvedCrossSelectorSelectsBinTuple(
    const obelisk::coverage::Database &schema, uint64_t type,
    const obelisk::coverage::Digest &configuration, uint64_t resolvedCross,
    const obelisk::coverage::CrossSelectorNode &root,
    const std::vector<uint64_t> &binTuple,
    const std::unordered_set<uint64_t> &suppressedTransitionAlternatives,
    bool &result);
obelisk_rt_status resolvedCrossSelectorSetCandidates(
    const obelisk::coverage::Database &schema, uint64_t type,
    const obelisk::coverage::Digest &configuration, uint64_t resolvedCross,
    const obelisk::coverage::CrossSelectorNode &root,
    const std::vector<std::vector<uint64_t>> &targetBins,
    const std::unordered_set<uint64_t> &suppressedTransitionAlternatives,
    std::vector<std::vector<uint64_t>> &result);
obelisk_rt_status
emitFunctionalOverlapWarnings(const obelisk::coverage::Database &schema,
                              uint64_t type,
                              const obelisk::coverage::Digest &configuration);

obelisk_rt_status resolveStaticFunctionalType(
    obelisk_rt_context *context, obelisk::coverage::Database &schema,
    uint64_t typeID, const obelisk_rt_functional_value_v1 *expressions,
    uint64_t expressionCount,
    obelisk::coverage::Digest &resolvedConfiguration) {
  using namespace obelisk::coverage;
  std::unordered_set<uint64_t> itemIDs, binIDs, valueSetIDs;
  for (const auto &item : schema.functionalItems)
    itemIDs.insert(item.id);
  for (const auto &bin : schema.functionalBins)
    binIDs.insert(bin.id);
  for (const auto &set : schema.functionalValueSets)
    valueSetIDs.insert(set.id);

  std::vector<const FunctionalItem *> templateItems;
  for (const auto &item : schema.functionalItems)
    if (item.type == typeID)
      templateItems.push_back(&item);
  std::sort(templateItems.begin(), templateItems.end(),
            [](const auto *a, const auto *b) {
              return std::tie(a->ordinal, a->id) < std::tie(b->ordinal, b->id);
            });

  std::vector<ResolvedFunctionalItem> resolvedItemRows;
  std::unordered_map<uint64_t, uint64_t> resolvedItems;
  for (const FunctionalItem *item : templateItems) {
    uint64_t id = resolvedFunctionalID("functional.resolved.item", typeID,
                                       item->id, 0, itemIDs);
    if (!id)
      return OBELISK_RT_INVALID_DESIGN;
    itemIDs.insert(id);
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
  std::sort(
      templateBins.begin(), templateBins.end(),
      [&](const auto *a, const auto *b) {
        auto itemOrdinal = [&](uint64_t id) {
          auto found =
              std::find_if(templateItems.begin(), templateItems.end(),
                           [&](const auto *item) { return item->id == id; });
          return found == templateItems.end() ? UINT32_MAX : (*found)->ordinal;
        };
        return std::make_tuple(itemOrdinal(a->item), a->ordinal, a->id) <
               std::make_tuple(itemOrdinal(b->item), b->ordinal, b->id);
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
  std::unordered_map<uint64_t, uint32_t> nextResolvedBinOrdinal;
  std::unordered_set<uint64_t> transitionAlternativeIDs;
  auto expressionValue =
      [&](uint64_t id) -> const obelisk_rt_functional_value_v1 * {
    for (uint64_t index = 0; index != expressionCount; ++index)
      if (expressions[index].id == id)
        return &expressions[index];
    return nullptr;
  };
  auto expressionSchema = [&](uint64_t id) -> const FunctionalExpression * {
    auto found = std::find_if(
        schema.functionalExpressions.begin(),
        schema.functionalExpressions.end(),
        [&](const FunctionalExpression &entry) { return entry.id == id; });
    return found == schema.functionalExpressions.end() ? nullptr : &*found;
  };
  std::vector<FunctionalConfigurationOption> configurationOptionRows;
  std::unordered_map<uint64_t, uint64_t> itemAtLeast;
  std::unordered_map<uint64_t, uint64_t> itemAutoBinMax;
  std::unordered_map<uint64_t, bool> itemCrossRetainAutoBins;
  std::unordered_map<uint64_t, double> itemRealInterval;
  std::optional<uint64_t> groupAtLeast;
  std::optional<uint64_t> groupAutoBinMax;
  std::optional<bool> groupCrossRetainAutoBins;
  bool groupDistributeFirst = false;
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
      if (!expression || !encoded ||
          expression->resultKind != FunctionalExpressionResultKind::Real ||
          encoded->kind != OBELISK_RT_FUNCTIONAL_VALUE_REAL ||
          encoded->value_size != sizeof(double) || !encoded->value)
        return OBELISK_RT_INVALID_DESIGN;
      double interval = 0.0;
      uint64_t intervalBits = 0;
      std::memcpy(&interval, encoded->value, sizeof(interval));
      std::memcpy(&intervalBits, encoded->value, sizeof(intervalBits));
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
    auto item = std::find_if(resolvedItemRows.begin(), resolvedItemRows.end(),
                             [&](const auto &candidate) {
                               return candidate.id == resolvedOwner->second;
                             });
    if (item == resolvedItemRows.end())
      return OBELISK_RT_INVALID_DESIGN;
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
      auto plan = std::find_if(
          schema.functionalBinPlans.begin(), schema.functionalBinPlans.end(),
          [&](const auto &entry) { return entry.bin == bin->id; });
      auto program = std::find_if(
          schema.transitionPrograms.begin(), schema.transitionPrograms.end(),
          [&](const auto &entry) { return entry.bin == bin->id; });
      const bool defaultSequence =
          (bin->flags & FunctionalBinDefaultSequence) != 0;
      const bool scalar = plan != schema.functionalBinPlans.end() &&
                          plan->arrayMode == FunctionalBinArrayMode::Scalar;
      const bool multiple = plan != schema.functionalBinPlans.end() &&
                            plan->arrayMode == FunctionalBinArrayMode::Unsized;
      if (plan == schema.functionalBinPlans.end() ||
          program == schema.transitionPrograms.end() || plan->valueSet ||
          (!scalar && !multiple) || plan->cardinalityExpression ||
          plan->arrayCardinality ||
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
            auto set = std::find_if(schema.functionalValueSets.begin(),
                                    schema.functionalValueSets.end(),
                                    [&](const auto &entry) {
                                      return entry.id == templateStep.valueSet;
                                    });
            if (set == schema.functionalValueSets.end() ||
                set->kind != FunctionalValueSetKind::Integral ||
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
          auto set = std::find_if(schema.functionalValueSets.begin(),
                                  schema.functionalValueSets.end(),
                                  [&](const auto &entry) {
                                    return entry.id == templateStep.valueSet;
                                  });
          if (set == schema.functionalValueSets.end() ||
              set->kind != FunctionalValueSetKind::Integral ||
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
    auto plan = std::find_if(
        schema.functionalBinPlans.begin(), schema.functionalBinPlans.end(),
        [&](const auto &entry) { return entry.bin == bin->id; });
    const bool isDefault = (bin->flags & FunctionalBinDefault) != 0;
    if (plan == schema.functionalBinPlans.end() ||
        (isDefault ? plan->valueSet != 0 : plan->valueSet == 0) ||
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
    std::vector<const FunctionalExpression *> withExpressions;
    for (const FunctionalExpression &expression : schema.functionalExpressions)
      if (expression.owner == bin->id &&
          expression.ownerKind == FunctionalExpressionOwnerKind::Bin &&
          expression.role == FunctionalExpressionRole::BinWith)
        withExpressions.push_back(&expression);
    std::sort(withExpressions.begin(), withExpressions.end(),
              [](const auto *lhs, const auto *rhs) {
                return lhs->ownerOrdinal < rhs->ownerOrdinal;
              });
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
      auto set = std::find_if(
          schema.functionalValueSets.begin(), schema.functionalValueSets.end(),
          [&](const auto &entry) { return entry.id == plan->valueSet; });
      if (set == schema.functionalValueSets.end() ||
          (set->flags & ~FunctionalValueSetNeedsResolution))
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
          value.kind = sourceReal ? OBELISK_RT_FUNCTIONAL_VALUE_REAL
                       : sourceFourState
                           ? OBELISK_RT_FUNCTIONAL_VALUE_FOUR_STATE
                           : OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL;
          return value;
        };
        auto decodeReal = [&](const obelisk_rt_functional_value_v1 &value,
                              double &result) {
          if (value.bit_width == 32 && value.value_size == sizeof(float)) {
            float source = 0.0f;
            std::memcpy(&source, value.value, sizeof(source));
            result = source;
            return true;
          }
          if (value.bit_width == 64 && value.value_size == sizeof(double)) {
            std::memcpy(&result, value.value, sizeof(result));
            return true;
          }
          return false;
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
              if ((!lowerUnbounded &&
                   low->kind != OBELISK_RT_FUNCTIONAL_VALUE_REAL) ||
                  (!upperUnbounded &&
                   high->kind != OBELISK_RT_FUNCTIONAL_VALUE_REAL) ||
                  atom.kind != FunctionalValueAtomKind::RealInterval)
                return OBELISK_RT_INVALID_DESIGN;
              uint64_t lowBits = 0, highBits = 0;
              double lowValue = 0.0, highValue = 0.0;
              if (!lowerUnbounded)
                std::memcpy(&lowValue, low->value, sizeof(lowValue));
              if (!upperUnbounded && atom.upperExpression)
                std::memcpy(&highValue, high->value, sizeof(highValue));
              else if (!upperUnbounded)
                highValue = lowValue;
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
                (!lowerUnbounded &&
                 low->kind == OBELISK_RT_FUNCTIONAL_VALUE_REAL) ||
                (!upperUnbounded &&
                 high->kind == OBELISK_RT_FUNCTIONAL_VALUE_REAL))
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
          {bin, &*plan, item->second, binID,
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

  for (const PendingDefaultArray &pendingDefault : pendingDefaultArrays) {
    const ResolvedFunctionalItem *resolvedItem = nullptr;
    for (const auto &candidate : resolvedItemRows)
      if (candidate.id == pendingDefault.item) {
        resolvedItem = &candidate;
        break;
      }
    const FunctionalExpression *sampleExpression = nullptr;
    if (resolvedItem)
      for (const auto &expression : schema.functionalExpressions)
        if (expression.owner == resolvedItem->templateItem &&
            expression.ownerKind == FunctionalExpressionOwnerKind::Item &&
            expression.role == FunctionalExpressionRole::CoverpointSample) {
          sampleExpression = &expression;
          break;
        }
    if (!resolvedItem || !sampleExpression ||
        sampleExpression->resultKind == FunctionalExpressionResultKind::Real ||
        !sampleExpression->bitWidth ||
        (sampleExpression->signedness != CoverageSignedness::Signed &&
         sampleExpression->signedness != CoverageSignedness::Unsigned))
      return OBELISK_RT_INVALID_DESIGN;

    const uint32_t bitWidth = sampleExpression->bitWidth;
    const uint32_t limbCount = functionalLimbCount(bitWidth);
    const auto signedness = sampleExpression->signedness;
    std::vector<IntegralCoverageShape> associatedShapes;
    for (const PendingSet &set : pendingSets) {
      if (set.set.item != pendingDefault.item ||
          set.set.kind != FunctionalValueSetKind::Integral ||
          set.set.bitWidth != bitWidth || set.set.signedness != signedness)
        continue;
      auto owner = std::find_if(resolvedBinRows.begin(), resolvedBinRows.end(),
                                [&](const auto &candidate) {
                                  return candidate.id == set.set.ownerBin;
                                });
      if (owner == resolvedBinRows.end() ||
          (owner->flags & FunctionalBinDefault))
        continue;
      for (const auto &atom : set.atoms) {
        if (atom.limbCount != limbCount || atom.firstLimb > set.limbs.size() ||
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
        if (coverageShapeContainsPrefix(shape, frame.prefix, frame.remainingBit,
                                        bitWidth)) {
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
        if (!functionalMagnitudeFitsU32(missingCardinality))
          return OBELISK_RT_OUT_OF_RESOURCES;
        missingBlocks.push_back({std::move(frame.prefix), frame.remainingBit});
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
        FunctionalMagnitude value =
            addFunctionalMagnitudes(block.prefix, FunctionalMagnitude{offset});
        value.resize(limbCount);
        if (signedness == CoverageSignedness::Signed)
          toggleFunctionalSignBias(value, bitWidth);
        values.push_back(std::move(value));
      }
    }

    const uint32_t cardinality = static_cast<uint32_t>(values.size());
    const uint32_t insertionIndex =
        resolvedGroupRows[pendingDefault.groupIndex].firstBin;
    uint32_t firstOrdinal = 0;
    for (uint32_t index = 0; index != insertionIndex; ++index)
      if (resolvedBinRows[index].item == pendingDefault.item)
        firstOrdinal =
            std::max(firstOrdinal, resolvedBinRows[index].ordinal + 1);
    for (size_t groupIndex = pendingDefault.groupIndex + 1;
         groupIndex != resolvedGroupRows.size(); ++groupIndex)
      resolvedGroupRows[groupIndex].firstBin += cardinality;
    resolvedGroupRows[pendingDefault.groupIndex].binCount = cardinality;
    resolvedGroupRows[pendingDefault.groupIndex].arrayCardinality = cardinality;
    for (auto &bin : resolvedBinRows)
      if (bin.item == pendingDefault.item && bin.ordinal >= firstOrdinal)
        bin.ordinal += cardinality;

    std::vector<ResolvedFunctionalBin> defaultBins;
    defaultBins.reserve(values.size());
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
      defaultBins.push_back({typeID,
                             {},
                             binID,
                             pendingDefault.bin->id,
                             pendingDefault.item,
                             std::move(name),
                             pendingDefault.bin->kind,
                             pendingDefault.bin->flags,
                             firstOrdinal + expansion,
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
    resolvedBinRows.insert(resolvedBinRows.begin() + insertionIndex,
                           defaultBins.begin(), defaultBins.end());
  }

  // A coverpoint with no user-defined ordinary bin receives the default set
  // of automatic integral bins (IEEE 1800-2017 19.5.3).  Ignore and illegal
  // bins do not suppress creation; they are applied to the uniformly
  // partitioned bins after distribution.
  for (const ResolvedFunctionalItem &resolvedItem : resolvedItemRows) {
    if (resolvedItem.kind != FunctionalItemKind::Coverpoint)
      continue;
    const bool hasUserBin = std::any_of(
        schema.functionalBins.begin(), schema.functionalBins.end(),
        [&](const FunctionalBin &bin) {
          return bin.item == resolvedItem.templateItem &&
                 (bin.kind == FunctionalBinKind::State ||
                  bin.kind == FunctionalBinKind::Transition) &&
                 !(bin.flags & (FunctionalBinIgnore | FunctionalBinIllegal |
                                FunctionalBinAutomatic));
        });
    const bool hasStaticAutomatic =
        std::any_of(schema.functionalBins.begin(), schema.functionalBins.end(),
                    [&](const FunctionalBin &bin) {
                      return bin.item == resolvedItem.templateItem &&
                             (bin.flags & FunctionalBinAutomatic);
                    });
    if (hasUserBin || hasStaticAutomatic)
      continue;

    const FunctionalExpression *sampleExpression = nullptr;
    for (const FunctionalExpression &expression : schema.functionalExpressions)
      if (expression.owner == resolvedItem.templateItem &&
          expression.ownerKind == FunctionalExpressionOwnerKind::Item &&
          expression.role == FunctionalExpressionRole::CoverpointSample) {
        sampleExpression = &expression;
        break;
      }
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
    if (binCount64 > UINT32_MAX)
      return OBELISK_RT_OUT_OF_RESOURCES;
    const uint32_t binCount = static_cast<uint32_t>(binCount64);

    auto itemOrdinal = [&](uint64_t item) {
      auto found = std::find_if(
          resolvedItemRows.begin(), resolvedItemRows.end(),
          [&](const auto &candidate) { return candidate.id == item; });
      return found == resolvedItemRows.end() ? UINT32_MAX : found->ordinal;
    };
    uint32_t insertionIndex = resolvedBinRows.size();
    for (uint32_t index = 0; index != resolvedBinRows.size(); ++index) {
      uint32_t ownerOrdinal = itemOrdinal(resolvedBinRows[index].item);
      if (ownerOrdinal > resolvedItem.ordinal) {
        insertionIndex = index;
        break;
      }
    }
    size_t groupInsertion = resolvedGroupRows.size();
    for (size_t index = 0; index != resolvedGroupRows.size(); ++index) {
      if (itemOrdinal(resolvedGroupRows[index].item) > resolvedItem.ordinal) {
        groupInsertion = index;
        break;
      }
    }
    if (!binCount) {
      resolvedGroupRows.insert(resolvedGroupRows.begin() + groupInsertion,
                               {typeID,
                                {},
                                resolvedItem.id,
                                0,
                                insertionIndex,
                                0,
                                0,
                                FunctionalBinArrayMode::Fixed,
                                FunctionalBinDistributionKind::Uniform,
                                0,
                                FunctionalBinKind::State});
      continue;
    }
    for (size_t index = groupInsertion; index != resolvedGroupRows.size();
         ++index)
      resolvedGroupRows[index].firstBin += binCount;

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
    std::vector<ResolvedFunctionalBin> automaticBins;
    automaticBins.reserve(binCount);
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
      automaticBins.push_back({typeID,
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
    resolvedBinRows.insert(resolvedBinRows.begin() + insertionIndex,
                           automaticBins.begin(), automaticBins.end());
    resolvedGroupRows.insert(resolvedGroupRows.begin() + groupInsertion,
                             {typeID,
                              {},
                              resolvedItem.id,
                              0,
                              insertionIndex,
                              binCount,
                              binCount,
                              FunctionalBinArrayMode::Fixed,
                              FunctionalBinDistributionKind::Uniform,
                              0,
                              FunctionalBinKind::State});
  }
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

  std::vector<ResolvedCrossSelectorBinding> resolvedSelectorBindingRows;
  for (const CrossSelectorNode &node : schema.crossSelectorNodes) {
    auto resolvedCross = resolvedItems.find(node.cross);
    if (resolvedCross == resolvedItems.end())
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
          set->kind != FunctionalValueSetKind::Integral || !set->bitWidth ||
          set->setExpression ||
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
            atom.kind != FunctionalValueAtomKind::IntegralRange)
          return OBELISK_RT_INVALID_DESIGN;
        const bool lowerUnbounded =
            atom.flags & FunctionalValueAtomLowerUnbounded;
        const bool upperUnbounded =
            atom.flags & FunctionalValueAtomUpperUnbounded;
        const auto *low =
            lowerUnbounded ? nullptr : expressionValue(atom.lowerExpression);
        const auto *high = upperUnbounded ? nullptr
                           : atom.kind == FunctionalValueAtomKind::IntegralValue
                               ? low
                               : expressionValue(atom.upperExpression);
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
    if (node.constructionExpression) {
      const auto staticPlan = std::find_if(
          schema.crossPlans.begin(), schema.crossPlans.end(),
          [&](const CrossPlan &plan) { return plan.item == node.cross; });
      const auto *construction = expressionValue(node.constructionExpression);
      if (staticPlan == schema.crossPlans.end() || !construction ||
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
              ParseLimits{}.maxRecords, snapshot);
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
      std::set<std::vector<uint8_t>> uniqueValues;
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
          if (target.tupleResultKind == FunctionalExpressionResultKind::Real) {
            component.realBits = component.aval.front();
            double real = 0.0;
            std::memcpy(&real, &component.realBits, sizeof(real));
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
          if (component.target->tupleResultKind ==
              FunctionalExpressionResultKind::Real) {
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
      std::sort(values.begin(), values.end(),
                [](const TupleValue &left, const TupleValue &right) {
                  return left.key < right.key;
                });

      resolvedMatchesPolicy = node.matchesPolicy;
      if (node.matchesPolicy == CrossMatchesPolicy::Count) {
        resolvedMatchesCount = node.matchesCount;
        if (node.matchesExpression) {
          const FunctionalExpression *matchesSchema =
              expressionSchema(node.matchesExpression);
          const auto *matchesValue = expressionValue(node.matchesExpression);
          FunctionalInteger integer;
          bool unknownValue = false;
          if (!matchesSchema || !matchesValue ||
              !decodeFunctionalInteger(*matchesValue, matchesSchema->signedness,
                                       integer, unknownValue) ||
              unknownValue || integer.negative)
            return OBELISK_RT_INVALID_DESIGN;
          const uint64_t cap = ParseLimits{}.maxRecords == UINT64_MAX
                                   ? UINT64_MAX
                                   : ParseLimits{}.maxRecords + 1;
          resolvedMatchesCount = integer.magnitude.empty() ? 0
                                 : integer.magnitude.size() > 1
                                     ? cap
                                     : std::min(integer.magnitude.front(), cap);
        } else if (resolvedMatchesCount > ParseLimits{}.maxRecords + 1)
          resolvedMatchesCount = ParseLimits{}.maxRecords + 1;
        if (!resolvedMatchesCount)
          return OBELISK_RT_INVALID_DESIGN;
      } else if (node.matchesPolicy != CrossMatchesPolicy::All) {
        return OBELISK_RT_INVALID_DESIGN;
      }

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
                      component.target->tupleBitWidth,
                      component.target->tupleResultKind ==
                              FunctionalExpressionResultKind::Real
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
          atom.atom.kind = component.target->tupleResultKind ==
                                   FunctionalExpressionResultKind::Real
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
    } else if (node.withExpression) {
      auto staticPlan = std::find_if(
          schema.crossPlans.begin(), schema.crossPlans.end(),
          [&](const CrossPlan &plan) { return plan.item == node.cross; });
      if (staticPlan == schema.crossPlans.end() ||
          staticPlan->targetCount < 2 ||
          uint64_t{staticPlan->firstTarget} + staticPlan->targetCount >
              schema.crossTargets.size())
        return OBELISK_RT_INVALID_DESIGN;

      struct TargetDomain {
        uint64_t item = 0;
        uint32_t bitWidth = 0;
        CoverageSignedness signedness = CoverageSignedness::NotApplicable;
        uint32_t cardinality = 0;
        bool fourState = false;
      };
      std::vector<TargetDomain> domains;
      uint64_t candidateCount = 1;
      for (uint32_t ordinal = 0; ordinal != staticPlan->targetCount;
           ++ordinal) {
        const CrossTarget &target =
            schema.crossTargets[staticPlan->firstTarget + ordinal];
        auto resolvedTargetItem = resolvedItems.find(target.target);
        const FunctionalExpression *sampleExpression = nullptr;
        for (const FunctionalExpression &expression :
             schema.functionalExpressions)
          if (expression.owner == target.target &&
              expression.ownerKind == FunctionalExpressionOwnerKind::Item &&
              expression.role == FunctionalExpressionRole::CoverpointSample) {
            sampleExpression = &expression;
            break;
          }
        const bool fourState =
            (target.tupleFlags & CrossTargetDomainFourState) != 0;
        const uint64_t domainBits =
            sampleExpression
                ? uint64_t{sampleExpression->bitWidth} * (fourState ? 2u : 1u)
                : 0;
        if (target.cross != node.cross || target.ordinal != ordinal ||
            resolvedTargetItem == resolvedItems.end() || !sampleExpression ||
            sampleExpression->resultKind !=
                FunctionalExpressionResultKind::Integral ||
            !sampleExpression->bitWidth || domainBits >= 32 ||
            candidateCount > uint64_t{MaxFunctionalCrossWithCandidates} >>
                domainBits)
          return OBELISK_RT_INVALID_DESIGN;
        const uint32_t cardinality = uint32_t{1} << domainBits;
        candidateCount *= cardinality;
        domains.push_back(
            {resolvedTargetItem->second, sampleExpression->bitWidth,
             sampleExpression->signedness, cardinality, fourState});
      }
      if (!candidateCount || candidateCount > MaxFunctionalCrossWithCandidates)
        return OBELISK_RT_OUT_OF_RESOURCES;

      std::vector<const FunctionalExpression *> predicates;
      for (const FunctionalExpression &expression :
           schema.functionalExpressions)
        if (expression.owner == node.id &&
            expression.ownerKind == FunctionalExpressionOwnerKind::Selector &&
            expression.role == FunctionalExpressionRole::SelectorWith)
          predicates.push_back(&expression);
      std::sort(predicates.begin(), predicates.end(),
                [](const auto *left, const auto *right) {
                  return left->ownerOrdinal < right->ownerOrdinal;
                });
      if (predicates.size() != candidateCount || predicates.empty() ||
          predicates.front()->id != node.withExpression)
        return OBELISK_RT_INVALID_DESIGN;
      for (size_t ordinal = 0; ordinal != predicates.size(); ++ordinal)
        if (predicates[ordinal]->ownerOrdinal != ordinal ||
            predicates[ordinal]->ownerSubordinal ||
            predicates[ordinal]->semanticDigest !=
                predicates.front()->semanticDigest)
          return OBELISK_RT_INVALID_DESIGN;

      resolvedMatchesPolicy = node.matchesPolicy;
      if (node.matchesPolicy == CrossMatchesPolicy::Count) {
        resolvedMatchesCount = node.matchesCount;
        if (node.matchesExpression) {
          const FunctionalExpression *matchesSchema =
              expressionSchema(node.matchesExpression);
          const auto *matchesValue = expressionValue(node.matchesExpression);
          FunctionalInteger integer;
          bool unknown = false;
          if (!matchesSchema || !matchesValue ||
              !decodeFunctionalInteger(*matchesValue, matchesSchema->signedness,
                                       integer, unknown) ||
              unknown || integer.negative)
            return OBELISK_RT_INVALID_DESIGN;
          resolvedMatchesCount =
              integer.magnitude.empty() ? 0
              : integer.magnitude.size() > 1
                  ? uint64_t{MaxFunctionalCrossWithCandidates} + 1
                  : std::min(integer.magnitude.front(),
                             uint64_t{MaxFunctionalCrossWithCandidates} + 1);
        }
        if (!resolvedMatchesCount)
          return OBELISK_RT_INVALID_DESIGN;
      } else if (node.matchesPolicy != CrossMatchesPolicy::All) {
        return OBELISK_RT_INVALID_DESIGN;
      }

      resolvedTupleSetID =
          resolvedFunctionalID("functional.resolved.selector-with-tuples",
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
      for (uint32_t candidate = 0; candidate != candidateCount; ++candidate) {
        const auto *predicate = expressionValue(predicates[candidate]->id);
        if (!predicate ||
            (predicate->kind != OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL &&
             predicate->kind != OBELISK_RT_FUNCTIONAL_VALUE_FOUR_STATE) ||
            predicate->bit_width != 1 || predicate->value_size != 1 ||
            !predicate->value || predicate->owner || predicate->payload ||
            predicate->argument_ref_kind ||
            (predicate->kind == OBELISK_RT_FUNCTIONAL_VALUE_FOUR_STATE) !=
                (predicate->unknown != nullptr))
          return OBELISK_RT_INVALID_DESIGN;
        if (!functionalBooleanTrue(*predicate))
          continue;
        const uint32_t tupleOrdinal = pendingTupleSet.tuples.size();
        uint64_t tupleID =
            resolvedFunctionalID("functional.resolved.selector-with-tuple",
                                 typeID, node.id, tupleOrdinal, tupleIDs);
        if (!tupleID)
          return OBELISK_RT_INVALID_DESIGN;
        tupleIDs.insert(tupleID);
        const uint32_t firstComponent = pendingTupleSet.components.size();
        uint32_t remainder = candidate;
        std::vector<uint32_t> targetValues(domains.size());
        for (size_t reverse = domains.size(); reverse != 0; --reverse) {
          const size_t targetOrdinal = reverse - 1;
          targetValues[targetOrdinal] =
              remainder % domains[targetOrdinal].cardinality;
          remainder /= domains[targetOrdinal].cardinality;
        }
        for (size_t targetOrdinal = 0; targetOrdinal != domains.size();
             ++targetOrdinal) {
          const TargetDomain &domain = domains[targetOrdinal];
          uint64_t encoded = targetValues[targetOrdinal];
          uint64_t bval = 0;
          if (domain.fourState) {
            bval = encoded >> domain.bitWidth;
            encoded &= (uint64_t{1} << domain.bitWidth) - 1;
          } else if (domain.signedness == CoverageSignedness::Signed) {
            encoded ^= uint64_t{1} << (domain.bitWidth - 1);
          }
          const uint32_t componentOrdinal =
              static_cast<uint32_t>(targetOrdinal);
          const uint32_t stableOrdinal =
              tupleOrdinal * staticPlan->targetCount + componentOrdinal;
          uint64_t componentSetID =
              resolvedFunctionalID("functional.resolved.selector-with-value",
                                   typeID, node.id, stableOrdinal, valueSetIDs);
          if (!componentSetID)
            return OBELISK_RT_INVALID_DESIGN;
          valueSetIDs.insert(componentSetID);
          PendingSet base;
          base.set = {typeID,
                      {},
                      0,
                      0,
                      domain.item,
                      0,
                      0,
                      domain.bitWidth,
                      FunctionalValueSetKind::Integral,
                      0,
                      domain.signedness,
                      0,
                      node.id,
                      tupleOrdinal,
                      componentOrdinal,
                      ResolvedFunctionalValueSetRole::TupleComponent};
          PendingAtom atom;
          atom.atom.kind = FunctionalValueAtomKind::IntegralValue;
          atom.atom.flags = FunctionalValueAtomLowerInclusive |
                            FunctionalValueAtomUpperInclusive;
          atom.atom.limbCount = 1;
          atom.limbs.push_back({0, 0, 0, encoded, bval, encoded, bval, 0});
          pendingSets.push_back(
              packPendingSet(base, {atom}, componentSetID, 0, tupleOrdinal));
          // packPendingSet assigns ownerBin and ownerOrdinal for state-bin
          // expansion. Tuple components instead retain their selector-owned
          // coordinates.
          pendingSets.back().set.ownerBin = 0;
          pendingSets.back().set.ownerOrdinal = tupleOrdinal;
          pendingSets.back().set.ownerSubordinal = componentOrdinal;
          pendingTupleSet.components.push_back(
              {tupleID, domain.item, 0, componentSetID, componentOrdinal, 0});
        }
        pendingTupleSet.tuples.push_back(
            {resolvedTupleSetID, tupleID, firstComponent,
             static_cast<uint32_t>(domains.size()), tupleOrdinal, 0});
      }
      pendingTupleSet.set.tupleCount =
          static_cast<uint32_t>(pendingTupleSet.tuples.size());
      pendingTupleSets.push_back(std::move(pendingTupleSet));
    }
    if (!resolvedSetID && !resolvedTupleSetID)
      continue;
    resolvedSelectorBindingRows.push_back({typeID,
                                           {},
                                           resolvedCross->second,
                                           node.id,
                                           resolvedSetID,
                                           node.withExpression,
                                           resolvedTupleSetID,
                                           resolvedMatchesPolicy,
                                           0,
                                           resolvedMatchesCount});
  }
  std::sort(pendingTupleSets.begin(), pendingTupleSets.end(),
            [](const auto &left, const auto &right) {
              return left.set.id < right.set.id;
            });
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
  std::sort(resolvedSelectorBindingRows.begin(),
            resolvedSelectorBindingRows.end(),
            [](const auto &a, const auto &b) {
              return std::tie(a.cross, a.node) < std::tie(b.cross, b.node);
            });
  if (resolvedItems.empty())
    return OBELISK_RT_INVALID_DESIGN;

  auto pendingSetOrderKey = [&](const PendingSet &pending) {
    auto item = std::find_if(resolvedItemRows.begin(), resolvedItemRows.end(),
                             [&](const auto &candidate) {
                               return candidate.id == pending.set.item;
                             });
    auto bin = std::find_if(resolvedBinRows.begin(), resolvedBinRows.end(),
                            [&](const auto &candidate) {
                              return candidate.id == pending.set.ownerBin;
                            });
    return std::make_tuple(
        pending.set.templateValueSet, static_cast<uint32_t>(pending.set.role),
        item == resolvedItemRows.end() ? uint64_t{0} : item->templateItem,
        bin == resolvedBinRows.end() ? uint64_t{0} : bin->templateBin,
        bin == resolvedBinRows.end() ? uint32_t{0} : bin->expansionOrdinal,
        pending.set.ownerSelector, pending.set.ownerOrdinal,
        pending.set.ownerSubordinal);
  };
  std::sort(pendingSets.begin(), pendingSets.end(),
            [&](const auto &a, const auto &b) {
              return pendingSetOrderKey(a) < pendingSetOrderKey(b);
            });
  std::vector<ResolvedFunctionalValueSet> resolvedSetRows;
  std::vector<ResolvedFunctionalValueAtom> resolvedAtomRows;
  std::vector<ResolvedFunctionalValueLimb> resolvedLimbRows;
  for (PendingSet &pending : pendingSets) {
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
  Database fingerprintInput = schema;
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
  markStaticallyEmptyStateBins(fingerprintInput, typeID, Digest{});
  std::unordered_set<uint64_t> suppressedTransitionAlternatives;
  std::unordered_set<uint64_t> emptyTransitionBins;
  obelisk_rt_status transitionExclusionStatus = deriveTransitionExclusions(
      fingerprintInput, typeID, Digest{}, suppressedTransitionAlternatives,
      emptyTransitionBins);
  if (transitionExclusionStatus != OBELISK_RT_OK)
    return transitionExclusionStatus;
  for (auto &bin : fingerprintInput.resolvedFunctionalBins)
    if (bin.type == typeID && bin.configuration == Digest{} &&
        emptyTransitionBins.count(bin.id))
      bin.flags |= FunctionalBinEmpty;
  for (auto &row : resolvedBinRows) {
    auto resolved =
        std::find_if(fingerprintInput.resolvedFunctionalBins.begin(),
                     fingerprintInput.resolvedFunctionalBins.end(),
                     [&](const auto &candidate) {
                       return candidate.type == typeID &&
                              candidate.configuration == Digest{} &&
                              candidate.id == row.id;
                     });
    if (resolved == fingerprintInput.resolvedFunctionalBins.end())
      return OBELISK_RT_INVALID_DESIGN;
    row.flags = resolved->flags;
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
  std::sort(
      resolvedCrossItems.begin(), resolvedCrossItems.end(),
      [](const auto *left, const auto *right) { return left->id < right->id; });
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
    auto staticPlan =
        std::find_if(schema.crossPlans.begin(), schema.crossPlans.end(),
                     [&](const CrossPlan &candidate) {
                       return candidate.item == cross->templateItem;
                     });
    if (staticPlan == schema.crossPlans.end() || staticPlan->targetCount < 2 ||
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
      std::vector<const ResolvedFunctionalBin *> bins;
      for (const auto &bin : resolvedBinRows) {
        constexpr uint32_t omitted =
            FunctionalBinDefault | FunctionalBinDefaultSequence |
            FunctionalBinIgnore | FunctionalBinIllegal | FunctionalBinEmpty;
        if (bin.item == target->second && !(bin.flags & omitted))
          bins.push_back(&bin);
      }
      std::sort(bins.begin(), bins.end(),
                [](const auto *left, const auto *right) {
                  return std::tie(left->name, left->id) <
                         std::tie(right->name, right->id);
                });
      if (bins.size() > UINT32_MAX)
        return OBELISK_RT_OUT_OF_RESOURCES;
      targetBins.push_back(std::move(bins));
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
      auto node = std::find_if(schema.crossSelectorNodes.begin(),
                               schema.crossSelectorNodes.end(),
                               [&](const CrossSelectorNode &candidate) {
                                 return candidate.id == crossBin.rootSelector;
                               });
      CrossSelectorDNF alternatives;
      if (crossBin.cross != cross->templateItem ||
          node == schema.crossSelectorNodes.end() ||
          node->cross != cross->templateItem)
        return OBELISK_RT_INVALID_DESIGN;

      bool hasSignificantAlternative = false;
      if (crossSelectorUsesWith(schema, *node)) {
        std::vector<std::vector<uint64_t>> candidateTuples;
        if (crossSelectorIsWithWrappedSet(schema, *node)) {
          std::vector<std::vector<uint64_t>> targetBinIDs(allTargetBins.size());
          for (size_t ordinal = 0; ordinal != allTargetBins.size(); ++ordinal)
            for (const ResolvedFunctionalBin *bin : allTargetBins[ordinal])
              targetBinIDs[ordinal].push_back(bin->id);
          obelisk_rt_status selectorStatus = resolvedCrossSelectorSetCandidates(
              fingerprintInput, typeID, Digest{}, cross->id, *node,
              targetBinIDs, suppressedTransitionAlternatives, candidateTuples);
          if (selectorStatus != OBELISK_RT_OK)
            return selectorStatus;
        } else {
          uint64_t binTupleCount = 1;
          for (const auto &bins : allTargetBins) {
            if (bins.empty()) {
              binTupleCount = 0;
              break;
            }
            if (binTupleCount > recordLimit / bins.size())
              return OBELISK_RT_OUT_OF_RESOURCES;
            binTupleCount *= bins.size();
          }
          std::vector<uint64_t> tuple(allTargetBins.size());
          for (uint64_t encodedTuple = 0; encodedTuple != binTupleCount;
               ++encodedTuple) {
            uint64_t remainder = encodedTuple;
            for (size_t reverse = allTargetBins.size(); reverse != 0;
                 --reverse) {
              const size_t ordinal = reverse - 1;
              tuple[ordinal] =
                  allTargetBins[ordinal]
                               [remainder % allTargetBins[ordinal].size()]
                                   ->id;
              remainder /= allTargetBins[ordinal].size();
            }
            bool selected = false;
            obelisk_rt_status selectorStatus =
                resolvedCrossSelectorSelectsBinTuple(
                    fingerprintInput, typeID, Digest{}, cross->id, *node, tuple,
                    suppressedTransitionAlternatives, selected);
            if (selectorStatus != OBELISK_RT_OK)
              return selectorStatus;
            if (selected)
              candidateTuples.push_back(tuple);
          }
        }
        for (const auto &tuple : candidateTuples) {
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
      } else {
        const bool usesSet = crossSelectorUsesSet(schema, *node);
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
                    suppressedTransitionAlternatives, intersects);
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
              targetBinIDs, suppressedTransitionAlternatives, candidates);
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
        auto mirror =
            std::find_if(resolvedBinRows.begin(), resolvedBinRows.end(),
                         [&](const ResolvedFunctionalBin &candidate) {
                           return candidate.templateBin == crossBin.bin &&
                                  candidate.item == cross->id;
                         });
        if (mirror == resolvedBinRows.end())
          return OBELISK_RT_INVALID_DESIGN;
        mirror->flags |= FunctionalBinEmpty;
        auto fingerprintMirror =
            std::find_if(fingerprintInput.resolvedFunctionalBins.begin(),
                         fingerprintInput.resolvedFunctionalBins.end(),
                         [&](const ResolvedFunctionalBin &candidate) {
                           return candidate.type == typeID &&
                                  candidate.configuration == Digest{} &&
                                  candidate.id == mirror->id;
                         });
        if (fingerprintMirror == fingerprintInput.resolvedFunctionalBins.end())
          return OBELISK_RT_INVALID_DESIGN;
        fingerprintMirror->flags = mirror->flags;
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
    };
    std::vector<std::vector<RawState>> levels(staticPlan->targetCount);
    RectangleState initial(selectorRectangles.size());
    std::iota(initial.begin(), initial.end(), uint32_t{0});
    levels.front().push_back({std::move(initial), {}});
    uint64_t rawStateCount = 1;
    uint64_t rawEdgeCount = 0;
    for (uint32_t ordinal = 0; ordinal != staticPlan->targetCount; ++ordinal) {
      std::map<RectangleState, uint32_t> successorIndices;
      for (RawState &state : levels[ordinal]) {
        for (const ResolvedFunctionalBin *bin : allTargetBins[ordinal]) {
          RectangleState next;
          for (uint32_t rectangle : state.active)
            if (std::binary_search(
                    selectorRectangles[rectangle].allowedBins[ordinal].begin(),
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
      std::map<NodeSignature, CanonicalNode> canonical;
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
  std::sort(pendingCrossNodes.begin(), pendingCrossNodes.end(),
            [](const auto &left, const auto &right) {
              return std::tie(left.node.cross, left.node.id) <
                     std::tie(right.node.cross, right.node.id);
            });
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

  schema.functionalConfigurations.push_back({typeID, configuration, 0});
  schema.resolvedFunctionalItems.insert(schema.resolvedFunctionalItems.end(),
                                        resolvedItemRows.begin(),
                                        resolvedItemRows.end());
  std::sort(schema.resolvedFunctionalItems.begin(),
            schema.resolvedFunctionalItems.end(),
            [](const auto &a, const auto &b) {
              return std::tie(a.type, a.configuration, a.id) <
                     std::tie(b.type, b.configuration, b.id);
            });

  auto binPosition = std::lower_bound(
      schema.resolvedFunctionalBins.begin(),
      schema.resolvedFunctionalBins.end(), std::tie(typeID, configuration),
      [](const auto &bin, const auto &key) {
        return std::tie(bin.type, bin.configuration) < key;
      });
  uint32_t insertedBin = binPosition - schema.resolvedFunctionalBins.begin();
  for (auto &group : schema.resolvedFunctionalBinGroups) {
    const bool followsInsertion = std::tie(group.type, group.configuration) >=
                                  std::tie(typeID, configuration);
    if (group.firstBin > insertedBin ||
        (group.firstBin == insertedBin && followsInsertion))
      group.firstBin += resolvedBinRows.size();
  }
  schema.resolvedFunctionalBins.insert(binPosition, resolvedBinRows.begin(),
                                       resolvedBinRows.end());
  for (auto &group : resolvedGroupRows)
    group.firstBin += insertedBin;

  auto setPosition = std::lower_bound(
      schema.resolvedFunctionalValueSets.begin(),
      schema.resolvedFunctionalValueSets.end(), std::tie(typeID, configuration),
      [](const auto &set, const auto &key) {
        return std::tie(set.type, set.configuration) < key;
      });
  size_t setIndex = setPosition - schema.resolvedFunctionalValueSets.begin();
  uint32_t insertedAtom =
      setPosition == schema.resolvedFunctionalValueSets.end()
          ? schema.resolvedFunctionalValueAtoms.size()
          : setPosition->firstAtom;
  uint32_t insertedLimb =
      insertedAtom == schema.resolvedFunctionalValueAtoms.size()
          ? schema.resolvedFunctionalValueLimbs.size()
          : schema.resolvedFunctionalValueAtoms[insertedAtom].firstLimb;
  for (size_t index = setIndex;
       index != schema.resolvedFunctionalValueSets.size(); ++index)
    schema.resolvedFunctionalValueSets[index].firstAtom +=
        resolvedAtomRows.size();
  for (size_t index = insertedAtom;
       index != schema.resolvedFunctionalValueAtoms.size(); ++index)
    schema.resolvedFunctionalValueAtoms[index].firstLimb +=
        resolvedLimbRows.size();
  for (auto &set : resolvedSetRows)
    set.firstAtom += insertedAtom;
  for (auto &atom : resolvedAtomRows)
    atom.firstLimb += insertedLimb;
  schema.resolvedFunctionalValueLimbs.insert(
      schema.resolvedFunctionalValueLimbs.begin() + insertedLimb,
      resolvedLimbRows.begin(), resolvedLimbRows.end());
  schema.resolvedFunctionalValueAtoms.insert(
      schema.resolvedFunctionalValueAtoms.begin() + insertedAtom,
      resolvedAtomRows.begin(), resolvedAtomRows.end());
  schema.resolvedFunctionalValueSets.insert(
      setPosition, resolvedSetRows.begin(), resolvedSetRows.end());

  auto transitionAlternativePosition = std::lower_bound(
      schema.resolvedTransitionAlternatives.begin(),
      schema.resolvedTransitionAlternatives.end(),
      std::tie(typeID, configuration),
      [](const auto &alternative, const auto &key) {
        return std::tie(alternative.type, alternative.configuration) < key;
      });
  const uint32_t insertedTransitionAlternative =
      static_cast<uint32_t>(transitionAlternativePosition -
                            schema.resolvedTransitionAlternatives.begin());
  const uint32_t insertedTransitionStep =
      transitionAlternativePosition ==
              schema.resolvedTransitionAlternatives.end()
          ? static_cast<uint32_t>(schema.resolvedTransitionSteps.size())
          : transitionAlternativePosition->firstStep;
  for (auto alternative = transitionAlternativePosition;
       alternative != schema.resolvedTransitionAlternatives.end();
       ++alternative)
    alternative->firstStep += resolvedTransitionStepRows.size();
  for (auto &alternative : resolvedTransitionAlternativeRows)
    alternative.firstStep += insertedTransitionStep;
  schema.resolvedTransitionSteps.insert(
      schema.resolvedTransitionSteps.begin() + insertedTransitionStep,
      resolvedTransitionStepRows.begin(), resolvedTransitionStepRows.end());
  schema.resolvedTransitionAlternatives.insert(
      transitionAlternativePosition, resolvedTransitionAlternativeRows.begin(),
      resolvedTransitionAlternativeRows.end());

  auto transitionGroupPosition = std::lower_bound(
      schema.resolvedTransitionExpansionGroups.begin(),
      schema.resolvedTransitionExpansionGroups.end(),
      std::tie(typeID, configuration), [](const auto &group, const auto &key) {
        return std::tie(group.type, group.configuration) < key;
      });
  for (auto &group : schema.resolvedTransitionExpansionGroups) {
    const bool followsInsertion = std::tie(group.type, group.configuration) >=
                                  std::tie(typeID, configuration);
    if (group.firstAlternative > insertedTransitionAlternative ||
        (group.firstAlternative == insertedTransitionAlternative &&
         followsInsertion))
      group.firstAlternative += resolvedTransitionAlternativeRows.size();
  }
  for (auto &group : resolvedTransitionExpansionGroupRows)
    group.firstAlternative += insertedTransitionAlternative;
  schema.resolvedTransitionExpansionGroups.insert(
      transitionGroupPosition, resolvedTransitionExpansionGroupRows.begin(),
      resolvedTransitionExpansionGroupRows.end());

  schema.resolvedFunctionalBinPlans.insert(
      schema.resolvedFunctionalBinPlans.end(), resolvedPlanRows.begin(),
      resolvedPlanRows.end());
  std::sort(schema.resolvedFunctionalBinPlans.begin(),
            schema.resolvedFunctionalBinPlans.end(),
            [](const auto &a, const auto &b) {
              return std::tie(a.type, a.configuration, a.bin) <
                     std::tie(b.type, b.configuration, b.bin);
            });
  auto groupPosition = std::lower_bound(
      schema.resolvedFunctionalBinGroups.begin(),
      schema.resolvedFunctionalBinGroups.end(), std::tie(typeID, configuration),
      [](const auto &group, const auto &key) {
        return std::tie(group.type, group.configuration) < key;
      });
  schema.resolvedFunctionalBinGroups.insert(
      groupPosition, resolvedGroupRows.begin(), resolvedGroupRows.end());
  schema.functionalConfigurationOptions.insert(
      schema.functionalConfigurationOptions.end(),
      configurationOptionRows.begin(), configurationOptionRows.end());
  std::sort(schema.functionalConfigurationOptions.begin(),
            schema.functionalConfigurationOptions.end(),
            [](const auto &a, const auto &b) {
              return std::tie(a.type, a.configuration, a.ownerKind, a.owner,
                              a.scope, a.option) <
                     std::tie(b.type, b.configuration, b.ownerKind, b.owner,
                              b.scope, b.option);
            });
  std::sort(schema.functionalConfigurations.begin(),
            schema.functionalConfigurations.end(),
            [](const auto &a, const auto &b) {
              return std::tie(a.type, a.configuration) <
                     std::tie(b.type, b.configuration);
            });

  auto crossPlanPosition = std::lower_bound(
      schema.resolvedCrossPlans.begin(), schema.resolvedCrossPlans.end(),
      std::make_tuple(typeID, configuration, uint64_t{0}),
      [](const auto &plan, const auto &key) {
        return std::tie(plan.type, plan.configuration, plan.cross) < key;
      });
  if (schema.resolvedCrossAutomaticBinCountLimbs.size() > UINT32_MAX ||
      resolvedCrossCountLimbs.size() >
          UINT32_MAX - schema.resolvedCrossAutomaticBinCountLimbs.size())
    return OBELISK_RT_OUT_OF_RESOURCES;
  const uint32_t insertedCrossCount =
      crossPlanPosition == schema.resolvedCrossPlans.end()
          ? static_cast<uint32_t>(
                schema.resolvedCrossAutomaticBinCountLimbs.size())
          : crossPlanPosition->firstAutomaticBinCountLimb;
  for (auto plan = crossPlanPosition; plan != schema.resolvedCrossPlans.end();
       ++plan)
    plan->firstAutomaticBinCountLimb += resolvedCrossCountLimbs.size();
  for (auto &plan : resolvedCrossPlanRows)
    plan.firstAutomaticBinCountLimb += insertedCrossCount;
  schema.resolvedCrossAutomaticBinCountLimbs.insert(
      schema.resolvedCrossAutomaticBinCountLimbs.begin() + insertedCrossCount,
      resolvedCrossCountLimbs.begin(), resolvedCrossCountLimbs.end());
  schema.resolvedCrossPlans.insert(crossPlanPosition,
                                   resolvedCrossPlanRows.begin(),
                                   resolvedCrossPlanRows.end());

  auto crossNodePosition = std::lower_bound(
      schema.resolvedCrossAutomaticNodes.begin(),
      schema.resolvedCrossAutomaticNodes.end(),
      std::make_tuple(typeID, configuration, uint64_t{0}, uint64_t{0}),
      [](const auto &node, const auto &key) {
        return std::tie(node.type, node.configuration, node.cross, node.id) <
               key;
      });
  if (schema.resolvedCrossAutomaticEdges.size() > UINT32_MAX ||
      resolvedCrossEdgeRows.size() >
          UINT32_MAX - schema.resolvedCrossAutomaticEdges.size())
    return OBELISK_RT_OUT_OF_RESOURCES;
  const uint32_t insertedCrossEdge =
      crossNodePosition == schema.resolvedCrossAutomaticNodes.end()
          ? static_cast<uint32_t>(schema.resolvedCrossAutomaticEdges.size())
          : crossNodePosition->firstEdge;
  for (auto &node : schema.resolvedCrossAutomaticNodes)
    if (node.firstEdge >= insertedCrossEdge)
      node.firstEdge += resolvedCrossEdgeRows.size();
  for (auto &node : resolvedCrossNodeRows)
    node.firstEdge += insertedCrossEdge;
  schema.resolvedCrossAutomaticEdges.insert(
      schema.resolvedCrossAutomaticEdges.begin() + insertedCrossEdge,
      resolvedCrossEdgeRows.begin(), resolvedCrossEdgeRows.end());
  schema.resolvedCrossAutomaticNodes.insert(crossNodePosition,
                                            resolvedCrossNodeRows.begin(),
                                            resolvedCrossNodeRows.end());

  auto tupleSetPosition = std::lower_bound(
      schema.resolvedFunctionalTupleSets.begin(),
      schema.resolvedFunctionalTupleSets.end(), std::tie(typeID, configuration),
      [](const auto &set, const auto &key) {
        return std::tie(set.type, set.configuration) < key;
      });
  const uint32_t insertedTuple =
      tupleSetPosition == schema.resolvedFunctionalTupleSets.end()
          ? static_cast<uint32_t>(
                schema.resolvedFunctionalTupleSetTuples.size())
          : tupleSetPosition->firstTuple;
  const uint32_t insertedTupleComponent =
      insertedTuple == schema.resolvedFunctionalTupleSetTuples.size()
          ? static_cast<uint32_t>(
                schema.resolvedFunctionalTupleSetComponents.size())
          : schema.resolvedFunctionalTupleSetTuples[insertedTuple]
                .firstComponent;
  if (schema.resolvedFunctionalTupleSetTuples.size() > UINT32_MAX ||
      schema.resolvedFunctionalTupleSetComponents.size() > UINT32_MAX ||
      resolvedTupleRows.size() >
          UINT32_MAX - schema.resolvedFunctionalTupleSetTuples.size() ||
      resolvedTupleComponentRows.size() >
          UINT32_MAX - schema.resolvedFunctionalTupleSetComponents.size())
    return OBELISK_RT_OUT_OF_RESOURCES;
  for (auto set = tupleSetPosition;
       set != schema.resolvedFunctionalTupleSets.end(); ++set)
    set->firstTuple += resolvedTupleRows.size();
  for (size_t index = insertedTuple;
       index != schema.resolvedFunctionalTupleSetTuples.size(); ++index)
    schema.resolvedFunctionalTupleSetTuples[index].firstComponent +=
        resolvedTupleComponentRows.size();
  for (auto &set : resolvedTupleSetRows)
    set.firstTuple += insertedTuple;
  for (auto &tuple : resolvedTupleRows)
    tuple.firstComponent += insertedTupleComponent;
  schema.resolvedFunctionalTupleSetComponents.insert(
      schema.resolvedFunctionalTupleSetComponents.begin() +
          insertedTupleComponent,
      resolvedTupleComponentRows.begin(), resolvedTupleComponentRows.end());
  schema.resolvedFunctionalTupleSetTuples.insert(
      schema.resolvedFunctionalTupleSetTuples.begin() + insertedTuple,
      resolvedTupleRows.begin(), resolvedTupleRows.end());
  schema.resolvedFunctionalTupleSets.insert(tupleSetPosition,
                                            resolvedTupleSetRows.begin(),
                                            resolvedTupleSetRows.end());
  schema.resolvedCrossSelectorBindings.insert(
      schema.resolvedCrossSelectorBindings.end(),
      resolvedSelectorBindingRows.begin(), resolvedSelectorBindingRows.end());
  std::sort(schema.resolvedCrossSelectorBindings.begin(),
            schema.resolvedCrossSelectorBindings.end(),
            [](const auto &a, const auto &b) {
              return std::tie(a.type, a.configuration, a.cross, a.node) <
                     std::tie(b.type, b.configuration, b.cross, b.node);
            });
  schema.schemaFingerprint =
      obelisk::coverage::computeSchemaFingerprint(schema);
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
      type.items.size() != type.typeItemWeights.size() || !coverage.schema)
    return OBELISK_RT_INVALID_DESIGN;
  for (size_t index = 0; index != type.items.size(); ++index) {
    auto resolved = std::find_if(
        coverage.schema->resolvedFunctionalItems.begin(),
        coverage.schema->resolvedFunctionalItems.end(), [&](const auto &item) {
          return item.type == typeID &&
                 item.configuration == type.configuration &&
                 item.id == type.items[index];
        });
    if (resolved == coverage.schema->resolvedFunctionalItems.end())
      return OBELISK_RT_INVALID_DESIGN;
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
  auto &schema = *coverage.schema;
  if (std::none_of(
          schema.functionalConfigurations.begin(),
          schema.functionalConfigurations.end(), [&](const auto &entry) {
            return entry.type == typeID && entry.configuration == configuration;
          }))
    return OBELISK_RT_INVALID_DESIGN;
  FunctionalCoverageTypeState type;
  type.configuration = configuration;
  std::unordered_set<uint64_t> emptyTransitionBins;
  obelisk_rt_status transitionExclusionStatus = deriveTransitionExclusions(
      schema, typeID, configuration, type.suppressedTransitionAlternatives,
      emptyTransitionBins);
  if (transitionExclusionStatus != OBELISK_RT_OK)
    return transitionExclusionStatus;
  std::unordered_set<uint64_t> transitionBinsWithAlternatives;
  for (const auto &alternative : schema.resolvedTransitionAlternatives)
    if (alternative.type == typeID &&
        alternative.configuration == configuration)
      transitionBinsWithAlternatives.insert(alternative.bin);
  for (const auto &bin : schema.resolvedFunctionalBins) {
    if (bin.type != typeID || bin.configuration != configuration ||
        bin.kind != obelisk::coverage::FunctionalBinKind::Transition ||
        (bin.flags & (obelisk::coverage::FunctionalBinIgnore |
                      obelisk::coverage::FunctionalBinIllegal)) ||
        !transitionBinsWithAlternatives.count(bin.id))
      continue;
    if (bool(bin.flags & obelisk::coverage::FunctionalBinEmpty) !=
        bool(emptyTransitionBins.count(bin.id)))
      return OBELISK_RT_INVALID_DESIGN;
  }
  for (uint64_t bin : emptyTransitionBins) {
    auto foundBin = std::find_if(
        schema.resolvedFunctionalBins.begin(),
        schema.resolvedFunctionalBins.end(), [&](const auto &candidate) {
          return candidate.type == typeID &&
                 candidate.configuration == configuration &&
                 candidate.id == bin;
        });
    if (foundBin == schema.resolvedFunctionalBins.end() ||
        !(foundBin->flags & obelisk::coverage::FunctionalBinEmpty))
      return OBELISK_RT_INVALID_DESIGN;
  }
  for (const auto &option : schema.functionalConfigurationOptions) {
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
  std::sort(items.begin(), items.end(), [](const auto *a, const auto *b) {
    return a->ordinal < b->ordinal;
  });
  uint64_t groupAtLeast = 1;
  for (const auto &option : schema.functionalConfigurationOptions)
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
    const bool nonAggregating =
        (item->flags & obelisk::coverage::FunctionalItemNonAggregating) != 0;
    type.items.push_back(item->id);
    type.itemAggregating.push_back(nonAggregating ? 0 : 1);
    type.itemGoals.push_back(item->goal);
    type.itemWeights.push_back(nonAggregating ? 0 : item->weight);
    uint64_t itemAtLeast = groupAtLeast;
    for (const auto &option : schema.functionalConfigurationOptions)
      if (option.type == typeID && option.configuration == configuration &&
          option.owner == item->id &&
          option.ownerKind == obelisk::coverage::
                                  FunctionalConfigurationOptionOwnerKind::Item &&
          option.scope ==
              obelisk::coverage::FunctionalOptionScopeKind::Instance &&
          option.option ==
              obelisk::coverage::FunctionalConfigurationOptionKind::AtLeast)
        itemAtLeast = option.value;
    for (const auto &option : schema.functionalConfigurationOptions)
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
    for (const auto &option : schema.functionalConfigurationOptions) {
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
    auto plan = std::find_if(
        schema.resolvedFunctionalBinPlans.begin(),
        schema.resolvedFunctionalBinPlans.end(), [&](const auto &entry) {
          return entry.type == typeID &&
                 entry.configuration == type.configuration &&
                 entry.bin == bin.id;
        });
    uint32_t flags = bin.flags;
    bool excluded = std::any_of(
        schema.exclusions.begin(), schema.exclusions.end(),
        [&](const auto &entry) {
          return entry.metric == obelisk::coverage::MetricKind::Functional &&
                 (entry.entity == bin.id || entry.entity == bin.templateBin);
        });
    type.bins.push_back({bin.id, bin.templateBin, bin.item,
                         plan == schema.resolvedFunctionalBinPlans.end()
                             ? uint64_t{0}
                             : plan->valueSet,
                         bin.atLeast, bin.kind, flags, 0, false, excluded});
  }
  const uint64_t selectorResolutionLimit =
      obelisk::coverage::ParseLimits{}.maxRecords;
  uint64_t explicitRectangleCount = 0;
  uint64_t explicitRectangleBinEntryCount = 0;
  for (const auto &plan : schema.resolvedCrossPlans) {
    if (plan.type != typeID || plan.configuration != type.configuration)
      continue;
    auto item = std::find_if(
        schema.resolvedFunctionalItems.begin(),
        schema.resolvedFunctionalItems.end(), [&](const auto &entry) {
          return entry.type == typeID && entry.configuration == configuration &&
                 entry.id == plan.cross &&
                 entry.kind == obelisk::coverage::FunctionalItemKind::Cross;
        });
    auto staticPlan =
        item == schema.resolvedFunctionalItems.end()
            ? schema.crossPlans.end()
            : std::find_if(schema.crossPlans.begin(), schema.crossPlans.end(),
                           [&](const auto &entry) {
                             return entry.item == item->templateItem;
                           });
    if (item == schema.resolvedFunctionalItems.end() ||
        staticPlan == schema.crossPlans.end() ||
        (plan.retainAutoPolicy !=
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
    auto itemPosition = std::find(type.items.begin(), type.items.end(), item->id);
    if (itemPosition == type.items.end())
      return OBELISK_RT_INVALID_DESIGN;
    cross.atLeast = type.itemAtLeast[static_cast<size_t>(itemPosition -
                                                        type.items.begin())];
    cross.excluded = std::any_of(
        schema.exclusions.begin(), schema.exclusions.end(),
        [&](const auto &entry) {
          return entry.metric == obelisk::coverage::MetricKind::Functional &&
                 (entry.entity == item->id ||
                  entry.entity == item->templateItem);
        });
    for (uint32_t ordinal = 0; ordinal != staticPlan->targetCount; ++ordinal) {
      const auto &target =
          schema.crossTargets[staticPlan->firstTarget + ordinal];
      auto resolvedTarget = std::find_if(
          schema.resolvedFunctionalItems.begin(),
          schema.resolvedFunctionalItems.end(), [&](const auto &entry) {
            return entry.type == typeID &&
                   entry.configuration == configuration &&
                   entry.templateItem == target.target &&
                   entry.kind ==
                       obelisk::coverage::FunctionalItemKind::Coverpoint;
          });
      if (target.cross != item->templateItem || target.ordinal != ordinal ||
          resolvedTarget == schema.resolvedFunctionalItems.end())
        return OBELISK_RT_INVALID_DESIGN;
      cross.targets.push_back(resolvedTarget->id);
    }
    for (uint32_t binOrdinal = 0; binOrdinal != staticPlan->binCount;
         ++binOrdinal) {
      const auto &crossBin =
          schema.crossBins[staticPlan->firstBin + binOrdinal];
      auto selector = std::find_if(
          schema.crossSelectorNodes.begin(), schema.crossSelectorNodes.end(),
          [&](const auto &entry) { return entry.id == crossBin.rootSelector; });
      auto resolvedBin = std::find_if(
          schema.resolvedFunctionalBins.begin(),
          schema.resolvedFunctionalBins.end(), [&](const auto &entry) {
            return entry.type == typeID &&
                   entry.configuration == configuration &&
                   entry.templateBin == crossBin.bin &&
                   entry.item == cross.id &&
                   entry.kind == obelisk::coverage::FunctionalBinKind::Cross;
          });
      CrossSelectorDNF alternatives;
      if (crossBin.cross != item->templateItem ||
          selector == schema.crossSelectorNodes.end() ||
          selector->cross != item->templateItem ||
          resolvedBin == schema.resolvedFunctionalBins.end())
        return OBELISK_RT_INVALID_DESIGN;
      FunctionalCoverageCrossState::ExplicitBin explicitBin;
      explicitBin.bin = resolvedBin->id;
      if (crossSelectorUsesWith(schema, *selector)) {
        std::vector<std::vector<uint64_t>> targetBins(cross.targets.size());
        for (size_t targetOrdinal = 0; targetOrdinal != cross.targets.size();
             ++targetOrdinal) {
          for (const FunctionalCoverageBinState &candidate : type.bins) {
            constexpr uint32_t omitted =
                obelisk::coverage::FunctionalBinDefault |
                obelisk::coverage::FunctionalBinDefaultSequence |
                obelisk::coverage::FunctionalBinIgnore |
                obelisk::coverage::FunctionalBinIllegal |
                obelisk::coverage::FunctionalBinEmpty;
            if (candidate.item == cross.targets[targetOrdinal] &&
                !(candidate.flags & omitted))
              targetBins[targetOrdinal].push_back(candidate.id);
          }
          std::sort(targetBins[targetOrdinal].begin(),
                    targetBins[targetOrdinal].end());
        }
        std::vector<std::vector<uint64_t>> candidateTuples;
        if (crossSelectorIsWithWrappedSet(schema, *selector)) {
          obelisk_rt_status selectorStatus = resolvedCrossSelectorSetCandidates(
              schema, typeID, configuration, cross.id, *selector, targetBins,
              type.suppressedTransitionAlternatives, candidateTuples);
          if (selectorStatus != OBELISK_RT_OK)
            return selectorStatus;
        } else {
          uint64_t tupleCount = 1;
          for (const auto &bins : targetBins) {
            if (bins.empty()) {
              tupleCount = 0;
              break;
            }
            if (tupleCount > selectorResolutionLimit / bins.size())
              return OBELISK_RT_OUT_OF_RESOURCES;
            tupleCount *= bins.size();
          }
          std::vector<uint64_t> tuple(cross.targets.size());
          for (uint64_t encodedTuple = 0; encodedTuple != tupleCount;
               ++encodedTuple) {
            uint64_t remainder = encodedTuple;
            for (size_t reverse = targetBins.size(); reverse != 0; --reverse) {
              size_t ordinal = reverse - 1;
              tuple[ordinal] =
                  targetBins[ordinal][remainder % targetBins[ordinal].size()];
              remainder /= targetBins[ordinal].size();
            }
            bool selected = false;
            obelisk_rt_status selectorStatus =
                resolvedCrossSelectorSelectsBinTuple(
                    schema, typeID, configuration, cross.id, *selector, tuple,
                    type.suppressedTransitionAlternatives, selected);
            if (selectorStatus != OBELISK_RT_OK)
              return selectorStatus;
            if (selected)
              candidateTuples.push_back(tuple);
          }
        }
        for (const auto &tuple : candidateTuples) {
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
      } else {
        const bool usesSet = crossSelectorUsesSet(schema, *selector);
        obelisk_rt_status selectorStatus = decodeCrossSelectorDNF(
            schema, *selector, obelisk::coverage::ParseLimits{}.maxRecords,
            alternatives, usesSet);
        if (selectorStatus != OBELISK_RT_OK)
          return selectorStatus;
        for (const CrossSelectorConjunction &terms : alternatives) {
          FunctionalCoverageCrossState::Rectangle rectangle;
          std::map<uint32_t, std::vector<uint64_t>> constraints;
          uint64_t rectangleBinEntryCount = 0;
          for (const CrossSelectorTerm &term : terms) {
            const auto *condition = term.condition;
            auto staticTarget = std::find_if(
                schema.crossTargets.begin() + staticPlan->firstTarget,
                schema.crossTargets.begin() + staticPlan->firstTarget +
                    staticPlan->targetCount,
                [&](const auto &entry) {
                  return entry.target == condition->target;
                });
            if (staticTarget == schema.crossTargets.begin() +
                                    staticPlan->firstTarget +
                                    staticPlan->targetCount)
              return OBELISK_RT_INVALID_DESIGN;

            const obelisk::coverage::ResolvedFunctionalValueSet *selectedSet =
                nullptr;
            if (condition->valueSet) {
              auto binding =
                  std::find_if(schema.resolvedCrossSelectorBindings.begin(),
                               schema.resolvedCrossSelectorBindings.end(),
                               [&](const auto &entry) {
                                 return entry.type == typeID &&
                                        entry.configuration == configuration &&
                                        entry.cross == cross.id &&
                                        entry.node == condition->id;
                               });
              if (binding == schema.resolvedCrossSelectorBindings.end())
                return OBELISK_RT_INVALID_DESIGN;
              auto set =
                  std::find_if(schema.resolvedFunctionalValueSets.begin(),
                               schema.resolvedFunctionalValueSets.end(),
                               [&](const auto &entry) {
                                 return entry.type == typeID &&
                                        entry.configuration == configuration &&
                                        entry.id == binding->valueSet;
                               });
              if (set == schema.resolvedFunctionalValueSets.end())
                return OBELISK_RT_INVALID_DESIGN;
              selectedSet = &*set;
            }

            std::vector<uint64_t> selectedBins;
            const uint64_t targetItem = cross.targets[staticTarget->ordinal];
            for (const FunctionalCoverageBinState &candidate : type.bins) {
              constexpr uint32_t omitted =
                  obelisk::coverage::FunctionalBinDefault |
                  obelisk::coverage::FunctionalBinDefaultSequence |
                  obelisk::coverage::FunctionalBinIgnore |
                  obelisk::coverage::FunctionalBinIllegal |
                  obelisk::coverage::FunctionalBinEmpty;
              if (candidate.item != targetItem || (candidate.flags & omitted))
                continue;
              bool selected =
                  !condition->bin || candidate.templateBin == condition->bin;
              if (selected && selectedSet) {
                bool intersects = false;
                obelisk_rt_status status =
                    resolvedFunctionalBinIntersectsValueSet(
                        schema, typeID, configuration, candidate.id,
                        *selectedSet, type.suppressedTransitionAlternatives,
                        intersects);
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
            std::sort(selectedBins.begin(), selectedBins.end());
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
            for (const FunctionalCoverageBinState &candidate : type.bins) {
              constexpr uint32_t omitted =
                  obelisk::coverage::FunctionalBinDefault |
                  obelisk::coverage::FunctionalBinDefaultSequence |
                  obelisk::coverage::FunctionalBinIgnore |
                  obelisk::coverage::FunctionalBinIllegal |
                  obelisk::coverage::FunctionalBinEmpty;
              if (candidate.item == cross.targets[targetOrdinal] &&
                  !(candidate.flags & omitted))
                targetBins[targetOrdinal].push_back(candidate.id);
            }
            std::sort(targetBins[targetOrdinal].begin(),
                      targetBins[targetOrdinal].end());
          }
          std::vector<std::vector<uint64_t>> candidates;
          selectorStatus = resolvedCrossSelectorSetCandidates(
              schema, typeID, configuration, cross.id, *selector, targetBins,
              type.suppressedTransitionAlternatives, candidates);
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
    type.crosses.push_back(std::move(cross));
  }
  if (type.items.empty())
    return OBELISK_RT_INVALID_DESIGN;
  obelisk_rt_status typeOptionStatus =
      applyFunctionalTypeOptions(coverage, typeID, type);
  if (typeOptionStatus != OBELISK_RT_OK)
    return typeOptionStatus;
  obelisk_rt_status overlapStatus =
      emitFunctionalOverlapWarnings(schema, typeID, configuration);
  if (overlapStatus != OBELISK_RT_OK)
    return overlapStatus;
  auto insertion = coverage.types.emplace(key, std::move(type));
  result = &insertion.first->second;
  return OBELISK_RT_OK;
}

obelisk_rt_status
bindFunctionalType(obelisk_rt_context *context, CoverageState &coverage,
                   uint64_t typeID, FunctionalCoverageTypeState *&result,
                   const obelisk_rt_functional_value_v1 *expressions = nullptr,
                   uint64_t expressionCount = 0) {
  if (!coverage.schema)
    return OBELISK_RT_INVALID_DESIGN;

  if (!expressions && !expressionCount) {
    const obelisk::coverage::FunctionalConfiguration *only = nullptr;
    for (const auto &configuration :
         coverage.schema->functionalConfigurations) {
      if (configuration.type != typeID)
        continue;
      if (only)
        return OBELISK_RT_INVALID_DESIGN;
      only = &configuration;
    }
    if (only)
      return bindResolvedFunctionalType(coverage, typeID, only->configuration,
                                        result);
  }

  // Resolution is transactional: malformed constructor semantics must never
  // leave a half-bound schema or configuration cache visible to later calls.
  obelisk::coverage::Database resolved = *coverage.schema;
  obelisk::coverage::Digest exact{};
  obelisk_rt_status status = resolveStaticFunctionalType(
      context, resolved, typeID, expressions, expressionCount, exact);
  if (status != OBELISK_RT_OK)
    return status;
  obelisk::coverage::Diagnostic diagnostic;
  if (obelisk::coverage::validate(resolved, &diagnostic) !=
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
  *coverage.schema = std::move(resolved);
  return bindResolvedFunctionalType(coverage, typeID, exact, result);
}

uint64_t ownerType(const obelisk::coverage::Database &schema,
                   const obelisk::coverage::FunctionalExpression &expression) {
  using Owner = obelisk::coverage::FunctionalExpressionOwnerKind;
  if (expression.ownerKind == Owner::Type)
    return expression.owner;
  if (expression.ownerKind == Owner::Formal) {
    auto formal = std::find_if(
        schema.functionalFormals.begin(), schema.functionalFormals.end(),
        [&](const auto &entry) { return entry.id == expression.owner; });
    return formal == schema.functionalFormals.end() ? 0 : formal->type;
  }
  uint64_t itemID = expression.owner;
  if (expression.ownerKind == Owner::Bin) {
    auto bin = std::find_if(
        schema.functionalBins.begin(), schema.functionalBins.end(),
        [&](const auto &entry) { return entry.id == expression.owner; });
    itemID = bin == schema.functionalBins.end() ? 0 : bin->item;
  } else if (expression.ownerKind == Owner::ValueSet) {
    auto set = std::find_if(
        schema.functionalValueSets.begin(), schema.functionalValueSets.end(),
        [&](const auto &entry) { return entry.id == expression.owner; });
    itemID = set == schema.functionalValueSets.end() ? 0 : set->item;
  } else if (expression.ownerKind == Owner::Selector) {
    auto selector = std::find_if(
        schema.crossSelectorNodes.begin(), schema.crossSelectorNodes.end(),
        [&](const auto &entry) { return entry.id == expression.owner; });
    itemID = selector == schema.crossSelectorNodes.end() ? 0 : selector->cross;
  }
  auto item =
      std::find_if(schema.functionalItems.begin(), schema.functionalItems.end(),
                   [&](const auto &entry) { return entry.id == itemID; });
  return item == schema.functionalItems.end() ? 0 : item->type;
}

bool validateValue(obelisk_rt_context *context,
                   const obelisk_rt_functional_value_v1 &value,
                   obelisk::coverage::FunctionalExpressionResultKind result,
                   uint64_t width, bool reference) {
  using Result = obelisk::coverage::FunctionalExpressionResultKind;
  // Boolean schema descriptors have no integral type width, but their helper
  // ABI representation is the canonical one-bit logic value.
  if (result == Result::Boolean)
    width = 1;
  if (!value.id)
    return false;
  if (reference) {
    return value.kind == OBELISK_RT_FUNCTIONAL_VALUE_ARGUMENT_REF &&
           value.bit_width == width && value.value_size == (width + 7) / 8 &&
           !value.value && !value.unknown && value.argument_ref_kind <= 2 &&
           (value.argument_ref_kind == 0 || value.owner);
  }
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
  if (result == Result::Real)
    return value.kind == OBELISK_RT_FUNCTIONAL_VALUE_REAL &&
           value.bit_width == 64 && value.value_size == sizeof(double) &&
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
      source.kind == OBELISK_RT_FUNCTIONAL_VALUE_MANAGED_CONTAINER)
    return;
  // String constructor inputs are consumed by constructor/option helpers and
  // are not readable sample formals. Do not retain an unrooted managed word in
  // the long-lived coverage service after construction.
  if (source.kind == OBELISK_RT_FUNCTIONAL_VALUE_STRING) {
    destination.payload = 0;
    return;
  }
  auto *value = static_cast<const uint8_t *>(source.value);
  destination.value.assign(value, value + source.value_size);
  if (source.unknown) {
    auto *unknown = static_cast<const uint8_t *>(source.unknown);
    destination.unknown.assign(unknown, unknown + source.value_size);
  }
}

template <typename Entry>
std::vector<const Entry *>
schemaOrdered(const std::vector<Entry> &entries, uint64_t typeID,
              const obelisk::coverage::Database &schema, bool constructor) {
  std::vector<const Entry *> result;
  for (const auto &entry : entries) {
    if constexpr (std::is_same_v<Entry, obelisk::coverage::FunctionalFormal>) {
      if (entry.type == typeID &&
          (entry.kind ==
           obelisk::coverage::FunctionalFormalKind::Constructor) == constructor)
        result.push_back(&entry);
    } else if (ownerType(schema, entry) == typeID) {
      using Phase = obelisk::coverage::FunctionalExpressionEvaluationPhase;
      const bool selected =
          constructor ? (entry.evaluationPhase == Phase::Constructor ||
                         entry.evaluationPhase == Phase::Option)
                      : entry.evaluationPhase == Phase::Sample;
      if (selected)
        result.push_back(&entry);
    }
  }
  std::sort(result.begin(), result.end(), [](const auto *a, const auto *b) {
    if constexpr (std::is_same_v<Entry, obelisk::coverage::FunctionalFormal>)
      return a->ordinal < b->ordinal;
    else if (a->evaluationPhase != b->evaluationPhase)
      return a->evaluationPhase < b->evaluationPhase;
    else
      return a->resultOrdinal < b->resultOrdinal;
  });
  return result;
}

uint64_t readPlaneLimb(const void *plane, uint64_t size, uint32_t ordinal) {
  return readLittleEndianLimb(plane, size, ordinal);
}

bool functionalBooleanTrue(const obelisk_rt_functional_value_v1 &value) {
  uint64_t unknown = readPlaneLimb(value.unknown, value.value_size, 0);
  uint64_t encoded = readPlaneLimb(value.value, value.value_size, 0);
  // !obelisk_sim.logic transports (aval ^ bval, bval).  IEEE boolean
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
                     const obelisk::coverage::Digest &configuration,
                     uint64_t setID,
                     const obelisk_rt_functional_value_v1 &value) {
  auto set = std::find_if(
      schema.resolvedFunctionalValueSets.begin(),
      schema.resolvedFunctionalValueSets.end(), [&](const auto &entry) {
        return entry.id == setID && entry.configuration == configuration;
      });
  if (set == schema.resolvedFunctionalValueSets.end() ||
      set->bitWidth != value.bit_width)
    return false;
  for (uint32_t atomIndex = 0; atomIndex != set->atomCount; ++atomIndex) {
    const auto &atom =
        schema.resolvedFunctionalValueAtoms[set->firstAtom + atomIndex];
    if (atom.kind == obelisk::coverage::FunctionalValueAtomKind::RealInterval) {
      if (value.kind != OBELISK_RT_FUNCTIONAL_VALUE_REAL)
        continue;
      double actual = 0.0, low = 0.0, high = 0.0;
      std::memcpy(&actual, value.value, sizeof(actual));
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
    if (value.kind == OBELISK_RT_FUNCTIONAL_VALUE_REAL)
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

bool integralCoverageShapesIntersect(const IntegralCoverageShape &left,
                                     const IntegralCoverageShape &right,
                                     uint32_t bitWidth) {
  if (left.interval && right.interval)
    return compareIntegralBitVectors(left.high, right.low) >= 0 &&
           compareIntegralBitVectors(right.high, left.low) >= 0;
  if (!left.interval && !right.interval) {
    for (uint32_t ordinal = 0; ordinal != left.value.size(); ++ordinal)
      if ((left.value[ordinal] ^ right.value[ordinal]) &
          ~left.wildcard[ordinal] & ~right.wildcard[ordinal])
        return false;
    return true;
  }

  const IntegralCoverageShape &interval = left.interval ? left : right;
  const IntegralCoverageShape &cube = left.interval ? right : left;
  // Digit-DP over the signed-order-biased bit vectors. The two comparison
  // states track the candidate against the inclusive low/high bounds; only
  // nine states are possible regardless of the coverpoint width.
  bool states[3][3] = {};
  states[1][1] = true;
  auto compareBit = [](int relation, bool candidate, bool bound) {
    if (relation)
      return relation;
    if (candidate == bound)
      return 0;
    return candidate ? 1 : -1;
  };
  for (uint32_t reverse = bitWidth; reverse != 0; --reverse) {
    const uint32_t bit = reverse - 1;
    const uint32_t limb = bit / 64;
    const uint64_t mask = uint64_t{1} << (bit % 64);
    const bool wildcard = (cube.wildcard[limb] & mask) != 0;
    const bool cubeBit = (cube.value[limb] & mask) != 0;
    const bool lowBit = (interval.low[limb] & mask) != 0;
    const bool highBit = (interval.high[limb] & mask) != 0;
    bool next[3][3] = {};
    for (int lowRelation = -1; lowRelation <= 1; ++lowRelation)
      for (int highRelation = -1; highRelation <= 1; ++highRelation) {
        if (!states[lowRelation + 1][highRelation + 1])
          continue;
        for (unsigned candidate = 0; candidate != 2; ++candidate) {
          if (!wildcard && bool(candidate) != cubeBit)
            continue;
          const int nextLow = compareBit(lowRelation, candidate != 0, lowBit);
          const int nextHigh =
              compareBit(highRelation, candidate != 0, highBit);
          if (nextLow < 0 || nextHigh > 0)
            continue;
          next[nextLow + 1][nextHigh + 1] = true;
        }
      }
    for (unsigned low = 0; low != 3; ++low)
      for (unsigned high = 0; high != 3; ++high)
        states[low][high] = next[low][high];
  }
  for (int lowRelation = 0; lowRelation <= 1; ++lowRelation)
    for (int highRelation = -1; highRelation <= 0; ++highRelation)
      if (states[lowRelation + 1][highRelation + 1])
        return true;
  return false;
}

bool resolvedFunctionalValueSetsIntersect(
    const obelisk::coverage::Database &schema,
    const obelisk::coverage::ResolvedFunctionalValueSet &left,
    const obelisk::coverage::ResolvedFunctionalValueSet &right) {
  using namespace obelisk::coverage;
  if (left.kind != right.kind || left.bitWidth != right.bitWidth ||
      left.signedness != right.signedness)
    return false;
  auto realAtomsIntersect = [](const ResolvedFunctionalValueAtom &lhs,
                               const ResolvedFunctionalValueAtom &rhs) {
    double lhsLow = 0.0, lhsHigh = 0.0, rhsLow = 0.0, rhsHigh = 0.0;
    std::memcpy(&lhsLow, &lhs.realLowBits, sizeof(lhsLow));
    std::memcpy(&lhsHigh, &lhs.realHighBits, sizeof(lhsHigh));
    std::memcpy(&rhsLow, &rhs.realLowBits, sizeof(rhsLow));
    std::memcpy(&rhsHigh, &rhs.realHighBits, sizeof(rhsHigh));
    const bool lhsUpperUnbounded =
        lhs.flags & FunctionalValueAtomUpperUnbounded;
    const bool lhsLowerUnbounded =
        lhs.flags & FunctionalValueAtomLowerUnbounded;
    const bool rhsUpperUnbounded =
        rhs.flags & FunctionalValueAtomUpperUnbounded;
    const bool rhsLowerUnbounded =
        rhs.flags & FunctionalValueAtomLowerUnbounded;
    if (!lhsUpperUnbounded && !rhsLowerUnbounded &&
        (lhsHigh < rhsLow ||
         (lhsHigh == rhsLow &&
          (!(lhs.flags & FunctionalValueAtomUpperInclusive) ||
           !(rhs.flags & FunctionalValueAtomLowerInclusive)))))
      return false;
    if (!rhsUpperUnbounded && !lhsLowerUnbounded &&
        (rhsHigh < lhsLow ||
         (rhsHigh == lhsLow &&
          (!(rhs.flags & FunctionalValueAtomUpperInclusive) ||
           !(lhs.flags & FunctionalValueAtomLowerInclusive)))))
      return false;
    return true;
  };
  if (left.kind == FunctionalValueSetKind::Real) {
    for (uint32_t leftIndex = 0; leftIndex != left.atomCount; ++leftIndex)
      for (uint32_t rightIndex = 0; rightIndex != right.atomCount; ++rightIndex)
        if (realAtomsIntersect(
                schema.resolvedFunctionalValueAtoms[left.firstAtom + leftIndex],
                schema.resolvedFunctionalValueAtoms[right.firstAtom +
                                                    rightIndex]))
          return true;
    return false;
  }
  if (left.kind != FunctionalValueSetKind::Integral)
    return false;
  auto integralCubesIntersect = [&](const ResolvedFunctionalValueAtom &lhs,
                                    const ResolvedFunctionalValueAtom &rhs) {
    if (lhs.kind != FunctionalValueAtomKind::IntegralValue ||
        rhs.kind != FunctionalValueAtomKind::IntegralValue ||
        lhs.limbCount != rhs.limbCount)
      return false;
    for (uint32_t ordinal = 0; ordinal != lhs.limbCount; ++ordinal) {
      const auto &leftLimb =
          schema.resolvedFunctionalValueLimbs[lhs.firstLimb + ordinal];
      const auto &rightLimb =
          schema.resolvedFunctionalValueLimbs[rhs.firstLimb + ordinal];
      const uint64_t valid = ordinal + 1 == lhs.limbCount && left.bitWidth % 64
                                 ? lowBitsMask(left.bitWidth % 64)
                                 : UINT64_MAX;
      const uint64_t leftWildcard = leftLimb.wildcardMask & valid;
      const uint64_t rightWildcard = rightLimb.wildcardMask & valid;
      const uint64_t leftBval = leftLimb.lowBval & ~leftWildcard & valid;
      const uint64_t rightBval = rightLimb.lowBval & ~rightWildcard & valid;
      const uint64_t jointlyConstrained =
          ~(leftWildcard | rightWildcard) & valid;
      // Wildcard-bin X/Z/? positions expand only to known 0 and 1. They
      // therefore cannot intersect a constrained X/Z value from the other
      // cube. At jointly constrained positions, case equality defines the
      // exact four-state intersection.
      if ((leftBval & rightWildcard) || (rightBval & leftWildcard) ||
          ((leftLimb.lowAval ^ rightLimb.lowAval) & jointlyConstrained) ||
          ((leftBval ^ rightBval) & jointlyConstrained))
        return false;
    }
    return true;
  };
  for (uint32_t leftIndex = 0; leftIndex != left.atomCount; ++leftIndex) {
    const auto &leftAtom =
        schema.resolvedFunctionalValueAtoms[left.firstAtom + leftIndex];
    IntegralCoverageShape leftShape;
    for (uint32_t rightIndex = 0; rightIndex != right.atomCount; ++rightIndex) {
      const auto &rightAtom =
          schema.resolvedFunctionalValueAtoms[right.firstAtom + rightIndex];
      if (leftAtom.kind == FunctionalValueAtomKind::IntegralValue &&
          rightAtom.kind == FunctionalValueAtomKind::IntegralValue &&
          integralCubesIntersect(leftAtom, rightAtom))
        return true;
      if (!makeIntegralCoverageShape(schema, leftAtom, left.bitWidth,
                                     left.signedness, leftShape))
        continue;
      IntegralCoverageShape rightShape;
      if (!makeIntegralCoverageShape(schema, rightAtom, right.bitWidth,
                                     right.signedness, rightShape))
        continue;
      if (integralCoverageShapesIntersect(leftShape, rightShape, left.bitWidth))
        return true;
    }
  }
  return false;
}

/// Apply IEEE 1800-2023 19.6.1.1 value-set selection to one resolved
/// coverpoint bin. State bins use their value set. Transition bins use the
/// last step of every surviving transition alternative; the cross selects the
/// bin when any such terminal value set intersects the requested set.
obelisk_rt_status resolvedFunctionalBinIntersectsValueSet(
    const obelisk::coverage::Database &schema, uint64_t type,
    const obelisk::coverage::Digest &configuration, uint64_t binID,
    const obelisk::coverage::ResolvedFunctionalValueSet &selection,
    const std::unordered_set<uint64_t> &suppressedTransitionAlternatives,
    bool &result) {
  using namespace obelisk::coverage;
  result = false;
  if (selection.type != type || selection.configuration != configuration)
    return OBELISK_RT_INVALID_DESIGN;
  auto bin = std::find_if(schema.resolvedFunctionalBins.begin(),
                          schema.resolvedFunctionalBins.end(),
                          [&](const auto &candidate) {
                            return candidate.type == type &&
                                   candidate.configuration == configuration &&
                                   candidate.id == binID;
                          });
  if (bin == schema.resolvedFunctionalBins.end())
    return OBELISK_RT_INVALID_DESIGN;

  if (bin->kind == FunctionalBinKind::State) {
    auto plan = std::find_if(
        schema.resolvedFunctionalBinPlans.begin(),
        schema.resolvedFunctionalBinPlans.end(), [&](const auto &candidate) {
          return candidate.type == type &&
                 candidate.configuration == configuration &&
                 candidate.bin == binID;
        });
    if (plan == schema.resolvedFunctionalBinPlans.end() || !plan->valueSet)
      return OBELISK_RT_INVALID_DESIGN;
    const ResolvedFunctionalValueSet *valueSet =
        findResolvedValueSet(schema, configuration, plan->valueSet);
    if (!valueSet || valueSet->type != type)
      return OBELISK_RT_INVALID_DESIGN;
    result = resolvedFunctionalValueSetsIntersect(schema, *valueSet, selection);
    return OBELISK_RT_OK;
  }
  if (bin->kind != FunctionalBinKind::Transition)
    return OBELISK_RT_INVALID_DESIGN;

  bool sawAlternative = false;
  for (const ResolvedTransitionAlternative &alternative :
       schema.resolvedTransitionAlternatives) {
    if (alternative.type != type ||
        alternative.configuration != configuration || alternative.bin != binID)
      continue;
    sawAlternative = true;
    if (!alternative.stepCount ||
        uint64_t{alternative.firstStep} + alternative.stepCount >
            schema.resolvedTransitionSteps.size())
      return OBELISK_RT_INVALID_DESIGN;
    const ResolvedTransitionStep &terminal =
        schema.resolvedTransitionSteps[alternative.firstStep +
                                       alternative.stepCount - 1];
    if (terminal.type != type || terminal.configuration != configuration ||
        terminal.bin != binID || terminal.alternative != alternative.id ||
        terminal.ordinal != alternative.stepCount - 1)
      return OBELISK_RT_INVALID_DESIGN;
    if (suppressedTransitionAlternatives.count(alternative.id))
      continue;
    const ResolvedFunctionalValueSet *terminalSet =
        findResolvedValueSet(schema, configuration, terminal.valueSet);
    if (!terminalSet || terminalSet->type != type)
      return OBELISK_RT_INVALID_DESIGN;
    if (resolvedFunctionalValueSetsIntersect(schema, *terminalSet, selection)) {
      result = true;
      return OBELISK_RT_OK;
    }
  }
  return sawAlternative ? OBELISK_RT_OK : OBELISK_RT_INVALID_DESIGN;
}

bool crossSelectorUsesWith(const obelisk::coverage::Database &schema,
                           const obelisk::coverage::CrossSelectorNode &root) {
  std::vector<const obelisk::coverage::CrossSelectorNode *> worklist{&root};
  std::unordered_set<uint64_t> visited;
  while (!worklist.empty()) {
    const auto *node = worklist.back();
    worklist.pop_back();
    if (!visited.insert(node->id).second)
      continue;
    if (node->withExpression)
      return true;
    for (uint32_t ordinal = 0; ordinal != node->operandCount; ++ordinal) {
      if (uint64_t{node->firstOperand} + ordinal >=
          schema.crossSelectorOperands.size())
        return false;
      uint64_t childID =
          schema.crossSelectorOperands[node->firstOperand + ordinal].operand;
      auto child = std::find_if(
          schema.crossSelectorNodes.begin(), schema.crossSelectorNodes.end(),
          [&](const auto &candidate) {
            return candidate.id == childID && candidate.cross == root.cross;
          });
      if (child == schema.crossSelectorNodes.end())
        return false;
      worklist.push_back(&*child);
    }
  }
  return false;
}

bool crossSelectorIsWithWrappedSet(
    const obelisk::coverage::Database &schema,
    const obelisk::coverage::CrossSelectorNode &root) {
  using namespace obelisk::coverage;
  const CrossSelectorNode *node = &root;
  std::unordered_set<uint64_t> visited;
  while (node->kind == CrossSelectorKind::With) {
    if (!visited.insert(node->id).second || node->operandCount != 1 ||
        uint64_t{node->firstOperand} >= schema.crossSelectorOperands.size())
      return false;
    const CrossSelectorOperand &operand =
        schema.crossSelectorOperands[node->firstOperand];
    if (operand.node != node->id || operand.ordinal != 0)
      return false;
    auto child = std::find_if(schema.crossSelectorNodes.begin(),
                              schema.crossSelectorNodes.end(),
                              [&](const CrossSelectorNode &candidate) {
                                return candidate.id == operand.operand &&
                                       candidate.cross == root.cross;
                              });
    if (child == schema.crossSelectorNodes.end())
      return false;
    node = &*child;
  }
  return node->kind == CrossSelectorKind::Set;
}

bool crossSelectorUsesSet(const obelisk::coverage::Database &schema,
                          const obelisk::coverage::CrossSelectorNode &root) {
  std::vector<const obelisk::coverage::CrossSelectorNode *> worklist{&root};
  std::unordered_set<uint64_t> visited;
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
      auto child = std::find_if(
          schema.crossSelectorNodes.begin(), schema.crossSelectorNodes.end(),
          [&](const auto &candidate) {
            return candidate.id == childID && candidate.cross == root.cross;
          });
      if (child == schema.crossSelectorNodes.end())
        return false;
      worklist.push_back(&*child);
    }
  }
  return false;
}

obelisk_rt_status resolvedFunctionalBinCardinalityCapped(
    const obelisk::coverage::Database &schema, uint64_t type,
    const obelisk::coverage::Digest &configuration, uint64_t binID,
    uint64_t cap,
    const std::unordered_set<uint64_t> &suppressedTransitionAlternatives,
    uint64_t &result) {
  using namespace obelisk::coverage;
  result = 0;
  if (!cap)
    return OBELISK_RT_INVALID_ARGUMENT;
  auto bin = std::find_if(schema.resolvedFunctionalBins.begin(),
                          schema.resolvedFunctionalBins.end(),
                          [&](const auto &candidate) {
                            return candidate.type == type &&
                                   candidate.configuration == configuration &&
                                   candidate.id == binID;
                          });
  if (bin == schema.resolvedFunctionalBins.end())
    return OBELISK_RT_INVALID_DESIGN;
  std::vector<const ResolvedFunctionalValueSet *> sets;
  if (bin->kind == FunctionalBinKind::State) {
    auto plan = std::find_if(
        schema.resolvedFunctionalBinPlans.begin(),
        schema.resolvedFunctionalBinPlans.end(), [&](const auto &candidate) {
          return candidate.type == type &&
                 candidate.configuration == configuration &&
                 candidate.bin == binID;
        });
    const auto *set =
        plan == schema.resolvedFunctionalBinPlans.end()
            ? nullptr
            : findResolvedValueSet(schema, configuration, plan->valueSet);
    if (!set || set->type != type)
      return OBELISK_RT_INVALID_DESIGN;
    sets.push_back(set);
  } else if (bin->kind == FunctionalBinKind::Transition) {
    bool sawAlternative = false;
    for (const ResolvedTransitionAlternative &alternative :
         schema.resolvedTransitionAlternatives) {
      if (alternative.type != type ||
          alternative.configuration != configuration ||
          alternative.bin != binID)
        continue;
      sawAlternative = true;
      if (!alternative.stepCount ||
          uint64_t{alternative.firstStep} + alternative.stepCount >
              schema.resolvedTransitionSteps.size())
        return OBELISK_RT_INVALID_DESIGN;
      if (suppressedTransitionAlternatives.count(alternative.id))
        continue;
      const auto &terminal =
          schema.resolvedTransitionSteps[alternative.firstStep +
                                         alternative.stepCount - 1];
      const auto *set =
          findResolvedValueSet(schema, configuration, terminal.valueSet);
      if (!set || set->type != type)
        return OBELISK_RT_INVALID_DESIGN;
      sets.push_back(set);
    }
    if (!sawAlternative)
      return OBELISK_RT_INVALID_DESIGN;
  } else {
    return OBELISK_RT_INVALID_DESIGN;
  }
  if (sets.empty())
    return OBELISK_RT_OK;

  if (sets.front()->kind == FunctionalValueSetKind::Real) {
    std::set<double> singletons;
    for (const ResolvedFunctionalValueSet *set : sets) {
      if (set->kind != FunctionalValueSetKind::Real)
        return OBELISK_RT_INVALID_DESIGN;
      for (uint32_t ordinal = 0; ordinal != set->atomCount; ++ordinal) {
        const auto &atom =
            schema.resolvedFunctionalValueAtoms[set->firstAtom + ordinal];
        double low = 0.0, high = 0.0;
        std::memcpy(&low, &atom.realLowBits, sizeof(low));
        std::memcpy(&high, &atom.realHighBits, sizeof(high));
        if (low != high || atom.flags != (FunctionalValueAtomLowerInclusive |
                                          FunctionalValueAtomUpperInclusive)) {
          result = cap;
          return OBELISK_RT_OK;
        }
        singletons.insert(low == 0.0 ? 0.0 : low);
        if (singletons.size() >= cap) {
          result = cap;
          return OBELISK_RT_OK;
        }
      }
    }
    result = singletons.size();
    return OBELISK_RT_OK;
  }

  std::vector<IntegralCoverageShape> shapes;
  std::set<std::vector<uint64_t>> exactFourStateValues;
  for (const ResolvedFunctionalValueSet *set : sets) {
    if (set->kind != FunctionalValueSetKind::Integral ||
        set->bitWidth != sets.front()->bitWidth ||
        set->signedness != sets.front()->signedness)
      return OBELISK_RT_INVALID_DESIGN;
    for (uint32_t ordinal = 0; ordinal != set->atomCount; ++ordinal) {
      const auto &atom =
          schema.resolvedFunctionalValueAtoms[set->firstAtom + ordinal];
      bool exactFourState = atom.kind == FunctionalValueAtomKind::IntegralValue;
      bool hasBval = false;
      std::vector<uint64_t> exactKey;
      if (exactFourState) {
        exactKey.reserve(atom.limbCount * 2);
        for (uint32_t limbOrdinal = 0; limbOrdinal != atom.limbCount;
             ++limbOrdinal) {
          const auto &limb =
              schema.resolvedFunctionalValueLimbs[atom.firstLimb + limbOrdinal];
          exactFourState &= limb.wildcardMask == 0;
          hasBval |= limb.lowBval != 0;
          exactKey.push_back(limb.lowAval);
          exactKey.push_back(limb.lowBval);
        }
      }
      if (exactFourState && hasBval) {
        exactFourStateValues.insert(std::move(exactKey));
        if (exactFourStateValues.size() >= cap) {
          result = cap;
          return OBELISK_RT_OK;
        }
        continue;
      }
      IntegralCoverageShape shape;
      if (!makeIntegralCoverageShape(schema, atom, set->bitWidth,
                                     set->signedness, shape))
        return OBELISK_RT_INVALID_DESIGN;
      shapes.push_back(std::move(shape));
    }
  }
  uint64_t knownCardinality = 0;
  obelisk_rt_status status = integralShapeUnionCardinalityCapped(
      shapes, sets.front()->bitWidth, cap, knownCardinality);
  if (status != OBELISK_RT_OK)
    return status;
  result = knownCardinality >=
                   cap - std::min<uint64_t>(exactFourStateValues.size(), cap)
               ? cap
               : knownCardinality + exactFourStateValues.size();
  return OBELISK_RT_OK;
}

obelisk_rt_status resolvedCrossSelectorSelectsBinTuple(
    const obelisk::coverage::Database &schema, uint64_t type,
    const obelisk::coverage::Digest &configuration, uint64_t resolvedCross,
    const obelisk::coverage::CrossSelectorNode &root,
    const std::vector<uint64_t> &binTuple,
    const std::unordered_set<uint64_t> &suppressedTransitionAlternatives,
    bool &result) {
  using namespace obelisk::coverage;
  result = false;
  auto cross = std::find_if(
      schema.resolvedFunctionalItems.begin(),
      schema.resolvedFunctionalItems.end(), [&](const auto &candidate) {
        return candidate.type == type &&
               candidate.configuration == configuration &&
               candidate.id == resolvedCross &&
               candidate.kind == FunctionalItemKind::Cross;
      });
  if (cross == schema.resolvedFunctionalItems.end() ||
      root.cross != cross->templateItem)
    return OBELISK_RT_INVALID_DESIGN;
  auto plan = std::find_if(schema.crossPlans.begin(), schema.crossPlans.end(),
                           [&](const CrossPlan &candidate) {
                             return candidate.item == cross->templateItem;
                           });
  if (plan == schema.crossPlans.end() || binTuple.size() != plan->targetCount ||
      uint64_t{plan->firstTarget} + plan->targetCount >
          schema.crossTargets.size())
    return OBELISK_RT_INVALID_DESIGN;

  std::unordered_map<uint64_t, const CrossSelectorNode *> nodes;
  for (const CrossSelectorNode &node : schema.crossSelectorNodes)
    if (node.cross == root.cross)
      nodes.emplace(node.id, &node);
  std::vector<uint64_t> resolvedTargets;
  resolvedTargets.reserve(plan->targetCount);
  for (uint32_t ordinal = 0; ordinal != plan->targetCount; ++ordinal) {
    const CrossTarget &target =
        schema.crossTargets[plan->firstTarget + ordinal];
    auto resolvedTarget = std::find_if(
        schema.resolvedFunctionalItems.begin(),
        schema.resolvedFunctionalItems.end(), [&](const auto &candidate) {
          return candidate.type == type &&
                 candidate.configuration == configuration &&
                 candidate.templateItem == target.target &&
                 candidate.kind == FunctionalItemKind::Coverpoint;
        });
    if (target.cross != root.cross || target.ordinal != ordinal ||
        resolvedTarget == schema.resolvedFunctionalItems.end())
      return OBELISK_RT_INVALID_DESIGN;
    auto bin = std::find_if(schema.resolvedFunctionalBins.begin(),
                            schema.resolvedFunctionalBins.end(),
                            [&](const auto &candidate) {
                              return candidate.type == type &&
                                     candidate.configuration == configuration &&
                                     candidate.id == binTuple[ordinal] &&
                                     candidate.item == resolvedTarget->id;
                            });
    if (bin == schema.resolvedFunctionalBins.end())
      return OBELISK_RT_INVALID_DESIGN;
    resolvedTargets.push_back(resolvedTarget->id);
  }

  auto bindingFor = [&](uint64_t node) {
    return std::find_if(schema.resolvedCrossSelectorBindings.begin(),
                        schema.resolvedCrossSelectorBindings.end(),
                        [&](const auto &binding) {
                          return binding.type == type &&
                                 binding.configuration == configuration &&
                                 binding.cross == resolvedCross &&
                                 binding.node == node;
                        });
  };
  std::unordered_map<uint64_t, bool> memo;
  std::unordered_set<uint64_t> evaluating;
  std::function<obelisk_rt_status(const CrossSelectorNode &, bool &)> evaluate =
      [&](const CrossSelectorNode &node, bool &selected) -> obelisk_rt_status {
    auto memoized = memo.find(node.id);
    if (memoized != memo.end()) {
      selected = memoized->second;
      return OBELISK_RT_OK;
    }
    if (!evaluating.insert(node.id).second)
      return OBELISK_RT_INVALID_DESIGN;
    auto finish = [&](bool value) {
      evaluating.erase(node.id);
      memo.emplace(node.id, value);
      selected = value;
      return OBELISK_RT_OK;
    };
    bool base = false;
    if (node.kind == CrossSelectorKind::Binsof) {
      auto target = std::find_if(
          schema.crossTargets.begin() + plan->firstTarget,
          schema.crossTargets.begin() + plan->firstTarget + plan->targetCount,
          [&](const CrossTarget &candidate) {
            return candidate.target == node.target;
          });
      if (target ==
          schema.crossTargets.begin() + plan->firstTarget + plan->targetCount)
        return OBELISK_RT_INVALID_DESIGN;
      auto bin = std::find_if(
          schema.resolvedFunctionalBins.begin(),
          schema.resolvedFunctionalBins.end(), [&](const auto &candidate) {
            return candidate.type == type &&
                   candidate.configuration == configuration &&
                   candidate.id == binTuple[target->ordinal];
          });
      if (bin == schema.resolvedFunctionalBins.end())
        return OBELISK_RT_INVALID_DESIGN;
      base = !node.bin || bin->templateBin == node.bin;
      if (base && node.valueSet) {
        auto binding = bindingFor(node.id);
        if (binding == schema.resolvedCrossSelectorBindings.end() ||
            !binding->valueSet)
          return OBELISK_RT_INVALID_DESIGN;
        auto set = std::find_if(schema.resolvedFunctionalValueSets.begin(),
                                schema.resolvedFunctionalValueSets.end(),
                                [&](const auto &entry) {
                                  return entry.type == type &&
                                         entry.configuration == configuration &&
                                         entry.id == binding->valueSet;
                                });
        if (set == schema.resolvedFunctionalValueSets.end())
          return OBELISK_RT_INVALID_DESIGN;
        obelisk_rt_status status = resolvedFunctionalBinIntersectsValueSet(
            schema, type, configuration, bin->id, *set,
            suppressedTransitionAlternatives, base);
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
        auto child = nodes.find(operand.operand);
        if (operand.node != node.id || operand.ordinal != ordinal ||
            child == nodes.end())
          return OBELISK_RT_INVALID_DESIGN;
        obelisk_rt_status status = evaluate(*child->second, operands[ordinal]);
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
    if (!base || (!node.withExpression && !node.constructionExpression))
      return finish(base);

    auto binding = bindingFor(node.id);
    if (binding == schema.resolvedCrossSelectorBindings.end() ||
        binding->withExpression != node.withExpression || !binding->tupleSet)
      return OBELISK_RT_INVALID_DESIGN;
    auto tupleSet = std::find_if(
        schema.resolvedFunctionalTupleSets.begin(),
        schema.resolvedFunctionalTupleSets.end(), [&](const auto &set) {
          return set.type == type && set.configuration == configuration &&
                 set.id == binding->tupleSet && set.selector == node.id &&
                 set.elementMode == FunctionalTupleElementMode::ValueTuple;
        });
    if (tupleSet == schema.resolvedFunctionalTupleSets.end() ||
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
        auto set = std::find_if(schema.resolvedFunctionalValueSets.begin(),
                                schema.resolvedFunctionalValueSets.end(),
                                [&](const auto &entry) {
                                  return entry.type == type &&
                                         entry.configuration == configuration &&
                                         entry.id == component.valueSet;
                                });
        if (component.ordinal != componentOrdinal ||
            component.target != resolvedTargets[componentOrdinal] ||
            set == schema.resolvedFunctionalValueSets.end())
          return OBELISK_RT_INVALID_DESIGN;
        bool intersects = false;
        obelisk_rt_status status = resolvedFunctionalBinIntersectsValueSet(
            schema, type, configuration, binTuple[componentOrdinal], *set,
            suppressedTransitionAlternatives, intersects);
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
      return finish(satisfying >= binding->matchesCount);
    if (binding->matchesPolicy != CrossMatchesPolicy::All ||
        binding->matchesCount)
      return OBELISK_RT_INVALID_DESIGN;

    const uint64_t cap = uint64_t{tupleSet->tupleCount} + 1;
    uint64_t total = 1;
    for (uint32_t ordinal = 0; ordinal != plan->targetCount; ++ordinal) {
      uint64_t containedValues = 0;
      obelisk_rt_status status = resolvedFunctionalBinCardinalityCapped(
          schema, type, configuration, binTuple[ordinal], cap,
          suppressedTransitionAlternatives, containedValues);
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
    std::vector<std::vector<uint64_t>> &result) {
  using namespace obelisk::coverage;
  result.clear();
  const uint64_t limit = ParseLimits{}.maxRecords;
  std::set<std::vector<uint64_t>> unique;
  std::vector<const CrossSelectorNode *> worklist{&root};
  std::unordered_set<uint64_t> visited;
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
        auto child = std::find_if(
            schema.crossSelectorNodes.begin(), schema.crossSelectorNodes.end(),
            [&](const auto &candidate) {
              return candidate.id == childID && candidate.cross == root.cross;
            });
        if (child == schema.crossSelectorNodes.end())
          return OBELISK_RT_INVALID_DESIGN;
        worklist.push_back(&*child);
      }
      continue;
    }
    auto binding = std::find_if(
        schema.resolvedCrossSelectorBindings.begin(),
        schema.resolvedCrossSelectorBindings.end(), [&](const auto &entry) {
          return entry.type == type && entry.configuration == configuration &&
                 entry.cross == resolvedCross && entry.node == node->id;
        });
    auto tupleSet =
        binding == schema.resolvedCrossSelectorBindings.end()
            ? schema.resolvedFunctionalTupleSets.end()
            : std::find_if(schema.resolvedFunctionalTupleSets.begin(),
                           schema.resolvedFunctionalTupleSets.end(),
                           [&](const auto &entry) {
                             return entry.type == type &&
                                    entry.configuration == configuration &&
                                    entry.id == binding->tupleSet &&
                                    entry.selector == node->id;
                           });
    if (tupleSet == schema.resolvedFunctionalTupleSets.end() ||
        targetBins.size() == 0 ||
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
        const auto *selection =
            findResolvedValueSet(schema, configuration, component.valueSet);
        if (!selection || selection->type != type ||
            component.ordinal != targetOrdinal)
          return OBELISK_RT_INVALID_DESIGN;
        for (uint64_t bin : targetBins[targetOrdinal]) {
          bool intersects = false;
          obelisk_rt_status status = resolvedFunctionalBinIntersectsValueSet(
              schema, type, configuration, bin, *selection,
              suppressedTransitionAlternatives, intersects);
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
            suppressedTransitionAlternatives, selected);
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
  std::sort(result.begin(), result.end());
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

bool integralShapeCoveredByUnion(
    const IntegralCoverageShape &subject,
    const std::vector<IntegralCoverageShape> &exclusions, uint32_t bitWidth) {
  struct Frame {
    int64_t remainingBit = -1;
    int64_t decisionBit = -1;
    bool decisionOne = false;
    uint8_t stage = 0;
    bool zeroBranch = false;
    bool oneBranch = false;
    std::vector<size_t> candidates;
  };

  std::vector<uint64_t> prefix((uint64_t{bitWidth} + 63) / 64);
  std::vector<Frame> stack;
  Frame root;
  root.remainingBit = static_cast<int64_t>(bitWidth) - 1;
  root.candidates.resize(exclusions.size());
  for (size_t index = 0; index != exclusions.size(); ++index)
    root.candidates[index] = index;
  stack.push_back(std::move(root));

  auto popCovered = [&] {
    int64_t decisionBit = stack.back().decisionBit;
    bool decisionOne = stack.back().decisionOne;
    stack.pop_back();
    if (decisionOne)
      setCubeBit(prefix, static_cast<uint32_t>(decisionBit), false);
  };
  while (!stack.empty()) {
    Frame &frame = stack.back();
    if (frame.stage == 0) {
      std::vector<size_t> intersecting;
      intersecting.reserve(frame.candidates.size());
      bool covered = false;
      for (size_t index : frame.candidates) {
        const auto &exclusion = exclusions[index];
        if (!coverageShapeIntersectsPrefix(exclusion, prefix,
                                           frame.remainingBit, bitWidth))
          continue;
        if (coverageShapeContainsPrefix(exclusion, prefix, frame.remainingBit,
                                        bitWidth)) {
          covered = true;
          break;
        }
        intersecting.push_back(index);
      }
      if (covered || (frame.remainingBit < 0 && !intersecting.empty())) {
        popCovered();
        continue;
      }
      if (intersecting.empty())
        return false;
      frame.candidates = std::move(intersecting);
      bool cubePartition = !subject.interval ||
                           coverageShapeContainsPrefix(
                               subject, prefix, frame.remainingBit, bitWidth);
      for (size_t index : frame.candidates)
        cubePartition &= !exclusions[index].interval;
      if (cubePartition) {
        int64_t relevantBit = -1;
        auto includeConstraints = [&](const IntegralCoverageShape &shape) {
          if (shape.interval)
            return;
          uint32_t lastLimb = static_cast<uint32_t>(frame.remainingBit) / 64;
          for (uint32_t ordinal = 0; ordinal <= lastLimb; ++ordinal) {
            uint64_t constrained = ~shape.wildcard[ordinal];
            if (ordinal == lastLimb)
              constrained &= lowBitsMask(
                  static_cast<uint32_t>(frame.remainingBit) % 64 + 1);
            if (!constrained)
              continue;
            uint32_t bit = 0;
            while (constrained >>= 1)
              ++bit;
            relevantBit =
                std::max(relevantBit, static_cast<int64_t>(ordinal) * 64 + bit);
          }
        };
        includeConstraints(subject);
        for (size_t index : frame.candidates)
          includeConstraints(exclusions[index]);
        if (relevantBit < 0)
          return false;
        frame.remainingBit = relevantBit;
      }
      auto subjectBranchExists = [&](bool one) {
        setCubeBit(prefix, static_cast<uint32_t>(frame.remainingBit), one);
        bool exists = coverageShapeIntersectsPrefix(
            subject, prefix, frame.remainingBit - 1, bitWidth);
        setCubeBit(prefix, static_cast<uint32_t>(frame.remainingBit), false);
        return exists;
      };
      frame.zeroBranch = subjectBranchExists(false);
      frame.oneBranch = subjectBranchExists(true);
      if (!frame.zeroBranch && !frame.oneBranch)
        return false;
      frame.stage = 1;
      if (frame.zeroBranch) {
        Frame child;
        child.remainingBit = frame.remainingBit - 1;
        child.decisionBit = frame.remainingBit;
        child.candidates = frame.candidates;
        stack.push_back(std::move(child));
        continue;
      }
    }
    if (frame.stage == 1) {
      frame.stage = 2;
      if (frame.oneBranch) {
        setCubeBit(prefix, static_cast<uint32_t>(frame.remainingBit), true);
        Frame child;
        child.remainingBit = frame.remainingBit - 1;
        child.decisionBit = frame.remainingBit;
        child.decisionOne = true;
        child.candidates = frame.candidates;
        stack.push_back(std::move(child));
        continue;
      }
    }
    popCovered();
  }
  return true;
}

bool valueSetCoveredBy(
    const obelisk::coverage::Database &schema,
    const obelisk::coverage::ResolvedFunctionalValueSet &subject,
    const std::vector<const obelisk::coverage::ResolvedFunctionalValueSet *>
        &exclusions) {
  for (uint32_t subjectIndex = 0; subjectIndex != subject.atomCount;
       ++subjectIndex) {
    const auto &subjectAtom =
        schema.resolvedFunctionalValueAtoms[subject.firstAtom + subjectIndex];
    bool covered = false;
    for (const auto *exclusion : exclusions) {
      for (uint32_t exclusionIndex = 0; exclusionIndex != exclusion->atomCount;
           ++exclusionIndex) {
        const auto &exclusionAtom =
            schema.resolvedFunctionalValueAtoms[exclusion->firstAtom +
                                                exclusionIndex];
        if (atomContains(schema, *exclusion, exclusionAtom, subject,
                         subjectAtom)) {
          covered = true;
          break;
        }
      }
      if (covered)
        break;
    }
    if (!covered &&
        subject.kind == obelisk::coverage::FunctionalValueSetKind::Integral) {
      IntegralCoverageShape subjectShape;
      if (makeIntegralCoverageShape(schema, subjectAtom, subject.bitWidth,
                                    subject.signedness, subjectShape)) {
        std::vector<IntegralCoverageShape> exclusionShapes;
        for (const auto *exclusion : exclusions) {
          if (exclusion->kind != subject.kind ||
              exclusion->bitWidth != subject.bitWidth ||
              exclusion->signedness != subject.signedness)
            continue;
          for (uint32_t exclusionIndex = 0;
               exclusionIndex != exclusion->atomCount; ++exclusionIndex) {
            IntegralCoverageShape shape;
            if (makeIntegralCoverageShape(
                    schema,
                    schema.resolvedFunctionalValueAtoms[exclusion->firstAtom +
                                                        exclusionIndex],
                    subject.bitWidth, subject.signedness, shape))
              exclusionShapes.push_back(std::move(shape));
          }
        }
        if (!exclusionShapes.empty())
          covered = integralShapeCoveredByUnion(subjectShape, exclusionShapes,
                                                subject.bitWidth);
      }
    }
    if (!covered)
      return false;
  }
  return true;
}

struct ExactTransitionWord {
  uint64_t alternative = 0;
  uint64_t bin = 0;
  uint64_t item = 0;
  uint32_t flags = 0;
  std::vector<const obelisk::coverage::ResolvedFunctionalValueSet *> valueSets;
};

/// Derive the post-distribution subtraction required by IEEE 1800-2023
/// 19.5.5 and 19.5.6. Both ordinary and exclusion alternatives are finite
/// symbolic words. Integral ranges are decomposed into prefix cubes and each
/// word is represented as a Cartesian product in a concatenated bit space.
/// Union containment in that space is exact and does not enumerate values.
/// No derived matcher state enters the v1 database identity.
obelisk_rt_status
deriveTransitionExclusions(const obelisk::coverage::Database &schema,
                           uint64_t typeID,
                           const obelisk::coverage::Digest &configuration,
                           std::unordered_set<uint64_t> &suppressedAlternatives,
                           std::unordered_set<uint64_t> &emptyBins) {
  using namespace obelisk::coverage;
  constexpr uint64_t maxDerivationWork = uint64_t{1} << 14;
  uint64_t derivationWork = 0;
  auto consumeWork = [&](uint64_t amount = 1) {
    if (amount > maxDerivationWork - derivationWork)
      return false;
    derivationWork += amount;
    return true;
  };
  suppressedAlternatives.clear();
  emptyBins.clear();
  std::unordered_set<uint64_t> affectedItems;
  std::unordered_map<uint64_t, const ResolvedFunctionalBin *> bins;
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

  std::vector<ExactTransitionWord> words;
  std::unordered_map<uint64_t, size_t> ordinaryAlternativeCounts;
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
    ExactTransitionWord word{alternative.id,
                             alternative.bin,
                             bin->second->item,
                             bin->second->flags,
                             {}};
    word.valueSets.reserve(alternative.stepCount);
    for (uint32_t ordinal = 0; ordinal != alternative.stepCount; ++ordinal) {
      const auto &step =
          schema.resolvedTransitionSteps[alternative.firstStep + ordinal];
      const auto *set =
          findResolvedValueSet(schema, configuration, step.valueSet);
      if (step.alternative != alternative.id || step.ordinal != ordinal ||
          step.lowerBound != 1 || step.upperBound != 1 || !set ||
          set->kind != FunctionalValueSetKind::Integral)
        return OBELISK_RT_INVALID_DESIGN;
      word.valueSets.push_back(set);
    }
    if (!(bin->second->flags & (FunctionalBinIgnore | FunctionalBinIllegal)))
      ++ordinaryAlternativeCounts[bin->second->id];
    words.push_back(std::move(word));
  }

  auto wildcardPlane = [](uint32_t bitWidth) {
    std::vector<uint64_t> result((uint64_t{bitWidth} + 63) / 64, UINT64_MAX);
    if (bitWidth % 64)
      result.back() = lowBitsMask(bitWidth % 64);
    return result;
  };

  auto appendShapeCubes = [&](const IntegralCoverageShape &shape,
                              uint32_t bitWidth,
                              std::vector<IntegralCoverageShape> &cubes) {
    if (!shape.interval) {
      if (!consumeWork())
        return false;
      cubes.push_back(shape);
      return true;
    }
    struct Frame {
      std::vector<uint64_t> prefix;
      int64_t remainingBit = -1;
    };
    std::vector<Frame> stack;
    stack.push_back({std::vector<uint64_t>((uint64_t{bitWidth} + 63) / 64),
                     static_cast<int64_t>(bitWidth) - 1});
    while (!stack.empty()) {
      if (!consumeWork())
        return false;
      Frame frame = std::move(stack.back());
      stack.pop_back();
      if (!coverageShapeIntersectsPrefix(shape, frame.prefix,
                                         frame.remainingBit, bitWidth))
        continue;
      if (coverageShapeContainsPrefix(shape, frame.prefix, frame.remainingBit,
                                      bitWidth)) {
        IntegralCoverageShape cube;
        cube.value = std::move(frame.prefix);
        cube.wildcard.assign(cube.value.size(), 0);
        if (frame.remainingBit >= 0) {
          uint32_t limb = static_cast<uint32_t>(frame.remainingBit) / 64;
          for (uint32_t ordinal = 0; ordinal < limb; ++ordinal)
            cube.wildcard[ordinal] = UINT64_MAX;
          cube.wildcard[limb] =
              lowBitsMask(static_cast<uint32_t>(frame.remainingBit) % 64 + 1);
        }
        cubes.push_back(std::move(cube));
        continue;
      }
      if (frame.remainingBit < 0)
        return false;
      Frame one = frame;
      --frame.remainingBit;
      setCubeBit(one.prefix, static_cast<uint32_t>(one.remainingBit), true);
      --one.remainingBit;
      stack.push_back(std::move(one));
      stack.push_back(std::move(frame));
    }
    return true;
  };

  auto valueSetCubes = [&](const ResolvedFunctionalValueSet &set,
                           std::vector<IntegralCoverageShape> &cubes) {
    if (!set.bitWidth || uint64_t{set.firstAtom} + set.atomCount >
                             schema.resolvedFunctionalValueAtoms.size())
      return OBELISK_RT_INVALID_DESIGN;
    for (uint32_t ordinal = 0; ordinal != set.atomCount; ++ordinal) {
      const auto &atom =
          schema.resolvedFunctionalValueAtoms[set.firstAtom + ordinal];
      IntegralCoverageShape shape;
      if (atom.valueSet != set.id || atom.ordinal != ordinal ||
          !makeIntegralCoverageShape(schema, atom, set.bitWidth, set.signedness,
                                     shape))
        return OBELISK_RT_INVALID_DESIGN;
      if (!appendShapeCubes(shape, set.bitWidth, cubes))
        return OBELISK_RT_OUT_OF_RESOURCES;
    }
    return OBELISK_RT_OK;
  };

  auto buildWordCubes = [&](const ExactTransitionWord &word,
                            uint32_t totalSteps, uint32_t stepOffset,
                            std::vector<IntegralCoverageShape> &result) {
    if (word.valueSets.empty() || stepOffset > totalSteps ||
        word.valueSets.size() > totalSteps - stepOffset)
      return OBELISK_RT_INVALID_DESIGN;
    const uint32_t stepWidth = word.valueSets.front()->bitWidth;
    const auto signedness = word.valueSets.front()->signedness;
    if (!stepWidth || uint64_t{stepWidth} * totalSteps > UINT32_MAX)
      return OBELISK_RT_OUT_OF_RESOURCES;
    const uint32_t totalWidth = stepWidth * totalSteps;
    IntegralCoverageShape initial;
    initial.value.assign((uint64_t{totalWidth} + 63) / 64, 0);
    initial.wildcard = wildcardPlane(totalWidth);
    result.push_back(std::move(initial));
    for (uint32_t step = 0; step != word.valueSets.size(); ++step) {
      const auto &set = *word.valueSets[step];
      if (set.bitWidth != stepWidth || set.signedness != signedness)
        return OBELISK_RT_INVALID_DESIGN;
      std::vector<IntegralCoverageShape> stepCubes;
      obelisk_rt_status status = valueSetCubes(set, stepCubes);
      if (status != OBELISK_RT_OK)
        return status;
      if (stepCubes.empty()) {
        result.clear();
        return OBELISK_RT_OK;
      }
      if (result.size() > maxDerivationWork / stepCubes.size())
        return OBELISK_RT_OUT_OF_RESOURCES;
      std::vector<IntegralCoverageShape> expanded;
      expanded.reserve(result.size() * stepCubes.size());
      for (const auto &prefix : result) {
        for (const auto &stepCube : stepCubes) {
          if (!consumeWork())
            return OBELISK_RT_OUT_OF_RESOURCES;
          IntegralCoverageShape combined = prefix;
          const uint32_t destination = (stepOffset + step) * stepWidth;
          for (uint32_t bit = 0; bit != stepWidth; ++bit) {
            setCubeBit(combined.value, destination + bit,
                       cubeBit(stepCube.value, bit));
            setCubeBit(combined.wildcard, destination + bit,
                       cubeBit(stepCube.wildcard, bit));
          }
          expanded.push_back(std::move(combined));
        }
      }
      result = std::move(expanded);
    }
    return OBELISK_RT_OK;
  };

  std::unordered_map<uint64_t, size_t> suppressedPerBin;
  for (const auto &word : words) {
    if (word.flags & (FunctionalBinIgnore | FunctionalBinIllegal))
      continue;
    std::vector<IntegralCoverageShape> ordinaryCubes;
    obelisk_rt_status status = buildWordCubes(
        word, static_cast<uint32_t>(word.valueSets.size()), 0, ordinaryCubes);
    if (status != OBELISK_RT_OK)
      return status;
    std::vector<IntegralCoverageShape> exclusionCubes;
    for (const auto &exclusion : words) {
      if (exclusion.item != word.item ||
          !(exclusion.flags & (FunctionalBinIgnore | FunctionalBinIllegal)) ||
          exclusion.valueSets.size() > word.valueSets.size())
        continue;
      for (uint32_t offset = 0;
           offset + exclusion.valueSets.size() <= word.valueSets.size();
           ++offset) {
        std::vector<IntegralCoverageShape> window;
        status = buildWordCubes(exclusion,
                                static_cast<uint32_t>(word.valueSets.size()),
                                offset, window);
        if (status != OBELISK_RT_OK)
          return status;
        exclusionCubes.insert(exclusionCubes.end(),
                              std::make_move_iterator(window.begin()),
                              std::make_move_iterator(window.end()));
      }
    }
    const uint32_t totalWidth =
        word.valueSets.front()->bitWidth * word.valueSets.size();
    bool suppressed = !ordinaryCubes.empty();
    for (const auto &ordinaryCube : ordinaryCubes)
      if (!integralShapeCoveredByUnion(ordinaryCube, exclusionCubes,
                                       totalWidth)) {
        suppressed = false;
        break;
      }
    if (suppressed) {
      suppressedAlternatives.insert(word.alternative);
      ++suppressedPerBin[word.bin];
    }
  }
  for (const auto &[bin, count] : ordinaryAlternativeCounts)
    if (suppressedPerBin[bin] == count)
      emptyBins.insert(bin);
  return OBELISK_RT_OK;
}

void markStaticallyEmptyStateBins(
    obelisk::coverage::Database &schema, uint64_t typeID,
    const obelisk::coverage::Digest &configuration) {
  using namespace obelisk::coverage;
  auto planFor = [&](uint64_t bin) -> const ResolvedFunctionalBinPlan * {
    auto found = std::find_if(schema.resolvedFunctionalBinPlans.begin(),
                              schema.resolvedFunctionalBinPlans.end(),
                              [&](const auto &entry) {
                                return entry.type == typeID &&
                                       entry.configuration == configuration &&
                                       entry.bin == bin;
                              });
    return found == schema.resolvedFunctionalBinPlans.end() ? nullptr : &*found;
  };
  for (auto &bin : schema.resolvedFunctionalBins) {
    constexpr uint32_t nonContributing =
        FunctionalBinDefault | FunctionalBinIgnore | FunctionalBinIllegal |
        FunctionalBinEmpty;
    if (bin.type != typeID || bin.configuration != configuration ||
        bin.kind != FunctionalBinKind::State || (bin.flags & nonContributing))
      continue;
    const auto *plan = planFor(bin.id);
    const auto *subject =
        plan ? findResolvedValueSet(schema, configuration, plan->valueSet)
             : nullptr;
    if (!subject)
      continue;
    if (!subject->atomCount) {
      bin.flags |= FunctionalBinEmpty;
      continue;
    }
    std::vector<const ResolvedFunctionalValueSet *> exclusions;
    for (const auto &candidate : schema.resolvedFunctionalBins) {
      if (candidate.type != typeID ||
          candidate.configuration != configuration ||
          candidate.item != bin.item ||
          !(candidate.flags & (FunctionalBinIgnore | FunctionalBinIllegal)) ||
          (candidate.flags & FunctionalBinEmpty))
        continue;
      const auto *candidatePlan = planFor(candidate.id);
      const auto *set = candidatePlan
                            ? findResolvedValueSet(schema, configuration,
                                                   candidatePlan->valueSet)
                            : nullptr;
      // An iff-guarded exclusion is runtime state and therefore cannot make a
      // resolved schema bin globally empty.
      if (set && !candidatePlan->iffExpression)
        exclusions.push_back(set);
    }
    if (!exclusions.empty() && valueSetCoveredBy(schema, *subject, exclusions))
      bin.flags |= FunctionalBinEmpty;
  }
}

const FunctionalCoverageCrossState *
findFunctionalCross(const FunctionalCoverageTypeState &type, uint64_t item) {
  auto found =
      std::find_if(type.crosses.begin(), type.crosses.end(),
                   [&](const auto &cross) { return cross.id == item; });
  return found == type.crosses.end() ? nullptr : &*found;
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
    for (const auto &bin : instance.bins) {
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
  for (const auto &bin : instance.bins) {
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

struct LoadedFunctionalInstance {
  obelisk::coverage::UUID run{};
  uint64_t typeID = 0;
  uint64_t id = 0;
  std::string name;
  obelisk::coverage::Digest configuration{};
  std::vector<uint64_t> counts;
  std::map<uint64_t,
           std::map<std::vector<uint64_t>, FunctionalCoverageCrossTupleState>>
      crossTuples;
  std::vector<obelisk::coverage::ResolvedInstanceOption> options;
};

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

std::vector<LoadedFunctionalInstance>
collectLoadedFunctionalInstances(const CoverageState &coverage, uint64_t typeID,
                                 const FunctionalCoverageTypeState &type) {
  std::vector<LoadedFunctionalInstance> groups;
  if (!coverage.schema)
    return groups;
  for (const auto &instance : coverage.schema->resolvedInstances) {
    if (instance.type != typeID || instance.configuration != type.configuration)
      continue;
    auto group = groups.end();
    if (!instance.name.empty() &&
        !(instance.flags & obelisk::coverage::ResolvedInstanceGeneratedName))
      group =
          std::find_if(groups.begin(), groups.end(), [&](const auto &entry) {
            return entry.name == instance.name &&
                   entry.configuration == instance.configuration;
          });
    if (group == groups.end()) {
      groups.push_back({instance.run,
                        instance.type,
                        instance.id,
                        instance.name,
                        instance.configuration,
                        std::vector<uint64_t>(type.bins.size(), 0),
                        {},
                        {}});
      group = std::prev(groups.end());
    }
    for (const auto &option : coverage.schema->resolvedInstanceOptions)
      if (option.run == instance.run && option.instance == instance.id)
        group->options.push_back(option);
    for (const auto &counter : coverage.schema->counters) {
      if (counter.metric != obelisk::coverage::MetricKind::Functional ||
          counter.run != instance.run || counter.instance != instance.id)
        continue;
      auto bin = std::find_if(
          type.bins.begin(), type.bins.end(),
          [&](const auto &entry) { return entry.id == counter.entity; });
      if (bin == type.bins.end())
        continue;
      size_t ordinal = static_cast<size_t>(bin - type.bins.begin());
      group->counts[ordinal] =
          saturatingAdd(group->counts[ordinal], counter.value);
    }
    for (const auto &tuple : coverage.schema->sparseCrossTuples) {
      if (tuple.run != instance.run || tuple.instance != instance.id ||
          uint64_t{tuple.firstComponent} + tuple.componentCount >
              coverage.schema->sparseCrossTupleComponents.size())
        continue;
      auto cross = std::find_if(
          coverage.schema->resolvedFunctionalItems.begin(),
          coverage.schema->resolvedFunctionalItems.end(),
          [&](const auto &entry) {
            return entry.type == typeID &&
                   entry.configuration == type.configuration &&
                   entry.templateItem == tuple.cross &&
                   entry.kind == obelisk::coverage::FunctionalItemKind::Cross;
          });
      if (cross == coverage.schema->resolvedFunctionalItems.end())
        continue;
      std::vector<uint64_t> components(
          coverage.schema->sparseCrossTupleComponents.begin() +
              tuple.firstComponent,
          coverage.schema->sparseCrossTupleComponents.begin() +
              tuple.firstComponent + tuple.componentCount);
      FunctionalCoverageCrossTupleState &state =
          group->crossTuples[cross->id][components];
      uint64_t previous = state.count;
      state.count = saturatingAdd(state.count, tuple.hits);
      state.overflow =
          state.overflow ||
          (tuple.flags & obelisk::coverage::SparseCrossTupleOverflow) ||
          state.count < previous || (UINT64_MAX - previous < tuple.hits);
    }
  }
  return groups;
}

obelisk_rt_status materializeLoadedFunctionalInstance(
    const CoverageState &coverage, const LoadedFunctionalInstance &loaded,
    const FunctionalCoverageTypeState &type,
    FunctionalCoverageInstanceState &instance) {
  using Owner = obelisk::coverage::FunctionalConfigurationOptionOwnerKind;
  using Option = obelisk::coverage::FunctionalConfigurationOptionKind;
  initializeFunctionalInstanceProfile(instance, type);
  instance.bins = type.bins;
  if (instance.bins.size() != loaded.counts.size() || !coverage.schema)
    return OBELISK_RT_INVALID_DESIGN;
  for (size_t index = 0; index != instance.bins.size(); ++index)
    instance.bins[index].count = loaded.counts[index];
  instance.crossTuples = loaded.crossTuples;
  bool hasGroupGoal = false;
  bool hasGroupWeight = false;
  std::unordered_set<uint64_t> itemGoals;
  std::unordered_set<uint64_t> itemWeights;
  std::optional<uint64_t> groupAtLeast;
  std::map<uint64_t, uint64_t> itemAtLeast;
  std::optional<uint64_t> groupCrossNumPrintMissing;
  std::map<uint64_t, uint64_t> itemCrossNumPrintMissing;
  for (const auto &option : loaded.options) {
    if (option.ownerKind == Owner::Group) {
      if (option.option == Option::Goal) {
        instance.instanceGoal = hasGroupGoal
                                    ? std::max(
                                          instance.instanceGoal,
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
      }
      else if (option.option == Option::AtLeast)
        groupAtLeast = groupAtLeast ? std::max(*groupAtLeast, option.value)
                                    : option.value;
      else if (option.option == Option::CrossNumPrintMissing)
        groupCrossNumPrintMissing =
            groupCrossNumPrintMissing
                ? std::max(*groupCrossNumPrintMissing, option.value)
                : option.value;
      continue;
    }
    auto resolved = std::find_if(
        coverage.schema->resolvedFunctionalItems.begin(),
        coverage.schema->resolvedFunctionalItems.end(), [&](const auto &item) {
          return item.type == loaded.typeID &&
                 item.configuration == loaded.configuration &&
                 item.templateItem == option.owner;
        });
    if (resolved == coverage.schema->resolvedFunctionalItems.end())
      return OBELISK_RT_INVALID_DESIGN;
    auto position = std::find(type.items.begin(), type.items.end(), resolved->id);
    // A point/cross method query projects the type to one item. The snapshot
    // still correctly carries the mutable profiles of its sibling items.
    if (position == type.items.end() && type.projectedItemIndex != SIZE_MAX)
      continue;
    if (position == type.items.end())
      return OBELISK_RT_INVALID_DESIGN;
    size_t index = static_cast<size_t>(position - type.items.begin());
    if (option.option == Option::Goal) {
      const bool seen = !itemGoals.insert(resolved->templateItem).second;
      instance.itemGoals[index] =
          seen ? std::max(instance.itemGoals[index],
                          static_cast<uint32_t>(option.value))
               : static_cast<uint32_t>(option.value);
    } else if (option.option == Option::Weight) {
      const bool seen = !itemWeights.insert(resolved->templateItem).second;
      instance.itemWeights[index] =
          seen ? std::max(instance.itemWeights[index],
                          static_cast<uint32_t>(option.value))
               : static_cast<uint32_t>(option.value);
    }
    else if (option.option == Option::AtLeast)
      itemAtLeast[resolved->templateItem] =
          std::max(itemAtLeast[resolved->templateItem], option.value);
    else if (option.option == Option::CrossNumPrintMissing) {
      if (resolved->kind != obelisk::coverage::FunctionalItemKind::Cross)
        return OBELISK_RT_INVALID_DESIGN;
      itemCrossNumPrintMissing[resolved->templateItem] = std::max(
          itemCrossNumPrintMissing[resolved->templateItem], option.value);
    }
  }
  std::unordered_set<uint64_t> staticallyExplicitAtLeast;
  for (const auto &option : coverage.schema->functionalConfigurationOptions) {
    if (option.type != loaded.typeID ||
        option.configuration != loaded.configuration ||
        option.ownerKind != Owner::Item || option.option != Option::AtLeast ||
        option.scope !=
            obelisk::coverage::FunctionalOptionScopeKind::Instance)
      continue;
    auto resolved = std::find_if(
        coverage.schema->resolvedFunctionalItems.begin(),
        coverage.schema->resolvedFunctionalItems.end(), [&](const auto &item) {
          return item.type == loaded.typeID &&
                 item.configuration == loaded.configuration &&
                 item.id == option.owner;
        });
    if (resolved == coverage.schema->resolvedFunctionalItems.end())
      return OBELISK_RT_INVALID_DESIGN;
    staticallyExplicitAtLeast.insert(resolved->templateItem);
  }
  auto applyAtLeast = [&](uint64_t resolvedItem, uint64_t threshold) {
    auto position =
        std::find(type.items.begin(), type.items.end(), resolvedItem);
    if (position == type.items.end())
      return false;
    instance.itemAtLeast[static_cast<size_t>(position - type.items.begin())] =
        threshold;
    for (auto &bin : instance.bins)
      if (bin.item == resolvedItem)
        bin.atLeast = threshold;
    if (auto cross = instance.crossAtLeast.find(resolvedItem);
        cross != instance.crossAtLeast.end())
      cross->second = threshold;
    return true;
  };
  if (groupAtLeast)
    instance.groupAtLeast = *groupAtLeast;
  if (groupCrossNumPrintMissing)
    instance.groupCrossNumPrintMissing = *groupCrossNumPrintMissing;
  for (const auto &[item, value] : itemCrossNumPrintMissing) {
    instance.crossNumPrintMissing[item] = value;
    instance.explicitCrossNumPrintMissingItems.insert(item);
  }
  for (uint64_t resolvedID : type.items) {
    auto resolved = std::find_if(
        coverage.schema->resolvedFunctionalItems.begin(),
        coverage.schema->resolvedFunctionalItems.end(), [&](const auto &item) {
          return item.type == loaded.typeID &&
                 item.configuration == loaded.configuration &&
                 item.id == resolvedID;
        });
    if (resolved == coverage.schema->resolvedFunctionalItems.end())
      return OBELISK_RT_INVALID_DESIGN;
    if (auto threshold = itemAtLeast.find(resolved->templateItem);
        threshold != itemAtLeast.end()) {
      if (!applyAtLeast(resolvedID, threshold->second))
        return OBELISK_RT_INVALID_DESIGN;
      continue;
    }
    if (groupAtLeast &&
        !staticallyExplicitAtLeast.count(resolved->templateItem))
      if (!applyAtLeast(resolvedID, *groupAtLeast))
        return OBELISK_RT_INVALID_DESIGN;
  }
  return OBELISK_RT_OK;
}

obelisk_rt_status collectCumulativeAtLeast(
    const CoverageState &coverage, uint64_t typeID,
    const std::vector<const FunctionalCoverageTypeState *> &types,
    std::map<uint64_t, uint64_t> &thresholds) {
  if (!coverage.schema)
    return OBELISK_RT_INVALID_DESIGN;
  thresholds.clear();
  for (const FunctionalCoverageTypeState *type : types) {
    if (!type)
      return OBELISK_RT_INVALID_DESIGN;
    std::vector<uint64_t> templateItems;
    templateItems.reserve(type->items.size());
    for (uint64_t resolvedID : type->items) {
      auto resolved = std::find_if(
          coverage.schema->resolvedFunctionalItems.begin(),
          coverage.schema->resolvedFunctionalItems.end(),
          [&](const auto &item) {
            return item.type == typeID &&
                   item.configuration == type->configuration &&
                   item.id == resolvedID;
          });
      if (resolved == coverage.schema->resolvedFunctionalItems.end())
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
      FunctionalCoverageInstanceState instance;
      obelisk_rt_status status =
          materializeLoadedFunctionalInstance(coverage, loaded, *type, instance);
      if (status != OBELISK_RT_OK)
        return status;
      if (!accumulate(instance, false))
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
    std::map<std::string, MergedBin> bins;
    std::map<std::vector<std::string>, uint64_t> tuples;
  };
  std::map<uint64_t, MergedItem> items;
  std::map<uint64_t, uint64_t> cumulativeAtLeast;
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
        auto resolvedItem =
            std::find_if(coverage.schema->resolvedFunctionalItems.begin(),
                         coverage.schema->resolvedFunctionalItems.end(),
                         [&](const auto &entry) {
                           return entry.type == typeID &&
                                  entry.configuration == type->configuration &&
                                  entry.id == resolvedItemID;
                         });
        if (resolvedItem == coverage.schema->resolvedFunctionalItems.end())
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
            if (std::find(item.automaticProfiles.begin(),
                          item.automaticProfiles.end(),
                          profile) == item.automaticProfiles.end())
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
                auto resolvedBin = std::find_if(
                    coverage.schema->resolvedFunctionalBins.begin(),
                    coverage.schema->resolvedFunctionalBins.end(),
                    [&](const auto &entry) {
                      return entry.type == typeID &&
                             entry.configuration == type->configuration &&
                             entry.id == binID;
                    });
                if (resolvedBin ==
                    coverage.schema->resolvedFunctionalBins.end())
                  return OBELISK_RT_INVALID_DESIGN;
                names.push_back(resolvedBin->name);
              }
              item.tuples[names] =
                  saturatingAdd(item.tuples[names], state.count);
            }
          }
          for (const FunctionalCoverageBinState &bin : instance.bins) {
            constexpr uint32_t omitted =
                FunctionalBinIgnore | FunctionalBinIllegal | FunctionalBinEmpty;
            if (bin.item != resolvedItemID || (bin.flags & omitted) ||
                bin.excluded)
              continue;
            auto resolvedBin = std::find_if(
                coverage.schema->resolvedFunctionalBins.begin(),
                coverage.schema->resolvedFunctionalBins.end(),
                [&](const auto &entry) {
                  return entry.type == typeID &&
                         entry.configuration == type->configuration &&
                         entry.id == bin.id &&
                         entry.kind == FunctionalBinKind::Cross;
                });
            if (resolvedBin == coverage.schema->resolvedFunctionalBins.end())
              return OBELISK_RT_INVALID_DESIGN;
            MergedBin &merged = item.bins[resolvedBin->name];
            merged.count = saturatingAdd(merged.count, bin.count);
          }
          continue;
        }
        if (item.cross)
          return OBELISK_RT_INVALID_DESIGN;
        for (const FunctionalCoverageBinState &bin : instance.bins) {
          constexpr uint32_t omitted =
              FunctionalBinIgnore | FunctionalBinIllegal | FunctionalBinEmpty |
              FunctionalBinDefault | FunctionalBinDefaultSequence;
          if (bin.item != resolvedItemID || (bin.flags & omitted) ||
              bin.excluded)
            continue;
          auto resolvedBin = std::find_if(
              coverage.schema->resolvedFunctionalBins.begin(),
              coverage.schema->resolvedFunctionalBins.end(),
              [&](const auto &entry) {
                return entry.type == typeID &&
                       entry.configuration == type->configuration &&
                       entry.id == bin.id;
              });
          if (resolvedBin == coverage.schema->resolvedFunctionalBins.end())
            return OBELISK_RT_INVALID_DESIGN;
          MergedBin &merged = item.bins[resolvedBin->name];
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
      FunctionalCoverageInstanceState materialized;
      obelisk_rt_status materialize = materializeLoadedFunctionalInstance(
          coverage, instance, *type, materialized);
      if (materialize != OBELISK_RT_OK)
        return materialize;
      obelisk_rt_status status = accumulate(materialized);
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

CoverageResult queryType(const CoverageState &coverage, uint64_t typeID,
                         const FunctionalCoverageTypeState &type,
                         uint64_t *outInstanceCount = nullptr,
                         uint64_t *outTotalWeight = nullptr,
                         const std::map<uint64_t, uint64_t>
                             *cumulativeAtLeast = nullptr) {
  CoverageResult result;
  uint64_t instanceCount = 0;
  uint64_t totalWeight = 0;
  double weightedPercentage = 0.0;
  std::vector<std::optional<uint64_t>> itemThresholds(type.items.size());
  if (cumulativeAtLeast && coverage.schema)
    for (size_t itemIndex = 0; itemIndex != type.items.size(); ++itemIndex) {
      auto resolved = std::find_if(
          coverage.schema->resolvedFunctionalItems.begin(),
          coverage.schema->resolvedFunctionalItems.end(),
          [&](const auto &item) {
            return item.type == typeID &&
                   item.configuration == type.configuration &&
                   item.id == type.items[itemIndex];
          });
      if (resolved != coverage.schema->resolvedFunctionalItems.end())
        if (auto threshold = cumulativeAtLeast->find(resolved->templateItem);
            threshold != cumulativeAtLeast->end())
          itemThresholds[itemIndex] = threshold->second;
    }
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
    FunctionalCoverageInstanceState materialized;
    if (materializeLoadedFunctionalInstance(coverage, instance, type,
                                            materialized) != OBELISK_RT_OK)
      continue;
    CoverageResult current = queryInstance(
        materialized, type, cumulativeAtLeast ? &itemThresholds : nullptr);
    if (current.contributes && materialized.instanceWeight) {
      weightedPercentage +=
          current.percentage * static_cast<double>(materialized.instanceWeight);
      totalWeight = saturatingAdd(totalWeight, materialized.instanceWeight);
    }
    result.covered = saturatingAdd(result.covered, current.covered);
    result.total = saturatingAdd(result.total, current.total);
    ++instanceCount;
  }
  if (totalWeight) {
    result.percentage = weightedPercentage / static_cast<double>(totalWeight);
    result.contributes = true;
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
  std::vector<obelisk::coverage::Digest> configurations;
  for (const auto &configuration : coverage.schema->functionalConfigurations)
    if (configuration.type == typeID)
      configurations.push_back(configuration.configuration);
  if (configurations.empty()) {
    const bool knownType =
        std::any_of(coverage.schema->functionalTypes.begin(),
                    coverage.schema->functionalTypes.end(),
                    [&](const auto &type) { return type.id == typeID; });
    if (!knownType)
      return OBELISK_RT_INVALID_DESIGN;

    // A type query aggregates constructed instances.  Before the first
    // construction there is no constructor-resolved configuration to bind and
    // no bins contributing to the type result.  In particular, do not attempt
    // to evaluate constructor-dependent options or bin expressions without
    // their constructor values.  IEEE 1800-2023 19.11 requires the method
    // result and optional numerator / denominator to be zero in this case;
    // the distinct global $get_coverage query handles the no-instance case as
    // 100 percent.
    result = {};
    instanceCount = 0;
    if (outTypeWeight)
      *outTypeWeight = 0;
    return OBELISK_RT_OK;
  }

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
    std::map<uint64_t, uint64_t> cumulativeAtLeast;
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
  auto exact = std::find_if(coverage.schema->resolvedFunctionalItems.begin(),
                            coverage.schema->resolvedFunctionalItems.end(),
                            [&](const auto &item) {
                              return item.type == typeID &&
                                     item.configuration == configuration &&
                                     item.templateItem == templateItem;
                            });
  if (exact != coverage.schema->resolvedFunctionalItems.end())
    return &*exact;

  auto source =
      std::find_if(coverage.schema->functionalItems.begin(),
                   coverage.schema->functionalItems.end(),
                   [&](const auto &item) { return item.id == templateItem; });
  if (source == coverage.schema->functionalItems.end())
    return nullptr;

  const obelisk::coverage::ResolvedFunctionalItem *result = nullptr;
  for (const auto &candidate : coverage.schema->resolvedFunctionalItems) {
    if (candidate.type != typeID || candidate.configuration != configuration ||
        candidate.name != source->name || candidate.kind != source->kind ||
        (candidate.flags & obelisk::coverage::FunctionalItemNonAggregating) !=
            0)
      continue;
    if (result)
      return nullptr;
    result = &candidate;
  }
  return result;
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
  auto item = std::find(type.items.begin(), type.items.end(), resolved->id);
  if (item == type.items.end())
    return false;
  size_t index = static_cast<size_t>(item - type.items.begin());
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
                            bool projectedLiveInstance = true,
                            std::optional<uint64_t> cumulativeAtLeast =
                                std::nullopt) {
  if (type.items.size() != 1 || type.itemGoals.size() != 1 ||
      type.itemWeights.size() != 1 || instance.bins.size() != type.bins.size())
    return {};
  const size_t profileIndex = projectedLiveInstance
                                  ? type.projectedItemIndex
                                  : size_t{0};
  return queryInstanceItem(instance, type, 0, false, profileIndex,
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
  auto accumulate = [&](const FunctionalCoverageInstanceState &instance) {
    CoverageResult current = queryFunctionalItemInstance(
        instance, type, true, cumulativeAtLeast);
    if (current.contributes && type.instanceWeight) {
      weightedPercentage +=
          current.percentage * static_cast<double>(type.instanceWeight);
      totalWeight = saturatingAdd(totalWeight, type.instanceWeight);
    }
    result.covered = saturatingAdd(result.covered, current.covered);
    result.total = saturatingAdd(result.total, current.total);
  };
  for (uint64_t handle : type.instances) {
    auto instance = coverage.instances.find(handle);
    if (instance != coverage.instances.end())
      accumulate(instance->second);
  }
  for (const auto &loaded :
       collectLoadedFunctionalInstances(coverage, typeID, type)) {
    FunctionalCoverageInstanceState instance;
    obelisk_rt_status materialize =
        materializeLoadedFunctionalInstance(coverage, loaded, type, instance);
    if (materialize != OBELISK_RT_OK)
      return materialize;
    CoverageResult current =
        queryFunctionalItemInstance(instance, type, false, cumulativeAtLeast);
    if (current.contributes && type.instanceWeight) {
      weightedPercentage +=
          current.percentage * static_cast<double>(type.instanceWeight);
      totalWeight = saturatingAdd(totalWeight, type.instanceWeight);
    }
    result.covered = saturatingAdd(result.covered, current.covered);
    result.total = saturatingAdd(result.total, current.total);
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
  auto templateSchema = std::find_if(
      coverage.schema->functionalItems.begin(),
      coverage.schema->functionalItems.end(), [&](const auto &item) {
        return item.id == templateItem && item.type == typeID;
      });
  if (templateSchema == coverage.schema->functionalItems.end())
    return OBELISK_RT_INVALID_DESIGN;

  std::vector<FunctionalCoverageTypeState> selectedTypes;
  for (const auto &configuration : coverage.schema->functionalConfigurations) {
    if (configuration.type != typeID)
      continue;
    FunctionalCoverageTypeState *type = nullptr;
    obelisk_rt_status status = bindResolvedFunctionalType(
        coverage, typeID, configuration.configuration, type);
    if (status != OBELISK_RT_OK)
      return status;
    selectedTypes.emplace_back();
    if (!selectFunctionalItem(coverage, typeID, templateItem, *type,
                              selectedTypes.back()))
      return OBELISK_RT_INVALID_DESIGN;
  }
  if (selectedTypes.empty()) {
    // Like the enclosing type query, a static item query has no contributing
    // bins until an instance establishes a constructor-resolved
    // configuration.  Return the empty aggregate without speculatively
    // resolving constructor-time expressions.
    result = {};
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
    std::map<uint64_t, uint64_t> cumulativeAtLeast;
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
    auto expectedFormals = schemaOrdered(coverage->schema->functionalFormals,
                                         typeID, *coverage->schema, true);
    auto expectedExpressions =
        schemaOrdered(coverage->schema->functionalExpressions, typeID,
                      *coverage->schema, true);
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
              reference))
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
              false))
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
    for (const FunctionalCoverageCrossState &cross : type->crosses)
      instance.crossAtLeast.emplace(cross.id, cross.atLeast);
    for (const auto &option : coverage->schema->functionalConfigurationOptions) {
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
      auto resolved = std::find_if(
          coverage->schema->resolvedFunctionalItems.begin(),
          coverage->schema->resolvedFunctionalItems.end(),
          [&](const auto &item) {
            return item.type == typeID &&
                   item.configuration == type->configuration &&
                   item.id == option.owner;
          });
      if (resolved == coverage->schema->resolvedFunctionalItems.end())
        return OBELISK_RT_INVALID_DESIGN;
      if (option.option == Option::AtLeast)
        instance.explicitAtLeastItems.insert(resolved->templateItem);
      else if (option.option == Option::CrossNumPrintMissing) {
        if (resolved->kind !=
            obelisk::coverage::FunctionalItemKind::Cross)
          return OBELISK_RT_INVALID_DESIGN;
        instance.crossNumPrintMissing[resolved->templateItem] = option.value;
        instance.explicitCrossNumPrintMissingItems.insert(
            resolved->templateItem);
      }
      else if (option.option == Option::Comment)
        instance.comments[resolved->templateItem] = option.stringValue;
    }
    bool hasExplicitName = false;
    for (const auto &plan : coverage->schema->functionalOptionPlans) {
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
    instance.formals.resize(expectedFormals.size());
    for (size_t index = 0; index != expectedFormals.size(); ++index)
      snapshotValue(formals[index], instance.formals[index]);
    if (type->instances.size() == type->instances.max_size())
      return OBELISK_RT_OUT_OF_RESOURCES;
    type->instances.reserve(type->instances.size() + 1);
    std::vector<uint64_t> retainedAutomaticRefs;
    retainedAutomaticRefs.reserve(instance.formals.size());
    auto insertion = coverage->instances.emplace(handle, std::move(instance));
    if (!insertion.second)
      return OBELISK_RT_OUT_OF_RESOURCES;
    for (const auto &formal : insertion.first->second.formals) {
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
    if (type == coverage->types.end())
      return OBELISK_RT_INVALID_DESIGN;
    if (instance.itemAtLeast.size() != type->second.items.size())
      return OBELISK_RT_INVALID_DESIGN;

    const obelisk::coverage::ResolvedFunctionalItem *resolved = nullptr;
    size_t itemIndex = 0;
    if (itemID) {
      resolved = findResolvedFunctionalItem(*coverage, instance.typeID,
                                            instance.configuration, itemID);
      if (!resolved)
        return OBELISK_RT_INVALID_DESIGN;
      auto position = std::find(type->second.items.begin(),
                                type->second.items.end(), resolved->id);
      if (position == type->second.items.end())
        return OBELISK_RT_INVALID_DESIGN;
      itemIndex = static_cast<size_t>(position - type->second.items.begin());
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

    auto applyAtLeast = [&](uint64_t resolvedItem, uint64_t threshold) {
      for (FunctionalCoverageBinState &bin : instance.bins)
        if (bin.item == resolvedItem)
          bin.atLeast = threshold;
      auto cross = instance.crossAtLeast.find(resolvedItem);
      if (cross != instance.crossAtLeast.end())
        cross->second = threshold;
    };
    if (resolved) {
      instance.explicitAtLeastItems.insert(resolved->templateItem);
      instance.itemAtLeast[itemIndex] = unsignedValue;
      applyAtLeast(resolved->id, unsignedValue);
      return OBELISK_RT_OK;
    }
    std::vector<const obelisk::coverage::ResolvedFunctionalItem *> items;
    items.reserve(type->second.items.size());
    for (uint64_t resolvedItem : type->second.items) {
      auto item = std::find_if(
          coverage->schema->resolvedFunctionalItems.begin(),
          coverage->schema->resolvedFunctionalItems.end(),
          [&](const auto &candidate) {
            return candidate.type == instance.typeID &&
                   candidate.configuration == instance.configuration &&
                   candidate.id == resolvedItem;
          });
      if (item == coverage->schema->resolvedFunctionalItems.end())
        return OBELISK_RT_INVALID_DESIGN;
      items.push_back(&*item);
    }
    instance.groupAtLeast = unsignedValue;
    for (size_t itemIndex = 0; itemIndex != items.size(); ++itemIndex) {
      const auto *item = items[itemIndex];
      if (!instance.explicitAtLeastItems.count(item->templateItem)) {
        instance.itemAtLeast[itemIndex] = unsignedValue;
        applyAtLeast(item->id, unsignedValue);
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
       option != OBELISK_RT_COVERGROUP_OPTION_CROSS_NUM_PRINT_MISSING))
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
  if (type == coverage->types.end())
    return OBELISK_RT_INVALID_DESIGN;

  const obelisk::coverage::ResolvedFunctionalItem *resolved = nullptr;
  size_t itemIndex = 0;
  if (itemID) {
    resolved = findResolvedFunctionalItem(*coverage, instance.typeID,
                                          instance.configuration, itemID);
    if (!resolved)
      return OBELISK_RT_INVALID_DESIGN;
    auto position = std::find(type->second.items.begin(),
                              type->second.items.end(), resolved->id);
    if (position == type->second.items.end())
      return OBELISK_RT_INVALID_DESIGN;
    itemIndex = static_cast<size_t>(position - type->second.items.begin());
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
  } else {
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

extern "C" obelisk_rt_status
obelisk_rt_v1_covergroup_set_type_integer_option(
    obelisk_rt_context *context, uint64_t typeID, uint64_t itemID,
    obelisk_rt_covergroup_instance_option_v1 option, int64_t value) {
  if (!context || !typeID || value < 0 ||
      (option != OBELISK_RT_COVERGROUP_OPTION_WEIGHT &&
       option != OBELISK_RT_COVERGROUP_OPTION_GOAL &&
       option != OBELISK_RT_COVERGROUP_OPTION_MERGE_INSTANCES) ||
      (option == OBELISK_RT_COVERGROUP_OPTION_GOAL && value > 100) ||
      (option == OBELISK_RT_COVERGROUP_OPTION_WEIGHT &&
       static_cast<uint64_t>(value) > UINT32_MAX) ||
      (option == OBELISK_RT_COVERGROUP_OPTION_MERGE_INSTANCES &&
       (itemID || value > 1)))
    return OBELISK_RT_INVALID_ARGUMENT;
  OBELISK_RT_TRY {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    CoverageState *coverage = coverageState(context, false);
    if (!coverage || !coverage->schema)
      return OBELISK_RT_INVALID_HANDLE;
    const bool knownType = std::any_of(
        coverage->schema->functionalTypes.begin(),
        coverage->schema->functionalTypes.end(),
        [&](const auto &type) { return type.id == typeID; });
    const bool knownItem = !itemID || std::any_of(
        coverage->schema->functionalItems.begin(),
        coverage->schema->functionalItems.end(), [&](const auto &item) {
          return item.id == itemID && item.type == typeID;
        });
    if (!knownType || !knownItem)
      return OBELISK_RT_INVALID_DESIGN;

    FunctionalCoverageTypeOptions &overrides = coverage->typeOptions[typeID];
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

    for (auto &[key, type] : coverage->types) {
      if (key.typeID != typeID)
        continue;
      obelisk_rt_status status =
          applyFunctionalTypeOptions(*coverage, typeID, type);
      if (status != OBELISK_RT_OK)
        return status;
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
    const bool knownType = std::any_of(
        coverage->schema->functionalTypes.begin(),
        coverage->schema->functionalTypes.end(),
        [&](const auto &type) { return type.id == typeID; });
    const bool knownItem = !itemID || std::any_of(
        coverage->schema->functionalItems.begin(),
        coverage->schema->functionalItems.end(), [&](const auto &item) {
          return item.id == itemID && item.type == typeID;
        });
    if (!knownType || !knownItem)
      return OBELISK_RT_INVALID_DESIGN;
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
    auto schemaExpressions =
        schemaOrdered(coverage->schema->functionalExpressions, instance.typeID,
                      *coverage->schema, false);
    std::vector<const obelisk::coverage::FunctionalExpression *> expected;
    size_t supplied = 0;
    for (const auto *entry : schemaExpressions) {
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
              false))
        return OBELISK_RT_INVALID_DESIGN;
      expected.push_back(entry);
      ++supplied;
    }
    if (supplied != expressionCount)
      return OBELISK_RT_INVALID_DESIGN;
    if (!instance.enabled)
      return OBELISK_RT_OK;

    auto &schema = *coverage->schema;
    auto type = coverage->types.find({instance.typeID, instance.configuration});
    if (type == coverage->types.end())
      return OBELISK_RT_INVALID_DESIGN;
    std::vector<size_t> pendingBinIncrements;
    std::set<size_t> pendingExplicitBinIncrements;
    std::map<uint64_t, uint64_t> pendingIllegalIncrements;
    std::map<uint64_t, std::map<std::vector<uint64_t>, uint64_t>>
        pendingCrossIncrements;
    std::map<uint64_t, std::vector<uint64_t>> sampledBins;
    auto pendingTransitionActive = instance.transitionActive;
    auto pendingTransitionSampledItems = instance.transitionSampledItems;
    for (uint64_t resolvedItem : type->second.items) {
      auto item = std::find_if(
          schema.resolvedFunctionalItems.begin(),
          schema.resolvedFunctionalItems.end(), [&](const auto &entry) {
            return entry.id == resolvedItem &&
                   entry.configuration == type->second.configuration;
          });
      if (item == schema.resolvedFunctionalItems.end())
        return OBELISK_RT_INVALID_DESIGN;
      if (item->kind != obelisk::coverage::FunctionalItemKind::Coverpoint)
        continue;
      if (instance.disabledItems.count(item->templateItem))
        continue;
      const obelisk_rt_functional_value_v1 *sample = nullptr;
      bool itemEnabled = true;
      for (size_t index = 0; index != expected.size(); ++index) {
        if (expected[index]->owner != item->templateItem)
          continue;
        if (expected[index]->role ==
            obelisk::coverage::FunctionalExpressionRole::CoverpointSample)
          sample = &expressions[index];
        else if (expected[index]->role ==
                 obelisk::coverage::FunctionalExpressionRole::CoverpointIff)
          itemEnabled = functionalBooleanTrue(expressions[index]);
      }
      if (!sample || !itemEnabled)
        continue;
      const bool hadPreviousTransitionSample =
          pendingTransitionSampledItems.count(resolvedItem) != 0;
      std::set<uint64_t> illegalBins;
      bool ignored = false;
      bool matchedNonDefault = false;
      std::vector<size_t> ordinary;
      std::vector<size_t> defaults;
      for (size_t binIndex = 0; binIndex != instance.bins.size(); ++binIndex) {
        auto &bin = instance.bins[binIndex];
        if (bin.item != resolvedItem ||
            bin.kind == obelisk::coverage::FunctionalBinKind::Transition ||
            (bin.flags & obelisk::coverage::FunctionalBinEmpty))
          continue;
        bool binEnabled = true;
        for (size_t expressionIndex = 0; expressionIndex != expected.size();
             ++expressionIndex)
          if (expected[expressionIndex]->role ==
                  obelisk::coverage::FunctionalExpressionRole::BinIff &&
              expected[expressionIndex]->owner == bin.templateBin)
            binEnabled = functionalBooleanTrue(expressions[expressionIndex]);
        if (bin.flags & obelisk::coverage::FunctionalBinDefault) {
          if (binEnabled && (!bin.valueSet ||
                             valueSetMatches(schema, type->second.configuration,
                                             bin.valueSet, *sample)))
            defaults.push_back(binIndex);
          continue;
        }
        if (!bin.valueSet ||
            !valueSetMatches(schema, type->second.configuration, bin.valueSet,
                             *sample))
          continue;
        // The iff is a sampling guard, not part of the bin's value set.  A
        // matching defined bin therefore prevents fallback to default even when
        // its iff is false; only the counter update is disabled (IEEE 1800-2017
        // 19.5).
        matchedNonDefault = true;
        if (!binEnabled)
          continue;
        if (bin.flags & obelisk::coverage::FunctionalBinIllegal)
          illegalBins.insert(bin.id);
        else if (bin.flags & obelisk::coverage::FunctionalBinIgnore)
          ignored = true;
        else
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
            ignored = true;
          else
            ordinary.push_back(binIndex);
        }
      }
      if (!illegalBins.empty()) {
        for (uint64_t illegalBin : illegalBins) {
          uint64_t &increment = pendingIllegalIncrements[illegalBin];
          increment = saturatingAdd(increment, 1);
        }
      } else if (!ignored) {
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
      std::set<size_t> transitionHits;
      struct OrdinaryTransitionHit {
        size_t binIndex = 0;
        uint32_t stepCount = 0;
        bool unexcluded = false;
      };
      std::vector<OrdinaryTransitionHit> ordinaryTransitionHits;
      std::vector<uint32_t> exclusionHitLengths;
      std::map<uint64_t, uint32_t> ordinaryAlternativeLengths;
      bool previousPendingRemains = false;
      for (const auto &alternative : schema.resolvedTransitionAlternatives) {
        if (alternative.type != instance.typeID ||
            alternative.configuration != instance.configuration)
          continue;
        auto bin = std::find_if(
            instance.bins.begin(), instance.bins.end(),
            [&](const FunctionalCoverageBinState &candidate) {
              return candidate.id == alternative.bin &&
                     candidate.item == resolvedItem &&
                     candidate.kind ==
                         obelisk::coverage::FunctionalBinKind::Transition;
            });
        if (bin == instance.bins.end())
          continue;
        const size_t binIndex =
            static_cast<size_t>(bin - instance.bins.begin());
        if (bin->flags & obelisk::coverage::FunctionalBinEmpty)
          continue;
        if (!(bin->flags & (obelisk::coverage::FunctionalBinIgnore |
                            obelisk::coverage::FunctionalBinIllegal)) &&
            type->second.suppressedTransitionAlternatives.count(alternative.id))
          continue;
        bool binEnabled = true;
        for (size_t expressionIndex = 0; expressionIndex != expected.size();
             ++expressionIndex)
          if (expected[expressionIndex]->role ==
                  obelisk::coverage::FunctionalExpressionRole::BinIff &&
              expected[expressionIndex]->owner == bin->templateBin)
            binEnabled = functionalBooleanTrue(expressions[expressionIndex]);
        auto active = pendingTransitionActive.find(alternative.id);
        if (!binEnabled) {
          if (active != pendingTransitionActive.end() &&
              !active->second.empty())
            previousPendingRemains = true;
          continue;
        }
        if (!alternative.stepCount ||
            uint64_t{alternative.firstStep} + alternative.stepCount >
                schema.resolvedTransitionSteps.size())
          return OBELISK_RT_INVALID_DESIGN;
        auto program = std::find_if(schema.transitionPrograms.begin(),
                                    schema.transitionPrograms.end(),
                                    [&](const auto &candidate) {
                                      return candidate.bin == bin->templateBin;
                                    });
        if (program == schema.transitionPrograms.end() ||
            alternative.templateAlternativeOrdinal >=
                program->alternativeCount ||
            uint64_t{program->firstAlternative} + program->alternativeCount >
                schema.transitionAlternatives.size())
          return OBELISK_RT_INVALID_DESIGN;
        const auto &templateAlternative =
            schema
                .transitionAlternatives[program->firstAlternative +
                                        alternative.templateAlternativeOrdinal];
        if (templateAlternative.stepCount != alternative.stepCount ||
            uint64_t{templateAlternative.firstStep} +
                    templateAlternative.stepCount >
                schema.transitionSteps.size())
          return OBELISK_RT_INVALID_DESIGN;
        auto matchesStep = [&](uint32_t ordinal) {
          const auto &step =
              schema.resolvedTransitionSteps[alternative.firstStep + ordinal];
          return step.alternative == alternative.id &&
                 step.ordinal == ordinal &&
                 valueSetMatches(schema, type->second.configuration,
                                 step.valueSet, *sample);
        };
        bool hit = false;
        bool unexcludedHit = false;
        using ActiveState =
            FunctionalCoverageInstanceState::TransitionActiveState;
        std::set<ActiveState> next;
        auto retain = [&](const ActiveState &state, bool fromPrevious) {
          next.insert(state);
          previousPendingRemains |= fromPrevious;
        };
        auto advance = [&](const ActiveState &activeState,
                           bool fromPrevious) -> bool {
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
              hit = true;
              unexcludedHit |= !activeState.excluded;
              retain(activeState, fromPrevious);
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
          if (!matchesStep(ordinal)) {
            if (activeState.gapForbiddenOrdinal != UINT32_MAX) {
              if (activeState.gapForbiddenOrdinal >= alternative.stepCount)
                return false;
              if (!matchesStep(activeState.gapForbiddenOrdinal))
                retain(activeState, fromPrevious);
            } else if (repetition ==
                           obelisk::coverage::TransitionRepetitionKind::Goto ||
                       repetition ==
                           obelisk::coverage::TransitionRepetitionKind::
                               Nonconsecutive) {
              retain(activeState, fromPrevious);
            }
            return true;
          }
          const uint64_t consumed = repetitionsConsumed + 1;
          if (consumed >= step.lowerBound) {
            if (ordinal + 1 == alternative.stepCount) {
              hit = true;
              unexcludedHit |= !activeState.excluded;
              if (repetition ==
                  obelisk::coverage::TransitionRepetitionKind::Nonconsecutive)
                retain(
                    {alternative.stepCount, ordinal, 0, activeState.excluded},
                    fromPrevious);
            } else
              retain({ordinal + 1,
                      repetition == obelisk::coverage::
                                        TransitionRepetitionKind::Nonconsecutive
                          ? ordinal
                          : UINT32_MAX,
                      0, activeState.excluded},
                     fromPrevious);
          }
          if (consumed < step.upperBound)
            retain({ordinal, UINT32_MAX, consumed, activeState.excluded},
                   fromPrevious);
          return true;
        };
        if (active != pendingTransitionActive.end())
          for (const ActiveState &state : active->second)
            if (!advance(state, true))
              return OBELISK_RT_INVALID_DESIGN;
        if (!advance({0, UINT32_MAX, 0, false}, false))
          return OBELISK_RT_INVALID_DESIGN;
        if (next.empty())
          pendingTransitionActive.erase(alternative.id);
        else
          pendingTransitionActive[alternative.id] = std::move(next);
        if (!(bin->flags & (obelisk::coverage::FunctionalBinIgnore |
                            obelisk::coverage::FunctionalBinIllegal)))
          ordinaryAlternativeLengths.emplace(alternative.id,
                                             alternative.stepCount);
        if (hit) {
          if (bin->flags & (obelisk::coverage::FunctionalBinIgnore |
                            obelisk::coverage::FunctionalBinIllegal)) {
            transitionHits.insert(binIndex);
            exclusionHitLengths.push_back(alternative.stepCount);
          } else {
            ordinaryTransitionHits.push_back(
                {binIndex, alternative.stepCount, unexcludedHit});
          }
        }
      }
      if (!exclusionHitLengths.empty()) {
        const uint32_t shortestExclusion = *std::min_element(
            exclusionHitLengths.begin(), exclusionHitLengths.end());
        // A fixed exclusion word that ends on this sample taints every live
        // ordinary prefix containing that suffix. Keep the taint with the
        // overlapping matcher state so a longer ordinary word cannot be
        // counted after the ignored or illegal subsequence has completed.
        for (const auto &[alternativeID, stepCount] :
             ordinaryAlternativeLengths) {
          (void)stepCount;
          auto active = pendingTransitionActive.find(alternativeID);
          if (active == pendingTransitionActive.end())
            continue;
          std::set<FunctionalCoverageInstanceState::TransitionActiveState>
              tainted;
          for (auto state : active->second) {
            if (state.ordinal >= shortestExclusion)
              state.excluded = true;
            tainted.insert(std::move(state));
          }
          active->second = std::move(tainted);
        }
      }
      for (const OrdinaryTransitionHit &hit : ordinaryTransitionHits) {
        bool excluded = !hit.unexcluded;
        for (uint32_t exclusionLength : exclusionHitLengths)
          excluded |= exclusionLength <= hit.stepCount;
        if (!excluded)
          transitionHits.insert(hit.binIndex);
      }
      if (hadPreviousTransitionSample && transitionHits.empty() &&
          !previousPendingRemains) {
        for (size_t binIndex = 0; binIndex != instance.bins.size();
             ++binIndex) {
          const auto &bin = instance.bins[binIndex];
          if (bin.item != resolvedItem ||
              bin.kind != obelisk::coverage::FunctionalBinKind::Transition ||
              !(bin.flags & obelisk::coverage::FunctionalBinDefaultSequence) ||
              (bin.flags & obelisk::coverage::FunctionalBinEmpty))
            continue;
          bool binEnabled = true;
          for (size_t expressionIndex = 0; expressionIndex != expected.size();
               ++expressionIndex)
            if (expected[expressionIndex]->role ==
                    obelisk::coverage::FunctionalExpressionRole::BinIff &&
                expected[expressionIndex]->owner == bin.templateBin)
              binEnabled = functionalBooleanTrue(expressions[expressionIndex]);
          if (binEnabled)
            pendingBinIncrements.push_back(binIndex);
        }
      }
      pendingTransitionSampledItems.insert(resolvedItem);
      for (size_t binIndex : transitionHits) {
        const auto &bin = instance.bins[binIndex];
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
      bool crossEnabled = true;
      for (size_t index = 0; index != expected.size(); ++index)
        if (expected[index]->owner == cross.templateItem &&
            expected[index]->role ==
                obelisk::coverage::FunctionalExpressionRole::CrossIff)
          crossEnabled = functionalBooleanTrue(expressions[index]);
      if (!crossEnabled)
        continue;

      std::vector<std::vector<uint64_t>> tuples(1);
      for (uint64_t target : cross.targets) {
        auto hits = sampledBins.find(target);
        if (hits == sampledBins.end() || hits->second.empty()) {
          tuples.clear();
          break;
        }
        if (tuples.size() > SIZE_MAX / hits->second.size())
          return OBELISK_RT_OUT_OF_RESOURCES;
        std::vector<std::vector<uint64_t>> expanded;
        expanded.reserve(tuples.size() * hits->second.size());
        for (const auto &prefix : tuples)
          for (uint64_t bin : hits->second) {
            expanded.push_back(prefix);
            expanded.back().push_back(bin);
          }
        tuples = std::move(expanded);
      }
      for (const auto &tuple : tuples) {
        bool selectedByExplicitBin = false;
        bool ignored = false;
        std::set<uint64_t> illegalBins;
        std::set<size_t> ordinaryBins;
        for (const auto &explicitBin : cross.explicitBins) {
          const bool selected = std::any_of(
              explicitBin.alternatives.begin(), explicitBin.alternatives.end(),
              [&](const auto &rectangle) {
                return std::all_of(
                    rectangle.constraints.begin(), rectangle.constraints.end(),
                    [&](const auto &constraint) {
                      return constraint.targetOrdinal < tuple.size() &&
                             std::binary_search(
                                 constraint.selectedTargetBins.begin(),
                                 constraint.selectedTargetBins.end(),
                                 tuple[constraint.targetOrdinal]);
                    });
              });
          if (!selected)
            continue;
          selectedByExplicitBin = true;
          auto bin =
              std::find_if(instance.bins.begin(), instance.bins.end(),
                           [&](const FunctionalCoverageBinState &candidate) {
                             return candidate.id == explicitBin.bin &&
                                    candidate.item == cross.id;
                           });
          if (bin == instance.bins.end())
            return OBELISK_RT_INVALID_DESIGN;
          bool binEnabled = true;
          for (size_t expressionIndex = 0; expressionIndex != expected.size();
               ++expressionIndex)
            if (expected[expressionIndex]->role ==
                    obelisk::coverage::FunctionalExpressionRole::BinIff &&
                expected[expressionIndex]->owner == bin->templateBin)
              binEnabled = functionalBooleanTrue(expressions[expressionIndex]);
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
            ordinaryBins.insert(
                static_cast<size_t>(bin - instance.bins.begin()));
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
          pendingExplicitBinIncrements.insert(ordinaryBins.begin(),
                                              ordinaryBins.end());
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
    decltype(instance.crossTuples) stagedCrosses;
    for (const auto &[crossID, increments] : pendingCrossIncrements) {
      auto current = instance.crossTuples.find(crossID);
      auto staged =
          stagedCrosses
              .emplace(crossID,
                       current == instance.crossTuples.end()
                           ? decltype(instance.crossTuples)::mapped_type{}
                           : current->second)
              .first;
      for (const auto &[tuple, increment] : increments) {
        FunctionalCoverageCrossTupleState &counter = staged->second[tuple];
        if (increment > UINT64_MAX - counter.count) {
          counter.count = UINT64_MAX;
          counter.overflow = true;
        } else
          counter.count += increment;
      }
    }

    std::vector<obelisk::coverage::IllegalBinDiagnostic> pendingNewDiagnostics;
    std::vector<std::string> pendingIllegalHierarchies;
    pendingIllegalHierarchies.reserve(pendingIllegalIncrements.size());
    const obelisk::coverage::UUID currentRun = makeRunUUID(context, *coverage);
    for (const auto &[bin, increment] : pendingIllegalIncrements) {
      const uint64_t binID = bin;
      auto resolvedBin =
          std::find_if(schema.resolvedFunctionalBins.begin(),
                       schema.resolvedFunctionalBins.end(),
                       [&](const auto &entry) { return entry.id == binID; });
      if (resolvedBin == schema.resolvedFunctionalBins.end())
        return OBELISK_RT_INVALID_DESIGN;
      pendingIllegalHierarchies.push_back(resolvedBin->hierarchy);
      auto diagnostic = std::find_if(
          schema.illegalBinDiagnostics.begin(),
          schema.illegalBinDiagnostics.end(), [&](const auto &entry) {
            return entry.run == currentRun && entry.bin == binID &&
                   entry.instance == handle &&
                   entry.simulationTime == context->schedulerTime;
          });
      if (diagnostic == schema.illegalBinDiagnostics.end())
        pendingNewDiagnostics.push_back({currentRun, bin, handle,
                                         context->schedulerTime, increment,
                                         "illegal bin sampled", 0});
    }
    if (pendingNewDiagnostics.size() > schema.illegalBinDiagnostics.max_size() -
                                           schema.illegalBinDiagnostics.size())
      return OBELISK_RT_OUT_OF_RESOURCES;
    schema.illegalBinDiagnostics.reserve(schema.illegalBinDiagnostics.size() +
                                         pendingNewDiagnostics.size());

    for (auto staged = stagedCrosses.begin(); staged != stagedCrosses.end();) {
      auto current = instance.crossTuples.find(staged->first);
      if (current != instance.crossTuples.end()) {
        current->second.swap(staged->second);
        ++staged;
        continue;
      }
      auto node = stagedCrosses.extract(staged++);
      instance.crossTuples.insert(std::move(node));
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
    for (const auto &[bin, increment] : pendingIllegalIncrements) {
      const uint64_t binID = bin;
      auto diagnostic = std::find_if(
          schema.illegalBinDiagnostics.begin(),
          schema.illegalBinDiagnostics.end(), [&](const auto &entry) {
            return entry.run == currentRun && entry.bin == binID &&
                   entry.instance == handle &&
                   entry.simulationTime == context->schedulerTime;
          });
      if (diagnostic == schema.illegalBinDiagnostics.end())
        continue;
      if (increment > UINT64_MAX - diagnostic->count) {
        diagnostic->count = UINT64_MAX;
        diagnostic->flags |= obelisk::coverage::IllegalBinDiagnosticOverflow;
      } else
        diagnostic->count += increment;
    }
    for (auto &diagnostic : pendingNewDiagnostics)
      schema.illegalBinDiagnostics.push_back(std::move(diagnostic));
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
    uint64_t bitWidth, uint64_t valueSize, void *outValue, void *outUnknown) {
  if (!context || !formalID || !outValue)
    return OBELISK_RT_INVALID_ARGUMENT;
  std::lock_guard<std::recursive_mutex> lock(context->mutex);
  CoverageState *coverage = coverageState(context, false);
  if (!coverage || !coverage->schema)
    return OBELISK_RT_INVALID_HANDLE;
  auto found = coverage->instances.find(handle);
  if (!handle || found == coverage->instances.end())
    return OBELISK_RT_INVALID_HANDLE;
  auto formal =
      std::find_if(found->second.formals.begin(), found->second.formals.end(),
                   [&](const auto &entry) { return entry.id == formalID; });
  if (formal == found->second.formals.end() || formal->bitWidth != bitWidth ||
      formal->valueSize != valueSize)
    return OBELISK_RT_INVALID_DESIGN;
  uint32_t storedKind = formal->kind;
  if (storedKind == OBELISK_RT_FUNCTIONAL_VALUE_ARGUMENT_REF) {
    uint32_t fourState = kind == OBELISK_RT_FUNCTIONAL_VALUE_FOUR_STATE;
    if (kind != OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL && !fourState &&
        kind != OBELISK_RT_FUNCTIONAL_VALUE_REAL)
      return OBELISK_RT_INVALID_ARGUMENT;
    const uint8_t *stateValue =
        context->nativeStateValue
            ? context->nativeStateValue
            : reinterpret_cast<const uint8_t *>(context->stateValue.data());
    const uint8_t *stateUnknown =
        context->nativeStateUnknown
            ? context->nativeStateUnknown
            : reinterpret_cast<const uint8_t *>(context->stateUnknown.data());
    uint64_t stateBits = context->nativeStateValue
                             ? context->nativeStateBitCount
                             : context->stateValue.size() * uint64_t{64};
    return obelisk_rt_v1_argument_ref_load(
        context, stateValue, stateUnknown, stateBits, formal->owner,
        formal->payload, formal->argumentRefKind, bitWidth, valueSize,
        fourState, OBELISK_RT_ARGUMENT_VALUE_BITS, outValue, outUnknown);
  }
  if (storedKind != kind ||
      (kind == OBELISK_RT_FUNCTIONAL_VALUE_FOUR_STATE && !outUnknown))
    return OBELISK_RT_INVALID_DESIGN;
  std::memcpy(outValue, formal->value.data(), static_cast<size_t>(valueSize));
  if (outUnknown) {
    std::memset(outUnknown, 0, static_cast<size_t>(valueSize));
    if (!formal->unknown.empty())
      std::memcpy(outUnknown, formal->unknown.data(),
                  static_cast<size_t>(valueSize));
  }
  return OBELISK_RT_OK;
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
    obelisk::coverage::Status mergeStatus =
        mergePersistentMetricSlice(combined, incoming, *selected);
    if (mergeStatus == obelisk::coverage::Status::OutOfMemory)
      return OBELISK_RT_OUT_OF_MEMORY;
    if (mergeStatus != obelisk::coverage::Status::Ok)
      return OBELISK_RT_OK;
    coverage->schema =
        std::make_unique<obelisk::coverage::Database>(std::move(combined));
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
    for (const auto &boundType : coverage->types) {
      uint64_t typeID = boundType.first.typeID;
      const auto &type = boundType.second;
      if (std::none_of(combined.functionalConfigurations.begin(),
                       combined.functionalConfigurations.end(),
                       [&](const auto &entry) {
                         return entry.type == typeID &&
                                entry.configuration == type.configuration;
                       }))
        return OBELISK_RT_INVALID_DESIGN;
    }
    auto committedSchema =
        std::make_unique<obelisk::coverage::Database>(std::move(combined));
    coverage->schema = std::move(committedSchema);
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
