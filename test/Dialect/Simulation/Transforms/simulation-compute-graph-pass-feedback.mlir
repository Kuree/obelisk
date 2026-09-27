// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph))' | FileCheck %s

// IEEE 1800-2017 4.9.5 and 28.8 require coordinated processing of a complete
// switch-connected net. A control derived from that net is feedback: preserve
// its change watch and self-resume edge so native scheduling can install
// static fanout for changes produced while resolving the pass component.
// CHECK: compute_graph = #schedule.graph<
// CHECK: #schedule.fragment<id = [[CONTROL:[0-9]+]], function = @control, block = 1
// CHECK-SAME: effect = watch, resource = net
// CHECK-SAME: descriptor = 0
// CHECK-SAME: trigger = change
// CHECK: source = [[CONTROL]], target = [[CONTROL]], kind = resume

module {
  simulation.design @pass_feedback {
    simulation.scope.decl 0 hierarchy "top"
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
    simulation.net.decl 1 in 0 : !simulation.logic<1> design
    simulation.net.pass.decl 0 in 0 0[0] to 1[0] width 1 reversed = false {
      controlled = true
    }
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "top.root"
    simulation.code_unit.decl 2 in 0 continuous hierarchy "top.control"

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %pin = simulation.context.net %ctx[0] :
          !simulation.net<!simulation.logic<1>>
      %process = simulation.spawn @control(%ctx, %pin) :
          !simulation.context, !simulation.net<!simulation.logic<1>> ->
          !simulation.process
      simulation.return
    }

    simulation.func private @control(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %pin: !simulation.net<!simulation.logic<1>> {
          simulation.capture_kind = 4 : i32,
          simulation.descriptor_id = 0 : i64
        }) attributes {entry_kind = 7 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %value = simulation.net.read %pin :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      simulation.net.pass.control 0 = %value : !simulation.logic<1>
      simulation.suspend.change %pin to ^body :
          !simulation.net<!simulation.logic<1>>
    }
  }
}
