// RUN: obelisk-opt %s | FileCheck %s

module {
  simulation.design @chandle {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 function hierarchy "top.identity"

    // CHECK-LABEL: simulation.func private @identity(
    // CHECK-SAME: %[[VALUE:[^ ]+]]: !simulation.chandle
    // CHECK-SAME: -> !simulation.chandle
    simulation.func private @identity(
        %ctx: !simulation.context
          {simulation.capture_kind = 0 : i32},
        %value: !simulation.chandle
          {simulation.capture_kind = 1 : i32}) -> !simulation.chandle
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %null = simulation.chandle.null : !simulation.chandle
      // CHECK: simulation.chandle.equal %[[VALUE]], %{{.*}}
      %unused = simulation.chandle.equal %value, %null
        : !simulation.chandle
      simulation.return %value : !simulation.chandle
    }
  }
}
