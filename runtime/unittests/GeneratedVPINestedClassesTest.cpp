//===- GeneratedVPINestedClassesTest.cpp - Nested lexical classes -------===//

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
#include <set>
#include <string>
#include <vector>

namespace {
using namespace talos::reflection;

TEST(GeneratedDesignDatabase, NestedLexicalClasses) {
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
  auto related = [&](int selector, vpiHandle source) {
    return keep(vpi_handle(selector, source));
  };
  auto collect = [&](int selector, vpiHandle source) {
    std::vector<vpiHandle> result;
    auto iterator = vpi_iterate(selector, source);
    if (!iterator)
      return result;
    while (auto handle = vpi_scan(iterator))
      result.push_back(keep(handle));
    return result;
  };
  auto names = [](const std::vector<vpiHandle> &values) {
    std::set<std::string> result;
    for (auto value : values) {
      const char *name = vpi_get_str(vpiName, value);
      if (name)
        result.emplace(name);
    }
    return result;
  };
  auto g = lookup("G");
  auto inner = lookup("G::Inner");
  auto deep = lookup("G::Inner::Deep");
  auto unused = lookup("G::Unused");
  ASSERT_NE(g, nullptr);
  ASSERT_NE(inner, nullptr);
  ASSERT_NE(deep, nullptr);
  ASSERT_NE(unused, nullptr);
  EXPECT_EQ(vpi_get(vpiType, inner), vpiClassDefn);
  EXPECT_EQ(vpi_get(vpiType, deep), vpiClassDefn);
  EXPECT_EQ(vpi_get(vpiVirtual, inner), 1);
  EXPECT_EQ(vpi_get(vpiVirtual, unused), 0);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);
  EXPECT_EQ(vpi_compare_objects(related(vpiScope, inner), g), 1);
  EXPECT_EQ(vpi_compare_objects(related(vpiScope, deep), inner), 1);
  EXPECT_EQ(names(collect(vpiInternalScope, g)),
            (std::set<std::string>{"Inner", "Unused"}));
  auto innerScopes = collect(vpiInternalScope, inner);
  ASSERT_EQ(innerScopes.size(), 2u);
  EXPECT_EQ(names(innerScopes), (std::set<std::string>{"Deep", "inspect"}));
  for (auto scope : innerScopes) {
    if (vpi_get(vpiType, scope) == vpiClassDefn) {
      EXPECT_EQ(vpi_compare_objects(scope, deep), 1);
    }
  }
  EXPECT_EQ(names(collect(vpiMethods, inner)),
            (std::set<std::string>{"inspect"}));
  auto variables = collect(vpiVariables, inner);
  ASSERT_EQ(variables.size(), 1u);
  EXPECT_STREQ(vpi_get_str(vpiName, variables[0]), "value");
  EXPECT_EQ(vpi_get(vpiType, variables[0]), vpiIntVar);
  EXPECT_EQ(vpi_compare_objects(related(vpiScope, variables[0]), inner), 1);
  s_vpi_value value{};
  value.format = vpiIntVal;
  vpi_get_value(variables[0], &value);
  EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
  EXPECT_EQ(names(collect(vpiVariables, unused)),
            (std::set<std::string>{"value"}));
  EXPECT_TRUE(collect(vpiClassTypespec, unused).empty());
  EXPECT_TRUE(collect(vpiClassTypespec, deep).empty());

  auto parameters = collect(vpiParameter, deep);
  auto aliases = collect(vpiTypedef, deep);
  ASSERT_EQ(parameters.size(), 1u);
  ASSERT_EQ(aliases.size(), 1u);
  EXPECT_STREQ(vpi_get_str(vpiName, parameters[0]), "U");
  EXPECT_STREQ(vpi_get_str(vpiName, aliases[0]), "UA");
  EXPECT_EQ(
      vpi_compare_objects(related(vpiTypedefAlias, aliases[0]), parameters[0]),
      1);

  auto specializations = collect(vpiClassTypespec, inner);
  ASSERT_EQ(specializations.size(), 2u);
  EXPECT_EQ(vpi_compare_objects(specializations[0], specializations[1]), 0);
  std::set<int> widths;
  for (auto specialization : specializations) {
    EXPECT_EQ(vpi_get(vpiType, specialization), vpiClassTypespec);
    EXPECT_EQ(vpi_compare_objects(related(vpiClassDefn, specialization), inner),
              1);
    auto members = collect(vpiVariables, specialization);
    ASSERT_EQ(members.size(), 1u);
    EXPECT_STREQ(vpi_get_str(vpiName, members[0]), "dependent");
    EXPECT_EQ(vpi_get(vpiType, members[0]), vpiBitVar);
    widths.insert(vpi_get(vpiSize, members[0]));
    EXPECT_EQ(
        vpi_compare_objects(related(vpiScope, members[0]), specialization), 1);
  }
  EXPECT_EQ(widths, (std::set<int>{8, 16}));
  EXPECT_EQ(vpi_chk_error(nullptr), 0);
  for (auto handle : handles)
    EXPECT_EQ(vpi_release_handle(handle), 1);
  talos_rt_v1_context_destroy(runtime);
}
} // namespace
