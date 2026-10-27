//===- GeneratedVPIPortExpressionsTest.cpp - Port expressions --------------===//
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
TEST(GeneratedDesignDatabase, PortExpressions) {
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
  auto top = named("top");
  auto child = named("top.child");
  auto source = named("top.source");
  auto destination = named("top.destination");
  ASSERT_NE(top, nullptr);
  ASSERT_NE(child, nullptr);
  ASSERT_NE(source, nullptr);
  ASSERT_NE(destination, nullptr);
  auto ports = children(child, vpiPort);
  ASSERT_EQ(ports.size(), 5u);
  std::array<vpiHandle, 5> ordered{}, connections{};
  const char *names[] = {"first", "again", "concat", "output", "bit"};
  for (auto port : ports) {
    int index = vpi_get(vpiPortIndex, port);
    ASSERT_GE(index, 0);
    ASSERT_LT(index, 5);
    ASSERT_EQ(ordered[index], nullptr);
    ordered[index] = port;
    EXPECT_STREQ(vpi_get_str(vpiName, port), names[index]);
    EXPECT_EQ(vpi_get(vpiDirection, port), index == 3 ? vpiOutput : vpiInput);
    auto high = keep(vpi_handle(vpiHighConn, port));
    ASSERT_NE(high, nullptr);
    connections[index] = high;
    EXPECT_EQ(vpi_compare_objects(keep(vpi_handle(vpiHighConn, port)), high),
              1);
    EXPECT_EQ(vpi_compare_objects(high, index == 3 ? destination : source), 0);
    EXPECT_EQ(vpi_get(vpiType, high), index == 2   ? vpiOperation
                                      : index == 4 ? vpiRegBit
                                                   : vpiPartSelect);
    EXPECT_EQ(vpi_get(vpiSize, high), index == 4 ? 1 : 4);
    if (index == 4) {
      // This bit also feeds the vector ports above. Until those port-bit
      // identities are supported, the query must not return only this port.
      EXPECT_EQ(vpi_iterate(vpiPortInst, high), nullptr);
      EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
      auto indexed = keep(vpi_handle_by_index(source, 7));
      ASSERT_NE(indexed, nullptr);
      EXPECT_EQ(vpi_iterate(vpiPortInst, indexed), nullptr);
      EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
    }
    EXPECT_EQ(vpi_compare_objects(keep(vpi_handle(vpiModule, high)), top), 1);
    EXPECT_EQ(vpi_compare_objects(keep(vpi_handle(vpiInstance, high)), top), 1);
    auto low = keep(vpi_handle(vpiLowConn, port));
    std::string path = std::string("top.child.inside_") + names[index];
    EXPECT_EQ(vpi_compare_objects(low, named(path.c_str())), 1);
    if (index == 2) {
      EXPECT_EQ(vpi_get(vpiOpType, high), vpiConcatOp);
      auto operands = children(high, vpiOperand);
      ASSERT_EQ(operands.size(), 2u);
      for (auto operand : operands) {
        EXPECT_EQ(vpi_get(vpiType, operand), vpiPartSelect);
        EXPECT_EQ(vpi_get(vpiSize, operand), 2);
        EXPECT_EQ(
            vpi_compare_objects(keep(vpi_handle(vpiParent, operand)), source),
            1);
      }
    } else {
      EXPECT_EQ(vpi_compare_objects(keep(vpi_handle(vpiParent, high)),
                                    index == 3 ? destination : source),
                1);
    }
  }
  for (unsigned i = 0; i != connections.size(); ++i)
    for (unsigned j = 0; j != i; ++j)
      EXPECT_EQ(vpi_compare_objects(connections[i], connections[j]),
                i == 1 && j == 0 ? 1 : 0);
  // The two range expressions are distinct source occurrences even though
  // their current selections denote the same underlying four bits.
  EXPECT_EQ(vpi_compare_objects(keep(vpi_handle(vpiLeftRange, connections[0])),
                                keep(vpi_handle(vpiLeftRange, connections[1]))),
            0);
  auto write = [&](vpiHandle object, int value) {
    s_vpi_value data{};
    data.format = vpiIntVal;
    data.value.integer = value;
    vpi_put_value(object, &data, nullptr, vpiNoDelay);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
  };
  auto read = [&](vpiHandle object) {
    s_vpi_value data{};
    data.format = vpiIntVal;
    vpi_get_value(object, &data);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    return data.value.integer;
  };
  write(source, 0xa5);
  write(destination, 0x3c);
  const int first[] = {0xa, 0xa, 9, 0xc, 1};
  for (unsigned i = 0; i != connections.size(); ++i)
    EXPECT_EQ(read(connections[i]), first[i]);
  write(source, 0x5a);
  EXPECT_EQ(read(connections[0]), 5);
  EXPECT_EQ(read(connections[1]), 5);
  EXPECT_EQ(read(connections[2]), 6);
  EXPECT_EQ(read(connections[3]), 12);
  EXPECT_EQ(read(connections[4]), 0);
  auto uses = children(source, vpiUse);
  ASSERT_EQ(uses.size(), 4u);
  for (unsigned i : {0u, 1u, 2u, 4u})
    EXPECT_EQ(std::count_if(uses.begin(), uses.end(),
                            [&](auto use) {
                              return vpi_compare_objects(use, ordered[i]) == 1;
                            }),
              1);
  auto outputUses = children(destination, vpiUse);
  ASSERT_EQ(outputUses.size(), 1u);
  EXPECT_EQ(vpi_compare_objects(outputUses[0], ordered[3]), 1);
  for (auto handle : handles)
    EXPECT_EQ(vpi_release_handle(handle), 1);
}
} // namespace
