// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=SHARED
// RUN: sed '/^\/\/ OBSERVER-BEGIN/,/^\/\/ OBSERVER-END/d' %s > %t.private.mlir
// RUN: obelisk-opt %t.private.mlir --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=PRIVATE

// Two consecutive fusions share the incremental identity/escape index.
// Removing old actors and replacing their root spawns makes each temporary
// exclusive to its new group. An independent accessor must still prevent
// deleting the second group's canonical store. Removing that accessor permits
// promotion in both groups, including the one indexed before it was fused.
// SHARED-LABEL: obelisk_sim.func private @__obelisk_fused_0
// SHARED-NOT: obelisk_sim.ref.store
// SHARED-NOT: obelisk_sim.ref.load
// SHARED: obelisk_sim.nba.enqueue
// SHARED-LABEL: obelisk_sim.func private @__obelisk_fused_1
// SHARED: obelisk_sim.ref.store
// SHARED: obelisk_sim.ref.load
// SHARED: obelisk_sim.nba.enqueue
// PRIVATE-LABEL: obelisk_sim.func private @__obelisk_fused_0
// PRIVATE-NOT: obelisk_sim.ref.store
// PRIVATE-NOT: obelisk_sim.ref.load
// PRIVATE: obelisk_sim.nba.enqueue
// PRIVATE-LABEL: obelisk_sim.func private @__obelisk_fused_1
// PRIVATE-NOT: obelisk_sim.ref.store
// PRIVATE-NOT: obelisk_sim.ref.load
// PRIVATE: obelisk_sim.nba.enqueue

!ref = !obelisk_sim.ref<i32>
!clock = !obelisk_sim.ref<i1>
module attributes {obelisk.native_scheduler = 1 : i32} {
  obelisk_sim.design @storage_index {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i1 design
    obelisk_sim.storage.decl 1 in 0 : i1 design
    obelisk_sim.storage.decl 2 in 0 : i32 static
    obelisk_sim.storage.decl 3 in 0 : i32 static
    obelisk_sim.storage.decl 4 in 0 : i32 design
    obelisk_sim.storage.decl 5 in 0 : i32 design
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "first_writer"
    obelisk_sim.code_unit.decl 3 in 0 always hierarchy "first_reader"
    obelisk_sim.code_unit.decl 4 in 0 always hierarchy "second_writer"
    obelisk_sim.code_unit.decl 5 in 0 always hierarchy "second_reader"
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk0 = obelisk_sim.context.storage %ctx[0] : !clock
      %clk1 = obelisk_sim.context.storage %ctx[1] : !clock
      %tmp0 = obelisk_sim.context.storage %ctx[2] : !ref
      %tmp1 = obelisk_sim.context.storage %ctx[3] : !ref
      %out0 = obelisk_sim.context.storage %ctx[4] : !ref
      %out1 = obelisk_sim.context.storage %ctx[5] : !ref
      %a = obelisk_sim.spawn @first_writer(%ctx, %clk0, %tmp0, %out0) : !obelisk_sim.context, !clock, !ref, !ref -> !obelisk_sim.process
      %b = obelisk_sim.spawn @first_reader(%ctx, %clk0, %tmp0, %out0) : !obelisk_sim.context, !clock, !ref, !ref -> !obelisk_sim.process
      %c = obelisk_sim.spawn @second_writer(%ctx, %clk1, %tmp1, %out1) : !obelisk_sim.context, !clock, !ref, !ref -> !obelisk_sim.process
      %d = obelisk_sim.spawn @second_reader(%ctx, %clk1, %tmp1, %out1) : !obelisk_sim.context, !clock, !ref, !ref -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func private @first_writer(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %clk: !clock {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}, %tmp: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64}, %out: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 4 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      %value = arith.constant 11 : i32
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.edge posedge %clk to ^body : !clock
    ^body:
      obelisk_sim.ref.store %value to %tmp : i32, !ref
      %loaded = obelisk_sim.ref.load %tmp : !ref -> i32
      obelisk_sim.nba.enqueue %loaded to %out : (i32, !ref) -> ()
      cf.br ^wait
    }
    obelisk_sim.func private @first_reader(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %clk: !clock {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}, %tmp: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64}, %out: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 4 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.edge posedge %clk to ^body : !clock
    ^body:
      cf.br ^wait
    }
    obelisk_sim.func private @second_writer(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %clk: !clock {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64}, %tmp: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 3 : i64}, %out: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 5 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64} {
      %value = arith.constant 22 : i32
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.edge posedge %clk to ^body : !clock
    ^body:
      obelisk_sim.ref.store %value to %tmp : i32, !ref
      %loaded = obelisk_sim.ref.load %tmp : !ref -> i32
      obelisk_sim.nba.enqueue %loaded to %out : (i32, !ref) -> ()
      cf.br ^wait
    }
    obelisk_sim.func private @second_reader(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %clk: !clock {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64}, %tmp: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 3 : i64}, %out: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 5 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 5 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.edge posedge %clk to ^body : !clock
    ^body:
      cf.br ^wait
    }
// OBSERVER-BEGIN
    obelisk_sim.code_unit.decl 6 in 0 function hierarchy "observer"
    obelisk_sim.func private @observer(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %tmp: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 3 : i64}) -> i32 attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64} {
      %value = obelisk_sim.ref.load %tmp : !ref -> i32
      obelisk_sim.return %value : i32
    }
// OBSERVER-END
  }
}
