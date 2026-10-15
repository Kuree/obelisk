// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' '--encode-obelisk-sim-to-bytecode=vpi=off' --convert-obelisk-sim-processes-to-llvm-coroutines | obelisk-translate --mlir-to-llvmir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup' | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --execution-tier=native | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s

// IEEE 1800-2023 14.16: a synchronous drive may target a clockvar of any
// assignment-compatible type. An unpacked-array and a real clockvar are
// driven in the Re-NBA region of the clocking event in both tiers.
//
// module top;
//   bit clk;
//   int words [2];
//   real r;
//   clocking cb @(posedge clk);
//     output words, r;
//   endclocking
//   initial begin
//     @(cb);
//     cb.words <= '{5, 7};
//     cb.r <= 1.5;
//     #1 $display("%0d %0d %0.1f", words[0], words[1], r);
//     $finish;
//   end
//   initial #1 clk = 1;
// endmodule

// CHECK: 5 7 1.5

module attributes {coverage.language_version = 2023 : i32, llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    obelisk.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        obelisk.sv.symbol.variable @s5.clk attributes {hierarchical_name = "top.clk", lifetime = 1 : i32, name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
        }
        obelisk.sv.symbol.variable @s6.words attributes {hierarchical_name = "top.words", lifetime = 1 : i32, name = "words", node_id = 6 : i64, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.integral<32, true, false, 31 : 0, int>>} {
        }
        obelisk.sv.symbol.variable @s7.r attributes {hierarchical_name = "top.r", lifetime = 1 : i32, name = "r", node_id = 7 : i64, semantic_type = !obelisk.real} {
        }
        obelisk.sv.symbol.clocking_block @s8.cb attributes {hierarchical_name = "top.cb", is_default = false, is_global = false, name = "cb", node_id = 8 : i64} {
          obelisk.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 9 : i64} {
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 10 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
            }
          }
          obelisk.sv.symbol.clock_var @s9.words attributes {direction = 1 : i32, has_input_delay = false, has_output_delay = false, hierarchical_name = "top.cb.words", input_edge = 0 : i32, lifetime = 1 : i32, name = "words", node_id = 11 : i64, output_edge = 0 : i32, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.integral<32, true, false, 31 : 0, int>>} {
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 12 : i64, referenced_path = "top.words", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.words, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.integral<32, true, false, 31 : 0, int>>} {
            }
          }
          obelisk.sv.symbol.clock_var @s10.r attributes {direction = 1 : i32, has_input_delay = false, has_output_delay = false, hierarchical_name = "top.cb.r", input_edge = 0 : i32, lifetime = 1 : i32, name = "r", node_id = 13 : i64, output_edge = 0 : i32, semantic_type = !obelisk.real} {
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 14 : i64, referenced_path = "top.r", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.r, semantic_type = !obelisk.real} {
            }
          }
        }
        obelisk.sv.symbol.procedural_block @s11 attributes {hierarchical_name = "top", node_id = 15 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 16 : i64} {
            obelisk.sv.statement.list attributes {node_id = 17 : i64} {
              obelisk.sv.statement.timed attributes {node_id = 18 : i64} {
                obelisk.sv.timing.signal_event attributes {edge_kind = 0 : i32, has_iff = false, node_id = 19 : i64} {
                  obelisk.sv.expression.arbitrary_symbol attributes {clocking_block_event, clocking_event_edge = 1 : i32, clocking_event_path = "top.clk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, is_signed = false, node_id = 20 : i64, referenced_path = "top.cb", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.cb, semantic_type = !obelisk.void} {
                  }
                }
                obelisk.sv.statement.empty attributes {node_id = 21 : i64} {
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 22 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 1 : i32, is_signed = false, node_id = 23 : i64, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.integral<32, true, false, 31 : 0, int>>} {
                  obelisk.sv.expression.named_value attributes {clocking_access_direction = 1 : i32, clocking_event_edge = 1 : i32, clocking_event_path = "top.clk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, clocking_input_skew_edge = 0 : i32, clocking_input_skew_one_step, clocking_output_skew_delay = "0", clocking_output_skew_edge = 0 : i32, clocking_source_path = "top.words", clocking_source_symbol = @s1.$root::@s3.top::@s4.top::@s6.words, clocking_time_precision_fs = 1000000 : i64, clocking_time_unit_fs = 1000000 : i64, clocking_variable, is_signed = false, node_id = 24 : i64, referenced_path = "top.cb.words", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.cb::@s9.words, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.integral<32, true, false, 31 : 0, int>>} {
                  }
                  obelisk.sv.expression.simple_assignment_pattern attributes {is_signed = false, node_id = 25 : i64, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.integral<32, true, false, 31 : 0, int>>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "5", is_declared_unsized = true, is_signed = true, node_id = 26 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "7", is_declared_unsized = true, is_signed = true, node_id = 27 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 28 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 1 : i32, is_signed = false, node_id = 29 : i64, semantic_type = !obelisk.real} {
                  obelisk.sv.expression.named_value attributes {clocking_access_direction = 1 : i32, clocking_event_edge = 1 : i32, clocking_event_path = "top.clk", clocking_event_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, clocking_input_skew_edge = 0 : i32, clocking_input_skew_one_step, clocking_output_skew_delay = "0", clocking_output_skew_edge = 0 : i32, clocking_source_path = "top.r", clocking_source_symbol = @s1.$root::@s3.top::@s4.top::@s7.r, clocking_time_precision_fs = 1000000 : i64, clocking_time_unit_fs = 1000000 : i64, clocking_variable, is_signed = false, node_id = 30 : i64, referenced_path = "top.cb.r", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.cb::@s10.r, semantic_type = !obelisk.real} {
                  }
                  obelisk.sv.expression.real_literal attributes {constant_value = "1.5", is_signed = false, node_id = 31 : i64, semantic_type = !obelisk.real} {
                  }
                }
              }
              obelisk.sv.statement.timed attributes {node_id = 32 : i64} {
                obelisk.sv.timing.delay attributes {node_id = 33 : i64} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 34 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 35 : i64} {
                  obelisk.sv.expression.call attributes {argument_count = 4 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 36 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                    obelisk.sv.expression.string_literal attributes {constant_value = "%0d %0d %0.1f", is_signed = false, node_id = 37 : i64, semantic_type = !obelisk.ranged_packed_array<103 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    }
                    obelisk.sv.expression.element_select attributes {is_signed = true, node_id = 38 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 39 : i64, referenced_path = "top.words", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.words, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.integral<32, true, false, 31 : 0, int>>} {
                      }
                      obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 40 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                    obelisk.sv.expression.element_select attributes {is_signed = true, node_id = 41 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 42 : i64, referenced_path = "top.words", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.words, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.integral<32, true, false, 31 : 0, int>>} {
                      }
                      obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 43 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 44 : i64, referenced_path = "top.r", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.r, semantic_type = !obelisk.real} {
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 45 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$finish", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 46 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block @s12 attributes {hierarchical_name = "top", node_id = 47 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.timed attributes {node_id = 48 : i64} {
            obelisk.sv.timing.delay attributes {node_id = 49 : i64} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 50 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 51 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 52 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 53 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                }
                obelisk.sv.expression.conversion attributes {folded_constant = "1'b1", is_implicit = true, is_signed = false, node_id = 54 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 55 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
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
