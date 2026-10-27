//===- GeneratedVPIRTLPropertiesTest.cpp - RTL statement properties ------===//

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/Parser/Parser.h"
#include "talos/Conversion/SimulationToBytecode.h"
#include "talos/Dialect/Simulation/SimulationOps.h"
#include "talos/Runtime/Runtime.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "gtest/gtest.h"
#include <cstdlib>
#include <utility>

namespace {

TEST(GeneratedDesignDatabase, RTLStatementProperties) {
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
  options.vpi = "read";
  auto encoded = talos::encodeSimulationDesign(design, options);
  ASSERT_TRUE(mlir::succeeded(encoded));
  uint64_t checksum = 0;
  for (unsigned byte = 0; byte != 8; ++byte)
    checksum |= uint64_t{encoded->bytecode[32 + byte]} << (byte * 8);
  const talos_rt_execution_descriptor_v1 execution{
      TALOS_RT_VERSION,
      encoded->executionFlags,
      0,
      encoded->bytecode.data(),
      encoded->bytecode.size(),
      encoded->designDatabase.data(),
      encoded->designDatabase.size(),
      encoded->stateBitCount,
      checksum,
      nullptr,
      0,
      0,
      0,
      nullptr,
      0,
      nullptr,
      0};
  ASSERT_EQ(talos_rt_v1_design_validate(&execution), TALOS_RT_OK);
  talos_rt_context *runtime = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &runtime),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_vpi_startup(runtime, nullptr, 0), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_vpi_end_compile(runtime), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_vpi_start_simulation(runtime), TALOS_RT_OK);
  char topName[] = "top";
  vpiHandle top = vpi_handle_by_name(topName, nullptr);
  ASSERT_NE(top, nullptr);
  auto processes = vpi_iterate(vpiProcess, top);
  ASSERT_NE(processes, nullptr);
  auto process = vpi_scan(processes);
  ASSERT_NE(process, nullptr);
  EXPECT_EQ(vpi_scan(processes), nullptr);
  auto block = vpi_handle(vpiStmt, process);
  ASSERT_NE(block, nullptr);
  EXPECT_EQ(vpi_get(vpiType, block), vpiBegin);
  auto statements = vpi_iterate(vpiStmt, block);
  ASSERT_NE(statements, nullptr);
  const std::pair<int, const char *> operators[] = {
      {vpiAssignmentOp, "vpiAssignmentOp"},
      {vpiAssignmentOp, "vpiAssignmentOp"},
      {vpiAddOp, "vpiAddOp"},
      {vpiSubOp, "vpiSubOp"},
      {vpiMultOp, "vpiMultOp"},
      {vpiDivOp, "vpiDivOp"},
      {vpiModOp, "vpiModOp"},
      {vpiBitAndOp, "vpiBitAndOp"},
      {vpiBitOrOp, "vpiBitOrOp"},
      {vpiBitXorOp, "vpiBitXorOp"},
      {vpiLShiftOp, "vpiLShiftOp"},
      {vpiRShiftOp, "vpiRShiftOp"},
      {vpiArithLShiftOp, "vpiArithLShiftOp"},
      {vpiArithRShiftOp, "vpiArithRShiftOp"},
  };
  unsigned index = 0;
  for (const auto &[op, spelling] : operators) {
    SCOPED_TRACE(index);
    auto statement = vpi_scan(statements);
    ASSERT_NE(statement, nullptr);
    EXPECT_EQ(vpi_get(vpiType, statement), vpiAssignment);
    EXPECT_EQ(vpi_get(vpiOpType, statement), op);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    EXPECT_STREQ(vpi_get_str(vpiOpType, statement), spelling);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    EXPECT_EQ(vpi_get(vpiBlocking, statement), index++ != 1);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    EXPECT_EQ(vpi_release_handle(statement), 1);
  }
  const std::pair<int, int> cases[] = {
      {vpiCaseExact, vpiNoQualifier},
      {vpiCaseX, vpiUniqueQualifier},
      {vpiCaseZ, vpiPriorityQualifier},
      {vpiCaseExact, vpiUniqueQualifier | vpiInsideQualifier},
      {vpiCaseExact, vpiUniqueQualifier}};
  for (const auto &[type, qualifier] : cases) {
    auto statement = vpi_scan(statements);
    ASSERT_NE(statement, nullptr);
    EXPECT_EQ(vpi_get(vpiType, statement), vpiCase);
    EXPECT_EQ(vpi_get(vpiCaseType, statement), type);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    EXPECT_EQ(vpi_get(vpiQualifier, statement), qualifier);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    EXPECT_EQ(vpi_release_handle(statement), 1);
  }
  for (int qualifier : {vpiNoQualifier, vpiUniqueQualifier, vpiUniqueQualifier,
                        vpiPriorityQualifier}) {
    auto statement = vpi_scan(statements);
    ASSERT_NE(statement, nullptr);
    EXPECT_EQ(vpi_get(vpiType, statement), vpiIf);
    EXPECT_EQ(vpi_get(vpiQualifier, statement), qualifier);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    EXPECT_EQ(vpi_release_handle(statement), 1);
  }
  EXPECT_EQ(vpi_scan(statements), nullptr);
  EXPECT_EQ(vpi_release_handle(block), 1);
  EXPECT_EQ(vpi_release_handle(process), 1);
  EXPECT_EQ(vpi_release_handle(top), 1);
  ASSERT_EQ(talos_rt_v1_vpi_end_simulation(runtime), TALOS_RT_OK);
  talos_rt_v1_context_destroy(runtime);
}
} // namespace
