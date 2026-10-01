// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

!packed = !obelisk.integral<24, false, true, 23 : 0, logic>

module {
  obelisk.sv.symbol.definition @top_def attributes {
    definition_kind = 0 : i32, hierarchical_name = "top", name = "top",
    node_id = 0 : i64
  } {}
  obelisk.sv.symbol.root @root attributes {
    hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64
  } {
    obelisk.sv.symbol.instance @top attributes {
      hierarchical_name = "top", is_uninstantiated = false, name = "top",
      node_id = 2 : i64, referenced_path = "top",
      referenced_symbol = @top_def
    } {
      obelisk.sv.symbol.instance_body @body attributes {
        hierarchical_name = "top", name = "top", node_id = 3 : i64
      } {
        obelisk.sv.symbol.variable @source attributes {
          hierarchical_name = "top.source", lifetime = 1 : i32,
          name = "source", node_id = 4 : i64, semantic_type = !obelisk.string
        } {}
        obelisk.sv.symbol.variable @explicit attributes {
          hierarchical_name = "top.explicit", lifetime = 1 : i32,
          name = "explicit", node_id = 5 : i64, semantic_type = !packed
        } {
          obelisk.sv.expression.conversion attributes {
            is_implicit = false, is_signed = false, node_id = 6 : i64,
            semantic_type = !packed
          } {
            obelisk.sv.expression.named_value attributes {
              is_signed = false, node_id = 7 : i64,
              referenced_path = "top.source",
              referenced_symbol = @root::@top::@body::@source,
              semantic_type = !obelisk.string
            } {}
          }
        }
        obelisk.sv.symbol.variable @implicit attributes {
          hierarchical_name = "top.implicit", lifetime = 1 : i32,
          name = "implicit", node_id = 8 : i64, semantic_type = !packed
        } {
          obelisk.sv.expression.conversion attributes {
            is_implicit = true, is_signed = false, node_id = 9 : i64,
            semantic_type = !packed
          } {
            obelisk.sv.expression.named_value attributes {
              is_signed = false, node_id = 10 : i64,
              referenced_path = "top.source",
              referenced_symbol = @root::@top::@body::@source,
              semantic_type = !obelisk.string
            } {}
          }
        }
      }
    }
  }
}

// The explicit cast reads one managed-string SSA value through an exact
// string.to_packed_exact operation. Native and bytecode lowering fuse the
// required length check with packing. The ordinary implicit conversion stays
// on string.to_packed.
// CHECK-LABEL: simulation.func private @unit_0
// CHECK: %[[SOURCE:.*]] = simulation.ref.load
// CHECK: simulation.string.to_packed_exact %[[SOURCE]]
// CHECK: cf.cond_br
// CHECK: simulation.logic.from_bits
// CHECK: simulation.bytes.constant "{{.*}}bit-stream cast source and destination widths differ"
// CHECK: simulation.fatal
// CHECK-LABEL: simulation.func private @unit_1
// CHECK-NOT: simulation.string.length
// CHECK: simulation.string.to_packed
// CHECK: simulation.logic.from_bits
