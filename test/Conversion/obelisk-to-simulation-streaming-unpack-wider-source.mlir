// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 11.4.14.3: "If the source expression contains more bits than
// are needed, the appropriate number of bits shall be consumed from its left
// (most significant) end." A left-to-right target reorders nothing, so the
// stream still arrives as long as the eight-bit source; the six bits its
// targets need have to be separated from the two that follow them before the
// bulk export packs the stream.
module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "unit"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @top, sym_name = "instance"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, sym_name = "body"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.src", lifetime = 1 : i32, name = "src", node_id = 5 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "src"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.dst", lifetime = 1 : i32, name = "dst", node_id = 6 : i64, semantic_type = !obelisk.ranged_unpacked_array<0 : 2 x !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>, sym_name = "dst"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 7 : i64, procedure_kind = 0 : i32, sym_name = "initial", time_precision_fs = 1 : i64, time_unit_fs = 1 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 8 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 9 : i64, semantic_type = !obelisk.void} {
              obelisk.sv.expression.streaming attributes {bitstream_width = 6 : i64, is_fixed_size = true, is_signed = false, node_id = 10 : i64, semantic_type = !obelisk.void, slice_size = 0 : i64, stream_count = 1 : i64, stream_with_flags = array<i64: 0>} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 11 : i64, referenced_path = "top.dst", referenced_symbol = @root::@instance::@body::@dst, semantic_type = !obelisk.ranged_unpacked_array<0 : 2 x !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>} {
                }
              }
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 12 : i64, referenced_path = "top.src", referenced_symbol = @root::@instance::@body::@src, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              }
            }
          }
        }
      }
    }
  }
}

// The trailing bits are dropped from the stream, so the export packs exactly
// the six bits the three targets take.
// CHECK: obelisk_sim.queue.delete
// CHECK: obelisk_sim.container.export_bitstream %{{.*}} -> !obelisk_sim.logic<6>
