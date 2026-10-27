//===- GeneratedVPIEventConditionsTest.cpp - Event ownership -------------===//
#include "../lib/RuntimeInternal.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Parser/Parser.h"
#include "talos/Conversion/SimulationToBytecode.h"
#include "talos/Dialect/Simulation/SimulationOps.h"
#include "talos/Reflection/DesignReflection.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "gtest/gtest.h"
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

static void checkEventConditions(unsigned expectedDirect) {
  using namespace talos;
  const char *input = std::getenv("TALOS_TEST_INPUT");
  ASSERT_NE(input, nullptr);
  mlir::DialectRegistry registry;
  registry.insert<mlir::arith::ArithDialect, mlir::cf::ControlFlowDialect,
                  sim::TalosSimulationDialect>();
  mlir::MLIRContext context(registry);
  auto module = mlir::parseSourceFile<mlir::ModuleOp>(input, &context);
  ASSERT_TRUE(module);
  auto design = *module->getOps<sim::SimDesignOp>().begin();
  SimulationBytecodeOptions options;
  options.vpi = "read";
  auto encoded = encodeSimulationDesign(design, options);
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
  auto top = keep(vpi_handle_by_name(const_cast<char *>("top"), nullptr));
  auto clk = keep(vpi_handle_by_name(const_cast<char *>("top.clk"), nullptr));
  ASSERT_NE(top, nullptr);
  ASSERT_NE(clk, nullptr);
  auto processes = vpi_iterate(vpiProcess, top);
  ASSERT_NE(processes, nullptr);
  auto reset =
      keep(vpi_handle_by_name(const_cast<char *>("top.reset"), nullptr));
  auto enable =
      keep(vpi_handle_by_name(const_cast<char *>("top.enable"), nullptr));
  ASSERT_NE(reset, nullptr);
  ASSERT_NE(enable, nullptr);
  auto checkEdge = [&](vpiHandle expression, int operation,
                       vpiHandle declaration) {
    ASSERT_NE(expression, nullptr);
    EXPECT_EQ(vpi_get(vpiType, expression), vpiOperation);
    EXPECT_EQ(vpi_get(vpiOpType, expression), operation);
    auto operands = vpi_iterate(vpiOperand, expression);
    ASSERT_NE(operands, nullptr);
    auto operand = keep(vpi_scan(operands));
    ASSERT_NE(operand, nullptr);
    EXPECT_EQ(vpi_compare_objects(operand, declaration), 1);
    EXPECT_EQ(vpi_scan(operands), nullptr);
  };
  std::vector<vpiHandle> controls;
  unsigned processCount = 0, directCount = 0, posedgeCount = 0;
  unsigned assignmentCount = 0, repeatedCount = 0;
  vpiHandle listControl = nullptr, iffControl = nullptr;
  // Process iteration has no source-order guarantee.
  while (auto process = keep(vpi_scan(processes))) {
    ++processCount;
    EXPECT_EQ(vpi_get(vpiType, process), vpiInitial);
    auto statement = keep(vpi_handle(vpiStmt, process));
    ASSERT_NE(statement, nullptr);
    int statementKind = vpi_get(vpiType, statement);
    auto control = statement;
    if (statementKind == vpiAssignment || statementKind == vpiRepeatControl)
      control = keep(vpi_handle(vpiEventControl, statement));
    ASSERT_NE(control, nullptr);
    EXPECT_EQ(vpi_get(vpiType, control), vpiEventControl);
    if (statementKind == vpiAssignment)
      ++assignmentCount;
    else if (statementKind == vpiRepeatControl)
      ++repeatedCount;
    else
      EXPECT_EQ(statementKind, vpiEventControl);
    for (auto previous : controls)
      EXPECT_EQ(vpi_compare_objects(control, previous), 0);
    controls.push_back(control);
    auto condition = keep(vpi_handle(vpiCondition, control));
    ASSERT_NE(condition, nullptr);
    if (vpi_compare_objects(condition, clk))
      ++directCount;
    else {
      EXPECT_EQ(vpi_get(vpiType, condition), vpiOperation);
      int operation = vpi_get(vpiOpType, condition);
      if (operation == vpiPosedgeOp) {
        ++posedgeCount;
        checkEdge(condition, vpiPosedgeOp, clk);
      } else if (operation == vpiEventOrOp) {
        ASSERT_EQ(listControl, nullptr);
        listControl = control;
        auto operands = vpi_iterate(vpiOperand, condition);
        ASSERT_NE(operands, nullptr);
        std::vector<vpiHandle> edges;
        for (unsigned i = 0; i != 3; ++i) {
          auto edge = keep(vpi_scan(operands));
          ASSERT_NE(edge, nullptr);
          checkEdge(edge, i == 1 ? vpiNegedgeOp : vpiPosedgeOp,
                    i == 1 ? reset : clk);
          for (auto previous : edges)
            EXPECT_EQ(vpi_compare_objects(previous, edge), 0);
          edges.push_back(edge);
        }
        EXPECT_EQ(vpi_scan(operands), nullptr);
        EXPECT_STREQ(vpi_get_str(vpiDecompile, condition),
                     "posedge \\clk or negedge \\reset or posedge \\clk ");
      } else {
        EXPECT_EQ(operation, vpiIffOp);
        ASSERT_EQ(iffControl, nullptr);
        iffControl = control;
        auto operands = vpi_iterate(vpiOperand, condition);
        ASSERT_NE(operands, nullptr);
        auto event = keep(vpi_scan(operands));
        ASSERT_NE(event, nullptr);
        if (expectedDirect == 3)
          EXPECT_EQ(vpi_compare_objects(event, clk), 1);
        else
          checkEdge(event, vpiPosedgeOp, clk);
        auto guard = keep(vpi_scan(operands));
        ASSERT_NE(guard, nullptr);
        EXPECT_EQ(vpi_compare_objects(guard, enable), 1);
        EXPECT_EQ(vpi_scan(operands), nullptr);
      }
    }
    auto owner = keep(vpi_handle(vpiScope, control));
    ASSERT_NE(owner, nullptr);
    EXPECT_EQ(vpi_compare_objects(owner, top), 1);
    auto body = keep(vpi_handle(vpiStmt, control));
    if (statementKind == vpiAssignment) {
      // IEEE 37.65 detail1: intra-assignment event control has no statement.
      EXPECT_EQ(body, nullptr);
    } else {
      ASSERT_NE(body, nullptr);
      EXPECT_EQ(vpi_get(vpiType, body), vpiNullStmt);
    }
  }
  EXPECT_EQ(processCount, 6u);
  EXPECT_EQ(directCount, expectedDirect);
  EXPECT_EQ(posedgeCount, 4u - expectedDirect);
  EXPECT_EQ(assignmentCount, 1u);
  EXPECT_EQ(repeatedCount, 1u);
  ASSERT_NE(listControl, nullptr);
  ASSERT_NE(iffControl, nullptr);
  for (auto [declaration, expected] :
       {std::pair{reset, listControl}, std::pair{enable, iffControl}}) {
    auto uses = vpi_iterate(vpiUse, declaration);
    ASSERT_NE(uses, nullptr);
    auto use = keep(vpi_scan(uses));
    ASSERT_NE(use, nullptr);
    EXPECT_EQ(vpi_compare_objects(use, expected), 1);
    EXPECT_EQ(vpi_scan(uses), nullptr);
  }
  auto uses = vpi_iterate(vpiUse, clk);
  ASSERT_NE(uses, nullptr);
  std::vector<bool> seen(controls.size());
  unsigned count = 0;
  while (auto use = keep(vpi_scan(uses))) {
    EXPECT_EQ(vpi_get(vpiType, use), vpiEventControl);
    unsigned match = controls.size();
    for (unsigned i = 0; i != controls.size(); ++i)
      if (vpi_compare_objects(use, controls[i]))
        match = i;
    ASSERT_LT(match, controls.size());
    EXPECT_FALSE(seen[match]);
    seen[match] = true;
    ++count;
  }
  EXPECT_EQ(count, 6u);
  for (auto handle : handles)
    EXPECT_EQ(vpi_free_object(handle), 1);
  EXPECT_EQ(talos_rt_v1_vpi_end_simulation(raw), TALOS_RT_OK);
}

TEST(GeneratedDesignDatabase, DirectEventConditions) {
  checkEventConditions(3);
}

TEST(GeneratedDesignDatabase, CompoundEventConditions) {
  checkEventConditions(1);
}
