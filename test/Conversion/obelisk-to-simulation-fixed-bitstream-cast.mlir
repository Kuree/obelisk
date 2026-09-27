// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

!bytes = !obelisk.ranged_unpacked_array<0 : 8191 x !obelisk.integral<8, false, true, 7 : 0, logic>>
!packed = !obelisk.integral<65536, false, true, 65535 : 0, logic>
!nibbles = !obelisk.dynarray<!obelisk.integral<4, false, true, 3 : 0, logic>>

module {
  obelisk.sv.symbol.definition attributes {
    definition_kind = 0 : i32, hierarchical_name = "top", name = "top",
    node_id = 0 : i64, sym_name = "top_def"
  } {}
  obelisk.sv.symbol.root attributes {
    hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64,
    sym_name = "root"
  } {
    obelisk.sv.symbol.instance attributes {
      hierarchical_name = "top", is_uninstantiated = false, name = "top",
      node_id = 2 : i64, referenced_path = "top",
      referenced_symbol = @top_def, sym_name = "top"
    } {
      obelisk.sv.symbol.instance_body attributes {
        hierarchical_name = "top", name = "top", node_id = 3 : i64,
        sym_name = "body"
      } {
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "top.source", lifetime = 1 : i32,
          name = "source", node_id = 4 : i64, semantic_type = !bytes,
          sym_name = "source"
        } {}
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "top.result", lifetime = 1 : i32,
          name = "result", node_id = 5 : i64, semantic_type = !packed,
          sym_name = "result"
        } {
          obelisk.sv.expression.conversion attributes {
            is_implicit = false, is_signed = false, node_id = 6 : i64,
            semantic_type = !packed
          } {
            obelisk.sv.expression.named_value attributes {
              is_signed = false, node_id = 7 : i64,
              referenced_path = "top.source",
              referenced_symbol = @root::@top::@body::@source,
              semantic_type = !bytes
            } {}
          }
        }
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "top.repartitioned", lifetime = 1 : i32,
          name = "repartitioned", node_id = 12 : i64,
          semantic_type = !nibbles, sym_name = "repartitioned"
        } {
          obelisk.sv.expression.conversion attributes {
            is_implicit = false, is_signed = false, node_id = 13 : i64,
            semantic_type = !nibbles
          } {
            obelisk.sv.expression.named_value attributes {
              is_signed = false, node_id = 14 : i64,
              referenced_path = "top.source",
              referenced_symbol = @root::@top::@body::@source,
              semantic_type = !bytes
            } {}
          }
        }
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "top.packed_source", lifetime = 1 : i32,
          name = "packed_source", node_id = 8 : i64, semantic_type = !packed,
          sym_name = "packed_source"
        } {}
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "top.unpacked_result", lifetime = 1 : i32,
          name = "unpacked_result", node_id = 9 : i64, semantic_type = !bytes,
          sym_name = "unpacked_result"
        } {
          obelisk.sv.expression.conversion attributes {
            is_implicit = false, is_signed = false, node_id = 10 : i64,
            semantic_type = !bytes
          } {
            obelisk.sv.expression.named_value attributes {
              is_signed = false, node_id = 11 : i64,
              referenced_path = "top.packed_source",
              referenced_symbol = @root::@top::@body::@packed_source,
              semantic_type = !packed
            } {}
          }
        }
      }
    }
  }
}

// CHECK: %[[SOURCE:.*]] = simulation.ref.load
// CHECK: simulation.aggregate.export_bitstream %[[SOURCE]] plan
// CHECK-SAME: [5407724624, 2, 65536, 65536, 4294967298, 0, 8192, 8, 8, 8,
// CHECK-SAME: 1, 0, 8, 0, 0, 8]
// CHECK: simulation.container.create {{.*}} -> !simulation.dynamic_array<!simulation.logic<4>>
// CHECK-COUNT-1: simulation.logic.dyn_extract
// CHECK: %[[PACKED:.*]] = simulation.ref.load
// CHECK: simulation.aggregate.import_bitstream %[[PACKED]] plan
// CHECK-SAME: [5407724624, 2, 65536, 65536, 4294967298, 0, 8192, 8, 8, 8,
// CHECK-SAME: 3, 0, 8, 0, 0, 8]
