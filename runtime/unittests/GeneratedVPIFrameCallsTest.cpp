//===- GeneratedVPIFrameCallsTest.cpp - Exact frame call handoff ------===//

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
#include "vpi_user.h"
#include "gtest/gtest.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <memory>
#include <vector>

extern "C" void talos_rt_v1_dpi_import_bytecode_link_anchor(void);

namespace {
enum class Engine { Bytecode, Native, AOT };
enum class Exit { Return, Kill, DisableTask, DisableBlock, EscapeBlock };

struct CallProbe {
  Exit exit;
  uint64_t token = 0, taskControl = 0, blockControl = 0, blockID = 0;
  uint64_t outerTicket = 0, innerTicket = 0, functionTicket = 0;
  vpiHandle functionOrigin = nullptr;
  std::array<vpiHandle, 4> frames{};
  vpiHandle thread = nullptr, statement = nullptr;
  const talos_rt_process_descriptor_v1 *middle = nullptr, *inner = nullptr;
  std::vector<unsigned> phases;
};

void expectExpired(vpiHandle handle) {
  ASSERT_NE(handle, nullptr);
  EXPECT_EQ(vpi_get(vpiType, handle), vpiUndefined);
  EXPECT_NE(vpi_chk_error(nullptr), 0);
}

void expectDepth(talos_rt_context *context, uint64_t expected) {
  uint64_t depth = UINT64_MAX;
  ASSERT_EQ(talos_rt_v1_vpi_frame_depth(context, &depth), TALOS_RT_OK);
  EXPECT_EQ(depth, expected);
}

void inspectCalls(talos_rt_context *context, CallProbe &probe,
                  unsigned phase) {
  probe.phases.push_back(phase);
  const uint64_t current = talos_rt_v1_process_current(context);
  ASSERT_NE(current, 0u);
  if (phase == 0)
    probe.token = current;
  ASSERT_EQ(current, probe.token);
  if (phase == 10) {
    expectDepth(context, 4);
    uint64_t activation = 0;
    ASSERT_EQ(
        talos_rt_v1_vpi_frame_enter_prepared(context, 47, 0, &activation),
        TALOS_RT_OK);
    expectDepth(context, 5);
    auto frame = vpi_handle(vpiFrame, nullptr);
    ASSERT_NE(frame, nullptr);
    probe.functionOrigin = vpi_handle(vpiOrigin, frame);
    ASSERT_NE(probe.functionOrigin, nullptr);
    EXPECT_EQ(vpi_get(vpiType, probe.functionOrigin), vpiFuncCall);
    auto parent = vpi_handle(vpiParent, frame);
    ASSERT_NE(parent, nullptr);
    EXPECT_EQ(vpi_compare_objects(parent, probe.frames[3]), 1);
    EXPECT_EQ(vpi_release_handle(parent), 1);
    EXPECT_EQ(talos_rt_v1_vpi_call_finish(context, probe.functionTicket),
              TALOS_RT_INVALID_LIFECYCLE);
    uint64_t rejected = UINT64_MAX;
    EXPECT_EQ(talos_rt_v1_vpi_frame_enter_prepared(context, 47, 0, &rejected),
              TALOS_RT_INVALID_LIFECYCLE);
    EXPECT_EQ(rejected, 0u);
    ASSERT_EQ(talos_rt_v1_vpi_frame_checkpoint(context, 15), TALOS_RT_OK);
    ASSERT_EQ(talos_rt_v1_vpi_frame_leave(context, activation),
              TALOS_RT_OK);
    expectExpired(frame);
    return;
  }
  if (phase == 11) {
    expectDepth(context, 4);
    ASSERT_EQ(talos_rt_v1_vpi_call_finish(context, probe.functionTicket),
              TALOS_RT_OK);
    EXPECT_EQ(talos_rt_v1_vpi_call_finish(context, probe.functionTicket),
              TALOS_RT_INVALID_FRAME);
    uint64_t pending = UINT64_MAX;
    ASSERT_EQ(talos_rt_vpi_pending_call(context, &pending), TALOS_RT_OK);
    EXPECT_EQ(pending, 0u);
    return;
  }
  if (phase <= 3) {
    // The real scheduler has already saved the caller depth before each task
    // entry. Controls precede their frame entry, matching compiler ordering.
    expectDepth(context, phase);
    if (phase == 1)
      ASSERT_EQ(talos_rt_v1_control_enter(context, 888, &probe.taskControl),
                TALOS_RT_OK);
    uint64_t activation = 0;
    const uint64_t unit = phase == 0 ? 44 : phase == 3 ? 46 : 45;
    const uint32_t kind = phase == 0   ? TALOS_RT_VPI_FRAME_CODE_UNIT_SCOPE
                          : phase == 2 ? TALOS_RT_VPI_FRAME_STATEMENT_SCOPE
                                       : TALOS_RT_VPI_FRAME_CALL;
    const uint64_t origin = phase == 0   ? 44
                            : phase == 1 ? 20
                            : phase == 2 ? 12
                                         : 21;
    if (phase == 1 || phase == 3) {
      uint64_t carried = UINT64_MAX;
      ASSERT_EQ(talos_rt_vpi_activation_callsite(context, &carried),
                TALOS_RT_OK);
      EXPECT_EQ(carried, origin);
      uint64_t rejected = UINT64_MAX;
      EXPECT_EQ(
          talos_rt_v1_vpi_frame_enter_prepared(context, unit, 0, &rejected),
          TALOS_RT_INVALID_LIFECYCLE);
      EXPECT_EQ(rejected, 0u);
      EXPECT_EQ(
          talos_rt_v1_vpi_frame_enter_prepared(context, 47, 1, &rejected),
          TALOS_RT_INVALID_ARGUMENT);
      EXPECT_EQ(rejected, 0u);
      ASSERT_EQ(
          talos_rt_v1_vpi_frame_enter_prepared(context, unit, 1, &activation),
          TALOS_RT_OK);
    } else {
      ASSERT_EQ(talos_rt_v1_vpi_frame_enter(context, unit, kind, origin,
                                              &activation),
                TALOS_RT_OK);
    }
    if (phase == 2)
      probe.blockID = activation;
    probe.frames[phase] = vpi_handle(vpiFrame, nullptr);
    ASSERT_NE(probe.frames[phase], nullptr);
    EXPECT_EQ(vpi_get(vpiActive, probe.frames[phase]), 1);
    expectDepth(context, phase + 1);
    ASSERT_EQ(talos_rt_v1_vpi_frame_checkpoint(context, 11 + phase),
              TALOS_RT_OK);
    if (phase == 0) {
      probe.thread = vpi_handle(vpiThread, probe.frames[0]);
      ASSERT_NE(probe.thread, nullptr);
      probe.statement = vpi_handle(vpiStmt, probe.frames[0]);
      ASSERT_NE(probe.statement, nullptr);
    }
    if (phase == 0 || phase == 2 || phase == 3) {
      uint64_t &ticket = phase == 0   ? probe.outerTicket
                         : phase == 2 ? probe.innerTicket
                                      : probe.functionTicket;
      uint64_t call = phase == 0 ? 20 : phase == 2 ? 21 : 22;
      ASSERT_EQ(talos_rt_v1_vpi_call_prepare(context, call, &ticket),
                TALOS_RT_OK);
      ASSERT_NE(ticket, 0u);
      uint64_t rejected = UINT64_MAX;
      EXPECT_EQ(talos_rt_v1_vpi_call_prepare(context, call, &rejected),
                TALOS_RT_INVALID_LIFECYCLE);
      EXPECT_EQ(rejected, 0u);
      EXPECT_EQ(talos_rt_v1_vpi_call_finish(context, ticket),
                TALOS_RT_INVALID_LIFECYCLE);
      expectDepth(context, phase + 1);
    }
    return;
  }
  if (phase == 4 || phase == 5) {
    expectDepth(context, 4);
    EXPECT_EQ(vpi_get(vpiActive, probe.frames[3]), 1);
    if (phase == 4 && probe.exit == Exit::EscapeBlock) {
      ASSERT_EQ(talos_rt_v1_control_disable(context, 777, 0, 1),
                TALOS_RT_OK);
      expectDepth(context, 2);
      expectExpired(probe.frames[2]);
      expectExpired(probe.frames[3]);
    }
    return;
  }
  if (phase == 6) {
    expectDepth(context, 3);
    expectExpired(probe.frames[3]);
    EXPECT_EQ(vpi_get(vpiActive, probe.frames[2]), 1);
    ASSERT_EQ(talos_rt_v1_vpi_call_finish(context, probe.innerTicket),
              TALOS_RT_OK);
    ASSERT_EQ(talos_rt_v1_vpi_frame_leave(context, probe.blockID),
              TALOS_RT_OK);
  } else if (phase == 7) {
    expectDepth(context, 2);
    expectExpired(probe.frames[2]);
    expectExpired(probe.frames[3]);
    EXPECT_EQ(vpi_get(vpiActive, probe.frames[1]), 1);
    if (probe.exit == Exit::DisableBlock || probe.exit == Exit::EscapeBlock)
      ASSERT_EQ(talos_rt_v1_vpi_call_finish(context, probe.innerTicket),
                TALOS_RT_OK);
    // A prepared call whose lexical scope is disabled must never supply the
    // source identity for an unrelated activation after that scope exits.
    uint64_t scope = 0, cancelled = 0;
    ASSERT_EQ(
        talos_rt_v1_vpi_frame_enter(
            context, 45, TALOS_RT_VPI_FRAME_STATEMENT_SCOPE, 12, &scope),
        TALOS_RT_OK);
    ASSERT_EQ(talos_rt_v1_vpi_call_prepare(context, 21, &cancelled),
              TALOS_RT_OK);
    ASSERT_EQ(talos_rt_v1_vpi_frame_unwind(context, 2), TALOS_RT_OK);
    ASSERT_EQ(talos_rt_v1_vpi_call_finish(context, cancelled), TALOS_RT_OK);
    uint64_t pending = UINT64_MAX;
    ASSERT_EQ(talos_rt_vpi_pending_call(context, &pending), TALOS_RT_OK);
    EXPECT_EQ(pending, 0u);
    ASSERT_EQ(talos_rt_v1_control_leave(context, probe.taskControl),
              TALOS_RT_OK);
  } else {
    ASSERT_EQ(phase, 8u);
    expectDepth(context, 1);
    for (unsigned i = 1; i != 4; ++i)
      expectExpired(probe.frames[i]);
    EXPECT_EQ(vpi_get(vpiActive, probe.frames[0]), 1);
    ASSERT_EQ(talos_rt_v1_vpi_call_finish(context, probe.outerTicket),
              TALOS_RT_OK);
    uint64_t pending = UINT64_MAX;
    ASSERT_EQ(talos_rt_vpi_pending_call(context, &pending), TALOS_RT_OK);
    EXPECT_EQ(pending, 0u);
  }
  // Root and task frames deliberately have no compiler leave in this fixture.
  // Their return must restore the exact saved caller depth in the scheduler.
}

talos_rt_status inspectCallsImport(talos_rt_context *context,
                                     uint32_t importID,
                                     const talos_rt_import_input_v1 *inputs,
                                     uint32_t inputCount,
                                     talos_rt_import_output_v1 *,
                                     uint32_t outputCount, void *userData) {
  EXPECT_EQ(importID, 123u);
  EXPECT_EQ(outputCount, 0u);
  if (inputCount != 1 || !inputs || !inputs[0].value)
    return TALOS_RT_INVALID_ARGUMENT;
  inspectCalls(context, *static_cast<CallProbe *>(userData),
               inputs[0].value[0]);
  return ::testing::Test::HasFatalFailure() ? TALOS_RT_FATAL : TALOS_RT_OK;
}

struct NativeFrame {
  talos_rt_wait_record_v1 wait;
  talos_rt_wait_entry_v1 event;
  CallProbe *probe;
};

talos_rt_status nativeCalls(talos_rt_process_instance_v1 *instance) {
  auto &frame = *static_cast<NativeFrame *>(instance->frame);
  auto &probe = *frame.probe;
  auto *context = instance->context;
  instance->native_handle = instance;
  auto inspect = [&](unsigned phase) {
    inspectCalls(context, probe, phase);
    return !::testing::Test::HasFatalFailure();
  };
  auto call = [&](const talos_rt_process_descriptor_v1 *descriptor) {
    talos_rt_process_instance_v1 *callee = nullptr;
    auto status = talos_rt_v1_process_instance_create(descriptor, &callee);
    if (status != TALOS_RT_OK)
      return status;
    static_cast<NativeFrame *>(callee->frame)->probe = &probe;
    uint64_t callID = 0;
    status = talos_rt_vpi_pending_call(context, &callID);
    if (status != TALOS_RT_OK || !callID) {
      talos_rt_v1_process_instance_destroy(callee);
      return status == TALOS_RT_OK ? TALOS_RT_INVALID_FRAME : status;
    }
    *instance->action = {TALOS_RT_FRAGMENT_TASK_CALL,
                         TALOS_RT_SUSPEND_NONE,
                         1,
                         TALOS_RT_ACTION_VPI_CALL_SITE,
                         reinterpret_cast<uintptr_t>(callee),
                         callID};
    return TALOS_RT_OK;
  };
  const uint64_t unit = instance->descriptor->handle.id;
  if (unit == 48) {
    EXPECT_NE(talos_rt_v1_process_current(context), probe.token);
    auto status = talos_rt_v1_control_disable(
        context, probe.exit == Exit::DisableTask ? 888 : 777, 0, 1);
    if (status != TALOS_RT_OK)
      return status;
  } else if (unit == 44) {
    if (instance->continuation == 0) {
      if (!inspect(0))
        return TALOS_RT_FATAL;
      return call(probe.middle);
    }
    if (!inspect(8))
      return TALOS_RT_FATAL;
  } else if (unit == 45) {
    if (instance->continuation == 0) {
      if (!inspect(1))
        return TALOS_RT_FATAL;
      auto status =
          talos_rt_v1_control_enter(context, 777, &probe.blockControl);
      if (status != TALOS_RT_OK)
        return status;
      status = talos_rt_v1_control_boundary(context, probe.blockControl, 2);
      if (status != TALOS_RT_OK)
        return status;
      if (!inspect(2))
        return TALOS_RT_FATAL;
      return call(probe.inner);
    }
    if (instance->continuation == 1) {
      if (!inspect(6))
        return TALOS_RT_FATAL;
      auto status = talos_rt_v1_control_leave(context, probe.blockControl);
      if (status != TALOS_RT_OK)
        return status;
    }
    if (!inspect(7))
      return TALOS_RT_FATAL;
  } else {
    EXPECT_EQ(unit, 46u);
    if (instance->continuation == 0) {
      if (!inspect(3) || !inspect(10) || !inspect(11))
        return TALOS_RT_FATAL;
      frame.wait = {TALOS_RT_VERSION, TALOS_RT_SUSPEND_EVENT, 0, 1, 0, 0};
      frame.event = {7, TALOS_RT_WAIT_EDGE_NONE, 0};
      *instance->action = {TALOS_RT_FRAGMENT_SUSPEND,
                           TALOS_RT_SUSPEND_EVENT,
                           1,
                           TALOS_RT_ACTION_FRAME_WAIT_RECORD,
                           0,
                           sizeof(frame.wait) + sizeof(frame.event)};
      return TALOS_RT_OK;
    }
    if (!inspect(4))
      return TALOS_RT_FATAL;
    if (!talos_rt_v1_control_escape_pending(context) && !inspect(5))
      return TALOS_RT_FATAL;
  }
  *instance->action = {
      TALOS_RT_FRAGMENT_TERMINATE, TALOS_RT_SUSPEND_NONE, 0, 0, 0, 0};
  return TALOS_RT_OK;
}

struct NativeDescriptor {
  std::array<talos_rt_frame_field_v1, 2> fields{
      {{TALOS_RT_FRAME_WAIT, 0, 0,
        sizeof(talos_rt_wait_record_v1) + sizeof(talos_rt_wait_entry_v1),
        alignof(talos_rt_wait_record_v1), 0},
       {TALOS_RT_FRAME_CAPTURE, 0, offsetof(NativeFrame, probe),
        sizeof(CallProbe *), alignof(CallProbe *), 0}}};
  std::array<uint32_t, 3> continuations{{0, 1, 2}};
  talos_rt_frame_layout_v1 layout{};
  talos_rt_process_descriptor_v1 descriptor{};
  explicit NativeDescriptor(uint64_t id) {
    layout = {TALOS_RT_VERSION,
              0,
              sizeof(NativeFrame),
              alignof(NativeFrame),
              fields.data(),
              static_cast<uint32_t>(fields.size()),
              static_cast<uint32_t>(continuations.size()),
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
        nativeCalls,
        [](talos_rt_process_instance_v1 *p) { p->native_handle = nullptr; },
        nullptr,
        nullptr,
        nullptr};
  }
  talos_rt_process_instance_v1 *create(CallProbe &probe) {
    talos_rt_process_instance_v1 *instance = nullptr;
    EXPECT_EQ(talos_rt_v1_process_instance_create(&descriptor, &instance),
              TALOS_RT_OK);
    if (instance)
      static_cast<NativeFrame *>(instance->frame)->probe = &probe;
    return instance;
  }
};

TEST(GeneratedDesignDatabase, FrameCallActionContract) {
  NativeDescriptor descriptor(44);
  CallProbe probe{};
  auto *instance = descriptor.create(probe);
  ASSERT_NE(instance, nullptr);
  talos_rt_fragment_action_v1 action{TALOS_RT_FRAGMENT_TASK_CALL,
                                       TALOS_RT_SUSPEND_NONE,
                                       1,
                                       0,
                                       reinterpret_cast<uintptr_t>(instance),
                                       0};
  EXPECT_EQ(talos::process::validateAction(*instance, action, false),
            TALOS_RT_OK);
  action.flags = TALOS_RT_ACTION_VPI_CALL_SITE;
  action.auxiliary = UINT64_C(0x100000001);
  EXPECT_EQ(talos::process::validateAction(*instance, action, false),
            TALOS_RT_OK);
  action.auxiliary = 0;
  EXPECT_EQ(talos::process::validateAction(*instance, action, false),
            TALOS_RT_INVALID_ARGUMENT);
  action.auxiliary = UINT64_C(1) << 63;
  EXPECT_EQ(talos::process::validateAction(*instance, action, false),
            TALOS_RT_INVALID_ARGUMENT);
  action.auxiliary = 20;
  action.flags |= TALOS_RT_ACTION_FRAME_WAIT_RECORD;
  EXPECT_EQ(talos::process::validateAction(*instance, action, false),
            TALOS_RT_INVALID_ARGUMENT);
  action.flags = 0;
  EXPECT_EQ(talos::process::validateAction(*instance, action, false),
            TALOS_RT_INVALID_ARGUMENT);
  EXPECT_EQ(talos_rt_v1_process_instance_destroy(instance), TALOS_RT_OK);
}

TEST(GeneratedDesignDatabase, FrameCallProtocol) {
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
  const auto startup = std::find_if(
      encoded->functions.begin(), encoded->functions.end(),
      [](const auto &function) { return function.symbol == "startup"; });
  ASSERT_NE(startup, encoded->functions.end());

  for (Engine engine : {Engine::Bytecode, Engine::Native, Engine::AOT}) {
    for (Exit exit : {Exit::Return, Exit::Kill, Exit::DisableTask,
                      Exit::DisableBlock, Exit::EscapeBlock}) {
      SCOPED_TRACE(::testing::Message() << "engine=" << unsigned(engine)
                                        << " exit=" << unsigned(exit));
      CallProbe probe{};
      probe.exit = exit;
      NativeDescriptor outer(44), middle(45), inner(46), controller(48);
      for (auto *descriptor : {&outer, &middle, &inner, &controller})
        descriptor->descriptor.execution = &execution;
      probe.middle = &middle.descriptor;
      probe.inner = &inner.descriptor;
      talos_rt_process_instance_v1 *actor = nullptr;
      talos_rt_native_schedule_plan plan{};
      talos_rt_context *rawContext = nullptr;
      ASSERT_EQ(
          talos_rt_v1_context_create_for_design(&execution, &rawContext),
          TALOS_RT_OK);
      std::unique_ptr<talos_rt_context,
                      decltype(&talos_rt_v1_context_destroy)>
          runtime(rawContext, talos_rt_v1_context_destroy);
      ASSERT_EQ(talos_rt_v1_context_register_import(
                    runtime.get(), 123, inspectCallsImport, &probe),
                TALOS_RT_OK);
      ASSERT_EQ(talos_rt_v1_vpi_startup(runtime.get(), nullptr, 0),
                TALOS_RT_OK);
      ASSERT_EQ(talos_rt_v1_vpi_end_compile(runtime.get()), TALOS_RT_OK);
      ASSERT_EQ(talos_rt_v1_vpi_start_simulation(runtime.get()),
                TALOS_RT_OK);
      if (engine == Engine::AOT) {
        plan.size = sizeof(plan);
        plan.graph_layout_checksum = execution.checksum;
        plan.flags = TALOS_RT_NATIVE_SCHEDULE_FULLY_STATIC;
        plan.mutable_state = &actor;
        plan.mutable_state_size = sizeof(actor);
        plan.actor_capacity = 1;
        plan.bind = [](void *state, talos_rt_context *, uint32_t slot,
                       talos_rt_process_instance_v1 *instance) {
          if (slot != 0)
            return TALOS_RT_INVALID_ARGUMENT;
          *static_cast<talos_rt_process_instance_v1 **>(state) = instance;
          return TALOS_RT_OK;
        };
        plan.run = [](void *, talos_rt_context *ctx) {
          constexpr talos_rt_native_schedule_node nodes[] = {
              {0, 0, UINT32_MAX}, {0, 1, UINT32_MAX}};
          return talos_rt_v1_scheduler_run_aot_nodes(ctx, nodes,
                                                       std::size(nodes));
        };
        plan.fallback_snapshot = [](void *, talos_rt_context *ctx,
                                    talos_rt_aot_deopt_snapshot *snapshot) {
          return talos_rt_v1_scheduler_snapshot_aot(ctx, snapshot);
        };
        ASSERT_EQ(talos_rt_v1_scheduler_install_aot(runtime.get(), &plan),
                  TALOS_RT_OK);
      }
      if (engine == Engine::Bytecode) {
        talos_rt_design_bytecode_entry_v1 entry{&execution, startup->index,
                                                  0};
        uint64_t scratchSize = 0, scratchAlignment = 0;
        ASSERT_EQ(talos_rt_validate_design_bytecode(
                      entry, runtime.get(), &scratchSize, &scratchAlignment),
                  TALOS_RT_OK);
        ASSERT_LE(scratchAlignment, alignof(uint64_t));
        std::vector<uint64_t> frame((scratchSize + 7) / 8);
        talos_rt_fragment_action_v1 action{};
        ASSERT_EQ(talos_rt_execute_design_bytecode(
                      entry, runtime.get(), frame.data(), frame.size() * 8, 0,
                      scratchSize, 0, 0, &action),
                  TALOS_RT_OK);
        ASSERT_EQ(action.kind, TALOS_RT_FRAGMENT_TERMINATE);
      } else {
        auto *instance = outer.create(probe);
        ASSERT_NE(instance, nullptr);
        ASSERT_EQ(engine == Engine::AOT
                      ? talos_rt_v1_scheduler_add_aot(runtime.get(), instance,
                                                        0, 0, 0, nullptr,
                                                        nullptr, 0, nullptr, 0)
                      : talos_rt_v1_scheduler_add(runtime.get(), instance, 0),
                  TALOS_RT_OK);
      }
      auto run = [&] {
        return engine == Engine::AOT
                   ? talos_rt_v1_scheduler_run_aot(runtime.get())
                   : talos_rt_v1_scheduler_run(runtime.get());
      };
      ASSERT_EQ(run(), TALOS_RT_OK);
      ASSERT_EQ(probe.phases, (std::vector<unsigned>{0, 1, 2, 3, 10, 11}));
      if (engine == Engine::AOT) {
        EXPECT_EQ(runtime->nativeSchedulePlan, &plan);
        EXPECT_GT(runtime->signalDiagnostics.aotNodeExecutions, 0u);
        EXPECT_FALSE(runtime->nativeScheduleDeoptimized);
      }
      ASSERT_NE(probe.token, 0u);
      EXPECT_EQ(vpi_handle(vpiFrame, nullptr), nullptr);
      for (auto frame : probe.frames) {
        ASSERT_NE(frame, nullptr);
        EXPECT_EQ(vpi_get(vpiType, frame), vpiFrame);
        EXPECT_EQ(vpi_get(vpiActive, frame), 0);
      }
      auto suspended = vpi_handle(vpiFrame, probe.thread);
      ASSERT_NE(suspended, nullptr);
      EXPECT_EQ(vpi_compare_objects(suspended, probe.frames[3]), 1);
      EXPECT_EQ(vpi_release_handle(suspended), 1);
      const uint64_t aotExecutions =
          runtime->signalDiagnostics.aotNodeExecutions;
      if (exit == Exit::Kill) {
        talos_rt_process_control_disposition disposition{};
        auto status = talos_rt_v1_process_control(
            runtime.get(), probe.token, TALOS_RT_PROCESS_CONTROL_KILL,
            &disposition);
        if (engine == Engine::AOT) {
          // process.control is deliberately excluded from installed AOT plans.
          // Rejection must retain the suspended activation; resume normally.
          EXPECT_EQ(status, TALOS_RT_TIER_UNAVAILABLE);
          EXPECT_EQ(disposition, TALOS_RT_PROCESS_CONTROL_CONTINUE);
          for (auto frame : probe.frames)
            EXPECT_EQ(vpi_get(vpiType, frame), vpiFrame);
          talos_rt_v1_scheduler_event(runtime.get(), 7, 0);
        } else {
          ASSERT_EQ(status, TALOS_RT_OK);
        }
      } else if (exit == Exit::DisableTask || exit == Exit::DisableBlock) {
        auto *instance = controller.create(probe);
        ASSERT_NE(instance, nullptr);
        ASSERT_EQ(talos_rt_v1_scheduler_add(runtime.get(), instance, 0),
                  TALOS_RT_OK);
      } else {
        talos_rt_v1_scheduler_event(runtime.get(), 7, 0);
      }
      ASSERT_EQ(run(), TALOS_RT_OK);
      if (engine == Engine::AOT) {
        EXPECT_EQ(runtime->nativeSchedulePlan, &plan);
        EXPECT_FALSE(runtime->nativeScheduleDeoptimized);
        EXPECT_GT(runtime->signalDiagnostics.aotNodeExecutions, aotExecutions);
      }
      std::vector<unsigned> expected{0, 1, 2, 3, 10, 11};
      if (exit == Exit::Return || (engine == Engine::AOT && exit == Exit::Kill))
        expected.insert(expected.end(), {4, 5, 6, 7, 8});
      else if (exit == Exit::DisableTask)
        expected.push_back(8);
      else if (exit == Exit::DisableBlock)
        expected.insert(expected.end(), {7, 8});
      else if (exit == Exit::EscapeBlock)
        expected.insert(expected.end(), {4, 7, 8});
      EXPECT_EQ(probe.phases, expected);
      for (auto frame : probe.frames)
        expectExpired(frame);
      expectExpired(probe.thread);
      uint64_t depth = UINT64_MAX;
      ASSERT_EQ(talos_rt_vpi_frame_depth_process(runtime.get(), probe.token,
                                                   &depth),
                TALOS_RT_OK);
      EXPECT_EQ(depth, 0u);
      EXPECT_EQ(vpi_get(vpiType, probe.statement), vpiTaskCall);
      EXPECT_EQ(vpi_release_handle(probe.statement), 1);
      ASSERT_NE(probe.functionOrigin, nullptr);
      EXPECT_EQ(vpi_get(vpiType, probe.functionOrigin), vpiFuncCall);
      EXPECT_EQ(vpi_release_handle(probe.functionOrigin), 1);
      // Destroy the context before descriptor/plan/probe storage leaves scope.
      runtime.reset();
    }
  }
}
} // namespace
