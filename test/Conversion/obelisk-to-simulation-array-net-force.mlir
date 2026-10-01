// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null

// IEEE 1800-2017 10.6.2: a constant element selection of a fixed unpacked
// array of built-in nets is a force/release target. The array is flattened in
// declaration order, so [2] is the two-bit window at offset 0 and [1] is the
// window at offset 2. A force whose RHS names a variable remains dynamically
// reevaluated.

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition @s0.array_net_force attributes {definition_kind = 0 : i32, hierarchical_name = "array_net_force", name = "array_net_force", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    obelisk.sv.symbol.instance @s3.array_net_force attributes {hierarchical_name = "array_net_force", is_uninstantiated = false, name = "array_net_force", node_id = 3 : i64, referenced_path = "array_net_force", referenced_symbol = @s0.array_net_force} {
      obelisk.sv.symbol.instance_body @s4.array_net_force attributes {hierarchical_name = "array_net_force", name = "array_net_force", node_id = 4 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.net @s5.n attributes {hierarchical_name = "array_net_force.n", is_implicit = false, name = "n", net_kind = 1 : i32, node_id = 5 : i64, semantic_type = !obelisk.ranged_unpacked_array<2 : 1 x !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>} {
        }
        obelisk.sv.symbol.variable @s6.value attributes {hierarchical_name = "array_net_force.value", lifetime = 1 : i32, name = "value", node_id = 6 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>} {
        }
        obelisk.sv.symbol.procedural_block @s7 attributes {hierarchical_name = "array_net_force", node_id = 7 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 8 : i64} {
            obelisk.sv.statement.list attributes {node_id = 9 : i64} {
              obelisk.sv.statement.procedural_assign attributes {is_force = true, node_id = 10 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 11 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.element_select attributes {is_signed = false, node_id = 12 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 13 : i64, referenced_path = "array_net_force.n", referenced_symbol = @s1.$root::@s3.array_net_force::@s4.array_net_force::@s5.n, semantic_type = !obelisk.ranged_unpacked_array<2 : 1 x !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 14 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.expression.conversion attributes {folded_constant = "2'b1", is_implicit = true, is_signed = false, node_id = 15 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "2'b1", is_signed = false, node_id = 16 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.procedural_deassign attributes {is_release = true, node_id = 17 : i64} {
                obelisk.sv.expression.element_select attributes {is_signed = false, node_id = 18 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 19 : i64, referenced_path = "array_net_force.n", referenced_symbol = @s1.$root::@s3.array_net_force::@s4.array_net_force::@s5.n, semantic_type = !obelisk.ranged_unpacked_array<2 : 1 x !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 20 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.statement.procedural_assign attributes {is_force = true, node_id = 21 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 22 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.element_select attributes {is_signed = false, node_id = 23 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 24 : i64, referenced_path = "array_net_force.n", referenced_symbol = @s1.$root::@s3.array_net_force::@s4.array_net_force::@s5.n, semantic_type = !obelisk.ranged_unpacked_array<2 : 1 x !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "2", is_declared_unsized = true, is_signed = true, node_id = 25 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 26 : i64, referenced_path = "array_net_force.value", referenced_symbol = @s1.$root::@s3.array_net_force::@s4.array_net_force::@s6.value, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, reg>>} {
                  }
                }
              }
              obelisk.sv.statement.procedural_deassign attributes {is_release = true, node_id = 27 : i64} {
                obelisk.sv.expression.element_select attributes {is_signed = false, node_id = 28 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 29 : i64, referenced_path = "array_net_force.n", referenced_symbol = @s1.$root::@s3.array_net_force::@s4.array_net_force::@s5.n, semantic_type = !obelisk.ranged_unpacked_array<2 : 1 x !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "2", is_declared_unsized = true, is_signed = true, node_id = 30 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
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

// CHECK: simulation.func private @unit_0(
// CHECK-SAME: %[[NET:[^:]*]]: !simulation.net<!simulation.unpacked_array<2 : 1 x !simulation.packed_array<1 : 0 x !simulation.logic<1>>>>
// CHECK: %[[ONE:.*]] = simulation.net.extract %[[NET]] from 2
// CHECK-SAME: -> !simulation.net<!simulation.packed_array<1 : 0 x !simulation.logic<1>>>
// CHECK: simulation.override %[[ONE]] = {{.*}} assign false
// CHECK: simulation.release_override %[[ONE]] assign false
// CHECK: %[[TWO:.*]] = simulation.net.extract %[[NET]] from 0
// CHECK: simulation.spawn @{{.*override.*}}
// CHECK: simulation.dynamic_override %[[TWO]] = {{.*}} owner {{.*}} assign false claim true
// CHECK: simulation.release_override %[[TWO]] assign false
