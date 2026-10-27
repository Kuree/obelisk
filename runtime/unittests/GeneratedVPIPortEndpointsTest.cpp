//===- GeneratedVPIPortEndpointsTest.cpp - Source port endpoints -------===//
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Parser/Parser.h"
#include "talos/Conversion/SimulationToBytecode.h"
#include "talos/Dialect/Simulation/SimulationOps.h"
#include "talos/Runtime/Runtime.h"
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
void checkPortEndpoints(bool nets) {
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
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.flags = encoded->executionFlags;
  execution.bytecode = encoded->bytecode.data();
  execution.bytecode_size = encoded->bytecode.size();
  execution.design_database = encoded->designDatabase.data();
  execution.design_database_size = encoded->designDatabase.size();
  execution.state_bit_count = encoded->stateBitCount;
  for (unsigned byte = 0; byte != 8; ++byte)
    execution.checksum |= uint64_t{encoded->bytecode[32 + byte]} << (byte * 8);
  ASSERT_EQ(talos_rt_v1_design_validate(&execution), TALOS_RT_OK);
  talos_rt_context *raw = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &raw),
            TALOS_RT_OK);
  std::unique_ptr<talos_rt_context, decltype(&talos_rt_v1_context_destroy)>
      runtime(raw, talos_rt_v1_context_destroy);
  ASSERT_EQ(talos_rt_v1_vpi_startup(raw, nullptr, 0), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_vpi_end_compile(raw), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_vpi_start_simulation(raw), TALOS_RT_OK);
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
  if (nets) {
    auto child = named("wired_parent.child");
    auto high = named("wired_parent.value");
    auto low = named("wired_parent.child.value");
    ASSERT_NE(child, nullptr);
    ASSERT_NE(high, nullptr);
    ASSERT_NE(low, nullptr);
    auto ports = children(child, vpiPort);
    ASSERT_EQ(ports.size(), 1u);
    auto highConnection = keep(vpi_handle(vpiHighConn, ports[0]));
    auto lowConnection = keep(vpi_handle(vpiLowConn, ports[0]));
    ASSERT_NE(highConnection, nullptr);
    ASSERT_NE(lowConnection, nullptr);
    EXPECT_EQ(vpi_compare_objects(highConnection, high), 1);
    EXPECT_EQ(vpi_compare_objects(lowConnection, low), 1);
    EXPECT_EQ(vpi_compare_objects(highConnection, lowConnection), 0);
    EXPECT_EQ(vpi_get(vpiNetType, high), vpiWand);
    EXPECT_EQ(vpi_get(vpiNetType, low), vpiTri0);
    auto uses = children(high, vpiPortInst);
    ASSERT_EQ(uses.size(), 1u);
    EXPECT_EQ(vpi_compare_objects(uses[0], ports[0]), 1);
  } else {
    auto top = named("top");
    auto child = named("top.child");
    auto source = named("top.source");
    auto destination = named("top.destination");
    auto parameter = named("pkg::constant");
    auto package = named("pkg");
    ASSERT_NE(package, nullptr);
    ASSERT_NE(parameter, nullptr);
    EXPECT_EQ(
        vpi_compare_objects(keep(vpi_handle(vpiInstance, parameter)), package),
        1);
    ASSERT_NE(top, nullptr);
    ASSERT_NE(child, nullptr);
    ASSERT_NE(source, nullptr);
    ASSERT_NE(destination, nullptr);
    auto ports = children(child, vpiPort);
    ASSERT_EQ(ports.size(), 5u);
    const char *names[] = {"input", "again", "output", "open", "parameter"};
    const char *lowNames[] = {"inside_input", "inside_again", "inside_output",
                              "inside_open", "inside_parameter"};
    std::array<vpiHandle, 5> ordered{};
    for (auto port : ports) {
      int ordinal = vpi_get(vpiPortIndex, port);
      ASSERT_GE(ordinal, 0);
      ASSERT_LT(ordinal, 5);
      EXPECT_EQ(ordered[ordinal], nullptr);
      ordered[ordinal] = port;
      EXPECT_STREQ(vpi_get_str(vpiName, port), names[ordinal]);
      EXPECT_EQ(vpi_compare_objects(keep(vpi_handle(vpiInstance, port)), child),
                1);
      std::string lowPath = std::string("top.child.") + lowNames[ordinal];
      auto low = named(lowPath.c_str());
      auto lowConnection = keep(vpi_handle(vpiLowConn, port));
      ASSERT_NE(low, nullptr);
      ASSERT_NE(lowConnection, nullptr);
      EXPECT_EQ(vpi_compare_objects(lowConnection, low), 1);
      EXPECT_EQ(vpi_compare_objects(lowConnection, port), 0);
      EXPECT_EQ(vpi_get(vpiSize, port), 8);
      EXPECT_EQ(vpi_get(vpiDirection, port),
                ordinal == 2 ? vpiOutput : vpiInput);
      auto highConnection = keep(vpi_handle(vpiHighConn, port));
      if (ordinal == 3) {
        EXPECT_EQ(highConnection, nullptr);
        EXPECT_EQ(vpi_chk_error(nullptr), 0);
      } else {
        ASSERT_NE(highConnection, nullptr);
        EXPECT_EQ(vpi_compare_objects(highConnection, ordinal == 4 ? parameter
                                                      : ordinal == 2
                                                          ? destination
                                                          : source),
                  1);
        EXPECT_EQ(
            vpi_compare_objects(keep(vpi_handle(vpiScope, highConnection)),
                                ordinal == 4 ? package : top),
            1);
        EXPECT_EQ(vpi_compare_objects(keep(vpi_handle(vpiHighConn, port)),
                                      highConnection),
                  1);
      }
    }
    auto instances = children(source, vpiPortInst);
    ASSERT_EQ(instances.size(), 2u);
    for (auto expected : {ordered[0], ordered[1]})
      EXPECT_EQ(std::count_if(instances.begin(), instances.end(),
                              [&](auto actual) {
                                return vpi_compare_objects(actual, expected) ==
                                       1;
                              }),
                1);
    auto uses = children(source, vpiUse);
    ASSERT_EQ(uses.size(), 3u);
    EXPECT_EQ(std::count_if(uses.begin(), uses.end(),
                            [](auto use) {
                              return vpi_get(vpiType, use) == vpiContAssign;
                            }),
              1);
    for (auto expected : {ordered[0], ordered[1]})
      EXPECT_EQ(std::count_if(uses.begin(), uses.end(),
                              [&](auto actual) {
                                return vpi_compare_objects(actual, expected) ==
                                       1;
                              }),
                1);
    auto parameterUses = children(parameter, vpiUse);
    ASSERT_EQ(parameterUses.size(), 1u);
    EXPECT_EQ(vpi_compare_objects(parameterUses[0], ordered[4]), 1);
    auto destinationPorts = children(destination, vpiPortInst);
    ASSERT_EQ(destinationPorts.size(), 1u);
    EXPECT_EQ(vpi_compare_objects(destinationPorts[0], ordered[2]), 1);
  }
  for (auto handle : handles)
    EXPECT_EQ(vpi_release_handle(handle), 1);
}
TEST(GeneratedDesignDatabase, WholePortEndpoints) { checkPortEndpoints(false); }
TEST(GeneratedDesignDatabase, IndependentNetPortEndpoints) {
  checkPortEndpoints(true);
}
} // namespace
