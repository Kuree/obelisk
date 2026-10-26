//===- GeneratedVPISharedParameterDefaultsTest.cpp - Original defaults
//-----===//

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Parser/Parser.h"
#include "obelisk/Conversion/SimulationToBytecode.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Reflection/DesignReflection.h"
#include "obelisk/Runtime/Runtime.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "gtest/gtest.h"
#include <array>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
TEST(GeneratedDesignDatabase, SharedParameterDefaults) {
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
  std::vector<vpiHandle> handles;
  auto keep = [&](vpiHandle handle) {
    if (handle)
      handles.push_back(handle);
    return handle;
  };
  auto named = [&](const std::string &name) {
    return keep(vpi_handle_by_name(const_cast<char *>(name.c_str()), nullptr));
  };
  auto related = [&](int selector, vpiHandle owner) {
    return keep(vpi_handle(selector, owner));
  };
  auto children = [&](int selector, vpiHandle owner) {
    std::vector<vpiHandle> result;
    if (auto iterator = vpi_iterate(selector, owner))
      while (auto child = vpi_scan(iterator))
        result.push_back(keep(child));
    return result;
  };
  auto value = [&](vpiHandle object) {
    s_vpi_value result{};
    result.format = vpiIntVal;
    vpi_get_value(object, &result);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    return result.value.integer;
  };
  const std::array<const char *, 3> instances{"left", "right", "different"};
  const std::array<int, 3> parameterValues{3, 3, 5};
  const std::array<int, 3> overriddenValues{99, 99, 101};
  std::array<vpiHandle, 3> literals{}, expressions{}, parameters{};
  for (unsigned index = 0; index != instances.size(); ++index) {
    std::string prefix = instances[index];
    auto scope = named(prefix);
    auto p = named(prefix + ".P");
    auto q = named(prefix + ".Q");
    auto r = named(prefix + ".R");
    auto equal = named(prefix + ".Equal");
    ASSERT_NE(scope, nullptr);
    ASSERT_NE(p, nullptr);
    ASSERT_NE(q, nullptr);
    ASSERT_NE(r, nullptr);
    ASSERT_NE(equal, nullptr);
    parameters[index] = p;
    EXPECT_EQ(value(p), parameterValues[index]);
    EXPECT_EQ(vpi_get(vpiSize, p), index == 2 ? 8 : 32);
    EXPECT_EQ(value(q), overriddenValues[index]);
    auto literal = related(vpiExpr, p);
    ASSERT_NE(literal, nullptr);
    literals[index] = literal;
    EXPECT_EQ(vpi_get(vpiType, literal), vpiConstant);
    EXPECT_EQ(vpi_get(vpiConstType, literal), vpiIntConst);
    EXPECT_EQ(vpi_get(vpiSize, literal), 32);
    EXPECT_EQ(vpi_get(vpiSigned, literal), 1);
    EXPECT_EQ(value(literal), 1);
    auto expression = related(vpiExpr, q);
    ASSERT_NE(expression, nullptr);
    expressions[index] = expression;
    EXPECT_EQ(vpi_get(vpiType, expression), vpiOperation);
    EXPECT_EQ(vpi_get(vpiOpType, expression), vpiAddOp);
    EXPECT_STREQ(vpi_get_str(vpiDecompile, expression),
                 "\\P + 32'sb00000000000000000000000000000001");
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    auto operands = children(vpiOperand, expression);
    ASSERT_EQ(operands.size(), 2u);
    EXPECT_EQ(vpi_compare_objects(operands[0], p), 1);
    EXPECT_EQ(value(operands[0]), parameterValues[index]);
    EXPECT_EQ(value(operands[1]), 1);
    EXPECT_EQ(vpi_compare_objects(operands[1], literal), 0);
    EXPECT_EQ(vpi_compare_objects(related(vpiExpr, r), q), 1);
    auto equalExpression = related(vpiExpr, equal);
    ASSERT_NE(equalExpression, nullptr);
    EXPECT_EQ(vpi_compare_objects(equalExpression, expression), 0);
    auto equalOperands = children(vpiOperand, equalExpression);
    ASSERT_EQ(equalOperands.size(), 2u);
    EXPECT_EQ(vpi_compare_objects(equalOperands[0], p), 1);
    EXPECT_EQ(vpi_compare_objects(equalOperands[1], operands[1]), 0);
    EXPECT_EQ(vpi_get(vpiSize, expression), vpiUndefined);
    s_vpi_error_info error{};
    ASSERT_NE(vpi_chk_error(&error), 0);
    EXPECT_EQ(error.level, vpiNotice);
    s_vpi_value unavailable{};
    unavailable.format = vpiIntVal;
    vpi_get_value(expression, &unavailable);
    ASSERT_NE(vpi_chk_error(&error), 0);
    EXPECT_EQ(error.level, vpiNotice);
    EXPECT_EQ(related(vpiExpr, named(prefix + ".Absent")), nullptr);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    EXPECT_EQ(related(vpiExpr, named(prefix + ".Unknown")), nullptr);
    ASSERT_NE(vpi_chk_error(&error), 0);
    EXPECT_EQ(error.level, vpiNotice);
    EXPECT_EQ(related(vpiExpr, named(prefix + ".Late")), nullptr);
    ASSERT_NE(vpi_chk_error(&error), 0);
    EXPECT_EQ(error.level, vpiNotice);
    EXPECT_EQ(vpi_compare_objects(related(vpiModule, expression), scope), 1);
    EXPECT_EQ(vpi_compare_objects(related(vpiInstance, expression), scope), 1);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
  }
  for (unsigned index = 1; index != instances.size(); ++index) {
    EXPECT_EQ(vpi_compare_objects(parameters[0], parameters[index]), 0);
    EXPECT_EQ(vpi_compare_objects(literals[0], literals[index]), 0);
    EXPECT_EQ(vpi_compare_objects(expressions[0], expressions[index]), 0);
  }
  for (auto handle : handles)
    vpi_free_object(handle);
  EXPECT_EQ(obelisk_rt_v1_vpi_end_simulation(runtime), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(runtime);

  using namespace obelisk::reflection;
  HeaderView header(encoded->designDatabase.data());
  SemanticDirectoryView directory(encoded->designDatabase.data() +
                                  header.getReserved());
  // Four original defaults and their five computed occurrences are retained
  // once, despite three contexts and two effective type/value specializations.
  EXPECT_EQ(directory.getExpressionRootBindingCount(), 4u);
  EXPECT_EQ(directory.getExpressionNodeCount(), 5u);
  EXPECT_EQ(directory.getExpressionLeafCount(), 3u);
  RecordProperty("design_database_bytes", encoded->designDatabase.size());
  auto reject = [&](auto mutate) {
    std::vector<uint8_t> bytes(encoded->designDatabase.begin(),
                               encoded->designDatabase.end());
    mutate(bytes);
    HeaderWriter writer(bytes.data());
    writer.setChecksum(0);
    uint64_t hash = UINT64_C(14695981039346656037);
    for (uint8_t byte : bytes) {
      hash ^= byte;
      hash *= UINT64_C(1099511628211);
    }
    writer.setChecksum(hash);
    auto malformed = execution;
    malformed.design_database = bytes.data();
    malformed.design_database_size = bytes.size();
    EXPECT_EQ(obelisk_rt_v1_design_validate(&malformed),
              OBELISK_RT_INVALID_DESIGN);
  };
  uint64_t roots = directory.getExpressionRootBindingOffset();
  uint64_t nodes = directory.getExpressionNodeOffset();
  uint64_t members = directory.getDefinitionMemberOffset();
  auto rootAt = [&](uint32_t index) {
    return ExpressionRootBindingView(encoded->designDatabase.data() + roots +
                                     index * ExpressionRootBindingLayout.size);
  };
  ASSERT_EQ(rootAt(0).getOwnerIndexAndTable(), 0u);
  ASSERT_EQ(rootAt(1).getOwnerIndexAndTable(), 1u);
  ASSERT_EQ(rootAt(2).getTemplateIndex(), UINT32_MAX);
  reject([&](auto &bytes) {
    ExpressionRootBindingWriter(bytes.data() + roots).setMode(8);
  });
  reject([&](auto &bytes) {
    DefinitionMemberWriter(bytes.data() + members)
        .setFlags((1u << 10) | (1u << 11));
  });
  reject([&](auto &bytes) {
    ExpressionRootBindingWriter(bytes.data() + roots).setOwnerIndexAndTable(4);
  });
  reject([&](auto &bytes) {
    // Absent has valid effective values and no source graph: the independent
    // availability fact alone must require a definition-owned Expr root.
    uint64_t absent = members + 4 * DefinitionMemberLayout.size;
    DefinitionMemberView member(bytes.data() + absent);
    ASSERT_EQ(member.getVPIKind(), uint16_t(vpiParameter));
    DefinitionMemberWriter(bytes.data() + absent)
        .setFlags(member.getFlags() | (1u << 11));
  });
  reject([&](auto &bytes) {
    ExpressionRootBindingWriter(bytes.data() + roots)
        .setTemplateIndex(rootAt(1).getTemplateIndex());
  });
  reject([&](auto &bytes) {
    ExpressionNodeWriter(bytes.data() + nodes).setSemanticTypeIndex(UINT32_MAX);
  });
  reject([&](auto &bytes) {
    // Event-or has the same arity as addition but is not a value default.
    auto recipe = rootAt(1).getTemplateIndex();
    ExpressionTemplateView type(encoded->designDatabase.data() +
                                directory.getExpressionTemplateOffset() +
                                recipe * ExpressionTemplateLayout.size);
    ExpressionNodeWriter(bytes.data() + nodes +
                         type.getFirstNode() * ExpressionNodeLayout.size)
        .setCode(vpiEventOrOp);
  });
  reject([&](auto &bytes) {
    // Q's original expression cannot refer to Q itself.
    auto firstLeaf = rootAt(1).getFirstLeaf();
    ExpressionLeafWriter(bytes.data() + directory.getExpressionLeafOffset() +
                         firstLeaf * ExpressionLeafLayout.size)
        .setTarget(1);
  });
  reject([&](auto &bytes) {
    ExpressionLeafShapeWriter(bytes.data() +
                              directory.getExpressionLeafShapeOffset())
        .setSemanticTypeIndex(0);
  });
  reject([&](auto &bytes) {
    // Removing a root leaves its independent declaration fact unsatisfied.
    std::memmove(bytes.data() + roots,
                 bytes.data() + roots + ExpressionRootBindingLayout.size,
                 (directory.getExpressionRootBindingCount() - 1) *
                     ExpressionRootBindingLayout.size);
    SemanticDirectoryWriter(bytes.data() + header.getReserved())
        .setExpressionRootBindingCount(
            directory.getExpressionRootBindingCount() - 1);
  });

  // Encode actual 1/32/128-instance designs. Source recipe cost stays constant;
  // only existing scope/binding/name records grow with instance count.
  auto validateEncoded = [&](const auto &design) {
    auto descriptor = execution;
    descriptor.flags = design.executionFlags;
    descriptor.bytecode = design.bytecode.data();
    descriptor.bytecode_size = design.bytecode.size();
    descriptor.design_database = design.designDatabase.data();
    descriptor.design_database_size = design.designDatabase.size();
    descriptor.state_bit_count = design.stateBitCount;
    descriptor.checksum = 0;
    for (unsigned byte = 0; byte != 8; ++byte)
      descriptor.checksum |= uint64_t{design.bytecode[32 + byte]} << (byte * 8);
    return obelisk_rt_v1_design_validate(&descriptor);
  };
  uint64_t recipeBytes = 0, extraImageBytes = 0;
  for (uint32_t count : {1u, 32u, 128u}) {
    mlir::OwningOpRef<mlir::ModuleOp> clone(
        mlir::cast<mlir::ModuleOp>(module->clone()));
    auto design = *clone->getOps<obelisk::sim::SimDesignOp>().begin();
    obelisk::sim::SimScopeDeclOp prototype;
    for (auto scope : llvm::make_early_inc_range(
             design.getOps<obelisk::sim::SimScopeDeclOp>())) {
      if (scope.getId() == 1)
        prototype = scope;
      else if (scope.getId())
        scope.erase();
    }
    ASSERT_TRUE(prototype);
    auto specialization = prototype.getVpiSpecializationAttr();
    for (auto binding : llvm::make_early_inc_range(
             design.getOps<
                 obelisk::sim::SimVPIDefinitionMemberSpecializationOp>()))
      if (binding.getSpecializationAttr() != specialization)
        binding.erase();
    for (auto type : llvm::make_early_inc_range(
             design
                 .getOps<obelisk::sim::SimVPIDefinitionSpecializationDeclOp>()))
      if (mlir::FlatSymbolRefAttr::get(type) != specialization)
        type.erase();
    for (auto anchor : llvm::make_early_inc_range(
             design.getOps<obelisk::sim::SimVPIObjectAnchorOp>())) {
      ASSERT_EQ(anchor.getVpiKind(), uint32_t(vpiModule));
      anchor.erase();
    }
    mlir::OpBuilder builder(&context);
    builder.setInsertionPointAfter(prototype);
    for (uint32_t instance = 1; instance != count; ++instance) {
      auto scope =
          mlir::cast<obelisk::sim::SimScopeDeclOp>(builder.clone(*prototype));
      scope.setIdAttr(builder.getI64IntegerAttr(instance + 1));
      scope.setHierarchicalNameAttr(
          builder.getStringAttr("copy" + std::to_string(instance)));
    }
    ASSERT_TRUE(mlir::succeeded(mlir::verify(*clone)));
    auto shared = obelisk::encodeSimulationDesign(design, options);
    ASSERT_TRUE(mlir::succeeded(shared));
    ASSERT_EQ(validateEncoded(*shared), OBELISK_RT_OK);
    HeaderView sharedHeader(shared->designDatabase.data());
    SemanticDirectoryView sharedDirectory(shared->designDatabase.data() +
                                          sharedHeader.getReserved());
    EXPECT_EQ(sharedDirectory.getDefinitionBindingCount(), count);
    EXPECT_EQ(sharedDirectory.getExpressionRootBindingCount(), 4u);
    EXPECT_EQ(sharedDirectory.getExpressionNodeCount(), 5u);
    uint64_t rows =
        sharedDirectory.getExpressionRootBindingCount() *
            ExpressionRootBindingLayout.size +
        sharedDirectory.getExpressionTemplateCount() *
            ExpressionTemplateLayout.size +
        sharedDirectory.getExpressionNodeCount() * ExpressionNodeLayout.size +
        sharedDirectory.getExpressionOperandCount() *
            ExpressionOperandLayout.size +
        sharedDirectory.getExpressionLeafCount() * ExpressionLeafLayout.size +
        sharedDirectory.getExpressionLeafShapeCount() *
            ExpressionLeafShapeLayout.size;
    if (count == 1)
      recipeBytes = rows;
    EXPECT_EQ(rows, recipeBytes);
    for (auto expression : llvm::make_early_inc_range(
             design.getOps<obelisk::sim::SimVPIExpressionDeclOp>()))
      expression.erase();
    for (auto relation : llvm::make_early_inc_range(
             design.getOps<obelisk::sim::SimVPIRelationDeclOp>()))
      relation.erase();
    for (auto member :
         design.getOps<obelisk::sim::SimVPIDefinitionMemberDeclOp>())
      if (member.getHasOriginalDefaultAttr()) {
        member.removeHasOriginalDefaultAttr();
        member.setUnavailableDefaultAttr(builder.getUnitAttr());
      }
    ASSERT_TRUE(mlir::succeeded(mlir::verify(*clone)));
    auto baseline = obelisk::encodeSimulationDesign(design, options);
    ASSERT_TRUE(mlir::succeeded(baseline));
    ASSERT_EQ(validateEncoded(*baseline), OBELISK_RT_OK);
    ASSERT_GT(shared->designDatabase.size(), baseline->designDatabase.size());
    uint64_t growth =
        shared->designDatabase.size() - baseline->designDatabase.size();
    if (count == 1)
      extraImageBytes = growth;
    EXPECT_EQ(growth, extraImageBytes);
    RecordProperty("bytes_" + std::to_string(count),
                   shared->designDatabase.size());
  }
  RecordProperty("shared_default_recipe_bytes", recipeBytes);
  RecordProperty("shared_default_image_growth", extraImageBytes);
}
} // namespace
