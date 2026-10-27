//===- GeneratedVPIDependentAliasesTest.cpp - Unresolved typedefs -------===//

#include "../lib/RuntimeInternal.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Parser/Parser.h"
#include "talos/Conversion/SimulationToBytecode.h"
#include "talos/Dialect/Simulation/SimulationOps.h"
#include "talos/Reflection/DesignReflection.h"
#include "talos/Reflection/VPIObjectModel.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "gtest/gtest.h"
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace {
using namespace talos::reflection;

TEST(GeneratedDesignDatabase, DependentTypeAliases) {
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
  uint64_t checksum = 0;
  for (unsigned byte = 0; byte != 8; ++byte)
    checksum |= uint64_t{encoded->bytecode[32 + byte]} << (byte * 8);
  const talos_rt_execution_descriptor_v1 execution{
      TALOS_RT_VERSION,
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
  auto lookup = [&](const char *name) {
    return keep(vpi_handle_by_name(const_cast<char *>(name), nullptr));
  };
  auto notice = [] {
    s_vpi_error_info error{};
    EXPECT_EQ(vpi_chk_error(&error), vpiNotice);
    EXPECT_EQ(error.level, vpiNotice);
  };
  auto definition = lookup("G");
  ASSERT_NE(definition, nullptr);
  EXPECT_EQ(vpi_iterate(vpiClassTypespec, definition), nullptr);
  EXPECT_EQ(vpi_iterate(vpiVariables, definition), nullptr);
  auto iterator = vpi_iterate(vpiParameter, definition);
  ASSERT_NE(iterator, nullptr);
  auto parameter = keep(vpi_scan(iterator));
  ASSERT_NE(parameter, nullptr);
  EXPECT_STREQ(vpi_get_str(vpiName, parameter), "T");
  EXPECT_EQ(vpi_get(vpiType, parameter), vpiTypeParameter);
  EXPECT_EQ(vpi_get(vpiLocalParam, parameter), 0);
  EXPECT_EQ(vpi_scan(iterator), nullptr);
  std::map<std::string, vpiHandle> aliases;
  iterator = vpi_iterate(vpiTypedef, definition);
  ASSERT_NE(iterator, nullptr);
  EXPECT_EQ(vpi_compare_objects(keep(vpi_handle(vpiUse, iterator)), definition),
            1);
  while (auto alias = vpi_scan(iterator)) {
    keep(alias);
    aliases.emplace(vpi_get_str(vpiName, alias), alias);
  }
  ASSERT_EQ(aliases.size(), 3u);
  ASSERT_TRUE(aliases.count("A"));
  ASSERT_TRUE(aliases.count("B.alias"));
  ASSERT_TRUE(aliases.count("Word"));
  auto a = aliases.at("A"), b = aliases.at("B.alias");
  for (auto alias : {a, b}) {
    EXPECT_EQ(vpi_get(vpiType, alias), vpiTypeParameter);
    EXPECT_EQ(vpi_compare_objects(alias, parameter), 0);
    EXPECT_EQ(
        vpi_compare_objects(keep(vpi_handle(vpiScope, alias)), definition), 1);
    EXPECT_EQ(vpi_handle(vpiTypespec, alias), nullptr);
    notice();
    EXPECT_EQ(vpi_handle(vpiExpr, alias), nullptr);
    notice();
    EXPECT_EQ(vpi_get(vpiLocalParam, alias), vpiUndefined);
    notice();
  }
  EXPECT_EQ(vpi_compare_objects(a, b), 0);
  EXPECT_EQ(
      vpi_compare_objects(keep(vpi_handle(vpiTypedefAlias, a)), parameter), 1);
  EXPECT_EQ(vpi_compare_objects(keep(vpi_handle(vpiTypedefAlias, b)), a), 1);
  EXPECT_EQ(vpi_compare_objects(lookup("G::B.alias"), b), 1);
  EXPECT_STREQ(vpi_get_str(vpiFullName, b), "G::B.alias");
  EXPECT_EQ(vpi_get(vpiType, aliases.at("Word")), vpiIntTypespec);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);

  auto cursor = [&](const char *name) {
    talos_rt_design_cursor_v1 result{};
    EXPECT_EQ(talos_rt_cached_design_lookup(
                  runtime, reinterpret_cast<const uint8_t *>(name),
                  std::char_traits<char>::length(name), &result),
              TALOS_RT_OK);
    return result;
  };
  const auto aCursor = cursor("G::A"), bCursor = cursor("G::B.alias"),
             gCursor = cursor("G"), tCursor = cursor("G::T"),
             hostCursor = cursor("host");
  for (auto handle : handles)
    EXPECT_EQ(vpi_release_handle(handle), 1);
  talos_rt_v1_context_destroy(runtime);

  const auto &image = encoded->designDatabase;
  HeaderView header(image.data());
  SemanticDirectoryView directory(image.data() + header.getReserved());
  auto packed = [&](talos_rt_design_cursor_v1 object) {
    uint32_t result = UINT32_MAX;
    if (object.offset >= header.getObjectOffset() &&
        object.offset < header.getObjectOffset() +
                            header.getObjectCount() * ObjectLayout.size) {
      EXPECT_TRUE(tryPackTableIndex(TableKind::Object,
                                    (object.offset - header.getObjectOffset()) /
                                        ObjectLayout.size,
                                    result));
    } else {
      EXPECT_GE(object.offset, directory.getStaticObjectOffset());
      EXPECT_LT(object.offset,
                directory.getStaticObjectOffset() +
                    directory.getStaticObjectCount() * StaticObjectLayout.size);
      EXPECT_TRUE(tryPackTableIndex(
          TableKind::StaticObject,
          (object.offset - directory.getStaticObjectOffset()) /
              StaticObjectLayout.size,
          result));
    }
    return result;
  };
  const auto aPacked = packed(aCursor), bPacked = packed(bCursor),
             gPacked = packed(gCursor), tPacked = packed(tCursor);
  EXPECT_EQ(ObjectView(image.data() + aCursor.offset).getCaps(),
            TALOS_RT_DESIGN_CAP_NAMED_TYPESPEC |
                TALOS_RT_DESIGN_CAP_UNRESOLVED_TYPE_ALIAS);
  auto relationRow = [&](uint32_t source, uint32_t selector) {
    for (uint64_t index = 0; index != header.getRelationCount(); ++index) {
      uint64_t row = header.getRelationOffset() + index * RelationLayout.size;
      RelationView relation(image.data() + row);
      uint32_t packedSource = 0;
      if (tryPackTableIndex(
              unpackRelationSourceTable(relation.getSourceKindAndTable()),
              relation.getSourceIndex(), packedSource) &&
          packedSource == source && relation.getSelector() == selector)
        return row;
    }
    return uint64_t{0};
  };
  const auto targetRow = relationRow(bPacked, vpiTypedefAlias),
             scopeRow = relationRow(bPacked, vpiScope),
             parameterRow = relationRow(gPacked, vpiParameter);
  ASSERT_NE(targetRow, 0u);
  ASSERT_NE(scopeRow, 0u);
  ASSERT_NE(parameterRow, 0u);
  ASSERT_EQ(RelationView(image.data() + targetRow).getTargetIndexAndTable(),
            aPacked);
  ASSERT_EQ(RelationView(image.data() + parameterRow).getTargetIndexAndTable(),
            tPacked);
  uint64_t nameRow = 0;
  for (uint64_t index = 0; index != directory.getFixedPropertyCount();
       ++index) {
    auto row =
        directory.getFixedPropertyOffset() + index * FixedPropertyLayout.size;
    FixedPropertyView property(image.data() + row);
    if (property.getSourceIndexAndTable() == aPacked &&
        property.getSelector() == vpiName)
      nameRow = row;
  }
  ASSERT_NE(nameRow, 0u);
  auto rejects = [&](auto corrupt) {
    std::vector<uint8_t> bytes(image.begin(), image.end());
    corrupt(bytes);
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
    invalid.design_database_size = bytes.size();
    EXPECT_EQ(talos_rt_v1_design_validate(&invalid),
              TALOS_RT_INVALID_DESIGN);
  };
  rejects([&](auto &bytes) {
    ObjectWriter(bytes.data() + aCursor.offset)
        .setCaps(TALOS_RT_DESIGN_CAP_NAMED_TYPESPEC);
  });
  rejects([&](auto &bytes) {
    ObjectWriter(bytes.data() + aCursor.offset)
        .setCaps(TALOS_RT_DESIGN_CAP_NAMED_TYPESPEC |
                 TALOS_RT_DESIGN_CAP_UNRESOLVED_TYPE_ALIAS |
                 TALOS_RT_DESIGN_CAP_UNRESOLVED_TYPE_PARAMETER);
  });
  // Keep the physical child chain valid while changing the alias's enclosing
  // scope. Its lexical Scope relation still names G, whose scope is $root.
  ASSERT_NE(ObjectView(image.data() + aCursor.offset).getOwner(),
            hostCursor.offset);
  rejects([&](auto &bytes) {
    const uint64_t oldOwner =
        ObjectView(bytes.data() + aCursor.offset).getOwner();
    const uint64_t next =
        ObjectView(bytes.data() + aCursor.offset).getNextSibling();
    uint64_t previous = 0;
    uint64_t child = ScopeView(bytes.data() + oldOwner).getFirstChild();
    auto isScope = [&](uint64_t offset) {
      return offset >= header.getScopeOffset() &&
             offset < header.getScopeOffset() +
                          header.getScopeCount() * ScopeLayout.size;
    };
    while (child && child != aCursor.offset) {
      previous = child;
      child = isScope(child)
                  ? ScopeView(bytes.data() + child).getNextSibling()
                  : ObjectView(bytes.data() + child).getNextSibling();
    }
    ASSERT_EQ(child, aCursor.offset);
    if (!previous)
      ScopeWriter(bytes.data() + oldOwner).setFirstChild(next);
    else if (isScope(previous))
      ScopeWriter(bytes.data() + previous).setNextSibling(next);
    else
      ObjectWriter(bytes.data() + previous).setNextSibling(next);
    ObjectWriter(bytes.data() + aCursor.offset).setOwner(hostCursor.offset);
    ObjectWriter(bytes.data() + aCursor.offset)
        .setNextSibling(
            ScopeView(bytes.data() + hostCursor.offset).getFirstChild());
    ScopeWriter(bytes.data() + hostCursor.offset).setFirstChild(aCursor.offset);
  });
  rejects([&](auto &bytes) {
    RelationWriter(bytes.data() + targetRow).setTargetIndexAndTable(bPacked);
  });
  rejects([&](auto &bytes) {
    RelationWriter(bytes.data() + scopeRow).setTargetIndexAndTable(tPacked);
  });
  rejects([&](auto &bytes) {
    RelationWriter(bytes.data() + parameterRow).setTargetIndexAndTable(aPacked);
  });
  rejects([&](auto &bytes) {
    FixedPropertyWriter(bytes.data() + nameRow)
        .setPayload(header.getStringOffset());
  });
  rejects([&](auto &bytes) {
    FixedPropertyWriter property(bytes.data() + nameRow);
    property.setSelector(vpiLocalParam);
    property.setKindAndFlags(
        static_cast<uint16_t>(VPIPropertyValueKind::Boolean));
    property.setPayload(1);
  });
}
} // namespace
