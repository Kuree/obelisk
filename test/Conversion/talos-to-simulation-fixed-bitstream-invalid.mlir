// RUN: talos-opt %s --split-input-file --verify-diagnostics \
// RUN:   '--lower-talos-to-sim=opt-level=0'

!bytes = !talos.ranged_unpacked_array<0 : 1 x !talos.integral<8, false, true, 7 : 0, logic>>
!packed = !talos.integral<24, false, true, 23 : 0, logic>

module {
  talos.sv.symbol.definition @top_def attributes {definition_kind = 0 : i32,
      hierarchical_name = "top", name = "top", node_id = 0 : i64
  } {}
  talos.sv.symbol.root @root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.instance @top attributes {hierarchical_name = "top",
        is_uninstantiated = false, name = "top", node_id = 2 : i64,
        referenced_path = "top", referenced_symbol = @top_def
    } {
      talos.sv.symbol.instance_body @body attributes {hierarchical_name = "top",
          name = "top", node_id = 3 : i64} {
        talos.sv.symbol.variable @source attributes {hierarchical_name = "top.source",
            lifetime = 1 : i32, name = "source", node_id = 4 : i64,
            semantic_type = !bytes} {}
        talos.sv.symbol.variable @result attributes {hierarchical_name = "top.result",
            lifetime = 1 : i32, name = "result", node_id = 5 : i64,
            semantic_type = !packed} {
          // expected-error@+1 {{bit-stream cast source and destination widths differ (16 vs 24)}}
          talos.sv.expression.conversion attributes {is_implicit = false,
              is_signed = false, node_id = 6 : i64, semantic_type = !packed} {
            talos.sv.expression.named_value attributes {is_signed = false,
                node_id = 7 : i64, referenced_path = "top.source",
                referenced_symbol = @root::@top::@body::@source,
                semantic_type = !bytes} {}
          }
        }
      }
    }
  }
}

// -----

!bytes = !talos.ranged_unpacked_array<0 : 1 x !talos.integral<8, false, true, 7 : 0, logic>>
!packed = !talos.integral<16, false, true, 15 : 0, logic>

module {
  talos.sv.symbol.definition @top_def attributes {definition_kind = 0 : i32,
      hierarchical_name = "top", name = "top", node_id = 0 : i64
  } {}
  talos.sv.symbol.root @root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.instance @top attributes {hierarchical_name = "top",
        is_uninstantiated = false, name = "top", node_id = 2 : i64,
        referenced_path = "top", referenced_symbol = @top_def
    } {
      talos.sv.symbol.instance_body @body attributes {hierarchical_name = "top",
          name = "top", node_id = 3 : i64} {
        talos.sv.symbol.variable @source attributes {hierarchical_name = "top.source",
            lifetime = 1 : i32, name = "source", node_id = 4 : i64,
            semantic_type = !bytes} {}
        talos.sv.symbol.variable @result attributes {hierarchical_name = "top.result",
            lifetime = 1 : i32, name = "result", node_id = 5 : i64,
            semantic_type = !packed} {
          // expected-error@+1 {{cannot convert unpacked aggregate}}
          talos.sv.expression.conversion attributes {is_implicit = true,
              is_signed = false, node_id = 6 : i64, semantic_type = !packed} {
            talos.sv.expression.named_value attributes {is_signed = false,
                node_id = 7 : i64, referenced_path = "top.source",
                referenced_symbol = @root::@top::@body::@source,
                semantic_type = !bytes} {}
          }
        }
      }
    }
  }
}
