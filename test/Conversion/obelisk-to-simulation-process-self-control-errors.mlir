// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2023 9.7: a process cannot await its own termination, and a
// function cannot suspend its own process. Both report a runtime error when
// the target is the executing process and continue instead of waiting.
//
//   module top;
//     process p;
//     function void stop(); p.suspend(); endfunction
//     initial begin p.await(); stop(); end
//   endmodule

// CHECK-LABEL: simulation.func private @unit_1(
// CHECK: %[[AWAITED:.*]] = simulation.ref.load %arg1
// CHECK-NEXT: %[[SELF:.*]] = simulation.process.current
// CHECK-NEXT: %[[IS_SELF:.*]] = simulation.process.equal %[[AWAITED]], %[[SELF]]
// CHECK-NEXT: cf.cond_br %[[IS_SELF]], ^[[REPORT:bb[0-9]+]], ^[[WAIT:bb[0-9]+]]
// CHECK: ^[[REPORT]]:
// CHECK-NEXT: simulation.error %arg0
// CHECK-NEXT: simulation.bytes.constant "ERROR: {{.*}}: a process cannot await its own termination"
// CHECK: ^[[WAIT]]:
// CHECK-NEXT: simulation.suspend.await %[[AWAITED]]
// CHECK: %[[TARGET:.*]] = simulation.ref.load
// CHECK-NEXT: %[[CURRENT:.*]] = simulation.process.current
// CHECK-NEXT: %[[TARGETS_SELF:.*]] = simulation.process.equal %[[TARGET]], %[[CURRENT]]
// CHECK-NEXT: cf.cond_br %[[TARGETS_SELF]], ^[[SUSPEND_ERROR:bb[0-9]+]], ^[[SUSPEND:bb[0-9]+]]
// CHECK: ^[[SUSPEND_ERROR]]:
// CHECK-NEXT: simulation.error %arg0
// CHECK: "ERROR: {{.*}}: a function cannot suspend its own process"
// CHECK: ^[[SUSPEND]]:
// CHECK-NEXT: simulation.process.control suspend %[[TARGET]]

module attributes {coverage.language_version = 2023 : i32, obelisk.enum_domains = [{names = ["FINISHED", "RUNNING", "WAITING", "SUSPENDED", "KILLED"], type = !obelisk.enum<"std::process", !obelisk.integral<32, true, false, 31 : 0, int>>, values = ["0", "1", "2", "3", "4"]}]} {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    obelisk.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        obelisk.sv.symbol.variable @s5.p attributes {hierarchical_name = "top.p", lifetime = 1 : i32, name = "p", node_id = 5 : i64, semantic_type = !obelisk.class_handle<@s7.std::@s6.process>} {
        }
        obelisk.sv.symbol.subroutine @s8.stop attributes {default_lifetime = 1 : i32, hierarchical_name = "top.stop", name = "stop", node_id = 6 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 7 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "suspend", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = true, is_signed = false, is_super_class = false, is_system_call = false, node_id = 8 : i64, referenced_path = "std::process::suspend", referenced_symbol = @s7.std::@s6.process::@s20.suspend, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 9 : i64, referenced_path = "top.p", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.p, semantic_type = !obelisk.class_handle<@s7.std::@s6.process>} {
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block @s9 attributes {hierarchical_name = "top", node_id = 10 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 11 : i64} {
            obelisk.sv.statement.list attributes {node_id = 12 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 13 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "await", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = true, is_signed = false, is_super_class = false, is_system_call = false, node_id = 14 : i64, referenced_path = "std::process::await", referenced_symbol = @s7.std::@s6.process::@s19.await, semantic_type = !obelisk.void, subroutine_kind = 1 : i32} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 15 : i64, referenced_path = "top.p", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.p, semantic_type = !obelisk.class_handle<@s7.std::@s6.process>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 16 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "stop", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = false, node_id = 17 : i64, referenced_path = "top.stop", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.stop, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
                }
              }
            }
          }
        }
      }
    }
  }
  obelisk.sv.symbol.package @s7.std attributes {hierarchical_name = "std", name = "std", node_id = 18 : i64, simulation.vpi_automatic = false, simulation.vpi_definition_name = "std"} {
    obelisk.sv.type.class_type @s6.process attributes {bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "std::process", implemented_interfaces = [], is_abstract = true, is_final = true, is_interface = false, is_uninstantiated = false, name = "process", node_id = 19 : i64, semantic_type = !obelisk.class_handle<@s7.std::@s6.process>} {
      obelisk.sv.symbol.transparent_member @s10.FINISHED attributes {hierarchical_name = "std::process::FINISHED", name = "FINISHED", node_id = 20 : i64} {
      }
      obelisk.sv.symbol.transparent_member @s11.RUNNING attributes {hierarchical_name = "std::process::RUNNING", name = "RUNNING", node_id = 21 : i64} {
      }
      obelisk.sv.symbol.transparent_member @s12.WAITING attributes {hierarchical_name = "std::process::WAITING", name = "WAITING", node_id = 22 : i64} {
      }
      obelisk.sv.symbol.transparent_member @s13.SUSPENDED attributes {hierarchical_name = "std::process::SUSPENDED", name = "SUSPENDED", node_id = 23 : i64} {
      }
      obelisk.sv.symbol.transparent_member @s14.KILLED attributes {hierarchical_name = "std::process::KILLED", name = "KILLED", node_id = 24 : i64} {
      }
      obelisk.sv.type.type_alias @s15.state attributes {hierarchical_name = "std::process::state", name = "state", node_id = 25 : i64, semantic_type = !obelisk.enum<"std::process", !obelisk.integral<32, true, false, 31 : 0, int>>, vpi_source_type_identity = 0 : i64, vpi_typedef_layers = [{aliases = [@s7.std::@s6.process::@s15.state], path = array<i64>}]} {
      }
      obelisk.sv.symbol.subroutine @s16.self attributes {hierarchical_name = "std::process::self", is_builtin, is_static, name = "self", node_id = 26 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.class_handle<@s7.std::@s6.process>, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.statement.list attributes {node_id = 27 : i64} {
        }
      }
      obelisk.sv.symbol.subroutine @s17.status attributes {hierarchical_name = "std::process::status", is_builtin, name = "status", node_id = 28 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.enum<"std::process", !obelisk.integral<32, true, false, 31 : 0, int>>, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_source_type_identity = 0 : i64, vpi_typedef_layers = [{aliases = [@s7.std::@s6.process::@s15.state], path = array<i64>}]} {
        obelisk.sv.statement.list attributes {node_id = 29 : i64} {
        }
      }
      obelisk.sv.symbol.subroutine @s18.kill attributes {hierarchical_name = "std::process::kill", is_builtin, name = "kill", node_id = 30 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.statement.list attributes {node_id = 31 : i64} {
        }
      }
      obelisk.sv.symbol.subroutine @s19.await attributes {hierarchical_name = "std::process::await", is_builtin, name = "await", node_id = 32 : i64, semantic_type = !obelisk.subroutine<() -> (), true>, subroutine_kind = 1 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.statement.list attributes {node_id = 33 : i64} {
        }
      }
      obelisk.sv.symbol.subroutine @s20.suspend attributes {hierarchical_name = "std::process::suspend", is_builtin, name = "suspend", node_id = 34 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.statement.list attributes {node_id = 35 : i64} {
        }
      }
      obelisk.sv.symbol.subroutine @s21.resume attributes {hierarchical_name = "std::process::resume", is_builtin, name = "resume", node_id = 36 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.statement.list attributes {node_id = 37 : i64} {
        }
      }
      obelisk.sv.symbol.subroutine @s22.get_randstate attributes {hierarchical_name = "std::process::get_randstate", is_builtin, name = "get_randstate", node_id = 38 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.statement.list attributes {node_id = 39 : i64} {
        }
      }
      obelisk.sv.symbol.subroutine @s23.srandom attributes {hierarchical_name = "std::process::srandom", is_builtin, name = "srandom", node_id = 40 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.statement.list attributes {node_id = 41 : i64} {
        }
        obelisk.sv.symbol.formal_argument @s24.seed attributes {direction = 0 : i32, hierarchical_name = "std::process::srandom.seed", name = "seed", node_id = 42 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        }
      }
      obelisk.sv.symbol.subroutine @s25.set_randstate attributes {hierarchical_name = "std::process::set_randstate", is_builtin, name = "set_randstate", node_id = 43 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.statement.list attributes {node_id = 44 : i64} {
        }
        obelisk.sv.symbol.formal_argument @s26.state attributes {direction = 0 : i32, hierarchical_name = "std::process::set_randstate.state", name = "state", node_id = 45 : i64, semantic_type = !obelisk.string} {
        }
      }
    }
    obelisk.sv.type.class_type @s27.semaphore attributes {bitstream_width = 0 : i64, constructor_path = "std::semaphore::new", constructor_symbol = @s7.std::@s27.semaphore::@s28.new, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "std::semaphore", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "semaphore", node_id = 46 : i64, semantic_type = !obelisk.class_handle<@s7.std::@s27.semaphore>} {
      obelisk.sv.symbol.subroutine @s28.new attributes {hierarchical_name = "std::semaphore::new", is_builtin, is_constructor, name = "new", node_id = 47 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.statement.list attributes {node_id = 48 : i64} {
        }
        obelisk.sv.symbol.formal_argument @s29.keyCount attributes {direction = 0 : i32, hierarchical_name = "std::semaphore::new.keyCount", name = "keyCount", node_id = 49 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 50 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
      }
      obelisk.sv.symbol.subroutine @s30.put attributes {hierarchical_name = "std::semaphore::put", is_builtin, name = "put", node_id = 51 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.statement.list attributes {node_id = 52 : i64} {
        }
        obelisk.sv.symbol.formal_argument @s31.keyCount attributes {direction = 0 : i32, hierarchical_name = "std::semaphore::put.keyCount", name = "keyCount", node_id = 53 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 54 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
      }
      obelisk.sv.symbol.subroutine @s32.get attributes {hierarchical_name = "std::semaphore::get", is_builtin, name = "get", node_id = 55 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> (), true>, subroutine_kind = 1 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.statement.list attributes {node_id = 56 : i64} {
        }
        obelisk.sv.symbol.formal_argument @s33.keyCount attributes {direction = 0 : i32, hierarchical_name = "std::semaphore::get.keyCount", name = "keyCount", node_id = 57 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 58 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
      }
      obelisk.sv.symbol.subroutine @s34.try_get attributes {hierarchical_name = "std::semaphore::try_get", is_builtin, name = "try_get", node_id = 59 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.statement.list attributes {node_id = 60 : i64} {
        }
        obelisk.sv.symbol.formal_argument @s35.keyCount attributes {direction = 0 : i32, hierarchical_name = "std::semaphore::try_get.keyCount", name = "keyCount", node_id = 61 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 62 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
      }
    }
    obelisk.sv.symbol.generic_class_def @s36.mailbox attributes {hierarchical_name = "std::mailbox", is_interface = false, name = "mailbox", node_id = 63 : i64, specialization_count = 0 : i64} {
    }
    obelisk.sv.symbol.subroutine @s37.randomize attributes {hierarchical_name = "std::randomize", is_builtin, is_randomize, name = "randomize", node_id = 64 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
      obelisk.sv.statement.list attributes {node_id = 65 : i64} {
      }
    }
    obelisk.sv.symbol.generic_class_def @s38.weak_reference attributes {hierarchical_name = "std::weak_reference", is_interface = false, name = "weak_reference", node_id = 66 : i64, specialization_count = 0 : i64} {
    }
  }
}

