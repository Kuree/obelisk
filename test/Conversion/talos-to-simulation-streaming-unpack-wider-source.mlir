// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 11.4.14.3: "If the source expression contains more bits than
// are needed, the appropriate number of bits shall be consumed from its left
// (most significant) end." A left-to-right target reorders nothing, so the
// six-bit bulk export must consume the stream's leading six bits and leave the
// two trailing source bits unused.
module {
  talos.sv.symbol.definition @top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  talos.sv.symbol.root @root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @unit attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    talos.sv.symbol.instance @instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @top} {
      talos.sv.symbol.instance_body @body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64} {
        talos.sv.symbol.variable @src attributes {hierarchical_name = "top.src", lifetime = 1 : i32, name = "src", node_id = 5 : i64, semantic_type = !talos.ranged_packed_array<7 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {
        }
        talos.sv.symbol.variable @dst attributes {hierarchical_name = "top.dst", lifetime = 1 : i32, name = "dst", node_id = 6 : i64, semantic_type = !talos.ranged_unpacked_array<0 : 2 x !talos.ranged_packed_array<1 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>>} {
        }
        talos.sv.symbol.procedural_block @initial attributes {hierarchical_name = "top", node_id = 7 : i64, procedure_kind = 0 : i32, time_precision_fs = 1 : i64, time_unit_fs = 1 : i64} {
          talos.sv.statement.expression_statement attributes {node_id = 8 : i64} {
            talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 9 : i64, semantic_type = !talos.void} {
              talos.sv.expression.streaming attributes {bitstream_width = 6 : i64, is_fixed_size = true, is_signed = false, node_id = 10 : i64, semantic_type = !talos.void, slice_size = 0 : i64, stream_count = 1 : i64, stream_with_flags = array<i64: 0>} {
                talos.sv.expression.named_value attributes {is_signed = false, node_id = 11 : i64, referenced_path = "top.dst", referenced_symbol = @root::@instance::@body::@dst, semantic_type = !talos.ranged_unpacked_array<0 : 2 x !talos.ranged_packed_array<1 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>>} {
                }
              }
              talos.sv.expression.named_value attributes {is_signed = false, node_id = 12 : i64, referenced_path = "top.src", referenced_symbol = @root::@instance::@body::@src, semantic_type = !talos.ranged_packed_array<7 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {
              }
            }
          }
        }
      }
    }
  }
}

// The export reads only the leading six bits. Truncation must not synthesize a
// loop that deletes each unused bit from the internal queue.
// CHECK-NOT: simulation.queue.delete
// CHECK: simulation.container.export_bitstream %{{.*}} -> !simulation.logic<6>
