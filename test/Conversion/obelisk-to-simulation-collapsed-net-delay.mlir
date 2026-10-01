// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' | FileCheck %s --check-prefix=O3

module {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "collapsed_delay_top", name = "collapsed_delay_top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.definition @s1.child attributes {definition_kind = 0 : i32, hierarchical_name = "collapsed_delay_child", name = "collapsed_delay_child", node_id = 1 : i64} {
  }
  obelisk.sv.symbol.root @s2.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 2 : i64} {
    obelisk.sv.symbol.compilation_unit @s3 attributes {hierarchical_name = "$unit", node_id = 3 : i64} {
    }
    obelisk.sv.symbol.instance @s4.top attributes {hierarchical_name = "collapsed_delay_top", is_uninstantiated = false, name = "collapsed_delay_top", node_id = 4 : i64, referenced_path = "collapsed_delay_top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s5.top attributes {hierarchical_name = "collapsed_delay_top", name = "collapsed_delay_top", node_id = 5 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.net @s6.external attributes {delay_fs = array<i64: 5000000>, hierarchical_name = "collapsed_delay_top.external", is_implicit = false, name = "external", net_kind = 12 : i32, node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
          obelisk.sv.timing.delay attributes {node_id = 7 : i64} {
            obelisk.sv.expression.integer_literal attributes {constant_value = "5", node_id = 8 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            }
          }
        }
        obelisk.sv.symbol.instance @s7.child attributes {hierarchical_name = "collapsed_delay_top.child", is_uninstantiated = false, name = "child", node_id = 9 : i64, referenced_path = "collapsed_delay_child", referenced_symbol = @s1.child} {
          obelisk.sv.port.connection attributes {actual_is_constant = false, direction = 2 : i32, formal_name = "formal", formal_ordinal = 0 : i64, formal_path = "collapsed_delay_top.child.formal", formal_symbol = @s2.$root::@s4.top::@s5.top::@s7.child::@s8.child::@s9.formal, formal_type = !obelisk.integral<1, false, true, 0 : 0, logic>, internal_path = "collapsed_delay_top.child.formal", internal_symbol = @s2.$root::@s4.top::@s5.top::@s7.child::@s8.child::@s10.formal, is_ansi = true, is_net = true, node_id = 10 : i64, provenance = 0 : i32} {
          } {
            obelisk.sv.expression.named_value attributes {node_id = 11 : i64, referenced_path = "collapsed_delay_top.external", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.external, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
          obelisk.sv.symbol.instance_body @s8.child attributes {hierarchical_name = "collapsed_delay_top.child", name = "collapsed_delay_child", node_id = 12 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.symbol.port @s9.formal attributes {direction = 2 : i32, hierarchical_name = "collapsed_delay_top.child.formal", name = "formal", node_id = 13 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
            obelisk.sv.symbol.net @s10.formal attributes {hierarchical_name = "collapsed_delay_top.child.formal", is_implicit = false, name = "formal", net_kind = 1 : i32, node_id = 14 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
            obelisk.sv.symbol.continuous_assign @s11.assign attributes {hierarchical_name = "collapsed_delay_top.child", node_id = 15 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 16 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                obelisk.sv.expression.named_value attributes {node_id = 17 : i64, referenced_path = "collapsed_delay_top.child.formal", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s7.child::@s8.child::@s10.formal, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.expression.conversion attributes {node_id = 18 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 19 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}

// LRM 23.3.3.7/Table 23-1: an external uwire dominates an internal wire. Its
// delay therefore applies to both logical declarations in the simulated net.
// CHECK-DAG: simulation.net.decl 0 {{.*}} hierarchy "collapsed_delay_top.external" {{.*}}propagation_delays = array<i64: 5, 5, 5>{{.*}}resolution_kind = 2 : i32
// CHECK-DAG: simulation.net.decl 1 {{.*}} hierarchy "collapsed_delay_top.child.formal" {{.*}}propagation_delays = array<i64: 5, 5, 5>
// CHECK: simulation.net.connect.decl 0 {{.*}} 0[0] to 1[0] width 1 reversed = false provenance "ordered" rhs_dominates = false
// CHECK: simulation.func private @unit_0
// CHECK-SAME: simulation.delayed_net
// CHECK: simulation.driver.drive_delayed_net

// O3: simulation.net.connect.decl
// O3-SAME: rhs_dominates = false
// O3-LABEL: simulation.func private @unit_0
// O3: simulation.driver.drive_delayed_net
