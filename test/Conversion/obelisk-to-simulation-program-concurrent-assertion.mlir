// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 24.3.1 gives a concurrent assertion the same scheduling in
// program and design code. Its synthetic monitor evaluates in Observed and is
// not a program process; the assertion action is separately run in Reactive.

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 2 : i32, hierarchical_name = "p", name = "p", node_id = 0 : i64, sym_name = "s0.p"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {}
    obelisk.sv.symbol.instance attributes {hierarchical_name = "p", is_uninstantiated = false, name = "p", node_id = 3 : i64, referenced_path = "p", referenced_symbol = @s0.p, sym_name = "s3.p"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "p", name = "p", node_id = 4 : i64, sym_name = "s4.p"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "p.clk", lifetime = 1 : i32, name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.clk"} {}
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "p", node_id = 6 : i64, procedure_kind = 2 : i32, sym_name = "s6", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = false, node_id = 7 : i64} {
            obelisk.sv.assertion.clocking attributes {node_id = 8 : i64} {
              obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 9 : i64} {
                obelisk.sv.expression.named_value attributes {node_id = 10 : i64, referenced_path = "p.clk", referenced_symbol = @s1.$root::@s3.p::@s4.p::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {}
              }
              obelisk.sv.assertion.simple attributes {has_repetition = false, is_null = true, node_id = 11 : i64, repetition_is_unbounded = false} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 12 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
              }
            }
          }
        }
      }
    }
  }
}

// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK-SAME: domain = 0 : i32
// CHECK-SAME: home_region = 8 : i32
// CHECK-NOT: obelisk_sim.program_owner_id
// CHECK: obelisk_sim.suspend.edge posedge
// CHECK-SAME: resume_region = 8 : i32
// CHECK-NOT: obelisk.sv.
