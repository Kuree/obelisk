//===- GeneratedVPICompiledFramesTest.cpp - Typed frame probe execution
//------===//
#include "../lib/DesignBytecodeExecution.h"
#include "../lib/RuntimeInternal.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Parser/Parser.h"
#include "talos/Conversion/SimulationToBytecode.h"
#include "talos/Dialect/DPI/DPIDialect.h"
#include "talos/Dialect/Runtime/RuntimeDialect.h"
#include "talos/Dialect/Simulation/SimulationOps.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <cstdlib>
#include <vector>

extern "C" void talos_rt_v1_dpi_import_bytecode_link_anchor(void);
namespace {
struct FrameProbe {
  vpiHandle root = nullptr, task = nullptr, function = nullptr, block = nullptr;
  std::vector<vpiHandle> immutable;
  std::vector<unsigned> phases;
};
void inspectFrames(talos_rt_context *, FrameProbe &probe, unsigned phase) {
  probe.phases.push_back(phase);
  vpiHandle current = vpi_handle(vpiFrame, nullptr);
  if (phase == 5) {
    EXPECT_EQ(current, nullptr);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    EXPECT_EQ(vpi_get(vpiType, probe.root), vpiUndefined);
    EXPECT_NE(vpi_chk_error(nullptr), 0);
    return;
  }
  ASSERT_NE(current, nullptr);
  EXPECT_EQ(vpi_get(vpiType, current), vpiFrame);
  if (phase == 0 || phase == 1 || phase == 2) {
    auto statement = vpi_handle(vpiStmt, current);
    ASSERT_NE(statement, nullptr);
    EXPECT_EQ(vpi_get(vpiType, statement), phase == 0   ? vpiTaskCall
                                           : phase == 1 ? vpiFuncCall
                                                        : vpiNullStmt);
    probe.immutable.push_back(statement);
    auto origin = vpi_handle(vpiOrigin, current);
    ASSERT_NE(origin, nullptr);
    EXPECT_EQ(vpi_get(vpiType, origin), phase == 0   ? vpiModule
                                        : phase == 1 ? vpiTaskCall
                                                     : vpiNamedBegin);
    probe.immutable.push_back(origin);
    if (phase == 0) {
      probe.root = current;
      EXPECT_EQ(vpi_handle(vpiParent, current), nullptr);
    } else if (phase == 1) {
      probe.task = current;
      auto parent = vpi_handle(vpiParent, current);
      ASSERT_NE(parent, nullptr);
      EXPECT_EQ(vpi_compare_objects(parent, probe.root), 1);
      EXPECT_EQ(vpi_release_handle(parent), 1);
    } else {
      probe.block = current;
      probe.function = vpi_handle(vpiParent, current);
      ASSERT_NE(probe.function, nullptr);
      auto call = vpi_handle(vpiOrigin, probe.function);
      ASSERT_NE(call, nullptr);
      EXPECT_EQ(vpi_get(vpiType, call), vpiFuncCall);
      probe.immutable.push_back(call);
      auto parent = vpi_handle(vpiParent, probe.function);
      ASSERT_NE(parent, nullptr);
      EXPECT_EQ(vpi_compare_objects(parent, probe.task), 1);
      EXPECT_EQ(vpi_release_handle(parent), 1);
    }
    return;
  }
  ASSERT_TRUE(phase == 3 || phase == 4);
  EXPECT_EQ(vpi_compare_objects(current, phase == 3 ? probe.task : probe.root),
            1);
  EXPECT_EQ(vpi_release_handle(current), 1);
  EXPECT_EQ(vpi_get(vpiType, phase == 3 ? probe.function : probe.task),
            vpiUndefined);
  EXPECT_NE(vpi_chk_error(nullptr), 0);
  if (phase == 3) {
    EXPECT_EQ(vpi_get(vpiType, probe.block), vpiUndefined);
    EXPECT_NE(vpi_chk_error(nullptr), 0);
  }
}
talos_rt_status
inspectCompiledFrames(talos_rt_context *context, uint32_t id,
                      const talos_rt_import_input_v1 *inputs, uint32_t count,
                      talos_rt_import_output_v1 *, uint32_t outputs,
                      void *data) {
  if (id != 123 || count != 1 || !inputs || !inputs[0].value || outputs)
    return TALOS_RT_INVALID_ARGUMENT;
  inspectFrames(context, *static_cast<FrameProbe *>(data), inputs[0].value[0]);
  return ::testing::Test::HasFatalFailure() ? TALOS_RT_FATAL : TALOS_RT_OK;
}
TEST(GeneratedDesignDatabase, CompiledFrameProbes) {
  talos_rt_v1_dpi_import_bytecode_link_anchor();
  const char *input = std::getenv("TALOS_TEST_INPUT");
  ASSERT_NE(input, nullptr);
  mlir::DialectRegistry registry;
  registry
      .insert<mlir::arith::ArithDialect, mlir::cf::ControlFlowDialect,
              talos::dpi::DPIDialect, talos::runtime::TalosRuntimeDialect,
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
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.flags = encoded->executionFlags;
  execution.bytecode = encoded->bytecode.data();
  execution.bytecode_size = encoded->bytecode.size();
  execution.design_database = encoded->designDatabase.data();
  execution.design_database_size = encoded->designDatabase.size();
  execution.state_bit_count = encoded->stateBitCount;
  execution.checksum = checksum;
  const talos_rt_dpi_scope_v1 scopes[] = {
      {0, UINT64_MAX, "$root", 5, -9, -12, 0}, {1, 0, "top", 3, -9, -12, 0}};
  execution.dpi_scopes = scopes;
  execution.dpi_scope_count = 2;
  execution.dpi_time_precision = -12;
  ASSERT_EQ(talos_rt_v1_design_validate(&execution), TALOS_RT_OK);
  talos_rt_context *runtime = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &runtime),
            TALOS_RT_OK);
  FrameProbe probe;
  ASSERT_EQ(talos_rt_v1_context_register_import(
                runtime, 123, inspectCompiledFrames, &probe),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_vpi_startup(runtime, nullptr, 0), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_vpi_end_compile(runtime), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_vpi_start_simulation(runtime), TALOS_RT_OK);
  auto startup = std::find_if(
      encoded->functions.begin(), encoded->functions.end(),
      [](const auto &function) { return function.symbol == "startup"; });
  ASSERT_NE(startup, encoded->functions.end());
  talos_rt_design_bytecode_entry_v1 entry{&execution, startup->index, 0};
  uint64_t scratchSize = 0, scratchAlignment = 0;
  ASSERT_EQ(talos_rt_validate_design_bytecode(entry, runtime, &scratchSize,
                                                &scratchAlignment),
            TALOS_RT_OK);
  ASSERT_LE(scratchAlignment, alignof(uint64_t));
  std::vector<uint64_t> frame((scratchSize + 7) / 8);
  talos_rt_fragment_action_v1 action{};
  ASSERT_EQ(talos_rt_execute_design_bytecode(entry, runtime, frame.data(),
                                               frame.size() * 8, 0, scratchSize,
                                               0, 0, &action),
            TALOS_RT_OK);
  ASSERT_EQ(action.kind, TALOS_RT_FRAGMENT_TERMINATE);
  ASSERT_EQ(talos_rt_v1_scheduler_run(runtime), TALOS_RT_OK);
  EXPECT_EQ(probe.phases, (std::vector<unsigned>{0, 1, 2, 3, 4, 5}));
  for (auto handle : probe.immutable) {
    EXPECT_NE(vpi_get(vpiType, handle), vpiUndefined);
    EXPECT_EQ(vpi_release_handle(handle), 1);
  }
  talos_rt_v1_context_destroy(runtime);
}

TEST(GeneratedDesignDatabase, FrameProbeSignaturesRejectNonIdentityLayouts) {
  using namespace talos::designbytecode;
  struct Signature {
    uint32_t id, inputs, outputs;
  };
  const Signature signatures[] = {
      {TALOS_RT_INTRINSIC_V1_VPI_CALL_PREPARE, 1, 1},
      {TALOS_RT_INTRINSIC_V1_VPI_RESOLUTION_PREPARE, 1, 1},
      {TALOS_RT_INTRINSIC_V1_VPI_FRAME_ENTER_PREPARED, 2, 1},
      {TALOS_RT_INTRINSIC_V1_VPI_FRAME_ENTER, 3, 1},
      {TALOS_RT_INTRINSIC_V1_VPI_FRAME_CHECKPOINT, 1, 0},
      {TALOS_RT_INTRINSIC_V1_VPI_FRAME_LEAVE, 1, 0},
      {TALOS_RT_INTRINSIC_V1_VPI_CALL_FINISH, 1, 0}};
  for (const auto &signature : signatures) {
    SCOPED_TRACE(signature.id);
    const uint32_t count = signature.inputs + signature.outputs;
    const uint64_t intrinsicOffset = uint64_t{count} * 40;
    const uint64_t siteOffset = intrinsicOffset + 16;
    const uint64_t operandOffset = siteOffset + 16;
    std::vector<uint8_t> bytes(operandOffset + uint64_t{count} * 8);
    auto put = [&](uint64_t offset, uint64_t value, unsigned width) {
      for (unsigned byte = 0; byte != width; ++byte)
        bytes[offset + byte] = static_cast<uint8_t>(value >> (byte * 8));
    };
    for (uint32_t index = 0; index != count; ++index) {
      const uint64_t layout = uint64_t{index} * 40;
      bytes[layout] = TALOS_RT_DBREG_BITS;
      put(layout + 4, 64, 4);
      put(layout + 8, uint64_t{index} * 8, 8);
      put(layout + 16, 8, 8);
      put(operandOffset + uint64_t{index} * 8 +
              (index < signature.inputs ? 4 : 0),
          index, 4);
    }
    put(intrinsicOffset, signature.id, 4);
    put(intrinsicOffset + 4, signature.inputs, 4);
    put(intrinsicOffset + 8, signature.outputs, 4);
    put(siteOffset + 8, signature.inputs, 4);
    put(siteOffset + 12, signature.outputs, 4);
    Image image{};
    image.data = bytes.data();
    image.size = bytes.size();
    image.layouts = 0;
    image.layoutCount = count;
    image.intrinsics = intrinsicOffset;
    image.intrinsicCount = 1;
    image.sites = siteOffset;
    image.siteCount = 1;
    image.operands = operandOffset;
    image.operandCount = count;
    Function function{};
    function.layoutCount = count;
    ASSERT_TRUE(validIntrinsic(image, function, 0));
    for (uint32_t index = 0; index != count; ++index) {
      SCOPED_TRACE(index);
      const uint64_t layout = uint64_t{index} * 40;
      bytes[layout] = TALOS_RT_DBREG_LOGIC;
      EXPECT_FALSE(validIntrinsic(image, function, 0));
      bytes[layout] = TALOS_RT_DBREG_BITS;
      bytes[layout + 1] = 1;
      EXPECT_FALSE(validIntrinsic(image, function, 0));
      bytes[layout + 1] = 0;
      put(layout + 16, 16, 8);
      EXPECT_FALSE(validIntrinsic(image, function, 0));
      put(layout + 16, 8, 8);
      put(layout + 4, 32, 4);
      EXPECT_FALSE(validIntrinsic(image, function, 0));
      put(layout + 4, 64, 4);
      EXPECT_TRUE(validIntrinsic(image, function, 0));
    }
    put(intrinsicOffset + 12, 1, 4);
    EXPECT_FALSE(validIntrinsic(image, function, 0));
  }
}
} // namespace
