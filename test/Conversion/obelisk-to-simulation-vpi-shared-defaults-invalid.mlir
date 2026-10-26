// RUN: obelisk-opt %s --lower-obelisk-to-sim='vpi=read' --verify-diagnostics -o /dev/null
!int = !obelisk.integral<32, true, false, 31 : 0, int>
module {
  obelisk.sv.symbol.definition @cell attributes {node_id = 1 : i64, definition_kind = 0 : i32, hierarchical_name = "cell", name = "cell"} {}
  obelisk.sv.symbol.root @root attributes {node_id = 2 : i64, hierarchical_name = "$root", name = "$root"} {
    obelisk.sv.symbol.instance @left attributes {node_id = 10 : i64, hierarchical_name = "left", is_uninstantiated = false, name = "left", referenced_path = "cell", referenced_symbol = @cell} {
      obelisk.sv.symbol.instance_body @body attributes {node_id = 11 : i64, hierarchical_name = "left", name = "cell"} {
        obelisk.sv.symbol.parameter @P attributes {node_id = 12 : i64, hierarchical_name = "left.P", name = "P", constant_value = "3", semantic_type = !int} {
          obelisk.sv.source.value_default attributes {node_id = 13 : i64} {
            obelisk.sv.expression.integer_literal attributes {node_id = 14 : i64, constant_value = "1", is_signed = true, semantic_type = !int} {}
          }
        }
      }
    }
    obelisk.sv.symbol.instance @right attributes {node_id = 20 : i64, hierarchical_name = "right", is_uninstantiated = false, name = "right", referenced_path = "cell", referenced_symbol = @cell} {
      // expected-error @+1 {{instances of one definition disagree on declaration identity}}
      obelisk.sv.symbol.instance_body @body attributes {node_id = 21 : i64, hierarchical_name = "right", name = "cell"} {
        obelisk.sv.symbol.parameter @P attributes {node_id = 22 : i64, hierarchical_name = "right.P", name = "P", constant_value = "3", semantic_type = !int} {
          obelisk.sv.source.value_default attributes {node_id = 23 : i64} {
            obelisk.sv.expression.integer_literal attributes {node_id = 24 : i64, constant_value = "2", is_signed = true, semantic_type = !int} {}
          }
        }
      }
    }
  }
}
