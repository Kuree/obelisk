// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2023 10.11: alias members share the physical nets of their bits.
// Selected bits and concatenations join bit by bit through static net
// connections, counting both sides from their least significant bit.
//
// module top;
//   wire [0:0] a;
//   wire b;
//   wire [15:0] w;
//   wire [7:0] lo, hi;
//   alias a[0] = b;
//   alias {w[7:0], w[15:8]} = {hi, lo};
// endmodule

module attributes {coverage.language_version = 2023 : i32} {
  talos.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  talos.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    talos.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      talos.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        talos.sv.symbol.net @s5.a attributes {hierarchical_name = "top.a", is_implicit = false, name = "a", net_kind = 1 : i32, node_id = 5 : i64, semantic_type = !talos.ranged_packed_array<0 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {
        }
        talos.sv.symbol.net @s6.b attributes {hierarchical_name = "top.b", is_implicit = false, name = "b", net_kind = 1 : i32, node_id = 6 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
        }
        talos.sv.symbol.net @s7.w attributes {hierarchical_name = "top.w", is_implicit = false, name = "w", net_kind = 1 : i32, node_id = 7 : i64, semantic_type = !talos.ranged_packed_array<15 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {
        }
        talos.sv.symbol.net @s8.lo attributes {hierarchical_name = "top.lo", is_implicit = false, name = "lo", net_kind = 1 : i32, node_id = 8 : i64, semantic_type = !talos.ranged_packed_array<7 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {
        }
        talos.sv.symbol.net @s9.hi attributes {hierarchical_name = "top.hi", is_implicit = false, name = "hi", net_kind = 1 : i32, node_id = 9 : i64, semantic_type = !talos.ranged_packed_array<7 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {
        }
        talos.sv.symbol.net_alias @s10 attributes {hierarchical_name = "top", node_id = 10 : i64} {
          talos.sv.expression.element_select attributes {is_signed = false, node_id = 11 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
            talos.sv.expression.named_value attributes {is_signed = false, node_id = 12 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.a, semantic_type = !talos.ranged_packed_array<0 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {
            }
            talos.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 13 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
            }
          }
          talos.sv.expression.named_value attributes {is_signed = false, node_id = 14 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.b, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
          }
        }
        talos.sv.symbol.net_alias @s11 attributes {hierarchical_name = "top", node_id = 15 : i64} {
          talos.sv.expression.concatenation attributes {is_signed = false, node_id = 16 : i64, semantic_type = !talos.ranged_packed_array<15 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {
            talos.sv.expression.range_select attributes {is_signed = false, node_id = 17 : i64, selection_kind = 0 : i32, semantic_type = !talos.ranged_packed_array<7 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {
              talos.sv.expression.named_value attributes {is_signed = false, node_id = 18 : i64, referenced_path = "top.w", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.w, semantic_type = !talos.ranged_packed_array<15 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {
              }
              talos.sv.expression.integer_literal attributes {constant_value = "7", is_declared_unsized = true, is_signed = true, node_id = 19 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
              }
              talos.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 20 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
              }
            }
            talos.sv.expression.range_select attributes {is_signed = false, node_id = 21 : i64, selection_kind = 0 : i32, semantic_type = !talos.ranged_packed_array<15 : 8 x !talos.integral<1, false, true, 0 : 0, logic>>} {
              talos.sv.expression.named_value attributes {is_signed = false, node_id = 22 : i64, referenced_path = "top.w", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.w, semantic_type = !talos.ranged_packed_array<15 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {
              }
              talos.sv.expression.integer_literal attributes {constant_value = "15", is_declared_unsized = true, is_signed = true, node_id = 23 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
              }
              talos.sv.expression.integer_literal attributes {constant_value = "8", is_declared_unsized = true, is_signed = true, node_id = 24 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
              }
            }
          }
          talos.sv.expression.concatenation attributes {is_signed = false, node_id = 25 : i64, semantic_type = !talos.ranged_packed_array<15 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {
            talos.sv.expression.named_value attributes {is_signed = false, node_id = 26 : i64, referenced_path = "top.hi", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s9.hi, semantic_type = !talos.ranged_packed_array<7 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {
            }
            talos.sv.expression.named_value attributes {is_signed = false, node_id = 27 : i64, referenced_path = "top.lo", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.lo, semantic_type = !talos.ranged_packed_array<7 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {
            }
          }
        }
      }
    }
  }
}


// CHECK: simulation.net.decl 0 {{.*}} hierarchy "top.a"
// CHECK: simulation.net.decl 1 {{.*}} hierarchy "top.b"
// CHECK: simulation.net.decl 2 {{.*}} hierarchy "top.w"
// CHECK: simulation.net.decl 3 {{.*}} hierarchy "top.lo"
// CHECK: simulation.net.decl 4 {{.*}} hierarchy "top.hi"
// CHECK: simulation.net.connect.decl 0 in 1 0[0] to 1[0] width 1 reversed = false provenance "alias"
// CHECK: simulation.net.connect.decl 1 in 1 2[8] to 3[0] width 8 reversed = false provenance "alias"
// CHECK: simulation.net.connect.decl 2 in 1 2[0] to 4[0] width 8 reversed = false provenance "alias"
