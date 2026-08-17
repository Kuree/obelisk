// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s

// An automatic event variable captured by a spawned branch lives in an
// automatic reference cell. The cell holds an event handle, so it has to be
// sized like one rather than like the event's one-bit provenance span.

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @automatic_event {
    obelisk_sim.scope.decl 0 hierarchy "top" debug "top"
    obelisk_sim.code_unit.decl 9200001 in 0 initial hierarchy "top.spawner"
        debug "spawner"
    obelisk_sim.code_unit.decl 9200002 in 0 fork hierarchy "top.waiter"
        debug "waiter" {internal}

    obelisk_sim.func @waiter(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %cell: !obelisk_sim.ref<!obelisk_sim.event>
            {obelisk_sim.automatic_reference_capture,
             obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 13 : i32, code_unit_id = 9200002 : i64} {
      %event = obelisk_sim.ref.load %cell :
          !obelisk_sim.ref<!obelisk_sim.event> -> !obelisk_sim.event
      obelisk_sim.suspend.event %event to ^resumed
    ^resumed:
      obelisk_sim.return
    }

    obelisk_sim.func @spawner(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9200001 : i64} {
      %event = obelisk_sim.event.create
      %cell = obelisk_sim.ref.alloc %event :
          !obelisk_sim.event -> !obelisk_sim.ref<!obelisk_sim.event>
      obelisk_sim.ref.store %event to %cell :
          !obelisk_sim.event, !obelisk_sim.ref<!obelisk_sim.event>
      %child = obelisk_sim.spawn @waiter(%ctx, %cell) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.event> ->
          !obelisk_sim.process
      obelisk_sim.return
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
