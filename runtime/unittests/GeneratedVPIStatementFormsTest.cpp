//===- GeneratedVPIStatementFormsTest.cpp - Source statement forms ------===//

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
#include <array>
#include <cstdlib>
#include <set>
#include <string>

namespace {

TEST(GeneratedDesignDatabase, StatementForms) {
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
      checksum,
      nullptr,
      0,
      0,
      0,
      nullptr,
      0,
      nullptr,
      0};
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
  vpiHandle processes = vpi_iterate(vpiProcess, top);
  ASSERT_NE(processes, nullptr);
  std::set<int> kinds{vpiFor, vpiFuncCall, vpiTaskCall, vpiCase};
  while (vpiHandle process = vpi_scan(processes)) {
    EXPECT_EQ(vpi_get(vpiType, process), vpiInitial);
    vpiHandle statement = vpi_handle(vpiStmt, process);
    ASSERT_NE(statement, nullptr);
    const int kind = vpi_get(vpiType, statement);
    SCOPED_TRACE(kind);
    ASSERT_EQ(kinds.erase(kind), 1u);
    vpiHandle scope = vpi_handle(vpiScope, statement);
    ASSERT_NE(scope, nullptr);
    EXPECT_EQ(vpi_compare_objects(scope, top), 1);
    if (kind == vpiFuncCall) {
      EXPECT_EQ(vpi_get(vpiSize, statement), vpiUndefined);
      s_vpi_error_info error{};
      ASSERT_NE(vpi_chk_error(&error), 0);
      EXPECT_EQ(error.level, vpiNotice);
    }
    if (kind == vpiCase) {
      EXPECT_EQ(vpi_get(vpiQualifier, statement), vpiRandQualifier);
      EXPECT_EQ(vpi_chk_error(nullptr), 0);
      vpiHandle items = vpi_iterate(vpiCaseItem, statement);
      ASSERT_NE(items, nullptr);
      vpiHandle item = vpi_scan(items);
      ASSERT_NE(item, nullptr);
      vpiHandle body = vpi_handle(vpiStmt, item);
      ASSERT_NE(body, nullptr);
      EXPECT_EQ(vpi_get(vpiType, body), vpiNullStmt);
      EXPECT_EQ(vpi_scan(items), nullptr);
      EXPECT_EQ(vpi_release_handle(body), 1);
      EXPECT_EQ(vpi_release_handle(item), 1);
    }
    if (kind == vpiFor) {
      for (int selector : {vpiForInitStmt, vpiForIncStmt}) {
        vpiHandle first = vpi_handle(selector, statement);
        ASSERT_NE(first, nullptr);
        vpiHandle iterator = vpi_iterate(selector, statement);
        ASSERT_NE(iterator, nullptr);
        for (unsigned ordinal = 0; ordinal != 2; ++ordinal) {
          vpiHandle child = vpi_scan(iterator);
          ASSERT_NE(child, nullptr);
          EXPECT_EQ(vpi_get(vpiType, child), vpiAssignment);
          EXPECT_EQ(vpi_compare_objects(first, child), ordinal == 0);
          EXPECT_EQ(vpi_release_handle(child), 1);
        }
        EXPECT_EQ(vpi_scan(iterator), nullptr);
        EXPECT_EQ(vpi_release_handle(first), 1);
      }
    }
    for (auto handle : {scope, statement, process})
      EXPECT_EQ(vpi_release_handle(handle), 1);
  }
  EXPECT_TRUE(kinds.empty());

  for (const auto &[name, expected] :
       std::array<std::pair<const char *, int>, 3>{
           {{"top.worker", vpiBegin},
            {"top.single", vpiNullStmt},
            {"top.zero", vpiUndefined}}}) {
    vpiHandle task = vpi_handle_by_name(const_cast<char *>(name), nullptr);
    ASSERT_NE(task, nullptr);
    vpiHandle body = vpi_handle(vpiStmt, task);
    if (expected == vpiUndefined) {
      EXPECT_EQ(body, nullptr);
      EXPECT_EQ(vpi_chk_error(nullptr), 0);
    } else {
      ASSERT_NE(body, nullptr);
      EXPECT_EQ(vpi_get(vpiType, body), expected);
      EXPECT_EQ(vpi_release_handle(body), 1);
    }
    EXPECT_EQ(vpi_release_handle(task), 1);
  }
  EXPECT_EQ(vpi_release_handle(top), 1);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_simulation(runtime), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(runtime);
}
} // namespace
