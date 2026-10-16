//===- GeneratedVPISourceConstraintsTest.cpp - Lexical constraints
//---===//

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/Parser/Parser.h"
#include "obelisk/Conversion/SimulationToBytecode.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/Runtime.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "gtest/gtest.h"
#include <cstdlib>
#include <string>
#include <vector>

namespace {

TEST(GeneratedDesignDatabase, SourceClassConstraints) {
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
      checksum};
  ASSERT_EQ(obelisk_rt_v1_design_validate(&execution), OBELISK_RT_OK);
  obelisk_rt_context *runtime = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &runtime),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(runtime, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(runtime), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(runtime), OBELISK_RT_OK);
  struct Expected {
    const char *name;
    bool automatic;
    int access;
  };
  auto constraints = [&](vpiHandle owner, const char *hierarchy,
                         const std::vector<Expected> &expected) {
    std::vector<vpiHandle> result;
    vpiHandle iterator = vpi_iterate(vpiConstraint, owner);
    EXPECT_NE(iterator, nullptr);
    if (!iterator)
      return result;
    vpiHandle use = vpi_handle(vpiUse, iterator);
    EXPECT_NE(use, nullptr);
    EXPECT_EQ(vpi_compare_objects(use, owner), 1);
    EXPECT_EQ(vpi_release_handle(use), 1);
    for (const auto &entry : expected) {
      vpiHandle constraint = vpi_scan(iterator);
      EXPECT_NE(constraint, nullptr);
      if (!constraint)
        return result;
      result.push_back(constraint);
      EXPECT_EQ(vpi_get(vpiType, constraint), vpiConstraint);
      EXPECT_STREQ(vpi_get_str(vpiName, constraint), entry.name);
      std::string fullName = std::string(hierarchy) + "::" + entry.name;
      EXPECT_STREQ(vpi_get_str(vpiFullName, constraint), fullName.c_str());
      EXPECT_EQ(vpi_get(vpiAutomatic, constraint), entry.automatic);
      EXPECT_EQ(vpi_get(vpiAccessType, constraint), entry.access);
    }
    EXPECT_EQ(vpi_scan(iterator), nullptr);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    return result;
  };
  char genericName[] = "G";
  vpiHandle generic = vpi_handle_by_name(genericName, nullptr);
  ASSERT_NE(generic, nullptr);
  EXPECT_EQ(vpi_get(vpiType, generic), vpiClassDefn);
  EXPECT_EQ(vpi_iterate(vpiClassTypespec, generic), nullptr);
  auto genericConstraints = constraints(generic, "G",
                                        {{"z_first", true, 0},
                                         {"external", true, vpiExternAcc},
                                         {"C.dot", false, 0},
                                         {"required", true, 0},
                                         {"implicit_empty", true, 0}});
  ASSERT_EQ(genericConstraints.size(), 5u);
  // The unrelated parameter is omitted without changing constraint order.
  vpiHandle parameters = vpi_iterate(vpiParameter, generic);
  ASSERT_NE(parameters, nullptr);
  vpiHandle parameter = vpi_scan(parameters);
  ASSERT_NE(parameter, nullptr);
  EXPECT_STREQ(vpi_get_str(vpiName, parameter), "T");
  EXPECT_EQ(vpi_scan(parameters), nullptr);
  EXPECT_EQ(vpi_release_handle(parameter), 1);
  char nestedName[] = "G::Nested";
  vpiHandle nested = vpi_handle_by_name(nestedName, nullptr);
  ASSERT_NE(nested, nullptr);
  EXPECT_EQ(vpi_iterate(vpiClassTypespec, nested), nullptr);
  auto nestedConstraints = constraints(nested, nestedName, {{"own", true, 0}});
  ASSERT_EQ(nestedConstraints.size(), 1u);

  char realizedName[] = "H";
  vpiHandle realized = vpi_handle_by_name(realizedName, nullptr);
  ASSERT_NE(realized, nullptr);
  auto lexicalConstraints = constraints(realized, "H", {{"own", true, 0}});
  ASSERT_EQ(lexicalConstraints.size(), 1u);
  vpiHandle specializations = vpi_iterate(vpiClassTypespec, realized);
  ASSERT_NE(specializations, nullptr);
  vpiHandle specialization = vpi_scan(specializations);
  ASSERT_NE(specialization, nullptr);
  EXPECT_EQ(vpi_scan(specializations), nullptr);
  auto effectiveConstraints =
      constraints(specialization, "H#(int)", {{"own", true, 0}});
  ASSERT_EQ(effectiveConstraints.size(), 1u);
  EXPECT_EQ(vpi_compare_objects(lexicalConstraints.front(),
                                effectiveConstraints.front()),
            0);
  for (auto &list : {genericConstraints, nestedConstraints, lexicalConstraints,
                     effectiveConstraints})
    for (auto handle : list)
      EXPECT_EQ(vpi_release_handle(handle), 1);
  for (auto handle : {generic, nested, realized, specialization})
    EXPECT_EQ(vpi_release_handle(handle), 1);
  obelisk_rt_v1_context_destroy(runtime);
}
} // namespace
