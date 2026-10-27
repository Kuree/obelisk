// RUN: talos-opt %s --verify-diagnostics \
// RUN:   '--lower-talos-to-sim=opt-level=0'

!packed = !talos.integral<12, false, true, 11 : 0, logic>

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
            semantic_type = !talos.string} {}
        talos.sv.symbol.variable @result attributes {hierarchical_name = "top.result",
            lifetime = 1 : i32, name = "result", node_id = 5 : i64,
            semantic_type = !packed} {
          // expected-error@+1 {{string bit-stream cast destination width must be a nonzero multiple of eight bits}}
          talos.sv.expression.conversion attributes {is_implicit = false,
              is_signed = false, node_id = 6 : i64, semantic_type = !packed} {
            talos.sv.expression.named_value attributes {is_signed = false,
                node_id = 7 : i64, referenced_path = "top.source",
                referenced_symbol = @root::@top::@body::@source,
                semantic_type = !talos.string} {}
          }
        }
      }
    }
  }
}
