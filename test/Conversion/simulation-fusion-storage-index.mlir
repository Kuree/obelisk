// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=SHARED
// RUN: sed '/^\/\/ OBSERVER-BEGIN/,/^\/\/ OBSERVER-END/d' %s > %t.private.mlir
// RUN: obelisk-opt %t.private.mlir --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=PRIVATE

// Two consecutive fusions share the incremental identity/escape index.
// Removing old actors and replacing their root spawns makes each temporary
// exclusive to its new group. An independent accessor must still prevent
// deleting the second group's canonical store. Removing that accessor permits
// promotion in both groups, including the one indexed before it was fused.
// SHARED-LABEL: simulation.func private @__obelisk_fused_0
// SHARED-NOT: simulation.ref.store
// SHARED-NOT: simulation.ref.load
// SHARED: simulation.nba.enqueue
// SHARED-LABEL: simulation.func private @__obelisk_fused_1
// SHARED: simulation.ref.store
// SHARED: simulation.ref.load
// SHARED: simulation.nba.enqueue
// PRIVATE-LABEL: simulation.func private @__obelisk_fused_0
// PRIVATE-NOT: simulation.ref.store
// PRIVATE-NOT: simulation.ref.load
// PRIVATE: simulation.nba.enqueue
// PRIVATE-LABEL: simulation.func private @__obelisk_fused_1
// PRIVATE-NOT: simulation.ref.store
// PRIVATE-NOT: simulation.ref.load
// PRIVATE: simulation.nba.enqueue

!ref = !simulation.ref<i32>
!clock = !simulation.ref<i1>
module attributes {schedule.native_scheduler = 1 : i32} {
  simulation.design @storage_index {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : i1 design
    simulation.storage.decl 2 in 0 : i32 static
    simulation.storage.decl 3 in 0 : i32 static
    simulation.storage.decl 4 in 0 : i32 design
    simulation.storage.decl 5 in 0 : i32 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "first_writer"
    simulation.code_unit.decl 3 in 0 always hierarchy "first_reader"
    simulation.code_unit.decl 4 in 0 always hierarchy "second_writer"
    simulation.code_unit.decl 5 in 0 always hierarchy "second_reader"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk0 = simulation.context.storage %ctx[0] : !clock
      %clk1 = simulation.context.storage %ctx[1] : !clock
      %tmp0 = simulation.context.storage %ctx[2] : !ref
      %tmp1 = simulation.context.storage %ctx[3] : !ref
      %out0 = simulation.context.storage %ctx[4] : !ref
      %out1 = simulation.context.storage %ctx[5] : !ref
      %a = simulation.spawn @first_writer(%ctx, %clk0, %tmp0, %out0) : !simulation.context, !clock, !ref, !ref -> !simulation.process
      %b = simulation.spawn @first_reader(%ctx, %clk0, %tmp0, %out0) : !simulation.context, !clock, !ref, !ref -> !simulation.process
      %c = simulation.spawn @second_writer(%ctx, %clk1, %tmp1, %out1) : !simulation.context, !clock, !ref, !ref -> !simulation.process
      %d = simulation.spawn @second_reader(%ctx, %clk1, %tmp1, %out1) : !simulation.context, !clock, !ref, !ref -> !simulation.process
      simulation.return
    }
    simulation.func private @first_writer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clock {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %tmp: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      %value = arith.constant 11 : i32
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !clock
    ^body:
      simulation.ref.store %value to %tmp : i32, !ref
      %loaded = simulation.ref.load %tmp : !ref -> i32
      simulation.nba.enqueue %loaded to %out : (i32, !ref) -> ()
      cf.br ^wait
    }
    simulation.func private @first_reader(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clock {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %tmp: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %out: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !clock
    ^body:
      cf.br ^wait
    }
    simulation.func private @second_writer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clock {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %tmp: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}, %out: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 5 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64} {
      %value = arith.constant 22 : i32
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !clock
    ^body:
      simulation.ref.store %value to %tmp : i32, !ref
      %loaded = simulation.ref.load %tmp : !ref -> i32
      simulation.nba.enqueue %loaded to %out : (i32, !ref) -> ()
      cf.br ^wait
    }
    simulation.func private @second_reader(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !clock {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %tmp: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}, %out: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 5 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 5 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !clock
    ^body:
      cf.br ^wait
    }
// OBSERVER-BEGIN
    simulation.code_unit.decl 6 in 0 function hierarchy "observer"
    simulation.func private @observer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %tmp: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64}) -> i32 attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64} {
      %value = simulation.ref.load %tmp : !ref -> i32
      simulation.return %value : i32
    }
// OBSERVER-END
  }
}
