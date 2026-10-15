// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2023 14.16: a synchronous drive may target a whole clockvar of
// any assignment-compatible type, or a select of one, including a member
// select of a structure. A whole unpacked-array clockvar is resolved like a
// packed one, and a member drive writes only its member.
//
// module top;
//   typedef struct { bit [3:0] a; int b; } S;
//   bit clk;
//   int words [2];
//   S s;
//   clocking cb @(posedge clk);
//     output words, s;
//   endclocking
//   initial begin
//     cb.words <= '{5, 7};
//     cb.s.b <= 9;
//   end
// endmodule

// CHECK-LABEL: simulation.func private @unit_0.$clocking_output.21(
// CHECK-SAME: %[[WORDS:[^:]+]]: !simulation.ref<!simulation.unpacked_array<0 : 1 x i32>>
// CHECK: simulation.nba.enqueue %{{.*}} to %[[WORDS]] {clocking_output = {{[0-9]+}} : i64
// CHECK-SAME: (!simulation.unpacked_array<0 : 1 x i32>, !simulation.ref<!simulation.unpacked_array<0 : 1 x i32>>)

// CHECK-LABEL: simulation.func private @unit_0.$clocking_output.28(
// CHECK-SAME: %[[MEMBER:[^:]+]]: !simulation.ref<i32>
// CHECK: simulation.nba.enqueue %{{.*}} to %[[MEMBER]] {clocking_output = {{[0-9]+}} : i64

// CHECK-LABEL: simulation.func private @unit_0(
// CHECK: %[[B:.+]] = simulation.ref.subelement %{{.*}}{{\[\[}}1]]
// CHECK: simulation.spawn @unit_0.$clocking_output.28(%{{.*}}, %[[B]],

module attributes {coverage.language_version = 2023 : i32} {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    obelisk.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        obelisk.sv.type.type_alias @s5.S attributes {hierarchical_name = "top.S", name = "S", node_id = 5 : i64, semantic_type = !obelisk.source_aggregate<"top", false, false, false, false, false, false, 0, 36, 36, 0, [{name = "a", ordinal = 0 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>}, {name = "b", ordinal = 1 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>, vpi_source_type_identity = 0 : i64, vpi_typedef_layers = [{aliases = [@s1.$root::@s4.top::@s5.S], path = array<i64>}]} {
        }
        obelisk.sv.symbol.variable @s6.clk attributes {hierarchical_name = "top.clk", lifetime = 1 : i32, name = "clk", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
        }
        obelisk.sv.symbol.variable @s7.words attributes {hierarchical_name = "top.words", lifetime = 1 : i32, name = "words", node_id = 7 : i64, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.integral<32, true, false, 31 : 0, int>>} {
        }
        obelisk.sv.symbol.variable @s8.s attributes {hierarchical_name = "top.s", lifetime = 1 : i32, name = "s", node_id = 8 : i64, semantic_type = !obelisk.source_aggregate<"top", false, false, false, false, false, false, 0, 36, 36, 0, [{name = "a", ordinal = 0 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>}, {name = "b", ordinal = 1 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>, vpi_typedef_layers = [{aliases = [@s1.$root::@s4.top::@s5.S], path = array<i64>}]} {
        }
        obelisk.sv.symbol.clocking_block @s9.cb attributes {hierarchical_name = "top.cb", is_default = false, is_global = false, name = "cb", node_id = 9 : i64} {
          obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 10 : i64} {
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 11 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
            }
          }
          obelisk.sv.symbol.clock_var @s10.words attributes {direction = 1 : i32, has_input_delay = false, has_output_delay = false, hierarchical_name = "top.cb.words", input_edge = 0 : i32, lifetime = 1 : i32, name = "words", node_id = 12 : i64, output_edge = 0 : i32, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.integral<32, true, false, 31 : 0, int>>} {
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 13 : i64, referenced_path = "top.words", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.words, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.integral<32, true, false, 31 : 0, int>>} {
            }
          }
          obelisk.sv.symbol.clock_var @s11.s attributes {direction = 1 : i32, has_input_delay = false, has_output_delay = false, hierarchical_name = "top.cb.s", input_edge = 0 : i32, lifetime = 1 : i32, name = "s", node_id = 14 : i64, output_edge = 0 : i32, semantic_type = !obelisk.source_aggregate<"top", false, false, false, false, false, false, 0, 36, 36, 0, [{name = "a", ordinal = 0 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>}, {name = "b", ordinal = 1 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>, vpi_typedef_layers = [{aliases = [@s1.$root::@s4.top::@s5.S], path = array<i64>}]} {
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 15 : i64, referenced_path = "top.s", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.s, semantic_type = !obelisk.source_aggregate<"top", false, false, false, false, false, false, 0, 36, 36, 0, [{name = "a", ordinal = 0 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>}, {name = "b", ordinal = 1 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>, vpi_typedef_layers = [{aliases = [@s1.$root::@s4.top::@s5.S], path = array<i64>}]} {
            }
          }
        }
        obelisk.sv.symbol.procedural_block @s12 attributes {hierarchical_name = "top", node_id = 16 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 17 : i64} {
            obelisk.sv.statement.list attributes {node_id = 18 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 19 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 1 : i32, is_signed = false, node_id = 20 : i64, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.integral<32, true, false, 31 : 0, int>>} {
                  obelisk.sv.expression.named_value attributes {clocking_access_direction = 1 : i32, clocking_event_edge = 1 : i32, clocking_event_path = "top.clk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, clocking_input_skew_edge = 0 : i32, clocking_input_skew_one_step, clocking_output_skew_delay = "0", clocking_output_skew_edge = 0 : i32, clocking_source_path = "top.words", clocking_source_symbol = @s1.$root::@s3.top::@s4.top::@s7.words, clocking_time_precision_fs = 1000000 : i64, clocking_time_unit_fs = 1000000 : i64, clocking_variable, is_signed = false, node_id = 21 : i64, referenced_path = "top.cb.words", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s9.cb::@s10.words, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.integral<32, true, false, 31 : 0, int>>} {
                  }
                  obelisk.sv.expression.simple_assignment_pattern attributes {is_signed = false, node_id = 22 : i64, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.integral<32, true, false, 31 : 0, int>>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "5", is_declared_unsized = true, is_signed = true, node_id = 23 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "7", is_declared_unsized = true, is_signed = true, node_id = 24 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 25 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 1 : i32, is_signed = true, node_id = 26 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.member_access attributes {field_ordinal = 1 : i64, is_signed = true, member_name = "b", node_id = 27 : i64, packed_offset = 4 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s13::@s15.b, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.named_value attributes {clocking_access_direction = 1 : i32, clocking_event_edge = 1 : i32, clocking_event_path = "top.clk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s6.clk, clocking_input_skew_edge = 0 : i32, clocking_input_skew_one_step, clocking_output_skew_delay = "0", clocking_output_skew_edge = 0 : i32, clocking_source_path = "top.s", clocking_source_symbol = @s1.$root::@s3.top::@s4.top::@s8.s, clocking_time_precision_fs = 1000000 : i64, clocking_time_unit_fs = 1000000 : i64, clocking_variable, is_signed = false, node_id = 28 : i64, referenced_path = "top.cb.s", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s9.cb::@s11.s, semantic_type = !obelisk.source_aggregate<"top", false, false, false, false, false, false, 0, 36, 36, 0, [{name = "a", ordinal = 0 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>}, {name = "b", ordinal = 1 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>, vpi_typedef_layers = [{aliases = [@s1.$root::@s4.top::@s5.S], path = array<i64>}]} {
                    }
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "9", is_declared_unsized = true, is_signed = true, node_id = 29 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
            }
          }
        }
        obelisk.sv.type.unpacked_struct_type @s13 attributes {hierarchical_name = "top", node_id = 30 : i64, semantic_type = !obelisk.source_aggregate<"top", false, false, false, false, false, false, 0, 36, 36, 0, [{name = "a", ordinal = 0 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>}, {name = "b", ordinal = 1 : i32, packed_offset = 0 : i64, rand_mode = 0 : i32, type = !obelisk.integral<32, true, false, 31 : 0, int>}]>} {
          obelisk.sv.symbol.field @s14.a attributes {bit_offset = 0 : i64, field_index = 0 : i64, hierarchical_name = "top.a", name = "a", node_id = 31 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
          }
          obelisk.sv.symbol.field @s15.b attributes {bit_offset = 4 : i64, field_index = 1 : i64, hierarchical_name = "top.b", name = "b", node_id = 32 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
      }
    }
  }
}
