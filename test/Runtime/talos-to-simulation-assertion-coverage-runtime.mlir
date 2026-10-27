// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' '--encode-talos-sim-to-bytecode=vpi=off' --convert-talos-sim-processes-to-llvm-coroutines | talos-translate --mlir-to-llvmir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup' | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %target_cxx %t.o %runtime_link_flags -o %t.exe
// RUN: %t.exe --execution-tier=native | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s
// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' | FileCheck %s --check-prefix=LOWER

// IEEE 1800-2023 40.2.1 and 40.2.2: assertion coverage counts, per assertion,
// whether it has succeeded. Of a passing concurrent assertion, a failing one
// without a pass action, and a passing immediate assertion, two are covered.
//
// module top;
//   bit clk, a;
//   pass_ok: assert property (@(posedge clk) 1);
//   never: assert property (@(posedge clk) a) else ;
//   initial begin
//     #1 clk = 1;
//     #1 begin
//       imm: assert (1);
//       $display("%0d %0d", $coverage_get_max(`SV_COV_ASSERTION, `SV_COV_MODULE, top),
//                $coverage_get(`SV_COV_ASSERTION, `SV_COV_MODULE, top));
//     end
//   end
// endmodule

// CHECK: 3 2

// Each concurrent assertion's pass report and the immediate assertion's pass
// branch record a success of its dense assertion point.
// LOWER: module attributes {coverage.assertion_point_count = 3 : i64
// LOWER-COUNT-3: simulation.coverage.point_hit %arg0 if %{{.*}}[{{[0-2]}}] {kind = #simulation.coverage_point_kind<assertion>}
// LOWER-NOT: simulation.coverage.point_hit

module attributes {coverage.language_version = 2023 : i32, coverage.metrics = ["assertion"], llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  talos.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  talos.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    talos.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      talos.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        talos.sv.symbol.variable @s5.clk attributes {hierarchical_name = "top.clk", lifetime = 1 : i32, name = "clk", node_id = 5 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
        }
        talos.sv.symbol.variable @s6.a attributes {hierarchical_name = "top.a", lifetime = 1 : i32, name = "a", node_id = 6 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
        }
        talos.sv.symbol.statement_block @s7.pass_ok attributes {block_kind = 0 : i32, hierarchical_name = "top.pass_ok", name = "pass_ok", node_id = 7 : i64} {
        }
        talos.sv.symbol.procedural_block @s8 attributes {hierarchical_name = "top", node_id = 8 : i64, procedure_kind = 2 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.block attributes {block_path = "top.pass_ok", block_symbol = @s1.$root::@s3.top::@s4.top::@s7.pass_ok, node_id = 9 : i64} {
            talos.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = false, has_pass_action = true, node_id = 10 : i64} {
              talos.sv.assertion.clocking attributes {node_id = 11 : i64} {
                talos.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 12 : i64} {
                  talos.sv.expression.named_value attributes {is_signed = false, node_id = 13 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
                  }
                }
                talos.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 14 : i64, repetition_is_unbounded = false} {
                  talos.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 15 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              talos.sv.statement.empty attributes {node_id = 16 : i64} {
              }
            }
          }
        }
        talos.sv.symbol.statement_block @s9.never attributes {block_kind = 0 : i32, hierarchical_name = "top.never", name = "never", node_id = 17 : i64} {
        }
        talos.sv.symbol.procedural_block @s10 attributes {hierarchical_name = "top", node_id = 18 : i64, procedure_kind = 2 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.block attributes {block_path = "top.never", block_symbol = @s1.$root::@s3.top::@s4.top::@s9.never, node_id = 19 : i64} {
            talos.sv.statement.concurrent_assertion attributes {assertion_kind = 0 : i32, has_default_disable = false, has_fail_action = true, has_pass_action = false, node_id = 20 : i64} {
              talos.sv.assertion.clocking attributes {node_id = 21 : i64} {
                talos.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 22 : i64} {
                  talos.sv.expression.named_value attributes {is_signed = false, node_id = 23 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
                  }
                }
                talos.sv.assertion.simple attributes {has_repetition = false, is_null = false, node_id = 24 : i64, repetition_is_unbounded = false} {
                  talos.sv.expression.named_value attributes {is_signed = false, node_id = 25 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.a, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
                  }
                }
              }
              talos.sv.statement.empty attributes {node_id = 26 : i64} {
              }
            }
          }
        }
        talos.sv.symbol.statement_block @s11.imm attributes {block_kind = 0 : i32, hierarchical_name = "top.imm", name = "imm", node_id = 27 : i64} {
        }
        talos.sv.symbol.procedural_block @s12 attributes {hierarchical_name = "top", node_id = 28 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.block attributes {node_id = 29 : i64} {
            talos.sv.statement.list attributes {node_id = 30 : i64} {
              talos.sv.statement.timed attributes {node_id = 31 : i64} {
                talos.sv.timing.delay attributes {node_id = 32 : i64} {
                  talos.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 33 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                talos.sv.statement.expression_statement attributes {node_id = 34 : i64} {
                  talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 35 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
                    talos.sv.expression.named_value attributes {is_signed = false, node_id = 36 : i64, referenced_path = "top.clk", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.clk, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
                    }
                    talos.sv.expression.conversion attributes {folded_constant = "1'b1", is_implicit = true, is_signed = false, node_id = 37 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
                      talos.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 38 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                  }
                }
              }
              talos.sv.statement.timed attributes {node_id = 39 : i64} {
                talos.sv.timing.delay attributes {node_id = 40 : i64} {
                  talos.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 41 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                talos.sv.statement.block attributes {node_id = 42 : i64} {
                  talos.sv.statement.list attributes {node_id = 43 : i64} {
                    talos.sv.statement.block attributes {block_path = "top.imm", block_symbol = @s1.$root::@s3.top::@s4.top::@s11.imm, node_id = 44 : i64} {
                      talos.sv.statement.immediate_assertion attributes {assertion_kind = 0 : i32, has_fail_action = false, has_pass_action = true, is_deferred = false, is_final = false, node_id = 45 : i64} {
                        talos.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 46 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                        }
                        talos.sv.statement.empty attributes {node_id = 47 : i64} {
                        }
                      }
                    }
                    talos.sv.statement.expression_statement attributes {node_id = 48 : i64} {
                      talos.sv.expression.call attributes {argument_count = 3 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 49 : i64, semantic_type = !talos.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                        talos.sv.expression.string_literal attributes {constant_value = "%0d %0d", is_signed = false, node_id = 50 : i64, semantic_type = !talos.ranged_packed_array<55 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
                        }
                        talos.sv.expression.call attributes {argument_count = 3 : i64, callee_name = "$coverage_get_max", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 51 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                          talos.sv.expression.integer_literal attributes {constant_value = "20", is_declared_unsized = true, is_signed = true, macro_expansion_stack = [{invocation = !talos.source_range<"../../.claude/jobs/2a837eb9/tmp/rt111.sv", 9, 45, "../../.claude/jobs/2a837eb9/tmp/rt111.sv", 9, 62, "">, name = "SV_COV_ASSERTION"}], macro_name = "SV_COV_ASSERTION", node_id = 52 : i64, semantic_type = !talos.ranged_packed_array<31 : 0 x !talos.integral<1, true, false, 0 : 0, bit>>} {
                          }
                          talos.sv.expression.integer_literal attributes {constant_value = "10", is_declared_unsized = true, is_signed = true, macro_expansion_stack = [{invocation = !talos.source_range<"../../.claude/jobs/2a837eb9/tmp/rt111.sv", 9, 64, "../../.claude/jobs/2a837eb9/tmp/rt111.sv", 9, 78, "">, name = "SV_COV_MODULE"}], macro_name = "SV_COV_MODULE", node_id = 53 : i64, semantic_type = !talos.ranged_packed_array<31 : 0 x !talos.integral<1, true, false, 0 : 0, bit>>} {
                          }
                          talos.sv.expression.arbitrary_symbol attributes {is_signed = false, node_id = 54 : i64, referenced_path = "top", referenced_symbol = @s1.$root::@s3.top, semantic_type = !talos.void} {
                          }
                        }
                        talos.sv.expression.call attributes {argument_count = 3 : i64, callee_name = "$coverage_get", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 55 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                          talos.sv.expression.integer_literal attributes {constant_value = "20", is_declared_unsized = true, is_signed = true, macro_expansion_stack = [{invocation = !talos.source_range<"../../.claude/jobs/2a837eb9/tmp/rt111.sv", 10, 30, "../../.claude/jobs/2a837eb9/tmp/rt111.sv", 10, 47, "">, name = "SV_COV_ASSERTION"}], macro_name = "SV_COV_ASSERTION", node_id = 56 : i64, semantic_type = !talos.ranged_packed_array<31 : 0 x !talos.integral<1, true, false, 0 : 0, bit>>} {
                          }
                          talos.sv.expression.integer_literal attributes {constant_value = "10", is_declared_unsized = true, is_signed = true, macro_expansion_stack = [{invocation = !talos.source_range<"../../.claude/jobs/2a837eb9/tmp/rt111.sv", 10, 49, "../../.claude/jobs/2a837eb9/tmp/rt111.sv", 10, 63, "">, name = "SV_COV_MODULE"}], macro_name = "SV_COV_MODULE", node_id = 57 : i64, semantic_type = !talos.ranged_packed_array<31 : 0 x !talos.integral<1, true, false, 0 : 0, bit>>} {
                          }
                          talos.sv.expression.arbitrary_symbol attributes {is_signed = false, node_id = 58 : i64, referenced_path = "top", referenced_symbol = @s1.$root::@s3.top, semantic_type = !talos.void} {
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
  }
}
