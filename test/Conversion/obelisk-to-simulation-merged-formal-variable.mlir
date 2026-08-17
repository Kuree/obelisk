// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 13.3: a non-ANSI subroutine port and the data declaration
// that gives it a type declare one object. The declaration keeps its own
// symbol linked to the port through `merged_variable_symbol`, so a reference
// through it is a reference to the formal and must not also capture the
// formal's storage as a separate descriptor.

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "dual_decl", name = "dual_decl", node_id = 0 : i64, sym_name = "s0.dual_decl"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "dual_decl", is_uninstantiated = false, name = "dual_decl", node_id = 3 : i64, referenced_path = "dual_decl", referenced_symbol = @s0.dual_decl, sym_name = "s3.dual_decl"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "dual_decl", name = "dual_decl", node_id = 4 : i64, sym_name = "s4.dual_decl", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.subroutine attributes {default_lifetime = 1 : i32, hierarchical_name = "dual_decl.clog2", name = "clog2", node_id = 5 : i64, return_variable_path = "dual_decl.clog2.clog2", return_variable_symbol = @s1.$root::@s3.dual_decl::@s4.dual_decl::@s5.clog2::@s6.clog2, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, true, 31 : 0, integer>) -> !obelisk.integral<32, true, true, 31 : 0, integer>, false>, subroutine_kind = 0 : i32, sym_name = "s5.clog2", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 6 : i64} {
            obelisk.sv.statement.variable_declaration attributes {node_id = 7 : i64, referenced_path = "dual_decl.clog2.value", referenced_symbol = @s1.$root::@s3.dual_decl::@s4.dual_decl::@s5.clog2::@s8.value} {
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 8 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 9 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 10 : i64, referenced_path = "dual_decl.clog2.clog2", referenced_symbol = @s1.$root::@s3.dual_decl::@s4.dual_decl::@s5.clog2::@s6.clog2, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                }
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 11 : i64, referenced_path = "dual_decl.clog2.value", referenced_symbol = @s1.$root::@s3.dual_decl::@s4.dual_decl::@s5.clog2::@s7.value, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                }
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "dual_decl.clog2.clog2", is_compiler_generated, name = "clog2", node_id = 12 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, sym_name = "s6.clog2"} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "dual_decl.clog2.value", lifetime = 1 : i32, merged_variable_path = "dual_decl.clog2.value", merged_variable_symbol = @s1.$root::@s3.dual_decl::@s4.dual_decl::@s5.clog2::@s8.value, name = "value", node_id = 13 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, sym_name = "s7.value"} {
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "dual_decl.clog2.value", lifetime = 1 : i32, name = "value", node_id = 14 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, sym_name = "s8.value"} {
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "dual_decl", node_id = 15 : i64, procedure_kind = 0 : i32, sym_name = "s9", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 16 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "clog2", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = false, node_id = 18 : i64, referenced_path = "dual_decl.clog2", referenced_symbol = @s1.$root::@s3.dual_decl::@s4.dual_decl::@s5.clog2, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, subroutine_kind = 0 : i32} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "3", is_declared_unsized = true, is_signed = true, node_id = 20 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
          }
        }
      }
    }
  }
}

// The formal has one storage descriptor and the function body reaches it
// through its argument, not through a second capture of the same path.

// CHECK: obelisk_sim.storage.decl [[VALUE:[0-9]+]] in {{[0-9]+}} : !obelisk_sim.logic<32> static hierarchy "dual_decl.clog2.value"
// CHECK-NOT: hierarchy "dual_decl.clog2.value"
// CHECK: obelisk_sim.func private @unit_0(
// CHECK-NOT: obelisk_sim.descriptor_id = [[VALUE]]
