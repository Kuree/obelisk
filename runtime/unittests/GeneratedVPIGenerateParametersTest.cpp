//===- GeneratedVPIGenerateParametersTest.cpp - Generate parameter queries
//-===//
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
#include <array>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
TEST(GeneratedDesignDatabase, GenerateParameters) {
  const char *input = std::getenv("OBELISK_TEST_INPUT");
  ASSERT_NE(input, nullptr);
  mlir::DialectRegistry registry;
  registry.insert<mlir::arith::ArithDialect, mlir::cf::ControlFlowDialect,
                  obelisk::sim::ObeliskSimulationDialect>();
  mlir::MLIRContext context(registry);
  auto module = mlir::parseSourceFile<mlir::ModuleOp>(input, &context);
  ASSERT_TRUE(module);
  obelisk::SimulationBytecodeOptions options;
  options.vpi = "read";
  auto encoded = obelisk::encodeSimulationDesign(
      *module->getOps<obelisk::sim::SimDesignOp>().begin(), options);
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
      checksum,
      nullptr,
      0,
      0,
      0,
      nullptr,
      0,
      nullptr,
      0};
  ASSERT_EQ(obelisk_rt_v1_design_validate(&execution), OBELISK_RT_OK);
  obelisk_rt_context *runtime = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &runtime),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(runtime, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(runtime), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(runtime), OBELISK_RT_OK);
  std::vector<vpiHandle> handles;
  auto keep = [&](vpiHandle handle) {
    if (handle)
      handles.push_back(handle);
    return handle;
  };
  auto named = [&](const std::string &name) {
    return keep(vpi_handle_by_name(const_cast<char *>(name.c_str()), nullptr));
  };
  auto children = [&](int selector, vpiHandle owner) {
    std::vector<vpiHandle> result;
    if (auto iterator = vpi_iterate(selector, owner)) {
      EXPECT_EQ(vpi_compare_objects(keep(vpi_handle(vpiUse, iterator)), owner),
                1);
      while (auto child = vpi_scan(iterator))
        result.push_back(keep(child));
    }
    return result;
  };
  auto value = [&](vpiHandle parameter) {
    s_vpi_value result{};
    result.format = vpiIntVal;
    vpi_get_value(parameter, &result);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    return result.value.integer;
  };
  auto top = named("top");
  auto array = named("top.g");
  ASSERT_NE(top, nullptr);
  ASSERT_NE(array, nullptr);
  EXPECT_TRUE(children(vpiParameter, top).empty());
  std::array<std::vector<vpiHandle>, 2> inventories;
  std::array<vpiHandle, 2> defaults{};
  const std::array<int, 2> indices{-1, 2};
  const std::array<const char *, 4> names{"idx", "K", "L.p", "Q"};
  for (unsigned iteration = 0; iteration != indices.size(); ++iteration) {
    const int index = indices[iteration];
    const std::string prefix = "top.g[" + std::to_string(index) + "]";
    auto scope = keep(vpi_handle_by_index(array, index));
    ASSERT_NE(scope, nullptr);
    EXPECT_EQ(vpi_get(vpiType, scope), vpiGenScope);
    EXPECT_EQ(vpi_compare_objects(scope, named(prefix)), 1);
    auto &parameters = inventories[iteration];
    parameters = children(vpiParameter, scope);
    ASSERT_EQ(parameters.size(), names.size());
    for (unsigned ordinal = 0; ordinal != names.size(); ++ordinal) {
      auto parameter = parameters[ordinal];
      EXPECT_EQ(vpi_get(vpiType, parameter), vpiParameter);
      EXPECT_EQ(vpi_get(vpiLocalParam, parameter), 1);
      EXPECT_STREQ(vpi_get_str(vpiName, parameter), names[ordinal]);
      EXPECT_STREQ(vpi_get_str(vpiFullName, parameter),
                   (prefix + "." + names[ordinal]).c_str());
      EXPECT_EQ(
          vpi_compare_objects(parameter, named(prefix + "." + names[ordinal])),
          1);
      EXPECT_EQ(
          vpi_compare_objects(keep(vpi_handle(vpiScope, parameter)), scope), 1);
      EXPECT_EQ(
          vpi_compare_objects(keep(vpi_handle(vpiModule, parameter)), top), 1);
      EXPECT_EQ(
          vpi_compare_objects(keep(vpi_handle(vpiInstance, parameter)), top),
          1);
    }
    EXPECT_EQ(value(parameters[0]), index);
    EXPECT_EQ(value(parameters[1]), 42);
    EXPECT_EQ(value(parameters[2]), 254);
    EXPECT_EQ(value(parameters[3]), 42);
    EXPECT_EQ(vpi_get(vpiSize, parameters[0]), 32);
    EXPECT_EQ(vpi_get(vpiSigned, parameters[0]), 1);
    EXPECT_EQ(vpi_get(vpiSize, parameters[2]), 8);
    EXPECT_EQ(vpi_get(vpiSigned, parameters[2]), 0);
    EXPECT_EQ(vpi_handle(vpiLeftRange, parameters[0]), nullptr);
    EXPECT_EQ(value(keep(vpi_handle(vpiLeftRange, parameters[2]))), 7);
    EXPECT_EQ(value(keep(vpi_handle(vpiRightRange, parameters[2]))), 0);
    defaults[iteration] = keep(vpi_handle(vpiExpr, parameters[1]));
    ASSERT_NE(defaults[iteration], nullptr);
    EXPECT_EQ(value(defaults[iteration]), 42);
    EXPECT_EQ(vpi_compare_objects(keep(vpi_handle(vpiExpr, parameters[3])),
                                  parameters[1]),
              1);
    s_vpi_value replacement{};
    replacement.format = vpiIntVal;
    replacement.value.integer = 99;
    EXPECT_EQ(vpi_put_value(parameters[0], &replacement, nullptr, vpiNoDelay),
              nullptr);
    EXPECT_NE(vpi_chk_error(nullptr), 0);
    EXPECT_EQ(value(parameters[0]), index);
  }
  EXPECT_EQ(vpi_compare_objects(inventories[0][1], inventories[1][1]), 0);
  EXPECT_EQ(vpi_compare_objects(defaults[0], defaults[1]), 0);
  auto inner = named("top.g[-1].inner");
  ASSERT_NE(inner, nullptr);
  auto innerParameters = children(vpiParameter, inner);
  ASSERT_EQ(innerParameters.size(), 1u);
  EXPECT_EQ(value(innerParameters[0]), 42);
  EXPECT_EQ(vpi_compare_objects(keep(vpi_handle(vpiScope, innerParameters[0])),
                                inner),
            1);
  EXPECT_EQ(vpi_compare_objects(
                keep(vpi_handle(vpiInstance, innerParameters[0])), top),
            1);
  EXPECT_EQ(vpi_compare_objects(innerParameters[0], inventories[0][1]), 0);
  EXPECT_EQ(named("top.inactive.hidden"), nullptr);
  EXPECT_EQ(named("top.hidden"), nullptr);
  auto bus = named("bus");
  auto chosen = named("bus.chosen");
  ASSERT_NE(bus, nullptr);
  ASSERT_NE(chosen, nullptr);
  auto interfaceParameters = children(vpiParameter, chosen);
  ASSERT_EQ(interfaceParameters.size(), 1u);
  EXPECT_EQ(value(interfaceParameters[0]), 42);
  EXPECT_EQ(vpi_compare_objects(
                keep(vpi_handle(vpiScope, interfaceParameters[0])), chosen),
            1);
  EXPECT_EQ(vpi_compare_objects(
                keep(vpi_handle(vpiInstance, interfaceParameters[0])), bus),
            1);
  EXPECT_EQ(vpi_handle(vpiModule, interfaceParameters[0]), nullptr);
  auto processes = children(vpiProcess, top);
  ASSERT_EQ(processes.size(), 1u);
  auto condition = keep(vpi_handle(vpiStmt, processes[0]));
  ASSERT_NE(condition, nullptr);
  EXPECT_EQ(vpi_get(vpiType, condition), vpiIf);
  EXPECT_EQ(vpi_compare_objects(keep(vpi_handle(vpiCondition, condition)),
                                inventories[0][0]),
            1);
  auto uses = children(vpiUse, inventories[0][0]);
  ASSERT_EQ(uses.size(), 1u);
  EXPECT_EQ(vpi_compare_objects(uses[0], condition), 1);
  for (auto handle : handles)
    vpi_free_object(handle);
  EXPECT_EQ(obelisk_rt_v1_vpi_end_simulation(runtime), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(runtime);

  using namespace obelisk::reflection;
  const auto &image = encoded->designDatabase;
  HeaderView header(image.data());
  SemanticDirectoryView directory(image.data() + header.getReserved());
  uint64_t compactParameters = 0;
  for (uint64_t index = 0; index != directory.getStaticObjectCount(); ++index)
    compactParameters +=
        StaticObjectView(image.data() + directory.getStaticObjectOffset() +
                         index * StaticObjectLayout.size)
            .getVPIKind() == vpiParameter;
  EXPECT_EQ(compactParameters, 10u);
  for (uint64_t index = 0; index != header.getObjectCount(); ++index)
    EXPECT_NE(unpackRecordKindPayload(ObjectView(image.data() +
                                                 header.getObjectOffset() +
                                                 index * ObjectLayout.size)
                                          .getKindAndVPIKind()),
              vpiParameter);
  // Each declaration retains its scope, while identical effective values and
  // original literal payloads share existing immutable pools.
  EXPECT_EQ(directory.getFrozenValueCount(), 4u);
  EXPECT_EQ(directory.getFrozenValueBindingCount(), 12u);
  EXPECT_EQ(directory.getDefinitionParameterValueCount(), 0u);
  RecordProperty("compact_parameter_bytes",
                 compactParameters * StaticObjectLayout.size);
  RecordProperty("design_database_bytes", image.size());
}
} // namespace
