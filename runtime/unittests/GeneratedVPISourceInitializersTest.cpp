//===- GeneratedVPISourceInitializersTest.cpp - Initializer probes --------===//

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
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <vector>

extern "C" void obelisk_rt_v1_dpi_import_bytecode_link_anchor(void);

namespace {
struct InitializerFrames {
  unsigned calls = 0;
  std::map<int, unsigned> sourceScopes;
  std::vector<vpiHandle> frames, declarations;
};

void inspectInitializer(InitializerFrames &state) {
  ++state.calls;
  auto foreign = vpi_handle(vpiFrame, nullptr);
  ASSERT_NE(foreign, nullptr);
  auto resolver = vpi_handle(vpiParent, foreign);
  ASSERT_NE(resolver, nullptr);
  EXPECT_EQ(vpi_handle(vpiParent, resolver), nullptr);
  for (auto frame : {foreign, resolver}) {
    EXPECT_EQ(vpi_get(vpiType, frame), vpiFrame);
    state.frames.push_back(frame);
    auto origin = vpi_handle(vpiOrigin, frame);
    ASSERT_NE(origin, nullptr);
    EXPECT_EQ(vpi_get(vpiType, origin), vpiFuncCall);
    state.declarations.push_back(origin);
    auto target = vpi_handle(vpiFunction, origin);
    ASSERT_NE(target, nullptr);
    state.declarations.push_back(target);
    if (frame == foreign) {
      EXPECT_STREQ(vpi_get_str(vpiFullName, target), "top.probe");
    } else {
      const char *name = vpi_get_str(vpiFullName, target);
      ASSERT_NE(name, nullptr);
      EXPECT_TRUE(std::string(name) == "top.compute" ||
                  std::string(name) == "top.make_event");
      auto scope = vpi_handle(vpiScope, origin);
      ASSERT_NE(scope, nullptr);
      int scopeKind = vpi_get(vpiType, scope);
      ++state.sourceScopes[scopeKind];
      state.declarations.push_back(scope);
      if (scopeKind == vpiModule || scopeKind == vpiFunction) {
        auto expected = vpi_handle_by_name(
            const_cast<char *>(scopeKind == vpiModule ? "top" : "top.owner"),
            nullptr);
        ASSERT_NE(expected, nullptr);
        EXPECT_EQ(vpi_compare_objects(scope, expected), 1);
        state.declarations.push_back(expected);
      }
    }
  }
}

obelisk_rt_status inspectInitializerImport(obelisk_rt_context *, uint32_t,
                                           const obelisk_rt_import_input_v1 *,
                                           uint32_t inputCount,
                                           obelisk_rt_import_output_v1 *outputs,
                                           uint32_t outputCount, void *data) {
  if (inputCount || outputCount != 1 || !outputs || !outputs[0].value ||
      outputs[0].bit_width != 32 || outputs[0].limb_count != 1)
    return OBELISK_RT_INVALID_ARGUMENT;
  inspectInitializer(*static_cast<InitializerFrames *>(data));
  outputs[0].value[0] = 23;
  return ::testing::Test::HasFatalFailure() ? OBELISK_RT_FATAL : OBELISK_RT_OK;
}

TEST(GeneratedDesignDatabase, SourceInitializerFrames) {
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
    EXPECT_EQ(call.getCIdentifier(), "initializer_probe");
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
  InitializerFrames state;
  ASSERT_EQ(obelisk_rt_v1_context_register_import(
                runtime, *importID, inspectInitializerImport, &state),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(runtime, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(runtime), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(runtime), OBELISK_RT_OK);
  unsigned bindings = 0;
  for (auto binding :
       design.getOps<obelisk::sim::SimVPIInitializerBindingOp>()) {
    ++bindings;
    unsigned calls = 0;
    for (auto call : design.getOps<obelisk::sim::SimVPICallDeclOp>())
      if (call.getCodeUnitId() == binding.getCodeUnitId()) {
        ++calls;
        EXPECT_FALSE(call.getStatementId());
        EXPECT_FALSE(call.getEnclosingStatementId());
      }
    EXPECT_EQ(calls, 1u);
  }
  EXPECT_EQ(bindings, 4u);
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
  EXPECT_EQ(state.sourceScopes[vpiModule], 2u);
  EXPECT_EQ(state.sourceScopes[vpiClassTypespec], 1u);
  EXPECT_EQ(state.sourceScopes[vpiFunction], 1u);
  EXPECT_EQ(vpi_handle(vpiFrame, nullptr), nullptr);
  for (const char *name : {"top.x", "top.Node::count", "top.owner.local"}) {
    auto variable = vpi_handle_by_name(const_cast<char *>(name), nullptr);
    ASSERT_NE(variable, nullptr) << name;
    s_vpi_value value{};
    value.format = vpiIntVal;
    vpi_get_value(variable, &value);
    EXPECT_EQ(vpi_chk_error(nullptr), 0);
    EXPECT_EQ(value.value.integer, 23);
    EXPECT_EQ(vpi_release_handle(variable), 1);
  }
  auto lookup = [&](const char *name) {
    obelisk_rt_design_cursor_v1 cursor{};
    EXPECT_EQ(obelisk_rt_cached_design_lookup(
                  runtime, reinterpret_cast<const uint8_t *>(name),
                  std::strlen(name), &cursor),
              OBELISK_RT_OK);
    return cursor;
  };
  uint64_t seed = UINT64_MAX, alias = UINT64_MAX;
  ASSERT_EQ(
      obelisk_rt_cached_vpi_event_binding(runtime, lookup("top.seed"), &seed),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_design_read(runtime, lookup("top.alias"), &alias,
                                      nullptr, 64),
            OBELISK_RT_OK);
  EXPECT_EQ(alias, seed);
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
} // namespace
