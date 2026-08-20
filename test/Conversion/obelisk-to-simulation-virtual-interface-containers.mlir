// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' --encode-obelisk-sim-to-bytecode='vpi=off' | FileCheck %s --check-prefix=BYTECODE
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.definition attributes {definition_kind = 1 : i32, hierarchical_name = "bus_if", name = "bus_if", node_id = 0 : i64, sym_name = "s0.bus_if"} {
  }
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 1 : i64, sym_name = "s1.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 2 : i64, sym_name = "s2.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 3 : i64, sym_name = "s3"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 4 : i64, referenced_path = "top", referenced_symbol = @s1.top, sym_name = "s4.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 5 : i64, sym_name = "s5.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.instance attributes {hierarchical_name = "top.a", is_uninstantiated = false, name = "a", node_id = 6 : i64, referenced_path = "bus_if", referenced_symbol = @s0.bus_if, sym_name = "s6.a"} {
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.a", name = "bus_if", node_id = 7 : i64, sym_name = "s7.bus_if", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, virtual_interface_identity = @s2.$root::@s5.top::@s6.a} {
            obelisk.sv.symbol.variable attributes {hierarchical_name = "top.a.x", lifetime = 1 : i32, name = "x", node_id = 8 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s8.x"} {
            }
          }
        }
        obelisk.sv.symbol.instance attributes {hierarchical_name = "top.b", is_uninstantiated = false, name = "b", node_id = 9 : i64, referenced_path = "bus_if", referenced_symbol = @s0.bus_if, sym_name = "s9.b"} {
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top.b", name = "bus_if", node_id = 10 : i64, sym_name = "s10.bus_if", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, virtual_interface_identity = @s2.$root::@s5.top::@s6.a} {
            obelisk.sv.symbol.variable attributes {hierarchical_name = "top.b.x", lifetime = 1 : i32, name = "x", node_id = 11 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s11.x"} {
            }
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.fixed", lifetime = 1 : i32, name = "fixed", node_id = 12 : i64, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">>, sym_name = "s12.fixed"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.dyn", lifetime = 1 : i32, name = "dyn", node_id = 13 : i64, semantic_type = !obelisk.dynarray<!obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">>, sym_name = "s13.dyn"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.queue", lifetime = 1 : i32, name = "queue", node_id = 14 : i64, semantic_type = !obelisk.queue<!obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">, 0>, sym_name = "s14.queue"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.assoc", lifetime = 1 : i32, name = "assoc", node_id = 15 : i64, semantic_type = !obelisk.assoc<!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">, false>, sym_name = "s15.assoc"} {
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "top.exchange", name = "exchange", node_id = 16 : i64, semantic_type = !obelisk.subroutine<(!obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">, !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">, !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">, !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">) -> (), true>, subroutine_kind = 1 : i32, sym_name = "s16.exchange", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 17 : i64} {
            obelisk.sv.statement.expression_statement attributes {node_id = 18 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 19 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 20 : i64, referenced_path = "top.exchange.o", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s16.exchange::@s18.o, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">} {
                }
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 21 : i64, referenced_path = "top.exchange.i", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s16.exchange::@s17.i, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 22 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 23 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 24 : i64, referenced_path = "top.exchange.io", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s16.exchange::@s19.io, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">} {
                }
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 25 : i64, referenced_path = "top.exchange.r", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s16.exchange::@s20.r, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">} {
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 26 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 27 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 28 : i64, referenced_path = "top.exchange.r", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s16.exchange::@s20.r, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">} {
                }
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 29 : i64, referenced_path = "top.exchange.i", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s16.exchange::@s17.i, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">} {
                }
              }
            }
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "top.exchange.i", name = "i", node_id = 30 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">, sym_name = "s17.i"} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 1 : i32, hierarchical_name = "top.exchange.o", name = "o", node_id = 31 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">, sym_name = "s18.o"} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 2 : i32, hierarchical_name = "top.exchange.io", name = "io", node_id = 32 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">, sym_name = "s19.io"} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 3 : i32, hierarchical_name = "top.exchange.r", name = "r", node_id = 33 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">, sym_name = "s20.r"} {
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 34 : i64, procedure_kind = 0 : i32, sym_name = "s21", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 35 : i64} {
            obelisk.sv.statement.list attributes {node_id = 36 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 37 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 38 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">} {
                  obelisk.sv.expression.element_select attributes {is_signed = false, node_id = 39 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 40 : i64, referenced_path = "top.fixed", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s12.fixed, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 41 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.expression.arbitrary_symbol attributes {is_signed = false, node_id = 42 : i64, referenced_path = "top.a", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.a, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 43 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 44 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">} {
                  obelisk.sv.expression.element_select attributes {is_signed = false, node_id = 45 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 46 : i64, referenced_path = "top.fixed", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s12.fixed, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 47 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.expression.arbitrary_symbol attributes {is_signed = false, node_id = 48 : i64, referenced_path = "top.b", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s9.b, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 49 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 50 : i64, semantic_type = !obelisk.dynarray<!obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 51 : i64, referenced_path = "top.dyn", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s13.dyn, semantic_type = !obelisk.dynarray<!obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">>} {
                  }
                  obelisk.sv.expression.new_array attributes {is_signed = false, node_id = 52 : i64, semantic_type = !obelisk.dynarray<!obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "2", is_declared_unsized = true, is_signed = true, node_id = 53 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 54 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 55 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">} {
                  obelisk.sv.expression.element_select attributes {is_signed = false, node_id = 56 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 57 : i64, referenced_path = "top.dyn", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s13.dyn, semantic_type = !obelisk.dynarray<!obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 58 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.expression.element_select attributes {is_signed = false, node_id = 59 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 60 : i64, referenced_path = "top.fixed", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s12.fixed, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 61 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 62 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "push_back", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 63 : i64, semantic_type = !obelisk.void, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s2.$root::@s4.top::@s5.top} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 64 : i64, referenced_path = "top.queue", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s14.queue, semantic_type = !obelisk.queue<!obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">, 0>} {
                  }
                  obelisk.sv.expression.arbitrary_symbol attributes {is_signed = false, node_id = 65 : i64, referenced_path = "top.a", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s6.a, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 66 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 67 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">} {
                  obelisk.sv.expression.element_select attributes {is_signed = false, node_id = 68 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 69 : i64, referenced_path = "top.assoc", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s15.assoc, semantic_type = !obelisk.assoc<!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">, false>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "3", is_declared_unsized = true, is_signed = true, node_id = 70 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.expression.arbitrary_symbol attributes {is_signed = false, node_id = 71 : i64, referenced_path = "top.b", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s9.b, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 72 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 4 : i64, callee_name = "exchange", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = false, node_id = 73 : i64, referenced_path = "top.exchange", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s16.exchange, semantic_type = !obelisk.void, subroutine_kind = 1 : i32} {
                  obelisk.sv.expression.element_select attributes {is_signed = false, node_id = 74 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 75 : i64, referenced_path = "top.fixed", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s12.fixed, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 76 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 77 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">} {
                    obelisk.sv.expression.element_select attributes {is_signed = false, node_id = 78 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 79 : i64, referenced_path = "top.fixed", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s12.fixed, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">>} {
                      }
                      obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 80 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                    obelisk.sv.expression.empty_argument attributes {is_signed = false, node_id = 81 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">} {
                    }
                  }
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 82 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">} {
                    obelisk.sv.expression.element_select attributes {is_signed = false, node_id = 83 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 84 : i64, referenced_path = "top.dyn", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s13.dyn, semantic_type = !obelisk.dynarray<!obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">>} {
                      }
                      obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 85 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                    obelisk.sv.expression.empty_argument attributes {is_signed = false, node_id = 86 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">} {
                    }
                  }
                  obelisk.sv.expression.element_select attributes {is_signed = false, node_id = 87 : i64, semantic_type = !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 88 : i64, referenced_path = "top.assoc", referenced_symbol = @s2.$root::@s4.top::@s5.top::@s15.assoc, semantic_type = !obelisk.assoc<!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.virtual_interface<@s2.$root::@s5.top::@s6.a, "">, false>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "3", is_declared_unsized = true, is_signed = true, node_id = 89 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
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

// CHECK-DAG: !obelisk_sim.dynamic_array<!obelisk_sim.virtual_interface
// CHECK-DAG: !obelisk_sim.queue<!obelisk_sim.virtual_interface
// CHECK-DAG: !obelisk_sim.assoc_array<i32, !obelisk_sim.virtual_interface
// CHECK-DAG: !obelisk_sim.unpacked_array<0 : 1 x !obelisk_sim.virtual_interface
// CHECK-LABEL: obelisk_sim.func private @unit_0(
// Input is a value, output is default-initialized and copied out, inout uses
// an isolated copy-in/out temporary, and ref remains a live argument reference
// to the selected container element.
// CHECK-SAME: %arg1: !obelisk_sim.virtual_interface
// CHECK-SAME: %arg3: !obelisk_sim.ref<!obelisk_sim.virtual_interface
// CHECK-SAME: %arg5: !obelisk_sim.ref<!obelisk_sim.virtual_interface
// CHECK-SAME: %arg6: !obelisk_sim.argument_ref<!obelisk_sim.virtual_interface
// CHECK: [[REF:%.*]] = obelisk_sim.argument_ref.load %arg6
// CHECK: obelisk_sim.argument_ref.store %arg1 to %arg6
// CHECK: obelisk_sim.ref.store %arg1 to %arg3
// CHECK: obelisk_sim.ref.store [[REF]] to %arg5
// CHECK-LABEL: obelisk_sim.func private @unit_1(
// Distinct elaborated instances and the output formal's null default must not
// be commoned merely because bind/null are pure handle constants.
// CHECK: obelisk_sim.virtual_interface.null
// CHECK: obelisk_sim.virtual_interface.bind 2
// CHECK: obelisk_sim.virtual_interface.bind 3
// CHECK: obelisk_sim.container.create {{.*}}bit_width = 64 : i64{{.*}}element_kind = 1 : i32
// CHECK: obelisk_sim.container.write
// CHECK: obelisk_sim.assoc.write
// CHECK: obelisk_sim.reference_path.index
// CHECK: obelisk_sim.reference_path.assoc
// CHECK: [[OUTNULL:%.*]] = obelisk_sim.virtual_interface.null
// CHECK: obelisk_sim.task.call @unit_0({{.*}}, [[OUTNULL]], {{.*}})
// CHECK: obelisk_sim.argument_ref.store
// CHECK-NOT: obelisk.sv.
// BYTECODE: obelisk.bytecode.image = array<i8:
// BYTECODE: obelisk.execution.state_bits = 384 : i64
// NATIVE: llvm.func
// NATIVE-NOT: obelisk_sim.virtual_interface
