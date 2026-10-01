// RUN: obelisk-opt %s --test-obelisk-semantic-roots -o /dev/null 2>&1 | FileCheck %s
// RUN: obelisk-opt %s --mlir-disable-threading --test-obelisk-semantic-roots -o /dev/null 2>&1 | FileCheck %s
// Suspension forwarding preserves a phi's semantic identity, even when its
// definition origin changes between iterations (LRM 6.21, 9.4).
module {
  simulation.design @roots {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "changing"
    simulation.code_unit.decl 2 in 0 initial hierarchy "forwarding"
    simulation.code_unit.decl 3 in 0 function hierarchy "mixed"
    simulation.func @changing(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: i32 {simulation.capture_kind = 2 : i32}, %repeat: i1 {simulation.capture_kind = 2 : i32}) attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %initial = arith.addi %input, %input {test.value = "initial"} : i32
      cf.br ^loop(%initial : i32)
    ^loop(%current: i32):
      // A changing phi is its own root, not its initial value.
      // CHECK: changing: block 1 argument 0
      %query = arith.addi %current, %current {test.root = "changing"} : i32
      cf.cond_br %repeat, ^wait, ^exit
    ^wait:
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^resume(%current : i32)
    ^resume(%restored: i32):
      // Its restored lane still represents that phi, not a new value.
      // CHECK: restored: block 1 argument 0
      %restored_query = arith.addi %restored, %restored {test.root = "restored"} : i32
      %updated = arith.addi %restored, %input {test.value = "updated"} : i32
      cf.br ^loop(%updated : i32)
    ^exit:
      simulation.return
    }
    simulation.func @forwarding(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: i32 {simulation.capture_kind = 2 : i32}) attributes {code_unit_id = 2 : i64, entry_kind = 1 : i32} {
      %initial = arith.addi %input, %input {test.value = "initial"} : i32
      cf.br ^loop(%initial : i32)
    ^loop(%current: i32):
      // CHECK: forwarding: initial
      %query = arith.addi %current, %current {test.root = "forwarding"} : i32
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^resume(%current : i32)
    ^resume(%restored: i32):
      // CHECK: forwarding_restored: initial
      %query_again = arith.addi %restored, %restored {test.root = "forwarding_restored"} : i32
      cf.br ^loop(%restored : i32)
    }
    simulation.func @mixed(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %a: i32 {simulation.capture_kind = 2 : i32}, %b: i32 {simulation.capture_kind = 2 : i32}, %choose: i1 {simulation.capture_kind = 2 : i32}) attributes {code_unit_id = 3 : i64, entry_kind = 8 : i32} {
      // Preserve both arms when the successor block is identical.
      cf.cond_br %choose, ^join(%a : i32), ^join(%b : i32)
    ^join(%selected: i32):
      // CHECK: mixed: block 1 argument 0
      %query = arith.addi %selected, %selected {test.root = "mixed"} : i32
      cf.br ^forward(%selected : i32)
    ^forward(%alias: i32):
      // CHECK: mixed_forward: block 1 argument 0
      %query_again = arith.addi %alias, %alias {test.root = "mixed_forward"} : i32
      simulation.return
    }
  }
}
