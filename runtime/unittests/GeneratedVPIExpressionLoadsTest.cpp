//===- GeneratedVPIExpressionLoadsTest.cpp - Expression loads -------------===//
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Parser/Parser.h"
#include "talos/Conversion/SimulationToBytecode.h"
#include "talos/Dialect/Simulation/SimulationOps.h"
#include "talos/Runtime/Runtime.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>
#include <vector>
namespace {
TEST(GeneratedDesignDatabase, ExpressionNetLoads) {
  const char *input = std::getenv("TALOS_TEST_INPUT");
  ASSERT_NE(input, nullptr);
  mlir::DialectRegistry registry;
  registry.insert<mlir::arith::ArithDialect, mlir::cf::ControlFlowDialect,
                  talos::sim::TalosSimulationDialect>();
  mlir::MLIRContext context(registry);
  auto module = mlir::parseSourceFile<mlir::ModuleOp>(input, &context);
  ASSERT_TRUE(module);
  auto design = *module->getOps<talos::sim::SimDesignOp>().begin();
  talos::SimulationBytecodeOptions options;
  options.vpi = "full";
  auto encoded = talos::encodeSimulationDesign(design, options);
  ASSERT_TRUE(mlir::succeeded(encoded));
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.flags = encoded->executionFlags;
  execution.bytecode = encoded->bytecode.data();
  execution.bytecode_size = encoded->bytecode.size();
  execution.design_database = encoded->designDatabase.data();
  execution.design_database_size = encoded->designDatabase.size();
  execution.state_bit_count = encoded->stateBitCount;
  for (unsigned byte = 0; byte != 8; ++byte)
    execution.checksum |= uint64_t{encoded->bytecode[32 + byte]} << (byte * 8);
  ASSERT_EQ(talos_rt_v1_design_validate(&execution), TALOS_RT_OK);
  talos_rt_context *raw = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &raw),
            TALOS_RT_OK);
  std::unique_ptr<talos_rt_context, decltype(&talos_rt_v1_context_destroy)>
      runtime(raw, talos_rt_v1_context_destroy);
  ASSERT_EQ(talos_rt_v1_vpi_startup(raw, nullptr, 0), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_vpi_end_compile(raw), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_vpi_start_simulation(raw), TALOS_RT_OK);
  std::vector<vpiHandle> handles;
  auto keep = [&](vpiHandle h) {
    if (h)
      handles.push_back(h);
    return h;
  };
  auto named = [&](const char *name) {
    return keep(vpi_handle_by_name(const_cast<char *>(name), nullptr));
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
  auto bit = [&](vpiHandle source, int index) {
    return keep(vpi_handle_by_index(source, index));
  };
  auto relation = [&](int selector, vpiHandle source) {
    return keep(vpi_handle(selector, source));
  };
  auto operand = [&](vpiHandle source, unsigned index) {
    auto operands = collect(vpiOperand, source);
    EXPECT_LT(index, operands.size());
    return index < operands.size() ? operands[index] : nullptr;
  };
  auto q = named("top.q"), r = named("top.r"), addr = named("top.addr"),
       asc = named("top.asc"), one = named("top.one");
  ASSERT_NE(q, nullptr);
  ASSERT_NE(r, nullptr);
  ASSERT_NE(addr, nullptr);
  auto force = [&](vpiHandle net, unsigned value, unsigned unknown = 0) {
    s_vpi_vecval vector{value, unknown};
    s_vpi_value data{};
    data.format = vpiVectorVal;
    data.value.vector = &vector;
    vpi_put_value(net, &data, nullptr, vpiForceFlag);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
  };
  force(q, 2);
  force(addr, 2);
  std::map<int, vpiHandle> assignments;
  for (auto scope : {named("top"), named("other")})
    for (auto owner : collect(vpiContAssign, scope))
      EXPECT_TRUE(assignments.emplace(vpi_get(vpiLineNo, owner), owner).second);
  ASSERT_EQ(assignments.size(), 16u);
  auto rhs = [&](int line) { return relation(vpiRhs, assignments.at(line)); };
  using Keys = std::vector<std::pair<int, int>>;
  auto keys = [&](const std::vector<vpiHandle> &objects) {
    Keys result;
    for (auto object : objects) {
      int type = vpi_get(vpiType, object);
      EXPECT_TRUE(type == vpiContAssign || type == vpiContAssignBit);
      int offset = -1;
      if (type == vpiContAssignBit) {
        offset = vpi_get(vpiOffset, object);
        object = relation(vpiParent, object);
      }
      result.emplace_back(vpi_get(vpiLineNo, object), offset);
    }
    std::sort(result.begin(), result.end());
    EXPECT_EQ(std::adjacent_find(result.begin(), result.end()), result.end());
    return result;
  };
  auto loads = [&](vpiHandle net, int index, int selector = vpiLoad) {
    return keys(collect(selector, bit(net, index)));
  };
  const Keys q1{{100, -1}, {101, 5}, {102, 1}, {102, 5},  {103, -1}, {108, -1},
                {109, -1}, {110, 3}, {110, 5}, {112, -1}, {114, -1}};
  EXPECT_EQ(loads(q, 1), q1);
  auto localQ1 = q1;
  localQ1.erase(
      std::remove(localQ1.begin(), localQ1.end(), std::pair<int, int>{112, -1}),
      localQ1.end());
  EXPECT_EQ(loads(q, 1, vpiLocalLoad), localQ1);
  auto alias = named("other.alias");
  ASSERT_NE(alias, nullptr);
  EXPECT_EQ(loads(alias, 1), q1);
  EXPECT_EQ(loads(alias, 1, vpiLocalLoad), (Keys{{112, -1}}));
  EXPECT_EQ(loads(r, 1), (Keys{{100, -1},
                               {101, 1},
                               {103, -1},
                               {107, 1},
                               {109, -1},
                               {110, 1},
                               {112, -1},
                               {113, -1},
                               {115, 1}}));
  EXPECT_EQ(loads(asc, 0), (Keys{{111, 3}, {111, 7}}));
  EXPECT_TRUE(loads(asc, 4).empty());
  EXPECT_EQ(keys(collect(vpiLoad, one)), (Keys{{105, -1}, {109, -1}}));
  const Keys addressLoads{
      {104, -1}, {105, -1}, {106, -1}, {113, -1}, {115, -1}};
  EXPECT_EQ(loads(addr, 3), addressLoads);
  EXPECT_TRUE(collect(vpiDriver, addr).empty());
  EXPECT_TRUE(collect(vpiDriver, bit(addr, 3)).empty());

  // Every exact partial read, including the selected address in114, remains
  // independently reachable when querying the whole vector.
  std::vector<vpiHandle> partials{operand(rhs(101), 0),
                                  operand(rhs(102), 1),
                                  operand(rhs(103), 0),
                                  operand(operand(rhs(103), 1), 0),
                                  operand(rhs(108), 0),
                                  operand(rhs(110), 0),
                                  operand(operand(rhs(110), 1), 0),
                                  operand(rhs(114), 0),
                                  relation(vpiIndex, operand(rhs(114), 1))};
  std::set<uint64_t> seen;
  unsigned wholeCount = 0;
  for (auto object : collect(vpiLoad, q)) {
    if (vpi_get(vpiType, object) == vpiContAssign) {
      ++wholeCount;
      continue;
    }
    ASSERT_EQ(vpi_get(vpiType, object), vpiPartSelect);
    auto bound = relation(vpiLeftRange, object);
    unsigned match = partials.size();
    for (unsigned i = 0; i != partials.size(); ++i)
      if (vpi_compare_objects(bound, relation(vpiLeftRange, partials[i])))
        match = i;
    EXPECT_LT(match, partials.size());
    EXPECT_TRUE(seen.insert(match).second);
  }
  EXPECT_EQ(seen.size(), partials.size());
  EXPECT_EQ(wholeCount, 5u);

  // Iterator construction snapshots the queried current bit and all dynamic
  // candidate spans. Index reads survive an invalid data selection.
  auto dynamic = rhs(104);
  auto before = loads(q, 2);
  auto pending = vpi_iterate(vpiLoad, dynamic);
  ASSERT_NE(pending, nullptr);
  force(addr, 3);
  std::vector<vpiHandle> snapshot;
  while (auto object = vpi_scan(pending))
    snapshot.push_back(keep(object));
  EXPECT_EQ(keys(snapshot), before);
  EXPECT_EQ(keys(collect(vpiLoad, dynamic)), loads(q, 3));
  force(addr, 15);
  EXPECT_EQ(loads(addr, 3), addressLoads);
  EXPECT_TRUE(collect(vpiLoad, dynamic).empty());
  force(addr, 0, 15);
  EXPECT_EQ(loads(addr, 3), addressLoads);
  EXPECT_TRUE(collect(vpiLoad, dynamic).empty());
  // Primitive inputs have a scalar result boundary. An ordinary operation
  // is indivisible; a concat connects only its low result bit. Output terminal
  // addresses remain reads even when their selected destination is invalid.
  auto terms = collect(vpiPrimTerm, named("top.g"));
  ASSERT_EQ(terms.size(), 3u);
  auto gateA = named("top.gate_a"), gateB = named("top.gate_b"),
       gateIndex = named("top.gate_index");
  ASSERT_NE(gateA, nullptr);
  ASSERT_NE(gateB, nullptr);
  ASSERT_NE(gateIndex, nullptr);
  auto exactTerminals = [&](const std::vector<vpiHandle> &actual,
                            std::initializer_list<unsigned> expected) {
    EXPECT_EQ(actual.size(), expected.size());
    for (unsigned index : expected)
      EXPECT_EQ(std::count_if(actual.begin(), actual.end(),
                              [&](auto object) {
                                return vpi_compare_objects(object,
                                                           terms[index]);
                              }),
                1);
  };
  exactTerminals(collect(vpiLoad, bit(gateA, 1)), {1});
  exactTerminals(collect(vpiLoad, bit(gateB, 0)), {1, 2});
  exactTerminals(collect(vpiLoad, bit(gateB, 1)), {1});
  force(gateIndex, 7);
  exactTerminals(collect(vpiLoad, bit(gateIndex, 3)), {0});
  force(gateIndex, 0, 15);
  exactTerminals(collect(vpiLoad, bit(gateIndex, 3)), {0});
  EXPECT_TRUE(collect(vpiDriver, gateIndex).empty());
  for (auto h : handles)
    EXPECT_EQ(vpi_release_handle(h), 1);
  ASSERT_EQ(talos_rt_v1_vpi_end_simulation(raw), TALOS_RT_OK);
}
} // namespace
