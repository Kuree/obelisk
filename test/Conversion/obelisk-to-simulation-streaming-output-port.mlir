// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 10.8 and 23.3.3 make an output-port connection an
// assignment-like continuous assignment. Clause 11.4.14.3 therefore permits
// its external lvalue to be a streaming concatenation.
module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "child", name = "child", node_id = 0 : i64, sym_name = "child_def"} {
  }
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 1 : i64, sym_name = "top_def"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 2 : i64, sym_name = "root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 3 : i64, sym_name = "unit"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 4 : i64, referenced_path = "top", referenced_symbol = @top_def, sym_name = "top_i"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 5 : i64, sym_name = "top_b"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.dst", lifetime = 1 : i32, name = "dst", node_id = 6 : i64, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>, sym_name = "dst"} {
        }
        obelisk.sv.symbol.instance attributes {hierarchical_name = "top.child", is_uninstantiated = false, name = "child", node_id = 7 : i64, referenced_path = "child", referenced_symbol = @child_def, sym_name = "child_i"} {
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 1 : i32, formal_name = "out", formal_ordinal = 0 : i64, formal_path = "top.child.out", formal_symbol = @root::@top_i::@top_b::@child_i::@child_b::@out_p, formal_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, internal_path = "top.child.out", internal_symbol = @root::@top_i::@top_b::@child_i::@child_b::@out_v, is_ansi = true, is_net = false, node_id = 8 : i64, provenance = 0 : i32} {
          } {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 9 : i64, semantic_type = !obelisk.void} {
              obelisk.sv.expression.streaming attributes {bitstream_width = 8 : i64, is_fixed_size = true, is_signed = false, node_id = 10 : i64, semantic_type = !obelisk.void, slice_size = 0 : i64, stream_count = 1 : i64, stream_with_flags = array<i64: 0>} {
                obelisk.sv.expression.named_value attributes {node_id = 11 : i64, referenced_path = "top.dst", referenced_symbol = @root::@top_i::@top_b::@dst, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>} {
                }
              }
              obelisk.sv.expression.empty_argument attributes {node_id = 12 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              }
            }
          }
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.child", name = "child", node_id = 13 : i64, sym_name = "child_b"} {
            obelisk.sv.symbol.port attributes {direction = 1 : i32, hierarchical_name = "top.child.out", name = "out", node_id = 14 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "out_p"} {
            }
            obelisk.sv.symbol.variable attributes {hierarchical_name = "top.child.out", lifetime = 1 : i32, name = "out", node_id = 15 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "out_v"} {
            }
          }
        }
      }
    }
  }
}

// CHECK: obelisk_sim.code_unit.decl {{[0-9]+}} in {{[0-9]+}} port_output
// CHECK: obelisk_sim.container.export_bitstream %{{.*}} -> !obelisk_sim.logic<8>
// CHECK-NOT: obelisk.sv.
