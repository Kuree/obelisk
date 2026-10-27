//===- GeneratedVPIResolutionFramesTest.cpp - Net-origin frames -----------===//

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
#include <memory>
#include <vector>

extern "C" void talos_rt_v1_dpi_import_bytecode_link_anchor(void);

namespace {
void inspectResolution(talos_rt_context *context) {
  ASSERT_NE(talos_rt_v1_process_current(context), 0u);
  EXPECT_EQ(vpi_handle(vpiFrame, nullptr), nullptr);
  for (uint64_t net : {uint64_t{0}, uint64_t{1}}) {
    SCOPED_TRACE(net);
    uint64_t ticket = 0, activation = 0, depth = UINT64_MAX;
    ASSERT_EQ(talos_rt_v1_vpi_resolution_prepare(context, net, &ticket),
              TALOS_RT_OK);
    ASSERT_NE(ticket, 0u);
    ASSERT_EQ(talos_rt_v1_vpi_frame_depth(context, &depth), TALOS_RT_OK);
    EXPECT_EQ(depth, 0u);
    EXPECT_EQ(talos_rt_v1_vpi_call_finish(context, ticket),
              TALOS_RT_INVALID_LIFECYCLE);
    uint64_t pending = UINT64_MAX;
    ASSERT_EQ(talos_rt_vpi_pending_call(context, &pending), TALOS_RT_OK);
    EXPECT_EQ(pending, 0u);
    // A valid function with the wrong source identity cannot consume it.
    EXPECT_EQ(
        talos_rt_v1_vpi_frame_enter_prepared(context, 46, 0, &activation),
        TALOS_RT_INVALID_ARGUMENT);
    EXPECT_EQ(activation, 0u);
    EXPECT_EQ(
        talos_rt_v1_vpi_frame_enter_prepared(context, 45, 1, &activation),
        TALOS_RT_INVALID_LIFECYCLE);
    EXPECT_EQ(activation, 0u);
    ASSERT_EQ(
        talos_rt_v1_vpi_frame_enter_prepared(context, 45, 0, &activation),
        TALOS_RT_OK);
    auto frame = vpi_handle(vpiFrame, nullptr);
    ASSERT_NE(frame, nullptr);
    EXPECT_EQ(vpi_get(vpiActive, frame), 1);
    EXPECT_EQ(vpi_handle(vpiParent, frame), nullptr);
    auto origin = vpi_handle(vpiOrigin, frame);
    ASSERT_NE(origin, nullptr);
    EXPECT_EQ(vpi_get(vpiType, origin), net ? vpiNetArray : vpiNet);
    EXPECT_STREQ(vpi_get_str(vpiFullName, origin),
                 net ? "top.array_n" : "top.n");
    auto thread = vpi_handle(vpiThread, frame);
    ASSERT_NE(thread, nullptr);
    ASSERT_EQ(talos_rt_v1_vpi_frame_checkpoint(context, 11), TALOS_RT_OK);
    auto statement = vpi_handle(vpiStmt, frame);
    ASSERT_NE(statement, nullptr);
    EXPECT_EQ(vpi_get(vpiType, statement), vpiAssignment);

    // Calls inside the resolution function retain ordinary call origins and
    // the actual resolver activation as their parent.
    uint64_t nestedTicket = 0, nestedActivation = 0;
    ASSERT_EQ(talos_rt_v1_vpi_call_prepare(context, 20, &nestedTicket),
              TALOS_RT_OK);
    ASSERT_EQ(talos_rt_v1_vpi_frame_enter_prepared(context, 46, 0,
                                                     &nestedActivation),
              TALOS_RT_OK);
    auto nested = vpi_handle(vpiFrame, nullptr);
    ASSERT_NE(nested, nullptr);
    auto parent = vpi_handle(vpiParent, nested);
    ASSERT_NE(parent, nullptr);
    EXPECT_EQ(vpi_compare_objects(parent, frame), 1);
    auto call = vpi_handle(vpiOrigin, nested);
    ASSERT_NE(call, nullptr);
    EXPECT_EQ(vpi_get(vpiType, call), vpiFuncCall);
    EXPECT_EQ(vpi_release_handle(parent), 1);
    EXPECT_EQ(vpi_release_handle(nested), 1);
    ASSERT_EQ(talos_rt_v1_vpi_frame_leave(context, nestedActivation),
              TALOS_RT_OK);
    ASSERT_EQ(talos_rt_v1_vpi_call_finish(context, nestedTicket),
              TALOS_RT_OK);
    ASSERT_EQ(talos_rt_v1_vpi_frame_leave(context, activation),
              TALOS_RT_OK);
    EXPECT_EQ(vpi_handle(vpiFrame, nullptr), nullptr);
    EXPECT_EQ(vpi_get(vpiType, frame), vpiUndefined);
    EXPECT_NE(vpi_chk_error(nullptr), 0);
    EXPECT_EQ(vpi_get(vpiType, origin), net ? vpiNetArray : vpiNet);
    ASSERT_EQ(talos_rt_v1_vpi_call_finish(context, ticket), TALOS_RT_OK);
    EXPECT_EQ(talos_rt_v1_vpi_call_finish(context, ticket),
              TALOS_RT_INVALID_FRAME);
    EXPECT_EQ(vpi_release_handle(frame), 0);
    for (auto handle : {origin, thread, statement, call})
      EXPECT_EQ(vpi_release_handle(handle), 1);
  }

  uint64_t ticket = UINT64_MAX, activation = 0;
  for (uint64_t net : {uint64_t{2}, UINT64_MAX}) {
    EXPECT_EQ(talos_rt_v1_vpi_resolution_prepare(context, net, &ticket),
              TALOS_RT_INVALID_ARGUMENT);
    EXPECT_EQ(ticket, 0u);
  }
  // Cancellation clears an unconsumed ticket and an entered root alike.
  for (bool enter : {false, true}) {
    ASSERT_EQ(talos_rt_v1_vpi_resolution_prepare(context, 0, &ticket),
              TALOS_RT_OK);
    if (enter)
      ASSERT_EQ(
          talos_rt_v1_vpi_frame_enter_prepared(context, 45, 0, &activation),
          TALOS_RT_OK);
    ASSERT_EQ(talos_rt_v1_vpi_frame_unwind(context, 0), TALOS_RT_OK);
    EXPECT_EQ(talos_rt_v1_vpi_call_finish(context, ticket),
              TALOS_RT_INVALID_FRAME);
  }
  EXPECT_EQ(vpi_handle(vpiFrame, nullptr), nullptr);
}

talos_rt_status inspectImport(talos_rt_context *context, uint32_t id,
                                const talos_rt_import_input_v1 *,
                                uint32_t inputs, talos_rt_import_output_v1 *,
                                uint32_t outputs, void *userData) {
  EXPECT_EQ(id, 123u);
  EXPECT_EQ(inputs, 0u);
  EXPECT_EQ(outputs, 0u);
  *static_cast<bool *>(userData) = true;
  inspectResolution(context);
  return TALOS_RT_OK;
}

TEST(GeneratedDesignDatabase, ResolutionFrames) {
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
  const talos_rt_dpi_scope_v1 scopes[] = {
      {0, UINT64_MAX, "$root", 5, -9, -12, 0}, {1, 0, "top", 3, -9, -12, 0}};
  execution.dpi_scopes = scopes;
  execution.dpi_scope_count = 2;
  execution.dpi_time_precision = -12;
  ASSERT_EQ(talos_rt_v1_design_validate(&execution), TALOS_RT_OK);
  talos_rt_context *raw = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &raw),
            TALOS_RT_OK);
  std::unique_ptr<talos_rt_context, decltype(&talos_rt_v1_context_destroy)>
      runtime(raw, talos_rt_v1_context_destroy);
  bool called = false;
  ASSERT_EQ(talos_rt_v1_context_register_import(runtime.get(), 123,
                                                  inspectImport, &called),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_vpi_startup(runtime.get(), nullptr, 0),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_vpi_end_compile(runtime.get()), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_vpi_start_simulation(runtime.get()), TALOS_RT_OK);
  auto startup = std::find_if(
      encoded->functions.begin(), encoded->functions.end(),
      [](const auto &function) { return function.symbol == "startup"; });
  ASSERT_NE(startup, encoded->functions.end());
  talos_rt_design_bytecode_entry_v1 entry{&execution, startup->index, 0};
  uint64_t size = 0, alignment = 0;
  ASSERT_EQ(talos_rt_validate_design_bytecode(entry, runtime.get(), &size,
                                                &alignment),
            TALOS_RT_OK);
  ASSERT_LE(alignment, alignof(uint64_t));
  std::vector<uint64_t> frame((size + 7) / 8);
  talos_rt_fragment_action_v1 action{};
  ASSERT_EQ(talos_rt_execute_design_bytecode(entry, runtime.get(),
                                               frame.data(), frame.size() * 8,
                                               0, size, 0, 0, &action),
            TALOS_RT_OK);
  ASSERT_EQ(action.kind, TALOS_RT_FRAGMENT_TERMINATE);
  ASSERT_EQ(talos_rt_v1_scheduler_run(runtime.get()), TALOS_RT_OK);
  EXPECT_TRUE(called);
}
} // namespace
