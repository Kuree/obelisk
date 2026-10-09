// RUN: obelisk-opt %s '--test-obelisk-activation-regions=functions=request slice-root=1' -o /dev/null 2>&1 | FileCheck %s --check-prefix=SLICE
// RUN: obelisk-opt %s '--test-obelisk-activation-regions=functions=request slice-root=3' -o /dev/null 2>&1 | FileCheck %s --check-prefix=EFFECT
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,schedule-plan-combinational-statements{explain=true},obelisk-sim-materialize-compute-fusion,obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' -o %t 2>&1 | FileCheck %s --check-prefix=PLAN
// RUN: FileCheck %s < %t
// An unused break path has a dependent finishing sweep. Canonicalization
// rotates the empty sweep's increment into its test and merges the outer
// latch. The original activation's finite proof still permits discarding
// the entire effect-free region. The output slice must keep its effects.
// IEEE 1800-2023 4.3: removing this dead computation preserves visible values;
// the planner separately checks that intermediate transitions are unobservable.
// SLICE-LABEL: activation slice @request
// SLICE: simulation.ref.load
// SLICE-NOT: simulation.ref.load
// SLICE-NOT: arith.cmpi
// SLICE-NOT: cf.cond_br
// SLICE: simulation.ref.store
// SLICE-NEXT: simulation.return
// EFFECT-LABEL: activation slice @request
// EFFECT: arith.cmpi
// EFFECT: simulation.ref.store
// EFFECT: simulation.ref.load
// EFFECT: cf.cond_br
// PLAN: 1 plans (1 data-dependence order)
// CHECK: simulation.spawn @__obelisk_region_kernel_0_0
// CHECK-NOT: simulation.spawn
// CHECK: simulation.func private @__obelisk_region_kernel_0_0
// CHECK: simulation.suspend.any
module {
  simulation.design @dead_exit_loop {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : i1 design
    simulation.storage.decl 2 in 0 : i1 design
    simulation.storage.decl 3 in 0 : i1 design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "request"
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "response"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32} {
      %a = simulation.spawn @request(%ctx) : !simulation.context -> !simulation.process
      %b = simulation.spawn @response(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @request(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %in = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      %req = simulation.context.storage %ctx[1] : !simulation.ref<i1>
      %res = simulation.context.storage %ctx[2] : !simulation.ref<i1>
      %out = simulation.context.storage %ctx[3] : !simulation.ref<i1>
      %false = arith.constant false
      %true = arith.constant true
      %zero = arith.constant 0 : i8
      %one = arith.constant 1 : i8
      %four = arith.constant 4 : i8
      cf.br ^start
    ^start:
      %q = simulation.ref.load %in : !simulation.ref<i1> -> i1
      cf.br ^outer(%zero : i8)
    ^outer(%i: i8):
      %more = arith.cmpi slt, %i, %four : i8
      cf.cond_br %more, ^body, ^publish
    ^body:
      simulation.ref.store %q to %out : i1, !simulation.ref<i1>
      %stop = simulation.ref.load %res : !simulation.ref<i1> -> i1
      %nexti = arith.addi %i, %one : i8
      cf.cond_br %stop, ^finish(%nexti : i8), ^advance
    ^advance:
      simulation.ref.store %false to %out : i1, !simulation.ref<i1>
      cf.br ^outer(%nexti : i8)
    ^finish(%j: i8):
      %again = arith.cmpi slt, %j, %four : i8
      cf.cond_br %again, ^fill, ^publish
    ^fill:
      simulation.ref.store %true to %out : i1, !simulation.ref<i1>
      %nextj = arith.addi %j, %one : i8
      cf.br ^finish(%nextj : i8)
    ^publish:
      simulation.ref.store %q to %req : i1, !simulation.ref<i1>
      simulation.suspend.any %in, %res edges [0, 0] to ^start : !simulation.ref<i1>, !simulation.ref<i1>
    }
    simulation.func @response(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      %req = simulation.context.storage %ctx[1] : !simulation.ref<i1>
      %res = simulation.context.storage %ctx[2] : !simulation.ref<i1>
      cf.br ^body
    ^body:
      %v = simulation.ref.load %req : !simulation.ref<i1> -> i1
      simulation.ref.store %v to %res : i1, !simulation.ref<i1>
      simulation.suspend.change %req to ^body : !simulation.ref<i1>
    }
  }
}
