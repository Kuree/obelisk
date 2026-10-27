//===- GeneratedVPICallArgumentsTest.cpp - Call argument queries ---------===//
#include "../lib/RuntimeInternal.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Parser/Parser.h"
#include "talos/Conversion/SimulationToBytecode.h"
#include "talos/Dialect/Simulation/SimulationOps.h"
#include "talos/Reflection/DesignReflection.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "gtest/gtest.h"
#include <cstdlib>
#include <map>
#include <memory>
#include <vector>

TEST(GeneratedDesignDatabase, CallArguments) {
  using namespace talos;
  const char *input = std::getenv("TALOS_TEST_INPUT");
  ASSERT_NE(input, nullptr);
  mlir::DialectRegistry registry;
  registry.insert<mlir::arith::ArithDialect, mlir::cf::ControlFlowDialect,
                  sim::TalosSimulationDialect>();
  mlir::MLIRContext context(registry);
  auto module = mlir::parseSourceFile<mlir::ModuleOp>(input, &context);
  ASSERT_TRUE(module);
  auto design = *module->getOps<sim::SimDesignOp>().begin();
  SimulationBytecodeOptions options;
  options.vpi = "full";
  auto encoded = encodeSimulationDesign(design, options);
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
  using namespace reflection;
  SimulationBytecodeOptions offOptions;
  offOptions.vpi = "off";
  auto off = encodeSimulationDesign(design, offOptions);
  ASSERT_TRUE(mlir::succeeded(off));
  EXPECT_TRUE(off->designDatabase.empty());
  EXPECT_EQ(off->executionFlags & (TALOS_RT_EXECUTION_VPI_READ |
                                   TALOS_RT_EXECUTION_VPI_WRITE),
            0u);
  const auto &image = encoded->designDatabase;
  HeaderView header(image.data());
  SemanticDirectoryView directory(image.data() + header.getReserved());
  ASSERT_EQ(directory.getExpressionRootBindingCount(), 12u);
  auto rejects = [&](auto mutate) {
    auto bytes = image;
    mutate(bytes);
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t index = 0; index != bytes.size(); ++index) {
      hash ^=
          index >= field::HeaderChecksum && index < field::HeaderChecksum + 8
              ? 0
              : bytes[index];
      hash *= UINT64_C(1099511628211);
    }
    HeaderWriter(bytes.data()).setChecksum(hash);
    auto invalid = execution;
    invalid.design_database = bytes.data();
    EXPECT_EQ(talos_rt_v1_design_validate(&invalid),
              TALOS_RT_INVALID_DESIGN);
  };
  uint64_t roots = directory.getExpressionRootBindingOffset();
  rejects([&](auto &bytes) {
    ExpressionRootBindingWriter(bytes.data() + roots).setOrdinal(1);
  });
  rejects([&](auto &bytes) {
    ExpressionRootBindingWriter(bytes.data() + roots).setMode(0);
  });
  rejects([&](auto &bytes) {
    ExpressionRootBindingWriter(bytes.data() + roots)
        .setSelector(uint16_t(vpiExpr));
  });
  talos_rt_context *raw = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &raw),
            TALOS_RT_OK);
  std::unique_ptr<talos_rt_context, decltype(&talos_rt_v1_context_destroy)>
      runtime(raw, talos_rt_v1_context_destroy);
  ASSERT_EQ(talos_rt_v1_vpi_startup(raw, nullptr, 0), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_vpi_end_compile(raw), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_vpi_start_simulation(raw), TALOS_RT_OK);
  std::vector<vpiHandle> handles;
  auto keep = [&](vpiHandle h) {
    if (h)
      handles.push_back(h);
    return h;
  };
  auto named = [&](const char *name) {
    return keep(vpi_handle_by_name(const_cast<char *>(name), nullptr));
  };
  auto top = named("top"), a = named("top.a"), parameter = named("top.P");
  ASSERT_NE(top, nullptr);
  ASSERT_NE(a, nullptr);
  ASSERT_NE(parameter, nullptr);
  auto value = [&](vpiHandle object) {
    s_vpi_value result{};
    result.format = vpiIntVal;
    vpi_get_value(object, &result);
    s_vpi_error_info error{};
    EXPECT_EQ(vpi_chk_error(&error), 0);
    return result.value.integer;
  };
  auto put = [&](int number) {
    s_vpi_value data{};
    data.format = vpiIntVal;
    data.value.integer = number;
    vpi_put_value(a, &data, nullptr, vpiNoDelay);
    s_vpi_error_info error{};
    EXPECT_EQ(vpi_chk_error(&error), 0);
  };
  put(5);
  std::map<int, vpiHandle> calls;
  auto processes = vpi_iterate(vpiProcess, top);
  ASSERT_NE(processes, nullptr);
  while (auto process = keep(vpi_scan(processes))) {
    auto statement = keep(vpi_handle(vpiStmt, process));
    ASSERT_NE(statement, nullptr);
    ASSERT_TRUE(calls.emplace(vpi_get(vpiLineNo, statement), statement).second);
  }
  ASSERT_EQ(calls.size(), 8u);
  auto uses = vpi_iterate(vpiUse, a);
  ASSERT_NE(uses, nullptr);
  unsigned useCount = 0;
  while (auto use = keep(vpi_scan(uses))) {
    ++useCount;
    int line = vpi_get(vpiLineNo, use);
    EXPECT_TRUE(line == 101 || line == 102 || line == 104 || line == 108 ||
                line == 203);
    if (line == 203) {
      EXPECT_EQ(vpi_get(vpiType, use), vpiFuncCall);
      ASSERT_TRUE(calls.emplace(line, use).second);
    } else {
      ASSERT_TRUE(calls.count(line));
      EXPECT_EQ(vpi_compare_objects(use, calls.at(line)), 1);
    }
  }
  EXPECT_EQ(useCount, 5u);
  ASSERT_TRUE(calls.count(203));
  auto arguments = [&](vpiHandle call) {
    std::vector<vpiHandle> result;
    if (auto iterator = vpi_iterate(vpiArgument, call))
      while (auto argument = keep(vpi_scan(iterator)))
        result.push_back(argument);
    return result;
  };
  auto actuals = arguments(calls.at(101));
  ASSERT_EQ(actuals.size(), 6u);
  EXPECT_EQ(vpi_compare_objects(actuals[0], a), 1);
  EXPECT_EQ(vpi_compare_objects(actuals[1], a), 1);
  EXPECT_EQ(value(actuals[0]), 5);
  EXPECT_EQ(vpi_compare_objects(actuals[2], actuals[3]), 0);
  EXPECT_EQ(vpi_get(vpiType, actuals[2]), vpiConstant);
  EXPECT_EQ(value(actuals[2]), 1);
  EXPECT_EQ(value(actuals[3]), 1);
  EXPECT_EQ(value(actuals[4]), 6);
  EXPECT_EQ(vpi_compare_objects(actuals[5], parameter), 1);
  EXPECT_EQ(value(actuals[5]), 9);
  put(11);
  EXPECT_EQ(value(actuals[0]), 11);
  EXPECT_EQ(value(actuals[1]), 11);
  for (auto line : {102, 203}) {
    auto inputs = arguments(calls.at(line));
    ASSERT_EQ(inputs.size(), 2u);
    EXPECT_EQ(vpi_compare_objects(inputs[0], a), 1);
    EXPECT_EQ(value(inputs[0]), 11);
    EXPECT_EQ(value(inputs[1]), line == 102 ? 2 : 3);
    auto callee = keep(vpi_handle(vpiFunction, calls.at(line)));
    auto expected = named("top.f");
    ASSERT_NE(callee, nullptr);
    EXPECT_EQ(vpi_compare_objects(callee, expected), 1);
    // A result query must not execute the source function.
    s_vpi_value result{};
    result.format = vpiIntVal;
    vpi_get_value(calls.at(line), &result);
    s_vpi_error_info error{};
    EXPECT_EQ(vpi_chk_error(&error), vpiNotice);
  }
  auto castArguments = arguments(calls.at(104));
  ASSERT_EQ(castArguments.size(), 2u);
  EXPECT_EQ(vpi_compare_objects(castArguments[0], a), 1);
  EXPECT_EQ(vpi_get(vpiOpType, castArguments[1]), vpiCastOp);
  EXPECT_EQ(value(castArguments[1]), 4);
  EXPECT_NE(keep(vpi_handle(vpiTypespec, castArguments[1])), nullptr);
  for (int line : {103, 105, 106, 107})
    EXPECT_TRUE(arguments(calls.at(line)).empty());
  auto direct = arguments(calls.at(108));
  ASSERT_EQ(direct.size(), 1u);
  EXPECT_EQ(vpi_compare_objects(direct.front(), a), 1);
  auto parameterUses = vpi_iterate(vpiUse, parameter);
  ASSERT_NE(parameterUses, nullptr);
  auto parameterUse = keep(vpi_scan(parameterUses));
  ASSERT_NE(parameterUse, nullptr);
  EXPECT_EQ(vpi_compare_objects(parameterUse, calls.at(101)), 1);
  EXPECT_EQ(vpi_scan(parameterUses), nullptr);
  EXPECT_EQ(value(a), 11);
  for (auto handle : handles)
    EXPECT_EQ(vpi_free_object(handle), 1);
  EXPECT_EQ(talos_rt_v1_vpi_end_simulation(raw), TALOS_RT_OK);
}
