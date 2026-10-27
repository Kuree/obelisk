//===- GeneratedVPIForeachTest.cpp - Fixed-array loop inventories ---------===//
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/Parser/Parser.h"
#include "talos/Conversion/SimulationToBytecode.h"
#include "talos/Dialect/Simulation/SimulationOps.h"
#include "talos/Reflection/DesignReflection.h"
#include "talos/Runtime/Runtime.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <cstdlib>
#include <map>
#include <vector>

namespace {
TEST(GeneratedDesignDatabase, FixedArrayForeach) {
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
  talos_rt_context *runtime = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &runtime),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_vpi_startup(runtime, nullptr, 0), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_vpi_end_compile(runtime), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_vpi_start_simulation(runtime), TALOS_RT_OK);
  std::vector<vpiHandle> handles;
  auto keep = [&](vpiHandle handle) {
    if (handle)
      handles.push_back(handle);
    return handle;
  };
  auto children = [&](vpiHandle owner, int selector) {
    std::vector<vpiHandle> result;
    auto iterator = vpi_iterate(selector, owner);
    if (!iterator)
      return result;
    auto use = keep(vpi_handle(vpiUse, iterator));
    EXPECT_EQ(vpi_compare_objects(use, owner), 1);
    while (auto child = vpi_scan(iterator))
      result.push_back(keep(child));
    return result;
  };
  auto top = keep(vpi_handle_by_name(const_cast<char *>("top"), nullptr));
  auto array = keep(vpi_handle_by_name(const_cast<char *>("top.a"), nullptr));
  ASSERT_NE(top, nullptr);
  ASSERT_NE(array, nullptr);
  auto initials = children(top, vpiProcess);
  ASSERT_EQ(initials.size(), 1u);
  ASSERT_EQ(vpi_get(vpiType, initials[0]), vpiInitial);
  auto block = keep(vpi_handle(vpiStmt, initials[0]));
  ASSERT_NE(block, nullptr);
  auto statements = children(block, vpiStmt);
  ASSERT_EQ(statements.size(), 4u);
  std::map<int, vpiHandle> loops;
  for (auto statement : statements) {
    ASSERT_EQ(vpi_get(vpiType, statement), vpiForeachStmt);
    loops[vpi_get(vpiLineNo, statement)] = statement;
    auto collection = children(statement, vpiVariables);
    ASSERT_EQ(collection.size(), 1u);
    auto expected = vpi_get(vpiLineNo, statement) == 40
                        ? keep(vpi_handle_by_name(
                              const_cast<char *>("top.packed"), nullptr))
                        : array;
    ASSERT_NE(expected, nullptr);
    EXPECT_EQ(vpi_compare_objects(collection[0], expected), 1);
  }
  ASSERT_EQ(loops.size(), 4u);
  for (int line : {10, 20, 30, 40})
    ASSERT_TRUE(loops.count(line));
  EXPECT_STREQ(vpi_get_str(vpiName, loops.at(10)), "sweep");
  EXPECT_STREQ(vpi_get_str(vpiName, loops.at(40)), "bits");
  EXPECT_EQ(vpi_get_str(vpiName, loops.at(20)), nullptr);
  EXPECT_EQ(vpi_get_str(vpiName, loops.at(30)), nullptr);
  auto packedIndices = children(loops.at(40), vpiLoopVars);
  ASSERT_EQ(packedIndices.size(), 1u);
  EXPECT_STREQ(vpi_get_str(vpiName, packedIndices[0]), "n");
  EXPECT_EQ(vpi_compare_objects(keep(vpi_handle(vpiScope, packedIndices[0])),
                                loops.at(40)),
            1);
  auto first = children(loops.at(10), vpiLoopVars);
  auto prefix = children(loops.at(20), vpiLoopVars);
  auto skipped = children(loops.at(30), vpiLoopVars);
  ASSERT_EQ(first.size(), 3u);
  ASSERT_EQ(prefix.size(), 1u);
  ASSERT_EQ(skipped.size(), 3u);
  auto repeated = children(loops.at(10), vpiLoopVars);
  ASSERT_EQ(repeated.size(), first.size());
  for (size_t i = 0; i != first.size(); ++i)
    EXPECT_EQ(vpi_compare_objects(first[i], repeated[i]), 1);
  for (auto [index, name] : {std::pair{0u, "i"}, std::pair{2u, "j"}}) {
    EXPECT_EQ(vpi_get(vpiType, first[index]), vpiIntVar);
    EXPECT_STREQ(vpi_get_str(vpiName, first[index]), name);
    EXPECT_EQ(vpi_get(vpiAutomatic, first[index]), 1);
    EXPECT_EQ(vpi_get(vpiSize, first[index]), 32);
    auto owner = keep(vpi_handle(vpiScope, first[index]));
    EXPECT_EQ(vpi_compare_objects(owner, loops.at(10)), 1);
    auto type = keep(vpi_handle(vpiTypespec, first[index]));
    ASSERT_NE(type, nullptr);
    EXPECT_EQ(vpi_get(vpiType, type), vpiIntTypespec);
  }
  EXPECT_STREQ(vpi_get_str(vpiName, prefix[0]), "k");
  auto prefixOwner = keep(vpi_handle(vpiScope, prefix[0]));
  EXPECT_EQ(vpi_compare_objects(prefixOwner, loops.at(20)), 1);
  auto body = keep(vpi_handle(vpiStmt, loops.at(10)));
  auto condition = keep(vpi_handle(vpiCondition, body));
  EXPECT_EQ(vpi_compare_objects(condition, first[0]), 1);
  skipped.push_back(first[1]);
  for (size_t i = 0; i != skipped.size(); ++i) {
    EXPECT_EQ(vpi_get(vpiType, skipped[i]), vpiOperation);
    EXPECT_EQ(vpi_get(vpiOpType, skipped[i]), vpiNullOp);
    EXPECT_STREQ(vpi_get_str(vpiOpType, skipped[i]), "vpiNullOp");
    EXPECT_EQ(vpi_iterate(vpiOperand, skipped[i]), nullptr);
    for (size_t j = 0; j != i; ++j)
      EXPECT_EQ(vpi_compare_objects(skipped[i], skipped[j]), 0);
  }
  using namespace talos::reflection;
  const auto &image = encoded->designDatabase;
  HeaderView header(image.data());
  SemanticDirectoryView directory(image.data() + header.getReserved());
  std::vector<uint32_t> nulls;
  for (uint32_t index = 0; index != directory.getStaticObjectCount(); ++index) {
    StaticObjectView row(image.data() + directory.getStaticObjectOffset() +
                         index * StaticObjectLayout.size);
    if (row.getVPIKind() == vpiOperation) {
      EXPECT_EQ(row.getName(), 0u);
      EXPECT_EQ(row.getFlags(), 0u);
      nulls.push_back((static_cast<uint32_t>(TableKind::StaticObject) << 30) |
                      index);
    }
  }
  ASSERT_EQ(nulls.size(), 4u);
  EXPECT_EQ(directory.getFrameLocalBindingCount(), 4u);
  auto write32 = [](auto &bytes, uint64_t offset, uint32_t value) {
    for (unsigned byte = 0; byte != 4; ++byte)
      bytes[offset + byte] = value >> (byte * 8);
  };
  auto reject = [&](auto bytes) {
    uint64_t hash = UINT64_C(14695981039346656037);
    for (uint64_t i = 0; i != bytes.size(); ++i) {
      hash ^= i >= field::HeaderChecksum && i < field::HeaderChecksum + 8
                  ? uint8_t{0}
                  : bytes[i];
      hash *= UINT64_C(1099511628211);
    }
    HeaderWriter(bytes.data()).setChecksum(hash);
    auto malformed = execution;
    malformed.design_database = bytes.data();
    malformed.design_database_size = bytes.size();
    EXPECT_EQ(talos_rt_v1_design_validate(&malformed),
              TALOS_RT_INVALID_DESIGN);
  };
  {
    auto bytes = image;
    write32(bytes,
            directory.getStaticObjectOffset() +
                unpackTableIndex(nulls[0]) * StaticObjectLayout.size + 16,
            1);
    reject(std::move(bytes));
  }
  uint64_t nullProperty = UINT64_MAX, firstNullRelation = UINT64_MAX;
  for (uint64_t i = 0; i != directory.getFixedPropertyCount(); ++i) {
    uint64_t offset =
        directory.getFixedPropertyOffset() + i * FixedPropertyLayout.size;
    FixedPropertyView property(image.data() + offset);
    if (property.getSourceIndexAndTable() == nulls[0] &&
        property.getSelector() == vpiOpType)
      nullProperty = offset;
  }
  ASSERT_NE(nullProperty, UINT64_MAX);
  {
    auto bytes = image;
    write32(bytes, nullProperty + 8, vpiAddOp);
    reject(std::move(bytes));
  }
  for (uint64_t i = 0; i != header.getRelationCount(); ++i) {
    uint64_t offset = header.getRelationOffset() + i * RelationLayout.size;
    RelationView row(image.data() + offset);
    if (row.getSelector() == vpiLoopVars &&
        row.getTargetIndexAndTable() == nulls[0])
      firstNullRelation = offset;
  }
  ASSERT_NE(firstNullRelation, UINT64_MAX);
  {
    auto bytes = image;
    write32(bytes, firstNullRelation + field::RelationTargetIndexAndTable,
            nulls[1]);
    reject(std::move(bytes));
  }
  {
    auto bytes = image;
    write32(bytes, firstNullRelation + field::RelationOrdinal, 10);
    reject(std::move(bytes));
  }
  {
    auto bytes = image;
    const uint64_t end =
        directory.getFixedPropertyOffset() +
        directory.getFixedPropertyCount() * FixedPropertyLayout.size;
    std::copy(bytes.begin() + nullProperty + FixedPropertyLayout.size,
              bytes.begin() + end, bytes.begin() + nullProperty);
    SemanticDirectoryWriter(bytes.data() + header.getReserved())
        .setFixedPropertyCount(directory.getFixedPropertyCount() - 1);
    reject(std::move(bytes));
  }
  std::vector<uint64_t> localRows, collectionRows;
  for (uint64_t i = 0; i != header.getRelationCount(); ++i) {
    uint64_t offset = header.getRelationOffset() + i * RelationLayout.size;
    RelationView row(image.data() + offset);
    if (row.getSelector() == vpiLoopVars &&
        std::find(nulls.begin(), nulls.end(), row.getTargetIndexAndTable()) ==
            nulls.end())
      localRows.push_back(offset);
    if (row.getSelector() == vpiVariables &&
        unpackRelationSourceTable(row.getSourceKindAndTable()) ==
            TableKind::Statement)
      collectionRows.push_back(offset);
  }
  ASSERT_EQ(localRows.size(), 4u);
  ASSERT_EQ(collectionRows.size(), 4u);
  {
    auto bytes = image;
    auto first =
        RelationView(image.data() + localRows.front()).getTargetIndexAndTable();
    auto last =
        RelationView(image.data() + localRows.back()).getTargetIndexAndTable();
    write32(bytes, localRows.front() + field::RelationTargetIndexAndTable,
            last);
    write32(bytes, localRows.back() + field::RelationTargetIndexAndTable,
            first);
    reject(std::move(
        bytes)); // Same physical scope and type, wrong exact loop owners.
  }
  {
    auto bytes = image;
    auto packed = RelationView(image.data() + collectionRows.back())
                      .getTargetIndexAndTable();
    write32(bytes, collectionRows.front() + field::RelationTargetIndexAndTable,
            packed);
    reject(std::move(bytes)); // Three slots cannot index one packed dimension.
  }
  {
    // Re-encode a source with only the collection relation retained, so no
    // orphan NullOp or local record independently causes the rejection.
    auto partial = mlir::cast<mlir::ModuleOp>(module->clone());
    auto partialDesign = *partial.getOps<talos::sim::SimDesignOp>().begin();
    talos::sim::SimVPIForeachDeclOp allSkipped;
    for (auto declaration :
         partialDesign.getOps<talos::sim::SimVPIForeachDeclOp>())
      if (llvm::all_of(declaration.getLoopVariables(),
                       [](mlir::Attribute slot) {
                         return mlir::isa<mlir::UnitAttr>(slot);
                       }))
        allSkipped = declaration;
    ASSERT_TRUE(allSkipped);
    mlir::OpBuilder builder(allSkipped);
    auto source = talos::sim::VPIObjectRefAttr::get(
        &context, talos::sim::VPIObjectRefKind::Statement,
        allSkipped.getStatementIdAttr());
    talos::sim::SimVPIRelationDeclOp::create(
        builder, allSkipped.getLoc(), source, vpiVariables,
        talos::sim::VPIRelationMode::Iterate, 0, allSkipped.getCollection());
    allSkipped.erase();
    auto incomplete = talos::encodeSimulationDesign(partialDesign, options);
    ASSERT_TRUE(mlir::succeeded(incomplete));
    auto malformed = execution;
    malformed.design_database = incomplete->designDatabase.data();
    malformed.design_database_size = incomplete->designDatabase.size();
    EXPECT_EQ(talos_rt_v1_design_validate(&malformed),
              TALOS_RT_INVALID_DESIGN);
    partial.erase();
  }
  for (auto handle : handles)
    vpi_release_handle(handle);
  talos_rt_v1_context_destroy(runtime);
}
} // namespace
