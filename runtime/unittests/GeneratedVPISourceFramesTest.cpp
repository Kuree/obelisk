//===- GeneratedVPISourceFramesTest.cpp - Source activation probes --------===//

#include "../lib/DesignBytecodeExecution.h"
#include "../lib/RuntimeInternal.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Parser/Parser.h"
#include "obelisk/Conversion/SimulationToBytecode.h"
#include "obelisk/Dialect/DPI/DPIDialect.h"
#include "obelisk/Dialect/Runtime/RuntimeDialect.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <optional>
#include <vector>

extern "C" void obelisk_rt_v1_dpi_import_bytecode_link_anchor(void);

namespace {
struct SourceFrames {
  unsigned calls = 0;
  bool nonlocalExit = false;
  bool recursiveExit = false;
  std::vector<vpiHandle> firstFrames, frames, origins, declarations;
};

void inspectSourceFrames(SourceFrames &state) {
  const std::array<int, 6> origins = {vpiFuncCall,   vpiFuncCall, vpiFuncCall,
                                      vpiNamedBegin, vpiTaskCall, vpiModule};
  const std::array<int, 6> statements = {0,
                                         state.nonlocalExit ? vpiFuncCall
                                                            : vpiReturnStmt,
                                         vpiReturnStmt,
                                         vpiFuncCall,
                                         vpiNamedBegin,
                                         vpiTaskCall};
  auto current = vpi_handle(vpiFrame, nullptr);
  ASSERT_NE(current, nullptr);
  const unsigned phase = state.calls++;
  ASSERT_LT(phase, 2u);
  auto thread = vpi_handle(vpiThread, current);
  ASSERT_NE(thread, nullptr);
  for (unsigned depth = 0; depth != origins.size(); ++depth) {
    SCOPED_TRACE(depth);
    ASSERT_NE(current, nullptr);
    EXPECT_EQ(vpi_get(vpiType, current), vpiFrame);
    state.frames.push_back(current);
    auto frameThread = vpi_handle(vpiThread, current);
    ASSERT_NE(frameThread, nullptr);
    EXPECT_EQ(vpi_compare_objects(frameThread, thread), 1);
    EXPECT_EQ(vpi_release_handle(frameThread), 1);
    auto origin = vpi_handle(vpiOrigin, current);
    ASSERT_NE(origin, nullptr);
    EXPECT_EQ(vpi_get(vpiType, origin), origins[depth]);
    state.declarations.push_back(origin);
    auto statement = vpi_handle(vpiStmt, current);
    if (statements[depth]) {
      ASSERT_NE(statement, nullptr);
      EXPECT_EQ(vpi_get(vpiType, statement), statements[depth]);
      state.declarations.push_back(statement);
    } else {
      EXPECT_EQ(statement, nullptr);
    }
    if (!phase) {
      state.firstFrames.push_back(current);
      state.origins.push_back(origin);
    } else {
      // The second call reuses source declarations, while every callee and
      // block activation is new. Only the enclosing initial remains live.
      if (depth == 5) {
        EXPECT_EQ(vpi_compare_objects(current, state.firstFrames[depth]), 1);
      } else {
        EXPECT_EQ(vpi_get(vpiType, state.firstFrames[depth]), vpiUndefined);
        EXPECT_NE(vpi_chk_error(nullptr), 0);
      }
      EXPECT_EQ(vpi_compare_objects(origin, state.origins[depth]),
                depth == 4 ? 0 : 1);
    }
    current = vpi_handle(vpiParent, current);
  }
  EXPECT_EQ(current, nullptr);
  EXPECT_EQ(vpi_release_handle(thread), 1);
}

void inspectRecursiveSourceFrames(SourceFrames &state) {
  const unsigned phase = state.calls++;
  ASSERT_LT(phase, 4u) << "disabled recursive body executed its trailing probe";
  std::vector<int> expected = {vpiFuncCall, vpiNamedBegin, vpiFuncCall,
                               vpiFuncCall, vpiNamedBegin, vpiTaskCall,
                               vpiModule};
  if (phase % 2)
    expected.insert(expected.begin() + 1, {vpiNamedBegin, vpiFuncCall});
  auto current = vpi_handle(vpiFrame, nullptr);
  ASSERT_NE(current, nullptr);
  std::vector<vpiHandle> activation;
  for (unsigned depth = 0; depth != expected.size(); ++depth) {
    SCOPED_TRACE(depth);
    ASSERT_NE(current, nullptr);
    EXPECT_EQ(vpi_get(vpiType, current), vpiFrame);
    activation.push_back(current);
    state.frames.push_back(current);
    auto origin = vpi_handle(vpiOrigin, current);
    ASSERT_NE(origin, nullptr);
    EXPECT_EQ(vpi_get(vpiType, origin), expected[depth]);
    state.declarations.push_back(origin);
    if (phase % 2 && depth >= 3)
      EXPECT_EQ(vpi_compare_objects(current, state.firstFrames[depth - 2]), 1);
    current = vpi_handle(vpiParent, current);
  }
  EXPECT_EQ(current, nullptr);
  if (!(phase % 2)) {
    if (phase) {
      for (unsigned depth = 0; depth + 1 != state.firstFrames.size(); ++depth) {
        EXPECT_EQ(vpi_get(vpiType, state.firstFrames[depth]), vpiUndefined);
        EXPECT_NE(vpi_chk_error(nullptr), 0);
      }
      EXPECT_EQ(
          vpi_compare_objects(activation.back(), state.firstFrames.back()), 1);
    }
    state.firstFrames = activation;
  } else {
    EXPECT_EQ(vpi_get(vpiType, state.firstFrames.front()), vpiUndefined);
    EXPECT_NE(vpi_chk_error(nullptr), 0);
    EXPECT_EQ(vpi_compare_objects(activation[1], activation[3]), 0);
    auto inner = vpi_handle(vpiOrigin, activation[1]);
    auto outer = vpi_handle(vpiOrigin, activation[3]);
    ASSERT_NE(inner, nullptr);
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(vpi_compare_objects(inner, outer), 1);
    state.declarations.push_back(inner);
    state.declarations.push_back(outer);
  }
}

obelisk_rt_status inspectSourceFrameImport(obelisk_rt_context *, uint32_t,
                                           const obelisk_rt_import_input_v1 *,
                                           uint32_t inputCount,
                                           obelisk_rt_import_output_v1 *outputs,
                                           uint32_t outputCount, void *data) {
  if (inputCount || outputCount != 1 || !outputs || !outputs[0].value ||
      outputs[0].bit_width != 32 || outputs[0].limb_count != 1)
    return OBELISK_RT_INVALID_ARGUMENT;
  auto &state = *static_cast<SourceFrames *>(data);
  if (state.recursiveExit)
    inspectRecursiveSourceFrames(state);
  else
    inspectSourceFrames(state);
  outputs[0].value[0] = state.recursiveExit ? state.calls % 2 : 1;
  return ::testing::Test::HasFatalFailure() ? OBELISK_RT_FATAL : OBELISK_RT_OK;
}

void runSourceFrameActivations(bool nonlocalExit, bool recursiveExit = false) {
  obelisk_rt_v1_dpi_import_bytecode_link_anchor();
  const char *input = std::getenv("OBELISK_TEST_INPUT");
  ASSERT_NE(input, nullptr);
  mlir::DialectRegistry registry;
  registry
      .insert<mlir::arith::ArithDialect, mlir::cf::ControlFlowDialect,
              obelisk::dpi::DPIDialect, obelisk::runtime::ObeliskRuntimeDialect,
              obelisk::sim::ObeliskSimulationDialect>();
  mlir::MLIRContext context(registry);
  auto module = mlir::parseSourceFile<mlir::ModuleOp>(input, &context);
  ASSERT_TRUE(module);
  auto design = *module->getOps<obelisk::sim::SimDesignOp>().begin();
  std::optional<uint32_t> importID;
  design.walk([&](obelisk::sim::SimDPICallOp call) {
    EXPECT_EQ(call.getCIdentifier(), "frame_source_probe");
    importID = call.getImportId();
  });
  ASSERT_TRUE(importID);
  obelisk::SimulationBytecodeOptions options;
  options.vpi = "full";
  auto encoded = obelisk::encodeSimulationDesign(design, options);
  ASSERT_TRUE(mlir::succeeded(encoded));
  uint64_t checksum = 0;
  for (unsigned byte = 0; byte != 8; ++byte)
    checksum |= uint64_t{encoded->bytecode[32 + byte]} << (byte * 8);
  obelisk_rt_execution_descriptor_v1 execution{};
  execution.version = OBELISK_RT_VERSION;
  execution.flags = encoded->executionFlags;
  execution.bytecode = encoded->bytecode.data();
  execution.bytecode_size = encoded->bytecode.size();
  execution.design_database = encoded->designDatabase.data();
  execution.design_database_size = encoded->designDatabase.size();
  execution.state_bit_count = encoded->stateBitCount;
  execution.checksum = checksum;
  const obelisk_rt_dpi_scope_v1 scopes[] = {
      {0, UINT64_MAX, "$root", 5, -15, -15, 0}, {1, 0, "top", 3, -15, -15, 0}};
  execution.dpi_scopes = scopes;
  execution.dpi_scope_count = 2;
  execution.dpi_time_precision = -15;
  ASSERT_EQ(obelisk_rt_v1_design_validate(&execution), OBELISK_RT_OK);
  obelisk_rt_context *runtime = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &runtime),
            OBELISK_RT_OK);
  SourceFrames state;
  state.nonlocalExit = nonlocalExit;
  state.recursiveExit = recursiveExit;
  ASSERT_EQ(obelisk_rt_v1_context_register_import(
                runtime, *importID, inspectSourceFrameImport, &state),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(runtime, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(runtime), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(runtime), OBELISK_RT_OK);
  auto startup = std::find_if(
      encoded->functions.begin(), encoded->functions.end(),
      [](const auto &function) { return function.symbol == "__obelisk_root"; });
  ASSERT_NE(startup, encoded->functions.end());
  obelisk_rt_design_bytecode_entry_v1 entry{&execution, startup->index, 0};
  uint64_t scratchSize = 0, scratchAlignment = 0;
  ASSERT_EQ(obelisk_rt_validate_design_bytecode(entry, runtime, &scratchSize,
                                                &scratchAlignment),
            OBELISK_RT_OK);
  ASSERT_LE(scratchAlignment, alignof(uint64_t));
  std::vector<uint64_t> scratch((scratchSize + 7) / 8);
  obelisk_rt_fragment_action_v1 action{};
  ASSERT_EQ(obelisk_rt_execute_design_bytecode(entry, runtime, scratch.data(),
                                               scratch.size() * 8, 0,
                                               scratchSize, 0, 0, &action),
            OBELISK_RT_OK);
  ASSERT_EQ(action.kind, OBELISK_RT_FRAGMENT_TERMINATE);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(runtime), OBELISK_RT_OK);
  EXPECT_EQ(state.calls, recursiveExit ? 4u : 2u);
  for (auto frame : state.frames) {
    EXPECT_EQ(vpi_get(vpiType, frame), vpiUndefined);
    EXPECT_NE(vpi_chk_error(nullptr), 0);
  }
  for (auto declaration : state.declarations) {
    EXPECT_NE(vpi_get(vpiType, declaration), vpiUndefined);
    EXPECT_EQ(vpi_release_handle(declaration), 1);
  }
  obelisk_rt_v1_context_destroy(runtime);
}
TEST(GeneratedDesignDatabase, SourceFrameActivations) {
  runSourceFrameActivations(false);
}
TEST(GeneratedDesignDatabase, SourceFrameNonlocalExit) {
  runSourceFrameActivations(true);
}
TEST(GeneratedDesignDatabase, SourceFrameRecursiveExit) {
  runSourceFrameActivations(true, true);
}
} // namespace
