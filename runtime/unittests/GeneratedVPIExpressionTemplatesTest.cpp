//===- GeneratedVPIExpressionTemplatesTest.cpp - Shared expressions
//--------===//
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

TEST(GeneratedDesignDatabase, CompactExpressionTemplates) {
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
  EXPECT_EQ(directory.getExpressionTemplateCount(), 1u);
  EXPECT_EQ(directory.getExpressionNodeCount(), 7u);
  EXPECT_EQ(directory.getExpressionOperandCount(), 8u);
  EXPECT_EQ(directory.getExpressionLeafCount(), 4u);
  EXPECT_EQ(directory.getExpressionLeafShapeCount(), 2u);
  EXPECT_EQ(directory.getExpressionBoundMemberCount(), 0u);
  EXPECT_EQ(directory.getFrozenValueCount(), 4u);
  EXPECT_EQ(directory.getFrozenValueBindingCount(), 0u);
  uint64_t compactBytes =
      directory.getExpressionRootBindingCount() *
          ExpressionRootBindingLayout.size +
      directory.getExpressionTemplateCount() * ExpressionTemplateLayout.size +
      directory.getExpressionNodeCount() * ExpressionNodeLayout.size +
      directory.getExpressionOperandCount() * ExpressionOperandLayout.size +
      directory.getExpressionLeafCount() * ExpressionLeafLayout.size +
      directory.getExpressionLeafShapeCount() * ExpressionLeafShapeLayout.size;
  EXPECT_EQ(compactBytes, 364u);
  RecordProperty("image_bytes", image.size());
  RecordProperty("expression_rows_bytes", compactBytes);
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
  rejects([&](auto &bytes) {
    SemanticDirectoryWriter(bytes.data() + header.getReserved())
        .setExpressionOperandCount(UINT64_C(1) << 30);
  });
  rejects([&](auto &bytes) {
    ExpressionRootBindingWriter(bytes.data() +
                                directory.getExpressionRootBindingOffset())
        .setTemplateIndex(1);
  });
  rejects([&](auto &bytes) {
    ExpressionOperandWriter(bytes.data() +
                            directory.getExpressionOperandOffset())
        .setTarget(0);
  });
  rejects([&](auto &bytes) {
    ExpressionNodeWriter(bytes.data() + directory.getExpressionNodeOffset())
        .setOperandCount(1);
  });
  rejects([&](auto &bytes) {
    ExpressionLeafShapeWriter(bytes.data() +
                              directory.getExpressionLeafShapeOffset())
        .setKind(vpiParameter);
  });
  rejects([&](auto &bytes) {
    ExpressionLeafWriter(bytes.data() + directory.getExpressionLeafOffset())
        .setTarget(UINT32_MAX);
  });
  rejects([&](auto &bytes) {
    ExpressionRootBindingWriter(bytes.data() +
                                directory.getExpressionRootBindingOffset() +
                                ExpressionRootBindingLayout.size)
        .setOwnerIndexAndTable(
            ExpressionRootBindingView(
                image.data() + directory.getExpressionRootBindingOffset())
                .getOwnerIndexAndTable());
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
  auto value = [&](vpiHandle object) {
    s_vpi_value result{};
    result.format = vpiIntVal;
    vpi_get_value(object, &result);
    s_vpi_error_info error{};
    EXPECT_EQ(vpi_chk_error(&error), 0);
    return result.value.integer;
  };
  auto put = [&](vpiHandle object, int number) {
    s_vpi_value data{};
    data.format = vpiIntVal;
    data.value.integer = number;
    vpi_put_value(object, &data, nullptr, vpiNoDelay);
    s_vpi_error_info error{};
    EXPECT_EQ(vpi_chk_error(&error), 0);
  };
  auto left = named("left"), right = named("right");
  auto leftBits = named("left.bits"), rightBits = named("right.bits");
  ASSERT_NE(leftBits, nullptr);
  ASSERT_NE(rightBits, nullptr);
  put(leftBits, int(0xaabbccddu));
  put(rightBits, 0x11223344);
  auto statement = [&](vpiHandle scope) {
    auto iterator = vpi_iterate(vpiProcess, scope);
    auto process = iterator ? keep(vpi_scan(iterator)) : nullptr;
    EXPECT_NE(process, nullptr);
    if (iterator)
      EXPECT_EQ(vpi_scan(iterator), nullptr);
    return get(vpiStmt, process);
  };
  auto leftStatement = statement(left);
  auto rightStatement = statement(right);
  ASSERT_NE(leftStatement, nullptr);
  ASSERT_NE(rightStatement, nullptr);
  auto leftRhs = get(vpiRhs, leftStatement),
       rightRhs = get(vpiRhs, rightStatement);
  ASSERT_NE(leftRhs, nullptr);
  ASSERT_NE(rightRhs, nullptr);
  EXPECT_EQ(vpi_get(vpiType, leftRhs), vpiOperation);
  EXPECT_EQ(vpi_get(vpiOpType, leftRhs), vpiConcatOp);
  EXPECT_EQ(vpi_get(vpiSize, leftRhs), 16);
  EXPECT_EQ(vpi_compare_objects(leftRhs, rightRhs), 0);
  EXPECT_EQ(vpi_compare_objects(leftRhs, get(vpiRhs, leftStatement)), 1);
  EXPECT_EQ(value(leftRhs), 0xccdd);
  EXPECT_EQ(value(rightRhs), 0x3344);
  EXPECT_EQ(vpi_compare_objects(get(vpiModule, leftRhs), left), 1);
  EXPECT_EQ(vpi_compare_objects(get(vpiModule, rightRhs), right), 1);
  EXPECT_EQ(vpi_compare_objects(get(vpiInstance, leftRhs), left), 1);
  EXPECT_EQ(vpi_compare_objects(get(vpiInstance, rightRhs), right), 1);
  auto uses = vpi_iterate(vpiUse, leftBits);
  ASSERT_NE(uses, nullptr);
  EXPECT_EQ(vpi_compare_objects(get(vpiUse, uses), leftBits), 1);
  auto owner = keep(vpi_scan(uses));
  EXPECT_EQ(vpi_compare_objects(owner, leftStatement), 1);
  EXPECT_EQ(vpi_scan(uses),
            nullptr); // Two leaf occurrences, one owning statement.

  auto iterator = vpi_iterate(vpiOperand, leftRhs);
  ASSERT_NE(iterator, nullptr);
  EXPECT_EQ(vpi_compare_objects(get(vpiUse, iterator), leftRhs), 1);
  auto upper = keep(vpi_scan(iterator)), lower = keep(vpi_scan(iterator));
  ASSERT_NE(upper, nullptr);
  ASSERT_NE(lower, nullptr);
  EXPECT_EQ(vpi_scan(iterator), nullptr);
  EXPECT_EQ(vpi_get(vpiType, upper), vpiPartSelect);
  EXPECT_EQ(vpi_compare_objects(get(vpiParent, upper), leftBits), 1);
  EXPECT_EQ(value(upper), 0xcc);
  EXPECT_EQ(value(lower), 0xdd);
  EXPECT_EQ(value(get(vpiLeftRange, upper)), 15);
  EXPECT_EQ(value(get(vpiRightRange, upper)), 8);
  EXPECT_EQ(vpi_get(vpiConstType, get(vpiLeftRange, upper)), vpiIntConst);
  EXPECT_EQ(vpi_get(vpiConstantSelect, upper), 1);
  EXPECT_STREQ(vpi_get_str(vpiDecompile, upper),
               "\\bits [ 32'sb00000000000000000000000000001111 : "
               "32'sb00000000000000000000000000001000 ]");
  const char *decompiled = vpi_get_str(vpiDecompile, leftRhs);
  ASSERT_NE(decompiled, nullptr);
  EXPECT_EQ(std::string(decompiled),
            "{ \\bits [ 32'sb00000000000000000000000000001111 : "
            "32'sb00000000000000000000000000001000 ] , "
            "\\bits [ 32'sb00000000000000000000000000000111 : "
            "32'sb00000000000000000000000000000000 ] }");

  s_vpi_value rejectedWrite{};
  rejectedWrite.format = vpiIntVal;
  rejectedWrite.value.integer = 0;
  EXPECT_EQ(vpi_put_value(leftRhs, &rejectedWrite, nullptr, vpiNoDelay),
            nullptr);
  EXPECT_NE(vpi_chk_error(nullptr), 0);
  EXPECT_EQ(value(leftRhs), 0xccdd);

  put(leftBits, 0x12345678);
  EXPECT_EQ(value(leftRhs), 0x5678);
  EXPECT_EQ(value(rightRhs), 0x3344);
  for (auto handle : handles)
    vpi_release_handle(handle);
}

TEST(GeneratedDesignDatabase, ContextualExpressionParameters) {
  const char *input = std::getenv("OBELISK_TEST_INPUT");
  ASSERT_NE(input, nullptr);
  mlir::DialectRegistry registry;
  registry.insert<mlir::arith::ArithDialect, mlir::cf::ControlFlowDialect,
                  obelisk::sim::ObeliskSimulationDialect>();
  mlir::MLIRContext context(registry);
  auto module = mlir::parseSourceFile<mlir::ModuleOp>(input, &context);
  ASSERT_TRUE(module);
  obelisk::SimulationBytecodeOptions options;
  options.vpi = "read";
  auto encoded = obelisk::encodeSimulationDesign(
      *module->getOps<obelisk::sim::SimDesignOp>().begin(), options);
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
  SemanticDirectoryView directory(image.data() +
                                  HeaderView(image.data()).getReserved());
  EXPECT_EQ(directory.getDefinitionParameterValueCount(), 4u);
  EXPECT_EQ(directory.getExpressionRootBindingCount(), 7u);
  EXPECT_EQ(directory.getExpressionTemplateCount(),
            6u); // Effective widths split templates.
  EXPECT_EQ(directory.getExpressionBoundMemberCount(), 0u);
  // The public image schema also accepts canonical scalar Int without source
  // range metadata. Prove that encoding independently before exercising its
  // implicit descending range through the compact expression reader.
  auto implicitImage = image;
  unsigned implicitTypes = 0;
  for (uint32_t index = 0; index != directory.getSemanticTypeCount(); ++index) {
    const uint64_t offset =
        directory.getSemanticTypeOffset() + index * SemanticTypeLayout.size;
    SemanticTypeView semantic(image.data() + offset);
    if ((semantic.getKindAndFlags() & 0xff) != OBELISK_RT_DESIGN_SEMANTIC_INT)
      continue;
    SemanticTypeWriter writer(implicitImage.data() + offset);
    writer.setKindAndFlags(semantic.getKindAndFlags() &
                           ~OBELISK_RT_DESIGN_SEMANTIC_HAS_RANGE);
    writer.setLeft(0);
    writer.setRight(0);
    writer.setBitWidth(0);
    ++implicitTypes;
  }
  ASSERT_GT(implicitTypes, 0u);
  auto checksum = [](auto &bytes) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t index = 0; index != bytes.size(); ++index) {
      hash ^=
          index >= field::HeaderChecksum && index < field::HeaderChecksum + 8
              ? 0
              : bytes[index];
      hash *= UINT64_C(1099511628211);
    }
    HeaderWriter(bytes.data()).setChecksum(hash);
  };
  checksum(implicitImage);
  execution.design_database = implicitImage.data();
  ASSERT_EQ(obelisk_rt_v1_design_validate(&execution), OBELISK_RT_OK);
  // Swap the two otherwise valid bounds of one scalar Int part-select. Its
  // implicit [31:0] range still requires a descending selection.
  auto invalidBytes = implicitImage;
  bool corrupted = false;
  for (uint32_t index = 0; index != directory.getExpressionNodeCount();
       ++index) {
    ExpressionNodeView node(image.data() + directory.getExpressionNodeOffset() +
                            index * ExpressionNodeLayout.size);
    if (node.getKind() != vpiPartSelect)
      continue;
    // This node's left bound is the contextual N parameter; the other select
    // has two literal bounds.
    uint32_t templateOperands = 0;
    bool foundTemplate = false;
    for (uint32_t recipeIndex = 0;
         recipeIndex != directory.getExpressionTemplateCount(); ++recipeIndex) {
      ExpressionTemplateView recipe(
          image.data() + directory.getExpressionTemplateOffset() +
          recipeIndex * ExpressionTemplateLayout.size);
      if (index >= recipe.getFirstNode() &&
          index - recipe.getFirstNode() < recipe.getNodeCount()) {
        templateOperands = recipe.getFirstOperand();
        foundTemplate = true;
        break;
      }
    }
    ASSERT_TRUE(foundTemplate);
    uint64_t first = directory.getExpressionOperandOffset() +
                     (templateOperands + node.getFirstOperand() + 1) *
                         ExpressionOperandLayout.size;
    uint32_t left = ExpressionOperandView(image.data() + first).getTarget();
    if (!(left & (UINT32_C(1) << 31)))
      continue;
    uint32_t right = ExpressionOperandView(image.data() + first +
                                           ExpressionOperandLayout.size)
                         .getTarget();
    ExpressionOperandWriter(invalidBytes.data() + first).setTarget(right);
    ExpressionOperandWriter(invalidBytes.data() + first +
                            ExpressionOperandLayout.size)
        .setTarget(left);
    corrupted = true;
    break;
  }
  ASSERT_TRUE(corrupted);
  uint64_t hash = UINT64_C(14695981039346656037);
  for (size_t index = 0; index != invalidBytes.size(); ++index) {
    hash ^= index >= field::HeaderChecksum && index < field::HeaderChecksum + 8
                ? 0
                : invalidBytes[index];
    hash *= UINT64_C(1099511628211);
  }
  HeaderWriter(invalidBytes.data()).setChecksum(hash);
  auto invalid = execution;
  invalid.design_database = invalidBytes.data();
  EXPECT_EQ(obelisk_rt_v1_design_validate(&invalid), OBELISK_RT_INVALID_DESIGN);

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
    s_vpi_value result{};
    result.format = vpiIntVal;
    vpi_get_value(object, &result);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    return result.value.integer;
  };
  auto parameter = named("top.N"), word = named("top.Word");
  ASSERT_NE(parameter, nullptr);
  ASSERT_NE(word, nullptr);
  EXPECT_EQ(value(parameter), 2);
  EXPECT_EQ(value(word), 90);
  auto directStatement = get(vpiStmt, named("top.direct"));
  auto direct = get(vpiRhs, directStatement);
  ASSERT_NE(direct, nullptr);
  EXPECT_EQ(vpi_compare_objects(direct, parameter), 1);
  EXPECT_EQ(value(direct), 2);
  auto repeatStatement = get(vpiStmt, named("top.repeat"));
  auto repeated = get(vpiRhs, repeatStatement);
  auto partStatement = get(vpiStmt, named("top.part"));
  auto part = get(vpiRhs, partStatement);
  auto indexedStatement = get(vpiStmt, named("top.indexed"));
  auto indexed = get(vpiRhs, indexedStatement);
  ASSERT_NE(repeated, nullptr);
  ASSERT_NE(part, nullptr);
  ASSERT_NE(indexed, nullptr);
  EXPECT_EQ(value(repeated), 0xaa);
  EXPECT_EQ(value(part), 2);
  EXPECT_EQ(value(indexed), 1);
  auto differentRepeat = get(vpiRhs, get(vpiStmt, named("different.repeat")));
  auto differentPart = get(vpiRhs, get(vpiStmt, named("different.part")));
  auto differentIndexed = get(vpiRhs, get(vpiStmt, named("different.indexed")));
  ASSERT_NE(differentRepeat, nullptr);
  ASSERT_NE(differentPart, nullptr);
  ASSERT_NE(differentIndexed, nullptr);
  EXPECT_EQ(value(differentRepeat), 0xaaaa);
  EXPECT_EQ(value(differentPart), 0x1a);
  EXPECT_EQ(value(differentIndexed), 0xd);
  EXPECT_EQ(vpi_get(vpiSize, differentRepeat), 16);
  EXPECT_EQ(vpi_get(vpiSize, differentPart), 5);
  EXPECT_EQ(vpi_get(vpiSize, differentIndexed), 4);
  EXPECT_EQ(vpi_compare_objects(repeated, differentRepeat), 0);
  EXPECT_EQ(vpi_get(vpiSize, repeated), 8);
  EXPECT_EQ(vpi_get(vpiSize, part), 3);
  EXPECT_EQ(vpi_get(vpiConstantSelect, indexed), 1);
  EXPECT_EQ(vpi_compare_objects(get(vpiLeftRange, part), parameter), 1);
  EXPECT_EQ(vpi_compare_objects(get(vpiWidthExpr, indexed), parameter), 1);
  EXPECT_EQ(vpi_compare_objects(get(vpiParent, part), word), 1);
  auto operands = vpi_iterate(vpiOperand, repeated);
  ASSERT_NE(operands, nullptr);
  EXPECT_EQ(vpi_compare_objects(keep(vpi_scan(operands)), parameter), 1);
  EXPECT_NE(keep(vpi_scan(operands)), nullptr);
  EXPECT_EQ(vpi_scan(operands), nullptr);
  auto uses = vpi_iterate(vpiUse, parameter);
  ASSERT_NE(uses, nullptr);
  std::vector<vpiHandle> expectedUses{repeatStatement, partStatement,
                                      indexedStatement, directStatement};
  unsigned count = 0;
  while (auto use = vpi_scan(uses)) {
    keep(use);
    auto match = std::find_if(expectedUses.begin(), expectedUses.end(),
                              [&](vpiHandle expected) {
                                return vpi_compare_objects(use, expected) == 1;
                              });
    ASSERT_NE(match, expectedUses.end());
    expectedUses.erase(match);
    ++count;
  }
  EXPECT_EQ(count, 4u);
  EXPECT_TRUE(expectedUses.empty());
  EXPECT_STREQ(vpi_get_str(vpiDecompile, part),
               "\\Word [ \\N : 32'sb00000000000000000000000000000000 ]");
  for (auto handle : handles)
    vpi_release_handle(handle);
}

TEST(GeneratedDesignDatabase, WideExpressionTemplates) {
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
  auto leftBits = named("left.bits");
  auto process = named("left.initial");
  ASSERT_NE(leftBits, nullptr);
  ASSERT_NE(process, nullptr);
  auto expression = get(vpiRhs, get(vpiStmt, process));
  ASSERT_NE(expression, nullptr);
  EXPECT_EQ(vpi_get(vpiSize, expression), 130);
  std::array<s_vpi_vecval, 5> expected{{{0x12345678, 0x0000000f},
                                        {0x89abcdefu, 0x00f00000},
                                        {0x01234567, 0},
                                        {0xfedcba98u, 0x000f0000},
                                        {2, 1}}};
  s_vpi_value inputValue{};
  inputValue.format = vpiVectorVal;
  inputValue.value.vector = expected.data();
  vpi_put_value(leftBits, &inputValue, nullptr, vpiNoDelay);
  ASSERT_EQ(vpi_chk_error(nullptr), 0);
  auto check = [&](vpiHandle object, unsigned sourceOffset, unsigned width) {
    s_vpi_value result{};
    result.format = vpiVectorVal;
    vpi_get_value(object, &result);
    ASSERT_EQ(vpi_chk_error(nullptr), 0);
    ASSERT_NE(result.value.vector, nullptr);
    for (unsigned bit = 0; bit != width; ++bit) {
      unsigned from = sourceOffset + bit;
      EXPECT_EQ((uint32_t(result.value.vector[bit / 32].aval) >> (bit % 32)) &
                    1,
                (uint32_t(expected[from / 32].aval) >> (from % 32)) & 1);
      EXPECT_EQ((uint32_t(result.value.vector[bit / 32].bval) >> (bit % 32)) &
                    1,
                (uint32_t(expected[from / 32].bval) >> (from % 32)) & 1);
    }
  };
  check(expression, 0, 130);
  auto operands = vpi_iterate(vpiOperand, expression);
  ASSERT_NE(operands, nullptr);
  auto upper = keep(vpi_scan(operands)), lower = keep(vpi_scan(operands));
  ASSERT_NE(upper, nullptr);
  ASSERT_NE(lower, nullptr);
  EXPECT_EQ(vpi_scan(operands), nullptr);
  check(upper, 65, 65);
  check(lower, 0, 65);
  expected[2] = {0xdeadbeefu, 0xff000000u};
  vpi_put_value(leftBits, &inputValue, nullptr, vpiNoDelay);
  ASSERT_EQ(vpi_chk_error(nullptr), 0);
  check(expression, 0, 130);
  for (auto handle : handles)
    vpi_release_handle(handle);
}
