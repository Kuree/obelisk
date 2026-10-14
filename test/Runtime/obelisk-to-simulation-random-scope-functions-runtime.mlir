// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | obelisk-translate --mlir-to-llvmir \
// RUN:   | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --seed=13 | FileCheck %s --check-prefix=RUNTIME
// RUN: %t.exe --execution-tier=bytecode --seed=13 | FileCheck %s --check-prefix=RUNTIME
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | obelisk-translate --mlir-to-llvmir \
// RUN:   | %llc -filetype=obj -relocation-model=pic -o %t.opt.o
// RUN: %llvm_dist/bin/clang++ %t.opt.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.opt.exe
// RUN: %t.opt.exe --seed=13 | FileCheck %s --check-prefix=RUNTIME
// RUN: %t.opt.exe --execution-tier=bytecode --seed=13 | FileCheck %s --check-prefix=RUNTIME
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.5.11 and 18.12: scope function arguments establish
// independent priority stages, with the same failure and rollback as objects.
// RUNTIME: function priority valid 1
// RUNTIME-NEXT: function priority failures 1

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.definition @top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 5028 : i64} {}
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 5029 : i64} {
    obelisk.sv.symbol.compilation_unit @unit attributes {hierarchical_name = "$unit", node_id = 5030 : i64} {}
    obelisk.sv.symbol.instance @instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", referenced_path = "top", referenced_symbol = @top, node_id = 5031 : i64} {
      obelisk.sv.symbol.instance_body @body attributes {hierarchical_name = "top", name = "top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 5032 : i64} {
        obelisk.sv.symbol.variable @x attributes {hierarchical_name = "top.x", name = "x", lifetime = 1 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5033 : i64} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "1", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5034 : i64} {}
        }
        obelisk.sv.symbol.variable @y attributes {hierarchical_name = "top.y", name = "y", lifetime = 1 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5036 : i64} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "1", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5037 : i64} {}
        }
        obelisk.sv.symbol.subroutine @id attributes {hierarchical_name = "top.id", name = "id", return_variable_path = "top.id.id", return_variable_symbol = @root::@instance::@body::@id::@result, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.integral<1, false, false, 0 : 0, bit>, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 5039 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 5040 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5041 : i64} {
              obelisk.sv.expression.named_value attributes {referenced_path = "top.id.id", referenced_symbol = @root::@instance::@body::@id::@result, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5042 : i64} {}
              obelisk.sv.expression.named_value attributes {referenced_path = "top.id.value", referenced_symbol = @root::@instance::@body::@id::@value, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5043 : i64} {}
            }
          }
          obelisk.sv.symbol.formal_argument @value attributes {direction = 0 : i32, hierarchical_name = "top.id.value", name = "value", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5046 : i64} {}
          obelisk.sv.symbol.variable @result attributes {hierarchical_name = "top.id.id", is_compiler_generated, name = "id", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5047 : i64} {}
        }
        obelisk.sv.symbol.variable @valid attributes {hierarchical_name = "top.valid", lifetime = 1 : i32, name = "valid", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5049 : i64} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "1", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5050 : i64} {}
        }
        obelisk.sv.symbol.variable @accepted attributes {hierarchical_name = "top.accepted", lifetime = 1 : i32, name = "accepted", semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 5052 : i64} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 5053 : i64} {}
        }
        obelisk.sv.symbol.variable @status attributes {hierarchical_name = "top.status", lifetime = 1 : i32, name = "status", semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 5055 : i64} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 5056 : i64} {}
        }
        obelisk.sv.symbol.variable @oldx attributes {hierarchical_name = "top.oldx", lifetime = 1 : i32, name = "oldx", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5058 : i64} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5059 : i64} {}
        }
        obelisk.sv.symbol.variable @oldy attributes {hierarchical_name = "top.oldy", lifetime = 1 : i32, name = "oldy", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5061 : i64} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5062 : i64} {}
        }
        obelisk.sv.symbol.procedural_block @initial attributes {hierarchical_name = "top", procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 5064 : i64} {
          obelisk.sv.statement.list attributes {node_id = 5065 : i64} {
            obelisk.sv.statement.repeat_loop attributes {node_id = 5066 : i64} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "256", semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, node_id = 5067 : i64} {}
              obelisk.sv.statement.list attributes {node_id = 5068 : i64} {
                obelisk.sv.statement.expression_statement attributes {node_id = 5069 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5070 : i64} {
                    obelisk.sv.expression.named_value attributes {referenced_path = "top.oldx", referenced_symbol = @root::@instance::@body::@oldx, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5071 : i64} {}
                    obelisk.sv.expression.named_value attributes {referenced_path = "top.x", referenced_symbol = @root::@instance::@body::@x, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5072 : i64} {}
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 5075 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5076 : i64} {
                    obelisk.sv.expression.named_value attributes {referenced_path = "top.oldy", referenced_symbol = @root::@instance::@body::@oldy, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5077 : i64} {}
                    obelisk.sv.expression.named_value attributes {referenced_path = "top.y", referenced_symbol = @root::@instance::@body::@y, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5078 : i64} {}
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 5081 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 5082 : i64} {
                    obelisk.sv.expression.named_value attributes {referenced_path = "top.status", referenced_symbol = @root::@instance::@body::@status, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 5083 : i64} {}
                    obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = true, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, subroutine_kind = 0 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, system_scope_path = "top", system_scope_symbol = @root::@instance::@body, node_id = 5084 : i64} {
                      obelisk.sv.constraint.list attributes {item_count = 2 : i64, node_id = 5085 : i64} {
                        obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 5086 : i64} {
                          obelisk.sv.expression.binary_op attributes {operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5087 : i64} {
                            obelisk.sv.expression.named_value attributes {referenced_path = "top.x", referenced_symbol = @root::@instance::@body::@x, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5088 : i64} {}
                            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "id", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = false, referenced_path = "top.id", referenced_symbol = @root::@instance::@body::@id, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, subroutine_kind = 0 : i32, node_id = 5089 : i64} {
                              obelisk.sv.expression.named_value attributes {referenced_path = "top.y", referenced_symbol = @root::@instance::@body::@y, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5090 : i64} {}
                            }
                          }
                        }
                        obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 5094 : i64} {
                          obelisk.sv.expression.binary_op attributes {operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5095 : i64} {
                            obelisk.sv.expression.named_value attributes {referenced_path = "top.x", referenced_symbol = @root::@instance::@body::@x, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5096 : i64} {}
                            obelisk.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5097 : i64} {}
                          }
                        }
                      }
                      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5101 : i64} {
                        obelisk.sv.expression.named_value attributes {referenced_path = "top.x", referenced_symbol = @root::@instance::@body::@x, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5102 : i64} {}
                        obelisk.sv.expression.empty_argument attributes {semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5103 : i64} {}
                      }
                      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5105 : i64} {
                        obelisk.sv.expression.named_value attributes {referenced_path = "top.y", referenced_symbol = @root::@instance::@body::@y, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5106 : i64} {}
                        obelisk.sv.expression.empty_argument attributes {semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5107 : i64} {}
                      }
                    }
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 5112 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5113 : i64} {
                    obelisk.sv.expression.named_value attributes {referenced_path = "top.valid", referenced_symbol = @root::@instance::@body::@valid, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5114 : i64} {}
                    obelisk.sv.expression.binary_op attributes {operator_kind = 19 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5115 : i64} {
                      obelisk.sv.expression.named_value attributes {referenced_path = "top.valid", referenced_symbol = @root::@instance::@body::@valid, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5116 : i64} {}
                      obelisk.sv.expression.binary_op attributes {operator_kind = 19 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5117 : i64} {
                        obelisk.sv.expression.binary_op attributes {operator_kind = 21 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5118 : i64} {
                          obelisk.sv.expression.unary_op attributes {operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5119 : i64} {
                            obelisk.sv.expression.binary_op attributes {operator_kind = 10 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5120 : i64} {
                              obelisk.sv.expression.named_value attributes {referenced_path = "top.status", referenced_symbol = @root::@instance::@body::@status, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 5121 : i64} {}
                              obelisk.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 5122 : i64} {}
                            }
                          }
                          obelisk.sv.expression.binary_op attributes {operator_kind = 19 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5125 : i64} {
                            obelisk.sv.expression.binary_op attributes {operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5126 : i64} {
                              obelisk.sv.expression.named_value attributes {referenced_path = "top.x", referenced_symbol = @root::@instance::@body::@x, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5127 : i64} {}
                              obelisk.sv.expression.named_value attributes {referenced_path = "top.oldx", referenced_symbol = @root::@instance::@body::@oldx, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5128 : i64} {}
                            }
                            obelisk.sv.expression.binary_op attributes {operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5130 : i64} {
                              obelisk.sv.expression.named_value attributes {referenced_path = "top.y", referenced_symbol = @root::@instance::@body::@y, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5131 : i64} {}
                              obelisk.sv.expression.named_value attributes {referenced_path = "top.oldy", referenced_symbol = @root::@instance::@body::@oldy, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5132 : i64} {}
                            }
                          }
                        }
                        obelisk.sv.expression.binary_op attributes {operator_kind = 21 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5136 : i64} {
                          obelisk.sv.expression.binary_op attributes {operator_kind = 10 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5137 : i64} {
                            obelisk.sv.expression.named_value attributes {referenced_path = "top.status", referenced_symbol = @root::@instance::@body::@status, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 5138 : i64} {}
                            obelisk.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 5139 : i64} {}
                          }
                          obelisk.sv.expression.binary_op attributes {operator_kind = 19 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5141 : i64} {
                            obelisk.sv.expression.binary_op attributes {operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5142 : i64} {
                              obelisk.sv.expression.named_value attributes {referenced_path = "top.x", referenced_symbol = @root::@instance::@body::@x, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5143 : i64} {}
                              obelisk.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5144 : i64} {}
                            }
                            obelisk.sv.expression.binary_op attributes {operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5146 : i64} {
                              obelisk.sv.expression.named_value attributes {referenced_path = "top.y", referenced_symbol = @root::@instance::@body::@y, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5147 : i64} {}
                              obelisk.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5148 : i64} {}
                            }
                          }
                        }
                      }
                    }
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 5156 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 5157 : i64} {
                    obelisk.sv.expression.named_value attributes {referenced_path = "top.accepted", referenced_symbol = @root::@instance::@body::@accepted, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 5158 : i64} {}
                    obelisk.sv.expression.binary_op attributes {operator_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 5159 : i64} {
                      obelisk.sv.expression.named_value attributes {referenced_path = "top.accepted", referenced_symbol = @root::@instance::@body::@accepted, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 5160 : i64} {}
                      obelisk.sv.expression.named_value attributes {referenced_path = "top.status", referenced_symbol = @root::@instance::@body::@status, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 5161 : i64} {}
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 5167 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, subroutine_kind = 1 : i32, semantic_type = !obelisk.void, system_scope_path = "top", system_scope_symbol = @root::@instance::@body, node_id = 5168 : i64} {
                obelisk.sv.expression.string_literal attributes {constant_value = "function priority valid %0d", semantic_type = !obelisk.ranged_packed_array<223 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 5169 : i64} {}
                obelisk.sv.expression.named_value attributes {referenced_path = "top.valid", referenced_symbol = @root::@instance::@body::@valid, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5170 : i64} {}
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 5173 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, subroutine_kind = 1 : i32, semantic_type = !obelisk.void, system_scope_path = "top", system_scope_symbol = @root::@instance::@body, node_id = 5174 : i64} {
                obelisk.sv.expression.string_literal attributes {constant_value = "function priority failures %0d", semantic_type = !obelisk.ranged_packed_array<247 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 5175 : i64} {}
                obelisk.sv.expression.binary_op attributes {operator_kind = 19 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5176 : i64} {
                  obelisk.sv.expression.binary_op attributes {operator_kind = 14 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5177 : i64} {
                    obelisk.sv.expression.named_value attributes {referenced_path = "top.accepted", referenced_symbol = @root::@instance::@body::@accepted, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 5178 : i64} {}
                    obelisk.sv.expression.integer_literal attributes {constant_value = "96", semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 5179 : i64} {}
                  }
                  obelisk.sv.expression.binary_op attributes {operator_kind = 16 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5181 : i64} {
                    obelisk.sv.expression.named_value attributes {referenced_path = "top.accepted", referenced_symbol = @root::@instance::@body::@accepted, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 5182 : i64} {}
                    obelisk.sv.expression.integer_literal attributes {constant_value = "160", semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 5183 : i64} {}
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
