// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

// An automatic event variable captured by a spawned branch lives in an
// automatic reference cell. The cell holds an event handle, so it has to be
// sized like one rather than like the event's one-bit provenance span.

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @automatic_event {
    simulation.scope.decl 0 hierarchy "top" debug "top"
    simulation.code_unit.decl 9200001 in 0 initial hierarchy "top.spawner"
        debug "spawner"
    simulation.code_unit.decl 9200002 in 0 fork hierarchy "top.waiter"
        debug "waiter" {internal}

    simulation.func @waiter(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %cell: !simulation.ref<!simulation.event>
            {simulation.automatic_reference_capture,
             simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 13 : i32, code_unit_id = 9200002 : i64} {
      %event = simulation.ref.load %cell :
          !simulation.ref<!simulation.event> -> !simulation.event
      simulation.suspend.event %event to ^resumed
    ^resumed:
      simulation.return
    }

    simulation.func @spawner(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9200001 : i64} {
      %event = simulation.event.create
      %cell = simulation.ref.alloc %event :
          !simulation.event -> !simulation.ref<!simulation.event>
      simulation.ref.store %event to %cell :
          !simulation.event, !simulation.ref<!simulation.event>
      %child = simulation.spawn @waiter(%ctx, %cell) :
          !simulation.context, !simulation.ref<!simulation.event> ->
          !simulation.process
      simulation.return
    }
  }
}

// CHECK-LABEL: llvm.func @waiter.__obelisk_coro_ramp
// CHECK: %[[LOAD_BITS:.*]] = llvm.mlir.constant(64 : i64) : i64
// CHECK: llvm.call @obelisk_rt_v1_native_state_load_plane({{.*}}, %[[LOAD_BITS]], {{.*}}, %[[SLOT:[0-9a-zA-Z_]+]])
// CHECK: llvm.load %[[SLOT]] {{.*}} : !llvm.ptr -> i64

// CHECK-LABEL: llvm.func @spawner
// CHECK: %[[ALLOC_BITS:.*]] = llvm.mlir.constant(64 : i64) : i64
// CHECK: llvm.call @obelisk_rt_v1_native_state_alloc({{.*}}, %[[ALLOC_BITS]],
