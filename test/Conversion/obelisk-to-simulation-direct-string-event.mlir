// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// A direct string-variable change event subscribes to the storage descriptor.
// String stores already publish only after exact content comparison, so no
// value observer, hash, or retained previous heap value is required.

module {
  obelisk.sv.symbol.definition @s0.top attributes {
      definition_kind = 0 : i32, hierarchical_name = "top", name = "top",
      node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {
      hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64
  } {
    obelisk.sv.symbol.compilation_unit @s2 attributes {
        hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    obelisk.sv.symbol.instance @s3.top attributes {
        hierarchical_name = "top", is_uninstantiated = false, name = "top",
        node_id = 3 : i64, referenced_path = "top",
        referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s4.top attributes {
          hierarchical_name = "top", name = "top", node_id = 4 : i64,
          time_precision_fs = 1000000 : i64,
          time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable @s5.value attributes {
            hierarchical_name = "top.value", lifetime = 1 : i32,
            name = "value", node_id = 5 : i64,
            semantic_type = !obelisk.string} {
        }
        obelisk.sv.symbol.procedural_block @s6 attributes {
            hierarchical_name = "top", node_id = 6 : i64,
            procedure_kind = 2 : i32,
            time_precision_fs = 1000000 : i64,
            time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.timed attributes {node_id = 7 : i64} {
            obelisk.sv.timing.signal_event attributes {
                edge_kind = 0 : i32, has_iff = false, node_id = 8 : i64} {
              obelisk.sv.expression.named_value attributes {
                  is_signed = false, node_id = 9 : i64,
                  referenced_path = "top.value",
                  referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.value,
                  semantic_type = !obelisk.string} {
              }
            }
            obelisk.sv.statement.empty attributes {node_id = 10 : i64} {
            }
          }
        }
      }
    }
  }
}

// CHECK-NOT: entry_kind = 7
// CHECK-NOT: simulation.observer
// CHECK: simulation.func private @unit_0
// CHECK: simulation.suspend.change %{{.*}} to ^{{.*}} {{.*}} : !simulation.ref<!simulation.string>
