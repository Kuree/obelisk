//===- GeneratedVPIForScopesTest.cpp - Exact declaration-loop scopes
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
struct ForScopes {
  unsigned calls = 0;
  std::vector<vpiHandle> values, scopes;
};

void inspectForScope(ForScopes &state) {
  unsigned phase = state.calls++;
  ASSERT_LT(phase, 4u);
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
  EXPECT_EQ(integer.value.integer, phase % 2);
  auto scope = vpi_handle(vpiScope, value);
  ASSERT_NE(scope, nullptr);
  EXPECT_EQ(vpi_get(vpiType, scope), vpiFor);
  EXPECT_EQ(vpi_get(vpiLocalVarDecls, scope), 1);
  if (phase < 2)
    EXPECT_STREQ(vpi_get_str(vpiName, scope), "sweep");
  if (phase % 2)
    EXPECT_EQ(vpi_compare_objects(value, state.values.back()), 1);
  if (phase == 2) {
    EXPECT_EQ(vpi_compare_objects(scope, state.scopes.front()), 0);
    EXPECT_EQ(vpi_get(vpiType, state.values.front()), vpiUndefined);
    EXPECT_NE(vpi_chk_error(nullptr), 0);
  }
  state.values.push_back(value);
  state.scopes.push_back(scope);
  EXPECT_EQ(vpi_release_handle(imported), 1);
  EXPECT_EQ(vpi_release_handle(loopFrame), 1);
}

obelisk_rt_status inspectForScopeImport(obelisk_rt_context *, uint32_t,
                                        const obelisk_rt_import_input_v1 *,
                                        uint32_t inputCount,
                                        obelisk_rt_import_output_v1 *outputs,
                                        uint32_t outputCount, void *data) {
  if (inputCount || outputCount != 1 || !outputs || !outputs[0].value ||
      outputs[0].bit_width != 32 || outputs[0].limb_count != 1)
    return OBELISK_RT_INVALID_ARGUMENT;
  inspectForScope(*static_cast<ForScopes *>(data));
  outputs[0].value[0] = 1;
  return ::testing::Test::HasFatalFailure() ? OBELISK_RT_FATAL : OBELISK_RT_OK;
}

TEST(GeneratedDesignDatabase, ForDeclarationScopes) {
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
    EXPECT_EQ(call.getCIdentifier(), "for_scope_probe");
    importID = call.getImportId();
  });
  ASSERT_TRUE(importID);
  obelisk::SimulationBytecodeOptions options;
  options.vpi = "full";
  auto encoded = obelisk::encodeSimulationDesign(design, options);
  ASSERT_TRUE(mlir::succeeded(encoded));
  using namespace obelisk::reflection;
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
  ForScopes state;
  ASSERT_EQ(obelisk_rt_v1_context_register_import(
                runtime, *importID, inspectForScopeImport, &state),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(runtime, nullptr, 0), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(runtime), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(runtime), OBELISK_RT_OK);
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
  for (int i = 0; i != 3; ++i) {
    auto loop = vpi_scan(loops);
    ASSERT_NE(loop, nullptr);
    EXPECT_EQ(vpi_get(vpiType, loop), vpiFor);
    EXPECT_EQ(vpi_get(vpiLocalVarDecls, loop), i != 2);
    if (i == 0) {
      EXPECT_STREQ(vpi_get_str(vpiName, loop), "sweep");
      EXPECT_STREQ(vpi_get_str(vpiFullName, loop), "top.sweep");
    }
    EXPECT_EQ(vpi_release_handle(loop), 1);
  }
  EXPECT_EQ(vpi_scan(loops), nullptr);
  EXPECT_EQ(vpi_release_handle(body), 1);
  EXPECT_EQ(vpi_release_handle(initial), 1);
  EXPECT_EQ(vpi_release_handle(top), 1);
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
  for (auto value : state.values) {
    EXPECT_EQ(vpi_get(vpiType, value), vpiUndefined);
    EXPECT_NE(vpi_chk_error(nullptr), 0);
  }
  for (auto scope : state.scopes) {
    EXPECT_EQ(vpi_get(vpiType, scope), vpiFor);
    EXPECT_EQ(vpi_get(vpiLocalVarDecls, scope), 1);
    EXPECT_EQ(vpi_release_handle(scope), 1);
  }
  obelisk_rt_v1_context_destroy(runtime);
}
} // namespace
