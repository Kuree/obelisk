//===- GeneratedVPIComputedBoundsTest.cpp - Constant bounds -----------===//

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

TEST(GeneratedDesignDatabase, ComputedBounds) {
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
  auto parameter = named("top.N");
  ASSERT_NE(parameter, nullptr);
  put(a, 0xa5);
  put(base, 1);
  std::map<int, vpiHandle> expressions;
  auto iterator = vpi_iterate(vpiProcess, top);
  ASSERT_NE(iterator, nullptr);
  while (auto process = keep(vpi_scan(iterator))) {
    auto statement = get(vpiStmt, process);
    int line = vpi_get(vpiLineNo, statement);
    auto expression = get(vpiRhs, statement);
    if (line == 60) {
      EXPECT_EQ(expression, nullptr);
      EXPECT_EQ(vpi_chk_error(nullptr), 0);
      continue;
    }
    ASSERT_NE(expression, nullptr);
    ASSERT_TRUE(expressions.emplace(line, expression).second);
  }
  ASSERT_EQ(expressions.size(), 4u);
  auto part = expressions.at(10), indexed = expressions.at(20);
  auto repeat = expressions.at(30), wide = expressions.at(40);
  EXPECT_EQ(vpi_get(vpiType, part), vpiPartSelect);
  EXPECT_EQ(vpi_get(vpiSize, part), 4);
  EXPECT_EQ(value(part), 0xa);
  EXPECT_EQ(value(indexed), 2);
  EXPECT_EQ(value(repeat), 0x55);
  EXPECT_EQ(value(wide), 0xa);
  auto left = get(vpiLeftRange, part), right = get(vpiRightRange, part);
  EXPECT_EQ(vpi_get(vpiType, left), vpiOperation);
  EXPECT_EQ(vpi_get(vpiOpType, left), vpiAddOp);
  EXPECT_EQ(value(left), 7);
  EXPECT_EQ(value(right), 4);
  auto operands = children(left);
  ASSERT_EQ(operands.size(), 2u);
  EXPECT_EQ(vpi_compare_objects(operands[0], parameter), 1);
  EXPECT_EQ(value(operands[1]), 3);
  auto width = get(vpiWidthExpr, indexed);
  EXPECT_EQ(vpi_get(vpiOpType, width), vpiMultOp);
  EXPECT_EQ(value(width), 4);
  operands = children(repeat);
  ASSERT_EQ(operands.size(), 2u);
  EXPECT_EQ(vpi_get(vpiOpType, operands[0]), vpiSubOp);
  EXPECT_EQ(value(operands[0]), 2);
  EXPECT_EQ(vpi_get(vpiSize, get(vpiLeftRange, wide)), 65);
  EXPECT_EQ(value(get(vpiLeftRange, wide)), 7);
  put(a, 0x3c);
  put(base, 4);
  EXPECT_EQ(value(part), 3);
  EXPECT_EQ(value(indexed), 3);
  EXPECT_EQ(value(repeat), 0xcc);
  EXPECT_EQ(value(wide), 3);
  for (auto [scope, expected, bound] :
       {std::tuple{"left", 2, 4}, std::tuple{"right", 9, 5}}) {
    auto inputName = std::string(scope) + ".a";
    put(named(inputName.c_str()), 0xa5);
    auto processes = vpi_iterate(vpiProcess, named(scope));
    ASSERT_NE(processes, nullptr);
    auto process = keep(vpi_scan(processes));
    ASSERT_NE(process, nullptr);
    EXPECT_EQ(vpi_scan(processes), nullptr);
    auto selected = get(vpiRhs, get(vpiStmt, process));
    EXPECT_EQ(value(selected), expected);
    EXPECT_EQ(value(get(vpiLeftRange, selected)), bound);
    auto operands = children(get(vpiLeftRange, selected));
    ASSERT_EQ(operands.size(), 2u);
    auto parameterName = std::string(scope) + ".N";
    EXPECT_EQ(vpi_compare_objects(operands[0], named(parameterName.c_str())),
              1);
  }
  // Every operation retains its syntax and has no frozen proof-value slot.
  unsigned arithmetic = 0;
  for (uint32_t index = 0; index != directory.getExpressionNodeCount();
       ++index) {
    ExpressionNodeView node(image.data() + directory.getExpressionNodeOffset() +
                            uint64_t(index) * ExpressionNodeLayout.size);
    if (node.getKind() == vpiOperation) {
      EXPECT_EQ(node.getFrozenValueIndex(), UINT32_MAX);
      arithmetic += node.getCode() != vpiMultiConcatOp;
    }
  }
  EXPECT_EQ(arithmetic, 8u);
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
  // A syntactically valid replacement operator changes the proven width.
  rejects([&](auto &bytes) {
    for (uint32_t index = 0; index != directory.getExpressionNodeCount();
         ++index) {
      auto offset = directory.getExpressionNodeOffset() +
                    uint64_t(index) * ExpressionNodeLayout.size;
      ExpressionNodeView node(bytes.data() + offset);
      if (node.getKind() == vpiOperation && node.getCode() == vpiMultOp) {
        ExpressionNodeWriter(bytes.data() + offset).setCode(vpiAddOp);
        return;
      }
    }
    FAIL() << "computed indexed width is missing";
  });
  // Corrupt a known four-state multiplier leaf into X. Its payload remains
  // a valid frozen value; the arithmetic width proof must reject it.
  rejects([&](auto &bytes) {
    for (uint32_t index = 0; index != directory.getFrozenValueCount();
         ++index) {
      FrozenValueView frozen(bytes.data() + directory.getFrozenValueOffset() +
                             uint64_t(index) * FrozenValueLayout.size);
      if (frozen.getBitWidth() == 32 &&
          (frozen.getKindAndFlags() & frozenValueFourState)) {
        bytes[directory.getFrozenValuePayloadOffset() +
              frozen.getPayloadOffset() + 4] = 1;
        return;
      }
    }
    FAIL() << "four-state multiplier leaf is missing";
  });
  // Locate the indexed width's nested arithmetic and its exact leaf slice.
  uint64_t nested = UINT64_MAX, operandOffset = 0, parameterLeaf = 0,
           parameterShape = 0, baseLeaf = 0, baseShape = 0;
  uint32_t nestedOrdinal = 0, wideType = UINT32_MAX;
  for (uint32_t index = 0; index != directory.getExpressionNodeCount();
       ++index) {
    ExpressionNodeView node(image.data() + directory.getExpressionNodeOffset() +
                            uint64_t(index) * ExpressionNodeLayout.size);
    SemanticTypeView type(image.data() + directory.getSemanticTypeOffset() +
                          uint64_t(node.getSemanticTypeIndex()) *
                              SemanticTypeLayout.size);
    if (type.getLeft() == 64)
      wideType = node.getSemanticTypeIndex();
  }
  for (uint32_t index = 0; index != directory.getExpressionRootBindingCount();
       ++index) {
    ExpressionRootBindingView root(
        image.data() + directory.getExpressionRootBindingOffset() +
        uint64_t(index) * ExpressionRootBindingLayout.size);
    ExpressionTemplateView recipe(
        image.data() + directory.getExpressionTemplateOffset() +
        uint64_t(root.getTemplateIndex()) * ExpressionTemplateLayout.size);
    auto nodeOffset = [&](uint32_t ordinal) {
      return directory.getExpressionNodeOffset() +
             uint64_t(recipe.getFirstNode() + ordinal) *
                 ExpressionNodeLayout.size;
    };
    ExpressionNodeView node(image.data() + nodeOffset(0));
    if (node.getKind() != vpiIndexedPartSelect)
      continue;
    auto operandAt = [&](ExpressionNodeView source, uint32_t child) {
      return directory.getExpressionOperandOffset() +
             uint64_t(recipe.getFirstOperand() + source.getFirstOperand() +
                      child) *
                 ExpressionOperandLayout.size;
    };
    uint32_t baseOrdinal =
        ExpressionOperandView(image.data() + operandAt(node, 1)).getTarget() &
        0x7fffffff;
    uint32_t widthOrdinal =
        ExpressionOperandView(image.data() + operandAt(node, 2)).getTarget();
    ExpressionNodeView widthNode(image.data() + nodeOffset(widthOrdinal));
    nestedOrdinal =
        ExpressionOperandView(image.data() + operandAt(widthNode, 0))
            .getTarget();
    nested = nodeOffset(nestedOrdinal);
    ExpressionNodeView inner(image.data() + nested);
    operandOffset = operandAt(inner, 0);
    uint32_t leafOrdinal =
        ExpressionOperandView(image.data() + operandOffset).getTarget() &
        0x7fffffff;
    parameterLeaf =
        directory.getExpressionLeafOffset() +
        uint64_t(root.getFirstLeaf() + leafOrdinal) * ExpressionLeafLayout.size;
    baseLeaf =
        directory.getExpressionLeafOffset() +
        uint64_t(root.getFirstLeaf() + baseOrdinal) * ExpressionLeafLayout.size;
    parameterShape = directory.getExpressionLeafShapeOffset() +
                     uint64_t(recipe.getFirstLeafShape() + leafOrdinal) *
                         ExpressionLeafShapeLayout.size;
    baseShape = directory.getExpressionLeafShapeOffset() +
                uint64_t(recipe.getFirstLeafShape() + baseOrdinal) *
                    ExpressionLeafShapeLayout.size;
  }
  ASSERT_NE(nested, UINT64_MAX);
  ASSERT_NE(wideType, UINT32_MAX);
  rejects([&](auto &bytes) {
    ExpressionNodeWriter(bytes.data() + nested).setSemanticTypeIndex(wideType);
  });
  rejects([&](auto &bytes) {
    ExpressionOperandWriter(bytes.data() + operandOffset)
        .setTarget(nestedOrdinal);
  });
  rejects([&](auto &bytes) {
    // Keep a matching, valid IntVar leaf shape; mutable storage cannot prove
    // a constant width even when its current value happens to match.
    std::copy_n(bytes.data() + baseLeaf, ExpressionLeafLayout.size,
                bytes.data() + parameterLeaf);
    std::copy_n(bytes.data() + baseShape, ExpressionLeafShapeLayout.size,
                bytes.data() + parameterShape);
  });
  for (auto handle : handles)
    vpi_free_object(handle);
  EXPECT_EQ(obelisk_rt_v1_vpi_end_simulation(raw), OBELISK_RT_OK);
}
