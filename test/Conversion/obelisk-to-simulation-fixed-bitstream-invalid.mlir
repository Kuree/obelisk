// RUN: obelisk-opt %s --split-input-file --verify-diagnostics \
// RUN:   '--lower-obelisk-to-sim=opt-level=0'

!bytes = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.integral<8, false, true, 7 : 0, logic>>
!packed = !obelisk.integral<24, false, true, 23 : 0, logic>

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32,
      hierarchical_name = "top", name = "top", node_id = 0 : i64,
      sym_name = "top_def"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 1 : i64, sym_name = "root"} {
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top",
        is_uninstantiated = false, name = "top", node_id = 2 : i64,
        referenced_path = "top", referenced_symbol = @top_def,
        sym_name = "top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top",
          name = "top", node_id = 3 : i64, sym_name = "body"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.source",
            lifetime = 1 : i32, name = "source", node_id = 4 : i64,
            semantic_type = !bytes, sym_name = "source"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.result",
            lifetime = 1 : i32, name = "result", node_id = 5 : i64,
            semantic_type = !packed, sym_name = "result"} {
          // expected-error@+1 {{bit-stream cast source and destination widths differ (16 vs 24)}}
          obelisk.sv.expression.conversion attributes {is_implicit = false,
              is_signed = false, node_id = 6 : i64, semantic_type = !packed} {
            obelisk.sv.expression.named_value attributes {is_signed = false,
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

!bytes = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.integral<8, false, true, 7 : 0, logic>>
!packed = !obelisk.integral<16, false, true, 15 : 0, logic>

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32,
      hierarchical_name = "top", name = "top", node_id = 0 : i64,
      sym_name = "top_def"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 1 : i64, sym_name = "root"} {
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top",
        is_uninstantiated = false, name = "top", node_id = 2 : i64,
        referenced_path = "top", referenced_symbol = @top_def,
        sym_name = "top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top",
          name = "top", node_id = 3 : i64, sym_name = "body"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.source",
            lifetime = 1 : i32, name = "source", node_id = 4 : i64,
            semantic_type = !bytes, sym_name = "source"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.result",
            lifetime = 1 : i32, name = "result", node_id = 5 : i64,
            semantic_type = !packed, sym_name = "result"} {
          // expected-error@+1 {{cannot convert unpacked aggregate}}
          obelisk.sv.expression.conversion attributes {is_implicit = true,
              is_signed = false, node_id = 6 : i64, semantic_type = !packed} {
            obelisk.sv.expression.named_value attributes {is_signed = false,
                node_id = 7 : i64, referenced_path = "top.source",
                referenced_symbol = @root::@top::@body::@source,
                semantic_type = !bytes} {}
          }
        }
      }
    }
  }
}
