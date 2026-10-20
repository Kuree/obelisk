// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' '--encode-obelisk-sim-to-bytecode=vpi=off' --convert-obelisk-sim-processes-to-llvm-coroutines | obelisk-translate --mlir-to-llvmir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup' | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %target_cxx %t.o %runtime_link_flags -o %t.exe
// RUN: %t.exe --execution-tier=native | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s

// IEEE 1800-2023 6.6.4.1: triregs that a conducting pass switch joins form a
// capacitive network, and their stored charges are shared. Two equal medium
// charges of opposite value both become x.
//
// module top;
//   trireg (medium) a, b;
//   bit en = 1, on = 0;
//   bufif1 (a, 1'b0, en);
//   bufif1 (b, 1'b1, en);
//   tranif1 (a, b, on);
//   initial begin
//     #1 en = 0;
//     #1 on = 1;
//     #1 $display("%b%b", a, b);
//   end
// endmodule

// CHECK: xx

module attributes {coverage.language_version = 2023 : i32, llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    obelisk.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        obelisk.sv.symbol.net @s5.a attributes {charge_strength = 1 : i32, hierarchical_name = "top.a", is_implicit = false, name = "a", net_kind = 9 : i32, node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.net @s6.b attributes {charge_strength = 1 : i32, hierarchical_name = "top.b", is_implicit = false, name = "b", net_kind = 9 : i32, node_id = 6 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.variable @s7.en attributes {hierarchical_name = "top.en", lifetime = 1 : i32, name = "en", node_id = 7 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
          obelisk.sv.expression.conversion attributes {folded_constant = "1'b1", is_implicit = true, is_signed = false, node_id = 8 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
            obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 9 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            }
          }
        }
        obelisk.sv.symbol.variable @s8.on attributes {hierarchical_name = "top.on", lifetime = 1 : i32, name = "on", node_id = 10 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
          obelisk.sv.expression.conversion attributes {folded_constant = "1'b0", is_implicit = true, is_signed = false, node_id = 11 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
            obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 12 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            }
          }
        }
        obelisk.sv.symbol.primitive_instance @s9 attributes {hierarchical_name = "top", node_id = 13 : i64, primitive_name = "bufif1", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 14 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 15 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
            obelisk.sv.expression.empty_argument attributes {is_signed = false, node_id = 16 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
          obelisk.sv.expression.conversion attributes {folded_constant = "1'b0", is_implicit = true, is_signed = false, node_id = 17 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            obelisk.sv.expression.integer_literal attributes {constant_value = "1'b0", is_signed = false, node_id = 18 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
            }
          }
          obelisk.sv.expression.conversion attributes {is_implicit = true, is_signed = false, node_id = 19 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 20 : i64, referenced_path = "top.en", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.en, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
            }
          }
        }
        obelisk.sv.symbol.primitive_instance @s10 attributes {hierarchical_name = "top", node_id = 21 : i64, primitive_name = "bufif1", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 22 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 23 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
            obelisk.sv.expression.empty_argument attributes {is_signed = false, node_id = 24 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
          obelisk.sv.expression.conversion attributes {folded_constant = "1'b1", is_implicit = true, is_signed = false, node_id = 25 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", is_signed = false, node_id = 26 : i64, semantic_type = !obelisk.ranged_packed_array<0 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
            }
          }
          obelisk.sv.expression.conversion attributes {is_implicit = true, is_signed = false, node_id = 27 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 28 : i64, referenced_path = "top.en", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.en, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
            }
          }
        }
        obelisk.sv.symbol.primitive_instance @s11 attributes {hierarchical_name = "top", node_id = 29 : i64, primitive_name = "tranif1", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 30 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 31 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
            obelisk.sv.expression.empty_argument attributes {is_signed = false, node_id = 32 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
          obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 33 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 34 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
            obelisk.sv.expression.empty_argument attributes {is_signed = false, node_id = 35 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            }
          }
          obelisk.sv.expression.conversion attributes {is_implicit = true, is_signed = false, node_id = 36 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 37 : i64, referenced_path = "top.on", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.on, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
            }
          }
        }
        obelisk.sv.symbol.procedural_block @s12 attributes {hierarchical_name = "top", node_id = 38 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 39 : i64} {
            obelisk.sv.statement.list attributes {node_id = 40 : i64} {
              obelisk.sv.statement.timed attributes {node_id = 41 : i64} {
                obelisk.sv.timing.delay attributes {node_id = 42 : i64} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 43 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 44 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 45 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 46 : i64, referenced_path = "top.en", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.en, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    }
                    obelisk.sv.expression.conversion attributes {folded_constant = "1'b0", is_implicit = true, is_signed = false, node_id = 47 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 48 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.timed attributes {node_id = 49 : i64} {
                obelisk.sv.timing.delay attributes {node_id = 50 : i64} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 51 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 52 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 53 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 54 : i64, referenced_path = "top.on", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.on, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    }
                    obelisk.sv.expression.conversion attributes {folded_constant = "1'b1", is_implicit = true, is_signed = false, node_id = 55 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                      obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 56 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                  }
                }
              }
              obelisk.sv.statement.timed attributes {node_id = 57 : i64} {
                obelisk.sv.timing.delay attributes {node_id = 58 : i64} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 59 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 60 : i64} {
                  obelisk.sv.expression.call attributes {argument_count = 3 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 61 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                    obelisk.sv.expression.string_literal attributes {constant_value = "%b%b", is_signed = false, node_id = 62 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    }
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 63 : i64, referenced_path = "top.a", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.a, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 64 : i64, referenced_path = "top.b", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.b, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
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

