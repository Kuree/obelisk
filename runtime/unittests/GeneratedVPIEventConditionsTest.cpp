//===- GeneratedVPIEventConditionsTest.cpp - Event ownership -------------===//
#include "../lib/RuntimeInternal.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Parser/Parser.h"
#include "obelisk/Conversion/SimulationToBytecode.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Reflection/DesignReflection.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "gtest/gtest.h"
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

TEST(GeneratedDesignDatabase, DirectEventConditions) {
  using namespace obelisk;
  const char *input = std::getenv("OBELISK_TEST_INPUT");
  ASSERT_NE(input, nullptr);
  mlir::DialectRegistry registry;
  registry.insert<mlir::arith::ArithDialect, mlir::cf::ControlFlowDialect,
                  sim::ObeliskSimulationDialect>();
  mlir::MLIRContext context(registry);
  auto module = mlir::parseSourceFile<mlir::ModuleOp>(input, &context);
  ASSERT_TRUE(module);
  auto design = *module->getOps<sim::SimDesignOp>().begin();
  SimulationBytecodeOptions options;
  options.vpi = "read";
  auto encoded = encodeSimulationDesign(design, options);
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
  std::vector<vpiHandle> controls;
  unsigned processCount = 0, excludedCount = 0;
  unsigned directCount = 0, assignmentCount = 0, repeatedCount = 0;
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
    auto condition = keep(vpi_handle(vpiCondition, control));
    if (!condition) {
      // Edge/list/iff controls require their own expression occurrences. A
      // direct resolver must not replace them with the clock declaration.
      EXPECT_EQ(statementKind, vpiEventControl);
      ++excludedCount;
      continue;
    }
    if (statementKind == vpiAssignment)
      ++assignmentCount;
    else if (statementKind == vpiRepeatControl)
      ++repeatedCount;
    else {
      EXPECT_EQ(statementKind, vpiEventControl);
      ++directCount;
    }
    for (auto previous : controls)
      EXPECT_EQ(vpi_compare_objects(control, previous), 0);
    controls.push_back(control);
    EXPECT_EQ(vpi_compare_objects(condition, clk), 1);
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
  EXPECT_EQ(excludedCount, 3u);
  EXPECT_EQ(directCount, 1u);
  EXPECT_EQ(assignmentCount, 1u);
  EXPECT_EQ(repeatedCount, 1u);
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
  EXPECT_EQ(count, 3u);
  for (auto handle : handles)
    EXPECT_EQ(vpi_free_object(handle), 1);
  EXPECT_EQ(obelisk_rt_v1_vpi_end_simulation(raw), OBELISK_RT_OK);
}
