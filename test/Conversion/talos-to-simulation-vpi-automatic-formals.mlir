// RUN: talos-opt %s --talos-sim-prepare | FileCheck %s --check-prefix=PREPARE
// RUN: talos-opt %s '--lower-talos-to-sim=vpi=full opt-level=0' | FileCheck %s
// Formal declaration order is source order, independent of reordered call ABI
// arguments and output-copy destinations.
// CHECK: simulation.code_unit.decl [[TASK:[0-9]+]] in 1 task hierarchy "automatic_formals.step"
// CHECK: simulation.vpi_local.decl @{{.*}} id 1 in [[TASK]] scope 1 ordinal 0 hierarchy "automatic_formals.step.produced" debug "produced" : i32
// CHECK-NEXT: simulation.vpi_local.decl @{{.*}} id 2 in [[TASK]] scope 1 ordinal 1 hierarchy "automatic_formals.step.added" debug "added" : i32
// CHECK-NEXT: simulation.vpi_local.decl @{{.*}} id 3 in [[TASK]] scope 1 ordinal 2 hierarchy "automatic_formals.step.reference" debug "reference" : i32
// CHECK-NOT: simulation.vpi_local.decl
// The ref formal keeps source identity, but it is not a new owned automatic
// location. Its arbitrary caller reference must not acquire a local bind.
// PREPARE: #simulation.argument_binding<path = "automatic_formals.step.produced", argument = 1, kind = formal_local, copyOut = true, copyIn = false, vpiLocal = @__talos_vpi_local_1>
// PREPARE-SAME: #simulation.argument_binding<path = "automatic_formals.step.produced", argument = 2, kind = copy_out_destination, copyOut = false>
// PREPARE-SAME: #simulation.argument_binding<path = "automatic_formals.step.added", argument = 3, kind = formal_local, copyOut = false, vpiLocal = @__talos_vpi_local_2>
// PREPARE-SAME: #simulation.argument_binding<path = "automatic_formals.step.reference", argument = 4, kind = direct, copyOut = false>
module {
  talos.sv.symbol.definition @s0.automatic_formals attributes {definition_kind = 0 : i32, hierarchical_name = "automatic_formals", name = "automatic_formals", node_id = 0 : i64} {
  }
  talos.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    talos.sv.symbol.instance @s3.automatic_formals attributes {hierarchical_name = "automatic_formals", is_uninstantiated = false, name = "automatic_formals", node_id = 3 : i64, referenced_path = "automatic_formals", referenced_symbol = @s0.automatic_formals} {
      talos.sv.symbol.instance_body @s4.automatic_formals attributes {hierarchical_name = "automatic_formals", name = "automatic_formals", node_id = 4 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        talos.sv.symbol.subroutine @s5.step attributes {default_lifetime = 0 : i32, hierarchical_name = "automatic_formals.step", name = "step", node_id = 5 : i64, semantic_type = !talos.subroutine<(!talos.integral<32, true, false, 31 : 0, int>, !talos.integral<32, true, false, 31 : 0, int>, !talos.integral<32, true, false, 31 : 0, int>) -> (), true>, subroutine_kind = 1 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.expression_statement attributes {node_id = 6 : i64} {
            talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 7 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
              talos.sv.expression.named_value attributes {is_signed = true, node_id = 8 : i64, referenced_path = "automatic_formals.step.produced", referenced_symbol = @s1.$root::@s3.automatic_formals::@s4.automatic_formals::@s5.step::@s6.produced, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
              }
              talos.sv.expression.binary_op attributes {is_signed = true, node_id = 9 : i64, operator_kind = 0 : i32, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                talos.sv.expression.named_value attributes {is_signed = true, node_id = 10 : i64, referenced_path = "automatic_formals.step.produced", referenced_symbol = @s1.$root::@s3.automatic_formals::@s4.automatic_formals::@s5.step::@s6.produced, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                }
                talos.sv.expression.named_value attributes {is_signed = true, node_id = 11 : i64, referenced_path = "automatic_formals.step.added", referenced_symbol = @s1.$root::@s3.automatic_formals::@s4.automatic_formals::@s5.step::@s7.added, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
          talos.sv.symbol.formal_argument @s6.produced attributes {direction = 1 : i32, hierarchical_name = "automatic_formals.step.produced", lifetime = 0 : i32, name = "produced", node_id = 12 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
          }
          talos.sv.symbol.formal_argument @s7.added attributes {direction = 0 : i32, hierarchical_name = "automatic_formals.step.added", lifetime = 0 : i32, name = "added", node_id = 13 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
          }
          talos.sv.symbol.formal_argument @reference attributes {direction = 3 : i32, hierarchical_name = "automatic_formals.step.reference", name = "reference", node_id = 22 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {}
        }
        talos.sv.symbol.variable @s8.result attributes {hierarchical_name = "automatic_formals.result", lifetime = 1 : i32, name = "result", node_id = 14 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
        }
        talos.sv.symbol.procedural_block @s9 attributes {hierarchical_name = "automatic_formals", node_id = 15 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.expression_statement attributes {node_id = 16 : i64} {
            talos.sv.expression.call attributes {argument_count = 3 : i64, callee_name = "step", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = false, node_id = 17 : i64, referenced_path = "automatic_formals.step", referenced_symbol = @s1.$root::@s3.automatic_formals::@s4.automatic_formals::@s5.step, semantic_type = !talos.void, subroutine_kind = 1 : i32} {
              talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 18 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                talos.sv.expression.named_value attributes {is_signed = true, node_id = 19 : i64, referenced_path = "automatic_formals.result", referenced_symbol = @s1.$root::@s3.automatic_formals::@s4.automatic_formals::@s8.result, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                }
                talos.sv.expression.empty_argument attributes {is_signed = true, node_id = 20 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                }
              }
              talos.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 21 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
              }
              talos.sv.expression.named_value attributes {is_signed = true, node_id = 23 : i64, referenced_path = "automatic_formals.result", referenced_symbol = @s1.$root::@s3.automatic_formals::@s4.automatic_formals::@s8.result, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {}
            }
          }
        }
      }
    }
  }
}
