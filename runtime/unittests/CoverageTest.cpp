//===- CoverageTest.cpp - Functional coverage runtime tests --------------===//

#include "../lib/RuntimeInternal.h"
#include "obelisk/Coverage/CoverageDatabase.h"
#include "obelisk/Runtime/Runtime.h"

#include "gtest/gtest.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

namespace {

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

size_t sectionOffset(const std::vector<uint8_t> &bytes,
                     obelisk::coverage::SectionKind kind) {
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
  const size_t payloadSize = bytes.size() - obelisk::coverage::ChecksumSize;
  obelisk::coverage::Digest digest =
      obelisk::coverage::sha256(bytes.data(), payloadSize);
  std::copy(digest.begin(), digest.end(), bytes.begin() + payloadSize);
}

class CoverageRuntimeTest : public testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
  }
  void TearDown() override { obelisk_rt_v1_context_destroy(context); }

  obelisk_rt_context *context = nullptr;
};

obelisk_rt_status finalizeCoverage(
    obelisk_rt_context *context, uint64_t lineCount, uint64_t toggleBitCount,
    const uint8_t *initialValue, const uint8_t *initialUnknown,
    uint32_t persistenceMask = OBELISK_RT_COVERAGE_PERSIST_ALL) {
  return obelisk_rt_v1_coverage_finalize(
      context, lineCount, toggleBitCount, initialValue, initialUnknown,
      persistenceMask);
}

template <typename Callback>
obelisk_rt_status withManagedString(obelisk_rt_context *context,
                                    const std::string &text,
                                    Callback &&callback) {
  obelisk_rt_gc_lane_v1 *lane = nullptr;
  obelisk_rt_status status = obelisk_rt_v1_gc_lane_create(context, &lane);
  if (status != OBELISK_RT_OK)
    return status;
  status = obelisk_rt_v1_gc_lane_enter(lane);
  if (status != OBELISK_RT_OK) {
    (void)obelisk_rt_v1_gc_lane_destroy(lane);
    return status;
  }
  obelisk_rt_string_v1 string = 0;
  status = obelisk_rt_v1_string_create(lane, text.data(), text.size(), &string);
  if (status == OBELISK_RT_OK)
    status = callback(string);
  obelisk_rt_status leaveStatus = obelisk_rt_v1_gc_lane_leave(lane);
  obelisk_rt_status destroyStatus = obelisk_rt_v1_gc_lane_destroy(lane);
  if (status != OBELISK_RT_OK)
    return status;
  return leaveStatus != OBELISK_RT_OK ? leaveStatus : destroyStatus;
}

obelisk::coverage::Database makeFunctionalSchema() {
  using namespace obelisk::coverage;
  Database schema;
  schema.scopes.push_back({1, 0, "top", 0, "top"});
  schema.functionalTypes.push_back({10, 1, "cg", 0, 2023, "top.cg"});
  schema.functionalItems.push_back({20, 10, "first",
                                    FunctionalItemKind::Coverpoint, 0, 100, 1,
                                    0, "top.cg.first"});
  schema.functionalItems.push_back({21, 10, "second",
                                    FunctionalItemKind::Coverpoint, 0, 100, 1,
                                    1, "top.cg.second"});
  schema.functionalBins.push_back(
      {30, 20, "zero", FunctionalBinKind::State, 0, 1, 0, "top.cg.first.zero"});
  schema.functionalBins.push_back(
      {31, 20, "one", FunctionalBinKind::State, 0, 1, 1, "top.cg.first.one"});
  schema.functionalBins.push_back(
      {40, 21, "two", FunctionalBinKind::State, 0, 1, 0, "top.cg.second.two"});
  schema.functionalValueSets.push_back({50, 20, 0, 1, 2,
                                        FunctionalValueSetKind::Integral, 0,
                                        CoverageSignedness::Unsigned, 0});
  schema.functionalValueSets.push_back({51, 20, 1, 1, 2,
                                        FunctionalValueSetKind::Integral, 0,
                                        CoverageSignedness::Unsigned, 0});
  schema.functionalValueSets.push_back({52, 21, 2, 1, 2,
                                        FunctionalValueSetKind::Integral, 0,
                                        CoverageSignedness::Unsigned, 0});
  constexpr uint32_t inclusive =
      FunctionalValueAtomLowerInclusive | FunctionalValueAtomUpperInclusive;
  schema.functionalValueAtoms.push_back({50, 0, 0, 0, 1, 0,
                                         FunctionalValueAtomKind::IntegralValue,
                                         inclusive, 0, 0});
  schema.functionalValueAtoms.push_back({51, 0, 0, 1, 1, 0,
                                         FunctionalValueAtomKind::IntegralValue,
                                         inclusive, 0, 0});
  schema.functionalValueAtoms.push_back({52, 0, 0, 2, 1, 0,
                                         FunctionalValueAtomKind::IntegralValue,
                                         inclusive, 0, 0});
  schema.functionalValueLimbs.push_back({50, 0, 0, 0, 0, 0, 0, 0});
  schema.functionalValueLimbs.push_back({51, 0, 0, 1, 0, 1, 0, 0});
  schema.functionalValueLimbs.push_back({52, 0, 0, 2, 0, 2, 0, 0});
  schema.functionalBinPlans.push_back({30, 50});
  schema.functionalBinPlans.push_back({31, 51});
  schema.functionalBinPlans.push_back({40, 52});
  FunctionalExpression firstSample;
  firstSample.id = 60;
  firstSample.owner = 20;
  firstSample.ownerKind = FunctionalExpressionOwnerKind::Item;
  firstSample.role = FunctionalExpressionRole::CoverpointSample;
  firstSample.resultKind = FunctionalExpressionResultKind::Integral;
  firstSample.bitWidth = 2;
  firstSample.signedness = CoverageSignedness::Unsigned;
  firstSample.evaluationPhase = FunctionalExpressionEvaluationPhase::Sample;
  firstSample.resultOrdinal = 0;
  schema.functionalExpressions.push_back(firstSample);
  FunctionalExpression secondSample = firstSample;
  secondSample.id = 61;
  secondSample.owner = 21;
  secondSample.resultOrdinal = 1;
  schema.functionalExpressions.push_back(secondSample);
  schema.schemaFingerprint = computeSchemaFingerprint(schema);
  return schema;
}

obelisk::coverage::Database makeSharedAndSelectorSchema() {
  using namespace obelisk::coverage;
  Database schema = makeFunctionalSchema();
  schema.functionalItems.push_back({22, 10, "product",
                                    FunctionalItemKind::Cross, 0, 100, 1, 2,
                                    "top.cg.product"});
  schema.functionalBins.push_back({41, 22, "selected", FunctionalBinKind::Cross,
                                   0, 1, 0, "top.cg.product.selected"});
  schema.crossPlans.push_back(
      {22, 0, 2, 0, 1, CrossRetainAutoPolicy::Discard, 0, 0, 220, 4, 0});
  schema.crossTargets.push_back({22, 20, 0, 0, 2,
                                 FunctionalExpressionResultKind::Integral,
                                 CoverageSignedness::Unsigned, 0});
  schema.crossTargets.push_back({22, 21, 1, 2, 2,
                                 FunctionalExpressionResultKind::Integral,
                                 CoverageSignedness::Unsigned, 0});
  schema.crossBins.push_back({41, 22, 73, 0});
  schema.crossSelectorNodes.push_back({70, 22, 20, 30, 0, 0, 0, 0, 0, 0, 0,
                                       CrossSelectorKind::Binsof, 0, 0,
                                       CrossMatchesPolicy::None, 0});
  schema.crossSelectorNodes.push_back({71, 22, 21, 40, 0, 0, 0, 0, 0, 0, 0,
                                       CrossSelectorKind::Binsof, 1, 0,
                                       CrossMatchesPolicy::None, 0});
  schema.crossSelectorNodes.push_back({72, 22, 0, 0, 0, 0, 0, 0, 0, 0, 2,
                                       CrossSelectorKind::And, 2, 0,
                                       CrossMatchesPolicy::None, 0});
  // The root intentionally references the same earlier AND node twice. This
  // is a valid v1 selector DAG and must not be expanded exponentially or
  // rejected as though the physical records formed only a tree.
  schema.crossSelectorNodes.push_back({73, 22, 0, 0, 0, 0, 0, 0, 0, 2, 2,
                                       CrossSelectorKind::And, 3, 0,
                                       CrossMatchesPolicy::None, 0});
  schema.crossSelectorOperands.push_back({72, 70, 0});
  schema.crossSelectorOperands.push_back({72, 71, 1});
  schema.crossSelectorOperands.push_back({73, 72, 0});
  schema.crossSelectorOperands.push_back({73, 72, 1});
  schema.schemaFingerprint = computeSchemaFingerprint(schema);
  return schema;
}

obelisk::coverage::Database
makeCrossWithSchema(obelisk::coverage::CrossMatchesPolicy policy,
                    uint64_t count, bool splitFirstTarget = false) {
  using namespace obelisk::coverage;
  Database schema = makeFunctionalSchema();
  schema.functionalBins = {
      {30, 20, "all", FunctionalBinKind::State, 0, 1, 0, "top.cg.first.all"},
      {40, 21, "all", FunctionalBinKind::State, 0, 1, 0, "top.cg.second.all"},
      {41, 22, "selected", FunctionalBinKind::Cross, 0, 1, 0,
       "top.cg.product.selected"}};
  schema.functionalItems.push_back({22, 10, "product",
                                    FunctionalItemKind::Cross, 0, 100, 1, 2,
                                    "top.cg.product"});
  schema.functionalValueSets = {
      {50, 20, 0, 1, 2, FunctionalValueSetKind::Integral, 0,
       CoverageSignedness::Unsigned, 0},
      {52, 21, 1, 1, 2, FunctionalValueSetKind::Integral, 0,
       CoverageSignedness::Unsigned, 0}};
  constexpr uint32_t inclusive =
      FunctionalValueAtomLowerInclusive | FunctionalValueAtomUpperInclusive;
  schema.functionalValueAtoms = {
      {50, 0, 0, 0, 1, 0, FunctionalValueAtomKind::IntegralRange, inclusive, 0,
       0},
      {52, 0, 0, 1, 1, 0, FunctionalValueAtomKind::IntegralRange, inclusive, 0,
       0}};
  schema.functionalValueLimbs = {{50, 0, 0, 0, 0, 3, 0, 0},
                                 {52, 0, 0, 0, 0, 3, 0, 0}};
  schema.functionalBinPlans = {{30, 50}, {40, 52}};
  schema.crossPlans.push_back(
      {22, 0, 2, 0, 1, CrossRetainAutoPolicy::Discard, 0, 0, 220, 4, 0});
  schema.crossTargets = {
      {22, 20, 0, 0, 2, FunctionalExpressionResultKind::Integral,
       CoverageSignedness::Unsigned, 0},
      {22, 21, 1, 2, 2, FunctionalExpressionResultKind::Integral,
       CoverageSignedness::Unsigned, 0}};
  schema.crossBins = {{41, 22, 70, 0}};
  CrossSelectorNode selector;
  selector.id = 70;
  selector.cross = 22;
  selector.withExpression = 100;
  selector.kind = CrossSelectorKind::AllTuples;
  selector.matchesPolicy = policy;
  selector.matchesCount = count;
  schema.crossSelectorNodes.push_back(selector);
  Digest digest{};
  digest.front() = 1;
  for (uint32_t ordinal = 0; ordinal != 16; ++ordinal) {
    FunctionalExpression predicate;
    predicate.id = 100 + ordinal;
    predicate.owner = 70;
    predicate.ownerKind = FunctionalExpressionOwnerKind::Selector;
    predicate.role = FunctionalExpressionRole::SelectorWith;
    predicate.resultKind = FunctionalExpressionResultKind::Boolean;
    predicate.semanticDigest = digest;
    predicate.ownerOrdinal = ordinal;
    predicate.evaluationPhase =
        FunctionalExpressionEvaluationPhase::Constructor;
    predicate.resultOrdinal = ordinal;
    schema.functionalExpressions.push_back(predicate);
  }
  if (splitFirstTarget) {
    schema.functionalBins.insert(schema.functionalBins.begin() + 1,
                                 {31, 20, "high", FunctionalBinKind::State, 0,
                                  1, 1, "top.cg.first.high"});
    schema.functionalBins.front().name = "low";
    schema.functionalBins.front().hierarchy = "top.cg.first.low";
    schema.functionalValueSets.insert(schema.functionalValueSets.begin() + 1,
                                      {51, 20, 1, 1, 2,
                                       FunctionalValueSetKind::Integral, 0,
                                       CoverageSignedness::Unsigned, 0});
    schema.functionalValueSets.back().firstAtom = 2;
    schema.functionalValueAtoms.insert(schema.functionalValueAtoms.begin() + 1,
                                       {51, 0, 0, 1, 1, 0,
                                        FunctionalValueAtomKind::IntegralRange,
                                        inclusive, 0, 0});
    schema.functionalValueAtoms.back().firstLimb = 2;
    schema.functionalValueLimbs.front().highAval = 1;
    schema.functionalValueLimbs.insert(schema.functionalValueLimbs.begin() + 1,
                                       {51, 0, 0, 2, 0, 3, 0, 0});
    schema.functionalBinPlans.insert(schema.functionalBinPlans.begin() + 1,
                                     {31, 51});
  }
  std::sort(
      schema.functionalExpressions.begin(), schema.functionalExpressions.end(),
      [](const auto &left, const auto &right) { return left.id < right.id; });
  schema.schemaFingerprint = computeSchemaFingerprint(schema);
  return schema;
}

obelisk::coverage::Database makeScopedCodeCoverageSchema() {
  using namespace obelisk::coverage;
  Database schema;
  schema.sourceFiles.push_back({100, "scope.sv", {}});
  schema.scopes.push_back({1, 0, "$root", 0, ""});
  schema.scopes.push_back({2, 1, "top", 32, "Top"});
  schema.scopes.push_back({3, 2, "top.first", 32, "DUT"});
  schema.scopes.push_back({4, 2, "top.second", 32, "DUT"});
  schema.scopes.push_back({5, 3, "top.first.child", 32, "Child"});
  schema.linePoints.push_back({10, 100, 100, 3, "", 1, 1, 1, 2, 0, 0});
  schema.linePoints.push_back({11, 100, 100, 4, "", 2, 1, 2, 2, 0, 0});
  schema.linePoints.push_back({12, 100, 100, 5, "", 3, 1, 3, 2, 0, 0});
  schema.linePoints.push_back({13, 100, 100, 4, "", 4, 1, 4, 2, 0, 0});
  schema.exclusions.push_back({13, MetricKind::Line, "excluded statement", 0});

  auto addToggle = [&](uint64_t id, uint64_t scope, const char *name) {
    uint32_t root = static_cast<uint32_t>(schema.toggleDimensions.size());
    schema.toggleObjects.push_back(
        {id, scope, 100, name, root, 1, 5, 1, 5, 2, 0});
    schema.toggleDimensions.push_back({id, UINT32_MAX,
                                       ToggleDimensionKind::Root, root + 1, 1,
                                       0, 0, 0, 1, "", 0});
    schema.toggleDimensions.push_back({id, root, ToggleDimensionKind::Scalar,
                                       UINT32_MAX, 0, 1, 0, 0, 1, "", 0});
  };
  addToggle(20, 3, "first");
  addToggle(21, 4, "second");
  addToggle(22, 5, "child");
  schema.schemaFingerprint = computeSchemaFingerprint(schema);
  return schema;
}

obelisk::coverage::Digest
addResolvedFunctionalConfiguration(obelisk::coverage::Database &database) {
  using namespace obelisk::coverage;
  database.functionalConfigurations.push_back({10, {}, 0});
  database.resolvedFunctionalItems.push_back({10,
                                              {},
                                              120,
                                              20,
                                              "first",
                                              FunctionalItemKind::Coverpoint,
                                              0,
                                              100,
                                              1,
                                              0,
                                              "top.cg.first"});
  database.resolvedFunctionalItems.push_back({10,
                                              {},
                                              121,
                                              21,
                                              "second",
                                              FunctionalItemKind::Coverpoint,
                                              0,
                                              100,
                                              1,
                                              1,
                                              "top.cg.second"});
  database.resolvedFunctionalBins.push_back({10,
                                             {},
                                             130,
                                             30,
                                             120,
                                             "zero",
                                             FunctionalBinKind::State,
                                             0,
                                             0,
                                             0,
                                             1,
                                             "top.cg.first.zero"});
  database.resolvedFunctionalBins.push_back({10,
                                             {},
                                             131,
                                             31,
                                             120,
                                             "one",
                                             FunctionalBinKind::State,
                                             0,
                                             1,
                                             0,
                                             1,
                                             "top.cg.first.one"});
  database.resolvedFunctionalBins.push_back({10,
                                             {},
                                             140,
                                             40,
                                             121,
                                             "two",
                                             FunctionalBinKind::State,
                                             0,
                                             0,
                                             0,
                                             1,
                                             "top.cg.second.two"});
  database.resolvedFunctionalValueSets.push_back(
      {10,
       {},
       150,
       50,
       120,
       0,
       1,
       2,
       FunctionalValueSetKind::Integral,
       0,
       CoverageSignedness::Unsigned,
       130,
       0,
       0,
       0,
       ResolvedFunctionalValueSetRole::StateBin});
  database.resolvedFunctionalValueSets.push_back(
      {10,
       {},
       151,
       51,
       120,
       1,
       1,
       2,
       FunctionalValueSetKind::Integral,
       0,
       CoverageSignedness::Unsigned,
       131,
       0,
       0,
       0,
       ResolvedFunctionalValueSetRole::StateBin});
  database.resolvedFunctionalValueSets.push_back(
      {10,
       {},
       152,
       52,
       121,
       2,
       1,
       2,
       FunctionalValueSetKind::Integral,
       0,
       CoverageSignedness::Unsigned,
       140,
       0,
       0,
       0,
       ResolvedFunctionalValueSetRole::StateBin});
  constexpr uint32_t inclusive =
      FunctionalValueAtomLowerInclusive | FunctionalValueAtomUpperInclusive;
  database.resolvedFunctionalValueAtoms.push_back(
      {150, 0, 0, 0, 1, 0, FunctionalValueAtomKind::IntegralValue, inclusive});
  database.resolvedFunctionalValueAtoms.push_back(
      {151, 0, 0, 1, 1, 0, FunctionalValueAtomKind::IntegralValue, inclusive});
  database.resolvedFunctionalValueAtoms.push_back(
      {152, 0, 0, 2, 1, 0, FunctionalValueAtomKind::IntegralValue, inclusive});
  database.resolvedFunctionalValueLimbs.push_back({150, 0, 0, 0, 0, 0, 0, 0});
  database.resolvedFunctionalValueLimbs.push_back({151, 0, 0, 1, 0, 1, 0, 0});
  database.resolvedFunctionalValueLimbs.push_back({152, 0, 0, 2, 0, 2, 0, 0});
  database.resolvedFunctionalBinPlans.push_back({10, {}, 130, 150});
  database.resolvedFunctionalBinPlans.push_back({10, {}, 131, 151});
  database.resolvedFunctionalBinPlans.push_back({10, {}, 140, 152});
  database.resolvedFunctionalBinGroups.push_back(
      {10,
       {},
       120,
       30,
       0,
       1,
       0,
       FunctionalBinArrayMode::Scalar,
       FunctionalBinDistributionKind::None,
       0,
       FunctionalBinKind::State});
  database.resolvedFunctionalBinGroups.push_back(
      {10,
       {},
       120,
       31,
       1,
       1,
       0,
       FunctionalBinArrayMode::Scalar,
       FunctionalBinDistributionKind::None,
       0,
       FunctionalBinKind::State});
  database.resolvedFunctionalBinGroups.push_back(
      {10,
       {},
       121,
       40,
       2,
       1,
       0,
       FunctionalBinArrayMode::Scalar,
       FunctionalBinDistributionKind::None,
       0,
       FunctionalBinKind::State});

  Digest configuration =
      computeFunctionalConfigurationFingerprint(database, 10, Digest{});
  database.functionalConfigurations.front().configuration = configuration;
  for (auto &item : database.resolvedFunctionalItems)
    item.configuration = configuration;
  for (auto &bin : database.resolvedFunctionalBins)
    bin.configuration = configuration;
  for (auto &set : database.resolvedFunctionalValueSets)
    set.configuration = configuration;
  for (auto &plan : database.resolvedFunctionalBinPlans)
    plan.configuration = configuration;
  for (auto &group : database.resolvedFunctionalBinGroups)
    group.configuration = configuration;
  return configuration;
}

class CoverageRuntimeDesignTest : public testing::Test {
protected:
  obelisk_rt_context *
  createContextForSchema(const obelisk::coverage::Database &schema,
                         bool preResolve = false);

private:
  struct Execution {
    obelisk_rt_execution_descriptor_v1 descriptor{};
    obelisk_rt_execution_extension_v1 extension{};
    std::vector<uint8_t> image;
  };
  // Contexts borrow execution metadata, so it must survive helper return and
  // every scheduler or GC operation until the test destroys those contexts.
  std::vector<std::unique_ptr<Execution>> executions;
};

obelisk_rt_context *CoverageRuntimeDesignTest::createContextForSchema(
    const obelisk::coverage::Database &schema, bool preResolve) {
  obelisk::coverage::Database runtimeSchema = schema;
  if (preResolve && runtimeSchema.functionalConfigurations.empty())
    addResolvedFunctionalConfiguration(runtimeSchema);
  runtimeSchema.schemaFingerprint =
      obelisk::coverage::computeSchemaFingerprint(runtimeSchema);
  std::vector<uint8_t> image;
  obelisk::coverage::Diagnostic diagnostic;
  if (obelisk::coverage::serialize(runtimeSchema, image, &diagnostic) !=
      obelisk::coverage::Status::Ok) {
    ADD_FAILURE() << "failed to serialize embedded coverage schema: "
                  << static_cast<unsigned>(diagnostic.status) << " field="
                  << (diagnostic.field ? diagnostic.field : "<none>")
                  << " detail=" << diagnostic.detail;
    return nullptr;
  }
  Execution &execution = *executions.emplace_back(std::make_unique<Execution>());
  execution.image = std::move(image);
  execution.descriptor.version = OBELISK_RT_VERSION;
  execution.descriptor.flags = OBELISK_RT_EXECUTION_COVERAGE_SCHEMA;
  execution.descriptor.reserved = sizeof(execution.descriptor);
  execution.descriptor.state_bit_count = 1;
  execution.extension.version = OBELISK_RT_EXECUTION_EXTENSION_VERSION;
  execution.extension.size = sizeof(execution.extension);
  execution.extension.coverage_schema = execution.image.data();
  execution.extension.coverage_schema_size = execution.image.size();
  obelisk_rt_context *result = nullptr;
  if (obelisk_rt_v1_context_create_for_design(&execution.descriptor, &result) !=
      OBELISK_RT_OK)
    ADD_FAILURE() << "failed to create context with embedded coverage schema";
  return result;
}

obelisk_rt_status createFunctional(obelisk_rt_context *context, uint64_t type,
                                   obelisk_rt_covergroup_v1 *handle) {
  return obelisk_rt_v1_covergroup_create(context, type, nullptr, 0, nullptr, 0,
                                         handle);
}

obelisk_rt_status sampleFunctional(obelisk_rt_context *context,
                                   obelisk_rt_covergroup_v1 handle,
                                   uint8_t first, uint8_t second) {
  std::array<obelisk_rt_functional_value_v1, 2> values{};
  values[0] = {60,      2,       1, &first,
               nullptr, nullptr, 0, OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL,
               0};
  values[1] = {61,      2,       1, &second,
               nullptr, nullptr, 0, OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL,
               0};
  return obelisk_rt_v1_covergroup_sample(context, handle, values.data(),
                                         values.size());
}

obelisk::coverage::Database saveCoverage(obelisk_rt_context *context,
                                         const std::string &suffix) {
  std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("obelisk-functional-" + suffix + "-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()) +
       ".obcov");
  std::string pathText = path.string();
  EXPECT_EQ(obelisk_rt_v1_coverage_save(context, pathText.data(),
                                        pathText.size(), 0, 0),
            OBELISK_RT_OK);
  obelisk::coverage::Database result;
  EXPECT_EQ(obelisk::coverage::readFile(pathText, result),
            obelisk::coverage::Status::Ok);
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
  return result;
}

TEST_F(CoverageRuntimeTest, LineHitsAreAtomicAndSaturatingServiceIsQueryable) {
  ASSERT_EQ(finalizeCoverage(context, 3, 0, nullptr, nullptr),
            OBELISK_RT_OK);
  EXPECT_EQ(finalizeCoverage(context, 3, 0, nullptr, nullptr),
            OBELISK_RT_INVALID_LIFECYCLE);
  std::vector<std::thread> workers;
  for (unsigned worker = 0; worker != 4; ++worker)
    workers.emplace_back([&] {
      for (unsigned iteration = 0; iteration != 1000; ++iteration)
        EXPECT_EQ(obelisk_rt_v1_coverage_point_hit(context, iteration & 1, 1),
                  OBELISK_RT_OK);
    });
  for (auto &worker : workers)
    worker.join();
  uint64_t covered = 0, total = 0;
  double percentage = 0;
  ASSERT_EQ(obelisk_rt_v1_coverage_query(context, OBELISK_RT_COVERAGE_STATEMENT,
                                         &covered, &total, &percentage),
            OBELISK_RT_OK);
  EXPECT_EQ(covered, 2u);
  EXPECT_EQ(total, 3u);
  EXPECT_DOUBLE_EQ(percentage, 200.0 / 3.0);
  ASSERT_EQ(obelisk_rt_v1_coverage_reset(context, OBELISK_RT_COVERAGE_STATEMENT,
                                         nullptr, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_coverage_query(context, OBELISK_RT_COVERAGE_STATEMENT,
                                         &covered, &total, &percentage),
            OBELISK_RT_OK);
  EXPECT_EQ(covered, 0u);
}

TEST_F(CoverageRuntimeDesignTest,
     CrossNumPrintMissingReadsDefaultsAndPreservesExplicitOverride) {
  obelisk::coverage::Database schema = makeSharedAndSelectorSchema();
  obelisk_rt_context *context = createContextForSchema(schema, false);
  ASSERT_NE(context, nullptr);
  ASSERT_EQ(finalizeCoverage(context, 0, 0, nullptr, nullptr), OBELISK_RT_OK);
  obelisk_rt_covergroup_v1 handle = 0;
  ASSERT_EQ(createFunctional(context, 10, &handle), OBELISK_RT_OK);

  EXPECT_EQ(obelisk_rt_v1_covergroup_set_integer_option(
                context, handle, 0,
                OBELISK_RT_COVERGROUP_OPTION_CROSS_NUM_PRINT_MISSING,
                int64_t{INT32_MAX} + 1),
            OBELISK_RT_INVALID_ARGUMENT);
  ASSERT_EQ(obelisk_rt_v1_covergroup_set_integer_option(
                context, handle, 0,
                OBELISK_RT_COVERGROUP_OPTION_CROSS_NUM_PRINT_MISSING, 7),
            OBELISK_RT_OK);
  int64_t value = 0;
  ASSERT_EQ(obelisk_rt_v1_covergroup_get_integer_option(
                context, handle, 0,
                OBELISK_RT_COVERGROUP_OPTION_CROSS_NUM_PRINT_MISSING, &value),
            OBELISK_RT_OK);
  EXPECT_EQ(value, 7);
  ASSERT_EQ(obelisk_rt_v1_covergroup_get_integer_option(
                context, handle, 22,
                OBELISK_RT_COVERGROUP_OPTION_CROSS_NUM_PRINT_MISSING, &value),
            OBELISK_RT_OK);
  EXPECT_EQ(value, 7);

  ASSERT_EQ(obelisk_rt_v1_covergroup_set_integer_option(
                context, handle, 22,
                OBELISK_RT_COVERGROUP_OPTION_CROSS_NUM_PRINT_MISSING, 3),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_covergroup_set_integer_option(
                context, handle, 0,
                OBELISK_RT_COVERGROUP_OPTION_CROSS_NUM_PRINT_MISSING, 9),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_covergroup_get_integer_option(
                context, handle, 22,
                OBELISK_RT_COVERGROUP_OPTION_CROSS_NUM_PRINT_MISSING, &value),
            OBELISK_RT_OK);
  EXPECT_EQ(value, 3);

  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeDesignTest,
     MutableOptionUpdatesPreserveOwnerLocalThresholds) {
  auto schema = makeSharedAndSelectorSchema();
  // Serialized ID order differs from declaration order and bin ownership.
  schema.functionalItems[0].ordinal = 1;
  schema.functionalItems[1].ordinal = 0;
  schema.schemaFingerprint =
      obelisk::coverage::computeSchemaFingerprint(schema);
  obelisk_rt_context *context = createContextForSchema(schema, false);
  ASSERT_NE(context, nullptr);
  ASSERT_EQ(finalizeCoverage(context, 0, 0, nullptr, nullptr), OBELISK_RT_OK);
  obelisk_rt_covergroup_v1 changed = 0, other = 0;
  ASSERT_EQ(createFunctional(context, 10, &changed), OBELISK_RT_OK);
  ASSERT_EQ(createFunctional(context, 10, &other), OBELISK_RT_OK);
  auto write = [&](uint64_t item,
                   obelisk_rt_covergroup_instance_option_v1 option,
                   int64_t value) {
    ASSERT_EQ(obelisk_rt_v1_covergroup_set_integer_option(context, changed,
                                                          item, option, value),
              OBELISK_RT_OK);
  };
  write(20, OBELISK_RT_COVERGROUP_OPTION_WEIGHT, 3);
  write(21, OBELISK_RT_COVERGROUP_OPTION_WEIGHT, 7);
  write(22, OBELISK_RT_COVERGROUP_OPTION_WEIGHT, 0);
  write(20, OBELISK_RT_COVERGROUP_OPTION_AT_LEAST, 4);
  write(0, OBELISK_RT_COVERGROUP_OPTION_AT_LEAST, 3);
  write(22, OBELISK_RT_COVERGROUP_OPTION_AT_LEAST, 5);
  write(0, OBELISK_RT_COVERGROUP_OPTION_AT_LEAST, 2);
  for (auto [item, threshold, weight] :
       {std::array<int64_t, 3>{20, 4, 3}, {21, 2, 7}, {22, 5, 0}}) {
    int64_t value = -1;
    ASSERT_EQ(obelisk_rt_v1_covergroup_get_integer_option(
                  context, changed, item, OBELISK_RT_COVERGROUP_OPTION_AT_LEAST,
                  &value),
              OBELISK_RT_OK);
    EXPECT_EQ(value, threshold);
    ASSERT_EQ(obelisk_rt_v1_covergroup_get_integer_option(
                  context, changed, item, OBELISK_RT_COVERGROUP_OPTION_WEIGHT,
                  &value),
              OBELISK_RT_OK);
    EXPECT_EQ(value, weight);
    ASSERT_EQ(obelisk_rt_v1_covergroup_get_integer_option(
                  context, other, item, OBELISK_RT_COVERGROUP_OPTION_AT_LEAST,
                  &value),
              OBELISK_RT_OK);
    EXPECT_EQ(value, 1);
  }
  auto query = [&](obelisk_rt_covergroup_v1 handle, uint64_t item,
                   double expected) {
    double percentage = -1.0;
    int32_t covered = -1, total = -1;
    ASSERT_EQ(obelisk_rt_v1_covergroup_instance_query(
                  context, handle, item, &percentage, &covered, &total),
              OBELISK_RT_OK);
    EXPECT_DOUBLE_EQ(percentage, expected);
  };
  for (unsigned hit = 0; hit != 3; ++hit)
    ASSERT_EQ(sampleFunctional(context, changed, 0, 2), OBELISK_RT_OK);
  query(changed, 20, 0.0);
  query(changed, 21, 100.0);
  query(changed, 22, 0.0);
  ASSERT_EQ(sampleFunctional(context, changed, 0, 2), OBELISK_RT_OK);
  query(changed, 20, 50.0);
  query(changed, 22, 0.0);
  ASSERT_EQ(sampleFunctional(context, changed, 0, 2), OBELISK_RT_OK);
  query(changed, 22, 100.0);
  ASSERT_EQ(sampleFunctional(context, other, 0, 2), OBELISK_RT_OK);
  query(other, 20, 50.0);
  query(other, 22, 100.0);
  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeDesignTest,
     OptionReadsHonorDefaultsAndRejectInvalidScopes) {
  const auto schema = makeSharedAndSelectorSchema();
  obelisk_rt_context *context = createContextForSchema(schema, false);
  ASSERT_NE(context, nullptr);
  ASSERT_EQ(finalizeCoverage(context, 0, 0, nullptr, nullptr), OBELISK_RT_OK);
  obelisk_rt_covergroup_v1 handle = 0;
  ASSERT_EQ(createFunctional(context, 10, &handle), OBELISK_RT_OK);
  struct Read {
    uint64_t item;
    obelisk_rt_covergroup_instance_option_v1 option;
    int64_t value;
  };
  const Read defaults[] = {
      {0, OBELISK_RT_COVERGROUP_OPTION_AUTO_BIN_MAX, 64},
      {20, OBELISK_RT_COVERGROUP_OPTION_AUTO_BIN_MAX, 64},
      {0, OBELISK_RT_COVERGROUP_OPTION_DETECT_OVERLAP, 0},
      {20, OBELISK_RT_COVERGROUP_OPTION_DETECT_OVERLAP, 0},
      {0, OBELISK_RT_COVERGROUP_OPTION_CROSS_RETAIN_AUTO_BINS, 1},
      {22, OBELISK_RT_COVERGROUP_OPTION_CROSS_RETAIN_AUTO_BINS, 1},
      {0, OBELISK_RT_COVERGROUP_OPTION_PER_INSTANCE, 0},
      {0, OBELISK_RT_COVERGROUP_OPTION_GET_INST_COVERAGE, 0}};
  for (const auto &read : defaults) {
    int64_t value = -99;
    ASSERT_EQ(obelisk_rt_v1_covergroup_get_integer_option(
                  context, handle, read.item, read.option, &value),
              OBELISK_RT_OK);
    EXPECT_EQ(value, read.value);
    EXPECT_EQ(obelisk_rt_v1_covergroup_set_integer_option(
                  context, handle, read.item, read.option, read.value),
              OBELISK_RT_INVALID_ARGUMENT);
  }
  for (const auto &read :
       {Read{22, OBELISK_RT_COVERGROUP_OPTION_AUTO_BIN_MAX, 0},
        Read{22, OBELISK_RT_COVERGROUP_OPTION_DETECT_OVERLAP, 0},
        Read{20, OBELISK_RT_COVERGROUP_OPTION_CROSS_RETAIN_AUTO_BINS, 0},
        Read{20, OBELISK_RT_COVERGROUP_OPTION_PER_INSTANCE, 0},
        Read{22, OBELISK_RT_COVERGROUP_OPTION_GET_INST_COVERAGE, 0},
        Read{0, OBELISK_RT_COVERGROUP_OPTION_NAME, 0}}) {
    int64_t value = -99;
    EXPECT_EQ(obelisk_rt_v1_covergroup_get_integer_option(
                  context, handle, read.item, read.option, &value),
              OBELISK_RT_INVALID_ARGUMENT);
    EXPECT_EQ(value, -99);
  }
  int64_t value = -99;
  EXPECT_EQ(obelisk_rt_v1_covergroup_get_integer_option(
                context, handle, 999, OBELISK_RT_COVERGROUP_OPTION_AUTO_BIN_MAX,
                &value),
            OBELISK_RT_INVALID_DESIGN);
  EXPECT_EQ(value, -99);
  EXPECT_EQ(
      obelisk_rt_v1_covergroup_get_integer_option(
          context, 0, 0, OBELISK_RT_COVERGROUP_OPTION_AUTO_BIN_MAX, &value),
      OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(obelisk_rt_v1_covergroup_get_integer_option(
                context, handle, 0, OBELISK_RT_COVERGROUP_OPTION_AUTO_BIN_MAX,
                nullptr),
            OBELISK_RT_INVALID_ARGUMENT);
  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeDesignTest, OptionNBAsKeepCapturedPathsAndStringsRooted) {
  const auto schema = makeSharedAndSelectorSchema();
  obelisk_rt_context *context = createContextForSchema(schema, false);
  ASSERT_NE(context, nullptr);
  ASSERT_EQ(finalizeCoverage(context, 0, 0, nullptr, nullptr), OBELISK_RT_OK);
  obelisk_rt_covergroup_v1 handle = 0;
  ASSERT_EQ(createFunctional(context, 10, &handle), OBELISK_RT_OK);
  obelisk_rt_gc_lane_v1 *lane = nullptr;
  bool entered = false;
  ASSERT_EQ(obelisk_rt_managed_execution_enter(context, &lane, &entered),
            OBELISK_RT_OK);
  obelisk_rt_object_v1 *path = nullptr;
  obelisk_rt_gc_root_v1 pathRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &pathRoot, &path), OBELISK_RT_OK);
  obelisk_rt_string_v1 text = 0;
  obelisk_rt_gc_managed_root_range_v1 textRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_managed_root_range_push(lane, &textRoot, &text, 1),
            OBELISK_RT_OK);
  auto capture = [&](uint64_t item,
                     obelisk_rt_covergroup_instance_option_v1 option,
                     uint64_t mask, int64_t index, bool character) {
    return obelisk_rt_v1_reference_path_covergroup_option_create(
        lane, handle, 0, item, option, mask, index, character, nullptr, 0, 0,
        nullptr, nullptr, 0, &path);
  };
  auto enqueue = [&](const void *value, uint64_t size, uint64_t delay = 0) {
    return obelisk_rt_v1_scheduler_managed_nba(context, path, UINT64_MAX, value,
                                               nullptr, size, delay);
  };
  uint64_t bits = 16;
  ASSERT_EQ(
      capture(0, OBELISK_RT_COVERGROUP_OPTION_WEIGHT, UINT32_MAX, 0, false),
      OBELISK_RT_OK);
  ASSERT_EQ(enqueue(&bits, sizeof(bits)), OBELISK_RT_OK);
  bits = 3;
  ASSERT_EQ(capture(0, OBELISK_RT_COVERGROUP_OPTION_WEIGHT, 15, 0, false),
            OBELISK_RT_OK);
  ASSERT_EQ(enqueue(&bits, sizeof(bits)), OBELISK_RT_OK);
  bits = 0;
  ASSERT_EQ(capture(0, OBELISK_RT_COVERGROUP_OPTION_WEIGHT, 2, 0, false),
            OBELISK_RT_OK);
  ASSERT_EQ(enqueue(&bits, sizeof(bits)), OBELISK_RT_OK);
  constexpr char message[] = "whole rhs snapshot";
  ASSERT_EQ(
      obelisk_rt_v1_string_create(lane, message, sizeof(message) - 1, &text),
      OBELISK_RT_OK);
  ASSERT_EQ(capture(20, OBELISK_RT_COVERGROUP_OPTION_COMMENT, 0, 0, false),
            OBELISK_RT_OK);
  ASSERT_EQ(enqueue(&text, sizeof(text)), OBELISK_RT_OK);
  uint8_t character = 'W';
  ASSERT_EQ(capture(20, OBELISK_RT_COVERGROUP_OPTION_COMMENT, 0, 0, true),
            OBELISK_RT_OK);
  ASSERT_EQ(enqueue(&character, sizeof(character)), OBELISK_RT_OK);
  bits = 2;
  ASSERT_EQ(
      capture(20, OBELISK_RT_COVERGROUP_OPTION_AT_LEAST, UINT32_MAX, 0, false),
      OBELISK_RT_OK);
  ASSERT_EQ(enqueue(&bits, sizeof(bits), 2), OBELISK_RT_OK);
  // A static option path retains the normal field binding as well as the
  // type identity. Its profile and ordinary storage must commit together.
  std::array<uint8_t, 4> storage{50, 0, 0, 0};
  ASSERT_EQ(obelisk_rt_v1_covergroup_set_type_integer_option(
                context, 10, 0, OBELISK_RT_COVERGROUP_OPTION_GOAL, 50),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_reference_path_covergroup_option_create(
                lane, 0, 10, 0, OBELISK_RT_COVERGROUP_OPTION_GOAL, UINT32_MAX,
                0, 0, nullptr, 0, 0, storage.data(), nullptr, 32, &path),
            OBELISK_RT_OK);
  bits = 80;
  ASSERT_EQ(enqueue(&bits, sizeof(bits)), OBELISK_RT_OK);
  EXPECT_EQ(capture(20, OBELISK_RT_COVERGROUP_OPTION_CROSS_NUM_PRINT_MISSING,
                    UINT32_MAX, 0, false),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(path, nullptr);
  EXPECT_EQ(capture(0, OBELISK_RT_COVERGROUP_OPTION_AUTO_BIN_MAX, UINT32_MAX, 0,
                    false),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(path, nullptr);
  // Only scheduled roots now keep the captured paths and heap string alive.
  path = nullptr;
  text = 0;
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  int64_t weight = -1;
  ASSERT_EQ(
      obelisk_rt_v1_covergroup_get_integer_option(
          context, handle, 0, OBELISK_RT_COVERGROUP_OPTION_WEIGHT, &weight),
      OBELISK_RT_OK);
  EXPECT_EQ(weight, 1);
  EXPECT_EQ(storage[0], 50);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(context), OBELISK_RT_OK);
  ASSERT_EQ(
      obelisk_rt_v1_covergroup_get_integer_option(
          context, handle, 0, OBELISK_RT_COVERGROUP_OPTION_WEIGHT, &weight),
      OBELISK_RT_OK);
  EXPECT_EQ(weight, 17);
  ASSERT_EQ(
      obelisk_rt_v1_covergroup_get_integer_option(
          context, handle, 20, OBELISK_RT_COVERGROUP_OPTION_AT_LEAST, &weight),
      OBELISK_RT_OK);
  EXPECT_EQ(weight, 2);
  EXPECT_EQ(storage[0], 80);
  ASSERT_EQ(obelisk_rt_v1_covergroup_get_string_option(
                lane, handle, 20, OBELISK_RT_COVERGROUP_OPTION_COMMENT, &text),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  char scratch[8];
  const char *bytes = nullptr;
  uint64_t size = 0;
  ASSERT_EQ(obelisk_rt_v1_string_view(text, scratch, &bytes, &size),
            OBELISK_RT_OK);
  EXPECT_EQ(std::string(bytes, size), "Whole rhs snapshot");
  ASSERT_EQ(obelisk_rt_v1_gc_managed_root_range_pop(lane, &textRoot),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_root_pop(lane, &pathRoot), OBELISK_RT_OK);
  obelisk_rt_managed_execution_leave(lane, entered);
  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeDesignTest, StringOptionReadsReturnIndependentGcSnapshots) {
  const auto schema = makeSharedAndSelectorSchema();
  obelisk_rt_context *context = createContextForSchema(schema, false);
  ASSERT_NE(context, nullptr);
  ASSERT_EQ(finalizeCoverage(context, 0, 0, nullptr, nullptr), OBELISK_RT_OK);
  obelisk_rt_covergroup_v1 first = 0, second = 0;
  ASSERT_EQ(createFunctional(context, 10, &first), OBELISK_RT_OK);
  ASSERT_EQ(createFunctional(context, 10, &second), OBELISK_RT_OK);
  obelisk_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(obelisk_rt_v1_gc_lane_create(context, &lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_enter(lane), OBELISK_RT_OK);
  obelisk_rt_string_v1 values[4]{};
  obelisk_rt_gc_managed_root_range_v1 roots{};
  ASSERT_EQ(obelisk_rt_v1_gc_managed_root_range_push(lane, &roots, values, 4),
            OBELISK_RT_OK);
  auto expectText = [](obelisk_rt_string_v1 value,
                       const std::string &expected) {
    char scratch[8];
    const char *bytes = nullptr;
    uint64_t size = 0;
    ASSERT_EQ(obelisk_rt_v1_string_view(value, scratch, &bytes, &size),
              OBELISK_RT_OK);
    EXPECT_EQ(std::string(bytes, size), expected);
  };
  ASSERT_EQ(obelisk_rt_v1_covergroup_get_string_option(
                lane, first, 0, OBELISK_RT_COVERGROUP_OPTION_NAME, &values[0]),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_covergroup_get_string_option(
                lane, second, 0, OBELISK_RT_COVERGROUP_OPTION_NAME, &values[1]),
            OBELISK_RT_OK);
  EXPECT_NE(values[0], values[1]);
  expectText(values[0], "$auto$1");
  expectText(values[1], "$auto$2");
  for (uint64_t item : {uint64_t{0}, uint64_t{20}, uint64_t{22}}) {
    ASSERT_EQ(obelisk_rt_v1_covergroup_get_string_option(
                  lane, first, item, OBELISK_RT_COVERGROUP_OPTION_COMMENT,
                  &values[2]),
              OBELISK_RT_OK);
    expectText(values[2], "");
  }
  const std::string comment = "a long point comment with independent storage";
  ASSERT_EQ(obelisk_rt_v1_string_create(lane, comment.data(), comment.size(),
                                        &values[3]),
            OBELISK_RT_OK);
  ASSERT_EQ(
      obelisk_rt_v1_covergroup_set_string_option(
          context, first, 20, OBELISK_RT_COVERGROUP_OPTION_COMMENT, values[3]),
      OBELISK_RT_OK);
  ASSERT_EQ(
      obelisk_rt_v1_covergroup_get_string_option(
          lane, first, 20, OBELISK_RT_COVERGROUP_OPTION_COMMENT, &values[2]),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_string_create(lane, "edited", 6, &values[3]),
            OBELISK_RT_OK);
  ASSERT_EQ(
      obelisk_rt_v1_covergroup_set_string_option(
          context, first, 20, OBELISK_RT_COVERGROUP_OPTION_COMMENT, values[3]),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_covergroup_set_name(context, first, values[3]),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  expectText(values[0], "$auto$1");
  expectText(values[2], comment);
  ASSERT_EQ(
      obelisk_rt_v1_covergroup_get_string_option(
          lane, first, 20, OBELISK_RT_COVERGROUP_OPTION_COMMENT, &values[3]),
      OBELISK_RT_OK);
  expectText(values[3], "edited");
  ASSERT_EQ(obelisk_rt_v1_covergroup_get_string_option(
                lane, first, 0, OBELISK_RT_COVERGROUP_OPTION_NAME, &values[3]),
            OBELISK_RT_OK);
  expectText(values[3], "edited");
  for (const auto &invalid :
       {std::make_pair(uint64_t{20}, OBELISK_RT_COVERGROUP_OPTION_NAME),
        std::make_pair(uint64_t{0}, OBELISK_RT_COVERGROUP_OPTION_WEIGHT)}) {
    EXPECT_EQ(obelisk_rt_v1_covergroup_get_string_option(
                  lane, first, invalid.first, invalid.second, &values[3]),
              OBELISK_RT_INVALID_ARGUMENT);
    expectText(values[3], "edited");
  }
  EXPECT_EQ(
      obelisk_rt_v1_covergroup_get_string_option(
          lane, first, 999, OBELISK_RT_COVERGROUP_OPTION_COMMENT, &values[3]),
      OBELISK_RT_INVALID_DESIGN);
  EXPECT_EQ(
      obelisk_rt_v1_covergroup_get_string_option(
          nullptr, first, 0, OBELISK_RT_COVERGROUP_OPTION_NAME, &values[3]),
      OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_covergroup_get_string_option(
                lane, 0, 0, OBELISK_RT_COVERGROUP_OPTION_NAME, &values[3]),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(obelisk_rt_v1_covergroup_get_string_option(
                lane, first, 0, OBELISK_RT_COVERGROUP_OPTION_NAME, nullptr),
            OBELISK_RT_INVALID_ARGUMENT);
  ASSERT_EQ(obelisk_rt_v1_gc_managed_root_range_pop(lane, &roots),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_leave(lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_destroy(lane), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeDesignTest, Clause40ControlAndQueryHonorScopeSelection) {
  obelisk::coverage::Database schema = makeScopedCodeCoverageSchema();
  obelisk_rt_context *context = createContextForSchema(schema, false);
  ASSERT_NE(context, nullptr);
  constexpr std::array<uint8_t, 1> zero{0};
  ASSERT_EQ(finalizeCoverage(context, 4, 3, zero.data(), zero.data()),
            OBELISK_RT_OK);

  obelisk_rt_string_v1 dut = 0;
  obelisk_rt_string_v1 missing = 0;
  ASSERT_EQ(obelisk_rt_v1_string_create(nullptr, "DUT", 3, &dut),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_string_create(nullptr, "missing", 7, &missing),
            OBELISK_RT_OK);

  int32_t result = -99;
  EXPECT_EQ(obelisk_rt_v1_coverage_control_definition(context, 3, 22, 10, dut,
                                                      &result),
            OBELISK_RT_OK);
  EXPECT_EQ(result, 2); // SV_COV_PARTIAL: one statement is excluded.
  EXPECT_EQ(
      obelisk_rt_v1_coverage_query_definition(context, 22, 10, dut, 1, &result),
      OBELISK_RT_OK);
  EXPECT_EQ(result, 2);
  EXPECT_EQ(
      obelisk_rt_v1_coverage_query_definition(context, 22, 11, dut, 1, &result),
      OBELISK_RT_OK);
  EXPECT_EQ(result, 3);
  EXPECT_EQ(
      obelisk_rt_v1_coverage_query_instance(context, 22, 10, 3, 1, &result),
      OBELISK_RT_OK);
  EXPECT_EQ(result, 1);
  EXPECT_EQ(
      obelisk_rt_v1_coverage_query_instance(context, 22, 11, 3, 1, &result),
      OBELISK_RT_OK);
  EXPECT_EQ(result, 2);
  EXPECT_EQ(
      obelisk_rt_v1_coverage_query_instance(context, 22, 10, 1, 1, &result),
      OBELISK_RT_OK);
  EXPECT_EQ(result, 0);
  EXPECT_EQ(
      obelisk_rt_v1_coverage_query_instance(context, 22, 11, 1, 1, &result),
      OBELISK_RT_OK);
  EXPECT_EQ(result, 3);

  ASSERT_EQ(obelisk_rt_v1_coverage_point_hit(context, 0, 1), OBELISK_RT_OK);
  EXPECT_EQ(
      obelisk_rt_v1_coverage_query_definition(context, 22, 10, dut, 0, &result),
      OBELISK_RT_OK);
  EXPECT_EQ(result, 1);
  EXPECT_EQ(
      obelisk_rt_v1_coverage_control_instance(context, 1, 22, 11, 3, &result),
      OBELISK_RT_OK);
  EXPECT_EQ(result, 1);
  ASSERT_EQ(obelisk_rt_v1_coverage_point_hit(context, 0, 1), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_coverage_point_hit(context, 2, 1), OBELISK_RT_OK);
  EXPECT_EQ(
      obelisk_rt_v1_coverage_query_instance(context, 22, 10, 5, 0, &result),
      OBELISK_RT_OK);
  EXPECT_EQ(result, 0);
  EXPECT_EQ(
      obelisk_rt_v1_coverage_control_instance(context, 0, 22, 11, 3, &result),
      OBELISK_RT_OK);
  EXPECT_EQ(result, 1);
  ASSERT_EQ(obelisk_rt_v1_coverage_point_hit(context, 2, 1), OBELISK_RT_OK);
  EXPECT_EQ(
      obelisk_rt_v1_coverage_query_instance(context, 22, 10, 5, 0, &result),
      OBELISK_RT_OK);
  EXPECT_EQ(result, 1);
  EXPECT_EQ(obelisk_rt_v1_coverage_control_definition(context, 2, 22, 10, dut,
                                                      &result),
            OBELISK_RT_OK);
  EXPECT_EQ(result, 1);
  EXPECT_EQ(
      obelisk_rt_v1_coverage_query_definition(context, 22, 11, dut, 0, &result),
      OBELISK_RT_OK);
  EXPECT_EQ(result, 1); // The descendant was outside SV_COV_MODULE reset.
  EXPECT_EQ(
      obelisk_rt_v1_coverage_control_instance(context, 2, 22, 11, 3, &result),
      OBELISK_RT_OK);
  EXPECT_EQ(result, 1);
  EXPECT_EQ(
      obelisk_rt_v1_coverage_query_instance(context, 22, 11, 3, 0, &result),
      OBELISK_RT_OK);
  EXPECT_EQ(result, 0);
  EXPECT_EQ(
      obelisk_rt_v1_coverage_control_instance(context, 0, 22, 11, 3, &result),
      OBELISK_RT_OK);
  EXPECT_EQ(result, 1);
  ASSERT_EQ(obelisk_rt_v1_coverage_point_hit(context, 0, 1), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_coverage_point_hit(context, 2, 1), OBELISK_RT_OK);
  EXPECT_EQ(
      obelisk_rt_v1_coverage_query_instance(context, 22, 11, 3, 0, &result),
      OBELISK_RT_OK);
  EXPECT_EQ(result, 2);

  constexpr std::array<uint8_t, 1> ones{0x7};
  ASSERT_EQ(obelisk_rt_v1_coverage_toggle_transition(context, 0, 3, ones.data(),
                                                     zero.data()),
            OBELISK_RT_OK);
  EXPECT_EQ(
      obelisk_rt_v1_coverage_control_instance(context, 1, 23, 10, 3, &result),
      OBELISK_RT_OK);
  EXPECT_EQ(result, 1);
  ASSERT_EQ(obelisk_rt_v1_coverage_toggle_transition(context, 0, 3, zero.data(),
                                                     zero.data()),
            OBELISK_RT_OK);
  EXPECT_EQ(
      obelisk_rt_v1_coverage_query_definition(context, 23, 10, dut, 0, &result),
      OBELISK_RT_OK);
  EXPECT_EQ(result, 3);
  EXPECT_EQ(
      obelisk_rt_v1_coverage_query_definition(context, 23, 11, dut, 0, &result),
      OBELISK_RT_OK);
  EXPECT_EQ(result, 5);
  EXPECT_EQ(obelisk_rt_v1_coverage_control_definition(context, 2, 23, 10, dut,
                                                      &result),
            OBELISK_RT_OK);
  EXPECT_EQ(result, 1);
  EXPECT_EQ(
      obelisk_rt_v1_coverage_query_definition(context, 23, 11, dut, 0, &result),
      OBELISK_RT_OK);
  EXPECT_EQ(result, 2);

  EXPECT_EQ(obelisk_rt_v1_coverage_control_definition(context, 3, 21, 10, dut,
                                                      &result),
            OBELISK_RT_OK);
  EXPECT_EQ(result, 0);
  EXPECT_EQ(obelisk_rt_v1_coverage_control_definition(context, 1, 21, 10, dut,
                                                      &result),
            OBELISK_RT_OK);
  EXPECT_EQ(result, 1);
  EXPECT_EQ(obelisk_rt_v1_coverage_query_definition(context, 22, 10, missing, 1,
                                                    &result),
            OBELISK_RT_OK);
  EXPECT_EQ(result, -1);
  EXPECT_EQ(
      obelisk_rt_v1_coverage_query_instance(context, 22, 10, 999, 1, &result),
      OBELISK_RT_OK);
  EXPECT_EQ(result, -1);
  EXPECT_EQ(obelisk_rt_v1_coverage_control_definition(context, 4, 22, 10, dut,
                                                      &result),
            OBELISK_RT_OK);
  EXPECT_EQ(result, -1);
  EXPECT_EQ(obelisk_rt_v1_coverage_control_definition(context, 3, 24, 10, dut,
                                                      &result),
            OBELISK_RT_OK);
  EXPECT_EQ(result, -1);
  EXPECT_EQ(
      obelisk_rt_v1_coverage_query_definition(context, 22, 12, dut, 1, &result),
      OBELISK_RT_OK);
  EXPECT_EQ(result, -1);
  EXPECT_EQ(
      obelisk_rt_v1_coverage_query_definition(context, 22, 10, dut, 2, &result),
      OBELISK_RT_INVALID_ARGUMENT);

  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeDesignTest,
     Clause40MetricDatabasesSaveFilterAndMergeTransactionally) {
  using namespace obelisk::coverage;
  Database schema = makeScopedCodeCoverageSchema();
  obelisk_rt_context *producer = createContextForSchema(schema, false);
  ASSERT_NE(producer, nullptr);
  constexpr std::array<uint8_t, 1> zero{0};
  constexpr std::array<uint8_t, 1> ones{0x7};
  ASSERT_EQ(
      finalizeCoverage(producer, 4, 3, zero.data(), zero.data()),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_coverage_point_hit(producer, 0, 1), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_coverage_toggle_transition(producer, 0, 3,
                                                     ones.data(), zero.data()),
            OBELISK_RT_OK);

  const std::string suffix = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  std::filesystem::path linePath =
      std::filesystem::temp_directory_path() /
      ("obelisk-clause40-line-" + suffix + ".obcov");
  std::filesystem::path togglePath =
      std::filesystem::temp_directory_path() /
      ("obelisk-clause40-toggle-" + suffix + ".obcov");
  auto save = [](obelisk_rt_context *context, int32_t coverageType,
                 const std::filesystem::path &path, int32_t *result) {
    return withManagedString(context, path.string(), [&](auto name) {
      return obelisk_rt_v1_coverage_database_save(context, coverageType, name,
                                                  result);
    });
  };
  auto merge = [](obelisk_rt_context *context, int32_t coverageType,
                  const std::filesystem::path &path, int32_t *result) {
    return withManagedString(context, path.string(), [&](auto name) {
      return obelisk_rt_v1_coverage_database_merge(context, coverageType, name,
                                                   result);
    });
  };
  int32_t result = -99;
  ASSERT_EQ(save(producer, 22, linePath, &result), OBELISK_RT_OK);
  EXPECT_EQ(result, 1);
  ASSERT_EQ(save(producer, 23, togglePath, &result), OBELISK_RT_OK);
  EXPECT_EQ(result, 1);
  ASSERT_EQ(merge(producer, 22, linePath, &result), OBELISK_RT_OK);
  EXPECT_EQ(result, -1);
  ASSERT_EQ(save(producer, 22, linePath, &result), OBELISK_RT_OK);
  EXPECT_EQ(result, 1);

  Database savedLine, savedToggle;
  ASSERT_EQ(readFile(linePath.string(), savedLine), Status::Ok);
  ASSERT_EQ(readFile(togglePath.string(), savedToggle), Status::Ok);
  ASSERT_EQ(savedLine.runs.size(), 1u);
  ASSERT_EQ(savedToggle.runs.size(), 1u);
  EXPECT_EQ(savedLine.runs.front().flags, RunContainsLine);
  EXPECT_EQ(savedToggle.runs.front().flags, RunContainsToggle);
  EXPECT_TRUE(std::all_of(savedLine.counters.begin(), savedLine.counters.end(),
                          [](const Counter &counter) {
                            return counter.metric == MetricKind::Line;
                          }));
  EXPECT_TRUE(std::all_of(savedToggle.counters.begin(),
                          savedToggle.counters.end(),
                          [](const Counter &counter) {
                            return counter.metric == MetricKind::Toggle;
                          }));
  EXPECT_EQ(savedLine.schemaFingerprint, savedToggle.schemaFingerprint);

  Database metadataConflict = savedToggle;
  metadataConflict.runs.front().name = "conflicting-test-name";
  std::filesystem::path metadataConflictPath =
      std::filesystem::temp_directory_path() /
      ("obelisk-clause40-metadata-conflict-" + suffix + ".obcov");
  ASSERT_EQ(
      writeFileAtomically(metadataConflictPath.string(), metadataConflict),
      Status::Ok);

  ASSERT_EQ(save(producer, 21, linePath, &result), OBELISK_RT_OK);
  EXPECT_EQ(result, 0);
  Database untouched;
  ASSERT_EQ(readFile(linePath.string(), untouched), Status::Ok);
  EXPECT_EQ(untouched.runs.front().flags, RunContainsLine);

  obelisk_rt_context *consumer = createContextForSchema(schema, false);
  ASSERT_NE(consumer, nullptr);
  ASSERT_EQ(
      finalizeCoverage(consumer, 4, 3, zero.data(), zero.data()),
      OBELISK_RT_OK);
  ASSERT_EQ(merge(consumer, 22, linePath, &result), OBELISK_RT_OK);
  EXPECT_EQ(result, 1);
  ASSERT_EQ(merge(consumer, 23, metadataConflictPath, &result), OBELISK_RT_OK);
  EXPECT_EQ(result, -1);
  uint64_t covered = 0, total = 0;
  double percentage = 0.0;
  ASSERT_EQ(obelisk_rt_v1_coverage_query(consumer, OBELISK_RT_COVERAGE_TOGGLE,
                                         &covered, &total, &percentage),
            OBELISK_RT_OK);
  EXPECT_EQ(covered, 0u);
  ASSERT_EQ(merge(consumer, 23, togglePath, &result), OBELISK_RT_OK);
  EXPECT_EQ(result, 1);
  ASSERT_EQ(obelisk_rt_v1_coverage_query(consumer,
                                         OBELISK_RT_COVERAGE_STATEMENT,
                                         &covered, &total, &percentage),
            OBELISK_RT_OK);
  EXPECT_EQ(covered, 1u);
  ASSERT_EQ(obelisk_rt_v1_coverage_query(consumer, OBELISK_RT_COVERAGE_TOGGLE,
                                         &covered, &total, &percentage),
            OBELISK_RT_OK);
  EXPECT_EQ(covered, 3u);

  ASSERT_EQ(merge(consumer, 22, linePath, &result), OBELISK_RT_OK);
  EXPECT_EQ(result, -1);
  ASSERT_EQ(merge(consumer, 23, linePath, &result), OBELISK_RT_OK);
  EXPECT_EQ(result, 0);

  std::filesystem::path missingPath =
      std::filesystem::temp_directory_path() /
      ("obelisk-clause40-missing-" + suffix + ".obcov");
  ASSERT_EQ(merge(consumer, 22, missingPath, &result), OBELISK_RT_OK);
  EXPECT_EQ(result, -1);

  Database mismatched = savedLine;
  mismatched.linePoints.front().line += 1;
  mismatched.schemaFingerprint = computeSchemaFingerprint(mismatched);
  std::filesystem::path mismatchPath =
      std::filesystem::temp_directory_path() /
      ("obelisk-clause40-mismatch-" + suffix + ".obcov");
  ASSERT_EQ(writeFileAtomically(mismatchPath.string(), mismatched), Status::Ok);
  ASSERT_EQ(merge(consumer, 22, mismatchPath, &result), OBELISK_RT_OK);
  EXPECT_EQ(result, -1);

  ASSERT_EQ(save(producer, 99, linePath, &result), OBELISK_RT_OK);
  EXPECT_EQ(result, -1);
  EXPECT_FALSE(std::filesystem::exists(linePath));

  obelisk_rt_v1_context_destroy(consumer);
  obelisk_rt_v1_context_destroy(producer);
  std::error_code ignored;
  std::filesystem::remove(linePath, ignored);
  std::filesystem::remove(togglePath, ignored);
  std::filesystem::remove(metadataConflictPath, ignored);
  std::filesystem::remove(mismatchPath, ignored);
}

TEST_F(CoverageRuntimeDesignTest,
     Clause40QueriesAndScopedResetIncludeLoadedBaselines) {
  using namespace obelisk::coverage;
  Database schema = makeScopedCodeCoverageSchema();
  obelisk_rt_context *context = createContextForSchema(schema, false);
  ASSERT_NE(context, nullptr);
  constexpr std::array<uint8_t, 1> zero{0};
  ASSERT_EQ(
      finalizeCoverage(context, 4, 3, zero.data(), zero.data()),
      OBELISK_RT_OK);

  Database incoming = schema;
  obelisk::coverage::Run run;
  run.uuid.back() = 1;
  run.name = "baseline";
  run.flags = RunContainsLine | RunContainsToggle;
  incoming.runs.push_back(run);
  incoming.counters.push_back({run.uuid, MetricKind::Line, 10, 0, 0, 0, 1});
  incoming.counters.push_back({run.uuid, MetricKind::Toggle, 20, 0, 0, 0, 1});
  std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("obelisk-clause40-baseline-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()) +
       ".obcov");
  ASSERT_EQ(writeFileAtomically(path.string(), incoming), Status::Ok);
  std::string pathText = path.string();
  ASSERT_EQ(
      obelisk_rt_v1_coverage_load(context, pathText.data(), pathText.size()),
      OBELISK_RT_OK);

  obelisk_rt_string_v1 dut = 0;
  ASSERT_EQ(obelisk_rt_v1_string_create(nullptr, "DUT", 3, &dut),
            OBELISK_RT_OK);
  int32_t result = -99;
  EXPECT_EQ(
      obelisk_rt_v1_coverage_query_definition(context, 22, 10, dut, 0, &result),
      OBELISK_RT_OK);
  EXPECT_EQ(result, 1);
  EXPECT_EQ(
      obelisk_rt_v1_coverage_query_definition(context, 23, 10, dut, 0, &result),
      OBELISK_RT_OK);
  EXPECT_EQ(result, 1);

  EXPECT_EQ(obelisk_rt_v1_coverage_control_definition(context, 2, 22, 10, dut,
                                                      &result),
            OBELISK_RT_OK);
  EXPECT_EQ(result, 1);
  EXPECT_EQ(
      obelisk_rt_v1_coverage_query_definition(context, 22, 10, dut, 0, &result),
      OBELISK_RT_OK);
  EXPECT_EQ(result, 0);
  EXPECT_EQ(
      obelisk_rt_v1_coverage_query_definition(context, 23, 10, dut, 0, &result),
      OBELISK_RT_OK);
  EXPECT_EQ(result, 1);
  EXPECT_EQ(
      obelisk_rt_v1_coverage_control_instance(context, 2, 23, 10, 3, &result),
      OBELISK_RT_OK);
  EXPECT_EQ(result, 1);
  EXPECT_EQ(
      obelisk_rt_v1_coverage_query_definition(context, 23, 10, dut, 0, &result),
      OBELISK_RT_OK);
  EXPECT_EQ(result, 0);

  obelisk_rt_v1_context_destroy(context);
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

TEST_F(CoverageRuntimeTest, ToggleResetRebasesTheCoverageShadow) {
  constexpr std::array<uint8_t, 1> zero{0};
  ASSERT_EQ(
      finalizeCoverage(context, 0, 2, zero.data(), zero.data()),
      OBELISK_RT_OK);
  constexpr std::array<uint8_t, 1> one{1};
  ASSERT_EQ(obelisk_rt_v1_coverage_toggle_transition(context, 0, 2, one.data(),
                                                     zero.data()),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_coverage_toggle_transition(context, 0, 2, zero.data(),
                                                     zero.data()),
            OBELISK_RT_OK);
  uint64_t covered = 0, total = 0;
  double percentage = 0;
  ASSERT_EQ(obelisk_rt_v1_coverage_query(context, OBELISK_RT_COVERAGE_TOGGLE,
                                         &covered, &total, &percentage),
            OBELISK_RT_OK);
  EXPECT_EQ(covered, 2u);
  EXPECT_EQ(total, 4u);
  EXPECT_DOUBLE_EQ(percentage, 50.0);

  ASSERT_EQ(obelisk_rt_v1_coverage_reset(context, OBELISK_RT_COVERAGE_TOGGLE,
                                         one.data(), zero.data()),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_coverage_toggle_transition(context, 0, 2, zero.data(),
                                                     zero.data()),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_coverage_query(context, OBELISK_RT_COVERAGE_TOGGLE,
                                         &covered, &total, &percentage),
            OBELISK_RT_OK);
  EXPECT_EQ(covered, 1u);
  EXPECT_EQ(total, 4u);
  EXPECT_DOUBLE_EQ(percentage, 25.0);
}

TEST(CoverageBindingTest, CanonicalTransitionsUpdateEverySourceAlias) {
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 2;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 2),
            OBELISK_RT_OK);
  constexpr std::array<uint8_t, 1> zero{0};
  ASSERT_EQ(
      finalizeCoverage(context, 0, 4, zero.data(), zero.data()),
      OBELISK_RT_OK);
  uint64_t state = obelisk_rt_v1_native_state_static_handle(1);
  ASSERT_NE(state, UINT64_MAX);
  ASSERT_EQ(obelisk_rt_v1_coverage_toggle_bind(context, 0, 2, state),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_coverage_toggle_bind(context, 2, 2, state),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_coverage_toggle_bind(context, 1, 1, state),
            OBELISK_RT_INVALID_DESIGN);
  ASSERT_EQ(obelisk_rt_v1_coverage_toggle_seal(context), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_coverage_toggle_bind(context, 0, 1, state),
            OBELISK_RT_INVALID_LIFECYCLE);

  uint8_t oldValue = 0;
  uint8_t newValue = 1;
  obelisk_rt_v1_scheduler_signal_transition(context, state, 2, &oldValue,
                                            nullptr, &newValue, nullptr);
  uint64_t covered = 0, total = 0;
  double percentage = 0;
  ASSERT_EQ(obelisk_rt_v1_coverage_query(context, OBELISK_RT_COVERAGE_TOGGLE,
                                         &covered, &total, &percentage),
            OBELISK_RT_OK);
  EXPECT_EQ(covered, 2u);
  EXPECT_EQ(total, 8u);
  EXPECT_DOUBLE_EQ(percentage, 25.0);

  oldValue = 1;
  newValue = 0;
  obelisk_rt_v1_scheduler_signal_transition(context, state, 2, &oldValue,
                                            nullptr, &newValue, nullptr);
  ASSERT_EQ(obelisk_rt_v1_coverage_query(context, OBELISK_RT_COVERAGE_TOGGLE,
                                         &covered, &total, &percentage),
            OBELISK_RT_OK);
  EXPECT_EQ(covered, 4u);
  EXPECT_DOUBLE_EQ(percentage, 50.0);
  obelisk_rt_v1_context_destroy(context);
}

TEST(CoverageBindingTest, SealRejectsMissingIncludedBinding) {
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 2;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 2),
            OBELISK_RT_OK);
  constexpr std::array<uint8_t, 1> zero{0};
  ASSERT_EQ(
      finalizeCoverage(context, 0, 2, zero.data(), zero.data()),
      OBELISK_RT_OK);
  uint64_t state = obelisk_rt_v1_native_state_static_handle(1);
  ASSERT_EQ(obelisk_rt_v1_coverage_toggle_bind(context, 0, 1, state),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_coverage_toggle_seal(context),
            OBELISK_RT_INVALID_DESIGN);
  obelisk_rt_v1_context_destroy(context);
}

TEST(CoverageBindingTest, DisjointPhysicalSegmentsDoNotOverlap) {
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.state_bit_count = 131;
  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &context),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_register_static(context, 1, 0, 131),
            OBELISK_RT_OK);
  constexpr std::array<uint8_t, 1> zero{0};
  ASSERT_EQ(
      finalizeCoverage(context, 0, 5, zero.data(), zero.data()),
      OBELISK_RT_OK);
  uint64_t root = obelisk_rt_v1_native_state_static_handle(1);
  uint64_t high = obelisk_rt_v1_native_handle_offset(root, 128);
  ASSERT_EQ(obelisk_rt_v1_coverage_toggle_bind(context, 0, 2, root),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_coverage_toggle_bind(context, 2, 3, high),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_coverage_toggle_seal(context), OBELISK_RT_OK);

  constexpr uint8_t one = 3;
  obelisk_rt_v1_scheduler_signal_transition(context, root, 2, zero.data(),
                                            nullptr, &one, nullptr);
  uint64_t covered = 0, total = 0;
  double percentage = 0;
  ASSERT_EQ(obelisk_rt_v1_coverage_query(context, OBELISK_RT_COVERAGE_TOGGLE,
                                         &covered, &total, &percentage),
            OBELISK_RT_OK);
  EXPECT_EQ(covered, 2u);
  EXPECT_EQ(total, 10u);
  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeTest, CoverageCommandLineOptionsAreValidated) {
  const char *valid[] = {"sim",
                         "--coverage-output=run-%p-%t.obcov",
                         "--coverage-test=smoke/test",
                         "--coverage-tag=suite=ci",
                         "--coverage-tag=shard=3",
                         "--no-coverage-dump"};
  EXPECT_EQ(obelisk_rt_v1_context_configure_argv(
                context, static_cast<int>(std::size(valid)), valid),
            OBELISK_RT_OK);

  const char *duplicateTag[] = {"sim", "--coverage-tag=suite=a",
                                "--coverage-tag=suite=b"};
  EXPECT_EQ(
      obelisk_rt_v1_context_configure_argv(
          context, static_cast<int>(std::size(duplicateTag)), duplicateTag),
      OBELISK_RT_INVALID_ARGUMENT);
  const char *emptyOutput[] = {"sim", "--coverage-output="};
  EXPECT_EQ(obelisk_rt_v1_context_configure_argv(
                context, static_cast<int>(std::size(emptyOutput)), emptyOutput),
            OBELISK_RT_INVALID_ARGUMENT);
  const char *missingLoad[] = {"sim", "--coverage-load=missing.obcov"};
  EXPECT_EQ(obelisk_rt_v1_context_configure_argv(
                context, static_cast<int>(std::size(missingLoad)), missingLoad),
            OBELISK_RT_IO_ERROR);
}

TEST_F(CoverageRuntimeDesignTest, CoverageSaveFailureNamesTheRequestedOutput) {
  obelisk::coverage::Database schema;
  schema.schemaFingerprint = obelisk::coverage::computeSchemaFingerprint(schema);
  obelisk_rt_context *context = createContextForSchema(schema);
  ASSERT_NE(context, nullptr);
  ASSERT_EQ(finalizeCoverage(context, 0, 0, nullptr, nullptr), OBELISK_RT_OK);
  std::filesystem::path output =
      std::filesystem::temp_directory_path() /
      ("obelisk-coverage-output-directory-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  ASSERT_TRUE(std::filesystem::create_directory(output));
  std::string outputText = output.string();

  EXPECT_EQ(obelisk_rt_v1_coverage_save(context, outputText.data(),
                                        outputText.size(), 0, 0),
            OBELISK_RT_IO_ERROR);
  obelisk_rt_buffer_v1 message{};
  ASSERT_EQ(obelisk_rt_v1_last_error(context, &message), OBELISK_RT_OK);
  std::string text(reinterpret_cast<const char *>(message.data), message.size);
  obelisk_rt_v1_buffer_release(&message);
  EXPECT_NE(text.find("failed to write coverage database"), std::string::npos);
  EXPECT_NE(text.find(outputText), std::string::npos);
  EXPECT_NE(text.find("rename"), std::string::npos);

  std::error_code ignored;
  std::filesystem::remove(output, ignored);
  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeTest, CoverageSnapshotFailureReplacesPriorDiagnostic) {
  char byte = 'x';
  uint64_t written = 0;
  ASSERT_EQ(obelisk_rt_v1_file_write(context, UINT32_MAX, &byte, 1, &written),
            OBELISK_RT_INVALID_HANDLE);
  const std::string output = "/tmp/obelisk-uninitialized-coverage.obcov";
  EXPECT_EQ(obelisk_rt_v1_coverage_save(context, output.data(), output.size(),
                                        0, 0),
            OBELISK_RT_INVALID_LIFECYCLE);
  obelisk_rt_buffer_v1 message{};
  ASSERT_EQ(obelisk_rt_v1_last_error(context, &message), OBELISK_RT_OK);
  std::string text(reinterpret_cast<const char *>(message.data), message.size);
  obelisk_rt_v1_buffer_release(&message);
  EXPECT_NE(text.find("failed to create coverage snapshot"), std::string::npos);
  EXPECT_NE(text.find(output), std::string::npos);
  EXPECT_NE(text.find("invalid process lifecycle transition"),
            std::string::npos);
  EXPECT_EQ(text.find("invalid output descriptor"), std::string::npos);
}

TEST_F(CoverageRuntimeTest, LoadedSchemaMustMatchFinalizedCounterLayout) {
  ASSERT_EQ(finalizeCoverage(context, 0, 0, nullptr, nullptr),
            OBELISK_RT_OK);
  obelisk::coverage::Database database;
  database.sourceFiles.push_back({1, "loaded.sv", {}});
  database.scopes.push_back({2, 0, "top", 0, "top"});
  database.linePoints.push_back({3, 1, 1, 2, "", 1, 1, 1, 2, 0, 0});
  std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("obelisk-coverage-layout-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()) +
       ".obcov");
  ASSERT_EQ(obelisk::coverage::writeFileAtomically(path.string(), database),
            obelisk::coverage::Status::Ok);
  std::string pathText = path.string();
  EXPECT_EQ(
      obelisk_rt_v1_coverage_load(context, pathText.data(), pathText.size()),
      OBELISK_RT_INVALID_DESIGN);
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

TEST_F(CoverageRuntimeDesignTest,
     FunctionalExclusionsChangeQueriesButRetainCounters) {
  using namespace obelisk::coverage;
  Database schema = makeFunctionalSchema();
  schema.exclusions.push_back(
      {31, MetricKind::Functional, "not part of denominator", 0});
  schema.schemaFingerprint = computeSchemaFingerprint(schema);
  obelisk_rt_context *context = createContextForSchema(schema);
  ASSERT_NE(context, nullptr);
  ASSERT_EQ(finalizeCoverage(context, 0, 0, nullptr, nullptr),
            OBELISK_RT_OK);
  obelisk_rt_covergroup_v1 handle = 0;
  ASSERT_EQ(createFunctional(context, 10, &handle), OBELISK_RT_OK);
  ASSERT_EQ(sampleFunctional(context, handle, 0, 2), OBELISK_RT_OK);
  ASSERT_EQ(sampleFunctional(context, handle, 1, 2), OBELISK_RT_OK);
  double percentage = 0.0;
  int32_t covered = 0, total = 0;
  ASSERT_EQ(obelisk_rt_v1_covergroup_instance_query(
                context, handle, 0, &percentage, &covered, &total),
            OBELISK_RT_OK);
  EXPECT_DOUBLE_EQ(percentage, 100.0);
  EXPECT_EQ(covered, 2);
  EXPECT_EQ(total, 2);

  std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("obelisk-functional-exclusion-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()) +
       ".obcov");
  std::string pathText = path.string();
  ASSERT_EQ(obelisk_rt_v1_coverage_save(context, pathText.data(),
                                        pathText.size(), 0, 0),
            OBELISK_RT_OK);
  Database saved;
  ASSERT_EQ(readFile(pathText, saved), Status::Ok);
  auto resolvedExcluded = std::find_if(
      saved.resolvedFunctionalBins.begin(), saved.resolvedFunctionalBins.end(),
      [](const auto &bin) { return bin.templateBin == 31; });
  ASSERT_NE(resolvedExcluded, saved.resolvedFunctionalBins.end());
  auto excluded =
      std::find_if(saved.counters.begin(), saved.counters.end(),
                   [&](const Counter &counter) {
                     return counter.metric == MetricKind::Functional &&
                            counter.entity == resolvedExcluded->id &&
                            counter.instance == handle;
                   });
  ASSERT_NE(excluded, saved.counters.end());
  EXPECT_EQ(excluded->value, 1u);
  obelisk_rt_v1_context_destroy(context);
  std::error_code ignored;
  std::filesystem::remove(path, ignored);

  schema.exclusions = {{30, MetricKind::Functional, "all", 0},
                       {31, MetricKind::Functional, "all", 0},
                       {40, MetricKind::Functional, "all", 0}};
  schema.schemaFingerprint = computeSchemaFingerprint(schema);
  context = createContextForSchema(schema);
  ASSERT_NE(context, nullptr);
  ASSERT_EQ(finalizeCoverage(context, 0, 0, nullptr, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(createFunctional(context, 10, &handle), OBELISK_RT_OK);
  ASSERT_EQ(sampleFunctional(context, handle, 1, 2), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_covergroup_instance_query(
                context, handle, 0, &percentage, &covered, &total),
            OBELISK_RT_OK);
  EXPECT_DOUBLE_EQ(percentage, 0.0);
  EXPECT_EQ(covered, 0);
  EXPECT_EQ(total, 0);
  ASSERT_EQ(obelisk_rt_v1_functional_coverage_get(context, &percentage),
            OBELISK_RT_OK);
  EXPECT_DOUBLE_EQ(percentage, 100.0);
  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeDesignTest, SharedAndSelectorDagBindsAndSamplesOnce) {
  obelisk::coverage::Database schema = makeSharedAndSelectorSchema();
  obelisk_rt_context *context = createContextForSchema(schema, false);
  ASSERT_NE(context, nullptr);
  ASSERT_EQ(finalizeCoverage(context, 0, 0, nullptr, nullptr),
            OBELISK_RT_OK);
  obelisk_rt_covergroup_v1 handle = 0;
  ASSERT_EQ(createFunctional(context, 10, &handle), OBELISK_RT_OK);
  ASSERT_EQ(sampleFunctional(context, handle, 0, 2), OBELISK_RT_OK);

  double percentage = 0.0;
  int32_t covered = 0, total = 0;
  ASSERT_EQ(obelisk_rt_v1_covergroup_instance_query(
                context, handle, 22, &percentage, &covered, &total),
            OBELISK_RT_OK);
  EXPECT_DOUBLE_EQ(percentage, 100.0);
  EXPECT_EQ(covered, 1);
  EXPECT_EQ(total, 1);
  obelisk_rt_v1_context_destroy(context);
}

TEST(CoverageDatabaseTest, CrossWithConstructorBatchIsStrictlyValidated) {
  using namespace obelisk::coverage;
  Database schema = makeCrossWithSchema(CrossMatchesPolicy::Count, /*count=*/1);
  EXPECT_EQ(validate(schema), Status::Ok);

  Database wrongFirst = schema;
  auto selector = std::find_if(wrongFirst.crossSelectorNodes.begin(),
                               wrongFirst.crossSelectorNodes.end(),
                               [](const auto &node) { return node.id == 70; });
  ASSERT_NE(selector, wrongFirst.crossSelectorNodes.end());
  selector->withExpression = 101;
  EXPECT_EQ(validate(wrongFirst), Status::InvalidReference);

  Database ordinalGap = schema;
  auto predicate =
      std::find_if(ordinalGap.functionalExpressions.begin(),
                   ordinalGap.functionalExpressions.end(),
                   [](const auto &expression) { return expression.id == 105; });
  ASSERT_NE(predicate, ordinalGap.functionalExpressions.end());
  predicate->ownerOrdinal = 17;
  EXPECT_EQ(validate(ordinalGap), Status::InvalidReference);

  Database digestMismatch = schema;
  predicate =
      std::find_if(digestMismatch.functionalExpressions.begin(),
                   digestMismatch.functionalExpressions.end(),
                   [](const auto &expression) { return expression.id == 105; });
  ASSERT_NE(predicate, digestMismatch.functionalExpressions.end());
  predicate->semanticDigest.back() = 1;
  EXPECT_EQ(validate(digestMismatch), Status::InvalidReference);
}

TEST(CoverageDatabaseTest,
     SchemaOnlyClassificationRejectsEveryResolvedTupleLayer) {
  using namespace obelisk::coverage;
  Database schema = makeFunctionalSchema();
  EXPECT_TRUE(isSchemaOnly(schema));
  schema.functionalConfigurations.push_back({});
  EXPECT_FALSE(isSchemaOnly(schema));
  schema.functionalConfigurations.clear();
  schema.resolvedFunctionalTupleSets.push_back({});
  EXPECT_FALSE(isSchemaOnly(schema));
  schema.resolvedFunctionalTupleSets.clear();
  schema.resolvedFunctionalTupleSetTuples.push_back({});
  EXPECT_FALSE(isSchemaOnly(schema));
  schema.resolvedFunctionalTupleSetTuples.clear();
  schema.resolvedFunctionalTupleSetComponents.push_back({});
  EXPECT_FALSE(isSchemaOnly(schema));
}

TEST_F(CoverageRuntimeDesignTest,
     CrossWithPersistsSatisfyingTuplesAndAppliesMatchesPolicies) {
  using namespace obelisk::coverage;
  auto exercise = [&](CrossMatchesPolicy policy, uint64_t count,
                      bool everyPredicate, bool expectedSelected,
                      const std::string &suffix) {
    Database schema = makeCrossWithSchema(policy, count);
    obelisk_rt_context *local = createContextForSchema(schema, false);
    ASSERT_NE(local, nullptr);
    ASSERT_EQ(finalizeCoverage(local, 0, 0, nullptr, nullptr),
              OBELISK_RT_OK);
    std::array<uint8_t, 16> predicateValues{};
    std::array<obelisk_rt_functional_value_v1, 16> predicates{};
    for (size_t ordinal = 0; ordinal != predicates.size(); ++ordinal) {
      uint32_t first = static_cast<uint32_t>(ordinal / 4);
      uint32_t second = static_cast<uint32_t>(ordinal % 4);
      predicateValues[ordinal] =
          everyPredicate || first == second ? uint8_t{1} : uint8_t{0};
      predicates[ordinal] = {100 + ordinal,
                             1,
                             1,
                             &predicateValues[ordinal],
                             nullptr,
                             nullptr,
                             0,
                             OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL,
                             0};
    }
    obelisk_rt_covergroup_v1 handle = 0;
    ASSERT_EQ(obelisk_rt_v1_covergroup_create(local, 10, nullptr, 0,
                                              predicates.data(),
                                              predicates.size(), &handle),
              OBELISK_RT_OK);
    ASSERT_EQ(sampleFunctional(local, handle, 2, 2), OBELISK_RT_OK);

    double percentage = 0.0;
    int32_t covered = 0;
    int32_t total = 0;
    ASSERT_EQ(obelisk_rt_v1_covergroup_instance_query(
                  local, handle, 22, &percentage, &covered, &total),
              OBELISK_RT_OK);
    EXPECT_EQ(total, expectedSelected ? 1 : 0);
    EXPECT_EQ(covered, expectedSelected ? 1 : 0);

    Database saved = saveCoverage(local, suffix);
    ASSERT_EQ(validate(saved), Status::Ok);
    ASSERT_EQ(saved.resolvedFunctionalTupleSets.size(), 1u);
    const uint32_t satisfyingCount = everyPredicate ? 16 : 4;
    EXPECT_EQ(saved.resolvedFunctionalTupleSets.front().tupleCount,
              satisfyingCount);
    EXPECT_EQ(saved.resolvedFunctionalTupleSetTuples.size(), satisfyingCount);
    EXPECT_EQ(saved.resolvedFunctionalTupleSetComponents.size(),
              satisfyingCount * 2);
    EXPECT_EQ(
        std::count_if(saved.resolvedFunctionalValueSets.begin(),
                      saved.resolvedFunctionalValueSets.end(),
                      [](const auto &set) {
                        return set.role ==
                               ResolvedFunctionalValueSetRole::TupleComponent;
                      }),
        satisfyingCount * 2);
    obelisk_rt_v1_context_destroy(local);
  };

  exercise(CrossMatchesPolicy::Count, /*count=*/4,
           /*everyPredicate=*/false, /*expectedSelected=*/true, "with-count");
  exercise(CrossMatchesPolicy::Count, /*count=*/5,
           /*everyPredicate=*/false, /*expectedSelected=*/false,
           "with-count-miss");
  exercise(CrossMatchesPolicy::All, /*count=*/0,
           /*everyPredicate=*/true, /*expectedSelected=*/true, "with-all");
}

TEST_F(CoverageRuntimeDesignTest,
     CrossSetConsumesManagedQueueAndPersistsCanonicalTuple) {
  using namespace obelisk::coverage;
  Database schema = makeCrossWithSchema(CrossMatchesPolicy::Count, 1);
  schema.functionalExpressions.erase(
      std::remove_if(
          schema.functionalExpressions.begin(),
          schema.functionalExpressions.end(),
          [](const auto &expression) { return expression.id >= 100; }),
      schema.functionalExpressions.end());
  auto &selector = schema.crossSelectorNodes.front();
  selector.kind = CrossSelectorKind::Set;
  selector.withExpression = 0;
  selector.constructionExpression = 100;
  FunctionalExpression construction;
  construction.id = 100;
  construction.owner = selector.id;
  construction.ownerKind = FunctionalExpressionOwnerKind::Selector;
  construction.role = FunctionalExpressionRole::BinSet;
  construction.resultKind = FunctionalExpressionResultKind::TupleQueue;
  construction.evaluationPhase =
      FunctionalExpressionEvaluationPhase::Constructor;
  construction.resultOrdinal = 0;
  schema.functionalExpressions.push_back(construction);
  std::sort(
      schema.functionalExpressions.begin(), schema.functionalExpressions.end(),
      [](const auto &left, const auto &right) { return left.id < right.id; });

  obelisk_rt_context *local = createContextForSchema(schema, false);
  ASSERT_NE(local, nullptr);
  ASSERT_EQ(finalizeCoverage(local, 0, 0, nullptr, nullptr),
            OBELISK_RT_OK);
  obelisk_rt_gc_lane_v1 *lane = nullptr;
  ASSERT_EQ(obelisk_rt_v1_gc_lane_create(local, &lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_enter(lane), OBELISK_RT_OK);
  obelisk_rt_element_type_v1 element{OBELISK_RT_VERSION,
                                     OBELISK_RT_ELEMENT_AGGREGATE,
                                     220,
                                     0,
                                     0,
                                     1,
                                     1,
                                     8,
                                     nullptr};
  obelisk_rt_object_v1 *firstQueue = nullptr;
  obelisk_rt_object_v1 *secondQueue = nullptr;
  ASSERT_EQ(obelisk_rt_v1_queue_create(lane, &element, UINT64_MAX, &firstQueue),
            OBELISK_RT_OK);
  ASSERT_EQ(
      obelisk_rt_v1_queue_create(lane, &element, UINT64_MAX, &secondQueue),
      OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 firstRoot{};
  obelisk_rt_gc_root_v1 secondRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &firstRoot, &firstQueue),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &secondRoot, &secondQueue),
            OBELISK_RT_OK);
  const uint8_t firstTuple = uint8_t{1} | (uint8_t{1} << 2);
  const uint8_t secondTuple = uint8_t{2} | (uint8_t{2} << 2);
  for (uint8_t tuple : {secondTuple, firstTuple, secondTuple})
    ASSERT_EQ(obelisk_rt_v1_queue_push(lane, firstQueue, 0, &tuple, nullptr),
              OBELISK_RT_OK);
  for (uint8_t tuple : {firstTuple, secondTuple})
    ASSERT_EQ(obelisk_rt_v1_queue_push(lane, secondQueue, 0, &tuple, nullptr),
              OBELISK_RT_OK);
  const obelisk_rt_functional_value_v1 firstExpression{
      100,     0,          0, nullptr,
      nullptr, firstQueue, 0, OBELISK_RT_FUNCTIONAL_VALUE_MANAGED_CONTAINER,
      0};
  const obelisk_rt_functional_value_v1 secondExpression{
      100,     0,           0, nullptr,
      nullptr, secondQueue, 0, OBELISK_RT_FUNCTIONAL_VALUE_MANAGED_CONTAINER,
      0};
  obelisk_rt_covergroup_v1 firstHandle = 0;
  obelisk_rt_covergroup_v1 secondHandle = 0;
  ASSERT_EQ(obelisk_rt_v1_covergroup_create(local, 10, nullptr, 0,
                                            &firstExpression, 1, &firstHandle),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_covergroup_create(
                local, 10, nullptr, 0, &secondExpression, 1, &secondHandle),
            OBELISK_RT_OK);
  ASSERT_EQ(sampleFunctional(local, firstHandle, 2, 2), OBELISK_RT_OK);
  ASSERT_EQ(sampleFunctional(local, secondHandle, 1, 1), OBELISK_RT_OK);
  double percentage = 0.0;
  int32_t covered = 0;
  int32_t total = 0;
  ASSERT_EQ(obelisk_rt_v1_covergroup_instance_query(
                local, firstHandle, 22, &percentage, &covered, &total),
            OBELISK_RT_OK);
  EXPECT_EQ(covered, 1);
  EXPECT_EQ(total, 1);
  Database saved = saveCoverage(local, "cross-set-queue");
  ASSERT_EQ(validate(saved), Status::Ok);
  ASSERT_EQ(saved.resolvedFunctionalTupleSets.size(), 1u);
  EXPECT_EQ(saved.resolvedFunctionalTupleSets.front().tupleCount, 2u);
  ASSERT_EQ(saved.resolvedInstances.size(), 2u);
  EXPECT_EQ(saved.resolvedInstances[0].configuration,
            saved.resolvedInstances[1].configuration);
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &secondRoot), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &firstRoot), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_lane_leave(lane), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_lane_destroy(lane), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(local);
}

TEST_F(CoverageRuntimeDesignTest, CrossWithMatchesCountIsAppliedPerBinTuple) {
  using namespace obelisk::coverage;
  // The predicate is true for four value tuples globally, but only twice in
  // each candidate bin tuple because the first target is split into [0:1]
  // and [2:3]. IEEE 1800-2023 19.6.1.2 therefore rejects both tuples for
  // `matches 3`; treating the count as cross-global would incorrectly retain
  // the explicit bin.
  Database schema = makeCrossWithSchema(CrossMatchesPolicy::Count,
                                        /*count=*/3,
                                        /*splitFirstTarget=*/true);
  ASSERT_EQ(validate(schema), Status::Ok);
  obelisk_rt_context *local = createContextForSchema(schema, false);
  ASSERT_NE(local, nullptr);
  ASSERT_EQ(finalizeCoverage(local, 0, 0, nullptr, nullptr),
            OBELISK_RT_OK);

  std::array<uint8_t, 16> predicateValues{};
  std::array<obelisk_rt_functional_value_v1, 16> predicates{};
  for (uint32_t ordinal = 0; ordinal != predicates.size(); ++ordinal) {
    predicateValues[ordinal] = ordinal / 4 == ordinal % 4;
    predicates[ordinal] = {100 + ordinal,
                           1,
                           1,
                           &predicateValues[ordinal],
                           nullptr,
                           nullptr,
                           0,
                           OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL,
                           0};
  }
  obelisk_rt_covergroup_v1 handle = 0;
  ASSERT_EQ(obelisk_rt_v1_covergroup_create(local, 10, nullptr, 0,
                                            predicates.data(),
                                            predicates.size(), &handle),
            OBELISK_RT_OK);
  double percentage = 0.0;
  int32_t covered = 0;
  int32_t total = 0;
  ASSERT_EQ(obelisk_rt_v1_covergroup_instance_query(
                local, handle, 22, &percentage, &covered, &total),
            OBELISK_RT_OK);
  EXPECT_DOUBLE_EQ(percentage, 0.0);
  EXPECT_EQ(covered, 0);
  EXPECT_EQ(total, 0);
  obelisk_rt_v1_context_destroy(local);
}

TEST_F(CoverageRuntimeDesignTest, CrossWithWideDynamicMatchesClampsExactly) {
  using namespace obelisk::coverage;
  Database schema = makeCrossWithSchema(CrossMatchesPolicy::Count, /*count=*/0);
  schema.crossSelectorNodes.front().matchesExpression = 200;
  FunctionalExpression matches;
  matches.id = 200;
  matches.owner = 70;
  matches.ownerKind = FunctionalExpressionOwnerKind::Selector;
  matches.role = FunctionalExpressionRole::SelectorMatches;
  matches.resultKind = FunctionalExpressionResultKind::Integral;
  matches.bitWidth = 128;
  matches.signedness = CoverageSignedness::Unsigned;
  matches.evaluationPhase = FunctionalExpressionEvaluationPhase::Constructor;
  matches.resultOrdinal = 16;
  schema.functionalExpressions.push_back(matches);
  schema.schemaFingerprint = computeSchemaFingerprint(schema);
  ASSERT_EQ(validate(schema), Status::Ok);

  obelisk_rt_context *local = createContextForSchema(schema, false);
  ASSERT_NE(local, nullptr);
  ASSERT_EQ(finalizeCoverage(local, 0, 0, nullptr, nullptr),
            OBELISK_RT_OK);
  std::array<std::array<uint8_t, 16>, 17> storage{};
  std::array<obelisk_rt_functional_value_v1, 17> expressions{};
  for (uint32_t ordinal = 0; ordinal != 16; ++ordinal) {
    storage[ordinal][0] = ordinal / 4 == ordinal % 4;
    expressions[ordinal] = {100 + ordinal,
                            1,
                            1,
                            storage[ordinal].data(),
                            nullptr,
                            nullptr,
                            0,
                            OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL,
                            0};
  }
  // 2^64 is positive and legal, but exceeds every bounded candidate bin
  // tuple. The v1 resolved representation canonicalizes it to 4097.
  storage.back()[8] = 1;
  expressions.back() = {
      200,     128,     16, storage.back().data(),
      nullptr, nullptr, 0,  OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL,
      0};
  obelisk_rt_covergroup_v1 handle = 0;
  ASSERT_EQ(obelisk_rt_v1_covergroup_create(local, 10, nullptr, 0,
                                            expressions.data(),
                                            expressions.size(), &handle),
            OBELISK_RT_OK);
  Database saved = saveCoverage(local, "wide-dynamic-matches");
  ASSERT_EQ(validate(saved), Status::Ok);
  ASSERT_EQ(saved.resolvedCrossSelectorBindings.size(), 1u);
  EXPECT_EQ(saved.resolvedCrossSelectorBindings.front().matchesCount, 4097u);
  obelisk_rt_v1_context_destroy(local);
}

TEST_F(CoverageRuntimeDesignTest, ConstructorInputsSnapshotAndRefsRemainLive) {
  using namespace obelisk::coverage;
  Database schema = makeFunctionalSchema();
  schema.functionalFormals.push_back(
      {70, 10, "snapshot", FunctionalFormalKind::Constructor,
       FunctionalFormalDirection::Input,
       FunctionalExpressionResultKind::Integral, 0, 8,
       CoverageSignedness::Unsigned, 0, 0});
  schema.functionalFormals.push_back(
      {71, 10, "live", FunctionalFormalKind::Constructor,
       FunctionalFormalDirection::Ref, FunctionalExpressionResultKind::Integral,
       0, 8, CoverageSignedness::Unsigned, 1, 0});
  schema.schemaFingerprint = computeSchemaFingerprint(schema);
  obelisk_rt_context *context = createContextForSchema(schema);
  ASSERT_NE(context, nullptr);

  uint8_t snapshot = 3;
  uint8_t live = 5;
  uint64_t liveHandle = UINT64_MAX;
  ASSERT_EQ(
      obelisk_rt_v1_native_state_alloc(context, 8, &live, nullptr, &liveHandle),
      OBELISK_RT_OK);
  std::array<obelisk_rt_functional_value_v1, 2> formals{};
  formals[0] = {70,      8,       1, &snapshot,
                nullptr, nullptr, 0, OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL,
                0};
  formals[1] = {
      71,      8,       1,          nullptr,
      nullptr, nullptr, liveHandle, OBELISK_RT_FUNCTIONAL_VALUE_ARGUMENT_REF,
      0};
  obelisk_rt_covergroup_v1 handle = 0;
  ASSERT_EQ(obelisk_rt_v1_covergroup_create(context, 10, formals.data(),
                                            formals.size(), nullptr, 0,
                                            &handle),
            OBELISK_RT_OK);

  snapshot = 9;
  uint8_t dummy = 0;
  live = 7;
  ASSERT_EQ(obelisk_rt_v1_argument_ref_store(
                context, &dummy, &dummy, 8, nullptr, liveHandle, 0, 8, 1, 0,
                OBELISK_RT_ARGUMENT_VALUE_BITS, &live, nullptr),
            OBELISK_RT_OK);
  // The covergroup owns a retain on automatic ref storage after the caller
  // drops its original reference.
  ASSERT_EQ(obelisk_rt_v1_native_state_release(context, liveHandle, 0),
            OBELISK_RT_OK);
  uint8_t actual = 0;
  ASSERT_EQ(obelisk_rt_v1_covergroup_formal_read(
                context, handle, 70, OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL, 8, 1,
                nullptr, nullptr, 0, &actual, nullptr),
            OBELISK_RT_OK);
  EXPECT_EQ(actual, 3);
  ASSERT_EQ(obelisk_rt_v1_covergroup_formal_read(
                context, handle, 71, OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL, 8, 1,
                nullptr, nullptr, 0, &actual, nullptr),
            OBELISK_RT_OK);
  EXPECT_EQ(actual, 7);
  std::array<uint64_t, 3> reference{};
  ASSERT_EQ(obelisk_rt_v1_covergroup_formal_read(
                context, handle, 71, OBELISK_RT_FUNCTIONAL_VALUE_ARGUMENT_REF,
                192, 24, nullptr, nullptr, 0, reference.data(), nullptr),
            OBELISK_RT_OK);
  EXPECT_EQ(reference, (std::array<uint64_t, 3>{0, liveHandle, 0}));
  live = 11;
  ASSERT_EQ(obelisk_rt_v1_argument_ref_store(
                context, &dummy, &dummy, 8, nullptr, liveHandle, 0, 8, 1, 0,
                OBELISK_RT_ARGUMENT_VALUE_BITS, &live, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_argument_ref_load(
                context, &dummy, &dummy, 8, nullptr, reference[1], reference[2],
                8, 1, 0, OBELISK_RT_ARGUMENT_VALUE_BITS, &actual, nullptr),
            OBELISK_RT_OK);
  EXPECT_EQ(actual, 11);
  for (auto [id, width, size] :
       {std::tuple{uint64_t{70}, uint64_t{192}, uint64_t{24}},
        std::tuple{uint64_t{71}, uint64_t{8}, uint64_t{24}},
        std::tuple{uint64_t{71}, uint64_t{192}, uint64_t{8}}}) {
    reference.fill(UINT64_MAX);
    EXPECT_EQ(obelisk_rt_v1_covergroup_formal_read(
                  context, handle, id, OBELISK_RT_FUNCTIONAL_VALUE_ARGUMENT_REF,
                  width, size, nullptr, nullptr, 0, reference.data(), nullptr),
              OBELISK_RT_INVALID_DESIGN);
    EXPECT_EQ(reference,
              (std::array<uint64_t, 3>{UINT64_MAX, UINT64_MAX, UINT64_MAX}));
  }
  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeDesignTest,
       SampleOrdinalsPreserveInterleavedDefaultsAndIndependentItemGuards) {
  using namespace obelisk::coverage;
  constexpr uint32_t itemCount = 64;
  Database schema;
  schema.scopes.push_back({1, 0, "top", 0, "top"});
  schema.functionalTypes.push_back({10, 1, "cg", 0, 2023, "top.cg"});
  std::array<uint8_t, itemCount> values{}, pointGuards{}, binGuards{};
  std::vector<obelisk_rt_functional_value_v1> batch;
  for (uint32_t ordinal = 0; ordinal != itemCount; ++ordinal) {
    const uint64_t item = 1000 + ordinal, bin = 2000 + 2 * ordinal;
    const uint64_t set = 3000 + ordinal, formal = 4000 + ordinal;
    // Deliberately order expression IDs opposite to their ABI ordinals.
    const uint64_t expression = 20000 + 4 * (itemCount - 1 - ordinal);
    const std::string name = "point" + std::to_string(ordinal);
    const std::string hierarchy = "top.cg." + name;
    schema.functionalItems.push_back({item, 10, name,
                                      FunctionalItemKind::Coverpoint, 0, 100, 1,
                                      ordinal, hierarchy});
    schema.functionalBins.push_back({bin, item, "hit", FunctionalBinKind::State,
                                     0, 1, 0, hierarchy + ".hit"});
    schema.functionalBins.push_back(
        {bin + 1, item, "fallback", FunctionalBinKind::State,
         FunctionalBinDefault, 1, 1, hierarchy + ".fallback"});
    schema.functionalValueSets.push_back({set, item, ordinal, 1, 2,
                                          FunctionalValueSetKind::Integral, 0,
                                          CoverageSignedness::Unsigned, 0});
    schema.functionalValueAtoms.push_back(
        {set, 0, 0, ordinal, 1, 0, FunctionalValueAtomKind::IntegralValue,
         FunctionalValueAtomLowerInclusive | FunctionalValueAtomUpperInclusive,
         0, 0});
    uint64_t value = ordinal % 4;
    schema.functionalValueLimbs.push_back({set, 0, 0, value, 0, value, 0, 0});
    schema.functionalBinPlans.push_back({bin, set, expression + 2});
    schema.functionalBinPlans.push_back({bin + 1, 0});
    schema.functionalFormals.push_back(
        {formal, 10, name, FunctionalFormalKind::Sample,
         FunctionalFormalDirection::Input,
         FunctionalExpressionResultKind::Integral, FunctionalFormalHasDefault,
         2, CoverageSignedness::Unsigned, ordinal, expression + 3});
    FunctionalExpression sample;
    sample.id = expression;
    sample.owner = item;
    sample.ownerKind = FunctionalExpressionOwnerKind::Item;
    sample.role = FunctionalExpressionRole::CoverpointSample;
    sample.resultKind = FunctionalExpressionResultKind::Integral;
    sample.bitWidth = 2;
    sample.signedness = CoverageSignedness::Unsigned;
    sample.evaluationPhase = FunctionalExpressionEvaluationPhase::Sample;
    sample.resultOrdinal = 4 * ordinal;
    schema.functionalExpressions.push_back(sample);
    auto pointIff = sample;
    pointIff.id = expression + 1;
    pointIff.role = FunctionalExpressionRole::CoverpointIff;
    pointIff.resultKind = FunctionalExpressionResultKind::Boolean;
    pointIff.bitWidth = 0;
    pointIff.signedness = CoverageSignedness::NotApplicable;
    ++pointIff.resultOrdinal;
    schema.functionalExpressions.push_back(pointIff);
    auto binIff = pointIff;
    binIff.id = expression + 2;
    binIff.owner = bin;
    binIff.ownerKind = FunctionalExpressionOwnerKind::Bin;
    binIff.role = FunctionalExpressionRole::BinIff;
    ++binIff.resultOrdinal;
    schema.functionalExpressions.push_back(binIff);
    auto defaultValue = sample;
    defaultValue.id = expression + 3;
    defaultValue.owner = formal;
    defaultValue.ownerKind = FunctionalExpressionOwnerKind::Formal;
    defaultValue.role = FunctionalExpressionRole::FormalDefault;
    defaultValue.resultOrdinal += 3;
    schema.functionalExpressions.push_back(defaultValue);
    values[ordinal] = value;
    pointGuards[ordinal] = ordinal % 3 != 0;
    binGuards[ordinal] = ordinal % 4 != 0;
    batch.push_back({expression, 2, 1, &values[ordinal], nullptr, nullptr, 0,
                     OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL, 0});
    batch.push_back({expression + 1, 1, 1, &pointGuards[ordinal], nullptr,
                     nullptr, 0, OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL, 0});
    batch.push_back({expression + 2, 1, 1, &binGuards[ordinal], nullptr,
                     nullptr, 0, OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL, 0});
    if (ordinal % 2)
      batch.push_back({expression + 3, 2, 1, &values[ordinal], nullptr, nullptr,
                       0, OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL, 0});
  }
  detail::sortByKey(schema.functionalExpressions.begin(),
                    schema.functionalExpressions.end(),
                    [](const auto &expression) { return expression.id; });
  schema.schemaFingerprint = computeSchemaFingerprint(schema);
  obelisk_rt_context *context = createContextForSchema(schema);
  ASSERT_NE(context, nullptr);
  obelisk_rt_covergroup_v1 first = 0, second = 0;
  ASSERT_EQ(createFunctional(context, 10, &first), OBELISK_RT_OK);
  ASSERT_EQ(createFunctional(context, 10, &second), OBELISK_RT_OK);
  auto sample = [&](uint64_t handle) {
    return obelisk_rt_v1_covergroup_sample(context, handle, batch.data(),
                                           batch.size());
  };
  ASSERT_EQ(sample(first), OBELISK_RT_OK);
  ASSERT_EQ(sample(second), OBELISK_RT_OK);
  for (auto &value : values)
    value = (value + 1) % 4;
  ASSERT_EQ(sample(first), OBELISK_RT_OK);
  // A malformed late value must not partially publish the earlier items.
  uint32_t lastKind = batch.back().kind;
  batch.back().kind = UINT32_MAX;
  EXPECT_EQ(sample(first), OBELISK_RT_INVALID_DESIGN);
  batch.back().kind = lastKind;
  for (uint64_t handle : {first, second}) {
    const auto &instance = context->coverage->instances.at(handle);
    ASSERT_EQ(instance.bins.size(), 2 * itemCount);
    for (const auto &bin : instance.bins) {
      uint32_t ordinal = (bin.templateBin - 2000) / 2;
      ASSERT_LT(ordinal, itemCount);
      bool fallback = (bin.templateBin - 2000) % 2;
      uint64_t expected = fallback
                              ? (handle == first && pointGuards[ordinal])
                              : (pointGuards[ordinal] && binGuards[ordinal]);
      EXPECT_EQ(bin.count, expected)
          << "item " << ordinal << " handle " << handle;
    }
  }
  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeDesignTest, RealFormalsPreservePrecisionBitsAndLiveRefs) {
  using namespace obelisk::coverage;
  Database schema = makeFunctionalSchema();
  for (auto [id, name, direction, kind, ordinal] :
       {std::tuple{uint64_t{70}, "snapshot", FunctionalFormalDirection::Input,
                   FunctionalExpressionResultKind::ShortReal, uint32_t{0}},
        std::tuple{uint64_t{71}, "live", FunctionalFormalDirection::Ref,
                   FunctionalExpressionResultKind::ShortReal, uint32_t{1}},
        std::tuple{uint64_t{72}, "wide", FunctionalFormalDirection::Ref,
                   FunctionalExpressionResultKind::Real, uint32_t{2}}})
    schema.functionalFormals.push_back(
        {id, 10, name, FunctionalFormalKind::Constructor, direction, kind, 0, 0,
         CoverageSignedness::NotApplicable, ordinal, 0});
  schema.functionalFormals.push_back(
      {73, 10, "integer", FunctionalFormalKind::Constructor,
       FunctionalFormalDirection::Input,
       FunctionalExpressionResultKind::Integral, 0, 32,
       CoverageSignedness::Unsigned, 3, 0});
  schema.schemaFingerprint = computeSchemaFingerprint(schema);
  obelisk_rt_context *context = createContextForSchema(schema);
  ASSERT_NE(context, nullptr);
  uint32_t snapshot = UINT32_C(0x80000000);
  uint32_t live = UINT32_C(0x3dcccccd);
  uint64_t wide = UINT64_C(0x3ff8000000000000);
  uint32_t integer = UINT32_C(0x3f800000);
  uint64_t liveHandle = UINT64_MAX, wideHandle = UINT64_MAX;
  ASSERT_EQ(obelisk_rt_v1_native_state_alloc(
                context, 32, reinterpret_cast<const uint8_t *>(&live), nullptr,
                &liveHandle),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_alloc(
                context, 64, reinterpret_cast<const uint8_t *>(&wide), nullptr,
                &wideHandle),
            OBELISK_RT_OK);
  std::array<obelisk_rt_functional_value_v1, 4> formals{
      obelisk_rt_functional_value_v1{70, 32, 4, &snapshot, nullptr, nullptr, 0,
                                     OBELISK_RT_FUNCTIONAL_VALUE_SHORTREAL, 0},
      {71, 32, 4, nullptr, nullptr, nullptr, liveHandle,
       OBELISK_RT_FUNCTIONAL_VALUE_ARGUMENT_REF, 0},
      {72, 64, 8, nullptr, nullptr, nullptr, wideHandle,
       OBELISK_RT_FUNCTIONAL_VALUE_ARGUMENT_REF, 0},
      {73, 32, 4, &integer, nullptr, nullptr, 0,
       OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL, 0}};
  obelisk_rt_covergroup_v1 handle = UINT64_MAX;
  formals[0].kind = OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL;
  EXPECT_EQ(obelisk_rt_v1_covergroup_create(context, 10, formals.data(),
                                            formals.size(), nullptr, 0,
                                            &handle),
            OBELISK_RT_INVALID_DESIGN);
  EXPECT_EQ(handle, UINT64_MAX);
  formals[0].kind = OBELISK_RT_FUNCTIONAL_VALUE_SHORTREAL;
  ASSERT_EQ(obelisk_rt_v1_covergroup_create(context, 10, formals.data(),
                                            formals.size(), nullptr, 0,
                                            &handle),
            OBELISK_RT_OK);
  uint32_t wrongReal = UINT32_MAX;
  EXPECT_EQ(obelisk_rt_v1_covergroup_formal_read(
                context, handle, 73, OBELISK_RT_FUNCTIONAL_VALUE_SHORTREAL, 32,
                4, nullptr, nullptr, 0, &wrongReal, nullptr),
            OBELISK_RT_INVALID_DESIGN);
  EXPECT_EQ(wrongReal, UINT32_MAX);
  EXPECT_EQ(obelisk_rt_v1_covergroup_formal_read(
                context, handle, 73, OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL, 32,
                4, nullptr, nullptr, 0, &wrongReal, nullptr),
            OBELISK_RT_OK);
  EXPECT_EQ(wrongReal, integer);
  snapshot = 0;
  live = UINT32_C(0x7fc12345);
  uint8_t dummy = 0;
  ASSERT_EQ(obelisk_rt_v1_argument_ref_store(
                context, &dummy, &dummy, 8, nullptr, liveHandle, 0, 32, 4, 0,
                OBELISK_RT_ARGUMENT_VALUE_BITS, &live, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_release(context, liveHandle, 0),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_release(context, wideHandle, 0),
            OBELISK_RT_OK);
  for (auto [id, expected] : {std::pair{uint64_t{70}, UINT32_C(0x80000000)},
                              std::pair{uint64_t{71}, UINT32_C(0x7fc12345)}}) {
    uint32_t actual = 0;
    ASSERT_EQ(obelisk_rt_v1_covergroup_formal_read(
                  context, handle, id, OBELISK_RT_FUNCTIONAL_VALUE_SHORTREAL,
                  32, 4, nullptr, nullptr, 0, &actual, nullptr),
              OBELISK_RT_OK);
    EXPECT_EQ(actual, expected);
    actual = UINT32_MAX;
    EXPECT_EQ(obelisk_rt_v1_covergroup_formal_read(
                  context, handle, id, OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL, 32,
                  4, nullptr, nullptr, 0, &actual, nullptr),
              OBELISK_RT_INVALID_DESIGN);
    EXPECT_EQ(actual, UINT32_MAX);
  }
  uint64_t actual = 0;
  ASSERT_EQ(obelisk_rt_v1_covergroup_formal_read(
                context, handle, 72, OBELISK_RT_FUNCTIONAL_VALUE_REAL, 64, 8,
                nullptr, nullptr, 0, &actual, nullptr),
            OBELISK_RT_OK);
  EXPECT_EQ(actual, wide);
  actual = UINT64_MAX;
  EXPECT_EQ(obelisk_rt_v1_covergroup_formal_read(
                context, handle, 72, OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL, 64,
                8, nullptr, nullptr, 0, &actual, nullptr),
            OBELISK_RT_INVALID_DESIGN);
  EXPECT_EQ(actual, UINT64_MAX);
  EXPECT_EQ(obelisk_rt_v1_covergroup_formal_read(
                context, handle, 71, OBELISK_RT_FUNCTIONAL_VALUE_REAL, 64, 8,
                nullptr, nullptr, 0, &actual, nullptr),
            OBELISK_RT_INVALID_DESIGN);
  EXPECT_EQ(actual, UINT64_MAX);
  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeDesignTest, StringConstructorValuesAndRefPathsSurviveGc) {
  using namespace obelisk::coverage;
  Database schema = makeFunctionalSchema();
  schema.functionalFormals.push_back(
      {90, 10, "snapshot", FunctionalFormalKind::Constructor,
       FunctionalFormalDirection::Input, FunctionalExpressionResultKind::String,
       0, 0, CoverageSignedness::NotApplicable, 0, 0});
  schema.functionalFormals.push_back(
      {71, 10, "live", FunctionalFormalKind::Constructor,
       FunctionalFormalDirection::Ref, FunctionalExpressionResultKind::String,
       0, 0, CoverageSignedness::NotApplicable, 1, 0});
  schema.schemaFingerprint = computeSchemaFingerprint(schema);
  obelisk_rt_context *context = createContextForSchema(schema);
  ASSERT_NE(context, nullptr);
  obelisk_rt_gc_lane_v1 *lane = nullptr;
  bool entered = false;
  ASSERT_EQ(obelisk_rt_managed_execution_enter(context, &lane, &entered),
            OBELISK_RT_OK);
  std::array<obelisk_rt_string_v1, 2> strings{};
  obelisk_rt_gc_managed_root_range_v1 stringRoots{};
  ASSERT_EQ(obelisk_rt_v1_gc_managed_root_range_push(
                lane, &stringRoots, strings.data(), strings.size()),
            OBELISK_RT_OK);
  const std::string frozenText = "an independently captured constructor string";
  const std::string initialText = "an initial live constructor string";
  const std::string updatedText = "the updated live constructor string";
  ASSERT_EQ(obelisk_rt_v1_string_create(lane, frozenText.data(),
                                        frozenText.size(), &strings[0]),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_string_create(lane, initialText.data(),
                                        initialText.size(), &strings[1]),
            OBELISK_RT_OK);
  const obelisk_rt_trace_entry_v1 entry{
      0,      0, 1, OBELISK_RT_TRACE_STRONG, OBELISK_RT_MANAGED_SLOT_STRING,
      nullptr};
  const obelisk_rt_trace_layout_v1 trace{
      OBELISK_RT_VERSION, 0, 8, 8, &entry, 1};
  const obelisk_rt_element_type_v1 element{
      OBELISK_RT_VERSION, OBELISK_RT_ELEMENT_STRING, 99, 0, 0, 8, 8, 0, &trace};
  obelisk_rt_object_v1 *array = nullptr;
  obelisk_rt_gc_root_v1 arrayRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &arrayRoot, &array),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_dynamic_array_create(lane, &element, 1, &array),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_container_write(lane, array, 0, &strings[1], nullptr),
            OBELISK_RT_OK);
  obelisk_rt_object_v1 *path = nullptr;
  ASSERT_EQ(obelisk_rt_v1_reference_path_index_create(
                lane, array, 0, nullptr, 0, 0, nullptr, nullptr, 0, &path),
            OBELISK_RT_OK);
  const std::array<obelisk_rt_functional_value_v1, 2> formals{
      {{90, 0, 0, nullptr, nullptr, nullptr, strings[0],
        OBELISK_RT_FUNCTIONAL_VALUE_STRING, 0},
       {71, 64, 8, nullptr, nullptr, path, 0,
        OBELISK_RT_FUNCTIONAL_VALUE_ARGUMENT_REF, 2}}};
  obelisk_rt_covergroup_v1 handle = 0;
  ASSERT_EQ(obelisk_rt_v1_covergroup_create(context, 10, formals.data(),
                                            formals.size(), nullptr, 0,
                                            &handle),
            OBELISK_RT_OK);
  strings[0] = 0;
  ASSERT_EQ(obelisk_rt_v1_string_create(lane, updatedText.data(),
                                        updatedText.size(), &strings[1]),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_container_write(lane, array, 0, &strings[1], nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_managed_root_range_pop(lane, &stringRoots),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_root_pop(lane, &arrayRoot), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  std::array<uint64_t, 3> reference{};
  ASSERT_EQ(obelisk_rt_v1_covergroup_formal_read(
                context, handle, 71, OBELISK_RT_FUNCTIONAL_VALUE_ARGUMENT_REF,
                192, 24, nullptr, nullptr, 0, reference.data(), nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(reference[2], 2u);
  obelisk_rt_string_v1 projectedString = 0;
  ASSERT_EQ(obelisk_rt_v1_argument_ref_load(
                context, nullptr, nullptr, 0,
                obelisk_rt_object_from_managed_word(reference[0]), reference[1],
                reference[2], 64, 8, 0, OBELISK_RT_ARGUMENT_VALUE_STRING,
                &projectedString, nullptr),
            OBELISK_RT_OK);
  char projectedScratch[8];
  const char *projectedBytes = nullptr;
  uint64_t projectedSize = 0;
  ASSERT_EQ(obelisk_rt_v1_string_view(projectedString, projectedScratch,
                                      &projectedBytes, &projectedSize),
            OBELISK_RT_OK);
  EXPECT_EQ(std::string(projectedBytes, projectedSize), updatedText);
  for (auto [id, expected] : {std::pair{uint64_t{90}, frozenText},
                              std::pair{uint64_t{71}, updatedText}}) {
    obelisk_rt_string_v1 value = 0;
    ASSERT_EQ(obelisk_rt_v1_covergroup_formal_read(
                  context, handle, id, OBELISK_RT_FUNCTIONAL_VALUE_STRING, 64,
                  8, nullptr, nullptr, 0, &value, nullptr),
              OBELISK_RT_OK);
    char scratch[8];
    const char *bytes = nullptr;
    uint64_t size = 0;
    ASSERT_EQ(obelisk_rt_v1_string_view(value, scratch, &bytes, &size),
              OBELISK_RT_OK);
    EXPECT_EQ(std::string(bytes, size), expected);
    value = UINT64_MAX;
    EXPECT_EQ(obelisk_rt_v1_covergroup_formal_read(
                  context, handle, id, OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL, 64,
                  8, nullptr, nullptr, 0, &value, nullptr),
              OBELISK_RT_INVALID_DESIGN);
    EXPECT_EQ(value, UINT64_MAX);
  }
  obelisk_rt_managed_execution_leave(lane, entered);
  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeDesignTest, ContainerFormalsRetainSnapshotsAndLiveStorage) {
  using namespace obelisk::coverage;
  Database schema = makeFunctionalSchema();
  schema.functionalContainerTypes.push_back(
      {800, FunctionalContainerKind::DynamicArray,
       FunctionalContainerKeyKind::None, 99, 0, 0});
  schema.functionalFormals = {
      {70, 10, "snapshot", FunctionalFormalKind::Constructor,
       FunctionalFormalDirection::Input,
       FunctionalExpressionResultKind::Container, 0, 0,
       CoverageSignedness::NotApplicable, 0, 0, 800},
      {71, 10, "live", FunctionalFormalKind::Constructor,
       FunctionalFormalDirection::Ref,
       FunctionalExpressionResultKind::Container, 0, 0,
       CoverageSignedness::NotApplicable, 1, 0, 800}};
  schema.schemaFingerprint = computeSchemaFingerprint(schema);
  obelisk_rt_context *context = createContextForSchema(schema);
  ASSERT_NE(context, nullptr);
  obelisk_rt_gc_lane_v1 *lane = nullptr;
  bool entered = false;
  ASSERT_EQ(obelisk_rt_managed_execution_enter(context, &lane, &entered),
            OBELISK_RT_OK);
  const obelisk_rt_element_type_v1 element{
      OBELISK_RT_VERSION, OBELISK_RT_ELEMENT_BITS, 99, 0, 0, 4, 4, 32, nullptr};
  obelisk_rt_object_v1 *frozen = nullptr;
  ASSERT_EQ(obelisk_rt_v1_dynamic_array_create(lane, &element, 1, &frozen),
            OBELISK_RT_OK);
  uint32_t initial = 3;
  ASSERT_EQ(obelisk_rt_v1_container_write(lane, frozen, 0, &initial, nullptr),
            OBELISK_RT_OK);
  uint64_t word = obelisk_rt_managed_word_from_object(frozen), storage = 0;
  const obelisk_rt_managed_root_slot_v1 root{
      0, OBELISK_RT_MANAGED_ROOT_KIND_CONTAINER, 0};
  ASSERT_EQ(obelisk_rt_v1_native_state_alloc_with_typed_roots(
                context, 64, reinterpret_cast<const uint8_t *>(&word), nullptr,
                &root, 1, &storage),
            OBELISK_RT_OK);
  std::array<obelisk_rt_functional_value_v1, 2> formals{
      {{70, 0, 0, nullptr, nullptr, frozen, 0,
        OBELISK_RT_FUNCTIONAL_VALUE_MANAGED_CONTAINER, 0},
       {71, 64, 8, nullptr, nullptr, nullptr, storage,
        OBELISK_RT_FUNCTIONAL_VALUE_ARGUMENT_REF, 0}}};
  obelisk_rt_covergroup_v1 handle = 0;
  auto create = [&] {
    return obelisk_rt_v1_covergroup_create(context, 10, formals.data(),
                                           formals.size(), nullptr, 0, &handle);
  };
  auto original = formals[0];
  for (auto [kind, typeID, bound] :
       {std::tuple{uint32_t{OBELISK_RT_CONTAINER_QUEUE}, uint64_t{99},
                   uint64_t{4}},
        std::tuple{uint32_t{OBELISK_RT_CONTAINER_DYNAMIC_ARRAY}, uint64_t{100},
                   uint64_t{0}}}) {
    obelisk_rt_object_v1 *wrong = nullptr;
    ASSERT_EQ(obelisk_rt_v1_container_create_typed(
                  lane, kind, typeID, OBELISK_RT_ELEMENT_BITS, 0, 4, 4, 32,
                  nullptr, 0, 1, bound, &wrong),
              OBELISK_RT_OK);
    formals[0].owner = wrong;
    EXPECT_EQ(create(), OBELISK_RT_INVALID_DESIGN);
  }
  formals[0] = original;
  ASSERT_EQ(create(), OBELISK_RT_OK);
  obelisk_rt_object_v1 *updated = nullptr;
  ASSERT_EQ(obelisk_rt_v1_dynamic_array_create(lane, &element, 1, &updated),
            OBELISK_RT_OK);
  initial = 7;
  ASSERT_EQ(obelisk_rt_v1_container_write(lane, updated, 0, &initial, nullptr),
            OBELISK_RT_OK);
  word = obelisk_rt_managed_word_from_object(updated);
  uint8_t dummy = 0;
  ASSERT_EQ(obelisk_rt_v1_argument_ref_store(
                context, &dummy, &dummy, 8, nullptr, storage, 0, 64, 8, 0,
                OBELISK_RT_ARGUMENT_VALUE_BITS, &word, nullptr),
            OBELISK_RT_OK);
  // The constructor retains automatic storage and the captured array after
  // the caller relinquishes both. Collection must trace the complete values.
  ASSERT_EQ(obelisk_rt_v1_native_state_release(context, storage, 0),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  for (auto [id, expected] : {std::pair{uint64_t{70}, uint32_t{3}},
                              std::pair{uint64_t{71}, uint32_t{7}}}) {
    uint64_t actual = 0;
    ASSERT_EQ(obelisk_rt_v1_covergroup_formal_read(
                  context, handle, id,
                  OBELISK_RT_FUNCTIONAL_VALUE_MANAGED_CONTAINER, 64, 8, nullptr,
                  nullptr, 0, &actual, nullptr),
              OBELISK_RT_OK);
    uint32_t value = 0;
    ASSERT_EQ(
        obelisk_rt_v1_container_read(
            obelisk_rt_object_from_managed_word(actual), 0, &value, nullptr),
        OBELISK_RT_OK);
    EXPECT_EQ(value, expected);
    actual = UINT64_MAX;
    EXPECT_EQ(obelisk_rt_v1_covergroup_formal_read(
                  context, handle, id,
                  OBELISK_RT_FUNCTIONAL_VALUE_MANAGED_CONTAINER, 32, 8, nullptr,
                  nullptr, 0, &actual, nullptr),
              OBELISK_RT_INVALID_DESIGN);
    EXPECT_EQ(actual, UINT64_MAX);
  }
  obelisk_rt_managed_execution_leave(lane, entered);
  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeDesignTest,
       CovergroupFormalsCopyHandlesAndValidateLiveNominalTypes) {
  using namespace obelisk::coverage;
  Database schema = makeFunctionalSchema();
  schema.functionalTypes.push_back({2, 1, "source", 0, 2023, "top.source"});
  schema.functionalTypes.push_back({3, 1, "derived", 2, 2023, "top.derived"});
  schema.functionalTypes.push_back(
      {4, 1, "unrelated", 0, 2023, "top.unrelated"});
  for (uint64_t id : {70, 71, 72})
    schema.functionalFormals.push_back(
        {id, 10, "group" + std::to_string(id),
         FunctionalFormalKind::Constructor,
         id == 71 ? FunctionalFormalDirection::Ref
                  : FunctionalFormalDirection::Input,
         FunctionalExpressionResultKind::Covergroup, 0, 0,
         CoverageSignedness::NotApplicable, static_cast<uint32_t>(id - 70), 0,
         2});
  schema.functionalFormals.push_back(
      {73, 10, "sample_default", FunctionalFormalKind::Sample,
       FunctionalFormalDirection::Input,
       FunctionalExpressionResultKind::Covergroup, FunctionalFormalHasDefault,
       0, CoverageSignedness::NotApplicable, 0, 74, 2});
  FunctionalExpression defaultValue;
  defaultValue.id = 74;
  defaultValue.owner = 73;
  defaultValue.ownerKind = FunctionalExpressionOwnerKind::Formal;
  defaultValue.role = FunctionalExpressionRole::FormalDefault;
  defaultValue.resultKind = FunctionalExpressionResultKind::Covergroup;
  defaultValue.signedness = CoverageSignedness::NotApplicable;
  defaultValue.evaluationPhase = FunctionalExpressionEvaluationPhase::Sample;
  defaultValue.resultOrdinal = 2;
  schema.functionalExpressions.push_back(defaultValue);
  std::rotate(schema.functionalTypes.begin(),
              schema.functionalTypes.begin() + 1, schema.functionalTypes.end());
  schema.schemaFingerprint = computeSchemaFingerprint(schema);
  obelisk_rt_context *context = createContextForSchema(schema);
  ASSERT_NE(context, nullptr);
  obelisk_rt_gc_lane_v1 *lane = nullptr;
  bool entered = false;
  ASSERT_EQ(obelisk_rt_managed_execution_enter(context, &lane, &entered),
            OBELISK_RT_OK);
  obelisk_rt_covergroup_v1 first = 0, derived = 0, unrelated = 0;
  ASSERT_EQ(obelisk_rt_v1_covergroup_create(context, 2, nullptr, 0, nullptr, 0,
                                            &first),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_covergroup_create(context, 3, nullptr, 0, nullptr, 0,
                                            &derived),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_covergroup_create(context, 4, nullptr, 0, nullptr, 0,
                                            &unrelated),
            OBELISK_RT_OK);
  uint64_t word = first, storage = 0;
  ASSERT_EQ(obelisk_rt_v1_native_state_alloc(
                context, 64, reinterpret_cast<const uint8_t *>(&word), nullptr,
                &storage),
            OBELISK_RT_OK);
  obelisk_rt_functional_value_v1 formals[] = {
      {70, 0, 0, nullptr, nullptr, nullptr, first,
       OBELISK_RT_FUNCTIONAL_VALUE_COVERGROUP, 0},
      {71, 64, 8, nullptr, nullptr, nullptr, storage,
       OBELISK_RT_FUNCTIONAL_VALUE_ARGUMENT_REF, 0},
      {72, 0, 0, nullptr, nullptr, nullptr, 0,
       OBELISK_RT_FUNCTIONAL_VALUE_COVERGROUP, 0}};
  obelisk_rt_covergroup_v1 handle = 0;
  for (uint64_t invalid : {unrelated, UINT64_MAX}) {
    formals[0].payload = invalid;
    EXPECT_EQ(obelisk_rt_v1_covergroup_create(context, 10, formals, 3, nullptr,
                                              0, &handle),
              OBELISK_RT_INVALID_DESIGN);
    EXPECT_EQ(handle, 0u);
  }
  formals[0].payload = first;
  formals[0].kind = OBELISK_RT_FUNCTIONAL_VALUE_CHANDLE;
  EXPECT_EQ(obelisk_rt_v1_covergroup_create(context, 10, formals, 3, nullptr, 0,
                                            &handle),
            OBELISK_RT_INVALID_DESIGN);
  formals[0].kind = OBELISK_RT_FUNCTIONAL_VALUE_COVERGROUP;
  ASSERT_EQ(obelisk_rt_v1_covergroup_create(context, 10, formals, 3, nullptr, 0,
                                            &handle),
            OBELISK_RT_OK);
  uint8_t sampled = 0;
  obelisk_rt_functional_value_v1 expressions[] = {
      {60, 2, 1, &sampled, nullptr, nullptr, 0,
       OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL, 0},
      {61, 2, 1, &sampled, nullptr, nullptr, 0,
       OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL, 0},
      {74, 0, 0, nullptr, nullptr, nullptr, derived,
       OBELISK_RT_FUNCTIONAL_VALUE_COVERGROUP, 0}};
  ASSERT_EQ(obelisk_rt_v1_covergroup_sample(context, handle, expressions, 3),
            OBELISK_RT_OK);
  expressions[2].payload = unrelated;
  EXPECT_EQ(obelisk_rt_v1_covergroup_sample(context, handle, expressions, 3),
            OBELISK_RT_INVALID_DESIGN);
  expressions[2].payload = 0;
  ASSERT_EQ(obelisk_rt_v1_covergroup_sample(context, handle, expressions, 3),
            OBELISK_RT_OK);
  auto store = [&](uint64_t replacement) {
    uint8_t dummy = 0;
    return obelisk_rt_v1_argument_ref_store(
        context, &dummy, &dummy, 8, nullptr, storage, 0, 64, 8, 0,
        OBELISK_RT_ARGUMENT_VALUE_BITS, &replacement, nullptr);
  };
  ASSERT_EQ(store(derived), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_release(context, storage, 0),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  for (uint64_t id : {70, 71, 72}) {
    uint64_t captured = 42;
    ASSERT_EQ(obelisk_rt_v1_covergroup_formal_read(
                  context, handle, id, OBELISK_RT_FUNCTIONAL_VALUE_COVERGROUP,
                  64, 8, nullptr, nullptr, 0, &captured, nullptr),
              OBELISK_RT_OK);
    EXPECT_EQ(captured, id == 70 ? first : id == 71 ? derived : 0);
    captured = 42;
    EXPECT_EQ(obelisk_rt_v1_covergroup_formal_read(
                  context, handle, id, OBELISK_RT_FUNCTIONAL_VALUE_CHANDLE, 64,
                  8, nullptr, nullptr, 0, &captured, nullptr),
              OBELISK_RT_INVALID_DESIGN);
    EXPECT_EQ(captured, 42u);
  }
  ASSERT_EQ(store(unrelated), OBELISK_RT_OK);
  uint64_t captured = 42;
  EXPECT_EQ(obelisk_rt_v1_covergroup_formal_read(
                context, handle, 71, OBELISK_RT_FUNCTIONAL_VALUE_COVERGROUP, 64,
                8, nullptr, nullptr, 0, &captured, nullptr),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(captured, 42u);
  std::array<uint64_t, 3> reference{7, 8, 9};
  EXPECT_EQ(obelisk_rt_v1_covergroup_formal_read(
                context, handle, 71, OBELISK_RT_FUNCTIONAL_VALUE_ARGUMENT_REF,
                192, 24, nullptr, nullptr, 0, reference.data(), nullptr),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(reference, (std::array<uint64_t, 3>{7, 8, 9}));
  obelisk_rt_managed_execution_leave(lane, entered);
  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeDesignTest,
       VirtualInterfaceFormalsCopyScopesAndValidateLiveSpecializations) {
  using namespace obelisk::coverage;
  Database schema = makeFunctionalSchema();
  schema.functionalVirtualInterfaceTypes = {{1000, 99}, {1001, 99}, {1002, 88}};
  schema.functionalVirtualInterfaceScopes = {{7, 99}, {9, 99}, {11, 88}};
  for (uint64_t id : {70, 71, 72})
    schema.functionalFormals.push_back(
        {id, 10, "interface" + std::to_string(id),
         FunctionalFormalKind::Constructor,
         id == 71 ? FunctionalFormalDirection::Ref
                  : FunctionalFormalDirection::Input,
         FunctionalExpressionResultKind::VirtualInterface, 0, 0,
         CoverageSignedness::NotApplicable, static_cast<uint32_t>(id - 70), 0,
         1001});
  schema.functionalFormals.push_back(
      {73, 10, "sample_default", FunctionalFormalKind::Sample,
       FunctionalFormalDirection::Input,
       FunctionalExpressionResultKind::VirtualInterface,
       FunctionalFormalHasDefault, 0, CoverageSignedness::NotApplicable, 0, 74,
       1001});
  FunctionalExpression defaultValue;
  defaultValue.id = 74;
  defaultValue.owner = 73;
  defaultValue.ownerKind = FunctionalExpressionOwnerKind::Formal;
  defaultValue.role = FunctionalExpressionRole::FormalDefault;
  defaultValue.resultKind = FunctionalExpressionResultKind::VirtualInterface;
  defaultValue.signedness = CoverageSignedness::NotApplicable;
  defaultValue.evaluationPhase = FunctionalExpressionEvaluationPhase::Sample;
  defaultValue.resultOrdinal = 2;
  schema.functionalExpressions.push_back(defaultValue);
  schema.schemaFingerprint = computeSchemaFingerprint(schema);
  obelisk_rt_context *context = createContextForSchema(schema);
  ASSERT_NE(context, nullptr);
  obelisk_rt_gc_lane_v1 *lane = nullptr;
  bool entered = false;
  ASSERT_EQ(obelisk_rt_managed_execution_enter(context, &lane, &entered),
            OBELISK_RT_OK);
  const uint64_t first = 7, second = 9;
  uint64_t word = first, storage = 0;
  ASSERT_EQ(obelisk_rt_v1_native_state_alloc(
                context, 64, reinterpret_cast<const uint8_t *>(&word), nullptr,
                &storage),
            OBELISK_RT_OK);
  obelisk_rt_functional_value_v1 formals[] = {
      {70, 0, 0, nullptr, nullptr, nullptr, first,
       OBELISK_RT_FUNCTIONAL_VALUE_VIRTUAL_INTERFACE, 0},
      {71, 64, 8, nullptr, nullptr, nullptr, storage,
       OBELISK_RT_FUNCTIONAL_VALUE_ARGUMENT_REF, 0},
      {72, 0, 0, nullptr, nullptr, nullptr, 0,
       OBELISK_RT_FUNCTIONAL_VALUE_VIRTUAL_INTERFACE, 0}};
  obelisk_rt_covergroup_v1 handle = 0;
  formals[0].kind = OBELISK_RT_FUNCTIONAL_VALUE_CHANDLE;
  EXPECT_EQ(obelisk_rt_v1_covergroup_create(context, 10, formals, 3, nullptr, 0,
                                            &handle),
            OBELISK_RT_INVALID_DESIGN);
  formals[0].kind = OBELISK_RT_FUNCTIONAL_VALUE_VIRTUAL_INTERFACE;
  formals[0].bit_width = 64;
  EXPECT_EQ(obelisk_rt_v1_covergroup_create(context, 10, formals, 3, nullptr, 0,
                                            &handle),
            OBELISK_RT_INVALID_DESIGN);
  formals[0].bit_width = 0;
  for (uint64_t invalid : {uint64_t{11}, UINT64_MAX}) {
    formals[0].payload = invalid;
    EXPECT_EQ(obelisk_rt_v1_covergroup_create(context, 10, formals, 3, nullptr,
                                              0, &handle),
              OBELISK_RT_INVALID_DESIGN);
    EXPECT_EQ(handle, 0u);
  }
  formals[0].payload = first;
  ASSERT_EQ(obelisk_rt_v1_covergroup_create(context, 10, formals, 3, nullptr, 0,
                                            &handle),
            OBELISK_RT_OK);
  uint8_t sampled = 0;
  obelisk_rt_functional_value_v1 expressions[] = {
      {60, 2, 1, &sampled, nullptr, nullptr, 0,
       OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL, 0},
      {61, 2, 1, &sampled, nullptr, nullptr, 0,
       OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL, 0},
      {74, 0, 0, nullptr, nullptr, nullptr, first,
       OBELISK_RT_FUNCTIONAL_VALUE_VIRTUAL_INTERFACE, 0}};
  // Modport views share the same instance identity and parameter
  // specialization.
  for (uint64_t identity : {first, second, uint64_t{0}}) {
    expressions[2].payload = identity;
    ASSERT_EQ(obelisk_rt_v1_covergroup_sample(context, handle, expressions, 3),
              OBELISK_RT_OK);
  }
  expressions[2].payload = 11;
  EXPECT_EQ(obelisk_rt_v1_covergroup_sample(context, handle, expressions, 3),
            OBELISK_RT_INVALID_DESIGN);
  word = second;
  uint8_t dummy = 0;
  ASSERT_EQ(obelisk_rt_v1_argument_ref_store(
                context, &dummy, &dummy, 8, nullptr, storage, 0, 64, 8, 0,
                OBELISK_RT_ARGUMENT_VALUE_BITS, &word, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_release(context, storage, 0),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  for (uint64_t id : {70, 71, 72}) {
    uint64_t captured = 42;
    ASSERT_EQ(obelisk_rt_v1_covergroup_formal_read(
                  context, handle, id,
                  OBELISK_RT_FUNCTIONAL_VALUE_VIRTUAL_INTERFACE, 64, 8, nullptr,
                  nullptr, 0, &captured, nullptr),
              OBELISK_RT_OK);
    EXPECT_EQ(captured, id == 70 ? first : id == 71 ? second : 0);
    captured = 42;
    EXPECT_EQ(obelisk_rt_v1_covergroup_formal_read(
                  context, handle, id, OBELISK_RT_FUNCTIONAL_VALUE_CHANDLE, 64,
                  8, nullptr, nullptr, 0, &captured, nullptr),
              OBELISK_RT_INVALID_DESIGN);
    EXPECT_EQ(captured, 42u);
  }
  uint64_t invalid = 11;
  ASSERT_EQ(obelisk_rt_v1_argument_ref_store(
                context, &dummy, &dummy, 8, nullptr, storage, 0, 64, 8, 0,
                OBELISK_RT_ARGUMENT_VALUE_BITS, &invalid, nullptr),
            OBELISK_RT_OK);
  uint64_t captured = 42;
  EXPECT_EQ(obelisk_rt_v1_covergroup_formal_read(
                context, handle, 71,
                OBELISK_RT_FUNCTIONAL_VALUE_VIRTUAL_INTERFACE, 64, 8, nullptr,
                nullptr, 0, &captured, nullptr),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(captured, 42u);
  std::array<uint64_t, 3> reference{7, 8, 9};
  EXPECT_EQ(obelisk_rt_v1_covergroup_formal_read(
                context, handle, 71, OBELISK_RT_FUNCTIONAL_VALUE_ARGUMENT_REF,
                192, 24, nullptr, nullptr, 0, reference.data(), nullptr),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(reference, (std::array<uint64_t, 3>{7, 8, 9}));
  obelisk_rt_managed_execution_leave(lane, entered);
  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeDesignTest,
       SemaphoreFormalsRetainSharedBucketsAndValidateLiveRefs) {
  using namespace obelisk::coverage;
  Database schema = makeFunctionalSchema();
  for (uint64_t id : {70, 71, 72})
    schema.functionalFormals.push_back(
        {id, 10, "semaphore" + std::to_string(id),
         FunctionalFormalKind::Constructor,
         id == 71 ? FunctionalFormalDirection::Ref
                  : FunctionalFormalDirection::Input,
         FunctionalExpressionResultKind::Semaphore, 0, 0,
         CoverageSignedness::NotApplicable, static_cast<uint32_t>(id - 70), 0,
         0});
  schema.functionalFormals.push_back(
      {73, 10, "sample_default", FunctionalFormalKind::Sample,
       FunctionalFormalDirection::Input,
       FunctionalExpressionResultKind::Semaphore, FunctionalFormalHasDefault, 0,
       CoverageSignedness::NotApplicable, 0, 74, 0});
  FunctionalExpression defaultValue;
  defaultValue.id = 74;
  defaultValue.owner = 73;
  defaultValue.ownerKind = FunctionalExpressionOwnerKind::Formal;
  defaultValue.role = FunctionalExpressionRole::FormalDefault;
  defaultValue.resultKind = FunctionalExpressionResultKind::Semaphore;
  defaultValue.signedness = CoverageSignedness::NotApplicable;
  defaultValue.evaluationPhase = FunctionalExpressionEvaluationPhase::Sample;
  defaultValue.resultOrdinal = 2;
  schema.functionalExpressions.push_back(defaultValue);
  schema.schemaFingerprint = computeSchemaFingerprint(schema);
  obelisk_rt_context *context = createContextForSchema(schema);
  ASSERT_NE(context, nullptr);
  obelisk_rt_gc_lane_v1 *lane = nullptr;
  bool entered = false;
  ASSERT_EQ(obelisk_rt_managed_execution_enter(context, &lane, &entered),
            OBELISK_RT_OK);
  obelisk_rt_object_v1 *first = nullptr, *second = nullptr,
                       *unrelated = nullptr;
  ASSERT_EQ(obelisk_rt_v1_semaphore_create(lane, 2, &first), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_semaphore_create(lane, 3, &second), OBELISK_RT_OK);
  obelisk_rt_element_type_v1 element{};
  element.version = OBELISK_RT_VERSION;
  element.type_id = 99;
  element.kind = OBELISK_RT_ELEMENT_BITS;
  element.value_size = 4;
  element.alignment = 4;
  element.bit_width = 32;
  ASSERT_EQ(obelisk_rt_v1_queue_create(lane, &element, UINT64_MAX, &unrelated),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 unrelatedRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &unrelatedRoot, &unrelated),
            OBELISK_RT_OK);
  const uint64_t firstWord = obelisk_rt_managed_word_from_object(first);
  const uint64_t secondWord = obelisk_rt_managed_word_from_object(second);
  uint64_t word = firstWord, storage = 0;
  const obelisk_rt_managed_root_slot_v1 root{
      0, OBELISK_RT_MANAGED_ROOT_KIND_CONTAINER, 0};
  ASSERT_EQ(obelisk_rt_v1_native_state_alloc_with_typed_roots(
                context, 64, reinterpret_cast<const uint8_t *>(&word), nullptr,
                &root, 1, &storage),
            OBELISK_RT_OK);
  obelisk_rt_functional_value_v1 formals[] = {
      {70, 0, 0, nullptr, nullptr, first, 0,
       OBELISK_RT_FUNCTIONAL_VALUE_SEMAPHORE, 0},
      {71, 64, 8, nullptr, nullptr, nullptr, storage,
       OBELISK_RT_FUNCTIONAL_VALUE_ARGUMENT_REF, 0},
      {72, 0, 0, nullptr, nullptr, nullptr, 0,
       OBELISK_RT_FUNCTIONAL_VALUE_SEMAPHORE, 0}};
  obelisk_rt_covergroup_v1 handle = 0;
  formals[0].kind = OBELISK_RT_FUNCTIONAL_VALUE_MANAGED_CONTAINER;
  EXPECT_EQ(obelisk_rt_v1_covergroup_create(context, 10, formals, 3, nullptr, 0,
                                            &handle),
            OBELISK_RT_INVALID_DESIGN);
  formals[0].kind = OBELISK_RT_FUNCTIONAL_VALUE_SEMAPHORE;
  formals[0].bit_width = 64;
  EXPECT_EQ(obelisk_rt_v1_covergroup_create(context, 10, formals, 3, nullptr, 0,
                                            &handle),
            OBELISK_RT_INVALID_DESIGN);
  formals[0].bit_width = 0;
  formals[0].owner = unrelated;
  EXPECT_EQ(obelisk_rt_v1_covergroup_create(context, 10, formals, 3, nullptr, 0,
                                            &handle),
            OBELISK_RT_INVALID_DESIGN);
  EXPECT_EQ(handle, 0u);
  formals[0].owner = first;
  ASSERT_EQ(obelisk_rt_v1_covergroup_create(context, 10, formals, 3, nullptr, 0,
                                            &handle),
            OBELISK_RT_OK);
  uint8_t sampled = 0;
  obelisk_rt_functional_value_v1 expressions[] = {
      {60, 2, 1, &sampled, nullptr, nullptr, 0,
       OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL, 0},
      {61, 2, 1, &sampled, nullptr, nullptr, 0,
       OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL, 0},
      {74, 0, 0, nullptr, nullptr, first, 0,
       OBELISK_RT_FUNCTIONAL_VALUE_SEMAPHORE, 0}};
  for (auto *identity :
       {first, second, static_cast<obelisk_rt_object_v1 *>(nullptr)}) {
    expressions[2].owner = identity;
    ASSERT_EQ(obelisk_rt_v1_covergroup_sample(context, handle, expressions, 3),
              OBELISK_RT_OK);
  }
  expressions[2].owner = unrelated;
  EXPECT_EQ(obelisk_rt_v1_covergroup_sample(context, handle, expressions, 3),
            OBELISK_RT_INVALID_DESIGN);
  uint32_t acquired = 0;
  ASSERT_EQ(obelisk_rt_v1_semaphore_try_get(first, 1, &acquired),
            OBELISK_RT_OK);
  ASSERT_EQ(acquired, 1u);
  word = secondWord;
  uint8_t dummy = 0;
  ASSERT_EQ(obelisk_rt_v1_argument_ref_store(
                context, &dummy, &dummy, 8, nullptr, storage, 0, 64, 8, 0,
                OBELISK_RT_ARGUMENT_VALUE_CLASS, &word, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_release(context, storage, 0),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  for (uint64_t id : {70, 71, 72}) {
    uint64_t captured = 42;
    ASSERT_EQ(obelisk_rt_v1_covergroup_formal_read(
                  context, handle, id, OBELISK_RT_FUNCTIONAL_VALUE_SEMAPHORE,
                  64, 8, nullptr, nullptr, 0, &captured, nullptr),
              OBELISK_RT_OK);
    EXPECT_EQ(captured, id == 70 ? firstWord : id == 71 ? secondWord : 0);
    captured = 42;
    EXPECT_EQ(obelisk_rt_v1_covergroup_formal_read(
                  context, handle, id,
                  OBELISK_RT_FUNCTIONAL_VALUE_MANAGED_CONTAINER, 64, 8, nullptr,
                  nullptr, 0, &captured, nullptr),
              OBELISK_RT_INVALID_DESIGN);
    EXPECT_EQ(captured, 42u);
  }
  // The copied input still observes the original bucket's acquired key.
  ASSERT_EQ(obelisk_rt_v1_semaphore_try_get(
                obelisk_rt_object_from_managed_word(firstWord), 2, &acquired),
            OBELISK_RT_OK);
  EXPECT_EQ(acquired, 0u);
  ASSERT_EQ(obelisk_rt_v1_semaphore_try_get(
                obelisk_rt_object_from_managed_word(firstWord), 1, &acquired),
            OBELISK_RT_OK);
  EXPECT_EQ(acquired, 1u);
  uint64_t invalid = obelisk_rt_managed_word_from_object(unrelated);
  ASSERT_EQ(obelisk_rt_v1_argument_ref_store(
                context, &dummy, &dummy, 8, nullptr, storage, 0, 64, 8, 0,
                OBELISK_RT_ARGUMENT_VALUE_CLASS, &invalid, nullptr),
            OBELISK_RT_OK);
  uint64_t captured = 42;
  EXPECT_EQ(obelisk_rt_v1_covergroup_formal_read(
                context, handle, 71, OBELISK_RT_FUNCTIONAL_VALUE_SEMAPHORE, 64,
                8, nullptr, nullptr, 0, &captured, nullptr),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(captured, 42u);
  std::array<uint64_t, 3> reference{7, 8, 9};
  EXPECT_EQ(obelisk_rt_v1_covergroup_formal_read(
                context, handle, 71, OBELISK_RT_FUNCTIONAL_VALUE_ARGUMENT_REF,
                192, 24, nullptr, nullptr, 0, reference.data(), nullptr),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(reference, (std::array<uint64_t, 3>{7, 8, 9}));
  ASSERT_EQ(obelisk_rt_v1_gc_root_pop(lane, &unrelatedRoot), OBELISK_RT_OK);
  obelisk_rt_managed_execution_leave(lane, entered);
  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeDesignTest,
       MailboxFormalsRetainSharedMessagesAndValidateNominalTypes) {
  using namespace obelisk::coverage;
  Database schema = makeFunctionalSchema();
  schema.functionalContainerTypes.push_back(
      {77, FunctionalContainerKind::Mailbox, FunctionalContainerKeyKind::None,
       99, 0, 0});
  for (uint64_t id : {70, 71, 72})
    schema.functionalFormals.push_back({id, 10, "mailbox" + std::to_string(id),
                                        FunctionalFormalKind::Constructor,
                                        id == 71
                                            ? FunctionalFormalDirection::Ref
                                            : FunctionalFormalDirection::Input,
                                        FunctionalExpressionResultKind::Mailbox,
                                        0, 0, CoverageSignedness::NotApplicable,
                                        static_cast<uint32_t>(id - 70), 0, 77});
  schema.functionalFormals.push_back(
      {73, 10, "sample_default", FunctionalFormalKind::Sample,
       FunctionalFormalDirection::Input,
       FunctionalExpressionResultKind::Mailbox, FunctionalFormalHasDefault, 0,
       CoverageSignedness::NotApplicable, 0, 74, 77});
  FunctionalExpression defaultValue;
  defaultValue.id = 74;
  defaultValue.owner = 73;
  defaultValue.ownerKind = FunctionalExpressionOwnerKind::Formal;
  defaultValue.role = FunctionalExpressionRole::FormalDefault;
  defaultValue.resultKind = FunctionalExpressionResultKind::Mailbox;
  defaultValue.signedness = CoverageSignedness::NotApplicable;
  defaultValue.evaluationPhase = FunctionalExpressionEvaluationPhase::Sample;
  defaultValue.resultOrdinal = 2;
  schema.functionalExpressions.push_back(defaultValue);
  schema.schemaFingerprint = computeSchemaFingerprint(schema);
  obelisk_rt_context *context = createContextForSchema(schema);
  ASSERT_NE(context, nullptr);
  obelisk_rt_gc_lane_v1 *lane = nullptr;
  bool entered = false;
  ASSERT_EQ(obelisk_rt_managed_execution_enter(context, &lane, &entered),
            OBELISK_RT_OK);
  obelisk_rt_object_v1 *first = nullptr, *second = nullptr,
                       *unrelated = nullptr;
  ASSERT_EQ(obelisk_rt_v1_mailbox_create_typed(lane, 99,
                                               OBELISK_RT_ELEMENT_BITS, 0, 4, 4,
                                               32, nullptr, 0, 1, &first),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_mailbox_create_typed(lane, 99,
                                               OBELISK_RT_ELEMENT_BITS, 0, 4, 4,
                                               32, nullptr, 0, 0, &second),
            OBELISK_RT_OK);
  obelisk_rt_object_v1 *wrongMessage = nullptr;
  ASSERT_EQ(obelisk_rt_v1_mailbox_create_typed(
                lane, 100, OBELISK_RT_ELEMENT_BITS, 0, 4, 4, 32, nullptr, 0, 0,
                &wrongMessage),
            OBELISK_RT_OK);
  obelisk_rt_element_type_v1 element{};
  element.version = OBELISK_RT_VERSION;
  element.type_id = 99;
  element.kind = OBELISK_RT_ELEMENT_BITS;
  element.value_size = 4;
  element.alignment = 4;
  element.bit_width = 32;
  ASSERT_EQ(obelisk_rt_v1_queue_create(lane, &element, UINT64_MAX, &unrelated),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 unrelatedRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &unrelatedRoot, &unrelated),
            OBELISK_RT_OK);
  const uint64_t firstWord = obelisk_rt_managed_word_from_object(first);
  const uint64_t secondWord = obelisk_rt_managed_word_from_object(second);
  uint64_t word = firstWord, storage = 0;
  const obelisk_rt_managed_root_slot_v1 root{
      0, OBELISK_RT_MANAGED_ROOT_KIND_CONTAINER, 0};
  ASSERT_EQ(obelisk_rt_v1_native_state_alloc_with_typed_roots(
                context, 64, reinterpret_cast<const uint8_t *>(&word), nullptr,
                &root, 1, &storage),
            OBELISK_RT_OK);
  obelisk_rt_functional_value_v1 formals[] = {
      {70, 0, 0, nullptr, nullptr, first, 0,
       OBELISK_RT_FUNCTIONAL_VALUE_MAILBOX, 0},
      {71, 64, 8, nullptr, nullptr, nullptr, storage,
       OBELISK_RT_FUNCTIONAL_VALUE_ARGUMENT_REF, 0},
      {72, 0, 0, nullptr, nullptr, nullptr, 0,
       OBELISK_RT_FUNCTIONAL_VALUE_MAILBOX, 0}};
  obelisk_rt_covergroup_v1 handle = 0;
  formals[0].kind = OBELISK_RT_FUNCTIONAL_VALUE_MANAGED_CONTAINER;
  EXPECT_EQ(obelisk_rt_v1_covergroup_create(context, 10, formals, 3, nullptr, 0,
                                            &handle),
            OBELISK_RT_INVALID_DESIGN);
  formals[0].kind = OBELISK_RT_FUNCTIONAL_VALUE_MAILBOX;
  formals[0].bit_width = 64;
  EXPECT_EQ(obelisk_rt_v1_covergroup_create(context, 10, formals, 3, nullptr, 0,
                                            &handle),
            OBELISK_RT_INVALID_DESIGN);
  formals[0].bit_width = 0;
  formals[0].owner = unrelated;
  EXPECT_EQ(obelisk_rt_v1_covergroup_create(context, 10, formals, 3, nullptr, 0,
                                            &handle),
            OBELISK_RT_INVALID_DESIGN);
  EXPECT_EQ(handle, 0u);
  formals[0].owner = wrongMessage;
  EXPECT_EQ(obelisk_rt_v1_covergroup_create(context, 10, formals, 3, nullptr, 0,
                                            &handle),
            OBELISK_RT_INVALID_DESIGN);
  formals[0].owner = first;
  ASSERT_EQ(obelisk_rt_v1_covergroup_create(context, 10, formals, 3, nullptr, 0,
                                            &handle),
            OBELISK_RT_OK);
  uint8_t sampled = 0;
  obelisk_rt_functional_value_v1 expressions[] = {
      {60, 2, 1, &sampled, nullptr, nullptr, 0,
       OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL, 0},
      {61, 2, 1, &sampled, nullptr, nullptr, 0,
       OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL, 0},
      {74, 0, 0, nullptr, nullptr, first, 0,
       OBELISK_RT_FUNCTIONAL_VALUE_MAILBOX, 0}};
  for (auto *identity :
       {first, second, static_cast<obelisk_rt_object_v1 *>(nullptr)}) {
    expressions[2].owner = identity;
    ASSERT_EQ(obelisk_rt_v1_covergroup_sample(context, handle, expressions, 3),
              OBELISK_RT_OK);
  }
  expressions[2].owner = unrelated;
  EXPECT_EQ(obelisk_rt_v1_covergroup_sample(context, handle, expressions, 3),
            OBELISK_RT_INVALID_DESIGN);
  expressions[2].owner = wrongMessage;
  EXPECT_EQ(obelisk_rt_v1_covergroup_sample(context, handle, expressions, 3),
            OBELISK_RT_INVALID_DESIGN);
  uint32_t sent = 0, message = 11;
  for (auto *mailbox : {first, second, second}) {
    ASSERT_EQ(
        obelisk_rt_v1_mailbox_try_put(lane, mailbox, &message, nullptr, &sent),
        OBELISK_RT_OK);
    ASSERT_EQ(sent, 1u);
  }
  word = secondWord;
  uint8_t dummy = 0;
  ASSERT_EQ(obelisk_rt_v1_argument_ref_store(
                context, &dummy, &dummy, 8, nullptr, storage, 0, 64, 8, 0,
                OBELISK_RT_ARGUMENT_VALUE_CLASS, &word, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_release(context, storage, 0),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  for (uint64_t id : {70, 71, 72}) {
    uint64_t captured = 42;
    ASSERT_EQ(obelisk_rt_v1_covergroup_formal_read(
                  context, handle, id, OBELISK_RT_FUNCTIONAL_VALUE_MAILBOX, 64,
                  8, nullptr, nullptr, 0, &captured, nullptr),
              OBELISK_RT_OK);
    EXPECT_EQ(captured, id == 70 ? firstWord : id == 71 ? secondWord : 0);
    captured = 42;
    EXPECT_EQ(obelisk_rt_v1_covergroup_formal_read(
                  context, handle, id,
                  OBELISK_RT_FUNCTIONAL_VALUE_MANAGED_CONTAINER, 64, 8, nullptr,
                  nullptr, 0, &captured, nullptr),
              OBELISK_RT_INVALID_DESIGN);
    EXPECT_EQ(captured, 42u);
  }
  uint32_t count = 0;
  ASSERT_EQ(obelisk_rt_v1_mailbox_num(first, &count), OBELISK_RT_OK);
  EXPECT_EQ(count, 1u);
  ASSERT_EQ(obelisk_rt_v1_mailbox_num(second, &count), OBELISK_RT_OK);
  EXPECT_EQ(count, 2u);
  uint32_t received = 0;
  ASSERT_EQ(obelisk_rt_v1_mailbox_try_get(first, &message, nullptr, &received),
            OBELISK_RT_OK);
  EXPECT_EQ(received, 1u);
  EXPECT_EQ(message, 11u);
  ASSERT_EQ(obelisk_rt_v1_mailbox_num(first, &count), OBELISK_RT_OK);
  EXPECT_EQ(count, 0u);
  uint64_t invalid = obelisk_rt_managed_word_from_object(unrelated);
  ASSERT_EQ(obelisk_rt_v1_argument_ref_store(
                context, &dummy, &dummy, 8, nullptr, storage, 0, 64, 8, 0,
                OBELISK_RT_ARGUMENT_VALUE_CLASS, &invalid, nullptr),
            OBELISK_RT_OK);
  uint64_t captured = 42;
  EXPECT_EQ(obelisk_rt_v1_covergroup_formal_read(
                context, handle, 71, OBELISK_RT_FUNCTIONAL_VALUE_MAILBOX, 64, 8,
                nullptr, nullptr, 0, &captured, nullptr),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(captured, 42u);
  std::array<uint64_t, 3> reference{7, 8, 9};
  EXPECT_EQ(obelisk_rt_v1_covergroup_formal_read(
                context, handle, 71, OBELISK_RT_FUNCTIONAL_VALUE_ARGUMENT_REF,
                192, 24, nullptr, nullptr, 0, reference.data(), nullptr),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(reference, (std::array<uint64_t, 3>{7, 8, 9}));
  ASSERT_EQ(obelisk_rt_v1_gc_root_pop(lane, &unrelatedRoot), OBELISK_RT_OK);
  obelisk_rt_managed_execution_leave(lane, entered);
  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeDesignTest,
       ProcessFormalsCopyIdentitiesAndRetainLiveAliases) {
  using namespace obelisk::coverage;
  Database schema = makeFunctionalSchema();
  for (uint64_t id : {70, 71, 72})
    schema.functionalFormals.push_back({id, 10, "process" + std::to_string(id),
                                        FunctionalFormalKind::Constructor,
                                        id == 71
                                            ? FunctionalFormalDirection::Ref
                                            : FunctionalFormalDirection::Input,
                                        FunctionalExpressionResultKind::Process,
                                        0, 0, CoverageSignedness::NotApplicable,
                                        static_cast<uint32_t>(id - 70), 0, 0});
  schema.functionalFormals.push_back(
      {73, 10, "sample_default", FunctionalFormalKind::Sample,
       FunctionalFormalDirection::Input,
       FunctionalExpressionResultKind::Process, FunctionalFormalHasDefault, 0,
       CoverageSignedness::NotApplicable, 0, 74, 0});
  FunctionalExpression defaultValue;
  defaultValue.id = 74;
  defaultValue.owner = 73;
  defaultValue.ownerKind = FunctionalExpressionOwnerKind::Formal;
  defaultValue.role = FunctionalExpressionRole::FormalDefault;
  defaultValue.resultKind = FunctionalExpressionResultKind::Process;
  defaultValue.signedness = CoverageSignedness::NotApplicable;
  defaultValue.evaluationPhase = FunctionalExpressionEvaluationPhase::Sample;
  defaultValue.resultOrdinal = 2;
  schema.functionalExpressions.push_back(defaultValue);
  schema.schemaFingerprint = computeSchemaFingerprint(schema);
  obelisk_rt_context *context = createContextForSchema(schema);
  ASSERT_NE(context, nullptr);
  obelisk_rt_gc_lane_v1 *lane = nullptr;
  bool entered = false;
  ASSERT_EQ(obelisk_rt_managed_execution_enter(context, &lane, &entered),
            OBELISK_RT_OK);
  const uint64_t first = OBELISK_RT_LOGICAL_PROCESS_NATIVE_TAG | 7;
  const uint64_t second = OBELISK_RT_LOGICAL_PROCESS_NATIVE_TAG | 9;
  context->terminatedNativeProcesses.insert(7);
  context->terminatedDesignTasks.insert(7);
  context->killedDesignTasks.insert(9);
  context->activeLogicalProcessToken = second;
  uint64_t word = first, storage = 0;
  ASSERT_EQ(obelisk_rt_v1_native_state_alloc(
                context, 64, reinterpret_cast<const uint8_t *>(&word), nullptr,
                &storage),
            OBELISK_RT_OK);
  obelisk_rt_functional_value_v1 formals[] = {
      {70, 0, 0, nullptr, nullptr, nullptr, first,
       OBELISK_RT_FUNCTIONAL_VALUE_PROCESS, 0},
      {71, 64, 8, nullptr, nullptr, nullptr, storage,
       OBELISK_RT_FUNCTIONAL_VALUE_ARGUMENT_REF, 0},
      {72, 0, 0, nullptr, nullptr, nullptr, 0,
       OBELISK_RT_FUNCTIONAL_VALUE_PROCESS, 0}};
  obelisk_rt_covergroup_v1 handle = 0;
  formals[0].kind = OBELISK_RT_FUNCTIONAL_VALUE_CHANDLE;
  EXPECT_EQ(obelisk_rt_v1_covergroup_create(context, 10, formals, 3, nullptr, 0,
                                            &handle),
            OBELISK_RT_INVALID_DESIGN);
  formals[0].kind = OBELISK_RT_FUNCTIONAL_VALUE_PROCESS;
  formals[0].bit_width = 64;
  EXPECT_EQ(obelisk_rt_v1_covergroup_create(context, 10, formals, 3, nullptr, 0,
                                            &handle),
            OBELISK_RT_INVALID_DESIGN);
  formals[0].bit_width = 0;
  formals[0].payload = UINT64_MAX;
  EXPECT_EQ(obelisk_rt_v1_covergroup_create(context, 10, formals, 3, nullptr, 0,
                                            &handle),
            OBELISK_RT_INVALID_DESIGN);
  EXPECT_EQ(handle, 0u);
  formals[0].payload = first;
  ASSERT_EQ(obelisk_rt_v1_covergroup_create(context, 10, formals, 3, nullptr, 0,
                                            &handle),
            OBELISK_RT_OK);
  uint8_t sampled = 0;
  obelisk_rt_functional_value_v1 expressions[] = {
      {60, 2, 1, &sampled, nullptr, nullptr, 0,
       OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL, 0},
      {61, 2, 1, &sampled, nullptr, nullptr, 0,
       OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL, 0},
      {74, 0, 0, nullptr, nullptr, nullptr, first,
       OBELISK_RT_FUNCTIONAL_VALUE_PROCESS, 0}};
  // Completed identities remain valid in both scheduler namespaces.
  for (uint64_t identity :
       {first, second, uint64_t{7}, uint64_t{9}, uint64_t{0}}) {
    expressions[2].payload = identity;
    ASSERT_EQ(obelisk_rt_v1_covergroup_sample(context, handle, expressions, 3),
              OBELISK_RT_OK);
  }
  expressions[2].payload = UINT64_MAX;
  EXPECT_EQ(obelisk_rt_v1_covergroup_sample(context, handle, expressions, 3),
            OBELISK_RT_INVALID_DESIGN);
  word = second;
  uint8_t dummy = 0;
  ASSERT_EQ(obelisk_rt_v1_argument_ref_store(
                context, &dummy, &dummy, 8, nullptr, storage, 0, 64, 8, 0,
                OBELISK_RT_ARGUMENT_VALUE_BITS, &word, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_release(context, storage, 0),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  for (uint64_t id : {70, 71, 72}) {
    uint64_t captured = 42;
    ASSERT_EQ(obelisk_rt_v1_covergroup_formal_read(
                  context, handle, id, OBELISK_RT_FUNCTIONAL_VALUE_PROCESS, 64,
                  8, nullptr, nullptr, 0, &captured, nullptr),
              OBELISK_RT_OK);
    EXPECT_EQ(captured, id == 70 ? first : id == 71 ? second : 0);
    captured = 42;
    EXPECT_EQ(obelisk_rt_v1_covergroup_formal_read(
                  context, handle, id, OBELISK_RT_FUNCTIONAL_VALUE_CHANDLE, 64,
                  8, nullptr, nullptr, 0, &captured, nullptr),
              OBELISK_RT_INVALID_DESIGN);
    EXPECT_EQ(captured, 42u);
  }
  uint64_t invalid = UINT64_MAX;
  ASSERT_EQ(obelisk_rt_v1_argument_ref_store(
                context, &dummy, &dummy, 8, nullptr, storage, 0, 64, 8, 0,
                OBELISK_RT_ARGUMENT_VALUE_BITS, &invalid, nullptr),
            OBELISK_RT_OK);
  uint64_t captured = 42;
  EXPECT_EQ(obelisk_rt_v1_covergroup_formal_read(
                context, handle, 71, OBELISK_RT_FUNCTIONAL_VALUE_PROCESS, 64, 8,
                nullptr, nullptr, 0, &captured, nullptr),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(captured, 42u);
  std::array<uint64_t, 3> reference{7, 8, 9};
  EXPECT_EQ(obelisk_rt_v1_covergroup_formal_read(
                context, handle, 71, OBELISK_RT_FUNCTIONAL_VALUE_ARGUMENT_REF,
                192, 24, nullptr, nullptr, 0, reference.data(), nullptr),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(reference, (std::array<uint64_t, 3>{7, 8, 9}));
  obelisk_rt_managed_execution_leave(lane, entered);
  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeDesignTest,
       EventFormalsCopyIdentityAndRetainLiveAliases) {
  using namespace obelisk::coverage;
  Database schema = makeFunctionalSchema();
  for (uint64_t id : {70, 71, 72})
    schema.functionalFormals.push_back({id, 10, "event" + std::to_string(id),
                                        FunctionalFormalKind::Constructor,
                                        id == 71
                                            ? FunctionalFormalDirection::Ref
                                            : FunctionalFormalDirection::Input,
                                        FunctionalExpressionResultKind::Event,
                                        0, 0, CoverageSignedness::NotApplicable,
                                        static_cast<uint32_t>(id - 70), 0, 0});
  schema.schemaFingerprint = computeSchemaFingerprint(schema);
  obelisk_rt_context *context = createContextForSchema(schema);
  ASSERT_NE(context, nullptr);
  obelisk_rt_gc_lane_v1 *lane = nullptr;
  bool entered = false;
  ASSERT_EQ(obelisk_rt_managed_execution_enter(context, &lane, &entered),
            OBELISK_RT_OK);
  uint64_t first = UINT64_MAX, second = UINT64_MAX;
  ASSERT_EQ(obelisk_rt_v1_scheduler_event_create(context, &first),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_event_create(context, &second),
            OBELISK_RT_OK);
  ASSERT_NE(first, second);
  uint64_t word = first, storage = 0;
  ASSERT_EQ(obelisk_rt_v1_native_state_alloc(
                context, 64, reinterpret_cast<const uint8_t *>(&word), nullptr,
                &storage),
            OBELISK_RT_OK);
  obelisk_rt_functional_value_v1 formals[] = {
      {70, 0, 0, nullptr, nullptr, nullptr, first,
       OBELISK_RT_FUNCTIONAL_VALUE_EVENT, 0},
      {71, 64, 8, nullptr, nullptr, nullptr, storage,
       OBELISK_RT_FUNCTIONAL_VALUE_ARGUMENT_REF, 0},
      {72, 0, 0, nullptr, nullptr, nullptr, UINT64_MAX,
       OBELISK_RT_FUNCTIONAL_VALUE_EVENT, 0}};
  obelisk_rt_covergroup_v1 handle = 0;
  formals[0].kind = OBELISK_RT_FUNCTIONAL_VALUE_CHANDLE;
  EXPECT_EQ(obelisk_rt_v1_covergroup_create(context, 10, formals, 3, nullptr, 0,
                                            &handle),
            OBELISK_RT_INVALID_DESIGN);
  formals[0].kind = OBELISK_RT_FUNCTIONAL_VALUE_EVENT;
  formals[0].bit_width = 64;
  EXPECT_EQ(obelisk_rt_v1_covergroup_create(context, 10, formals, 3, nullptr, 0,
                                            &handle),
            OBELISK_RT_INVALID_DESIGN);
  formals[0].bit_width = 0;
  ASSERT_EQ(obelisk_rt_v1_covergroup_create(context, 10, formals, 3, nullptr, 0,
                                            &handle),
            OBELISK_RT_OK);
  word = second;
  uint8_t dummy = 0;
  ASSERT_EQ(obelisk_rt_v1_argument_ref_store(
                context, &dummy, &dummy, 8, nullptr, storage, 0, 64, 8, 0,
                OBELISK_RT_ARGUMENT_VALUE_BITS, &word, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_release(context, storage, 0),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  for (uint64_t id : {70, 71, 72}) {
    uint64_t captured = 42;
    ASSERT_EQ(obelisk_rt_v1_covergroup_formal_read(
                  context, handle, id, OBELISK_RT_FUNCTIONAL_VALUE_EVENT, 64, 8,
                  nullptr, nullptr, 0, &captured, nullptr),
              OBELISK_RT_OK);
    EXPECT_EQ(captured, id == 70 ? first : id == 71 ? second : UINT64_MAX);
    captured = 42;
    EXPECT_EQ(obelisk_rt_v1_covergroup_formal_read(
                  context, handle, id, OBELISK_RT_FUNCTIONAL_VALUE_CHANDLE, 64,
                  8, nullptr, nullptr, 0, &captured, nullptr),
              OBELISK_RT_INVALID_DESIGN);
    EXPECT_EQ(captured, 42u);
  }
  obelisk_rt_managed_execution_leave(lane, entered);
  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeDesignTest,
       ChandleFormalsCopyOpaqueWordsAndAliasStorage) {
  using namespace obelisk::coverage;
  Database schema = makeFunctionalSchema();
  for (uint64_t id : {70, 71, 72})
    schema.functionalFormals.push_back({id, 10, "chandle" + std::to_string(id),
                                        FunctionalFormalKind::Constructor,
                                        id == 71
                                            ? FunctionalFormalDirection::Ref
                                            : FunctionalFormalDirection::Input,
                                        FunctionalExpressionResultKind::Chandle,
                                        0, 0, CoverageSignedness::NotApplicable,
                                        static_cast<uint32_t>(id - 70), 0, 0});
  schema.schemaFingerprint = computeSchemaFingerprint(schema);
  obelisk_rt_context *context = createContextForSchema(schema);
  ASSERT_NE(context, nullptr);
  obelisk_rt_gc_lane_v1 *lane = nullptr;
  bool entered = false;
  ASSERT_EQ(obelisk_rt_managed_execution_enter(context, &lane, &entered),
            OBELISK_RT_OK);
  // Foreign pointers are opaque: even an unmapped word must never be followed
  // by validation or GC, nor confused with a managed class or numeric value.
  uint64_t word = UINT64_MAX, storage = 0;
  ASSERT_EQ(obelisk_rt_v1_native_state_alloc(
                context, 64, reinterpret_cast<const uint8_t *>(&word), nullptr,
                &storage),
            OBELISK_RT_OK);
  obelisk_rt_functional_value_v1 formals[] = {
      {70, 0, 0, nullptr, nullptr, nullptr, word,
       OBELISK_RT_FUNCTIONAL_VALUE_CHANDLE, 0},
      {71, 64, 8, nullptr, nullptr, nullptr, storage,
       OBELISK_RT_FUNCTIONAL_VALUE_ARGUMENT_REF, 0},
      {72, 0, 0, nullptr, nullptr, nullptr, 0,
       OBELISK_RT_FUNCTIONAL_VALUE_CHANDLE, 0}};
  obelisk_rt_covergroup_v1 handle = 0;
  auto valid = formals[0];
  formals[0].kind = OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL;
  EXPECT_EQ(obelisk_rt_v1_covergroup_create(context, 10, formals, 3, nullptr, 0,
                                            &handle),
            OBELISK_RT_INVALID_DESIGN);
  formals[0] = valid;
  formals[0].bit_width = 64;
  EXPECT_EQ(obelisk_rt_v1_covergroup_create(context, 10, formals, 3, nullptr, 0,
                                            &handle),
            OBELISK_RT_INVALID_DESIGN);
  formals[0] = valid;
  formals[0].value = &word;
  EXPECT_EQ(obelisk_rt_v1_covergroup_create(context, 10, formals, 3, nullptr, 0,
                                            &handle),
            OBELISK_RT_INVALID_DESIGN);
  formals[0] = valid;
  formals[0].owner = reinterpret_cast<obelisk_rt_object_v1 *>(&word);
  EXPECT_EQ(obelisk_rt_v1_covergroup_create(context, 10, formals, 3, nullptr, 0,
                                            &handle),
            OBELISK_RT_INVALID_DESIGN);
  formals[0] = valid;
  ASSERT_EQ(obelisk_rt_v1_covergroup_create(context, 10, formals, 3, nullptr, 0,
                                            &handle),
            OBELISK_RT_OK);
  word = UINT64_C(0x123456789abcdef0);
  uint8_t dummy = 0;
  ASSERT_EQ(obelisk_rt_v1_argument_ref_store(
                context, &dummy, &dummy, 8, nullptr, storage, 0, 64, 8, 0,
                OBELISK_RT_ARGUMENT_VALUE_BITS, &word, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_release(context, storage, 0),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  for (uint64_t id : {70, 71, 72}) {
    uint64_t captured = 42;
    ASSERT_EQ(obelisk_rt_v1_covergroup_formal_read(
                  context, handle, id, OBELISK_RT_FUNCTIONAL_VALUE_CHANDLE, 64,
                  8, nullptr, nullptr, 0, &captured, nullptr),
              OBELISK_RT_OK);
    EXPECT_EQ(captured, id == 70 ? UINT64_MAX : id == 71 ? word : uint64_t{0});
    captured = 42;
    EXPECT_EQ(obelisk_rt_v1_covergroup_formal_read(
                  context, handle, id, OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL, 64,
                  8, nullptr, nullptr, 0, &captured, nullptr),
              OBELISK_RT_INVALID_DESIGN);
    EXPECT_EQ(captured, 42u);
  }
  obelisk_rt_managed_execution_leave(lane, entered);
  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeDesignTest,
       ClassFormalsRetainIdentityLiveStorageAndInterfaces) {
  using namespace obelisk::coverage;
  Database schema = makeFunctionalSchema();
  schema.functionalClassTypes = {{800, 99}, {801, 101}};
  for (uint64_t id : {70, 71, 72, 73})
    schema.functionalFormals.push_back(
        {id, 10, "class" + std::to_string(id),
         FunctionalFormalKind::Constructor,
         id == 71 ? FunctionalFormalDirection::Ref
                  : FunctionalFormalDirection::Input,
         FunctionalExpressionResultKind::ClassHandle, 0, 0,
         CoverageSignedness::NotApplicable, static_cast<uint32_t>(id - 70), 0,
         id == 72 ? uint64_t{801} : uint64_t{800}});
  schema.schemaFingerprint = computeSchemaFingerprint(schema);
  obelisk_rt_context *context = createContextForSchema(schema);
  ASSERT_NE(context, nullptr);
  obelisk_rt_gc_lane_v1 *lane = nullptr;
  bool entered = false;
  ASSERT_EQ(obelisk_rt_managed_execution_enter(context, &lane, &entered),
            OBELISK_RT_OK);
  obelisk_rt_class_descriptor_v1 base{};
  base.version = OBELISK_RT_VERSION;
  base.class_id = 99;
  base.instance_size = 16;
  base.instance_alignment = 8;
  obelisk_rt_interface_descriptor_v1 implemented{101, nullptr, 0};
  base.interfaces = &implemented;
  base.interface_count = 1;
  auto derived = base;
  derived.class_id = 100;
  derived.base = &base;
  // The runtime must recognize an interface inherited from the base even
  // when a foreign descriptor lists only its own direct interfaces.
  derived.interfaces = nullptr;
  derived.interface_count = 0;
  auto viewDescriptor = base;
  viewDescriptor.class_id = 101;
  viewDescriptor.flags = OBELISK_RT_CLASS_INTERFACE | OBELISK_RT_CLASS_ABSTRACT;
  viewDescriptor.interfaces = nullptr;
  viewDescriptor.interface_count = 0;
  auto unrelated = viewDescriptor;
  unrelated.class_id = 102;
  unrelated.flags = 0;
  for (const auto *descriptor : {&base, &derived, &viewDescriptor, &unrelated})
    ASSERT_EQ(obelisk_rt_v1_class_register(context, descriptor), OBELISK_RT_OK);
  obelisk_rt_object_v1 *original = nullptr, *replacement = nullptr,
                       *wrong = nullptr;
  ASSERT_EQ(obelisk_rt_v1_object_allocate(lane, &derived, &original),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_object_allocate(lane, &derived, &replacement),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_object_allocate(lane, &unrelated, &wrong),
            OBELISK_RT_OK);
  uint64_t originalID = obelisk_rt_v1_object_id(original);
  uint64_t replacementID = obelisk_rt_v1_object_id(replacement);
  uint64_t word = obelisk_rt_managed_word_from_object(original), storage = 0;
  const obelisk_rt_managed_root_slot_v1 root{
      0, OBELISK_RT_MANAGED_ROOT_KIND_CLASS, 0};
  ASSERT_EQ(obelisk_rt_v1_native_state_alloc_with_typed_roots(
                context, 64, reinterpret_cast<const uint8_t *>(&word), nullptr,
                &root, 1, &storage),
            OBELISK_RT_OK);
  obelisk_rt_functional_value_v1 formals[] = {
      {70, 0, 0, nullptr, nullptr, original, 0,
       OBELISK_RT_FUNCTIONAL_VALUE_CLASS_HANDLE, 0},
      {71, 64, 8, nullptr, nullptr, nullptr, storage,
       OBELISK_RT_FUNCTIONAL_VALUE_ARGUMENT_REF, 0},
      {72, 0, 0, nullptr, nullptr, original, 0,
       OBELISK_RT_FUNCTIONAL_VALUE_CLASS_HANDLE, 0},
      {73, 0, 0, nullptr, nullptr, nullptr, 0,
       OBELISK_RT_FUNCTIONAL_VALUE_CLASS_HANDLE, 0}};
  obelisk_rt_covergroup_v1 handle = 0;
  formals[0].owner = wrong;
  EXPECT_EQ(obelisk_rt_v1_covergroup_create(context, 10, formals, 4, nullptr, 0,
                                            &handle),
            OBELISK_RT_INVALID_DESIGN);
  formals[0].owner = original;
  ASSERT_EQ(obelisk_rt_v1_covergroup_create(context, 10, formals, 4, nullptr, 0,
                                            &handle),
            OBELISK_RT_OK);
  word = obelisk_rt_managed_word_from_object(replacement);
  uint8_t dummy = 0;
  ASSERT_EQ(obelisk_rt_v1_argument_ref_store(
                context, &dummy, &dummy, 8, nullptr, storage, 0, 64, 8, 0,
                OBELISK_RT_ARGUMENT_VALUE_CLASS, &word, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_release(context, storage, 0),
            OBELISK_RT_OK);
  original = nullptr;
  replacement = nullptr;
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  for (uint64_t id : {70, 71, 72, 73}) {
    uint64_t captured = UINT64_MAX;
    ASSERT_EQ(obelisk_rt_v1_covergroup_formal_read(
                  context, handle, id, OBELISK_RT_FUNCTIONAL_VALUE_CLASS_HANDLE,
                  64, 8, nullptr, nullptr, 0, &captured, nullptr),
              OBELISK_RT_OK);
    EXPECT_EQ(
        obelisk_rt_v1_object_id(obelisk_rt_object_from_managed_word(captured)),
        id == 73   ? uint64_t{0}
        : id == 71 ? replacementID
                   : originalID);
  }
  obelisk_rt_managed_execution_leave(lane, entered);
  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeDesignTest, AggregateFormalsTraceSnapshotsAndLiveStorage) {
  using namespace obelisk::coverage;
  Database schema = makeFunctionalSchema();
  schema.functionalAggregateTypes = {{800, 99, 24, 1, 192, 0}};
  schema.functionalFormals = {
      {70, 10, "snapshot", FunctionalFormalKind::Constructor,
       FunctionalFormalDirection::Input,
       FunctionalExpressionResultKind::Aggregate, 0, 192,
       CoverageSignedness::NotApplicable, 0, 0, 800},
      {71, 10, "live", FunctionalFormalKind::Constructor,
       FunctionalFormalDirection::Ref,
       FunctionalExpressionResultKind::Aggregate, 0, 192,
       CoverageSignedness::NotApplicable, 1, 0, 800}};
  schema.schemaFingerprint = computeSchemaFingerprint(schema);
  obelisk_rt_context *context = createContextForSchema(schema);
  ASSERT_NE(context, nullptr);
  obelisk_rt_gc_lane_v1 *lane = nullptr;
  bool entered = false;
  ASSERT_EQ(obelisk_rt_managed_execution_enter(context, &lane, &entered),
            OBELISK_RT_OK);
  struct Record {
    uint64_t number;
    obelisk_rt_string_v1 label;
    obelisk_rt_managed_word_v1 array;
  } record{3, 0, 0};
  std::array<obelisk_rt_trace_entry_v1, 2> traceEntries{
      {{8, 0, 1, OBELISK_RT_TRACE_STRONG, OBELISK_RT_MANAGED_SLOT_STRING,
        nullptr},
       {16, 0, 1, OBELISK_RT_TRACE_STRONG, OBELISK_RT_MANAGED_SLOT_CONTAINER,
        nullptr}}};
  const obelisk_rt_trace_layout_v1 trace{
      OBELISK_RT_VERSION, 0, 24, 1, traceEntries.data(), traceEntries.size()};
  const obelisk_rt_element_type_v1 element{OBELISK_RT_VERSION,
                                           OBELISK_RT_ELEMENT_AGGREGATE,
                                           99,
                                           0,
                                           0,
                                           24,
                                           1,
                                           192,
                                           &trace};
  const obelisk_rt_element_type_v1 nestedElement{OBELISK_RT_VERSION,
                                                 OBELISK_RT_ELEMENT_BITS,
                                                 100,
                                                 0,
                                                 0,
                                                 4,
                                                 1,
                                                 32,
                                                 nullptr};
  obelisk_rt_gc_managed_root_range_v1 recordRoots{};
  ASSERT_EQ(obelisk_rt_v1_gc_managed_root_range_push(lane, &recordRoots,
                                                     &record.label, 2),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_string_create(lane, "before", 6, &record.label),
            OBELISK_RT_OK);
  obelisk_rt_object_v1 *nested = nullptr, *array = nullptr, *snapshot = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_dynamic_array_create(lane, &nestedElement, 1, &nested),
      OBELISK_RT_OK);
  uint32_t initial = 11;
  ASSERT_EQ(obelisk_rt_v1_container_write(lane, nested, 0, &initial, nullptr),
            OBELISK_RT_OK);
  record.array = obelisk_rt_managed_word_from_object(nested);
  ASSERT_EQ(obelisk_rt_v1_dynamic_array_create(lane, &element, 1, &array),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_container_write(lane, array, 0, &record, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_reference_path_index_create(
                lane, array, 0, nullptr, 0, 0, nullptr, nullptr, 0, &snapshot),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 snapshotRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &snapshotRoot, &snapshot),
            OBELISK_RT_OK);
  const obelisk_rt_managed_root_slot_v1 roots[] = {
      {64, OBELISK_RT_MANAGED_ROOT_KIND_STRING, 0},
      {128, OBELISK_RT_MANAGED_ROOT_KIND_CONTAINER, 0}};
  uint64_t storage = 0;
  ASSERT_EQ(obelisk_rt_v1_native_state_alloc_with_typed_roots(
                context, 192, reinterpret_cast<const uint8_t *>(&record),
                nullptr, roots, 2, &storage),
            OBELISK_RT_OK);
  obelisk_rt_functional_value_v1 formals[] = {
      {70, 0, 0, nullptr, nullptr, snapshot, 0,
       OBELISK_RT_FUNCTIONAL_VALUE_MANAGED_AGGREGATE, 0},
      {71, 192, 24, nullptr, nullptr, nullptr, storage,
       OBELISK_RT_FUNCTIONAL_VALUE_ARGUMENT_REF, 0}};
  obelisk_rt_covergroup_v1 handle = 0;
  ASSERT_EQ(obelisk_rt_v1_covergroup_create(context, 10, formals, 2, nullptr, 0,
                                            &handle),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_root_pop(lane, &snapshotRoot), OBELISK_RT_OK);
  record.number = 7;
  ASSERT_EQ(obelisk_rt_v1_string_create(lane, "after", 5, &record.label),
            OBELISK_RT_OK);
  ASSERT_EQ(
      obelisk_rt_v1_dynamic_array_create(lane, &nestedElement, 1, &nested),
      OBELISK_RT_OK);
  initial = 22;
  ASSERT_EQ(obelisk_rt_v1_container_write(lane, nested, 0, &initial, nullptr),
            OBELISK_RT_OK);
  record.array = obelisk_rt_managed_word_from_object(nested);
  uint8_t dummy = 0;
  ASSERT_EQ(obelisk_rt_v1_argument_ref_store(
                context, &dummy, &dummy, 8, nullptr, storage, 0, 192, 24, 0,
                OBELISK_RT_ARGUMENT_VALUE_BITS, &record, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_native_state_release(context, storage, 0),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_managed_root_range_pop(lane, &recordRoots),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  for (auto [id, expected] : {std::pair{uint64_t{70}, uint64_t{3}},
                              std::pair{uint64_t{71}, uint64_t{7}}}) {
    std::array<uint64_t, 3> reference{};
    ASSERT_EQ(obelisk_rt_v1_covergroup_formal_read(
                  context, handle, id, OBELISK_RT_FUNCTIONAL_VALUE_ARGUMENT_REF,
                  192, 24, nullptr, nullptr, 0, reference.data(), nullptr),
              OBELISK_RT_OK);
    Record actual{};
    ASSERT_EQ(obelisk_rt_v1_argument_ref_load(
                  context, &dummy, &dummy, 8,
                  obelisk_rt_object_from_managed_word(reference[0]),
                  reference[1], reference[2], 192, 24, 0,
                  OBELISK_RT_ARGUMENT_VALUE_BITS, &actual, nullptr),
              OBELISK_RT_OK);
    EXPECT_EQ(actual.number, expected);
    EXPECT_EQ(obelisk_rt_validate_string(context, actual.label), OBELISK_RT_OK);
    uint32_t value = 0;
    ASSERT_EQ(obelisk_rt_v1_container_read(
                  obelisk_rt_object_from_managed_word(actual.array), 0, &value,
                  nullptr),
              OBELISK_RT_OK);
    EXPECT_EQ(value, id == 70 ? 11u : 22u);
  }
  obelisk_rt_managed_execution_leave(lane, entered);
  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeDesignTest,
     ConflictingTypeProfileDoesNotPublishConfiguration) {
  using namespace obelisk::coverage;
  Database schema = makeFunctionalSchema();
  FunctionalExpression comment;
  comment.id = 62;
  comment.owner = 10;
  comment.ownerKind = FunctionalExpressionOwnerKind::Type;
  comment.role = FunctionalExpressionRole::OptionRHS;
  comment.resultKind = FunctionalExpressionResultKind::String;
  comment.signedness = CoverageSignedness::NotApplicable;
  comment.ownerOrdinal =
      static_cast<uint32_t>(FunctionalConfigurationOptionKind::Comment);
  comment.ownerSubordinal =
      static_cast<uint32_t>(FunctionalOptionScopeKind::Type);
  comment.evaluationPhase = FunctionalExpressionEvaluationPhase::Option;
  schema.functionalExpressions.push_back(comment);
  schema.functionalOptionPlans.push_back(
      {10, 62, FunctionalConfigurationOptionOwnerKind::Group,
       FunctionalOptionScopeKind::Type,
       FunctionalConfigurationOptionKind::Comment,
       static_cast<uint32_t>(FunctionalConfigurationOptionKind::Comment), 0});
  schema.schemaFingerprint = computeSchemaFingerprint(schema);
  obelisk_rt_context *context = createContextForSchema(schema, false);
  ASSERT_NE(context, nullptr);
  ASSERT_EQ(finalizeCoverage(context, 0, 0, nullptr, nullptr), OBELISK_RT_OK);
  auto construct = [&](const char *text, obelisk_rt_covergroup_v1 &handle) {
    return withManagedString(context, text, [&](auto value) {
      const obelisk_rt_functional_value_v1 expression{
          62,      0,       0,     nullptr,
          nullptr, nullptr, value, OBELISK_RT_FUNCTIONAL_VALUE_STRING,
          0};
      return obelisk_rt_v1_covergroup_create(context, 10, nullptr, 0,
                                             &expression, 1, &handle);
    });
  };
  obelisk_rt_covergroup_v1 first = 0, rejected = 0, reused = 0;
  ASSERT_EQ(construct("same static comment", first), OBELISK_RT_OK);
  EXPECT_EQ(construct("conflicting static comment", rejected),
            OBELISK_RT_INVALID_DESIGN);
  EXPECT_EQ(rejected, 0u);
  ASSERT_EQ(construct("same static comment", reused), OBELISK_RT_OK);
  EXPECT_NE(first, reused);
  Database saved = saveCoverage(context, "type-profile-transaction");
  ASSERT_EQ(saved.functionalConfigurations.size(), 1u);
  EXPECT_EQ(saved.resolvedInstances.size(), 2u);
  ASSERT_EQ(saved.functionalConfigurationOptions.size(), 2u);
  EXPECT_EQ(saved.functionalConfigurationOptions.back().stringValue,
            "same static comment");
  EXPECT_EQ(saved.functionalConfigurationOptions.front().option,
            FunctionalConfigurationOptionKind::DistributeFirst);
  EXPECT_EQ(saved.functionalConfigurationOptions.front().value, 0u);
  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeDesignTest, InvalidUtf8CommentDoesNotPublishResolvedState) {
  using namespace obelisk::coverage;
  Database schema = makeFunctionalSchema();
  FunctionalExpression comment;
  comment.id = 62;
  comment.owner = 10;
  comment.ownerKind = FunctionalExpressionOwnerKind::Type;
  comment.role = FunctionalExpressionRole::OptionRHS;
  comment.resultKind = FunctionalExpressionResultKind::String;
  comment.signedness = CoverageSignedness::NotApplicable;
  comment.ownerOrdinal =
      static_cast<uint32_t>(FunctionalConfigurationOptionKind::Comment);
  comment.ownerSubordinal =
      static_cast<uint32_t>(FunctionalOptionScopeKind::Instance);
  comment.evaluationPhase = FunctionalExpressionEvaluationPhase::Option;
  schema.functionalExpressions.push_back(comment);
  schema.functionalOptionPlans.push_back(
      {10, 62, FunctionalConfigurationOptionOwnerKind::Group,
       FunctionalOptionScopeKind::Instance,
       FunctionalConfigurationOptionKind::Comment,
       static_cast<uint32_t>(FunctionalConfigurationOptionKind::Comment), 0});

  obelisk_rt_context *context = createContextForSchema(schema, false);
  ASSERT_NE(context, nullptr);
  ASSERT_EQ(finalizeCoverage(context, 0, 0, nullptr, nullptr),
            OBELISK_RT_OK);

  const std::string invalid("\xc3\x28", 2);
  obelisk_rt_covergroup_v1 rejected = 0;
  ASSERT_EQ(withManagedString(context, invalid,
                              [&](auto value) {
                                const obelisk_rt_functional_value_v1 expression{
                                    62,      0,
                                    0,       nullptr,
                                    nullptr, nullptr,
                                    value,   OBELISK_RT_FUNCTIONAL_VALUE_STRING,
                                    0};
                                return obelisk_rt_v1_covergroup_create(
                                    context, 10, nullptr, 0, &expression, 1,
                                    &rejected);
                              }),
            OBELISK_RT_INVALID_DESIGN);
  EXPECT_EQ(rejected, 0u);
  Database afterFailure = saveCoverage(context, "invalid-comment");
  EXPECT_TRUE(afterFailure.functionalConfigurations.empty());
  EXPECT_TRUE(afterFailure.functionalConfigurationOptions.empty());
  EXPECT_TRUE(afterFailure.resolvedInstances.empty());

  obelisk_rt_covergroup_v1 accepted = 0;
  ASSERT_EQ(withManagedString(context, "valid comment",
                              [&](auto value) {
                                const obelisk_rt_functional_value_v1 expression{
                                    62,      0,
                                    0,       nullptr,
                                    nullptr, nullptr,
                                    value,   OBELISK_RT_FUNCTIONAL_VALUE_STRING,
                                    0};
                                return obelisk_rt_v1_covergroup_create(
                                    context, 10, nullptr, 0, &expression, 1,
                                    &accepted);
                              }),
            OBELISK_RT_OK);
  EXPECT_NE(accepted, 0u);
  Database afterSuccess = saveCoverage(context, "valid-comment");
  ASSERT_EQ(afterSuccess.functionalConfigurations.size(), 1u);
  ASSERT_EQ(afterSuccess.functionalConfigurationOptions.size(), 2u);
  auto storedComment = std::find_if(
      afterSuccess.functionalConfigurationOptions.begin(),
      afterSuccess.functionalConfigurationOptions.end(), [](const auto &row) {
        return row.option == FunctionalConfigurationOptionKind::Comment;
      });
  ASSERT_NE(storedComment, afterSuccess.functionalConfigurationOptions.end());
  EXPECT_EQ(storedComment->stringValue, "valid comment");
  ASSERT_EQ(afterSuccess.resolvedInstances.size(), 1u);
  EXPECT_EQ(afterSuccess.resolvedInstances[0].id, accepted);
  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeDesignTest,
     TypeQueriesBeforeConstructionDoNotResolveConstructorExpressions) {
  using namespace obelisk::coverage;
  Database schema = makeFunctionalSchema();
  FunctionalExpression comment;
  comment.id = 62;
  comment.owner = 10;
  comment.ownerKind = FunctionalExpressionOwnerKind::Type;
  comment.role = FunctionalExpressionRole::OptionRHS;
  comment.resultKind = FunctionalExpressionResultKind::String;
  comment.signedness = CoverageSignedness::NotApplicable;
  comment.ownerOrdinal =
      static_cast<uint32_t>(FunctionalConfigurationOptionKind::Comment);
  comment.ownerSubordinal =
      static_cast<uint32_t>(FunctionalOptionScopeKind::Instance);
  comment.evaluationPhase = FunctionalExpressionEvaluationPhase::Option;
  schema.functionalExpressions.push_back(comment);
  schema.functionalOptionPlans.push_back(
      {10, 62, FunctionalConfigurationOptionOwnerKind::Group,
       FunctionalOptionScopeKind::Instance,
       FunctionalConfigurationOptionKind::Comment,
       static_cast<uint32_t>(FunctionalConfigurationOptionKind::Comment), 0});

  obelisk_rt_context *context = createContextForSchema(schema, false);
  ASSERT_NE(context, nullptr);
  ASSERT_EQ(finalizeCoverage(context, 0, 0, nullptr, nullptr),
            OBELISK_RT_OK);

  double percentage = -1.0;
  int32_t covered = -1;
  int32_t total = -1;
  EXPECT_EQ(obelisk_rt_v1_covergroup_type_query(
                context, 10, 0, &percentage, &covered, &total),
            OBELISK_RT_OK);
  EXPECT_DOUBLE_EQ(percentage, 0.0);
  EXPECT_EQ(covered, 0);
  EXPECT_EQ(total, 0);

  percentage = -1.0;
  covered = total = -1;
  EXPECT_EQ(obelisk_rt_v1_covergroup_type_query(
                context, 10, 20, &percentage, &covered, &total),
            OBELISK_RT_OK);
  EXPECT_DOUBLE_EQ(percentage, 0.0);
  EXPECT_EQ(covered, 0);
  EXPECT_EQ(total, 0);

  // Static option writes are meaningful before any constructor bundle exists.
  // Zero-denominator queries honor only the queried owner's type weight;
  // they still must not attempt to evaluate the unresolved comment expression.
  for (uint64_t owner : {uint64_t{0}, uint64_t{20}}) {
    ASSERT_EQ(obelisk_rt_v1_covergroup_set_type_integer_option(
                  context, 10, owner, OBELISK_RT_COVERGROUP_OPTION_WEIGHT, 0),
              OBELISK_RT_OK);
    covered = total = -1;
    ASSERT_EQ(obelisk_rt_v1_covergroup_type_query(
                  context, 10, owner, &percentage, &covered, &total),
              OBELISK_RT_OK);
    EXPECT_DOUBLE_EQ(percentage, 100.0);
    EXPECT_EQ(covered, 0);
    EXPECT_EQ(total, 0);
    ASSERT_EQ(obelisk_rt_v1_covergroup_set_type_integer_option(
                  context, 10, owner, OBELISK_RT_COVERGROUP_OPTION_WEIGHT, 2),
              OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_covergroup_type_query(
                  context, 10, owner, &percentage, &covered, &total),
              OBELISK_RT_OK);
    EXPECT_DOUBLE_EQ(percentage, 0.0);
    EXPECT_EQ(covered, 0);
    EXPECT_EQ(total, 0);
  }
  ASSERT_EQ(obelisk_rt_v1_functional_coverage_get(context, &percentage),
            OBELISK_RT_OK);
  EXPECT_DOUBLE_EQ(percentage, 100.0);
  EXPECT_EQ(obelisk_rt_v1_covergroup_set_type_integer_option(
                context, 11, 0, OBELISK_RT_COVERGROUP_OPTION_WEIGHT, 0),
            OBELISK_RT_INVALID_DESIGN);
  EXPECT_EQ(obelisk_rt_v1_covergroup_set_type_integer_option(
                context, 10, 22, OBELISK_RT_COVERGROUP_OPTION_WEIGHT, 0),
            OBELISK_RT_INVALID_DESIGN);

  EXPECT_EQ(obelisk_rt_v1_covergroup_type_query(
                context, 11, 0, &percentage, &covered, &total),
            OBELISK_RT_INVALID_DESIGN);
  EXPECT_EQ(obelisk_rt_v1_covergroup_type_query(
                context, 10, 22, &percentage, &covered, &total),
            OBELISK_RT_INVALID_DESIGN);
  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeDesignTest, DistributionUpdatesReusePayloadlessConfigs) {
  using namespace obelisk::coverage;
  Database schema = makeFunctionalSchema();
  obelisk_rt_context *context = createContextForSchema(schema);
  ASSERT_NE(context, nullptr);
  ASSERT_EQ(finalizeCoverage(context, 0, 0, nullptr, nullptr), OBELISK_RT_OK);
  std::array<obelisk_rt_covergroup_v1, 4> handles{};
  ASSERT_EQ(createFunctional(context, 10, &handles[0]), OBELISK_RT_OK);
  ASSERT_EQ(sampleFunctional(context, handles[0], 0, 2), OBELISK_RT_OK);
  for (size_t ordinal = 1; ordinal != handles.size(); ++ordinal) {
    ASSERT_EQ(obelisk_rt_v1_covergroup_set_type_integer_option(
                  context, 10, 0, OBELISK_RT_COVERGROUP_OPTION_DISTRIBUTE_FIRST,
                  ordinal % 2),
              OBELISK_RT_OK);
    ASSERT_EQ(createFunctional(context, 10, &handles[ordinal]), OBELISK_RT_OK);
  }
  ASSERT_EQ(
      obelisk_rt_v1_covergroup_set_type_integer_option(
          context, 10, 0, OBELISK_RT_COVERGROUP_OPTION_DISTRIBUTE_FIRST, 0),
      OBELISK_RT_OK);
  for (size_t ordinal = 0; ordinal != handles.size(); ++ordinal) {
    double percentage = -1;
    int32_t covered = -1, total = -1;
    ASSERT_EQ(obelisk_rt_v1_covergroup_instance_query(
                  context, handles[ordinal], 0, &percentage, &covered, &total),
              OBELISK_RT_OK);
    EXPECT_EQ(covered, ordinal == 0 ? 2 : 0);
    EXPECT_EQ(total, 3);
  }
  EXPECT_EQ(
      obelisk_rt_v1_covergroup_set_type_integer_option(
          context, 10, 0, OBELISK_RT_COVERGROUP_OPTION_DISTRIBUTE_FIRST, 2),
      OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(
      obelisk_rt_v1_covergroup_set_type_integer_option(
          context, 10, 20, OBELISK_RT_COVERGROUP_OPTION_DISTRIBUTE_FIRST, 1),
      OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(
      obelisk_rt_v1_covergroup_set_type_integer_option(
          context, 11, 0, OBELISK_RT_COVERGROUP_OPTION_DISTRIBUTE_FIRST, 1),
      OBELISK_RT_INVALID_DESIGN);
  Database saved = saveCoverage(context, "distribution-cache");
  ASSERT_EQ(saved.functionalConfigurations.size(), 2u);
  ASSERT_EQ(saved.resolvedInstances.size(), handles.size());
  EXPECT_EQ(saved.resolvedInstances[0].configuration,
            saved.resolvedInstances[2].configuration);
  EXPECT_EQ(saved.resolvedInstances[1].configuration,
            saved.resolvedInstances[3].configuration);
  EXPECT_NE(saved.resolvedInstances[0].configuration,
            saved.resolvedInstances[1].configuration);
  ASSERT_EQ(saved.functionalConfigurationOptions.size(), 2u);
  EXPECT_NE(saved.functionalConfigurationOptions[0].value,
            saved.functionalConfigurationOptions[1].value);
  auto current = std::find_if(
      saved.resolvedInstanceOptions.begin(),
      saved.resolvedInstanceOptions.end(), [](const auto &row) {
        return !row.instance &&
               row.option == FunctionalConfigurationOptionKind::DistributeFirst;
      });
  ASSERT_NE(current, saved.resolvedInstanceOptions.end());
  EXPECT_EQ(current->value, 0u);
  // Load both policies while live configurations and constructor-cache entries
  // exist. A later constructor must still select its current policy by key.
  obelisk_rt_context *loaded = createContextForSchema(schema);
  ASSERT_NE(loaded, nullptr);
  ASSERT_EQ(finalizeCoverage(loaded, 0, 0, nullptr, nullptr), OBELISK_RT_OK);
  obelisk_rt_covergroup_v1 first = 0, second = 0, third = 0;
  ASSERT_EQ(createFunctional(loaded, 10, &first), OBELISK_RT_OK);
  ASSERT_EQ(
      obelisk_rt_v1_covergroup_set_type_integer_option(
          loaded, 10, 0, OBELISK_RT_COVERGROUP_OPTION_DISTRIBUTE_FIRST, 1),
      OBELISK_RT_OK);
  ASSERT_EQ(createFunctional(loaded, 10, &second), OBELISK_RT_OK);
  const auto path =
      std::filesystem::temp_directory_path() /
      ("obelisk-distribution-load-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()) +
       ".obcov");
  const std::string pathText = path.string();
  ASSERT_EQ(writeFileAtomically(pathText, saved), Status::Ok);
  ASSERT_EQ(
      obelisk_rt_v1_coverage_load(loaded, pathText.data(), pathText.size()),
      OBELISK_RT_OK);
  ASSERT_EQ(
      obelisk_rt_v1_covergroup_set_type_integer_option(
          loaded, 10, 0, OBELISK_RT_COVERGROUP_OPTION_DISTRIBUTE_FIRST, 0),
      OBELISK_RT_OK);
  ASSERT_EQ(createFunctional(loaded, 10, &third), OBELISK_RT_OK);
  Database reloaded = saveCoverage(loaded, "distribution-reloaded");
  EXPECT_EQ(reloaded.functionalConfigurations.size(), 2u);
  EXPECT_EQ(reloaded.resolvedInstances.size(), handles.size() + 3);
  obelisk_rt_v1_context_destroy(loaded);
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeDesignTest, EmptyGroupsKeepIdentitiesAndValidateReferences) {
  using namespace obelisk::coverage;
  Database schema;
  schema.scopes.push_back({1, 0, "top", 0, "top"});
  schema.functionalTypes.push_back({10, 1, "empty", 0, 2023, "top.empty"});
  obelisk_rt_context *context = createContextForSchema(schema);
  ASSERT_NE(context, nullptr);
  ASSERT_EQ(finalizeCoverage(context, 0, 0, nullptr, nullptr), OBELISK_RT_OK);
  obelisk_rt_covergroup_v1 first = 0, second = 0, invalid = 0;
  ASSERT_EQ(createFunctional(context, 10, &first), OBELISK_RT_OK);
  ASSERT_EQ(createFunctional(context, 10, &second), OBELISK_RT_OK);
  EXPECT_NE(first, 0u);
  EXPECT_NE(first, second);
  EXPECT_EQ(createFunctional(context, 11, &invalid), OBELISK_RT_INVALID_DESIGN);
  EXPECT_EQ(invalid, 0u);
  ASSERT_EQ(obelisk_rt_v1_covergroup_sample(context, first, nullptr, 0),
            OBELISK_RT_OK);
  double percentage = -1.0;
  int32_t covered = -1, total = -1;
  ASSERT_EQ(obelisk_rt_v1_covergroup_instance_query(
                context, first, 0, &percentage, &covered, &total),
            OBELISK_RT_OK);
  EXPECT_DOUBLE_EQ(percentage, 0.0);
  EXPECT_EQ(covered, 0);
  EXPECT_EQ(total, 0);
  EXPECT_EQ(obelisk_rt_v1_covergroup_instance_query(
                context, first, 20, &percentage, &covered, &total),
            OBELISK_RT_INVALID_DESIGN);
  EXPECT_EQ(obelisk_rt_v1_covergroup_type_query(
                context, 11, 0, &percentage, &covered, &total),
            OBELISK_RT_INVALID_DESIGN);
  ASSERT_EQ(obelisk_rt_v1_covergroup_set_integer_option(
                context, first, 0, OBELISK_RT_COVERGROUP_OPTION_WEIGHT, 0),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_covergroup_instance_query(
                context, first, 0, &percentage, &covered, &total),
            OBELISK_RT_OK);
  EXPECT_DOUBLE_EQ(percentage, 100.0);
  ASSERT_EQ(obelisk_rt_v1_covergroup_set_type_integer_option(
                context, 10, 0, OBELISK_RT_COVERGROUP_OPTION_WEIGHT, 0),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_covergroup_type_query(
                context, 10, 0, &percentage, &covered, &total),
            OBELISK_RT_OK);
  EXPECT_DOUBLE_EQ(percentage, 100.0);
  EXPECT_EQ(covered, 0);
  EXPECT_EQ(total, 0);
  ASSERT_EQ(obelisk_rt_v1_functional_coverage_get(context, &percentage),
            OBELISK_RT_OK);
  EXPECT_DOUBLE_EQ(percentage, 100.0);
  Database saved = saveCoverage(context, "empty-groups");
  ASSERT_EQ(saved.runs.size(), 1u);
  EXPECT_TRUE(saved.runs.front().flags & RunContainsFunctional);
  EXPECT_EQ(saved.resolvedInstances.size(), 2u);
  ASSERT_EQ(saved.functionalConfigurations.size(), 1u);
  EXPECT_EQ(saved.functionalConfigurations.front().type, 10u);
  EXPECT_TRUE(saved.resolvedFunctionalItems.empty());
  EXPECT_TRUE(saved.resolvedFunctionalBins.empty());
  EXPECT_TRUE(saved.counters.empty());
  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeDesignTest,
     LoadedQueryIndexesRefreshWithoutReplacingLiveProfiles) {
  using namespace obelisk::coverage;
  Database schema = makeFunctionalSchema();
  obelisk_rt_context *producer = createContextForSchema(schema);
  obelisk_rt_context *consumer = createContextForSchema(schema);
  ASSERT_NE(producer, nullptr);
  ASSERT_NE(consumer, nullptr);
  ASSERT_EQ(finalizeCoverage(producer, 0, 0, nullptr, nullptr), OBELISK_RT_OK);
  ASSERT_EQ(finalizeCoverage(consumer, 0, 0, nullptr, nullptr), OBELISK_RT_OK);
  obelisk_rt_covergroup_v1 named = 0, generated = 0, live = 0;
  ASSERT_EQ(createFunctional(producer, 10, &named), OBELISK_RT_OK);
  ASSERT_EQ(createFunctional(producer, 10, &generated), OBELISK_RT_OK);
  ASSERT_EQ(createFunctional(consumer, 10, &live), OBELISK_RT_OK);
  ASSERT_EQ(withManagedString(producer, "same",
                              [&](auto name) {
                                return obelisk_rt_v1_covergroup_set_name(
                                    producer, named, name);
                              }),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_covergroup_set_integer_option(
                producer, named, 20, OBELISK_RT_COVERGROUP_OPTION_GOAL, 50),
            OBELISK_RT_OK);
  ASSERT_EQ(sampleFunctional(producer, named, 0, 2), OBELISK_RT_OK);
  ASSERT_EQ(sampleFunctional(producer, generated, 0, 2), OBELISK_RT_OK);
  Database first = saveCoverage(producer, "loaded-query-first");
  Database second = first;
  second.runs.front().uuid.back() ^= 1;
  second.runs.front().name = "second";
  for (auto &instance : second.resolvedInstances)
    instance.run = second.runs.front().uuid;
  for (auto &option : second.resolvedInstanceOptions)
    option.run = second.runs.front().uuid;
  for (auto &counter : second.counters) {
    counter.run = second.runs.front().uuid;
    const auto bin =
        std::find_if(second.resolvedFunctionalBins.begin(),
                     second.resolvedFunctionalBins.end(),
                     [&](const auto &row) { return row.id == counter.entity; });
    ASSERT_NE(bin, second.resolvedFunctionalBins.end());
    if (bin->templateBin == 30)
      counter.value = 0;
    else if (bin->templateBin == 31)
      counter.value = 1;
  }
  auto query = [&](uint64_t item, double expected) {
    double percentage = -1.0;
    int32_t covered = -1, total = -1;
    ASSERT_EQ(obelisk_rt_v1_covergroup_type_query(
                  consumer, 10, item, &percentage, &covered, &total),
              OBELISK_RT_OK);
    EXPECT_NEAR(percentage, expected, 1.0e-9);
  };
  query(0, 0.0); // Populate an empty loaded-data cache before the first load.
  std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("obelisk-loaded-query-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()) +
       ".obcov");
  const std::string pathText = path.string();
  ASSERT_EQ(writeFileAtomically(pathText, first), Status::Ok);
  ASSERT_EQ(
      obelisk_rt_v1_coverage_load(consumer, pathText.data(), pathText.size()),
      OBELISK_RT_OK);
  query(0, 175.0 / 3.0);
  ASSERT_EQ(writeFileAtomically(pathText, second), Status::Ok);
  ASSERT_EQ(
      obelisk_rt_v1_coverage_load(consumer, pathText.data(), pathText.size()),
      OBELISK_RT_OK);
  // The explicit name merges across the two runs; generated names retain two
  // identities. The live instance is still empty and independent.
  query(0, 62.5);
  query(20, 50.0);
  query(21, 75.0);
  ASSERT_EQ(sampleFunctional(consumer, live, 0, 2), OBELISK_RT_OK);
  query(0, 81.25);
  ASSERT_EQ(obelisk_rt_v1_covergroup_set_type_integer_option(
                consumer, 10, 0, OBELISK_RT_COVERGROUP_OPTION_GOAL, 50),
            OBELISK_RT_OK);
  // With merge_instances off, type coverage depends on instance profiles,
  // including their goals, rather than applying a second type goal (19.11.3).
  query(0, 81.25);
  query(20, 62.5); // Cached instance profiles retain their own item goals.
  ASSERT_EQ(obelisk_rt_v1_covergroup_set_type_integer_option(
                consumer, 10, 0, OBELISK_RT_COVERGROUP_OPTION_MERGE_INSTANCES, 1),
            OBELISK_RT_OK);
  query(0, 100.0);
  query(20, 100.0);
  obelisk_rt_v1_context_destroy(consumer);
  obelisk_rt_v1_context_destroy(producer);
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

TEST_F(CoverageRuntimeDesignTest,
       SparseCrossUpdatesPreserveHistoryIsolationAndSaturatingCounts) {
  using namespace obelisk::coverage;
  Database schema = makeFunctionalSchema();
  schema.functionalBins.clear();
  schema.functionalBinPlans.clear();
  schema.functionalValueSets.clear();
  schema.functionalValueAtoms.clear();
  schema.functionalValueLimbs.clear();
  for (auto &expression : schema.functionalExpressions)
    expression.bitWidth = 3;
  for (uint32_t ordinal = 0; ordinal != 2; ++ordinal) {
    uint64_t item = 22 + ordinal;
    std::string name = "product" + std::to_string(ordinal);
    schema.functionalItems.push_back({item, 10, name, FunctionalItemKind::Cross,
                                      0, 100, 1, ordinal + 2,
                                      "top.cg." + name});
    schema.crossPlans.push_back({item, ordinal * 2, 2, 0, 0,
                                 CrossRetainAutoPolicy::Retain, 0, 62 + ordinal,
                                 220 + 10 * ordinal, 6, 0});
    schema.crossTargets.push_back({item, 20, 0, 0, 3,
                                   FunctionalExpressionResultKind::Integral,
                                   CoverageSignedness::Unsigned, 0});
    schema.crossTargets.push_back({item, 21, 1, 3, 3,
                                   FunctionalExpressionResultKind::Integral,
                                   CoverageSignedness::Unsigned, 0});
    auto iff = schema.functionalExpressions.front();
    iff.id = 62 + ordinal;
    iff.owner = item;
    iff.role = FunctionalExpressionRole::CrossIff;
    iff.resultKind = FunctionalExpressionResultKind::Boolean;
    iff.bitWidth = 0;
    iff.signedness = CoverageSignedness::NotApplicable;
    iff.resultOrdinal = ordinal + 2;
    schema.functionalExpressions.push_back(iff);
  }
  schema.schemaFingerprint = computeSchemaFingerprint(schema);
  obelisk_rt_context *context = createContextForSchema(schema);
  ASSERT_NE(context, nullptr);
  ASSERT_EQ(finalizeCoverage(context, 0, 0, nullptr, nullptr), OBELISK_RT_OK);
  obelisk_rt_covergroup_v1 handle = 0, other = 0;
  ASSERT_EQ(createFunctional(context, 10, &handle), OBELISK_RT_OK);
  ASSERT_EQ(createFunctional(context, 10, &other), OBELISK_RT_OK);
  uint8_t first = 0, second = 0, firstGuard = 1, secondGuard = 1;
  std::array<obelisk_rt_functional_value_v1, 4> batch{
      obelisk_rt_functional_value_v1{60, 3, 1, &first, nullptr, nullptr, 0,
                                     OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL, 0},
      {61, 3, 1, &second, nullptr, nullptr, 0,
       OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL, 0},
      {62, 1, 1, &firstGuard, nullptr, nullptr, 0,
       OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL, 0},
      {63, 1, 1, &secondGuard, nullptr, nullptr, 0,
       OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL, 0}};
  auto sample = [&](uint64_t selected) {
    return obelisk_rt_v1_covergroup_sample(context, selected, batch.data(),
                                           batch.size());
  };
  ASSERT_EQ(sample(handle), OBELISK_RT_OK);
  auto &instance = context->coverage->instances.at(handle);
  const auto &type = context->coverage->types.at({10, instance.configuration});
  auto product =
      std::find_if(type.crosses.begin(), type.crosses.end(),
                   [](const auto &cross) { return cross.templateItem == 22; });
  ASSERT_NE(product, type.crosses.end());
  auto &tuples = instance.crossTuples.at(product->id);
  ASSERT_EQ(tuples.size(), 1u);
  const auto originalTuple = tuples.begin()->first;
  auto *originalCounter = &tuples.begin()->second;
  originalCounter->count = UINT64_MAX - 1;
  for (unsigned pass = 0; pass != 2; ++pass)
    for (first = 0; first != 8; ++first)
      for (second = 0; second != 8; ++second) {
        secondGuard = pass || first < 4;
        ASSERT_EQ(sample(handle), OBELISK_RT_OK);
        if (!pass && !first && !second) {
          EXPECT_EQ(originalCounter->count, UINT64_MAX);
          EXPECT_FALSE(originalCounter->overflow);
        }
      }
  ASSERT_EQ(tuples.size(), 64u);
  EXPECT_EQ(&tuples.at(originalTuple), originalCounter);
  EXPECT_EQ(originalCounter->count, UINT64_MAX);
  EXPECT_TRUE(originalCounter->overflow);
  for (const auto &[components, counter] : tuples)
    if (components != originalTuple) {
      EXPECT_EQ(counter.count, 2u);
    }
  EXPECT_TRUE(context->coverage->instances.at(other).crossTuples.empty());
  std::vector<uint64_t> binCounts;
  for (const auto &bin : instance.bins)
    binCounts.push_back(bin.count);
  first = second = 0;
  secondGuard = 1;
  batch.back().kind = UINT32_MAX;
  EXPECT_EQ(sample(handle), OBELISK_RT_INVALID_DESIGN);
  batch.back().kind = OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL;
  for (size_t index = 0; index != instance.bins.size(); ++index)
    EXPECT_EQ(instance.bins[index].count, binCounts[index]);
  EXPECT_EQ(tuples.size(), 64u);
  ASSERT_EQ(sample(other), OBELISK_RT_OK);
  for (const auto &[cross, counters] :
       context->coverage->instances.at(other).crossTuples) {
    ASSERT_EQ(counters.size(), 1u);
    EXPECT_EQ(counters.begin()->second.count, 1u);
  }
  double percentage = 0;
  int32_t covered = 0, total = 0;
  ASSERT_EQ(obelisk_rt_v1_covergroup_instance_query(
                context, handle, 22, &percentage, &covered, &total),
            OBELISK_RT_OK);
  EXPECT_DOUBLE_EQ(percentage, 100.0);
  EXPECT_EQ(covered, 64);
  EXPECT_EQ(total, 64);
  Database snapshot = saveCoverage(context, "sparse-incremental");
  ASSERT_EQ(snapshot.sparseCrossTuples.size(), 130u);
  uint32_t saturated = 0;
  for (const auto &tuple : snapshot.sparseCrossTuples) {
    EXPECT_EQ(tuple.componentCount, 2u);
    if (tuple.flags & SparseCrossTupleOverflow) {
      ++saturated;
      EXPECT_EQ(tuple.hits, UINT64_MAX);
    } else
      EXPECT_GE(tuple.hits, 1u);
  }
  EXPECT_EQ(saturated, 1u);
  obelisk_rt_v1_context_destroy(context);
}

// A wide cross with only four observed tuples exercises target order without
// expanding a large sample space. The fully overlapping variant verifies that
// an empty final target is detected before considering its enormous product.
obelisk::coverage::Database makeWideCrossSchema(bool fullyOverlapping) {
  using namespace obelisk::coverage;
  Database schema;
  schema.scopes.push_back({1, 0, "top", 0, "top"});
  schema.functionalTypes.push_back({10, 1, "cg", 0, 2023, "top.cg"});
  constexpr uint32_t arity = 64;
  constexpr uint64_t cross = 2000;
  constexpr uint32_t inclusive =
      FunctionalValueAtomLowerInclusive | FunctionalValueAtomUpperInclusive;
  for (uint32_t ordinal = 0; ordinal != arity; ++ordinal) {
    uint64_t point = 1000 + ordinal;
    std::string name = "cp" + std::to_string(ordinal);
    schema.functionalItems.push_back({point, 10, name,
                                      FunctionalItemKind::Coverpoint, 0, 100, 1,
                                      ordinal, "top.cg." + name});
    uint32_t binCount = fullyOverlapping ? 4 : (ordinal < 2 ? 2 : 1);
    for (uint32_t binOrdinal = 0; binOrdinal != binCount; ++binOrdinal) {
      uint64_t bin = 5000 + ordinal * 4 + binOrdinal;
      uint64_t set = 7000 + ordinal * 4 + binOrdinal;
      uint32_t atom = schema.functionalValueAtoms.size();
      schema.functionalValueSets.push_back({set, point, atom, 1, 1,
                                            FunctionalValueSetKind::Integral, 0,
                                            CoverageSignedness::Unsigned, 0});
      schema.functionalValueAtoms.push_back(
          {set, 0, 0, atom, 1, 0, FunctionalValueAtomKind::IntegralValue,
           inclusive, 0, 0});
      schema.functionalValueLimbs.push_back({set, 0, 0, 0, 0, 0, 0, 0});
      std::string binName = "zero" + std::to_string(binOrdinal);
      schema.functionalBins.push_back(
          {bin, point, binName, FunctionalBinKind::State, 0, 1, binOrdinal,
           "top.cg." + name + "." + binName});
      schema.functionalBinPlans.push_back({bin, set});
    }
    FunctionalExpression expression;
    expression.id = 4000 + ordinal;
    expression.owner = point;
    expression.ownerKind = FunctionalExpressionOwnerKind::Item;
    expression.role = FunctionalExpressionRole::CoverpointSample;
    expression.resultKind = FunctionalExpressionResultKind::Integral;
    expression.bitWidth = 1;
    expression.signedness = CoverageSignedness::Unsigned;
    expression.evaluationPhase = FunctionalExpressionEvaluationPhase::Sample;
    expression.resultOrdinal = ordinal;
    schema.functionalExpressions.push_back(expression);
    schema.crossTargets.push_back({cross, point, ordinal, ordinal, 1,
                                   FunctionalExpressionResultKind::Integral,
                                   CoverageSignedness::Unsigned, 0});
  }
  schema.functionalItems.push_back({cross, 10, "product",
                                    FunctionalItemKind::Cross, 0, 100, 1, arity,
                                    "top.cg.product"});
  schema.crossPlans.push_back({cross, 0, arity, 0, 0,
                               CrossRetainAutoPolicy::Retain, 0, 0, 220, arity,
                               0});
  schema.schemaFingerprint = computeSchemaFingerprint(schema);
  return schema;
}

TEST_F(CoverageRuntimeDesignTest,
       WideCrossPreservesOverlappingTupleComponents) {
  using namespace obelisk::coverage;
  obelisk_rt_context *context =
      createContextForSchema(makeWideCrossSchema(false));
  ASSERT_NE(context, nullptr);
  ASSERT_EQ(finalizeCoverage(context, 0, 0, nullptr, nullptr), OBELISK_RT_OK);
  obelisk_rt_covergroup_v1 handle = 0;
  ASSERT_EQ(createFunctional(context, 10, &handle), OBELISK_RT_OK);
  std::array<uint8_t, 64> values{};
  std::array<obelisk_rt_functional_value_v1, 64> batch;
  for (size_t ordinal = 0; ordinal != batch.size(); ++ordinal)
    batch[ordinal] = {4000 + ordinal,
                      1,
                      1,
                      &values[ordinal],
                      nullptr,
                      nullptr,
                      0,
                      OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL,
                      0};
  auto sample = [&] {
    return obelisk_rt_v1_covergroup_sample(context, handle, batch.data(),
                                           batch.size());
  };
  ASSERT_EQ(sample(), OBELISK_RT_OK);
  ASSERT_EQ(sample(), OBELISK_RT_OK);
  // IEEE 1800-2023 19.6: a missing bin on any target makes this sample's
  // Cartesian product empty, even when the other targets overlap.
  values.back() = 1;
  ASSERT_EQ(sample(), OBELISK_RT_OK);
  auto &instance = context->coverage->instances.at(handle);
  const auto &type = context->coverage->types.at({10, instance.configuration});
  ASSERT_EQ(type.crosses.size(), 1u);
  const auto &tuples = instance.crossTuples.at(type.crosses.front().id);
  ASSERT_EQ(tuples.size(), 4u);
  detail::IndexMap<uint64_t, uint64_t> binItems;
  for (const auto &bin : instance.bins)
    binItems.emplace(bin.id, bin.item);
  detail::IndexSet<std::pair<uint64_t, uint64_t>> leadingComponents;
  for (const auto &[components, counter] : tuples) {
    ASSERT_EQ(components.size(), values.size());
    EXPECT_EQ(counter.count, 2u);
    EXPECT_TRUE(leadingComponents.emplace(components[0], components[1]).second);
    for (size_t ordinal = 0; ordinal != components.size(); ++ordinal)
      EXPECT_EQ(binItems.at(components[ordinal]),
                type.crosses.front().targets[ordinal])
          << ordinal;
  }
  EXPECT_EQ(leadingComponents.size(), 4u);
  Database snapshot = saveCoverage(context, "wide-cross");
  ASSERT_EQ(snapshot.sparseCrossTuples.size(), 4u);
  for (const auto &tuple : snapshot.sparseCrossTuples) {
    EXPECT_EQ(tuple.componentCount, 64u);
    EXPECT_EQ(tuple.hits, 2u);
  }
  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeDesignTest,
       WideCrossEmptyFinalTargetPrecedesProductOverflowAndCommitsAtomically) {
  obelisk_rt_context *context =
      createContextForSchema(makeWideCrossSchema(true));
  ASSERT_NE(context, nullptr);
  ASSERT_EQ(finalizeCoverage(context, 0, 0, nullptr, nullptr), OBELISK_RT_OK);
  obelisk_rt_covergroup_v1 handle = 0;
  ASSERT_EQ(createFunctional(context, 10, &handle), OBELISK_RT_OK);
  std::array<uint8_t, 64> values{};
  values.back() = 1;
  std::array<obelisk_rt_functional_value_v1, 64> batch;
  for (size_t ordinal = 0; ordinal != batch.size(); ++ordinal)
    batch[ordinal] = {4000 + ordinal,
                      1,
                      1,
                      &values[ordinal],
                      nullptr,
                      nullptr,
                      0,
                      OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL,
                      0};
  auto sample = [&] {
    return obelisk_rt_v1_covergroup_sample(context, handle, batch.data(),
                                           batch.size());
  };
  // Do not enumerate 4^63 prefixes: the final target has no matching bin.
  ASSERT_EQ(sample(), OBELISK_RT_OK);
  auto &instance = context->coverage->instances.at(handle);
  EXPECT_TRUE(instance.crossTuples.empty());
  std::vector<uint64_t> counts;
  for (const auto &bin : instance.bins)
    counts.push_back(bin.count);
  values.back() = 0;
  EXPECT_EQ(sample(), OBELISK_RT_OUT_OF_RESOURCES);
  EXPECT_TRUE(instance.crossTuples.empty());
  for (size_t bin = 0; bin != counts.size(); ++bin)
    EXPECT_EQ(instance.bins[bin].count, counts[bin]);
  values.back() = 1;
  ASSERT_EQ(sample(), OBELISK_RT_OK);
  for (size_t bin = 0; bin != counts.size(); ++bin)
    EXPECT_EQ(instance.bins[bin].count, 2 * counts[bin]);
  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeDesignTest, ItemTypeAveragesUseMutableInstanceWeights) {
  using namespace obelisk::coverage;
  Database schema = makeFunctionalSchema();
  schema.functionalItems.push_back({22, 10, "product",
                                    FunctionalItemKind::Cross, 0, 100, 1, 2,
                                    "top.cg.product"});
  schema.crossPlans.push_back(
      {22, 0, 2, 0, 0, CrossRetainAutoPolicy::Retain, 0, 0, 220, 4, 0});
  schema.crossTargets = {
      {22, 20, 0, 0, 2, FunctionalExpressionResultKind::Integral,
       CoverageSignedness::Unsigned, 0},
      {22, 21, 1, 2, 2, FunctionalExpressionResultKind::Integral,
       CoverageSignedness::Unsigned, 0}};
  schema.schemaFingerprint = computeSchemaFingerprint(schema);
  ASSERT_EQ(validate(schema), Status::Ok);
  obelisk_rt_context *context = createContextForSchema(schema);
  obelisk_rt_context *loaded = createContextForSchema(schema);
  ASSERT_NE(context, nullptr);
  ASSERT_NE(loaded, nullptr);
  ASSERT_EQ(finalizeCoverage(context, 0, 0, nullptr, nullptr), OBELISK_RT_OK);
  ASSERT_EQ(finalizeCoverage(loaded, 0, 0, nullptr, nullptr), OBELISK_RT_OK);
  obelisk_rt_covergroup_v1 hit = 0, miss = 0;
  ASSERT_EQ(createFunctional(context, 10, &hit), OBELISK_RT_OK);
  ASSERT_EQ(createFunctional(context, 10, &miss), OBELISK_RT_OK);
  auto weight = [&](obelisk_rt_covergroup_v1 handle, uint64_t item, int value) {
    ASSERT_EQ(
        obelisk_rt_v1_covergroup_set_integer_option(
            context, handle, item, OBELISK_RT_COVERGROUP_OPTION_WEIGHT, value),
        OBELISK_RT_OK);
  };
  weight(hit, 0, 11);
  weight(miss, 0, 17); // Group weights must not affect item averages.
  weight(hit, 20, 3);
  weight(miss, 20, 1);
  weight(hit, 22, 5);
  weight(miss, 22, 1);
  ASSERT_EQ(sampleFunctional(context, hit, 0, 2), OBELISK_RT_OK);
  ASSERT_EQ(sampleFunctional(context, hit, 1, 2), OBELISK_RT_OK);
  auto query = [&](obelisk_rt_context *where, uint64_t item, double expected,
                   bool empty = false) {
    double percentage = -1.0;
    int32_t covered = -1, total = -1;
    ASSERT_EQ(obelisk_rt_v1_covergroup_type_query(where, 10, item, &percentage,
                                                  &covered, &total),
              OBELISK_RT_OK);
    EXPECT_NEAR(percentage, expected, 1.0e-9);
    if (empty) {
      EXPECT_EQ(covered, 0);
      EXPECT_EQ(total, 0);
    } else {
      ASSERT_GT(total, 0);
      EXPECT_NEAR(100.0 * covered / total, expected, 1.0e-6);
    }
  };
  query(context, 20, 75.0);
  query(context, 22, 500.0 / 6.0);
  std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("obelisk-item-weights-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()) +
       ".obcov");
  const std::string pathText = path.string();
  ASSERT_EQ(
      writeFileAtomically(pathText, saveCoverage(context, "item-weights")),
      Status::Ok);
  ASSERT_EQ(
      obelisk_rt_v1_coverage_load(loaded, pathText.data(), pathText.size()),
      OBELISK_RT_OK);
  query(loaded, 20, 75.0);
  query(loaded, 22, 500.0 / 6.0);
  weight(hit, 20, 0);
  weight(hit, 22, 0);
  query(context, 20, 0.0);
  query(context, 22, 0.0);
  query(loaded, 20, 75.0); // A persisted profile is independent of live edits.
  weight(miss, 20, 0);
  weight(miss, 22, 0);
  query(context, 20, 0.0, true);
  query(context, 22, 0.0, true);
  for (uint64_t item : {uint64_t{20}, uint64_t{22}}) {
    ASSERT_EQ(obelisk_rt_v1_covergroup_set_type_integer_option(
                  context, 10, item, OBELISK_RT_COVERGROUP_OPTION_WEIGHT, 0),
              OBELISK_RT_OK);
    query(context, item, 100.0, true);
  }
  obelisk_rt_v1_context_destroy(loaded);
  obelisk_rt_v1_context_destroy(context);
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

TEST(CoverageDatabaseTest, AtomicWriteReplacesExistingImageAndCleansTemporary) {
  std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("obelisk-coverage-replace-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()) +
       ".obcov");
  obelisk::coverage::Database first;
  first.producer = "first";
  obelisk::coverage::Database second;
  second.producer = "second";
  ASSERT_EQ(obelisk::coverage::writeFileAtomically(path.string(), first),
            obelisk::coverage::Status::Ok);
  ASSERT_EQ(obelisk::coverage::writeFileAtomically(path.string(), second),
            obelisk::coverage::Status::Ok);

  obelisk::coverage::Database decoded;
  ASSERT_EQ(obelisk::coverage::readFile(path.string(), decoded),
            obelisk::coverage::Status::Ok);
  EXPECT_EQ(decoded.producer, "second");

  const std::string temporaryPrefix = path.filename().string() + ".tmp.";
  for (const std::filesystem::directory_entry &entry :
       std::filesystem::directory_iterator(path.parent_path())) {
    std::string filename = entry.path().filename().string();
    EXPECT_NE(filename.compare(0, temporaryPrefix.size(), temporaryPrefix), 0);
  }
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

TEST_F(CoverageRuntimeDesignTest, ParsesAndBindsEmbeddedSchema) {
  obelisk::coverage::Database schema;
  schema.sourceFiles.push_back({1, "design.sv", {}});
  schema.scopes.push_back({2, 0, "top", 0, "top"});
  schema.linePoints.push_back({3, 1, 1, 2, "", 7, 3, 7, 8, 0, 0});
  schema.toggleObjects.push_back({4, 2, 1, "state", 0, 2, 1, 1, 1, 5, 0});
  schema.toggleDimensions.push_back(
      {4, UINT32_MAX, obelisk::coverage::ToggleDimensionKind::Root, 1, 1, 0, 0,
       0, 2, "", 0});
  schema.toggleDimensions.push_back(
      {4, 0, obelisk::coverage::ToggleDimensionKind::Scalar, UINT32_MAX, 0, 1,
       0, 0, 2, "", 0});
  schema.exclusions.push_back(
      {4, obelisk::coverage::MetricKind::Toggle, "excluded", 0});
  std::vector<uint8_t> image;
  ASSERT_EQ(obelisk::coverage::serialize(schema, image),
            obelisk::coverage::Status::Ok);

  struct Execution {
    obelisk_rt_execution_descriptor_v1 descriptor{};
    obelisk_rt_execution_extension_v1 extension{};
  } execution;
  static_assert(offsetof(Execution, extension) ==
                sizeof(obelisk_rt_execution_descriptor_v1));
  execution.descriptor.version = OBELISK_RT_VERSION;
  execution.descriptor.flags = OBELISK_RT_EXECUTION_COVERAGE_SCHEMA;
  execution.descriptor.reserved = sizeof(execution.descriptor);
  execution.extension.version = OBELISK_RT_EXECUTION_EXTENSION_VERSION;
  execution.extension.size = sizeof(execution.extension);
  execution.extension.coverage_schema = image.data();
  execution.extension.coverage_schema_size = image.size();

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&execution.descriptor, &context),
      OBELISK_RT_OK);
  constexpr std::array<uint8_t, 1> zero{0};
  EXPECT_EQ(
      finalizeCoverage(context, 0, 2, zero.data(), zero.data()),
      OBELISK_RT_INVALID_DESIGN);
  EXPECT_EQ(
      finalizeCoverage(context, 1, 2, zero.data(), zero.data()),
      OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_coverage_toggle_seal(context), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);

  image.back() ^= 1;
  execution.extension.coverage_schema = image.data();
  context = nullptr;
  EXPECT_EQ(
      obelisk_rt_v1_context_create_for_design(&execution.descriptor, &context),
      OBELISK_RT_INVALID_DESIGN);
  EXPECT_EQ(context, nullptr);
}

TEST_F(CoverageRuntimeDesignTest, RejectsResolvedStateInEmbeddedSchema) {
  using namespace obelisk::coverage;
  Database schema = makeFunctionalSchema();
  addResolvedFunctionalConfiguration(schema);
  schema.schemaFingerprint = computeSchemaFingerprint(schema);
  ASSERT_FALSE(isSchemaOnly(schema));
  std::vector<uint8_t> image;
  ASSERT_EQ(serialize(schema, image), Status::Ok);

  struct Execution {
    obelisk_rt_execution_descriptor_v1 descriptor{};
    obelisk_rt_execution_extension_v1 extension{};
  } execution;
  static_assert(offsetof(Execution, extension) ==
                sizeof(obelisk_rt_execution_descriptor_v1));
  execution.descriptor.version = OBELISK_RT_VERSION;
  execution.descriptor.flags = OBELISK_RT_EXECUTION_COVERAGE_SCHEMA;
  execution.descriptor.reserved = sizeof(execution.descriptor);
  execution.extension.version = OBELISK_RT_EXECUTION_EXTENSION_VERSION;
  execution.extension.size = sizeof(execution.extension);
  execution.extension.coverage_schema = image.data();
  execution.extension.coverage_schema_size = image.size();

  obelisk_rt_context *context = nullptr;
  EXPECT_EQ(
      obelisk_rt_v1_context_create_for_design(&execution.descriptor, &context),
      OBELISK_RT_INVALID_DESIGN);
  EXPECT_EQ(context, nullptr);
}

TEST_F(CoverageRuntimeDesignTest,
     PersistsDistinctFunctionalInstancesFromEmbeddedSchema) {
  using namespace obelisk::coverage;
  Database schema = makeFunctionalSchema();
  schema.schemaFingerprint = computeSchemaFingerprint(schema);
  std::vector<uint8_t> image;
  ASSERT_EQ(serialize(schema, image), Status::Ok);

  struct Execution {
    obelisk_rt_execution_descriptor_v1 descriptor{};
    obelisk_rt_execution_extension_v1 extension{};
  } execution;
  static_assert(offsetof(Execution, extension) ==
                sizeof(obelisk_rt_execution_descriptor_v1));
  execution.descriptor.version = OBELISK_RT_VERSION;
  execution.descriptor.flags = OBELISK_RT_EXECUTION_COVERAGE_SCHEMA;
  execution.descriptor.reserved = sizeof(execution.descriptor);
  execution.extension.version = OBELISK_RT_EXECUTION_EXTENSION_VERSION;
  execution.extension.size = sizeof(execution.extension);
  execution.extension.coverage_schema = image.data();
  execution.extension.coverage_schema_size = image.size();

  obelisk_rt_context *context = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_context_create_for_design(&execution.descriptor, &context),
      OBELISK_RT_OK);
  ASSERT_EQ(finalizeCoverage(context, 0, 0, nullptr, nullptr),
            OBELISK_RT_OK);
  obelisk_rt_covergroup_v1 first = 0;
  obelisk_rt_covergroup_v1 second = 0;
  ASSERT_EQ(createFunctional(context, 10, &first), OBELISK_RT_OK);
  ASSERT_EQ(createFunctional(context, 10, &second), OBELISK_RT_OK);
  ASSERT_NE(first, second);
  ASSERT_EQ(sampleFunctional(context, first, 0, 3), OBELISK_RT_OK);
  ASSERT_EQ(sampleFunctional(context, second, 1, 2), OBELISK_RT_OK);

  std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("obelisk-functional-save-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()) +
       ".obcov");
  std::string pathText = path.string();
  ASSERT_EQ(obelisk_rt_v1_coverage_save(context, pathText.data(),
                                        pathText.size(), 0, 17),
            OBELISK_RT_OK);
  Database snapshot;
  ASSERT_EQ(readFile(pathText, snapshot), Status::Ok);
  ASSERT_EQ(snapshot.resolvedInstances.size(), 2u);
  EXPECT_EQ(snapshot.resolvedInstances[0].id, first);
  EXPECT_EQ(snapshot.resolvedInstances[1].id, second);
  EXPECT_EQ(snapshot.resolvedInstances[0].name, "$auto$1");
  EXPECT_EQ(snapshot.resolvedInstances[1].name, "$auto$2");
  EXPECT_NE(snapshot.resolvedInstances[0].name,
            snapshot.resolvedInstances[1].name);
  EXPECT_EQ(snapshot.resolvedInstances[0].flags, ResolvedInstanceGeneratedName);
  EXPECT_EQ(snapshot.resolvedInstances[1].flags, ResolvedInstanceGeneratedName);
  ASSERT_EQ(snapshot.counters.size(), 6u);
  auto count = [&](uint64_t instance, uint64_t bin) {
    auto found =
        std::find_if(snapshot.counters.begin(), snapshot.counters.end(),
                     [&](const Counter &counter) {
                       return counter.metric == MetricKind::Functional &&
                              counter.instance == instance &&
                              counter.entity == bin;
                     });
    EXPECT_NE(found, snapshot.counters.end());
    return found == snapshot.counters.end() ? uint64_t{0} : found->value;
  };
  auto resolvedBin = [&](uint64_t templateBin) {
    auto found = std::find_if(
        snapshot.resolvedFunctionalBins.begin(),
        snapshot.resolvedFunctionalBins.end(),
        [&](const auto &bin) { return bin.templateBin == templateBin; });
    EXPECT_NE(found, snapshot.resolvedFunctionalBins.end());
    return found == snapshot.resolvedFunctionalBins.end() ? uint64_t{0}
                                                          : found->id;
  };
  const uint64_t zeroBin = resolvedBin(30);
  const uint64_t oneBin = resolvedBin(31);
  const uint64_t twoBin = resolvedBin(40);
  EXPECT_EQ(count(first, zeroBin), 1u);
  EXPECT_EQ(count(first, oneBin), 0u);
  EXPECT_EQ(count(first, twoBin), 0u);
  EXPECT_EQ(count(second, zeroBin), 0u);
  EXPECT_EQ(count(second, oneBin), 1u);
  EXPECT_EQ(count(second, twoBin), 1u);

  uint8_t malformedValue = 0;
  obelisk_rt_functional_value_v1 malformed{
      999,     1,       1, &malformedValue,
      nullptr, nullptr, 0, OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL,
      0};
  obelisk_rt_covergroup_v1 rejected = 0;
  EXPECT_EQ(obelisk_rt_v1_covergroup_create(context, 10, &malformed, 1, nullptr,
                                            0, &rejected),
            OBELISK_RT_INVALID_DESIGN);
  obelisk_rt_v1_context_destroy(context);
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

TEST_F(CoverageRuntimeDesignTest,
     TemplateOnlySchemaResolvesStableNamespacesAndRoundTrips) {
  using namespace obelisk::coverage;
  Database schema = makeFunctionalSchema();
  obelisk_rt_context *context = createContextForSchema(schema, false);
  ASSERT_NE(context, nullptr);
  ASSERT_EQ(finalizeCoverage(context, 0, 0, nullptr, nullptr),
            OBELISK_RT_OK);
  obelisk_rt_covergroup_v1 handle = 0;
  ASSERT_EQ(createFunctional(context, 10, &handle), OBELISK_RT_OK);
  ASSERT_EQ(sampleFunctional(context, handle, 1, 2), OBELISK_RT_OK);

  Database saved = saveCoverage(context, "template-only");
  ASSERT_EQ(validate(saved), Status::Ok);
  ASSERT_EQ(saved.functionalConfigurations.size(), 1u);
  ASSERT_EQ(saved.resolvedFunctionalItems.size(), 2u);
  ASSERT_EQ(saved.resolvedFunctionalBins.size(), 3u);
  ASSERT_EQ(saved.resolvedFunctionalValueSets.size(), 3u);
  for (const auto &item : saved.resolvedFunctionalItems) {
    EXPECT_NE(item.id, item.templateItem);
    EXPECT_TRUE(
        std::none_of(saved.functionalItems.begin(), saved.functionalItems.end(),
                     [&](const auto &row) { return row.id == item.id; }));
  }
  for (const auto &bin : saved.resolvedFunctionalBins) {
    EXPECT_NE(bin.id, bin.templateBin);
    EXPECT_TRUE(
        std::none_of(saved.functionalBins.begin(), saved.functionalBins.end(),
                     [&](const auto &row) { return row.id == bin.id; }));
  }
  for (const auto &set : saved.resolvedFunctionalValueSets) {
    EXPECT_NE(set.id, set.templateValueSet);
    EXPECT_TRUE(std::none_of(
        saved.functionalValueSets.begin(), saved.functionalValueSets.end(),
        [&](const auto &row) { return row.id == set.id; }));
  }
  auto one = std::find_if(
      saved.resolvedFunctionalBins.begin(), saved.resolvedFunctionalBins.end(),
      [](const auto &bin) { return bin.templateBin == 31; });
  ASSERT_NE(one, saved.resolvedFunctionalBins.end());
  auto counter = std::find_if(
      saved.counters.begin(), saved.counters.end(), [&](const auto &entry) {
        return entry.metric == MetricKind::Functional &&
               entry.instance == handle && entry.entity == one->id;
      });
  ASSERT_NE(counter, saved.counters.end());
  EXPECT_EQ(counter->value, 1u);
  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeDesignTest, KnownBoundConsecutiveTransitionArrayResolves) {
  using namespace obelisk::coverage;
  Database schema;
  schema.scopes.push_back({1, 0, "top", 0, "top"});
  schema.functionalTypes.push_back({10, 1, "cg", 0, 2023, "top.cg"});
  schema.functionalItems.push_back({20, 10, "cp",
                                    FunctionalItemKind::Coverpoint, 0, 100, 1,
                                    0, "top.cg.cp"});
  schema.functionalBins.push_back({30, 20, "paths",
                                   FunctionalBinKind::Transition, 0, 1, 0,
                                   "top.cg.cp.paths"});
  schema.functionalValueSets.push_back(
      {50, 20, 0, 1, 4, FunctionalValueSetKind::Integral,
       FunctionalValueSetNeedsResolution, CoverageSignedness::Unsigned, 0});
  constexpr uint32_t inclusive =
      FunctionalValueAtomLowerInclusive | FunctionalValueAtomUpperInclusive;
  schema.functionalValueAtoms.push_back({50, 0, 0, 0, 0, 0,
                                         FunctionalValueAtomKind::IntegralValue,
                                         inclusive, 70, 0});
  FunctionalBinPlan plan;
  plan.bin = 30;
  plan.arrayMode = FunctionalBinArrayMode::Unsized;
  plan.distribution = FunctionalBinDistributionKind::PerValue;
  schema.functionalBinPlans.push_back(plan);
  schema.transitionPrograms.push_back({30, 20, 0, 1, 0});
  schema.transitionAlternatives.push_back({30, 50, 0, 1, 0, 0});
  schema.transitionSteps.push_back(
      {30, 50, 0, 0, 2, 3, 0, 0, TransitionRepetitionKind::Consecutive, 0});
  FunctionalExpression sampleExpression;
  sampleExpression.id = 60;
  sampleExpression.owner = 20;
  sampleExpression.ownerKind = FunctionalExpressionOwnerKind::Item;
  sampleExpression.role = FunctionalExpressionRole::CoverpointSample;
  sampleExpression.resultKind = FunctionalExpressionResultKind::Integral;
  sampleExpression.bitWidth = 4;
  sampleExpression.signedness = CoverageSignedness::Unsigned;
  sampleExpression.evaluationPhase =
      FunctionalExpressionEvaluationPhase::Sample;
  schema.functionalExpressions.push_back(sampleExpression);
  FunctionalExpression valueExpression = sampleExpression;
  valueExpression.id = 70;
  valueExpression.owner = 50;
  valueExpression.ownerKind = FunctionalExpressionOwnerKind::ValueSet;
  valueExpression.role = FunctionalExpressionRole::ValueAtomSingleton;
  valueExpression.evaluationPhase =
      FunctionalExpressionEvaluationPhase::Constructor;
  schema.functionalExpressions.push_back(valueExpression);
  schema.schemaFingerprint = computeSchemaFingerprint(schema);

  obelisk_rt_context *context = createContextForSchema(schema, false);
  ASSERT_NE(context, nullptr);
  ASSERT_EQ(finalizeCoverage(context, 0, 0, nullptr, nullptr),
            OBELISK_RT_OK);
  obelisk_rt_covergroup_v1 handle = 0;
  uint8_t transitionValue = 3;
  obelisk_rt_functional_value_v1 constructorValue{
      70,      4,       1, &transitionValue,
      nullptr, nullptr, 0, OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL,
      0};
  ASSERT_EQ(obelisk_rt_v1_covergroup_create(context, 10, nullptr, 0,
                                            &constructorValue, 1, &handle),
            OBELISK_RT_OK);
  auto sample = [&](uint8_t value) {
    obelisk_rt_functional_value_v1 sampled{
        60,      4,       1, &value,
        nullptr, nullptr, 0, OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL,
        0};
    return obelisk_rt_v1_covergroup_sample(context, handle, &sampled, 1);
  };
  ASSERT_EQ(sample(3), OBELISK_RT_OK);
  ASSERT_EQ(sample(3), OBELISK_RT_OK);
  ASSERT_EQ(sample(3), OBELISK_RT_OK);

  Database saved = saveCoverage(context, "known-transition-bounds");
  ASSERT_EQ(validate(saved), Status::Ok);
  ASSERT_EQ(saved.resolvedFunctionalBins.size(), 2u);
  EXPECT_EQ(saved.resolvedFunctionalBins[0].name, "paths[3[*2]]");
  EXPECT_EQ(saved.resolvedFunctionalBins[1].name, "paths[3[*3]]");
  ASSERT_EQ(saved.counters.size(), 2u);
  EXPECT_EQ(saved.counters[0].value, 2u);
  EXPECT_EQ(saved.counters[1].value, 1u);
  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeDesignTest,
     WildcardTransitionArrayRejectsHugeExpansionBeforeAllocation) {
  using namespace obelisk::coverage;
  Database schema;
  schema.scopes.push_back({1, 0, "top", 0, "top"});
  schema.functionalTypes.push_back({10, 1, "cg", 0, 2023, "top.cg"});
  schema.functionalItems.push_back({20, 10, "cp",
                                    FunctionalItemKind::Coverpoint, 0, 100, 1,
                                    0, "top.cg.cp"});
  schema.functionalBins.push_back(
      {30, 20, "huge", FunctionalBinKind::Transition, FunctionalBinWildcard, 1,
       0, "top.cg.cp.huge"});
  schema.functionalValueSets.push_back(
      {50, 20, 0, 1, 32, FunctionalValueSetKind::Integral,
       FunctionalValueSetNeedsResolution, CoverageSignedness::Unsigned, 0});
  constexpr uint32_t inclusive =
      FunctionalValueAtomLowerInclusive | FunctionalValueAtomUpperInclusive;
  schema.functionalValueAtoms.push_back({50, 0, 0, 0, 0, 0,
                                         FunctionalValueAtomKind::IntegralValue,
                                         inclusive, 70, 0});
  FunctionalBinPlan plan;
  plan.bin = 30;
  plan.arrayMode = FunctionalBinArrayMode::Unsized;
  plan.distribution = FunctionalBinDistributionKind::PerValue;
  schema.functionalBinPlans.push_back(plan);
  schema.transitionPrograms.push_back({30, 20, 0, 1, 0});
  schema.transitionAlternatives.push_back({30, 50, 0, 1, 0, 0});
  schema.transitionSteps.push_back(
      {30, 50, 0, 0, 2, 2, 0, 0, TransitionRepetitionKind::Consecutive, 0});
  FunctionalExpression sampleExpression;
  sampleExpression.id = 60;
  sampleExpression.owner = 20;
  sampleExpression.ownerKind = FunctionalExpressionOwnerKind::Item;
  sampleExpression.role = FunctionalExpressionRole::CoverpointSample;
  sampleExpression.resultKind = FunctionalExpressionResultKind::Integral;
  sampleExpression.bitWidth = 32;
  sampleExpression.signedness = CoverageSignedness::Unsigned;
  sampleExpression.evaluationPhase =
      FunctionalExpressionEvaluationPhase::Sample;
  schema.functionalExpressions.push_back(sampleExpression);
  FunctionalExpression valueExpression = sampleExpression;
  valueExpression.id = 70;
  valueExpression.owner = 50;
  valueExpression.ownerKind = FunctionalExpressionOwnerKind::ValueSet;
  valueExpression.role = FunctionalExpressionRole::ValueAtomSingleton;
  valueExpression.evaluationPhase =
      FunctionalExpressionEvaluationPhase::Constructor;
  schema.functionalExpressions.push_back(valueExpression);
  schema.schemaFingerprint = computeSchemaFingerprint(schema);

  obelisk_rt_context *context = createContextForSchema(schema, false);
  ASSERT_NE(context, nullptr);
  ASSERT_EQ(finalizeCoverage(context, 0, 0, nullptr, nullptr),
            OBELISK_RT_OK);
  uint32_t encoded = UINT32_MAX;
  uint32_t unknown = UINT32_MAX;
  obelisk_rt_functional_value_v1 constructorValue{
      70,
      32,
      sizeof(encoded),
      &encoded,
      &unknown,
      nullptr,
      0,
      OBELISK_RT_FUNCTIONAL_VALUE_FOUR_STATE,
      0};
  obelisk_rt_covergroup_v1 handle = 0;
  EXPECT_EQ(obelisk_rt_v1_covergroup_create(context, 10, nullptr, 0,
                                            &constructorValue, 1, &handle),
            OBELISK_RT_OUT_OF_RESOURCES);
  EXPECT_EQ(handle, 0u);
  obelisk_rt_v1_context_destroy(context);
}

obelisk::coverage::Database
makeGuardedTransitionSchema(uint32_t length, bool guardExclusions,
                            bool partialOrdinary = false) {
  using namespace obelisk::coverage;
  Database schema;
  schema.scopes.push_back({1, 0, "top", 0, "top"});
  schema.functionalTypes.push_back({10, 1, "cg", 0, 2023, "top.cg"});
  schema.functionalItems.push_back({20, 10, "cp",
                                    FunctionalItemKind::Coverpoint, 0, 100, 1,
                                    0, "top.cg.cp"});
  FunctionalExpression sample;
  sample.id = 60;
  sample.owner = 20;
  sample.ownerKind = FunctionalExpressionOwnerKind::Item;
  sample.role = FunctionalExpressionRole::CoverpointSample;
  sample.resultKind = FunctionalExpressionResultKind::Integral;
  sample.bitWidth = 2;
  sample.signedness = CoverageSignedness::Unsigned;
  sample.evaluationPhase = FunctionalExpressionEvaluationPhase::Sample;
  schema.functionalExpressions.push_back(sample);
  std::vector<FunctionalExpression> constructionExpressions;
  constexpr uint32_t inclusive =
      FunctionalValueAtomLowerInclusive | FunctionalValueAtomUpperInclusive;
  constexpr std::array<uint32_t, 3> flags{0, FunctionalBinIgnore,
                                          FunctionalBinIllegal};
  constexpr std::array<const char *, 3> names{"ordinary", "ignored", "illegal"};
  for (uint32_t binOrdinal = 0; binOrdinal != names.size(); ++binOrdinal) {
    uint64_t bin = 30 + binOrdinal;
    uint32_t firstStep = schema.transitionSteps.size();
    schema.functionalBins.push_back(
        {bin, 20, names[binOrdinal], FunctionalBinKind::Transition,
         flags[binOrdinal], 1, binOrdinal,
         std::string("top.cg.cp.") + names[binOrdinal]});
    FunctionalBinPlan plan;
    plan.bin = bin;
    if (!binOrdinal || guardExclusions) {
      FunctionalExpression guard = sample;
      guard.id = 61 + binOrdinal;
      guard.owner = bin;
      guard.ownerKind = FunctionalExpressionOwnerKind::Bin;
      guard.role = FunctionalExpressionRole::BinIff;
      guard.resultKind = FunctionalExpressionResultKind::Boolean;
      guard.bitWidth = 0;
      guard.signedness = CoverageSignedness::NotApplicable;
      guard.resultOrdinal = 1 + binOrdinal;
      schema.functionalExpressions.push_back(guard);
      plan.iffExpression = guard.id;
    }
    schema.functionalBinPlans.push_back(plan);
    schema.transitionPrograms.push_back({bin, 20, binOrdinal, 1, 0});
    for (uint32_t ordinal = 0; ordinal != length; ++ordinal) {
      uint32_t atom = schema.functionalValueAtoms.size();
      uint64_t set = 100 + atom, expressionID = 10000 + atom;
      bool range = partialOrdinary && !binOrdinal && !ordinal;
      schema.functionalValueSets.push_back(
          {set, 20, atom, 1, 2, FunctionalValueSetKind::Integral,
           FunctionalValueSetNeedsResolution, CoverageSignedness::Unsigned, 0});
      schema.functionalValueAtoms.push_back(
          {set, 0, 0, 0, 0, 0,
           range ? FunctionalValueAtomKind::IntegralRange
                 : FunctionalValueAtomKind::IntegralValue,
           inclusive, expressionID, range ? uint64_t{20000} : uint64_t{0}});
      FunctionalExpression value = sample;
      value.id = expressionID;
      value.owner = set;
      value.ownerKind = FunctionalExpressionOwnerKind::ValueSet;
      value.role = range ? FunctionalExpressionRole::ValueAtomLower
                         : FunctionalExpressionRole::ValueAtomSingleton;
      value.evaluationPhase = FunctionalExpressionEvaluationPhase::Constructor;
      value.resultOrdinal = atom;
      constructionExpressions.push_back(value);
      schema.transitionSteps.push_back({bin, set, 0, 0, 1, 1, 0, ordinal,
                                        TransitionRepetitionKind::Once, 0});
    }
    schema.transitionAlternatives.push_back(
        {bin, 100 + firstStep + length - 1, firstStep, length, 0, 0});
  }
  if (partialOrdinary) {
    auto high = constructionExpressions.front();
    high.id = 20000;
    high.role = FunctionalExpressionRole::ValueAtomUpper;
    high.resultOrdinal = constructionExpressions.size();
    constructionExpressions.push_back(high);
  }
  schema.functionalExpressions.insert(schema.functionalExpressions.end(),
                                      constructionExpressions.begin(),
                                      constructionExpressions.end());
  schema.functionalBins.push_back(
      {33, 20, "fallback", FunctionalBinKind::Transition,
       FunctionalBinDefaultSequence, 1, 3, "top.cg.cp.fallback"});
  schema.functionalBinPlans.push_back({33, 0});
  schema.transitionPrograms.push_back({33, 20, 3, 0, 0});
  schema.schemaFingerprint = computeSchemaFingerprint(schema);
  return schema;
}

obelisk_rt_status
createGuardedTransitionFunctional(obelisk_rt_context *context, uint32_t length,
                                  bool matchingExclusions, bool partialOrdinary,
                                  obelisk_rt_covergroup_v1 *handle) {
  size_t count = 3 * length;
  std::vector<uint8_t> values(count + partialOrdinary);
  std::vector<obelisk_rt_functional_value_v1> expressions(values.size());
  for (size_t index = 0; index != count; ++index) {
    values[index] = matchingExclusions || index < length
                        ? 1 + index % length % 2
                        : (index < 2 * length ? 3 : 0);
    expressions[index] = {10000 + index,
                          2,
                          1,
                          &values[index],
                          nullptr,
                          nullptr,
                          0,
                          OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL,
                          0};
  }
  if (partialOrdinary) {
    values.back() = 3;
    expressions.back() = {
        20000,   2,       1, &values.back(),
        nullptr, nullptr, 0, OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL,
        0};
  }
  return obelisk_rt_v1_covergroup_create(
      context, 10, nullptr, 0, expressions.data(), expressions.size(), handle);
}

struct GuardedTransitionSample {
  obelisk_rt_context *context;
  obelisk_rt_covergroup_v1 handle;
  uint8_t enabled = 1, ignored = 0, illegal = 0;

  obelisk_rt_status operator()(uint8_t value) {
    std::array<obelisk_rt_functional_value_v1, 4> batch{
        obelisk_rt_functional_value_v1{60, 2, 1, &value, nullptr, nullptr, 0,
                                       OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL, 0},
        {61, 1, 1, &enabled, nullptr, nullptr, 0,
         OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL, 0},
        {62, 1, 1, &ignored, nullptr, nullptr, 0,
         OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL, 0},
        {63, 1, 1, &illegal, nullptr, nullptr, 0,
         OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL, 0}};
    return obelisk_rt_v1_covergroup_sample(context, handle, batch.data(),
                                           batch.size());
  }
};

TEST_F(CoverageRuntimeDesignTest,
       TransitionBinIffGuardsCountersWithoutChangingDenominator) {
  obelisk_rt_context *context =
      createContextForSchema(makeGuardedTransitionSchema(2, true));
  ASSERT_NE(context, nullptr);
  ASSERT_EQ(finalizeCoverage(context, 0, 0, nullptr, nullptr), OBELISK_RT_OK);
  obelisk_rt_covergroup_v1 handle = 0;
  ASSERT_EQ(
      createGuardedTransitionFunctional(context, 2, false, false, &handle),
      OBELISK_RT_OK);
  GuardedTransitionSample sample{context, handle};
  sample.enabled = 0;
  const auto &bins = context->coverage->instances.at(handle).bins;
  ASSERT_EQ(sample(1), OBELISK_RT_OK);
  ASSERT_EQ(sample(2), OBELISK_RT_OK);
  EXPECT_EQ(bins.front().count, 0u);
  sample.enabled = 1;
  ASSERT_EQ(sample(1), OBELISK_RT_OK);
  ASSERT_EQ(sample(2), OBELISK_RT_OK);
  EXPECT_EQ(bins.front().count, 1u);
  double percentage = 0;
  int32_t covered = 0, total = 0;
  ASSERT_EQ(obelisk_rt_v1_covergroup_instance_query(
                context, handle, 20, &percentage, &covered, &total),
            OBELISK_RT_OK);
  EXPECT_EQ(total, 1);
  EXPECT_EQ(covered, 1);
  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeDesignTest,
       GuardedTransitionExclusionsRemainStaticAndGuardIllegalDiagnostics) {
  using namespace obelisk::coverage;
  obelisk_rt_context *context =
      createContextForSchema(makeGuardedTransitionSchema(2, true));
  ASSERT_NE(context, nullptr);
  ASSERT_EQ(finalizeCoverage(context, 0, 0, nullptr, nullptr), OBELISK_RT_OK);
  obelisk_rt_covergroup_v1 handle = 0;
  ASSERT_EQ(createGuardedTransitionFunctional(context, 2, true, false, &handle),
            OBELISK_RT_OK);
  GuardedTransitionSample sample{context, handle};
  const auto &bins = context->coverage->instances.at(handle).bins;
  EXPECT_TRUE(bins.front().flags & FunctionalBinEmpty);
  ASSERT_EQ(sample(1), OBELISK_RT_OK);
  ASSERT_EQ(sample(2), OBELISK_RT_OK);
  EXPECT_TRUE(
      saveCoverage(context, "disabled-illegal").illegalBinDiagnostics.empty());
  sample.illegal = 1;
  ASSERT_EQ(sample(1), OBELISK_RT_OK);
  ASSERT_EQ(sample(2), OBELISK_RT_OK);
  sample.illegal = 0;
  ASSERT_EQ(sample(1), OBELISK_RT_OK);
  ASSERT_EQ(sample(2), OBELISK_RT_OK);
  EXPECT_EQ(bins.front().count, 0u);
  Database saved = saveCoverage(context, "guarded-transition-exclusions");
  ASSERT_EQ(saved.illegalBinDiagnostics.size(), 1u);
  EXPECT_EQ(saved.illegalBinDiagnostics.front().bin, bins[2].id);
  EXPECT_EQ(saved.illegalBinDiagnostics.front().count, 1u);
  double percentage = 0;
  int32_t covered = 0, total = 0;
  ASSERT_EQ(obelisk_rt_v1_covergroup_instance_query(
                context, handle, 20, &percentage, &covered, &total),
            OBELISK_RT_OK);
  EXPECT_EQ(total, 0);
  EXPECT_EQ(covered, 0);
  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeDesignTest,
       GuardedTransitionExclusionsRemoveDisabledMatchingSubsequences) {
  using namespace obelisk::coverage;
  obelisk_rt_context *context =
      createContextForSchema(makeGuardedTransitionSchema(2, true, true));
  ASSERT_NE(context, nullptr);
  ASSERT_EQ(finalizeCoverage(context, 0, 0, nullptr, nullptr), OBELISK_RT_OK);
  obelisk_rt_covergroup_v1 handle = 0;
  ASSERT_EQ(createGuardedTransitionFunctional(context, 2, true, true, &handle),
            OBELISK_RT_OK);
  GuardedTransitionSample sample{context, handle};
  const auto &bins = context->coverage->instances.at(handle).bins;
  EXPECT_FALSE(bins.front().flags & FunctionalBinEmpty);
  ASSERT_EQ(sample(1), OBELISK_RT_OK);
  ASSERT_EQ(sample(2), OBELISK_RT_OK);
  EXPECT_EQ(bins.front().count, 0u);
  ASSERT_EQ(sample(3), OBELISK_RT_OK);
  ASSERT_EQ(sample(2), OBELISK_RT_OK);
  EXPECT_EQ(bins.front().count, 1u);
  EXPECT_TRUE(saveCoverage(context, "partial-exclusions")
                  .illegalBinDiagnostics.empty());
  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeDesignTest,
       LongFixedTransitionExclusionsKeepGuardedOrdinaryBinsEmpty) {
  using namespace obelisk::coverage;
  for (bool guardExclusions : {false, true}) {
    obelisk_rt_context *context = createContextForSchema(
        makeGuardedTransitionSchema(64, guardExclusions));
    ASSERT_NE(context, nullptr);
    ASSERT_EQ(finalizeCoverage(context, 0, 0, nullptr, nullptr), OBELISK_RT_OK);
    obelisk_rt_covergroup_v1 handle = 0;
    ASSERT_EQ(
        createGuardedTransitionFunctional(context, 64, true, false, &handle),
        OBELISK_RT_OK);
    const auto &instance = context->coverage->instances.at(handle);
    EXPECT_TRUE(instance.bins.front().flags & FunctionalBinEmpty);
    const auto &type =
        context->coverage->types.at({10, instance.configuration});
    EXPECT_EQ(type.suppressedTransitionAlternatives.size(), 1u);
    double percentage = 0;
    int32_t covered = 0, total = 0;
    ASSERT_EQ(obelisk_rt_v1_covergroup_instance_query(
                  context, handle, 20, &percentage, &covered, &total),
              OBELISK_RT_OK);
    EXPECT_EQ(total, 0);
    EXPECT_EQ(covered, 0);
    obelisk_rt_v1_context_destroy(context);
  }
}

obelisk::coverage::Database
makeRepeatedExclusionSchema(uint64_t ordinaryLower, uint64_t ordinaryUpper,
                            uint64_t ignoredLower, uint64_t ignoredUpper,
                            bool partialOrdinary = false) {
  using namespace obelisk::coverage;
  Database schema = makeGuardedTransitionSchema(2, true, partialOrdinary);
  const std::array<std::tuple<size_t, uint64_t, uint64_t>, 2> bounds{
      {{0, ordinaryLower, ordinaryUpper}, {2, ignoredLower, ignoredUpper}}};
  for (auto [index, lower, upper] : bounds) {
    auto &step = schema.transitionSteps[index];
    step.repetition = TransitionRepetitionKind::Consecutive;
    step.lowerBound = lower;
    step.upperBound = upper;
  }
  schema.schemaFingerprint = computeSchemaFingerprint(schema);
  return schema;
}

obelisk_rt_status
createRepeatedExclusionFunctional(obelisk_rt_context *context,
                                  bool partialOrdinary, uint8_t ignoredLast,
                                  obelisk_rt_covergroup_v1 *handle) {
  std::array<uint8_t, 7> values{1, 2, 1, ignoredLast, 0, 0, 3};
  std::array<obelisk_rt_functional_value_v1, 7> expressions;
  for (size_t index = 0; index != expressions.size(); ++index)
    expressions[index] = {index == 6 ? 20000 : 10000 + index,
                          2,
                          1,
                          &values[index],
                          nullptr,
                          nullptr,
                          0,
                          OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL,
                          0};
  return obelisk_rt_v1_covergroup_create(context, 10, nullptr, 0,
                                         expressions.data(),
                                         partialOrdinary ? 7 : 6, handle);
}

TEST_F(CoverageRuntimeDesignTest,
       RepeatedTransitionExclusionsRemoveEveryCoveredLengthFromDenominator) {
  using namespace obelisk::coverage;
  constexpr std::array<std::array<uint64_t, 4>, 4> bounds{
      {{2, 2, 1, 1}, {3, 3, 2, 3}, {2, 3, 1, 1}, {2, 3, 2, 2}}};
  for (const auto &test : bounds) {
    Database schema =
        makeRepeatedExclusionSchema(test[0], test[1], test[2], test[3]);
    obelisk_rt_context *context = createContextForSchema(schema);
    ASSERT_NE(context, nullptr);
    ASSERT_EQ(finalizeCoverage(context, 0, 0, nullptr, nullptr), OBELISK_RT_OK);
    obelisk_rt_covergroup_v1 handle = 0;
    ASSERT_EQ(createRepeatedExclusionFunctional(context, false, 2, &handle),
              OBELISK_RT_OK);
    const auto &instance = context->coverage->instances.at(handle);
    EXPECT_TRUE(instance.bins.front().flags & FunctionalBinEmpty);
    double percentage = 0;
    int32_t covered = 0, total = 0;
    ASSERT_EQ(obelisk_rt_v1_covergroup_instance_query(
                  context, handle, 20, &percentage, &covered, &total),
              OBELISK_RT_OK);
    EXPECT_EQ(covered, 0);
    EXPECT_EQ(total, 0);
    obelisk_rt_v1_context_destroy(context);
  }
}

TEST_F(CoverageRuntimeDesignTest,
       BoundedTransitionExclusionsPreserveShorterOverlappingMatches) {
  using namespace obelisk::coverage;
  for (bool adjacentRepetition : {false, true}) {
    Database schema =
        makeRepeatedExclusionSchema(1, adjacentRepetition ? 2 : 3, 2, 2);
    if (adjacentRepetition) {
      auto &ordinaryLast = schema.transitionSteps[1];
      ordinaryLast.repetition = TransitionRepetitionKind::Consecutive;
      ordinaryLast.upperBound = 2;
      auto &ignoredLast = schema.transitionSteps[3];
      ignoredLast.repetition = TransitionRepetitionKind::Consecutive;
      ignoredLast.lowerBound = ignoredLast.upperBound = 2;
      schema.schemaFingerprint = computeSchemaFingerprint(schema);
    }
    obelisk_rt_context *context = createContextForSchema(schema);
    ASSERT_NE(context, nullptr);
    ASSERT_EQ(finalizeCoverage(context, 0, 0, nullptr, nullptr), OBELISK_RT_OK);
    obelisk_rt_covergroup_v1 handle = 0;
    ASSERT_EQ(createRepeatedExclusionFunctional(context, false, 2, &handle),
              OBELISK_RT_OK);
    const auto &instance = context->coverage->instances.at(handle);
    EXPECT_FALSE(instance.bins.front().flags & FunctionalBinEmpty);
    GuardedTransitionSample sample{context, handle};
    ASSERT_EQ(sample(1), OBELISK_RT_OK);
    ASSERT_EQ(sample(1), OBELISK_RT_OK);
    ASSERT_EQ(sample(2), OBELISK_RT_OK);
    EXPECT_EQ(instance.bins.front().count, 1u);
    if (adjacentRepetition) {
      ASSERT_EQ(sample(2), OBELISK_RT_OK);
      EXPECT_EQ(instance.bins.front().count, 2u);
    }
    obelisk_rt_v1_context_destroy(context);
  }
}

TEST_F(CoverageRuntimeDesignTest,
       RepeatedTransitionPrefixKeepsEarlierExclusionTaint) {
  using namespace obelisk::coverage;
  Database schema = makeRepeatedExclusionSchema(3, 3, 1, 1, true);
  obelisk_rt_context *context = createContextForSchema(schema);
  ASSERT_NE(context, nullptr);
  ASSERT_EQ(finalizeCoverage(context, 0, 0, nullptr, nullptr), OBELISK_RT_OK);
  obelisk_rt_covergroup_v1 handle = 0;
  ASSERT_EQ(createRepeatedExclusionFunctional(context, true, 1, &handle),
            OBELISK_RT_OK);
  const auto &instance = context->coverage->instances.at(handle);
  EXPECT_FALSE(instance.bins.front().flags & FunctionalBinEmpty);
  GuardedTransitionSample sample{context, handle};
  for (uint8_t value : {1, 1, 3, 2})
    ASSERT_EQ(sample(value), OBELISK_RT_OK);
  EXPECT_EQ(instance.bins.front().count, 0u);
  for (uint8_t value : {3, 3, 3, 2})
    ASSERT_EQ(sample(value), OBELISK_RT_OK);
  EXPECT_EQ(instance.bins.front().count, 1u);
  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeDesignTest,
       RepeatedIllegalTransitionReportsOncePerSampleWhenGuarded) {
  using namespace obelisk::coverage;
  Database schema = makeRepeatedExclusionSchema(3, 3, 2, 3);
  schema.functionalBins[1].flags = FunctionalBinIllegal;
  schema.schemaFingerprint = computeSchemaFingerprint(schema);
  obelisk_rt_context *context = createContextForSchema(schema);
  ASSERT_NE(context, nullptr);
  ASSERT_EQ(finalizeCoverage(context, 0, 0, nullptr, nullptr), OBELISK_RT_OK);
  obelisk_rt_covergroup_v1 handle = 0;
  ASSERT_EQ(createRepeatedExclusionFunctional(context, false, 2, &handle),
            OBELISK_RT_OK);
  const auto &bins = context->coverage->instances.at(handle).bins;
  EXPECT_TRUE(bins.front().flags & FunctionalBinEmpty);
  GuardedTransitionSample sample{context, handle};
  for (uint8_t value : {1, 1, 2})
    ASSERT_EQ(sample(value), OBELISK_RT_OK);
  EXPECT_TRUE(saveCoverage(context, "disabled-repeated-illegal")
                  .illegalBinDiagnostics.empty());
  // This guard belongs to the bin changed from ignored to illegal above.
  sample.ignored = 1;
  for (uint8_t value : {1, 1, 1, 2})
    ASSERT_EQ(sample(value), OBELISK_RT_OK);
  EXPECT_EQ(bins.front().count, 0u);
  Database saved = saveCoverage(context, "enabled-repeated-illegal");
  ASSERT_EQ(saved.illegalBinDiagnostics.size(), 1u);
  EXPECT_EQ(saved.illegalBinDiagnostics.front().bin, bins[1].id);
  EXPECT_EQ(saved.illegalBinDiagnostics.front().count, 1u);
  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeDesignTest,
       HugeRepeatedTransitionExclusionDerivationFailsBeforePublication) {
  auto schema =
      makeRepeatedExclusionSchema(UINT64_MAX - 1, UINT64_MAX - 1, 1, 1);
  obelisk_rt_context *context = createContextForSchema(schema);
  ASSERT_NE(context, nullptr);
  ASSERT_EQ(finalizeCoverage(context, 0, 0, nullptr, nullptr), OBELISK_RT_OK);
  obelisk_rt_covergroup_v1 handle = 0;
  EXPECT_EQ(createRepeatedExclusionFunctional(context, false, 2, &handle),
            OBELISK_RT_OUT_OF_RESOURCES);
  EXPECT_EQ(handle, 0u);
  EXPECT_TRUE(context->coverage->instances.empty());
  EXPECT_TRUE(context->coverage->types.empty());
  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeDesignTest,
     FixedTransitionExclusionDerivationHasDeterministicWorkLimit) {
  using namespace obelisk::coverage;
  Database schema;
  schema.scopes.push_back({1, 0, "top", 0, "top"});
  schema.functionalTypes.push_back({10, 1, "cg", 0, 2023, "top.cg"});
  schema.functionalItems.push_back({20, 10, "cp",
                                    FunctionalItemKind::Coverpoint, 0, 100, 1,
                                    0, "top.cg.cp"});
  schema.functionalBins.push_back({30, 20, "ordinary",
                                   FunctionalBinKind::Transition, 0, 1, 0,
                                   "top.cg.cp.ordinary"});
  schema.functionalBins.push_back(
      {31, 20, "ignored", FunctionalBinKind::Transition, FunctionalBinIgnore, 1,
       1, "top.cg.cp.ignored"});
  constexpr uint32_t inclusive =
      FunctionalValueAtomLowerInclusive | FunctionalValueAtomUpperInclusive;
  schema.functionalBinPlans.push_back({30, 0});
  schema.functionalBinPlans.push_back({31, 0});

  constexpr uint32_t alternativeCount = 8192;
  constexpr uint32_t stepCount = (alternativeCount + 1) * 2;
  schema.functionalValueSets.reserve(stepCount);
  schema.functionalValueAtoms.reserve(stepCount);
  schema.functionalExpressions.reserve(stepCount + 1);
  for (uint32_t ordinal = 0; ordinal != stepCount; ++ordinal) {
    uint64_t set = 100 + ordinal;
    uint64_t expressionID = 100000 + ordinal;
    schema.functionalValueSets.push_back(
        {set, 20, ordinal, 1, 1, FunctionalValueSetKind::Integral,
         FunctionalValueSetNeedsResolution, CoverageSignedness::Unsigned, 0});
    schema.functionalValueAtoms.push_back(
        {set, 0, 0, 0, 0, 0, FunctionalValueAtomKind::IntegralValue, inclusive,
         expressionID, 0});
    FunctionalExpression expression;
    expression.id = expressionID;
    expression.owner = set;
    expression.ownerKind = FunctionalExpressionOwnerKind::ValueSet;
    expression.role = FunctionalExpressionRole::ValueAtomSingleton;
    expression.resultKind = FunctionalExpressionResultKind::Integral;
    expression.bitWidth = 1;
    expression.signedness = CoverageSignedness::Unsigned;
    expression.evaluationPhase =
        FunctionalExpressionEvaluationPhase::Constructor;
    expression.resultOrdinal = ordinal;
    schema.functionalExpressions.push_back(expression);
  }
  schema.transitionPrograms.push_back({30, 20, 0, alternativeCount, 0});
  schema.transitionPrograms.push_back({31, 20, alternativeCount, 1, 0});
  schema.transitionAlternatives.reserve(alternativeCount + 1);
  schema.transitionSteps.reserve((uint64_t{alternativeCount} + 1) * 2);
  for (uint32_t ordinal = 0; ordinal != alternativeCount; ++ordinal) {
    uint32_t firstStep = ordinal * 2;
    uint64_t firstSet = 100 + firstStep;
    uint64_t secondSet = firstSet + 1;
    schema.transitionAlternatives.push_back(
        {30, secondSet, firstStep, 2, ordinal, 0});
    schema.transitionSteps.push_back({30, firstSet, 0, 0, 1, 1, ordinal, 0,
                                      TransitionRepetitionKind::Once, 0});
    schema.transitionSteps.push_back({30, secondSet, 0, 0, 1, 1, ordinal, 1,
                                      TransitionRepetitionKind::Once, 0});
  }
  uint64_t ignoredFirstSet = 100 + alternativeCount * 2;
  uint64_t ignoredSecondSet = ignoredFirstSet + 1;
  schema.transitionAlternatives.push_back(
      {31, ignoredSecondSet, alternativeCount * 2, 2, 0, 0});
  schema.transitionSteps.push_back({31, ignoredFirstSet, 0, 0, 1, 1, 0, 0,
                                    TransitionRepetitionKind::Once, 0});
  schema.transitionSteps.push_back({31, ignoredSecondSet, 0, 0, 1, 1, 0, 1,
                                    TransitionRepetitionKind::Once, 0});
  FunctionalExpression sample;
  sample.id = 60;
  sample.owner = 20;
  sample.ownerKind = FunctionalExpressionOwnerKind::Item;
  sample.role = FunctionalExpressionRole::CoverpointSample;
  sample.resultKind = FunctionalExpressionResultKind::Integral;
  sample.bitWidth = 1;
  sample.signedness = CoverageSignedness::Unsigned;
  sample.evaluationPhase = FunctionalExpressionEvaluationPhase::Sample;
  schema.functionalExpressions.push_back(sample);
  std::sort(
      schema.functionalExpressions.begin(), schema.functionalExpressions.end(),
      [](const auto &left, const auto &right) { return left.id < right.id; });
  schema.schemaFingerprint = computeSchemaFingerprint(schema);

  obelisk_rt_context *context = createContextForSchema(schema, false);
  ASSERT_NE(context, nullptr);
  ASSERT_EQ(finalizeCoverage(context, 0, 0, nullptr, nullptr),
            OBELISK_RT_OK);
  std::vector<uint8_t> values(stepCount);
  std::vector<obelisk_rt_functional_value_v1> expressions(stepCount);
  for (uint32_t ordinal = 0; ordinal != stepCount; ++ordinal) {
    values[ordinal] = ordinal & 1;
    expressions[ordinal] = {100000 + ordinal,
                            1,
                            1,
                            &values[ordinal],
                            nullptr,
                            nullptr,
                            0,
                            OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL,
                            0};
  }
  obelisk_rt_covergroup_v1 handle = 0;
  EXPECT_EQ(obelisk_rt_v1_covergroup_create(context, 10, nullptr, 0,
                                            expressions.data(),
                                            expressions.size(), &handle),
            OBELISK_RT_OUT_OF_RESOURCES);
  EXPECT_EQ(handle, 0u);
  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeDesignTest,
     SymbolicTransitionExclusionDerivationHasDeterministicWorkLimit) {
  using namespace obelisk::coverage;
  Database schema;
  schema.scopes.push_back({1, 0, "top", 0, "top"});
  schema.functionalTypes.push_back({10, 1, "cg", 0, 2023, "top.cg"});
  schema.functionalItems.push_back({20, 10, "cp",
                                    FunctionalItemKind::Coverpoint, 0, 100, 1,
                                    0, "top.cg.cp"});
  schema.functionalBins.push_back({30, 20, "ordinary",
                                   FunctionalBinKind::Transition, 0, 1, 0,
                                   "top.cg.cp.ordinary"});
  schema.functionalBins.push_back(
      {31, 20, "ignored", FunctionalBinKind::Transition, FunctionalBinIgnore, 1,
       1, "top.cg.cp.ignored"});
  schema.functionalBinPlans.push_back({30, 0});
  schema.functionalBinPlans.push_back({31, 0});

  constexpr uint32_t inclusive =
      FunctionalValueAtomLowerInclusive | FunctionalValueAtomUpperInclusive;
  constexpr uint32_t alternativeCount = 3000;
  constexpr uint32_t stepCount = (alternativeCount + 1) * 2;
  auto addConstructorExpression = [&](uint64_t id, uint64_t owner,
                                      FunctionalExpressionRole role,
                                      uint32_t resultOrdinal) {
    FunctionalExpression expression;
    expression.id = id;
    expression.owner = owner;
    expression.ownerKind = FunctionalExpressionOwnerKind::ValueSet;
    expression.role = role;
    expression.resultKind = FunctionalExpressionResultKind::Integral;
    expression.bitWidth = 1;
    expression.signedness = CoverageSignedness::Unsigned;
    expression.evaluationPhase =
        FunctionalExpressionEvaluationPhase::Constructor;
    expression.resultOrdinal = resultOrdinal;
    schema.functionalExpressions.push_back(expression);
  };
  schema.functionalValueSets.reserve(stepCount);
  schema.functionalValueAtoms.reserve(stepCount);
  schema.functionalExpressions.reserve(stepCount + 2);
  for (uint32_t ordinal = 0; ordinal != stepCount; ++ordinal) {
    uint64_t set = 100 + ordinal;
    uint64_t expression = 100000 + ordinal;
    schema.functionalValueSets.push_back(
        {set, 20, ordinal, 1, 1, FunctionalValueSetKind::Integral,
         FunctionalValueSetNeedsResolution, CoverageSignedness::Unsigned, 0});
    schema.functionalValueAtoms.push_back(
        {set, 0, 0, 0, 0, 0, FunctionalValueAtomKind::IntegralValue, inclusive,
         expression, 0});
    addConstructorExpression(
        expression, set, FunctionalExpressionRole::ValueAtomSingleton, ordinal);
  }
  const uint32_t ignoredFirstOrdinal = alternativeCount * 2;
  auto &ignoredFirstAtom = schema.functionalValueAtoms[ignoredFirstOrdinal];
  ignoredFirstAtom.kind = FunctionalValueAtomKind::IntegralRange;
  ignoredFirstAtom.upperExpression = 200000;
  addConstructorExpression(200000, ignoredFirstAtom.valueSet,
                           FunctionalExpressionRole::ValueAtomUpper, stepCount);
  auto ignoredLower = std::find_if(
      schema.functionalExpressions.begin(), schema.functionalExpressions.end(),
      [&](const auto &expression) {
        return expression.id == ignoredFirstAtom.lowerExpression;
      });
  ASSERT_NE(ignoredLower, schema.functionalExpressions.end());
  ignoredLower->role = FunctionalExpressionRole::ValueAtomLower;
  FunctionalExpression sample;
  sample.id = 60;
  sample.owner = 20;
  sample.ownerKind = FunctionalExpressionOwnerKind::Item;
  sample.role = FunctionalExpressionRole::CoverpointSample;
  sample.resultKind = FunctionalExpressionResultKind::Integral;
  sample.bitWidth = 1;
  sample.signedness = CoverageSignedness::Unsigned;
  sample.evaluationPhase = FunctionalExpressionEvaluationPhase::Sample;
  schema.functionalExpressions.push_back(sample);
  std::sort(
      schema.functionalExpressions.begin(), schema.functionalExpressions.end(),
      [](const auto &left, const auto &right) { return left.id < right.id; });

  schema.transitionPrograms.push_back({30, 20, 0, alternativeCount, 0});
  schema.transitionPrograms.push_back({31, 20, alternativeCount, 1, 0});
  schema.transitionAlternatives.reserve(alternativeCount + 1);
  schema.transitionSteps.reserve((uint64_t{alternativeCount} + 1) * 2);
  for (uint32_t ordinal = 0; ordinal != alternativeCount; ++ordinal) {
    uint32_t firstStep = ordinal * 2;
    uint64_t firstSet = 100 + firstStep;
    uint64_t secondSet = firstSet + 1;
    schema.transitionAlternatives.push_back(
        {30, secondSet, firstStep, 2, ordinal, 0});
    schema.transitionSteps.push_back({30, firstSet, 0, 0, 1, 1, ordinal, 0,
                                      TransitionRepetitionKind::Once, 0});
    schema.transitionSteps.push_back({30, secondSet, 0, 0, 1, 1, ordinal, 1,
                                      TransitionRepetitionKind::Once, 0});
  }
  uint64_t ignoredFirstSet = 100 + ignoredFirstOrdinal;
  uint64_t ignoredSecondSet = ignoredFirstSet + 1;
  schema.transitionAlternatives.push_back(
      {31, ignoredSecondSet, alternativeCount * 2, 2, 0, 0});
  schema.transitionSteps.push_back({31, ignoredFirstSet, 0, 0, 1, 1, 0, 0,
                                    TransitionRepetitionKind::Once, 0});
  schema.transitionSteps.push_back({31, ignoredSecondSet, 0, 0, 1, 1, 0, 1,
                                    TransitionRepetitionKind::Once, 0});
  schema.schemaFingerprint = computeSchemaFingerprint(schema);

  obelisk_rt_context *context = createContextForSchema(schema, false);
  ASSERT_NE(context, nullptr);
  ASSERT_EQ(finalizeCoverage(context, 0, 0, nullptr, nullptr),
            OBELISK_RT_OK);
  std::vector<uint8_t> values(stepCount + 1);
  std::vector<obelisk_rt_functional_value_v1> expressions(stepCount + 1);
  for (uint32_t ordinal = 0; ordinal != expressions.size(); ++ordinal)
    expressions[ordinal] = {ordinal == stepCount ? 200000 : 100000 + ordinal,
                            1,
                            1,
                            &values[ordinal],
                            nullptr,
                            nullptr,
                            0,
                            OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL,
                            0};
  values[stepCount] = 1;
  obelisk_rt_covergroup_v1 handle = 0;
  EXPECT_EQ(obelisk_rt_v1_covergroup_create(context, 10, nullptr, 0,
                                            expressions.data(),
                                            expressions.size(), &handle),
            OBELISK_RT_OUT_OF_RESOURCES);
  EXPECT_EQ(handle, 0u);
  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeDesignTest,
     CompilerShapedDeferredSingletonFourStateAndRangeRoundTrip) {
  using namespace obelisk::coverage;
  Database schema = makeFunctionalSchema();
  for (auto &set : schema.functionalValueSets)
    set.flags = FunctionalValueSetNeedsResolution;
  schema.functionalValueLimbs.clear();
  for (size_t index = 0; index != schema.functionalValueAtoms.size(); ++index) {
    auto &atom = schema.functionalValueAtoms[index];
    atom.firstLimb = 0;
    atom.limbCount = 0;
    atom.lowerExpression = 70 + index;
  }
  // The second coverpoint's compiler-shaped atom is a constructor-resolved
  // range rather than the base fixture's literal singleton.
  schema.functionalValueAtoms[2].kind = FunctionalValueAtomKind::IntegralRange;
  schema.functionalValueAtoms[2].lowerExpression = 72;
  schema.functionalValueAtoms[2].upperExpression = 73;

  auto addConstructorExpression = [&](uint64_t id, uint64_t owner,
                                      FunctionalExpressionRole role,
                                      uint32_t ordinal) {
    FunctionalExpression expression = schema.functionalExpressions.front();
    expression.id = id;
    expression.owner = owner;
    expression.ownerKind = FunctionalExpressionOwnerKind::ValueSet;
    expression.role = role;
    expression.evaluationPhase =
        FunctionalExpressionEvaluationPhase::Constructor;
    expression.ownerOrdinal = 0;
    expression.ownerSubordinal = 0;
    expression.resultOrdinal = ordinal;
    schema.functionalExpressions.push_back(expression);
  };
  addConstructorExpression(70, 50, FunctionalExpressionRole::ValueAtomSingleton,
                           0);
  addConstructorExpression(71, 51, FunctionalExpressionRole::ValueAtomSingleton,
                           1);
  addConstructorExpression(72, 52, FunctionalExpressionRole::ValueAtomLower, 2);
  addConstructorExpression(73, 52, FunctionalExpressionRole::ValueAtomUpper, 3);
  schema.schemaFingerprint = computeSchemaFingerprint(schema);

  obelisk_rt_context *context = createContextForSchema(schema, false);
  ASSERT_NE(context, nullptr);
  ASSERT_EQ(finalizeCoverage(context, 0, 0, nullptr, nullptr),
            OBELISK_RT_OK);
  uint8_t zero = 0;
  uint8_t xEncoded = 1, xUnknown = 1;
  uint8_t low = 1, high = 2;
  std::array<obelisk_rt_functional_value_v1, 4> constructorValues{};
  constructorValues[0] = {
      70,      2,       1, &zero,
      nullptr, nullptr, 0, OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL,
      0};
  constructorValues[1] = {
      71,        2,       1, &xEncoded,
      &xUnknown, nullptr, 0, OBELISK_RT_FUNCTIONAL_VALUE_FOUR_STATE,
      0};
  constructorValues[2] = {
      72, 2, 1, &low, nullptr, nullptr, 0, OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL,
      0};
  constructorValues[3] = {
      73,      2,       1, &high,
      nullptr, nullptr, 0, OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL,
      0};
  obelisk_rt_covergroup_v1 handle = 0;
  ASSERT_EQ(obelisk_rt_v1_covergroup_create(context, 10, nullptr, 0,
                                            constructorValues.data(),
                                            constructorValues.size(), &handle),
            OBELISK_RT_OK);
  uint8_t second = 2;
  std::array<obelisk_rt_functional_value_v1, 2> samples{};
  samples[0] = {60,        2,       1, &xEncoded,
                &xUnknown, nullptr, 0, OBELISK_RT_FUNCTIONAL_VALUE_FOUR_STATE,
                0};
  samples[1] = {61,      2,       1, &second,
                nullptr, nullptr, 0, OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL,
                0};
  ASSERT_EQ(obelisk_rt_v1_covergroup_sample(context, handle, samples.data(),
                                            samples.size()),
            OBELISK_RT_OK);

  Database saved = saveCoverage(context, "compiler-shaped");
  ASSERT_EQ(validate(saved), Status::Ok);
  auto count = [&](uint64_t templateBin) {
    auto bin =
        std::find_if(saved.resolvedFunctionalBins.begin(),
                     saved.resolvedFunctionalBins.end(), [&](const auto &row) {
                       return row.templateBin == templateBin;
                     });
    EXPECT_NE(bin, saved.resolvedFunctionalBins.end());
    if (bin == saved.resolvedFunctionalBins.end())
      return uint64_t{0};
    auto counter = std::find_if(
        saved.counters.begin(), saved.counters.end(), [&](const auto &entry) {
          return entry.metric == MetricKind::Functional &&
                 entry.instance == handle && entry.entity == bin->id;
        });
    EXPECT_NE(counter, saved.counters.end());
    return counter == saved.counters.end() ? uint64_t{0} : counter->value;
  };
  // IEEE 1800-2017 19.5.7 excludes a non-wildcard singleton whose original
  // value contains X or Z from the resolved bin values.
  EXPECT_EQ(count(31), 0u);
  EXPECT_EQ(count(40), 1u);
  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeDesignTest,
     LateDefaultAndAutomaticGroupsPreserveDeclarationOrder) {
  using namespace obelisk::coverage;
  Database schema = makeFunctionalSchema();
  schema.functionalItems[1].ordinal = 3;
  schema.functionalBins[0].ordinal = 2;
  schema.functionalBins[1].ordinal = 3;
  for (uint64_t bin : {uint64_t{32}, uint64_t{33}}) {
    const std::string name = bin == 32 ? "other_a" : "other_b";
    schema.functionalBins.push_back(
        {bin, 20, name, FunctionalBinKind::State, FunctionalBinDefault, 1,
         static_cast<uint32_t>(bin - 32), "top.cg.first." + name});
    FunctionalBinPlan plan;
    plan.bin = bin;
    plan.arrayMode = FunctionalBinArrayMode::Unsized;
    plan.distribution = FunctionalBinDistributionKind::PerValue;
    schema.functionalBinPlans.push_back(plan);
  }
  for (uint64_t item : {uint64_t{22}, uint64_t{23}}) {
    const std::string name = item == 22 ? "automatic" : "empty_default";
    schema.functionalItems.push_back(
        {item, 10, name, FunctionalItemKind::Coverpoint, 0, 100, 1,
         static_cast<uint32_t>(item - 21), "top.cg." + name});
    auto sample = schema.functionalExpressions.front();
    sample.id = item + 40;
    sample.owner = item;
    sample.resultOrdinal = static_cast<uint32_t>(item - 21);
    schema.functionalExpressions.push_back(sample);
  }
  schema.functionalExpressions[1].resultOrdinal = 3;
  schema.functionalBins.push_back({34, 23, "other", FunctionalBinKind::State,
                                   FunctionalBinDefault, 1, 0,
                                   "top.cg.empty_default.other"});
  FunctionalBinPlan emptyDefault;
  emptyDefault.bin = 34;
  emptyDefault.arrayMode = FunctionalBinArrayMode::Unsized;
  emptyDefault.distribution = FunctionalBinDistributionKind::PerValue;
  schema.functionalBinPlans.push_back(emptyDefault);
  schema.functionalBins.push_back({35, 23, "all", FunctionalBinKind::State, 0,
                                   1, 1, "top.cg.empty_default.all"});
  schema.functionalBinPlans.push_back({35, 53});
  schema.functionalValueSets.push_back({53, 23, 3, 1, 2,
                                        FunctionalValueSetKind::Integral, 0,
                                        CoverageSignedness::Unsigned, 0});
  schema.functionalValueAtoms.push_back(
      {53, 0, 0, 3, 1, 0, FunctionalValueAtomKind::IntegralRange,
       FunctionalValueAtomLowerInclusive | FunctionalValueAtomUpperInclusive, 0,
       0});
  schema.functionalValueLimbs.push_back({53, 0, 0, 0, 0, 3, 0, 0});
  std::sort(
      schema.functionalBins.begin(), schema.functionalBins.end(),
      [](const auto &left, const auto &right) { return left.id < right.id; });
  std::sort(
      schema.functionalBinPlans.begin(), schema.functionalBinPlans.end(),
      [](const auto &left, const auto &right) { return left.bin < right.bin; });
  obelisk_rt_context *context = createContextForSchema(schema);
  ASSERT_NE(context, nullptr);
  ASSERT_EQ(finalizeCoverage(context, 0, 0, nullptr, nullptr), OBELISK_RT_OK);
  obelisk_rt_covergroup_v1 first = 0, second = 0;
  ASSERT_EQ(createFunctional(context, 10, &first), OBELISK_RT_OK);
  ASSERT_EQ(createFunctional(context, 10, &second), OBELISK_RT_OK);
  EXPECT_NE(first, second);
  Database saved = saveCoverage(context, "late-bin-groups");
  obelisk_rt_v1_context_destroy(context);
  ASSERT_EQ(validate(saved), Status::Ok);
  ASSERT_EQ(saved.functionalConfigurations.size(), 1u);
  ASSERT_EQ(saved.resolvedFunctionalBins.size(), 12u);
  ASSERT_EQ(saved.resolvedFunctionalBinGroups.size(), 8u);
  const std::array<std::array<uint64_t, 3>, 8> groups = {{{32, 0, 2},
                                                          {33, 2, 2},
                                                          {30, 4, 1},
                                                          {31, 5, 1},
                                                          {0, 6, 4},
                                                          {34, 10, 0},
                                                          {35, 10, 1},
                                                          {40, 11, 1}}};
  for (size_t index = 0; index != groups.size(); ++index) {
    const auto &group = saved.resolvedFunctionalBinGroups[index];
    EXPECT_EQ(group.templateBin, groups[index][0]);
    EXPECT_EQ(group.firstBin, groups[index][1]);
    EXPECT_EQ(group.binCount, groups[index][2]);
    for (uint32_t offset = 0; offset != group.binCount; ++offset)
      EXPECT_EQ(saved.resolvedFunctionalBins[group.firstBin + offset].item,
                group.item);
  }
  const std::array<const char *, 12> names = {
      "other_a[2]", "other_a[3]", "other_b[2]", "other_b[3]", "zero", "one",
      "auto[0]",    "auto[1]",    "auto[2]",    "auto[3]",    "all",  "two"};
  const std::array<uint32_t, 12> ordinals = {0, 1, 2, 3, 4, 5,
                                             0, 1, 2, 3, 0, 0};
  for (size_t index = 0; index != names.size(); ++index) {
    EXPECT_EQ(saved.resolvedFunctionalBins[index].name, names[index]);
    EXPECT_EQ(saved.resolvedFunctionalBins[index].ordinal, ordinals[index]);
  }
}

TEST_F(CoverageRuntimeDesignTest, DefaultBinCatchesButNeverEntersDenominator) {
  using namespace obelisk::coverage;
  Database schema = makeFunctionalSchema();
  schema.functionalBins.push_back({32, 20, "other", FunctionalBinKind::State,
                                   FunctionalBinDefault, 1, 2,
                                   "top.cg.first.other"});
  schema.functionalBinPlans.push_back({32, 0});
  std::sort(schema.functionalBins.begin(), schema.functionalBins.end(),
            [](const auto &a, const auto &b) { return a.id < b.id; });
  std::sort(schema.functionalBinPlans.begin(), schema.functionalBinPlans.end(),
            [](const auto &a, const auto &b) { return a.bin < b.bin; });
  schema.schemaFingerprint = computeSchemaFingerprint(schema);
  obelisk_rt_context *context = createContextForSchema(schema, false);
  ASSERT_NE(context, nullptr);
  ASSERT_EQ(finalizeCoverage(context, 0, 0, nullptr, nullptr),
            OBELISK_RT_OK);
  obelisk_rt_covergroup_v1 handle = 0;
  ASSERT_EQ(createFunctional(context, 10, &handle), OBELISK_RT_OK);
  ASSERT_EQ(sampleFunctional(context, handle, 3, 2), OBELISK_RT_OK);
  double percentage = 0.0;
  int32_t covered = 0, total = 0;
  ASSERT_EQ(obelisk_rt_v1_covergroup_instance_query(
                context, handle, 0, &percentage, &covered, &total),
            OBELISK_RT_OK);
  EXPECT_EQ(covered, 1);
  EXPECT_EQ(total, 3);
  EXPECT_DOUBLE_EQ(percentage, 50.0);
  Database saved = saveCoverage(context, "default");
  auto resolved = std::find_if(
      saved.resolvedFunctionalBins.begin(), saved.resolvedFunctionalBins.end(),
      [](const auto &bin) { return bin.templateBin == 32; });
  ASSERT_NE(resolved, saved.resolvedFunctionalBins.end());
  auto counter = std::find_if(
      saved.counters.begin(), saved.counters.end(), [&](const auto &entry) {
        return entry.metric == MetricKind::Functional &&
               entry.instance == handle && entry.entity == resolved->id;
      });
  ASSERT_NE(counter, saved.counters.end());
  EXPECT_EQ(counter->value, 1u);
  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeDesignTest,
     FourStateExactWildcardAndIffUseCanonicalLogicPlanes) {
  using namespace obelisk::coverage;
  Database schema = makeFunctionalSchema();
  constexpr uint32_t inclusive =
      FunctionalValueAtomLowerInclusive | FunctionalValueAtomUpperInclusive;

  // Replace the first two bins with exact X, exact Z, and wildcard 1?. The
  // runtime !simulation.logic transport is (aval ^ bval, bval), whereas the
  // schema stores canonical (aval, bval).
  schema.functionalBins[0].name = "exact_x";
  schema.functionalBins[0].hierarchy = "top.cg.first.exact_x";
  schema.functionalValueLimbs[0] = {50, 0, 0, 0, 1, 0, 1, 0};
  schema.functionalBins[1].name = "wildcard";
  schema.functionalBins[1].hierarchy = "top.cg.first.wildcard";
  schema.functionalBins[1].flags = FunctionalBinWildcard;
  schema.functionalValueLimbs[1] = {51, 0, 0, 2, 1, 2, 1, 1};
  schema.functionalBins.push_back({32, 20, "exact_z", FunctionalBinKind::State,
                                   0, 1, 2, "top.cg.first.exact_z"});
  schema.functionalValueSets.push_back({53, 20, 3, 1, 2,
                                        FunctionalValueSetKind::Integral, 0,
                                        CoverageSignedness::Unsigned, 0});
  schema.functionalValueAtoms.push_back({53, 0, 0, 3, 1, 0,
                                         FunctionalValueAtomKind::IntegralValue,
                                         inclusive, 0, 0});
  schema.functionalValueLimbs.push_back({53, 0, 0, 1, 1, 1, 1, 0});
  schema.functionalBinPlans.push_back({32, 53});
  std::sort(schema.functionalBins.begin(), schema.functionalBins.end(),
            [](const auto &a, const auto &b) { return a.id < b.id; });
  std::sort(schema.functionalBinPlans.begin(), schema.functionalBinPlans.end(),
            [](const auto &a, const auto &b) { return a.bin < b.bin; });

  FunctionalExpression iff = schema.functionalExpressions.front();
  iff.id = 62;
  iff.role = FunctionalExpressionRole::CoverpointIff;
  iff.resultKind = FunctionalExpressionResultKind::Boolean;
  iff.bitWidth = 0;
  iff.signedness = CoverageSignedness::NotApplicable;
  iff.resultOrdinal = 2;
  schema.functionalExpressions.push_back(iff);
  FunctionalExpression binIff = iff;
  binIff.id = 63;
  binIff.owner = 31;
  binIff.ownerKind = FunctionalExpressionOwnerKind::Bin;
  binIff.role = FunctionalExpressionRole::BinIff;
  binIff.resultOrdinal = 3;
  schema.functionalExpressions.push_back(binIff);
  auto wildcardPlan = std::find_if(
      schema.functionalBinPlans.begin(), schema.functionalBinPlans.end(),
      [](const auto &plan) { return plan.bin == 31; });
  ASSERT_NE(wildcardPlan, schema.functionalBinPlans.end());
  wildcardPlan->iffExpression = 63;
  schema.schemaFingerprint = computeSchemaFingerprint(schema);

  obelisk_rt_context *context = createContextForSchema(schema, false);
  ASSERT_NE(context, nullptr);
  ASSERT_EQ(finalizeCoverage(context, 0, 0, nullptr, nullptr),
            OBELISK_RT_OK);
  obelisk_rt_covergroup_v1 handle = 0;
  ASSERT_EQ(createFunctional(context, 10, &handle), OBELISK_RT_OK);
  auto sample = [&](uint8_t value, uint8_t unknown, uint8_t pointIffValue,
                    uint8_t pointIffUnknown, uint8_t binIffValue,
                    uint8_t binIffUnknown) {
    uint8_t second = 2;
    std::array<obelisk_rt_functional_value_v1, 4> values{};
    values[0] = {60,       2,       1, &value,
                 &unknown, nullptr, 0, OBELISK_RT_FUNCTIONAL_VALUE_FOUR_STATE,
                 0};
    values[1] = {61,      2,       1, &second,
                 nullptr, nullptr, 0, OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL,
                 0};
    values[2] = {62,
                 1,
                 1,
                 &pointIffValue,
                 &pointIffUnknown,
                 nullptr,
                 0,
                 OBELISK_RT_FUNCTIONAL_VALUE_FOUR_STATE,
                 0};
    values[3] = {63,
                 1,
                 1,
                 &binIffValue,
                 &binIffUnknown,
                 nullptr,
                 0,
                 OBELISK_RT_FUNCTIONAL_VALUE_FOUR_STATE,
                 0};
    return obelisk_rt_v1_covergroup_sample(context, handle, values.data(),
                                           values.size());
  };
  uint8_t knownOne = 1;
  uint8_t known = 0;
  // X is (encoded=1,bval=1), Z is (encoded=0,bval=1).
  ASSERT_EQ(sample(1, 1, knownOne, known, knownOne, known), OBELISK_RT_OK);
  ASSERT_EQ(sample(0, 1, knownOne, known, knownOne, known), OBELISK_RT_OK);
  // The sampled X is in the wildcarded low bit and therefore does not match.
  ASSERT_EQ(sample(3, 1, knownOne, known, knownOne, known), OBELISK_RT_OK);
  // X and Z iff results are both false, even though their encoded low bits
  // differ. Neither may admit the otherwise matching known value 2'b11.
  ASSERT_EQ(sample(3, 0, 1, 1, knownOne, known), OBELISK_RT_OK);
  ASSERT_EQ(sample(3, 0, 0, 1, knownOne, known), OBELISK_RT_OK);
  ASSERT_EQ(sample(3, 0, knownOne, known, 1, 1), OBELISK_RT_OK);
  ASSERT_EQ(sample(3, 0, knownOne, known, 0, 1), OBELISK_RT_OK);
  ASSERT_EQ(sample(3, 0, knownOne, known, knownOne, known), OBELISK_RT_OK);

  Database saved = saveCoverage(context, "four-state");
  auto count = [&](uint64_t templateBin) {
    auto bin =
        std::find_if(saved.resolvedFunctionalBins.begin(),
                     saved.resolvedFunctionalBins.end(), [&](const auto &row) {
                       return row.templateBin == templateBin;
                     });
    EXPECT_NE(bin, saved.resolvedFunctionalBins.end());
    if (bin == saved.resolvedFunctionalBins.end())
      return uint64_t{0};
    auto counter = std::find_if(
        saved.counters.begin(), saved.counters.end(), [&](const auto &entry) {
          return entry.metric == MetricKind::Functional &&
                 entry.instance == handle && entry.entity == bin->id;
        });
    EXPECT_NE(counter, saved.counters.end());
    return counter == saved.counters.end() ? uint64_t{0} : counter->value;
  };
  EXPECT_EQ(count(30), 1u);
  EXPECT_EQ(count(31), 1u);
  EXPECT_EQ(count(32), 1u);
  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeDesignTest,
       NumericExclusionUnionsCoverWideSignedPartitionsAndKeepOutsideValues) {
  using namespace obelisk::coverage;
  Database schema = makeFunctionalSchema();
  schema.functionalItems.resize(1);
  schema.functionalBins.clear();
  schema.functionalValueSets.clear();
  schema.functionalValueAtoms.clear();
  schema.functionalValueLimbs.clear();
  schema.functionalBinPlans.clear();
  schema.functionalExpressions.resize(1);
  schema.functionalExpressions[0].bitWidth = 65;
  schema.functionalExpressions[0].signedness = CoverageSignedness::Signed;
  constexpr uint32_t inclusive =
      FunctionalValueAtomLowerInclusive | FunctionalValueAtomUpperInclusive;
  auto addBin = [&](uint64_t id, int64_t low, int64_t high, uint32_t flags) {
    uint64_t set = 5000 + id;
    uint32_t ordinal = schema.functionalBins.size();
    uint32_t atom = schema.functionalValueAtoms.size();
    uint32_t limb = schema.functionalValueLimbs.size();
    std::string name = "bin" + std::to_string(id);
    schema.functionalBins.push_back({id, 20, name, FunctionalBinKind::State,
                                     flags, 1, ordinal,
                                     "top.cg.first." + name});
    schema.functionalBinPlans.push_back({id, set});
    schema.functionalValueSets.push_back({set, 20, atom, 1, 65,
                                          FunctionalValueSetKind::Integral, 0,
                                          CoverageSignedness::Signed, 0});
    schema.functionalValueAtoms.push_back(
        {set, 0, 0, limb, 2, 0,
         low == high ? FunctionalValueAtomKind::IntegralValue
                     : FunctionalValueAtomKind::IntegralRange,
         inclusive, 0, 0});
    schema.functionalValueLimbs.push_back({set, 0, 0,
                                           static_cast<uint64_t>(low), 0,
                                           static_cast<uint64_t>(high), 0, 0});
    schema.functionalValueLimbs.push_back(
        {set, 0, 1, low < 0 ? 1u : 0u, 0, high < 0 ? 1u : 0u, 0, 0});
  };
  addBin(30, -16, 15, 0);
  addBin(31, -16, 16, 0);
  for (uint32_t ordinal = 0; ordinal != 32; ++ordinal)
    addBin(32 + ordinal, int64_t{ordinal} - 16, int64_t{ordinal} - 16, 0);
  // Reverse endpoint order exercises radix ordering before the merge/sweep.
  for (uint32_t ordinal = 0; ordinal != 32; ++ordinal)
    addBin(1000 + ordinal, 15 - int64_t{ordinal}, 15 - int64_t{ordinal},
           FunctionalBinIgnore);
  schema.schemaFingerprint = computeSchemaFingerprint(schema);
  obelisk_rt_context *context = createContextForSchema(schema, false);
  ASSERT_NE(context, nullptr);
  ASSERT_EQ(finalizeCoverage(context, 0, 0, nullptr, nullptr), OBELISK_RT_OK);
  obelisk_rt_covergroup_v1 handle = 0;
  ASSERT_EQ(createFunctional(context, 10, &handle), OBELISK_RT_OK);
  for (const auto &bin : context->coverage->instances.at(handle).bins)
    if (!(bin.flags & FunctionalBinIgnore))
      EXPECT_EQ(bool(bin.flags & FunctionalBinEmpty), bin.templateBin != 31);
  double percentage = 0;
  int32_t covered = 0, total = 0;
  ASSERT_EQ(obelisk_rt_v1_covergroup_instance_query(
                context, handle, 20, &percentage, &covered, &total),
            OBELISK_RT_OK);
  EXPECT_EQ(covered, 0);
  EXPECT_EQ(total, 1);
  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeDesignTest,
       RealExclusionUnionsRespectPrecisionAdjacencyOpenBoundsAndSignedZero) {
  using namespace obelisk::coverage;
  constexpr uint32_t lower = FunctionalValueAtomLowerInclusive;
  constexpr uint32_t upper = FunctionalValueAtomUpperInclusive;
  constexpr uint32_t both = lower | upper;
  struct Case {
    double low, high, leftLow, leftHigh, rightLow, rightHigh;
    uint32_t leftFlags, rightFlags;
    bool realEmpty, shortEmpty;
  };
  double nextFloat =
      std::nextafter(1.0f, std::numeric_limits<float>::infinity());
  const std::array<Case, 7> cases{
      {{0, 4, 0, 2, 2, 4, both, both, true, true},
       {0, 4, 0, 2, 2, 4, lower, both, true, true},
       {0, 4, 0, 2, 2, 4, lower, upper, false, false},
       {0, 2, 0, 1, nextFloat, 2, both, both, false, true},
       {-1, 1, -1, -0.0, +0.0, 1, both, upper, true, true},
       {-1, 1, -1, -0.0, +0.0, 1, lower, upper, false, false},
       {-1, 1, 0, 0, 0, 0, FunctionalValueAtomLowerUnbounded | both,
        FunctionalValueAtomUpperUnbounded | upper, true, true}}};
  for (bool shortReal : {false, true}) {
    for (const Case &test : cases) {
      SCOPED_TRACE(::testing::Message() << "shortreal=" << shortReal
                                        << " left_high=" << test.leftHigh
                                        << " right_low=" << test.rightLow
                                        << " left_flags=" << test.leftFlags
                                        << " right_flags=" << test.rightFlags);
      Database schema = makeFunctionalSchema();
      schema.functionalItems.resize(1);
      schema.functionalBins.clear();
      schema.functionalValueSets.clear();
      schema.functionalValueAtoms.clear();
      schema.functionalValueLimbs.clear();
      schema.functionalBinPlans.clear();
      schema.functionalExpressions.resize(1);
      auto &sample = schema.functionalExpressions.front();
      sample.resultKind = shortReal ? FunctionalExpressionResultKind::ShortReal
                                    : FunctionalExpressionResultKind::Real;
      sample.bitWidth = 0;
      sample.signedness = CoverageSignedness::NotApplicable;
      auto addBin = [&](double low, double high, uint32_t flags,
                        uint32_t atomFlags) {
        uint32_t ordinal = schema.functionalBins.size();
        uint64_t bin = 30 + ordinal, set = 50 + ordinal;
        std::string name = "bin" + std::to_string(bin);
        schema.functionalBins.push_back({bin, 20, name,
                                         FunctionalBinKind::State, flags, 1,
                                         ordinal, "top.cg.first." + name});
        uint32_t setFlags = atomFlags & (FunctionalValueAtomLowerUnbounded |
                                         FunctionalValueAtomUpperUnbounded)
                                ? FunctionalValueSetNeedsResolution
                                : 0;
        schema.functionalValueSets.push_back(
            {set, 20, ordinal, 1, 64, FunctionalValueSetKind::Real, setFlags,
             CoverageSignedness::NotApplicable, 0});
        uint64_t lowBits, highBits;
        std::memcpy(&lowBits, &low, sizeof(low));
        std::memcpy(&highBits, &high, sizeof(high));
        uint64_t lowerExpression = 0, upperExpression = 0;
        if (setFlags) {
          auto bound = schema.functionalExpressions.front();
          bound.id = 100 + ordinal;
          bound.owner = set;
          bound.ownerKind = FunctionalExpressionOwnerKind::ValueSet;
          bound.role = atomFlags & FunctionalValueAtomLowerUnbounded
                           ? FunctionalExpressionRole::ValueAtomUpper
                           : FunctionalExpressionRole::ValueAtomLower;
          bound.resultKind = FunctionalExpressionResultKind::Real;
          bound.evaluationPhase =
              FunctionalExpressionEvaluationPhase::Constructor;
          bound.resultOrdinal = ordinal - 1;
          if (atomFlags & FunctionalValueAtomLowerUnbounded)
            upperExpression = bound.id;
          else
            lowerExpression = bound.id;
          schema.functionalExpressions.push_back(bound);
        }
        schema.functionalValueAtoms.push_back(
            {set, lowBits, highBits, 0, 0, 0,
             FunctionalValueAtomKind::RealInterval,
             atomFlags | FunctionalValueAtomRealRange, lowerExpression,
             upperExpression});
        uint64_t iff = 0;
        if (flags) {
          auto guard = schema.functionalExpressions.front();
          iff = guard.id = 60 + ordinal;
          guard.owner = bin;
          guard.ownerKind = FunctionalExpressionOwnerKind::Bin;
          guard.role = FunctionalExpressionRole::BinIff;
          guard.resultKind = FunctionalExpressionResultKind::Boolean;
          guard.resultOrdinal = ordinal;
          schema.functionalExpressions.push_back(guard);
        }
        schema.functionalBinPlans.push_back({bin, set, iff});
      };
      addBin(test.low, test.high, 0, both);
      addBin(test.leftLow, test.leftHigh, FunctionalBinIgnore, test.leftFlags);
      addBin(test.rightLow, test.rightHigh, FunctionalBinIllegal,
             test.rightFlags);
      detail::sortByKey(schema.functionalExpressions.begin(),
                        schema.functionalExpressions.end(),
                        [](const auto &expression) { return expression.id; });
      schema.schemaFingerprint = computeSchemaFingerprint(schema);
      obelisk_rt_context *context = createContextForSchema(schema, false);
      ASSERT_NE(context, nullptr);
      ASSERT_EQ(finalizeCoverage(context, 0, 0, nullptr, nullptr),
                OBELISK_RT_OK);
      obelisk_rt_covergroup_v1 handle = 0;
      if (test.leftFlags & FunctionalValueAtomLowerUnbounded) {
        double zero = 0;
        std::array<obelisk_rt_functional_value_v1, 2> bounds{
            {{101, 64, sizeof(zero), &zero, nullptr, nullptr, 0,
              OBELISK_RT_FUNCTIONAL_VALUE_REAL, 0},
             {102, 64, sizeof(zero), &zero, nullptr, nullptr, 0,
              OBELISK_RT_FUNCTIONAL_VALUE_REAL, 0}}};
        ASSERT_EQ(obelisk_rt_v1_covergroup_create(context, 10, nullptr, 0,
                                                  bounds.data(), bounds.size(),
                                                  &handle),
                  OBELISK_RT_OK);
      } else
        ASSERT_EQ(createFunctional(context, 10, &handle), OBELISK_RT_OK);
      const auto &bins = context->coverage->instances.at(handle).bins;
      bool empty = shortReal ? test.shortEmpty : test.realEmpty;
      EXPECT_EQ(bool(bins.front().flags & FunctionalBinEmpty), empty);
      double percentage = 0;
      int32_t covered = 0, total = 0;
      ASSERT_EQ(obelisk_rt_v1_covergroup_instance_query(
                    context, handle, 20, &percentage, &covered, &total),
                OBELISK_RT_OK);
      EXPECT_EQ(covered, 0);
      EXPECT_EQ(total, empty ? 0 : 1);
      obelisk_rt_v1_context_destroy(context);
    }
  }
}

TEST_F(CoverageRuntimeDesignTest,
       GuardedStateExclusionsRemoveValuesAndOnlyGateIllegalDiagnostics) {
  using namespace obelisk::coverage;
  constexpr uint32_t inclusive =
      FunctionalValueAtomLowerInclusive | FunctionalValueAtomUpperInclusive;
  for (bool fullyExcluded : {false, true}) {
    Database schema = makeFunctionalSchema();
    if (!fullyExcluded) {
      schema.functionalValueAtoms[0].kind =
          FunctionalValueAtomKind::IntegralRange;
      schema.functionalValueLimbs[0].highAval = 2;
    }
    for (auto [bin, set, value, flag, expressionID, ordinal] :
         {std::tuple{uint64_t{32}, uint64_t{53}, uint64_t{0},
                     FunctionalBinIgnore, uint64_t{62}, uint32_t{2}},
          std::tuple{uint64_t{33}, uint64_t{54}, uint64_t{1},
                     FunctionalBinIllegal, uint64_t{63}, uint32_t{3}}}) {
      std::string name = flag == FunctionalBinIgnore ? "ignored" : "illegal";
      schema.functionalBins.push_back({bin, 20, name, FunctionalBinKind::State,
                                       flag, 1, ordinal,
                                       "top.cg.first." + name});
      uint32_t atomIndex = schema.functionalValueAtoms.size();
      uint32_t limbIndex = schema.functionalValueLimbs.size();
      schema.functionalValueSets.push_back({set, 20, atomIndex, 1, 2,
                                            FunctionalValueSetKind::Integral, 0,
                                            CoverageSignedness::Unsigned, 0});
      schema.functionalValueAtoms.push_back(
          {set, 0, 0, limbIndex, 1, 0, FunctionalValueAtomKind::IntegralValue,
           inclusive, 0, 0});
      schema.functionalValueLimbs.push_back({set, 0, 0, value, 0, value, 0, 0});
      schema.functionalBinPlans.push_back({bin, set, expressionID});
      auto guard = schema.functionalExpressions.front();
      guard.id = expressionID;
      guard.owner = bin;
      guard.ownerKind = FunctionalExpressionOwnerKind::Bin;
      guard.role = FunctionalExpressionRole::BinIff;
      guard.resultKind = FunctionalExpressionResultKind::Boolean;
      guard.bitWidth = 0;
      guard.signedness = CoverageSignedness::NotApplicable;
      guard.resultOrdinal = ordinal;
      schema.functionalExpressions.push_back(guard);
    }
    detail::sortByKey(schema.functionalBins.begin(),
                      schema.functionalBins.end(),
                      [](const auto &bin) { return bin.id; });
    detail::sortByKey(schema.functionalBinPlans.begin(),
                      schema.functionalBinPlans.end(),
                      [](const auto &plan) { return plan.bin; });
    schema.schemaFingerprint = computeSchemaFingerprint(schema);
    obelisk_rt_context *context = createContextForSchema(schema, false);
    ASSERT_NE(context, nullptr);
    ASSERT_EQ(finalizeCoverage(context, 0, 0, nullptr, nullptr), OBELISK_RT_OK);
    obelisk_rt_covergroup_v1 handle = 0;
    ASSERT_EQ(createFunctional(context, 10, &handle), OBELISK_RT_OK);
    auto sample = [&](uint8_t value, uint8_t guard, uint8_t unknown = 0) {
      uint8_t second = 2;
      std::array<obelisk_rt_functional_value_v1, 4> values{
          {{60, 2, 1, &value, nullptr, nullptr, 0,
            OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL, 0},
           {61, 2, 1, &second, nullptr, nullptr, 0,
            OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL, 0},
           {62, 1, 1, &guard, &unknown, nullptr, 0,
            OBELISK_RT_FUNCTIONAL_VALUE_FOUR_STATE, 0},
           {63, 1, 1, &guard, &unknown, nullptr, 0,
            OBELISK_RT_FUNCTIONAL_VALUE_FOUR_STATE, 0}}};
      return obelisk_rt_v1_covergroup_sample(context, handle, values.data(),
                                             values.size());
    };
    const auto &bins = context->coverage->instances.at(handle).bins;
    EXPECT_EQ(bool(bins[0].flags & FunctionalBinEmpty), fullyExcluded);
    EXPECT_TRUE(bins[1].flags & FunctionalBinEmpty);
    for (uint8_t value : {0, 1}) {
      ASSERT_EQ(sample(value, 0), OBELISK_RT_OK);
      ASSERT_EQ(sample(value, 1, 1), OBELISK_RT_OK);
      ASSERT_EQ(sample(value, 0, 1), OBELISK_RT_OK);
    }
    EXPECT_EQ(bins[0].count, 0u);
    EXPECT_EQ(bins[1].count, 0u);
    EXPECT_TRUE(saveCoverage(context, "disabled-state-exclusions")
                    .illegalBinDiagnostics.empty());
    ASSERT_EQ(sample(2, 0), OBELISK_RT_OK);
    EXPECT_EQ(bins[0].count, fullyExcluded ? 0u : 1u);
    double percentage = 0;
    int32_t covered = 0, total = 0;
    ASSERT_EQ(obelisk_rt_v1_covergroup_instance_query(
                  context, handle, 20, &percentage, &covered, &total),
              OBELISK_RT_OK);
    EXPECT_EQ(covered, fullyExcluded ? 0 : 1);
    EXPECT_EQ(total, fullyExcluded ? 0 : 1);
    ASSERT_EQ(sample(1, 1), OBELISK_RT_OK);
    ASSERT_EQ(sample(0, 1), OBELISK_RT_OK);
    EXPECT_EQ(bins[0].count, fullyExcluded ? 0u : 1u);
    Database saved = saveCoverage(context, "enabled-state-exclusions");
    ASSERT_EQ(saved.illegalBinDiagnostics.size(), 1u);
    EXPECT_EQ(saved.illegalBinDiagnostics.front().bin, bins[3].id);
    EXPECT_EQ(saved.illegalBinDiagnostics.front().count, 1u);
    obelisk_rt_v1_context_destroy(context);
  }
}

TEST_F(CoverageRuntimeDesignTest,
     IgnoreIllegalOverlapEmptiesOrdinaryBinAndIllegalWins) {
  using namespace obelisk::coverage;
  Database schema = makeFunctionalSchema();
  constexpr uint32_t inclusive =
      FunctionalValueAtomLowerInclusive | FunctionalValueAtomUpperInclusive;
  schema.functionalBins.push_back(
      {32, 20, "ignored_zero", FunctionalBinKind::State, FunctionalBinIgnore, 1,
       2, "top.cg.first.ignored_zero"});
  schema.functionalBins.push_back(
      {33, 20, "illegal_zero", FunctionalBinKind::State, FunctionalBinIllegal,
       1, 3, "top.cg.first.illegal_zero"});
  schema.functionalValueSets.push_back({53, 20, 3, 1, 2,
                                        FunctionalValueSetKind::Integral, 0,
                                        CoverageSignedness::Unsigned, 0});
  schema.functionalValueSets.push_back({54, 20, 4, 1, 2,
                                        FunctionalValueSetKind::Integral, 0,
                                        CoverageSignedness::Unsigned, 0});
  schema.functionalValueAtoms.push_back({53, 0, 0, 3, 1, 0,
                                         FunctionalValueAtomKind::IntegralValue,
                                         inclusive, 0, 0});
  schema.functionalValueAtoms.push_back({54, 0, 0, 4, 1, 0,
                                         FunctionalValueAtomKind::IntegralValue,
                                         inclusive, 0, 0});
  schema.functionalValueLimbs.push_back({53, 0, 0, 0, 0, 0, 0, 0});
  schema.functionalValueLimbs.push_back({54, 0, 0, 0, 0, 0, 0, 0});
  schema.functionalBinPlans.push_back({32, 53});
  schema.functionalBinPlans.push_back({33, 54});
  std::sort(schema.functionalBins.begin(), schema.functionalBins.end(),
            [](const auto &a, const auto &b) { return a.id < b.id; });
  std::sort(schema.functionalBinPlans.begin(), schema.functionalBinPlans.end(),
            [](const auto &a, const auto &b) { return a.bin < b.bin; });
  schema.schemaFingerprint = computeSchemaFingerprint(schema);

  obelisk_rt_context *context = createContextForSchema(schema, false);
  ASSERT_NE(context, nullptr);
  ASSERT_EQ(finalizeCoverage(context, 0, 0, nullptr, nullptr),
            OBELISK_RT_OK);
  obelisk_rt_covergroup_v1 handle = 0;
  ASSERT_EQ(createFunctional(context, 10, &handle), OBELISK_RT_OK);
  ASSERT_EQ(sampleFunctional(context, handle, 0, 2), OBELISK_RT_OK);

  double percentage = 0.0;
  int32_t covered = 0, total = 0;
  ASSERT_EQ(obelisk_rt_v1_covergroup_instance_query(
                context, handle, 0, &percentage, &covered, &total),
            OBELISK_RT_OK);
  EXPECT_EQ(covered, 1);
  EXPECT_EQ(total, 2);
  EXPECT_DOUBLE_EQ(percentage, 50.0);

  Database saved = saveCoverage(context, "overlap");
  auto ordinary = std::find_if(
      saved.resolvedFunctionalBins.begin(), saved.resolvedFunctionalBins.end(),
      [](const auto &bin) { return bin.templateBin == 30; });
  ASSERT_NE(ordinary, saved.resolvedFunctionalBins.end());
  EXPECT_TRUE(ordinary->flags & FunctionalBinEmpty);
  auto counter = std::find_if(
      saved.counters.begin(), saved.counters.end(), [&](const auto &entry) {
        return entry.metric == MetricKind::Functional &&
               entry.instance == handle && entry.entity == ordinary->id;
      });
  ASSERT_NE(counter, saved.counters.end());
  EXPECT_EQ(counter->value, 0u);
  ASSERT_EQ(saved.illegalBinDiagnostics.size(), 1u);
  auto illegal = std::find_if(
      saved.resolvedFunctionalBins.begin(), saved.resolvedFunctionalBins.end(),
      [](const auto &bin) { return bin.templateBin == 33; });
  ASSERT_NE(illegal, saved.resolvedFunctionalBins.end());
  EXPECT_EQ(saved.illegalBinDiagnostics.front().bin, illegal->id);
  // Repeated hits share a diagnostic only within the same run, bin,
  // instance and simulation time. Live and loaded rows remain independent.
  ASSERT_EQ(sampleFunctional(context, handle, 0, 2), OBELISK_RT_OK);
  Database repeated = saveCoverage(context, "illegal-repeated");
  ASSERT_EQ(repeated.illegalBinDiagnostics.size(), 1u);
  EXPECT_EQ(repeated.illegalBinDiagnostics.front().count, 2u);
  context->schedulerTime = 7;
  ASSERT_EQ(sampleFunctional(context, handle, 0, 2), OBELISK_RT_OK);
  Database beforeReload = saveCoverage(context, "illegal-index");
  ASSERT_EQ(beforeReload.illegalBinDiagnostics.size(), 2u);
  ASSERT_EQ(beforeReload.runs.size(), 1u);
  const UUID liveRun = beforeReload.runs.front().uuid;
  UUID loadedRun = liveRun;
  loadedRun[0] ^= 1;
  beforeReload.runs.front().uuid = loadedRun;
  for (auto &row : beforeReload.resolvedInstances)
    row.run = loadedRun;
  for (auto &row : beforeReload.resolvedInstanceOptions)
    row.run = loadedRun;
  for (auto &row : beforeReload.counters)
    row.run = loadedRun;
  for (auto &row : beforeReload.illegalBinDiagnostics)
    row.run = loadedRun;
  const std::string reloadPath =
      (std::filesystem::temp_directory_path() /
       ("obelisk-illegal-index-" +
        std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) +
        ".obcov"))
          .string();
  ASSERT_EQ(writeFileAtomically(reloadPath, beforeReload), Status::Ok);
  obelisk_rt_status loadStatus = obelisk_rt_v1_coverage_load(
      context, reloadPath.data(), reloadPath.size());
  std::error_code ignored;
  std::filesystem::remove(reloadPath, ignored);
  ASSERT_EQ(loadStatus, OBELISK_RT_OK);
  ASSERT_EQ(sampleFunctional(context, handle, 0, 2), OBELISK_RT_OK);
  Database afterReload = saveCoverage(context, "illegal-after-load");
  ASSERT_EQ(afterReload.illegalBinDiagnostics.size(), 4u);
  for (const auto &row : afterReload.illegalBinDiagnostics)
    EXPECT_EQ(row.count,
              row.simulationTime == 7 && row.run == loadedRun ? 1u : 2u);
  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeDesignTest, WideWildcardExclusionUnionUsesIterativeProof) {
  using namespace obelisk::coverage;
  constexpr uint32_t bitWidth = 8192;
  constexpr uint32_t limbCount = bitWidth / 64;
  constexpr uint32_t inclusive =
      FunctionalValueAtomLowerInclusive | FunctionalValueAtomUpperInclusive;

  Database schema;
  schema.scopes.push_back({1, 0, "top", 0, "top"});
  schema.functionalTypes.push_back({10, 1, "cg", 0, 2023, "top.cg"});
  schema.functionalItems.push_back({20, 10, "complete",
                                    FunctionalItemKind::Coverpoint, 0, 100, 1,
                                    0, "top.cg.complete"});
  schema.functionalItems.push_back({21, 10, "partial",
                                    FunctionalItemKind::Coverpoint, 0, 100, 1,
                                    1, "top.cg.partial"});
  schema.functionalBins.push_back({30, 20, "all", FunctionalBinKind::State, 0,
                                   1, 0, "top.cg.complete.all"});
  schema.functionalBins.push_back({31, 20, "split", FunctionalBinKind::State,
                                   FunctionalBinIgnore | FunctionalBinWildcard,
                                   1, 1, "top.cg.complete.split"});
  schema.functionalBins.push_back(
      {40, 21, "all", FunctionalBinKind::State, 0, 1, 0, "top.cg.partial.all"});
  schema.functionalBins.push_back({41, 21, "even", FunctionalBinKind::State,
                                   FunctionalBinIgnore | FunctionalBinWildcard,
                                   1, 1, "top.cg.partial.even"});
  schema.functionalValueSets = {
      {50, 20, 0, 1, bitWidth, FunctionalValueSetKind::Integral, 0,
       CoverageSignedness::Unsigned, 0},
      {51, 20, 1, 2, bitWidth, FunctionalValueSetKind::Integral, 0,
       CoverageSignedness::Unsigned, 0},
      {52, 21, 3, 1, bitWidth, FunctionalValueSetKind::Integral, 0,
       CoverageSignedness::Unsigned, 0},
      {53, 21, 4, 1, bitWidth, FunctionalValueSetKind::Integral, 0,
       CoverageSignedness::Unsigned, 0}};
  schema.functionalValueAtoms = {
      {50, 0, 0, 0, limbCount, 0, FunctionalValueAtomKind::IntegralRange,
       inclusive, 0, 0},
      {51, 0, 0, limbCount, limbCount, 0,
       FunctionalValueAtomKind::IntegralValue, inclusive, 0, 0},
      {51, 0, 0, 2 * limbCount, limbCount, 1,
       FunctionalValueAtomKind::IntegralValue, inclusive, 0, 0},
      {52, 0, 0, 3 * limbCount, limbCount, 0,
       FunctionalValueAtomKind::IntegralRange, inclusive, 0, 0},
      {53, 0, 0, 4 * limbCount, limbCount, 0,
       FunctionalValueAtomKind::IntegralValue, inclusive, 0, 0}};
  auto addRange = [&](uint64_t set, uint32_t atomOrdinal) {
    for (uint32_t ordinal = 0; ordinal != limbCount; ++ordinal)
      schema.functionalValueLimbs.push_back(
          {set, atomOrdinal, ordinal, 0, 0, UINT64_MAX, 0, 0});
  };
  auto addParityCube = [&](uint64_t set, uint32_t atomOrdinal, bool odd) {
    for (uint32_t ordinal = 0; ordinal != limbCount; ++ordinal) {
      uint64_t wildcard = ordinal ? UINT64_MAX : UINT64_MAX - 1;
      uint64_t value = ordinal == 0 && odd ? 1 : 0;
      schema.functionalValueLimbs.push_back({set, atomOrdinal, ordinal, value,
                                             wildcard, value, wildcard,
                                             wildcard});
    }
  };
  addRange(50, 0);
  addParityCube(51, 0, false);
  addParityCube(51, 1, true);
  addRange(52, 0);
  addParityCube(53, 0, false);
  schema.functionalBinPlans = {{30, 50}, {31, 51}, {40, 52}, {41, 53}};
  FunctionalExpression sample;
  sample.id = 60;
  sample.owner = 20;
  sample.ownerKind = FunctionalExpressionOwnerKind::Item;
  sample.role = FunctionalExpressionRole::CoverpointSample;
  sample.resultKind = FunctionalExpressionResultKind::Integral;
  sample.bitWidth = bitWidth;
  sample.signedness = CoverageSignedness::Unsigned;
  sample.evaluationPhase = FunctionalExpressionEvaluationPhase::Sample;
  sample.resultOrdinal = 0;
  schema.functionalExpressions.push_back(sample);
  sample.id = 61;
  sample.owner = 21;
  sample.resultOrdinal = 1;
  schema.functionalExpressions.push_back(sample);
  schema.schemaFingerprint = computeSchemaFingerprint(schema);

  obelisk_rt_context *context = createContextForSchema(schema, false);
  ASSERT_NE(context, nullptr);
  ASSERT_EQ(finalizeCoverage(context, 0, 0, nullptr, nullptr),
            OBELISK_RT_OK);
  obelisk_rt_covergroup_v1 handle = 0;
  ASSERT_EQ(createFunctional(context, 10, &handle), OBELISK_RT_OK);
  double percentage = 0.0;
  int32_t covered = 0, total = 0;
  ASSERT_EQ(obelisk_rt_v1_covergroup_instance_query(
                context, handle, 0, &percentage, &covered, &total),
            OBELISK_RT_OK);
  EXPECT_EQ(covered, 0);
  EXPECT_EQ(total, 1);

  Database saved = saveCoverage(context, "wide-wildcard-union");
  auto flagsFor = [&](uint64_t templateBin) {
    auto bin = std::find_if(
        saved.resolvedFunctionalBins.begin(),
        saved.resolvedFunctionalBins.end(),
        [&](const auto &entry) { return entry.templateBin == templateBin; });
    EXPECT_NE(bin, saved.resolvedFunctionalBins.end());
    return bin == saved.resolvedFunctionalBins.end() ? uint32_t{0} : bin->flags;
  };
  EXPECT_TRUE(flagsFor(30) & FunctionalBinEmpty);
  EXPECT_FALSE(flagsFor(40) & FunctionalBinEmpty);
  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeDesignTest, SignedWideAndRealIntervalsMatchDirectly) {
  using namespace obelisk::coverage;
  Database schema = makeFunctionalSchema();
  constexpr uint32_t inclusive =
      FunctionalValueAtomLowerInclusive | FunctionalValueAtomUpperInclusive;
  schema.functionalBins = {{30, 20, "negative", FunctionalBinKind::State, 0, 1,
                            0, "top.cg.first.negative"},
                           {40, 21, "real_interval", FunctionalBinKind::State,
                            0, 1, 0, "top.cg.second.real_interval"}};
  schema.functionalValueSets = {{50, 20, 0, 1, 65,
                                 FunctionalValueSetKind::Integral, 0,
                                 CoverageSignedness::Signed, 0},
                                {52, 21, 1, 1, 64, FunctionalValueSetKind::Real,
                                 0, CoverageSignedness::NotApplicable, 0}};
  uint64_t realLow = 0, realHigh = 0;
  double low = 1.5, high = 2.5;
  std::memcpy(&realLow, &low, sizeof(low));
  std::memcpy(&realHigh, &high, sizeof(high));
  schema.functionalValueAtoms = {
      {50, 0, 0, 0, 2, 0, FunctionalValueAtomKind::IntegralRange, inclusive, 0,
       0},
      {52, realLow, realHigh, 2, 0, 0, FunctionalValueAtomKind::RealInterval,
       inclusive | FunctionalValueAtomRealRange, 0, 0}};
  schema.functionalValueLimbs = {
      {50, 0, 0, UINT64_MAX - 1, 0, UINT64_MAX, 0, 0},
      {50, 0, 1, 1, 0, 1, 0, 0}};
  schema.functionalBinPlans = {{30, 50}, {40, 52}};
  schema.functionalExpressions[0].bitWidth = 65;
  schema.functionalExpressions[0].signedness = CoverageSignedness::Signed;
  schema.functionalExpressions[1].resultKind =
      FunctionalExpressionResultKind::Real;
  schema.functionalExpressions[1].bitWidth = 0;
  schema.functionalExpressions[1].signedness =
      CoverageSignedness::NotApplicable;
  schema.schemaFingerprint = computeSchemaFingerprint(schema);

  obelisk_rt_context *context = createContextForSchema(schema, false);
  ASSERT_NE(context, nullptr);
  ASSERT_EQ(finalizeCoverage(context, 0, 0, nullptr, nullptr),
            OBELISK_RT_OK);
  obelisk_rt_covergroup_v1 handle = 0;
  ASSERT_EQ(createFunctional(context, 10, &handle), OBELISK_RT_OK);
  std::array<uint8_t, 9> negativeOne{};
  std::fill_n(negativeOne.begin(), 8, UINT8_MAX);
  negativeOne.back() = 1;
  double real = 2.0;
  std::array<obelisk_rt_functional_value_v1, 2> values{};
  values[0] = {60,
               65,
               negativeOne.size(),
               negativeOne.data(),
               nullptr,
               nullptr,
               0,
               OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL,
               0};
  values[1] = {61,
               64,
               sizeof(real),
               &real,
               nullptr,
               nullptr,
               0,
               OBELISK_RT_FUNCTIONAL_VALUE_REAL,
               0};
  ASSERT_EQ(obelisk_rt_v1_covergroup_sample(context, handle, values.data(),
                                            values.size()),
            OBELISK_RT_OK);
  double percentage = 0.0;
  int32_t covered = 0, total = 0;
  ASSERT_EQ(obelisk_rt_v1_covergroup_instance_query(
                context, handle, 0, &percentage, &covered, &total),
            OBELISK_RT_OK);
  EXPECT_EQ(covered, 2);
  EXPECT_EQ(total, 2);
  EXPECT_DOUBLE_EQ(percentage, 100.0);
  obelisk_rt_v1_context_destroy(context);
}

TEST_F(CoverageRuntimeDesignTest,
     FunctionalConfigurationIgnoresUnrelatedMetricSchema) {
  using namespace obelisk::coverage;
  Database functionalOnly = makeFunctionalSchema();
  Database withLine = functionalOnly;
  withLine.sourceFiles.push_back({50, "line.sv", {}});
  withLine.linePoints.push_back({51, 50, 50, 1, "", 1, 1, 1, 2, 0, 0});
  withLine.schemaFingerprint = computeSchemaFingerprint(withLine);

  auto snapshotConfiguration = [&](const Database &schema, uint64_t lineCount,
                                   const std::string &suffix) -> Digest {
    obelisk_rt_context *local = createContextForSchema(schema);
    if (!local)
      return {};
    EXPECT_EQ(
        finalizeCoverage(local, lineCount, 0, nullptr, nullptr),
        OBELISK_RT_OK);
    obelisk_rt_covergroup_v1 handle = 0;
    EXPECT_EQ(createFunctional(local, 10, &handle), OBELISK_RT_OK);
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("obelisk-functional-configuration-" + suffix + "-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()) +
         ".obcov");
    std::string pathText = path.string();
    EXPECT_EQ(obelisk_rt_v1_coverage_save(local, pathText.data(),
                                          pathText.size(), 0, 0),
              OBELISK_RT_OK);
    Database snapshot;
    EXPECT_EQ(readFile(pathText, snapshot), Status::Ok);
    Digest result{};
    if (snapshot.resolvedInstances.size() == 1)
      result = snapshot.resolvedInstances[0].configuration;
    else
      ADD_FAILURE() << "missing resolved functional instance";
    obelisk_rt_v1_context_destroy(local);
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    return result;
  };

  EXPECT_EQ(snapshotConfiguration(functionalOnly, 0, "functional"),
            snapshotConfiguration(withLine, 1, "line"));
}

TEST_F(CoverageRuntimeDesignTest,
     Clause40CodeMetricSaveOmitsResolvedFunctionalPayload) {
  using namespace obelisk::coverage;
  Database schema = makeFunctionalSchema();
  schema.sourceFiles.push_back({50, "line.sv", {}});
  schema.linePoints.push_back({51, 50, 50, 1, "", 1, 1, 1, 2, 0, 0});
  schema.schemaFingerprint = computeSchemaFingerprint(schema);
  obelisk_rt_context *context = createContextForSchema(schema);
  ASSERT_NE(context, nullptr);
  ASSERT_EQ(finalizeCoverage(context, 1, 0, nullptr, nullptr),
            OBELISK_RT_OK);
  obelisk_rt_covergroup_v1 handle = 0;
  ASSERT_EQ(createFunctional(context, 10, &handle), OBELISK_RT_OK);

  std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("obelisk-clause40-no-functional-payload-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()) +
       ".obcov");
  std::string pathText = path.string();
  int32_t status = -99;
  ASSERT_EQ(withManagedString(context, pathText,
                              [&](auto name) {
                                return obelisk_rt_v1_coverage_database_save(
                                    context, 22, name, &status);
                              }),
            OBELISK_RT_OK);
  EXPECT_EQ(status, 1);
  Database saved;
  ASSERT_EQ(readFile(pathText, saved), Status::Ok);
  EXPECT_EQ(saved.runs.front().flags, RunContainsLine);
  const std::array<size_t, 24> resolvedSizes{
      saved.resolvedInstances.size(),
      saved.illegalBinDiagnostics.size(),
      saved.functionalConfigurations.size(),
      saved.functionalConfigurationOptions.size(),
      saved.resolvedFunctionalItems.size(),
      saved.resolvedFunctionalBins.size(),
      saved.resolvedTransitionAlternatives.size(),
      saved.resolvedTransitionExpansionGroups.size(),
      saved.resolvedTransitionSteps.size(),
      saved.resolvedCrossPlans.size(),
      saved.resolvedCrossAutomaticBinCountLimbs.size(),
      saved.resolvedCrossAutomaticNodes.size(),
      saved.resolvedCrossAutomaticEdges.size(),
      saved.resolvedCrossSelectorBindings.size(),
      saved.resolvedFunctionalValueSets.size(),
      saved.resolvedFunctionalValueAtoms.size(),
      saved.resolvedFunctionalValueLimbs.size(),
      saved.resolvedFunctionalBinPlans.size(),
      saved.resolvedFunctionalBinGroups.size(),
      saved.resolvedFunctionalTupleSets.size(),
      saved.resolvedFunctionalTupleSetTuples.size(),
      saved.resolvedFunctionalTupleSetComponents.size(),
      saved.sparseCrossTuples.size(),
      saved.sparseCrossTupleComponents.size()};
  EXPECT_TRUE(std::all_of(resolvedSizes.begin(), resolvedSizes.end(),
                          [](size_t size) { return size == 0; }));
  obelisk_rt_v1_context_destroy(context);
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

TEST_F(CoverageRuntimeDesignTest,
     PersistenceMaskRetainsStaticSchemaButOnlySelectedRunData) {
  using namespace obelisk::coverage;
  Database schema = makeFunctionalSchema();
  schema.sourceFiles.push_back({50, "line.sv", {}});
  schema.linePoints.push_back({51, 50, 50, 1, "", 1, 1, 1, 2, 0, 0});
  schema.schemaFingerprint = computeSchemaFingerprint(schema);

  obelisk_rt_context *context = createContextForSchema(schema);
  ASSERT_NE(context, nullptr);
  ASSERT_EQ(finalizeCoverage(context, 1, 0, nullptr, nullptr,
                             OBELISK_RT_COVERAGE_PERSIST_LINE),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_coverage_point_hit(context, 0, 1), OBELISK_RT_OK);
  obelisk_rt_covergroup_v1 instance = 0;
  ASSERT_EQ(createFunctional(context, 10, &instance), OBELISK_RT_OK);
  ASSERT_EQ(sampleFunctional(context, instance, 0, 2), OBELISK_RT_OK);

  Database snapshot = saveCoverage(context, "line-persistence-mask");
  ASSERT_EQ(snapshot.runs.size(), 1u);
  EXPECT_EQ(snapshot.runs.front().flags, RunContainsLine);
  ASSERT_EQ(snapshot.counters.size(), 1u);
  EXPECT_EQ(snapshot.counters.front().metric, MetricKind::Line);
  EXPECT_EQ(snapshot.counters.front().entity, 51u);
  EXPECT_EQ(snapshot.counters.front().value, 1u);

  // Static functional templates are part of the exact schema identity.  The
  // execution-resolved instance and its counters are not persisted unless the
  // functional metric was explicitly selected.
  EXPECT_EQ(snapshot.functionalTypes.size(), schema.functionalTypes.size());
  EXPECT_EQ(snapshot.functionalItems.size(), schema.functionalItems.size());
  EXPECT_EQ(snapshot.functionalBins.size(), schema.functionalBins.size());
  EXPECT_EQ(snapshot.schemaFingerprint, schema.schemaFingerprint);
  EXPECT_TRUE(snapshot.functionalConfigurations.empty());
  EXPECT_TRUE(snapshot.resolvedFunctionalItems.empty());
  EXPECT_TRUE(snapshot.resolvedFunctionalBins.empty());
  EXPECT_TRUE(snapshot.resolvedInstances.empty());
  EXPECT_TRUE(snapshot.sparseCrossTuples.empty());
  EXPECT_TRUE(snapshot.illegalBinDiagnostics.empty());

  obelisk_rt_v1_context_destroy(context);
}

TEST(CoverageDatabaseTest,
     FunctionalOrdinalsAndInstanceCountersRoundTripAndValidate) {
  using namespace obelisk::coverage;
  Database database = makeFunctionalSchema();
  database.sourceFiles.push_back({1, "expanded.sv", {}});
  database.sourceFiles.push_back({2, "macro.svh", {}});
  database.functionalSourceRanges.push_back(
      {30, FunctionalSourceRole::Expanded, 0, 1, 1, "", 3, 4, 3, 8, 0});
  database.functionalSourceRanges.push_back(
      {30, FunctionalSourceRole::MacroDefinition, 0, 2, 2, "MAKE_BIN", 7, 1, 7,
       20, 0});
  Digest configuration = addResolvedFunctionalConfiguration(database);
  obelisk::coverage::Run run;
  run.uuid.back() = 1;
  run.name = "run";
  run.flags = RunContainsFunctional;
  database.runs.push_back(run);
  database.resolvedInstances.push_back({run.uuid, 10, 7, "", 0, configuration});
  database.counters.push_back(
      {run.uuid, MetricKind::Functional, 130, 7, 0, 0, 2});
  database.counters.push_back(
      {run.uuid, MetricKind::Functional, 131, 7, 0, 1, UINT64_MAX});
  database.schemaFingerprint = computeSchemaFingerprint(database);

  std::vector<uint8_t> firstImage;
  ASSERT_EQ(serialize(database, firstImage), Status::Ok);
  Database parsed;
  ASSERT_EQ(parse(firstImage.data(), firstImage.size(), parsed), Status::Ok);
  ASSERT_EQ(parsed.functionalItems.size(), 2u);
  EXPECT_EQ(parsed.functionalItems[1].ordinal, 1u);
  ASSERT_EQ(parsed.functionalBins.size(), 3u);
  EXPECT_EQ(parsed.functionalBins[1].ordinal, 1u);
  ASSERT_EQ(parsed.functionalSourceRanges.size(), 2u);
  EXPECT_EQ(parsed.functionalSourceRanges[1].role,
            FunctionalSourceRole::MacroDefinition);
  EXPECT_EQ(parsed.functionalSourceRanges[1].macroName, "MAKE_BIN");
  ASSERT_EQ(parsed.counters.size(), 2u);
  EXPECT_EQ(parsed.counters[0].instance, 7u);
  EXPECT_EQ(parsed.counters[1].flags, 1u);
  std::vector<uint8_t> secondImage;
  ASSERT_EQ(serialize(parsed, secondImage), Status::Ok);
  EXPECT_EQ(firstImage, secondImage);

  std::vector<uint8_t> malformedImage = firstImage;
  size_t sourceSection =
      sectionOffset(malformedImage, SectionKind::FunctionalSourceRanges);
  ASSERT_NE(sourceSection, std::string::npos);
  put32(malformedImage, sourceSection + 8, 99);
  refreshChecksum(malformedImage);
  EXPECT_EQ(parse(malformedImage.data(), malformedImage.size(), parsed),
            Status::InvalidEnum);
  malformedImage = firstImage;
  malformedImage[sourceSection + 56] = 1;
  refreshChecksum(malformedImage);
  EXPECT_EQ(parse(malformedImage.data(), malformedImage.size(), parsed),
            Status::InvalidDatabase);
  malformedImage = firstImage;
  size_t instanceSection =
      sectionOffset(malformedImage, SectionKind::ResolvedCovergroupInstances);
  ASSERT_NE(instanceSection, std::string::npos);
  put32(malformedImage, instanceSection + 36, 2);
  refreshChecksum(malformedImage);
  Diagnostic instanceDiagnostic;
  EXPECT_EQ(parse(malformedImage.data(), malformedImage.size(), parsed, {},
                  &instanceDiagnostic),
            Status::InvalidEnum);
  EXPECT_EQ(instanceDiagnostic.section,
            SectionKind::ResolvedCovergroupInstances);
  EXPECT_EQ(instanceDiagnostic.record, 0u);
  EXPECT_STREQ(instanceDiagnostic.field, "flags");

  Database malformed = database;
  malformed.functionalItems[1].ordinal = 0;
  EXPECT_EQ(validate(malformed), Status::InvalidReference);
  malformed = database;
  malformed.functionalBins[1].ordinal = 2;
  EXPECT_EQ(validate(malformed), Status::InvalidReference);
  malformed = database;
  malformed.functionalBins[0].kind = FunctionalBinKind::Cross;
  EXPECT_EQ(validate(malformed), Status::InvalidReference);
  malformed = database;
  malformed.functionalTypes[0].languageVersion = 2022;
  EXPECT_EQ(validate(malformed), Status::InvalidReference);
  malformed = database;
  malformed.counters[0].instance = 0;
  EXPECT_EQ(validate(malformed), Status::InvalidReference);
  malformed = database;
  malformed.resolvedInstances[0].flags = 1;
  EXPECT_EQ(validate(malformed), Status::InvalidReference);
  malformed = database;
  malformed.functionalSourceRanges.erase(
      malformed.functionalSourceRanges.begin());
  EXPECT_EQ(validate(malformed), Status::InvalidReference);
  malformed = database;
  malformed.counters[0].entity = 30;
  EXPECT_EQ(validate(malformed), Status::InvalidReference);
}

TEST(CoverageDatabaseTest, ToggleDimensionTreeRoundTripsAndRejectsBadParents) {
  using namespace obelisk::coverage;
  Database schema;
  schema.sourceFiles.push_back({1, "aggregate.sv", {}});
  schema.scopes.push_back({2, 0, "top", 0, "top"});
  schema.toggleObjects.push_back({3, 2, 1, "mem", 0, 16, 1, 1, 1, 4, 0});
  schema.toggleDimensions.push_back(
      {3, UINT32_MAX, ToggleDimensionKind::Root, 1, 2, 0, 0, 0, 16, "", 0});
  schema.toggleDimensions.push_back(
      {3, 0, ToggleDimensionKind::UnpackedArray, 2, 1, 3, 2, 0, 16, "", 0});
  schema.toggleDimensions.push_back(
      {3, 1, ToggleDimensionKind::Scalar, UINT32_MAX, 0, 7, 0, 0, 8, "", 0});
  schema.schemaFingerprint = computeSchemaFingerprint(schema);

  std::vector<uint8_t> image;
  ASSERT_EQ(serialize(schema, image), Status::Ok);
  Database parsed;
  ASSERT_EQ(parse(image.data(), image.size(), parsed), Status::Ok);
  ASSERT_EQ(parsed.toggleDimensions.size(), 3u);
  EXPECT_EQ(parsed.toggleDimensions[1].kind,
            ToggleDimensionKind::UnpackedArray);
  EXPECT_EQ(parsed.toggleDimensions[1].firstChild, 2u);
  EXPECT_EQ(parsed.toggleDimensions[1].childCount, 1u);
  EXPECT_EQ(parsed.toggleDimensions[1].left, 3);
  EXPECT_EQ(parsed.toggleDimensions[1].right, 2);
  EXPECT_EQ(parsed.toggleDimensions[1].bitWidth, 16u);

  schema.toggleDimensions[2].parent = 2;
  EXPECT_EQ(validate(schema), Status::InvalidReference);

  // A child span denotes all preorder descendants, not merely an arbitrary
  // range following the node.  In particular, a sibling cannot be claimed as
  // a child's descendant while retaining the root as its parent.
  schema.toggleDimensions.clear();
  schema.toggleObjects.front().bitWidth = 3;
  schema.toggleObjects.front().typeRoot = 0;
  schema.toggleDimensions.push_back(
      {3, UINT32_MAX, ToggleDimensionKind::Root, 1, 3, 0, 0, 0, 3, "", 0});
  schema.toggleDimensions.push_back(
      {3, 0, ToggleDimensionKind::Scalar, UINT32_MAX, 0, 0, 0, 0, 1, "a", 0});
  schema.toggleDimensions.push_back(
      {3, 0, ToggleDimensionKind::Scalar, UINT32_MAX, 0, 0, 0, 1, 1, "b", 0});
  schema.toggleDimensions.push_back(
      {3, 0, ToggleDimensionKind::Scalar, UINT32_MAX, 0, 0, 0, 2, 1, "c", 0});
  schema.schemaFingerprint = computeSchemaFingerprint(schema);
  EXPECT_EQ(validate(schema), Status::Ok);
  schema.toggleDimensions[1].firstChild = 2;
  schema.toggleDimensions[1].childCount = 1;
  EXPECT_EQ(validate(schema), Status::InvalidReference);
}

TEST(CoverageDatabaseTest, RejectsNullReferencesAndAmbiguousRunMetadata) {
  obelisk::coverage::Database database;
  database.linePoints.push_back({1, 0, 0, 0, "", 1, 1, 1, 1, 0, 0});
  EXPECT_EQ(obelisk::coverage::validate(database),
            obelisk::coverage::Status::InvalidReference);

  database.linePoints.clear();
  obelisk::coverage::Run run;
  run.name = "nil";
  database.runs.push_back(run);
  EXPECT_EQ(obelisk::coverage::validate(database),
            obelisk::coverage::Status::DuplicateRun);

  database.runs.front().uuid.back() = 1;
  database.runs.front().tags.push_back({"", "value"});
  EXPECT_EQ(obelisk::coverage::validate(database),
            obelisk::coverage::Status::UnsortedOrDuplicate);
}

TEST(CoverageDatabaseTest, RunMetricFlagsOwnEveryDynamicCounter) {
  using namespace obelisk::coverage;
  Database database;
  database.sourceFiles.push_back({1, "source.sv", {}});
  database.scopes.push_back({2, 0, "top", 0, "top"});
  database.linePoints.push_back({3, 1, 1, 2, "", 1, 1, 1, 2, 0, 0});
  obelisk::coverage::Run run;
  run.uuid.back() = 1;
  run.name = "run";
  run.flags = RunContainsLine;
  database.runs.push_back(run);
  database.counters.push_back({run.uuid, MetricKind::Line, 3, 0, 0, 0, 0});
  database.schemaFingerprint = computeSchemaFingerprint(database);
  EXPECT_EQ(validate(database), Status::Ok);
  database.runs.front().flags = RunContainsToggle;
  EXPECT_EQ(validate(database), Status::InvalidReference);
  database.runs.front().flags = RunContainsCoverageMask | uint64_t{8};
  EXPECT_EQ(validate(database), Status::InvalidDatabase);
}

TEST(CoverageDatabaseTest, ZeroMetricRunsNeverCoalesceAsDisjointPayloads) {
  using namespace obelisk::coverage;
  Database first;
  first.sourceFiles.push_back({1, "source.sv", {}});
  first.scopes.push_back({2, 0, "top", 0, "top"});
  first.linePoints.push_back({3, 1, 1, 2, "", 1, 1, 1, 2, 0, 0});
  obelisk::coverage::Run run;
  run.uuid.back() = 1;
  run.name = "run";
  first.runs.push_back(run);
  first.schemaFingerprint = computeSchemaFingerprint(first);
  Database second = first;
  EXPECT_EQ(merge(first, second), Status::DuplicateRun);

  first = second;
  second.runs.front().flags = RunContainsLine;
  EXPECT_EQ(merge(first, second), Status::DuplicateRun);
}

TEST(CoverageDatabaseTest, RejectsNoncanonicalReservedBytesAndRunTagCounts) {
  obelisk::coverage::Database database;
  database.sourceFiles.push_back({1, "source.sv", {}});
  obelisk::coverage::Run run;
  run.uuid.back() = 1;
  run.name = "run";
  run.tags.push_back({"suite", "smoke"});
  database.runs.push_back(run);
  std::vector<uint8_t> image;
  ASSERT_EQ(obelisk::coverage::serialize(database, image),
            obelisk::coverage::Status::Ok);

  size_t sources =
      sectionOffset(image, obelisk::coverage::SectionKind::SourceFiles);
  ASSERT_NE(sources, std::string::npos);
  image[sources + 12] = 1;
  refreshChecksum(image);
  obelisk::coverage::Database parsed;
  EXPECT_EQ(obelisk::coverage::parse(image.data(), image.size(), parsed),
            obelisk::coverage::Status::InvalidDatabase);

  ASSERT_EQ(obelisk::coverage::serialize(database, image),
            obelisk::coverage::Status::Ok);
  size_t runs = sectionOffset(image, obelisk::coverage::SectionKind::Runs);
  ASSERT_NE(runs, std::string::npos);
  put32(image, runs + 48, 2);
  refreshChecksum(image);
  EXPECT_EQ(obelisk::coverage::parse(image.data(), image.size(), parsed),
            obelisk::coverage::Status::BadCount);

  ASSERT_EQ(obelisk::coverage::serialize(database, image),
            obelisk::coverage::Status::Ok);
  put32(image, 12, 1);
  refreshChecksum(image);
  EXPECT_EQ(obelisk::coverage::parse(image.data(), image.size(), parsed),
            obelisk::coverage::Status::InvalidEnum);
}

TEST(CoverageDatabaseTest, RejectsNoncanonicalPhysicalMetadataOrder) {
  obelisk::coverage::Database database;
  obelisk::coverage::Run first;
  first.uuid.back() = 1;
  first.name = "first";
  first.tags.push_back({"suite", "one"});
  database.runs.push_back(first);
  obelisk::coverage::Run second;
  second.uuid.back() = 2;
  second.name = "second";
  second.tags.push_back({"suite", "two"});
  database.runs.push_back(second);
  std::vector<uint8_t> image;
  ASSERT_EQ(obelisk::coverage::serialize(database, image),
            obelisk::coverage::Status::Ok);

  size_t metadata =
      sectionOffset(image, obelisk::coverage::SectionKind::UserMetadata);
  ASSERT_NE(metadata, std::string::npos);
  // Record zero is producer metadata. Reverse the two otherwise-valid run
  // records so redistribution into Run::tags cannot hide wire disorder.
  for (size_t byte = 0; byte != 24; ++byte)
    std::swap(image[metadata + 24 + byte], image[metadata + 48 + byte]);
  refreshChecksum(image);
  obelisk::coverage::Database parsed;
  EXPECT_EQ(obelisk::coverage::parse(image.data(), image.size(), parsed),
            obelisk::coverage::Status::UnsortedOrDuplicate);
}

TEST(CoverageDatabaseTest,
     AggregateMergeRemapsInstancesAndSaturatesDeterministically) {
  obelisk::coverage::Database first = makeFunctionalSchema();
  obelisk::coverage::Digest configuration =
      addResolvedFunctionalConfiguration(first);
  obelisk::coverage::Run firstRun;
  firstRun.uuid.back() = 1;
  firstRun.name = "first";
  firstRun.flags = obelisk::coverage::RunContainsFunctional;
  first.runs.push_back(firstRun);
  first.resolvedInstances.push_back(
      {firstRun.uuid, 10, 1, "named", 0, configuration});
  first.resolvedInstances.push_back(
      {firstRun.uuid, 10, 2, "$auto$2",
       obelisk::coverage::ResolvedInstanceGeneratedName, configuration});
  first.counters.push_back({firstRun.uuid,
                            obelisk::coverage::MetricKind::Functional, 130, 1,
                            0, 0, UINT64_MAX - 2});
  first.counters.push_back({firstRun.uuid,
                            obelisk::coverage::MetricKind::Functional, 130, 2,
                            0, 0, 3});
  first.schemaFingerprint = obelisk::coverage::computeSchemaFingerprint(first);

  obelisk::coverage::Database second = first;
  second.runs.front().uuid.back() = 2;
  second.runs.front().name = "second";
  second.resolvedInstances[0].run = second.runs.front().uuid;
  second.resolvedInstances[0].id = 4;
  second.resolvedInstances[1].run = second.runs.front().uuid;
  second.resolvedInstances[1].id = 5;
  second.counters[0].run = second.runs.front().uuid;
  second.counters[0].instance = 4;
  second.counters[0].value = 5;
  second.counters[1].run = second.runs.front().uuid;
  second.counters[1].instance = 5;
  second.counters[1].value = 7;
  obelisk::coverage::Database firstInput = first;
  obelisk::coverage::Database forward = firstInput;
  ASSERT_EQ(obelisk::coverage::merge(forward, second, true),
            obelisk::coverage::Status::Ok);
  ASSERT_EQ(forward.resolvedInstances.size(), 3u);
  EXPECT_EQ(forward.resolvedInstances.front().flags, 0u);
  EXPECT_EQ(
      std::count_if(forward.resolvedInstances.begin(),
                    forward.resolvedInstances.end(),
                    [](const obelisk::coverage::ResolvedInstance &instance) {
                      return instance.flags &
                             obelisk::coverage::ResolvedInstanceGeneratedName;
                    }),
      2);
  ASSERT_EQ(forward.counters.size(), 3u);
  EXPECT_EQ(forward.counters[0].value, UINT64_MAX);
  EXPECT_EQ(forward.counters[0].flags, 1u);
  EXPECT_EQ(forward.counters[1].value, 3u);
  EXPECT_EQ(forward.counters[2].value, 7u);

  obelisk::coverage::Database reverse = second;
  ASSERT_EQ(obelisk::coverage::merge(reverse, firstInput, true),
            obelisk::coverage::Status::Ok);
  std::vector<uint8_t> forwardImage, reverseImage;
  ASSERT_EQ(obelisk::coverage::serialize(forward, forwardImage),
            obelisk::coverage::Status::Ok);
  ASSERT_EQ(obelisk::coverage::serialize(reverse, reverseImage),
            obelisk::coverage::Status::Ok);
  EXPECT_EQ(forwardImage, reverseImage);
}

} // namespace
