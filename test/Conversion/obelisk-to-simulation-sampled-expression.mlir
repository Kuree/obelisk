// RUN: obelisk-opt %s --lower-obelisk-to-sim=opt-level=0 -o %t.sim.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.sim.mlir
// RUN: obelisk-opt %t.sim.mlir '--encode-obelisk-sim-to-bytecode=vpi=off' --convert-obelisk-sim-processes-to-llvm-coroutines -o %t.native.mlir
// RUN: obelisk-opt %t.sim.mlir '--encode-obelisk-sim-to-bytecode=vpi=off require-bytecode=true' --convert-obelisk-sim-processes-to-llvm-coroutines -o %t.bytecode.mlir
// RUN: mlir-translate --mlir-to-llvmir %t.native.mlir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O0>' | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --execution-tier=native | FileCheck %s
// RUN: mlir-translate --mlir-to-llvmir %t.native.mlir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O3>' | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --execution-tier=native | FileCheck %s
// RUN: mlir-translate --mlir-to-llvmir %t.bytecode.mlir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O0>' | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s
// RUN: mlir-translate --mlir-to-llvmir %t.bytecode.mlir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O3>' | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s
// IEEE 1800-2023 16.5.1, 16.9.3: $sampled(data+1), $past(data+1),
// and $fell($past(flag)) evaluate sampled operands recursively. The clocked
// process changes data and index before evaluating these expressions, while
// the ordinary adjacent display operand must still see the current data.
// The dynamic selection uses the Preponed index, not its newly assigned 1.
// This is semantic MLIR input; no SystemVerilog frontend is needed by the test.
// PLAN: %[[DATA:.*]] = obelisk_sim.assert.sampled_read {{.*}} from %arg2
// PLAN: %[[FLAT:.*]] = obelisk_sim.packed.flatten %[[DATA]]
// PLAN: %[[SUM:.*]] = obelisk_sim.logic.binary add %[[FLAT]], {{.*}}
// PLAN: obelisk_sim.ref.load %arg2
// PLAN: %[[INDEX:.*]] = obelisk_sim.assert.sampled_read {{.*}} from %arg4
// PLAN: obelisk_sim.array.extract_dynamic %[[DATA]]
// PLAN: %[[PACKED:.*]] = obelisk_sim.packed.unflatten %[[SUM]]
// PLAN: obelisk_sim.assert.sampled_history {{.*}} from %[[PACKED]]
// PLAN: %[[FLAG:.*]] = obelisk_sim.assert.sampled_read {{.*}} from %arg3
// PLAN: %[[PAST:.*]] = obelisk_sim.assert.sampled_history {{.*}} from %[[FLAG]]
// PLAN: obelisk_sim.assert.sampled_history {{.*}} from %[[PAST]]
// CHECK: sample=4 live=5 pick=1 past=x nested=0
// CHECK-NEXT: sample=6 live=7 pick=1 past=4 nested=0
// CHECK-NEXT: sample=8 live=9 pick=1 past=6 nested=1
// CHECK-NEXT: sample=a live=b pick=1 past=8 nested=0

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu", obelisk.coverage.language_version = 2023 : i32} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "sampled_expression", name = "sampled_expression", node_id = 0 : i64, sym_name = "s0.sampled_expression"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, obelisk_sim.vpi_definition_name = "$unit", sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "sampled_expression", is_uninstantiated = false, name = "sampled_expression", node_id = 3 : i64, referenced_path = "sampled_expression", referenced_symbol = @s0.sampled_expression, sym_name = "s3.sampled_expression"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "sampled_expression", name = "sampled_expression", node_id = 4 : i64, obelisk_sim.vpi_automatic = false, obelisk_sim.vpi_cell_instance = false, obelisk_sim.vpi_definition_name = "sampled_expression", obelisk_sim.vpi_top = true, sym_name = "s4.sampled_expression", time_precision_fs = 1000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "sampled_expression.clk", lifetime = 1 : i32, name = "clk", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.clk"} {
          obelisk.sv.expression.conversion attributes {folded_constant = "1'b0", is_implicit = true, is_signed = false, node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 2, 15, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 2, 16, "">} {
            obelisk.sv.expression.conversion attributes {folded_constant = "0", is_implicit = true, is_signed = true, node_id = 7 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 2, 15, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 2, 16, "">} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 8 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 2, 15, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 2, 16, "">} {
              }
            }
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "sampled_expression.data", lifetime = 1 : i32, name = "data", node_id = 9 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s6.data"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "sampled_expression.flag", lifetime = 1 : i32, name = "flag", node_id = 10 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s7.flag"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "sampled_expression.index", lifetime = 1 : i32, name = "index", node_id = 11 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s8.index"} {
          obelisk.sv.expression.conversion attributes {folded_constant = "2'b0", is_implicit = true, is_signed = false, node_id = 12 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 5, 23, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 5, 24, "">} {
            obelisk.sv.expression.conversion attributes {folded_constant = "0", is_implicit = true, is_signed = true, node_id = 13 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 5, 23, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 5, 24, "">} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 14 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 5, 23, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 5, 24, "">} {
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "sampled_expression", node_id = 15 : i64, procedure_kind = 2 : i32, sym_name = "s9", time_precision_fs = 1000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.timed attributes {node_id = 16 : i64, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 6, 10, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 6, 24, "">} {
            obelisk.sv.timing.delay attributes {node_id = 17 : i64, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 6, 10, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 6, 12, "">} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 18 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 6, 11, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 6, 12, "">} {
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 19 : i64, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 6, 13, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 6, 24, "">} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 20 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 6, 13, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 6, 23, "">} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 21 : i64, referenced_path = "sampled_expression.clk", referenced_symbol = @s1.$root::@s3.sampled_expression::@s4.sampled_expression::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 6, 13, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 6, 16, "">} {
                }
                obelisk.sv.expression.unary_op attributes {is_signed = false, node_id = 22 : i64, operator_kind = 2 : i32, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 6, 19, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 6, 23, "">} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 23 : i64, referenced_path = "sampled_expression.clk", referenced_symbol = @s1.$root::@s3.sampled_expression::@s4.sampled_expression::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 6, 20, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 6, 23, "">} {
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "sampled_expression", node_id = 24 : i64, procedure_kind = 2 : i32, sym_name = "s10", time_precision_fs = 1000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.timed attributes {node_id = 25 : i64, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 7, 10, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 13, 6, "">} {
            obelisk.sv.timing.signal_event attributes {edge_kind = 2 : i32, has_iff = false, node_id = 26 : i64, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 7, 12, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 7, 23, "">} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 27 : i64, referenced_path = "sampled_expression.clk", referenced_symbol = @s1.$root::@s3.sampled_expression::@s4.sampled_expression::@s5.clk, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 7, 20, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 7, 23, "">} {
              }
            }
            obelisk.sv.statement.block attributes {node_id = 28 : i64, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 7, 25, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 13, 6, "">} {
              obelisk.sv.statement.list attributes {node_id = 29 : i64, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 7, 25, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 13, 6, "">} {
                obelisk.sv.statement.expression_statement attributes {node_id = 30 : i64, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 8, 5, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 8, 21, "">} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 31 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 8, 5, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 8, 20, "">} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 32 : i64, referenced_path = "sampled_expression.data", referenced_symbol = @s1.$root::@s3.sampled_expression::@s4.sampled_expression::@s6.data, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 8, 5, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 8, 9, "">} {
                    }
                    obelisk.sv.expression.conversion attributes {is_implicit = true, is_signed = false, node_id = 33 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 8, 12, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 8, 20, "">} {
                      obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 34 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 8, 12, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 8, 20, "">} {
                        obelisk.sv.expression.conversion attributes {is_implicit = true, is_signed = false, node_id = 35 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 8, 12, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 8, 16, "">} {
                          obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 36 : i64, referenced_path = "sampled_expression.data", referenced_symbol = @s1.$root::@s3.sampled_expression::@s4.sampled_expression::@s6.data, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 8, 12, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 8, 16, "">} {
                          }
                        }
                        obelisk.sv.expression.conversion attributes {folded_constant = "32'd2", is_implicit = true, is_signed = false, node_id = 37 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 8, 19, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 8, 20, "">} {
                          obelisk.sv.expression.integer_literal attributes {constant_value = "2", is_declared_unsized = true, is_signed = true, node_id = 38 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 8, 19, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 8, 20, "">} {
                          }
                        }
                      }
                    }
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 39 : i64, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 9, 5, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 9, 15, "">} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 40 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 9, 5, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 9, 14, "">} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 41 : i64, referenced_path = "sampled_expression.index", referenced_symbol = @s1.$root::@s3.sampled_expression::@s4.sampled_expression::@s8.index, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 9, 5, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 9, 10, "">} {
                    }
                    obelisk.sv.expression.conversion attributes {folded_constant = "2'b1", is_implicit = true, is_signed = false, node_id = 42 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 9, 13, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 9, 14, "">} {
                      obelisk.sv.expression.conversion attributes {folded_constant = "1", is_implicit = true, is_signed = true, node_id = 43 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 9, 13, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 9, 14, "">} {
                        obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 44 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 9, 13, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 9, 14, "">} {
                        }
                      }
                    }
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 45 : i64, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 10, 5, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 12, 54, "">} {
                  obelisk.sv.expression.call attributes {argument_count = 6 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0, 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 46 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.sampled_expression", system_scope_path = "sampled_expression", system_scope_symbol = @s1.$root::@s3.sampled_expression::@s4.sampled_expression} {
                    obelisk.sv.expression.string_literal attributes {constant_value = "sample=%h live=%h pick=%b past=%h nested=%b", is_signed = false, node_id = 47 : i64, semantic_type = !obelisk.ranged_packed_array<343 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 10, 14, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 10, 59, "">} {
                    }
                    obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$sampled", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 48 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, subroutine_kind = 0 : i32, system_library_cell = "work.sampled_expression", system_scope_path = "sampled_expression", system_scope_symbol = @s1.$root::@s3.sampled_expression::@s4.sampled_expression} {
                      obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 49 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 11, 23, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 11, 34, "">} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 50 : i64, referenced_path = "sampled_expression.data", referenced_symbol = @s1.$root::@s3.sampled_expression::@s4.sampled_expression::@s6.data, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 11, 23, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 11, 27, "">} {
                        }
                        obelisk.sv.expression.conversion attributes {folded_constant = "4'b1", is_implicit = true, is_signed = false, node_id = 51 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 11, 30, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 11, 34, "">} {
                          obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", is_signed = false, node_id = 52 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 11, 30, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 11, 34, "">} {
                          }
                        }
                      }
                    }
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 53 : i64, referenced_path = "sampled_expression.data", referenced_symbol = @s1.$root::@s3.sampled_expression::@s4.sampled_expression::@s6.data, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 11, 37, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 11, 41, "">} {
                    }
                    obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$sampled", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 54 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, subroutine_kind = 0 : i32, system_library_cell = "work.sampled_expression", system_scope_path = "sampled_expression", system_scope_symbol = @s1.$root::@s3.sampled_expression::@s4.sampled_expression} {
                      obelisk.sv.expression.element_select attributes {is_signed = false, node_id = 55 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 11, 52, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 11, 63, "">} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 56 : i64, referenced_path = "sampled_expression.data", referenced_symbol = @s1.$root::@s3.sampled_expression::@s4.sampled_expression::@s6.data, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 11, 52, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 11, 56, "">} {
                        }
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 57 : i64, referenced_path = "sampled_expression.index", referenced_symbol = @s1.$root::@s3.sampled_expression::@s4.sampled_expression::@s8.index, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 11, 57, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 11, 62, "">} {
                        }
                      }
                    }
                    obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$past", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 58 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, subroutine_kind = 0 : i32, system_library_cell = "work.sampled_expression", system_scope_path = "sampled_expression", system_scope_symbol = @s1.$root::@s3.sampled_expression::@s4.sampled_expression} {
                      obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 59 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 12, 20, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 12, 31, "">} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 60 : i64, referenced_path = "sampled_expression.data", referenced_symbol = @s1.$root::@s3.sampled_expression::@s4.sampled_expression::@s6.data, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 12, 20, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 12, 24, "">} {
                        }
                        obelisk.sv.expression.conversion attributes {folded_constant = "4'b1", is_implicit = true, is_signed = false, node_id = 61 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 12, 27, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 12, 31, "">} {
                          obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", is_signed = false, node_id = 62 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 12, 27, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 12, 31, "">} {
                          }
                        }
                      }
                    }
                    obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$fell", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 63 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, subroutine_kind = 0 : i32, system_library_cell = "work.sampled_expression", system_scope_path = "sampled_expression", system_scope_symbol = @s1.$root::@s3.sampled_expression::@s4.sampled_expression} {
                      obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$past", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 64 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, subroutine_kind = 0 : i32, system_library_cell = "work.sampled_expression", system_scope_path = "sampled_expression", system_scope_symbol = @s1.$root::@s3.sampled_expression::@s4.sampled_expression} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 65 : i64, referenced_path = "sampled_expression.flag", referenced_symbol = @s1.$root::@s3.sampled_expression::@s4.sampled_expression::@s7.flag, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 12, 46, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 12, 50, "">} {
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "sampled_expression", node_id = 66 : i64, procedure_kind = 0 : i32, sym_name = "s11", time_precision_fs = 1000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 67 : i64, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 14, 11, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 20, 6, "">} {
            obelisk.sv.statement.list attributes {node_id = 68 : i64, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 14, 11, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 20, 6, "">} {
              obelisk.sv.statement.timed attributes {node_id = 69 : i64, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 15, 5, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 15, 17, "">} {
                obelisk.sv.timing.delay attributes {node_id = 70 : i64, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 15, 5, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 15, 7, "">} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 71 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 15, 6, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 15, 7, "">} {
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 72 : i64, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 15, 8, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 15, 17, "">} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 73 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 15, 8, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 15, 16, "">} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 74 : i64, referenced_path = "sampled_expression.data", referenced_symbol = @s1.$root::@s3.sampled_expression::@s4.sampled_expression::@s6.data, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 15, 8, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 15, 12, "">} {
                    }
                    obelisk.sv.expression.conversion attributes {folded_constant = "4'b11", is_implicit = true, is_signed = false, node_id = 75 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 15, 15, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 15, 16, "">} {
                      obelisk.sv.expression.conversion attributes {folded_constant = "3", is_implicit = true, is_signed = true, node_id = 76 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 15, 15, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 15, 16, "">} {
                        obelisk.sv.expression.integer_literal attributes {constant_value = "3", is_declared_unsized = true, is_signed = true, node_id = 77 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 15, 15, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 15, 16, "">} {
                        }
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 78 : i64, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 15, 18, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 15, 27, "">} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 79 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 15, 18, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 15, 26, "">} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 80 : i64, referenced_path = "sampled_expression.flag", referenced_symbol = @s1.$root::@s3.sampled_expression::@s4.sampled_expression::@s7.flag, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 15, 18, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 15, 22, "">} {
                  }
                  obelisk.sv.expression.conversion attributes {folded_constant = "1'b1", is_implicit = true, is_signed = false, node_id = 81 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 15, 25, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 15, 26, "">} {
                    obelisk.sv.expression.conversion attributes {folded_constant = "1", is_implicit = true, is_signed = true, node_id = 82 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 15, 25, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 15, 26, "">} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 83 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 15, 25, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 15, 26, "">} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 84 : i64, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 15, 28, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 15, 38, "">} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 85 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 15, 28, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 15, 37, "">} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 86 : i64, referenced_path = "sampled_expression.index", referenced_symbol = @s1.$root::@s3.sampled_expression::@s4.sampled_expression::@s8.index, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 15, 28, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 15, 33, "">} {
                  }
                  obelisk.sv.expression.conversion attributes {folded_constant = "2'b0", is_implicit = true, is_signed = false, node_id = 87 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 15, 36, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 15, 37, "">} {
                    obelisk.sv.expression.conversion attributes {folded_constant = "0", is_implicit = true, is_signed = true, node_id = 88 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 15, 36, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 15, 37, "">} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 89 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 15, 36, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 15, 37, "">} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.timed attributes {node_id = 90 : i64, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 16, 5, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 16, 17, "">} {
                obelisk.sv.timing.delay attributes {node_id = 91 : i64, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 16, 5, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 16, 7, "">} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "2", is_declared_unsized = true, is_signed = true, node_id = 92 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 16, 6, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 16, 7, "">} {
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 93 : i64, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 16, 8, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 16, 17, "">} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 94 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 16, 8, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 16, 16, "">} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 95 : i64, referenced_path = "sampled_expression.data", referenced_symbol = @s1.$root::@s3.sampled_expression::@s4.sampled_expression::@s6.data, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 16, 8, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 16, 12, "">} {
                    }
                    obelisk.sv.expression.conversion attributes {folded_constant = "4'b101", is_implicit = true, is_signed = false, node_id = 96 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 16, 15, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 16, 16, "">} {
                      obelisk.sv.expression.conversion attributes {folded_constant = "5", is_implicit = true, is_signed = true, node_id = 97 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 16, 15, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 16, 16, "">} {
                        obelisk.sv.expression.integer_literal attributes {constant_value = "5", is_declared_unsized = true, is_signed = true, node_id = 98 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 16, 15, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 16, 16, "">} {
                        }
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 99 : i64, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 16, 18, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 16, 27, "">} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 100 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 16, 18, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 16, 26, "">} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 101 : i64, referenced_path = "sampled_expression.flag", referenced_symbol = @s1.$root::@s3.sampled_expression::@s4.sampled_expression::@s7.flag, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 16, 18, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 16, 22, "">} {
                  }
                  obelisk.sv.expression.conversion attributes {folded_constant = "1'b0", is_implicit = true, is_signed = false, node_id = 102 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 16, 25, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 16, 26, "">} {
                    obelisk.sv.expression.conversion attributes {folded_constant = "0", is_implicit = true, is_signed = true, node_id = 103 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 16, 25, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 16, 26, "">} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 104 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 16, 25, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 16, 26, "">} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 105 : i64, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 16, 28, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 16, 38, "">} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 106 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 16, 28, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 16, 37, "">} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 107 : i64, referenced_path = "sampled_expression.index", referenced_symbol = @s1.$root::@s3.sampled_expression::@s4.sampled_expression::@s8.index, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 16, 28, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 16, 33, "">} {
                  }
                  obelisk.sv.expression.conversion attributes {folded_constant = "2'b0", is_implicit = true, is_signed = false, node_id = 108 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 16, 36, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 16, 37, "">} {
                    obelisk.sv.expression.conversion attributes {folded_constant = "0", is_implicit = true, is_signed = true, node_id = 109 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 16, 36, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 16, 37, "">} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 110 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 16, 36, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 16, 37, "">} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.timed attributes {node_id = 111 : i64, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 17, 5, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 17, 17, "">} {
                obelisk.sv.timing.delay attributes {node_id = 112 : i64, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 17, 5, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 17, 7, "">} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "2", is_declared_unsized = true, is_signed = true, node_id = 113 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 17, 6, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 17, 7, "">} {
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 114 : i64, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 17, 8, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 17, 17, "">} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 115 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 17, 8, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 17, 16, "">} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 116 : i64, referenced_path = "sampled_expression.data", referenced_symbol = @s1.$root::@s3.sampled_expression::@s4.sampled_expression::@s6.data, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 17, 8, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 17, 12, "">} {
                    }
                    obelisk.sv.expression.conversion attributes {folded_constant = "4'b111", is_implicit = true, is_signed = false, node_id = 117 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 17, 15, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 17, 16, "">} {
                      obelisk.sv.expression.conversion attributes {folded_constant = "7", is_implicit = true, is_signed = true, node_id = 118 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 17, 15, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 17, 16, "">} {
                        obelisk.sv.expression.integer_literal attributes {constant_value = "7", is_declared_unsized = true, is_signed = true, node_id = 119 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 17, 15, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 17, 16, "">} {
                        }
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 120 : i64, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 17, 18, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 17, 27, "">} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 121 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 17, 18, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 17, 26, "">} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 122 : i64, referenced_path = "sampled_expression.flag", referenced_symbol = @s1.$root::@s3.sampled_expression::@s4.sampled_expression::@s7.flag, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 17, 18, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 17, 22, "">} {
                  }
                  obelisk.sv.expression.conversion attributes {folded_constant = "1'b1", is_implicit = true, is_signed = false, node_id = 123 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 17, 25, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 17, 26, "">} {
                    obelisk.sv.expression.conversion attributes {folded_constant = "1", is_implicit = true, is_signed = true, node_id = 124 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 17, 25, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 17, 26, "">} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 125 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 17, 25, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 17, 26, "">} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 126 : i64, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 17, 28, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 17, 38, "">} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 127 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 17, 28, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 17, 37, "">} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 128 : i64, referenced_path = "sampled_expression.index", referenced_symbol = @s1.$root::@s3.sampled_expression::@s4.sampled_expression::@s8.index, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 17, 28, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 17, 33, "">} {
                  }
                  obelisk.sv.expression.conversion attributes {folded_constant = "2'b0", is_implicit = true, is_signed = false, node_id = 129 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 17, 36, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 17, 37, "">} {
                    obelisk.sv.expression.conversion attributes {folded_constant = "0", is_implicit = true, is_signed = true, node_id = 130 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 17, 36, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 17, 37, "">} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 131 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 17, 36, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 17, 37, "">} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.timed attributes {node_id = 132 : i64, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 18, 5, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 18, 17, "">} {
                obelisk.sv.timing.delay attributes {node_id = 133 : i64, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 18, 5, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 18, 7, "">} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "2", is_declared_unsized = true, is_signed = true, node_id = 134 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 18, 6, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 18, 7, "">} {
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 135 : i64, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 18, 8, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 18, 17, "">} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 136 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 18, 8, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 18, 16, "">} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 137 : i64, referenced_path = "sampled_expression.data", referenced_symbol = @s1.$root::@s3.sampled_expression::@s4.sampled_expression::@s6.data, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 18, 8, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 18, 12, "">} {
                    }
                    obelisk.sv.expression.conversion attributes {folded_constant = "4'b1001", is_implicit = true, is_signed = false, node_id = 138 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 18, 15, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 18, 16, "">} {
                      obelisk.sv.expression.conversion attributes {folded_constant = "9", is_implicit = true, is_signed = true, node_id = 139 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 18, 15, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 18, 16, "">} {
                        obelisk.sv.expression.integer_literal attributes {constant_value = "9", is_declared_unsized = true, is_signed = true, node_id = 140 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 18, 15, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 18, 16, "">} {
                        }
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 141 : i64, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 18, 18, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 18, 27, "">} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 142 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 18, 18, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 18, 26, "">} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 143 : i64, referenced_path = "sampled_expression.flag", referenced_symbol = @s1.$root::@s3.sampled_expression::@s4.sampled_expression::@s7.flag, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 18, 18, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 18, 22, "">} {
                  }
                  obelisk.sv.expression.conversion attributes {folded_constant = "1'b0", is_implicit = true, is_signed = false, node_id = 144 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 18, 25, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 18, 26, "">} {
                    obelisk.sv.expression.conversion attributes {folded_constant = "0", is_implicit = true, is_signed = true, node_id = 145 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 18, 25, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 18, 26, "">} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 146 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 18, 25, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 18, 26, "">} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 147 : i64, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 18, 28, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 18, 38, "">} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 148 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 18, 28, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 18, 37, "">} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 149 : i64, referenced_path = "sampled_expression.index", referenced_symbol = @s1.$root::@s3.sampled_expression::@s4.sampled_expression::@s8.index, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 18, 28, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 18, 33, "">} {
                  }
                  obelisk.sv.expression.conversion attributes {folded_constant = "2'b0", is_implicit = true, is_signed = false, node_id = 150 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 18, 36, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 18, 37, "">} {
                    obelisk.sv.expression.conversion attributes {folded_constant = "0", is_implicit = true, is_signed = true, node_id = 151 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, true, 0 : 0, logic>>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 18, 36, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 18, 37, "">} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 152 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 18, 36, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 18, 37, "">} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.timed attributes {node_id = 153 : i64, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 19, 5, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 19, 16, "">} {
                obelisk.sv.timing.delay attributes {node_id = 154 : i64, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 19, 5, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 19, 7, "">} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "2", is_declared_unsized = true, is_signed = true, node_id = 155 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 19, 6, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 19, 7, "">} {
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 156 : i64, source_range = !obelisk.source_range<"tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 19, 8, "tmp/bench/unified-loop/cores/scr1/sampled-expression.sv", 19, 16, "">} {
                  obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "$finish", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 157 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.sampled_expression", system_scope_path = "sampled_expression", system_scope_symbol = @s1.$root::@s3.sampled_expression::@s4.sampled_expression} {
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
