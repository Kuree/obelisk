// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --seed=13 | FileCheck %s --check-prefix=RUNTIME
// RUN: %t.exe --execution-tier=bytecode --seed=13 | FileCheck %s --check-prefix=RUNTIME
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llc -filetype=obj -relocation-model=pic -o %t.opt.o
// RUN: %llvm_dist/bin/clang++ %t.opt.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.opt.exe
// RUN: %t.opt.exe --seed=13 | FileCheck %s --check-prefix=RUNTIME
// RUN: %t.opt.exe --execution-tier=bytecode --seed=13 | FileCheck %s --check-prefix=RUNTIME
// REQUIRES: runtime-z3

// LRM 18.5.11 and 12.5: a scope function case returns a 130-bit local
// initialized above bit 100. A nonmatching case falls through to a compound
// update. Independent argument priority and rollback apply to the whole result.
// RUNTIME: function priority valid 1
// RUNTIME-NEXT: function priority failures 1

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.definition @top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 18029 : i64} {}
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 18030 : i64} {
    obelisk.sv.symbol.compilation_unit @unit attributes {hierarchical_name = "$unit", node_id = 18031 : i64} {}
    obelisk.sv.symbol.instance @instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", referenced_path = "top", referenced_symbol = @top, node_id = 18032 : i64} {
      obelisk.sv.symbol.instance_body @body attributes {hierarchical_name = "top", name = "top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 18033 : i64} {
        obelisk.sv.symbol.variable @x attributes {hierarchical_name = "top.x", name = "x", lifetime = 1 : i32, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18034 : i64} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "1", semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18035 : i64} {}
        }
        obelisk.sv.symbol.variable @y attributes {hierarchical_name = "top.y", name = "y", lifetime = 1 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 18037 : i64} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "1", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 18038 : i64} {}
        }
        obelisk.sv.symbol.subroutine @id attributes {hierarchical_name = "top.id", name = "id", default_lifetime = 0 : i32, return_variable_path = "top.id.id", return_variable_symbol = @root::@instance::@body::@id::@result, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 18040 : i64} {
          obelisk.sv.statement.list attributes {node_id = 18041 : i64} {
            obelisk.sv.statement.variable_declaration attributes {referenced_path = "top.id.temp", referenced_symbol = @root::@instance::@body::@id::@temp, node_id = 18042 : i64} {}
            obelisk.sv.statement.case attributes {condition_kind = 0 : i32, check_kind = 0 : i32, item_count = 1 : i64, item_label_counts = array<i64: 1>, has_default = false, node_id = 18043 : i64} {
              obelisk.sv.expression.named_value attributes {referenced_path = "top.id.value", referenced_symbol = @root::@instance::@body::@id::@value, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 18044 : i64} {}
              obelisk.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 18045 : i64} {}
              obelisk.sv.statement.return attributes {node_id = 18046 : i64} {
                obelisk.sv.expression.named_value attributes {referenced_path = "top.id.temp", referenced_symbol = @root::@instance::@body::@id::@temp, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18047 : i64} {}
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 18050 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18051 : i64} {
                obelisk.sv.expression.named_value attributes {referenced_path = "top.id.temp", referenced_symbol = @root::@instance::@body::@id::@temp, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18052 : i64} {}
                obelisk.sv.expression.binary_op attributes {operator_kind = 7 : i32, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18053 : i64} {
                  obelisk.sv.expression.l_value_reference attributes {semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18054 : i64} {}
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18055 : i64} {}
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 18059 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18060 : i64} {
                obelisk.sv.expression.named_value attributes {referenced_path = "top.id.result", referenced_symbol = @root::@instance::@body::@id::@result, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18061 : i64} {}
                obelisk.sv.expression.named_value attributes {referenced_path = "top.id.temp", referenced_symbol = @root::@instance::@body::@id::@temp, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18062 : i64} {}
              }
            }
          }
          obelisk.sv.symbol.formal_argument @value attributes {direction = 0 : i32, hierarchical_name = "top.id.value", name = "value", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 18066 : i64} {}
          obelisk.sv.symbol.variable @result attributes {hierarchical_name = "top.id.id", is_compiler_generated, lifetime = 0 : i32, name = "id", semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18067 : i64} {}
          obelisk.sv.symbol.variable @temp attributes {hierarchical_name = "top.id.temp", lifetime = 0 : i32, name = "temp", semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18068 : i64} {
            obelisk.sv.expression.integer_literal attributes {constant_value = "1267650600228229401496703205376", semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18069 : i64} {}
          }
        }
        obelisk.sv.symbol.variable @valid attributes {hierarchical_name = "top.valid", lifetime = 1 : i32, name = "valid", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 18072 : i64} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "1", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 18073 : i64} {}
        }
        obelisk.sv.symbol.variable @accepted attributes {hierarchical_name = "top.accepted", lifetime = 1 : i32, name = "accepted", semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18075 : i64} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18076 : i64} {}
        }
        obelisk.sv.symbol.variable @status attributes {hierarchical_name = "top.status", lifetime = 1 : i32, name = "status", semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18078 : i64} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18079 : i64} {}
        }
        obelisk.sv.symbol.variable @oldx attributes {hierarchical_name = "top.oldx", lifetime = 1 : i32, name = "oldx", semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18081 : i64} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "1267650600228229401496703205376", semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18082 : i64} {}
        }
        obelisk.sv.symbol.variable @oldy attributes {hierarchical_name = "top.oldy", lifetime = 1 : i32, name = "oldy", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 18084 : i64} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 18085 : i64} {}
        }
        obelisk.sv.symbol.procedural_block @initial attributes {hierarchical_name = "top", procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 18087 : i64} {
          obelisk.sv.statement.list attributes {node_id = 18088 : i64} {
            obelisk.sv.statement.repeat_loop attributes {node_id = 18089 : i64} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "256", semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, node_id = 18090 : i64} {}
              obelisk.sv.statement.list attributes {node_id = 18091 : i64} {
                obelisk.sv.statement.expression_statement attributes {node_id = 18092 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18093 : i64} {
                    obelisk.sv.expression.named_value attributes {referenced_path = "top.oldx", referenced_symbol = @root::@instance::@body::@oldx, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18094 : i64} {}
                    obelisk.sv.expression.named_value attributes {referenced_path = "top.x", referenced_symbol = @root::@instance::@body::@x, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18095 : i64} {}
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 18098 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 18099 : i64} {
                    obelisk.sv.expression.named_value attributes {referenced_path = "top.oldy", referenced_symbol = @root::@instance::@body::@oldy, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 18100 : i64} {}
                    obelisk.sv.expression.named_value attributes {referenced_path = "top.y", referenced_symbol = @root::@instance::@body::@y, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 18101 : i64} {}
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 18104 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18105 : i64} {
                    obelisk.sv.expression.named_value attributes {referenced_path = "top.status", referenced_symbol = @root::@instance::@body::@status, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18106 : i64} {}
                    obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = true, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, subroutine_kind = 0 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, system_scope_path = "top", system_scope_symbol = @root::@instance::@body, node_id = 18107 : i64} {
                      obelisk.sv.constraint.list attributes {item_count = 2 : i64, node_id = 18108 : i64} {
                        obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 18109 : i64} {
                          obelisk.sv.expression.binary_op attributes {operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 18110 : i64} {
                            obelisk.sv.expression.named_value attributes {referenced_path = "top.x", referenced_symbol = @root::@instance::@body::@x, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18111 : i64} {}
                            obelisk.sv.expression.conversion attributes {conversion_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18112 : i64} {
                              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "id", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = false, referenced_path = "top.id", referenced_symbol = @root::@instance::@body::@id, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, subroutine_kind = 0 : i32, node_id = 18113 : i64} {
                                obelisk.sv.expression.named_value attributes {referenced_path = "top.y", referenced_symbol = @root::@instance::@body::@y, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 18114 : i64} {}
                              }
                            }
                          }
                        }
                        obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 18119 : i64} {
                          obelisk.sv.expression.binary_op attributes {operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 18120 : i64} {
                            obelisk.sv.expression.named_value attributes {referenced_path = "top.x", referenced_symbol = @root::@instance::@body::@x, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18121 : i64} {}
                            obelisk.sv.expression.integer_literal attributes {constant_value = "1267650600228229401496703205376", semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18122 : i64} {}
                          }
                        }
                      }
                      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18126 : i64} {
                        obelisk.sv.expression.named_value attributes {referenced_path = "top.x", referenced_symbol = @root::@instance::@body::@x, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18127 : i64} {}
                        obelisk.sv.expression.empty_argument attributes {semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18128 : i64} {}
                      }
                      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 18130 : i64} {
                        obelisk.sv.expression.named_value attributes {referenced_path = "top.y", referenced_symbol = @root::@instance::@body::@y, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 18131 : i64} {}
                        obelisk.sv.expression.empty_argument attributes {semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 18132 : i64} {}
                      }
                    }
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 18137 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 18138 : i64} {
                    obelisk.sv.expression.named_value attributes {referenced_path = "top.valid", referenced_symbol = @root::@instance::@body::@valid, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 18139 : i64} {}
                    obelisk.sv.expression.binary_op attributes {operator_kind = 19 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 18140 : i64} {
                      obelisk.sv.expression.named_value attributes {referenced_path = "top.valid", referenced_symbol = @root::@instance::@body::@valid, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 18141 : i64} {}
                      obelisk.sv.expression.binary_op attributes {operator_kind = 19 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 18142 : i64} {
                        obelisk.sv.expression.binary_op attributes {operator_kind = 21 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 18143 : i64} {
                          obelisk.sv.expression.unary_op attributes {operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 18144 : i64} {
                            obelisk.sv.expression.binary_op attributes {operator_kind = 10 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 18145 : i64} {
                              obelisk.sv.expression.named_value attributes {referenced_path = "top.status", referenced_symbol = @root::@instance::@body::@status, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18146 : i64} {}
                              obelisk.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18147 : i64} {}
                            }
                          }
                          obelisk.sv.expression.binary_op attributes {operator_kind = 19 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 18150 : i64} {
                            obelisk.sv.expression.binary_op attributes {operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 18151 : i64} {
                              obelisk.sv.expression.named_value attributes {referenced_path = "top.x", referenced_symbol = @root::@instance::@body::@x, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18152 : i64} {}
                              obelisk.sv.expression.named_value attributes {referenced_path = "top.oldx", referenced_symbol = @root::@instance::@body::@oldx, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18153 : i64} {}
                            }
                            obelisk.sv.expression.binary_op attributes {operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 18155 : i64} {
                              obelisk.sv.expression.named_value attributes {referenced_path = "top.y", referenced_symbol = @root::@instance::@body::@y, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 18156 : i64} {}
                              obelisk.sv.expression.named_value attributes {referenced_path = "top.oldy", referenced_symbol = @root::@instance::@body::@oldy, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 18157 : i64} {}
                            }
                          }
                        }
                        obelisk.sv.expression.binary_op attributes {operator_kind = 21 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 18161 : i64} {
                          obelisk.sv.expression.binary_op attributes {operator_kind = 10 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 18162 : i64} {
                            obelisk.sv.expression.named_value attributes {referenced_path = "top.status", referenced_symbol = @root::@instance::@body::@status, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18163 : i64} {}
                            obelisk.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18164 : i64} {}
                          }
                          obelisk.sv.expression.binary_op attributes {operator_kind = 19 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 18166 : i64} {
                            obelisk.sv.expression.binary_op attributes {operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 18167 : i64} {
                              obelisk.sv.expression.named_value attributes {referenced_path = "top.x", referenced_symbol = @root::@instance::@body::@x, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18168 : i64} {}
                              obelisk.sv.expression.integer_literal attributes {constant_value = "1267650600228229401496703205376", semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18169 : i64} {}
                            }
                            obelisk.sv.expression.binary_op attributes {operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 18171 : i64} {
                              obelisk.sv.expression.named_value attributes {referenced_path = "top.y", referenced_symbol = @root::@instance::@body::@y, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 18172 : i64} {}
                              obelisk.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 18173 : i64} {}
                            }
                          }
                        }
                      }
                    }
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 18181 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18182 : i64} {
                    obelisk.sv.expression.named_value attributes {referenced_path = "top.accepted", referenced_symbol = @root::@instance::@body::@accepted, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18183 : i64} {}
                    obelisk.sv.expression.binary_op attributes {operator_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18184 : i64} {
                      obelisk.sv.expression.named_value attributes {referenced_path = "top.accepted", referenced_symbol = @root::@instance::@body::@accepted, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18185 : i64} {}
                      obelisk.sv.expression.named_value attributes {referenced_path = "top.status", referenced_symbol = @root::@instance::@body::@status, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18186 : i64} {}
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 18192 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, subroutine_kind = 1 : i32, semantic_type = !obelisk.void, system_scope_path = "top", system_scope_symbol = @root::@instance::@body, node_id = 18193 : i64} {
                obelisk.sv.expression.string_literal attributes {constant_value = "function priority valid %0d", semantic_type = !obelisk.ranged_packed_array<223 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18194 : i64} {}
                obelisk.sv.expression.named_value attributes {referenced_path = "top.valid", referenced_symbol = @root::@instance::@body::@valid, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 18195 : i64} {}
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 18198 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, subroutine_kind = 1 : i32, semantic_type = !obelisk.void, system_scope_path = "top", system_scope_symbol = @root::@instance::@body, node_id = 18199 : i64} {
                obelisk.sv.expression.string_literal attributes {constant_value = "function priority failures %0d", semantic_type = !obelisk.ranged_packed_array<247 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18200 : i64} {}
                obelisk.sv.expression.binary_op attributes {operator_kind = 19 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 18201 : i64} {
                  obelisk.sv.expression.binary_op attributes {operator_kind = 14 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 18202 : i64} {
                    obelisk.sv.expression.named_value attributes {referenced_path = "top.accepted", referenced_symbol = @root::@instance::@body::@accepted, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18203 : i64} {}
                    obelisk.sv.expression.integer_literal attributes {constant_value = "96", semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18204 : i64} {}
                  }
                  obelisk.sv.expression.binary_op attributes {operator_kind = 16 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 18206 : i64} {
                    obelisk.sv.expression.named_value attributes {referenced_path = "top.accepted", referenced_symbol = @root::@instance::@body::@accepted, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18207 : i64} {}
                    obelisk.sv.expression.integer_literal attributes {constant_value = "160", semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 18208 : i64} {}
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
