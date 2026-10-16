//===- GeneratedVPIPrimitiveDefinitionsTest.cpp - Shared primitive facts
//----===//
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
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace {
struct Primitive {
  const char *name;
  int kind;
  int subtype;
  const char *symbolic;
  const char *definition;
};

void checkPrimitiveDefinitions(llvm::ArrayRef<Primitive> expected,
                               uint64_t definitionCount, bool compareSize) {
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
  options.vpi = "read";
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
  for (const auto &spec : expected) {
    SCOPED_TRACE(spec.name);
    auto handle = vpi_handle_by_name(const_cast<char *>(spec.name), nullptr);
    ASSERT_NE(handle, nullptr);
    for (auto previous : handles)
      EXPECT_EQ(vpi_compare_objects(handle, previous), 0);
    handles.push_back(handle);
    EXPECT_EQ(vpi_get(vpiType, handle), spec.kind);
    EXPECT_EQ(vpi_get(vpiPrimType, handle), spec.subtype);
    EXPECT_STREQ(vpi_get_str(vpiPrimType, handle), spec.symbolic);
    EXPECT_STREQ(vpi_get_str(vpiDefName, handle), spec.definition);
    EXPECT_STREQ(vpi_get_str(vpiFullName, handle), spec.name);
  }
  using namespace obelisk::reflection;
  const auto &image = encoded->designDatabase;
  HeaderView header(image.data());
  SemanticDirectoryView directory(image.data() + header.getReserved());
  uint64_t primitiveDefinitions = 0, primitiveBindings = 0;
  uint64_t firstDefinition = 0, firstBinding = 0;
  for (uint64_t i = 0; i != directory.getDefinitionCount(); ++i) {
    uint64_t offset =
        directory.getDefinitionOffset() + i * DefinitionLayout.size;
    DefinitionView row(image.data() + offset);
    if (row.getVPIKind() != vpiGate && row.getVPIKind() != vpiSwitch)
      continue;
    ++primitiveDefinitions;
    if (!firstDefinition)
      firstDefinition = offset;
    EXPECT_NE(row.getFlags(), 0);
    EXPECT_EQ(row.getMemberCount(), 0u);
    EXPECT_EQ(row.getMemberRelationCount(), 0u);
    EXPECT_EQ(row.getFile(), 0u);
    EXPECT_EQ(row.getLine(), 0u);
  }
  for (uint64_t i = 0; i != directory.getDefinitionBindingCount(); ++i) {
    uint64_t offset = directory.getDefinitionBindingOffset() +
                      i * DefinitionBindingLayout.size;
    DefinitionBindingView row(image.data() + offset);
    if (unpackTableIndexKind(row.getSourceIndexAndTable()) != TableKind::Object)
      continue;
    ++primitiveBindings;
    if (!firstBinding)
      firstBinding = offset;
    EXPECT_EQ(row.getSpecialization(), UINT32_MAX);
    for (uint64_t j = 0; j != directory.getFixedPropertyCount(); ++j) {
      FixedPropertyView property(image.data() +
                                 directory.getFixedPropertyOffset() +
                                 j * FixedPropertyLayout.size);
      if (property.getSourceIndexAndTable() == row.getSourceIndexAndTable()) {
        EXPECT_NE(property.getSelector(), vpiPrimType);
        EXPECT_NE(property.getSelector(), vpiDefName);
      }
    }
  }
  EXPECT_EQ(primitiveDefinitions, definitionCount);
  EXPECT_EQ(primitiveBindings, expected.size());
  ASSERT_NE(firstDefinition, 0u);
  ASSERT_EQ(DefinitionView(image.data() + firstDefinition).getFlags(),
            vpiAndPrim);
  ASSERT_NE(firstBinding, 0u);
  auto rejects = [&](auto mutate) {
    auto bytes = image;
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
    DefinitionWriter(bytes.data() + firstDefinition).setFlags(0);
  });
  rejects([&](auto &bytes) {
    DefinitionWriter(bytes.data() + firstDefinition).setFlags(vpiCombPrim);
  });
  rejects([&](auto &bytes) {
    DefinitionWriter(bytes.data() + firstDefinition).setFlags(vpiNandPrim);
  });
  rejects([&](auto &bytes) {
    DefinitionWriter(bytes.data() + firstDefinition).setVPIKind(vpiSwitch);
  });
  rejects([&](auto &bytes) {
    DefinitionWriter(bytes.data() + firstDefinition).setMemberCount(1);
  });
  rejects([&](auto &bytes) {
    DefinitionWriter(bytes.data() + firstDefinition).setMemberRelationCount(1);
  });
  rejects([&](auto &bytes) {
    DefinitionWriter(bytes.data() + firstDefinition).setLine(1);
  });
  rejects([&](auto &bytes) {
    uint32_t packed = 0;
    ASSERT_TRUE(tryPackTableIndex(TableKind::Scope, 1, packed));
    DefinitionBindingWriter(bytes.data() + firstBinding)
        .setSourceIndexAndTable(packed);
  });
  if (compareSize) {
    // Encode equivalent per-instance fixed properties as an independent size
    // baseline. The shared form must preserve all public query identities.
    auto flatModule = mlir::OwningOpRef<mlir::ModuleOp>(module->clone());
    auto flatDesign = *flatModule->getOps<obelisk::sim::SimDesignOp>().begin();
    mlir::Builder builder(&context);
    for (auto anchor :
         flatDesign.getOps<obelisk::sim::SimVPIObjectAnchorOp>()) {
      const Primitive *spec = nullptr;
      for (const auto &candidate : expected)
        if (anchor.getHierarchicalName() == candidate.name)
          spec = &candidate;
      if (!spec)
        continue;
      llvm::SmallVector<mlir::Attribute> properties;
      properties.push_back(obelisk::sim::VPIPropertyAttr::get(
          &context, builder.getI32IntegerAttr(vpiDefName),
          builder.getStringAttr(spec->definition)));
      properties.push_back(obelisk::sim::VPIPropertyAttr::get(
          &context, builder.getI32IntegerAttr(vpiPrimType),
          builder.getI32IntegerAttr(spec->subtype)));
      anchor->setAttr("vpi_properties",
                      obelisk::sim::VPIPropertySetAttr::get(
                          &context, builder.getArrayAttr(properties)));
    }
    for (auto binding : llvm::make_early_inc_range(
             flatDesign.getOps<obelisk::sim::SimVPIDefinitionBindingOp>()))
      binding.erase();
    for (auto definition : llvm::make_early_inc_range(
             flatDesign.getOps<obelisk::sim::SimVPIDefinitionDeclOp>()))
      definition.erase();
    auto flat = obelisk::encodeSimulationDesign(flatDesign, options);
    ASSERT_TRUE(mlir::succeeded(flat));
    uint64_t saving = expected.size() * (2 * FixedPropertyLayout.size -
                                         DefinitionBindingLayout.size) -
                      definitionCount * DefinitionLayout.size;
    ASSERT_GT(saving, 0u);
    EXPECT_EQ(flat->designDatabase.size() - image.size(), saving);
    ::testing::Test::RecordProperty("shared_definition_saved_bytes", saving);
  }
  for (auto handle : handles)
    EXPECT_EQ(vpi_release_handle(handle), 1);
}

TEST(GeneratedDesignDatabase, SourcePrimitiveDefinitions) {
  const Primitive expected[] = {
      {"top.g", vpiGate, vpiAndPrim, "vpiAndPrim", "and"},
      {"top.g2", vpiGate, vpiAndPrim, "vpiAndPrim", "and"},
      {"top.multi", vpiGate, vpiBufPrim, "vpiBufPrim", "buf"},
      {"top.sw", vpiSwitch, vpiTranif1Prim, "vpiTranif1Prim", "tranif1"},
      {"other.g", vpiGate, vpiBufPrim, "vpiBufPrim", "buf"},
  };
  checkPrimitiveDefinitions(expected, 3, false);
}
TEST(GeneratedDesignDatabase, SharedPrimitiveDefinitions) {
  const Primitive expected[] = {
      {"top.p0", vpiGate, vpiAndPrim, "vpiAndPrim", "and"},
      {"top.p1", vpiGate, vpiAndPrim, "vpiAndPrim", "and"},
      {"top.p2", vpiGate, vpiAndPrim, "vpiAndPrim", "and"},
      {"top.p3", vpiGate, vpiAndPrim, "vpiAndPrim", "and"},
      {"top.p4", vpiSwitch, vpiTranPrim, "vpiTranPrim", "tran"},
      {"top.p5", vpiSwitch, vpiTranPrim, "vpiTranPrim", "tran"},
      {"top.p6", vpiSwitch, vpiTranPrim, "vpiTranPrim", "tran"},
      {"top.p7", vpiSwitch, vpiTranPrim, "vpiTranPrim", "tran"},
  };
  checkPrimitiveDefinitions(expected, 2, true);
}
} // namespace
