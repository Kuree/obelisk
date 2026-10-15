// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2023 18.8: when the random variable is an object handle,
// object.random_variable.rand_mode(0) changes only that variable's mode, not
// the modes of the random variables in the object it refers to. Slang binds
// a.child.rand_mode(0) to Child's object-wide builtin; preparation keeps it a
// mode of Parent::child. A handle that is not rand still disables every
// random variable of the object it refers to.
//
//   class Child; rand bit x; endclass
//   class Parent; rand Child child; Child plain; endclass
//   module top;
//     Parent a;
//     initial begin
//       a.child.rand_mode(0);
//       a.plain.rand_mode(0);
//     end
//   endmodule

// CHECK-LABEL: simulation.func private @unit_0(
// CHECK: %[[PARENT_MODES:.*]] = simulation.class.field_ref %{{.*}}[@__obelisk_class_s17_Parent_field___obelisk_rand_mode]
// CHECK: %[[OLD:.*]] = simulation.managed.load %[[PARENT_MODES]]
// CHECK: %[[OFF:.*]] = arith.ori %[[OLD]], %{{.*}}
// CHECK: simulation.managed.store %[[OFF]] to %[[PARENT_MODES]]
// CHECK: %[[CHILD_MODES:.*]] = simulation.class.field_ref %{{.*}}[@__obelisk_class_s3_Child_field___obelisk_rand_mode]
// CHECK: simulation.managed.store %{{.*}} to %[[CHILD_MODES]]

module attributes {coverage.language_version = 2023 : i32} {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
      obelisk.sv.type.class_type @s3.Child attributes {bitstream_width = 1 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "Child", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "Child", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Child>, this_variable_path = "Child::this", this_variable_symbol = @s1.$root::@s2::@s3.Child::@s36.this} {
        obelisk.sv.symbol.class_property @s4.x attributes {hierarchical_name = "Child::x", name = "x", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
        }
        obelisk.sv.symbol.subroutine @s5.randomize attributes {hierarchical_name = "Child::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 5 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 6 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine @s13.rand_mode attributes {hierarchical_name = "Child::rand_mode", is_builtin, name = "rand_mode", node_id = 19 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 20 : i64} {
          }
          obelisk.sv.symbol.formal_argument @s14.on_ff attributes {direction = 0 : i32, hierarchical_name = "Child::rand_mode.on_ff", name = "on_ff", node_id = 21 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
          }
        }
        obelisk.sv.symbol.variable @s36.this attributes {hierarchical_name = "Child::this", is_compiler_generated, is_const, name = "this", node_id = 66 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Child>} {
        }
      }
      obelisk.sv.type.class_type @s17.Parent attributes {bitstream_width = 2 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "Parent", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "Parent", node_id = 25 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s17.Parent>, this_variable_path = "Parent::this", this_variable_symbol = @s1.$root::@s2::@s17.Parent::@s37.this} {
        obelisk.sv.symbol.class_property @s18.child attributes {hierarchical_name = "Parent::child", name = "child", node_id = 26 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Child>} {
        }
        obelisk.sv.symbol.class_property @s19.plain attributes {hierarchical_name = "Parent::plain", name = "plain", node_id = 27 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Child>} {
        }
        obelisk.sv.symbol.subroutine @s20.randomize attributes {hierarchical_name = "Parent::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 28 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 29 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine @s28.rand_mode attributes {hierarchical_name = "Parent::rand_mode", is_builtin, name = "rand_mode", node_id = 42 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 43 : i64} {
          }
          obelisk.sv.symbol.formal_argument @s29.on_ff attributes {direction = 0 : i32, hierarchical_name = "Parent::rand_mode.on_ff", name = "on_ff", node_id = 44 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
          }
        }
        obelisk.sv.symbol.variable @s37.this attributes {hierarchical_name = "Parent::this", is_compiler_generated, is_const, name = "this", node_id = 67 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s17.Parent>} {
        }
      }
    }
    obelisk.sv.symbol.instance @s32.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 48 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s33.top attributes {hierarchical_name = "top", name = "top", node_id = 49 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        obelisk.sv.symbol.variable @s34.a attributes {hierarchical_name = "top.a", lifetime = 1 : i32, name = "a", node_id = 50 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s17.Parent>} {
        }
        obelisk.sv.symbol.procedural_block @s35 attributes {hierarchical_name = "top", node_id = 51 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 52 : i64} {
            obelisk.sv.statement.list attributes {node_id = 53 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 54 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "rand_mode", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = true, is_signed = false, is_super_class = false, is_system_call = false, node_id = 55 : i64, referenced_path = "Child::rand_mode", referenced_symbol = @s1.$root::@s2::@s3.Child::@s13.rand_mode, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
                  obelisk.sv.expression.member_access attributes {is_signed = false, member_name = "child", node_id = 56 : i64, referenced_path = "Parent::child", referenced_symbol = @s1.$root::@s2::@s17.Parent::@s18.child, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Child>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 57 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s32.top::@s33.top::@s34.a, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s17.Parent>} {
                    }
                  }
                  obelisk.sv.expression.conversion attributes {folded_constant = "1'b0", is_implicit = true, is_signed = false, node_id = 58 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 59 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 60 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "rand_mode", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = true, is_signed = false, is_super_class = false, is_system_call = false, node_id = 61 : i64, referenced_path = "Child::rand_mode", referenced_symbol = @s1.$root::@s2::@s3.Child::@s13.rand_mode, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
                  obelisk.sv.expression.member_access attributes {is_signed = false, member_name = "plain", node_id = 62 : i64, referenced_path = "Parent::plain", referenced_symbol = @s1.$root::@s2::@s17.Parent::@s19.plain, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Child>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 63 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s32.top::@s33.top::@s34.a, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s17.Parent>} {
                    }
                  }
                  obelisk.sv.expression.conversion attributes {folded_constant = "1'b0", is_implicit = true, is_signed = false, node_id = 64 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 65 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
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
}

