// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph))' | FileCheck %s

// IEEE 1800-2017 4.9.5 and 28.8 require coordinated processing of a complete
// switch-connected net. A control derived from that net is feedback: preserve
// its change watch and self-resume edge so native scheduling can install
// static fanout for changes produced while resolving the pass component.
// CHECK: compute_graph = #obelisk_sim.graph<
// CHECK: #obelisk_sim.fragment<id = [[CONTROL:[0-9]+]], function = @control, block = 1
// CHECK-SAME: effect = watch, resource = net
// CHECK-SAME: descriptor = 0
// CHECK-SAME: trigger = change
// CHECK: source = [[CONTROL]], target = [[CONTROL]], kind = resume

module {
  obelisk_sim.design @pass_feedback {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.pass.decl 0 in 0 0[0] to 1[0] width 1 reversed = false {
      controlled = true
    }
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "top.root"
    obelisk_sim.code_unit.decl 2 in 0 continuous hierarchy "top.control"

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %pin = obelisk_sim.context.net %ctx[0] :
          !obelisk_sim.net<!obelisk_sim.logic<1>>
      %process = obelisk_sim.spawn @control(%ctx, %pin) :
          !obelisk_sim.context, !obelisk_sim.net<!obelisk_sim.logic<1>> ->
          !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @control(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %pin: !obelisk_sim.net<!obelisk_sim.logic<1>> {
          obelisk_sim.capture_kind = 4 : i32,
          obelisk_sim.descriptor_id = 0 : i64
        }) attributes {entry_kind = 7 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %value = obelisk_sim.net.read %pin :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      obelisk_sim.net.pass.control 0 = %value : !obelisk_sim.logic<1>
      obelisk_sim.suspend.change %pin to ^body :
          !obelisk_sim.net<!obelisk_sim.logic<1>>
    }
  }
}
