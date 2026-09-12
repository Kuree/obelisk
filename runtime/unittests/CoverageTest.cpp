//===- CoverageTest.cpp - Functional coverage runtime tests --------------===//

#include "obelisk/Coverage/CoverageDatabase.h"
#include "obelisk/Runtime/Runtime.h"

#include "gtest/gtest.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
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
  std::fill(bytes.begin() + 72, bytes.begin() + 104, 0);
  obelisk::coverage::Digest digest =
      obelisk::coverage::sha256(bytes.data(), bytes.size());
  std::copy(digest.begin(), digest.end(), bytes.begin() + 72);
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

obelisk_rt_context *
createContextForSchema(const obelisk::coverage::Database &schema,
                       bool preResolve = false) {
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
  struct Execution {
    obelisk_rt_execution_descriptor_v1 descriptor{};
    obelisk_rt_execution_extension_v1 extension{};
  } execution;
  execution.descriptor.version = OBELISK_RT_VERSION;
  execution.descriptor.flags = OBELISK_RT_EXECUTION_COVERAGE_SCHEMA;
  execution.descriptor.reserved = sizeof(execution.descriptor);
  execution.descriptor.state_bit_count = 1;
  execution.extension.version = OBELISK_RT_EXECUTION_EXTENSION_VERSION;
  execution.extension.size = sizeof(execution.extension);
  execution.extension.coverage_schema = image.data();
  execution.extension.coverage_schema_size = image.size();
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

TEST(CoverageRuntimeDesignTest,
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

TEST(CoverageRuntimeDesignTest, Clause40ControlAndQueryHonorScopeSelection) {
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

TEST(CoverageRuntimeDesignTest,
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

TEST(CoverageRuntimeDesignTest,
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

TEST(CoverageRuntimeDesignTest, CoverageSaveFailureNamesTheRequestedOutput) {
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

TEST(CoverageRuntimeDesignTest,
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

TEST(CoverageRuntimeDesignTest, SharedAndSelectorDagBindsAndSamplesOnce) {
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

TEST(CoverageRuntimeDesignTest,
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

TEST(CoverageRuntimeDesignTest,
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

TEST(CoverageRuntimeDesignTest, CrossWithMatchesCountIsAppliedPerBinTuple) {
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

TEST(CoverageRuntimeDesignTest, CrossWithWideDynamicMatchesClampsExactly) {
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

TEST(CoverageRuntimeDesignTest, ConstructorInputsSnapshotAndRefsRemainLive) {
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
                &actual, nullptr),
            OBELISK_RT_OK);
  EXPECT_EQ(actual, 3);
  ASSERT_EQ(obelisk_rt_v1_covergroup_formal_read(
                context, handle, 71, OBELISK_RT_FUNCTIONAL_VALUE_INTEGRAL, 8, 1,
                &actual, nullptr),
            OBELISK_RT_OK);
  EXPECT_EQ(actual, 7);
  obelisk_rt_v1_context_destroy(context);
}

TEST(CoverageRuntimeDesignTest, InvalidUtf8CommentDoesNotPublishResolvedState) {
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
  ASSERT_EQ(afterSuccess.functionalConfigurationOptions.size(), 1u);
  EXPECT_EQ(afterSuccess.functionalConfigurationOptions[0].stringValue,
            "valid comment");
  ASSERT_EQ(afterSuccess.resolvedInstances.size(), 1u);
  EXPECT_EQ(afterSuccess.resolvedInstances[0].id, accepted);
  obelisk_rt_v1_context_destroy(context);
}

TEST(CoverageRuntimeDesignTest,
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

  EXPECT_EQ(obelisk_rt_v1_covergroup_type_query(
                context, 11, 0, &percentage, &covered, &total),
            OBELISK_RT_INVALID_DESIGN);
  EXPECT_EQ(obelisk_rt_v1_covergroup_type_query(
                context, 10, 22, &percentage, &covered, &total),
            OBELISK_RT_INVALID_DESIGN);
  obelisk_rt_v1_context_destroy(context);
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

TEST(CoverageRuntimeDesignTest, ParsesAndBindsEmbeddedSchema) {
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

TEST(CoverageRuntimeDesignTest, RejectsResolvedStateInEmbeddedSchema) {
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

TEST(CoverageRuntimeDesignTest,
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

TEST(CoverageRuntimeDesignTest,
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

TEST(CoverageRuntimeDesignTest, KnownBoundConsecutiveTransitionArrayResolves) {
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

TEST(CoverageRuntimeDesignTest,
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

TEST(CoverageRuntimeDesignTest,
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

TEST(CoverageRuntimeDesignTest,
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

TEST(CoverageRuntimeDesignTest,
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

TEST(CoverageRuntimeDesignTest, DefaultBinCatchesButNeverEntersDenominator) {
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

TEST(CoverageRuntimeDesignTest,
     FourStateExactWildcardAndIffUseCanonicalLogicPlanes) {
  using namespace obelisk::coverage;
  Database schema = makeFunctionalSchema();
  constexpr uint32_t inclusive =
      FunctionalValueAtomLowerInclusive | FunctionalValueAtomUpperInclusive;

  // Replace the first two bins with exact X, exact Z, and wildcard 1?. The
  // runtime !obelisk_sim.logic transport is (aval ^ bval, bval), whereas the
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

TEST(CoverageRuntimeDesignTest,
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
  obelisk_rt_v1_context_destroy(context);
}

TEST(CoverageRuntimeDesignTest, WideWildcardExclusionUnionUsesIterativeProof) {
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

TEST(CoverageRuntimeDesignTest, SignedWideAndRealIntervalsMatchDirectly) {
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

TEST(CoverageRuntimeDesignTest,
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

TEST(CoverageRuntimeDesignTest,
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

TEST(CoverageRuntimeDesignTest,
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
