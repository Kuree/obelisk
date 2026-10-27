//===- GeneratedVPIResolutionProbesTest.cpp - Compiled net-origin frames
//-----------===//

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
struct Probe {
  std::vector<unsigned> phases;
  std::vector<vpiHandle> frames, origins;
};
void inspectResolution(Probe &probe, unsigned phase) {
  probe.phases.push_back(phase);
  auto current = vpi_handle(vpiFrame, nullptr);
  if (phase == 2) {
    EXPECT_EQ(current, nullptr);
    for (auto frame : probe.frames) {
      EXPECT_EQ(vpi_get(vpiType, frame), vpiUndefined);
      EXPECT_NE(vpi_chk_error(nullptr), 0);
    }
    return;
  }
  ASSERT_LT(phase, 2u);
  ASSERT_NE(current, nullptr);
  EXPECT_EQ(vpi_get(vpiActive, current), 1);
  EXPECT_EQ(vpi_handle(vpiParent, current), nullptr);
  auto origin = vpi_handle(vpiOrigin, current);
  ASSERT_NE(origin, nullptr);
  EXPECT_EQ(vpi_get(vpiType, origin), phase ? vpiNetArray : vpiNet);
  auto expected = vpi_handle_by_name(
      const_cast<char *>(phase ? "top.array_n" : "top.n"), nullptr);
  ASSERT_NE(expected, nullptr);
  EXPECT_EQ(vpi_compare_objects(origin, expected), 1);
  EXPECT_EQ(vpi_release_handle(expected), 1);
  auto statement = vpi_handle(vpiStmt, current);
  ASSERT_NE(statement, nullptr);
  EXPECT_EQ(vpi_get(vpiType, statement), vpiAssignment);
  EXPECT_EQ(vpi_release_handle(statement), 1);
  auto thread = vpi_handle(vpiThread, current);
  ASSERT_NE(thread, nullptr);
  EXPECT_EQ(vpi_get(vpiType, thread), vpiThread);
  EXPECT_EQ(vpi_release_handle(thread), 1);
  probe.frames.push_back(current);
  probe.origins.push_back(origin);
}
talos_rt_status inspectImport(talos_rt_context *, uint32_t id,
                                const talos_rt_import_input_v1 *inputs,
                                uint32_t count, talos_rt_import_output_v1 *,
                                uint32_t outputs, void *userData) {
  if (id != 123 || count != 1 || !inputs || !inputs[0].value || outputs)
    return TALOS_RT_INVALID_ARGUMENT;
  inspectResolution(*static_cast<Probe *>(userData), inputs[0].value[0]);
  return ::testing::Test::HasFatalFailure() ? TALOS_RT_FATAL : TALOS_RT_OK;
}

TEST(GeneratedDesignDatabase, CompiledResolutionProbes) {
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
  Probe probe;
  ASSERT_EQ(talos_rt_v1_context_register_import(runtime.get(), 123,
                                                  inspectImport, &probe),
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
  EXPECT_EQ(probe.phases, (std::vector<unsigned>{0, 1, 2}));
  for (auto origin : probe.origins) {
    EXPECT_NE(vpi_get(vpiType, origin), vpiUndefined);
    EXPECT_EQ(vpi_release_handle(origin), 1);
  }
  for (auto frame : probe.frames)
    EXPECT_EQ(vpi_release_handle(frame), 0);
}
} // namespace
