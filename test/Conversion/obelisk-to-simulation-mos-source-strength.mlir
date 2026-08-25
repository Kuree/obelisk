// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// Hand-authored semantic IR checks the source-net specialization boundary:
// an undelayed MOS primitive becomes a directed controlled topology edge and
// publishes only its normalized control. The ordinary output driver is gone.
module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "mos_topology", name = "mos_topology", node_id = 0 : i64, sym_name = "s0.mos_topology"} {
    obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
      obelisk.sv.symbol.instance attributes {hierarchical_name = "mos_topology", is_uninstantiated = false, name = "mos_topology", node_id = 2 : i64, referenced_path = "mos_topology", referenced_symbol = @s0.mos_topology, sym_name = "s2.mos_topology"} {
        obelisk.sv.symbol.instance_body attributes {hierarchical_name = "mos_topology", name = "mos_topology", node_id = 3 : i64, sym_name = "s3.mos_topology", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.symbol.net attributes {hierarchical_name = "mos_topology.source", is_implicit = false, name = "source", net_kind = 1 : i32, node_id = 4 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s4.source"} {
          }
          obelisk.sv.symbol.net attributes {hierarchical_name = "mos_topology.out", is_implicit = false, name = "out", net_kind = 1 : i32, node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.out"} {
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "mos_topology.control", lifetime = 1 : i32, name = "control", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.control"} {
          }
          obelisk.sv.symbol.primitive_instance attributes {hierarchical_name = "mos_topology.m", name = "m", node_id = 7 : i64, primitive_name = "nmos", sym_name = "s7.m", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 8 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 9 : i64, referenced_path = "mos_topology.out", referenced_symbol = @s1.$root::@s2.mos_topology::@s3.mos_topology::@s5.out, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
              obelisk.sv.expression.empty_argument attributes {is_signed = false, node_id = 10 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
            }
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 11 : i64, referenced_path = "mos_topology.source", referenced_symbol = @s1.$root::@s2.mos_topology::@s3.mos_topology::@s4.source, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 12 : i64, referenced_path = "mos_topology.control", referenced_symbol = @s1.$root::@s2.mos_topology::@s3.mos_topology::@s6.control, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
        }
      }
    }
  }
}

// CHECK: obelisk_sim.net.pass.decl {{[0-9]+}} in {{[0-9]+}} {{[0-9]+}}[0] to {{[0-9]+}}[0] width 1 reversed = false
// CHECK-SAME: controlled = true
// CHECK-SAME: directed = true
// CHECK: obelisk_sim.net.pass.control {{[0-9]+}} =
// CHECK-NOT: obelisk_sim.driver.decl
