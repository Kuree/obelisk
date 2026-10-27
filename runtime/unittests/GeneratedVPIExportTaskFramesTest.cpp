//===- GeneratedVPIExportTaskFramesTest.cpp - DPI scope-origin frames
//----===//
#include "../lib/ProcessValidation.h"
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
#include "svdpi.h"
#include "vpi_user.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <memory>

extern "C" void talos_rt_v1_dpi_import_bytecode_link_anchor(void);
namespace {
struct NativeDescriptor;
struct Probe {
  bool bytecode, implicitReturn, escapedChild, killExport, nested;
  unsigned phases = 0, unrelated = 0, escaped = 0;
  uint64_t activation = 0, sourceToken = 0, executor = 0, nestedTicket = 0;
  const NativeDescriptor *task = nullptr, *child = nullptr, *callee = nullptr;
  vpiHandle thread = nullptr, parent = nullptr, frame = nullptr,
            origin = nullptr;
};
thread_local Probe *activeProbe = nullptr;
struct NativeFrame {
  talos_rt_wait_record_v1 wait;
  talos_rt_wait_entry_v1 event;
  Probe *probe;
};
talos_rt_status nativeEntry(talos_rt_process_instance_v1 *instance);
struct NativeDescriptor {
  std::array<talos_rt_frame_field_v1, 2> fields{
      {{TALOS_RT_FRAME_WAIT, 0, 0,
        sizeof(talos_rt_wait_record_v1) + sizeof(talos_rt_wait_entry_v1),
        alignof(talos_rt_wait_record_v1), 0},
       {TALOS_RT_FRAME_CAPTURE, 0, offsetof(NativeFrame, probe),
        sizeof(Probe *), alignof(Probe *), 0}}};
  std::array<uint32_t, 3> continuations{{0, 1, 2}};
  talos_rt_frame_layout_v1 layout{};
  talos_rt_process_descriptor_v1 descriptor{};
  NativeDescriptor(uint64_t id,
                   const talos_rt_execution_descriptor_v1 *execution) {
    layout = {TALOS_RT_VERSION,
              0,
              sizeof(NativeFrame),
              alignof(NativeFrame),
              fields.data(),
              uint32_t(fields.size()),
              uint32_t(continuations.size()),
              continuations.data(),
              0};
    layout.checksum = talos::process::layoutChecksum(layout);
    descriptor = {
        {TALOS_RT_DESCRIPTOR_PROCESS, 0, id},
        TALOS_RT_VERSION,
        0,
        TALOS_RT_TIER_MASK_NATIVE,
        0,
        &layout,
        [](uint64_t *size, uint64_t *alignment) {
          *size = 0;
          *alignment = 1;
          return TALOS_RT_OK;
        },
        nativeEntry,
        [](talos_rt_process_instance_v1 *p) { p->native_handle = nullptr; },
        nullptr,
        execution,
        nullptr};
  }
  talos_rt_process_instance_v1 *create(Probe &probe) const {
    talos_rt_process_instance_v1 *instance = nullptr;
    EXPECT_EQ(talos_rt_v1_process_instance_create(&descriptor, &instance),
              TALOS_RT_OK);
    if (instance)
      static_cast<NativeFrame *>(instance->frame)->probe = &probe;
    return instance;
  }
};
void inspectTask(talos_rt_context *context, Probe &probe, unsigned phase,
                 uint64_t activation) {
  EXPECT_EQ(phase, probe.phases++);
  auto frame = vpi_handle(vpiFrame, nullptr);
  ASSERT_NE(frame, nullptr);
  auto thread = vpi_handle(vpiThread, frame);
  auto parent = vpi_handle(vpiParent, frame);
  ASSERT_NE(thread, nullptr);
  ASSERT_NE(parent, nullptr);
  EXPECT_EQ(vpi_compare_objects(thread, probe.thread), 1);
  EXPECT_EQ(vpi_compare_objects(parent, probe.parent), 1);
  EXPECT_EQ(vpi_get(vpiActive, thread), 1);
  EXPECT_EQ(vpi_get(vpiActive, frame), 1);
  EXPECT_EQ(vpi_get(vpiActive, parent), 0);
  EXPECT_EQ(vpi_release_handle(thread), 1);
  EXPECT_EQ(vpi_release_handle(parent), 1);
  if (phase == 0) {
    probe.executor = talos_rt_v1_process_current(context);
    EXPECT_NE(probe.executor, probe.sourceToken);
    probe.activation = activation;
    probe.frame = frame;
    probe.origin = vpi_handle(vpiOrigin, frame);
    ASSERT_NE(probe.origin, nullptr);
    EXPECT_EQ(vpi_get(vpiType, probe.origin), vpiTask);
    EXPECT_STREQ(vpi_get_str(vpiFullName, probe.origin), "top.exported");
  } else {
    EXPECT_EQ(vpi_compare_objects(frame, probe.frame), 1);
    EXPECT_EQ(activation, probe.activation);
    EXPECT_EQ(vpi_release_handle(frame), 1);
  }
}
void inspectUnrelated(talos_rt_context *context, Probe &probe) {
  ++probe.unrelated;
  EXPECT_EQ(probe.phases, 1u);
  EXPECT_EQ(vpi_handle(vpiFrame, nullptr), nullptr);
  EXPECT_EQ(vpi_get(vpiActive, probe.thread), 0);
  EXPECT_EQ(vpi_get(vpiActive, probe.frame), 0);
  auto suspended = vpi_handle(vpiFrame, probe.thread);
  ASSERT_NE(suspended, nullptr);
  EXPECT_EQ(vpi_compare_objects(suspended, probe.frame), 1);
  EXPECT_EQ(vpi_release_handle(suspended), 1);
  auto children = vpi_iterate(vpiThread, probe.thread);
  if (probe.escapedChild) {
    ASSERT_NE(children, nullptr);
    auto child = vpi_scan(children);
    ASSERT_NE(child, nullptr);
    auto parent = vpi_handle(vpiParent, child);
    ASSERT_NE(parent, nullptr);
    EXPECT_EQ(vpi_compare_objects(parent, probe.thread), 1);
    EXPECT_EQ(vpi_release_handle(parent), 1);
    EXPECT_EQ(vpi_release_handle(child), 1);
    EXPECT_EQ(vpi_scan(children), nullptr);
  } else {
    EXPECT_EQ(children, nullptr);
  }
  auto event = vpi_handle_by_name(const_cast<char *>("top.ready"), nullptr);
  ASSERT_NE(event, nullptr);
  auto waiting = vpi_iterate(vpiWaitingProcesses, event);
  ASSERT_NE(waiting, nullptr);
  auto thread = vpi_scan(waiting);
  ASSERT_NE(thread, nullptr);
  EXPECT_EQ(vpi_compare_objects(thread, probe.thread), 1);
  EXPECT_EQ(vpi_scan(waiting), nullptr);
  EXPECT_EQ(vpi_release_handle(thread), 1);
  EXPECT_EQ(vpi_release_handle(event), 1);
}
void inspectOuter(talos_rt_context *context, Probe &probe) {
  probe.sourceToken = talos_rt_v1_process_current(context);
  uint64_t root = 0, ticket = 0, imported = 0;
  ASSERT_EQ(talos_rt_v1_vpi_frame_enter(
                context, 44, TALOS_RT_VPI_FRAME_CODE_UNIT_SCOPE, 44, &root),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_vpi_call_prepare(context, 20, &ticket),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_vpi_frame_enter_prepared(context, 45, 0, &imported),
            TALOS_RT_OK);
  probe.parent = vpi_handle(vpiFrame, nullptr);
  ASSERT_NE(probe.parent, nullptr);
  probe.thread = vpi_handle(vpiThread, probe.parent);
  ASSERT_NE(probe.thread, nullptr);
  const talos_rt_import_site_v1 site{TALOS_RT_VERSION,
                                       TALOS_RT_IMPORT_CONTEXT |
                                           TALOS_RT_IMPORT_TASK,
                                       123,
                                       0,
                                       1,
                                       "export-task.mlir",
                                       16,
                                       1,
                                       1,
                                       7};
  activeProbe = &probe;
  ASSERT_EQ(talos_rt_v1_import_call(context, &site, nullptr, 0, nullptr, 0),
            TALOS_RT_OK);
  activeProbe = nullptr;
  auto current = vpi_handle(vpiFrame, nullptr);
  ASSERT_NE(current, nullptr);
  EXPECT_EQ(vpi_compare_objects(current, probe.parent), 1);
  EXPECT_EQ(vpi_get(vpiActive, probe.thread), 1);
  EXPECT_EQ(vpi_get(vpiType, probe.frame), vpiUndefined);
  EXPECT_NE(vpi_chk_error(nullptr), 0);
  EXPECT_EQ(vpi_get(vpiType, probe.origin), vpiTask);
  EXPECT_EQ(vpi_release_handle(current), 1);
  EXPECT_EQ(vpi_release_handle(probe.parent), 1);
  if (!probe.escapedChild)
    EXPECT_EQ(vpi_release_handle(probe.thread), 1);
  EXPECT_EQ(vpi_release_handle(probe.origin), 1);
  ASSERT_EQ(talos_rt_v1_vpi_frame_leave(context, imported), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_vpi_call_finish(context, ticket), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_vpi_frame_leave(context, root), TALOS_RT_OK);
}
talos_rt_status nativeEntry(talos_rt_process_instance_v1 *instance) {
  instance->native_handle = instance;
  auto &frame = *static_cast<NativeFrame *>(instance->frame);
  auto &probe = *frame.probe;
  auto *context = instance->context;
  const uint64_t unit = instance->descriptor->handle.id;
  if (unit == 44) {
    if (!instance->continuation) {
      inspectOuter(context, probe);
      if (probe.escapedChild) {
        // Keep the source thread alive after the export dispatch has ended.
        frame.wait = {TALOS_RT_VERSION, TALOS_RT_SUSPEND_DELAY, 0, 0, 3, 0};
        *instance->action = {TALOS_RT_FRAGMENT_SUSPEND,
                             TALOS_RT_SUSPEND_DELAY,
                             1,
                             TALOS_RT_ACTION_FRAME_WAIT_RECORD,
                             0,
                             sizeof(frame.wait) + sizeof(frame.event)};
        return TALOS_RT_OK;
      }
    }
  } else if (unit == 47) {
    uint64_t activation = 0;
    auto status =
        talos_rt_v1_vpi_frame_enter_prepared(context, 47, 1, &activation);
    if (status != TALOS_RT_OK)
      return status;
    auto current = vpi_handle(vpiFrame, nullptr);
    auto thread = vpi_handle(vpiThread, current);
    auto parent = vpi_handle(vpiParent, current);
    EXPECT_EQ(vpi_compare_objects(thread, probe.thread), 1);
    EXPECT_EQ(vpi_compare_objects(parent, probe.frame), 1);
    EXPECT_EQ(vpi_get(vpiActive, current), 1);
    EXPECT_EQ(vpi_get(vpiActive, probe.frame), 0);
    EXPECT_EQ(vpi_release_handle(parent), 1);
    EXPECT_EQ(vpi_release_handle(thread), 1);
    EXPECT_EQ(vpi_release_handle(current), 1);
    // Task-return cleanup must restore the saved canonical source depth.
  } else if (unit == 49) {
    if (!instance->continuation) {
      frame.wait = {TALOS_RT_VERSION, TALOS_RT_SUSPEND_DELAY, 0, 0, 2, 0};
      *instance->action = {TALOS_RT_FRAGMENT_SUSPEND,
                           TALOS_RT_SUSPEND_DELAY,
                           1,
                           TALOS_RT_ACTION_FRAME_WAIT_RECORD,
                           0,
                           sizeof(frame.wait) + sizeof(frame.event)};
      return TALOS_RT_OK;
    }
    EXPECT_EQ(vpi_get(vpiType, probe.frame), vpiUndefined);
    EXPECT_NE(vpi_chk_error(nullptr), 0);
    EXPECT_EQ(vpi_handle(vpiFrame, nullptr), nullptr);
    uint64_t activation = 0;
    auto status = talos_rt_v1_vpi_frame_enter(
        context, 49, TALOS_RT_VPI_FRAME_CODE_UNIT_SCOPE, 49, &activation);
    if (status != TALOS_RT_OK)
      return status;
    auto childFrame = vpi_handle(vpiFrame, nullptr);
    auto child = vpi_handle(vpiThread, childFrame);
    auto parent = vpi_handle(vpiParent, child);
    EXPECT_NE(parent, nullptr);
    EXPECT_EQ(vpi_compare_objects(parent, probe.thread), 1);
    EXPECT_EQ(vpi_compare_objects(child, probe.thread), 0);
    EXPECT_EQ(vpi_get(vpiActive, probe.thread), 0);
    EXPECT_EQ(vpi_release_handle(parent), 1);
    EXPECT_EQ(vpi_release_handle(child), 1);
    EXPECT_EQ(vpi_release_handle(childFrame), 1);
    EXPECT_EQ(vpi_release_handle(probe.thread), 1);
    ++probe.escaped;
    status = talos_rt_v1_vpi_frame_leave(context, activation);
    if (status != TALOS_RT_OK)
      return status;
  } else if (unit == 48) {
    if (!instance->continuation) {
      frame.wait = {TALOS_RT_VERSION, TALOS_RT_SUSPEND_DELAY, 0, 0, 1, 0};
      *instance->action = {TALOS_RT_FRAGMENT_SUSPEND,
                           TALOS_RT_SUSPEND_DELAY,
                           1,
                           TALOS_RT_ACTION_FRAME_WAIT_RECORD,
                           0,
                           sizeof(frame.wait) + sizeof(frame.event)};
      return TALOS_RT_OK;
    }
    inspectUnrelated(context, probe);
    if (probe.killExport) {
      talos_rt_process_control_disposition disposition = 0;
      auto status = talos_rt_v1_process_control(
          context, probe.executor, TALOS_RT_PROCESS_CONTROL_KILL,
          &disposition);
      if (status != TALOS_RT_OK)
        return status;
      EXPECT_EQ(disposition, TALOS_RT_PROCESS_CONTROL_CONTINUE);
    } else {
      talos_rt_v1_scheduler_event(context, 7, 0);
    }
  } else {
    EXPECT_EQ(unit, 46u);
    if (!instance->continuation) {
      auto status = talos_rt_v1_vpi_frame_enter_prepared(context, 46, 1,
                                                           &probe.activation);
      if (status != TALOS_RT_OK)
        return status;
      inspectTask(context, probe, 0, probe.activation);
      if (probe.escapedChild) {
        auto status =
            talos_rt_v1_scheduler_add(context, probe.child->create(probe), 0);
        if (status != TALOS_RT_OK)
          return status;
      }
      if (probe.nested) {
        auto status =
            talos_rt_v1_vpi_call_prepare(context, 21, &probe.nestedTicket);
        if (status != TALOS_RT_OK)
          return status;
        auto *callee = probe.callee->create(probe);
        if (!callee)
          return TALOS_RT_FATAL;
        *instance->action = {TALOS_RT_FRAGMENT_TASK_CALL,
                             TALOS_RT_SUSPEND_NONE,
                             1,
                             TALOS_RT_ACTION_VPI_CALL_SITE,
                             reinterpret_cast<uintptr_t>(callee),
                             21};
        return TALOS_RT_OK;
      }
    }
    if (!instance->continuation ||
        (probe.nested && instance->continuation == 1)) {
      if (probe.nested) {
        auto status =
            talos_rt_v1_vpi_call_finish(context, probe.nestedTicket);
        if (status != TALOS_RT_OK)
          return status;
      }
      frame.wait = {TALOS_RT_VERSION, TALOS_RT_SUSPEND_EVENT, 0, 1, 0, 0};
      frame.event = {7, TALOS_RT_WAIT_EDGE_NONE, 0};
      *instance->action = {TALOS_RT_FRAGMENT_SUSPEND,
                           TALOS_RT_SUSPEND_EVENT,
                           probe.nested ? 2u : 1u,
                           TALOS_RT_ACTION_FRAME_WAIT_RECORD,
                           0,
                           sizeof(frame.wait) + sizeof(frame.event)};
      return TALOS_RT_OK;
    }
    inspectTask(context, probe, 1, probe.activation);
    if (!probe.implicitReturn) {
      auto status = talos_rt_v1_vpi_frame_leave(context, probe.activation);
      if (status != TALOS_RT_OK)
        return status;
    }
  }
  *instance->action = {
      TALOS_RT_FRAGMENT_TERMINATE, TALOS_RT_SUSPEND_NONE, 0, 0, 0, 0};
  return ::testing::Test::HasFatalFailure() ? TALOS_RT_FATAL : TALOS_RT_OK;
}
talos_rt_status exported(talos_rt_context *context,
                           const talos_rt_import_input_v1 *, uint32_t,
                           talos_rt_import_output_v1 *, uint32_t) {
  uint64_t ticket = 0;
  auto status = talos_rt_v1_vpi_export_prepare(context, 46, &ticket);
  if (status != TALOS_RT_OK)
    return status;
  if (activeProbe->bytecode)
    status = talos_rt_v1_dpi_export_task_bytecode_run(
        context, 1, nullptr, 0, nullptr, 0, nullptr, nullptr, nullptr);
  else {
    auto *instance = activeProbe->task->create(*activeProbe);
    if (!instance)
      return TALOS_RT_FATAL;
    status = talos_rt_v1_dpi_export_task_run(context, instance);
  }
  if (status != TALOS_RT_OK)
    return status;
  return talos_rt_v1_vpi_call_finish(context, ticket);
}
talos_rt_status imported(talos_rt_context *, uint32_t,
                           const talos_rt_import_input_v1 *, uint32_t,
                           talos_rt_import_output_v1 *, uint32_t, void *) {
  return talos_rt_v1_export_call(1, 7, nullptr, 0, nullptr, 0);
}
talos_rt_status inspectImport(talos_rt_context *context, uint32_t,
                                const talos_rt_import_input_v1 *inputs,
                                uint32_t inputCount,
                                talos_rt_import_output_v1 *, uint32_t,
                                void *data) {
  if (inputCount != 2)
    return TALOS_RT_ARGUMENT_MISMATCH;
  inspectTask(context, *static_cast<Probe *>(data), inputs[0].value[0],
              inputs[1].value[0]);
  return ::testing::Test::HasFatalFailure() ? TALOS_RT_FATAL : TALOS_RT_OK;
}

void testTask(bool bytecode, bool implicitReturn, bool escapedChild = false,
              bool killExport = false, bool nested = false) {
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
  struct Execution {
    talos_rt_execution_descriptor_v1 design;
    talos_rt_execution_extension_v1 extension{};
  } aggregate;
  auto &execution = aggregate.design;
  execution = {TALOS_RT_VERSION,
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
  execution.flags |= TALOS_RT_EXECUTION_DPI_EXPORTS;
  execution.reserved = offsetof(Execution, extension);
  const talos_rt_dpi_scope_v1 scopes[] = {
      {0, UINT64_MAX, "$root", 5, -9, -12, 0},
      {1, 0, "top", 3, -9, -12, 0},
      {2, 0, "other", 5, -9, -12, 0}};
  execution.dpi_scopes = scopes;
  execution.dpi_scope_count = 3;
  execution.dpi_time_precision = -12;

  auto function =
      std::find_if(encoded->functions.begin(), encoded->functions.end(),
                   [](const auto &f) { return f.symbol == "exported"; });
  ASSERT_NE(function, encoded->functions.end());
  const talos_rt_export_descriptor_v1 exports[] = {
      {1, TALOS_RT_EXPORT_TASK | TALOS_RT_EXPORT_HAS_BYTECODE, 1, 46, 7, 0,
       0, function->index, 0, exported, 0}};
  aggregate.extension.version = TALOS_RT_EXECUTION_EXTENSION_VERSION;
  aggregate.extension.size = sizeof(aggregate.extension);
  aggregate.extension.exports = exports;
  aggregate.extension.export_count = 1;
  ASSERT_EQ(talos_rt_v1_design_validate(&execution), TALOS_RT_OK);
  talos_rt_context *raw = nullptr;
  ASSERT_EQ(talos_rt_v1_context_create_for_design(&execution, &raw),
            TALOS_RT_OK);
  std::unique_ptr<talos_rt_context, decltype(&talos_rt_v1_context_destroy)>
      runtime(raw, talos_rt_v1_context_destroy);
  Probe probe{bytecode, implicitReturn, escapedChild, killExport, nested};
  NativeDescriptor outer(44, &execution), task(46, &execution),
      driver(48, &execution), child(49, &execution), callee(47, &execution);
  probe.task = &task;
  probe.child = &child;
  probe.callee = &callee;
  ASSERT_EQ(talos_rt_v1_context_register_import(raw, 123, imported, &probe),
            TALOS_RT_OK);
  ASSERT_EQ(
      talos_rt_v1_context_register_import(raw, 124, inspectImport, &probe),
      TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_vpi_startup(raw, nullptr, 0), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_vpi_end_compile(raw), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_vpi_start_simulation(raw), TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_add(raw, driver.create(probe), 0),
            TALOS_RT_OK);
  ASSERT_EQ(talos_rt_v1_scheduler_add(raw, outer.create(probe), 0),
            TALOS_RT_OK);
  auto status = talos_rt_v1_scheduler_run(raw);
  EXPECT_EQ(probe.phases, killExport ? 1u : 2u);
  EXPECT_EQ(probe.escaped, escapedChild ? 1u : 0u);
  EXPECT_EQ(probe.unrelated, 1u);
  EXPECT_EQ(status, TALOS_RT_OK);
}
TEST(GeneratedDesignDatabase, DPIExportTaskFramesNative) {
  testTask(false, false);
}
TEST(GeneratedDesignDatabase, DPIExportTaskFramesBytecode) {
  testTask(true, false);
}
TEST(GeneratedDesignDatabase, DPIExportTaskFramesUnwind) {
  testTask(false, true);
}
TEST(GeneratedDesignDatabase, DPIExportTaskFramesEscapedChild) {
  testTask(false, false, true);
}
TEST(GeneratedDesignDatabase, DPIExportTaskFramesNativeKill) {
  testTask(false, false, false, true);
}
TEST(GeneratedDesignDatabase, DPIExportTaskFramesBytecodeKill) {
  testTask(true, false, false, true);
}
TEST(GeneratedDesignDatabase, DPIExportTaskFramesNestedTask) {
  testTask(false, false, false, false, true);
}
} // namespace
