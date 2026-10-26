//===- GeneratedVPISourceOnlyExpressionsTest.cpp - Source syntax -----------===//

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Parser/Parser.h"
#include "obelisk/Conversion/SimulationToBytecode.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Reflection/DesignReflection.h"
#include "obelisk/Runtime/Runtime.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <memory>
#include <vector>

TEST(GeneratedDesignDatabase, SourceOnlyExpressions) {
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
  using namespace obelisk::reflection;
  const auto &image = encoded->designDatabase;
  HeaderView header(image.data());
  SemanticDirectoryView directory(image.data() + header.getReserved());
  EXPECT_EQ(directory.getExpressionRootBindingCount(), 2u);
  EXPECT_EQ(directory.getExpressionNodeCount(), 8u);
  unsigned sourceNodes = 0;
  uint32_t sourceNode = UINT32_MAX, literalNode = UINT32_MAX;
  for (uint32_t index = 0; index != directory.getExpressionNodeCount();
       ++index) {
    ExpressionNodeView node(image.data() + directory.getExpressionNodeOffset() +
                            index * ExpressionNodeLayout.size);
    if (node.getKind() == vpiOperation) {
      EXPECT_EQ(node.getSemanticTypeIndex(), UINT32_MAX);
      ++sourceNodes;
      sourceNode = index;
    } else {
      EXPECT_NE(node.getSemanticTypeIndex(), UINT32_MAX);
      literalNode = index;
    }
  }
  EXPECT_EQ(sourceNodes, 6u);
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
  ASSERT_NE(literalNode, UINT32_MAX);
  rejects([&](auto &bytes) {
    ExpressionNodeWriter(bytes.data() + directory.getExpressionNodeOffset() +
                         literalNode * ExpressionNodeLayout.size)
        .setSemanticTypeIndex(UINT32_MAX);
  });
  rejects([&](auto &bytes) {
    ExpressionNodeWriter(bytes.data() + directory.getExpressionNodeOffset() +
                         sourceNode * ExpressionNodeLayout.size)
        .setCode(24);
  });
  rejects([&](auto &bytes) {
    // A typed value concat cannot consume the retained source-only sum.
    ExpressionNodeView literal(image.data() +
                               directory.getExpressionNodeOffset() +
                               literalNode * ExpressionNodeLayout.size);
    ExpressionNodeWriter writer(bytes.data() +
                                directory.getExpressionNodeOffset());
    writer.setCode(33);
    writer.setSemanticTypeIndex(literal.getSemanticTypeIndex());
  });
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
  auto named = [&](const char *name) {
    return keep(vpi_handle_by_name(const_cast<char *>(name), nullptr));
  };
  auto get = [&](int selector, vpiHandle source) {
    return keep(vpi_handle(selector, source));
  };
  auto arithmetic = get(vpiRhs, get(vpiStmt, named("top.arithmetic")));
  auto events = get(vpiCondition, get(vpiStmt, named("top.event")));
  ASSERT_NE(arithmetic, nullptr);
  ASSERT_NE(events, nullptr);
  EXPECT_EQ(vpi_get(vpiOpType, arithmetic), vpiMultOp);
  EXPECT_EQ(vpi_get(vpiOpType, events), vpiEventOrOp);
  EXPECT_STREQ(vpi_get_str(vpiDecompile, arithmetic),
               "(\\word + 32'sb00000000000000000000000000000010) * "
               "32'sb00000000000000000000000000000011");
  EXPECT_STREQ(vpi_get_str(vpiDecompile, events),
               "posedge \\word iff \\word or negedge \\word ");
  for (auto expression : {arithmetic, events}) {
    EXPECT_EQ(vpi_get(vpiSize, expression), vpiUndefined);
    s_vpi_error_info error{};
    ASSERT_NE(vpi_chk_error(&error), 0);
    EXPECT_EQ(error.level, vpiNotice);
    EXPECT_EQ(get(vpiTypespec, expression), nullptr);
    ASSERT_NE(vpi_chk_error(&error), 0);
    EXPECT_EQ(error.level, vpiNotice);
    s_vpi_value value{};
    value.format = vpiIntVal;
    vpi_get_value(expression, &value);
    ASSERT_NE(vpi_chk_error(&error), 0);
    EXPECT_EQ(error.level, vpiNotice);
  }
  auto operands = vpi_iterate(vpiOperand, arithmetic);
  ASSERT_NE(operands, nullptr);
  auto sum = keep(vpi_scan(operands));
  auto three = keep(vpi_scan(operands));
  EXPECT_EQ(vpi_scan(operands), nullptr);
  ASSERT_NE(sum, nullptr);
  ASSERT_NE(three, nullptr);
  EXPECT_EQ(vpi_get(vpiOpType, sum), vpiAddOp);
  EXPECT_EQ(vpi_get(vpiSize, three), 32);
  s_vpi_value literal{};
  literal.format = vpiIntVal;
  vpi_get_value(three, &literal);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);
  EXPECT_EQ(literal.value.integer, 3);
  for (auto handle : handles)
    vpi_release_handle(handle);
}
