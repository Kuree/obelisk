// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' '--encode-talos-sim-to-bytecode=vpi=off' --convert-talos-sim-processes-to-llvm-coroutines | talos-translate --mlir-to-llvmir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup' | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %target_cxx %t.o %runtime_link_flags -o %t.exe
// RUN: %t.exe --execution-tier=native | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s

// IEEE 1800-2023 16.4.2: only resuming from an event control or a wait
// statement flushes a process's pending deferred assertion reports, and
// 16.4.4: disabling a non-outermost scope does not. A #0 resume and an inner
// block disable keep their reports; an @(e) resume flushes its report.
//
// module top;
//   int calls[3];
//   function automatic void record(input int site);
//     calls[site]++;
//   endfunction
//   event e, f;
//   initial begin assert #0 (1) record(0); #0; end
//   initial begin assert #0 (1) record(1); @(e); end
//   initial begin : outer
//     begin : inner
//       assert #0 (1) record(2);
//       @(f);
//     end
//   end
//   initial begin
//     #0 ->e;
//     disable outer.inner;
//     #1 $display("%0d %0d %0d", calls[0], calls[1], calls[2]);
//   end
// endmodule

// CHECK: 1 0 1

module attributes {coverage.language_version = 2023 : i32, llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  talos.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  talos.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    talos.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      talos.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        talos.sv.symbol.variable @s5.calls attributes {hierarchical_name = "top.calls", lifetime = 1 : i32, name = "calls", node_id = 5 : i64, semantic_type = !talos.ranged_unpacked_array<0 : 2 x !talos.integral<32, true, false, 31 : 0, int>>} {
        }
        talos.sv.symbol.subroutine @s6.record attributes {hierarchical_name = "top.record", name = "record", node_id = 6 : i64, semantic_type = !talos.subroutine<(!talos.integral<32, true, false, 31 : 0, int>) -> !talos.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.expression_statement attributes {node_id = 7 : i64} {
            talos.sv.expression.unary_op attributes {is_signed = true, node_id = 8 : i64, operator_kind = 12 : i32, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
              talos.sv.expression.element_select attributes {is_signed = true, node_id = 9 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                talos.sv.expression.named_value attributes {is_signed = false, node_id = 10 : i64, referenced_path = "top.calls", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.calls, semantic_type = !talos.ranged_unpacked_array<0 : 2 x !talos.integral<32, true, false, 31 : 0, int>>} {
                }
                talos.sv.expression.named_value attributes {is_signed = true, node_id = 11 : i64, referenced_path = "top.record.site", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.record::@s7.site, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
          talos.sv.symbol.formal_argument @s7.site attributes {direction = 0 : i32, hierarchical_name = "top.record.site", name = "site", node_id = 12 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
          }
        }
        talos.sv.symbol.variable @s8.e attributes {hierarchical_name = "top.e", lifetime = 1 : i32, name = "e", node_id = 13 : i64, semantic_type = !talos.event} {
        }
        talos.sv.symbol.variable @s9.f attributes {hierarchical_name = "top.f", lifetime = 1 : i32, name = "f", node_id = 14 : i64, semantic_type = !talos.event} {
        }
        talos.sv.symbol.procedural_block @s10 attributes {hierarchical_name = "top", node_id = 15 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.block attributes {node_id = 16 : i64} {
            talos.sv.statement.list attributes {node_id = 17 : i64} {
              talos.sv.statement.immediate_assertion attributes {assertion_kind = 0 : i32, has_fail_action = false, has_pass_action = true, is_deferred = true, is_final = false, node_id = 18 : i64} {
                talos.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 19 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                }
                talos.sv.statement.expression_statement attributes {node_id = 20 : i64} {
                  talos.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "record", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = false, node_id = 21 : i64, referenced_path = "top.record", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.record, semantic_type = !talos.void, subroutine_kind = 0 : i32} {
                    talos.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 22 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              talos.sv.statement.timed attributes {node_id = 23 : i64} {
                talos.sv.timing.delay attributes {node_id = 24 : i64} {
                  talos.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 25 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                talos.sv.statement.empty attributes {node_id = 26 : i64} {
                }
              }
            }
          }
        }
        talos.sv.symbol.procedural_block @s11 attributes {hierarchical_name = "top", node_id = 27 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.block attributes {node_id = 28 : i64} {
            talos.sv.statement.list attributes {node_id = 29 : i64} {
              talos.sv.statement.immediate_assertion attributes {assertion_kind = 0 : i32, has_fail_action = false, has_pass_action = true, is_deferred = true, is_final = false, node_id = 30 : i64} {
                talos.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 31 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                }
                talos.sv.statement.expression_statement attributes {node_id = 32 : i64} {
                  talos.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "record", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = false, node_id = 33 : i64, referenced_path = "top.record", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.record, semantic_type = !talos.void, subroutine_kind = 0 : i32} {
                    talos.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 34 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              talos.sv.statement.timed attributes {node_id = 35 : i64} {
                talos.sv.timing.signal_event attributes {edge_kind = 0 : i32, has_iff = false, node_id = 36 : i64} {
                  talos.sv.expression.named_value attributes {is_signed = false, node_id = 37 : i64, referenced_path = "top.e", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.e, semantic_type = !talos.event} {
                  }
                }
                talos.sv.statement.empty attributes {node_id = 38 : i64} {
                }
              }
            }
          }
        }
        talos.sv.symbol.statement_block @s12.outer attributes {block_kind = 0 : i32, hierarchical_name = "top.outer", name = "outer", node_id = 39 : i64} {
          talos.sv.symbol.statement_block @s13.inner attributes {block_kind = 0 : i32, hierarchical_name = "top.outer.inner", name = "inner", node_id = 40 : i64} {
          }
        }
        talos.sv.symbol.procedural_block @s14 attributes {hierarchical_name = "top", node_id = 41 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.block attributes {block_path = "top.outer", block_symbol = @s1.$root::@s3.top::@s4.top::@s12.outer, node_id = 42 : i64} {
            talos.sv.statement.block attributes {block_path = "top.outer.inner", block_symbol = @s1.$root::@s3.top::@s4.top::@s12.outer::@s13.inner, node_id = 43 : i64} {
              talos.sv.statement.list attributes {node_id = 44 : i64} {
                talos.sv.statement.immediate_assertion attributes {assertion_kind = 0 : i32, has_fail_action = false, has_pass_action = true, is_deferred = true, is_final = false, node_id = 45 : i64} {
                  talos.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 46 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                  }
                  talos.sv.statement.expression_statement attributes {node_id = 47 : i64} {
                    talos.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "record", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = false, node_id = 48 : i64, referenced_path = "top.record", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.record, semantic_type = !talos.void, subroutine_kind = 0 : i32} {
                      talos.sv.expression.integer_literal attributes {constant_value = "2", is_declared_unsized = true, is_signed = true, node_id = 49 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                  }
                }
                talos.sv.statement.timed attributes {node_id = 50 : i64} {
                  talos.sv.timing.signal_event attributes {edge_kind = 0 : i32, has_iff = false, node_id = 51 : i64} {
                    talos.sv.expression.named_value attributes {is_signed = false, node_id = 52 : i64, referenced_path = "top.f", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s9.f, semantic_type = !talos.event} {
                    }
                  }
                  talos.sv.statement.empty attributes {node_id = 53 : i64} {
                  }
                }
              }
            }
          }
        }
        talos.sv.symbol.procedural_block @s15 attributes {hierarchical_name = "top", node_id = 54 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.block attributes {node_id = 55 : i64} {
            talos.sv.statement.list attributes {node_id = 56 : i64} {
              talos.sv.statement.timed attributes {node_id = 57 : i64} {
                talos.sv.timing.delay attributes {node_id = 58 : i64} {
                  talos.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 59 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                talos.sv.statement.event_trigger attributes {node_id = 60 : i64} {
                  talos.sv.expression.named_value attributes {is_signed = false, node_id = 61 : i64, referenced_path = "top.e", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.e, semantic_type = !talos.event} {
                  }
                }
              }
              talos.sv.statement.disable attributes {is_hierarchical = true, node_id = 62 : i64, target_path = "top.outer.inner", target_symbol = @s1.$root::@s3.top::@s4.top::@s12.outer::@s13.inner} {
              }
              talos.sv.statement.timed attributes {node_id = 63 : i64} {
                talos.sv.timing.delay attributes {node_id = 64 : i64} {
                  talos.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 65 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                talos.sv.statement.expression_statement attributes {node_id = 66 : i64} {
                  talos.sv.expression.call attributes {argument_count = 4 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 67 : i64, semantic_type = !talos.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                    talos.sv.expression.string_literal attributes {constant_value = "%0d %0d %0d", is_signed = false, node_id = 68 : i64, semantic_type = !talos.ranged_packed_array<87 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
                    }
                    talos.sv.expression.element_select attributes {is_signed = true, node_id = 69 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                      talos.sv.expression.named_value attributes {is_signed = false, node_id = 70 : i64, referenced_path = "top.calls", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.calls, semantic_type = !talos.ranged_unpacked_array<0 : 2 x !talos.integral<32, true, false, 31 : 0, int>>} {
                      }
                      talos.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 71 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                    talos.sv.expression.element_select attributes {is_signed = true, node_id = 72 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                      talos.sv.expression.named_value attributes {is_signed = false, node_id = 73 : i64, referenced_path = "top.calls", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.calls, semantic_type = !talos.ranged_unpacked_array<0 : 2 x !talos.integral<32, true, false, 31 : 0, int>>} {
                      }
                      talos.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 74 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                    talos.sv.expression.element_select attributes {is_signed = true, node_id = 75 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                      talos.sv.expression.named_value attributes {is_signed = false, node_id = 76 : i64, referenced_path = "top.calls", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.calls, semantic_type = !talos.ranged_unpacked_array<0 : 2 x !talos.integral<32, true, false, 31 : 0, int>>} {
                      }
                      talos.sv.expression.integer_literal attributes {constant_value = "2", is_declared_unsized = true, is_signed = true, node_id = 77 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
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
