// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph),encode-obelisk-sim-to-bytecode{prune-native=true vpi=full})' | FileCheck %s
// RUN: sed '/^      %checkpoint =/a\      %delay = simulation.time.constant 1\n      simulation.suspend.delay %delay to ^resume\n    ^resume:\n      simulation.call @helper(%ctx) : (!simulation.context) -> ()' %s | obelisk-opt --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph),encode-obelisk-sim-to-bytecode{prune-native=true vpi=full})' | FileCheck %s --check-prefix=ROOT

// IEEE 1800-2023 4.5, 4.6: a checkpoint must retain its callable body.
// ROOT-LABEL: simulation.func @root
// ROOT-SAME: obelisk.bytecode.function = 0
// ROOT-LABEL: simulation.func @process
// ROOT-SAME: obelisk.bytecode.function = 1
// CHECK-LABEL: simulation.func @root
// CHECK-NOT: obelisk.bytecode.function
// CHECK-LABEL: simulation.func @process
// CHECK-NOT: obelisk.bytecode.function
// CHECK-LABEL: simulation.func @checkpoint
// CHECK-SAME: obelisk.bytecode.function = 0
// CHECK-LABEL: simulation.func private @helper
// CHECK-SAME: obelisk.bytecode.function = 1

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32
} {
  simulation.design @direct_generic {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "direct_generic.root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "direct_generic.process"
    simulation.code_unit.decl 3 in 0 initial hierarchy "direct_generic.checkpoint"
    simulation.code_unit.decl 4 in 0 function hierarchy "direct_generic.helper"
    simulation.code_unit.decl 5 in 0 function hierarchy "direct_generic.native_initializer"
    simulation.storage.decl 0 in 0 : !simulation.logic<128> design

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      simulation.call @native_initializer(%ctx) : (!simulation.context) -> ()
      %storage = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<128>>
      %process = simulation.spawn @process(%ctx, %storage) :
          !simulation.context, !simulation.ref<!simulation.logic<128>>
          -> !simulation.process
      %checkpoint = simulation.spawn @checkpoint(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }

    simulation.func @process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %state: !simulation.ref<!simulation.logic<128>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %value = simulation.ref.load %state :
          !simulation.ref<!simulation.logic<128>> -> !simulation.logic<128>
      simulation.ref.store %value to %state :
          !simulation.logic<128>, !simulation.ref<!simulation.logic<128>>
      simulation.return
    }
    simulation.func @checkpoint(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      simulation.call @helper(%ctx) : (!simulation.context) -> ()
      simulation.return
    }
    simulation.func private @helper(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %zero = arith.constant 0 : i32
      simulation.finish %ctx, %zero
      simulation.return
    }
    simulation.func private @native_initializer(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      simulation.return
    }
  }
}

