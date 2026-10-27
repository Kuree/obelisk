// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' '--encode-talos-sim-to-bytecode=vpi=off' --convert-talos-sim-processes-to-llvm-coroutines | talos-translate --mlir-to-llvmir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup' | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %target_cxx %t.o %runtime_link_flags -o %t.exe
// RUN: %t.exe --execution-tier=native | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s

// IEEE 1800-2023 10.4.2: nonblocking assignments to selections of a class
// property update only their selected bits at commit. Two bit updates in one
// slot both land, a blocking write between issue and commit survives, and an
// element of an unpacked-array property merges through its reference path.
//
// class C;
//   bit [3:0] b;
//   bit [3:0] m [2];
// endclass
// module top;
//   C c = new;
//   initial begin
//     c.b[1] <= 1'b1;
//     c.b[3] <= 1'b1;
//     c.b[0] = 1'b1;
//     c.m[1][1] <= 1'b1;
//     c.m[1][3] <= 1'b1;
//     #1 $display("%b %b", c.b, c.m[1]);
//   end
// endmodule

// CHECK: 1011 1010

module attributes {coverage.language_version = 2023 : i32, llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  talos.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  talos.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
      talos.sv.type.class_type @s3.C attributes {bitstream_width = 12 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.C>, this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s22.this} {
        talos.sv.symbol.class_property @s4.b attributes {hierarchical_name = "C::b", name = "b", node_id = 4 : i64, semantic_type = !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
        }
        talos.sv.symbol.class_property @s5.m attributes {hierarchical_name = "C::m", name = "m", node_id = 5 : i64, semantic_type = !talos.ranged_unpacked_array<0 : 1 x !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>>} {
        }
        talos.sv.symbol.variable @s22.this attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 84 : i64, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.C>} {
        }
      }
    }
    talos.sv.symbol.instance @s18.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 26 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      talos.sv.symbol.instance_body @s19.top attributes {hierarchical_name = "top", name = "top", node_id = 27 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        talos.sv.symbol.variable @s20.c attributes {hierarchical_name = "top.c", lifetime = 1 : i32, name = "c", node_id = 28 : i64, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.C>} {
          talos.sv.expression.new_class attributes {is_signed = false, is_super_class = false, node_id = 29 : i64, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        talos.sv.symbol.procedural_block @s21 attributes {hierarchical_name = "top", node_id = 30 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.block attributes {node_id = 31 : i64} {
            talos.sv.statement.list attributes {node_id = 32 : i64} {
              talos.sv.statement.expression_statement attributes {node_id = 33 : i64} {
                talos.sv.expression.assignment attributes {assignment_kind = 1 : i32, is_signed = false, node_id = 34 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
                  talos.sv.expression.element_select attributes {is_signed = false, node_id = 35 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
                    talos.sv.expression.member_access attributes {is_signed = false, member_name = "b", node_id = 36 : i64, referenced_path = "C::b", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.b, semantic_type = !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
                      talos.sv.expression.named_value attributes {is_signed = false, node_id = 37 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s18.top::@s19.top::@s20.c, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.C>} {
                      }
                    }
                    talos.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 38 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  talos.sv.expression.integer_literal attributes {constant_value = "1'b1", is_signed = false, node_id = 39 : i64, semantic_type = !talos.ranged_packed_array<0 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
                  }
                }
              }
              talos.sv.statement.expression_statement attributes {node_id = 40 : i64} {
                talos.sv.expression.assignment attributes {assignment_kind = 1 : i32, is_signed = false, node_id = 41 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
                  talos.sv.expression.element_select attributes {is_signed = false, node_id = 42 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
                    talos.sv.expression.member_access attributes {is_signed = false, member_name = "b", node_id = 43 : i64, referenced_path = "C::b", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.b, semantic_type = !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
                      talos.sv.expression.named_value attributes {is_signed = false, node_id = 44 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s18.top::@s19.top::@s20.c, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.C>} {
                      }
                    }
                    talos.sv.expression.integer_literal attributes {constant_value = "3", is_declared_unsized = true, is_signed = true, node_id = 45 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  talos.sv.expression.integer_literal attributes {constant_value = "1'b1", is_signed = false, node_id = 46 : i64, semantic_type = !talos.ranged_packed_array<0 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
                  }
                }
              }
              talos.sv.statement.expression_statement attributes {node_id = 47 : i64} {
                talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 48 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
                  talos.sv.expression.element_select attributes {is_signed = false, node_id = 49 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
                    talos.sv.expression.member_access attributes {is_signed = false, member_name = "b", node_id = 50 : i64, referenced_path = "C::b", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.b, semantic_type = !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
                      talos.sv.expression.named_value attributes {is_signed = false, node_id = 51 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s18.top::@s19.top::@s20.c, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.C>} {
                      }
                    }
                    talos.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 52 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  talos.sv.expression.integer_literal attributes {constant_value = "1'b1", is_signed = false, node_id = 53 : i64, semantic_type = !talos.ranged_packed_array<0 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
                  }
                }
              }
              talos.sv.statement.expression_statement attributes {node_id = 54 : i64} {
                talos.sv.expression.assignment attributes {assignment_kind = 1 : i32, is_signed = false, node_id = 55 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
                  talos.sv.expression.element_select attributes {is_signed = false, node_id = 56 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
                    talos.sv.expression.element_select attributes {is_signed = false, node_id = 57 : i64, semantic_type = !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
                      talos.sv.expression.member_access attributes {is_signed = false, member_name = "m", node_id = 58 : i64, referenced_path = "C::m", referenced_symbol = @s1.$root::@s2::@s3.C::@s5.m, semantic_type = !talos.ranged_unpacked_array<0 : 1 x !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>>} {
                        talos.sv.expression.named_value attributes {is_signed = false, node_id = 59 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s18.top::@s19.top::@s20.c, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.C>} {
                        }
                      }
                      talos.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 60 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                    talos.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 61 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  talos.sv.expression.integer_literal attributes {constant_value = "1'b1", is_signed = false, node_id = 62 : i64, semantic_type = !talos.ranged_packed_array<0 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
                  }
                }
              }
              talos.sv.statement.expression_statement attributes {node_id = 63 : i64} {
                talos.sv.expression.assignment attributes {assignment_kind = 1 : i32, is_signed = false, node_id = 64 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
                  talos.sv.expression.element_select attributes {is_signed = false, node_id = 65 : i64, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>} {
                    talos.sv.expression.element_select attributes {is_signed = false, node_id = 66 : i64, semantic_type = !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
                      talos.sv.expression.member_access attributes {is_signed = false, member_name = "m", node_id = 67 : i64, referenced_path = "C::m", referenced_symbol = @s1.$root::@s2::@s3.C::@s5.m, semantic_type = !talos.ranged_unpacked_array<0 : 1 x !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>>} {
                        talos.sv.expression.named_value attributes {is_signed = false, node_id = 68 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s18.top::@s19.top::@s20.c, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.C>} {
                        }
                      }
                      talos.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 69 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                      }
                    }
                    talos.sv.expression.integer_literal attributes {constant_value = "3", is_declared_unsized = true, is_signed = true, node_id = 70 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  talos.sv.expression.integer_literal attributes {constant_value = "1'b1", is_signed = false, node_id = 71 : i64, semantic_type = !talos.ranged_packed_array<0 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
                  }
                }
              }
              talos.sv.statement.timed attributes {node_id = 72 : i64} {
                talos.sv.timing.delay attributes {node_id = 73 : i64} {
                  talos.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 74 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                talos.sv.statement.expression_statement attributes {node_id = 75 : i64} {
                  talos.sv.expression.call attributes {argument_count = 3 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 76 : i64, semantic_type = !talos.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s18.top::@s19.top} {
                    talos.sv.expression.string_literal attributes {constant_value = "%b %b", is_signed = false, node_id = 77 : i64, semantic_type = !talos.ranged_packed_array<39 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
                    }
                    talos.sv.expression.member_access attributes {is_signed = false, member_name = "b", node_id = 78 : i64, referenced_path = "C::b", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.b, semantic_type = !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
                      talos.sv.expression.named_value attributes {is_signed = false, node_id = 79 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s18.top::@s19.top::@s20.c, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.C>} {
                      }
                    }
                    talos.sv.expression.element_select attributes {is_signed = false, node_id = 80 : i64, semantic_type = !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
                      talos.sv.expression.member_access attributes {is_signed = false, member_name = "m", node_id = 81 : i64, referenced_path = "C::m", referenced_symbol = @s1.$root::@s2::@s3.C::@s5.m, semantic_type = !talos.ranged_unpacked_array<0 : 1 x !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>>} {
                        talos.sv.expression.named_value attributes {is_signed = false, node_id = 82 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s18.top::@s19.top::@s20.c, semantic_type = !talos.class_handle<@s1.$root::@s2::@s3.C>} {
                        }
                      }
                      talos.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 83 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
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
