// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | FileCheck %s --check-prefix=BYTECODE

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  // The startup wildcard body writes and watches the same storage in separate
  // fragments of one process.  Its own write precedes the terminal wait and
  // cannot activate it, but a write from another process must retain its
  // sensitivity edge.
  // CHECK-LABEL: obelisk_sim.design @startup_wildcard_wait attributes {compute_graph = #obelisk_sim.graph<
  // CHECK-SAME: #obelisk_sim.fragment<id = [[PRODUCER:[0-9]+]], function = @producer, block = 0
  // CHECK-SAME: #obelisk_sim.fragment<id = [[WAIT:[0-9]+]], function = @wildcard, block = 1
  // CHECK-SAME: effect = watch
  // CHECK-SAME: #obelisk_sim.fragment<id = [[SELF:[0-9]+]], function = @wildcard, block = 2
  // CHECK-SAME: effect = write
  // CHECK-SAME: #obelisk_sim.edge<source = [[PRODUCER]], target = [[WAIT]], kind = sensitivity
  // CHECK-NOT: #obelisk_sim.edge<source = [[SELF]], target = [[WAIT]], kind = sensitivity
  // CHECK-SAME: regions =
  // The native wait record stores SUPPRESS_ACTIVE_SELF (bit 2) in its flags
  // word at byte offset 8.
  // NATIVE-LABEL: llvm.func @wildcard.__obelisk_coro_ramp
  // NATIVE: %[[FLAGS:.*]] = llvm.mlir.constant(4 : i32) : i32
  // NATIVE-NEXT: %[[FLAG_ADDRESS:.*]] = llvm.getelementptr %{{.*}}[8]
  // NATIVE-NEXT: llvm.store %[[FLAGS]], %[[FLAG_ADDRESS]]
  // The bytecode image preserves the same wait header: version, CHANGE kind,
  // flags, and watcher count.
  // BYTECODE: obelisk.bytecode.image = array<i8: {{.*}}1, 0, 0, 0, 2, 0, 0, 0, 4, 0, 0, 0, 1, 0, 0, 0
  obelisk_sim.design @startup_wildcard_wait {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer
        hierarchy "startup_wildcard_wait.root"
    obelisk_sim.code_unit.decl 2 in 0 always
        hierarchy "startup_wildcard_wait.wildcard"
    obelisk_sim.code_unit.decl 3 in 0 initial
        hierarchy "startup_wildcard_wait.producer"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %state = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %wildcard = obelisk_sim.spawn @wildcard(%ctx, %state) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>
          -> !obelisk_sim.process
      %producer = obelisk_sim.spawn @producer(%ctx, %state) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>
          -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func @wildcard(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %state: !obelisk_sim.ref<!obelisk_sim.logic<1>>
          {obelisk_sim.capture_kind = 3 : i32,
           obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.change %state to ^done
          {obelisk_sim.top_level_wildcard_wait} :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^done:
      %value = obelisk_sim.ref.load %state :
          !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      obelisk_sim.ref.store %value to %state : !obelisk_sim.logic<1>,
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      cf.br ^wait
    }

    obelisk_sim.func @producer(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %state: !obelisk_sim.ref<!obelisk_sim.logic<1>>
          {obelisk_sim.capture_kind = 3 : i32,
           obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %one = obelisk_sim.logic.constant true, false : !obelisk_sim.logic<1>
      obelisk_sim.ref.store %one to %state : !obelisk_sim.logic<1>,
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      obelisk_sim.return
    }
  }
}
