// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' 2>&1 \
// RUN:   | FileCheck %s --check-prefix=DIAG
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null

// IEEE 1800-2017 9.2.2.1: an always procedure with no timing control cannot
// advance simulation time, so the run never terminates. What the rest of the
// slot does is left open -- 4.7 lets a simulator suspend a process that
// reached no time control and interleave others, but does not require it, and
// 4.6 promises no fairness -- which means interleaving would produce values
// that depend on an ordering the standard does not define. Report the
// deadlock rather than manufacture a result, and leave the loop as written.

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "always_zero_delay_yield", name = "always_zero_delay_yield", node_id = 0 : i64, sym_name = "s0.always_zero_delay_yield"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "always_zero_delay_yield", is_uninstantiated = false, name = "always_zero_delay_yield", node_id = 3 : i64, referenced_path = "always_zero_delay_yield", referenced_symbol = @s0.always_zero_delay_yield, sym_name = "s3.always_zero_delay_yield"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "always_zero_delay_yield", name = "always_zero_delay_yield", node_id = 4 : i64, sym_name = "s4.always_zero_delay_yield", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "always_zero_delay_yield.c", lifetime = 1 : i32, name = "c", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, reg>, sym_name = "s5.c"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "always_zero_delay_yield.spun", lifetime = 1 : i32, name = "spun", node_id = 6 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>, sym_name = "s6.spun"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "always_zero_delay_yield.clocked", lifetime = 1 : i32, name = "clocked", node_id = 7 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>, sym_name = "s7.clocked"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "always_zero_delay_yield", node_id = 8 : i64, procedure_kind = 2 : i32, sym_name = "s8", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 9 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 10 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 11 : i64, referenced_path = "always_zero_delay_yield.spun", referenced_symbol = @s1.$root::@s3.always_zero_delay_yield::@s4.always_zero_delay_yield::@s6.spun, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>} {
              }
              obelisk.sv.expression.conversion attributes {folded_constant = "4'b101", is_signed = false, node_id = 12 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "4'b101", is_signed = false, node_id = 13 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "always_zero_delay_yield", node_id = 14 : i64, procedure_kind = 2 : i32, sym_name = "s9", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.timed attributes {node_id = 15 : i64} {
            obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 16 : i64} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 17 : i64, referenced_path = "always_zero_delay_yield.c", referenced_symbol = @s1.$root::@s3.always_zero_delay_yield::@s4.always_zero_delay_yield::@s5.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, reg>} {
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 18 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 19 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 20 : i64, referenced_path = "always_zero_delay_yield.clocked", referenced_symbol = @s1.$root::@s3.always_zero_delay_yield::@s4.always_zero_delay_yield::@s7.clocked, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>} {
                }
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 21 : i64, referenced_path = "always_zero_delay_yield.spun", referenced_symbol = @s1.$root::@s3.always_zero_delay_yield::@s4.always_zero_delay_yield::@s6.spun, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>} {
                }
              }
            }
          }
        }
      }
    }
  }
}


// DIAG: warning: always procedure has no timing control

// The zero-delay loop keeps its plain back edge; nothing is scheduled for it.
// CHECK: obelisk_sim.func private @unit_0(
// CHECK: cf.br ^[[SPUN:.*]]
// CHECK: ^[[SPUN]]:
// CHECK-NOT: obelisk_sim.suspend
// CHECK: cf.br ^[[SPUN]]

// A loop that already suspends is a working process and is left untouched.
// CHECK: obelisk_sim.func private @unit_1(
// CHECK: cf.br ^[[CLOCKED:.*]]
// CHECK: ^[[CLOCKED]]:
// CHECK-NEXT: obelisk_sim.suspend.edge posedge
// CHECK: cf.br ^[[CLOCKED]]
