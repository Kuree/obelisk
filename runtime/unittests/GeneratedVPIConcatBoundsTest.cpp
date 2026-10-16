//===- GeneratedVPIConcatBoundsTest.cpp - Concatenated bounds -----------===//

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
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

TEST(GeneratedDesignDatabase, ConcatBounds) {
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
  auto value = [&](vpiHandle object) {
    EXPECT_NE(object, nullptr);
    s_vpi_value data{};
    data.format = vpiIntVal;
    vpi_get_value(object, &data);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    return data.value.integer;
  };
  auto put = [&](vpiHandle object, int number) {
    s_vpi_value data{};
    data.format = vpiIntVal;
    data.value.integer = number;
    vpi_put_value(object, &data, nullptr, vpiNoDelay);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
  };
  auto children = [&](vpiHandle object) {
    std::vector<vpiHandle> result;
    if (auto iterator = vpi_iterate(vpiOperand, object))
      while (auto child = keep(vpi_scan(iterator)))
        result.push_back(child);
    return result;
  };
  auto top = named("top"), a = named("top.a"), base = named("top.base");
  put(a, 0xa5);
  put(base, 1);
  std::map<int, vpiHandle> expressions;
  auto iterator = vpi_iterate(vpiProcess, top);
  ASSERT_NE(iterator, nullptr);
  while (auto process = keep(vpi_scan(iterator))) {
    auto statement = get(vpiStmt, process);
    auto expression = get(vpiRhs, statement);
    ASSERT_NE(expression, nullptr);
    ASSERT_TRUE(
        expressions.emplace(vpi_get(vpiLineNo, statement), expression).second);
  }
  ASSERT_EQ(expressions.size(), 4u);
  auto part = expressions.at(10), indexed = expressions.at(20);
  auto repeat = expressions.at(30), wide = expressions.at(40);
  EXPECT_EQ(value(part), 0xa);
  EXPECT_EQ(value(indexed), 2);
  EXPECT_EQ(value(repeat), 0x55);
  EXPECT_EQ(value(wide), 0xa);
  auto left = get(vpiLeftRange, part), right = get(vpiRightRange, part);
  EXPECT_EQ(vpi_get(vpiOpType, left), vpiConcatOp);
  EXPECT_EQ(vpi_get(vpiOpType, right), vpiConcatOp);
  EXPECT_EQ(value(left), 7);
  EXPECT_EQ(value(right), 4);
  auto operands = children(left);
  ASSERT_EQ(operands.size(), 4u);
  EXPECT_EQ(value(operands[0]), 0);
  EXPECT_EQ(value(operands[1]), 1);
  EXPECT_EQ(value(operands[2]), 1);
  EXPECT_EQ(value(operands[3]), 1);
  auto width = get(vpiWidthExpr, indexed);
  EXPECT_EQ(vpi_get(vpiOpType, width), vpiAddOp);
  EXPECT_EQ(value(width), 4);
  operands = children(width);
  ASSERT_EQ(operands.size(), 2u);
  EXPECT_EQ(vpi_get(vpiOpType, operands[0]), vpiMultiConcatOp);
  EXPECT_EQ(value(operands[0]), 3);
  operands = children(repeat);
  ASSERT_EQ(operands.size(), 2u);
  EXPECT_EQ(vpi_get(vpiOpType, operands[0]), vpiConcatOp);
  EXPECT_EQ(value(operands[0]), 2);
  EXPECT_EQ(vpi_get(vpiSize, get(vpiLeftRange, wide)), 67);
  EXPECT_EQ(value(get(vpiLeftRange, wide)), 7);
  put(a, 0x3c);
  put(base, 4);
  EXPECT_EQ(value(part), 3);
  EXPECT_EQ(value(indexed), 3);
  EXPECT_EQ(value(repeat), 0xcc);
  EXPECT_EQ(value(wide), 3);
  // The same template reads each instance's exact parameter; no parameter's
  // effective value is frozen into a shared concatenation node.
  for (auto [scope, expected, bound] :
       {std::tuple{"left", 9, 5}, std::tuple{"right", 2, 4}}) {
    auto inputName = std::string(scope) + ".a";
    put(named(inputName.c_str()), 0xa5);
    auto processes = vpi_iterate(vpiProcess, named(scope));
    ASSERT_NE(processes, nullptr);
    auto process = keep(vpi_scan(processes));
    ASSERT_NE(process, nullptr);
    EXPECT_EQ(vpi_scan(processes), nullptr);
    auto selected = get(vpiRhs, get(vpiStmt, process));
    EXPECT_EQ(value(selected), expected);
    auto concatenation = get(vpiLeftRange, selected);
    EXPECT_EQ(vpi_get(vpiOpType, concatenation), vpiConcatOp);
    EXPECT_EQ(value(concatenation), bound);
    auto operands = children(concatenation);
    ASSERT_EQ(operands.size(), 2u);
    auto parameterName = std::string(scope) + ".N";
    EXPECT_EQ(vpi_compare_objects(operands[1], named(parameterName.c_str())),
              1);
  }
  unsigned concatenations = 0;
  uint64_t concatOffset = UINT64_MAX;
  uint32_t wrongType = UINT32_MAX;
  for (uint32_t index = 0; index != directory.getExpressionNodeCount();
       ++index) {
    auto offset = directory.getExpressionNodeOffset() +
                  uint64_t(index) * ExpressionNodeLayout.size;
    ExpressionNodeView node(image.data() + offset);
    if (node.getKind() == vpiOperation) {
      EXPECT_EQ(node.getFrozenValueIndex(), UINT32_MAX);
      concatenations +=
          node.getCode() == vpiConcatOp || node.getCode() == vpiMultiConcatOp;
      if (node.getCode() == vpiConcatOp)
        concatOffset = offset;
    }
    if (node.getKind() == vpiPartSelect)
      wrongType = node.getSemanticTypeIndex();
  }
  EXPECT_EQ(concatenations, 8u);
  EXPECT_EQ(directory.getExpressionRootBindingCount(), 6u);
  EXPECT_EQ(directory.getExpressionTemplateCount(), 5u);
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
  ASSERT_NE(concatOffset, UINT64_MAX);
  ASSERT_NE(wrongType, UINT32_MAX);
  rejects([&](auto &bytes) {
    ExpressionNodeWriter(bytes.data() + concatOffset)
        .setSemanticTypeIndex(wrongType);
  });
  // Only the immutable multiplier has a four-state two-bit frozen leaf.
  uint64_t multiplierPayload = UINT64_MAX;
  for (uint32_t index = 0; index != directory.getFrozenValueCount(); ++index) {
    FrozenValueView frozen(image.data() + directory.getFrozenValueOffset() +
                           uint64_t(index) * FrozenValueLayout.size);
    if (frozen.getBitWidth() == 2 &&
        (frozen.getKindAndFlags() & frozenValueFourState)) {
      multiplierPayload =
          directory.getFrozenValuePayloadOffset() + frozen.getPayloadOffset();
      break;
    }
  }
  ASSERT_NE(multiplierPayload, UINT64_MAX);
  rejects([&](auto &bytes) { bytes[multiplierPayload + 1] = 1; });
  rejects([&](auto &bytes) { bytes[multiplierPayload] = 0; });
  for (auto handle : handles)
    vpi_free_object(handle);
  EXPECT_EQ(obelisk_rt_v1_vpi_end_simulation(raw), OBELISK_RT_OK);
}
