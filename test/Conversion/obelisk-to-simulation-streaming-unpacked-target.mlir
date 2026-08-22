// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 11.4.14 makes a fixed unpacked array of bit-stream types a
// bit-stream type, so `{<<{dst}}` is a legal assignment target: the stream is
// laid into the array's elements in the left-to-right order the pack direction
// flattens them.
//
// 11.4.14.3 also fixes which bits reach it -- "if the source expression
// contains more bits than are needed, the appropriate number of bits shall be
// consumed from its left (most significant) end". The eight-bit source here
// feeds a six-bit target, so the left-to-right reordering covers those six
// leading bits and not the whole source; reordering all eight first and taking
// the front of the result would hand the target the source's *trailing* bits
// instead.

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "t", name = "t", node_id = 0 : i64, sym_name = "s0.t"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "t", is_uninstantiated = false, name = "t", node_id = 3 : i64, referenced_path = "t", referenced_symbol = @s0.t, sym_name = "s3.t"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "t", name = "t", node_id = 4 : i64, sym_name = "s4.t", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "t.dst", lifetime = 1 : i32, name = "dst", node_id = 5 : i64, semantic_type = !obelisk.ranged_unpacked_array<0 : 2 x !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>, sym_name = "s5.dst"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "t.src", lifetime = 1 : i32, name = "src", node_id = 6 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s6.src"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "t", node_id = 7 : i64, procedure_kind = 0 : i32, sym_name = "s7", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 8 : i64} {
            obelisk.sv.statement.expression_statement attributes {node_id = 9 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 10 : i64, semantic_type = !obelisk.void} {
                obelisk.sv.expression.streaming attributes {bitstream_width = 6 : i64, is_fixed_size = true, is_signed = false, node_id = 11 : i64, semantic_type = !obelisk.void, slice_size = 1 : i64, stream_count = 1 : i64, stream_with_flags = array<i64: 0>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 12 : i64, referenced_path = "t.dst", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s5.dst, semantic_type = !obelisk.ranged_unpacked_array<0 : 2 x !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>} {
                  }
                }
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 13 : i64, referenced_path = "t.src", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s6.src, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                }
              }
            }
          }
        }
      }
    }
  }
}


// The reordering is bounded by the target's six-bit bit-stream width, not by
// the size of the eight-bit source stream.
// CHECK: %[[WIDTH:.*]] = arith.constant 6 : i64
// CHECK: arith.cmpi ult, %{{.*}}, %[[WIDTH]]

// Each element takes its own window, and the array is assembled from them.
// CHECK: obelisk_sim.aggregate.construct
// CHECK-SAME: -> !obelisk_sim.unpacked_array<0 : 2 x !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>>
