//===- GeneratedVPICompiledLocalsTest.cpp - Typed local bind execution
//------===//
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
struct BindProbe {
  uint64_t root = 0, block = 0;
  vpiHandle frame = nullptr, variable = nullptr, typespec = nullptr,
            scope = nullptr;
  std::vector<unsigned> phases;
};
void inspectBinding(talos_rt_context *context, BindProbe &probe,
                    unsigned phase) {
  probe.phases.push_back(phase);
  if (!phase) {
    ASSERT_EQ(
        talos_rt_v1_vpi_frame_enter(
            context, 44, TALOS_RT_VPI_FRAME_CODE_UNIT_SCOPE, 44, &probe.root),
        TALOS_RT_OK);
    ASSERT_EQ(talos_rt_v1_vpi_frame_enter(
                  context, 44, TALOS_RT_VPI_FRAME_STATEMENT_SCOPE, 11,
                  &probe.block),
              TALOS_RT_OK);
    probe.frame = vpi_handle(vpiFrame, nullptr);
    ASSERT_NE(probe.frame, nullptr);
    auto locals = vpi_iterate(vpiAutomatics, probe.frame);
    ASSERT_NE(locals, nullptr);
    probe.variable = vpi_scan(locals);
    ASSERT_NE(probe.variable, nullptr);
    EXPECT_EQ(vpi_scan(locals), nullptr);
    EXPECT_EQ(vpi_get(vpiHasActual, probe.variable), 0);
    EXPECT_STREQ(vpi_get_str(vpiName, probe.variable), "value");
    s_vpi_value value{};
    value.format = vpiIntVal;
    vpi_get_value(probe.variable, &value);
    EXPECT_EQ(vpi_chk_error(nullptr), vpiNotice);
    probe.typespec = vpi_handle(vpiTypespec, probe.variable);
    probe.scope = vpi_handle(vpiScope, probe.variable);
    ASSERT_NE(probe.typespec, nullptr);
    ASSERT_NE(probe.scope, nullptr);
    return;
  }
  if (phase == 1 || phase == 2) {
    EXPECT_EQ(vpi_get(vpiHasActual, probe.variable), 1);
    s_vpi_value value{};
    value.format = vpiIntVal;
    vpi_get_value(probe.variable, &value);
    ASSERT_EQ(vpi_chk_error(nullptr), 0);
    EXPECT_EQ(value.value.integer, phase == 1 ? 23 : 47);
    auto same = vpi_handle_by_name(const_cast<char *>("value"), probe.frame);
    ASSERT_NE(same, nullptr);
    EXPECT_EQ(vpi_compare_objects(same, probe.variable), 1);
    EXPECT_EQ(vpi_release_handle(same), 1);
    return;
  }
  ASSERT_EQ(phase, 3u);
  ASSERT_EQ(talos_rt_v1_vpi_frame_leave(context, probe.block), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_vpi_frame_leave(context, probe.root), TALOS_RT_OK);
  EXPECT_EQ(vpi_get(vpiType, probe.variable), vpiUndefined);
  EXPECT_NE(vpi_chk_error(nullptr), 0);
}
talos_rt_status
inspectCompiledBinding(talos_rt_context *context, uint32_t id,
                       const talos_rt_import_input_v1 *inputs, uint32_t count,
                       talos_rt_import_output_v1 *, uint32_t outputs,
                       void *data) {
  if (id != 123 || count != 1 || !inputs || !inputs[0].value || outputs)
    return TALOS_RT_INVALID_ARGUMENT;
  inspectBinding(context, *static_cast<BindProbe *>(data), inputs[0].value[0]);
  return ::testing::Test::HasFatalFailure() ? TALOS_RT_FATAL : TALOS_RT_OK;
}
TEST(GeneratedDesignDatabase, CompiledFrameLocalBinding) {
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
  BindProbe probe;
  ASSERT_EQ(talos_rt_v1_context_register_import(
                runtime, 123, inspectCompiledBinding, &probe),
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
  EXPECT_EQ(probe.phases, (std::vector<unsigned>{0, 1, 2, 3}));
  EXPECT_EQ(vpi_get(vpiType, probe.frame), vpiUndefined);
  EXPECT_NE(vpi_chk_error(nullptr), 0);
  EXPECT_EQ(vpi_get(vpiType, probe.typespec), vpiIntTypespec);
  EXPECT_EQ(vpi_get(vpiType, probe.scope), vpiNamedBegin);
  EXPECT_EQ(vpi_release_handle(probe.typespec), 1);
  EXPECT_EQ(vpi_release_handle(probe.scope), 1);
  EXPECT_EQ(runtime->stateValue[0], 47);
  talos_rt_v1_context_destroy(runtime);
}
} // namespace
