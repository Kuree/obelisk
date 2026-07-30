// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-specialize-static-state-nba),test-obelisk-static-specialization-analysis)' 2>&1 | FileCheck %s

module {
  obelisk_sim.design @static_specialization {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial
        hierarchy "static_specialization.process"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<8> design

    obelisk_sim.func @process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %destination: !obelisk_sim.ref<!obelisk_sim.logic<8>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %value = obelisk_sim.logic.constant 1 : i8, 0 : i8 :
          !obelisk_sim.logic<8>
      obelisk_sim.nba.enqueue %value to %destination :
          (!obelisk_sim.logic<8>,
           !obelisk_sim.ref<!obelisk_sim.logic<8>>) -> ()
      obelisk_sim.return
    }
  }
}

// CHECK: static-specialization present=true
// CHECK-NEXT: root 0 width=8 direct=true guarded=false nba=true
// CHECK-NEXT: nba-root 0
// CHECK-NEXT: nba-site [[SITE:[0-9]+]]
// CHECK-NEXT: nba-commit [[COMMIT:[0-9]+]] descriptor=0
