//===- GeneratedVPIStaticStrengthsTest.cpp - Driver strengths -------------===//
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
struct Strengths {
  const char *name;
  int zero;
  int one;
};
constexpr Strengths gates[] = {
    {"top.g", vpiStrongDrive, vpiStrongDrive},
    {"top.override", vpiHiZ, vpiWeakDrive},
    {"top.pu", vpiPullDrive, vpiPullDrive},
    {"top.pd", vpiPullDrive, vpiPullDrive},
    // IEEE 28.10 ignores the opposite polarity. Obelisk canonicalizes that
    // unused polarity to Pull, consistently with executable driver lowering.
    {"top.pu_override", vpiPullDrive, vpiSupplyDrive},
    {"top.pd_override", vpiSupplyDrive, vpiPullDrive},
    {"top.explicit_default", vpiStrongDrive, vpiStrongDrive}};
constexpr Strengths assignments[] = {{"", vpiStrongDrive, vpiStrongDrive},
                                     {"", vpiPullDrive, vpiWeakDrive}};

void checkStaticStrengths(bool missingDefinition) {
  using namespace obelisk;
  const char *input = std::getenv("OBELISK_TEST_INPUT");
  ASSERT_NE(input, nullptr);
  mlir::DialectRegistry registry;
  registry.insert<mlir::arith::ArithDialect, mlir::cf::ControlFlowDialect,
                  sim::ObeliskSimulationDialect>();
  mlir::MLIRContext context(registry);
  auto module = mlir::parseSourceFile<mlir::ModuleOp>(input, &context);
  ASSERT_TRUE(module);
  auto design = *module->getOps<sim::SimDesignOp>().begin();
  if (missingDefinition) {
    mlir::FlatSymbolRefAttr target;
    for (auto anchor : design.getOps<sim::SimVPIObjectAnchorOp>())
      if (anchor.getHierarchicalName() == "top.g")
        target = mlir::FlatSymbolRefAttr::get(anchor);
    ASSERT_TRUE(target);
    for (auto binding : llvm::make_early_inc_range(
             design.getOps<sim::SimVPIDefinitionBindingOp>()))
      if (binding.getSourceAttr() == target)
        binding.erase();
  }
  SimulationBytecodeOptions options;
  options.vpi = "read";
  auto encoded = encodeSimulationDesign(design, options);
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
  for (const auto &expected : gates) {
    SCOPED_TRACE(expected.name);
    auto handle =
        vpi_handle_by_name(const_cast<char *>(expected.name), nullptr);
    ASSERT_NE(handle, nullptr);
    handles.push_back(handle);
    for (auto property : {vpiStrength0, vpiStrength1}) {
      if (missingDefinition && std::string(expected.name) == "top.g") {
        EXPECT_EQ(vpi_get(property, handle), vpiUndefined);
        s_vpi_error_info error{};
        EXPECT_EQ(vpi_chk_error(&error), vpiNotice);
      } else {
        EXPECT_EQ(vpi_get(property, handle),
                  property == vpiStrength0 ? expected.zero : expected.one);
      }
    }
  }
  auto sw = vpi_handle_by_name(const_cast<char *>("top.sw"), nullptr);
  ASSERT_NE(sw, nullptr);
  handles.push_back(sw);
  for (auto property : {vpiStrength0, vpiStrength1}) {
    EXPECT_EQ(vpi_get(property, sw), vpiUndefined);
    s_vpi_error_info error{};
    EXPECT_EQ(vpi_chk_error(&error), vpiNotice);
  }
  auto top = vpi_handle_by_name(const_cast<char *>("top"), nullptr);
  ASSERT_NE(top, nullptr);
  handles.push_back(top);
  auto iterator = vpi_iterate(vpiContAssign, top);
  ASSERT_NE(iterator, nullptr);
  for (const auto &expected : assignments) {
    auto assignment = vpi_scan(iterator);
    ASSERT_NE(assignment, nullptr);
    handles.push_back(assignment);
    EXPECT_EQ(vpi_get(vpiStrength0, assignment), expected.zero);
    EXPECT_EQ(vpi_get(vpiStrength1, assignment), expected.one);
  }
  EXPECT_EQ(vpi_scan(iterator), nullptr);

  using namespace reflection;
  const auto &image = encoded->designDatabase;
  HeaderView header(image.data());
  SemanticDirectoryView directory(image.data() + header.getReserved());
  uint64_t strengthRows = 0, firstStrength = 0;
  for (uint64_t i = 0; i != directory.getFixedPropertyCount(); ++i) {
    uint64_t offset =
        directory.getFixedPropertyOffset() + i * FixedPropertyLayout.size;
    FixedPropertyView property(image.data() + offset);
    if (property.getSelector() != vpiStrength0 &&
        property.getSelector() != vpiStrength1)
      continue;
    ++strengthRows;
    if (!firstStrength)
      firstStrength = offset;
  }
  EXPECT_EQ(strengthRows, 6u);
  ASSERT_NE(firstStrength, 0u);
  auto invalidBytes = image;
  FixedPropertyWriter(invalidBytes.data() + firstStrength).setPayload(0);
  uint64_t hash = UINT64_C(14695981039346656037);
  for (size_t i = 0; i != invalidBytes.size(); ++i) {
    hash ^= i >= field::HeaderChecksum && i < field::HeaderChecksum + 8
                ? 0
                : invalidBytes[i];
    hash *= UINT64_C(1099511628211);
  }
  HeaderWriter(invalidBytes.data()).setChecksum(hash);
  auto invalid = execution;
  invalid.design_database = invalidBytes.data();
  EXPECT_EQ(obelisk_rt_v1_design_validate(&invalid), OBELISK_RT_INVALID_DESIGN);

  if (!missingDefinition) {
    auto expanded = mlir::OwningOpRef<mlir::ModuleOp>(module->clone());
    auto expandedDesign = *expanded->getOps<sim::SimDesignOp>().begin();
    mlir::Builder builder(&context);
    auto addDefaults = [&](mlir::Operation *op, const Strengths &expected) {
      llvm::SmallVector<mlir::Attribute> properties;
      if (auto previous =
              op->getAttrOfType<sim::VPIPropertySetAttr>("vpi_properties"))
        for (auto attribute : previous.getProperties()) {
          auto property = mlir::cast<sim::VPIPropertyAttr>(attribute);
          if (property.getSelector().getInt() != vpiStrength0 &&
              property.getSelector().getInt() != vpiStrength1)
            properties.push_back(attribute);
        }
      for (auto selector : {vpiStrength0, vpiStrength1})
        properties.push_back(sim::VPIPropertyAttr::get(
            &context, builder.getI32IntegerAttr(selector),
            builder.getI32IntegerAttr(
                selector == vpiStrength0 ? expected.zero : expected.one)));
      llvm::sort(properties, [](mlir::Attribute a, mlir::Attribute b) {
        return mlir::cast<sim::VPIPropertyAttr>(a).getSelector().getInt() <
               mlir::cast<sim::VPIPropertyAttr>(b).getSelector().getInt();
      });
      op->setAttr("vpi_properties",
                  sim::VPIPropertySetAttr::get(
                      &context, builder.getArrayAttr(properties)));
    };
    for (auto anchor : expandedDesign.getOps<sim::SimVPIObjectAnchorOp>())
      for (const auto &expected : gates)
        if (anchor.getHierarchicalName() == expected.name)
          addDefaults(anchor, expected);
    unsigned index = 0;
    for (auto statement : expandedDesign.getOps<sim::SimStatementDeclOp>())
      if (statement.getVpiKind() == vpiContAssign) {
        ASSERT_LT(index, std::size(assignments));
        addDefaults(statement, assignments[index++]);
      }
    ASSERT_EQ(index, std::size(assignments));
    auto expandedImage = encodeSimulationDesign(expandedDesign, options);
    ASSERT_TRUE(mlir::succeeded(expandedImage));
    EXPECT_EQ(expandedImage->designDatabase.size() - image.size(),
              12u * FixedPropertyLayout.size);
  }
  for (auto handle : handles)
    EXPECT_EQ(vpi_free_object(handle), 1);
  EXPECT_EQ(obelisk_rt_v1_vpi_end_simulation(raw), OBELISK_RT_OK);
}
} // namespace

TEST(GeneratedDesignDatabase, StaticDriverStrengths) {
  checkStaticStrengths(false);
}
TEST(GeneratedDesignDatabase, MissingGateStrengthDefault) {
  checkStaticStrengths(true);
}
