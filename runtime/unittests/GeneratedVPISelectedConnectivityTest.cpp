//===- Selected net connectivity -----------------------------------------===//
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Parser/Parser.h"
#include "obelisk/Conversion/SimulationToBytecode.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/Runtime.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <cstdlib>
#include <memory>
#include <set>
#include <string>
#include <vector>
namespace {
TEST(GeneratedDesignDatabase, SelectedNetConnectivity) {
  const char *input = std::getenv("OBELISK_TEST_INPUT");
  ASSERT_NE(input, nullptr);
  mlir::DialectRegistry registry;
  registry.insert<mlir::arith::ArithDialect, mlir::cf::ControlFlowDialect,
                  obelisk::sim::ObeliskSimulationDialect>();
  mlir::MLIRContext context(registry);
  auto module = mlir::parseSourceFile<mlir::ModuleOp>(input, &context);
  ASSERT_TRUE(module);
  auto design = *module->getOps<obelisk::sim::SimDesignOp>().begin();
  obelisk::SimulationBytecodeOptions options;
  options.vpi = "full";
  auto encoded = obelisk::encodeSimulationDesign(design, options);
  ASSERT_TRUE(mlir::succeeded(encoded));
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.flags = encoded->executionFlags;
  execution.bytecode = encoded->bytecode.data();
  execution.bytecode_size = encoded->bytecode.size();
  execution.design_database = encoded->designDatabase.data();
  execution.design_database_size = encoded->designDatabase.size();
  execution.state_bit_count = encoded->stateBitCount;
  for (unsigned byte = 0; byte != 8; ++byte)
    execution.checksum |= uint64_t{encoded->bytecode[32 + byte]} << (byte * 8);
  ASSERT_EQ(obelisk_rt_v1_design_validate(&execution), OBELISK_RT_OK);
  obelisk_rt_context *raw = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &raw),
            OBELISK_RT_OK);
  std::unique_ptr<obelisk_rt_context, decltype(&obelisk_rt_v1_context_destroy)>
      runtime(raw, obelisk_rt_v1_context_destroy);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(raw, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(raw), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(raw), OBELISK_RT_OK);
  std::vector<vpiHandle> handles;
  auto keep = [&](vpiHandle h) {
    if (h)
      handles.push_back(h);
    return h;
  };
  auto named = [&](const char *n) {
    return keep(vpi_handle_by_name(const_cast<char *>(n), nullptr));
  };
  auto collect = [&](int selector, vpiHandle source) {
    std::vector<vpiHandle> result;
    auto iterator = vpi_iterate(selector, source);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    if (iterator)
      while (auto next = vpi_scan(iterator))
        result.push_back(keep(next));
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    return result;
  };
  auto top = named("top"), other = named("other"), q = named("top.q"),
       inputNet = named("top.s"), asc = named("top.asc"),
       index = named("top.index");
  ASSERT_NE(q, nullptr);
  ASSERT_NE(inputNet, nullptr);
  ASSERT_NE(index, nullptr);
  auto assignments = collect(vpiContAssign, top);
  auto foreign = collect(vpiContAssign, other);
  ASSERT_EQ(assignments.size(), 7u);
  ASSERT_EQ(foreign.size(), 1u);
  std::vector<vpiHandle> owners(7);
  for (auto h : assignments) {
    auto id = vpi_get(vpiLineNo, h);
    ASSERT_GE(id, 100);
    ASSERT_LE(id, 107);
    if (id <= 106)
      owners[id - 100] = h;
  }
  owners[4] = foreign[0];
  for (auto h : owners)
    ASSERT_NE(h, nullptr);
  auto lhs = [&](unsigned i) { return keep(vpi_handle(vpiLhs, owners[i])); };
  auto bit = [&](vpiHandle net, int i) {
    return keep(vpi_handle_by_index(net, i));
  };
  auto writeIndex = [&](int value, unsigned unknown = 0) {
    s_vpi_vecval vector{static_cast<unsigned>(value), unknown};
    s_vpi_value data{};
    data.format = vpiVectorVal;
    data.value.vector = &vector;
    vpi_put_value(index, &data, nullptr, vpiNoDelay);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
  };
  auto assignmentBits = [&](const std::vector<vpiHandle> &result) {
    std::vector<std::pair<unsigned, int>> actual;
    for (auto h : result) {
      if (vpi_get(vpiType, h) != vpiContAssignBit)
        continue;
      auto parent = keep(vpi_handle(vpiParent, h));
      unsigned which = owners.size();
      for (unsigned i = 0; i != owners.size(); ++i)
        if (vpi_compare_objects(parent, owners[i]))
          which = i;
      EXPECT_LT(which, owners.size());
      actual.emplace_back(which, vpi_get(vpiOffset, h));
    }
    std::sort(actual.begin(), actual.end());
    return actual;
  };
  writeIndex(4);
  auto q4 = bit(q, 4);
  auto drivers = collect(vpiDriver, q4);
  EXPECT_EQ(assignmentBits(drivers),
            (std::vector<std::pair<unsigned, int>>{
                {0, 4}, {1, 0}, {2, 2}, {3, 0}, {4, 0}, {6, 0}}));
  EXPECT_EQ(assignmentBits(collect(vpiLocalDriver, q4)),
            (std::vector<std::pair<unsigned, int>>{
                {0, 4}, {1, 0}, {2, 2}, {3, 0}, {6, 0}}));
  auto alias = named("other.alias");
  ASSERT_NE(alias, nullptr);
  EXPECT_EQ(assignmentBits(collect(vpiDriver, bit(alias, 4))),
            assignmentBits(drivers));
  EXPECT_EQ(assignmentBits(collect(vpiLocalDriver, bit(alias, 4))),
            (std::vector<std::pair<unsigned, int>>{{4, 0}}));
  EXPECT_EQ(assignmentBits(collect(vpiDriver, bit(q, 1))),
            (std::vector<std::pair<unsigned, int>>{{0, 1}}));
  EXPECT_EQ(assignmentBits(collect(vpiLoad, bit(inputNet, 0))),
            (std::vector<std::pair<unsigned, int>>{
                {0, 0}, {1, 0}, {3, 0}, {4, 0}, {5, 0}, {6, 0}}));
  EXPECT_EQ(assignmentBits(collect(vpiLoad, bit(inputNet, 7))),
            (std::vector<std::pair<unsigned, int>>{{0, 7}, {2, 3}}));
  EXPECT_EQ(assignmentBits(collect(vpiDriver, bit(asc, 0))),
            (std::vector<std::pair<unsigned, int>>{{5, 3}}));
  auto whole = collect(vpiDriver, q);
  EXPECT_EQ(
      whole.size(),
      7u); // whole, three part selections, dynamic part, bit owner, primitive
  unsigned parts = 0, indexed = 0, terminals = 0;
  for (auto h : whole) {
    int k = vpi_get(vpiType, h);
    parts += k == vpiPartSelect;
    indexed += k == vpiIndexedPartSelect;
    terminals += k == vpiPrimTerm;
  }
  EXPECT_EQ(parts, 3u);
  EXPECT_EQ(indexed, 1u);
  EXPECT_EQ(terminals, 1u);
  std::set<unsigned> partialOwners;
  for (auto h : whole)
    if (vpi_get(vpiType, h) == vpiPartSelect) {
      auto bound = keep(vpi_handle(vpiLeftRange, h));
      unsigned which = owners.size();
      for (unsigned candidate : {1u, 2u, 4u}) {
        auto expected = keep(vpi_handle(vpiLeftRange, lhs(candidate)));
        if (vpi_compare_objects(bound, expected))
          which = candidate;
      }
      EXPECT_LT(which, owners.size());
      EXPECT_TRUE(partialOwners.insert(which).second);
      auto module = keep(vpi_handle(vpiModule, h));
      EXPECT_EQ(vpi_compare_objects(module, which == 4 ? other : top), 1);
    }
  EXPECT_EQ(partialOwners, (std::set<unsigned>{1, 2, 4}));
  auto gate = named("top.g");
  auto terms = collect(vpiPrimTerm, gate);
  ASSERT_EQ(terms.size(), 3u);
  auto q6drivers = collect(vpiDriver, bit(q, 6));
  EXPECT_EQ(
      std::count_if(q6drivers.begin(), q6drivers.end(),
                    [&](auto h) { return vpi_compare_objects(h, terms[0]); }),
      1);
  EXPECT_TRUE(collect(vpiLoad, bit(q, 6)).empty());
  auto s6loads = collect(vpiLoad, bit(inputNet, 6));
  EXPECT_EQ(
      std::count_if(s6loads.begin(), s6loads.end(),
                    [&](auto h) { return vpi_compare_objects(h, terms[1]); }),
      1);
  EXPECT_TRUE(collect(vpiDriver, bit(inputNet, 6)).empty());
  // The query is a live source NetBit, but its iterator snapshots current bit4.
  auto dynamic = lhs(6);
  ASSERT_EQ(vpi_get(vpiType, dynamic), vpiNetBit);
  auto pending = vpi_iterate(vpiDriver, dynamic);
  ASSERT_NE(pending, nullptr);
  writeIndex(0);
  std::vector<vpiHandle> snapshot;
  while (auto next = vpi_scan(pending))
    snapshot.push_back(keep(next));
  EXPECT_EQ(assignmentBits(snapshot), assignmentBits(drivers));
  EXPECT_EQ(assignmentBits(collect(vpiDriver, dynamic)),
            (std::vector<std::pair<unsigned, int>>{{0, 0}, {3, 0}, {6, 0}}));
  writeIndex(8);
  EXPECT_TRUE(collect(vpiDriver, dynamic).empty());
  writeIndex(0, ~0u);
  EXPECT_TRUE(collect(vpiDriver, dynamic).empty());
  // Exact occurrence identity survives same-span overlap; equal offsets in
  // distinct assignments are not the same ContAssignBit.
  ASSERT_GE(drivers.size(), 2u);
  EXPECT_EQ(vpi_compare_objects(drivers[0], drivers[1]), 0);
  writeIndex(4);
  auto again = collect(vpiDriver, q4);
  ASSERT_EQ(again.size(), drivers.size());
  for (auto first : drivers)
    EXPECT_EQ(std::count_if(again.begin(), again.end(),
                            [&](auto second) {
                              return vpi_compare_objects(first, second);
                            }),
              1);
  // Preserve exact physical Local rows inside an interface, and never derive
  // sibling-interface local connections through the outer module.
  auto interfaceNet = named("top.first.n");
  auto interfaceAlias = named("top.second.alias");
  auto outerAlias = named("top.interface_alias");
  ASSERT_NE(interfaceNet, nullptr);
  ASSERT_NE(interfaceAlias, nullptr);
  ASSERT_NE(outerAlias, nullptr);
  for (int selector : {vpiLocalDriver, vpiLocalLoad}) {
    auto local = collect(selector, interfaceNet);
    ASSERT_EQ(local.size(), 1u);
    EXPECT_EQ(vpi_get(vpiType, local.front()), vpiContAssign);
    EXPECT_EQ(vpi_get(vpiLineNo, local.front()), 300);
    EXPECT_TRUE(collect(selector, interfaceAlias).empty());
    EXPECT_TRUE(collect(selector, outerAlias).empty());
  }
  for (int selector : {vpiDriver, vpiLoad}) {
    auto all = collect(selector, interfaceNet);
    ASSERT_EQ(all.size(), 2u);
    std::set<int> locations;
    for (auto connection : all) {
      EXPECT_EQ(vpi_get(vpiType, connection), vpiContAssign);
      locations.insert(vpi_get(vpiLineNo, connection));
    }
    EXPECT_EQ(locations, (std::set<int>{300, 301}));
  }
  s_vpi_value unavailable{};
  unavailable.format = vpiIntVal;
  vpi_get_value(drivers.front(), &unavailable);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
  for (auto h : handles)
    EXPECT_EQ(vpi_release_handle(h), 1);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_simulation(raw), OBELISK_RT_OK);
}
} // namespace
