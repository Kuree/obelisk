// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' | FileCheck %s --check-prefix=VPI
// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2023 9.4.5: a repeat event control counts each OR'd event that
// occurs, including several at the same simulation time. The wait carries
// what each event watches, and the resumption counts every term whose
// transition qualifies: posedge on the least significant bit, a change on the
// whole value.
//
// module top;
//   bit a;
//   logic [1:0] b;
//   int r;
//   initial r = repeat (2) @(posedge a or b) 1;
// endmodule

module attributes {coverage.language_version = 2023 : i32} {
  talos.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  talos.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    talos.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      talos.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        talos.sv.symbol.variable @s5.a attributes {hierarchical_name = "top.a", lifetime = 1 : i32, name = "a", node_id = 5 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
        }
        talos.sv.symbol.variable @s6.b attributes {hierarchical_name = "top.b", lifetime = 1 : i32, name = "b", node_id = 6 : i64, semantic_type = !talos.ranged_packed_array<1 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {
        }
        talos.sv.symbol.variable @s7.r attributes {hierarchical_name = "top.r", lifetime = 1 : i32, name = "r", node_id = 7 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
        }
        talos.sv.symbol.procedural_block @s8 attributes {hierarchical_name = "top", node_id = 8 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.expression_statement attributes {node_id = 9 : i64} {
            talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, has_timing_control = true, is_signed = true, node_id = 10 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
              talos.sv.timing.repeated_event attributes {node_id = 11 : i64} {
                talos.sv.expression.integer_literal attributes {constant_value = "2", is_declared_unsized = true, is_signed = true, node_id = 12 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                }
                talos.sv.timing.event_list attributes {event_count = 2 : i64, node_id = 13 : i64} {
                  talos.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 14 : i64} {
                    talos.sv.expression.named_value attributes {is_signed = false, node_id = 15 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.a, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
                    }
                  }
                  talos.sv.timing.signal_event attributes {edge_kind = 0 : i32, has_iff = false, node_id = 16 : i64} {
                    talos.sv.expression.named_value attributes {is_signed = false, node_id = 17 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.b, semantic_type = !talos.ranged_packed_array<1 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {
                    }
                  }
                }
              }
              talos.sv.expression.named_value attributes {is_signed = true, node_id = 18 : i64, referenced_path = "top.r", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.r, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
              }
              talos.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 19 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
              }
            }
          }
        }
      }
    }
  }
}


// CHECK-LABEL: simulation.func private @unit_0(
// CHECK-SAME: %[[A:[a-z0-9]+]]: !simulation.ref<i1>
// CHECK-SAME: %[[B:[a-z0-9]+]]: !simulation.ref<!simulation.packed_array<1 : 0 x !simulation.logic<1>>>
// CHECK: ^[[WAIT:bb[0-9]+]](%[[COUNT:.*]]: i64):
// CHECK: %[[A_OLD:.*]] = simulation.ref.load %[[A]]
// CHECK: %[[B_LOAD:.*]] = simulation.ref.load %[[B]]
// CHECK: %[[B_OLD:.*]] = simulation.packed.flatten %[[B_LOAD]]
// CHECK: simulation.suspend.any %[[A]], %[[B]], %[[COUNT]], %[[A_OLD]], %[[B_OLD]] edges [1, 0] to ^[[RESUME:bb[0-9]+]]
// CHECK: ^[[RESUME]](%[[LEFT:.*]]: i64, %[[A_PREV:.*]]: i1, %[[B_PREV:.*]]: !simulation.logic<2>):
// CHECK: %[[A_NEW:.*]] = simulation.ref.load %[[A]]
// CHECK: %[[A_LOW:.*]] = arith.xori %[[A_PREV]]
// CHECK: %[[ROSE:.*]] = arith.andi %[[A_LOW]], %[[A_NEW]]
// CHECK: %[[ROSE_COUNT:.*]] = arith.extui %[[ROSE]] : i1 to i64
// CHECK: %[[B_NEW:.*]] = simulation.packed.flatten
// CHECK: %[[CHANGED:.*]] = simulation.logic.compare case_ne %[[B_PREV]], %[[B_NEW]]
// CHECK: %[[CHANGED_COUNT:.*]] = arith.extui %[[CHANGED]] : i1 to i64
// CHECK: %[[SUM:.*]] = arith.addi %[[ROSE_COUNT]], %[[CHANGED_COUNT]]
// CHECK: %[[OCCURRED:.*]] = arith.maxui %[[SUM]]
// CHECK: %[[REMAINING:.*]] = arith.subi %[[LEFT]], %[[OCCURRED]]
// CHECK: %[[MORE:.*]] = arith.cmpi sgt, %[[REMAINING]]
// CHECK: cf.cond_br %[[MORE]], ^[[WAIT]](%[[REMAINING]] : i64)

// The assignment owns a repeat control, which owns its exact event control.
// No statement is invented for the repeat count or individual event operands.
// VPI: simulation.statement.decl [[ASSIGN:[0-9]+]] in [[UNIT:[0-9]+]] scope [[SCOPE:[0-9]+]] type 3
// VPI: simulation.vpi_thread_origin_binding [[UNIT]] origin [[ASSIGN]]
// VPI: simulation.statement.decl [[REPEAT:[0-9]+]] in [[UNIT]] scope [[SCOPE]] type 52 parent [[ASSIGN]]
// VPI: simulation.vpi_statement_relation.decl statement [[ASSIGN]] type 3 selector 52 ordinal 0 modes 1 to [[REPEAT]]
// VPI: simulation.statement.decl [[EVENT:[0-9]+]] in [[UNIT]] scope [[SCOPE]] type 13 parent [[REPEAT]]
// VPI: simulation.vpi_statement_relation.decl statement [[REPEAT]] type 52 selector 13 ordinal 0 modes 1 to [[EVENT]]
