// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,schedule-plan-combinational-statements{explain=true},obelisk-sim-materialize-compute-fusion,obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' -o %t 2>&1 | FileCheck %s --check-prefix=PLAN
// RUN: FileCheck %s < %t
// A join carries two values with different control dependence. Once the
// unrelated publication is sliced away, both response branches choose the
// same request. Treating the whole branch operand list as one SSA value, or
// retaining dead control flow, invents combinational feedback.
// PLAN: 1 plans (1 data-dependence order)
// CHECK: simulation.spawn @__obelisk_region_kernel_0_0
// CHECK-NOT: simulation.spawn
// CHECK: simulation.func private @__obelisk_region_kernel_0_0
// CHECK: simulation.suspend.any
module {
  simulation.design @control_lanes {
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
      cf.br ^body
    ^body:
      %state = simulation.ref.load %in : !simulation.ref<i1> -> i1
      cf.cond_br %state, ^join(%true, %false : i1, i1), ^read
    ^read:
      %r = simulation.ref.load %res : !simulation.ref<i1> -> i1
      cf.cond_br %r, ^join(%false, %true : i1, i1), ^join(%false, %false : i1, i1)
    ^join(%q: i1, %rout: i1):
      simulation.ref.store %rout to %out : i1, !simulation.ref<i1>
      simulation.ref.store %q to %req : i1, !simulation.ref<i1>
      simulation.suspend.any %in, %res edges [0, 0] to ^body : !simulation.ref<i1>, !simulation.ref<i1>
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
