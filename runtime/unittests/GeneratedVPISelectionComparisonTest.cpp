//===- GeneratedVPISelectionComparisonTest.cpp - Selected objects ---------===//
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Parser/Parser.h"
#include "obelisk/Conversion/SimulationToBytecode.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "obelisk/Runtime/Runtime.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "gtest/gtest.h"
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>
namespace {
TEST(GeneratedDesignDatabase, SelectionComparison) {
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
  auto named = [&](const char *name) {
    return keep(vpi_handle_by_name(const_cast<char *>(name), nullptr));
  };
  auto expression = [&](const char *name) {
    std::string path = std::string("top.") + name;
    auto process = named(path.c_str());
    auto statement = keep(vpi_handle(vpiStmt, process));
    auto result = keep(vpi_handle(vpiRhs, statement));
    EXPECT_NE(result, nullptr) << name;
    return result;
  };
  auto compare = [&](vpiHandle a, vpiHandle b, int expected) {
    EXPECT_EQ(vpi_compare_objects(a, b), expected);
    EXPECT_EQ(vpi_compare_objects(b, a), expected);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
  };
  auto write = [&](vpiHandle object, int value, unsigned unknown = 0) {
    s_vpi_vecval vector{static_cast<PLI_UINT32>(value), unknown};
    s_vpi_value data{};
    data.format = vpiVectorVal;
    data.value.vector = &vector;
    vpi_put_value(object, &data, nullptr, vpiNoDelay);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
  };
  auto a = named("top.a"), b = named("top.b");
  auto index = named("top.index");
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);
  ASSERT_NE(index, nullptr);
  write(a, 0xa5);
  write(b, 0xa5);
  write(index, 4);
  auto first = expression("first"), again = expression("again");
  compare(first, again, 1);
  compare(first, expression("first"), 1);
  compare(first, expression("plus"), 1);
  compare(first, expression("minus"), 1);
  compare(first, expression("different"), 0);
  compare(first, expression("other_value"), 0);
  compare(expression("whole"), a, 1);
  compare(first, a, 0);
  auto bit = keep(vpi_handle_by_index(a, 7));
  ASSERT_NE(bit, nullptr);
  compare(expression("bit"), bit, 1);
  compare(expression("bit"), keep(vpi_handle_by_index(a, 6)), 0);
  auto left = keep(vpi_handle(vpiLeftRange, first));
  auto anotherLeft = keep(vpi_handle(vpiLeftRange, again));
  ASSERT_NE(left, nullptr);
  ASSERT_NE(anotherLeft, nullptr);
  compare(left, anotherLeft, 0);
  compare(left, keep(vpi_handle(vpiLeftRange, first)), 1);
  auto concat = expression("concat");
  compare(concat, a, 0);
  compare(concat, expression("whole"), 0);
  compare(concat, expression("concat_again"), 0);
  compare(concat, expression("concat"), 1);
  auto ascending = expression("ascending");
  compare(ascending, expression("ascending_minus"), 1);
  compare(ascending, expression("ascending_plus"), 1);
  compare(ascending, first, 0);
  auto net = named("top.netword"), netFirst = expression("net_first");
  ASSERT_NE(net, nullptr);
  compare(netFirst, expression("net_again"), 1);
  compare(expression("net_whole"), net, 1);
  compare(expression("net_bit"), keep(vpi_handle_by_index(net, 7)), 1);
  compare(netFirst, first, 0);
  auto parameter = expression("parameter");
  compare(parameter, expression("parameter_again"), 1);
  compare(parameter, expression("other_parameter"), 0);
  compare(expression("whole_parameter"), named("top.P"), 1);
  compare(parameter, first, 0);
  auto dynamic = expression("dynamic");
  compare(dynamic, first, 1);
  write(index, 3);
  compare(dynamic, first, 0);
  compare(dynamic, expression("different"), 1);
  write(index, 7);
  compare(dynamic, dynamic, 0);
  compare(dynamic, first, 0);
  write(index, -1);
  compare(dynamic, dynamic, 0);
  write(index, 4, 1);
  compare(dynamic, dynamic, 0);
  write(index, 4);
  compare(dynamic, first, 1);
  auto invalid = expression("invalid"), negative = expression("negative");
  compare(invalid, invalid, 0);
  compare(negative, negative, 0);
  compare(invalid, first, 0);
  for (auto handle : handles)
    EXPECT_EQ(vpi_release_handle(handle), 1);
}
} // namespace
