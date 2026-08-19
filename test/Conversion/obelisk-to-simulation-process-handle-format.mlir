// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// A process handle is a handle like any other. IEEE 1800-2017 12.4 reads a
// condition as a comparison against zero, which for a handle is a comparison
// against the null 9.7 gives it, and 21.2.1.7 renders it as a singular value
// whose null spelling is "null".

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "process_handle_format", name = "process_handle_format", node_id = 0 : i64, sym_name = "s0.process_handle_format"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "process_handle_format", is_uninstantiated = false, name = "process_handle_format", node_id = 3 : i64, referenced_path = "process_handle_format", referenced_symbol = @s0.process_handle_format, sym_name = "s3.process_handle_format"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "process_handle_format", name = "process_handle_format", node_id = 4 : i64, sym_name = "s4.process_handle_format", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "process_handle_format.p", lifetime = 1 : i32, name = "p", node_id = 5 : i64, semantic_type = !obelisk.class_handle<@s7.std::@s6.process>, sym_name = "s5.p"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "process_handle_format", node_id = 6 : i64, procedure_kind = 0 : i32, sym_name = "s8", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 7 : i64} {
            obelisk.sv.statement.conditional attributes {check_kind = 0 : i32, condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, has_else = false, node_id = 8 : i64} {
              obelisk.sv.expression.unary_op attributes {is_signed = false, node_id = 9 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 10 : i64, referenced_path = "process_handle_format.p", referenced_symbol = @s1.$root::@s3.process_handle_format::@s4.process_handle_format::@s5.p, semantic_type = !obelisk.class_handle<@s7.std::@s6.process>} {
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 11 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 12 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.process_handle_format", system_scope_path = "process_handle_format", system_scope_symbol = @s1.$root::@s3.process_handle_format::@s4.process_handle_format} {
                  obelisk.sv.expression.string_literal attributes {constant_value = "p=%p", is_signed = false, node_id = 13 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 14 : i64, referenced_path = "process_handle_format.p", referenced_symbol = @s1.$root::@s3.process_handle_format::@s4.process_handle_format::@s5.p, semantic_type = !obelisk.class_handle<@s7.std::@s6.process>} {
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  obelisk.sv.symbol.package attributes {hierarchical_name = "std", name = "std", node_id = 15 : i64, sym_name = "s7.std"} {
    obelisk.sv.type.class_type attributes {bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "std::process", implemented_interfaces = [], is_abstract = true, is_final = true, is_interface = false, is_uninstantiated = false, name = "process", node_id = 16 : i64, semantic_type = !obelisk.class_handle<@s7.std::@s6.process>, sym_name = "s6.process"} {
      obelisk.sv.symbol.transparent_member attributes {hierarchical_name = "std::process::FINISHED", name = "FINISHED", node_id = 17 : i64, sym_name = "s9.FINISHED"} {
      }
      obelisk.sv.symbol.transparent_member attributes {hierarchical_name = "std::process::RUNNING", name = "RUNNING", node_id = 18 : i64, sym_name = "s10.RUNNING"} {
      }
      obelisk.sv.symbol.transparent_member attributes {hierarchical_name = "std::process::WAITING", name = "WAITING", node_id = 19 : i64, sym_name = "s11.WAITING"} {
      }
      obelisk.sv.symbol.transparent_member attributes {hierarchical_name = "std::process::SUSPENDED", name = "SUSPENDED", node_id = 20 : i64, sym_name = "s12.SUSPENDED"} {
      }
      obelisk.sv.symbol.transparent_member attributes {hierarchical_name = "std::process::KILLED", name = "KILLED", node_id = 21 : i64, sym_name = "s13.KILLED"} {
      }
      obelisk.sv.type.type_alias attributes {hierarchical_name = "std::process::state", name = "state", node_id = 22 : i64, semantic_type = !obelisk.enum<"std::process", !obelisk.integral<32, true, false, 31 : 0, int>>, sym_name = "s14.state"} {
      }
      obelisk.sv.symbol.subroutine attributes {hierarchical_name = "std::process::self", is_builtin, is_static, name = "self", node_id = 23 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.class_handle<@s7.std::@s6.process>, false>, subroutine_kind = 0 : i32, sym_name = "s15.self", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.statement.list attributes {node_id = 24 : i64} {
        }
      }
      obelisk.sv.symbol.subroutine attributes {hierarchical_name = "std::process::status", is_builtin, name = "status", node_id = 25 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.enum<"std::process", !obelisk.integral<32, true, false, 31 : 0, int>>, false>, subroutine_kind = 0 : i32, sym_name = "s16.status", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.statement.list attributes {node_id = 26 : i64} {
        }
      }
      obelisk.sv.symbol.subroutine attributes {hierarchical_name = "std::process::kill", is_builtin, name = "kill", node_id = 27 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s17.kill", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.statement.list attributes {node_id = 28 : i64} {
        }
      }
      obelisk.sv.symbol.subroutine attributes {hierarchical_name = "std::process::await", is_builtin, name = "await", node_id = 29 : i64, semantic_type = !obelisk.subroutine<() -> (), true>, subroutine_kind = 1 : i32, sym_name = "s18.await", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.statement.list attributes {node_id = 30 : i64} {
        }
      }
      obelisk.sv.symbol.subroutine attributes {hierarchical_name = "std::process::suspend", is_builtin, name = "suspend", node_id = 31 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s19.suspend", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.statement.list attributes {node_id = 32 : i64} {
        }
      }
      obelisk.sv.symbol.subroutine attributes {hierarchical_name = "std::process::resume", is_builtin, name = "resume", node_id = 33 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s20.resume", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.statement.list attributes {node_id = 34 : i64} {
        }
      }
      obelisk.sv.symbol.subroutine attributes {hierarchical_name = "std::process::get_randstate", is_builtin, name = "get_randstate", node_id = 35 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, sym_name = "s21.get_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.statement.list attributes {node_id = 36 : i64} {
        }
      }
      obelisk.sv.symbol.subroutine attributes {hierarchical_name = "std::process::srandom", is_builtin, name = "srandom", node_id = 37 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s22.srandom", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.statement.list attributes {node_id = 38 : i64} {
        }
        obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "std::process::srandom.seed", name = "seed", node_id = 39 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s23.seed"} {
        }
      }
      obelisk.sv.symbol.subroutine attributes {hierarchical_name = "std::process::set_randstate", is_builtin, name = "set_randstate", node_id = 40 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s24.set_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.statement.list attributes {node_id = 41 : i64} {
        }
        obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "std::process::set_randstate.state", name = "state", node_id = 42 : i64, semantic_type = !obelisk.string, sym_name = "s25.state"} {
        }
      }
    }
    obelisk.sv.type.class_type attributes {bitstream_width = 0 : i64, constructor_path = "std::semaphore::new", constructor_symbol = @s7.std::@s26.semaphore::@s27.new, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "std::semaphore", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "semaphore", node_id = 43 : i64, semantic_type = !obelisk.class_handle<@s7.std::@s26.semaphore>, sym_name = "s26.semaphore"} {
      obelisk.sv.symbol.subroutine attributes {hierarchical_name = "std::semaphore::new", is_builtin, is_constructor, name = "new", node_id = 44 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s27.new", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.statement.list attributes {node_id = 45 : i64} {
        }
        obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "std::semaphore::new.keyCount", name = "keyCount", node_id = 46 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s28.keyCount"} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 47 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
      }
      obelisk.sv.symbol.subroutine attributes {hierarchical_name = "std::semaphore::put", is_builtin, name = "put", node_id = 48 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s29.put", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.statement.list attributes {node_id = 49 : i64} {
        }
        obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "std::semaphore::put.keyCount", name = "keyCount", node_id = 50 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s30.keyCount"} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 51 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
      }
      obelisk.sv.symbol.subroutine attributes {hierarchical_name = "std::semaphore::get", is_builtin, name = "get", node_id = 52 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> (), true>, subroutine_kind = 1 : i32, sym_name = "s31.get", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.statement.list attributes {node_id = 53 : i64} {
        }
        obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "std::semaphore::get.keyCount", name = "keyCount", node_id = 54 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s32.keyCount"} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 55 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
      }
      obelisk.sv.symbol.subroutine attributes {hierarchical_name = "std::semaphore::try_get", is_builtin, name = "try_get", node_id = 56 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s33.try_get", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.statement.list attributes {node_id = 57 : i64} {
        }
        obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "std::semaphore::try_get.keyCount", name = "keyCount", node_id = 58 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s34.keyCount"} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 59 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
      }
    }
    obelisk.sv.symbol.generic_class_def attributes {hierarchical_name = "std::mailbox", is_interface = false, name = "mailbox", node_id = 60 : i64, specialization_count = 0 : i64, sym_name = "s35.mailbox"} {
    }
    obelisk.sv.symbol.subroutine attributes {hierarchical_name = "std::randomize", is_builtin, is_randomize, name = "randomize", node_id = 61 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s36.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
      obelisk.sv.statement.list attributes {node_id = 62 : i64} {
      }
    }
    obelisk.sv.symbol.generic_class_def attributes {hierarchical_name = "std::weak_reference", is_interface = false, name = "weak_reference", node_id = 63 : i64, specialization_count = 0 : i64, sym_name = "s37.weak_reference"} {
    }
  }
}


// CHECK-LABEL: obelisk_sim.func private @unit_0
// CHECK: %[[NULL:.*]] = obelisk_sim.process.null
// CHECK: obelisk_sim.process.equal %{{.*}}, %[[NULL]]
// The handle reaches the output list as a handle, not as a packed value.
// CHECK: obelisk_sim.display
// CHECK-SAME: flags = [0, 512]
// CHECK-SAME: !obelisk_sim.bytes, !obelisk_sim.process
