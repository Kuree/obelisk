//===- Net declaration assignments ----------------------------------------===//
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
#include <string>
#include <tuple>
#include <vector>
namespace {
TEST(GeneratedDesignDatabase, NetDeclarationAssignments) {
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
  auto named = [&](const char *name) {
    return keep(vpi_handle_by_name(const_cast<char *>(name), nullptr));
  };
  auto top = named("top");
  auto chosen = named("top.chosen");
  auto source = named("top.source");
  auto inputNet = named("top.input");
  auto output = named("top.out");
  auto slice = named("top.slice");
  auto delayed = named("top.delayed");
  auto inner = named("top.chosen.inner");
  for (auto handle :
       {top, chosen, source, inputNet, output, slice, delayed, inner})
    ASSERT_NE(handle, nullptr);
  EXPECT_EQ(vpi_get(vpiNetDeclAssign, inputNet), 0);
  for (auto handle : {output, slice, delayed, inner})
    EXPECT_EQ(vpi_get(vpiNetDeclAssign, handle), 1);
  auto assignments = children(top, vpiContAssign);
  auto nested = children(chosen, vpiContAssign);
  ASSERT_EQ(assignments.size(), 4u);
  ASSERT_EQ(nested.size(), 1u);
  std::array<vpiHandle, 4> declarations{};
  vpiHandle explicitAssignment = nullptr, selectedRhs = nullptr;
  auto relation = [&](int selector, vpiHandle handle) {
    return keep(vpi_handle(selector, handle));
  };
  auto same = [&](vpiHandle left, vpiHandle right) {
    return vpi_compare_objects(left, right) == 1;
  };
  assignments.push_back(nested.front());
  for (auto assignment : assignments) {
    EXPECT_EQ(vpi_get(vpiType, assignment), vpiContAssign);
    auto lhs = relation(vpiLhs, assignment);
    auto rhs = relation(vpiRhs, assignment);
    ASSERT_NE(lhs, nullptr);
    ASSERT_NE(rhs, nullptr);
    EXPECT_EQ(vpi_get(vpiType, lhs), vpiNet);
    bool netDeclaration = vpi_get(vpiNetDeclAssign, assignment);
    if (same(lhs, output)) {
      EXPECT_TRUE(same(rhs, inputNet));
      if (netDeclaration) {
        ASSERT_EQ(declarations[0], nullptr);
        declarations[0] = assignment;
        EXPECT_EQ(vpi_get(vpiStrength0, assignment), vpiWeakDrive);
        EXPECT_EQ(vpi_get(vpiStrength1, assignment), vpiPullDrive);
      } else {
        ASSERT_EQ(explicitAssignment, nullptr);
        explicitAssignment = assignment;
        EXPECT_EQ(vpi_get(vpiStrength0, assignment), vpiStrongDrive);
        EXPECT_EQ(vpi_get(vpiStrength1, assignment), vpiStrongDrive);
      }
    } else {
      EXPECT_TRUE(netDeclaration);
      EXPECT_EQ(vpi_get(vpiStrength0, assignment), vpiStrongDrive);
      EXPECT_EQ(vpi_get(vpiStrength1, assignment), vpiStrongDrive);
      if (same(lhs, slice)) {
        declarations[1] = assignment;
        selectedRhs = rhs;
        EXPECT_EQ(vpi_get(vpiType, rhs), vpiPartSelect);
        EXPECT_EQ(vpi_get(vpiSize, rhs), 4);
        EXPECT_TRUE(same(relation(vpiParent, rhs), source));
      } else if (same(lhs, delayed)) {
        declarations[2] = assignment;
        EXPECT_EQ(vpi_get(vpiType, rhs), vpiConstant);
        s_vpi_value data{};
        data.format = vpiIntVal;
        vpi_get_value(rhs, &data);
        EXPECT_EQ(data.value.integer, 1);
      } else {
        EXPECT_TRUE(same(lhs, inner));
        declarations[3] = assignment;
        EXPECT_TRUE(same(rhs, inputNet));
      }
    }
    EXPECT_TRUE(same(relation(vpiModule, assignment), top));
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
  }
  for (auto declaration : declarations)
    ASSERT_NE(declaration, nullptr);
  ASSERT_NE(explicitAssignment, nullptr);
  EXPECT_FALSE(same(explicitAssignment, declarations[0]));
  auto expectMembers = [&](vpiHandle object, int selector,
                           std::vector<vpiHandle> expected) {
    auto actual = children(object, selector);
    ASSERT_EQ(actual.size(), expected.size());
    for (auto member : expected)
      EXPECT_EQ(std::count_if(actual.begin(), actual.end(),
                              [&](auto item) { return same(item, member); }),
                1);
  };
  expectMembers(output, vpiDriver, {declarations[0], explicitAssignment});
  expectMembers(output, vpiLocalDriver, {declarations[0], explicitAssignment});
  expectMembers(output, vpiContAssign, {declarations[0], explicitAssignment});
  expectMembers(output, vpiUse, {declarations[0], explicitAssignment});
  expectMembers(inputNet, vpiLoad,
                {declarations[0], explicitAssignment, declarations[3]});
  expectMembers(inputNet, vpiLocalLoad,
                {declarations[0], explicitAssignment, declarations[3]});
  expectMembers(inputNet, vpiUse,
                {declarations[0], explicitAssignment, declarations[3]});
  expectMembers(source, vpiUse, {declarations[1]});
  expectMembers(inner, vpiDriver, {declarations[3]});
  ASSERT_NE(selectedRhs, nullptr);
  for (auto value : {0xa5, 0x5a}) {
    s_vpi_value data{};
    data.format = vpiIntVal;
    data.value.integer = value;
    vpi_put_value(source, &data, nullptr, vpiNoDelay);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    vpi_get_value(selectedRhs, &data);
    EXPECT_EQ(data.value.integer, value >> 4);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
  }
  for (auto handle : handles)
    EXPECT_EQ(vpi_free_object(handle), 1);
  EXPECT_EQ(obelisk_rt_v1_vpi_end_simulation(raw), OBELISK_RT_OK);

  // The declaration form adds one sparse Boolean fact, not another object
  // representation or a default-false row on every continuous assignment.
  auto baseline = mlir::OwningOpRef<mlir::ModuleOp>(module->clone());
  auto baselineDesign = *baseline->getOps<obelisk::sim::SimDesignOp>().begin();
  unsigned removed = 0;
  for (auto statement :
       baselineDesign.getOps<obelisk::sim::SimStatementDeclOp>()) {
    auto properties =
        statement->getAttrOfType<obelisk::sim::VPIPropertySetAttr>(
            "vpi_properties");
    if (!properties)
      continue;
    llvm::SmallVector<mlir::Attribute> remaining;
    for (auto attribute : properties.getProperties()) {
      auto property = mlir::cast<obelisk::sim::VPIPropertyAttr>(attribute);
      if (property.getSelector().getInt() == vpiNetDeclAssign) {
        EXPECT_TRUE(mlir::cast<mlir::BoolAttr>(property.getValue()).getValue());
        ++removed;
      } else {
        remaining.push_back(attribute);
      }
    }
    statement->setAttr(
        "vpi_properties",
        obelisk::sim::VPIPropertySetAttr::get(
            &context, mlir::ArrayAttr::get(&context, remaining)));
  }
  ASSERT_EQ(removed, 4u);
  auto baselineImage = obelisk::encodeSimulationDesign(baselineDesign, options);
  ASSERT_TRUE(mlir::succeeded(baselineImage));
  auto baselineExecution = execution;
  baselineExecution.design_database = baselineImage->designDatabase.data();
  baselineExecution.design_database_size = baselineImage->designDatabase.size();
  ASSERT_EQ(obelisk_rt_v1_design_validate(&baselineExecution), OBELISK_RT_OK);
  EXPECT_EQ(encoded->designDatabase.size() -
                baselineImage->designDatabase.size(),
            4u * obelisk::reflection::FixedPropertyLayout.size);
  options.vpi = "off";
  auto withoutVPI = obelisk::encodeSimulationDesign(design, options);
  ASSERT_TRUE(mlir::succeeded(withoutVPI));
  EXPECT_TRUE(withoutVPI->designDatabase.empty());
  EXPECT_EQ(withoutVPI->executionFlags & (OBELISK_RT_EXECUTION_VPI_READ |
                                          OBELISK_RT_EXECUTION_VPI_WRITE),
            0u);
}
} // namespace
