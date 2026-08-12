// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null

// randomize(null) treats every random property as state regardless of the
// live static rand_mode word. It reads the current static value to check the
// constraint but does not inspect the shared mode, draw randomness, or commit
// any object, property, or hidden RNG state.
// CHECK-LABEL: obelisk_sim.func private @unit_1
// CHECK-SAME: %[[STATIC:arg[0-9]+]]: !obelisk_sim.ref<i32>
// CHECK: %[[OBJECT:.*]] = obelisk_sim.ref.load
// CHECK: obelisk_sim.class.is_instance %[[OBJECT]] is @__obelisk_class_s3_C
// CHECK: obelisk_sim.class.field_ref %[[OBJECT]][@__obelisk_class_s3_C_field_1]
// CHECK: %[[CURRENT_STATIC:.*]] = obelisk_sim.ref.load %[[STATIC]] : !obelisk_sim.ref<i32> -> i32
// CHECK: arith.cmpi sge
// CHECK-NOT: obelisk_sim.context.storage
// CHECK-NOT: obelisk_sim.random.next
// CHECK-NOT: obelisk_sim.random.solve
// CHECK-NOT: obelisk_sim.ref.store {{.*}} to %[[STATIC]]
// CHECK-NOT: obelisk_sim.managed.store
// CHECK-LABEL: obelisk_sim.func private @__obelisk_class_s3_C_implicit_new

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 64 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s3.C", this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s25.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::s", lifetime = 1 : i32, name = "s", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s4.s"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::x", name = "x", node_id = 5 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s5.x"} {
        }
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "C::valid", name = "valid", node_id = 6 : i64, sym_name = "s6.valid", this_variable_path = "C::valid.this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s6.valid::@s7.this} {
          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 7 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 8 : i64} {
              obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 9 : i64, operator_kind = 13 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 10 : i64, referenced_path = "C::s", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.s, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 11 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "C::valid.this", is_compiler_generated, is_const, name = "this", node_id = 12 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s7.this"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 13 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s8.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 14 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::pre_randomize", is_builtin, name = "pre_randomize", node_id = 15 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s9.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 16 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::post_randomize", is_builtin, name = "post_randomize", node_id = 17 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s10.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 18 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::get_randstate", is_builtin, name = "get_randstate", node_id = 19 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, sym_name = "s11.get_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 20 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::set_randstate", is_builtin, name = "set_randstate", node_id = 21 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s12.set_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 22 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::set_randstate.state", name = "state", node_id = 23 : i64, semantic_type = !obelisk.string, sym_name = "s13.state"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::srandom", is_builtin, name = "srandom", node_id = 24 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s14.srandom", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 25 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::srandom.seed", name = "seed", node_id = 26 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s15.seed"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::rand_mode", is_builtin, name = "rand_mode", node_id = 27 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s16.rand_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 28 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::rand_mode.on_ff", name = "on_ff", node_id = 29 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s17.on_ff"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::constraint_mode", is_builtin, name = "constraint_mode", node_id = 30 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s18.constraint_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 31 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::constraint_mode.on_ff", name = "on_ff", node_id = 32 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s19.on_ff"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 45 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s25.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 33 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s20.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 34 : i64, sym_name = "s21.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", node_id = 35 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s22.object"} {
          obelisk.sv.expression.new_class attributes {is_signed = false, is_super_class = false, node_id = 36 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.result", lifetime = 1 : i32, name = "result", node_id = 37 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s23.result"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 38 : i64, procedure_kind = 0 : i32, sym_name = "s24", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 39 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 40 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 41 : i64, referenced_path = "top.result", referenced_symbol = @s1.$root::@s20.top::@s21.top::@s23.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
              obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 42 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s20.top::@s21.top} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 43 : i64, referenced_path = "top.object", referenced_symbol = @s1.$root::@s20.top::@s21.top::@s22.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
                }
                obelisk.sv.expression.null_literal attributes {is_signed = false, node_id = 44 : i64, semantic_type = !obelisk.null} {
                }
              }
            }
          }
        }
      }
    }
  }
}
