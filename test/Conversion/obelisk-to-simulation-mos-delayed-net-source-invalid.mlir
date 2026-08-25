// RUN: not obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s

// A delayed resolved-net source cannot fall back to a fixed-strength driver:
// that would silently lose a same-value strength transition. Keep the precise
// implementation boundary diagnosed until a strength-carrying inertial edge
// is available.
module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "delayed_mos", name = "delayed_mos", node_id = 0 : i64, sym_name = "s0.delayed_mos"} {
    obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
      obelisk.sv.symbol.instance attributes {hierarchical_name = "delayed_mos", is_uninstantiated = false, name = "delayed_mos", node_id = 2 : i64, referenced_path = "delayed_mos", referenced_symbol = @s0.delayed_mos, sym_name = "s2.delayed_mos"} {
        obelisk.sv.symbol.instance_body attributes {hierarchical_name = "delayed_mos", name = "delayed_mos", node_id = 3 : i64, sym_name = "s3.delayed_mos", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.symbol.net attributes {hierarchical_name = "delayed_mos.source", is_implicit = false, name = "source", net_kind = 1 : i32, node_id = 4 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s4.source"} {
          }
          obelisk.sv.symbol.net attributes {hierarchical_name = "delayed_mos.out", is_implicit = false, name = "out", net_kind = 1 : i32, node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.out"} {
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "delayed_mos.control", lifetime = 1 : i32, name = "control", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.control"} {
          }
          obelisk.sv.symbol.primitive_instance attributes {delay_fs = array<i64: 1000000>, hierarchical_name = "delayed_mos.m", name = "m", node_id = 7 : i64, primitive_name = "nmos", sym_name = "s7.m", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 8 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 9 : i64, referenced_path = "delayed_mos.out", referenced_symbol = @s1.$root::@s2.delayed_mos::@s3.delayed_mos::@s5.out, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
              obelisk.sv.expression.empty_argument attributes {is_signed = false, node_id = 10 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
            }
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 11 : i64, referenced_path = "delayed_mos.source", referenced_symbol = @s1.$root::@s2.delayed_mos::@s3.delayed_mos::@s4.source, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 12 : i64, referenced_path = "delayed_mos.control", referenced_symbol = @s1.$root::@s2.delayed_mos::@s3.delayed_mos::@s6.control, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
        }
      }
    }
  }
}

// CHECK: delayed MOS/CMOS with a resolved-net source requires strength-preserving inertial topology delay support
