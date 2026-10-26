//===- GeneratedVPIStaticLocalsTest.cpp - Procedural static scopes -------===//
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
#include <algorithm>
#include <array>
#include <cstdlib>
#include <memory>
#include <string>
#include <tuple>
#include <vector>
namespace {
void checkStaticLocals(bool sourceTypes) {
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

  if (sourceTypes) {
    EXPECT_EQ(vpi_get(vpiType, outer), vpiBegin);
    auto nested = children(outer, vpiStmt);
    ASSERT_EQ(nested.size(), 1u);
    auto inner = nested[0];
    auto outerValues = children(outer, vpiVariables);
    auto innerValues = children(inner, vpiVariables);
    ASSERT_EQ(outerValues.size(), 1u);
    ASSERT_EQ(innerValues.size(), 2u);
    auto outerAliases = children(outer, vpiTypedef);
    auto innerAliases = children(inner, vpiTypedef);
    ASSERT_EQ(outerAliases.size(), 1u);
    ASSERT_EQ(innerAliases.size(), 1u);
    const char *names[] = {"state", "value", "anonymous"};
    std::vector<vpiHandle> values{outerValues[0], innerValues[0],
                                  innerValues[1]};
    for (size_t index = 0; index != values.size(); ++index) {
      EXPECT_STREQ(vpi_get_str(vpiName, values[index]), names[index]);
      EXPECT_EQ(vpi_get(vpiAutomatic, values[index]), 0);
      EXPECT_EQ(vpi_compare_objects(keep(vpi_handle(vpiScope, values[index])),
                                    index ? inner : outer),
                1);
      auto type = keep(vpi_handle(vpiTypespec, values[index]));
      ASSERT_NE(type, nullptr);
      EXPECT_EQ(vpi_get(vpiType, type),
                index == 1 ? vpiIntTypespec : vpiEnumTypespec);
      if (index < 2)
        EXPECT_EQ(vpi_compare_objects(type, index ? innerAliases[0]
                                                  : outerAliases[0]),
                  1);
      else {
        auto constants = children(type, vpiEnumConst);
        ASSERT_EQ(constants.size(), 1u);
        EXPECT_STREQ(vpi_get_str(vpiName, constants[0]), "B");
      }
    }
  } else {
    auto values = children(function, vpiVariables);
    ASSERT_EQ(values.size(), 3u);
    const char *names[] = {"first", "b.dot", "last"};
    for (size_t index = 0; index != values.size(); ++index) {
      EXPECT_STREQ(vpi_get_str(vpiName, values[index]), names[index]);
      EXPECT_EQ(vpi_get(vpiAutomatic, values[index]), index != 1);
      EXPECT_EQ(vpi_compare_objects(keep(vpi_handle(vpiScope, values[index])),
                                    function),
                1);
    }
    auto blockValues = children(outer, vpiVariables);
    ASSERT_EQ(blockValues.size(), 1u);
    EXPECT_STREQ(vpi_get_str(vpiName, blockValues[0]), "x");
    EXPECT_EQ(
        vpi_compare_objects(keep(vpi_handle(vpiScope, blockValues[0])), outer),
        1);
    s_vpi_value value{};
    value.format = vpiIntVal;
    vpi_get_value(values[1], &value);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    EXPECT_EQ(value.value.integer, 0);

    obelisk_rt_design_cursor_v1 call{}, scope{}, statement{}, source{};
    uint64_t owner = 0;
    bool isStatement = false;
    ASSERT_EQ(obelisk_rt_cached_vpi_call(raw, 20, &call, &owner, &isStatement),
              OBELISK_RT_OK);
    EXPECT_EQ(owner, 46u);
    ASSERT_EQ(obelisk_rt_cached_vpi_statement(raw, 11, &statement, &owner),
              OBELISK_RT_OK);
    VPIRelationRange range{};
    ASSERT_EQ(obelisk_rt_cached_vpi_relation_range(raw, call, vpiScope, false,
                                                   &range),
              OBELISK_RT_OK);
    ASSERT_EQ(range.count, 1u);
    uint32_t kind = 0;
    ASSERT_EQ(obelisk_rt_cached_vpi_relation_target(raw, range.first, &scope,
                                                    &kind, &isStatement),
              OBELISK_RT_OK);
    EXPECT_TRUE(isStatement);
    EXPECT_EQ(scope.offset, statement.offset);
    ASSERT_EQ(obelisk_rt_cached_vpi_initializer(raw, 46, &source),
              OBELISK_RT_OK);
    obelisk_rt_design_cursor_v1 expected{};
    const std::string name = "top.work.block.x";
    ASSERT_EQ(obelisk_rt_v1_design_lookup(
                  &execution, reinterpret_cast<const uint8_t *>(name.data()),
                  name.size(), &expected),
              OBELISK_RT_OK);
    EXPECT_EQ(source.offset, expected.offset);

    using namespace obelisk::reflection;
    const auto &image = encoded->designDatabase;
    HeaderView header(image.data());
    auto rejects = [&](auto mutate) {
      std::vector<uint8_t> bytes(image.begin(), image.end());
      mutate(bytes);
      uint64_t hash = UINT64_C(14695981039346656037);
      for (size_t i = 0; i != bytes.size(); ++i) {
        hash ^= i >= field::HeaderChecksum && i < field::HeaderChecksum + 8
                    ? 0
                    : bytes[i];
        hash *= UINT64_C(1099511628211);
      }
      HeaderWriter(bytes.data()).setChecksum(hash);
      auto invalid = execution;
      invalid.design_database = bytes.data();
      EXPECT_EQ(obelisk_rt_v1_design_validate(&invalid),
                OBELISK_RT_INVALID_DESIGN);
    };
    rejects([&](auto &bytes) {
      ObjectWriter(bytes.data() + source.offset)
          .setCaps(ObjectView(bytes.data() + source.offset).getCaps() &
                   ~OBELISK_RT_DESIGN_CAP_LEXICAL_ANCHOR);
    });
    uint32_t packed = 0;
    ASSERT_TRUE(tryPackTableIndex(TableKind::Object,
                                  (source.offset - header.getObjectOffset()) /
                                      ObjectLayout.size,
                                  packed));
    uint64_t reverse = 0;
    for (uint64_t i = 0; i != header.getRelationCount(); ++i) {
      uint64_t offset = header.getRelationOffset() + i * RelationLayout.size;
      RelationView row(image.data() + offset);
      uint32_t endpoint = 0;
      ASSERT_TRUE(tryPackTableIndex(
          unpackRelationSourceTable(row.getSourceKindAndTable()),
          row.getSourceIndex(), endpoint));
      if (endpoint == packed && row.getSelector() == vpiScope &&
          !relationSourceIsIterate(row.getSourceKindAndTable()))
        reverse = offset;
    }
    ASSERT_NE(reverse, 0u);
    rejects([&](auto &bytes) {
      uint32_t module = 0;
      EXPECT_TRUE(tryPackTableIndex(TableKind::Scope, 1, module));
      RelationWriter(bytes.data() + reverse).setTargetIndexAndTable(module);
    });
  }
  for (auto handle : handles)
    EXPECT_EQ(vpi_release_handle(handle), 1);
}
TEST(GeneratedDesignDatabase, StaticLocalScopes) { checkStaticLocals(false); }
TEST(GeneratedDesignDatabase, StaticProceduralTypes) {
  checkStaticLocals(true);
}
} // namespace
