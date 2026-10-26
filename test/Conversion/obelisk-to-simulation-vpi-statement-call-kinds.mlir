// RUN: obelisk-opt %s --obelisk-sim-prepare | FileCheck %s

// Builtin method identity is explicit even when Slang puts its receiver in
// arguments. Static and implicit class methods resolve exact declarations.
module attributes {coverage.language_version = 2023 : i32} {
  obelisk.sv.symbol.definition @s0.method_calls attributes {definition_kind = 0 : i32, hierarchical_name = "method_calls", name = "method_calls", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
      obelisk.sv.type.class_type @s3.C attributes {bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s29.this} {
        obelisk.sv.symbol.subroutine @s4.stat attributes {hierarchical_name = "C::stat", is_static, name = "stat", node_id = 4 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 5 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine @s5.inst attributes {hierarchical_name = "C::inst", name = "inst", node_id = 6 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, this_variable_path = "C::inst.this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s5.inst::@s6.this, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 7 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "stat", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = false, node_id = 8 : i64, referenced_path = "C::stat", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.stat, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
            }
          }
          obelisk.sv.symbol.variable @s6.this attributes {hierarchical_name = "C::inst.this", is_compiler_generated, is_const, name = "this", node_id = 9 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        obelisk.sv.symbol.subroutine @s7.implicit_call attributes {hierarchical_name = "C::implicit_call", name = "implicit_call", node_id = 10 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, this_variable_path = "C::implicit_call.this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s7.implicit_call::@s8.this, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 11 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "inst", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = false, node_id = 12 : i64, referenced_path = "C::inst", referenced_symbol = @s1.$root::@s2::@s3.C::@s5.inst, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
            }
          }
          obelisk.sv.symbol.variable @s8.this attributes {hierarchical_name = "C::implicit_call.this", is_compiler_generated, is_const, name = "this", node_id = 13 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        obelisk.sv.symbol.subroutine @s9.randomize attributes {hierarchical_name = "C::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 14 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 15 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine @s10.pre_randomize attributes {hierarchical_name = "C::pre_randomize", is_builtin, name = "pre_randomize", node_id = 16 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 17 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine @s11.post_randomize attributes {hierarchical_name = "C::post_randomize", is_builtin, name = "post_randomize", node_id = 18 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 19 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine @s12.get_randstate attributes {hierarchical_name = "C::get_randstate", is_builtin, name = "get_randstate", node_id = 20 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 21 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine @s13.set_randstate attributes {hierarchical_name = "C::set_randstate", is_builtin, name = "set_randstate", node_id = 22 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 23 : i64} {
          }
          obelisk.sv.symbol.formal_argument @s14.state attributes {direction = 0 : i32, hierarchical_name = "C::set_randstate.state", name = "state", node_id = 24 : i64, semantic_type = !obelisk.string} {
          }
        }
        obelisk.sv.symbol.subroutine @s15.srandom attributes {hierarchical_name = "C::srandom", is_builtin, name = "srandom", node_id = 25 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 26 : i64} {
          }
          obelisk.sv.symbol.formal_argument @s16.seed attributes {direction = 0 : i32, hierarchical_name = "C::srandom.seed", name = "seed", node_id = 27 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
        obelisk.sv.symbol.subroutine @s17.rand_mode attributes {hierarchical_name = "C::rand_mode", is_builtin, name = "rand_mode", node_id = 28 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 29 : i64} {
          }
          obelisk.sv.symbol.formal_argument @s18.on_ff attributes {direction = 0 : i32, hierarchical_name = "C::rand_mode.on_ff", name = "on_ff", node_id = 30 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
          }
        }
        obelisk.sv.symbol.subroutine @s19.constraint_mode attributes {hierarchical_name = "C::constraint_mode", is_builtin, name = "constraint_mode", node_id = 31 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 32 : i64} {
          }
          obelisk.sv.symbol.formal_argument @s20.on_ff attributes {direction = 0 : i32, hierarchical_name = "C::constraint_mode.on_ff", name = "on_ff", node_id = 33 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
          }
        }
        obelisk.sv.symbol.variable @s29.this attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 52 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
        }
      }
    }
    obelisk.sv.symbol.instance @s21.method_calls attributes {hierarchical_name = "method_calls", is_uninstantiated = false, name = "method_calls", node_id = 34 : i64, referenced_path = "method_calls", referenced_symbol = @s0.method_calls} {
      obelisk.sv.symbol.instance_body @s22.method_calls attributes {hierarchical_name = "method_calls", name = "method_calls", node_id = 35 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "method_calls", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        obelisk.sv.symbol.variable @s23.c attributes {hierarchical_name = "method_calls.c", lifetime = 1 : i32, name = "c", node_id = 36 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
        }
        obelisk.sv.symbol.variable @s24.q attributes {hierarchical_name = "method_calls.q", lifetime = 1 : i32, name = "q", node_id = 37 : i64, semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>} {
        }
        obelisk.sv.symbol.procedural_block @s25 attributes {hierarchical_name = "method_calls", node_id = 38 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 39 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "stat", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = false, node_id = 40 : i64, referenced_path = "C::stat", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.stat, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
            }
          }
        }
        obelisk.sv.symbol.procedural_block @s26 attributes {hierarchical_name = "method_calls", node_id = 41 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 42 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "size", is_builtin_method, constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, is_void_casted, node_id = 43 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.method_calls", system_scope_path = "method_calls", system_scope_symbol = @s1.$root::@s21.method_calls::@s22.method_calls} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 44 : i64, referenced_path = "method_calls.q", referenced_symbol = @s1.$root::@s21.method_calls::@s22.method_calls::@s24.q, semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>} {
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block @s27 attributes {hierarchical_name = "method_calls", node_id = 45 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 46 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", is_builtin_method, constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, is_void_casted, node_id = 47 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.method_calls", system_scope_path = "method_calls", system_scope_symbol = @s1.$root::@s21.method_calls::@s22.method_calls} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 48 : i64, referenced_path = "method_calls.c", referenced_symbol = @s1.$root::@s21.method_calls::@s22.method_calls::@s23.c, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block @s28 attributes {hierarchical_name = "method_calls", node_id = 49 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 50 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$urandom", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, is_void_casted, node_id = 51 : i64, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.method_calls", system_scope_path = "method_calls", system_scope_symbol = @s1.$root::@s21.method_calls::@s22.method_calls} {
            }
          }
        }
      }
    }
  }
}


// CHECK-DAG: simulation.code_unit.decl [[STATIC:[0-9]+]] {{.*}} function hierarchy "C::stat"
// CHECK-DAG: simulation.code_unit.decl [[INSTANCE:[0-9]+]] {{.*}} function hierarchy "C::inst"
// CHECK-DAG: simulation.code_unit.decl [[IMPLICIT:[0-9]+]] {{.*}} function hierarchy "C::implicit_call"
// CHECK-DAG: simulation.statement.decl {{[0-9]+}} in [[INSTANCE]] scope {{[0-9]+}} type 648
// CHECK-DAG: simulation.statement.decl {{[0-9]+}} in [[IMPLICIT]] scope {{[0-9]+}} type 648
// CHECK-DAG: simulation.code_unit.decl [[CALL:[0-9]+]] {{.*}} initial hierarchy "method_calls.$code_unit_38"
// CHECK-DAG: simulation.code_unit.decl [[SIZE:[0-9]+]] {{.*}} initial hierarchy "method_calls.$code_unit_41"
// CHECK-DAG: simulation.code_unit.decl [[RANDOMIZE:[0-9]+]] {{.*}} initial hierarchy "method_calls.$code_unit_45"
// CHECK-DAG: simulation.code_unit.decl [[SYSTEM:[0-9]+]] {{.*}} initial hierarchy "method_calls.$code_unit_49"
// CHECK-DAG: simulation.statement.decl {{[0-9]+}} in [[CALL]] scope {{[0-9]+}} type 648
// CHECK-DAG: simulation.statement.decl {{[0-9]+}} in [[SIZE]] scope {{[0-9]+}} type 648
// CHECK-DAG: simulation.statement.decl {{[0-9]+}} in [[RANDOMIZE]] scope {{[0-9]+}} type 648
// CHECK-DAG: simulation.statement.decl {{[0-9]+}} in [[SYSTEM]] scope {{[0-9]+}} type 56

// Each exact call statement aliases the corresponding immutable occurrence.
// CHECK: simulation.vpi_call.decl @[[STATIC_CALL:[^ ]+]] id {{[0-9]+}} in {{[0-9]+}} scope 0 type 648 name "stat" statement [[S0:[0-9]+]] within [[S0]] callee {{[0-9]+}}
// CHECK: simulation.vpi_call.decl @[[IMPLICIT_CALL:[^ ]+]] id {{[0-9]+}} in {{[0-9]+}} scope 0 type 648 name "inst" statement [[S1:[0-9]+]] within [[S1]] callee {{[0-9]+}}
// CHECK: simulation.vpi_call.decl @{{[^ ]+}} id {{[0-9]+}} in {{[0-9]+}} scope 1 type 648 name "stat" statement {{[0-9]+}} within {{[0-9]+}} callee {{[0-9]+}}
// CHECK: simulation.vpi_call.decl @{{[^ ]+}} id {{[0-9]+}} in {{[0-9]+}} scope 1 type 648 name "size" statement {{[0-9]+}} within {{[0-9]+}}{{$}}
// CHECK: simulation.vpi_call.decl @{{[^ ]+}} id {{[0-9]+}} in {{[0-9]+}} scope 1 type 648 name "randomize" statement {{[0-9]+}} within {{[0-9]+}}{{$}}
// CHECK: simulation.vpi_call.decl @{{[^ ]+}} id {{[0-9]+}} in {{[0-9]+}} scope 1 type 56 name "$urandom" statement {{[0-9]+}} within {{[0-9]+}}{{$}}
