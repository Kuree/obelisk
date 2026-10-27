// RUN: talos-opt %s --talos-sim-prepare | FileCheck %s

// IEEE 1800-2017 6.21 gives variables in a static task/function static
// lifetime. An explicitly initialized static local is initialized once at the
// beginning of simulation, even though its symbol is nested in the subroutine.

module {
  talos.sv.symbol.root @s1.$root attributes {
      hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64
  } {
    talos.sv.symbol.compilation_unit @s2 attributes {
        hierarchical_name = "$unit", node_id = 2 : i64} {
      talos.sv.symbol.subroutine @s3.T attributes {
          default_lifetime = 1 : i32, hierarchical_name = "T", name = "T",
          node_id = 3 : i64,
          semantic_type = !talos.subroutine<() -> !talos.void, false>,
          subroutine_kind = 1 : i32} {
        talos.sv.statement.list attributes {node_id = 4 : i64} {
          talos.sv.statement.variable_declaration attributes {
              node_id = 5 : i64, referenced_path = "T.value",
              referenced_symbol = @s1.$root::@s2::@s3.T::@s4.value} {
          }
        }
        talos.sv.symbol.variable @s4.value attributes {
            hierarchical_name = "T.value", lifetime = 1 : i32, name = "value",
            node_id = 6 : i64,
            semantic_type = !talos.integral<32, true, false, 31 : 0, int>
        } {
          talos.sv.expression.integer_literal attributes {
              constant_value = "9", is_signed = true, node_id = 7 : i64,
              semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
          }
        }
      }
    }
  }
}

// The nested local remains descriptor-backed storage.
// CHECK: simulation.storage.decl {{[0-9]+}} in 0 : i32 design hierarchy "T.value"

// Its initializer is called by the root before simulation processes run.
// CHECK-LABEL: simulation.func @__talos_root
// CHECK: simulation.call @unit_1
// CHECK: simulation.return

// The task declaration is only a lexical occurrence of the static object; it
// does not clone the initializer for evaluation on task entry.
// CHECK-LABEL: simulation.func private @unit_0
// CHECK: talos.sv.statement.variable_declaration
// CHECK-NOT: talos.sv.expression.integer_literal
// CHECK: simulation.return

// CHECK-LABEL: simulation.func private @unit_1
// CHECK: talos.sv.expression.integer_literal attributes
// CHECK-SAME: constant_value = "9"
// CHECK-SAME: simulation.initialize_static = "T.value"
// CHECK: simulation.return
