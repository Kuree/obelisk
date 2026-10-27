//===- GeneratedVPITopologyExpressionsTest.cpp - Expressions --------------===//
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
#include <array>
#include <cstdlib>
#include <memory>
#include <string>
#include <tuple>
#include <vector>
namespace {
TEST(GeneratedDesignDatabase, TopologyExpressions) {
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
  auto keep = [&](vpiHandle handle) {
    if (handle)
      handles.push_back(handle);
    return handle;
  };
  auto children = [&](vpiHandle parent, int selector) {
    std::vector<vpiHandle> result;
    auto iterator = vpi_iterate(selector, parent);
    if (!iterator)
      return result;
    auto use = keep(vpi_handle(vpiUse, iterator));
    EXPECT_EQ(vpi_compare_objects(use, parent), 1);
    while (auto child = vpi_scan(iterator))
      result.push_back(keep(child));
    return result;
  };
  auto named = [&](const char *name) {
    return keep(vpi_handle_by_name(const_cast<char *>(name), nullptr));
  };
  auto top = named("top");
  auto source = named("top.source");
  auto q = named("top.q");
  auto gate = named("top.g");
  ASSERT_NE(top, nullptr);
  ASSERT_NE(source, nullptr);
  ASSERT_NE(q, nullptr);
  ASSERT_NE(gate, nullptr);
  auto assignments = children(top, vpiContAssign);
  ASSERT_EQ(assignments.size(), 2u);
  std::array<vpiHandle, 2> rhs{};
  for (auto assignment : assignments) {
    auto lhs = keep(vpi_handle(vpiLhs, assignment));
    auto expression = keep(vpi_handle(vpiRhs, assignment));
    ASSERT_NE(lhs, nullptr);
    ASSERT_NE(expression, nullptr);
    EXPECT_EQ(vpi_get(vpiType, lhs), vpiPartSelect);
    EXPECT_EQ(vpi_get(vpiSize, lhs), 4);
    EXPECT_EQ(vpi_compare_objects(keep(vpi_handle(vpiParent, lhs)), q), 1);
    EXPECT_EQ(vpi_get(vpiSize, expression), 4);
    EXPECT_EQ(vpi_compare_objects(keep(vpi_handle(vpiModule, expression)), top),
              1);
    if (vpi_get(vpiType, expression) == vpiPartSelect) {
      ASSERT_EQ(rhs[0], nullptr);
      rhs[0] = expression;
      EXPECT_EQ(
          vpi_compare_objects(keep(vpi_handle(vpiParent, expression)), source),
          1);
    } else {
      EXPECT_EQ(vpi_get(vpiOpType, expression), vpiConcatOp);
      ASSERT_EQ(rhs[1], nullptr);
      rhs[1] = expression;
      auto operands = children(expression, vpiOperand);
      ASSERT_EQ(operands.size(), 2u);
      for (auto operand : operands)
        EXPECT_EQ(
            vpi_compare_objects(keep(vpi_handle(vpiParent, operand)), source),
            1);
    }
  }
  ASSERT_NE(rhs[0], nullptr);
  ASSERT_NE(rhs[1], nullptr);
  auto terminals = children(gate, vpiPrimTerm);
  ASSERT_EQ(terminals.size(), 3u);
  std::array<vpiHandle, 3> terminal{}, expression{};
  for (auto item : terminals) {
    int index = vpi_get(vpiTermIndex, item);
    ASSERT_GE(index, 0);
    ASSERT_LT(index, 3);
    ASSERT_EQ(terminal[index], nullptr);
    terminal[index] = item;
    expression[index] = keep(vpi_handle(vpiExpr, item));
    ASSERT_NE(expression[index], nullptr);
    EXPECT_EQ(vpi_get(vpiDirection, item), index == 0 ? vpiOutput : vpiInput);
  }
  EXPECT_EQ(vpi_compare_objects(expression[0], named("top.out")), 1);
  EXPECT_EQ(vpi_get(vpiType, expression[1]), vpiPartSelect);
  EXPECT_EQ(
      vpi_compare_objects(keep(vpi_handle(vpiParent, expression[1])), source),
      1);
  EXPECT_EQ(vpi_get(vpiType, expression[2]), vpiConstant);
  auto write = [&](int value) {
    s_vpi_value data{};
    data.format = vpiIntVal;
    data.value.integer = value;
    vpi_put_value(source, &data, nullptr, vpiNoDelay);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
  };
  auto read = [&](vpiHandle object) {
    s_vpi_value data{};
    data.format = vpiIntVal;
    vpi_get_value(object, &data);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    return data.value.integer;
  };
  write(0xa5);
  EXPECT_EQ(read(rhs[0]), 10);
  EXPECT_EQ(read(rhs[1]), 9);
  EXPECT_EQ(read(expression[1]), 1);
  EXPECT_EQ(read(expression[2]), 1);
  write(0x5a);
  EXPECT_EQ(read(rhs[0]), 5);
  EXPECT_EQ(read(rhs[1]), 6);
  EXPECT_EQ(read(expression[1]), 0);
  auto uses = children(source, vpiUse);
  ASSERT_EQ(uses.size(), 3u);
  assignments.push_back(terminal[1]);
  for (auto expected : assignments)
    EXPECT_EQ(std::count_if(uses.begin(), uses.end(),
                            [&](auto actual) {
                              return vpi_compare_objects(actual, expected) == 1;
                            }),
              1);
  auto qUses = children(q, vpiUse);
  ASSERT_EQ(qUses.size(), 2u);
  for (unsigned i = 0; i != 2; ++i)
    EXPECT_EQ(std::count_if(qUses.begin(), qUses.end(), [&](auto actual) {
                return vpi_compare_objects(actual, assignments[i]) == 1;
              }),
              1);
  for (auto handle : handles)
    EXPECT_EQ(vpi_release_handle(handle), 1);
}
} // namespace
