//===- GeneratedVPICaseExpressionsTest.cpp - Case expressions -------------===//
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

TEST(GeneratedDesignDatabase, CaseItemExpressions) {
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
  bool unavailable = std::getenv("OBELISK_TEST_UNAVAILABLE_CASE") != nullptr;
  using namespace obelisk::reflection;
  const auto &image = encoded->designDatabase;
  HeaderView header(image.data());
  SemanticDirectoryView directory(image.data() + header.getReserved());
  ASSERT_EQ(directory.getExpressionRootBindingCount(), unavailable ? 0u : 6u);
  // The two equal literals retain their distinct source locations.
  EXPECT_EQ(directory.getExpressionTemplateCount(), unavailable ? 0u : 3u);
  EXPECT_EQ(directory.getExpressionNodeCount(), unavailable ? 0u : 5u);
  if (!unavailable) {
    auto rejects = [&](auto mutate) {
      auto bytes = image;
      mutate(bytes);
      uint64_t hash = UINT64_C(14695981039346656037);
      for (size_t index = 0; index != bytes.size(); ++index) {
        hash ^=
            index >= field::HeaderChecksum && index < field::HeaderChecksum + 8
                ? 0
                : bytes[index];
        hash *= UINT64_C(1099511628211);
      }
      HeaderWriter(bytes.data()).setChecksum(hash);
      auto invalid = execution;
      invalid.design_database = bytes.data();
      EXPECT_EQ(obelisk_rt_v1_design_validate(&invalid),
                OBELISK_RT_INVALID_DESIGN);
    };
    auto rootOffset = directory.getExpressionRootBindingOffset();
    rejects([&](auto &bytes) {
      ExpressionRootBindingWriter(bytes.data() + rootOffset).setOrdinal(1);
    });
    rejects([&](auto &bytes) {
      ExpressionRootBindingWriter(bytes.data() + rootOffset +
                                  ExpressionRootBindingLayout.size)
          .setOrdinal(0);
    });
    rejects([&](auto &bytes) {
      ExpressionRootBindingWriter(bytes.data() + rootOffset).setMode(0);
    });
    rejects([&](auto &bytes) {
      ExpressionRootBindingWriter(bytes.data() + rootOffset).setMode(2);
    });
  }
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
  auto a = keep(vpi_handle_by_name(const_cast<char *>("top.a"), nullptr));
  auto parameter =
      keep(vpi_handle_by_name(const_cast<char *>("top.P"), nullptr));
  auto selector =
      keep(vpi_handle_by_name(const_cast<char *>("top.selector"), nullptr));
  ASSERT_NE(top, nullptr);
  ASSERT_NE(a, nullptr);
  ASSERT_NE(parameter, nullptr);
  ASSERT_NE(selector, nullptr);
  auto processes = vpi_iterate(vpiProcess, top);
  ASSERT_NE(processes, nullptr);
  auto process = keep(vpi_scan(processes));
  ASSERT_NE(process, nullptr);
  EXPECT_EQ(vpi_scan(processes), nullptr);
  auto statement = keep(vpi_handle(vpiStmt, process));
  ASSERT_NE(statement, nullptr);
  EXPECT_EQ(vpi_get(vpiType, statement), vpiCase);
  auto condition = keep(vpi_handle(vpiCondition, statement));
  ASSERT_NE(condition, nullptr);
  EXPECT_EQ(vpi_compare_objects(condition, selector), 1);
  auto items = vpi_iterate(vpiCaseItem, statement);
  ASSERT_NE(items, nullptr);
  if (unavailable) {
    unsigned count = 0;
    while (auto item = keep(vpi_scan(items))) {
      ++count;
      EXPECT_EQ(vpi_iterate(vpiExpr, item), nullptr);
      auto body = keep(vpi_handle(vpiStmt, item));
      ASSERT_NE(body, nullptr);
      EXPECT_EQ(vpi_get(vpiType, body), vpiNullStmt);
    }
    EXPECT_EQ(count, 2u);
    EXPECT_EQ(vpi_iterate(vpiUse, a), nullptr);
    EXPECT_EQ(vpi_iterate(vpiUse, parameter), nullptr);
    for (auto handle : handles)
      EXPECT_EQ(vpi_free_object(handle), 1);
    EXPECT_EQ(obelisk_rt_v1_vpi_end_simulation(raw), OBELISK_RT_OK);
    return;
  }
  vpiHandle labeled = nullptr;
  unsigned itemCount = 0, defaultCount = 0;
  std::vector<vpiHandle> constants;
  while (auto item = keep(vpi_scan(items))) {
    ++itemCount;
    auto body = keep(vpi_handle(vpiStmt, item));
    ASSERT_NE(body, nullptr);
    EXPECT_EQ(vpi_get(vpiType, body), vpiNullStmt);
    auto labels = vpi_iterate(vpiExpr, item);
    if (!labels) {
      ++defaultCount;
      continue;
    }
    ASSERT_EQ(labeled, nullptr);
    labeled = item;
    unsigned count = 0, variableCount = 0, parameterCount = 0,
             operationCount = 0;
    while (auto label = keep(vpi_scan(labels))) {
      ++count;
      int kind = vpi_get(vpiType, label);
      s_vpi_value value{};
      value.format = vpiIntVal;
      vpi_get_value(label, &value);
      EXPECT_EQ(vpi_chk_error(nullptr), 0);
      if (vpi_compare_objects(label, a)) {
        ++variableCount;
        EXPECT_EQ(value.value.integer, 0);
      } else if (vpi_compare_objects(label, parameter)) {
        ++parameterCount;
        EXPECT_EQ(value.value.integer, 9);
      } else if (kind == vpiConstant) {
        EXPECT_EQ(value.value.integer, 1);
        for (auto previous : constants)
          EXPECT_EQ(vpi_compare_objects(previous, label), 0);
        constants.push_back(label);
      } else {
        EXPECT_EQ(kind, vpiOperation);
        EXPECT_EQ(vpi_get(vpiOpType, label), vpiConcatOp);
        EXPECT_EQ(value.value.integer, 6);
        ++operationCount;
        auto operands = vpi_iterate(vpiOperand, label);
        ASSERT_NE(operands, nullptr);
        for (int expected : {1, 2}) {
          auto operand = keep(vpi_scan(operands));
          ASSERT_NE(operand, nullptr);
          EXPECT_EQ(vpi_get(vpiType, operand), vpiConstant);
          value.format = vpiIntVal;
          vpi_get_value(operand, &value);
          EXPECT_EQ(value.value.integer, expected);
        }
        EXPECT_EQ(vpi_scan(operands), nullptr);
      }
    }
    EXPECT_EQ(count, 6u);
    EXPECT_EQ(variableCount, 2u);
    EXPECT_EQ(parameterCount, 1u);
    EXPECT_EQ(operationCount, 1u);
  }
  EXPECT_EQ(itemCount, 2u);
  EXPECT_EQ(defaultCount, 1u);
  EXPECT_EQ(constants.size(), 2u);
  ASSERT_NE(labeled, nullptr);
  // Repeated source references retain every Expr slot, but Use is a set of
  // containing statements and must expose the enclosing Case once.
  for (auto declaration : {a, parameter}) {
    auto uses = vpi_iterate(vpiUse, declaration);
    ASSERT_NE(uses, nullptr);
    auto use = keep(vpi_scan(uses));
    ASSERT_NE(use, nullptr);
    EXPECT_EQ(vpi_compare_objects(use, statement), 1);
    EXPECT_EQ(vpi_scan(uses), nullptr);
  }
  for (auto handle : handles)
    EXPECT_EQ(vpi_free_object(handle), 1);
  EXPECT_EQ(obelisk_rt_v1_vpi_end_simulation(raw), OBELISK_RT_OK);
}
