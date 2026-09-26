// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' '--encode-obelisk-sim-to-bytecode=vpi=off' --convert-obelisk-sim-processes-to-llvm-coroutines | mlir-translate --mlir-to-llvmir | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --seed=1 | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode --seed=1 | FileCheck %s

// IEEE 1800-2017 18.5.2 makes differently named derived constraints
// additional to inherited constraints. Section 18.6.3 permits randomize() to
// fail only when the active constraints are infeasible. Fresh domain samples
// must therefore find x > 0 && x < y instead of keeping x and y correlated.
// CHECK: inheritance 1 1

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 32 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "B", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "B", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.B>, sym_name = "s3.B", this_variable_path = "B::this", this_variable_symbol = @s1.$root::@s2::@s3.B::@s62.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "B::x", name = "x", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s4.x"} {
        }
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "B::positive", name = "positive", node_id = 5 : i64, sym_name = "s5.positive", this_variable_path = "B::positive.this", this_variable_symbol = @s1.$root::@s2::@s3.B::@s5.positive::@s6.this} {
          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 6 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 7 : i64} {
              obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 8 : i64, operator_kind = 14 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 9 : i64, referenced_path = "B::x", referenced_symbol = @s1.$root::@s2::@s3.B::@s4.x, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 10 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "B::positive.this", is_compiler_generated, is_const, name = "this", node_id = 11 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.B>, sym_name = "s6.this"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "B::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 12 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s7.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 13 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "B::pre_randomize", is_builtin, name = "pre_randomize", node_id = 14 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s8.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 15 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "B::post_randomize", is_builtin, name = "post_randomize", node_id = 16 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s9.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 17 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "B::get_randstate", is_builtin, name = "get_randstate", node_id = 18 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, sym_name = "s10.get_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 19 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "B::set_randstate", is_builtin, name = "set_randstate", node_id = 20 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s11.set_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 21 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "B::set_randstate.state", name = "state", node_id = 22 : i64, semantic_type = !obelisk.string, sym_name = "s12.state"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "B::srandom", is_builtin, name = "srandom", node_id = 23 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s13.srandom", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 24 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "B::srandom.seed", name = "seed", node_id = 25 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s14.seed"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "B::rand_mode", is_builtin, name = "rand_mode", node_id = 26 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s15.rand_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 27 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "B::rand_mode.on_ff", name = "on_ff", node_id = 28 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s16.on_ff"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "B::constraint_mode", is_builtin, name = "constraint_mode", node_id = 29 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s17.constraint_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 30 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "B::constraint_mode.on_ff", name = "on_ff", node_id = 31 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s18.on_ff"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "B::this", is_compiler_generated, is_const, name = "this", node_id = 138 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.B>, sym_name = "s62.this"} {
        }
      }
      obelisk.sv.type.class_type attributes {base_class = !obelisk.class_handle<@s1.$root::@s2::@s3.B>, bitstream_width = 64 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 32 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s19.C>, sym_name = "s19.C", this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s19.C::@s61.this} {
        obelisk.sv.symbol.transparent_member attributes {hierarchical_name = "C::positive", name = "positive", node_id = 33 : i64, sym_name = "s20.positive"} {
        }
        obelisk.sv.symbol.transparent_member attributes {hierarchical_name = "C::x", name = "x", node_id = 34 : i64, sym_name = "s21.x"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::y", name = "y", node_id = 35 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s22.y"} {
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 36 : i64, override_path = "B::randomize", override_symbol = @s1.$root::@s2::@s3.B::@s7.randomize, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s23.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 37 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::pre_randomize", is_builtin, name = "pre_randomize", node_id = 38 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s24.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 39 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::post_randomize", is_builtin, name = "post_randomize", node_id = 40 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s25.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 41 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::get_randstate", is_builtin, name = "get_randstate", node_id = 42 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, sym_name = "s26.get_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 43 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::set_randstate", is_builtin, name = "set_randstate", node_id = 44 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s27.set_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 45 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::set_randstate.state", name = "state", node_id = 46 : i64, semantic_type = !obelisk.string, sym_name = "s28.state"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::srandom", is_builtin, name = "srandom", node_id = 47 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s29.srandom", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 48 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::srandom.seed", name = "seed", node_id = 49 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s30.seed"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::rand_mode", is_builtin, name = "rand_mode", node_id = 50 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s31.rand_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 51 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::rand_mode.on_ff", name = "on_ff", node_id = 52 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s32.on_ff"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::constraint_mode", is_builtin, name = "constraint_mode", node_id = 53 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s33.constraint_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 54 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::constraint_mode.on_ff", name = "on_ff", node_id = 55 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s34.on_ff"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 137 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s19.C>, sym_name = "s61.this"} {
        }
      }
      obelisk.sv.type.class_type attributes {base_class = !obelisk.class_handle<@s1.$root::@s2::@s19.C>, bitstream_width = 64 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "D", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "D", node_id = 56 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s35.D>, sym_name = "s35.D", this_variable_path = "D::this", this_variable_symbol = @s1.$root::@s2::@s35.D::@s60.this} {
        obelisk.sv.symbol.transparent_member attributes {hierarchical_name = "D::y", name = "y", node_id = 57 : i64, sym_name = "s36.y"} {
        }
        obelisk.sv.symbol.transparent_member attributes {hierarchical_name = "D::x", name = "x", node_id = 58 : i64, sym_name = "s37.x"} {
        }
        obelisk.sv.symbol.transparent_member attributes {hierarchical_name = "D::positive", name = "positive", node_id = 59 : i64, sym_name = "s38.positive"} {
        }
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "D::ordered", name = "ordered", node_id = 60 : i64, sym_name = "s39.ordered", this_variable_path = "D::ordered.this", this_variable_symbol = @s1.$root::@s2::@s35.D::@s39.ordered::@s40.this} {
          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 61 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 62 : i64} {
              obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 63 : i64, operator_kind = 16 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 64 : i64, referenced_path = "B::x", referenced_symbol = @s1.$root::@s2::@s3.B::@s4.x, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 65 : i64, referenced_path = "C::y", referenced_symbol = @s1.$root::@s2::@s19.C::@s22.y, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "D::ordered.this", is_compiler_generated, is_const, name = "this", node_id = 66 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s35.D>, sym_name = "s40.this"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "D::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 67 : i64, override_path = "C::randomize", override_symbol = @s1.$root::@s2::@s19.C::@s23.randomize, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s41.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 68 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "D::pre_randomize", is_builtin, name = "pre_randomize", node_id = 69 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s42.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 70 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "D::post_randomize", is_builtin, name = "post_randomize", node_id = 71 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s43.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 72 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "D::get_randstate", is_builtin, name = "get_randstate", node_id = 73 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, sym_name = "s44.get_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 74 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "D::set_randstate", is_builtin, name = "set_randstate", node_id = 75 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s45.set_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 76 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "D::set_randstate.state", name = "state", node_id = 77 : i64, semantic_type = !obelisk.string, sym_name = "s46.state"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "D::srandom", is_builtin, name = "srandom", node_id = 78 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s47.srandom", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 79 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "D::srandom.seed", name = "seed", node_id = 80 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s48.seed"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "D::rand_mode", is_builtin, name = "rand_mode", node_id = 81 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s49.rand_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 82 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "D::rand_mode.on_ff", name = "on_ff", node_id = 83 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s50.on_ff"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "D::constraint_mode", is_builtin, name = "constraint_mode", node_id = 84 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s51.constraint_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 85 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "D::constraint_mode.on_ff", name = "on_ff", node_id = 86 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s52.on_ff"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "D::this", is_compiler_generated, is_const, name = "this", node_id = 136 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s35.D>, sym_name = "s60.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 87 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s53.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 88 : i64, sym_name = "s54.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "top", node_id = 89 : i64, sym_name = "s55"} {
          obelisk.sv.symbol.variable attributes {hierarchical_name = "top.d", name = "d", node_id = 90 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s35.D>, sym_name = "s56.d"} {
            obelisk.sv.expression.new_class attributes {is_signed = false, is_super_class = false, node_id = 91 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s35.D>} {
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "top.ok", name = "ok", node_id = 92 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s57.ok"} {
            obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 93 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "top.valid", name = "valid", node_id = 94 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s58.valid"} {
            obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 95 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 96 : i64, procedure_kind = 0 : i32, sym_name = "s59", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 97 : i64} {
            obelisk.sv.statement.list attributes {node_id = 98 : i64} {
              obelisk.sv.statement.variable_declaration attributes {node_id = 99 : i64, referenced_path = "top.d", referenced_symbol = @s1.$root::@s53.top::@s54.top::@s55::@s56.d} {
              }
              obelisk.sv.statement.variable_declaration attributes {node_id = 100 : i64, referenced_path = "top.ok", referenced_symbol = @s1.$root::@s53.top::@s54.top::@s55::@s57.ok} {
              }
              obelisk.sv.statement.variable_declaration attributes {node_id = 101 : i64, referenced_path = "top.valid", referenced_symbol = @s1.$root::@s53.top::@s54.top::@s55::@s58.valid} {
              }
              obelisk.sv.statement.repeat_loop attributes {node_id = 102 : i64} {
                obelisk.sv.expression.integer_literal attributes {constant_value = "128", is_declared_unsized = true, is_signed = true, node_id = 103 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.statement.block attributes {node_id = 104 : i64} {
                  obelisk.sv.statement.list attributes {node_id = 105 : i64} {
                    obelisk.sv.statement.expression_statement attributes {node_id = 106 : i64} {
                      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 107 : i64, operator_kind = 5 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                        obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 108 : i64, referenced_path = "top.ok", referenced_symbol = @s1.$root::@s53.top::@s54.top::@s55::@s57.ok, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                        }
                        obelisk.sv.expression.binary_op attributes {is_signed = true, node_id = 109 : i64, operator_kind = 5 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                          obelisk.sv.expression.l_value_reference attributes {is_signed = true, node_id = 110 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                          }
                          obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 111 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s53.top::@s54.top::@s55} {
                            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 112 : i64, referenced_path = "top.d", referenced_symbol = @s1.$root::@s53.top::@s54.top::@s55::@s56.d, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s35.D>} {
                            }
                          }
                        }
                      }
                    }
                    obelisk.sv.statement.expression_statement attributes {node_id = 113 : i64} {
                      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 114 : i64, operator_kind = 5 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                        obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 115 : i64, referenced_path = "top.valid", referenced_symbol = @s1.$root::@s53.top::@s54.top::@s55::@s58.valid, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                        }
                        obelisk.sv.expression.conversion attributes {is_signed = true, node_id = 116 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                          obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 117 : i64, operator_kind = 5 : i32, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                            obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 118 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                              obelisk.sv.expression.l_value_reference attributes {is_signed = true, node_id = 119 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                              }
                            }
                            obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 120 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                              obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 121 : i64, operator_kind = 19 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                                obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 122 : i64, operator_kind = 14 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                                  obelisk.sv.expression.member_access attributes {field_ordinal = 72 : i64, is_signed = true, node_id = 123 : i64, packed_offset = 4294967296 : i64, referenced_path = "B::x", referenced_symbol = @s1.$root::@s2::@s3.B::@s4.x, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 124 : i64, referenced_path = "top.d", referenced_symbol = @s1.$root::@s53.top::@s54.top::@s55::@s56.d, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s35.D>} {
                                    }
                                  }
                                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 125 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                                  }
                                }
                                obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 126 : i64, operator_kind = 16 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                                  obelisk.sv.expression.member_access attributes {field_ordinal = 72 : i64, is_signed = true, node_id = 127 : i64, packed_offset = 4294967296 : i64, referenced_path = "B::x", referenced_symbol = @s1.$root::@s2::@s3.B::@s4.x, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 128 : i64, referenced_path = "top.d", referenced_symbol = @s1.$root::@s53.top::@s54.top::@s55::@s56.d, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s35.D>} {
                                    }
                                  }
                                  obelisk.sv.expression.member_access attributes {field_ordinal = 64 : i64, is_signed = true, node_id = 129 : i64, packed_offset = 4294967296 : i64, referenced_path = "C::y", referenced_symbol = @s1.$root::@s2::@s19.C::@s22.y, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 130 : i64, referenced_path = "top.d", referenced_symbol = @s1.$root::@s53.top::@s54.top::@s55::@s56.d, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s35.D>} {
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
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 131 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 3 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 132 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s53.top::@s54.top::@s55} {
                  obelisk.sv.expression.string_literal attributes {constant_value = "inheritance %0d %0d", is_signed = false, node_id = 133 : i64, semantic_type = !obelisk.ranged_packed_array<151 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 134 : i64, referenced_path = "top.ok", referenced_symbol = @s1.$root::@s53.top::@s54.top::@s55::@s57.ok, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 135 : i64, referenced_path = "top.valid", referenced_symbol = @s1.$root::@s53.top::@s54.top::@s55::@s58.valid, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
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
