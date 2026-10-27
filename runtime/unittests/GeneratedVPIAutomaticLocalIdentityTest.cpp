//===- GeneratedVPIAutomaticLocalIdentityTest.cpp - Automatic identity
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
#include "talos/Reflection/DesignReflection.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <vector>

extern "C" void talos_rt_v1_dpi_import_bytecode_link_anchor(void);

namespace {
struct ForScopes {
  unsigned calls = 0;
  bool observe = true;
  std::vector<vpiHandle> values, scopes;
};

void inspectForScope(ForScopes &state) {
  unsigned phase = state.calls++;
  ASSERT_LT(phase, 10u);
  auto imported = vpi_handle(vpiFrame, nullptr);
  ASSERT_NE(imported, nullptr);
  auto loopFrame = vpi_handle(vpiParent, imported);
  ASSERT_NE(loopFrame, nullptr);
  auto locals = vpi_iterate(vpiAutomatics, loopFrame);
  ASSERT_NE(locals, nullptr);
  auto value = vpi_scan(locals);
  ASSERT_NE(value, nullptr);
  EXPECT_EQ(vpi_scan(locals), nullptr);
  EXPECT_STREQ(vpi_get_str(vpiName, value), "i");
  EXPECT_EQ(vpi_get(vpiHasActual, value), 1);
  s_vpi_value integer{};
  integer.format = vpiIntVal;
  vpi_get_value(value, &integer);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);
  EXPECT_EQ(integer.value.integer, phase % 2 + (phase < 4   ? 10
                                                : phase < 6 ? 5
                                                            : 2));
  auto scope = vpi_handle(vpiScope, value);
  ASSERT_NE(scope, nullptr);
  EXPECT_EQ(vpi_get(vpiType, scope), phase < 6 ? vpiFor : vpiForeachStmt);
  if (phase < 6)
    EXPECT_EQ(vpi_get(vpiLocalVarDecls, scope), 1);
  EXPECT_EQ(vpi_get_str(vpiName, scope), nullptr);
  if (phase % 2)
    EXPECT_EQ(vpi_compare_objects(value, state.values.back()), 1);
  if (phase != 0 && phase % 2 == 0) {
    EXPECT_EQ(vpi_compare_objects(value, state.values.back()), 0);
    EXPECT_EQ(vpi_get(vpiType, state.values.back()), vpiUndefined);
    EXPECT_NE(vpi_chk_error(nullptr), 0);
    EXPECT_EQ(vpi_compare_objects(scope, state.scopes.back()), phase == 2);
  }
  if (phase < 4) {
    auto outerFrame = vpi_handle(vpiParent, loopFrame);
    ASSERT_NE(outerFrame, nullptr);
    auto outerLocals = vpi_iterate(vpiAutomatics, outerFrame);
    ASSERT_NE(outerLocals, nullptr);
    auto outer = vpi_scan(outerLocals);
    ASSERT_NE(outer, nullptr);
    EXPECT_EQ(vpi_scan(outerLocals), nullptr);
    EXPECT_STREQ(vpi_get_str(vpiName, outer), "i");
    EXPECT_EQ(vpi_compare_objects(outer, value), 0);
    integer.format = vpiIntVal;
    vpi_get_value(outer, &integer);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    EXPECT_EQ(integer.value.integer, phase / 2);
    EXPECT_EQ(vpi_release_handle(outer), 1);
    EXPECT_EQ(vpi_release_handle(outerFrame), 1);
  }
  state.values.push_back(value);
  state.scopes.push_back(scope);
  EXPECT_EQ(vpi_release_handle(imported), 1);
  EXPECT_EQ(vpi_release_handle(loopFrame), 1);
}

talos_rt_status inspectForScopeImport(talos_rt_context *, uint32_t,
                                        const talos_rt_import_input_v1 *,
                                        uint32_t inputCount,
                                        talos_rt_import_output_v1 *outputs,
                                        uint32_t outputCount, void *data) {
  if (inputCount || outputCount != 1 || !outputs || !outputs[0].value ||
      outputs[0].bit_width != 32 || outputs[0].limb_count != 1)
    return TALOS_RT_INVALID_ARGUMENT;
  auto &state = *static_cast<ForScopes *>(data);
  unsigned phase = state.calls;
  if (phase >= 10)
    return TALOS_RT_INVALID_ARGUMENT;
  if (state.observe)
    inspectForScope(state);
  else
    ++state.calls;
  outputs[0].value[0] = 1;
  return ::testing::Test::HasFatalFailure() ? TALOS_RT_FATAL : TALOS_RT_OK;
}

TEST(GeneratedDesignDatabase, AutomaticLocalIdentity) {
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
  std::optional<uint32_t> importID;
  design.walk([&](talos::sim::SimDPICallOp call) {
    EXPECT_EQ(call.getCIdentifier(), "automatic_local_identity_probe");
    importID = call.getImportId();
  });
  ASSERT_TRUE(importID);
  talos::SimulationBytecodeOptions options;
  bool observe = !std::getenv("TALOS_TEST_VPI_OFF");
  options.vpi = observe ? "full" : "off";
  auto encoded = talos::encodeSimulationDesign(design, options);
  ASSERT_TRUE(mlir::succeeded(encoded));
  using namespace talos::reflection;
  if (observe) {
    HeaderView header(encoded->designDatabase.data());
    SemanticDirectoryView directory(encoded->designDatabase.data() +
                                    header.getReserved());
    for (uint64_t index = 0; index != directory.getFixedPropertyCount();
         ++index) {
      FixedPropertyView property(encoded->designDatabase.data() +
                                 directory.getFixedPropertyOffset() +
                                 index * FixedPropertyLayout.size);
      EXPECT_NE(property.getSelector(), vpiLocalVarDecls);
    }
  }
  uint64_t checksum = 0;
  for (unsigned byte = 0; byte != 8; ++byte)
    checksum |= uint64_t{encoded->bytecode[32 + byte]} << (byte * 8);
  talos_rt_execution_descriptor_v1 execution{};
  execution.version = TALOS_RT_VERSION;
  execution.flags = encoded->executionFlags;
  execution.bytecode = encoded->bytecode.data();
  execution.bytecode_size = encoded->bytecode.size();
  execution.design_database = encoded->designDatabase.empty()
                                  ? nullptr
                                  : encoded->designDatabase.data();
  execution.design_database_size = encoded->designDatabase.size();
  execution.state_bit_count = encoded->stateBitCount;
  execution.checksum = checksum;
  const talos_rt_dpi_scope_v1 scopes[] = {
      {0, UINT64_MAX, "$root", 5, -15, -15, 0},
      {1, 0, "top", 3, -15, -15, 0},
      {2, 0, "$local", 6, -15, -15, 0}};
  execution.dpi_scopes = scopes;
  execution.dpi_scope_count = 3;
  execution.dpi_time_precision = -15;
  if (observe)
    ASSERT_EQ(talos_rt_v1_design_validate(&execution), TALOS_RT_OK);
  talos_rt_context *runtime = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &runtime),
            TALOS_RT_OK);
  ForScopes state;
  state.observe = observe;
  ASSERT_EQ(talos_rt_v1_context_register_import(
                runtime, *importID, inspectForScopeImport, &state),
            TALOS_RT_OK);
  if (observe) {
    ASSERT_EQ(talos_rt_v1_vpi_startup(runtime, nullptr, 0), TALOS_RT_OK);
    ASSERT_EQ(talos_rt_v1_vpi_end_compile(runtime), TALOS_RT_OK);
    ASSERT_EQ(talos_rt_v1_vpi_start_simulation(runtime), TALOS_RT_OK);
    auto top = vpi_handle_by_name(const_cast<char *>("top"), nullptr);
    ASSERT_NE(top, nullptr);
    auto processes = vpi_iterate(vpiProcess, top);
    ASSERT_NE(processes, nullptr);
    auto initial = vpi_scan(processes);
    ASSERT_NE(initial, nullptr);
    EXPECT_EQ(vpi_get(vpiType, initial), vpiInitial);
    EXPECT_EQ(vpi_scan(processes), nullptr);
    auto body = vpi_handle(vpiStmt, initial);
    ASSERT_NE(body, nullptr);
    EXPECT_EQ(vpi_get(vpiType, body), vpiBegin);
    auto loops = vpi_iterate(vpiStmt, body);
    ASSERT_NE(loops, nullptr);
    for (int i = 0; i != 4; ++i) {
      auto loop = vpi_scan(loops);
      ASSERT_NE(loop, nullptr);
      EXPECT_EQ(vpi_get(vpiType, loop), i < 2 ? vpiFor : vpiForeachStmt);
      if (i < 2)
        EXPECT_EQ(vpi_get(vpiLocalVarDecls, loop), 1);
      EXPECT_EQ(vpi_get_str(vpiName, loop), nullptr);
      EXPECT_EQ(vpi_release_handle(loop), 1);
    }
    EXPECT_EQ(vpi_scan(loops), nullptr);
    EXPECT_EQ(vpi_release_handle(body), 1);
    EXPECT_EQ(vpi_release_handle(initial), 1);
    EXPECT_EQ(vpi_release_handle(top), 1);
  }
  auto startup = std::find_if(
      encoded->functions.begin(), encoded->functions.end(),
      [](const auto &function) { return function.symbol == "__talos_root"; });
  ASSERT_NE(startup, encoded->functions.end());
  talos_rt_design_bytecode_entry_v1 entry{&execution, startup->index, 0};
  uint64_t scratchSize = 0, scratchAlignment = 0;
  ASSERT_EQ(talos_rt_validate_design_bytecode(entry, runtime, &scratchSize,
                                                &scratchAlignment),
            TALOS_RT_OK);
  ASSERT_LE(scratchAlignment, alignof(uint64_t));
  std::vector<uint64_t> scratch((scratchSize + 7) / 8);
  talos_rt_fragment_action_v1 action{};
  ASSERT_EQ(talos_rt_execute_design_bytecode(entry, runtime, scratch.data(),
                                               scratch.size() * 8, 0,
                                               scratchSize, 0, 0, &action),
            TALOS_RT_OK);
  ASSERT_EQ(action.kind, TALOS_RT_FRAGMENT_TERMINATE);
  ASSERT_EQ(talos_rt_v1_scheduler_run(runtime), TALOS_RT_OK);
  EXPECT_EQ(state.calls, 10u);
  for (auto value : state.values) {
    EXPECT_EQ(vpi_get(vpiType, value), vpiUndefined);
    EXPECT_NE(vpi_chk_error(nullptr), 0);
  }
  for (unsigned index = 0; index != state.scopes.size(); ++index) {
    auto scope = state.scopes[index];
    EXPECT_EQ(vpi_get(vpiType, scope), index < 6 ? vpiFor : vpiForeachStmt);
    if (index < 6)
      EXPECT_EQ(vpi_get(vpiLocalVarDecls, scope), 1);
    EXPECT_EQ(vpi_release_handle(scope), 1);
  }
  talos_rt_v1_context_destroy(runtime);
}
} // namespace
