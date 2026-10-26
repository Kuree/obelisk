//===- GeneratedVPIThreadCallOriginsTest.cpp - Bytecode thread origins -----===//

#include "../lib/DesignBytecodeImage.h"
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
#include <cstdlib>
#include <vector>

extern "C" void obelisk_rt_v1_dpi_import_bytecode_link_anchor(void);

namespace {
struct OriginProbe {
  uint64_t token = 0;
  vpiHandle thread = nullptr;
  vpiHandle body = nullptr;
  std::vector<unsigned> phases;
};

obelisk_rt_status inspectThreadOrigin(obelisk_rt_context *context,
                                      uint32_t importID,
                                      const obelisk_rt_import_input_v1 *inputs,
                                      uint32_t inputCount,
                                      obelisk_rt_import_output_v1 *,
                                      uint32_t outputCount, void *userData) {
  auto &probe = *static_cast<OriginProbe *>(userData);
  EXPECT_EQ(importID, 123u);
  EXPECT_EQ(inputCount, 1u);
  EXPECT_EQ(outputCount, 0u);
  if (inputCount != 1 || !inputs || !inputs[0].value)
    return OBELISK_RT_INVALID_ARGUMENT;
  const unsigned phase = inputs[0].value[0];
  probe.phases.push_back(phase);
  const uint64_t token = obelisk_rt_v1_process_current(context);
  EXPECT_EQ(token, probe.token);
  EXPECT_EQ(context->activeLogicalProcessToken, token);
  // This is a real executing bytecode task: the scheduler removed its indexed
  // record while preserving the active record and the suspended call chain.
  EXPECT_EQ(context->scheduledDesignTaskIndices.count(token), 0u);
  const auto *task = context->activeDesignTask;
  EXPECT_NE(task, nullptr);
  if (!task)
    return OBELISK_RT_INVALID_LIFECYCLE;
  EXPECT_EQ(task->id, token);
  EXPECT_EQ(task->callers.size(), phase == 2                 ? 2u
                                  : phase == 1 || phase == 4 ? 1u
                                                             : 0u);
  EXPECT_EQ(obelisk::designbytecode::functionAt(context->designBytecodeImage,
                                                task->function)
                .id,
            phase == 2                 ? 46u
            : phase == 1 || phase == 4 ? 45u
                                       : 44u);
  uint64_t origin = UINT64_MAX;
  EXPECT_EQ(obelisk_rt_logical_process_origin(context, token, &origin),
            OBELISK_RT_OK);
  EXPECT_EQ(origin, 44u);
  EXPECT_EQ(vpi_get(vpiActive, probe.thread), 1);
  auto source = vpi_handle(vpiOrigin, probe.thread);
  EXPECT_NE(source, nullptr);
  if (source) {
    EXPECT_EQ(vpi_compare_objects(source, probe.body), 1);
    EXPECT_EQ(vpi_release_handle(source), 1);
  }
  return OBELISK_RT_OK;
}

TEST(GeneratedDesignDatabase, ThreadOriginKeepsOutermostBytecodeActivation) {
  // Generated simulators initialize the same optional DPI marshalling hooks.
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
      {0, UINT64_MAX, "$root", 5, -9, -12, 0},
      {1, 0, "top", 3, -9, -12, 0}};
  execution.dpi_scopes = scopes;
  execution.dpi_scope_count = 2;
  execution.dpi_time_precision = -12;
  ASSERT_EQ(obelisk_rt_v1_design_validate(&execution), OBELISK_RT_OK);
  obelisk_rt_context *runtime = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &runtime),
            OBELISK_RT_OK);
  OriginProbe probe;
  ASSERT_EQ(obelisk_rt_v1_context_register_import(runtime, 123,
                                                  inspectThreadOrigin, &probe),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(runtime, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(runtime), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(runtime), OBELISK_RT_OK);
  auto startup = std::find_if(
      encoded->functions.begin(), encoded->functions.end(),
      [](const auto &function) { return function.symbol == "startup"; });
  ASSERT_NE(startup, encoded->functions.end());
  obelisk_rt_design_bytecode_entry_v1 entry{&execution, startup->index, 0};
  uint64_t scratchSize = 0, scratchAlignment = 0;
  ASSERT_EQ(obelisk_rt_validate_design_bytecode(entry, runtime, &scratchSize,
                                                &scratchAlignment),
            OBELISK_RT_OK);
  ASSERT_LE(scratchAlignment, alignof(uint64_t));
  std::vector<uint64_t> frame((scratchSize + 7) / 8);
  obelisk_rt_fragment_action_v1 action{};
  ASSERT_EQ(obelisk_rt_execute_design_bytecode(entry, runtime, frame.data(),
                                               frame.size() * 8, 0, scratchSize,
                                               0, 0, &action),
            OBELISK_RT_OK);
  ASSERT_EQ(action.kind, OBELISK_RT_FRAGMENT_TERMINATE);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(runtime), OBELISK_RT_OK);
  ASSERT_EQ(runtime->scheduledDesignTasks.size(), 1u);
  probe.token = runtime->scheduledDesignTasks.front().id;
  ASSERT_TRUE(probe.phases.empty());
  auto event = vpi_handle_by_name(const_cast<char *>("top.ready"), nullptr);
  auto owner = vpi_handle_by_name(const_cast<char *>("top.outer"), nullptr);
  ASSERT_NE(event, nullptr);
  ASSERT_NE(owner, nullptr);
  probe.body = vpi_handle(vpiStmt, owner);
  ASSERT_NE(probe.body, nullptr);
  auto waiting = vpi_iterate(vpiWaitingProcesses, event);
  ASSERT_NE(waiting, nullptr);
  probe.thread = vpi_scan(waiting);
  ASSERT_NE(probe.thread, nullptr);
  EXPECT_EQ(vpi_scan(waiting), nullptr);
  EXPECT_EQ(vpi_get(vpiActive, probe.thread), 0);
  auto source = vpi_handle(vpiOrigin, probe.thread);
  ASSERT_NE(source, nullptr);
  EXPECT_EQ(vpi_compare_objects(source, probe.body), 1);
  uint64_t origin = UINT64_MAX;
  ASSERT_EQ(obelisk_rt_logical_process_origin(runtime, probe.token, &origin),
            OBELISK_RT_OK);
  EXPECT_EQ(origin, 44u);
  obelisk_rt_v1_scheduler_event(runtime, 7, 0);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(runtime), OBELISK_RT_OK);
  EXPECT_EQ(probe.phases, (std::vector<unsigned>{0, 1, 2, 4, 3}));
  EXPECT_EQ(obelisk_rt_logical_process_origin(runtime, probe.token, &origin),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(vpi_get(vpiType, probe.thread), vpiUndefined);
  EXPECT_NE(vpi_chk_error(nullptr), 0);
  // Finishing the logical thread does not invalidate its original source.
  EXPECT_EQ(vpi_get(vpiType, source), vpiNamedBegin);
  EXPECT_EQ(vpi_compare_objects(source, probe.body), 1);
  for (auto handle : {source, probe.body, owner, event})
    EXPECT_EQ(vpi_release_handle(handle), 1);
  obelisk_rt_v1_context_destroy(runtime);
}
} // namespace
