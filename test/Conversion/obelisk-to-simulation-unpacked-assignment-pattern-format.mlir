// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 21.2.1.7: %p prints an unpacked aggregate as an assignment
// pattern, traversing it until a singular element is reached -- structures
// with named elements, arrays without, and strings in quotes. The shape is
// static, so the punctuation becomes a format string and only the singular
// elements are passed as arguments.
module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "m", name = "m", node_id = 0 : i64, sym_name = "s0.m"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "m", is_uninstantiated = false, name = "m", node_id = 3 : i64, referenced_path = "m", referenced_symbol = @s0.m, sym_name = "s3.m"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "m", name = "m", node_id = 4 : i64, sym_name = "s4.m", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.type.type_alias attributes {hierarchical_name = "m.entry_t", name = "entry_t", node_id = 5 : i64, semantic_type = !obelisk.source_aggregate<"m", false, false, false, false, false, false, 0, 5, 4, 0, [{name = "label", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.string}, {name = "code", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}]>, sym_name = "s5.entry_t"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "m.entries", lifetime = 1 : i32, name = "entries", node_id = 6 : i64, semantic_type = !obelisk.ranged_unpacked_array<1 : 0 x !obelisk.source_aggregate<"m", false, false, false, false, false, false, 0, 5, 4, 0, [{name = "label", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.string}, {name = "code", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}]>>, sym_name = "s6.entries"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "m", node_id = 7 : i64, procedure_kind = 0 : i32, sym_name = "s7", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 8 : i64, source_range = !obelisk.source_range<"unpacked_pattern.sv", 4, 11, "unpacked_pattern.sv", 4, 35, "">} {
            obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 9 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.m", system_scope_path = "m", system_scope_symbol = @s1.$root::@s3.m::@s4.m} {
              obelisk.sv.expression.string_literal attributes {constant_value = "%p", node_id = 10 : i64, semantic_type = !obelisk.ranged_packed_array<15 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, source_range = !obelisk.source_range<"unpacked_pattern.sv", 4, 20, "unpacked_pattern.sv", 4, 24, "">} {
              }
              obelisk.sv.expression.named_value attributes {node_id = 11 : i64, referenced_path = "m.entries", referenced_symbol = @s1.$root::@s3.m::@s4.m::@s6.entries, semantic_type = !obelisk.ranged_unpacked_array<1 : 0 x !obelisk.source_aggregate<"m", false, false, false, false, false, false, 0, 5, 4, 0, [{name = "label", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.string}, {name = "code", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}]>>, source_range = !obelisk.source_range<"unpacked_pattern.sv", 4, 26, "unpacked_pattern.sv", 4, 33, "">} {
              }
            }
          }
        }
      }
    }
  }
}


// CHECK: %[[FORMAT:.*]] = obelisk_sim.bytes.constant "'{'{label:\22%p\22, code:%p}, '{label:\22%p\22, code:%p}}"
// CHECK: obelisk_sim.string.output_format %{{.*}}(%[[FORMAT]], %{{.*}}, %{{.*}}, %{{.*}}, %{{.*}})
