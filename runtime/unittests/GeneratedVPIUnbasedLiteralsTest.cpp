//===- GeneratedVPIUnbasedLiteralsTest.cpp - Contextual literals
//-----------===//

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
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

TEST(GeneratedDesignDatabase, UnbasedLiterals) {
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
  auto named = [&](const char *name) {
    return keep(vpi_handle_by_name(const_cast<char *>(name), nullptr));
  };
  auto get = [&](int selector, vpiHandle source) {
    return keep(vpi_handle(selector, source));
  };
  auto children = [&](int selector, vpiHandle parent) {
    std::vector<vpiHandle> result;
    if (auto iterator = vpi_iterate(selector, parent))
      while (auto child = keep(vpi_scan(iterator)))
        result.push_back(child);
    return result;
  };
  auto put = [&](const char *name, uint32_t value, uint32_t unknown = 0) {
    std::string path = std::string("top.") + name;
    auto object = named(path.c_str());
    ASSERT_NE(object, nullptr);
    s_vpi_vecval vector{value, unknown};
    s_vpi_value data{};
    data.format = vpiVectorVal;
    data.value.vector = &vector;
    vpi_put_value(object, &data, nullptr, vpiNoDelay);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
  };
  std::map<std::string, vpiHandle> expressions, owners;
  auto top = named("top");
  for (auto process : children(vpiProcess, top)) {
    auto statement = get(vpiStmt, process);
    ASSERT_NE(statement, nullptr);
    auto lhs = get(vpiLhs, statement);
    ASSERT_NE(lhs, nullptr);
    std::string name = vpi_get_str(vpiName, lhs);
    owners[name] = statement;
    expressions[name] = get(vpiRhs, statement);
  }
  ASSERT_EQ(expressions.size(), 10u);
  auto bits = [&](vpiHandle object) {
    EXPECT_NE(object, nullptr);
    s_vpi_value value{};
    value.format = vpiBinStrVal;
    vpi_get_value(object, &value);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    return value.value.str ? std::string(value.value.str) : std::string{};
  };
  for (auto [name, expected] : {std::pair{"ones", std::string(8, '1')},
                                std::pair{"zeros", std::string(8, '0')},
                                std::pair{"xs", std::string(8, 'x')},
                                std::pair{"zs", std::string(8, 'z')},
                                std::pair{"wide_ones", std::string(130, '1')},
                                std::pair{"wide_zs", std::string(130, 'z')},
                                std::pair{"again", std::string(8, '1')},
                                std::pair{"to_two", std::string(8, 'x')}}) {
    auto literal = expressions.at(name);
    ASSERT_NE(literal, nullptr);
    EXPECT_EQ(vpi_get(vpiType, literal), vpiConstant);
    EXPECT_EQ(vpi_get(vpiSize, literal), int(expected.size()));
    EXPECT_EQ(bits(literal), expected);
    auto rendered = std::to_string(expected.size()) + "'b" + expected;
    EXPECT_STREQ(vpi_get_str(vpiDecompile, literal), rendered.c_str());
    auto type = get(vpiTypespec, literal);
    ASSERT_NE(type, nullptr);
    EXPECT_EQ(vpi_get(vpiType, type), vpiLogicTypespec);
  }
  // Equal-valued source occurrences retain their own object identities.
  EXPECT_EQ(
      vpi_compare_objects(expressions.at("ones"), expressions.at("again")), 0);
  auto concat = expressions.at("concat");
  ASSERT_NE(concat, nullptr);
  EXPECT_EQ(vpi_get(vpiOpType, concat), vpiConcatOp);
  EXPECT_EQ(bits(concat), "10000000");
  auto operands = children(vpiOperand, concat);
  ASSERT_EQ(operands.size(), 2u);
  EXPECT_EQ(vpi_get(vpiSize, operands[0]), 1);
  EXPECT_EQ(bits(operands[0]), "1");
  EXPECT_EQ(vpi_get(vpiType, get(vpiTypespec, operands[0])), vpiBitTypespec);
  auto operation = expressions.at("and_value");
  ASSERT_NE(operation, nullptr);
  EXPECT_EQ(vpi_get(vpiOpType, operation), vpiBitAndOp);
  operands = children(vpiOperand, operation);
  ASSERT_EQ(operands.size(), 2u);
  EXPECT_EQ(vpi_get(vpiSize, operands[1]), 8);
  EXPECT_EQ(bits(operands[1]), "11111111");
  auto uses = children(vpiUse, named("top.a"));
  ASSERT_EQ(uses.size(), 1u);
  EXPECT_EQ(vpi_compare_objects(uses.front(), owners.at("and_value")), 1);
  put("a", 0xa5);
  EXPECT_EQ(bits(operation), "10100101");
  put("a", 0x3c);
  EXPECT_EQ(bits(operation), "00111100");
  // Assignment conversion does not change the literal object itself.
  put("to_two", 0);
  EXPECT_EQ(bits(named("top.to_two")), "00000000");
  EXPECT_EQ(bits(expressions.at("to_two")), "xxxxxxxx");
  for (auto handle : handles)
    vpi_free_object(handle);
  EXPECT_EQ(talos_rt_v1_vpi_end_simulation(raw), TALOS_RT_OK);
}
