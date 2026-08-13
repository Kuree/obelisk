// RUN: obelisk-opt %s --obelisk-sim-prepare | FileCheck %s

// IEEE 1800-2017 6.21 gives variables in a static task/function static
// lifetime. An explicitly initialized static local is initialized once at the
// beginning of simulation, even though its symbol is nested in the subroutine.

module {
  obelisk.sv.symbol.root attributes {
      hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64,
      sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {
        hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.symbol.subroutine attributes {
          default_lifetime = 1 : i32, hierarchical_name = "T", name = "T",
          node_id = 3 : i64,
          semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>,
          subroutine_kind = 1 : i32, sym_name = "s3.T"} {
        obelisk.sv.statement.list attributes {node_id = 4 : i64} {
          obelisk.sv.statement.variable_declaration attributes {
              node_id = 5 : i64, referenced_path = "T.value",
              referenced_symbol = @s1.$root::@s2::@s3.T::@s4.value} {
          }
        }
        obelisk.sv.symbol.variable attributes {
            hierarchical_name = "T.value", lifetime = 1 : i32, name = "value",
            node_id = 6 : i64,
            semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
            sym_name = "s4.value"} {
          obelisk.sv.expression.integer_literal attributes {
              constant_value = "9", is_signed = true, node_id = 7 : i64,
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
      }
    }
  }
}

// The nested local remains descriptor-backed storage.
// CHECK: obelisk_sim.storage.decl {{[0-9]+}} in 0 : i32 design hierarchy "T.value"

// Its initializer is called by the root before simulation processes run.
// CHECK-LABEL: obelisk_sim.func @__obelisk_root
// CHECK: obelisk_sim.call @unit_1
// CHECK: obelisk_sim.return

// The task declaration is only a lexical occurrence of the static object; it
// does not clone the initializer for evaluation on task entry.
// CHECK-LABEL: obelisk_sim.func private @unit_0
// CHECK: obelisk.sv.statement.variable_declaration
// CHECK-NOT: obelisk.sv.expression.integer_literal
// CHECK: obelisk_sim.return

// CHECK-LABEL: obelisk_sim.func private @unit_1
// CHECK: obelisk.sv.expression.integer_literal attributes
// CHECK-SAME: constant_value = "9"
// CHECK-SAME: obelisk_sim.initialize_static = "T.value"
// CHECK: obelisk_sim.return
