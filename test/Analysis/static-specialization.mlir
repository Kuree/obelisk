// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-specialize-static-state-nba),test-obelisk-static-specialization-analysis)' 2>&1 | FileCheck %s

module {
  simulation.design @static_specialization {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial
        hierarchy "static_specialization.process"
    simulation.storage.decl 0 in 0 : !simulation.logic<8> design

    simulation.func @process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %destination: !simulation.ref<!simulation.logic<8>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %value = simulation.logic.constant 1 : i8, 0 : i8 :
          !simulation.logic<8>
      simulation.nba.enqueue %value to %destination :
          (!simulation.logic<8>,
           !simulation.ref<!simulation.logic<8>>) -> ()
      simulation.return
    }
  }
}

// CHECK: static-specialization present=true
// CHECK-NEXT: root 0 width=8 direct=true guarded=false nba=true
// CHECK-NEXT: nba-root 0
// CHECK-NEXT: nba-site [[SITE:[0-9]+]]
// CHECK-NEXT: nba-commit [[COMMIT:[0-9]+]] descriptor=0
