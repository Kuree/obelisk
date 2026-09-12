// Produces a small, fully populated .obcov file for codec/tool regressions.

#include "obelisk/Coverage/CoverageDatabase.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
#include <tuple>
#include <vector>

using namespace obelisk::coverage;

int main(int argc, char **argv) {
  if (argc < 5 || argc > 14) {
    std::cerr << "usage: coverage-fixture OUT UUID-BYTE NAME HIT "
                 "[TYPE-GOAL [TYPE-WEIGHT [MERGE-INSTANCES "
                 "[PER-INSTANCE [INSTANCE-NAME [INSTANCE-WEIGHT "
                 "[ITEM-WEIGHT [AT-LEAST [BIN-NAME]]]]]]]]]\n";
    return 1;
  }
  unsigned uuidByte = std::strtoul(argv[2], nullptr, 10);
  unsigned hit = std::strtoul(argv[4], nullptr, 10);
  const std::string mode = argc > 5 ? argv[5] : "";
  bool crossMode =
      mode == "cross" || mode == "cross-wide" || mode == "cross-multi" ||
      mode == "cross-deep" || mode == "cross-explicit" || mode == "cross-cap" ||
      mode == "cross-duplicate-instance" || mode == "cross-unmaterialized";
  bool wideCross = mode == "cross-wide";
  bool multiCross = mode == "cross-multi";
  bool deepCross = mode == "cross-deep";
  bool explicitCross = mode == "cross-explicit";
  bool capCross = mode == "cross-cap";
  bool expandedCross =
      wideCross || multiCross || deepCross || explicitCross || capCross;
  const uint32_t crossTargetCount = capCross    ? 1023u
                                    : deepCross ? 4096u
                                    : wideCross ? 64u
                                                : 2u;
  bool transitionMode = mode == "transition";
  bool realMode = mode == "real";
  bool toggleTreeMode = mode == "toggle-tree";
  bool aggregateOverflowMode = mode == "aggregate-overflow";
  bool illegalMode = mode == "illegal";
  bool exclusionMode = mode == "exclusion";
  bool duplicateInstanceMode = mode == "cross-duplicate-instance";
  bool unmaterializedCrossMode = mode == "cross-unmaterialized";
  bool namedMode = crossMode || transitionMode || realMode || toggleTreeMode ||
                   aggregateOverflowMode || illegalMode || exclusionMode;
  unsigned typeGoal =
      argc > 5 && !namedMode ? std::strtoul(argv[5], nullptr, 10) : 100;
  unsigned typeWeight = argc > 6 ? std::strtoul(argv[6], nullptr, 10) : 1;
  unsigned mergeInstances = argc > 7 ? std::strtoul(argv[7], nullptr, 10) : 0;
  unsigned perInstance = argc > 8 ? std::strtoul(argv[8], nullptr, 10) : 0;
  std::string instanceName = argc > 9 ? argv[9] : "fixture";
  unsigned instanceWeight = argc > 10 ? std::strtoul(argv[10], nullptr, 10) : 1;
  unsigned itemWeight = argc > 11 ? std::strtoul(argv[11], nullptr, 10) : 1;
  unsigned atLeast = argc > 12 ? std::strtoul(argv[12], nullptr, 10) : 1;
  std::string binName = argc > 13 ? argv[13] : "one";
  const uint64_t primaryResolvedBin =
      binName == "merge-bin" ? 10000 + uuidByte : 170;
  if (instanceName == "-")
    instanceName.clear();
  if (uuidByte > 255 || hit > 1 || typeGoal > 100 || mergeInstances > 1 ||
      perInstance > 1)
    return 1;

  Database db;
  db.producer = "coverage-fixture";
  SourceFile file;
  file.id = 10;
  file.path = "dut.sv";
  constexpr char source[] = "line\nexcluded\n";
  file.digest =
      sha256(reinterpret_cast<const uint8_t *>(source), sizeof(source) - 1);
  db.sourceFiles.push_back(file);
  db.scopes.push_back({20, 0, "top.dut", 1, "DUT"});
  db.linePoints.push_back({30, 10, 10, 20, "", 1, 3, 1, 8, 0, 0});
  db.linePoints.push_back({31, 10, 10, 20, "", 1, 9, 1, 14, 0, 0});
  db.linePoints.push_back({32, 10, 10, 20, "", 2, 1, 2, 9, 0, 0});
  db.toggleObjects.push_back({40, 20, 10, "data", 0, 2, 1, 1, 1, 4, 0});
  db.toggleDimensions.push_back(
      {40, UINT32_MAX, ToggleDimensionKind::Root, 1, 1, 0, 0, 0, 2, "", 0});
  db.toggleDimensions.push_back(
      {40, 0, ToggleDimensionKind::Scalar, UINT32_MAX, 0, 1, 0, 0, 2, "", 0});
  if (toggleTreeMode) {
    db.toggleObjects.front().bitWidth = 4;
    db.toggleObjects.front().flags = ToggleObjectFourState;
    db.toggleDimensions = {
        {40, UINT32_MAX, ToggleDimensionKind::Root, 1, 8, 0, 0, 0, 4, "", 0},
        {40, 0, ToggleDimensionKind::UnpackedArray, 2, 7, 2, 3, 0, 4, "", 0},
        {40, 1, ToggleDimensionKind::PackedUnion, 3, 6, 0, 0, 0, 2, "", 0},
        {40, 2, ToggleDimensionKind::Field, 4, 2, 0, 0, 0, 2, "raw", 0},
        {40, 3, ToggleDimensionKind::PackedArray, 5, 1, 1, 0, 0, 2, "", 0},
        {40, 4, ToggleDimensionKind::Scalar, UINT32_MAX, 0, 0, 0, 0, 1, "", 0},
        {40, 2, ToggleDimensionKind::Field, 7, 2, 0, 0, 0, 2, "alias", 0},
        {40, 6, ToggleDimensionKind::Enum, 8, 1, 1, 0, 0, 2, "State", 0},
        {40, 7, ToggleDimensionKind::Scalar, UINT32_MAX, 0, 1, 0, 0, 2, "", 0},
    };
    db.toggleObjects.push_back(
        {41, 20, 10, "shape", 9, 6, 1, 1, 1, 4, ToggleObjectFourState});
    db.toggleDimensions.insert(
        db.toggleDimensions.end(),
        {
            {41, UINT32_MAX, ToggleDimensionKind::Root, 10, 12, 0, 0, 0, 6, "",
             0},
            {41, 9, ToggleDimensionKind::UnpackedArray, 11, 11, 3, 2, 0, 6, "",
             0},
            {41, 10, ToggleDimensionKind::UnpackedUnion, 12, 10, 0, 0, 0, 3, "",
             0},
            {41, 11, ToggleDimensionKind::Field, 13, 4, 0, 0, 0, 2, "structs",
             0},
            {41, 12, ToggleDimensionKind::PackedStruct, 14, 3, 0, 0, 0, 2, "",
             0},
            {41, 13, ToggleDimensionKind::Field, 15, 2, 0, 0, 0, 2, "ascending",
             0},
            {41, 14, ToggleDimensionKind::PackedArray, 16, 1, 0, 1, 0, 2, "",
             0},
            {41, 15, ToggleDimensionKind::Scalar, UINT32_MAX, 0, 0, 0, 0, 1, "",
             0},
            {41, 11, ToggleDimensionKind::Field, 18, 3, 0, 0, 0, 2, "unpacked",
             0},
            {41, 17, ToggleDimensionKind::UnpackedStruct, 19, 2, 0, 0, 0, 2, "",
             0},
            {41, 18, ToggleDimensionKind::Field, 20, 1, 0, 0, 0, 2, "value", 0},
            {41, 19, ToggleDimensionKind::Scalar, UINT32_MAX, 0, 1, 0, 0, 2, "",
             0},
            {41, 11, ToggleDimensionKind::Tag, UINT32_MAX, 0, 0, 0, 2, 1,
             "$tag", 0},
        });
  }
  db.functionalTypes.push_back({50, 20, "cg", 0, 2023, "top.dut.cg"});
  db.functionalItems.push_back({60, 50, "cp", FunctionalItemKind::Coverpoint, 0,
                                100, 1, 0, "top.dut.cg.cp"});
  db.functionalBins.push_back(
      {70, 60, "one", FunctionalBinKind::State, 0, 1, 0, "top.dut.cg.cp.one"});
  db.functionalValueSets.push_back({80, 60, 0, 1, 1,
                                    FunctionalValueSetKind::Integral, 0,
                                    CoverageSignedness::Unsigned, 0});
  db.functionalValueAtoms.push_back(
      {80, 0, 0, 0, 1, 0, FunctionalValueAtomKind::IntegralValue,
       FunctionalValueAtomLowerInclusive | FunctionalValueAtomUpperInclusive, 0,
       0});
  db.functionalValueLimbs.push_back({80, 0, 0, 1, 0, 1, 0, 0});
  db.functionalBinPlans.push_back({70, 80});
  if (illegalMode) {
    db.functionalBins.push_back({71, 60, "bad", FunctionalBinKind::State,
                                 FunctionalBinIllegal, 1, 1,
                                 "top.dut.cg.cp.bad"});
    db.functionalValueSets.push_back({81, 60, 1, 1, 1,
                                      FunctionalValueSetKind::Integral, 0,
                                      CoverageSignedness::Unsigned, 0});
    db.functionalValueAtoms.push_back(
        {81, 0, 0, 1, 1, 0, FunctionalValueAtomKind::IntegralValue,
         FunctionalValueAtomLowerInclusive | FunctionalValueAtomUpperInclusive,
         0, 0});
    db.functionalValueLimbs.push_back({81, 0, 0, 0, 0, 0, 0, 0});
    db.functionalBinPlans.push_back({71, 81});
  }
  if (realMode) {
    db.functionalValueSets.front().bitWidth = 64;
    db.functionalValueSets.front().kind = FunctionalValueSetKind::Real;
    db.functionalValueSets.front().signedness =
        CoverageSignedness::NotApplicable;
    auto &atom = db.functionalValueAtoms.front();
    atom.realLowBits = UINT64_C(0x3ff8000000000000);
    atom.realHighBits = UINT64_C(0x4004000000000000);
    atom.limbCount = 0;
    atom.kind = FunctionalValueAtomKind::RealInterval;
    atom.flags =
        FunctionalValueAtomLowerInclusive | FunctionalValueAtomRealRange;
    db.functionalValueLimbs.clear();
  }
  if (transitionMode) {
    db.functionalItems.push_back({61, 50, "transition",
                                  FunctionalItemKind::Coverpoint, 0, 100, 1, 1,
                                  "top.dut.cg.transition"});
    db.functionalBins.push_back({71, 61, "five_then_nine",
                                 FunctionalBinKind::Transition, 0, 1, 0,
                                 "top.dut.cg.transition.five_then_nine"});
    for (uint64_t ordinal = 0; ordinal != 2; ++ordinal) {
      const uint64_t valueSet = 81 + ordinal;
      const uint64_t value = ordinal ? 9 : 5;
      db.functionalValueSets.push_back(
          {valueSet, 61, static_cast<uint32_t>(db.functionalValueAtoms.size()),
           1, 8, FunctionalValueSetKind::Integral, 0,
           CoverageSignedness::Unsigned, 0});
      db.functionalValueAtoms.push_back(
          {valueSet, 0, 0,
           static_cast<uint32_t>(db.functionalValueLimbs.size()), 1, 0,
           FunctionalValueAtomKind::IntegralValue,
           FunctionalValueAtomLowerInclusive |
               FunctionalValueAtomUpperInclusive,
           0, 0});
      db.functionalValueLimbs.push_back(
          {valueSet, 0, 0, value, 0, value, 0, 0});
    }
    db.functionalBinPlans.push_back({71, 0});
    db.transitionPrograms.push_back({71, 61, 0, 1, 0});
    db.transitionAlternatives.push_back({71, 82, 0, 2, 0, 0});
    db.transitionSteps.push_back(
        {71, 81, 0, 0, 1, 1, 0, 0, TransitionRepetitionKind::Once, 0});
    db.transitionSteps.push_back(
        {71, 82, 0, 0, 1, 1, 0, 1, TransitionRepetitionKind::Once, 0});
  }
  if (crossMode) {
    db.functionalItems.push_back({61, 50, "cp2", FunctionalItemKind::Coverpoint,
                                  0, 100, 1, 1, "top.dut.cg.cp2"});
    db.functionalItems.push_back({62, 50, "cp_x_cp2", FunctionalItemKind::Cross,
                                  0, 100, 1, 2, "top.dut.cg.cp_x_cp2"});
    auto addCrossSample = [&](uint64_t item, uint32_t ordinal) {
      FunctionalExpression sample;
      sample.id = 100000 + ordinal;
      sample.owner = item;
      sample.ownerKind = FunctionalExpressionOwnerKind::Item;
      sample.role = FunctionalExpressionRole::CoverpointSample;
      sample.resultKind = FunctionalExpressionResultKind::Integral;
      sample.bitWidth = 2;
      sample.signedness = CoverageSignedness::Unsigned;
      sample.evaluationPhase = FunctionalExpressionEvaluationPhase::Sample;
      sample.resultOrdinal = ordinal;
      db.functionalExpressions.push_back(sample);
    };
    addCrossSample(60, 0);
    addCrossSample(61, 1);
    db.functionalBins.push_back({71, 61, "two", FunctionalBinKind::State, 0, 1,
                                 0, "top.dut.cg.cp2.two"});
    db.functionalValueSets.push_back({81, 61, 1, 1, 2,
                                      FunctionalValueSetKind::Integral, 0,
                                      CoverageSignedness::Unsigned, 0});
    db.functionalValueAtoms.push_back(
        {81, 0, 0, 1, 1, 0, FunctionalValueAtomKind::IntegralValue,
         FunctionalValueAtomLowerInclusive | FunctionalValueAtomUpperInclusive,
         0, 0});
    db.functionalValueLimbs.push_back({81, 0, 0, 2, 0, 2, 0, 0});
    db.functionalBinPlans.push_back({71, 81});
    db.crossPlans.push_back(
        {62, 0, 2, 0, 0, CrossRetainAutoPolicy::Retain, 0, 0, 6200, 4, 0});
    db.crossTargets.push_back({62, 60, 0, 0, 2,
                               FunctionalExpressionResultKind::Integral,
                               CoverageSignedness::Unsigned, 0});
    db.crossTargets.push_back({62, 61, 1, 2, 2,
                               FunctionalExpressionResultKind::Integral,
                               CoverageSignedness::Unsigned, 0});
    if (explicitCross) {
      db.functionalBins.push_back({72, 62, "selected", FunctionalBinKind::Cross,
                                   0, 1, 0, "top.dut.cg.cp_x_cp2.selected"});
      db.crossPlans.front().binCount = 1;
      db.crossBins.push_back({72, 62, 92, 0});
    }
  }
  db.functionalConfigurations.push_back({50, {}, 0});
  if (argc > 10)
    db.functionalConfigurationOptions.push_back(
        {50,
         {},
         50,
         FunctionalConfigurationOptionOwnerKind::Group,
         FunctionalOptionScopeKind::Instance,
         FunctionalConfigurationOptionKind::Weight,
         FunctionalConfigurationValueKind::Unsigned,
         0,
         instanceWeight,
         {}});
  if (argc > 8)
    db.functionalConfigurationOptions.push_back(
        {50,
         {},
         50,
         FunctionalConfigurationOptionOwnerKind::Group,
         FunctionalOptionScopeKind::Instance,
         FunctionalConfigurationOptionKind::PerInstance,
         FunctionalConfigurationValueKind::Boolean,
         0,
         perInstance,
         {}});
  if (argc > 5)
    db.functionalConfigurationOptions.push_back(
        {50,
         {},
         50,
         FunctionalConfigurationOptionOwnerKind::Group,
         FunctionalOptionScopeKind::Type,
         FunctionalConfigurationOptionKind::Goal,
         FunctionalConfigurationValueKind::Unsigned,
         0,
         typeGoal,
         {}});
  if (argc > 6)
    db.functionalConfigurationOptions.push_back(
        {50,
         {},
         50,
         FunctionalConfigurationOptionOwnerKind::Group,
         FunctionalOptionScopeKind::Type,
         FunctionalConfigurationOptionKind::Weight,
         FunctionalConfigurationValueKind::Unsigned,
         0,
         typeWeight,
         {}});
  if (argc > 7)
    db.functionalConfigurationOptions.push_back(
        {50,
         {},
         50,
         FunctionalConfigurationOptionOwnerKind::Group,
         FunctionalOptionScopeKind::Type,
         FunctionalConfigurationOptionKind::MergeInstances,
         FunctionalConfigurationValueKind::Boolean,
         0,
         mergeInstances,
         {}});
  if (argc > 11)
    db.functionalConfigurationOptions.push_back(
        {50,
         {},
         160,
         FunctionalConfigurationOptionOwnerKind::Item,
         FunctionalOptionScopeKind::Instance,
         FunctionalConfigurationOptionKind::Weight,
         FunctionalConfigurationValueKind::Unsigned,
         0,
         itemWeight,
         {}});
  if (argc > 12)
    db.functionalConfigurationOptions.push_back(
        {50,
         {},
         160,
         FunctionalConfigurationOptionOwnerKind::Item,
         FunctionalOptionScopeKind::Instance,
         FunctionalConfigurationOptionKind::AtLeast,
         FunctionalConfigurationValueKind::Unsigned,
         0,
         atLeast,
         {}});
  if (capCross && argc > 12 && atLeast == 0)
    db.functionalConfigurationOptions.push_back(
        {50,
         {},
         162,
         FunctionalConfigurationOptionOwnerKind::Item,
         FunctionalOptionScopeKind::Instance,
         FunctionalConfigurationOptionKind::AtLeast,
         FunctionalConfigurationValueKind::Unsigned,
         0,
         atLeast,
         {}});
  if (unmaterializedCrossMode && argc > 12)
    db.functionalConfigurationOptions.push_back(
        {50,
         {},
         162,
         FunctionalConfigurationOptionOwnerKind::Item,
         FunctionalOptionScopeKind::Instance,
         FunctionalConfigurationOptionKind::AtLeast,
         FunctionalConfigurationValueKind::Unsigned,
         0,
         atLeast,
         {}});
  db.resolvedFunctionalItems.push_back({50,
                                        {},
                                        160,
                                        60,
                                        "cp",
                                        FunctionalItemKind::Coverpoint,
                                        0,
                                        100,
                                        itemWeight,
                                        0,
                                        "top.dut.cg.cp"});
  db.resolvedFunctionalBins.push_back({50,
                                       {},
                                       primaryResolvedBin,
                                       70,
                                       160,
                                       binName,
                                       FunctionalBinKind::State,
                                       0,
                                       0,
                                       0,
                                       atLeast,
                                       "top.dut.cg.cp." + binName});
  db.resolvedFunctionalValueSets.push_back(
      {50,
       {},
       180,
       80,
       160,
       0,
       1,
       1,
       FunctionalValueSetKind::Integral,
       0,
       CoverageSignedness::Unsigned,
       primaryResolvedBin,
       0,
       0,
       0,
       ResolvedFunctionalValueSetRole::StateBin});
  db.resolvedFunctionalValueAtoms.push_back(
      {180, 0, 0, 0, 1, 0, FunctionalValueAtomKind::IntegralValue,
       FunctionalValueAtomLowerInclusive | FunctionalValueAtomUpperInclusive});
  db.resolvedFunctionalValueLimbs.push_back({180, 0, 0, 1, 0, 1, 0, 0});
  db.resolvedFunctionalBinPlans.push_back({50, {}, primaryResolvedBin, 180});
  db.resolvedFunctionalBinGroups.push_back({50,
                                            {},
                                            160,
                                            70,
                                            0,
                                            1,
                                            0,
                                            FunctionalBinArrayMode::Scalar,
                                            FunctionalBinDistributionKind::None,
                                            0,
                                            FunctionalBinKind::State});
  if (illegalMode) {
    db.resolvedFunctionalBins.push_back({50,
                                         {},
                                         171,
                                         71,
                                         160,
                                         "bad",
                                         FunctionalBinKind::State,
                                         FunctionalBinIllegal,
                                         1,
                                         0,
                                         1,
                                         "top.dut.cg.cp.bad"});
    db.resolvedFunctionalValueSets.push_back(
        {50,
         {},
         181,
         81,
         160,
         1,
         1,
         1,
         FunctionalValueSetKind::Integral,
         0,
         CoverageSignedness::Unsigned,
         171,
         0,
         0,
         0,
         ResolvedFunctionalValueSetRole::StateBin});
    db.resolvedFunctionalValueAtoms.push_back(
        {181, 0, 0, 1, 1, 0, FunctionalValueAtomKind::IntegralValue,
         FunctionalValueAtomLowerInclusive |
             FunctionalValueAtomUpperInclusive});
    db.resolvedFunctionalValueLimbs.push_back({181, 0, 0, 0, 0, 0, 0, 0});
    db.resolvedFunctionalBinPlans.push_back({50, {}, 171, 181});
    db.resolvedFunctionalBinGroups.push_back(
        {50,
         {},
         160,
         71,
         1,
         1,
         0,
         FunctionalBinArrayMode::Scalar,
         FunctionalBinDistributionKind::None,
         0,
         FunctionalBinKind::State});
  }
  if (realMode) {
    db.resolvedFunctionalValueSets.front().bitWidth = 64;
    db.resolvedFunctionalValueSets.front().kind = FunctionalValueSetKind::Real;
    db.resolvedFunctionalValueSets.front().signedness =
        CoverageSignedness::NotApplicable;
    auto &atom = db.resolvedFunctionalValueAtoms.front();
    atom.realLowBits = UINT64_C(0x3ff8000000000000);
    atom.realHighBits = UINT64_C(0x4004000000000000);
    atom.limbCount = 0;
    atom.kind = FunctionalValueAtomKind::RealInterval;
    atom.flags =
        FunctionalValueAtomLowerInclusive | FunctionalValueAtomRealRange;
    db.resolvedFunctionalValueLimbs.clear();
  }
  if (transitionMode) {
    db.resolvedFunctionalItems.push_back({50,
                                          {},
                                          161,
                                          61,
                                          "transition",
                                          FunctionalItemKind::Coverpoint,
                                          0,
                                          100,
                                          1,
                                          1,
                                          "top.dut.cg.transition"});
    db.resolvedFunctionalBins.push_back(
        {50,
         {},
         171,
         71,
         161,
         "five_then_nine",
         FunctionalBinKind::Transition,
         0,
         0,
         0,
         1,
         "top.dut.cg.transition.five_then_nine"});
    for (uint64_t ordinal = 0; ordinal != 2; ++ordinal) {
      const uint64_t valueSet = 181 + ordinal;
      const uint64_t templateSet = 81 + ordinal;
      const uint64_t value = ordinal ? 9 : 5;
      db.resolvedFunctionalValueSets.push_back(
          {50,
           {},
           valueSet,
           templateSet,
           161,
           static_cast<uint32_t>(db.resolvedFunctionalValueAtoms.size()),
           1,
           8,
           FunctionalValueSetKind::Integral,
           0,
           CoverageSignedness::Unsigned,
           171,
           0,
           0,
           static_cast<uint32_t>(ordinal),
           ResolvedFunctionalValueSetRole::TransitionStep});
      db.resolvedFunctionalValueAtoms.push_back(
          {valueSet, 0, 0,
           static_cast<uint32_t>(db.resolvedFunctionalValueLimbs.size()), 1, 0,
           FunctionalValueAtomKind::IntegralValue,
           FunctionalValueAtomLowerInclusive |
               FunctionalValueAtomUpperInclusive});
      db.resolvedFunctionalValueLimbs.push_back(
          {valueSet, 0, 0, value, 0, value, 0, 0});
    }
    db.resolvedFunctionalBinPlans.push_back({50, {}, 171, 0});
    db.resolvedTransitionAlternatives.push_back(
        {50, {}, 190, 171, 0, 0, 0, 2, 0, 0});
    db.resolvedTransitionExpansionGroups.push_back(
        {50, {}, 161, 71, 0, 0, 1, 0, 1});
    db.resolvedTransitionSteps.push_back({50, {}, 171, 190, 181, 1, 1, 0, 0});
    db.resolvedTransitionSteps.push_back({50, {}, 171, 190, 182, 1, 1, 1, 0});
    db.resolvedFunctionalBinGroups.push_back(
        {50,
         {},
         161,
         71,
         1,
         1,
         0,
         FunctionalBinArrayMode::Scalar,
         FunctionalBinDistributionKind::None,
         0,
         FunctionalBinKind::Transition});
  }
  if (crossMode) {
    db.resolvedFunctionalItems.push_back({50,
                                          {},
                                          161,
                                          61,
                                          "cp2",
                                          FunctionalItemKind::Coverpoint,
                                          0,
                                          100,
                                          1,
                                          1,
                                          "top.dut.cg.cp2"});
    db.resolvedFunctionalItems.push_back({50,
                                          {},
                                          162,
                                          62,
                                          "cp_x_cp2",
                                          FunctionalItemKind::Cross,
                                          0,
                                          100,
                                          1,
                                          2,
                                          "top.dut.cg.cp_x_cp2"});
    db.resolvedFunctionalBins.push_back({50,
                                         {},
                                         171,
                                         71,
                                         161,
                                         "two",
                                         FunctionalBinKind::State,
                                         0,
                                         0,
                                         0,
                                         1,
                                         "top.dut.cg.cp2.two"});
    db.resolvedFunctionalValueSets.push_back(
        {50,
         {},
         181,
         81,
         161,
         1,
         1,
         2,
         FunctionalValueSetKind::Integral,
         0,
         CoverageSignedness::Unsigned,
         171,
         0,
         0,
         0,
         ResolvedFunctionalValueSetRole::StateBin});
    db.resolvedFunctionalValueAtoms.push_back(
        {181, 0, 0, 1, 1, 0, FunctionalValueAtomKind::IntegralValue,
         FunctionalValueAtomLowerInclusive |
             FunctionalValueAtomUpperInclusive});
    db.resolvedFunctionalValueLimbs.push_back({181, 0, 0, 2, 0, 2, 0, 0});
    db.resolvedFunctionalBinPlans.push_back({50, {}, 171, 181});
    db.resolvedFunctionalBinGroups.push_back(
        {50,
         {},
         161,
         71,
         1,
         1,
         0,
         FunctionalBinArrayMode::Scalar,
         FunctionalBinDistributionKind::None,
         0,
         FunctionalBinKind::State});
    if (explicitCross)
      db.resolvedFunctionalBins.push_back({50,
                                           {},
                                           172,
                                           72,
                                           162,
                                           "selected",
                                           FunctionalBinKind::Cross,
                                           0,
                                           0,
                                           0,
                                           1,
                                           "top.dut.cg.cp_x_cp2.selected"});
    const uint32_t automaticExponent = deepCross       ? 0
                                       : explicitCross ? crossTargetCount - 1
                                                       : crossTargetCount;
    db.resolvedCrossPlans.push_back(
        {50,
         {},
         162,
         CrossRetainAutoPolicy::Retain,
         0,
         0,
         static_cast<uint32_t>(expandedCross ? automaticExponent / 64 + 1 : 1),
         190});
    if (expandedCross) {
      db.resolvedCrossAutomaticBinCountLimbs.assign(automaticExponent / 64 + 1,
                                                    0);
      if (capCross)
        db.resolvedCrossAutomaticBinCountLimbs.back() = uint64_t{3} << 62;
      else
        db.resolvedCrossAutomaticBinCountLimbs[automaticExponent / 64] =
            uint64_t{1} << (automaticExponent % 64);
    } else {
      db.resolvedCrossAutomaticBinCountLimbs = {1};
    }
    db.resolvedCrossAutomaticNodes.push_back({50, {}, 162, 190, 0, 0, 1, 0});
    db.resolvedCrossAutomaticNodes.push_back({50, {}, 162, 191, 1, 1, 1, 0});
    db.resolvedCrossAutomaticEdges.push_back(
        {190, primaryResolvedBin, 191, 0, 0});
    db.resolvedCrossAutomaticEdges.push_back({191, 171, 0, 0, 0});

    // Exercise exact denominators wider than uint64_t without materializing
    // Cartesian products. Deep mode deliberately retains one edge per node so
    // traversal depth and APInt width remain independent regression axes.
    if (expandedCross) {
      auto staticCross =
          std::find_if(db.functionalItems.begin(), db.functionalItems.end(),
                       [](const auto &item) { return item.id == 62; });
      auto resolvedCross = std::find_if(
          db.resolvedFunctionalItems.begin(), db.resolvedFunctionalItems.end(),
          [](const auto &item) { return item.id == 162; });
      staticCross->ordinal = crossTargetCount;
      resolvedCross->ordinal = crossTargetCount;
      db.crossPlans.front().targetCount = crossTargetCount;
      db.crossPlans.front().tupleProvenanceSpan =
          uint64_t{crossTargetCount} * 2;

      std::vector<std::array<uint64_t, 3>> targetBins(crossTargetCount);
      targetBins[0][0] = primaryResolvedBin;
      targetBins[1][0] = 171;
      auto addBin = [&](uint32_t targetOrdinal, uint32_t binOrdinal,
                        uint64_t templateItem, uint64_t resolvedItem,
                        const std::string &itemName) {
        uint64_t suffix = uint64_t(targetOrdinal) * 2 + binOrdinal;
        uint64_t templateBin = 2000 + suffix;
        uint64_t resolvedBin = 20000 + suffix;
        uint64_t templateSet = 30000 + suffix;
        uint64_t resolvedValueSet = (capCross ? 70000 : 40000) + suffix;
        std::string templateBinName = binOrdinal == 0   ? "value0"
                                      : binOrdinal == 1 ? "zzvalue1"
                                                        : "zzvalue2";
        std::string resolvedBinName =
            capCross && targetOrdinal == 0
                ? binName + (binOrdinal == 0   ? "-primary"
                             : binOrdinal == 1 ? "-alt"
                                               : "-third")
                : templateBinName;
        std::string templateHierarchy =
            "top.dut.cg." + itemName + "." + templateBinName;
        std::string resolvedHierarchy =
            "top.dut.cg." + itemName + "." + resolvedBinName;
        db.functionalBins.push_back({templateBin, templateItem, templateBinName,
                                     FunctionalBinKind::State, 0, 1, binOrdinal,
                                     templateHierarchy});
        db.functionalValueSets.push_back(
            {templateSet, templateItem,
             static_cast<uint32_t>(db.functionalValueAtoms.size()), 1, 8,
             FunctionalValueSetKind::Integral, 0, CoverageSignedness::Unsigned,
             0});
        db.functionalValueAtoms.push_back(
            {templateSet, 0, 0,
             static_cast<uint32_t>(db.functionalValueLimbs.size()), 1, 0,
             FunctionalValueAtomKind::IntegralValue,
             FunctionalValueAtomLowerInclusive |
                 FunctionalValueAtomUpperInclusive,
             0, 0});
        const uint64_t value = binOrdinal + 10;
        db.functionalValueLimbs.push_back(
            {templateSet, 0, 0, value, 0, value, 0, 0});
        db.functionalBinPlans.push_back({templateBin, templateSet});
        db.resolvedFunctionalBins.push_back({50,
                                             {},
                                             resolvedBin,
                                             templateBin,
                                             resolvedItem,
                                             resolvedBinName,
                                             FunctionalBinKind::State,
                                             0,
                                             binOrdinal,
                                             0,
                                             resolvedItem == 160 ? atLeast : 1,
                                             resolvedHierarchy});
        db.resolvedFunctionalValueSets.push_back(
            {50,
             {},
             resolvedValueSet,
             templateSet,
             resolvedItem,
             static_cast<uint32_t>(db.resolvedFunctionalValueAtoms.size()),
             1,
             8,
             FunctionalValueSetKind::Integral,
             0,
             CoverageSignedness::Unsigned,
             resolvedBin,
             0,
             0,
             0,
             ResolvedFunctionalValueSetRole::StateBin});
        db.resolvedFunctionalValueAtoms.push_back(
            {resolvedValueSet, 0, 0,
             static_cast<uint32_t>(db.resolvedFunctionalValueLimbs.size()), 1,
             0, FunctionalValueAtomKind::IntegralValue,
             FunctionalValueAtomLowerInclusive |
                 FunctionalValueAtomUpperInclusive});
        db.resolvedFunctionalValueLimbs.push_back(
            {resolvedValueSet, 0, 0, value, 0, value, 0, 0});
        db.resolvedFunctionalBinPlans.push_back(
            {50, {}, resolvedBin, resolvedValueSet});
        targetBins[targetOrdinal][binOrdinal] = resolvedBin;
      };

      addBin(0, 1, 60, 160, "cp");
      if (capCross)
        addBin(0, 2, 60, 160, "cp");
      addBin(1, 1, 61, 161, "cp2");
      if (explicitCross) {
        // Represent the explicit bin as a real selector DAG:
        //   set({cp=1, cp2=2}) || set({cp=1, cp2=11})
        // Each Set owns a one-element value-tuple set. The resolved tuple
        // components deliberately have distinct stable IDs from their static
        // templates so reporter extensions exercise every identity mapping.
        const std::array<uint64_t, 4> templateSets{31000, 31001, 31002, 31003};
        const std::array<uint64_t, 4> resolvedSets{41000, 41001, 41002, 41003};
        const std::array<uint64_t, 4> templateItems{60, 61, 60, 61};
        const std::array<uint64_t, 4> resolvedItems{160, 161, 160, 161};
        const std::array<uint64_t, 4> values{1, 2, 1, 11};
        for (uint32_t index = 0; index != templateSets.size(); ++index) {
          db.functionalValueSets.push_back(
              {templateSets[index], templateItems[index],
               static_cast<uint32_t>(db.functionalValueAtoms.size()), 1, 8,
               FunctionalValueSetKind::Integral, 0,
               CoverageSignedness::Unsigned, 0});
          db.functionalValueAtoms.push_back(
              {templateSets[index], 0, 0,
               static_cast<uint32_t>(db.functionalValueLimbs.size()), 1, 0,
               FunctionalValueAtomKind::IntegralValue,
               FunctionalValueAtomLowerInclusive |
                   FunctionalValueAtomUpperInclusive,
               0, 0});
          db.functionalValueLimbs.push_back({templateSets[index], 0, 0,
                                             values[index], 0, values[index], 0,
                                             0});

          const uint64_t selector = index < 2 ? 90 : 91;
          db.resolvedFunctionalValueSets.push_back(
              {50,
               {},
               resolvedSets[index],
               templateSets[index],
               resolvedItems[index],
               static_cast<uint32_t>(db.resolvedFunctionalValueAtoms.size()),
               1,
               8,
               FunctionalValueSetKind::Integral,
               0,
               CoverageSignedness::Unsigned,
               0,
               selector,
               0,
               index % 2,
               ResolvedFunctionalValueSetRole::TupleComponent});
          db.resolvedFunctionalValueAtoms.push_back(
              {resolvedSets[index], 0, 0,
               static_cast<uint32_t>(db.resolvedFunctionalValueLimbs.size()), 1,
               0, FunctionalValueAtomKind::IntegralValue,
               FunctionalValueAtomLowerInclusive |
                   FunctionalValueAtomUpperInclusive});
          db.resolvedFunctionalValueLimbs.push_back({resolvedSets[index], 0, 0,
                                                     values[index], 0,
                                                     values[index], 0, 0});
        }

        db.crossSelectorNodes.push_back({90, 62, 0, 0, 0, 0, 0, 100, 0, 0, 0,
                                         CrossSelectorKind::Set, 0, 0,
                                         CrossMatchesPolicy::All, 0});
        db.crossSelectorNodes.push_back({91, 62, 0, 0, 0, 0, 0, 101, 0, 0, 0,
                                         CrossSelectorKind::Set, 1, 0,
                                         CrossMatchesPolicy::All, 0});
        db.crossSelectorNodes.push_back({92, 62, 0, 0, 0, 0, 0, 0, 0, 0, 2,
                                         CrossSelectorKind::Or, 2, 0,
                                         CrossMatchesPolicy::None, 0});
        db.crossSelectorOperands.push_back({92, 90, 0});
        db.crossSelectorOperands.push_back({92, 91, 1});

        db.functionalTupleSets.push_back(
            {100, 62, 90, 0, 1, FunctionalTupleElementMode::ValueTuple, 0});
        db.functionalTupleSets.push_back(
            {101, 62, 91, 1, 1, FunctionalTupleElementMode::ValueTuple, 0});
        db.functionalTupleSetTuples.push_back({100, 1000, 0, 2, 0, 0});
        db.functionalTupleSetTuples.push_back({101, 1001, 2, 2, 0, 0});
        db.functionalTupleSetComponents.push_back({1000, 60, 0, 31000, 0, 0});
        db.functionalTupleSetComponents.push_back({1000, 61, 0, 31001, 1, 0});
        db.functionalTupleSetComponents.push_back({1001, 60, 0, 31002, 0, 0});
        db.functionalTupleSetComponents.push_back({1001, 61, 0, 31003, 1, 0});

        db.resolvedFunctionalTupleSets.push_back(
            {50,
             {},
             200,
             100,
             162,
             90,
             0,
             1,
             FunctionalTupleElementMode::ValueTuple,
             0});
        db.resolvedFunctionalTupleSets.push_back(
            {50,
             {},
             201,
             101,
             162,
             91,
             1,
             1,
             FunctionalTupleElementMode::ValueTuple,
             0});
        db.resolvedFunctionalTupleSetTuples.push_back({200, 2000, 0, 2, 0, 0});
        db.resolvedFunctionalTupleSetTuples.push_back({201, 2001, 2, 2, 0, 0});
        db.resolvedFunctionalTupleSetComponents.push_back(
            {2000, 160, 0, 41000, 0, 0});
        db.resolvedFunctionalTupleSetComponents.push_back(
            {2000, 161, 0, 41001, 1, 0});
        db.resolvedFunctionalTupleSetComponents.push_back(
            {2001, 160, 0, 41002, 0, 0});
        db.resolvedFunctionalTupleSetComponents.push_back(
            {2001, 161, 0, 41003, 1, 0});
        db.resolvedCrossSelectorBindings.push_back(
            {50, {}, 162, 90, 0, 0, 200, CrossMatchesPolicy::All, 0, 0});
        db.resolvedCrossSelectorBindings.push_back(
            {50, {}, 162, 91, 0, 0, 201, CrossMatchesPolicy::All, 0, 0});
      }
      for (uint32_t targetOrdinal = 2; targetOrdinal != crossTargetCount;
           ++targetOrdinal) {
        uint64_t templateItem = 1000 + targetOrdinal;
        uint64_t resolvedItem = 10000 + targetOrdinal;
        std::string name = "cp_extra_" + std::to_string(targetOrdinal);
        std::string hierarchy = "top.dut.cg." + name;
        db.functionalItems.push_back({templateItem, 50, name,
                                      FunctionalItemKind::Coverpoint, 0, 100, 1,
                                      targetOrdinal, hierarchy});
        db.resolvedFunctionalItems.push_back({50,
                                              {},
                                              resolvedItem,
                                              templateItem,
                                              name,
                                              FunctionalItemKind::Coverpoint,
                                              0,
                                              100,
                                              1,
                                              targetOrdinal,
                                              hierarchy});
        FunctionalExpression sample;
        sample.id = 100000 + targetOrdinal;
        sample.owner = templateItem;
        sample.ownerKind = FunctionalExpressionOwnerKind::Item;
        sample.role = FunctionalExpressionRole::CoverpointSample;
        sample.resultKind = FunctionalExpressionResultKind::Integral;
        sample.bitWidth = 2;
        sample.signedness = CoverageSignedness::Unsigned;
        sample.evaluationPhase = FunctionalExpressionEvaluationPhase::Sample;
        sample.resultOrdinal = targetOrdinal;
        db.functionalExpressions.push_back(sample);
        db.crossTargets.push_back({62, templateItem, targetOrdinal,
                                   uint64_t{targetOrdinal} * 2, 2,
                                   FunctionalExpressionResultKind::Integral,
                                   CoverageSignedness::Unsigned, 0});
        addBin(targetOrdinal, 0, templateItem, resolvedItem, name);
        addBin(targetOrdinal, 1, templateItem, resolvedItem, name);
      }

      auto itemOrdinal = [&](uint64_t id) {
        auto item = std::find_if(
            db.resolvedFunctionalItems.begin(),
            db.resolvedFunctionalItems.end(),
            [&](const auto &candidate) { return candidate.id == id; });
        return item->ordinal;
      };
      std::sort(db.resolvedFunctionalBins.begin(),
                db.resolvedFunctionalBins.end(),
                [&](const auto &left, const auto &right) {
                  return std::make_tuple(left.type, left.configuration,
                                         left.kind == FunctionalBinKind::Cross,
                                         itemOrdinal(left.item), left.ordinal,
                                         left.templateBin) <
                         std::make_tuple(right.type, right.configuration,
                                         right.kind == FunctionalBinKind::Cross,
                                         itemOrdinal(right.item), right.ordinal,
                                         right.templateBin);
                });
      db.resolvedFunctionalBinGroups.clear();
      for (uint32_t index = 0; index != db.resolvedFunctionalBins.size();
           ++index) {
        const auto &bin = db.resolvedFunctionalBins[index];
        if (bin.kind == FunctionalBinKind::Cross)
          continue;
        db.resolvedFunctionalBinGroups.push_back(
            {50,
             {},
             bin.item,
             bin.templateBin,
             index,
             1,
             0,
             FunctionalBinArrayMode::Scalar,
             FunctionalBinDistributionKind::None,
             0,
             FunctionalBinKind::State});
      }

      const uint64_t automaticNodeBase = capCross ? 100000 : 50000;
      db.resolvedCrossPlans.front().rootNode = automaticNodeBase;
      db.resolvedCrossAutomaticNodes.clear();
      db.resolvedCrossAutomaticEdges.clear();
      for (uint32_t targetOrdinal = 0; targetOrdinal != crossTargetCount;
           ++targetOrdinal) {
        uint64_t node = automaticNodeBase + targetOrdinal;
        const uint32_t firstBinOrdinal =
            explicitCross && targetOrdinal == 0 ? 1 : 0;
        const uint32_t endBinOrdinal = deepCross ? firstBinOrdinal + 1
                                       : capCross && targetOrdinal == 0 ? 3
                                                                        : 2;
        db.resolvedCrossAutomaticNodes.push_back(
            {50,
             {},
             162,
             node,
             targetOrdinal,
             static_cast<uint32_t>(db.resolvedCrossAutomaticEdges.size()),
             endBinOrdinal - firstBinOrdinal,
             0});
        uint64_t child = targetOrdinal + 1 == crossTargetCount
                             ? 0
                             : automaticNodeBase + targetOrdinal + 1;
        for (uint32_t binOrdinal = firstBinOrdinal; binOrdinal != endBinOrdinal;
             ++binOrdinal)
          db.resolvedCrossAutomaticEdges.push_back(
              {node, targetBins[targetOrdinal][binOrdinal], child,
               binOrdinal - firstBinOrdinal, 0});
      }
    }
  }
  Digest configuration =
      computeFunctionalConfigurationFingerprint(db, 50, Digest{});
  db.functionalConfigurations.front().configuration = configuration;
  for (auto &option : db.functionalConfigurationOptions)
    option.configuration = configuration;
  db.resolvedFunctionalItems.front().configuration = configuration;
  db.resolvedFunctionalBins.front().configuration = configuration;
  db.resolvedFunctionalValueSets.front().configuration = configuration;
  db.resolvedFunctionalBinPlans.front().configuration = configuration;
  db.resolvedFunctionalBinGroups.front().configuration = configuration;
  for (auto &item : db.resolvedFunctionalItems)
    item.configuration = configuration;
  for (auto &bin : db.resolvedFunctionalBins)
    bin.configuration = configuration;
  for (auto &set : db.resolvedFunctionalValueSets)
    set.configuration = configuration;
  for (auto &plan : db.resolvedFunctionalBinPlans)
    plan.configuration = configuration;
  for (auto &group : db.resolvedFunctionalBinGroups)
    group.configuration = configuration;
  for (auto &alternative : db.resolvedTransitionAlternatives)
    alternative.configuration = configuration;
  for (auto &group : db.resolvedTransitionExpansionGroups)
    group.configuration = configuration;
  for (auto &step : db.resolvedTransitionSteps)
    step.configuration = configuration;
  for (auto &plan : db.resolvedCrossPlans)
    plan.configuration = configuration;
  for (auto &node : db.resolvedCrossAutomaticNodes)
    node.configuration = configuration;
  for (auto &binding : db.resolvedCrossSelectorBindings)
    binding.configuration = configuration;
  for (auto &set : db.resolvedFunctionalTupleSets)
    set.configuration = configuration;
  db.exclusions.push_back({31, MetricKind::Line, "generated", 0});
  db.exclusions.push_back({32, MetricKind::Line, "entire generated line", 0});
  if (exclusionMode)
    db.exclusions.push_back({70, MetricKind::Functional, "fixture waiver", 0});
  Run run;
  run.uuid[15] = static_cast<uint8_t>(uuidByte);
  run.name = argv[3];
  run.status = hit ? 0 : 1;
  run.simulationTime = 42;
  run.seed = uuidByte;
  run.flags = RunContainsLine | RunContainsToggle | RunContainsFunctional;
  run.tags.emplace_back("suite", "codec");
  db.runs.push_back(run);
  db.resolvedInstances.push_back(
      {run.uuid, 50, 1, instanceName, 0, configuration});
  if (duplicateInstanceMode)
    db.resolvedInstances.push_back(
        {run.uuid, 50, 2, instanceName, 0, configuration});
  const uint64_t fixtureCount = aggregateOverflowMode && uuidByte == 24
                                    ? UINT64_MAX
                                    : static_cast<uint64_t>(hit);
  db.counters.push_back(
      {run.uuid, MetricKind::Line, 30, 0, 0, 0, fixtureCount});
  db.counters.push_back(
      {run.uuid, MetricKind::Toggle, 40, 0, 0, 0, fixtureCount});
  db.counters.push_back(
      {run.uuid, MetricKind::Toggle, 40, 0, 1, 0, fixtureCount});
  if (toggleTreeMode) {
    db.counters.push_back({run.uuid, MetricKind::Toggle, 40, 0, 2, 1, 2});
    db.counters.push_back({run.uuid, MetricKind::Toggle, 40, 0, 3, 0, 3});
  }
  if (illegalMode)
    db.illegalBinDiagnostics.push_back(
        {run.uuid, 171, 1, 42, 1, "illegal fixture bin", 0});
  const uint64_t primaryFunctionalCount =
      duplicateInstanceMode ? UINT64_MAX : fixtureCount;
  db.counters.push_back({run.uuid, MetricKind::Functional, primaryResolvedBin,
                         1, 0, 0, primaryFunctionalCount});
  if (duplicateInstanceMode)
    db.counters.push_back(
        {run.uuid, MetricKind::Functional, primaryResolvedBin, 2, 0, 0, 1});
  if (transitionMode)
    db.counters.push_back(
        {run.uuid, MetricKind::Functional, 171, 1, 0, 0, hit});
  if (explicitCross)
    db.counters.push_back(
        {run.uuid, MetricKind::Functional, 172, 1, 0, 0, hit});
  if (crossMode && (!unmaterializedCrossMode || hit)) {
    db.sparseCrossTuples.push_back(
        {run.uuid, 1, 62, 0, crossTargetCount, hit, 0});
    if (expandedCross) {
      db.sparseCrossTupleComponents.push_back(
          explicitCross ? 20001 : primaryResolvedBin);
      db.sparseCrossTupleComponents.push_back(171);
      for (uint32_t targetOrdinal = 2; targetOrdinal != crossTargetCount;
           ++targetOrdinal)
        db.sparseCrossTupleComponents.push_back(20000 + targetOrdinal * 2);
    } else {
      db.sparseCrossTupleComponents.push_back(primaryResolvedBin);
      db.sparseCrossTupleComponents.push_back(171);
    }
    if (duplicateInstanceMode) {
      const uint32_t firstComponent =
          static_cast<uint32_t>(db.sparseCrossTupleComponents.size());
      db.sparseCrossTuples.push_back(
          {run.uuid, 2, 62, firstComponent, crossTargetCount, hit, 0});
      db.sparseCrossTupleComponents.push_back(primaryResolvedBin);
      db.sparseCrossTupleComponents.push_back(171);
    }
    if (multiCross || explicitCross) {
      db.sparseCrossTuples.push_back(
          {run.uuid, 1, 62, crossTargetCount, crossTargetCount, hit, 0});
      db.sparseCrossTupleComponents.push_back(20001);
      db.sparseCrossTupleComponents.push_back(20003);
    }
  }
  db.schemaFingerprint = computeSchemaFingerprint(db);

  std::vector<uint8_t> first, second;
  Diagnostic diagnostic;
  if (serialize(db, first, &diagnostic) != Status::Ok) {
    std::cerr << "serialize: " << statusName(diagnostic.status)
              << " field=" << (diagnostic.field ? diagnostic.field : "")
              << " detail=" << diagnostic.detail << '\n';
    return 2;
  }
  Database decoded;
  if (parse(first.data(), first.size(), decoded, {}, &diagnostic) !=
      Status::Ok) {
    std::cerr << "parse: " << statusName(diagnostic.status)
              << " field=" << (diagnostic.field ? diagnostic.field : "")
              << " offset=" << diagnostic.offset << '\n';
    return 3;
  }
  if (serialize(decoded, second, &diagnostic) != Status::Ok || first != second)
    return 4;
  if (first.size() > 120) {
    first[120] ^= 1;
    Database corrupt;
    if (parse(first.data(), first.size(), corrupt, {}, &diagnostic) !=
        Status::ChecksumMismatch)
      return 5;
  }
  return writeFileAtomically(argv[1], db, &diagnostic) == Status::Ok ? 0 : 6;
}
