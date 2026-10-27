// Bounded constructor timing probe, deliberately outside the unit suite.
#include "talos/Coverage/CoverageDatabase.h"
#include "talos/Runtime/Runtime.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>

using namespace talos::coverage;

int main(int argc, char **argv) {
  const uint64_t count = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 16;
  if (!count || count > 128)
    return 2;
  Database schema;
  schema.scopes.push_back({1, 0, "top", 0, "top"});
  schema.functionalTypes.push_back({10, 1, "cg", 0, 2023, "top.cg"});
  for (uint32_t ordinal = 0; ordinal != count; ++ordinal) {
    const uint64_t item = 100 + ordinal;
    const std::string name = "cp" + std::to_string(ordinal);
    const std::string path = "top.cg." + name;
    schema.functionalItems.push_back({item, 10, name,
                                      FunctionalItemKind::Coverpoint, 0, 100, 1,
                                      ordinal, path});
    FunctionalExpression sample;
    sample.id = 1000 + ordinal;
    sample.owner = item;
    sample.ownerKind = FunctionalExpressionOwnerKind::Item;
    sample.role = FunctionalExpressionRole::CoverpointSample;
    sample.resultKind = FunctionalExpressionResultKind::Integral;
    sample.bitWidth = 4;
    sample.signedness = CoverageSignedness::Unsigned;
    sample.evaluationPhase = FunctionalExpressionEvaluationPhase::Sample;
    sample.resultOrdinal = ordinal;
    schema.functionalExpressions.push_back(sample);

    if (ordinal % 4 == 0)
      continue;
    const uint64_t bin = 2000 + ordinal * 2;
    const uint64_t set = 3000 + ordinal;
    const bool defaultArray = ordinal % 4 >= 2;
    if (defaultArray) {
      schema.functionalBins.push_back(
          {bin, item, "other", FunctionalBinKind::State, FunctionalBinDefault,
           1, 0, path + ".other"});
      FunctionalBinPlan plan;
      plan.bin = bin;
      plan.arrayMode = FunctionalBinArrayMode::Unsized;
      plan.distribution = FunctionalBinDistributionKind::PerValue;
      schema.functionalBinPlans.push_back(plan);
    }
    const bool fullDomain = ordinal % 4 == 3;
    const uint64_t low = defaultArray && !fullDomain ? 1 : 0;
    const uint64_t high = fullDomain ? 15 : low;
    schema.functionalBins.push_back(
        {bin + 1, item, "defined", FunctionalBinKind::State,
         defaultArray ? 0u : FunctionalBinIgnore, 1, defaultArray ? 1u : 0u,
         path + ".defined"});
    schema.functionalBinPlans.push_back({bin + 1, set});
    schema.functionalValueSets.push_back(
        {set, item, static_cast<uint32_t>(schema.functionalValueAtoms.size()),
         1, 4, FunctionalValueSetKind::Integral, 0,
         CoverageSignedness::Unsigned, 0});
    schema.functionalValueAtoms.push_back(
        {set, 0, 0, static_cast<uint32_t>(schema.functionalValueLimbs.size()),
         1, 0,
         fullDomain ? FunctionalValueAtomKind::IntegralRange
                    : FunctionalValueAtomKind::IntegralValue,
         FunctionalValueAtomLowerInclusive | FunctionalValueAtomUpperInclusive,
         0, 0});
    schema.functionalValueLimbs.push_back({set, 0, 0, low, 0, high, 0, 0});
  }
  schema.schemaFingerprint = computeSchemaFingerprint(schema);
  std::vector<uint8_t> image;
  Diagnostic diagnostic;
  if (serialize(schema, image, &diagnostic) != Status::Ok) {
    std::fprintf(stderr, "schema field=%s\n",
                 diagnostic.field ? diagnostic.field : "");
    return 1;
  }
  struct Execution {
    talos_rt_execution_descriptor_v1 descriptor{};
    talos_rt_execution_extension_v1 extension{};
  } execution;
  static_assert(offsetof(Execution, extension) ==
                sizeof(talos_rt_execution_descriptor_v1));
  execution.descriptor.version = TALOS_RT_VERSION;
  execution.descriptor.flags = TALOS_RT_EXECUTION_COVERAGE_SCHEMA;
  execution.descriptor.reserved = sizeof(execution.descriptor);
  execution.extension.version = TALOS_RT_EXECUTION_EXTENSION_VERSION;
  execution.extension.size = sizeof(execution.extension);
  execution.extension.coverage_schema = image.data();
  execution.extension.coverage_schema_size = image.size();
  talos_rt_context *context = nullptr;
  talos_rt_status status =
      talos_rt_v1_context_create_for_design(&execution.descriptor, &context);
  if (status == TALOS_RT_OK)
    status =
        talos_rt_v1_coverage_finalize(context, 0, 0, nullptr, nullptr,
                                        TALOS_RT_COVERAGE_PERSIST_FUNCTIONAL);
  const auto begin = std::chrono::steady_clock::now();
  talos_rt_covergroup_v1 handle = 0;
  if (status == TALOS_RT_OK)
    status = talos_rt_v1_covergroup_create(context, 10, nullptr, 0, nullptr,
                                             0, &handle);
  const auto end = std::chrono::steady_clock::now();
  std::printf("points=%llu construct=%.6f status=%u\n",
              static_cast<unsigned long long>(count),
              std::chrono::duration<double>(end - begin).count(),
              static_cast<unsigned>(status));
  if (status == TALOS_RT_OK && argc > 2) {
    const std::string path = argv[2];
    status =
        talos_rt_v1_coverage_save(context, path.data(), path.size(), 0, 0);
    Database saved;
    const std::array<uint32_t, 4> binPrefix = {0, 16, 33, 49};
    const uint64_t expectedBins = count / 4 * 50 + binPrefix[count % 4];
    const uint64_t expectedGroups = count * 2 - (count + 3) / 4;
    if (status == TALOS_RT_OK &&
        (readFile(path, saved) != Status::Ok ||
         saved.functionalConfigurations.size() != 1 ||
         saved.resolvedFunctionalItems.size() != count ||
         saved.resolvedFunctionalBins.size() != expectedBins ||
         saved.resolvedFunctionalBinGroups.size() != expectedGroups))
      status = TALOS_RT_INVALID_DESIGN;
  }
  talos_rt_v1_context_destroy(context);
  return status == TALOS_RT_OK ? 0 : 1;
}
