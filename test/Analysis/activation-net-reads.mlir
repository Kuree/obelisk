// RUN: obelisk-opt %s --test-obelisk-activation-regions='publications=true reaching-values=true' -o /dev/null 2>&1 | FileCheck %s
// Storage and net descriptor zero are distinct roots. Reading resolved net
// state does not imply permission to reorder its drivers or resolution.
module {
  simulation.design @net_reads {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : !simulation.logic<8> design
    simulation.storage.decl 0 in 0 : !simulation.logic<8> design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "copy"
    // CHECK-LABEL: regions @copy valid=1
    // CHECK-NEXT: node simulation.net.read read:s4611686018427387904[0,8)
    // CHECK-NEXT: node simulation.ref.store write:s0[0,8)
    // CHECK-NEXT: publication read:s4611686018427387904[0,8) write:s0[0,8)
    // CHECK: publication graph: acyclic
    simulation.func @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %net = simulation.context.net %ctx[0] : !simulation.net<!simulation.logic<8>>
      %out = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<8>>
      cf.br ^body
    ^body:
      %value = simulation.net.read %net : !simulation.net<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.ref.store %value to %out : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.suspend.change %net to ^body : !simulation.net<!simulation.logic<8>>
    }
  }
}
