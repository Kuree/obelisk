// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition attributes {
    definition_kind = 0 : i32, hierarchical_name = "process_front",
    name = "process_front", node_id = 0 : i64, sym_name = "s0.process_front"
  } {
  }
  obelisk.sv.symbol.root attributes {
    hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64,
    sym_name = "s1.$root"
  } {
    obelisk.sv.symbol.compilation_unit attributes {
      hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"
    } {
    }
    obelisk.sv.symbol.instance attributes {
      hierarchical_name = "process_front", is_uninstantiated = false,
      name = "process_front", node_id = 3 : i64,
      referenced_path = "process_front", referenced_symbol = @s0.process_front,
      sym_name = "s3.process_front"
    } {
      obelisk.sv.symbol.instance_body attributes {
        hierarchical_name = "process_front", name = "process_front",
        node_id = 4 : i64, sym_name = "s4.process_front",
        time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64
      } {
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "process_front.p", lifetime = 1 : i32,
          name = "p", node_id = 5 : i64,
          semantic_type = !obelisk.class_handle<@s7.std::@s6.process>,
          sym_name = "s5.p"
        } {
        }
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "process_front.st", lifetime = 1 : i32,
          name = "st", node_id = 6 : i64,
          semantic_type = !obelisk.enum<"std::process", !obelisk.integral<32, true, false, 31 : 0, int>>,
          sym_name = "s8.st"
        } {
        }
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "process_front.is_null", lifetime = 1 : i32,
          name = "is_null", node_id = 7 : i64,
          semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>,
          sym_name = "s9.is_null"
        } {
        }
        obelisk.sv.symbol.procedural_block attributes {
          hierarchical_name = "process_front", node_id = 8 : i64,
          procedure_kind = 0 : i32, sym_name = "s10",
          time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.block attributes {node_id = 9 : i64} {
            obelisk.sv.statement.list attributes {node_id = 10 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 100 : i64} {
                obelisk.sv.expression.assignment attributes {
                  assignment_kind = 0 : i32, is_signed = false,
                  node_id = 101 : i64,
                  semantic_type = !obelisk.class_handle<@s7.std::@s6.process>
                } {
                  obelisk.sv.expression.named_value attributes {
                    is_signed = false, node_id = 102 : i64,
                    referenced_path = "process_front.p",
                    referenced_symbol = @s1.$root::@s3.process_front::@s4.process_front::@s5.p,
                    semantic_type = !obelisk.class_handle<@s7.std::@s6.process>
                  } {
                  }
                  obelisk.sv.expression.conversion attributes {
                    is_signed = false, node_id = 103 : i64,
                    semantic_type = !obelisk.class_handle<@s7.std::@s6.process>
                  } {
                    obelisk.sv.expression.null_literal attributes {
                      is_signed = false, node_id = 104 : i64,
                      semantic_type = !obelisk.null
                    } {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 11 : i64} {
                obelisk.sv.expression.assignment attributes {
                  assignment_kind = 0 : i32, is_signed = false,
                  node_id = 12 : i64,
                  semantic_type = !obelisk.class_handle<@s7.std::@s6.process>
                } {
                  obelisk.sv.expression.named_value attributes {
                    is_signed = false, node_id = 13 : i64,
                    referenced_path = "process_front.p",
                    referenced_symbol = @s1.$root::@s3.process_front::@s4.process_front::@s5.p,
                    semantic_type = !obelisk.class_handle<@s7.std::@s6.process>
                  } {
                  }
                  obelisk.sv.expression.call attributes {
                    argument_count = 0 : i64, callee_name = "self",
                    constraint_restrictions = [], defaulted_arguments = array<i64>,
                    has_inline_constraints = false,
                    has_iterator_expression = false,
                    has_output_arguments = false, has_this_class = false,
                    is_signed = false, is_super_class = false,
                    is_system_call = false, node_id = 14 : i64,
                    referenced_path = "std::process::self",
                    referenced_symbol = @s7.std::@s6.process::@s17.self,
                    semantic_type = !obelisk.class_handle<@s7.std::@s6.process>,
                    subroutine_kind = 0 : i32
                  } {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 15 : i64} {
                obelisk.sv.expression.assignment attributes {
                  assignment_kind = 0 : i32, is_signed = false,
                  node_id = 16 : i64,
                  semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
                } {
                  obelisk.sv.expression.named_value attributes {
                    is_signed = false, node_id = 17 : i64,
                    referenced_path = "process_front.is_null",
                    referenced_symbol = @s1.$root::@s3.process_front::@s4.process_front::@s9.is_null,
                    semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
                  } {
                  }
                  obelisk.sv.expression.binary_op attributes {
                    is_signed = false, node_id = 18 : i64,
                    operator_kind = 9 : i32,
                    semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
                  } {
                    obelisk.sv.expression.named_value attributes {
                      is_signed = false, node_id = 19 : i64,
                      referenced_path = "process_front.p",
                      referenced_symbol = @s1.$root::@s3.process_front::@s4.process_front::@s5.p,
                      semantic_type = !obelisk.class_handle<@s7.std::@s6.process>
                    } {
                    }
                    obelisk.sv.expression.null_literal attributes {
                      is_signed = false, node_id = 20 : i64,
                      semantic_type = !obelisk.null
                    } {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 21 : i64} {
                obelisk.sv.expression.assignment attributes {
                  assignment_kind = 0 : i32, is_signed = true,
                  node_id = 22 : i64,
                  semantic_type = !obelisk.enum<"std::process", !obelisk.integral<32, true, false, 31 : 0, int>>
                } {
                  obelisk.sv.expression.named_value attributes {
                    is_signed = true, node_id = 23 : i64,
                    referenced_path = "process_front.st",
                    referenced_symbol = @s1.$root::@s3.process_front::@s4.process_front::@s8.st,
                    semantic_type = !obelisk.enum<"std::process", !obelisk.integral<32, true, false, 31 : 0, int>>
                  } {
                  }
                  obelisk.sv.expression.call attributes {
                    argument_count = 0 : i64, callee_name = "status",
                    constraint_restrictions = [], defaulted_arguments = array<i64>,
                    has_inline_constraints = false,
                    has_iterator_expression = false,
                    has_output_arguments = false, has_this_class = true,
                    is_signed = true, is_super_class = false,
                    is_system_call = false, node_id = 24 : i64,
                    referenced_path = "std::process::status",
                    referenced_symbol = @s7.std::@s6.process::@s18.status,
                    semantic_type = !obelisk.enum<"std::process", !obelisk.integral<32, true, false, 31 : 0, int>>,
                    subroutine_kind = 0 : i32
                  } {
                    obelisk.sv.expression.named_value attributes {
                      is_signed = false, node_id = 25 : i64,
                      referenced_path = "process_front.p",
                      referenced_symbol = @s1.$root::@s3.process_front::@s4.process_front::@s5.p,
                      semantic_type = !obelisk.class_handle<@s7.std::@s6.process>
                    } {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 26 : i64} {
                obelisk.sv.expression.call attributes {
                  argument_count = 0 : i64, callee_name = "await",
                  constraint_restrictions = [], defaulted_arguments = array<i64>,
                  has_inline_constraints = false,
                  has_iterator_expression = false,
                  has_output_arguments = false, has_this_class = true,
                  is_signed = false, is_super_class = false,
                  is_system_call = false, node_id = 27 : i64,
                  referenced_path = "std::process::await",
                  referenced_symbol = @s7.std::@s6.process::@s20.await,
                  semantic_type = !obelisk.void, subroutine_kind = 1 : i32
                } {
                  obelisk.sv.expression.named_value attributes {
                    is_signed = false, node_id = 28 : i64,
                    referenced_path = "process_front.p",
                    referenced_symbol = @s1.$root::@s3.process_front::@s4.process_front::@s5.p,
                    semantic_type = !obelisk.class_handle<@s7.std::@s6.process>
                  } {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 29 : i64} {
                obelisk.sv.expression.call attributes {
                  argument_count = 0 : i64, callee_name = "resume",
                  constraint_restrictions = [], defaulted_arguments = array<i64>,
                  has_inline_constraints = false,
                  has_iterator_expression = false,
                  has_output_arguments = false, has_this_class = true,
                  is_signed = false, is_super_class = false,
                  is_system_call = false, node_id = 30 : i64,
                  referenced_path = "std::process::resume",
                  referenced_symbol = @s7.std::@s6.process::@s22.resume,
                  semantic_type = !obelisk.void, subroutine_kind = 0 : i32
                } {
                  obelisk.sv.expression.named_value attributes {
                    is_signed = false, node_id = 31 : i64,
                    referenced_path = "process_front.p",
                    referenced_symbol = @s1.$root::@s3.process_front::@s4.process_front::@s5.p,
                    semantic_type = !obelisk.class_handle<@s7.std::@s6.process>
                  } {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 32 : i64} {
                obelisk.sv.expression.call attributes {
                  argument_count = 0 : i64, callee_name = "suspend",
                  constraint_restrictions = [], defaulted_arguments = array<i64>,
                  has_inline_constraints = false,
                  has_iterator_expression = false,
                  has_output_arguments = false, has_this_class = true,
                  is_signed = false, is_super_class = false,
                  is_system_call = false, node_id = 33 : i64,
                  referenced_path = "std::process::suspend",
                  referenced_symbol = @s7.std::@s6.process::@s21.suspend,
                  semantic_type = !obelisk.void, subroutine_kind = 0 : i32
                } {
                  obelisk.sv.expression.named_value attributes {
                    is_signed = false, node_id = 34 : i64,
                    referenced_path = "process_front.p",
                    referenced_symbol = @s1.$root::@s3.process_front::@s4.process_front::@s5.p,
                    semantic_type = !obelisk.class_handle<@s7.std::@s6.process>
                  } {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 35 : i64} {
                obelisk.sv.expression.call attributes {
                  argument_count = 0 : i64, callee_name = "kill",
                  constraint_restrictions = [], defaulted_arguments = array<i64>,
                  has_inline_constraints = false,
                  has_iterator_expression = false,
                  has_output_arguments = false, has_this_class = true,
                  is_signed = false, is_super_class = false,
                  is_system_call = false, node_id = 36 : i64,
                  referenced_path = "std::process::kill",
                  referenced_symbol = @s7.std::@s6.process::@s19.kill,
                  semantic_type = !obelisk.void, subroutine_kind = 0 : i32
                } {
                  obelisk.sv.expression.named_value attributes {
                    is_signed = false, node_id = 37 : i64,
                    referenced_path = "process_front.p",
                    referenced_symbol = @s1.$root::@s3.process_front::@s4.process_front::@s5.p,
                    semantic_type = !obelisk.class_handle<@s7.std::@s6.process>
                  } {
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  obelisk.sv.symbol.package attributes {
    hierarchical_name = "std", name = "std", node_id = 38 : i64,
    sym_name = "s7.std"
  } {
    obelisk.sv.type.class_type attributes {
      bitstream_width = 0 : i64, declared_interfaces = [],
      generic_parameter_paths = [], generic_parameter_symbols = [],
      has_base_constructor_call = false, has_cycles = false,
      hierarchical_name = "std::process", implemented_interfaces = [],
      is_abstract = true, is_final = true, is_interface = false,
      is_uninstantiated = false, name = "process", node_id = 39 : i64,
      semantic_type = !obelisk.class_handle<@s7.std::@s6.process>,
      sym_name = "s6.process"
    } {
      obelisk.sv.symbol.subroutine attributes {
        hierarchical_name = "std::process::self", is_builtin, is_static,
        name = "self", node_id = 40 : i64,
        semantic_type = !obelisk.subroutine<() -> !obelisk.class_handle<@s7.std::@s6.process>, false>,
        subroutine_kind = 0 : i32, sym_name = "s17.self",
        time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64
      } {
        obelisk.sv.statement.list attributes {node_id = 41 : i64} {
        }
      }
      obelisk.sv.symbol.subroutine attributes {
        hierarchical_name = "std::process::status", is_builtin,
        name = "status", node_id = 42 : i64,
        semantic_type = !obelisk.subroutine<() -> !obelisk.enum<"std::process", !obelisk.integral<32, true, false, 31 : 0, int>>, false>,
        subroutine_kind = 0 : i32, sym_name = "s18.status",
        time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64
      } {
        obelisk.sv.statement.list attributes {node_id = 43 : i64} {
        }
      }
      obelisk.sv.symbol.subroutine attributes {
        hierarchical_name = "std::process::kill", is_builtin, name = "kill",
        node_id = 44 : i64,
        semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>,
        subroutine_kind = 0 : i32, sym_name = "s19.kill",
        time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64
      } {
        obelisk.sv.statement.list attributes {node_id = 45 : i64} {
        }
      }
      obelisk.sv.symbol.subroutine attributes {
        hierarchical_name = "std::process::await", is_builtin,
        name = "await", node_id = 46 : i64,
        semantic_type = !obelisk.subroutine<() -> (), true>,
        subroutine_kind = 1 : i32, sym_name = "s20.await",
        time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64
      } {
        obelisk.sv.statement.list attributes {node_id = 47 : i64} {
        }
      }
      obelisk.sv.symbol.subroutine attributes {
        hierarchical_name = "std::process::suspend", is_builtin,
        name = "suspend", node_id = 48 : i64,
        semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>,
        subroutine_kind = 0 : i32, sym_name = "s21.suspend",
        time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64
      } {
        obelisk.sv.statement.list attributes {node_id = 49 : i64} {
        }
      }
      obelisk.sv.symbol.subroutine attributes {
        hierarchical_name = "std::process::resume", is_builtin,
        name = "resume", node_id = 50 : i64,
        semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>,
        subroutine_kind = 0 : i32, sym_name = "s22.resume",
        time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64
      } {
        obelisk.sv.statement.list attributes {node_id = 51 : i64} {
        }
      }
    }
  }
}

// The standard process class is an opaque scheduler token, never a managed
// class descriptor. Its default initialization and equality use typed process
// operations, while every standard method maps to the matching control op.
// CHECK: obelisk_sim.storage.decl {{.*}} : !obelisk_sim.process design hierarchy "process_front.p"
// CHECK-LABEL: obelisk_sim.func private @unit_0(
// CHECK: %[[NULL:.*]] = obelisk_sim.process.null
// CHECK: obelisk_sim.ref.store %[[NULL]] to {{.*}} : !obelisk_sim.process
// CHECK: %[[CURRENT:.*]] = obelisk_sim.process.current
// CHECK: obelisk_sim.ref.store %[[CURRENT]] to {{.*}} : !obelisk_sim.process
// CHECK: %[[PROCESS0:.*]] = obelisk_sim.ref.load {{.*}} : {{.*}} -> !obelisk_sim.process
// CHECK: %[[EQUAL:.*]] = obelisk_sim.process.equal %[[PROCESS0]], %[[NULL]]
// CHECK: %[[PROCESS1:.*]] = obelisk_sim.ref.load {{.*}} : {{.*}} -> !obelisk_sim.process
// CHECK: %[[STATUS:.*]] = obelisk_sim.process.status %[[PROCESS1]]
// CHECK: obelisk_sim.ref.store %[[STATUS]] to {{.*}} : i32
// CHECK: %[[PROCESS2:.*]] = obelisk_sim.ref.load {{.*}} : {{.*}} -> !obelisk_sim.process
// CHECK: obelisk_sim.suspend.await %[[PROCESS2]] to ^[[AWAIT:bb[0-9]+]]
// CHECK: ^[[AWAIT]]:
// CHECK: %[[PROCESS3:.*]] = obelisk_sim.ref.load {{.*}} : {{.*}} -> !obelisk_sim.process
// CHECK: obelisk_sim.process.control resume %[[PROCESS3]] to ^[[RESUME:bb[0-9]+]]
// CHECK: ^[[RESUME]]:
// CHECK: %[[PROCESS4:.*]] = obelisk_sim.ref.load {{.*}} : {{.*}} -> !obelisk_sim.process
// CHECK: obelisk_sim.process.control suspend %[[PROCESS4]] to ^[[SUSPEND:bb[0-9]+]]
// CHECK: ^[[SUSPEND]]:
// CHECK: %[[PROCESS5:.*]] = obelisk_sim.ref.load {{.*}} : {{.*}} -> !obelisk_sim.process
// CHECK: obelisk_sim.process.control kill %[[PROCESS5]] to ^[[KILL:bb[0-9]+]]
// CHECK: ^[[KILL]]:
// CHECK: obelisk_sim.return
