// RUN: obelisk-opt %s -o /dev/null \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),test-obelisk-native-aot-analysis)' \
// RUN:   2>&1 | FileCheck %s

// A while-loop over storage has no bounded-induction proof, so the compute
// graph classifies its activation as a control-loop group. The loop never
// suspends: it runs inside one activation, which IEEE 1800-2023 4.7 permits
// a simulator to execute as one event. Compiled code finishes it exactly as
// the interpreter does, and a loop that cannot finish hangs either executor
// (12.7.6). The actor therefore stays native instead of becoming bytecode.
// CHECK: native-aot eligible=true fully=true
// CHECK-NEXT: actor 0 @root
// CHECK-NEXT: actor 1 @shift
// CHECK-NOT: bytecode
// CHECK-NOT: reason

module {
  simulation.design @procedural_loop {
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "shift"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design

    simulation.func @root(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %in = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %work = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %process = simulation.spawn @shift(%ctx, %in, %work) :
          !simulation.context, !simulation.ref<i8>, !simulation.ref<i8>
          -> !simulation.process
      simulation.return
    }

    simulation.func @shift(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %in: !simulation.ref<i8>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64},
        %work: !simulation.ref<i8>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      cf.br ^activation
    ^activation:
      %seed = simulation.ref.load %in : !simulation.ref<i8> -> i8
      simulation.ref.store %seed to %work : i8, !simulation.ref<i8>
      cf.br ^head
    ^head:
      %value = simulation.ref.load %work : !simulation.ref<i8> -> i8
      %zero = arith.constant 0 : i8
      %more = arith.cmpi ne, %value, %zero : i8
      cf.cond_br %more, ^body, ^wait
    ^body:
      %one = arith.constant 1 : i8
      %shifted = arith.shrui %value, %one : i8
      simulation.ref.store %shifted to %work : i8, !simulation.ref<i8>
      cf.br ^head
    ^wait:
      simulation.suspend.any %in edges [0] to ^activation :
          !simulation.ref<i8>
    }
  }
}
