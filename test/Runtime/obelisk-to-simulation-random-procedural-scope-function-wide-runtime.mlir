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
// 130-bit automatic locals, typed initializers, early returns and compound
// assignments retain one-way priority and transactional scope writes.
// RUNTIME: function priority valid 1
// RUNTIME-NEXT: function priority failures 1

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.definition @top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 7028 : i64} {}
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 7029 : i64} {
    obelisk.sv.symbol.compilation_unit @unit attributes {hierarchical_name = "$unit", node_id = 7030 : i64} {}
    obelisk.sv.symbol.instance @instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", referenced_path = "top", referenced_symbol = @top, node_id = 7031 : i64} {
      obelisk.sv.symbol.instance_body @body attributes {hierarchical_name = "top", name = "top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 7032 : i64} {
        obelisk.sv.symbol.variable @x attributes {hierarchical_name = "top.x", name = "x", lifetime = 1 : i32, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7033 : i64} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "1", semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7034 : i64} {}
        }
        obelisk.sv.symbol.variable @y attributes {hierarchical_name = "top.y", name = "y", lifetime = 1 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 7036 : i64} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "1", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 7037 : i64} {}
        }
        obelisk.sv.symbol.subroutine @id attributes {hierarchical_name = "top.id", name = "id", default_lifetime = 0 : i32, return_variable_path = "top.id.id", return_variable_symbol = @root::@instance::@body::@id::@result, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 7039 : i64} {
          obelisk.sv.statement.list attributes {node_id = 7040 : i64} {
            obelisk.sv.statement.variable_declaration attributes {referenced_path = "top.id.temp", referenced_symbol = @root::@instance::@body::@id::@temp, node_id = 7041 : i64} {}
            obelisk.sv.statement.conditional attributes {check_kind = 0 : i32, condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, has_else = false, node_id = 7042 : i64} {
              obelisk.sv.expression.binary_op attributes {operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 7043 : i64} {
                obelisk.sv.expression.named_value attributes {referenced_path = "top.id.value", referenced_symbol = @root::@instance::@body::@id::@value, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 7044 : i64} {}
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 7045 : i64} {}
              }
              obelisk.sv.statement.return attributes {node_id = 7047 : i64} {
                obelisk.sv.expression.named_value attributes {referenced_path = "top.id.temp", referenced_symbol = @root::@instance::@body::@id::@temp, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7048 : i64} {}
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 7051 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7052 : i64} {
                obelisk.sv.expression.named_value attributes {referenced_path = "top.id.temp", referenced_symbol = @root::@instance::@body::@id::@temp, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7053 : i64} {}
                obelisk.sv.expression.binary_op attributes {operator_kind = 7 : i32, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7054 : i64} {
                  obelisk.sv.expression.l_value_reference attributes {semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7055 : i64} {}
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7056 : i64} {}
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 7060 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7061 : i64} {
                obelisk.sv.expression.named_value attributes {referenced_path = "top.id.result", referenced_symbol = @root::@instance::@body::@id::@result, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7062 : i64} {}
                obelisk.sv.expression.named_value attributes {referenced_path = "top.id.temp", referenced_symbol = @root::@instance::@body::@id::@temp, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7063 : i64} {}
              }
            }
          }
          obelisk.sv.symbol.formal_argument @value attributes {direction = 0 : i32, hierarchical_name = "top.id.value", name = "value", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 7067 : i64} {}
          obelisk.sv.symbol.variable @result attributes {hierarchical_name = "top.id.id", is_compiler_generated, lifetime = 0 : i32, name = "id", semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7068 : i64} {}
          obelisk.sv.symbol.variable @temp attributes {hierarchical_name = "top.id.temp", lifetime = 0 : i32, name = "temp", semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7069 : i64} {
            obelisk.sv.expression.integer_literal attributes {constant_value = "1267650600228229401496703205376", semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7070 : i64} {}
          }
        }
        obelisk.sv.symbol.variable @valid attributes {hierarchical_name = "top.valid", lifetime = 1 : i32, name = "valid", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 7073 : i64} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "1", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 7074 : i64} {}
        }
        obelisk.sv.symbol.variable @accepted attributes {hierarchical_name = "top.accepted", lifetime = 1 : i32, name = "accepted", semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7076 : i64} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7077 : i64} {}
        }
        obelisk.sv.symbol.variable @status attributes {hierarchical_name = "top.status", lifetime = 1 : i32, name = "status", semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7079 : i64} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7080 : i64} {}
        }
        obelisk.sv.symbol.variable @oldx attributes {hierarchical_name = "top.oldx", lifetime = 1 : i32, name = "oldx", semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7082 : i64} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "1267650600228229401496703205376", semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7083 : i64} {}
        }
        obelisk.sv.symbol.variable @oldy attributes {hierarchical_name = "top.oldy", lifetime = 1 : i32, name = "oldy", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 7085 : i64} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 7086 : i64} {}
        }
        obelisk.sv.symbol.procedural_block @initial attributes {hierarchical_name = "top", procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 7088 : i64} {
          obelisk.sv.statement.list attributes {node_id = 7089 : i64} {
            obelisk.sv.statement.repeat_loop attributes {node_id = 7090 : i64} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "256", semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, node_id = 7091 : i64} {}
              obelisk.sv.statement.list attributes {node_id = 7092 : i64} {
                obelisk.sv.statement.expression_statement attributes {node_id = 7093 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7094 : i64} {
                    obelisk.sv.expression.named_value attributes {referenced_path = "top.oldx", referenced_symbol = @root::@instance::@body::@oldx, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7095 : i64} {}
                    obelisk.sv.expression.named_value attributes {referenced_path = "top.x", referenced_symbol = @root::@instance::@body::@x, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7096 : i64} {}
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 7099 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 7100 : i64} {
                    obelisk.sv.expression.named_value attributes {referenced_path = "top.oldy", referenced_symbol = @root::@instance::@body::@oldy, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 7101 : i64} {}
                    obelisk.sv.expression.named_value attributes {referenced_path = "top.y", referenced_symbol = @root::@instance::@body::@y, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 7102 : i64} {}
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 7105 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7106 : i64} {
                    obelisk.sv.expression.named_value attributes {referenced_path = "top.status", referenced_symbol = @root::@instance::@body::@status, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7107 : i64} {}
                    obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = true, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, subroutine_kind = 0 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, system_scope_path = "top", system_scope_symbol = @root::@instance::@body, node_id = 7108 : i64} {
                      obelisk.sv.constraint.list attributes {item_count = 2 : i64, node_id = 7109 : i64} {
                        obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 7110 : i64} {
                          obelisk.sv.expression.binary_op attributes {operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 7111 : i64} {
                            obelisk.sv.expression.named_value attributes {referenced_path = "top.x", referenced_symbol = @root::@instance::@body::@x, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7112 : i64} {}
                            obelisk.sv.expression.conversion attributes {conversion_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7113 : i64} {
                              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "id", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = false, referenced_path = "top.id", referenced_symbol = @root::@instance::@body::@id, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, subroutine_kind = 0 : i32, node_id = 7114 : i64} {
                                obelisk.sv.expression.named_value attributes {referenced_path = "top.y", referenced_symbol = @root::@instance::@body::@y, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 7115 : i64} {}
                              }
                            }
                          }
                        }
                        obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 7120 : i64} {
                          obelisk.sv.expression.binary_op attributes {operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 7121 : i64} {
                            obelisk.sv.expression.named_value attributes {referenced_path = "top.x", referenced_symbol = @root::@instance::@body::@x, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7122 : i64} {}
                            obelisk.sv.expression.integer_literal attributes {constant_value = "1267650600228229401496703205376", semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7123 : i64} {}
                          }
                        }
                      }
                      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7127 : i64} {
                        obelisk.sv.expression.named_value attributes {referenced_path = "top.x", referenced_symbol = @root::@instance::@body::@x, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7128 : i64} {}
                        obelisk.sv.expression.empty_argument attributes {semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7129 : i64} {}
                      }
                      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 7131 : i64} {
                        obelisk.sv.expression.named_value attributes {referenced_path = "top.y", referenced_symbol = @root::@instance::@body::@y, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 7132 : i64} {}
                        obelisk.sv.expression.empty_argument attributes {semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 7133 : i64} {}
                      }
                    }
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 7138 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 7139 : i64} {
                    obelisk.sv.expression.named_value attributes {referenced_path = "top.valid", referenced_symbol = @root::@instance::@body::@valid, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 7140 : i64} {}
                    obelisk.sv.expression.binary_op attributes {operator_kind = 19 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 7141 : i64} {
                      obelisk.sv.expression.named_value attributes {referenced_path = "top.valid", referenced_symbol = @root::@instance::@body::@valid, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 7142 : i64} {}
                      obelisk.sv.expression.binary_op attributes {operator_kind = 19 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 7143 : i64} {
                        obelisk.sv.expression.binary_op attributes {operator_kind = 21 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 7144 : i64} {
                          obelisk.sv.expression.unary_op attributes {operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 7145 : i64} {
                            obelisk.sv.expression.binary_op attributes {operator_kind = 10 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 7146 : i64} {
                              obelisk.sv.expression.named_value attributes {referenced_path = "top.status", referenced_symbol = @root::@instance::@body::@status, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7147 : i64} {}
                              obelisk.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7148 : i64} {}
                            }
                          }
                          obelisk.sv.expression.binary_op attributes {operator_kind = 19 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 7151 : i64} {
                            obelisk.sv.expression.binary_op attributes {operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 7152 : i64} {
                              obelisk.sv.expression.named_value attributes {referenced_path = "top.x", referenced_symbol = @root::@instance::@body::@x, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7153 : i64} {}
                              obelisk.sv.expression.named_value attributes {referenced_path = "top.oldx", referenced_symbol = @root::@instance::@body::@oldx, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7154 : i64} {}
                            }
                            obelisk.sv.expression.binary_op attributes {operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 7156 : i64} {
                              obelisk.sv.expression.named_value attributes {referenced_path = "top.y", referenced_symbol = @root::@instance::@body::@y, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 7157 : i64} {}
                              obelisk.sv.expression.named_value attributes {referenced_path = "top.oldy", referenced_symbol = @root::@instance::@body::@oldy, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 7158 : i64} {}
                            }
                          }
                        }
                        obelisk.sv.expression.binary_op attributes {operator_kind = 21 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 7162 : i64} {
                          obelisk.sv.expression.binary_op attributes {operator_kind = 10 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 7163 : i64} {
                            obelisk.sv.expression.named_value attributes {referenced_path = "top.status", referenced_symbol = @root::@instance::@body::@status, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7164 : i64} {}
                            obelisk.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7165 : i64} {}
                          }
                          obelisk.sv.expression.binary_op attributes {operator_kind = 19 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 7167 : i64} {
                            obelisk.sv.expression.binary_op attributes {operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 7168 : i64} {
                              obelisk.sv.expression.named_value attributes {referenced_path = "top.x", referenced_symbol = @root::@instance::@body::@x, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7169 : i64} {}
                              obelisk.sv.expression.integer_literal attributes {constant_value = "1267650600228229401496703205376", semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7170 : i64} {}
                            }
                            obelisk.sv.expression.binary_op attributes {operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 7172 : i64} {
                              obelisk.sv.expression.named_value attributes {referenced_path = "top.y", referenced_symbol = @root::@instance::@body::@y, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 7173 : i64} {}
                              obelisk.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 7174 : i64} {}
                            }
                          }
                        }
                      }
                    }
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 7182 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7183 : i64} {
                    obelisk.sv.expression.named_value attributes {referenced_path = "top.accepted", referenced_symbol = @root::@instance::@body::@accepted, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7184 : i64} {}
                    obelisk.sv.expression.binary_op attributes {operator_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7185 : i64} {
                      obelisk.sv.expression.named_value attributes {referenced_path = "top.accepted", referenced_symbol = @root::@instance::@body::@accepted, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7186 : i64} {}
                      obelisk.sv.expression.named_value attributes {referenced_path = "top.status", referenced_symbol = @root::@instance::@body::@status, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7187 : i64} {}
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 7193 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, subroutine_kind = 1 : i32, semantic_type = !obelisk.void, system_scope_path = "top", system_scope_symbol = @root::@instance::@body, node_id = 7194 : i64} {
                obelisk.sv.expression.string_literal attributes {constant_value = "function priority valid %0d", semantic_type = !obelisk.ranged_packed_array<223 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7195 : i64} {}
                obelisk.sv.expression.named_value attributes {referenced_path = "top.valid", referenced_symbol = @root::@instance::@body::@valid, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 7196 : i64} {}
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 7199 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, subroutine_kind = 1 : i32, semantic_type = !obelisk.void, system_scope_path = "top", system_scope_symbol = @root::@instance::@body, node_id = 7200 : i64} {
                obelisk.sv.expression.string_literal attributes {constant_value = "function priority failures %0d", semantic_type = !obelisk.ranged_packed_array<247 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7201 : i64} {}
                obelisk.sv.expression.binary_op attributes {operator_kind = 19 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 7202 : i64} {
                  obelisk.sv.expression.binary_op attributes {operator_kind = 14 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 7203 : i64} {
                    obelisk.sv.expression.named_value attributes {referenced_path = "top.accepted", referenced_symbol = @root::@instance::@body::@accepted, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7204 : i64} {}
                    obelisk.sv.expression.integer_literal attributes {constant_value = "96", semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7205 : i64} {}
                  }
                  obelisk.sv.expression.binary_op attributes {operator_kind = 16 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 7207 : i64} {
                    obelisk.sv.expression.named_value attributes {referenced_path = "top.accepted", referenced_symbol = @root::@instance::@body::@accepted, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7208 : i64} {}
                    obelisk.sv.expression.integer_literal attributes {constant_value = "160", semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 7209 : i64} {}
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
