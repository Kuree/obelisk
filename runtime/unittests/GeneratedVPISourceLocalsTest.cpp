//===- GeneratedVPISourceLocalsTest.cpp - Source local bindings
//------------===//

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
#include <vector>

extern "C" void obelisk_rt_v1_dpi_import_bytecode_link_anchor(void);

namespace {
struct SourceFrames {
  unsigned calls = 0;
  std::vector<vpiHandle> values, scopes, types, frames;
  vpiHandle previousValue = nullptr;
};

int integer(vpiHandle object) {
  s_vpi_value value{};
  value.format = vpiIntVal;
  vpi_get_value(object, &value);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);
  return value.value.integer;
}

void inspectSourceFrames(SourceFrames &state) {
  const unsigned phase = state.calls++;
  ASSERT_LT(phase, 4u);
  const int seed = phase < 2 ? 23 : 47;
  const int expected = seed + (phase % 2 ? 11 : 0);
  auto imported = vpi_handle(vpiFrame, nullptr);
  ASSERT_NE(imported, nullptr);
  auto inner = vpi_handle(vpiParent, imported);
  ASSERT_NE(inner, nullptr);
  auto outer = vpi_handle(vpiParent, inner);
  ASSERT_NE(outer, nullptr);
  auto function = vpi_handle(vpiParent, outer);
  ASSERT_NE(function, nullptr);
  state.frames.insert(state.frames.end(), {imported, inner, outer, function});
  auto locals = [&](vpiHandle frame,
                    std::initializer_list<const char *> names) {
    std::vector<vpiHandle> result;
    auto iterator = vpi_iterate(vpiAutomatics, frame);
    EXPECT_NE(iterator, nullptr);
    if (!iterator)
      return result;
    auto use = vpi_handle(vpiUse, iterator);
    EXPECT_EQ(vpi_compare_objects(use, frame), 1);
    EXPECT_EQ(vpi_release_handle(use), 1);
    for (const char *name : names) {
      auto value = vpi_scan(iterator);
      EXPECT_NE(value, nullptr);
      if (!value)
        break;
      EXPECT_STREQ(vpi_get_str(vpiName, value), name);
      EXPECT_EQ(vpi_get(vpiAutomatic, value), 1);
      EXPECT_EQ(vpi_get(vpiHasActual, value), 1);
      EXPECT_EQ(vpi_chk_error(nullptr), 0);
      result.push_back(value);
      state.values.push_back(value);
      auto scope = vpi_handle(vpiScope, value);
      auto type = vpi_handle(vpiTypespec, value);
      EXPECT_NE(scope, nullptr);
      EXPECT_NE(type, nullptr);
      state.scopes.push_back(scope);
      state.types.push_back(type);
    }
    EXPECT_EQ(vpi_scan(iterator), nullptr);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    return result;
  };
  auto innerValues = locals(inner, {"value", "anonymous"});
  auto outerValues = locals(outer, {"state"});
  auto formals = locals(function, {"seed"});
  ASSERT_EQ(innerValues.size(), 2u);
  ASSERT_EQ(outerValues.size(), 1u);
  ASSERT_EQ(formals.size(), 1u);
  EXPECT_EQ(integer(innerValues[0]), expected);
  EXPECT_EQ(integer(innerValues[1]), 2);
  EXPECT_EQ(integer(formals[0]), seed);
  s_vpi_value stateValue{};
  stateValue.format = vpiDecStrVal;
  vpi_get_value(outerValues[0], &stateValue);
  EXPECT_EQ(vpi_chk_error(nullptr), 0);
  ASSERT_NE(stateValue.value.str, nullptr);
  EXPECT_STREQ(stateValue.value.str, "-2");
  const size_t first = state.types.size() - 4;
  EXPECT_EQ(vpi_get(vpiType, state.types[first]), vpiIntTypespec);
  EXPECT_STREQ(vpi_get_str(vpiName, state.types[first]), "same_t");
  EXPECT_EQ(vpi_get(vpiType, state.types[first + 1]), vpiEnumTypespec);
  EXPECT_EQ(vpi_get(vpiType, state.types[first + 2]), vpiEnumTypespec);
  EXPECT_STREQ(vpi_get_str(vpiName, state.types[first + 2]), "same_t");
  EXPECT_EQ(vpi_get(vpiType, state.scopes[first]), vpiBegin);
  EXPECT_EQ(vpi_get(vpiType, state.scopes[first + 2]), vpiBegin);
  EXPECT_EQ(vpi_get(vpiType, state.scopes[first + 3]), vpiFunction);
  EXPECT_EQ(vpi_compare_objects(state.scopes[first], state.scopes[first + 2]),
            0);
  if (phase % 2) {
    EXPECT_EQ(vpi_compare_objects(innerValues[0], state.previousValue), 1);
    EXPECT_EQ(integer(state.previousValue), expected);
  } else {
    if (state.previousValue) {
      EXPECT_EQ(vpi_get(vpiType, state.previousValue), vpiUndefined);
      EXPECT_NE(vpi_chk_error(nullptr), 0);
    }
    state.previousValue = innerValues[0];
  }
  s_vpi_value write{};
  write.format = vpiIntVal;
  write.value.integer = 99;
  EXPECT_EQ(vpi_put_value(innerValues[0], &write, nullptr, vpiNoDelay),
            nullptr);
  s_vpi_error_info error{};
  EXPECT_NE(vpi_chk_error(&error), 0);
  EXPECT_EQ(error.level, vpiNotice);
  EXPECT_EQ(integer(innerValues[0]), expected);
}

obelisk_rt_status inspectSourceFrameImport(obelisk_rt_context *, uint32_t,
                                           const obelisk_rt_import_input_v1 *,
                                           uint32_t inputCount,
                                           obelisk_rt_import_output_v1 *outputs,
                                           uint32_t outputCount, void *data) {
  if (inputCount || outputCount != 1 || !outputs || !outputs[0].value ||
      outputs[0].bit_width != 32 || outputs[0].limb_count != 1)
    return OBELISK_RT_INVALID_ARGUMENT;
  inspectSourceFrames(*static_cast<SourceFrames *>(data));
  outputs[0].value[0] = 1;
  return ::testing::Test::HasFatalFailure() ? OBELISK_RT_FATAL : OBELISK_RT_OK;
}

TEST(GeneratedDesignDatabase, SourceAutomaticLocals) {
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
    EXPECT_EQ(call.getCIdentifier(), "frame_local_probe");
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
  EXPECT_EQ(state.calls, 4u);
  for (auto frame : state.frames) {
    EXPECT_EQ(vpi_get(vpiType, frame), vpiUndefined);
    EXPECT_NE(vpi_chk_error(nullptr), 0);
  }
  for (auto value : state.values) {
    EXPECT_EQ(vpi_get(vpiType, value), vpiUndefined);
    EXPECT_NE(vpi_chk_error(nullptr), 0);
  }
  for (auto declaration : state.scopes) {
    EXPECT_NE(vpi_get(vpiType, declaration), vpiUndefined);
    EXPECT_EQ(vpi_release_handle(declaration), 1);
  }
  for (auto type : state.types) {
    EXPECT_NE(vpi_get(vpiType, type), vpiUndefined);
    EXPECT_EQ(vpi_release_handle(type), 1);
  }
  obelisk_rt_v1_context_destroy(runtime);
}
} // namespace
