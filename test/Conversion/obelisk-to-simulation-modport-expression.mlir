// RUN: obelisk-opt %s --obelisk-sim-prepare | FileCheck %s

// IEEE 1800-2017 25.5.4 permits a modport member to rename an expression
// over an item declared in the interface. The expression, not the visible
// modport name, identifies the descriptor that the member aliases.

module {
  obelisk.sv.symbol.root @root attributes {
      hierarchical_name = "\\$root ", name = "$root", node_id = 0 : i64
  } {
    obelisk.sv.symbol.instance @instance attributes {
        hierarchical_name = "top", is_uninstantiated = false, name = "top",
        node_id = 1 : i64, referenced_path = "top",
        referenced_symbol = @definition} {
      obelisk.sv.symbol.instance_body @body attributes {
          hierarchical_name = "top", name = "top", node_id = 2 : i64
      } {
        obelisk.sv.symbol.variable @signal attributes {
            hierarchical_name = "top.signal", lifetime = 1 : i32,
            name = "signal", node_id = 3 : i64,
            semantic_type = !obelisk.integral<16, false, true, 15 : 0, logic>
        } {
        }
        obelisk.sv.symbol.variable @sink attributes {
            hierarchical_name = "top.sink", lifetime = 1 : i32,
            name = "sink", node_id = 20 : i64,
            semantic_type = !obelisk.integral<8, false, true, 7 : 0, logic>
        } {
        }
        obelisk.sv.symbol.modport @consumer attributes {
            hierarchical_name = "top.consumer", name = "consumer",
            node_id = 4 : i64} {
          obelisk.sv.symbol.modport_port @renamed attributes {
              direction = 0 : i32,
              hierarchical_name = "top.consumer.renamed",
              name = "renamed", node_id = 5 : i64,
              semantic_type = !obelisk.integral<16, false, true, 15 : 0, logic>
          } {
            obelisk.sv.expression.named_value attributes {
                is_signed = false, node_id = 6 : i64,
                referenced_path = "top.signal",
                referenced_symbol = @root::@instance::@body::@signal,
                semantic_type = !obelisk.integral<16, false, true, 15 : 0, logic>} {
            }
          }
          obelisk.sv.symbol.modport_port @high attributes {
              direction = 0 : i32,
              hierarchical_name = "top.consumer.high",
              name = "high", node_id = 7 : i64,
              semantic_type = !obelisk.integral<8, false, true, 15 : 8, logic>
          } {
            obelisk.sv.expression.range_select attributes {
                is_signed = false, node_id = 8 : i64, selection_kind = 0 : i32,
                semantic_type = !obelisk.integral<8, false, true, 15 : 8, logic>} {
              obelisk.sv.expression.named_value attributes {
                  is_signed = false, node_id = 9 : i64,
                  referenced_path = "top.signal",
                  referenced_symbol = @root::@instance::@body::@signal,
                  semantic_type = !obelisk.integral<16, false, true, 15 : 0, logic>} {
              }
              obelisk.sv.expression.integer_literal attributes {
                  constant_value = "15", is_declared_unsized = true,
                  is_signed = true, node_id = 10 : i64,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
              obelisk.sv.expression.integer_literal attributes {
                  constant_value = "8", is_declared_unsized = true,
                  is_signed = true, node_id = 11 : i64,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
          }
        }
        obelisk.sv.symbol.continuous_assign @assign attributes {
            hierarchical_name = "top", node_id = 21 : i64
        } {
          obelisk.sv.expression.assignment attributes {
              assignment_kind = 0 : i32, is_signed = false,
              node_id = 22 : i64,
              semantic_type = !obelisk.integral<8, false, true, 7 : 0, logic>} {
            obelisk.sv.expression.named_value attributes {
                is_signed = false, node_id = 23 : i64,
                referenced_path = "top.sink",
                referenced_symbol = @root::@instance::@body::@sink,
                semantic_type = !obelisk.integral<8, false, true, 7 : 0, logic>} {
            }
            obelisk.sv.expression.hierarchical_value attributes {
                is_signed = false, node_id = 24 : i64,
                referenced_path = "top.consumer.high",
                referenced_symbol = @root::@instance::@body::@consumer::@high,
                semantic_type = !obelisk.integral<8, false, true, 15 : 8, logic>} {
            }
          }
        }
      }
    }
  }
  obelisk.sv.symbol.definition @definition attributes {
      definition_kind = 1 : i32, hierarchical_name = "top", name = "top",
      node_id = 12 : i64} {
  }
}

// CHECK: simulation.storage.decl 0 {{.*}}logic<16>{{.*}}hierarchy "top.signal"
// CHECK: simulation.storage.decl 1 {{.*}}logic<8>{{.*}}hierarchy "top.sink"
// CHECK: simulation.func private @{{.*}}(
// CHECK-SAME: descriptor_id = 0 : i64
// CHECK-SAME: descriptor_low = 8 : i64
// CHECK-SAME: descriptor_packed_low = 8 : i64
// CHECK-NOT: interface modport member has no flattened target
