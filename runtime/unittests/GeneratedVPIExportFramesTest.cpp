//===- GeneratedVPIExportFramesTest.cpp - DPI scope-origin frames
//----===//
#include "../lib/RuntimeInternal.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Parser/Parser.h"
#include "obelisk/Conversion/SimulationToBytecode.h"
#include "obelisk/Dialect/Simulation/SimulationOps.h"
#include "sv_vpi_user.h"
#include "svdpi.h"
#include "vpi_user.h"
#include "gtest/gtest.h"
#include <cstdlib>
#include <memory>

namespace {
enum class Exit { Return, PreparedFailure, EnteredFailure };
struct TransportProbe {
  Exit exit;
  unsigned imports = 0, exports = 0, nesting = 0;
  bool ran = false;
  vpiHandle parent = nullptr, thread = nullptr, source = nullptr;
};
thread_local TransportProbe *activeProbe = nullptr;
const obelisk_rt_import_site_v1 importSite{OBELISK_RT_VERSION,
                                           OBELISK_RT_IMPORT_CONTEXT,
                                           123,
                                           0,
                                           1,
                                           "export-frames.mlir",
                                           18,
                                           1,
                                           1,
                                           7};
void invokeImport(obelisk_rt_context *context, TransportProbe &probe,
                  uint64_t call);
void inspectExport(obelisk_rt_context *context, uint64_t unit,
                   TransportProbe &probe) {
  ++probe.exports;
  uint64_t ticket = UINT64_MAX, activation = UINT64_MAX;
  EXPECT_EQ(obelisk_rt_v1_vpi_export_prepare(context, 44, &ticket),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(ticket, 0u);
  ASSERT_EQ(obelisk_rt_v1_vpi_export_prepare(context, unit, &ticket),
            OBELISK_RT_OK);
  uint64_t duplicate = UINT64_MAX;
  EXPECT_EQ(obelisk_rt_v1_vpi_export_prepare(context, unit, &duplicate),
            OBELISK_RT_INVALID_LIFECYCLE);
  EXPECT_EQ(duplicate, 0u);
  if (probe.exit == Exit::PreparedFailure)
    return;
  EXPECT_EQ(
      obelisk_rt_v1_vpi_frame_enter_prepared(context, unit, 1, &activation),
      OBELISK_RT_INVALID_LIFECYCLE);
  EXPECT_EQ(activation, 0u);
  EXPECT_EQ(obelisk_rt_v1_vpi_frame_enter_prepared(context, 45, 0, &activation),
            OBELISK_RT_INVALID_ARGUMENT);
  ASSERT_EQ(
      obelisk_rt_v1_vpi_frame_enter_prepared(context, unit, 0, &activation),
      OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_vpi_frame_enter(context, unit,
                                          OBELISK_RT_VPI_FRAME_DPI_EXPORT, unit,
                                          &duplicate),
            OBELISK_RT_INVALID_FRAME);
  auto frame = vpi_handle(vpiFrame, nullptr);
  ASSERT_NE(frame, nullptr);
  auto origin = vpi_handle(vpiOrigin, frame);
  ASSERT_NE(origin, nullptr);
  EXPECT_EQ(vpi_get(vpiType, origin), vpiFunction);
  EXPECT_STREQ(vpi_get_str(vpiFullName, origin),
               unit == 46 ? "top.exported" : "top.nested");
  auto parent = vpi_handle(vpiParent, frame);
  auto thread = vpi_handle(vpiThread, frame);
  ASSERT_NE(parent, nullptr);
  ASSERT_NE(thread, nullptr);
  EXPECT_EQ(vpi_compare_objects(parent, probe.parent), 1);
  EXPECT_EQ(vpi_compare_objects(thread, probe.thread), 1);
  EXPECT_EQ(vpi_get(vpiActive, parent), 0);
  EXPECT_EQ(vpi_get(vpiActive, frame), 1);
  EXPECT_EQ(vpi_release_handle(parent), 1);
  EXPECT_EQ(vpi_release_handle(thread), 1);
  EXPECT_EQ(vpi_release_handle(frame), 1);
  if (probe.exit == Exit::EnteredFailure) {
    probe.source = origin;
    return;
  }
  if (unit == 46) {
    ASSERT_EQ(obelisk_rt_v1_vpi_frame_checkpoint(context, 12), OBELISK_RT_OK);
    invokeImport(context, probe, 21);
  }
  ASSERT_EQ(obelisk_rt_v1_vpi_frame_leave(context, activation), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_call_finish(context, ticket), OBELISK_RT_OK);
  EXPECT_EQ(vpi_get(vpiType, origin), vpiFunction);
  EXPECT_EQ(vpi_release_handle(origin), 1);
}
obelisk_rt_status exported(obelisk_rt_context *context,
                           const obelisk_rt_import_input_v1 *, uint32_t,
                           obelisk_rt_import_output_v1 *, uint32_t) {
  inspectExport(context, 46, *activeProbe);
  return activeProbe->exit == Exit::Return ? OBELISK_RT_OK : OBELISK_RT_FATAL;
}
obelisk_rt_status nested(obelisk_rt_context *context,
                         const obelisk_rt_import_input_v1 *, uint32_t,
                         obelisk_rt_import_output_v1 *, uint32_t) {
  inspectExport(context, 47, *activeProbe);
  return OBELISK_RT_OK;
}
obelisk_rt_status wrongScope(obelisk_rt_context *context,
                             const obelisk_rt_import_input_v1 *, uint32_t,
                             obelisk_rt_import_output_v1 *, uint32_t) {
  uint64_t ticket = UINT64_MAX;
  EXPECT_EQ(obelisk_rt_v1_vpi_export_prepare(context, 46, &ticket),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(ticket, 0u);
  return OBELISK_RT_OK;
}
obelisk_rt_status importCallback(obelisk_rt_context *, uint32_t id,
                                 const obelisk_rt_import_input_v1 *, uint32_t,
                                 obelisk_rt_import_output_v1 *, uint32_t,
                                 void *userData) {
  auto &probe = *static_cast<TransportProbe *>(userData);
  EXPECT_EQ(id, 123u);
  ++probe.imports;
  unsigned exportID = probe.nesting++ ? 2 : 1;
  auto status = obelisk_rt_v1_export_call(exportID, 7, nullptr, 0, nullptr, 0);
  --probe.nesting;
  if (probe.exit == Exit::Return && exportID == 1) {
    auto previous = svGetScope();
    auto other = svGetScopeFromName("other");
    EXPECT_NE(other, nullptr);
    svSetScope(other);
    EXPECT_EQ(obelisk_rt_v1_export_call(3, 7, nullptr, 0, nullptr, 0),
              OBELISK_RT_OK);
    svSetScope(previous);
  }
  return status;
}
void invokeImport(obelisk_rt_context *context, TransportProbe &probe,
                  uint64_t call) {
  uint64_t ticket = 0, activation = 0;
  ASSERT_EQ(obelisk_rt_v1_vpi_call_prepare(context, call, &ticket),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_frame_enter_prepared(context, 45, 0, &activation),
            OBELISK_RT_OK);
  auto previous = probe.parent;
  probe.parent = vpi_handle(vpiFrame, nullptr);
  ASSERT_NE(probe.parent, nullptr);
  EXPECT_EQ(
      obelisk_rt_v1_import_call(context, &importSite, nullptr, 0, nullptr, 0),
      probe.exit == Exit::Return ? OBELISK_RT_OK : OBELISK_RT_FATAL);
  auto current = vpi_handle(vpiFrame, nullptr);
  ASSERT_NE(current, nullptr);
  EXPECT_EQ(vpi_compare_objects(current, probe.parent), 1);
  EXPECT_EQ(vpi_get(vpiActive, current), 1);
  EXPECT_EQ(vpi_release_handle(current), 1);
  EXPECT_EQ(vpi_release_handle(probe.parent), 1);
  probe.parent = previous;
  ASSERT_EQ(obelisk_rt_v1_vpi_frame_leave(context, activation), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_call_finish(context, ticket), OBELISK_RT_OK);
}
void inspectTransport(obelisk_rt_context *context, TransportProbe &probe) {
  probe.ran = true;
  ASSERT_NE(obelisk_rt_v1_process_current(context), 0u);
  uint64_t root = 0, rejected = UINT64_MAX;
  EXPECT_EQ(obelisk_rt_v1_vpi_export_prepare(context, 46, &rejected),
            OBELISK_RT_INVALID_LIFECYCLE);
  EXPECT_EQ(rejected, 0u);
  ASSERT_EQ(obelisk_rt_v1_vpi_frame_enter(
                context, 44, OBELISK_RT_VPI_FRAME_CODE_UNIT_SCOPE, 44, &root),
            OBELISK_RT_OK);
  auto frame = vpi_handle(vpiFrame, nullptr);
  ASSERT_NE(frame, nullptr);
  probe.thread = vpi_handle(vpiThread, frame);
  ASSERT_NE(probe.thread, nullptr);
  EXPECT_EQ(vpi_release_handle(frame), 1);
  ASSERT_EQ(obelisk_rt_v1_vpi_frame_checkpoint(context, 11), OBELISK_RT_OK);
  activeProbe = &probe;
  invokeImport(context, probe, 20);
  activeProbe = nullptr;
  EXPECT_EQ(obelisk_rt_v1_vpi_export_prepare(context, 46, &rejected),
            OBELISK_RT_INVALID_LIFECYCLE);
  uint64_t depth = 0;
  ASSERT_EQ(obelisk_rt_v1_vpi_frame_depth(context, &depth), OBELISK_RT_OK);
  EXPECT_EQ(depth, 1u);
  EXPECT_EQ(vpi_release_handle(probe.thread), 1);
  ASSERT_EQ(obelisk_rt_v1_vpi_frame_leave(context, root), OBELISK_RT_OK);
}
obelisk_rt_status transportEntry(obelisk_rt_process_instance_v1 *instance) {
  instance->native_handle = instance;
  auto *probe = *static_cast<TransportProbe **>(instance->frame);
  inspectTransport(instance->context, *probe);
  *instance->action = {
      OBELISK_RT_FRAGMENT_TERMINATE, OBELISK_RT_SUSPEND_NONE, 0, 0, 0, 0};
  return ::testing::Test::HasFatalFailure() ? OBELISK_RT_FATAL : OBELISK_RT_OK;
}
void testTransport(Exit exit) {
  const char *input = std::getenv("OBELISK_TEST_INPUT");
  ASSERT_NE(input, nullptr);
  mlir::DialectRegistry registry;
  registry.insert<mlir::arith::ArithDialect, mlir::cf::ControlFlowDialect,
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
  struct Execution {
    obelisk_rt_execution_descriptor_v1 design;
    obelisk_rt_execution_extension_v1 extension{};
  } aggregate;
  auto &execution = aggregate.design;
  execution = {OBELISK_RT_VERSION,
               encoded->executionFlags,
               0,
               encoded->bytecode.data(),
               encoded->bytecode.size(),
               encoded->designDatabase.data(),
               encoded->designDatabase.size(),
               encoded->stateBitCount,
               checksum,
               nullptr,
               0,
               0,
               0,
               nullptr,
               0,
               nullptr,
               0};
  execution.flags |= OBELISK_RT_EXECUTION_DPI_EXPORTS;
  execution.reserved = offsetof(Execution, extension);
  const obelisk_rt_dpi_scope_v1 scopes[] = {
      {0, UINT64_MAX, "$root", 5, -9, -12, 0},
      {1, 0, "top", 3, -9, -12, 0},
      {2, 0, "other", 5, -9, -12, 0}};
  execution.dpi_scopes = scopes;
  execution.dpi_scope_count = 3;
  execution.dpi_time_precision = -12;
  const obelisk_rt_export_descriptor_v1 exports[] = {
      {1, OBELISK_RT_EXPORT_HAS_NATIVE, 1, 46, 7, 0, 0,
       OBELISK_RT_EXPORT_NO_BYTECODE, 0, exported, 0},
      {2, OBELISK_RT_EXPORT_HAS_NATIVE, 1, 47, 7, 0, 0,
       OBELISK_RT_EXPORT_NO_BYTECODE, 0, nested, 0},
      {3, OBELISK_RT_EXPORT_HAS_NATIVE, 2, 46, 7, 0, 0,
       OBELISK_RT_EXPORT_NO_BYTECODE, 0, wrongScope, 0}};
  aggregate.extension.version = OBELISK_RT_EXECUTION_EXTENSION_VERSION;
  aggregate.extension.size = sizeof(aggregate.extension);
  aggregate.extension.exports = exports;
  aggregate.extension.export_count = 3;
  ASSERT_EQ(obelisk_rt_v1_design_validate(&execution), OBELISK_RT_OK);
  obelisk_rt_context *rawContext = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create_for_design(&execution, &rawContext),
            OBELISK_RT_OK);
  std::unique_ptr<obelisk_rt_context, decltype(&obelisk_rt_v1_context_destroy)>
      runtime(rawContext, obelisk_rt_v1_context_destroy);
  ASSERT_EQ(obelisk_rt_v1_vpi_startup(runtime.get(), nullptr, 0),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_end_compile(runtime.get()), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_vpi_start_simulation(runtime.get()), OBELISK_RT_OK);
  TransportProbe probe{exit};
  ASSERT_EQ(obelisk_rt_v1_context_register_import(rawContext, 123,
                                                  importCallback, &probe),
            OBELISK_RT_OK);
  const obelisk_rt_frame_field_v1 field{
      OBELISK_RT_FRAME_CAPTURE,  0, 0, sizeof(TransportProbe *),
      alignof(TransportProbe *), 0};
  const uint32_t continuation = 0;
  obelisk_rt_frame_layout_v1 layout{OBELISK_RT_VERSION,
                                    0,
                                    sizeof(TransportProbe *),
                                    alignof(TransportProbe *),
                                    &field,
                                    1,
                                    1,
                                    &continuation,
                                    0};
  uint64_t hash = UINT64_C(14695981039346656037);
  auto append = [&](const auto &value) {
    const auto *bytes = reinterpret_cast<const uint8_t *>(&value);
    for (size_t i = 0; i != sizeof(value); ++i) {
      hash ^= bytes[i];
      hash *= UINT64_C(1099511628211);
    }
  };
  append(layout.version);
  append(layout.flags);
  append(layout.frame_size);
  append(layout.frame_alignment);
  append(layout.field_count);
  append(layout.continuation_count);
  append(field);
  append(continuation);
  layout.checksum = hash;
  const obelisk_rt_process_descriptor_v1 descriptor{
      {OBELISK_RT_DESCRIPTOR_PROCESS, 0, 44},
      OBELISK_RT_VERSION,
      0,
      OBELISK_RT_TIER_MASK_NATIVE,
      0,
      &layout,
      [](uint64_t *size, uint64_t *alignment) {
        *size = 0;
        *alignment = 1;
        return OBELISK_RT_OK;
      },
      transportEntry,
      [](obelisk_rt_process_instance_v1 *p) { p->native_handle = nullptr; },
      nullptr,
      &execution,
      nullptr};
  obelisk_rt_process_instance_v1 *instance = nullptr;
  ASSERT_EQ(obelisk_rt_v1_process_instance_create(&descriptor, &instance),
            OBELISK_RT_OK);
  *static_cast<TransportProbe **>(instance->frame) = &probe;
  ASSERT_EQ(obelisk_rt_v1_scheduler_add(runtime.get(), instance, 0),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_scheduler_run(runtime.get()), OBELISK_RT_OK);
  EXPECT_TRUE(probe.ran);
  EXPECT_EQ(probe.imports, exit == Exit::Return ? 2u : 1u);
  EXPECT_EQ(probe.exports, probe.imports);
  if (probe.source) {
    EXPECT_EQ(vpi_get(vpiType, probe.source), vpiFunction);
    EXPECT_EQ(vpi_release_handle(probe.source), 1);
  }
}
TEST(GeneratedDesignDatabase, DPIExportFrames) {
  for (auto exit :
       {Exit::Return, Exit::PreparedFailure, Exit::EnteredFailure}) {
    SCOPED_TRACE(static_cast<int>(exit));
    testTransport(exit);
  }
}
} // namespace
