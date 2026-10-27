//===- GeneratedVPIFramesTest.cpp - Dynamic procedural frame queries ------===//

#include "../lib/DesignBytecodeImage.h"
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
  uint64_t token = 0;
  uint64_t rootID = 0;
  uint64_t middleID = 0;
  uint64_t blockID = 0;
  vpiHandle thread = nullptr;
  vpiHandle root = nullptr;
  vpiHandle middle = nullptr;
  vpiHandle block = nullptr;
  vpiHandle call = nullptr;
  vpiHandle nestedCall = nullptr;
  vpiHandle statement = nullptr;
  std::vector<unsigned> phases;
};

void expectSameRelation(vpiHandle object, int relation, vpiHandle expected) {
  auto related = vpi_handle(relation, object);
  ASSERT_NE(related, nullptr);
  EXPECT_EQ(vpi_compare_objects(related, expected), 1);
  EXPECT_EQ(vpi_release_handle(related), 1);
}

void inspectFrameABI(talos_rt_context *context, FrameProbe &probe,
                     unsigned phase) {
  probe.phases.push_back(phase);
  ASSERT_NE(talos_rt_v1_process_current(context), 0u);
  if (phase == 0) {
    probe.token = talos_rt_v1_process_current(context);
    EXPECT_EQ(vpi_handle(vpiFrame, nullptr), nullptr);
    uint64_t depth = UINT64_MAX;
    ASSERT_EQ(talos_rt_v1_vpi_frame_depth(context, &depth), TALOS_RT_OK);
    EXPECT_EQ(depth, 0u);
    ASSERT_EQ(talos_rt_v1_vpi_frame_enter(
                  context, 44, TALOS_RT_VPI_FRAME_CODE_UNIT_SCOPE, 44,
                  &probe.rootID),
              TALOS_RT_OK);
    ASSERT_NE(probe.rootID, 0u);
    probe.root = vpi_handle(vpiFrame, nullptr);
    ASSERT_NE(probe.root, nullptr);
    EXPECT_EQ(vpi_get(vpiType, probe.root), vpiFrame);
    EXPECT_EQ(vpi_get(vpiActive, probe.root), 1);
    EXPECT_EQ(vpi_get(vpiAllocScheme, probe.root), vpiAutomaticScheme);
    EXPECT_EQ(vpi_handle(vpiParent, probe.root), nullptr);
    EXPECT_EQ(vpi_handle(vpiStmt, probe.root), nullptr);
    auto scope = vpi_handle(vpiOrigin, probe.root);
    ASSERT_NE(scope, nullptr);
    EXPECT_EQ(vpi_get(vpiType, scope), vpiModule);
    EXPECT_STREQ(vpi_get_str(vpiFullName, scope), "top");
    EXPECT_EQ(vpi_release_handle(scope), 1);
    probe.thread = vpi_handle(vpiThread, probe.root);
    ASSERT_NE(probe.thread, nullptr);
    expectSameRelation(probe.thread, vpiFrame, probe.root);
    // Before a source statement executes absence is quiet. An explicit clear
    // records unavailable metadata, and a valid checkpoint restores identity.
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    ASSERT_EQ(talos_rt_v1_vpi_frame_checkpoint(context, 0), TALOS_RT_OK);
    EXPECT_EQ(vpi_handle(vpiStmt, probe.root), nullptr);
    s_vpi_error_info unavailable{};
    EXPECT_NE(vpi_chk_error(&unavailable), 0);
    EXPECT_EQ(unavailable.level, vpiNotice);
    EXPECT_EQ(talos_rt_v1_vpi_frame_checkpoint(context, 13),
              TALOS_RT_INVALID_FRAME);
    EXPECT_EQ(vpi_handle(vpiStmt, probe.root), nullptr);
    EXPECT_NE(vpi_chk_error(&unavailable), 0);
    EXPECT_EQ(unavailable.level, vpiNotice);
    ASSERT_EQ(talos_rt_v1_vpi_frame_checkpoint(context, 11), TALOS_RT_OK);
    probe.call = vpi_handle(vpiStmt, probe.root);
    ASSERT_NE(probe.call, nullptr);
    EXPECT_EQ(vpi_get(vpiType, probe.call), vpiTaskCall);

    // Callee ownership and source ownership must both match before mutation.
    uint64_t rejected = UINT64_MAX;
    EXPECT_EQ(talos_rt_v1_vpi_frame_enter(
                  context, 46, TALOS_RT_VPI_FRAME_CALL, 20, &rejected),
              TALOS_RT_INVALID_ARGUMENT);
    EXPECT_EQ(rejected, 0u);
    EXPECT_EQ(talos_rt_v1_vpi_frame_checkpoint(context, 13),
              TALOS_RT_INVALID_FRAME);
    EXPECT_EQ(
        talos_rt_v1_vpi_frame_enter(
            context, 45, TALOS_RT_VPI_FRAME_STATEMENT_SCOPE, 12, &rejected),
        TALOS_RT_INVALID_FRAME);
    ASSERT_EQ(talos_rt_v1_vpi_frame_depth(context, &depth), TALOS_RT_OK);
    EXPECT_EQ(depth, 1u);
    EXPECT_EQ(
        talos_rt_v1_vpi_frame_enter(
            context, 44, TALOS_RT_VPI_FRAME_CODE_UNIT_SCOPE, 44, &rejected),
        TALOS_RT_INVALID_FRAME);
    EXPECT_EQ(talos_rt_v1_vpi_frame_checkpoint(context, UINT64_MAX),
              TALOS_RT_INVALID_ARGUMENT);
    expectSameRelation(probe.root, vpiStmt, probe.call);
    ASSERT_EQ(talos_rt_v1_vpi_frame_enter(
                  context, 45, TALOS_RT_VPI_FRAME_CALL, 20, &probe.middleID),
              TALOS_RT_OK);
    probe.middle = vpi_handle(vpiFrame, nullptr);
    ASSERT_NE(probe.middle, nullptr);
    expectSameRelation(probe.middle, vpiParent, probe.root);
    expectSameRelation(probe.middle, vpiOrigin, probe.call);
    expectSameRelation(probe.middle, vpiThread, probe.thread);
    EXPECT_EQ(vpi_get(vpiActive, probe.root), 0);
    EXPECT_EQ(vpi_get(vpiActive, probe.middle), 1);
    EXPECT_EQ(talos_rt_v1_vpi_frame_leave(context, probe.rootID),
              TALOS_RT_INVALID_FRAME);
    ASSERT_EQ(talos_rt_v1_vpi_frame_checkpoint(context, 12), TALOS_RT_OK);
    ASSERT_EQ(talos_rt_v1_vpi_frame_enter(
                  context, 45, TALOS_RT_VPI_FRAME_STATEMENT_SCOPE, 12,
                  &probe.blockID),
              TALOS_RT_OK);
    probe.block = vpi_handle(vpiFrame, nullptr);
    ASSERT_NE(probe.block, nullptr);
    expectSameRelation(probe.block, vpiParent, probe.middle);
    auto blockOrigin = vpi_handle(vpiOrigin, probe.block);
    ASSERT_NE(blockOrigin, nullptr);
    EXPECT_EQ(vpi_get(vpiType, blockOrigin), vpiNamedBegin);
    expectSameRelation(probe.middle, vpiStmt, blockOrigin);
    EXPECT_EQ(vpi_release_handle(blockOrigin), 1);
    ASSERT_EQ(talos_rt_v1_vpi_frame_checkpoint(context, 13), TALOS_RT_OK);
    probe.statement = vpi_handle(vpiStmt, probe.block);
    ASSERT_NE(probe.statement, nullptr);
    EXPECT_EQ(vpi_get(vpiType, probe.statement), vpiAssignment);

    uint64_t innerID = 0;
    ASSERT_EQ(talos_rt_v1_vpi_frame_enter(
                  context, 46, TALOS_RT_VPI_FRAME_CALL, 21, &innerID),
              TALOS_RT_OK);
    auto inner = vpi_handle(vpiFrame, nullptr);
    ASSERT_NE(inner, nullptr);
    expectSameRelation(inner, vpiParent, probe.block);
    expectSameRelation(probe.block, vpiStmt, probe.statement);
    probe.nestedCall = vpi_handle(vpiOrigin, inner);
    ASSERT_NE(probe.nestedCall, nullptr);
    EXPECT_EQ(vpi_get(vpiType, probe.nestedCall), vpiFuncCall);
    EXPECT_EQ(vpi_get(vpiSize, probe.nestedCall), vpiUndefined);
    EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
    ASSERT_EQ(talos_rt_v1_vpi_frame_checkpoint(context, 14), TALOS_RT_OK);
    auto body = vpi_handle(vpiStmt, inner);
    ASSERT_NE(body, nullptr);
    EXPECT_EQ(vpi_get(vpiType, body), vpiNullStmt);
    ASSERT_EQ(talos_rt_v1_vpi_frame_leave(context, innerID), TALOS_RT_OK);
    EXPECT_EQ(vpi_get(vpiType, inner), vpiUndefined);
    EXPECT_NE(vpi_chk_error(nullptr), 0);
    EXPECT_EQ(vpi_get(vpiType, body), vpiNullStmt);
    EXPECT_EQ(vpi_release_handle(body), 1);
    // Repeated calls share source identity but never activation identity.
    uint64_t repeatedID = 0;
    ASSERT_EQ(talos_rt_v1_vpi_frame_enter(
                  context, 46, TALOS_RT_VPI_FRAME_CALL, 21, &repeatedID),
              TALOS_RT_OK);
    EXPECT_GT(repeatedID, innerID);
    auto repeated = vpi_handle(vpiFrame, nullptr);
    ASSERT_NE(repeated, nullptr);
    expectSameRelation(repeated, vpiOrigin, probe.nestedCall);
    EXPECT_EQ(vpi_release_handle(repeated), 1);
    ASSERT_EQ(talos_rt_v1_vpi_frame_leave(context, repeatedID),
              TALOS_RT_OK);
    ASSERT_EQ(talos_rt_v1_vpi_frame_depth(context, &depth), TALOS_RT_OK);
    EXPECT_EQ(depth, 3u);
    EXPECT_EQ(vpi_get(vpiActive, probe.block), 1);
  } else {
    ASSERT_EQ(phase, 1u);
    EXPECT_EQ(talos_rt_v1_process_current(context), probe.token);
    auto resumed = vpi_handle(vpiFrame, nullptr);
    ASSERT_NE(resumed, nullptr);
    EXPECT_EQ(vpi_compare_objects(resumed, probe.block), 1);
    EXPECT_EQ(vpi_release_handle(resumed), 1);
    EXPECT_EQ(vpi_get(vpiActive, probe.block), 1);
    EXPECT_EQ(talos_rt_v1_vpi_frame_unwind(context, 4),
              TALOS_RT_INVALID_FRAME);
    ASSERT_EQ(talos_rt_v1_vpi_frame_unwind(context, 1), TALOS_RT_OK);
    EXPECT_EQ(vpi_get(vpiType, probe.block), vpiUndefined);
    EXPECT_NE(vpi_chk_error(nullptr), 0);
    EXPECT_EQ(vpi_get(vpiType, probe.middle), vpiUndefined);
    EXPECT_NE(vpi_chk_error(nullptr), 0);
    EXPECT_EQ(vpi_get(vpiActive, probe.root), 1);
    expectSameRelation(probe.root, vpiStmt, probe.call);
    ASSERT_EQ(talos_rt_v1_vpi_frame_leave(context, probe.rootID),
              TALOS_RT_OK);
    EXPECT_EQ(vpi_get(vpiType, probe.root), vpiUndefined);
    EXPECT_NE(vpi_chk_error(nullptr), 0);
    EXPECT_EQ(vpi_handle(vpiFrame, nullptr), nullptr);
    EXPECT_EQ(vpi_handle(vpiFrame, probe.thread), nullptr);
    uint64_t depth = UINT64_MAX;
    ASSERT_EQ(talos_rt_v1_vpi_frame_depth(context, &depth), TALOS_RT_OK);
    EXPECT_EQ(depth, 0u);
  }
}

talos_rt_status inspectFrames(talos_rt_context *context, uint32_t importID,
                                const talos_rt_import_input_v1 *inputs,
                                uint32_t inputCount,
                                talos_rt_import_output_v1 *,
                                uint32_t outputCount, void *userData) {
  EXPECT_EQ(importID, 123u);
  EXPECT_EQ(inputCount, 1u);
  EXPECT_EQ(outputCount, 0u);
  if (inputCount != 1 || !inputs || !inputs[0].value)
    return TALOS_RT_INVALID_ARGUMENT;
  inspectFrameABI(context, *static_cast<FrameProbe *>(userData),
                  inputs[0].value[0]);
  return ::testing::Test::HasFatalFailure() ? TALOS_RT_FATAL : TALOS_RT_OK;
}

TEST(GeneratedDesignDatabase, FrameQueriesFollowActivationLifetime) {
  // Drive the public activation ABI from a real executing process. The authored
  // metadata distinguishes an atomic call statement from a nested expression.
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
  ASSERT_EQ(talos_rt_v1_context_register_import(runtime, 123, inspectFrames,
                                                  &probe),
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
  ASSERT_EQ(probe.phases, (std::vector<unsigned>{0}));
  ASSERT_NE(probe.thread, nullptr);
  EXPECT_EQ(vpi_handle(vpiFrame, nullptr), nullptr);
  EXPECT_EQ(vpi_get(vpiActive, probe.thread), 0);
  EXPECT_EQ(vpi_get(vpiActive, probe.block), 0);
  expectSameRelation(probe.thread, vpiFrame, probe.block);
  expectSameRelation(probe.block, vpiParent, probe.middle);
  expectSameRelation(probe.block, vpiStmt, probe.statement);
  talos_rt_v1_scheduler_event(runtime, 7, 0);
  ASSERT_EQ(talos_rt_v1_scheduler_run(runtime), TALOS_RT_OK);
  EXPECT_EQ(probe.phases, (std::vector<unsigned>{0, 1}));
  EXPECT_EQ(vpi_get(vpiType, probe.thread), vpiUndefined);
  EXPECT_NE(vpi_chk_error(nullptr), 0);
  // Source objects have design lifetime, independent of every activation.
  EXPECT_EQ(vpi_get(vpiType, probe.call), vpiTaskCall);
  EXPECT_EQ(vpi_get(vpiType, probe.nestedCall), vpiFuncCall);
  EXPECT_EQ(vpi_get(vpiType, probe.statement), vpiAssignment);
  for (auto handle : {probe.call, probe.nestedCall, probe.statement})
    EXPECT_EQ(vpi_release_handle(handle), 1);
  talos_rt_v1_context_destroy(runtime);
}
} // namespace
