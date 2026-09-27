// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-lower-unit)))' | FileCheck %s

// IEEE 1800-2017 9.2.2: an always procedure repeats continuously. Distinguish
// its outer event control from nested procedural controls so an event enabled
// by one iteration can enqueue the next iteration.

!logic1 = !obelisk.integral<1, false, true, 0 : 0, logic>

module {
  simulation.design @explicit_event_self_trigger {
    simulation.code_unit.decl 9903001 in 0 always hierarchy "top.event_loop"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
        hierarchy "top.a"
    simulation.storage.decl 1 in 0 : !simulation.logic<1> design
        hierarchy "top.c"

    // CHECK-LABEL: simulation.func @event_loop
    // CHECK: simulation.suspend.any
    // CHECK-SAME: {schedule.procedural_event_wait, schedule.repeating_always_wait}
    simulation.func @event_loop(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %a: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64},
        %c: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 1 : i64})
        attributes {
          entry_kind = 3 : i32,
          simulation.bindings = [
            #simulation.argument_binding<path = "top.a", argument = 1,
                kind = direct, copyOut = false>,
            #simulation.argument_binding<path = "top.c", argument = 2,
                kind = direct, copyOut = false>
          ],
          simulation.delay_quantum = 1 : i64,
          simulation.delay_scale = 1 : i64,
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
      simulation.return
    }
  }
}
