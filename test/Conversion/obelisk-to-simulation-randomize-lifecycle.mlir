// RUN: %split-file %s %t
// RUN: obelisk-opt %t/lifecycle.mlir '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 18.6.2 and 18.11.1 require randomize(null) to invoke
// pre_randomize(), check the current values, and invoke post_randomize() only
// when that check succeeds. It remains a checker: no RNG or property stores.
// CHECK-DAG: obelisk_sim.class.field @__obelisk_class_s3_Base_field_0 {{.*}}obelisk_sim.random_mode_index = 0 : i64
// CHECK-DAG: obelisk_sim.class.field @__obelisk_class_s11_Derived_field_0 {{.*}}obelisk_sim.random_mode_index = 1 : i64
// CHECK-DAG: obelisk_sim.func private @[[BASE_PRE:unit_[0-9]+]]{{.*}}obelisk_sim.hierarchical_name = "Base::pre_randomize"
// CHECK-DAG: obelisk_sim.func private @[[BASE_POST:unit_[0-9]+]]{{.*}}obelisk_sim.hierarchical_name = "Base::post_randomize"
// CHECK-DAG: obelisk_sim.func private @[[DERIVED_PRE:unit_[0-9]+]]{{.*}}obelisk_sim.hierarchical_name = "Derived::pre_randomize"
// CHECK: %[[OBJECT:.*]] = obelisk_sim.ref.load {{.*}} : {{.*}} -> !obelisk_sim.class_handle<@__obelisk_class_s3_Base>
// CHECK: %[[IS_DERIVED:.*]] = obelisk_sim.class.is_instance %[[OBJECT]] is @__obelisk_class_s11_Derived
// CHECK: cf.cond_br %[[IS_DERIVED]], ^[[DERIVED_ENTRY:bb[0-9]+]], ^[[BASE_TEST:bb[0-9]+]]
// CHECK: ^[[DERIVED_ENTRY]]:
// CHECK: %[[DERIVED:.*]] = obelisk_sim.class.cast %[[OBJECT]]
// CHECK-NEXT: obelisk_sim.class.direct_call @[[DERIVED_PRE]] %[[DERIVED]]()
// CHECK: obelisk_sim.class.field_ref %[[DERIVED]][@__obelisk_class_s3_Base_field_0]
// CHECK: obelisk_sim.class.field_ref %[[DERIVED]][@__obelisk_class_s11_Derived_field_0]
// CHECK: ^[[BASE_TEST]]:
// CHECK: %[[IS_BASE:.*]] = obelisk_sim.class.is_instance %[[OBJECT]] is @__obelisk_class_s3_Base
// CHECK-NEXT: %[[NULL_RESULT:.*]] = arith.constant {{.*}}0 : i32
// CHECK-NEXT: cf.cond_br %[[IS_BASE]], ^[[BASE_ENTRY:bb[0-9]+]], {{.*}}(%[[NULL_RESULT]] : i32)
// CHECK: %[[DERIVED_AS_BASE:.*]] = obelisk_sim.class.cast %[[DERIVED]]
// CHECK-NEXT: obelisk_sim.class.direct_call @[[BASE_POST]] %[[DERIVED_AS_BASE]]()
// CHECK: ^[[BASE_ENTRY]]:
// CHECK-NEXT: obelisk_sim.class.direct_call @[[BASE_PRE]] %[[OBJECT]]({{.*}})
// CHECK: obelisk_sim.class.field_ref %[[OBJECT]][@__obelisk_class_s3_Base_field_0]
// CHECK: {{.*}}, %[[BASE_SOLVE_SUCCESS:.*]], {{.*}} = obelisk_sim.random.solve
// CHECK: %[[BASE_FAILURE:.*]] = arith.constant {{.*}}false
// CHECK-NEXT: cf.cond_br %[[BASE_SOLVE_SUCCESS]], {{.*}}, ^[[BASE_RESULT:bb[0-9]+]](%[[BASE_FAILURE]] : i1)
// CHECK: obelisk_sim.class.direct_call @[[BASE_POST]] %[[OBJECT]]()
// CHECK: ^[[BASE_RESULT]]({{.*}}):
// CHECK-NOT: obelisk_sim.class.direct_call
// CHECK: cf.br
// CHECK: ^[[CHECKER_DONE:bb[0-9]+]](%[[CHECKER_RESULT:.*]]: i32):
// CHECK-NEXT: obelisk_sim.ref.store %[[CHECKER_RESULT]]
// CHECK-NEXT: %[[INTERFACE_OBJECT:.*]] = obelisk_sim.ref.load {{.*}} -> !obelisk_sim.class_handle<@__obelisk_class_s30_I>
// CHECK-NEXT: %[[IS_INTERFACE_IMPL:.*]] = obelisk_sim.class.is_instance %[[INTERFACE_OBJECT]] is @__obelisk_class_s11_Derived
// CHECK: ^[[CHECKER_DERIVED:bb[0-9]+]]:
// CHECK-NEXT: %[[CHECKER_CAST:.*]] = obelisk_sim.class.cast
// CHECK-NEXT: obelisk_sim.class.direct_call @[[DERIVED_PRE]] %[[CHECKER_CAST]]()
// CHECK-NEXT: obelisk_sim.class.field_ref %[[CHECKER_CAST]][@__obelisk_class_s3_Base_field_0]
// CHECK-NEXT: obelisk_sim.class.field_ref %[[CHECKER_CAST]][@__obelisk_class_s11_Derived_field_0]
// CHECK-NEXT: obelisk_sim.class.field_ref %[[CHECKER_CAST]][@__obelisk_class_s3_Base_field___obelisk_constraint_mode]
// CHECK-NOT: obelisk_sim.random.solve
// CHECK-NOT: __obelisk_rng
// CHECK-NOT: __obelisk_rand_mode
// CHECK-NOT: __obelisk_randc
// CHECK-NOT: obelisk_sim.managed.store
// CHECK: arith.cmpi eq
// CHECK: arith.cmpi ult
// CHECK: arith.andi
// CHECK: cf.cond_br
// CHECK: ^[[CHECKER_BASE_TEST:bb[0-9]+]]:
// CHECK: obelisk_sim.class.is_instance
// CHECK: ^[[CHECKER_DERIVED_POST:bb[0-9]+]]:
// CHECK: %[[CHECKER_POST_CAST:.*]] = obelisk_sim.class.cast %[[CHECKER_CAST]]
// CHECK-NEXT: obelisk_sim.class.direct_call @[[BASE_POST]] %[[CHECKER_POST_CAST]]()
// CHECK: ^[[CHECKER_BASE:bb[0-9]+]]:
// CHECK-NEXT: obelisk_sim.class.direct_call @[[BASE_PRE]] %[[CHECKER_BASE_OBJECT:[^( ]+]]({{.*}})
// CHECK-NEXT: obelisk_sim.class.field_ref %[[CHECKER_BASE_OBJECT]][@__obelisk_class_s3_Base_field_0]
// CHECK-NEXT: obelisk_sim.class.field_ref %{{.*}}[@__obelisk_class_s3_Base_field___obelisk_constraint_mode]
// CHECK-NOT: obelisk_sim.random.solve
// CHECK-NOT: __obelisk_rng
// CHECK-NOT: __obelisk_rand_mode
// CHECK-NOT: __obelisk_randc
// CHECK-NOT: obelisk_sim.managed.store
// CHECK: cf.cond_br
// CHECK: obelisk_sim.class.direct_call @[[BASE_POST]] %[[CHECKER_BASE_OBJECT]]()
// CHECK: cf.br ^[[CHECKER_DONE]]
// CHECK: ^[[INTERFACE_DONE:bb[0-9]+]]({{.*}}: i32):
// CHECK: obelisk_sim.return
// CHECK: ^[[INTERFACE_ENTRY:bb[0-9]+]]:
// CHECK-NEXT: %[[INTERFACE_CAST:.*]] = obelisk_sim.class.cast %[[INTERFACE_OBJECT]] : !obelisk_sim.class_handle<@__obelisk_class_s30_I> to !obelisk_sim.class_handle<@__obelisk_class_s11_Derived>
// CHECK-NEXT: obelisk_sim.class.direct_call @[[DERIVED_PRE]] %[[INTERFACE_CAST]]()

//--- lifecycle.mlir

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "I", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = true, is_uninstantiated = false, name = "I", node_id = 100 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s30.I>, sym_name = "s30.I", this_variable_path = "I::this", this_variable_symbol = @s1.$root::@s2::@s30.I::@s31.this} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "I::this", is_compiler_generated, is_const, name = "this", node_id = 101 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s30.I>, sym_name = "s31.this"} {
        }
      }
      obelisk.sv.type.class_type attributes {bitstream_width = 1 : i64, declared_interfaces = [!obelisk.class_handle<@s1.$root::@s2::@s30.I>], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "Base", implemented_interfaces = [!obelisk.class_handle<@s1.$root::@s2::@s30.I>], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "Base", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Base>, sym_name = "s3.Base", this_variable_path = "Base::this", this_variable_symbol = @s1.$root::@s2::@s3.Base::@s10.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "Base::x", name = "x", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s4.x"} {
        }
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "Base::bounded", name = "bounded", node_id = 34 : i64, sym_name = "s25.bounded", this_variable_path = "Base::bounded.this", this_variable_symbol = @s1.$root::@s2::@s3.Base::@s25.bounded::@s26.this} {
          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 35 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 36 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 37 : i64, operator_kind = 16 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 38 : i64, referenced_path = "Base::x", referenced_symbol = @s1.$root::@s2::@s3.Base::@s4.x, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                }
                obelisk.sv.expression.named_value attributes {node_id = 39 : i64, referenced_path = "top.limit", referenced_symbol = @s1.$root::@s19.top::@s20.top::@s23.limit, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                }
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "Base::bounded.this", is_compiler_generated, is_const, name = "this", node_id = 40 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Base>, sym_name = "s26.this"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "Base::pre_randomize", is_pre_post_randomize, name = "pre_randomize", node_id = 5 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s5.pre_randomize", this_variable_path = "Base::pre_randomize.this", this_variable_symbol = @s1.$root::@s2::@s3.Base::@s5.pre_randomize::@s6.this, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 6 : i64} {
            obelisk.sv.statement.expression_statement attributes {node_id = 28 : i64} {
              obelisk.sv.expression.named_value attributes {node_id = 29 : i64, referenced_path = "top.limit", referenced_symbol = @s1.$root::@s19.top::@s20.top::@s23.limit, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "Base::pre_randomize.this", is_compiler_generated, is_const, name = "this", node_id = 7 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Base>, sym_name = "s6.this"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "Base::post_randomize", is_pre_post_randomize, name = "post_randomize", node_id = 8 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s7.post_randomize", this_variable_path = "Base::post_randomize.this", this_variable_symbol = @s1.$root::@s2::@s3.Base::@s7.post_randomize::@s8.this, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 9 : i64} {
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "Base::post_randomize.this", is_compiler_generated, is_const, name = "this", node_id = 10 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Base>, sym_name = "s8.this"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "Base::this", is_compiler_generated, is_const, name = "this", node_id = 11 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Base>, sym_name = "s10.this"} {
        }
      }
      obelisk.sv.type.class_type attributes {base_class = !obelisk.class_handle<@s1.$root::@s2::@s3.Base>, bitstream_width = 2 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "Derived", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "Derived", node_id = 12 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s11.Derived>, sym_name = "s11.Derived", this_variable_path = "Derived::this", this_variable_symbol = @s1.$root::@s2::@s11.Derived::@s18.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "Derived::y", name = "y", node_id = 13 : i64, rand_mode = 2 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s12.y"} {
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "Derived::pre_randomize", is_pre_post_randomize, name = "pre_randomize", node_id = 14 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s13.pre_randomize", this_variable_path = "Derived::pre_randomize.this", this_variable_symbol = @s1.$root::@s2::@s11.Derived::@s13.pre_randomize::@s14.this, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 15 : i64} {
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "Derived::pre_randomize.this", is_compiler_generated, is_const, name = "this", node_id = 16 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s11.Derived>, sym_name = "s14.this"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "Derived::this", is_compiler_generated, is_const, name = "this", node_id = 20 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s11.Derived>, sym_name = "s18.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 21 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s19.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 22 : i64, sym_name = "s20.top"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.limit", lifetime = 1 : i32, name = "limit", node_id = 30 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s23.limit"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", node_id = 23 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Base>, sym_name = "s21.object"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.interface_object", lifetime = 1 : i32, name = "interface_object", node_id = 102 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s30.I>, sym_name = "s32.interface_object"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.result", lifetime = 1 : i32, name = "result", node_id = 31 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s24.result"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.checker_result", lifetime = 1 : i32, name = "checker_result", node_id = 103 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s33.checker_result"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.interface_result", lifetime = 1 : i32, name = "interface_result", node_id = 104 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s34.interface_result"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 24 : i64, procedure_kind = 0 : i32, sym_name = "s22", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 25 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 32 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              obelisk.sv.expression.named_value attributes {node_id = 33 : i64, referenced_path = "top.result", referenced_symbol = @s1.$root::@s19.top::@s20.top::@s24.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 26 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s19.top::@s20.top} {
                obelisk.sv.expression.named_value attributes {node_id = 27 : i64, referenced_path = "top.object", referenced_symbol = @s1.$root::@s19.top::@s20.top::@s21.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Base>} {
                }
              }
            }
          }
          obelisk.sv.statement.expression_statement attributes {node_id = 105 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 106 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              obelisk.sv.expression.named_value attributes {node_id = 107 : i64, referenced_path = "top.checker_result", referenced_symbol = @s1.$root::@s19.top::@s20.top::@s33.checker_result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
              obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = true, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 108 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s19.top::@s20.top} {
                obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 120 : i64} {
                  obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 121 : i64} {
                    obelisk.sv.expression.binary_op attributes {node_id = 122 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                      obelisk.sv.expression.named_value attributes {node_id = 123 : i64, referenced_path = "Base::x", referenced_symbol = @s1.$root::@s2::@s3.Base::@s4.x, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                      }
                      obelisk.sv.expression.named_value attributes {node_id = 124 : i64, referenced_path = "top.limit", referenced_symbol = @s1.$root::@s19.top::@s20.top::@s23.limit, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                      }
                    }
                  }
                }
                obelisk.sv.expression.named_value attributes {node_id = 109 : i64, referenced_path = "top.object", referenced_symbol = @s1.$root::@s19.top::@s20.top::@s21.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Base>} {
                }
                obelisk.sv.expression.null_literal attributes {node_id = 110 : i64, semantic_type = !obelisk.null} {
                }
              }
            }
          }
          obelisk.sv.statement.expression_statement attributes {node_id = 111 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 112 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              obelisk.sv.expression.named_value attributes {node_id = 113 : i64, referenced_path = "top.interface_result", referenced_symbol = @s1.$root::@s19.top::@s20.top::@s34.interface_result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 114 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s19.top::@s20.top} {
                obelisk.sv.expression.named_value attributes {node_id = 115 : i64, referenced_path = "top.interface_object", referenced_symbol = @s1.$root::@s19.top::@s20.top::@s32.interface_object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s30.I>} {
                }
              }
            }
          }
        }
        // Keep a second randomize-containing unit in this MLIR test. Unit
        // lowering is parallel by default, which exercises serialization of
        // the intentionally single-threaded compiler-side Z3 build.
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 116 : i64, procedure_kind = 0 : i32, sym_name = "s35", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 117 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 118 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s19.top::@s20.top} {
              obelisk.sv.expression.named_value attributes {node_id = 119 : i64, referenced_path = "top.object", referenced_symbol = @s1.$root::@s19.top::@s20.top::@s21.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Base>} {
              }
            }
          }
        }
      }
    }
  }
}
