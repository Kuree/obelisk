// RUN: %split-file %s %t
// RUN: obelisk-opt %t/wide-unconstrained.mlir '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=WIDE
// RUN: %if z3 %{ \
// RUN:   obelisk-opt %t/wide-equality.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:     | FileCheck %s --check-prefix=EQUALITY \
// RUN: %}
// RUN: %if z3 %{ \
// RUN:   obelisk-opt %t/wide-table.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:     | FileCheck %s --check-prefix=WIDE-TABLE \
// RUN: %}
// RUN: %if z3 %{ \
// RUN:   obelisk-opt %t/wide-residual.mlir \
// RUN:     '--lower-obelisk-to-sim=opt-level=0' \
// RUN:     | FileCheck %s --check-prefix=RESIDUAL \
// RUN: %}
// RUN: %if z3 %{ \
// RUN:   obelisk-opt %t/wide-residual.mlir \
// RUN:     '--lower-obelisk-to-sim=opt-level=0' \
// RUN:     --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:     | FileCheck %s --check-prefix=RESIDUAL-NATIVE \
// RUN: %}
// RUN: %if z3 %{ \
// RUN:   obelisk-opt %t/wide-residual.mlir \
// RUN:     '--lower-obelisk-to-sim=opt-level=0' \
// RUN:     '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null \
// RUN: %}

// WIDE-LABEL: obelisk_sim.func private @unit_1
// WIDE-COUNT-4: arith.muli
// WIDE: %[[TOP:.*]] = arith.andi {{.*}}, {{.*}} : i64
// WIDE-NEXT: arith.extui %[[TOP]] : i64 to i65
// WIDE: arith.shli {{.*}} : i65
// WIDE: obelisk_sim.managed.store
// WIDE-NOT: obelisk_sim.random.solve

// EQUALITY-LABEL: obelisk_sim.func private @unit_1
// EQUALITY: arith.constant 18446744073709551620 : i128
// EQUALITY: arith.cmpi eq, {{.*}} : i128
// EQUALITY-NOT: obelisk_sim.random.solve %

// WIDE-TABLE-LABEL: obelisk_sim.func private @unit_1
// WIDE-TABLE: %[[ALL_RAND:.*]] = arith.cmpi eq, {{.*}} : i64
// WIDE-TABLE: arith.cmpi eq, {{.*}} : i64
// WIDE-TABLE: %[[ALL_CONSTRAINTS:.*]] = arith.cmpi eq, {{.*}} : i64
// WIDE-TABLE: %[[USE_PLAN:.*]] = arith.andi %[[ALL_CONSTRAINTS]], %[[ALL_RAND]] : i1
// WIDE-TABLE: cf.cond_br %[[USE_PLAN]],
// WIDE-TABLE: arith.trunci {{.*}} : i128 to i64
// WIDE-TABLE: arith.remui {{.*}}, {{.*}} : i64
// WIDE-TABLE: arith.cmpi ult, {{.*}} : i64
// WIDE-TABLE: arith.extui {{.*}} : i64 to i128
// WIDE-TABLE: %[[ROW:.*]] = arith.remui {{.*}}, {{.*}} : i128
// WIDE-TABLE: arith.cmpi eq, %[[ROW]], {{.*}} : i128
// WIDE-TABLE: arith.select {{.*}}, {{.*}}, {{.*}} : i128

// RESIDUAL-LABEL: obelisk_sim.func private @unit_1
// RESIDUAL: obelisk_sim.random.solve_wide
// RESIDUAL-SAME: : (!obelisk_sim.context, i128, i128, i64, i64, i64, i64, i128) -> (i128, i1, i64)

// RESIDUAL-NATIVE: llvm.call @obelisk_rt_v1_random_solve_wide_modes_state

//--- wide-residual.mlir

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 128 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s3.C", this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s24.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::value", name = "value", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<127 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s4.value"} {
        }
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "C::impossible", name = "impossible", node_id = 5 : i64, sym_name = "s5.impossible", this_variable_path = "C::impossible.this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s5.impossible::@s6.this} {
          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 6 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 7 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 8 : i64, operator_kind = 10 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 9 : i64, referenced_path = "C::value", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.value, semantic_type = !obelisk.ranged_packed_array<127 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.binary_op attributes {node_id = 10 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<127 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "18446744073709551617", node_id = 44 : i64, semantic_type = !obelisk.ranged_packed_array<127 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "3", node_id = 45 : i64, semantic_type = !obelisk.ranged_packed_array<127 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                }
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "C::impossible.this", is_compiler_generated, is_const, name = "this", node_id = 11 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s6.this"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 12 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s7.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 13 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::pre_randomize", is_builtin, name = "pre_randomize", node_id = 14 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s8.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 15 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::post_randomize", is_builtin, name = "post_randomize", node_id = 16 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s9.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 17 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::get_randstate", is_builtin, name = "get_randstate", node_id = 18 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, sym_name = "s10.get_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 19 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::set_randstate", is_builtin, name = "set_randstate", node_id = 20 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s11.set_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 21 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::set_randstate.state", name = "state", node_id = 22 : i64, semantic_type = !obelisk.string, sym_name = "s12.state"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::srandom", is_builtin, name = "srandom", node_id = 23 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s13.srandom", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 24 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::srandom.seed", name = "seed", node_id = 25 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s14.seed"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::rand_mode", is_builtin, name = "rand_mode", node_id = 26 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s15.rand_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 27 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::rand_mode.on_ff", name = "on_ff", node_id = 28 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s16.on_ff"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::constraint_mode", is_builtin, name = "constraint_mode", node_id = 29 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s17.constraint_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 30 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::constraint_mode.on_ff", name = "on_ff", node_id = 31 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s18.on_ff"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 43 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s24.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 32 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s19.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 33 : i64, sym_name = "s20.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", node_id = 34 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s21.object"} {
          obelisk.sv.expression.new_class attributes {is_super_class = false, node_id = 35 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.result", lifetime = 1 : i32, name = "result", node_id = 36 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s22.result"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 37 : i64, procedure_kind = 0 : i32, sym_name = "s23", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 38 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 39 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              obelisk.sv.expression.named_value attributes {node_id = 40 : i64, referenced_path = "top.result", referenced_symbol = @s1.$root::@s19.top::@s20.top::@s22.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 41 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s19.top::@s20.top} {
                obelisk.sv.expression.named_value attributes {node_id = 42 : i64, referenced_path = "top.object", referenced_symbol = @s1.$root::@s19.top::@s20.top::@s21.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
                }
              }
            }
          }
        }
      }
    }
  }
}

//--- wide-unconstrained.mlir
module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 65 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s3.C", this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s24.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::value", name = "value", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<64 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s4.value"} {
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 12 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s7.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 13 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::pre_randomize", is_builtin, name = "pre_randomize", node_id = 14 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s8.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 15 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::post_randomize", is_builtin, name = "post_randomize", node_id = 16 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s9.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 17 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::get_randstate", is_builtin, name = "get_randstate", node_id = 18 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, sym_name = "s10.get_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 19 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::set_randstate", is_builtin, name = "set_randstate", node_id = 20 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s11.set_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 21 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::set_randstate.state", name = "state", node_id = 22 : i64, semantic_type = !obelisk.string, sym_name = "s12.state"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::srandom", is_builtin, name = "srandom", node_id = 23 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s13.srandom", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 24 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::srandom.seed", name = "seed", node_id = 25 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s14.seed"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::rand_mode", is_builtin, name = "rand_mode", node_id = 26 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s15.rand_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 27 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::rand_mode.on_ff", name = "on_ff", node_id = 28 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s16.on_ff"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::constraint_mode", is_builtin, name = "constraint_mode", node_id = 29 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s17.constraint_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 30 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::constraint_mode.on_ff", name = "on_ff", node_id = 31 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s18.on_ff"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 43 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s24.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 32 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s19.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 33 : i64, sym_name = "s20.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", node_id = 34 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s21.object"} {
          obelisk.sv.expression.new_class attributes {is_super_class = false, node_id = 35 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.result", lifetime = 1 : i32, name = "result", node_id = 36 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s22.result"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 37 : i64, procedure_kind = 0 : i32, sym_name = "s23", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 38 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 39 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              obelisk.sv.expression.named_value attributes {node_id = 40 : i64, referenced_path = "top.result", referenced_symbol = @s1.$root::@s19.top::@s20.top::@s22.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 41 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s19.top::@s20.top} {
                obelisk.sv.expression.named_value attributes {node_id = 42 : i64, referenced_path = "top.object", referenced_symbol = @s1.$root::@s19.top::@s20.top::@s21.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
                }
              }
            }
          }
        }
      }
    }
  }
}
//--- wide-equality.mlir

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 128 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s3.C", this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s24.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::value", name = "value", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<127 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s4.value"} {
        }
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "C::impossible", name = "impossible", node_id = 5 : i64, sym_name = "s5.impossible", this_variable_path = "C::impossible.this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s5.impossible::@s6.this} {
          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 6 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 7 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 8 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 9 : i64, referenced_path = "C::value", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.value, semantic_type = !obelisk.ranged_packed_array<127 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.binary_op attributes {node_id = 10 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<127 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "18446744073709551617", node_id = 44 : i64, semantic_type = !obelisk.ranged_packed_array<127 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "3", node_id = 45 : i64, semantic_type = !obelisk.ranged_packed_array<127 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                }
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "C::impossible.this", is_compiler_generated, is_const, name = "this", node_id = 11 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s6.this"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 12 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s7.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 13 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::pre_randomize", is_builtin, name = "pre_randomize", node_id = 14 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s8.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 15 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::post_randomize", is_builtin, name = "post_randomize", node_id = 16 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s9.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 17 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::get_randstate", is_builtin, name = "get_randstate", node_id = 18 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, sym_name = "s10.get_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 19 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::set_randstate", is_builtin, name = "set_randstate", node_id = 20 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s11.set_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 21 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::set_randstate.state", name = "state", node_id = 22 : i64, semantic_type = !obelisk.string, sym_name = "s12.state"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::srandom", is_builtin, name = "srandom", node_id = 23 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s13.srandom", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 24 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::srandom.seed", name = "seed", node_id = 25 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s14.seed"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::rand_mode", is_builtin, name = "rand_mode", node_id = 26 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s15.rand_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 27 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::rand_mode.on_ff", name = "on_ff", node_id = 28 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s16.on_ff"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::constraint_mode", is_builtin, name = "constraint_mode", node_id = 29 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s17.constraint_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 30 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::constraint_mode.on_ff", name = "on_ff", node_id = 31 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s18.on_ff"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 43 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s24.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 32 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s19.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 33 : i64, sym_name = "s20.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", node_id = 34 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s21.object"} {
          obelisk.sv.expression.new_class attributes {is_super_class = false, node_id = 35 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.result", lifetime = 1 : i32, name = "result", node_id = 36 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s22.result"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 37 : i64, procedure_kind = 0 : i32, sym_name = "s23", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 38 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 39 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              obelisk.sv.expression.named_value attributes {node_id = 40 : i64, referenced_path = "top.result", referenced_symbol = @s1.$root::@s19.top::@s20.top::@s22.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 41 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s19.top::@s20.top} {
                obelisk.sv.expression.named_value attributes {node_id = 42 : i64, referenced_path = "top.object", referenced_symbol = @s1.$root::@s19.top::@s20.top::@s21.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
                }
              }
            }
          }
        }
      }
    }
  }
}
//--- wide-table.mlir

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 128 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s3.C", this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s24.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::value", name = "value", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<127 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s4.value"} {
        }
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "C::small", name = "small", node_id = 5 : i64, sym_name = "s5.small", this_variable_path = "C::small.this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s5.small::@s6.this} {
          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 6 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 7 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 8 : i64, operator_kind = 16 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 9 : i64, referenced_path = "C::value", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.value, semantic_type = !obelisk.ranged_packed_array<127 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "3", node_id = 10 : i64, semantic_type = !obelisk.ranged_packed_array<127 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "C::small.this", is_compiler_generated, is_const, name = "this", node_id = 11 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s6.this"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 12 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s7.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 13 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::pre_randomize", is_builtin, name = "pre_randomize", node_id = 14 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s8.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 15 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::post_randomize", is_builtin, name = "post_randomize", node_id = 16 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s9.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 17 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::get_randstate", is_builtin, name = "get_randstate", node_id = 18 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, sym_name = "s10.get_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 19 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::set_randstate", is_builtin, name = "set_randstate", node_id = 20 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s11.set_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 21 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::set_randstate.state", name = "state", node_id = 22 : i64, semantic_type = !obelisk.string, sym_name = "s12.state"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::srandom", is_builtin, name = "srandom", node_id = 23 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s13.srandom", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 24 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::srandom.seed", name = "seed", node_id = 25 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s14.seed"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::rand_mode", is_builtin, name = "rand_mode", node_id = 26 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s15.rand_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 27 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::rand_mode.on_ff", name = "on_ff", node_id = 28 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s16.on_ff"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::constraint_mode", is_builtin, name = "constraint_mode", node_id = 29 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s17.constraint_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 30 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::constraint_mode.on_ff", name = "on_ff", node_id = 31 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s18.on_ff"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 43 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s24.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 32 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s19.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 33 : i64, sym_name = "s20.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", node_id = 34 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s21.object"} {
          obelisk.sv.expression.new_class attributes {is_super_class = false, node_id = 35 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.result", lifetime = 1 : i32, name = "result", node_id = 36 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s22.result"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 37 : i64, procedure_kind = 0 : i32, sym_name = "s23", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 38 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 39 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              obelisk.sv.expression.named_value attributes {node_id = 40 : i64, referenced_path = "top.result", referenced_symbol = @s1.$root::@s19.top::@s20.top::@s22.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 41 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s19.top::@s20.top} {
                obelisk.sv.expression.named_value attributes {node_id = 42 : i64, referenced_path = "top.object", referenced_symbol = @s1.$root::@s19.top::@s20.top::@s21.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
                }
              }
            }
          }
        }
      }
    }
  }
}
