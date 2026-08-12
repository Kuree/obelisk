// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "simulation_aggregates", name = "simulation_aggregates", node_id = 0 : i64, sym_name = "s0.simulation_aggregates"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "simulation_aggregates", is_uninstantiated = false, name = "simulation_aggregates", node_id = 3 : i64, referenced_path = "simulation_aggregates", referenced_symbol = @s0.simulation_aggregates, sym_name = "s3.simulation_aggregates"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "simulation_aggregates", name = "simulation_aggregates", node_id = 4 : i64, sym_name = "s4.simulation_aggregates"} {
        obelisk.sv.type.type_alias attributes {hierarchical_name = "simulation_aggregates.packed_record_t", name = "packed_record_t", node_id = 5 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", true, false, false, false, true, false, 5, 5, 5, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 1 : i64, type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>, sym_name = "s5.packed_record_t"} {
        }
        obelisk.sv.type.type_alias attributes {hierarchical_name = "simulation_aggregates.unpacked_record_t", name = "unpacked_record_t", node_id = 6 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>, sym_name = "s6.unpacked_record_t"} {
        }
        obelisk.sv.type.type_alias attributes {hierarchical_name = "simulation_aggregates.word_array_t", name = "word_array_t", node_id = 7 : i64, semantic_type = !obelisk.ranged_unpacked_array<3 : 1 x !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>, sym_name = "s7.word_array_t"} {
        }
        obelisk.sv.type.type_alias attributes {hierarchical_name = "simulation_aggregates.ascending_word_array_t", name = "ascending_word_array_t", node_id = 8 : i64, semantic_type = !obelisk.ranged_unpacked_array<-1 : 1 x !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>, sym_name = "s8.ascending_word_array_t"} {
        }
        obelisk.sv.type.type_alias attributes {hierarchical_name = "simulation_aggregates.packed_union_t", name = "packed_union_t", node_id = 9 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", true, true, false, false, true, false, 8, 8, 8, 0, [{name = "logic_word", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "bit_word", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>}]>, sym_name = "s9.packed_union_t"} {
        }
        obelisk.sv.type.type_alias attributes {hierarchical_name = "simulation_aggregates.unpacked_union_t", name = "unpacked_union_t", node_id = 10 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, true, false, false, false, false, 0, 32, 32, 0, [{name = "byte_value", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "int_value", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>, sym_name = "s10.unpacked_union_t"} {
        }
        obelisk.sv.type.type_alias attributes {hierarchical_name = "simulation_aggregates.tagged_packed_union_t", name = "tagged_packed_union_t", node_id = 11 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", true, true, true, false, true, false, 18, 18, 18, 2, [{name = "byte_value", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "word_value", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<15 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "nibble_value", ordinal = 2 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}]>, sym_name = "s11.tagged_packed_union_t"} {
        }
        obelisk.sv.type.type_alias attributes {hierarchical_name = "simulation_aggregates.tagged_unpacked_union_t", name = "tagged_unpacked_union_t", node_id = 12 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, true, true, false, false, false, 0, 32, 32, 0, [{name = "byte_value", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "int_value", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>, sym_name = "s12.tagged_unpacked_union_t"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_aggregates.packed_record", lifetime = 1 : i32, name = "packed_record", node_id = 13 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", true, false, false, false, true, false, 5, 5, 5, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 1 : i64, type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>, sym_name = "s13.packed_record"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_aggregates.unpacked_record", lifetime = 1 : i32, name = "unpacked_record", node_id = 14 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>, sym_name = "s14.unpacked_record"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_aggregates.copied_record", lifetime = 1 : i32, name = "copied_record", node_id = 15 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>, sym_name = "s15.copied_record"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_aggregates.output_record", lifetime = 1 : i32, name = "output_record", node_id = 16 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>, sym_name = "s16.output_record"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_aggregates.words", lifetime = 1 : i32, name = "words", node_id = 17 : i64, semantic_type = !obelisk.ranged_unpacked_array<3 : 1 x !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>, sym_name = "s17.words"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_aggregates.ascending_words", lifetime = 1 : i32, name = "ascending_words", node_id = 18 : i64, semantic_type = !obelisk.ranged_unpacked_array<-1 : 1 x !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>, sym_name = "s18.ascending_words"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_aggregates.selected", lifetime = 1 : i32, name = "selected", node_id = 19 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s19.selected"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_aggregates.index", lifetime = 1 : i32, name = "index", node_id = 20 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s20.index"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_aggregates.packed_choice", lifetime = 1 : i32, name = "packed_choice", node_id = 21 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", true, true, false, false, true, false, 8, 8, 8, 0, [{name = "logic_word", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "bit_word", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>}]>, sym_name = "s21.packed_choice"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_aggregates.unpacked_choice", lifetime = 1 : i32, name = "unpacked_choice", node_id = 22 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, true, false, false, false, false, 0, 32, 32, 0, [{name = "byte_value", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "int_value", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>, sym_name = "s22.unpacked_choice"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_aggregates.tagged_packed_choice", lifetime = 1 : i32, name = "tagged_packed_choice", node_id = 23 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", true, true, true, false, true, false, 18, 18, 18, 2, [{name = "byte_value", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "word_value", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<15 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "nibble_value", ordinal = 2 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}]>, sym_name = "s23.tagged_packed_choice"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_aggregates.tagged_unpacked_choice", lifetime = 1 : i32, name = "tagged_unpacked_choice", node_id = 24 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, true, true, false, false, false, 0, 32, 32, 0, [{name = "byte_value", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "int_value", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>, sym_name = "s24.tagged_unpacked_choice"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_aggregates.tagged_left", lifetime = 1 : i32, name = "tagged_left", node_id = 25 : i64, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.source_aggregate<"simulation_aggregates", false, true, true, false, false, false, 0, 32, 32, 0, [{name = "byte_value", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "int_value", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>>, sym_name = "s25.tagged_left"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_aggregates.tagged_right", lifetime = 1 : i32, name = "tagged_right", node_id = 26 : i64, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.source_aggregate<"simulation_aggregates", false, true, true, false, false, false, 0, 32, 32, 0, [{name = "byte_value", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "int_value", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>>, sym_name = "s26.tagged_right"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_aggregates.tagged_merged", lifetime = 1 : i32, name = "tagged_merged", node_id = 27 : i64, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.source_aggregate<"simulation_aggregates", false, true, true, false, false, false, 0, 32, 32, 0, [{name = "byte_value", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "int_value", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>>, sym_name = "s27.tagged_merged"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_aggregates.ambiguous", lifetime = 1 : i32, name = "ambiguous", node_id = 28 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s28.ambiguous"} {
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "simulation_aggregates.copy_record", name = "copy_record", node_id = 29 : i64, return_variable_path = "simulation_aggregates.copy_record.copy_record", return_variable_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s29.copy_record::@s31.copy_record, semantic_type = !obelisk.subroutine<(!obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>) -> !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>, false>, subroutine_kind = 0 : i32, sym_name = "s29.copy_record", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 30 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 31 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>} {
              obelisk.sv.expression.named_value attributes {node_id = 32 : i64, referenced_path = "simulation_aggregates.copy_record.copy_record", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s29.copy_record::@s31.copy_record, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>} {
              }
              obelisk.sv.expression.named_value attributes {node_id = 33 : i64, referenced_path = "simulation_aggregates.copy_record.value", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s29.copy_record::@s30.value, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>} {
              }
            }
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "simulation_aggregates.copy_record.value", name = "value", node_id = 34 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>, sym_name = "s30.value"} {
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_aggregates.copy_record.copy_record", is_compiler_generated, name = "copy_record", node_id = 35 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>, sym_name = "s31.copy_record"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "simulation_aggregates.copy_update", name = "copy_update", node_id = 36 : i64, return_variable_path = "simulation_aggregates.copy_update.copy_update", return_variable_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s32.copy_update::@s36.copy_update, semantic_type = !obelisk.subroutine<(!obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>, !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>, !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>) -> !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>, false>, subroutine_kind = 0 : i32, sym_name = "s32.copy_update", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 37 : i64} {
            obelisk.sv.statement.expression_statement attributes {node_id = 38 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 39 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>} {
                obelisk.sv.expression.named_value attributes {node_id = 40 : i64, referenced_path = "simulation_aggregates.copy_update.output_value", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s32.copy_update::@s34.output_value, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 41 : i64, referenced_path = "simulation_aggregates.copy_update.value", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s32.copy_update::@s33.value, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 42 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 43 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>} {
                obelisk.sv.expression.named_value attributes {node_id = 44 : i64, referenced_path = "simulation_aggregates.copy_update.inout_value", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s32.copy_update::@s35.inout_value, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 45 : i64, referenced_path = "simulation_aggregates.copy_update.value", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s32.copy_update::@s33.value, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 46 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 47 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>} {
                obelisk.sv.expression.named_value attributes {node_id = 48 : i64, referenced_path = "simulation_aggregates.copy_update.copy_update", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s32.copy_update::@s36.copy_update, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 49 : i64, referenced_path = "simulation_aggregates.copy_update.value", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s32.copy_update::@s33.value, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>} {
                }
              }
            }
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "simulation_aggregates.copy_update.value", name = "value", node_id = 50 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>, sym_name = "s33.value"} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 1 : i32, hierarchical_name = "simulation_aggregates.copy_update.output_value", name = "output_value", node_id = 51 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>, sym_name = "s34.output_value"} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 2 : i32, hierarchical_name = "simulation_aggregates.copy_update.inout_value", name = "inout_value", node_id = 52 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>, sym_name = "s35.inout_value"} {
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "simulation_aggregates.copy_update.copy_update", is_compiler_generated, name = "copy_update", node_id = 53 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>, sym_name = "s36.copy_update"} {
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "simulation_aggregates", node_id = 54 : i64, procedure_kind = 0 : i32, sym_name = "s37", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 55 : i64} {
            obelisk.sv.statement.list attributes {node_id = 56 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 57 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 58 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>} {
                  obelisk.sv.expression.named_value attributes {node_id = 59 : i64, referenced_path = "simulation_aggregates.unpacked_record", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s14.unpacked_record, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>} {
                  }
                  obelisk.sv.expression.simple_assignment_pattern attributes {node_id = 60 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>} {
                    obelisk.sv.expression.conversion attributes {node_id = 61 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "8'd16", node_id = 62 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                      }
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "1'b0", node_id = 63 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    }
                  }
                }
              }
              // Named structure setters are stored in source order. The
              // explicit ordinals preserve their declaration-order targets.
              obelisk.sv.statement.expression_statement attributes {node_id = 300 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 301 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", true, false, false, false, true, false, 5, 5, 5, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 1 : i64, type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>} {
                  obelisk.sv.expression.named_value attributes {node_id = 302 : i64, referenced_path = "simulation_aggregates.packed_record", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s13.packed_record, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", true, false, false, false, true, false, 5, 5, 5, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 1 : i64, type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>} {
                  }
                  obelisk.sv.expression.structured_assignment_pattern attributes {has_default_setter = false, index_setter_count = 0 : i64, member_setter_count = 2 : i64, member_setter_ordinals = array<i64: 1, 0>, node_id = 303 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", true, false, false, false, true, false, 5, 5, 5, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 1 : i64, type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>, type_setter_count = 0 : i64} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 304 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    }
                    obelisk.sv.expression.conversion attributes {node_id = 305 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "4'b0110", node_id = 306 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 64 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 65 : i64, semantic_type = !obelisk.ranged_unpacked_array<3 : 1 x !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>} {
                  obelisk.sv.expression.named_value attributes {node_id = 66 : i64, referenced_path = "simulation_aggregates.words", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s17.words, semantic_type = !obelisk.ranged_unpacked_array<3 : 1 x !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>} {
                  }
                  obelisk.sv.expression.simple_assignment_pattern attributes {node_id = 67 : i64, semantic_type = !obelisk.ranged_unpacked_array<3 : 1 x !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>} {
                    obelisk.sv.expression.conversion attributes {node_id = 68 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "8'd17", node_id = 69 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                      }
                    }
                    obelisk.sv.expression.conversion attributes {node_id = 70 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "8'd34", node_id = 71 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                      }
                    }
                    obelisk.sv.expression.conversion attributes {node_id = 72 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "8'd51", node_id = 73 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 74 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 75 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.member_access attributes {field_ordinal = 0 : i64, node_id = 76 : i64, packed_offset = 1 : i64, referenced_path = "simulation_aggregates.payload", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s47::@s48.payload, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.named_value attributes {node_id = 77 : i64, referenced_path = "simulation_aggregates.packed_record", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s13.packed_record, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", true, false, false, false, true, false, 5, 5, 5, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 1 : i64, type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>} {
                    }
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 78 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "4'b1010", node_id = 79 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 80 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 81 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  obelisk.sv.expression.member_access attributes {field_ordinal = 1 : i64, node_id = 82 : i64, packed_offset = 0 : i64, referenced_path = "simulation_aggregates.valid", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s47::@s49.valid, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.named_value attributes {node_id = 83 : i64, referenced_path = "simulation_aggregates.packed_record", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s13.packed_record, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", true, false, false, false, true, false, 5, 5, 5, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 1 : i64, type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>} {
                    }
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 84 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 85 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 86 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.member_access attributes {field_ordinal = 0 : i64, node_id = 87 : i64, packed_offset = 0 : i64, referenced_path = "simulation_aggregates.payload", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s44::@s45.payload, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.named_value attributes {node_id = 88 : i64, referenced_path = "simulation_aggregates.unpacked_record", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s14.unpacked_record, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>} {
                    }
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 89 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "8'd42", node_id = 90 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 91 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 92 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  obelisk.sv.expression.member_access attributes {field_ordinal = 1 : i64, node_id = 93 : i64, packed_offset = 8 : i64, referenced_path = "simulation_aggregates.valid", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s44::@s46.valid, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.named_value attributes {node_id = 94 : i64, referenced_path = "simulation_aggregates.unpacked_record", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s14.unpacked_record, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>} {
                    }
                  }
                  obelisk.sv.expression.member_access attributes {field_ordinal = 1 : i64, node_id = 95 : i64, packed_offset = 0 : i64, referenced_path = "simulation_aggregates.valid", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s47::@s49.valid, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.named_value attributes {node_id = 96 : i64, referenced_path = "simulation_aggregates.packed_record", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s13.packed_record, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", true, false, false, false, true, false, 5, 5, 5, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 1 : i64, type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 97 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 98 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.element_select attributes {node_id = 99 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.named_value attributes {node_id = 100 : i64, referenced_path = "simulation_aggregates.words", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s17.words, semantic_type = !obelisk.ranged_unpacked_array<3 : 1 x !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "3", node_id = 101 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.expression.member_access attributes {field_ordinal = 0 : i64, node_id = 102 : i64, packed_offset = 0 : i64, referenced_path = "simulation_aggregates.payload", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s44::@s45.payload, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.named_value attributes {node_id = 103 : i64, referenced_path = "simulation_aggregates.unpacked_record", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s14.unpacked_record, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 104 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 105 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.element_select attributes {node_id = 106 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.named_value attributes {node_id = 107 : i64, referenced_path = "simulation_aggregates.words", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s17.words, semantic_type = !obelisk.ranged_unpacked_array<3 : 1 x !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "2", node_id = 108 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 109 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "8'd85", node_id = 110 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 111 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 112 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.element_select attributes {node_id = 113 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.named_value attributes {node_id = 114 : i64, referenced_path = "simulation_aggregates.words", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s17.words, semantic_type = !obelisk.ranged_unpacked_array<3 : 1 x !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>} {
                    }
                    obelisk.sv.expression.named_value attributes {node_id = 115 : i64, referenced_path = "simulation_aggregates.index", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s20.index, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 116 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "8'd119", node_id = 117 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 118 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 119 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.named_value attributes {node_id = 120 : i64, referenced_path = "simulation_aggregates.selected", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s19.selected, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                  obelisk.sv.expression.element_select attributes {node_id = 121 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.named_value attributes {node_id = 122 : i64, referenced_path = "simulation_aggregates.words", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s17.words, semantic_type = !obelisk.ranged_unpacked_array<3 : 1 x !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>} {
                    }
                    obelisk.sv.expression.named_value attributes {node_id = 123 : i64, referenced_path = "simulation_aggregates.index", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s20.index, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 124 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 125 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.element_select attributes {node_id = 126 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.named_value attributes {node_id = 127 : i64, referenced_path = "simulation_aggregates.ascending_words", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s18.ascending_words, semantic_type = !obelisk.ranged_unpacked_array<-1 : 1 x !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>} {
                    }
                    obelisk.sv.expression.unary_op attributes {node_id = 128 : i64, operator_kind = 1 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 129 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 130 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "8'd136", node_id = 131 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 132 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 133 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.element_select attributes {node_id = 134 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.named_value attributes {node_id = 135 : i64, referenced_path = "simulation_aggregates.ascending_words", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s18.ascending_words, semantic_type = !obelisk.ranged_unpacked_array<-1 : 1 x !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 136 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.expression.named_value attributes {node_id = 137 : i64, referenced_path = "simulation_aggregates.selected", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s19.selected, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 138 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 139 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.member_access attributes {field_ordinal = 0 : i64, node_id = 140 : i64, packed_offset = 0 : i64, referenced_path = "simulation_aggregates.logic_word", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s41::@s42.logic_word, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.named_value attributes {node_id = 141 : i64, referenced_path = "simulation_aggregates.packed_choice", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s21.packed_choice, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", true, true, false, false, true, false, 8, 8, 8, 0, [{name = "logic_word", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "bit_word", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>}]>} {
                    }
                  }
                  obelisk.sv.expression.named_value attributes {node_id = 142 : i64, referenced_path = "simulation_aggregates.selected", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s19.selected, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 143 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 144 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.member_access attributes {field_ordinal = 1 : i64, node_id = 145 : i64, packed_offset = 0 : i64, referenced_path = "simulation_aggregates.int_value", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s38::@s40.int_value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.named_value attributes {node_id = 146 : i64, referenced_path = "simulation_aggregates.unpacked_choice", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s22.unpacked_choice, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, true, false, false, false, false, 0, 32, 32, 0, [{name = "byte_value", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "int_value", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>} {
                    }
                  }
                  obelisk.sv.expression.named_value attributes {node_id = 147 : i64, referenced_path = "simulation_aggregates.index", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s20.index, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 148 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 149 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.named_value attributes {node_id = 150 : i64, referenced_path = "simulation_aggregates.selected", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s19.selected, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                  obelisk.sv.expression.member_access attributes {field_ordinal = 0 : i64, node_id = 151 : i64, packed_offset = 0 : i64, referenced_path = "simulation_aggregates.byte_value", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s38::@s39.byte_value, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.named_value attributes {node_id = 152 : i64, referenced_path = "simulation_aggregates.unpacked_choice", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s22.unpacked_choice, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, true, false, false, false, false, 0, 32, 32, 0, [{name = "byte_value", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "int_value", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 153 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 154 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", true, true, true, false, true, false, 18, 18, 18, 2, [{name = "byte_value", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "word_value", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<15 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "nibble_value", ordinal = 2 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}]>} {
                  obelisk.sv.expression.named_value attributes {node_id = 155 : i64, referenced_path = "simulation_aggregates.tagged_packed_choice", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s23.tagged_packed_choice, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", true, true, true, false, true, false, 18, 18, 18, 2, [{name = "byte_value", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "word_value", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<15 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "nibble_value", ordinal = 2 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}]>} {
                  }
                  obelisk.sv.expression.tagged_union attributes {field_ordinal = 1 : i64, node_id = 156 : i64, packed_offset = 0 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", true, true, true, false, true, false, 18, 18, 18, 2, [{name = "byte_value", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "word_value", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<15 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "nibble_value", ordinal = 2 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}]>} {
                    obelisk.sv.expression.conversion attributes {node_id = 157 : i64, semantic_type = !obelisk.ranged_packed_array<15 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "16'd4660", node_id = 158 : i64, semantic_type = !obelisk.ranged_packed_array<15 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 159 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 160 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, true, true, false, false, false, 0, 32, 32, 0, [{name = "byte_value", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "int_value", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>} {
                  obelisk.sv.expression.named_value attributes {node_id = 161 : i64, referenced_path = "simulation_aggregates.tagged_unpacked_choice", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s24.tagged_unpacked_choice, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, true, true, false, false, false, 0, 32, 32, 0, [{name = "byte_value", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "int_value", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>} {
                  }
                  obelisk.sv.expression.tagged_union attributes {field_ordinal = 1 : i64, node_id = 162 : i64, packed_offset = 0 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, true, true, false, false, false, 0, 32, 32, 0, [{name = "byte_value", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "int_value", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>} {
                    obelisk.sv.expression.named_value attributes {node_id = 163 : i64, referenced_path = "simulation_aggregates.index", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s20.index, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 164 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 165 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.named_value attributes {node_id = 166 : i64, referenced_path = "simulation_aggregates.ambiguous", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s28.ambiguous, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1'bx", node_id = 167 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 168 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 169 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, true, true, false, false, false, 0, 32, 32, 0, [{name = "byte_value", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "int_value", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>} {
                  obelisk.sv.expression.element_select attributes {node_id = 170 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, true, true, false, false, false, 0, 32, 32, 0, [{name = "byte_value", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "int_value", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>} {
                    obelisk.sv.expression.named_value attributes {node_id = 171 : i64, referenced_path = "simulation_aggregates.tagged_left", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s25.tagged_left, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.source_aggregate<"simulation_aggregates", false, true, true, false, false, false, 0, 32, 32, 0, [{name = "byte_value", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "int_value", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 172 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.expression.tagged_union attributes {field_ordinal = 1 : i64, node_id = 173 : i64, packed_offset = 0 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, true, true, false, false, false, 0, 32, 32, 0, [{name = "byte_value", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "int_value", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "7", node_id = 174 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 175 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 176 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, true, true, false, false, false, 0, 32, 32, 0, [{name = "byte_value", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "int_value", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>} {
                  obelisk.sv.expression.element_select attributes {node_id = 177 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, true, true, false, false, false, 0, 32, 32, 0, [{name = "byte_value", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "int_value", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>} {
                    obelisk.sv.expression.named_value attributes {node_id = 178 : i64, referenced_path = "simulation_aggregates.tagged_right", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s26.tagged_right, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.source_aggregate<"simulation_aggregates", false, true, true, false, false, false, 0, 32, 32, 0, [{name = "byte_value", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "int_value", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 179 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.expression.tagged_union attributes {field_ordinal = 1 : i64, node_id = 180 : i64, packed_offset = 0 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, true, true, false, false, false, 0, 32, 32, 0, [{name = "byte_value", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "int_value", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "7", node_id = 181 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 182 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 183 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, true, true, false, false, false, 0, 32, 32, 0, [{name = "byte_value", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "int_value", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>} {
                  obelisk.sv.expression.element_select attributes {node_id = 184 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, true, true, false, false, false, 0, 32, 32, 0, [{name = "byte_value", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "int_value", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>} {
                    obelisk.sv.expression.named_value attributes {node_id = 185 : i64, referenced_path = "simulation_aggregates.tagged_left", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s25.tagged_left, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.source_aggregate<"simulation_aggregates", false, true, true, false, false, false, 0, 32, 32, 0, [{name = "byte_value", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "int_value", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 186 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.expression.tagged_union attributes {field_ordinal = 1 : i64, node_id = 187 : i64, packed_offset = 0 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, true, true, false, false, false, 0, 32, 32, 0, [{name = "byte_value", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "int_value", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "9", node_id = 188 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 189 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 190 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, true, true, false, false, false, 0, 32, 32, 0, [{name = "byte_value", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "int_value", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>} {
                  obelisk.sv.expression.element_select attributes {node_id = 191 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, true, true, false, false, false, 0, 32, 32, 0, [{name = "byte_value", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "int_value", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>} {
                    obelisk.sv.expression.named_value attributes {node_id = 192 : i64, referenced_path = "simulation_aggregates.tagged_right", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s26.tagged_right, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.source_aggregate<"simulation_aggregates", false, true, true, false, false, false, 0, 32, 32, 0, [{name = "byte_value", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "int_value", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 193 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.expression.tagged_union attributes {field_ordinal = 0 : i64, node_id = 194 : i64, packed_offset = 0 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, true, true, false, false, false, 0, 32, 32, 0, [{name = "byte_value", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "int_value", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>} {
                    obelisk.sv.expression.conversion attributes {node_id = 195 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "8'd9", node_id = 196 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 197 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 198 : i64, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.source_aggregate<"simulation_aggregates", false, true, true, false, false, false, 0, 32, 32, 0, [{name = "byte_value", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "int_value", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>>} {
                  obelisk.sv.expression.named_value attributes {node_id = 199 : i64, referenced_path = "simulation_aggregates.tagged_merged", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s27.tagged_merged, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.source_aggregate<"simulation_aggregates", false, true, true, false, false, false, 0, 32, 32, 0, [{name = "byte_value", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "int_value", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>>} {
                  }
                  obelisk.sv.expression.conditional_op attributes {condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, node_id = 200 : i64, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.source_aggregate<"simulation_aggregates", false, true, true, false, false, false, 0, 32, 32, 0, [{name = "byte_value", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "int_value", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>>} {
                    obelisk.sv.expression.named_value attributes {node_id = 201 : i64, referenced_path = "simulation_aggregates.ambiguous", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s28.ambiguous, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                    obelisk.sv.expression.named_value attributes {node_id = 202 : i64, referenced_path = "simulation_aggregates.tagged_left", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s25.tagged_left, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.source_aggregate<"simulation_aggregates", false, true, true, false, false, false, 0, 32, 32, 0, [{name = "byte_value", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "int_value", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>>} {
                    }
                    obelisk.sv.expression.named_value attributes {node_id = 203 : i64, referenced_path = "simulation_aggregates.tagged_right", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s26.tagged_right, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.source_aggregate<"simulation_aggregates", false, true, true, false, false, false, 0, 32, 32, 0, [{name = "byte_value", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "int_value", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 204 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 205 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>} {
                  obelisk.sv.expression.named_value attributes {node_id = 206 : i64, referenced_path = "simulation_aggregates.copied_record", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s15.copied_record, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>} {
                  }
                  obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "copy_record", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = false, node_id = 207 : i64, referenced_path = "simulation_aggregates.copy_record", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s29.copy_record, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>, subroutine_kind = 0 : i32} {
                    obelisk.sv.expression.named_value attributes {node_id = 208 : i64, referenced_path = "simulation_aggregates.unpacked_record", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s14.unpacked_record, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 209 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 210 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>} {
                  obelisk.sv.expression.named_value attributes {node_id = 211 : i64, referenced_path = "simulation_aggregates.copied_record", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s15.copied_record, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>} {
                  }
                  obelisk.sv.expression.call attributes {argument_count = 3 : i64, callee_name = "copy_update", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_super_class = false, is_system_call = false, node_id = 212 : i64, referenced_path = "simulation_aggregates.copy_update", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s32.copy_update, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>, subroutine_kind = 0 : i32} {
                    obelisk.sv.expression.named_value attributes {node_id = 213 : i64, referenced_path = "simulation_aggregates.unpacked_record", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s14.unpacked_record, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>} {
                    }
                    obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 214 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>} {
                      obelisk.sv.expression.named_value attributes {node_id = 215 : i64, referenced_path = "simulation_aggregates.output_record", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s16.output_record, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>} {
                      }
                      obelisk.sv.expression.empty_argument attributes {node_id = 216 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>} {
                      }
                    }
                    obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 217 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>} {
                      obelisk.sv.expression.named_value attributes {node_id = 218 : i64, referenced_path = "simulation_aggregates.copied_record", referenced_symbol = @s1.$root::@s3.simulation_aggregates::@s4.simulation_aggregates::@s15.copied_record, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>} {
                      }
                      obelisk.sv.expression.empty_argument attributes {node_id = 219 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>} {
                      }
                    }
                  }
                }
              }
            }
          }
        }
        obelisk.sv.type.unpacked_union_type attributes {hierarchical_name = "simulation_aggregates", node_id = 220 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, true, false, false, false, false, 0, 32, 32, 0, [{name = "byte_value", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "int_value", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>, sym_name = "s38"} {
          obelisk.sv.symbol.field attributes {bit_offset = 0 : i64, field_index = 0 : i64, hierarchical_name = "simulation_aggregates.byte_value", name = "byte_value", node_id = 221 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s39.byte_value"} {
          }
          obelisk.sv.symbol.field attributes {bit_offset = 0 : i64, field_index = 1 : i64, hierarchical_name = "simulation_aggregates.int_value", name = "int_value", node_id = 222 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s40.int_value"} {
          }
        }
        obelisk.sv.type.packed_union_type attributes {hierarchical_name = "simulation_aggregates", node_id = 223 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", true, true, false, false, true, false, 8, 8, 8, 0, [{name = "logic_word", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "bit_word", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>}]>, sym_name = "s41"} {
          obelisk.sv.symbol.field attributes {bit_offset = 0 : i64, field_index = 0 : i64, hierarchical_name = "simulation_aggregates.logic_word", name = "logic_word", node_id = 224 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s42.logic_word"} {
          }
          obelisk.sv.symbol.field attributes {bit_offset = 0 : i64, field_index = 1 : i64, hierarchical_name = "simulation_aggregates.bit_word", name = "bit_word", node_id = 225 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s43.bit_word"} {
          }
        }
        obelisk.sv.type.unpacked_struct_type attributes {hierarchical_name = "simulation_aggregates", node_id = 226 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", false, false, false, false, false, false, 0, 9, 9, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>, sym_name = "s44"} {
          obelisk.sv.symbol.field attributes {bit_offset = 0 : i64, field_index = 0 : i64, hierarchical_name = "simulation_aggregates.payload", name = "payload", node_id = 227 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s45.payload"} {
          }
          obelisk.sv.symbol.field attributes {bit_offset = 8 : i64, field_index = 1 : i64, hierarchical_name = "simulation_aggregates.valid", name = "valid", node_id = 228 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s46.valid"} {
          }
        }
        obelisk.sv.type.packed_struct_type attributes {hierarchical_name = "simulation_aggregates", node_id = 229 : i64, semantic_type = !obelisk.source_aggregate<"simulation_aggregates", true, false, false, false, true, false, 5, 5, 5, 0, [{name = "payload", ordinal = 0 : i32, packed_offset = 1 : i64, type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "valid", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<1, false, false, 0 : 0, bit>}]>, sym_name = "s47"} {
          obelisk.sv.symbol.field attributes {bit_offset = 1 : i64, field_index = 0 : i64, hierarchical_name = "simulation_aggregates.payload", name = "payload", node_id = 230 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s48.payload"} {
          }
          obelisk.sv.symbol.field attributes {bit_offset = 0 : i64, field_index = 1 : i64, hierarchical_name = "simulation_aggregates.valid", name = "valid", node_id = 231 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s49.valid"} {
          }
        }
      }
    }
  }
}

// CHECK: dynamic = true
// CHECK: !obelisk_sim.packed_struct<[
// CHECK: !obelisk_sim.unpacked_struct<[
// CHECK: !obelisk_sim.unpacked_array<3 : 1 x !obelisk_sim.packed_array<7 : 0 x !obelisk_sim.logic<1>>>
// CHECK: !obelisk_sim.unpacked_array<-1 : 1 x !obelisk_sim.packed_array<7 : 0 x !obelisk_sim.logic<1>>>
// CHECK: !obelisk_sim.packed_union<fields = [
// CHECK: !obelisk_sim.unpacked_union<fields = [
// CHECK: !obelisk_sim.packed_union<fields = {{.*}}isTagged = true, tagBits = 2>
// Named setters were written valid-first, but aggregate construction follows
// declaration order: payload, then valid.
// CHECK: %[[NAMED_PAYLOAD:.*]] = obelisk_sim.logic.constant 6 : i4, 0 : i4
// CHECK: %[[NAMED_VALID:.*]] = arith.constant true
// CHECK: %[[NAMED_PAYLOAD_ARRAY:.*]] = obelisk_sim.packed.unflatten %[[NAMED_PAYLOAD]]
// CHECK: obelisk_sim.aggregate.construct %[[NAMED_PAYLOAD_ARRAY]], %[[NAMED_VALID]]
// CHECK: obelisk_sim.aggregate.construct
// Descending range [3:1] maps source indices 3 and 2 to ordinals 0 and 1.
// CHECK: obelisk_sim.ref.subelement {{.*}}{{\[\[0\]\]}} : !obelisk_sim.ref<!obelisk_sim.unpacked_array<3 : 1
// CHECK: obelisk_sim.ref.subelement {{.*}}{{\[\[1\]\]}} : !obelisk_sim.ref<!obelisk_sim.unpacked_array<3 : 1
// CHECK: %[[DYNAMIC_WRITE:.*]] = obelisk_sim.ref.array_element
// CHECK: obelisk_sim.ref.store {{.*}} to %[[DYNAMIC_WRITE]]
// A dynamic element read addresses the element instead of loading the whole
// array and selecting out of the loaded value.
// CHECK: %[[DYNAMIC_READ:.*]] = obelisk_sim.ref.array_element
// CHECK: obelisk_sim.ref.load %[[DYNAMIC_READ]]
// Ascending range [-1:1] maps source indices -1 and 1 to ordinals 0 and 2.
// CHECK: obelisk_sim.ref.subelement {{.*}}{{\[\[0\]\]}} : !obelisk_sim.ref<!obelisk_sim.unpacked_array<-1 : 1
// CHECK: obelisk_sim.ref.subelement {{.*}}{{\[\[2\]\]}} : !obelisk_sim.ref<!obelisk_sim.unpacked_array<-1 : 1
// An unpacked union member write preserves every overlapping byte through a
// whole-value read/modify/write instead of manufacturing a subreference.
// CHECK: %[[UNTAGGED_OLD:.*]] = obelisk_sim.ref.load {{.*}} : {{.*}} -> !obelisk_sim.unpacked_union<{{.*}}isTagged = false>
// CHECK: %[[UNTAGGED_UPDATED:.*]] = obelisk_sim.aggregate.insert {{.*}} into %[[UNTAGGED_OLD]][1] :
// CHECK: obelisk_sim.ref.store %[[UNTAGGED_UPDATED]] to {{.*}} : !obelisk_sim.unpacked_union<{{.*}}isTagged = false>
// CHECK: obelisk_sim.union.construct {{.*}} as 1 : {{.*}}isTagged = true, tagBits = 2>
// CHECK: obelisk_sim.union.construct {{.*}} as 1 : {{.*}}isTagged = true>
// Ambiguous fixed-array merging compares tagged-union activity before values.
// CHECK: obelisk_sim.union.is_active
// CHECK: obelisk_sim.logic.compare eq
// Aggregate output and inout values are explicit copy-out results.
// CHECK: %[[COPY_CALL:[0-9]+]]:3 = obelisk_sim.call
// CHECK: obelisk_sim.ref.store %[[COPY_CALL]]#1
// CHECK: obelisk_sim.ref.store %[[COPY_CALL]]#2
// CHECK-NOT: obelisk.sv.
