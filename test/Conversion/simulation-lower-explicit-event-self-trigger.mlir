// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-unit)))' | FileCheck %s

// IEEE 1800-2017 9.2.2: an always procedure repeats continuously. Distinguish
// its outer event control from nested procedural controls so an event enabled
// by one iteration can enqueue the next iteration.

!logic1 = !obelisk.integral<1, false, true, 0 : 0, logic>

module {
  obelisk_sim.design @explicit_event_self_trigger {
    obelisk_sim.code_unit.decl 9903001 in 0 always hierarchy "top.event_loop"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
        hierarchy "top.a"
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<1> design
        hierarchy "top.c"

    // CHECK-LABEL: obelisk_sim.func @event_loop
    // CHECK: obelisk_sim.suspend.any
    // CHECK-SAME: {obelisk_sim.procedural_event_wait, obelisk_sim.repeating_always_wait}
    obelisk_sim.func @event_loop(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %a: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %c: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 1 : i64})
        attributes {
          entry_kind = 3 : i32,
          obelisk_sim.bindings = [
            #obelisk_sim.argument_binding<path = "top.a", argument = 1,
                kind = direct, copyOut = false>,
            #obelisk_sim.argument_binding<path = "top.c", argument = 2,
                kind = direct, copyOut = false>
          ],
          obelisk_sim.delay_quantum = 1 : i64,
          obelisk_sim.delay_scale = 1 : i64,
          code_unit_id = 9903001 : i64
        } {
      obelisk.sv.statement.timed attributes {node_id = 1 : i64} {
        obelisk.sv.timing.event_list attributes {
            event_count = 2 : i64, node_id = 2 : i64} {
          obelisk.sv.timing.signal_event attributes {
              edge_kind = 0 : i32, has_iff = false, node_id = 3 : i64} {
            obelisk.sv.expression.named_value attributes {
                is_signed = false, node_id = 4 : i64,
                referenced_path = "top.a", referenced_symbol = @a,
                semantic_type = !logic1} {
            }
          }
          obelisk.sv.timing.signal_event attributes {
              edge_kind = 0 : i32, has_iff = false, node_id = 5 : i64} {
            obelisk.sv.expression.named_value attributes {
                is_signed = false, node_id = 6 : i64,
                referenced_path = "top.c", referenced_symbol = @c,
                semantic_type = !logic1} {
            }
          }
        }
        obelisk.sv.statement.expression_statement attributes {node_id = 7 : i64} {
          obelisk.sv.expression.assignment attributes {
              assignment_kind = 0 : i32, is_signed = false,
              node_id = 8 : i64, semantic_type = !logic1} {
            obelisk.sv.expression.named_value attributes {
                is_signed = false, node_id = 9 : i64,
                referenced_path = "top.a", referenced_symbol = @a,
                semantic_type = !logic1} {
            }
            obelisk.sv.expression.integer_literal attributes {
                constant_value = "1'b0", is_signed = false,
                node_id = 10 : i64, semantic_type = !logic1} {
            }
          }
        }
      }
      obelisk_sim.return
    }
  }
}
