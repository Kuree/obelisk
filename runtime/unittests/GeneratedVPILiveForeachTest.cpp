//===- GeneratedVPILiveForeachTest.cpp - Live fixed foreach indices -------===//

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
#include "obelisk/Reflection/DesignReflection.h"
#include "sv_vpi_user.h"
#include "vpi_user.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <vector>

extern "C" void obelisk_rt_v1_dpi_import_bytecode_link_anchor(void);

namespace {
struct ForeachState {
  unsigned calls = 0;
  std::vector<vpiHandle> values, scopes, declarations, types;
  std::array<vpiHandle, 3> sourceScopes{};
  std::array<vpiHandle, 2> previous{};
};

int integerValue(vpiHandle value) {
  s_vpi_value result{};
  result.format = vpiIntVal;
  vpi_get_value(value, &result);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);
  return result.value.integer;
}

void inspectForeach(ForeachState &state) {
  unsigned phase = state.calls++;
  ASSERT_LT(phase, 20u);
  unsigned position = phase % 10;
  unsigned loop = position < 4 ? 0 : position < 6 ? 1 : 2;
  bool firstIteration = position == 0 || position == 4 || position == 6;
  unsigned count = loop == 0 ? 2 : 1;
  std::array<int, 2> expected =
      loop == 0 ? std::array<int, 2>{1 - int(position / 2), int(position % 2)}
                : std::array<int, 2>{
                      loop == 1 ? 5 - int(position) : 9 - int(position), 0};
  auto imported = vpi_handle(vpiFrame, nullptr);
  ASSERT_NE(imported, nullptr);
  auto loopFrame = vpi_handle(vpiParent, imported);
  ASSERT_NE(loopFrame, nullptr);
  auto origin = vpi_handle(vpiOrigin, loopFrame);
  ASSERT_NE(origin, nullptr);
  EXPECT_EQ(vpi_get(vpiType, origin), vpiForeachStmt);
  auto locals = vpi_iterate(vpiAutomatics, loopFrame);
  ASSERT_NE(locals, nullptr);
  if (firstIteration && phase) {
    EXPECT_EQ(vpi_get(vpiType, state.previous[0]), vpiUndefined);
    EXPECT_NE(vpi_chk_error(nullptr), 0);
  }
  for (unsigned index = 0; index != count; ++index) {
    auto value = vpi_scan(locals);
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(vpi_get(vpiType, value), vpiIntVar);
    EXPECT_STREQ(vpi_get_str(vpiName, value), index ? "j" : "i");
    EXPECT_EQ(vpi_get(vpiAutomatic, value), 1);
    EXPECT_EQ(vpi_get(vpiHasActual, value), 1);
    EXPECT_EQ(integerValue(value), expected[index]);
    auto scope = vpi_handle(vpiScope, value);
    ASSERT_NE(scope, nullptr);
    EXPECT_EQ(vpi_compare_objects(scope, origin), 1);
    if (loop != 1)
      EXPECT_STREQ(vpi_get_str(vpiName, scope), loop == 0 ? "sweep" : "bits");
    if (!firstIteration) {
      EXPECT_EQ(vpi_compare_objects(value, state.previous[index]), 1);
      EXPECT_EQ(integerValue(state.previous[index]), expected[index]);
    }
    if (firstIteration && !index) {
      if (phase < 10)
        state.sourceScopes[loop] = scope;
      else
        EXPECT_EQ(vpi_compare_objects(scope, state.sourceScopes[loop]), 1);
    }
    auto type = vpi_handle(vpiTypespec, value);
    ASSERT_NE(type, nullptr);
    EXPECT_EQ(vpi_get(vpiType, type), vpiIntTypespec);
    state.values.push_back(value);
    state.scopes.push_back(scope);
    state.types.push_back(type);
    state.previous[index] = value;
  }
  EXPECT_EQ(vpi_scan(locals), nullptr);
  if (firstIteration) {
    auto positions = vpi_iterate(vpiLoopVars, origin);
    ASSERT_NE(positions, nullptr);
    unsigned slots = loop == 0 ? 3 : 1;
    for (unsigned slot = 0; slot != slots; ++slot) {
      auto declaration = vpi_scan(positions);
      ASSERT_NE(declaration, nullptr);
      if (loop == 0 && slot == 1) {
        EXPECT_EQ(vpi_get(vpiType, declaration), vpiOperation);
        EXPECT_EQ(vpi_get(vpiOpType, declaration), vpiNullOp);
      } else {
        EXPECT_EQ(vpi_get(vpiType, declaration), vpiIntVar);
        EXPECT_EQ(vpi_get(vpiHasActual, declaration), 0);
        auto type = vpi_handle(vpiTypespec, declaration);
        ASSERT_NE(type, nullptr);
        EXPECT_EQ(vpi_get(vpiType, type), vpiIntTypespec);
        unsigned localIndex = loop == 0 && slot == 2 ? 1 : 0;
        EXPECT_EQ(
            vpi_compare_objects(
                type, state.types[state.types.size() - count + localIndex]),
            1);
        EXPECT_EQ(vpi_release_handle(type), 1);
      }
      state.declarations.push_back(declaration);
    }
    EXPECT_EQ(vpi_scan(positions), nullptr);
  }
  EXPECT_EQ(vpi_release_handle(origin), 1);
  EXPECT_EQ(vpi_release_handle(imported), 1);
  EXPECT_EQ(vpi_release_handle(loopFrame), 1);
}

obelisk_rt_status inspectForeachImport(obelisk_rt_context *, uint32_t,
                                       const obelisk_rt_import_input_v1 *,
                                       uint32_t inputCount,
                                       obelisk_rt_import_output_v1 *outputs,
                                       uint32_t outputCount, void *data) {
  if (inputCount || outputCount != 1 || !outputs || !outputs[0].value ||
      outputs[0].bit_width != 32 || outputs[0].limb_count != 1)
    return OBELISK_RT_INVALID_ARGUMENT;
  inspectForeach(*static_cast<ForeachState *>(data));
  outputs[0].value[0] = 1;
  return ::testing::Test::HasFatalFailure() ? OBELISK_RT_FATAL : OBELISK_RT_OK;
}

TEST(GeneratedDesignDatabase, LiveFixedArrayForeach) {
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
    EXPECT_EQ(call.getCIdentifier(), "foreach_probe");
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
  ForeachState state;
  ASSERT_EQ(obelisk_rt_v1_context_register_import(runtime, *importID,
                                                  inspectForeachImport, &state),
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
  EXPECT_EQ(state.calls, 20u);
  for (auto value : state.values) {
    EXPECT_EQ(vpi_get(vpiType, value), vpiUndefined);
    EXPECT_NE(vpi_chk_error(nullptr), 0);
  }
  for (auto scope : state.scopes) {
    EXPECT_EQ(vpi_get(vpiType, scope), vpiForeachStmt);
    EXPECT_EQ(vpi_release_handle(scope), 1);
  }
  for (auto declaration : state.declarations) {
    auto kind = vpi_get(vpiType, declaration);
    EXPECT_TRUE(kind == vpiIntVar || kind == vpiOperation);
    EXPECT_EQ(vpi_release_handle(declaration), 1);
  }
  for (auto type : state.types) {
    EXPECT_EQ(vpi_get(vpiType, type), vpiIntTypespec);
    EXPECT_EQ(vpi_release_handle(type), 1);
  }
  obelisk_rt_v1_context_destroy(runtime);
}
} // namespace
