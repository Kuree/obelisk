//===- GeneratedVPIBitTypespecTest.cpp - Selected scalar type queries -----===//

#include "../lib/RuntimeInternal.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Parser/Parser.h"
#include "talos/Conversion/SimulationToBytecode.h"
#include "talos/Dialect/Simulation/SimulationOps.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "gtest/gtest.h"
#include <array>
#include <cstdlib>
#include <memory>

namespace {
TEST(GeneratedDesignDatabase, BitTypespec) {
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
  talos_rt_context *raw = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &raw),
            TALOS_RT_OK);
  std::unique_ptr<talos_rt_context, decltype(&talos_rt_v1_context_destroy)>
      runtime(raw, talos_rt_v1_context_destroy);
  ASSERT_EQ(talos_rt_v1_vpi_startup(raw, nullptr, 0), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_vpi_end_compile(raw), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_vpi_start_simulation(raw), TALOS_RT_OK);
  struct Case {
    const char *name;
    int index;
    int bitKind;
    int typespecKind;
  };
  const std::array<Case, 5> cases{{
      {"top.signed_int", 31, vpiRegBit, vpiBitTypespec},
      {"top.integer", 31, vpiRegBit, vpiLogicTypespec},
      {"top.word", 7, vpiRegBit, vpiLogicTypespec},
      {"top.state", 31, vpiRegBit, vpiBitTypespec},
      {"top.int_net", 31, vpiNetBit, vpiBitTypespec},
  }};
  for (const auto &test : cases) {
    SCOPED_TRACE(test.name);
    auto object = vpi_handle_by_name(const_cast<char *>(test.name), nullptr);
    ASSERT_NE(object, nullptr);
    auto originalType = vpi_handle(vpiTypespec, object);
    ASSERT_NE(originalType, nullptr);
    auto bit = vpi_handle_by_index(object, test.index);
    ASSERT_NE(bit, nullptr);
    EXPECT_EQ(vpi_get(vpiType, bit), test.bitKind);
    auto type = vpi_handle(vpiTypespec, bit);
    ASSERT_NE(type, nullptr);
    EXPECT_EQ(vpi_get(vpiType, type), test.typespecKind);
    EXPECT_EQ(vpi_get(vpiSize, bit), 1);
    EXPECT_EQ(vpi_get(vpiSigned, bit), 0);
    EXPECT_EQ(vpi_get(vpiVector, type), 0);
    EXPECT_EQ(vpi_get_str(vpiName, type), nullptr);
    EXPECT_EQ(vpi_handle(vpiTypedefAlias, type), nullptr);
    EXPECT_EQ(vpi_handle(vpiElemTypespec, type), nullptr);
    EXPECT_EQ(vpi_handle(vpiLeftRange, type), nullptr);
    EXPECT_EQ(vpi_handle(vpiRightRange, type), nullptr);
    EXPECT_EQ(vpi_iterate(vpiRange, type), nullptr);
    EXPECT_EQ(vpi_iterate(vpiMember, type), nullptr);
    EXPECT_EQ(vpi_compare_objects(originalType, type), 0);
    auto again = vpi_handle(vpiTypespec, bit);
    ASSERT_NE(again, nullptr);
    EXPECT_EQ(vpi_compare_objects(type, again), 1);
    EXPECT_EQ(vpi_release_handle(bit), 1);
    EXPECT_EQ(vpi_release_handle(object), 1);
    EXPECT_EQ(vpi_get(vpiVector, type), 0);
    for (auto handle : {again, type, originalType})
      EXPECT_EQ(vpi_release_handle(handle), 1);
  }
}
} // namespace
