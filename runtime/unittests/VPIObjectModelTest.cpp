//===- VPIObjectModelTest.cpp - IEEE VPI traversal model tests -----------===//

#include "obelisk/Reflection/VPIObjectModel.h"
#include "../lib/VPIInternal.h"

#include "gtest/gtest.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

namespace {

using namespace obelisk::reflection;

using Mode = VPITraversalMode;
using Order = VPITraversalOrder;
using KindSet = std::set<uint32_t>;

constexpr size_t kExpectedTraversalCount = 1872;
static_assert(sizeof(vpiTraversals) / sizeof(vpiTraversals[0]) ==
              kExpectedTraversalCount);

struct OracleKey {
  uint32_t source;
  uint32_t selector;
  Mode mode;

  bool operator<(const OracleKey &other) const {
    return std::tie(source, selector, mode) <
           std::tie(other.source, other.selector, other.mode);
  }
};

struct OracleEdge {
  KindSet targets;
  Order order;
  bool statementContainment;
  const char *clause;
};

using OracleGraph = std::map<OracleKey, OracleEdge>;

std::string objectName(uint32_t value) {
  if (value == 0)
    return "NULL";
  if (const auto *object = findVPIObjectSelector(value))
    return object->apiName;
  if (const auto *relation = findVPIRelation(value))
    return relation->apiName;
  return std::to_string(value);
}

std::string keyName(uint32_t source, uint32_t selector, Mode mode) {
  std::ostringstream stream;
  stream << objectName(source) << " --" << objectName(selector) << " ("
         << (mode == Mode::Handle ? "handle" : "iterate") << ")";
  return stream.str();
}

const VPITraversalDescriptor &requireTraversal(uint32_t source,
                                               uint32_t selector, Mode mode) {
  const auto *edge = findVPITraversal(source, selector, mode);
  EXPECT_NE(edge, nullptr) << keyName(source, selector, mode);
  // Keep subsequent diagnostics useful after a missing-edge failure.
  static constexpr VPITraversalDescriptor missing{
      0,
      0,
      Mode::Handle,
      Order::None,
      static_cast<VPIObjectSetID>(0),
      false,
      "missing",
      "missing"};
  return edge ? *edge : missing;
}

std::set<uint32_t> expandedTargets(VPIObjectSetID targets) {
  std::set<uint32_t> result;
  for (const auto &object : vpiObjectKinds)
    if (object.aliasOf == nullptr && object.role == VPIObjectRole::Concrete &&
        vpiObjectSetContains(targets, object.value))
      result.insert(object.value);
  return result;
}

std::string setNames(const std::set<uint32_t> &values) {
  std::ostringstream stream;
  bool first = true;
  for (uint32_t value : values) {
    if (!first)
      stream << ", ";
    first = false;
    stream << objectName(value);
  }
  return stream.str();
}

void expectTargetsExactly(uint32_t source, uint32_t selector, Mode mode,
                          std::initializer_list<uint32_t> expected) {
  const auto &edge = requireTraversal(source, selector, mode);
  std::set<uint32_t> expectedSet(expected);
  std::set<uint32_t> actualSet = expandedTargets(edge.targets);
  EXPECT_EQ(actualSet, expectedSet) << keyName(source, selector, mode)
                                    << "\nexpected: " << setNames(expectedSet)
                                    << "\nactual:   " << setNames(actualSet);
}

void expectContains(uint32_t source, uint32_t selector, Mode mode,
                    std::initializer_list<uint32_t> expected,
                    std::initializer_list<uint32_t> forbidden = {}) {
  const auto &edge = requireTraversal(source, selector, mode);
  for (uint32_t target : expected)
    EXPECT_TRUE(vpiObjectSetContains(edge.targets, target))
        << keyName(source, selector, mode) << " must contain "
        << objectName(target);
  for (uint32_t target : forbidden)
    EXPECT_FALSE(vpiObjectSetContains(edge.targets, target))
        << keyName(source, selector, mode) << " must not contain "
        << objectName(target);
}

void expectAbsent(uint32_t source, uint32_t selector, Mode mode) {
  EXPECT_EQ(findVPITraversal(source, selector, mode), nullptr)
      << keyName(source, selector, mode);
}

void expectOrdinaryExpressionTargets(uint32_t source, uint32_t selector,
                                     Mode mode) {
  expectContains(source, selector, mode,
                 {vpiNet, vpiReg, vpiConstant, vpiOperation, vpiRefObj},
                 {vpiAnyPattern, vpiTaggedPattern, vpiStructPattern,
                  vpiSequenceInst, vpiDistribution, vpiPropertyExpr,
                  vpiMulticlockSequenceExpr, vpiPropertyInst, vpiClockedProp,
                  vpiCaseProperty, vpiConstraint, vpiConstraintOrdering,
                  vpiImplication, vpiConstrIf, vpiConstrIfElse,
                  vpiConstrForEach});
}

void expectNoOrder(uint32_t source, uint32_t selector, Mode mode) {
  EXPECT_EQ(requireTraversal(source, selector, mode).order, Order::None)
      << keyName(source, selector, mode);
}

void refreshImageChecksum(std::vector<uint8_t> &image) {
  uint64_t checksum = checksumVPIObjectModelImage(image.data(), image.size());
  for (unsigned byte = 0; byte != 8; ++byte)
    image[16 + byte] = static_cast<uint8_t>(checksum >> (byte * 8));
}

OracleGraph buildLrmOracle() {
  OracleGraph graph;
  auto S = [](std::initializer_list<uint32_t> kinds) {
    return KindSet(kinds.begin(), kinds.end());
  };
  auto add = [&](const char *clause, const KindSet &sources, uint32_t selector,
                 Mode mode, const KindSet &targets, Order order,
                 bool statementContainment = false) {
    for (uint32_t source : sources) {
      auto [iterator, inserted] = graph.emplace(
          OracleKey{source, selector, mode},
          OracleEdge{targets, order, statementContainment, clause});
      if (!inserted)
        ADD_FAILURE() << "duplicate LRM oracle key "
                      << keyName(source, selector, mode) << " in " << clause
                      << "; previously declared in " << iterator->second.clause;
    }
  };

#include "VPITraversalLrmOracleEarly.inc"
#include "VPITraversalLrmOracleLate.inc"

  return graph;
}

TEST(VPIObjectModel, TraversalGraphExactlyMatchesIndependentLrmOracle) {
  OracleGraph expected = buildLrmOracle();
  ASSERT_EQ(expected.size(), kExpectedTraversalCount);
  std::set<OracleKey> actualKeys;

  for (const auto &edge : vpiTraversals) {
    OracleKey key{edge.sourceType, edge.selector, edge.mode};
    actualKeys.insert(key);
    auto iterator = expected.find(key);
    ASSERT_NE(iterator, expected.end())
        << "unexpected traversal "
        << keyName(edge.sourceType, edge.selector, edge.mode) << " from "
        << edge.clause;
    EXPECT_EQ(expandedTargets(edge.targets), iterator->second.targets)
        << keyName(edge.sourceType, edge.selector, edge.mode)
        << " differs from LRM " << iterator->second.clause;
    EXPECT_EQ(edge.order, iterator->second.order)
        << keyName(edge.sourceType, edge.selector, edge.mode)
        << " differs from LRM " << iterator->second.clause;
    EXPECT_EQ(edge.statementContainment, iterator->second.statementContainment)
        << keyName(edge.sourceType, edge.selector, edge.mode)
        << " has incorrect statement-containment semantics";
  }

  EXPECT_EQ(actualKeys.size(), expected.size());
  for (const auto &[key, edge] : expected)
    EXPECT_TRUE(actualKeys.count(key))
        << "missing LRM " << edge.clause << " traversal "
        << keyName(key.source, key.selector, key.mode);
}

TEST(VPIObjectModel, StatementCallbacksExactlyMatchLrmTable38_6) {
  using Policy = VPIStatementCallbackPolicy;
  const std::map<uint32_t, Policy> expected{
      {vpiBegin, Policy::OnceBefore},
      {vpiNamedBegin, Policy::OnceBefore},
      {vpiFork, Policy::OnceBefore},
      {vpiNamedFork, Policy::OnceBefore},
      {vpiIf, Policy::OnceBefore},
      {vpiIfElse, Policy::OnceBefore},
      {vpiWhile, Policy::ConditionEachIteration},
      {vpiRepeat, Policy::RepeatEncounterAndIteration},
      {vpiFor, Policy::ForInitialAndIncrement},
      {vpiForever, Policy::ForeverEncounterAndIteration},
      {vpiWait, Policy::OnceBefore},
      {vpiCase, Policy::OnceBefore},
      {vpiAssignment, Policy::OnceBefore},
      {vpiAssignStmt, Policy::OnceBefore},
      {vpiDeassign, Policy::OnceBefore},
      {vpiDisable, Policy::OnceBefore},
      {vpiForce, Policy::OnceBefore},
      {vpiRelease, Policy::OnceBefore},
      {vpiEventStmt, Policy::OnceBefore},
      {vpiDelayControl, Policy::DelayEncounter},
      {vpiEventControl, Policy::EventEncounter},
      {vpiTaskCall, Policy::CallBefore},
      {vpiSysTaskCall, Policy::CallBefore},
  };
  std::map<uint32_t, Policy> actual;
  for (const auto &callback : vpiStatementCallbacks) {
    EXPECT_TRUE(actual.emplace(callback.objectType, callback.policy).second);
    EXPECT_STREQ(callback.clause, "IEEE 1800-2017 Table 38-6");
    EXPECT_EQ(callback.phaseMask,
              callback.objectType == vpiFor ? uint8_t{0x6} : uint8_t{0x1})
        << objectName(callback.objectType);
  }
  EXPECT_EQ(actual, expected);

  using Phase = VPIStatementCallbackPhase;
  EXPECT_TRUE(isVPIStatementCallbackPhase(vpiFor, Phase::BeforeForControls));
  EXPECT_TRUE(isVPIStatementCallbackPhase(vpiFor, Phase::BeforeForIncrement));
  EXPECT_FALSE(isVPIStatementCallbackPhase(vpiFor, Phase::BeforeExecute));
  for (uint32_t kind :
       {vpiNullStmt, vpiCaseItem, vpiContAssign, vpiImmediateAssert, vpiDoWhile,
        vpiForeachStmt, vpiReturnStmt, vpiBreak, vpiContinue})
    EXPECT_EQ(findVPIStatementCallback(kind), nullptr) << objectName(kind);
}

TEST(VPIObjectModel, ScopeOwnedStatementsAreExactlyTheModuleLevelKinds) {
  const uint64_t scopeOwnedMask =
      vpiFamilyMask(VPIObjectFamily::ScopeOwnedStatement);
  for (const auto &object : vpiObjectKinds) {
    if (object.aliasOf != nullptr || object.role != VPIObjectRole::Concrete)
      continue;
    bool expected = object.value == vpiContAssign ||
                    object.value == vpiContAssignBit ||
                    object.value == vpiAliasStmt;
    EXPECT_EQ((object.families & scopeOwnedMask) != 0, expected)
        << object.apiName;
  }
}

TEST(VPIObjectModel, BlocksIterateStatementsWithoutAnLrmOrderGuarantee) {
  for (uint32_t source : {vpiBegin, vpiNamedBegin, vpiFork, vpiNamedFork}) {
    const auto &edge = requireTraversal(source, vpiStmt, Mode::Iterate);
    EXPECT_EQ(edge.order, Order::None) << objectName(source);
    EXPECT_TRUE(vpiObjectSetContains(edge.targets, vpiAssignment));
    EXPECT_TRUE(vpiObjectSetContains(edge.targets, vpiIfElse));
    EXPECT_TRUE(vpiObjectSetContains(edge.targets, vpiFor));
    EXPECT_FALSE(vpiObjectSetContains(edge.targets, vpiCaseItem));
    expectAbsent(source, vpiStmt, Mode::Handle);
  }
}

TEST(VPIObjectModel, StatementContainmentIsDistinctFromCrossReferences) {
  struct Key {
    uint32_t source;
    uint32_t selector;
    Mode mode;
  };
  constexpr Key containment[] = {
      {vpiModule, vpiContAssign, Mode::Iterate},
      {vpiModule, vpiAliasStmt, Mode::Iterate},
      {vpiContAssign, vpiBit, Mode::Iterate},
      {vpiTask, vpiStmt, Mode::Handle},
      {vpiBegin, vpiStmt, Mode::Iterate},
      {vpiInitial, vpiStmt, Mode::Handle},
      {vpiAssignment, vpiDelayControl, Mode::Handle},
      {vpiRepeatControl, vpiEventControl, Mode::Handle},
      {vpiIfElse, vpiElseStmt, Mode::Handle},
      {vpiCase, vpiCaseItem, Mode::Iterate},
      {vpiFor, vpiForInitStmt, Mode::Handle},
      {vpiFor, vpiForInitStmt, Mode::Iterate},
      {vpiImmediateAssert, vpiStmt, Mode::Handle},
      {vpiAssert, vpiElseStmt, Mode::Handle},
  };
  for (const auto &key : containment)
    EXPECT_TRUE(isVPIStatementContainment(key.source, key.selector, key.mode))
        << keyName(key.source, key.selector, key.mode);

  constexpr Key crossReferences[] = {
      {vpiNet, vpiContAssign, Mode::Iterate},
      {vpiFrame, vpiStmt, Mode::Handle},
      {vpiThread, vpiOrigin, Mode::Handle},
      {vpiContAssignBit, vpiParent, Mode::Handle},
      {vpiClockingBlock, vpiClockingEvent, Mode::Handle},
      {vpiIfElse, vpiScope, Mode::Handle},
  };
  for (const auto &key : crossReferences) {
    ASSERT_NE(findVPITraversal(key.source, key.selector, key.mode), nullptr)
        << keyName(key.source, key.selector, key.mode);
    EXPECT_FALSE(isVPIStatementContainment(key.source, key.selector, key.mode))
        << keyName(key.source, key.selector, key.mode);
  }
}

TEST(VPIObjectModel, IteratorUseIsExactlyTheDerivedIterationSourceClosure) {
  KindSet expectedSources;
  for (const auto &edge : vpiTraversals)
    if (edge.mode == Mode::Iterate && edge.sourceType != 0)
      expectedSources.insert(edge.sourceType);

  const auto &use = requireTraversal(vpiIterator, vpiUse, Mode::Handle);
  EXPECT_EQ(expandedTargets(use.targets), expectedSources);
  expectAbsent(vpiIterator, vpiUse, Mode::Iterate);
}

TEST(VPIObjectModel, CompactImageExactlyMatchesTheLrmTraversalGraph) {
  ASSERT_TRUE(validateVPIObjectModelImage(vpiObjectModelImage,
                                          sizeof(vpiObjectModelImage)));
  EXPECT_LT(sizeof(vpiObjectModelImage), 32u * 1024u);
  EXPECT_EQ(readVPIObjectModelImage32(vpiObjectModelImage, 60),
            kExpectedTraversalCount);

  size_t canonicalObjects = 0;
  for (const auto &object : vpiObjectKinds)
    canonicalObjects += object.aliasOf == nullptr;
  size_t canonicalRelations = 0;
  for (const auto &relation : vpiRelations)
    canonicalRelations += relation.aliasOf == nullptr;
  EXPECT_EQ(readVPIObjectModelImage32(vpiObjectModelImage, 28),
            canonicalObjects);
  EXPECT_EQ(readVPIObjectModelImage32(vpiObjectModelImage, 36),
            canonicalRelations);

  uint32_t objectOffset = readVPIObjectModelImage32(vpiObjectModelImage, 24);
  uint32_t imageObject = 0;
  for (const auto &object : vpiObjectKinds) {
    if (object.aliasOf != nullptr)
      continue;
    const uint8_t *record = vpiObjectModelImage + objectOffset +
                            imageObject * vpiObjectModelImageObjectSize;
    EXPECT_EQ(readVPIObjectModelImage16(record, 0), object.value)
        << object.apiName;
    EXPECT_EQ(record[2], static_cast<uint8_t>(object.role)) << object.apiName;
    EXPECT_EQ(record[3], 0) << object.apiName;
    EXPECT_EQ(readVPIObjectModelImage64(record, 4), object.families)
        << object.apiName;
    ++imageObject;
  }
  uint32_t relationOffset = readVPIObjectModelImage32(vpiObjectModelImage, 32);
  uint32_t imageRelation = 0;
  for (const auto &relation : vpiRelations) {
    if (relation.aliasOf != nullptr)
      continue;
    const uint8_t *record = vpiObjectModelImage + relationOffset +
                            imageRelation * vpiObjectModelImageRelationSize;
    EXPECT_EQ(readVPIObjectModelImage16(record, 0), relation.value)
        << relation.apiName;
    EXPECT_EQ(record[2], static_cast<uint8_t>(relation.cardinality))
        << relation.apiName;
    EXPECT_EQ(record[3], 0) << relation.apiName;
    ++imageRelation;
  }

  for (const auto &edge : vpiTraversals) {
    SCOPED_TRACE(keyName(edge.sourceType, edge.selector, edge.mode));
    VPIObjectModelImageTraversal imageEdge{};
    ASSERT_TRUE(findVPIObjectModelImageTraversal(vpiObjectModelImage,
                                                 edge.sourceType, edge.selector,
                                                 edge.mode, imageEdge));
    EXPECT_EQ(imageEdge.order, edge.order);
    EXPECT_EQ(imageEdge.statementContainment, edge.statementContainment);
    for (const auto &object : vpiObjectKinds) {
      if (object.aliasOf != nullptr || object.role != VPIObjectRole::Concrete)
        continue;
      EXPECT_EQ(vpiObjectModelImageTargetContains(
                    vpiObjectModelImage, imageEdge.targets, object.value),
                vpiObjectSetContains(edge.targets, object.value))
          << object.apiName;
    }
  }
}

TEST(VPIObjectModel, CompactImageValidationRejectsCorruptionAndTruncation) {
  std::vector<uint8_t> damaged(std::begin(vpiObjectModelImage),
                               std::end(vpiObjectModelImage));
  auto reset = [&] {
    damaged.assign(std::begin(vpiObjectModelImage),
                   std::end(vpiObjectModelImage));
  };
  auto write16 = [&](uint32_t offset, uint16_t value) {
    damaged[offset] = static_cast<uint8_t>(value);
    damaged[offset + 1] = static_cast<uint8_t>(value >> 8);
  };
  damaged[0] ^= 1;
  EXPECT_FALSE(validateVPIObjectModelImage(damaged.data(), damaged.size()));
  damaged[0] ^= 1;
  damaged.back() ^= 1;
  EXPECT_FALSE(validateVPIObjectModelImage(damaged.data(), damaged.size()));
  EXPECT_FALSE(validateVPIObjectModelImage(vpiObjectModelImage,
                                           sizeof(vpiObjectModelImage) - 1));

  reset();
  uint32_t objectOffset = readVPIObjectModelImage32(damaged.data(), 24);
  damaged[objectOffset + 3] = 1;
  refreshImageChecksum(damaged);
  EXPECT_FALSE(
      validateVPIObjectModelImageStructure(damaged.data(), damaged.size()));
  EXPECT_FALSE(validateVPIObjectModelImage(damaged.data(), damaged.size()));

  reset();
  uint32_t setOffset = readVPIObjectModelImage32(damaged.data(), 40);
  damaged[setOffset + 2] = 0;
  damaged[setOffset + 3] = 0;
  refreshImageChecksum(damaged);
  EXPECT_FALSE(
      validateVPIObjectModelImageStructure(damaged.data(), damaged.size()));
  EXPECT_FALSE(validateVPIObjectModelImage(damaged.data(), damaged.size()));

  reset();
  uint32_t traversalOffset = readVPIObjectModelImage32(damaged.data(), 56);
  uint32_t traversalCount = readVPIObjectModelImage32(damaged.data(), 60);
  for (uint32_t index = 0; index != traversalCount; ++index) {
    uint8_t *record = damaged.data() + traversalOffset +
                      index * vpiObjectModelImageTraversalSize;
    if (record[6] == static_cast<uint8_t>(Mode::Handle)) {
      record[7] = static_cast<uint8_t>(Order::Source);
      break;
    }
  }
  refreshImageChecksum(damaged);
  EXPECT_FALSE(
      validateVPIObjectModelImageStructure(damaged.data(), damaged.size()));
  EXPECT_FALSE(validateVPIObjectModelImage(damaged.data(), damaged.size()));

  reset();
  uint32_t setCount = readVPIObjectModelImage32(damaged.data(), 44);
  uint32_t kindOffset = readVPIObjectModelImage32(damaged.data(), 48);
  for (uint32_t index = 0; index != setCount; ++index) {
    const uint8_t *record =
        damaged.data() + setOffset + index * vpiObjectModelImageSetSize;
    uint16_t first = readVPIObjectModelImage16(record, 0);
    uint16_t count = readVPIObjectModelImage16(record, 2);
    if (count < 2)
      continue;
    uint16_t left =
        readVPIObjectModelImage16(damaged.data(), kindOffset + first * 2);
    uint16_t right = readVPIObjectModelImage16(
        damaged.data(), kindOffset + (uint32_t{first} + 1) * 2);
    write16(kindOffset + first * 2, right);
    write16(kindOffset + (uint32_t{first} + 1) * 2, left);
    break;
  }
  refreshImageChecksum(damaged);
  EXPECT_FALSE(
      validateVPIObjectModelImageStructure(damaged.data(), damaged.size()));

  reset();
  write16(traversalOffset + 4, static_cast<uint16_t>(setCount));
  refreshImageChecksum(damaged);
  EXPECT_FALSE(
      validateVPIObjectModelImageStructure(damaged.data(), damaged.size()));

  reset();
  damaged[traversalOffset + 6] = 2;
  refreshImageChecksum(damaged);
  EXPECT_FALSE(
      validateVPIObjectModelImageStructure(damaged.data(), damaged.size()));

  reset();
  write16(traversalOffset, UINT16_MAX);
  refreshImageChecksum(damaged);
  EXPECT_FALSE(
      validateVPIObjectModelImageStructure(damaged.data(), damaged.size()));

  reset();
  uint32_t relationOffset = readVPIObjectModelImage32(damaged.data(), 32);
  damaged[relationOffset + 2] = 3;
  refreshImageChecksum(damaged);
  EXPECT_FALSE(
      validateVPIObjectModelImageStructure(damaged.data(), damaged.size()));

  // A structurally valid but different model is rejected by the generated
  // canonical fingerprint, keeping compiler and runtime schemas in lockstep.
  reset();
  damaged[objectOffset + 4] ^= 0x80;
  refreshImageChecksum(damaged);
  EXPECT_TRUE(
      validateVPIObjectModelImageStructure(damaged.data(), damaged.size()));
  EXPECT_FALSE(validateVPIObjectModelImage(damaged.data(), damaged.size()));
}

TEST(VPIObjectModel, TraversalDescriptorsAreSortedUniqueAndSearchable) {
  const VPITraversalDescriptor *previous = nullptr;
  for (const auto &edge : vpiTraversals) {
    SCOPED_TRACE(keyName(edge.sourceType, edge.selector, edge.mode));
    EXPECT_EQ(findVPITraversal(edge.sourceType, edge.selector, edge.mode),
              &edge);
    if (edge.sourceType != 0) {
      EXPECT_NE(findVPIObjectKind(edge.sourceType), nullptr);
    }
    EXPECT_TRUE(findVPIObjectSelector(edge.selector) != nullptr ||
                findVPIRelation(edge.selector) != nullptr);
    if (previous) {
      bool strictlyOrdered = previous->sourceType < edge.sourceType ||
                             (previous->sourceType == edge.sourceType &&
                              (previous->selector < edge.selector ||
                               (previous->selector == edge.selector &&
                                static_cast<uint8_t>(previous->mode) <
                                    static_cast<uint8_t>(edge.mode))));
      EXPECT_TRUE(strictlyOrdered) << "duplicate or unsorted traversal key";
    }
    previous = &edge;
  }
}

TEST(VPIObjectModel, EveryTraversalHasConcreteNonemptyTargets) {
  for (const auto &edge : vpiTraversals) {
    SCOPED_TRACE(keyName(edge.sourceType, edge.selector, edge.mode));
    EXPECT_FALSE(expandedTargets(edge.targets).empty());
    EXPECT_NE(edge.selectorName, nullptr);
    EXPECT_NE(edge.clause, nullptr);
    EXPECT_NE(edge.clause[0], '\0');
  }
}

// The unqualified "expr" enclosure recurs throughout 37.5-37.83.  Every one
// of these independently listed arcs accepts nets and variables as ordinary
// expressions, while assertion, pattern, and constraint nodes belong only to
// diagrams that name those nodes explicitly.
TEST(VPIObjectModel, OrdinaryExpressionArcsUseTheExactLrmUnion) {
  struct Key {
    uint32_t source;
    uint32_t selector;
    Mode mode;
  };
  constexpr Key keys[] = {
      {vpiModule, vpiDefaultDisableIff, Mode::Handle},
      {vpiInterface, vpiDefaultDisableIff, Mode::Handle},
      {vpiProgram, vpiDefaultDisableIff, Mode::Handle},
      {vpiGateArray, vpiLeftRange, Mode::Handle},
      {vpiGateArray, vpiRightRange, Mode::Handle},
      {vpiGateArray, vpiDelay, Mode::Handle},
      {vpiPort, vpiHighConn, Mode::Handle},
      {vpiPort, vpiLowConn, Mode::Handle},
      {vpiNet, vpiLeftRange, Mode::Handle},
      {vpiNet, vpiRightRange, Mode::Handle},
      {vpiNetBit, vpiIndex, Mode::Handle},
      {vpiNetBit, vpiIndex, Mode::Iterate},
      {vpiReg, vpiExpr, Mode::Handle},
      {vpiReg, vpiLeftRange, Mode::Handle},
      {vpiReg, vpiRightRange, Mode::Handle},
      {vpiVarSelect, vpiIndex, Mode::Handle},
      {vpiVarSelect, vpiIndex, Mode::Iterate},
      {vpiRange, vpiLeftRange, Mode::Handle},
      {vpiRange, vpiRightRange, Mode::Handle},
      {vpiParameter, vpiExpr, Mode::Handle},
      {vpiExtends, vpiArgument, Mode::Iterate},
      {vpiClassObj, vpiMessages, Mode::Iterate},
      {vpiConstraintOrdering, vpiSolveBefore, Mode::Iterate},
      {vpiConstraintOrdering, vpiSolveAfter, Mode::Iterate},
      {vpiDistribution, vpiExpr, Mode::Handle},
      {vpiDistItem, vpiWeight, Mode::Handle},
      {vpiGate, vpiIndex, Mode::Handle},
      {vpiGate, vpiDelay, Mode::Handle},
      {vpiModPath, vpiCondition, Mode::Handle},
      {vpiModPath, vpiDelay, Mode::Handle},
      {vpiTchk, vpiDelay, Mode::Handle},
      {vpiTchkTerm, vpiCondition, Mode::Handle},
      {vpiFunction, vpiLeftRange, Mode::Handle},
      {vpiFunction, vpiRightRange, Mode::Handle},
      {vpiContAssign, vpiLhs, Mode::Handle},
      {vpiContAssign, vpiRhs, Mode::Handle},
      {vpiAssert, vpiClockingEvent, Mode::Handle},
      {vpiPropertyInst, vpiDisableCondition, Mode::Handle},
      {vpiPropertySpec, vpiClockingEvent, Mode::Handle},
      {vpiCaseProperty, vpiCondition, Mode::Handle},
      {vpiCasePropertyItem, vpiExpr, Mode::Iterate},
      {vpiImmediateAssert, vpiExpr, Mode::Handle},
      {vpiLetDecl, vpiExpr, Mode::Handle},
      {vpiLetExpr, vpiArgument, Mode::Iterate},
      {vpiIndexedPartSelect, vpiBaseExpr, Mode::Handle},
      {vpiIndexedPartSelect, vpiWidthExpr, Mode::Handle},
      {vpiPartSelect, vpiLeftRange, Mode::Handle},
      {vpiPartSelect, vpiRightRange, Mode::Handle},
      {vpiAssignment, vpiRhs, Mode::Handle},
      {vpiWhile, vpiCondition, Mode::Handle},
      {vpiWait, vpiCondition, Mode::Handle},
      {vpiDelayControl, vpiDelay, Mode::Handle},
      {vpiRepeatControl, vpiExpr, Mode::Handle},
      {vpiIf, vpiCondition, Mode::Handle},
      {vpiCase, vpiCondition, Mode::Handle},
      {vpiCaseItem, vpiExpr, Mode::Iterate},
      {vpiFor, vpiCondition, Mode::Handle},
      {vpiReturnStmt, vpiCondition, Mode::Handle},
      {vpiDoWhile, vpiCondition, Mode::Handle},
      {vpiAssignStmt, vpiLhs, Mode::Handle},
      {vpiAssignStmt, vpiRhs, Mode::Handle},
      {vpiAliasStmt, vpiLhs, Mode::Handle},
      {vpiAliasStmt, vpiRhs, Mode::Handle},
      {vpiGenScope, vpiIndex, Mode::Handle},
  };
  for (const auto &key : keys) {
    SCOPED_TRACE(keyName(key.source, key.selector, key.mode));
    expectOrdinaryExpressionTargets(key.source, key.selector, key.mode);
  }
}

// IEEE 1800-2017 37.5, 37.40, 37.41, and 37.79-37.80.  Root traversals are
// unusually easy to over-broaden because their reference object is NULL.
TEST(VPIObjectModel, RootTraversalsHaveExactSpecialTargetsAndOrders) {
  expectTargetsExactly(0, vpiModule, Mode::Iterate, {vpiModule});
  expectTargetsExactly(0, vpiSysTfCall, Mode::Handle,
                       {vpiSysFuncCall, vpiSysTaskCall});
  expectTargetsExactly(0, vpiFrame, Mode::Handle, {vpiFrame});
  expectTargetsExactly(0, vpiActiveTimeFormat, Mode::Handle, {vpiSysTaskCall});
  EXPECT_EQ(requireTraversal(0, vpiTimeQueue, Mode::Iterate).order,
            Order::Time);
  expectAbsent(0, vpiActiveTimeFormat, Mode::Iterate);
  expectAbsent(0, vpiSysTfCall, Mode::Iterate);
}

// 37.6-37.12 distinguish immediate instance, lexical scope, and array-parent
// relationships.  These negative cases prevent a generic Scope/Array family
// from leaking into a relation with a narrower LRM node.
TEST(VPIObjectModel, InstanceScopeAndArrayRelationsDoNotLeak) {
  expectTargetsExactly(vpiModport, vpiInterface, Mode::Handle, {vpiInterface});
  expectTargetsExactly(vpiInterfaceTfDecl, vpiTask, Mode::Iterate, {vpiTask});
  expectTargetsExactly(vpiInterfaceTfDecl, vpiFunction, Mode::Iterate,
                       {vpiFunction});
  expectTargetsExactly(vpiModule, vpiInstance, Mode::Handle, {vpiModule});
  expectTargetsExactly(vpiNamedEvent, vpiParent, Mode::Handle,
                       {vpiNamedEventArray});
  expectAbsent(vpiModuleArray, vpiIndex, Mode::Handle);
  expectAbsent(vpiPrimitiveArray, vpiIndex, Mode::Handle);
  expectAbsent(vpiPortBit, vpiBit, Mode::Iterate);
}

// Cross-checks for dashed enclosures that span multiple concrete object
// kinds.  These are transcribed from 37.10, 37.12, 37.15-37.16, 37.26,
// 37.29, 37.32, 37.36-37.37, and 37.41.
TEST(VPIObjectModel, EarlyDiagramEnclosuresExpandWithoutFamilyLeakage) {
  expectContains(vpiModule, vpiParameter, Mode::Iterate,
                 {vpiParameter, vpiTypeParameter}, {vpiSpecParam});
  EXPECT_NE(findVPITraversal(vpiTypeParameter, vpiScope, Mode::Handle),
            nullptr);
  expectContains(vpiAssignment, vpiScope, Mode::Handle,
                 {vpiModule, vpiTask, vpiFunction, vpiClassObj});
  expectContains(vpiFrame, vpiOrigin, Mode::Handle,
                 {vpiModule, vpiTask, vpiFunction, vpiClassObj});
  expectContains(vpiRefObj, vpiActual, Mode::Handle,
                 {vpiNetArray, vpiInterconnectArray});

  EXPECT_NE(findVPITraversal(vpiNet, vpiBit, Mode::Iterate), nullptr);
  expectAbsent(vpiNetBit, vpiBit, Mode::Iterate);
  expectAbsent(vpiNetArray, vpiBit, Mode::Iterate);
  expectAbsent(vpiInterconnectArray, vpiBit, Mode::Iterate);

  expectContains(vpiConstraint, vpiConstraintItem, Mode::Iterate,
                 {vpiConstant, vpiOperation, vpiRefObj, vpiConstrIf,
                  vpiConstraintOrdering},
                 {vpiNamedEvent, vpiNamedEventArray, vpiVirtualInterfaceVar});
  expectTargetsExactly(vpiModPath, vpiInstance, Mode::Handle,
                       {vpiModule, vpiInterface});

  EXPECT_EQ(
      requireTraversal(vpiClassDefn, vpiDerivedClasses, Mode::Iterate).order,
      Order::None);
  EXPECT_EQ(requireTraversal(vpiClassDefn, vpiInstance, Mode::Iterate).order,
            Order::None);
  EXPECT_EQ(requireTraversal(vpiFrame, vpiAutomatics, Mode::Iterate).order,
            Order::None);
}

// 37.19-37.28 defines typespec, parameter, enum, array, and virtual-interface
// relations.  In particular, a virtual interface cannot accept an arbitrary
// concrete typespec merely because both are declarations.
TEST(VPIObjectModel, TypeRelationsPreserveLrmNarrowing) {
  expectTargetsExactly(vpiVirtualInterfaceVar, vpiTypespec, Mode::Handle,
                       {vpiInterfaceTypespec});
  expectContains(vpiVirtualInterfaceVar, vpiExpr, Mode::Handle,
                 {vpiInterface, vpiModport, vpiVirtualInterfaceVar, vpiRefObj},
                 {vpiIntTypespec, vpiNamedEvent, vpiNamedEventArray});
  expectTargetsExactly(vpiNamedEvent, vpiParent, Mode::Handle,
                       {vpiNamedEventArray});
  expectAbsent(vpiPackedArrayVar, vpiVarSelect, Mode::Iterate);
  expectTargetsExactly(vpiRegArray, vpiVarSelect, Mode::Iterate,
                       {vpiVarSelect});
}

// 37.14-37.17.  Connection relations have deliberately different target
// nodes: low connection is a reference object, high connection is an
// expression, and ref-object actuals include declared objects but not
// declaration metadata.
TEST(VPIObjectModel, PortsReferencesNetsAndVariablesKeepConnectionKinds) {
  expectContains(vpiPort, vpiLowConn, Mode::Handle,
                 {vpiConstant, vpiOperation, vpiRefObj},
                 {vpiPropertyExpr, vpiNamedEvent, vpiTypespec});
  expectContains(vpiPort, vpiHighConn, Mode::Handle,
                 {vpiConstant, vpiOperation, vpiRefObj},
                 {vpiPropertyExpr, vpiNamedEvent, vpiTypespec});
  expectContains(vpiRefObj, vpiActual, Mode::Handle,
                 {vpiInterface, vpiInterfaceArray, vpiModport, vpiNet, vpiReg,
                  vpiNamedEvent, vpiNamedEventArray, vpiPartSelect},
                 {vpiTypespec, vpiPropertyExpr, vpiConstraint});
  expectContains(vpiNet, vpiDriver, Mode::Iterate,
                 {vpiPort, vpiForce, vpiDelayTerm, vpiContAssign, vpiPrimTerm},
                 {vpiAssignStmt, vpiAssignment});
  expectContains(vpiReg, vpiDriver, Mode::Iterate,
                 {vpiPort, vpiForce, vpiContAssign, vpiAssignStmt},
                 {vpiPrimTerm, vpiDelayTerm, vpiAssignment});
}

// 37.29-37.36.  Constraints use expression nodes, never constraint-item
// declarations, and only UDP instances have a UDP definition transition.
TEST(VPIObjectModel, ClassesConstraintsAndPrimitivesUseExactNodeKinds) {
  expectContains(vpiConstrIfElse, vpiConstraintExpr, Mode::Iterate,
                 {vpiConstant, vpiOperation, vpiRefObj},
                 {vpiConstraintOrdering, vpiConstraint, vpiNamedEvent});
  expectAbsent(vpiGate, vpiUdpDefn, Mode::Handle);
  expectAbsent(vpiSwitch, vpiUdpDefn, Mode::Handle);
  expectTargetsExactly(vpiUdp, vpiUdpDefn, Mode::Handle, {vpiUdpDefn});
  expectTargetsExactly(vpiGateArray, vpiPrimitive, Mode::Iterate, {vpiGate});
  expectTargetsExactly(vpiSwitchArray, vpiPrimitive, Mode::Iterate,
                       {vpiSwitch});
  expectTargetsExactly(vpiUdpArray, vpiPrimitive, Mode::Iterate, {vpiUdp});
}

// 37.48: restrict has no action statement.  Assert alone has a fail action;
// the disable condition belongs to the referenced property object rather than
// directly to each concurrent assertion statement.
TEST(VPIObjectModel, ConcurrentAssertionEdgesMatchEachStatementKind) {
  expectAbsent(vpiRestrict, vpiStmt, Mode::Handle);
  expectAbsent(vpiCover, vpiElseStmt, Mode::Handle);
  expectAbsent(vpiAssume, vpiElseStmt, Mode::Handle);
  expectAbsent(vpiRestrict, vpiElseStmt, Mode::Handle);
  for (uint32_t source : {vpiAssert, vpiAssume, vpiCover})
    EXPECT_NE(findVPITraversal(source, vpiStmt, Mode::Handle), nullptr);
  EXPECT_NE(findVPITraversal(vpiAssert, vpiElseStmt, Mode::Handle), nullptr);
  for (uint32_t source : {vpiAssert, vpiAssume, vpiCover, vpiRestrict})
    expectAbsent(source, vpiDisableCondition, Mode::Handle);
}

// 37.40-37.46.  Calls, runtime frames, continuous assignments, and clocking
// declarations are separate diagrams even though they share many expression
// targets.  These checks keep their object-specific reverse edges intact.
TEST(VPIObjectModel, CallsFramesAssignmentsAndClockingKeepSpecificEdges) {
  expectTargetsExactly(vpiTaskCall, vpiTask, Mode::Handle, {vpiTask});
  expectTargetsExactly(vpiFuncCall, vpiFunction, Mode::Handle, {vpiFunction});
  expectContains(vpiFrame, vpiParent, Mode::Handle,
                 {vpiModule, vpiTask, vpiFunction, vpiTaskCall, vpiFuncCall,
                  vpiFrame, vpiMethodTaskCall, vpiMethodFuncCall},
                 {vpiThread});
  expectTargetsExactly(vpiThread, vpiParent, Mode::Handle, {vpiThread});
  expectTargetsExactly(vpiThread, vpiFrame, Mode::Handle, {vpiFrame});
  expectContains(vpiContAssign, vpiLhs, Mode::Handle,
                 {vpiConstant, vpiOperation, vpiRefObj},
                 {vpiAssignStmt, vpiPropertyExpr, vpiNamedEvent});
  expectTargetsExactly(vpiClockingBlock, vpiPrefix, Mode::Handle,
                       {vpiVirtualInterfaceVar});
  expectTargetsExactly(vpiClockingBlock, vpiActual, Mode::Handle,
                       {vpiClockingBlock});
}

// 37.40-37.41.  Task/function arguments use a broader explicit union than an
// ordinary expression, but "primitive" means concrete gate/switch/UDP
// objects, never terminals or primitive arrays.  Automatic frame state also
// includes virtual-interface variables.
TEST(VPIObjectModel, CallArgumentsAndFrameAutomaticsUseExactUnions) {
  for (uint32_t source : {vpiTaskCall, vpiFuncCall, vpiMethodTaskCall,
                          vpiMethodFuncCall, vpiSysTaskCall, vpiSysFuncCall}) {
    SCOPED_TRACE(objectName(source));
    expectContains(source, vpiArgument, Mode::Iterate,
                   {vpiNet, vpiReg, vpiConstant, vpiOperation, vpiInterface,
                    vpiModport, vpiModule, vpiTask, vpiClassObj, vpiGate,
                    vpiSwitch, vpiUdp, vpiNamedEvent, vpiNamedEventArray},
                   {vpiPrimTerm, vpiGateArray, vpiSwitchArray, vpiUdpArray,
                    vpiPrimitiveArray, vpiPropertyExpr, vpiPropertyInst,
                    vpiSequenceInst, vpiConstraint});
  }
  expectContains(vpiFrame, vpiAutomatics, Mode::Iterate,
                 {vpiVirtualInterfaceVar, vpiClassVar, vpiStringVar,
                  vpiNamedEvent, vpiNamedEventArray},
                 {vpiParameter, vpiSpecParam});
}

// 37.26, 37.32, 37.36, 37.38, 37.40, and 37.46 each augment an ordinary
// expression with one diagram-specific node.  None admits arbitrary members
// of ExpressionFamily.
TEST(VPIObjectModel, SpecialExpressionRelationsAddOnlyTheirNamedNodes) {
  expectContains(vpiParamAssign, vpiRhs, Mode::Handle,
                 {vpiNet, vpiReg, vpiConstant, vpiOperation, vpiIntTypespec},
                 {vpiAnyPattern, vpiPropertyExpr, vpiSequenceInst,
                  vpiConstraint, vpiNamedEvent});
  expectContains(vpiDistItem, vpiValueRange, Mode::Handle,
                 {vpiRange, vpiNet, vpiReg, vpiConstant, vpiOperation},
                 {vpiAnyPattern, vpiPropertyExpr, vpiSequenceInst,
                  vpiConstraint, vpiDistribution});
  expectContains(
      vpiTchk, vpiExpr, Mode::Iterate,
      {vpiTchkTerm, vpiNet, vpiReg, vpiConstant, vpiOperation},
      {vpiAnyPattern, vpiPropertyExpr, vpiSequenceInst, vpiConstraint});
  expectContains(
      vpiMethodFuncCall, vpiWith, Mode::Handle,
      {vpiConstraint, vpiNet, vpiReg, vpiConstant, vpiOperation},
      {vpiAnyPattern, vpiPropertyExpr, vpiSequenceInst, vpiConstraintOrdering});
  expectOrdinaryExpressionTargets(vpiClockingIODecl, vpiExpr, Mode::Handle);

  expectContains(vpiConstrForEach, vpiLoopVars, Mode::Iterate,
                 {vpiIntegerVar, vpiReg, vpiLongIntVar, vpiShortIntVar,
                  vpiIntVar, vpiByteVar, vpiEnumVar, vpiBitVar, vpiStringVar,
                  vpiClassVar, vpiOperation},
                 {vpiRealVar, vpiShortRealVar, vpiNamedEvent});
}

TEST(VPIObjectModel, ClassClassObjectAndClockingIterationsDoNotInventOrder) {
  for (uint32_t selector : {vpiDerivedClasses, vpiInstance, vpiMethods})
    expectNoOrder(vpiClassDefn, selector, Mode::Iterate);
  EXPECT_EQ(requireTraversal(vpiClassDefn, vpiConstraint, Mode::Iterate).order,
            Order::Declaration);
  for (uint32_t selector : {vpiMethods, vpiConstraint})
    expectNoOrder(vpiClassTypespec, selector, Mode::Iterate);
  for (uint32_t selector : {vpiVariables, vpiWaitingProcesses, vpiMessages,
                            vpiMethods, vpiConstraint, vpiParameter})
    expectNoOrder(vpiClassObj, selector, Mode::Iterate);
  expectNoOrder(vpiClockingBlock, vpiClockingIODecl, Mode::Iterate);
}

// 37.49-37.50: property arguments and formals admit named events, but not
// named-event arrays; disable conditions are ordinary expressions (plus a
// distribution on property spec), not sequence-instance nodes.
TEST(VPIObjectModel, PropertyRelationsUsePropertySpecificExpressionUnions) {
  expectContains(
      vpiPropertyInst, vpiArgument, Mode::Iterate,
      {vpiNamedEvent, vpiPropertyExpr, vpiSequenceInst, vpiDistribution},
      {vpiNamedEventArray, vpiVirtualInterfaceVar});
  expectContains(
      vpiPropFormalDecl, vpiExpr, Mode::Handle,
      {vpiNamedEvent, vpiPropertyExpr, vpiSequenceInst, vpiDistribution},
      {vpiNamedEventArray, vpiVirtualInterfaceVar});
  expectContains(vpiPropertyInst, vpiDisableCondition, Mode::Handle,
                 {vpiConstant, vpiOperation},
                 {vpiSequenceInst, vpiDistribution, vpiNamedEvent});
  expectContains(vpiPropertySpec, vpiDisableCondition, Mode::Handle,
                 {vpiConstant, vpiOperation, vpiDistribution},
                 {vpiSequenceInst, vpiNamedEvent});
}

// 37.51-37.53: a sequence actual/formal may be a named event; a match item
// may hang off any complete sequence expression, including an ordinary
// expression, but never off a property expression.
TEST(VPIObjectModel, SequenceRelationsUseCompleteSequenceExpressionUnion) {
  expectContains(vpiSequenceInst, vpiArgument, Mode::Iterate,
                 {vpiNamedEvent, vpiConstant, vpiOperation, vpiSequenceInst,
                  vpiDistribution},
                 {vpiNamedEventArray, vpiVirtualInterfaceVar, vpiPropertyExpr});
  expectContains(vpiSeqFormalDecl, vpiExpr, Mode::Handle,
                 {vpiNamedEvent, vpiConstant, vpiOperation, vpiSequenceInst,
                  vpiDistribution},
                 {vpiNamedEventArray, vpiVirtualInterfaceVar, vpiPropertyExpr});
  for (uint32_t source :
       {vpiConstant, vpiOperation, vpiRefObj, vpiSequenceInst, vpiDistribution})
    EXPECT_NE(findVPITraversal(source, vpiMatchItem, Mode::Iterate), nullptr)
        << objectName(source);
  expectAbsent(vpiPropertyExpr, vpiMatchItem, Mode::Iterate);
}

// 37.54-37.57 and 37.60.  Immediate assertions are statements, while event
// statements point to a named-event object.  Expression operands may include
// interface expressions and assertion expressions, but a use traversal is
// only available on simple expressions.
TEST(VPIObjectModel, ImmediateAssertionsAndExpressionNodesStayDistinct) {
  for (uint32_t source :
       {vpiImmediateAssert, vpiImmediateAssume, vpiImmediateCover}) {
    EXPECT_NE(findVPITraversal(source, vpiExpr, Mode::Handle), nullptr);
    EXPECT_NE(findVPITraversal(source, vpiStmt, Mode::Handle), nullptr);
  }
  expectAbsent(vpiImmediateCover, vpiElseStmt, Mode::Handle);
  expectTargetsExactly(vpiEventStmt, vpiNamedEvent, Mode::Handle,
                       {vpiNamedEvent});
  expectContains(vpiOperation, vpiOperand, Mode::Iterate,
                 {vpiConstant, vpiOperation, vpiInterface, vpiModport,
                  vpiSequenceInst, vpiPropertyInst},
                 {vpiNamedEvent, vpiNamedEventArray});
  EXPECT_NE(findVPITraversal(vpiRefObj, vpiUse, Mode::Iterate), nullptr);
  expectAbsent(vpiOperation, vpiUse, Mode::Iterate);
}

// 37.40 and 37.59.  Method receivers admit ordinary expression objects, while
// dynamic prefixing adds class variables, virtual interfaces, and clocking
// blocks.  Named events are source objects in 37.59, not receiver targets.
TEST(VPIObjectModel, ExpressionPrefixAndTypespecExceptionsAreExact) {
  expectContains(vpiMethodFuncCall, vpiPrefix, Mode::Handle,
                 {vpiClockingBlock, vpiClassVar, vpiVirtualInterfaceVar,
                  vpiRefObj, vpiConstant, vpiOperation},
                 {vpiInterface, vpiModport, vpiNamedEvent, vpiNamedEventArray});
  for (uint32_t source : {vpiRefObj, vpiVarSelect, vpiBitSelect, vpiPartSelect,
                          vpiIndexedPartSelect})
    EXPECT_NE(findVPITraversal(source, vpiTypespec, Mode::Handle), nullptr)
        << objectName(source);
}

// 37.61-37.77 statement diagrams.  These assertions cover distinctions that
// broad Statement/Expression family sets tend to erase.
TEST(VPIObjectModel, StatementRelationsKeepHandleAndIterationSemantics) {
  expectAbsent(vpiOrderedWait, vpiCondition, Mode::Handle);
  EXPECT_NE(findVPITraversal(vpiOrderedWait, vpiCondition, Mode::Iterate),
            nullptr);
  EXPECT_NE(findVPITraversal(vpiFor, vpiForInitStmt, Mode::Handle), nullptr);
  EXPECT_NE(findVPITraversal(vpiFor, vpiForInitStmt, Mode::Iterate), nullptr);
  EXPECT_NE(findVPITraversal(vpiFor, vpiForIncStmt, Mode::Handle), nullptr);
  EXPECT_NE(findVPITraversal(vpiFor, vpiForIncStmt, Mode::Iterate), nullptr);
  expectTargetsExactly(vpiForeachStmt, vpiVariables, Mode::Iterate,
                       {vpiRegArray, vpiPackedArrayVar, vpiStringVar});
  expectContains(vpiForeachStmt, vpiLoopVars, Mode::Iterate,
                 {vpiIntegerVar, vpiReg, vpiLongIntVar, vpiShortIntVar,
                  vpiIntVar, vpiByteVar, vpiEnumVar, vpiBitVar, vpiStringVar,
                  vpiClassVar, vpiOperation},
                 {vpiRealVar, vpiShortRealVar, vpiNamedEvent});
  expectTargetsExactly(vpiAliasStmt, vpiInstance, Mode::Handle,
                       {vpiModule, vpiInterface, vpiProgram, vpiPackage});
}

// 37.78-37.83: callbacks and attributes are associated only through the
// diagram's source objects.  They must not become generic transitions from a
// relation-only selector or the NULL root beyond the explicitly listed root
// callback edge.
TEST(VPIObjectModel, RuntimeAndMetadataRelationsDoNotBecomeUniversal) {
  EXPECT_NE(findVPITraversal(0, vpiCallback, Mode::Iterate), nullptr);
  expectAbsent(vpiReturn, vpiCallback, Mode::Iterate);
  expectAbsent(vpiTypespec, vpiCallback, Mode::Iterate);
  expectAbsent(vpiReturn, vpiAttribute, Mode::Iterate);
  expectAbsent(0, vpiAttribute, Mode::Iterate);
  expectAbsent(vpiIterator, vpiUse, Mode::Iterate);
  EXPECT_NE(findVPITraversal(vpiIterator, vpiUse, Mode::Handle), nullptr);
}

} // namespace
