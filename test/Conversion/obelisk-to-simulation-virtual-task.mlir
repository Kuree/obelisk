// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// CHECK: #obelisk_sim.fragment<{{.*}}function = @unit_0
// CHECK: #obelisk_sim.fragment<{{.*}}function = @unit_1
// CHECK: obelisk_sim.func private @unit_0
// CHECK-SAME: !obelisk_sim.ref<i32>
// CHECK-SAME: !obelisk_sim.argument_ref<i32>
// CHECK: obelisk_sim.argument_ref.store
// CHECK: obelisk_sim.ref.store
// CHECK: obelisk_sim.func private @unit_1
// CHECK-SAME: !obelisk_sim.ref<i32>
// CHECK-SAME: !obelisk_sim.argument_ref<i32>
// CHECK: obelisk_sim.argument_ref.store
// CHECK: obelisk_sim.ref.store
// CHECK-NOT: obelisk_sim.class.is_instance
// CHECK-NOT: obelisk_sim.task.call
// CHECK: obelisk_sim.argument_ref.from_ref
// CHECK: obelisk_sim.class.virtual_task_call
// CHECK-SAME: slot 0 signature_id
// CHECK-SAME: arguments 4
// CHECK-SAME: !obelisk_sim.ref<i32>
// CHECK-SAME: !obelisk_sim.argument_ref<i32>
// CHECK-NOT: obelisk_sim.class.is_instance
// CHECK-NOT: obelisk_sim.task.call

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "Base", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "Base", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Base>, sym_name = "s3.Base", this_variable_path = "Base::this", this_variable_symbol = @s1.$root::@s2::@s3.Base::@s46.this} {
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "Base::run", is_declared_virtual, is_virtual, name = "run", node_id = 4 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>) -> (), true>, subroutine_kind = 1 : i32, sym_name = "s4.run", this_variable_path = "Base::run.this", this_variable_symbol = @s1.$root::@s2::@s3.Base::@s4.run::@s8.this, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 5 : i64} {
            obelisk.sv.statement.timed attributes {node_id = 6 : i64} {
              obelisk.sv.timing.delay attributes {node_id = 7 : i64} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 8 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
              obelisk.sv.statement.empty attributes {node_id = 9 : i64} {
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 10 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 11 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 12 : i64, referenced_path = "Base::run.result", referenced_symbol = @s1.$root::@s2::@s3.Base::@s4.run::@s6.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 13 : i64, referenced_path = "Base::run.value", referenced_symbol = @s1.$root::@s2::@s3.Base::@s4.run::@s5.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 14 : i64} {
              obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 15 : i64, operator_kind = 12 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 16 : i64, referenced_path = "Base::run.shared", referenced_symbol = @s1.$root::@s2::@s3.Base::@s4.run::@s7.shared, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "Base::run.value", name = "value", node_id = 17 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s5.value"} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 1 : i32, hierarchical_name = "Base::run.result", name = "result", node_id = 18 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s6.result"} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 3 : i32, hierarchical_name = "Base::run.shared", name = "shared", node_id = 19 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s7.shared"} {
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "Base::run.this", is_compiler_generated, is_const, name = "this", node_id = 20 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Base>, sym_name = "s8.this"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "Base::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 21 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s9.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 22 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "Base::pre_randomize", is_builtin, name = "pre_randomize", node_id = 23 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s10.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 24 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "Base::post_randomize", is_builtin, name = "post_randomize", node_id = 25 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s11.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 26 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "Base::get_randstate", is_builtin, name = "get_randstate", node_id = 27 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, sym_name = "s12.get_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 28 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "Base::set_randstate", is_builtin, name = "set_randstate", node_id = 29 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s13.set_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 30 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "Base::set_randstate.state", name = "state", node_id = 31 : i64, semantic_type = !obelisk.string, sym_name = "s14.state"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "Base::srandom", is_builtin, name = "srandom", node_id = 32 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s15.srandom", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 33 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "Base::srandom.seed", name = "seed", node_id = 34 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s16.seed"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "Base::rand_mode", is_builtin, name = "rand_mode", node_id = 35 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s17.rand_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 36 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "Base::rand_mode.on_ff", name = "on_ff", node_id = 37 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s18.on_ff"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "Base::constraint_mode", is_builtin, name = "constraint_mode", node_id = 38 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s19.constraint_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 39 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "Base::constraint_mode.on_ff", name = "on_ff", node_id = 40 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s20.on_ff"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "Base::this", is_compiler_generated, is_const, name = "this", node_id = 102 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Base>, sym_name = "s46.this"} {
        }
      }
      obelisk.sv.type.class_type attributes {base_class = !obelisk.class_handle<@s1.$root::@s2::@s3.Base>, bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "Derived", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "Derived", node_id = 41 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s21.Derived>, sym_name = "s21.Derived", this_variable_path = "Derived::this", this_variable_symbol = @s1.$root::@s2::@s21.Derived::@s45.this} {
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "Derived::run", is_declared_virtual, is_virtual, name = "run", node_id = 42 : i64, override_path = "Base::run", override_symbol = @s1.$root::@s2::@s3.Base::@s4.run, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>) -> (), true>, subroutine_kind = 1 : i32, sym_name = "s22.run", this_variable_path = "Derived::run.this", this_variable_symbol = @s1.$root::@s2::@s21.Derived::@s22.run::@s26.this, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 43 : i64} {
            obelisk.sv.statement.timed attributes {node_id = 44 : i64} {
              obelisk.sv.timing.delay attributes {node_id = 45 : i64} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "2", is_declared_unsized = true, is_signed = true, node_id = 46 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
              obelisk.sv.statement.empty attributes {node_id = 47 : i64} {
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 48 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 49 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 50 : i64, referenced_path = "Derived::run.result", referenced_symbol = @s1.$root::@s2::@s21.Derived::@s22.run::@s24.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.binary_op attributes {is_signed = true, node_id = 51 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 52 : i64, referenced_path = "Derived::run.value", referenced_symbol = @s1.$root::@s2::@s21.Derived::@s22.run::@s23.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 53 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 54 : i64} {
              obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 55 : i64, operator_kind = 12 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 56 : i64, referenced_path = "Derived::run.shared", referenced_symbol = @s1.$root::@s2::@s21.Derived::@s22.run::@s25.shared, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "Derived::run.value", name = "value", node_id = 57 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s23.value"} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 1 : i32, hierarchical_name = "Derived::run.result", name = "result", node_id = 58 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s24.result"} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 3 : i32, hierarchical_name = "Derived::run.shared", name = "shared", node_id = 59 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s25.shared"} {
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "Derived::run.this", is_compiler_generated, is_const, name = "this", node_id = 60 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s21.Derived>, sym_name = "s26.this"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "Derived::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 61 : i64, override_path = "Base::randomize", override_symbol = @s1.$root::@s2::@s3.Base::@s9.randomize, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s27.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 62 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "Derived::pre_randomize", is_builtin, name = "pre_randomize", node_id = 63 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s28.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 64 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "Derived::post_randomize", is_builtin, name = "post_randomize", node_id = 65 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s29.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 66 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "Derived::get_randstate", is_builtin, name = "get_randstate", node_id = 67 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, sym_name = "s30.get_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 68 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "Derived::set_randstate", is_builtin, name = "set_randstate", node_id = 69 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s31.set_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 70 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "Derived::set_randstate.state", name = "state", node_id = 71 : i64, semantic_type = !obelisk.string, sym_name = "s32.state"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "Derived::srandom", is_builtin, name = "srandom", node_id = 72 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s33.srandom", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 73 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "Derived::srandom.seed", name = "seed", node_id = 74 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s34.seed"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "Derived::rand_mode", is_builtin, name = "rand_mode", node_id = 75 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s35.rand_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 76 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "Derived::rand_mode.on_ff", name = "on_ff", node_id = 77 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s36.on_ff"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "Derived::constraint_mode", is_builtin, name = "constraint_mode", node_id = 78 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s37.constraint_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 79 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "Derived::constraint_mode.on_ff", name = "on_ff", node_id = 80 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s38.on_ff"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "Derived::this", is_compiler_generated, is_const, name = "this", node_id = 101 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s21.Derived>, sym_name = "s45.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 81 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s39.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 82 : i64, sym_name = "s40.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", node_id = 83 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Base>, sym_name = "s41.object"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.result", lifetime = 1 : i32, name = "result", node_id = 84 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s42.result"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.shared", lifetime = 1 : i32, name = "shared", node_id = 85 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s43.shared"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 86 : i64, procedure_kind = 0 : i32, sym_name = "s44", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 87 : i64} {
            obelisk.sv.statement.list attributes {node_id = 88 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 89 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 90 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Base>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 91 : i64, referenced_path = "top.object", referenced_symbol = @s1.$root::@s39.top::@s40.top::@s41.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Base>} {
                  }
                  obelisk.sv.expression.new_class attributes {is_signed = false, is_super_class = false, node_id = 92 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Base>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 93 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 3 : i64, callee_name = "run", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = true, is_signed = false, is_super_class = false, is_system_call = false, node_id = 94 : i64, referenced_path = "Base::run", referenced_symbol = @s1.$root::@s2::@s3.Base::@s4.run, semantic_type = !obelisk.void, subroutine_kind = 1 : i32} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 95 : i64, referenced_path = "top.object", referenced_symbol = @s1.$root::@s39.top::@s40.top::@s41.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Base>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "7", is_declared_unsized = true, is_signed = true, node_id = 96 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 97 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 98 : i64, referenced_path = "top.result", referenced_symbol = @s1.$root::@s39.top::@s40.top::@s42.result, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.empty_argument attributes {is_signed = true, node_id = 99 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 100 : i64, referenced_path = "top.shared", referenced_symbol = @s1.$root::@s39.top::@s40.top::@s43.shared, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
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
