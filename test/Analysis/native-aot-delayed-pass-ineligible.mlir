// RUN: obelisk-opt %s -o /dev/null \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),test-obelisk-native-aot-analysis)' \
// RUN:   2>&1 | FileCheck %s

// CHECK: native-aot eligible=false fully=false
// CHECK-NEXT: reason delayed switch contribution requires generic ordering

module {
  simulation.design @delayed_pass_ineligible {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : !simulation.logic<1> design
    simulation.net.decl 1 in 0 : !simulation.logic<1> design
    simulation.net.pass.decl 0 in 0 0[0] to 1[0] width 1 reversed = false {
      controlled = true
    }
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "initial"

    simulation.func @root(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %initial = simulation.spawn @initial(%ctx) :
          !simulation.context -> !simulation.process
      simulation.return
    }

    simulation.func @initial(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %one = simulation.logic.constant true, false : !simulation.logic<1>
      %on = simulation.time.constant 3
      %off = simulation.time.constant 5
      %unknown = simulation.time.constant 3
      simulation.net.pass.control_delayed 0 = %one
          after[%on, %off, %unknown] : !simulation.logic<1>
      simulation.return
    }
  }
}
