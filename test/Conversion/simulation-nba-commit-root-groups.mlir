// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' | FileCheck %s
//
// Nine roots span a full group and a partial final group. Skipping the first
// group must still visit the ninth root; per-root selection remains intact.
// CHECK-LABEL: llvm.func internal @__obelisk_aot_static_nba_commit_two_state_fast_v1
// CHECK: llvm.mlir.constant(255 : i64)
// CHECK: llvm.and
// CHECK: llvm.icmp "eq"
// CHECK: llvm.cond_br {{.*}}, ^[[TAIL:bb[0-9]+]], ^[[FIRST:bb[0-9]+]]
// CHECK: ^[[FIRST]]:
// CHECK: llvm.lshr
// CHECK: llvm.cond_br
// CHECK: ^[[TAIL]]:
// CHECK: llvm.mlir.constant(256 : i64)
// CHECK: llvm.and
// CHECK: llvm.cond_br

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32
} {
  obelisk_sim.design @groups {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "groups.root"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "groups.writer"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<8> design
    obelisk_sim.storage.decl 2 in 0 : !obelisk_sim.logic<8> design
    obelisk_sim.storage.decl 3 in 0 : !obelisk_sim.logic<8> design
    obelisk_sim.storage.decl 4 in 0 : !obelisk_sim.logic<8> design
    obelisk_sim.storage.decl 5 in 0 : !obelisk_sim.logic<8> design
    obelisk_sim.storage.decl 6 in 0 : !obelisk_sim.logic<8> design
    obelisk_sim.storage.decl 7 in 0 : !obelisk_sim.logic<8> design
    obelisk_sim.storage.decl 8 in 0 : !obelisk_sim.logic<8> design
    obelisk_sim.storage.decl 9 in 0 : !obelisk_sim.logic<8> design
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %p = obelisk_sim.spawn @writer(%ctx, %clk) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func @writer(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clk: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.edge posedge %clk to ^write {site = #schedule.continuation<id = 1>} : !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^write:
      %value = obelisk_sim.logic.constant 42 : i8, 0 : i8 : !obelisk_sim.logic<8>
      %r1 = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.nba.enqueue %value to %r1 : (!obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>) -> ()
      %r2 = obelisk_sim.context.storage %ctx[2] : !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.nba.enqueue %value to %r2 : (!obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>) -> ()
      %r3 = obelisk_sim.context.storage %ctx[3] : !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.nba.enqueue %value to %r3 : (!obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>) -> ()
      %r4 = obelisk_sim.context.storage %ctx[4] : !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.nba.enqueue %value to %r4 : (!obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>) -> ()
      %r5 = obelisk_sim.context.storage %ctx[5] : !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.nba.enqueue %value to %r5 : (!obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>) -> ()
      %r6 = obelisk_sim.context.storage %ctx[6] : !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.nba.enqueue %value to %r6 : (!obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>) -> ()
      %r7 = obelisk_sim.context.storage %ctx[7] : !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.nba.enqueue %value to %r7 : (!obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>) -> ()
      %r8 = obelisk_sim.context.storage %ctx[8] : !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.nba.enqueue %value to %r8 : (!obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>) -> ()
      %r9 = obelisk_sim.context.storage %ctx[9] : !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.nba.enqueue %value to %r9 : (!obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>) -> ()
      cf.br ^wait
    }
  }
}
