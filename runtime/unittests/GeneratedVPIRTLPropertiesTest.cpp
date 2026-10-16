//===- GeneratedVPIRTLPropertiesTest.cpp - RTL statement properties ------===//

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/Parser/Parser.h"
#include "obelisk/Conversion/SimulationToBytecode.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/Runtime.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "gtest/gtest.h"
#include <cstdlib>
#include <utility>

namespace {

TEST(GeneratedDesignDatabase, RTLStatementProperties) {
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
  options.vpi = "read";
  auto encoded = obelisk::encodeSimulationDesign(design, options);
  ASSERT_TRUE(mlir::succeeded(encoded));
  uint64_t checksum = 0;
  for (unsigned byte = 0; byte != 8; ++byte)
    checksum |= uint64_t{encoded->bytecode[32 + byte]} << (byte * 8);
  const obelisk_rt_execution_descriptor_v1 execution{
      OBELISK_RT_VERSION,
      encoded->executionFlags,
      0,
      encoded->bytecode.data(),
      encoded->bytecode.size(),
      encoded->designDatabase.data(),
      encoded->designDatabase.size(),
      encoded->stateBitCount,
      checksum};
  ASSERT_EQ(obelisk_rt_v1_design_validate(&execution), OBELISK_RT_OK);
  obelisk_rt_context *runtime = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &runtime),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(runtime, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(runtime), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(runtime), OBELISK_RT_OK);
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
  const int operators[] = {vpiAssignmentOp,  vpiAssignmentOp, vpiAddOp,
                           vpiSubOp,         vpiMultOp,       vpiDivOp,
                           vpiModOp,         vpiBitAndOp,     vpiBitOrOp,
                           vpiBitXorOp,      vpiLShiftOp,     vpiRShiftOp,
                           vpiArithLShiftOp, vpiArithRShiftOp};
  unsigned index = 0;
  for (int op : operators) {
    SCOPED_TRACE(index);
    auto statement = vpi_scan(statements);
    ASSERT_NE(statement, nullptr);
    EXPECT_EQ(vpi_get(vpiType, statement), vpiAssignment);
    EXPECT_EQ(vpi_get(vpiOpType, statement), op);
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
  ASSERT_EQ(obelisk_rt_v1_vpi_end_simulation(runtime), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(runtime);
}
} // namespace
