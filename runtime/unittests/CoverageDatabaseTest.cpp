//===- CoverageDatabaseTest.cpp - Coverage codec tests -------------------===//

#include "obelisk/Coverage/CoverageDatabase.h"

#include "gtest/gtest.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <tuple>
#include <vector>

namespace {

using namespace obelisk::coverage;

uint64_t realBits(double value) {
  uint64_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

uint32_t read32(const std::vector<uint8_t> &bytes, size_t offset) {
  uint32_t value = 0;
  for (unsigned index = 0; index != 4; ++index)
    value |= uint32_t(bytes[offset + index]) << (index * 8);
  return value;
}

uint64_t read64(const std::vector<uint8_t> &bytes, size_t offset) {
  uint64_t value = 0;
  for (unsigned index = 0; index != 8; ++index)
    value |= uint64_t(bytes[offset + index]) << (index * 8);
  return value;
}

void put32(std::vector<uint8_t> &bytes, size_t offset, uint32_t value) {
  for (unsigned index = 0; index != 4; ++index)
    bytes[offset + index] = uint8_t(value >> (index * 8));
}

size_t sectionOffset(const std::vector<uint8_t> &bytes, SectionKind kind) {
  uint64_t directory = read64(bytes, 24);
  uint32_t count = read32(bytes, 32);
  for (uint32_t index = 0; index != count; ++index) {
    size_t record = static_cast<size_t>(directory + index * 32);
    if (read32(bytes, record) == static_cast<uint32_t>(kind))
      return static_cast<size_t>(read64(bytes, record + 8));
  }
  return std::string::npos;
}

void refreshChecksum(std::vector<uint8_t> &bytes) {
  std::fill(bytes.begin() + 72, bytes.begin() + 104, 0);
  Digest digest = sha256(bytes.data(), bytes.size());
  std::copy(digest.begin(), digest.end(), bytes.begin() + 72);
}

template <typename Range>
void setConfiguration(Range &range, const Digest &configuration) {
  for (auto &record : range)
    record.configuration = configuration;
}

FunctionalValueSet integralSet(uint64_t id, uint64_t item, uint32_t firstAtom,
                               uint32_t width) {
  FunctionalValueSet result;
  result.id = id;
  result.item = item;
  result.firstAtom = firstAtom;
  result.atomCount = 1;
  result.bitWidth = width;
  result.signedness = CoverageSignedness::Signed;
  return result;
}

FunctionalValueAtom integralAtom(uint64_t set, uint32_t firstLimb,
                                 uint32_t limbCount) {
  FunctionalValueAtom result;
  result.valueSet = set;
  result.firstLimb = firstLimb;
  result.limbCount = limbCount;
  result.flags =
      FunctionalValueAtomLowerInclusive | FunctionalValueAtomUpperInclusive;
  return result;
}

FunctionalValueLimb integralLimb(uint64_t set, uint32_t atom, uint32_t ordinal,
                                 uint64_t aval, uint64_t bval = 0,
                                 uint64_t wildcard = 0) {
  FunctionalValueLimb result;
  result.valueSet = set;
  result.atomOrdinal = atom;
  result.ordinal = ordinal;
  result.lowAval = result.highAval = aval;
  result.lowBval = result.highBval = bval;
  result.wildcardMask = wildcard;
  return result;
}

ResolvedFunctionalValueSet
resolvedSet(uint64_t id, uint64_t templateSet, uint64_t item,
            uint32_t firstAtom, uint32_t width, uint64_t ownerBin,
            uint64_t ownerSelector, uint32_t ownerOrdinal,
            uint32_t ownerSubordinal, ResolvedFunctionalValueSetRole role) {
  ResolvedFunctionalValueSet result;
  result.type = 10;
  result.id = id;
  result.templateValueSet = templateSet;
  result.item = item;
  result.firstAtom = firstAtom;
  result.atomCount = 1;
  result.bitWidth = width;
  result.signedness = CoverageSignedness::Signed;
  result.ownerBin = ownerBin;
  result.ownerSelector = ownerSelector;
  result.ownerOrdinal = ownerOrdinal;
  result.ownerSubordinal = ownerSubordinal;
  result.role = role;
  return result;
}

ResolvedFunctionalValueAtom resolvedAtom(uint64_t set, uint32_t firstLimb,
                                         uint32_t limbCount) {
  ResolvedFunctionalValueAtom result;
  result.valueSet = set;
  result.firstLimb = firstLimb;
  result.limbCount = limbCount;
  result.flags =
      FunctionalValueAtomLowerInclusive | FunctionalValueAtomUpperInclusive;
  return result;
}

ResolvedFunctionalValueLimb resolvedLimb(uint64_t set, uint32_t ordinal,
                                         uint64_t aval, uint64_t bval = 0,
                                         uint64_t wildcard = 0) {
  ResolvedFunctionalValueLimb result;
  result.valueSet = set;
  result.ordinal = ordinal;
  result.lowAval = result.highAval = aval;
  result.lowBval = result.highBval = bval;
  result.wildcardMask = wildcard;
  return result;
}

Database makeTypedDatabase() {
  Database db;
  db.producer = "coverage-database-test";
  db.scopes.push_back({1, 0, "top", 0, "top"});
  db.functionalTypes.push_back({10, 1, "cg", 0, 2023, "top.cg"});
  db.functionalItems.push_back({20, 10, "state_a",
                                FunctionalItemKind::Coverpoint, 0, 100, 1, 0,
                                "top.cg.state_a"});
  db.functionalItems.push_back({21, 10, "transition",
                                FunctionalItemKind::Coverpoint, 0, 100, 1, 1,
                                "top.cg.transition"});
  db.functionalItems.push_back({22, 10, "state_b",
                                FunctionalItemKind::Coverpoint, 0, 100, 1, 2,
                                "top.cg.state_b"});
  db.functionalItems.push_back({23, 10, "cross", FunctionalItemKind::Cross, 0,
                                100, 1, 3, "top.cg.cross"});
  db.functionalBins.push_back({30, 20, "wide", FunctionalBinKind::State,
                               FunctionalBinWildcard, 1, 0,
                               "top.cg.state_a.wide"});
  db.functionalBins.push_back({31, 21, "sequence",
                               FunctionalBinKind::Transition, 0, 1, 0,
                               "top.cg.transition.sequence"});
  db.functionalBins.push_back({32, 22, "other", FunctionalBinKind::State, 0, 1,
                               0, "top.cg.state_b.other"});
  db.functionalBins.push_back({33, 23, "selected", FunctionalBinKind::Cross, 0,
                               1, 0, "top.cg.cross.selected"});
  db.functionalBins.push_back({34, 20, "illegal", FunctionalBinKind::State,
                               FunctionalBinIllegal, 1, 1,
                               "top.cg.state_a.illegal"});

  db.functionalValueSets.push_back(integralSet(100, 20, 0, 130));
  db.functionalValueSets.push_back(integralSet(101, 21, 1, 8));
  db.functionalValueSets.push_back(integralSet(102, 21, 2, 8));
  db.functionalValueSets.push_back(integralSet(103, 22, 3, 8));
  db.functionalValueSets.push_back(integralSet(104, 20, 4, 130));
  db.functionalValueSets.push_back(integralSet(105, 20, 5, 8));
  db.functionalValueAtoms.push_back(integralAtom(100, 0, 3));
  db.functionalValueAtoms.push_back(integralAtom(101, 3, 1));
  db.functionalValueAtoms.push_back(integralAtom(102, 4, 1));
  db.functionalValueAtoms.push_back(integralAtom(103, 5, 1));
  db.functionalValueAtoms.push_back(integralAtom(104, 6, 3));
  db.functionalValueAtoms.push_back(integralAtom(105, 9, 1));
  db.functionalValueLimbs.push_back(integralLimb(100, 0, 0, 0x42, 0x10, 0x10));
  db.functionalValueLimbs.push_back(
      integralLimb(100, 0, 1, 0x1234, 0x100, 0x100));
  db.functionalValueLimbs.push_back(integralLimb(100, 0, 2, 0x2));
  db.functionalValueLimbs.push_back(integralLimb(101, 0, 0, 1));
  db.functionalValueLimbs.push_back(integralLimb(102, 0, 0, 2));
  db.functionalValueLimbs.push_back(integralLimb(103, 0, 0, 3));
  db.functionalValueLimbs.push_back(integralLimb(104, 0, 0, 0x42));
  db.functionalValueLimbs.push_back(integralLimb(104, 0, 1, 0x1234));
  db.functionalValueLimbs.push_back(integralLimb(104, 0, 2, 0x2));
  db.functionalValueLimbs.push_back(integralLimb(105, 0, 0, 4));

  db.functionalFormals.push_back(
      {60, 10, "constructor_value", FunctionalFormalKind::Constructor,
       FunctionalFormalDirection::Input,
       FunctionalExpressionResultKind::Integral, FunctionalFormalHasDefault, 8,
       CoverageSignedness::Signed, 0, 899});
  db.functionalFormals.push_back(
      {61, 10, "sample_value", FunctionalFormalKind::Sample,
       FunctionalFormalDirection::Ref, FunctionalExpressionResultKind::Real,
       FunctionalFormalHasDefault, 0, CoverageSignedness::NotApplicable, 0,
       898});

  FunctionalExpression sampleDefaultExpression;
  sampleDefaultExpression.id = 898;
  sampleDefaultExpression.owner = 61;
  sampleDefaultExpression.ownerKind = FunctionalExpressionOwnerKind::Formal;
  sampleDefaultExpression.role = FunctionalExpressionRole::FormalDefault;
  sampleDefaultExpression.resultKind = FunctionalExpressionResultKind::Real;
  sampleDefaultExpression.signedness = CoverageSignedness::NotApplicable;
  sampleDefaultExpression.evaluationPhase =
      FunctionalExpressionEvaluationPhase::Sample;
  db.functionalExpressions.push_back(sampleDefaultExpression);

  FunctionalExpression defaultExpression;
  defaultExpression.id = 899;
  defaultExpression.owner = 60;
  defaultExpression.ownerKind = FunctionalExpressionOwnerKind::Formal;
  defaultExpression.role = FunctionalExpressionRole::FormalDefault;
  defaultExpression.resultKind = FunctionalExpressionResultKind::Integral;
  defaultExpression.bitWidth = 8;
  defaultExpression.signedness = CoverageSignedness::Signed;
  defaultExpression.evaluationPhase =
      FunctionalExpressionEvaluationPhase::Constructor;
  db.functionalExpressions.push_back(defaultExpression);

  FunctionalExpression nameExpression;
  nameExpression.id = 900;
  nameExpression.owner = 10;
  nameExpression.ownerKind = FunctionalExpressionOwnerKind::Type;
  nameExpression.role = FunctionalExpressionRole::OptionRHS;
  nameExpression.resultKind = FunctionalExpressionResultKind::String;
  nameExpression.signedness = CoverageSignedness::NotApplicable;
  nameExpression.ownerSubordinal =
      static_cast<uint32_t>(FunctionalOptionScopeKind::Type);
  nameExpression.evaluationPhase = FunctionalExpressionEvaluationPhase::Option;
  db.functionalExpressions.push_back(nameExpression);
  FunctionalExpression firstCrossSample;
  firstCrossSample.id = 9000001;
  firstCrossSample.owner = 20;
  firstCrossSample.ownerKind = FunctionalExpressionOwnerKind::Item;
  firstCrossSample.role = FunctionalExpressionRole::CoverpointSample;
  firstCrossSample.resultKind = FunctionalExpressionResultKind::Integral;
  firstCrossSample.bitWidth = 130;
  firstCrossSample.signedness = CoverageSignedness::Signed;
  firstCrossSample.evaluationPhase =
      FunctionalExpressionEvaluationPhase::Sample;
  firstCrossSample.resultOrdinal = 1;
  db.functionalExpressions.push_back(firstCrossSample);
  FunctionalExpression secondCrossSample = firstCrossSample;
  secondCrossSample.id = 9000002;
  secondCrossSample.owner = 22;
  secondCrossSample.bitWidth = 8;
  secondCrossSample.resultOrdinal = 2;
  db.functionalExpressions.push_back(secondCrossSample);
  db.functionalOptionPlans.push_back(
      {10, 900, FunctionalConfigurationOptionOwnerKind::Group,
       FunctionalOptionScopeKind::Type,
       FunctionalConfigurationOptionKind::Comment, 0, 0});

  db.functionalBinPlans.push_back({30, 100});
  db.functionalBinPlans.push_back({31, 0});
  db.functionalBinPlans.push_back({32, 103});
  db.functionalBinPlans.push_back({34, 105});
  db.transitionPrograms.push_back({31, 21, 0, 1, 0});
  db.transitionAlternatives.push_back({31, 102, 0, 2, 0, 0});
  db.transitionSteps.push_back(
      {31, 101, 0, 0, 1, 1, 0, 0, TransitionRepetitionKind::Once, 0});
  db.transitionSteps.push_back(
      {31, 102, 0, 0, 1, 1, 0, 1, TransitionRepetitionKind::Once, 0});

  db.crossPlans.push_back({23, 0, 2, 0, 1, CrossRetainAutoPolicy::Retain, 0, 0,
                           2300, 138, CrossTupleFourState});
  db.crossTargets.push_back(
      {23, 20, 0, 0, 130, FunctionalExpressionResultKind::Integral,
       CoverageSignedness::Signed, CrossTupleFieldFourState});
  db.crossTargets.push_back({23, 22, 1, 130, 8,
                             FunctionalExpressionResultKind::Integral,
                             CoverageSignedness::Signed, 0});
  db.crossBins.push_back({33, 23, 504, 0});
  db.crossSelectorNodes.push_back({500, 23, 0, 0, 0, 0, 0, 700, 0, 0, 0,
                                   CrossSelectorKind::Set, 0, 0,
                                   CrossMatchesPolicy::Count, 1});
  db.crossSelectorNodes.push_back({501, 23, 0, 0, 0, 0, 0, 701, 0, 0, 0,
                                   CrossSelectorKind::Set, 1, 0,
                                   CrossMatchesPolicy::Count, 1});
  db.crossSelectorNodes.push_back({502, 23, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                   CrossSelectorKind::AllTuples, 2, 0,
                                   CrossMatchesPolicy::None, 0});
  db.crossSelectorNodes.push_back({503, 23, 0, 0, 0, 0, 0, 0, 0, 0, 2,
                                   CrossSelectorKind::Or, 3, 0,
                                   CrossMatchesPolicy::None, 0});
  db.crossSelectorNodes.push_back({504, 23, 0, 0, 0, 0, 0, 0, 0, 2, 2,
                                   CrossSelectorKind::Or, 4, 0,
                                   CrossMatchesPolicy::None, 0});
  db.crossSelectorOperands.push_back({503, 500, 0});
  db.crossSelectorOperands.push_back({503, 501, 1});
  db.crossSelectorOperands.push_back({504, 502, 0});
  db.crossSelectorOperands.push_back({504, 503, 1});
  db.functionalTupleSets.push_back(
      {700, 23, 500, 0, 1, FunctionalTupleElementMode::BinTuple, 0});
  db.functionalTupleSets.push_back(
      {701, 23, 501, 1, 1, FunctionalTupleElementMode::ValueTuple, 0});
  db.functionalTupleSetTuples.push_back({700, 710, 0, 2, 0, 0});
  db.functionalTupleSetTuples.push_back({701, 711, 2, 2, 0, 0});
  db.functionalTupleSetComponents.push_back({710, 20, 30, 0, 0, 0});
  db.functionalTupleSetComponents.push_back({710, 22, 32, 0, 1, 0});
  db.functionalTupleSetComponents.push_back({711, 20, 0, 104, 0, 0});
  db.functionalTupleSetComponents.push_back({711, 22, 0, 103, 1, 0});

  db.functionalConfigurations.push_back({10, {}, 0});
  db.functionalConfigurationOptions.push_back(
      {10,
       {},
       10,
       FunctionalConfigurationOptionOwnerKind::Group,
       FunctionalOptionScopeKind::Type,
       FunctionalConfigurationOptionKind::Comment,
       FunctionalConfigurationValueKind::String,
       0,
       0,
       "instance"});
  db.resolvedFunctionalItems.push_back({10,
                                        {},
                                        200,
                                        20,
                                        "state_a",
                                        FunctionalItemKind::Coverpoint,
                                        0,
                                        100,
                                        1,
                                        0,
                                        "top.cg.state_a"});
  db.resolvedFunctionalItems.push_back({10,
                                        {},
                                        201,
                                        21,
                                        "transition",
                                        FunctionalItemKind::Coverpoint,
                                        0,
                                        100,
                                        1,
                                        1,
                                        "top.cg.transition"});
  db.resolvedFunctionalItems.push_back({10,
                                        {},
                                        202,
                                        22,
                                        "state_b",
                                        FunctionalItemKind::Coverpoint,
                                        0,
                                        100,
                                        1,
                                        2,
                                        "top.cg.state_b"});
  db.resolvedFunctionalItems.push_back({10,
                                        {},
                                        203,
                                        23,
                                        "cross",
                                        FunctionalItemKind::Cross,
                                        0,
                                        100,
                                        1,
                                        3,
                                        "top.cg.cross"});
  db.resolvedFunctionalBins.push_back({10,
                                       {},
                                       300,
                                       30,
                                       200,
                                       "wide",
                                       FunctionalBinKind::State,
                                       FunctionalBinWildcard,
                                       0,
                                       0,
                                       1,
                                       "top.cg.state_a.wide"});
  db.resolvedFunctionalBins.push_back({10,
                                       {},
                                       304,
                                       34,
                                       200,
                                       "illegal",
                                       FunctionalBinKind::State,
                                       FunctionalBinIllegal,
                                       1,
                                       0,
                                       1,
                                       "top.cg.state_a.illegal"});
  db.resolvedFunctionalBins.push_back({10,
                                       {},
                                       301,
                                       31,
                                       201,
                                       "sequence",
                                       FunctionalBinKind::Transition,
                                       0,
                                       0,
                                       0,
                                       1,
                                       "top.cg.transition.sequence"});
  db.resolvedFunctionalBins.push_back({10,
                                       {},
                                       302,
                                       32,
                                       202,
                                       "other",
                                       FunctionalBinKind::State,
                                       0,
                                       0,
                                       0,
                                       1,
                                       "top.cg.state_b.other"});
  db.resolvedFunctionalBins.push_back({10,
                                       {},
                                       303,
                                       33,
                                       203,
                                       "selected",
                                       FunctionalBinKind::Cross,
                                       0,
                                       0,
                                       0,
                                       1,
                                       "top.cg.cross.selected"});

  db.resolvedFunctionalValueSets.push_back(
      resolvedSet(400, 100, 200, 0, 130, 300, 0, 0, 0,
                  ResolvedFunctionalValueSetRole::StateBin));
  db.resolvedFunctionalValueSets.push_back(
      resolvedSet(401, 101, 201, 1, 8, 301, 0, 0, 0,
                  ResolvedFunctionalValueSetRole::TransitionStep));
  db.resolvedFunctionalValueSets.push_back(
      resolvedSet(402, 102, 201, 2, 8, 301, 0, 0, 1,
                  ResolvedFunctionalValueSetRole::TransitionStep));
  db.resolvedFunctionalValueSets.push_back(
      resolvedSet(403, 103, 202, 3, 8, 302, 0, 0, 0,
                  ResolvedFunctionalValueSetRole::StateBin));
  db.resolvedFunctionalValueSets.push_back(
      resolvedSet(405, 103, 202, 4, 8, 0, 501, 0, 1,
                  ResolvedFunctionalValueSetRole::TupleComponent));
  db.resolvedFunctionalValueSets.push_back(
      resolvedSet(404, 104, 200, 5, 130, 0, 501, 0, 0,
                  ResolvedFunctionalValueSetRole::TupleComponent));
  db.resolvedFunctionalValueSets.push_back(
      resolvedSet(406, 105, 200, 6, 8, 304, 0, 0, 0,
                  ResolvedFunctionalValueSetRole::StateBin));
  db.resolvedFunctionalValueAtoms.push_back(resolvedAtom(400, 0, 3));
  db.resolvedFunctionalValueAtoms.push_back(resolvedAtom(401, 3, 1));
  db.resolvedFunctionalValueAtoms.push_back(resolvedAtom(402, 4, 1));
  db.resolvedFunctionalValueAtoms.push_back(resolvedAtom(403, 5, 1));
  db.resolvedFunctionalValueAtoms.push_back(resolvedAtom(405, 6, 1));
  db.resolvedFunctionalValueAtoms.push_back(resolvedAtom(404, 7, 3));
  db.resolvedFunctionalValueAtoms.push_back(resolvedAtom(406, 10, 1));
  db.resolvedFunctionalValueLimbs.push_back(
      resolvedLimb(400, 0, 0x42, 0x10, 0x10));
  db.resolvedFunctionalValueLimbs.push_back(
      resolvedLimb(400, 1, 0x1234, 0x100, 0x100));
  db.resolvedFunctionalValueLimbs.push_back(resolvedLimb(400, 2, 0x2));
  db.resolvedFunctionalValueLimbs.push_back(resolvedLimb(401, 0, 1));
  db.resolvedFunctionalValueLimbs.push_back(resolvedLimb(402, 0, 2));
  db.resolvedFunctionalValueLimbs.push_back(resolvedLimb(403, 0, 3));
  db.resolvedFunctionalValueLimbs.push_back(resolvedLimb(405, 0, 3));
  db.resolvedFunctionalValueLimbs.push_back(resolvedLimb(404, 0, 0x42));
  db.resolvedFunctionalValueLimbs.push_back(resolvedLimb(404, 1, 0x1234));
  db.resolvedFunctionalValueLimbs.push_back(resolvedLimb(404, 2, 0x2));
  db.resolvedFunctionalValueLimbs.push_back(resolvedLimb(406, 0, 4));

  db.resolvedFunctionalBinPlans.push_back({10, {}, 300, 400});
  db.resolvedFunctionalBinPlans.push_back({10, {}, 301, 0});
  db.resolvedFunctionalBinPlans.push_back({10, {}, 302, 403});
  db.resolvedFunctionalBinPlans.push_back({10, {}, 304, 406});
  db.resolvedTransitionAlternatives.push_back(
      {10, {}, 600, 301, 0, 0, 0, 2, 0, 0});
  db.resolvedTransitionExpansionGroups.push_back(
      {10, {}, 201, 31, 0, 0, 1, 0, 1});
  db.resolvedTransitionSteps.push_back({10, {}, 301, 600, 401, 1, 1, 0, 0});
  db.resolvedTransitionSteps.push_back({10, {}, 301, 600, 402, 1, 1, 1, 0});
  db.resolvedCrossPlans.push_back(
      {10, {}, 203, CrossRetainAutoPolicy::Retain, 0, 0, 1, 900});
  db.resolvedCrossAutomaticBinCountLimbs.push_back(1);
  db.resolvedCrossAutomaticNodes.push_back({10, {}, 203, 900, 0, 0, 1, 0});
  db.resolvedCrossAutomaticNodes.push_back({10, {}, 203, 901, 1, 1, 1, 0});
  db.resolvedCrossAutomaticEdges.push_back({900, 300, 901, 0, 0});
  db.resolvedCrossAutomaticEdges.push_back({901, 302, 0, 0, 0});
  db.resolvedFunctionalBinGroups.push_back({10,
                                            {},
                                            200,
                                            30,
                                            0,
                                            1,
                                            0,
                                            FunctionalBinArrayMode::Scalar,
                                            FunctionalBinDistributionKind::None,
                                            0,
                                            FunctionalBinKind::State});
  db.resolvedFunctionalBinGroups.push_back({10,
                                            {},
                                            200,
                                            34,
                                            1,
                                            1,
                                            0,
                                            FunctionalBinArrayMode::Scalar,
                                            FunctionalBinDistributionKind::None,
                                            0,
                                            FunctionalBinKind::State});
  db.resolvedFunctionalBinGroups.push_back({10,
                                            {},
                                            201,
                                            31,
                                            2,
                                            1,
                                            0,
                                            FunctionalBinArrayMode::Scalar,
                                            FunctionalBinDistributionKind::None,
                                            0,
                                            FunctionalBinKind::Transition});
  db.resolvedFunctionalBinGroups.push_back({10,
                                            {},
                                            202,
                                            32,
                                            3,
                                            1,
                                            0,
                                            FunctionalBinArrayMode::Scalar,
                                            FunctionalBinDistributionKind::None,
                                            0,
                                            FunctionalBinKind::State});
  db.resolvedFunctionalTupleSets.push_back(
      {10,
       {},
       800,
       700,
       203,
       500,
       0,
       1,
       FunctionalTupleElementMode::BinTuple,
       0});
  db.resolvedFunctionalTupleSets.push_back(
      {10,
       {},
       801,
       701,
       203,
       501,
       1,
       1,
       FunctionalTupleElementMode::ValueTuple,
       0});
  db.resolvedFunctionalTupleSetTuples.push_back({800, 810, 0, 2, 0, 0});
  db.resolvedFunctionalTupleSetTuples.push_back({801, 811, 2, 2, 0, 0});
  db.resolvedFunctionalTupleSetComponents.push_back({810, 200, 300, 0, 0, 0});
  db.resolvedFunctionalTupleSetComponents.push_back({810, 202, 302, 0, 1, 0});
  db.resolvedFunctionalTupleSetComponents.push_back({811, 200, 0, 404, 0, 0});
  db.resolvedFunctionalTupleSetComponents.push_back({811, 202, 0, 405, 1, 0});
  db.resolvedCrossSelectorBindings.push_back(
      {10, {}, 203, 500, 0, 0, 800, CrossMatchesPolicy::Count, 0, 1});
  db.resolvedCrossSelectorBindings.push_back(
      {10, {}, 203, 501, 0, 0, 801, CrossMatchesPolicy::Count, 0, 1});

  Digest configuration =
      computeFunctionalConfigurationFingerprint(db, 10, Digest{});
  db.functionalConfigurations.front().configuration = configuration;
  setConfiguration(db.functionalConfigurationOptions, configuration);
  setConfiguration(db.resolvedFunctionalItems, configuration);
  setConfiguration(db.resolvedFunctionalBins, configuration);
  setConfiguration(db.resolvedFunctionalValueSets, configuration);
  setConfiguration(db.resolvedFunctionalBinPlans, configuration);
  setConfiguration(db.resolvedFunctionalBinGroups, configuration);
  setConfiguration(db.resolvedTransitionAlternatives, configuration);
  setConfiguration(db.resolvedTransitionExpansionGroups, configuration);
  setConfiguration(db.resolvedTransitionSteps, configuration);
  setConfiguration(db.resolvedCrossPlans, configuration);
  setConfiguration(db.resolvedCrossAutomaticNodes, configuration);
  setConfiguration(db.resolvedCrossSelectorBindings, configuration);
  setConfiguration(db.resolvedFunctionalTupleSets, configuration);

  obelisk::coverage::Run run;
  run.uuid[0] = 1;
  run.name = "test";
  run.flags = RunContainsFunctional;
  run.tags.push_back({"seed", "one"});
  db.runs.push_back(run);
  db.resolvedInstances.push_back(
      {run.uuid, 10, 1000, "named", 0, configuration});
  db.counters.push_back({run.uuid, MetricKind::Functional, 300, 1000, 0, 0, 7});
  db.sparseCrossTuples.push_back({run.uuid, 1000, 23, 0, 2, 4, 0});
  db.sparseCrossTupleComponents.push_back(300);
  db.sparseCrossTupleComponents.push_back(302);
  db.illegalBinDiagnostics.push_back(
      {run.uuid, 304, 1000, 42, 1, "diagnostic", 0});
  db.schemaFingerprint = computeSchemaFingerprint(db);
  return db;
}

Database makeSetExpressionDatabase(uint32_t sourceWidth,
                                   CoverageSignedness sourceSignedness,
                                   uint32_t expressionFlags = 0) {
  Database db;
  db.scopes.push_back({1, 0, "top", 0, "top"});
  db.functionalTypes.push_back({10, 1, "cg", 0, 2023, "top.cg"});
  db.functionalItems.push_back({20, 10, "cp", FunctionalItemKind::Coverpoint, 0,
                                100, 1, 0, "top.cg.cp"});
  db.functionalBins.push_back(
      {30, 20, "set", FunctionalBinKind::State, 0, 1, 0, "top.cg.cp.set"});
  FunctionalValueSet set;
  set.id = 100;
  set.item = 20;
  set.bitWidth = 8;
  set.flags = FunctionalValueSetNeedsResolution;
  set.signedness = CoverageSignedness::Unsigned;
  set.setExpression = 9000003;
  db.functionalValueSets.push_back(set);
  db.functionalBinPlans.push_back({30, set.id});

  FunctionalExpression expression;
  expression.id = db.functionalValueSets.front().setExpression;
  expression.owner = db.functionalValueSets.front().id;
  expression.ownerKind = FunctionalExpressionOwnerKind::ValueSet;
  expression.role = FunctionalExpressionRole::BinSet;
  expression.resultKind = FunctionalExpressionResultKind::Set;
  expression.flags = expressionFlags;
  expression.bitWidth = sourceWidth;
  expression.signedness = sourceSignedness;
  expression.evaluationPhase = FunctionalExpressionEvaluationPhase::Constructor;
  expression.resultOrdinal = 0;
  db.functionalExpressions.push_back(expression);
  db.schemaFingerprint = computeSchemaFingerprint(db);
  return db;
}

Database makeSingleStepTransition(TransitionRepetitionKind repetition,
                                  uint64_t lower, uint64_t upper) {
  Database db;
  db.scopes.push_back({1, 0, "top", 0, "top"});
  db.functionalTypes.push_back({10, 1, "cg", 0, 2017, "top.cg"});
  db.functionalItems.push_back({20, 10, "cp", FunctionalItemKind::Coverpoint, 0,
                                100, 1, 0, "top.cg.cp"});
  db.functionalBins.push_back({30, 20, "transition",
                               FunctionalBinKind::Transition, 0, 1, 0,
                               "top.cg.cp.transition"});
  db.functionalValueSets.push_back(integralSet(100, 20, 0, 8));
  db.functionalValueAtoms.push_back(integralAtom(100, 0, 1));
  db.functionalValueLimbs.push_back(integralLimb(100, 0, 0, 1));
  db.functionalBinPlans.push_back({30, 0});
  db.transitionPrograms.push_back({30, 20, 0, 1, 0});
  db.transitionAlternatives.push_back({30, 100, 0, 1, 0, 0});
  db.transitionSteps.push_back(
      {30, 100, 0, 0, lower, upper, 0, 0, repetition, 0});
  return db;
}

Database makeBoundedTransitionArrayDatabase() {
  Database db;
  db.scopes.push_back({1, 0, "top", 0, "top"});
  db.functionalTypes.push_back({10, 1, "cg", 0, 2023, "top.cg"});
  db.functionalItems.push_back({20, 10, "cp", FunctionalItemKind::Coverpoint, 0,
                                100, 1, 0, "top.cg.cp"});
  db.functionalBins.push_back({30, 20, "paths", FunctionalBinKind::Transition,
                               0, 1, 0, "top.cg.cp.paths"});
  db.functionalValueSets.push_back(integralSet(100, 20, 0, 8));
  db.functionalValueSets.push_back(integralSet(101, 20, 1, 8));
  db.functionalValueAtoms.push_back(integralAtom(100, 0, 1));
  db.functionalValueAtoms.push_back(integralAtom(101, 1, 1));
  db.functionalValueLimbs.push_back(integralLimb(100, 0, 0, 1));
  db.functionalValueLimbs.push_back(integralLimb(101, 0, 0, 2));
  FunctionalBinPlan plan;
  plan.bin = 30;
  plan.arrayMode = FunctionalBinArrayMode::Unsized;
  plan.distribution = FunctionalBinDistributionKind::PerValue;
  db.functionalBinPlans.push_back(plan);
  db.transitionPrograms.push_back({30, 20, 0, 1, 0});
  db.transitionAlternatives.push_back({30, 101, 0, 2, 0, 0});
  db.transitionSteps.push_back(
      {30, 100, 0, 0, 1, 1, 0, 0, TransitionRepetitionKind::Once, 0});
  db.transitionSteps.push_back(
      {30, 101, 0, 0, 1, 1, 0, 1, TransitionRepetitionKind::Once, 0});
  return db;
}

Database makeDefaultSequenceDatabase() {
  Database db;
  db.scopes.push_back({1, 0, "top", 0, "top"});
  db.functionalTypes.push_back({10, 1, "cg", 0, 2023, "top.cg"});
  db.functionalItems.push_back({20, 10, "cp", FunctionalItemKind::Coverpoint, 0,
                                100, 1, 0, "top.cg.cp"});
  db.functionalBins.push_back({30, 20, "other", FunctionalBinKind::Transition,
                               FunctionalBinDefaultSequence, 1, 0,
                               "top.cg.cp.other"});
  db.functionalBinPlans.push_back({30, 0});
  db.transitionPrograms.push_back({30, 20, 0, 0, 0});

  db.functionalConfigurations.push_back({10, {}, 0});
  db.resolvedFunctionalItems.push_back({10,
                                        {},
                                        200,
                                        20,
                                        "cp",
                                        FunctionalItemKind::Coverpoint,
                                        0,
                                        100,
                                        1,
                                        0,
                                        "top.cg.cp"});
  db.resolvedFunctionalBins.push_back({10,
                                       {},
                                       300,
                                       30,
                                       200,
                                       "other",
                                       FunctionalBinKind::Transition,
                                       FunctionalBinDefaultSequence,
                                       0,
                                       0,
                                       1,
                                       "top.cg.cp.other"});
  db.resolvedFunctionalBinPlans.push_back({10, {}, 300, 0});
  db.resolvedFunctionalBinGroups.push_back({10,
                                            {},
                                            200,
                                            30,
                                            0,
                                            1,
                                            0,
                                            FunctionalBinArrayMode::Scalar,
                                            FunctionalBinDistributionKind::None,
                                            0,
                                            FunctionalBinKind::Transition});

  Digest configuration =
      computeFunctionalConfigurationFingerprint(db, 10, Digest{});
  db.functionalConfigurations.front().configuration = configuration;
  setConfiguration(db.resolvedFunctionalItems, configuration);
  setConfiguration(db.resolvedFunctionalBins, configuration);
  setConfiguration(db.resolvedFunctionalBinPlans, configuration);
  setConfiguration(db.resolvedFunctionalBinGroups, configuration);
  db.schemaFingerprint = computeSchemaFingerprint(db);
  return db;
}

Database makeRealTemplateDatabase() {
  Database db =
      makeSingleStepTransition(TransitionRepetitionKind::Consecutive, 1, 1);
  db.functionalTypes.front().languageVersion = 2023;
  db.functionalBins.front().kind = FunctionalBinKind::State;
  db.functionalBins.front().name = "interval";
  db.functionalBins.front().hierarchy = "top.cg.cp.interval";
  db.functionalBinPlans.front().valueSet = 100;
  db.transitionPrograms.clear();
  db.transitionAlternatives.clear();
  db.transitionSteps.clear();
  auto &set = db.functionalValueSets.front();
  set.bitWidth = 64;
  set.kind = FunctionalValueSetKind::Real;
  set.signedness = CoverageSignedness::NotApplicable;
  auto &atom = db.functionalValueAtoms.front();
  atom.kind = FunctionalValueAtomKind::RealInterval;
  atom.firstLimb = 0;
  atom.limbCount = 0;
  atom.realLowBits = realBits(1.0);
  atom.realHighBits = realBits(2.0);
  atom.flags |= FunctionalValueAtomRealRange;
  db.functionalValueLimbs.clear();
  return db;
}

Database makeZeroCardinalityDefaultArray() {
  Database db;
  db.scopes.push_back({1, 0, "top", 0, "top"});
  db.functionalTypes.push_back({10, 1, "cg", 0, 2017, "top.cg"});
  db.functionalItems.push_back({20, 10, "cp", FunctionalItemKind::Coverpoint, 0,
                                100, 1, 0, "top.cg.cp"});
  db.functionalBins.push_back({30, 20, "others", FunctionalBinKind::State,
                               FunctionalBinDefault, 1, 0, "top.cg.cp.others"});
  FunctionalBinPlan plan;
  plan.bin = 30;
  plan.arrayMode = FunctionalBinArrayMode::Unsized;
  plan.distribution = FunctionalBinDistributionKind::PerValue;
  db.functionalBinPlans.push_back(plan);
  db.functionalConfigurations.push_back({10, {}, 0});
  db.resolvedFunctionalItems.push_back({10,
                                        {},
                                        200,
                                        20,
                                        "cp",
                                        FunctionalItemKind::Coverpoint,
                                        0,
                                        100,
                                        1,
                                        0,
                                        "top.cg.cp"});
  db.resolvedFunctionalBinGroups.push_back(
      {10,
       {},
       200,
       30,
       0,
       0,
       0,
       FunctionalBinArrayMode::Unsized,
       FunctionalBinDistributionKind::PerValue,
       0,
       FunctionalBinKind::State});
  Digest configuration =
      computeFunctionalConfigurationFingerprint(db, 10, Digest{});
  db.functionalConfigurations.front().configuration = configuration;
  setConfiguration(db.resolvedFunctionalItems, configuration);
  setConfiguration(db.resolvedFunctionalBinGroups, configuration);
  db.schemaFingerprint = computeSchemaFingerprint(db);
  return db;
}

Database makeStaticAutomaticBinDatabase() {
  Database db;
  db.scopes.push_back({1, 0, "top", 0, "top"});
  db.functionalTypes.push_back({10, 1, "cg", 0, 2017, "top.cg"});
  db.functionalItems.push_back({20, 10, "cp", FunctionalItemKind::Coverpoint, 0,
                                100, 1, 0, "top.cg.cp"});
  db.functionalBins.push_back({30, 20, "auto[RED]", FunctionalBinKind::State,
                               FunctionalBinAutomatic, 1, 0,
                               "top.cg.cp.auto[RED]"});
  db.functionalValueSets.push_back(integralSet(100, 20, 0, 4));
  db.functionalValueAtoms.push_back(integralAtom(100, 0, 1));
  db.functionalValueLimbs.push_back(integralLimb(100, 0, 0, 1));
  db.functionalBinPlans.push_back({30, 100});
  db.functionalConfigurations.push_back({10, {}, 0});
  db.resolvedFunctionalItems.push_back({10,
                                        {},
                                        200,
                                        20,
                                        "cp",
                                        FunctionalItemKind::Coverpoint,
                                        0,
                                        100,
                                        1,
                                        0,
                                        "top.cg.cp"});
  db.resolvedFunctionalBins.push_back({10,
                                       {},
                                       300,
                                       30,
                                       200,
                                       "auto[RED]",
                                       FunctionalBinKind::State,
                                       FunctionalBinAutomatic,
                                       0,
                                       0,
                                       1,
                                       "top.cg.cp.auto[RED]"});
  db.resolvedFunctionalValueSets.push_back(
      resolvedSet(400, 100, 200, 0, 4, 300, 0, 0, 0,
                  ResolvedFunctionalValueSetRole::StateBin));
  db.resolvedFunctionalValueAtoms.push_back(resolvedAtom(400, 0, 1));
  db.resolvedFunctionalValueLimbs.push_back(resolvedLimb(400, 0, 1));
  db.resolvedFunctionalBinPlans.push_back({10, {}, 300, 400});
  db.resolvedFunctionalBinGroups.push_back({10,
                                            {},
                                            200,
                                            30,
                                            0,
                                            1,
                                            0,
                                            FunctionalBinArrayMode::Scalar,
                                            FunctionalBinDistributionKind::None,
                                            0,
                                            FunctionalBinKind::State});
  Digest configuration =
      computeFunctionalConfigurationFingerprint(db, 10, Digest{});
  db.functionalConfigurations.front().configuration = configuration;
  setConfiguration(db.resolvedFunctionalItems, configuration);
  setConfiguration(db.resolvedFunctionalBins, configuration);
  setConfiguration(db.resolvedFunctionalValueSets, configuration);
  setConfiguration(db.resolvedFunctionalBinPlans, configuration);
  setConfiguration(db.resolvedFunctionalBinGroups, configuration);
  db.schemaFingerprint = computeSchemaFingerprint(db);
  return db;
}

void assignSecondRun(Database &db) {
  UUID oldRun = db.runs.front().uuid;
  UUID newRun{};
  newRun[0] = 2;
  db.runs.front().uuid = newRun;
  db.runs.front().name = "second";
  for (auto &instance : db.resolvedInstances)
    if (instance.run == oldRun)
      instance.run = newRun;
  for (auto &option : db.resolvedInstanceOptions)
    if (option.run == oldRun)
      option.run = newRun;
  for (auto &counter : db.counters)
    if (counter.run == oldRun)
      counter.run = newRun;
  for (auto &tuple : db.sparseCrossTuples)
    if (tuple.run == oldRun)
      tuple.run = newRun;
  for (auto &diagnostic : db.illegalBinDiagnostics)
    if (diagnostic.run == oldRun)
      diagnostic.run = newRun;
}

void canonicalizeResolvedValueSets(Database &db) {
  auto itemTemplate = [&](uint64_t id) {
    auto item = std::find_if(db.resolvedFunctionalItems.begin(),
                             db.resolvedFunctionalItems.end(),
                             [&](const auto &value) { return value.id == id; });
    return item == db.resolvedFunctionalItems.end() ? uint64_t{0}
                                                    : item->templateItem;
  };
  auto ownerKey = [&](uint64_t id) {
    auto bin = std::find_if(db.resolvedFunctionalBins.begin(),
                            db.resolvedFunctionalBins.end(),
                            [&](const auto &value) { return value.id == id; });
    return bin == db.resolvedFunctionalBins.end()
               ? std::make_pair(uint64_t{0}, uint32_t{0})
               : std::make_pair(bin->templateBin, bin->expansionOrdinal);
  };
  std::vector<ResolvedFunctionalValueSet> sets = db.resolvedFunctionalValueSets;
  std::sort(sets.begin(), sets.end(), [&](const auto &a, const auto &b) {
    auto key = [&](const auto &set) {
      auto [templateBin, expansionOrdinal] = ownerKey(set.ownerBin);
      return std::make_tuple(set.type, set.configuration, set.templateValueSet,
                             static_cast<uint32_t>(set.role),
                             itemTemplate(set.item), templateBin,
                             expansionOrdinal, set.ownerSelector,
                             set.ownerOrdinal, set.ownerSubordinal);
    };
    return key(a) < key(b);
  });
  const auto atoms = db.resolvedFunctionalValueAtoms;
  const auto limbs = db.resolvedFunctionalValueLimbs;
  db.resolvedFunctionalValueSets.clear();
  db.resolvedFunctionalValueAtoms.clear();
  db.resolvedFunctionalValueLimbs.clear();
  for (auto set : sets) {
    uint32_t oldFirstAtom = set.firstAtom;
    set.firstAtom = db.resolvedFunctionalValueAtoms.size();
    for (uint32_t atomIndex = 0; atomIndex != set.atomCount; ++atomIndex) {
      auto atom = atoms[oldFirstAtom + atomIndex];
      uint32_t oldFirstLimb = atom.firstLimb;
      atom.firstLimb = db.resolvedFunctionalValueLimbs.size();
      db.resolvedFunctionalValueLimbs.insert(
          db.resolvedFunctionalValueLimbs.end(), limbs.begin() + oldFirstLimb,
          limbs.begin() + oldFirstLimb + atom.limbCount);
      db.resolvedFunctionalValueAtoms.push_back(atom);
    }
    db.resolvedFunctionalValueSets.push_back(set);
  }
}

void rekeyOnlyConfiguration(Database &db) {
  Digest empty{};
  db.functionalConfigurations.front().configuration = empty;
  setConfiguration(db.functionalConfigurationOptions, empty);
  setConfiguration(db.resolvedFunctionalItems, empty);
  setConfiguration(db.resolvedFunctionalBins, empty);
  setConfiguration(db.resolvedFunctionalValueSets, empty);
  setConfiguration(db.resolvedFunctionalBinPlans, empty);
  setConfiguration(db.resolvedFunctionalBinGroups, empty);
  setConfiguration(db.resolvedTransitionAlternatives, empty);
  setConfiguration(db.resolvedTransitionExpansionGroups, empty);
  setConfiguration(db.resolvedTransitionSteps, empty);
  setConfiguration(db.resolvedCrossPlans, empty);
  setConfiguration(db.resolvedCrossAutomaticNodes, empty);
  setConfiguration(db.resolvedCrossSelectorBindings, empty);
  setConfiguration(db.resolvedFunctionalTupleSets, empty);
  Digest configuration =
      computeFunctionalConfigurationFingerprint(db, 10, empty);
  db.functionalConfigurations.front().configuration = configuration;
  setConfiguration(db.functionalConfigurationOptions, configuration);
  setConfiguration(db.resolvedFunctionalItems, configuration);
  setConfiguration(db.resolvedFunctionalBins, configuration);
  setConfiguration(db.resolvedFunctionalValueSets, configuration);
  setConfiguration(db.resolvedFunctionalBinPlans, configuration);
  setConfiguration(db.resolvedFunctionalBinGroups, configuration);
  setConfiguration(db.resolvedTransitionAlternatives, configuration);
  setConfiguration(db.resolvedTransitionExpansionGroups, configuration);
  setConfiguration(db.resolvedTransitionSteps, configuration);
  setConfiguration(db.resolvedCrossPlans, configuration);
  setConfiguration(db.resolvedCrossAutomaticNodes, configuration);
  setConfiguration(db.resolvedCrossSelectorBindings, configuration);
  setConfiguration(db.resolvedFunctionalTupleSets, configuration);
  for (auto &instance : db.resolvedInstances)
    instance.configuration = configuration;
}

Database makeGeneratedResolvedRealDatabase() {
  Database db = makeStaticAutomaticBinDatabase();
  db.functionalTypes.front().languageVersion = 2023;
  db.functionalBins.clear();
  db.functionalBinPlans.clear();
  db.functionalValueSets.clear();
  db.functionalValueAtoms.clear();
  db.functionalValueLimbs.clear();
  db.resolvedFunctionalBins.front().templateBin = 0;
  db.resolvedFunctionalBinPlans.front().arrayCardinality = 1;
  db.resolvedFunctionalBinPlans.front().arrayMode =
      FunctionalBinArrayMode::Fixed;
  db.resolvedFunctionalBinPlans.front().distribution =
      FunctionalBinDistributionKind::Uniform;
  db.resolvedFunctionalBinGroups.front().templateBin = 0;
  db.resolvedFunctionalBinGroups.front().arrayCardinality = 1;
  db.resolvedFunctionalBinGroups.front().arrayMode =
      FunctionalBinArrayMode::Fixed;
  db.resolvedFunctionalBinGroups.front().distribution =
      FunctionalBinDistributionKind::Uniform;
  auto &set = db.resolvedFunctionalValueSets.front();
  set.templateValueSet = 0;
  set.bitWidth = 64;
  set.kind = FunctionalValueSetKind::Real;
  set.signedness = CoverageSignedness::NotApplicable;
  auto &atom = db.resolvedFunctionalValueAtoms.front();
  atom.kind = FunctionalValueAtomKind::RealInterval;
  atom.firstLimb = 0;
  atom.limbCount = 0;
  atom.realLowBits = realBits(1.0);
  atom.realHighBits = realBits(2.0);
  atom.flags |= FunctionalValueAtomRealRange;
  db.resolvedFunctionalValueLimbs.clear();
  rekeyOnlyConfiguration(db);
  db.schemaFingerprint = computeSchemaFingerprint(db);
  return db;
}

void permuteResolvedItemAndBinIDs(Database &db) {
  auto remapItem = [](uint64_t &id) {
    switch (id) {
    case 200:
      id = 920;
      break;
    case 201:
      id = 710;
      break;
    case 202:
      id = 830;
      break;
    case 203:
      id = 640;
      break;
    default:
      break;
    }
  };
  auto remapBin = [](uint64_t &id) {
    switch (id) {
    case 300:
      id = 990;
      break;
    case 301:
      id = 870;
      break;
    case 302:
      id = 760;
      break;
    case 303:
      id = 540;
      break;
    case 304:
      id = 650;
      break;
    default:
      break;
    }
  };
  auto remapSet = [](uint64_t &id) {
    switch (id) {
    case 400:
      id = 940;
      break;
    case 401:
      id = 720;
      break;
    case 402:
      id = 860;
      break;
    case 403:
      id = 610;
      break;
    case 404:
      id = 780;
      break;
    case 405:
      id = 550;
      break;
    case 406:
      id = 690;
      break;
    default:
      break;
    }
  };
  auto remapTupleSet = [](uint64_t &id) {
    if (id == 800)
      id = 980;
    else if (id == 801)
      id = 620;
  };
  auto remapTuple = [](uint64_t &id) {
    if (id == 810)
      id = 970;
    else if (id == 811)
      id = 630;
  };
  for (auto &item : db.resolvedFunctionalItems)
    remapItem(item.id);
  std::sort(db.resolvedFunctionalItems.begin(),
            db.resolvedFunctionalItems.end(), [](const auto &a, const auto &b) {
              return std::tie(a.type, a.configuration, a.id) <
                     std::tie(b.type, b.configuration, b.id);
            });
  for (auto &bin : db.resolvedFunctionalBins) {
    remapBin(bin.id);
    remapItem(bin.item);
  }
  for (auto &set : db.resolvedFunctionalValueSets) {
    remapSet(set.id);
    remapItem(set.item);
    remapBin(set.ownerBin);
  }
  for (auto &atom : db.resolvedFunctionalValueAtoms)
    remapSet(atom.valueSet);
  for (auto &limb : db.resolvedFunctionalValueLimbs)
    remapSet(limb.valueSet);
  for (auto &plan : db.resolvedFunctionalBinPlans) {
    remapBin(plan.bin);
    remapSet(plan.valueSet);
  }
  std::sort(db.resolvedFunctionalBinPlans.begin(),
            db.resolvedFunctionalBinPlans.end(),
            [](const auto &a, const auto &b) {
              return std::tie(a.type, a.configuration, a.bin) <
                     std::tie(b.type, b.configuration, b.bin);
            });
  for (auto &group : db.resolvedFunctionalBinGroups)
    remapItem(group.item);
  for (auto &alternative : db.resolvedTransitionAlternatives) {
    alternative.id = alternative.id == 600 ? 930 : alternative.id;
    remapBin(alternative.bin);
  }
  for (auto &step : db.resolvedTransitionSteps) {
    remapBin(step.bin);
    if (step.alternative == 600)
      step.alternative = 930;
    remapSet(step.valueSet);
  }
  for (auto &group : db.resolvedTransitionExpansionGroups)
    remapItem(group.item);
  for (auto &plan : db.resolvedCrossPlans)
    remapItem(plan.cross);
  for (auto &plan : db.resolvedCrossPlans)
    if (plan.rootNode == 900)
      plan.rootNode = 990;
  for (auto &node : db.resolvedCrossAutomaticNodes) {
    remapItem(node.cross);
    if (node.id == 900)
      node.id = 990;
    else if (node.id == 901)
      node.id = 991;
  }
  for (auto &edge : db.resolvedCrossAutomaticEdges) {
    if (edge.node == 900)
      edge.node = 990;
    else if (edge.node == 901)
      edge.node = 991;
    if (edge.child == 900)
      edge.child = 990;
    else if (edge.child == 901)
      edge.child = 991;
    remapBin(edge.bin);
  }
  for (auto &binding : db.resolvedCrossSelectorBindings) {
    remapItem(binding.cross);
    remapSet(binding.valueSet);
    remapTupleSet(binding.tupleSet);
  }
  for (auto &set : db.resolvedFunctionalTupleSets) {
    remapTupleSet(set.id);
    remapItem(set.cross);
  }
  for (auto &tuple : db.resolvedFunctionalTupleSetTuples) {
    remapTupleSet(tuple.tupleSet);
    remapTuple(tuple.id);
  }
  for (auto &component : db.resolvedFunctionalTupleSetComponents) {
    remapTuple(component.tuple);
    remapItem(component.target);
    remapBin(component.bin);
    remapSet(component.valueSet);
  }

  // ID-keyed tables remain physically canonical after the nonmonotonic
  // remapping; rebuild their dependent physical ranges rather than using IDs
  // as merge identity.
  canonicalizeResolvedValueSets(db);

  std::vector<ResolvedFunctionalTupleSet> tupleSets =
      db.resolvedFunctionalTupleSets;
  std::sort(tupleSets.begin(), tupleSets.end(),
            [](const auto &a, const auto &b) {
              return std::tie(a.type, a.configuration, a.id) <
                     std::tie(b.type, b.configuration, b.id);
            });
  const auto tuples = db.resolvedFunctionalTupleSetTuples;
  const auto components = db.resolvedFunctionalTupleSetComponents;
  db.resolvedFunctionalTupleSets.clear();
  db.resolvedFunctionalTupleSetTuples.clear();
  db.resolvedFunctionalTupleSetComponents.clear();
  for (auto set : tupleSets) {
    uint32_t oldFirstTuple = set.firstTuple;
    set.firstTuple = db.resolvedFunctionalTupleSetTuples.size();
    for (uint32_t tupleIndex = 0; tupleIndex != set.tupleCount; ++tupleIndex) {
      auto tuple = tuples[oldFirstTuple + tupleIndex];
      uint32_t oldFirstComponent = tuple.firstComponent;
      tuple.firstComponent = db.resolvedFunctionalTupleSetComponents.size();
      db.resolvedFunctionalTupleSetComponents.insert(
          db.resolvedFunctionalTupleSetComponents.end(),
          components.begin() + oldFirstComponent,
          components.begin() + oldFirstComponent + tuple.componentCount);
      db.resolvedFunctionalTupleSetTuples.push_back(tuple);
    }
    db.resolvedFunctionalTupleSets.push_back(set);
  }
  for (auto &counter : db.counters)
    if (counter.metric == MetricKind::Functional)
      remapBin(counter.entity);
  for (auto &diagnostic : db.illegalBinDiagnostics)
    remapBin(diagnostic.bin);
  for (auto &component : db.sparseCrossTupleComponents)
    remapBin(component);
}

TEST(CoverageDatabaseTest, TypedV1RoundTripsDeterministically) {
  Database db = makeTypedDatabase();
  ResolvedInstanceOption typeGoal;
  typeGoal.run = db.runs.front().uuid;
  typeGoal.owner = db.functionalTypes.front().id;
  typeGoal.ownerKind = FunctionalConfigurationOptionOwnerKind::Group;
  typeGoal.option = FunctionalConfigurationOptionKind::Goal;
  typeGoal.valueKind = FunctionalConfigurationValueKind::Unsigned;
  typeGoal.value = 80;
  db.resolvedInstanceOptions.push_back(typeGoal);
  ResolvedInstanceOption typeComment;
  typeComment.run = db.runs.front().uuid;
  typeComment.owner = db.functionalItems.front().id;
  typeComment.ownerKind = FunctionalConfigurationOptionOwnerKind::Item;
  typeComment.option = FunctionalConfigurationOptionKind::Comment;
  typeComment.valueKind = FunctionalConfigurationValueKind::String;
  typeComment.stringValue = "procedural type comment";
  db.resolvedInstanceOptions.push_back(typeComment);
  ResolvedInstanceOption comment;
  comment.run = db.runs.front().uuid;
  comment.instance = db.resolvedInstances.front().id;
  comment.ownerKind = FunctionalConfigurationOptionOwnerKind::Group;
  comment.option = FunctionalConfigurationOptionKind::Comment;
  comment.valueKind = FunctionalConfigurationValueKind::String;
  comment.stringValue = "procedural comment";
  db.resolvedInstanceOptions.push_back(comment);
  ResolvedInstanceOption atLeast;
  atLeast.run = db.runs.front().uuid;
  atLeast.instance = db.resolvedInstances.front().id;
  atLeast.owner = db.functionalItems.front().id;
  atLeast.ownerKind = FunctionalConfigurationOptionOwnerKind::Item;
  atLeast.option = FunctionalConfigurationOptionKind::AtLeast;
  atLeast.valueKind = FunctionalConfigurationValueKind::Unsigned;
  atLeast.value = 2;
  db.resolvedInstanceOptions.push_back(atLeast);
  Diagnostic diagnostic;
  ASSERT_EQ(validate(db, &diagnostic), Status::Ok)
      << (diagnostic.field ? diagnostic.field : "") << ": "
      << diagnostic.detail;

  std::vector<uint8_t> first;
  ASSERT_EQ(serialize(db, first, &diagnostic), Status::Ok) << diagnostic.detail;
  Database parsed;
  ASSERT_EQ(parse(first.data(), first.size(), parsed, {}, &diagnostic),
            Status::Ok)
      << diagnostic.detail;
  std::vector<uint8_t> second;
  ASSERT_EQ(serialize(parsed, second, &diagnostic), Status::Ok)
      << diagnostic.detail;
  EXPECT_EQ(first, second);
  ASSERT_EQ(parsed.scopes.size(), 1u);
  EXPECT_EQ(parsed.scopes.front().definition, "top");
  ASSERT_EQ(parsed.resolvedTransitionAlternatives.size(), 1u);
  ASSERT_EQ(parsed.resolvedTransitionExpansionGroups.size(), 1u);
  EXPECT_EQ(parsed.resolvedTransitionExpansionGroups.front().expansionCount,
            1u);
  ASSERT_EQ(parsed.resolvedFunctionalValueLimbs.size(), 11u);
  EXPECT_EQ(parsed.resolvedFunctionalValueLimbs[1].wildcardMask, 0x100u);
  ASSERT_EQ(parsed.resolvedFunctionalTupleSets.size(), 2u);
  ASSERT_EQ(parsed.resolvedCrossPlans.size(), 1u);
  EXPECT_EQ(parsed.resolvedCrossPlans.front().rootNode, 900u);
  ASSERT_EQ(parsed.resolvedCrossAutomaticNodes.size(), 2u);
  EXPECT_EQ(parsed.resolvedCrossAutomaticNodes[0].targetOrdinal, 0u);
  EXPECT_EQ(parsed.resolvedCrossAutomaticNodes[1].targetOrdinal, 1u);
  ASSERT_EQ(parsed.resolvedCrossAutomaticEdges.size(), 2u);
  EXPECT_EQ(parsed.resolvedCrossAutomaticEdges[0].bin, 300u);
  EXPECT_EQ(parsed.resolvedCrossAutomaticEdges[0].child, 901u);
  EXPECT_EQ(parsed.resolvedCrossAutomaticEdges[1].bin, 302u);
  EXPECT_EQ(parsed.resolvedCrossAutomaticEdges[1].child, 0u);
  ASSERT_EQ(parsed.functionalFormals.size(), 2u);
  EXPECT_EQ(parsed.functionalFormals[0].name, "constructor_value");
  EXPECT_EQ(parsed.functionalFormals[0].defaultExpression, 899u);
  EXPECT_EQ(parsed.functionalFormals[1].kind, FunctionalFormalKind::Sample);
  EXPECT_EQ(parsed.functionalFormals[1].defaultExpression, 898u);
  EXPECT_EQ(parsed.functionalExpressions.front().evaluationPhase,
            FunctionalExpressionEvaluationPhase::Sample);
  ASSERT_EQ(parsed.resolvedInstanceOptions.size(), 4u);
  EXPECT_EQ(parsed.resolvedInstanceOptions[0].instance, 0u);
  EXPECT_EQ(parsed.resolvedInstanceOptions[0].value, 80u);
  EXPECT_EQ(parsed.resolvedInstanceOptions[1].stringValue,
            "procedural type comment");
  EXPECT_EQ(parsed.resolvedInstanceOptions[2].stringValue,
            "procedural comment");
  EXPECT_EQ(parsed.resolvedInstanceOptions[3].owner,
            db.functionalItems.front().id);
  EXPECT_EQ(parsed.resolvedInstanceOptions[3].value, 2u);
  EXPECT_EQ(parsed.illegalBinDiagnostics.front().instance, 1000u);
}

TEST(CoverageDatabaseTest, RejectsMalformedResolvedInstanceOptions) {
  Database base = makeTypedDatabase();
  ResolvedInstanceOption option;
  option.run = base.runs.front().uuid;
  option.instance = base.resolvedInstances.front().id;
  option.ownerKind = FunctionalConfigurationOptionOwnerKind::Group;
  option.option = FunctionalConfigurationOptionKind::Goal;
  option.valueKind = FunctionalConfigurationValueKind::Unsigned;

  Database malformed = base;
  option.owner = base.functionalItems.front().id;
  malformed.resolvedInstanceOptions = {option};
  EXPECT_EQ(validate(malformed), Status::InvalidDatabase);

  malformed = base;
  option.owner = 0;
  option.value = 101;
  malformed.resolvedInstanceOptions = {option};
  EXPECT_EQ(validate(malformed), Status::InvalidDatabase);

  malformed = base;
  option.value = 0;
  option.option = FunctionalConfigurationOptionKind::Comment;
  malformed.resolvedInstanceOptions = {option};
  EXPECT_EQ(validate(malformed), Status::InvalidDatabase);

  malformed = base;
  option.option = FunctionalConfigurationOptionKind::AtLeast;
  malformed.resolvedInstanceOptions = {option, option};
  EXPECT_EQ(validate(malformed), Status::UnsortedOrDuplicate);

  malformed = base;
  option.ownerKind = FunctionalConfigurationOptionOwnerKind::Item;
  option.owner = UINT64_MAX;
  malformed.resolvedInstanceOptions = {option};
  EXPECT_EQ(validate(malformed), Status::InvalidReference);

  malformed = base;
  option = {};
  option.run = base.runs.front().uuid;
  option.owner = base.functionalTypes.front().id;
  option.ownerKind = FunctionalConfigurationOptionOwnerKind::Group;
  option.option = FunctionalConfigurationOptionKind::AtLeast;
  option.valueKind = FunctionalConfigurationValueKind::Unsigned;
  malformed.resolvedInstanceOptions = {option};
  EXPECT_EQ(validate(malformed), Status::InvalidDatabase);

  malformed = base;
  option.owner = base.functionalItems.front().id;
  option.ownerKind = FunctionalConfigurationOptionOwnerKind::Item;
  option.option = FunctionalConfigurationOptionKind::MergeInstances;
  malformed.resolvedInstanceOptions = {option};
  EXPECT_EQ(validate(malformed), Status::InvalidDatabase);

  option = {};
  option.run = base.runs.front().uuid;
  option.instance = base.resolvedInstances.front().id;
  option.ownerKind = FunctionalConfigurationOptionOwnerKind::Group;
  option.option = FunctionalConfigurationOptionKind::CrossNumPrintMissing;
  option.valueKind = FunctionalConfigurationValueKind::Unsigned;
  option.value = 7;
  malformed = base;
  malformed.resolvedInstanceOptions = {option};
  EXPECT_EQ(validate(malformed), Status::Ok);

  option.value = uint64_t{INT32_MAX} + 1;
  malformed.resolvedInstanceOptions = {option};
  EXPECT_EQ(validate(malformed), Status::InvalidDatabase);
  option.value = 7;

  option.ownerKind = FunctionalConfigurationOptionOwnerKind::Item;
  option.owner = base.functionalItems.front().id;
  malformed.resolvedInstanceOptions = {option};
  EXPECT_EQ(validate(malformed), Status::InvalidDatabase);

  auto cross = std::find_if(
      base.functionalItems.begin(), base.functionalItems.end(),
      [](const auto &item) { return item.kind == FunctionalItemKind::Cross; });
  ASSERT_NE(cross, base.functionalItems.end());
  option.owner = cross->id;
  malformed.resolvedInstanceOptions = {option};
  EXPECT_EQ(validate(malformed), Status::Ok);

  option.instance = 0;
  option.ownerKind = FunctionalConfigurationOptionOwnerKind::Group;
  option.owner = base.functionalTypes.front().id;
  malformed.resolvedInstanceOptions = {option};
  EXPECT_EQ(validate(malformed), Status::InvalidDatabase);

  FunctionalConfigurationOption configurationOption;
  configurationOption.type = base.functionalTypes.front().id;
  configurationOption.owner = base.functionalTypes.front().id;
  configurationOption.ownerKind = FunctionalConfigurationOptionOwnerKind::Group;
  configurationOption.scope = FunctionalOptionScopeKind::Instance;
  configurationOption.option =
      FunctionalConfigurationOptionKind::CrossNumPrintMissing;
  configurationOption.valueKind = FunctionalConfigurationValueKind::Unsigned;
  configurationOption.value = INT32_MAX;
  malformed = base;
  malformed.functionalConfigurationOptions.insert(
      malformed.functionalConfigurationOptions.begin(), configurationOption);
  rekeyOnlyConfiguration(malformed);
  malformed.schemaFingerprint = computeSchemaFingerprint(malformed);
  Diagnostic diagnostic;
  EXPECT_EQ(validate(malformed, &diagnostic), Status::Ok)
      << (diagnostic.field ? diagnostic.field : "") << ": "
      << diagnostic.detail;
  malformed.functionalConfigurationOptions.front().value =
      uint64_t{INT32_MAX} + 1;
  EXPECT_EQ(validate(malformed), Status::InvalidReference);
}

TEST(CoverageDatabaseTest, StreamsSameImageAndStopsAfterSinkFailure) {
  Database db = makeTypedDatabase();
  for (uint64_t index = 0; index != 128; ++index) {
    SourceFile source;
    source.id = 1000 + index;
    source.path = "generated-" + std::to_string(index) + ".sv";
    db.sourceFiles.push_back(std::move(source));
  }
  db.schemaFingerprint = computeSchemaFingerprint(db);
  std::vector<uint8_t> expected;
  ASSERT_EQ(serialize(db, expected), Status::Ok);

  struct ObservedStream {
    std::vector<uint8_t> bytes;
    size_t maxChunk = 0;
  } streamed;
  auto append = [](const uint8_t *data, size_t size, void *user) {
    auto &stream = *static_cast<ObservedStream *>(user);
    stream.maxChunk = std::max(stream.maxChunk, size);
    stream.bytes.insert(stream.bytes.end(), data, data + size);
    return true;
  };
  ASSERT_EQ(serialize(db, append, &streamed), Status::Ok);
  EXPECT_EQ(streamed.bytes, expected);
  constexpr size_t SourceSectionBytes = 128 * 48;
  EXPECT_LT(sectionOffset(expected, SectionKind::Strings), SourceSectionBytes);
  EXPECT_LT(streamed.maxChunk, SourceSectionBytes);

  struct FailingSink {
    uint32_t calls = 0;
    uint32_t failOn = 2;
    size_t maxChunk = 0;
  } sink;
  auto fail = [](const uint8_t *, size_t size, void *user) {
    auto &sink = *static_cast<FailingSink *>(user);
    ++sink.calls;
    sink.maxChunk = std::max(sink.maxChunk, size);
    return sink.calls != sink.failOn;
  };
  Diagnostic diagnostic;
  EXPECT_EQ(serialize(db, fail, &sink, &diagnostic), Status::IoError);
  EXPECT_EQ(sink.calls, sink.failOn);
  EXPECT_EQ(diagnostic.status, Status::IoError);
  EXPECT_EQ(diagnostic.section, SectionKind::Strings);
  EXPECT_EQ(diagnostic.record, 0u);
  EXPECT_STREQ(diagnostic.field, "sink");
  EXPECT_LE(sink.maxChunk, sectionOffset(expected, SectionKind::Strings));
}

TEST(CoverageDatabaseTest, GeneratedFixedRecordsPreserveV1Layout) {
  Database db = makeTypedDatabase();
  std::vector<uint8_t> image;
  ASSERT_EQ(serialize(db, image), Status::Ok);

  const size_t scope = sectionOffset(image, SectionKind::Scopes);
  ASSERT_NE(scope, std::string::npos);
  EXPECT_EQ(read64(image, scope), 1u);
  EXPECT_EQ(read64(image, scope + 8), 0u);
  EXPECT_NE(read32(image, scope + 16), 0u);
  EXPECT_EQ(read32(image, scope + 20), 0u);
  EXPECT_NE(read32(image, scope + 24), 0u);
  EXPECT_EQ(read32(image, scope + 28), 0u);

  const size_t type = sectionOffset(image, SectionKind::FunctionalTypes);
  ASSERT_NE(type, std::string::npos);
  EXPECT_EQ(read64(image, type), 10u);
  EXPECT_EQ(read64(image, type + 8), 1u);
  EXPECT_EQ(read32(image, type + 20), 0u);
  EXPECT_EQ(read32(image, type + 24), 2023u);

  const size_t item = sectionOffset(image, SectionKind::FunctionalItems);
  ASSERT_NE(item, std::string::npos);
  EXPECT_EQ(read64(image, item), 20u);
  EXPECT_EQ(read64(image, item + 8), 10u);
  EXPECT_EQ(read32(image, item + 20),
            static_cast<uint32_t>(FunctionalItemKind::Coverpoint));
  EXPECT_EQ(read32(image, item + 44), 0u);

  const size_t bin = sectionOffset(image, SectionKind::FunctionalBins);
  ASSERT_NE(bin, std::string::npos);
  EXPECT_EQ(read64(image, bin), 30u);
  EXPECT_EQ(read64(image, bin + 8), 20u);
  EXPECT_EQ(read64(image, bin + 32), 1u);
  EXPECT_EQ(read32(image, bin + 44), 0u);

  const size_t crossPlan =
      sectionOffset(image, SectionKind::ResolvedCrossPlans);
  ASSERT_NE(crossPlan, std::string::npos);
  EXPECT_EQ(read64(image, crossPlan), 10u);
  EXPECT_EQ(read64(image, crossPlan + 40), 203u);
  EXPECT_EQ(read32(image, crossPlan + 60), 1u);
  EXPECT_EQ(read64(image, crossPlan + 64), 900u);

  const size_t automaticNode =
      sectionOffset(image, SectionKind::ResolvedCrossAutomaticNodes);
  ASSERT_NE(automaticNode, std::string::npos);
  EXPECT_EQ(read64(image, automaticNode), 10u);
  EXPECT_EQ(read64(image, automaticNode + 40), 203u);
  EXPECT_EQ(read64(image, automaticNode + 48), 900u);
  EXPECT_EQ(read32(image, automaticNode + 56), 0u);
  EXPECT_EQ(read32(image, automaticNode + 60), 0u);
  EXPECT_EQ(read32(image, automaticNode + 64), 1u);

  const size_t automaticEdge =
      sectionOffset(image, SectionKind::ResolvedCrossAutomaticEdges);
  ASSERT_NE(automaticEdge, std::string::npos);
  EXPECT_EQ(read64(image, automaticEdge), 900u);
  EXPECT_EQ(read64(image, automaticEdge + 8), 300u);
  EXPECT_EQ(read64(image, automaticEdge + 16), 901u);
  EXPECT_EQ(read32(image, automaticEdge + 24), 0u);

  const size_t formal = sectionOffset(image, SectionKind::FunctionalFormals);
  ASSERT_NE(formal, std::string::npos);
  EXPECT_EQ(read64(image, formal), 60u);
  EXPECT_EQ(read64(image, formal + 8), 10u);
  EXPECT_EQ(read32(image, formal + 20),
            static_cast<uint32_t>(FunctionalFormalKind::Constructor));
  EXPECT_EQ(read32(image, formal + 24),
            static_cast<uint32_t>(FunctionalFormalDirection::Input));
  EXPECT_EQ(read32(image, formal + 28),
            static_cast<uint32_t>(FunctionalExpressionResultKind::Integral));
  EXPECT_EQ(read32(image, formal + 32), FunctionalFormalHasDefault);
  EXPECT_EQ(read32(image, formal + 36), 8u);
  EXPECT_EQ(read32(image, formal + 44), 0u);
  EXPECT_EQ(read64(image, formal + 48), 899u);
  EXPECT_EQ(read64(image, formal + 56), 0u);
}

TEST(CoverageDatabaseTest, GeneratedFixedReferencesAreValidatedBeforeDecode) {
  Database db = makeTypedDatabase();
  std::vector<uint8_t> image;
  ASSERT_EQ(serialize(db, image), Status::Ok);
  const size_t instances =
      sectionOffset(image, SectionKind::ResolvedCovergroupInstances);
  ASSERT_NE(instances, std::string::npos);
  image[instances] ^= 0x40;
  refreshChecksum(image);

  Database output;
  Diagnostic diagnostic;
  EXPECT_EQ(parse(image.data(), image.size(), output, {}, &diagnostic),
            Status::InvalidReference);
  EXPECT_EQ(diagnostic.section, SectionKind::ResolvedCovergroupInstances);
  EXPECT_STREQ(diagnostic.field, "run");

  ASSERT_EQ(serialize(db, image), Status::Ok);
  const size_t scopes = sectionOffset(image, SectionKind::Scopes);
  ASSERT_NE(scopes, std::string::npos);
  image[scopes + 8] = 0xff;
  refreshChecksum(image);
  EXPECT_EQ(parse(image.data(), image.size(), output, {}, &diagnostic),
            Status::InvalidReference);
  EXPECT_EQ(diagnostic.section, SectionKind::Scopes);
  EXPECT_STREQ(diagnostic.field, "parent");

  ASSERT_EQ(serialize(db, image), Status::Ok);
  const size_t scopeDefinition = sectionOffset(image, SectionKind::Scopes);
  ASSERT_NE(scopeDefinition, std::string::npos);
  put32(image, scopeDefinition + 24, UINT32_MAX);
  refreshChecksum(image);
  EXPECT_EQ(parse(image.data(), image.size(), output, {}, &diagnostic),
            Status::InvalidReference);
  EXPECT_EQ(diagnostic.section, SectionKind::Scopes);
  EXPECT_STREQ(diagnostic.field, "definition");

  ASSERT_EQ(serialize(db, image), Status::Ok);
  const size_t formals = sectionOffset(image, SectionKind::FunctionalFormals);
  ASSERT_NE(formals, std::string::npos);
  image[formals + 48] ^= 0x40;
  refreshChecksum(image);
  EXPECT_EQ(parse(image.data(), image.size(), output, {}, &diagnostic),
            Status::InvalidReference);
  EXPECT_EQ(diagnostic.section, SectionKind::FunctionalFormals);
  EXPECT_STREQ(diagnostic.field, "default_expression");

  db = makeTypedDatabase();
  db.functionalFormals.front().resultKind =
      FunctionalExpressionResultKind::String;
  db.functionalFormals.front().bitWidth = 0;
  db.functionalFormals.front().signedness = CoverageSignedness::NotApplicable;
  auto constructorDefault = std::find_if(
      db.functionalExpressions.begin(), db.functionalExpressions.end(),
      [](const FunctionalExpression &candidate) {
        return candidate.id == 899;
      });
  ASSERT_NE(constructorDefault, db.functionalExpressions.end());
  constructorDefault->resultKind = FunctionalExpressionResultKind::String;
  constructorDefault->bitWidth = 0;
  constructorDefault->signedness = CoverageSignedness::NotApplicable;
  db.schemaFingerprint = computeSchemaFingerprint(db);
  ASSERT_EQ(serialize(db, image, &diagnostic), Status::Ok)
      << diagnostic.field << " record " << diagnostic.record << " "
      << diagnostic.detail;
  const size_t stringConstructorFormals =
      sectionOffset(image, SectionKind::FunctionalFormals);
  ASSERT_NE(stringConstructorFormals, std::string::npos);
  put32(image, stringConstructorFormals + 24,
        static_cast<uint32_t>(FunctionalFormalDirection::Ref));
  refreshChecksum(image);
  EXPECT_EQ(parse(image.data(), image.size(), output, {}, &diagnostic),
            Status::InvalidReference);
  EXPECT_EQ(diagnostic.section, SectionKind::Strings);
  EXPECT_STREQ(diagnostic.field, "functional_formal");
}

TEST(CoverageDatabaseTest, RejectsCyclicScopeParents) {
  Database db;
  db.scopes.push_back({1, 2, "top.left", 0, "left"});
  db.scopes.push_back({2, 1, "top.right", 0, "right"});
  Diagnostic diagnostic;
  EXPECT_EQ(validate(db, &diagnostic), Status::InvalidReference);
  EXPECT_STREQ(diagnostic.field, "scope.parent_cycle");
}

TEST(CoverageDatabaseTest, GeneratedFixedFieldDiagnosticsAreExact) {
  Database source = makeTypedDatabase();
  auto check = [&](SectionKind section, size_t fieldOffset, uint32_t value,
                   Status expectedStatus, const char *expectedField) {
    std::vector<uint8_t> image;
    EXPECT_EQ(serialize(source, image), Status::Ok);
    size_t offset = sectionOffset(image, section);
    EXPECT_NE(offset, std::string::npos);
    put32(image, offset + fieldOffset, value);
    refreshChecksum(image);
    Database output;
    Diagnostic diagnostic;
    EXPECT_EQ(parse(image.data(), image.size(), output, {}, &diagnostic),
              expectedStatus);
    EXPECT_EQ(diagnostic.section, section);
    EXPECT_EQ(diagnostic.record, 0u);
    EXPECT_EQ(diagnostic.offset, offset + fieldOffset);
    EXPECT_STREQ(diagnostic.field, expectedField);
  };
  check(SectionKind::FunctionalItems, 44, 1, Status::InvalidDatabase,
        "reserved");
  check(SectionKind::Runs, 56, 8, Status::InvalidEnum, "flags");
  check(SectionKind::FunctionalItems, 20, 99, Status::InvalidEnum, "kind");
  check(SectionKind::FunctionalItems, 16, UINT32_MAX, Status::InvalidReference,
        "name");
  check(SectionKind::FunctionalFormals, 20, 99, Status::InvalidEnum, "kind");
  check(SectionKind::FunctionalFormals, 24, 99, Status::InvalidEnum,
        "direction");
  check(SectionKind::FunctionalFormals, 56, 1, Status::InvalidDatabase,
        "reserved");
}

TEST(CoverageDatabaseTest, RejectsNonV1AndUnknownSections) {
  Database db = makeTypedDatabase();
  std::vector<uint8_t> image;
  ASSERT_EQ(serialize(db, image), Status::Ok);
  put32(image, 8, CodecVersion + 1);
  EXPECT_EQ(parse(image.data(), image.size(), db), Status::VersionMismatch);

  ASSERT_EQ(serialize(makeTypedDatabase(), image), Status::Ok);
  put32(image, HeaderSize, 0xffffu);
  refreshChecksum(image);
  EXPECT_EQ(parse(image.data(), image.size(), db), Status::InvalidEnum);
}

TEST(CoverageDatabaseTest, SchemaFingerprintIncludesCrossIff) {
  Database db = makeTypedDatabase();
  Digest before = computeSchemaFingerprint(db);
  db.crossPlans.front().iffExpression = 12345;
  EXPECT_NE(before, computeSchemaFingerprint(db));
}

TEST(CoverageDatabaseTest, ScopeDefinitionAffectsSchemaAndStrictMerge) {
  Database first = makeTypedDatabase();
  Database second = makeTypedDatabase();
  second.scopes.front().definition = "different";
  EXPECT_NE(computeSchemaFingerprint(first), computeSchemaFingerprint(second));
  second.schemaFingerprint = computeSchemaFingerprint(second);
  EXPECT_EQ(merge(first, second), Status::SchemaMismatch);
}

TEST(CoverageDatabaseTest, FunctionalFormalsAffectSchemaAndStrictMerge) {
  Database first = makeTypedDatabase();
  Database second = makeTypedDatabase();
  second.functionalFormals.front().name = "different";
  EXPECT_NE(computeSchemaFingerprint(first), computeSchemaFingerprint(second));
  second.schemaFingerprint = computeSchemaFingerprint(second);
  EXPECT_EQ(merge(first, second), Status::SchemaMismatch);
}

TEST(CoverageDatabaseTest, RejectsMalformedFunctionalFormals) {
  Diagnostic diagnostic;
  Database db = makeTypedDatabase();
  db.functionalFormals[1].ordinal = 1;
  EXPECT_EQ(validate(db, &diagnostic), Status::InvalidReference);
  EXPECT_STREQ(diagnostic.field, "functional_formal");

  db = makeTypedDatabase();
  db.functionalFormals[1].name = db.functionalFormals[0].name;
  EXPECT_EQ(validate(db, &diagnostic), Status::InvalidReference);
  EXPECT_STREQ(diagnostic.field, "functional_formal");

  db = makeTypedDatabase();
  db.functionalFormals.front().bitWidth = 0;
  EXPECT_EQ(validate(db, &diagnostic), Status::InvalidReference);
  EXPECT_STREQ(diagnostic.field, "functional_formal");

  db = makeTypedDatabase();
  db.functionalFormals[0].defaultExpression = 0;
  EXPECT_EQ(validate(db, &diagnostic), Status::InvalidReference);
  EXPECT_STREQ(diagnostic.field, "functional_formal");

  db = makeTypedDatabase();
  db.functionalFormals.front().resultKind =
      FunctionalExpressionResultKind::String;
  db.functionalFormals.front().bitWidth = 0;
  db.functionalFormals.front().signedness = CoverageSignedness::NotApplicable;
  auto constructorDefault = std::find_if(
      db.functionalExpressions.begin(), db.functionalExpressions.end(),
      [](const FunctionalExpression &candidate) {
        return candidate.id == 899;
      });
  ASSERT_NE(constructorDefault, db.functionalExpressions.end());
  constructorDefault->resultKind = FunctionalExpressionResultKind::String;
  constructorDefault->bitWidth = 0;
  constructorDefault->signedness = CoverageSignedness::NotApplicable;
  db.functionalFormals.front().direction = FunctionalFormalDirection::Ref;
  EXPECT_EQ(validate(db, &diagnostic), Status::InvalidReference);
  EXPECT_STREQ(diagnostic.field, "functional_formal");

  db.functionalFormals.front().direction = FunctionalFormalDirection::Input;
  db.schemaFingerprint = computeSchemaFingerprint(db);
  EXPECT_EQ(validate(db, &diagnostic), Status::Ok)
      << diagnostic.field << " record " << diagnostic.record << " "
      << diagnostic.detail;

  db = makeTypedDatabase();
  db.functionalFormals.back().resultKind =
      FunctionalExpressionResultKind::String;
  db.functionalFormals.back().bitWidth = 0;
  db.functionalFormals.back().signedness = CoverageSignedness::NotApplicable;
  auto sampleDefault = std::find_if(db.functionalExpressions.begin(),
                                    db.functionalExpressions.end(),
                                    [](const FunctionalExpression &candidate) {
                                      return candidate.id == 898;
                                    });
  ASSERT_NE(sampleDefault, db.functionalExpressions.end());
  sampleDefault->resultKind = FunctionalExpressionResultKind::String;
  db.schemaFingerprint = computeSchemaFingerprint(db);
  EXPECT_EQ(validate(db, &diagnostic), Status::Ok)
      << diagnostic.field << " record " << diagnostic.record << " "
      << diagnostic.detail;

  db = makeTypedDatabase();
  auto expression = std::find_if(db.functionalExpressions.begin(),
                                 db.functionalExpressions.end(),
                                 [](const FunctionalExpression &candidate) {
                                   return candidate.id == 899;
                                 });
  ASSERT_NE(expression, db.functionalExpressions.end());
  expression->evaluationPhase = FunctionalExpressionEvaluationPhase::Sample;
  EXPECT_EQ(validate(db, &diagnostic), Status::InvalidReference);
  EXPECT_STREQ(diagnostic.field, "functional_expression");

  db = makeTypedDatabase();
  expression = std::find_if(db.functionalExpressions.begin(),
                            db.functionalExpressions.end(),
                            [](const FunctionalExpression &candidate) {
                              return candidate.id == 898;
                            });
  ASSERT_NE(expression, db.functionalExpressions.end());
  expression->evaluationPhase =
      FunctionalExpressionEvaluationPhase::Constructor;
  EXPECT_EQ(validate(db, &diagnostic), Status::InvalidReference);
  EXPECT_STREQ(diagnostic.field, "functional_expression");

  db = makeTypedDatabase();
  expression = std::find_if(db.functionalExpressions.begin(),
                            db.functionalExpressions.end(),
                            [](const FunctionalExpression &candidate) {
                              return candidate.id == 899;
                            });
  ASSERT_NE(expression, db.functionalExpressions.end());
  expression->bitWidth = 16;
  EXPECT_EQ(validate(db, &diagnostic), Status::InvalidReference);
  EXPECT_STREQ(diagnostic.field, "functional_expression");

  db = makeTypedDatabase();
  db.functionalFormals.front().defaultExpression = 900;
  EXPECT_EQ(validate(db, &diagnostic), Status::InvalidReference);
  EXPECT_STREQ(diagnostic.field, "functional_expression");
}

TEST(CoverageDatabaseTest, RejectsBinWithExpressionOwnedByCrossBin) {
  Database db = makeTypedDatabase();
  FunctionalExpression expression;
  expression.id = 9000003;
  expression.owner = 33;
  expression.ownerKind = FunctionalExpressionOwnerKind::Bin;
  expression.role = FunctionalExpressionRole::BinWith;
  expression.resultKind = FunctionalExpressionResultKind::Boolean;
  expression.signedness = CoverageSignedness::NotApplicable;
  expression.evaluationPhase = FunctionalExpressionEvaluationPhase::Constructor;
  expression.resultOrdinal = 1;
  db.functionalExpressions.push_back(expression);

  Diagnostic diagnostic;
  EXPECT_EQ(validate(db, &diagnostic), Status::InvalidReference);
  EXPECT_STREQ(diagnostic.field, "functional_expression");
}

TEST(CoverageDatabaseTest, ValidatesStateBinSetExpressionElementType) {
  Diagnostic diagnostic;

  Database db = makeSetExpressionDatabase(16, CoverageSignedness::Signed);
  EXPECT_EQ(validate(db, &diagnostic), Status::Ok)
      << diagnostic.field << " record " << diagnostic.record << " "
      << diagnostic.detail;

  db = makeSetExpressionDatabase(4, CoverageSignedness::Unsigned,
                                 FunctionalExpressionSetElementFourState);
  EXPECT_EQ(validate(db, &diagnostic), Status::Ok)
      << diagnostic.field << " record " << diagnostic.record << " "
      << diagnostic.detail;

  db = makeSetExpressionDatabase(64, CoverageSignedness::NotApplicable);
  EXPECT_EQ(validate(db, &diagnostic), Status::Ok)
      << diagnostic.field << " record " << diagnostic.record << " "
      << diagnostic.detail;

  db = makeSetExpressionDatabase(0, CoverageSignedness::Signed);
  EXPECT_EQ(validate(db, &diagnostic), Status::InvalidReference);
  EXPECT_STREQ(diagnostic.field, "functional_expression");

  db = makeSetExpressionDatabase(32, CoverageSignedness::NotApplicable);
  EXPECT_EQ(validate(db, &diagnostic), Status::Ok)
      << diagnostic.field << " record " << diagnostic.record << " "
      << diagnostic.detail;

  db = makeSetExpressionDatabase(16, CoverageSignedness::NotApplicable);
  EXPECT_EQ(validate(db, &diagnostic), Status::InvalidReference);
  EXPECT_STREQ(diagnostic.field, "functional_expression");

  db = makeSetExpressionDatabase(64, CoverageSignedness::NotApplicable,
                                 FunctionalExpressionSetElementFourState);
  EXPECT_EQ(validate(db, &diagnostic), Status::InvalidReference);
  EXPECT_STREQ(diagnostic.field, "functional_expression");

  db = makeSetExpressionDatabase(4, CoverageSignedness::Unsigned, 2);
  EXPECT_EQ(validate(db, &diagnostic), Status::InvalidReference);
  EXPECT_STREQ(diagnostic.field, "functional_expression");
}

TEST(CoverageDatabaseTest, RejectsMaxWidthMissingOrUndersizedIntegralLimbs) {
  Diagnostic diagnostic;

  Database db = makeTypedDatabase();
  db.functionalValueSets.front().bitWidth = UINT32_MAX;
  EXPECT_EQ(validate(db, &diagnostic), Status::InvalidReference);
  EXPECT_STREQ(diagnostic.field, "functional_value_atom");

  db = makeTypedDatabase();
  db.resolvedFunctionalValueSets.back().bitWidth = UINT32_MAX;
  db.resolvedFunctionalValueAtoms.back().limbCount = 0;
  db.resolvedFunctionalValueLimbs.pop_back();
  EXPECT_EQ(validate(db, &diagnostic), Status::InvalidReference);
  // The resolved set must first agree with its static template. This mismatch
  // is therefore rejected before the atom's independently malformed limb
  // count is inspected.
  EXPECT_STREQ(diagnostic.field, "resolved_functional_value_set");

  db = makeTypedDatabase();
  db.resolvedFunctionalValueSets.back().bitWidth = UINT32_MAX;
  EXPECT_EQ(validate(db, &diagnostic), Status::InvalidReference);
  EXPECT_STREQ(diagnostic.field, "resolved_functional_value_set");
}

TEST(CoverageDatabaseTest, RejectsSizedTransitionArrays) {
  Database db = makeTypedDatabase();
  db.functionalBinPlans[1].arrayMode = FunctionalBinArrayMode::Fixed;
  db.functionalBinPlans[1].arrayCardinality = 2;
  db.functionalBinPlans[1].distribution =
      FunctionalBinDistributionKind::Uniform;
  EXPECT_EQ(validate(db), Status::InvalidReference);
}

TEST(CoverageDatabaseTest, AcceptsBoundedUnrepeatedTransitionArrays) {
  Database db = makeBoundedTransitionArrayDatabase();
  ASSERT_EQ(validate(db), Status::Ok);
  std::vector<uint8_t> image;
  ASSERT_EQ(serialize(db, image), Status::Ok);
  Database parsed;
  ASSERT_EQ(parse(image.data(), image.size(), parsed), Status::Ok);
  ASSERT_EQ(parsed.functionalBinPlans.size(), 1u);
  EXPECT_EQ(parsed.functionalBinPlans.front().arrayMode,
            FunctionalBinArrayMode::Unsized);
  EXPECT_EQ(parsed.functionalBinPlans.front().distribution,
            FunctionalBinDistributionKind::PerValue);
}

TEST(CoverageDatabaseTest, AcceptsBoundedConsecutiveTransitionArrays) {
  Database db = makeBoundedTransitionArrayDatabase();
  auto &step = db.transitionSteps.front();
  step.repetition = TransitionRepetitionKind::Consecutive;
  step.lowerBound = 2;
  step.upperBound = 3;
  ASSERT_EQ(validate(db), Status::Ok);
  std::vector<uint8_t> image;
  ASSERT_EQ(serialize(db, image), Status::Ok);
  Database parsed;
  ASSERT_EQ(parse(image.data(), image.size(), parsed), Status::Ok);
  EXPECT_EQ(parsed.transitionSteps.front().repetition,
            TransitionRepetitionKind::Consecutive);
  EXPECT_EQ(parsed.transitionSteps.front().lowerBound, 2u);
  EXPECT_EQ(parsed.transitionSteps.front().upperBound, 3u);
}

TEST(CoverageDatabaseTest, RejectsVariableLengthTransitionArrays) {
  for (TransitionRepetitionKind repetition :
       {TransitionRepetitionKind::Goto,
        TransitionRepetitionKind::Nonconsecutive}) {
    Database db = makeBoundedTransitionArrayDatabase();
    auto &step = db.transitionSteps.front();
    step.repetition = repetition;
    step.lowerBound = 2;
    step.upperBound = 2;
    Diagnostic diagnostic;
    EXPECT_EQ(validate(db, &diagnostic), Status::InvalidReference)
        << static_cast<uint32_t>(repetition);
    EXPECT_STREQ(diagnostic.field, "transition_step.array_repetition");
  }
}

TEST(CoverageDatabaseTest, AcceptsStaticAutomaticEnumBin) {
  Database db = makeStaticAutomaticBinDatabase();
  ASSERT_EQ(validate(db), Status::Ok);
  std::vector<uint8_t> image;
  ASSERT_EQ(serialize(db, image), Status::Ok);
  Database parsed;
  ASSERT_EQ(parse(image.data(), image.size(), parsed), Status::Ok);
  ASSERT_EQ(parsed.functionalBins.size(), 1u);
  EXPECT_EQ(parsed.functionalBins.front().flags, FunctionalBinAutomatic);
  EXPECT_EQ(parsed.functionalBins.front().name, "auto[RED]");
}

TEST(CoverageDatabaseTest, RejectsInvalidStaticAutomaticBinFlags) {
  Database db = makeTypedDatabase();
  db.functionalBins.front().flags |= FunctionalBinAutomatic;
  EXPECT_EQ(validate(db), Status::InvalidReference);

  db = makeStaticAutomaticBinDatabase();
  db.functionalBins.front().flags |= FunctionalBinIgnore;
  EXPECT_EQ(validate(db), Status::InvalidReference);

  db = makeStaticAutomaticBinDatabase();
  db.functionalBins.front().kind = FunctionalBinKind::Transition;
  EXPECT_EQ(validate(db), Status::InvalidReference);
}

TEST(CoverageDatabaseTest, PhysicalFailurePreservesParseAndSerializeOutputs) {
  Database db = makeTypedDatabase();
  std::vector<uint8_t> image;
  ASSERT_EQ(serialize(db, image), Status::Ok);
  size_t sets = sectionOffset(image, SectionKind::ResolvedFunctionalValueSets);
  ASSERT_NE(sets, std::string::npos);
  put32(image, sets + 112, 99);
  refreshChecksum(image);

  Database output = makeTypedDatabase();
  const size_t originalRunCount = output.runs.size();
  Diagnostic diagnostic;
  EXPECT_EQ(parse(image.data(), image.size(), output, {}, &diagnostic),
            Status::InvalidEnum);
  EXPECT_EQ(output.runs.size(), originalRunCount);
  EXPECT_EQ(output.producer, "coverage-database-test");
  EXPECT_EQ(diagnostic.section, SectionKind::ResolvedFunctionalValueSets);
  EXPECT_STREQ(diagnostic.field, "role");

  std::vector<uint8_t> serialized{1, 2, 3};
  db.functionalConfigurations.front().configuration[0] ^= 1;
  EXPECT_EQ(serialize(db, serialized), Status::InvalidReference);
  EXPECT_EQ(serialized, (std::vector<uint8_t>{1, 2, 3}));
}

TEST(CoverageDatabaseTest, RejectsMalformedRangesDagsAndTuplePayloads) {
  Database db = makeTypedDatabase();
  std::vector<uint8_t> image;
  ASSERT_EQ(serialize(db, image), Status::Ok);
  size_t alternatives =
      sectionOffset(image, SectionKind::ResolvedTransitionAlternatives);
  ASSERT_NE(alternatives, std::string::npos);
  put32(image, alternatives + 64, std::numeric_limits<uint32_t>::max());
  refreshChecksum(image);
  Database parsed;
  Diagnostic diagnostic;
  EXPECT_EQ(parse(image.data(), image.size(), parsed, {}, &diagnostic),
            Status::InvalidReference);
  EXPECT_STREQ(diagnostic.field, "first_step");

  ASSERT_EQ(serialize(db, image), Status::Ok);
  size_t tupleSets =
      sectionOffset(image, SectionKind::ResolvedFunctionalTupleSets);
  ASSERT_NE(tupleSets, std::string::npos);
  put32(image, tupleSets + 72, std::numeric_limits<uint32_t>::max());
  refreshChecksum(image);
  EXPECT_EQ(parse(image.data(), image.size(), parsed, {}, &diagnostic),
            Status::InvalidReference);
  EXPECT_EQ(diagnostic.section, SectionKind::ResolvedFunctionalTupleSets);
  EXPECT_STREQ(diagnostic.field, "first_tuple");

  db = makeTypedDatabase();
  db.crossSelectorOperands.front().operand = 504;
  EXPECT_EQ(validate(db), Status::InvalidReference);
  db = makeTypedDatabase();
  db.resolvedFunctionalTupleSetComponents.front().valueSet = 404;
  EXPECT_EQ(validate(db), Status::InvalidReference);

  db = makeTypedDatabase();
  db.resolvedCrossAutomaticBinCountLimbs.front() = 2;
  EXPECT_EQ(validate(db, &diagnostic), Status::InvalidReference);
  EXPECT_STREQ(diagnostic.field, "resolved_cross_automatic_node.path_count");

  db = makeTypedDatabase();
  db.resolvedCrossAutomaticNodes[1].targetOrdinal = 0;
  EXPECT_EQ(validate(db, &diagnostic), Status::InvalidReference);
  EXPECT_STREQ(diagnostic.field, "resolved_cross_automatic_edge");

  db = makeTypedDatabase();
  db.resolvedCrossAutomaticNodes.push_back(
      {10, db.functionalConfigurations.front().configuration, 203, 902, 1, 2, 1,
       0});
  db.resolvedCrossAutomaticEdges.push_back({902, 302, 0, 0, 0});
  EXPECT_EQ(validate(db, &diagnostic), Status::InvalidReference);
  EXPECT_STREQ(diagnostic.field, "resolved_cross_automatic_node.not_reduced");

  db = makeTypedDatabase();
  db.resolvedCrossAutomaticEdges.front().child = 0;
  EXPECT_EQ(validate(db, &diagnostic), Status::InvalidReference);
  EXPECT_STREQ(diagnostic.field, "resolved_cross_automatic_edge");

  db = makeTypedDatabase();
  db.crossPlans.front().tupleProvenanceSpan = UINT64_MAX - 7;
  EXPECT_EQ(validate(db, &diagnostic), Status::InvalidReference);
  EXPECT_STREQ(diagnostic.field, "cross_plan");
}

TEST(CoverageDatabaseTest, RejectsMalformedResolvedGroupFields) {
  Database db = makeTypedDatabase();
  std::vector<uint8_t> image;
  ASSERT_EQ(serialize(db, image), Status::Ok);
  size_t binGroups =
      sectionOffset(image, SectionKind::ResolvedFunctionalBinGroups);
  ASSERT_NE(binGroups, std::string::npos);
  put32(image, binGroups + 84, 99);
  refreshChecksum(image);
  Database parsed;
  Diagnostic diagnostic;
  EXPECT_EQ(parse(image.data(), image.size(), parsed, {}, &diagnostic),
            Status::InvalidEnum);
  EXPECT_EQ(diagnostic.section, SectionKind::ResolvedFunctionalBinGroups);
  EXPECT_STREQ(diagnostic.field, "kind");

  image.clear();
  ASSERT_EQ(serialize(db, image), Status::Ok);
  size_t expansionGroups =
      sectionOffset(image, SectionKind::ResolvedTransitionExpansionGroups);
  ASSERT_NE(expansionGroups, std::string::npos);
  put32(image, expansionGroups + 60, std::numeric_limits<uint32_t>::max());
  refreshChecksum(image);
  EXPECT_EQ(parse(image.data(), image.size(), parsed, {}, &diagnostic),
            Status::InvalidReference);
  EXPECT_EQ(diagnostic.section, SectionKind::ResolvedTransitionExpansionGroups);
  EXPECT_STREQ(diagnostic.field, "first_alternative");
}

TEST(CoverageDatabaseTest, RejectsSingleElementTransitionForEveryRepeatKind) {
  for (TransitionRepetitionKind repetition :
       {TransitionRepetitionKind::Once, TransitionRepetitionKind::Consecutive,
        TransitionRepetitionKind::Goto,
        TransitionRepetitionKind::Nonconsecutive}) {
    Database db = makeSingleStepTransition(repetition, 1, 1);
    Diagnostic diagnostic;
    EXPECT_EQ(validate(db, &diagnostic), Status::InvalidReference)
        << static_cast<uint32_t>(repetition);
    EXPECT_STREQ(diagnostic.field, "transition_alternative.length");
  }
}

TEST(CoverageDatabaseTest, AcceptsFiniteTransitionRangesLongerThanOne) {
  for (TransitionRepetitionKind repetition :
       {TransitionRepetitionKind::Consecutive, TransitionRepetitionKind::Goto,
        TransitionRepetitionKind::Nonconsecutive}) {
    EXPECT_EQ(validate(makeSingleStepTransition(repetition, 1, 2)), Status::Ok)
        << static_cast<uint32_t>(repetition);
    EXPECT_EQ(validate(makeSingleStepTransition(repetition, 2, 2)), Status::Ok)
        << static_cast<uint32_t>(repetition);
  }
}

TEST(CoverageDatabaseTest, RejectsNoncanonicalDefaultSequenceFlags) {
  Database valid = makeDefaultSequenceDatabase();
  ASSERT_EQ(validate(valid), Status::Ok);

  for (uint32_t extra :
       {FunctionalBinDefault, FunctionalBinIgnore, FunctionalBinIllegal,
        FunctionalBinWildcard, FunctionalBinAutomatic, FunctionalBinEmpty}) {
    Database db = valid;
    db.functionalBins.front().flags |= extra;
    Diagnostic diagnostic;
    EXPECT_EQ(validate(db, &diagnostic), Status::InvalidReference) << extra;
    EXPECT_STREQ(diagnostic.field, "functional_bin") << extra;
  }

  Database db = valid;
  db.resolvedFunctionalBins.front().flags |= FunctionalBinEmpty;
  Diagnostic diagnostic;
  EXPECT_EQ(validate(db, &diagnostic), Status::InvalidReference);
  EXPECT_STREQ(diagnostic.field, "resolved_functional_bin");
}

TEST(CoverageDatabaseTest, RejectsNonScalarDefaultSequencePlans) {
  Database db = makeDefaultSequenceDatabase();
  ASSERT_EQ(validate(db), Status::Ok);

  db.functionalBinPlans.front().arrayMode = FunctionalBinArrayMode::Unsized;
  db.functionalBinPlans.front().distribution =
      FunctionalBinDistributionKind::PerValue;
  Diagnostic diagnostic;
  EXPECT_EQ(validate(db, &diagnostic), Status::InvalidReference);
  EXPECT_STREQ(diagnostic.field, "functional_bin_plan");
}

TEST(CoverageDatabaseTest, RejectsMalformedDefaultSequenceImages) {
  Database db = makeDefaultSequenceDatabase();
  std::vector<uint8_t> image;
  ASSERT_EQ(serialize(db, image), Status::Ok);

  size_t bins = sectionOffset(image, SectionKind::FunctionalBins);
  ASSERT_NE(bins, std::string::npos);
  put32(image, bins + 24, FunctionalBinDefaultSequence | FunctionalBinWildcard);
  refreshChecksum(image);
  Database parsed;
  Diagnostic diagnostic;
  EXPECT_EQ(parse(image.data(), image.size(), parsed, {}, &diagnostic),
            Status::InvalidReference);
  EXPECT_STREQ(diagnostic.field, "functional_bin");

  ASSERT_EQ(serialize(db, image), Status::Ok);
  size_t plans = sectionOffset(image, SectionKind::FunctionalBinPlans);
  ASSERT_NE(plans, std::string::npos);
  put32(image, plans + 40,
        static_cast<uint32_t>(FunctionalBinArrayMode::Unsized));
  put32(image, plans + 44,
        static_cast<uint32_t>(FunctionalBinDistributionKind::PerValue));
  refreshChecksum(image);
  EXPECT_EQ(parse(image.data(), image.size(), parsed, {}, &diagnostic),
            Status::InvalidReference);
  EXPECT_STREQ(diagnostic.field, "functional_bin_plan");
}

TEST(CoverageDatabaseTest, PreservesExplicitZeroCardinalityGroups) {
  Database db = makeZeroCardinalityDefaultArray();
  ASSERT_EQ(validate(db), Status::Ok);
  std::vector<uint8_t> image;
  ASSERT_EQ(serialize(db, image), Status::Ok);
  Database parsed;
  ASSERT_EQ(parse(image.data(), image.size(), parsed), Status::Ok);
  ASSERT_EQ(parsed.resolvedFunctionalBinGroups.size(), 1u);
  EXPECT_EQ(parsed.resolvedFunctionalBinGroups.front().binCount, 0u);
  EXPECT_EQ(parsed.resolvedFunctionalBinGroups.front().firstBin, 0u);

  db.resolvedFunctionalBinGroups.clear();
  EXPECT_EQ(validate(db), Status::InvalidReference);
}

TEST(CoverageDatabaseTest, RequiresExplicitGeneratedAutomaticBinGroup) {
  Database db;
  db.scopes.push_back({1, 0, "top", 0, "top"});
  db.functionalTypes.push_back({10, 1, "cg", 0, 2017, "top.cg"});
  db.functionalItems.push_back({20, 10, "cp", FunctionalItemKind::Coverpoint, 0,
                                100, 1, 0, "top.cg.cp"});
  db.functionalConfigurations.push_back({10, {}, 0});
  db.resolvedFunctionalItems.push_back({10,
                                        {},
                                        200,
                                        20,
                                        "cp",
                                        FunctionalItemKind::Coverpoint,
                                        0,
                                        100,
                                        1,
                                        0,
                                        "top.cg.cp"});
  db.resolvedFunctionalBinGroups.push_back(
      {10,
       {},
       200,
       0,
       0,
       0,
       0,
       FunctionalBinArrayMode::Fixed,
       FunctionalBinDistributionKind::Uniform,
       0,
       FunctionalBinKind::State});
  Digest configuration =
      computeFunctionalConfigurationFingerprint(db, 10, Digest{});
  db.functionalConfigurations.front().configuration = configuration;
  setConfiguration(db.resolvedFunctionalItems, configuration);
  setConfiguration(db.resolvedFunctionalBinGroups, configuration);
  ASSERT_EQ(validate(db), Status::Ok);

  db.resolvedFunctionalBinGroups.clear();
  EXPECT_EQ(validate(db), Status::InvalidReference);
}

TEST(CoverageDatabaseTest, IgnoreOnlyCoverpointRetainsAutomaticBinGroup) {
  Database db;
  db.scopes.push_back({1, 0, "top", 0, "top"});
  db.functionalTypes.push_back({10, 1, "cg", 0, 2017, "top.cg"});
  db.functionalItems.push_back({20, 10, "cp", FunctionalItemKind::Coverpoint, 0,
                                100, 1, 0, "top.cg.cp"});
  db.functionalBins.push_back({30, 20, "ignored", FunctionalBinKind::State,
                               FunctionalBinIgnore, 1, 0, "top.cg.cp.ignored"});
  db.functionalValueSets.push_back(integralSet(100, 20, 0, 8));
  db.functionalValueAtoms.push_back(integralAtom(100, 0, 1));
  db.functionalValueLimbs.push_back(integralLimb(100, 0, 0, 1));
  db.functionalBinPlans.push_back({30, 100});
  db.functionalConfigurations.push_back({10, {}, 0});
  db.resolvedFunctionalItems.push_back({10,
                                        {},
                                        200,
                                        20,
                                        "cp",
                                        FunctionalItemKind::Coverpoint,
                                        0,
                                        100,
                                        1,
                                        0,
                                        "top.cg.cp"});
  db.resolvedFunctionalBins.push_back({10,
                                       {},
                                       300,
                                       30,
                                       200,
                                       "ignored",
                                       FunctionalBinKind::State,
                                       FunctionalBinIgnore,
                                       0,
                                       0,
                                       1,
                                       "top.cg.cp.ignored"});
  db.resolvedFunctionalValueSets.push_back(
      resolvedSet(400, 100, 200, 0, 8, 300, 0, 0, 0,
                  ResolvedFunctionalValueSetRole::StateBin));
  db.resolvedFunctionalValueAtoms.push_back(resolvedAtom(400, 0, 1));
  db.resolvedFunctionalValueLimbs.push_back(resolvedLimb(400, 0, 1));
  db.resolvedFunctionalBinPlans.push_back({10, {}, 300, 400});
  db.resolvedFunctionalBinGroups.push_back({10,
                                            {},
                                            200,
                                            30,
                                            0,
                                            1,
                                            0,
                                            FunctionalBinArrayMode::Scalar,
                                            FunctionalBinDistributionKind::None,
                                            0,
                                            FunctionalBinKind::State});
  db.resolvedFunctionalBinGroups.push_back(
      {10,
       {},
       200,
       0,
       1,
       0,
       0,
       FunctionalBinArrayMode::Fixed,
       FunctionalBinDistributionKind::Uniform,
       0,
       FunctionalBinKind::State});
  Digest configuration =
      computeFunctionalConfigurationFingerprint(db, 10, Digest{});
  db.functionalConfigurations.front().configuration = configuration;
  setConfiguration(db.resolvedFunctionalItems, configuration);
  setConfiguration(db.resolvedFunctionalBins, configuration);
  setConfiguration(db.resolvedFunctionalValueSets, configuration);
  setConfiguration(db.resolvedFunctionalBinPlans, configuration);
  setConfiguration(db.resolvedFunctionalBinGroups, configuration);
  ASSERT_EQ(validate(db), Status::Ok);

  db.resolvedFunctionalBinGroups.pop_back();
  EXPECT_EQ(validate(db), Status::InvalidReference);
}

TEST(CoverageDatabaseTest, RejectsNoncanonicalWildcardAndEmptyDiagnostics) {
  Database db = makeTypedDatabase();
  db.functionalValueLimbs.front().lowAval |=
      db.functionalValueLimbs.front().wildcardMask;
  EXPECT_EQ(validate(db), Status::InvalidReference);
  db = makeTypedDatabase();
  db.illegalBinDiagnostics.front().count = 0;
  EXPECT_EQ(validate(db), Status::InvalidReference);
}

TEST(CoverageDatabaseTest, RejectsOpenNumericallyZeroRealInterval) {
  Database db =
      makeSingleStepTransition(TransitionRepetitionKind::Consecutive, 2, 2);
  auto &set = db.functionalValueSets.front();
  set.bitWidth = 64;
  set.kind = FunctionalValueSetKind::Real;
  set.signedness = CoverageSignedness::NotApplicable;
  auto &atom = db.functionalValueAtoms.front();
  atom.kind = FunctionalValueAtomKind::RealInterval;
  atom.firstLimb = 0;
  atom.limbCount = 0;
  atom.realLowBits = UINT64_C(1) << 63;
  atom.realHighBits = 0;
  atom.flags = FunctionalValueAtomLowerInclusive;
  db.functionalValueLimbs.clear();
  EXPECT_EQ(validate(db), Status::InvalidReference);
}

TEST(CoverageDatabaseTest, RejectsRealTemplateValueSetsBefore2023) {
  Database db = makeRealTemplateDatabase();
  ASSERT_EQ(validate(db), Status::Ok);

  std::vector<uint8_t> image;
  ASSERT_EQ(serialize(db, image), Status::Ok);
  const size_t types = sectionOffset(image, SectionKind::FunctionalTypes);
  ASSERT_NE(types, std::string::npos);
  put32(image, types + 24, 2017);
  refreshChecksum(image);
  Database parsed;
  EXPECT_EQ(parse(image.data(), image.size(), parsed),
            Status::InvalidReference);
}

TEST(CoverageDatabaseTest, RejectsResolvedRealValueSetsBefore2023) {
  Database db = makeGeneratedResolvedRealDatabase();
  Diagnostic diagnostic;
  Status status = validate(db, &diagnostic);
  ASSERT_EQ(status, Status::Ok)
      << (diagnostic.field ? diagnostic.field : "<no field>") << ": "
      << diagnostic.detail;
  db.functionalTypes.front().languageVersion = 2017;
  EXPECT_EQ(validate(db), Status::InvalidReference);
}

TEST(CoverageDatabaseTest, CanonicalizesUnboundedRealTemplateEndpoints) {
  Database db = makeRealTemplateDatabase();
  auto &set = db.functionalValueSets.front();
  auto &atom = db.functionalValueAtoms.front();
  set.flags = FunctionalValueSetNeedsResolution;
  atom.realLowBits = 0;
  atom.realHighBits = 0;
  atom.flags = FunctionalValueAtomLowerInclusive |
               FunctionalValueAtomUpperInclusive |
               FunctionalValueAtomLowerUnbounded | FunctionalValueAtomRealRange;
  atom.upperExpression = 900;
  FunctionalExpression upper;
  upper.id = 900;
  upper.owner = set.id;
  upper.ownerKind = FunctionalExpressionOwnerKind::ValueSet;
  upper.role = FunctionalExpressionRole::ValueAtomUpper;
  upper.resultKind = FunctionalExpressionResultKind::Real;
  upper.signedness = CoverageSignedness::NotApplicable;
  upper.evaluationPhase = FunctionalExpressionEvaluationPhase::Constructor;
  db.functionalExpressions.push_back(upper);
  ASSERT_EQ(validate(db), Status::Ok);

  atom.flags &= ~FunctionalValueAtomLowerInclusive;
  Diagnostic diagnostic;
  EXPECT_EQ(validate(db, &diagnostic), Status::InvalidReference);
  EXPECT_STREQ(diagnostic.field, "functional_value_atom");
  atom.flags |= FunctionalValueAtomLowerInclusive;
  atom.flags |= FunctionalValueAtomUpperUnbounded;
  atom.upperExpression = 0;
  db.functionalExpressions.clear();
  EXPECT_EQ(validate(db, &diagnostic), Status::InvalidReference);
  EXPECT_STREQ(diagnostic.field, "functional_value_atom");
}

TEST(CoverageDatabaseTest, CanonicalizesUnboundedIntegralTemplateEndpoints) {
  Database db =
      makeSingleStepTransition(TransitionRepetitionKind::Consecutive, 2, 2);
  auto &set = db.functionalValueSets.front();
  auto &atom = db.functionalValueAtoms.front();
  set.flags = FunctionalValueSetNeedsResolution;
  atom.kind = FunctionalValueAtomKind::IntegralRange;
  atom.firstLimb = 0;
  atom.limbCount = 0;
  atom.flags = FunctionalValueAtomLowerInclusive |
               FunctionalValueAtomUpperInclusive |
               FunctionalValueAtomLowerUnbounded;
  atom.upperExpression = 900;
  db.functionalValueLimbs.clear();
  FunctionalExpression endpoint;
  endpoint.id = 900;
  endpoint.owner = set.id;
  endpoint.ownerKind = FunctionalExpressionOwnerKind::ValueSet;
  endpoint.role = FunctionalExpressionRole::ValueAtomUpper;
  endpoint.resultKind = FunctionalExpressionResultKind::Integral;
  endpoint.bitWidth = 8;
  endpoint.signedness = CoverageSignedness::Signed;
  endpoint.evaluationPhase = FunctionalExpressionEvaluationPhase::Constructor;
  db.functionalExpressions.push_back(endpoint);
  ASSERT_EQ(validate(db), Status::Ok);

  atom.flags = FunctionalValueAtomLowerInclusive |
               FunctionalValueAtomUpperInclusive |
               FunctionalValueAtomUpperUnbounded;
  atom.lowerExpression = 900;
  atom.upperExpression = 0;
  db.functionalExpressions.front().role =
      FunctionalExpressionRole::ValueAtomLower;
  ASSERT_EQ(validate(db), Status::Ok);

  Diagnostic diagnostic;
  atom.flags |= FunctionalValueAtomLowerUnbounded;
  EXPECT_EQ(validate(db, &diagnostic), Status::InvalidReference);
  EXPECT_STREQ(diagnostic.field, "functional_value_atom");
  atom.flags &= ~FunctionalValueAtomLowerUnbounded;
  atom.kind = FunctionalValueAtomKind::IntegralValue;
  EXPECT_EQ(validate(db, &diagnostic), Status::InvalidReference);
  EXPECT_STREQ(diagnostic.field, "functional_value_atom");
  atom.kind = FunctionalValueAtomKind::IntegralRange;
  atom.flags &= ~FunctionalValueAtomUpperInclusive;
  EXPECT_EQ(validate(db, &diagnostic), Status::InvalidReference);
  EXPECT_STREQ(diagnostic.field, "functional_value_atom");
  atom.flags |= FunctionalValueAtomUpperInclusive;
  atom.lowerExpression = 0;
  db.functionalExpressions.clear();
  EXPECT_EQ(validate(db, &diagnostic), Status::InvalidReference);
  EXPECT_STREQ(diagnostic.field, "functional_value_atom.expression");
}

TEST(CoverageDatabaseTest, RejectsUnboundedResolvedIntegralEndpoint) {
  Database db = makeTypedDatabase();
  ASSERT_EQ(validate(db), Status::Ok);
  db.resolvedFunctionalValueAtoms.front().flags |=
      FunctionalValueAtomLowerUnbounded;
  Diagnostic diagnostic;
  EXPECT_EQ(validate(db, &diagnostic), Status::InvalidReference);
  EXPECT_STREQ(diagnostic.field, "resolved_functional_value_atom");
}

TEST(CoverageDatabaseTest, CanonicalizesUnboundedResolvedRealEndpoints) {
  Database db = makeGeneratedResolvedRealDatabase();
  auto &atom = db.resolvedFunctionalValueAtoms.front();
  atom.realHighBits = 0;
  atom.flags = FunctionalValueAtomLowerInclusive |
               FunctionalValueAtomUpperInclusive |
               FunctionalValueAtomUpperUnbounded | FunctionalValueAtomRealRange;
  rekeyOnlyConfiguration(db);
  db.schemaFingerprint = computeSchemaFingerprint(db);
  ASSERT_EQ(validate(db), Status::Ok);

  atom.flags &= ~FunctionalValueAtomUpperInclusive;
  Diagnostic diagnostic;
  EXPECT_EQ(validate(db, &diagnostic), Status::InvalidReference);
  EXPECT_STREQ(diagnostic.field, "resolved_functional_value_atom");
  atom.flags |=
      FunctionalValueAtomUpperInclusive | FunctionalValueAtomLowerUnbounded;
  atom.realLowBits = 0;
  EXPECT_EQ(validate(db, &diagnostic), Status::InvalidReference);
  EXPECT_STREQ(diagnostic.field, "resolved_functional_value_atom");
}

TEST(CoverageDatabaseTest, ConfigurationDigestIgnoresResolvedIDs) {
  Database first = makeTypedDatabase();
  Database second = first;
  permuteResolvedItemAndBinIDs(second);
  const Digest configuration =
      first.functionalConfigurations.front().configuration;
  EXPECT_EQ(
      computeFunctionalConfigurationFingerprint(first, 10, configuration),
      computeFunctionalConfigurationFingerprint(second, 10, configuration));
  ASSERT_EQ(validate(second), Status::Ok);
  second.resolvedTransitionExpansionGroups.front().expansionCount = 2;
  EXPECT_NE(
      computeFunctionalConfigurationFingerprint(first, 10, configuration),
      computeFunctionalConfigurationFingerprint(second, 10, configuration));
}

TEST(CoverageDatabaseTest, RejectsNoncanonicalResolvedValueSetOrder) {
  Database db = makeTypedDatabase();
  std::swap(db.resolvedFunctionalValueSets[4],
            db.resolvedFunctionalValueSets[5]);
  EXPECT_EQ(validate(db), Status::InvalidReference);
}

TEST(CoverageDatabaseTest, WildcardSetIdentityIsConfigurationScoped) {
  Database first = makeTypedDatabase();
  Database second = first;
  assignSecondRun(second);
  auto swapSet = [](uint64_t &id) {
    if (id == 400)
      id = 403;
    else if (id == 403)
      id = 400;
  };
  for (auto &set : second.resolvedFunctionalValueSets)
    swapSet(set.id);
  for (auto &atom : second.resolvedFunctionalValueAtoms)
    swapSet(atom.valueSet);
  for (auto &limb : second.resolvedFunctionalValueLimbs)
    swapSet(limb.valueSet);
  for (auto &plan : second.resolvedFunctionalBinPlans)
    swapSet(plan.valueSet);
  for (auto &step : second.resolvedTransitionSteps)
    swapSet(step.valueSet);
  for (auto &binding : second.resolvedCrossSelectorBindings)
    swapSet(binding.valueSet);
  for (auto &component : second.resolvedFunctionalTupleSetComponents)
    swapSet(component.valueSet);
  canonicalizeResolvedValueSets(second);
  auto normal = std::find_if(second.resolvedFunctionalValueSets.begin(),
                             second.resolvedFunctionalValueSets.end(),
                             [](const auto &set) { return set.id == 400; });
  ASSERT_NE(normal, second.resolvedFunctionalValueSets.end());
  auto &normalAtom = second.resolvedFunctionalValueAtoms[normal->firstAtom];
  auto &normalLimb = second.resolvedFunctionalValueLimbs[normalAtom.firstLimb];
  normalLimb.lowAval = normalLimb.highAval = 5;
  rekeyOnlyConfiguration(second);
  ASSERT_NE(first.functionalConfigurations.front().configuration,
            second.functionalConfigurations.front().configuration);
  ASSERT_EQ(validate(second), Status::Ok);
  ASSERT_EQ(merge(first, second), Status::Ok);
  EXPECT_EQ(validate(first), Status::Ok);
}

TEST(CoverageDatabaseTest, MergeRemapsEquivalentResolvedIDsTransactionally) {
  Database original = makeTypedDatabase();
  Database second = original;
  assignSecondRun(second);
  permuteResolvedItemAndBinIDs(second);
  ASSERT_EQ(validate(second), Status::Ok);
  Database forward = original;
  Database reverse = second;
  ASSERT_EQ(merge(forward, second), Status::Ok);
  ASSERT_EQ(merge(reverse, original), Status::Ok);
  std::vector<uint8_t> forwardImage;
  std::vector<uint8_t> reverseImage;
  ASSERT_EQ(serialize(forward, forwardImage), Status::Ok);
  ASSERT_EQ(serialize(reverse, reverseImage), Status::Ok);
  EXPECT_EQ(forwardImage, reverseImage);

  ASSERT_EQ(forward.runs.size(), 2u);
  ASSERT_EQ(forward.counters.size(), 2u);
  EXPECT_EQ(forward.counters[0].entity, forward.counters[1].entity);
  ASSERT_EQ(forward.sparseCrossTuples.size(), 2u);
  ASSERT_EQ(forward.sparseCrossTupleComponents.size(), 4u);
  EXPECT_EQ(
      std::vector<uint64_t>(forward.sparseCrossTupleComponents.begin(),
                            forward.sparseCrossTupleComponents.begin() + 2),
      std::vector<uint64_t>(forward.sparseCrossTupleComponents.begin() + 2,
                            forward.sparseCrossTupleComponents.end()));
  ASSERT_EQ(forward.illegalBinDiagnostics.size(), 2u);
  EXPECT_EQ(forward.illegalBinDiagnostics[0].bin,
            forward.illegalBinDiagnostics[1].bin);

  Database unchanged = forward;
  Database duplicate = second;
  EXPECT_EQ(merge(forward, duplicate), Status::DuplicateRun);
  std::vector<uint8_t> before;
  std::vector<uint8_t> after;
  ASSERT_EQ(serialize(unchanged, before), Status::Ok);
  ASSERT_EQ(serialize(forward, after), Status::Ok);
  EXPECT_EQ(before, after);
}

TEST(CoverageDatabaseTest, MergeRejectsDuplicateRunAcrossDisjointMetrics) {
  Database destination = makeTypedDatabase();
  SourceFile source;
  source.id = 2;
  source.path = "dut.sv";
  destination.sourceFiles.push_back(source);
  destination.linePoints.push_back({40, 2, 2, 1, "", 1, 1, 1, 2, 0, 0});
  destination.schemaFingerprint = computeSchemaFingerprint(destination);

  Database incoming = destination;
  incoming.runs.front().flags = RunContainsLine;
  incoming.resolvedInstances.clear();
  incoming.counters.clear();
  incoming.sparseCrossTuples.clear();
  incoming.sparseCrossTupleComponents.clear();
  incoming.illegalBinDiagnostics.clear();
  incoming.counters.push_back(
      {incoming.runs.front().uuid, MetricKind::Line, 40, 0, 0, 0, 1});
  ASSERT_EQ(validate(destination), Status::Ok);
  ASSERT_EQ(validate(incoming), Status::Ok);

  std::vector<uint8_t> before;
  ASSERT_EQ(serialize(destination, before), Status::Ok);
  Diagnostic diagnostic;
  EXPECT_EQ(merge(destination, incoming, false, &diagnostic),
            Status::DuplicateRun);
  EXPECT_STREQ(diagnostic.field, "uuid");
  std::vector<uint8_t> after;
  ASSERT_EQ(serialize(destination, after), Status::Ok);
  EXPECT_EQ(before, after);
}

TEST(CoverageDatabaseTest, RejectsConflictingStaticTypeOptionsAcrossConfigs) {
  Database first = makeTypedDatabase();
  Database second = first;
  assignSecondRun(second);
  ASSERT_EQ(second.functionalConfigurationOptions.size(), 1u);
  second.functionalConfigurationOptions.front().stringValue = "other";
  rekeyOnlyConfiguration(second);
  ASSERT_EQ(validate(second), Status::Ok);

  std::vector<uint8_t> before;
  ASSERT_EQ(serialize(first, before), Status::Ok);
  Diagnostic diagnostic;
  EXPECT_EQ(merge(first, second, false, &diagnostic), Status::InvalidReference);
  EXPECT_STREQ(diagnostic.field,
               "functional_configuration_option.type_consistency");
  std::vector<uint8_t> after;
  ASSERT_EQ(serialize(first, after), Status::Ok);
  EXPECT_EQ(before, after);
}

TEST(CoverageDatabaseTest, DiscardDetailRemapsAndSaturatesRunPayloads) {
  Database first = makeTypedDatabase();
  ResolvedInstanceOption typeGoal;
  typeGoal.run = first.runs.front().uuid;
  typeGoal.owner = first.functionalTypes.front().id;
  typeGoal.ownerKind = FunctionalConfigurationOptionOwnerKind::Group;
  typeGoal.option = FunctionalConfigurationOptionKind::Goal;
  typeGoal.valueKind = FunctionalConfigurationValueKind::Unsigned;
  typeGoal.value = 80;
  first.resolvedInstanceOptions.push_back(typeGoal);
  Database second = first;
  assignSecondRun(second);
  first.counters.front().value = std::numeric_limits<uint64_t>::max();
  second.counters.front().value = 1;
  first.sparseCrossTuples.front().hits = std::numeric_limits<uint64_t>::max();
  second.sparseCrossTuples.front().hits = 1;
  first.illegalBinDiagnostics.front().count =
      std::numeric_limits<uint64_t>::max();
  second.illegalBinDiagnostics.front().count = 1;

  ASSERT_EQ(merge(first, second, true), Status::Ok);
  ASSERT_EQ(first.runs.size(), 1u);
  ASSERT_EQ(first.resolvedInstances.size(), 1u);
  ASSERT_EQ(first.resolvedInstanceOptions.size(), 1u);
  EXPECT_EQ(first.resolvedInstanceOptions.front().instance, 0u);
  EXPECT_EQ(first.resolvedInstanceOptions.front().value, 80u);
  EXPECT_EQ(first.resolvedInstanceOptions.front().run, first.runs.front().uuid);
  ASSERT_EQ(first.counters.size(), 1u);
  EXPECT_EQ(first.counters.front().value, std::numeric_limits<uint64_t>::max());
  EXPECT_NE(first.counters.front().flags & 1u, 0u);
  ASSERT_EQ(first.sparseCrossTuples.size(), 1u);
  EXPECT_EQ(first.sparseCrossTuples.front().hits,
            std::numeric_limits<uint64_t>::max());
  EXPECT_NE(first.sparseCrossTuples.front().flags & SparseCrossTupleOverflow,
            0u);
  ASSERT_EQ(first.illegalBinDiagnostics.size(), 1u);
  EXPECT_EQ(first.illegalBinDiagnostics.front().count,
            std::numeric_limits<uint64_t>::max());
  EXPECT_NE(first.illegalBinDiagnostics.front().flags &
                IllegalBinDiagnosticOverflow,
            0u);
}

TEST(CoverageDatabaseTest,
     DiscardTestDetailAggregatesOneDatabaseTransactionally) {
  Database db = makeTypedDatabase();
  SourceFile source;
  source.id = 2;
  source.path = "dut.sv";
  db.sourceFiles.push_back(source);
  db.linePoints.push_back({40, 2, 2, 1, "", 1, 1, 1, 2, 0, 0});
  db.toggleObjects.push_back({50, 1, 2, "signal", 0, 1, 1, 1, 1, 2, 0});
  db.toggleDimensions.push_back({50, UINT32_MAX, ToggleDimensionKind::Root,
                                 UINT32_MAX, 0, 0, 0, 0, 1, "", 0});
  db.runs.front().flags |= RunContainsLine | RunContainsToggle;
  Counter line;
  line.run = db.runs.front().uuid;
  line.metric = MetricKind::Line;
  line.entity = 40;
  line.value = std::numeric_limits<uint64_t>::max();
  db.counters.push_back(line);
  Counter toggle = line;
  toggle.metric = MetricKind::Toggle;
  toggle.entity = 50;
  toggle.subindex = 0;
  db.counters.push_back(toggle);
  std::sort(
      db.counters.begin(), db.counters.end(), [](const auto &a, const auto &b) {
        return std::tie(a.run, a.metric, a.entity, a.instance, a.subindex) <
               std::tie(b.run, b.metric, b.entity, b.instance, b.subindex);
      });
  for (auto &counter : db.counters)
    counter.value = std::numeric_limits<uint64_t>::max();
  db.sparseCrossTuples.front().hits = std::numeric_limits<uint64_t>::max();
  db.illegalBinDiagnostics.front().count = std::numeric_limits<uint64_t>::max();
  db.schemaFingerprint = computeSchemaFingerprint(db);
  Database second = db;
  assignSecondRun(second);
  second.resolvedInstances.front().name.clear();
  for (auto &counter : second.counters)
    counter.value = 1;
  second.sparseCrossTuples.front().hits = 1;
  second.illegalBinDiagnostics.front().count = 1;
  Database third = db;
  assignSecondRun(third);
  UUID thirdRun = third.runs.front().uuid;
  thirdRun[0] = 3;
  third.runs.front().uuid = thirdRun;
  for (auto &instance : third.resolvedInstances)
    instance.run = thirdRun;
  for (auto &counter : third.counters) {
    counter.run = thirdRun;
    counter.value = 1;
  }
  for (auto &tuple : third.sparseCrossTuples) {
    tuple.run = thirdRun;
    tuple.hits = 1;
  }
  for (auto &illegal : third.illegalBinDiagnostics) {
    illegal.run = thirdRun;
    illegal.count = 1;
  }
  ASSERT_EQ(merge(db, second), Status::Ok);
  ASSERT_EQ(merge(db, third), Status::Ok);
  const auto configurationCount = db.functionalConfigurations.size();
  const auto configurationOptionCount =
      db.functionalConfigurationOptions.size();
  ASSERT_EQ(discardTestDetail(db), Status::Ok);
  EXPECT_EQ(db.runs.size(), 1u);
  EXPECT_EQ(db.runs.front().name, "aggregate");
  EXPECT_EQ(db.runs.front().status, 0u);
  EXPECT_EQ(db.runs.front().simulationTime, 0u);
  EXPECT_EQ(db.runs.front().seed, 0u);
  EXPECT_EQ(db.runs.front().timestamp, 0u);
  EXPECT_TRUE(db.runs.front().tags.empty());
  EXPECT_EQ(db.resolvedInstances.size(), 2u);
  EXPECT_EQ(db.counters.size(), 4u);
  EXPECT_EQ(db.sparseCrossTuples.size(), 2u);
  EXPECT_EQ(db.illegalBinDiagnostics.size(), 2u);
  EXPECT_EQ(db.functionalConfigurations.size(), configurationCount);
  EXPECT_EQ(db.functionalConfigurationOptions.size(), configurationOptionCount);
  for (const Counter &counter : db.counters)
    if (counter.metric != MetricKind::Functional) {
      EXPECT_EQ(counter.value, std::numeric_limits<uint64_t>::max());
      EXPECT_NE(counter.flags & 1u, 0u);
      EXPECT_EQ(counter.instance, 0u);
      EXPECT_EQ(counter.run, db.runs.front().uuid);
    }
  auto saturatedTuple = std::find_if(
      db.sparseCrossTuples.begin(), db.sparseCrossTuples.end(),
      [](const auto &tuple) { return tuple.flags & SparseCrossTupleOverflow; });
  ASSERT_NE(saturatedTuple, db.sparseCrossTuples.end());
  EXPECT_EQ(saturatedTuple->hits, std::numeric_limits<uint64_t>::max());
  EXPECT_EQ(saturatedTuple->run, db.runs.front().uuid);
  auto saturatedIllegal =
      std::find_if(db.illegalBinDiagnostics.begin(),
                   db.illegalBinDiagnostics.end(), [](const auto &illegal) {
                     return illegal.flags & IllegalBinDiagnosticOverflow;
                   });
  ASSERT_NE(saturatedIllegal, db.illegalBinDiagnostics.end());
  EXPECT_EQ(saturatedIllegal->count, std::numeric_limits<uint64_t>::max());
  EXPECT_EQ(saturatedIllegal->run, db.runs.front().uuid);

  Database invalid = db;
  invalid.counters.front().entity = 0;
  std::vector<uint8_t> before;
  ASSERT_EQ(serialize(db, before), Status::Ok);
  EXPECT_EQ(discardTestDetail(invalid), Status::InvalidReference);
  EXPECT_EQ(invalid.counters.front().entity, 0u);
}

} // namespace
