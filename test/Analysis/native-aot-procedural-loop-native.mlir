// RUN: obelisk-opt %s -o /dev/null \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),test-obelisk-native-aot-analysis)' \
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
  obelisk_sim.design @procedural_loop {
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 always_comb hierarchy "shift"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i8 design
    obelisk_sim.storage.decl 1 in 0 : i8 design

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %in = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i8>
      %work = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<i8>
      %process = obelisk_sim.spawn @shift(%ctx, %in, %work) :
          !obelisk_sim.context, !obelisk_sim.ref<i8>, !obelisk_sim.ref<i8>
          -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func @shift(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %in: !obelisk_sim.ref<i8>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %work: !obelisk_sim.ref<i8>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      cf.br ^activation
    ^activation:
      %seed = obelisk_sim.ref.load %in : !obelisk_sim.ref<i8> -> i8
      obelisk_sim.ref.store %seed to %work : i8, !obelisk_sim.ref<i8>
      cf.br ^head
    ^head:
      %value = obelisk_sim.ref.load %work : !obelisk_sim.ref<i8> -> i8
      %zero = arith.constant 0 : i8
      %more = arith.cmpi ne, %value, %zero : i8
      cf.cond_br %more, ^body, ^wait
    ^body:
      %one = arith.constant 1 : i8
      %shifted = arith.shrui %value, %one : i8
      obelisk_sim.ref.store %shifted to %work : i8, !obelisk_sim.ref<i8>
      cf.br ^head
    ^wait:
      obelisk_sim.suspend.any %in edges [0] to ^activation :
          !obelisk_sim.ref<i8>
    }
  }
}
