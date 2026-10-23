// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph),encode-obelisk-sim-to-bytecode{prune-native=true vpi=off})' | obelisk-opt --test-obelisk-print-bytecode=metadata=true -o /dev/null 2>&1 | FileCheck %s

// A generated eval body (code unit 3) is called only by the native dispatch,
// and a checkpoint resumes its owning actor, so the pruned image omits it.
// Other non-actor functions (code unit 4) stay conservatively.
// CHECK-NOT: id=3
// CHECK: function {{[0-9]+}}: id=4
// CHECK-NOT: id=3

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32
} {
  simulation.design @eval_body_scope {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "eval_body_scope.root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "eval_body_scope.process"
    simulation.code_unit.decl 3 in 0 function hierarchy "process.__obelisk_eval_body_0" debug "generated native eval body" {internal}
    simulation.code_unit.decl 4 in 0 function hierarchy "eval_body_scope.helper"
    simulation.storage.decl 0 in 0 : !simulation.logic<8> design

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %storage = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<8>>
      %process = simulation.spawn @process(%ctx, %storage) :
          !simulation.context, !simulation.ref<!simulation.logic<8>>
          -> !simulation.process
      simulation.return
    }

    simulation.func @process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %state: !simulation.ref<!simulation.logic<8>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %value = simulation.ref.load %state :
          !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.ref.store %value to %state :
          !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.return
    }

    simulation.func private @process.__obelisk_eval_body_0(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64,
                    schedule.eval.continuation = 1 : i32} {
      simulation.return
    }

    simulation.func private @helper(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      simulation.return
    }
  }
}
