// RUN: obelisk-opt %s | FileCheck %s

module {
  obelisk_sim.design @chandle {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "top.identity"

    // CHECK-LABEL: obelisk_sim.func private @identity(
    // CHECK-SAME: %[[VALUE:[^ ]+]]: !obelisk_sim.chandle
    // CHECK-SAME: -> !obelisk_sim.chandle
    obelisk_sim.func private @identity(
        %ctx: !obelisk_sim.context
          {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.chandle
          {obelisk_sim.capture_kind = 1 : i32}) -> !obelisk_sim.chandle
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %null = obelisk_sim.chandle.null : !obelisk_sim.chandle
      // CHECK: obelisk_sim.chandle.equal %[[VALUE]], %{{.*}}
      %unused = obelisk_sim.chandle.equal %value, %null
        : !obelisk_sim.chandle
      obelisk_sim.return %value : !obelisk_sim.chandle
    }
  }
}
