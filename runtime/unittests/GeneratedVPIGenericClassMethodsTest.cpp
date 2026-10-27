//===- GeneratedVPIGenericClassMethodsTest.cpp - Generic lexical methods
//---===//

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/Parser/Parser.h"
#include "talos/Conversion/SimulationToBytecode.h"
#include "talos/Dialect/Simulation/SimulationOps.h"
#include "talos/Runtime/Runtime.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "gtest/gtest.h"
#include <array>
#include <cstdlib>
#include <string>

namespace {

TEST(GeneratedDesignDatabase, GenericClassMethods) {
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
  char className[] = "G";
  vpiHandle definition = vpi_handle_by_name(className, nullptr);
  ASSERT_NE(definition, nullptr);
  EXPECT_EQ(vpi_get(vpiType, definition), vpiClassDefn);
  EXPECT_EQ(vpi_iterate(vpiClassTypespec, definition), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);

  const std::array<const char *, 5> names{"new", "run", "E.p", "external",
                                          "deferred"};
  const std::array<int, 5> kinds{vpiFunction, vpiTask, vpiFunction, vpiFunction,
                                 vpiFunction};
  const std::array<int, 5> visibility{vpiPublicVis, vpiProtectedVis,
                                      vpiLocalVis, vpiPublicVis, vpiPublicVis};
  vpiHandle methods = vpi_iterate(vpiMethods, definition);
  ASSERT_NE(methods, nullptr);
  vpiHandle use = vpi_handle(vpiUse, methods);
  ASSERT_NE(use, nullptr);
  EXPECT_EQ(vpi_compare_objects(use, definition), 1);
  EXPECT_EQ(vpi_release_handle(use), 1);
  for (size_t index = 0; index != names.size(); ++index) {
    vpiHandle method = vpi_scan(methods);
    ASSERT_NE(method, nullptr);
    EXPECT_EQ(vpi_get(vpiType, method), kinds[index]);
    EXPECT_STREQ(vpi_get_str(vpiName, method), names[index]);
    const std::string fullName = std::string("G::") + names[index];
    EXPECT_STREQ(vpi_get_str(vpiFullName, method), fullName.c_str());
    EXPECT_EQ(vpi_get(vpiMethod, method), 1);
    EXPECT_EQ(vpi_get(vpiVisibility, method), visibility[index]);
    // A static class method also has automatic invocation lifetime.
    EXPECT_EQ(vpi_get(vpiAutomatic, method), 1);
    vpiHandle scope = vpi_handle(vpiScope, method);
    ASSERT_NE(scope, nullptr);
    EXPECT_EQ(vpi_compare_objects(scope, definition), 1);
    vpiHandle byName =
        vpi_handle_by_name(const_cast<char *>(fullName.c_str()), nullptr);
    ASSERT_NE(byName, nullptr);
    EXPECT_EQ(vpi_compare_objects(byName, method), 1);
    for (auto handle : {scope, byName, method})
      EXPECT_EQ(vpi_release_handle(handle), 1);
  }
  EXPECT_EQ(vpi_scan(methods), nullptr);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);

  // Filtering the method inventory preserves source order across unrelated
  // lexical declarations, while parameter identity remains independently owned.
  vpiHandle parameters = vpi_iterate(vpiParameter, definition);
  ASSERT_NE(parameters, nullptr);
  for (const char *name : {"T", "U"}) {
    vpiHandle parameter = vpi_scan(parameters);
    ASSERT_NE(parameter, nullptr);
    EXPECT_EQ(vpi_get(vpiType, parameter), vpiTypeParameter);
    EXPECT_STREQ(vpi_get_str(vpiName, parameter), name);
    EXPECT_EQ(vpi_handle(vpiTypespec, parameter), nullptr);
    EXPECT_EQ(vpi_release_handle(parameter), 1);
  }
  EXPECT_EQ(vpi_scan(parameters), nullptr);
  EXPECT_EQ(vpi_release_handle(definition), 1);
  talos_rt_v1_context_destroy(runtime);
}

} // namespace
