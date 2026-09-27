// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 28.9 and 28.13: CMOS is the parallel combination of an
// nmos and pmos, and an uncertain control passes either the source strength or
// high impedance. A variable source cannot use the static net-topology path,
// so retain the L/H range with polarity-specific driver banks.
module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "cmos_strength", name = "cmos_strength", node_id = 0 : i64, sym_name = "s0.cmos_strength"} {
    obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
      obelisk.sv.symbol.instance attributes {hierarchical_name = "cmos_strength", is_uninstantiated = false, name = "cmos_strength", node_id = 2 : i64, referenced_path = "cmos_strength", referenced_symbol = @s0.cmos_strength, sym_name = "s2.cmos_strength"} {
        obelisk.sv.symbol.instance_body attributes {hierarchical_name = "cmos_strength", name = "cmos_strength", node_id = 3 : i64, sym_name = "s3.cmos_strength", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.symbol.variable attributes {hierarchical_name = "cmos_strength.source", lifetime = 1 : i32, name = "source", node_id = 4 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s4.source"} {
          }
          obelisk.sv.symbol.net attributes {hierarchical_name = "cmos_strength.out", is_implicit = false, name = "out", net_kind = 1 : i32, node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.out"} {
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "cmos_strength.ncontrol", lifetime = 1 : i32, name = "ncontrol", node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s6.ncontrol"} {
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "cmos_strength.pcontrol", lifetime = 1 : i32, name = "pcontrol", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s7.pcontrol"} {
          }
          obelisk.sv.symbol.primitive_instance attributes {hierarchical_name = "cmos_strength.m", name = "m", node_id = 8 : i64, primitive_name = "cmos", sym_name = "s8.m", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 9 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              obelisk.sv.expression.named_value attributes {node_id = 10 : i64, referenced_path = "cmos_strength.out", referenced_symbol = @s1.$root::@s2.cmos_strength::@s3.cmos_strength::@s5.out, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
              obelisk.sv.expression.empty_argument attributes {node_id = 11 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
              }
            }
            obelisk.sv.expression.named_value attributes {node_id = 12 : i64, referenced_path = "cmos_strength.source", referenced_symbol = @s1.$root::@s2.cmos_strength::@s3.cmos_strength::@s4.source, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
            obelisk.sv.expression.named_value attributes {node_id = 13 : i64, referenced_path = "cmos_strength.ncontrol", referenced_symbol = @s1.$root::@s2.cmos_strength::@s3.cmos_strength::@s6.ncontrol, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
            obelisk.sv.expression.named_value attributes {node_id = 14 : i64, referenced_path = "cmos_strength.pcontrol", referenced_symbol = @s1.$root::@s2.cmos_strength::@s3.cmos_strength::@s7.pcontrol, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
        }
      }
    }
  }
}

// CHECK: simulation.driver.decl [[LOW:[0-9]+]] in {{[0-9]+}} drives [[NET:[0-9]+]] : !simulation.logic<1> design
// CHECK-SAME: simulation.strength_bank = 0 : i32
// CHECK-SAME: simulation.strength_group = [[GROUP:[0-9]+]] : i64
// CHECK-SAME: strength1 = 0 : i32
// CHECK: simulation.driver.decl [[HIGH:[0-9]+]] in {{[0-9]+}} drives [[NET]] : !simulation.logic<1> design
// CHECK-SAME: simulation.strength_bank = 1 : i32
// CHECK-SAME: simulation.strength_group = [[GROUP]] : i64
// CHECK-SAME: strength0 = 0 : i32
// CHECK-NOT: simulation.net.pass.decl
// CHECK: %[[DATA:.*]] = simulation.ref.load
// CHECK: %[[CONTROL_NOT:.*]] = simulation.logic.unary bit_not
// CHECK: %[[ENABLED:.*]] = simulation.logic.binary or
// CHECK: %[[DATA_NOT:.*]] = simulation.logic.unary bit_not %[[DATA]]
// CHECK: %[[LOW_ENABLE:.*]] = simulation.logic.binary and %[[DATA_NOT]], %[[ENABLED]]
// CHECK: %[[HIGH_ENABLE:.*]] = simulation.logic.binary and %[[DATA]], %[[ENABLED]]
// CHECK: %[[LOW_RANGE:.*]] = simulation.logic.mux %[[LOW_ENABLE]]
// CHECK: %[[HIGH_RANGE:.*]] = simulation.logic.mux %[[HIGH_ENABLE]]
// CHECK: %[[DATA_IS_Z:.*]] = simulation.logic.compare case_eq
// CHECK: %[[LOW:.*]] = arith.select %[[DATA_IS_Z]], %{{.*}}, %[[LOW_RANGE]]
// CHECK: %[[HIGH:.*]] = arith.select %[[DATA_IS_Z]], %{{.*}}, %[[HIGH_RANGE]]
// CHECK: simulation.driver.drive {{.*}} = %[[LOW]]
// CHECK-SAME: schedule.defer_net_resolution
// CHECK: simulation.driver.drive {{.*}} = %[[HIGH]]
