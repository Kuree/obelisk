//===- GeneratedVPIGenericClassFieldsTest.cpp - Generic lexical fields
//-----===//

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
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace {
using namespace talos::reflection;

TEST(GeneratedDesignDatabase, GenericClassFields) {
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
  vpiHandle definition = lookup("G");
  ASSERT_NE(definition, nullptr);
  EXPECT_EQ(vpi_get(vpiType, definition), vpiClassDefn);
  EXPECT_EQ(vpi_iterate(vpiClassTypespec, definition), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);

  std::map<std::string, vpiHandle> variables;
  vpiHandle iterator = vpi_iterate(vpiVariables, definition);
  ASSERT_NE(iterator, nullptr);
  auto use = keep(vpi_handle(vpiUse, iterator));
  EXPECT_EQ(vpi_compare_objects(use, definition), 1);
  while (auto variable = vpi_scan(iterator)) {
    keep(variable);
    variables.emplace(vpi_get_str(vpiName, variable), variable);
    auto owner = keep(vpi_handle(vpiScope, variable));
    EXPECT_EQ(vpi_compare_objects(owner, definition), 1);
    EXPECT_EQ(vpi_get(vpiHasActual, variable), 0);
    s_vpi_value value{};
    value.format = vpiIntVal;
    value.value.integer = 777;
    vpi_get_value(variable, &value);
    notice();
    EXPECT_EQ(value.value.integer, 777);
    EXPECT_EQ(vpi_put_value(variable, &value, nullptr, vpiNoDelay), nullptr);
    notice();
  }
  ASSERT_EQ(variables.size(), 3u);
  ASSERT_TRUE(variables.count("x"));
  ASSERT_TRUE(variables.count("E.p"));
  ASSERT_TRUE(variables.count("flag"));
  auto x = variables.at("x"), escaped = variables.at("E.p"),
       flag = variables.at("flag");
  EXPECT_EQ(vpi_get(vpiType, x), vpiIntVar);
  EXPECT_EQ(vpi_get(vpiSize, x), 32);
  EXPECT_EQ(vpi_get(vpiSigned, x), 1);
  EXPECT_EQ(vpi_get(vpiSigned, flag), 0);
  EXPECT_EQ(vpi_get(vpiAutomatic, x), 1);
  EXPECT_EQ(vpi_get(vpiConstantVariable, x), 0);
  EXPECT_EQ(vpi_get(vpiRandType, x), vpiRand);
  EXPECT_EQ(vpi_get(vpiVisibility, x), vpiPublicVis);
  EXPECT_EQ(vpi_get(vpiAutomatic, escaped), 0);
  EXPECT_EQ(vpi_get(vpiConstantVariable, escaped), 1);
  EXPECT_EQ(vpi_get(vpiRandType, escaped), vpiNotRand);
  EXPECT_EQ(vpi_get(vpiVisibility, escaped), vpiLocalVis);
  EXPECT_STREQ(vpi_get_str(vpiFullName, escaped), "G::E.p");
  auto byName = lookup("G::E.p");
  EXPECT_EQ(vpi_compare_objects(byName, escaped), 1);
  EXPECT_EQ(vpi_get(vpiHasActual, byName), 0);
  EXPECT_EQ(vpi_get(vpiType, flag), vpiBitVar);
  EXPECT_EQ(vpi_get(vpiSize, flag), 1);
  EXPECT_EQ(vpi_get(vpiVisibility, flag), vpiProtectedVis);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);

  iterator = vpi_iterate(vpiNamedEvent, definition);
  ASSERT_NE(iterator, nullptr);
  auto event = keep(vpi_scan(iterator));
  ASSERT_NE(event, nullptr);
  EXPECT_EQ(vpi_get(vpiType, event), vpiNamedEvent);
  EXPECT_STREQ(vpi_get_str(vpiName, event), "wake");
  EXPECT_EQ(vpi_get(vpiAutomatic, event), 1);
  EXPECT_EQ(vpi_compare_objects(keep(vpi_handle(vpiScope, event)), definition),
            1);
  EXPECT_EQ(vpi_scan(iterator), nullptr);

  std::map<std::string, vpiHandle> aliases;
  iterator = vpi_iterate(vpiTypedef, definition);
  ASSERT_NE(iterator, nullptr);
  while (auto alias = vpi_scan(iterator)) {
    keep(alias);
    aliases.emplace(vpi_get_str(vpiName, alias), alias);
  }
  ASSERT_EQ(aliases.size(), 2u);
  ASSERT_TRUE(aliases.count("Word"));
  ASSERT_TRUE(aliases.count("Count"));
  auto aliasTarget = keep(vpi_handle(vpiTypedefAlias, aliases.at("Count")));
  EXPECT_EQ(vpi_compare_objects(aliasTarget, aliases.at("Word")), 1);
  auto fieldType = keep(vpi_handle(vpiTypespec, escaped));
  EXPECT_EQ(vpi_compare_objects(fieldType, aliases.at("Count")), 1);
  iterator = vpi_iterate(vpiParameter, definition);
  ASSERT_NE(iterator, nullptr);
  auto parameter = keep(vpi_scan(iterator));
  ASSERT_NE(parameter, nullptr);
  EXPECT_STREQ(vpi_get_str(vpiName, parameter), "T");
  EXPECT_EQ(vpi_scan(iterator), nullptr);
  EXPECT_EQ(lookup("G::dependent"), nullptr);

  auto staticMethod = lookup("G::static_method");
  auto virtualMethod = lookup("G::virtual_method");
  auto unknownMethod = lookup("G::unknown_method");
  ASSERT_NE(staticMethod, nullptr);
  ASSERT_NE(virtualMethod, nullptr);
  ASSERT_NE(unknownMethod, nullptr);
  EXPECT_EQ(vpi_get(vpiVirtual, staticMethod), 0);
  EXPECT_EQ(vpi_get(vpiVirtual, virtualMethod), 1);
  EXPECT_EQ(vpi_get(vpiAutomatic, staticMethod), 1);
  EXPECT_EQ(vpi_get(vpiVirtual, unknownMethod), vpiUndefined);
  notice();
  // Ordinary source method prototypes do not imply DPI or interface access.
  EXPECT_EQ(vpi_get(vpiAccessType, unknownMethod), vpiUndefined);
  notice();

  talos_rt_design_cursor_v1 xCursor{};
  constexpr char xName[] = "G::x";
  ASSERT_EQ(talos_rt_cached_design_lookup(
                runtime, reinterpret_cast<const uint8_t *>(xName),
                sizeof(xName) - 1, &xCursor),
            TALOS_RT_OK);
  for (auto handle : handles)
    EXPECT_EQ(vpi_release_handle(handle), 1);
  talos_rt_v1_context_destroy(runtime);

  const auto &image = encoded->designDatabase;
  HeaderView header(image.data());
  SemanticDirectoryView directory(image.data() + header.getReserved());
  ASSERT_GE(xCursor.offset, header.getObjectOffset());
  ASSERT_LT(xCursor.offset, header.getObjectOffset() +
                                header.getObjectCount() * ObjectLayout.size);
  ObjectView xRecord(image.data() + xCursor.offset);
  ASSERT_EQ(xRecord.getCaps(), TALOS_RT_DESIGN_CAP_SOURCE_CLASS_PROPERTY);
  uint32_t packedX = 0;
  ASSERT_TRUE(tryPackTableIndex(TableKind::Object,
                                (xCursor.offset - header.getObjectOffset()) /
                                    ObjectLayout.size,
                                packedX));
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
    ObjectWriter(bytes.data() + xCursor.offset).setCaps(0);
  });
  rejects([&](auto &bytes) {
    ObjectWriter(bytes.data() + xCursor.offset)
        .setCaps(TALOS_RT_DESIGN_CAP_SOURCE_CLASS_PROPERTY |
                 TALOS_RT_DESIGN_CAP_PORT_OUTPUT);
  });
  uint32_t wrongKind = 0;
  ASSERT_TRUE(
      tryPackRecordKindPayload(RecordKind::StaticObject, vpiBitVar, wrongKind));
  rejects([&](auto &bytes) {
    ObjectWriter(bytes.data() + xCursor.offset).setKindAndVPIKind(wrongKind);
  });
  uint64_t constRow = 0;
  for (uint64_t index = 0; index != directory.getFixedPropertyCount();
       ++index) {
    auto row =
        directory.getFixedPropertyOffset() + index * FixedPropertyLayout.size;
    FixedPropertyView property(image.data() + row);
    if (property.getSourceIndexAndTable() == packedX &&
        property.getSelector() == vpiConstantVariable)
      constRow = row;
  }
  ASSERT_NE(constRow, 0u);
  ASSERT_EQ(FixedPropertyView(image.data() + constRow).getPayload(), 0u);
  rejects([&](auto &bytes) {
    FixedPropertyWriter(bytes.data() + constRow).setPayload(2);
  });
}
} // namespace
