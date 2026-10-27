//===- GeneratedVPIProceduralTypesTest.cpp - Exact procedural types -------===//
#include "../lib/RuntimeInternal.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Parser/Parser.h"
#include "talos/Conversion/SimulationToBytecode.h"
#include "talos/Dialect/Simulation/SimulationOps.h"
#include "talos/Reflection/DesignReflection.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <memory>
#include <tuple>
#include <vector>
namespace {
TEST(GeneratedDesignDatabase, ProceduralTypes) {
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
  options.vpi = "full";
  auto encoded = talos::encodeSimulationDesign(design, options);
  ASSERT_TRUE(mlir::succeeded(encoded));
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.flags = encoded->executionFlags;
  execution.bytecode = encoded->bytecode.data();
  execution.bytecode_size = encoded->bytecode.size();
  execution.design_database = encoded->designDatabase.data();
  execution.design_database_size = encoded->designDatabase.size();
  execution.state_bit_count = encoded->stateBitCount;
  for (unsigned byte = 0; byte != 8; ++byte)
    execution.checksum |= uint64_t{encoded->bytecode[32 + byte]} << (byte * 8);
  ASSERT_EQ(talos_rt_v1_design_validate(&execution), TALOS_RT_OK);
  talos_rt_context *raw = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &raw),
            TALOS_RT_OK);
  std::unique_ptr<talos_rt_context, decltype(&talos_rt_v1_context_destroy)>
      runtime(raw, talos_rt_v1_context_destroy);
  ASSERT_EQ(talos_rt_v1_vpi_startup(raw, nullptr, 0), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_vpi_end_compile(raw), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_vpi_start_simulation(raw), TALOS_RT_OK);
  std::vector<vpiHandle> handles;
  auto keep = [&](vpiHandle handle) {
    if (handle)
      handles.push_back(handle);
    return handle;
  };
  auto children = [&](vpiHandle parent, int selector) {
    std::vector<vpiHandle> result;
    auto iterator = vpi_iterate(selector, parent);
    if (!iterator)
      return result;
    auto use = keep(vpi_handle(vpiUse, iterator));
    EXPECT_EQ(vpi_compare_objects(use, parent), 1);
    while (auto child = vpi_scan(iterator))
      result.push_back(keep(child));
    return result;
  };
  auto function =
      keep(vpi_handle_by_name(const_cast<char *>("top.work"), nullptr));
  ASSERT_NE(function, nullptr);
  EXPECT_EQ(vpi_get(vpiType, function), vpiFunction);
  auto outer = keep(vpi_handle(vpiStmt, function));
  ASSERT_NE(outer, nullptr);
  EXPECT_EQ(vpi_get(vpiType, outer), vpiBegin);
  auto statements = children(outer, vpiStmt);
  ASSERT_EQ(statements.size(), 1u);
  auto inner = statements[0];
  EXPECT_EQ(vpi_get(vpiType, inner), vpiBegin);
  auto functionAliases = children(function, vpiTypedef);
  auto outerAliases = children(outer, vpiTypedef);
  auto innerAliases = children(inner, vpiTypedef);
  ASSERT_EQ(functionAliases.size(), 1u);
  ASSERT_EQ(outerAliases.size(), 1u);
  ASSERT_EQ(innerAliases.size(), 1u);
  EXPECT_EQ(vpi_get(vpiType, functionAliases[0]), vpiIntTypespec);
  EXPECT_EQ(vpi_get(vpiType, outerAliases[0]), vpiEnumTypespec);
  EXPECT_EQ(vpi_get(vpiType, innerAliases[0]), vpiIntTypespec);
  for (auto alias : {functionAliases[0], outerAliases[0], innerAliases[0]})
    EXPECT_STREQ(vpi_get_str(vpiName, alias), "same_t");
  EXPECT_EQ(vpi_compare_objects(functionAliases[0], innerAliases[0]), 0);
  // Unnamed blocks share display hierarchy. Their local typedefs are reached
  // through their exact lexical scope, not duplicated in the global index.
  auto named =
      keep(vpi_handle_by_name(const_cast<char *>("top.work.same_t"), nullptr));
  EXPECT_EQ(vpi_compare_objects(named, functionAliases[0]), 1);
  auto outerVariables = children(outer, vpiVariables);
  auto innerVariables = children(inner, vpiVariables);
  ASSERT_EQ(outerVariables.size(), 1u);
  ASSERT_EQ(innerVariables.size(), 2u);
  EXPECT_STREQ(vpi_get_str(vpiName, outerVariables[0]), "state");
  EXPECT_STREQ(vpi_get_str(vpiName, innerVariables[0]), "value");
  EXPECT_STREQ(vpi_get_str(vpiName, innerVariables[1]), "anonymous");
  auto declaredState = keep(vpi_handle(vpiTypespec, outerVariables[0]));
  auto declaredValue = keep(vpi_handle(vpiTypespec, innerVariables[0]));
  auto anonymous = keep(vpi_handle(vpiTypespec, innerVariables[1]));
  EXPECT_EQ(vpi_compare_objects(declaredState, outerAliases[0]), 1);
  EXPECT_EQ(vpi_compare_objects(declaredValue, innerAliases[0]), 1);
  ASSERT_NE(anonymous, nullptr);
  EXPECT_EQ(vpi_get(vpiType, anonymous), vpiEnumTypespec);
  EXPECT_EQ(vpi_compare_objects(anonymous, declaredState), 0);
  auto namedValues = children(declaredState, vpiEnumConst);
  auto anonymousValues = children(anonymous, vpiEnumConst);
  ASSERT_EQ(namedValues.size(), 1u);
  ASSERT_EQ(anonymousValues.size(), 1u);
  EXPECT_STREQ(vpi_get_str(vpiName, namedValues[0]), "A.dot");
  EXPECT_STREQ(vpi_get_str(vpiName, anonymousValues[0]), "B");
  s_vpi_value value{};
  value.format = vpiDecStrVal;
  vpi_get_value(namedValues[0], &value);
  EXPECT_STREQ(value.value.str, "-2");
  value.format = vpiVectorVal;
  vpi_get_value(namedValues[0], &value);
  ASSERT_NE(value.value.vector, nullptr);
  EXPECT_EQ(value.value.vector[0].aval, 510u);
  EXPECT_EQ(value.value.vector[0].bval, 0u);
  value.format = vpiIntVal;
  vpi_get_value(anonymousValues[0], &value);
  EXPECT_EQ(value.value.integer, 2);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);
  for (auto local : {outerVariables[0], innerVariables[0], innerVariables[1]}) {
    EXPECT_EQ(vpi_get(vpiHasActual, local), 0);
    value.value.integer = 777;
    vpi_get_value(local, &value);
    EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
    EXPECT_EQ(value.value.integer, 777);
  }
  // Removing the scope fact cannot turn a statement into a typedef owner.
  using namespace talos::reflection;
  HeaderView header(encoded->designDatabase.data());
  uint64_t statementOffset = 0;
  for (uint64_t index = 0; index != header.getStatementCount(); ++index) {
    uint64_t offset =
        header.getStatementOffset() + index * StatementLayout.size;
    if (StatementView(encoded->designDatabase.data() + offset).getID() == 1)
      statementOffset = offset;
  }
  ASSERT_NE(statementOffset, 0u);
  auto malformed = encoded->designDatabase;
  StatementWriter writer(malformed.data() + statementOffset);
  writer.setFlags(StatementView(malformed.data() + statementOffset).getFlags() &
                  ~TALOS_RT_DESIGN_STATEMENT_SCOPE);
  auto rehash = [](auto &image) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i != image.size(); ++i) {
      hash ^= i >= 32 && i < 40 ? 0 : image[i];
      hash *= UINT64_C(1099511628211);
    }
    HeaderWriter(image.data()).setChecksum(hash);
  };
  rehash(malformed);
  auto invalid = execution;
  invalid.design_database = malformed.data();
  EXPECT_EQ(talos_rt_v1_design_validate(&invalid), TALOS_RT_INVALID_DESIGN);
  // Duplicate one block typedef into another exact lexical scope. Both
  // original declarations stay reachable, so only ownership becomes invalid.
  using Row = std::array<uint8_t, RelationLayout.size>;
  std::vector<Row> relations;
  Row outerTypedef{}, innerTypedef{};
  bool haveOuter = false, haveInner = false;
  for (uint64_t i = 0; i != header.getRelationCount(); ++i) {
    Row row;
    std::copy_n(encoded->designDatabase.data() + header.getRelationOffset() +
                    i * RelationLayout.size,
                RelationLayout.size, row.data());
    RelationView view(row.data());
    if (view.getSelector() == vpiTypedef &&
        unpackRelationSourceTable(view.getSourceKindAndTable()) ==
            TableKind::Statement) {
      auto id = StatementView(encoded->designDatabase.data() +
                              header.getStatementOffset() +
                              view.getSourceIndex() * StatementLayout.size)
                    .getID();
      if (id == 1) {
        outerTypedef = row;
        haveOuter = true;
      }
      if (id == 2) {
        innerTypedef = row;
        haveInner = true;
      }
    }
    relations.push_back(row);
  }
  ASSERT_TRUE(haveOuter && haveInner);
  auto validateRelations = [&](std::vector<Row> rows) {
    auto key = [](const Row &row) {
      RelationView r(row.data());
      return std::tuple(unpackRelationSourceTable(r.getSourceKindAndTable()),
                        r.getSourceIndex(), r.getSelector(),
                        relationSourceIsIterate(r.getSourceKindAndTable()),
                        r.getOrdinal());
    };
    std::sort(rows.begin(), rows.end(),
              [&](const Row &a, const Row &b) { return key(a) < key(b); });
    auto image = encoded->designDatabase;
    image.resize((image.size() + 7) & ~size_t{7}, 0);
    uint64_t offset = image.size();
    for (const Row &row : rows)
      image.insert(image.end(), row.begin(), row.end());
    HeaderWriter(image.data()).setRelationOffset(offset);
    HeaderWriter(image.data()).setRelationCount(rows.size());
    HeaderWriter(image.data()).setImageSize(image.size());
    rehash(image);
    auto descriptor = execution;
    descriptor.design_database = image.data();
    descriptor.design_database_size = image.size();
    return talos_rt_v1_design_validate(&descriptor);
  };
  EXPECT_EQ(validateRelations(relations), TALOS_RT_OK);
  Row duplicate = outerTypedef;
  RelationWriter(duplicate.data())
      .setSourceIndex(RelationView(innerTypedef.data()).getSourceIndex());
  RelationWriter(duplicate.data()).setOrdinal(1);
  auto duplicateOwners = relations;
  duplicateOwners.push_back(duplicate);
  EXPECT_EQ(validateRelations(duplicateOwners), TALOS_RT_INVALID_DESIGN);
  // A typed frozen enum value requires its exact owning enum and reverse
  // declaration inventory; dropping only the handle edge is not valid.
  auto missingEnumOwner = relations;
  auto ownerEdge = std::find_if(
      missingEnumOwner.begin(), missingEnumOwner.end(), [](const Row &row) {
        RelationView r(row.data());
        return unpackRelationSourceKind(r.getSourceKindAndTable()) ==
                   vpiEnumConst &&
               r.getSelector() == vpiEnumTypespec;
      });
  ASSERT_NE(ownerEdge, missingEnumOwner.end());
  missingEnumOwner.erase(ownerEdge);
  EXPECT_EQ(validateRelations(missingEnumOwner), TALOS_RT_INVALID_DESIGN);
  for (auto handle : handles)
    EXPECT_EQ(vpi_release_handle(handle), 1);
  handles.clear();
  runtime.reset();
  // Old authored enum declarations carry spelling only. They remain valid
  // metadata and report an unavailable value instead of decoding that text.
  for (auto constant : design.getOps<talos::sim::SimVPIEnumConstDeclOp>())
    constant.removeImmutableValueAttr();
  auto legacy = talos::encodeSimulationDesign(design, options);
  ASSERT_TRUE(mlir::succeeded(legacy));
  execution.bytecode = legacy->bytecode.data();
  execution.bytecode_size = legacy->bytecode.size();
  execution.design_database = legacy->designDatabase.data();
  execution.design_database_size = legacy->designDatabase.size();
  execution.checksum = 0;
  for (unsigned byte = 0; byte != 8; ++byte)
    execution.checksum |= uint64_t{legacy->bytecode[32 + byte]} << (byte * 8);
  ASSERT_EQ(talos_rt_v1_design_validate(&execution), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &raw),
            TALOS_RT_OK);
  runtime.reset(raw);
  ASSERT_EQ(talos_rt_v1_vpi_startup(raw, nullptr, 0), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_vpi_end_compile(raw), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_vpi_start_simulation(raw), TALOS_RT_OK);
  function = keep(vpi_handle_by_name(const_cast<char *>("top.work"), nullptr));
  outer = keep(vpi_handle(vpiStmt, function));
  outerAliases = children(outer, vpiTypedef);
  ASSERT_EQ(outerAliases.size(), 1u);
  namedValues = children(outerAliases[0], vpiEnumConst);
  ASSERT_EQ(namedValues.size(), 1u);
  value.format = vpiIntVal;
  value.value.integer = 777;
  vpi_get_value(namedValues[0], &value);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
  EXPECT_EQ(value.value.integer, 777);
  for (auto handle : handles)
    EXPECT_EQ(vpi_release_handle(handle), 1);
}
} // namespace
