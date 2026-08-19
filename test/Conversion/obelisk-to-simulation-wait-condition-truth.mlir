// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 12.4 reads a condition as a comparison against zero, which
// only a packed integral value answers from its stored bits. A handle compares
// against null instead, so waiting on the storage being nonzero would never
// release; the condition has to be re-evaluated by an observer.

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "wait_condition_truth", name = "wait_condition_truth", node_id = 0 : i64, sym_name = "s0.wait_condition_truth"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s3.C", this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s22.this} {
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 4 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s4.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 5 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::pre_randomize", is_builtin, name = "pre_randomize", node_id = 6 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s5.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 7 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::post_randomize", is_builtin, name = "post_randomize", node_id = 8 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s6.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 9 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::get_randstate", is_builtin, name = "get_randstate", node_id = 10 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, sym_name = "s7.get_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 11 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::set_randstate", is_builtin, name = "set_randstate", node_id = 12 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s8.set_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 13 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::set_randstate.state", name = "state", node_id = 14 : i64, semantic_type = !obelisk.string, sym_name = "s9.state"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::srandom", is_builtin, name = "srandom", node_id = 15 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s10.srandom", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 16 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::srandom.seed", name = "seed", node_id = 17 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s11.seed"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::rand_mode", is_builtin, name = "rand_mode", node_id = 18 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s12.rand_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 19 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::rand_mode.on_ff", name = "on_ff", node_id = 20 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s13.on_ff"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::constraint_mode", is_builtin, name = "constraint_mode", node_id = 21 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s14.constraint_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 22 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "C::constraint_mode.on_ff", name = "on_ff", node_id = 23 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s15.on_ff"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 36 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s22.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "wait_condition_truth", is_uninstantiated = false, name = "wait_condition_truth", node_id = 24 : i64, referenced_path = "wait_condition_truth", referenced_symbol = @s0.wait_condition_truth, sym_name = "s16.wait_condition_truth"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "wait_condition_truth", name = "wait_condition_truth", node_id = 25 : i64, sym_name = "s17.wait_condition_truth", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "wait_condition_truth.handle", lifetime = 1 : i32, name = "handle", node_id = 26 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s18.handle"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "wait_condition_truth.packed_value", lifetime = 1 : i32, name = "packed_value", node_id = 27 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s19.packed_value"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "wait_condition_truth", node_id = 28 : i64, procedure_kind = 0 : i32, sym_name = "s20", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.wait attributes {node_id = 29 : i64} {
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 30 : i64, referenced_path = "wait_condition_truth.handle", referenced_symbol = @s1.$root::@s16.wait_condition_truth::@s17.wait_condition_truth::@s18.handle, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
            }
            obelisk.sv.statement.empty attributes {node_id = 31 : i64} {
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "wait_condition_truth", node_id = 32 : i64, procedure_kind = 0 : i32, sym_name = "s21", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.wait attributes {node_id = 33 : i64} {
            obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 34 : i64, referenced_path = "wait_condition_truth.packed_value", referenced_symbol = @s1.$root::@s16.wait_condition_truth::@s17.wait_condition_truth::@s19.packed_value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            }
            obelisk.sv.statement.empty attributes {node_id = 35 : i64} {
            }
          }
        }
      }
    }
  }
}


// CHECK-LABEL: obelisk_sim.func private @unit_0
// CHECK: obelisk_sim.observer.bind
// CHECK: obelisk_sim.suspend.observe
// CHECK-NOT: obelisk_sim.suspend.level

// A packed condition keeps the direct suspend on its storage.
// CHECK-LABEL: obelisk_sim.func private @unit_1
// CHECK: obelisk_sim.suspend.level %arg1
