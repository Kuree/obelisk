// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

!logic4 = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>
!logic8 = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>
!logic16 = !obelisk.ranged_packed_array<15 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>
!choice = !obelisk.source_aggregate<"top", false, true, false, false, false, false, 0, 16, 16, 0, [
  {name = "b", ordinal = 0 : i32, packed_offset = 0 : i64, type = !logic4},
  {name = "c", ordinal = 1 : i32, packed_offset = 0 : i64, type = !logic16}
]>
!record = !obelisk.source_aggregate<"top", false, false, false, false, false, false, 0, 24, 24, 0, [
  {name = "a", ordinal = 0 : i32, packed_offset = 0 : i64, type = !logic8},
  {name = "choice", ordinal = 1 : i32, packed_offset = 0 : i64, type = !choice}
]>

module {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32,
      hierarchical_name = "top", name = "top", node_id = 0 : i64
  } {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit",
        node_id = 2 : i64} {
    }
    obelisk.sv.symbol.instance @s3.top attributes {hierarchical_name = "top",
        is_uninstantiated = false, name = "top", node_id = 3 : i64,
        referenced_path = "top", referenced_symbol = @s0.top
    } {
      obelisk.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top",
          name = "top", node_id = 4 : i64,
          time_precision_fs = 1000000 : i64,
          time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable @s5.source attributes {hierarchical_name = "top.source",
            lifetime = 1 : i32, name = "source", node_id = 5 : i64,
            semantic_type = !obelisk.string} {
        }
        obelisk.sv.symbol.variable @s6.format attributes {hierarchical_name = "top.format",
            lifetime = 1 : i32, name = "format", node_id = 6 : i64,
            semantic_type = !obelisk.string} {
        }
        obelisk.sv.symbol.variable @s7.value attributes {hierarchical_name = "top.value",
            lifetime = 1 : i32, name = "value", node_id = 7 : i64,
            semantic_type = !record} {
        }
        obelisk.sv.symbol.procedural_block @s8 attributes {hierarchical_name = "top",
            node_id = 8 : i64, procedure_kind = 0 : i32,
            time_precision_fs = 1000000 : i64,
            time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 9 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 3 : i64,
                callee_name = "$sscanf", constraint_restrictions = [],
                defaulted_arguments = array<i64: 0, 0, 0>,
                has_inline_constraints = false, has_iterator_expression = false,
                has_output_arguments = true, has_this_class = false,
                is_super_class = false, is_system_call = true, node_id = 10 : i64,
                semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>,
                subroutine_kind = 0 : i32, system_library_cell = "work.top",
                system_scope_path = "top",
                system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
              obelisk.sv.expression.named_value attributes {node_id = 11 : i64,
                  referenced_path = "top.source",
                  referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.source,
                  semantic_type = !obelisk.string} {
              }
              obelisk.sv.expression.named_value attributes {node_id = 12 : i64,
                  referenced_path = "top.format",
                  referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.format,
                  semantic_type = !obelisk.string} {
              }
              obelisk.sv.expression.assignment attributes {
                  assignment_kind = 0 : i32, node_id = 13 : i64,
                  semantic_type = !record} {
                obelisk.sv.expression.named_value attributes {node_id = 14 : i64,
                    referenced_path = "top.value",
                    referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.value,
                    semantic_type = !record} {
                }
                obelisk.sv.expression.empty_argument attributes {
                    node_id = 15 : i64, semantic_type = !record} {
                }
              }
            }
          }
        }
      }
    }
  }
}

// CHECK: simulation.scan_dynamic_validate {{.*}} {allowed_specifiers = 34603008 : i64
// CHECK: simulation.string.scan_dynamic {{.*}} {allowed_specifiers = 34603008 : i64, finalize = false, raw_four_state_bytes = 16 : i64, raw_two_state_bytes = 8 : i64}
// CHECK: cf.switch
// CHECK: 9:
// CHECK: 10:
// CHECK-DAG: simulation.string.scan_raw
// CHECK-DAG: simulation.string.scan_raw
// CHECK-DAG: simulation.string.scan_raw
// CHECK-DAG: simulation.string.scan_raw
// CHECK-DAG: simulation.union.construct
// CHECK-DAG: simulation.union.construct
// CHECK-DAG: simulation.aggregate.construct
// CHECK-DAG: simulation.aggregate.construct
