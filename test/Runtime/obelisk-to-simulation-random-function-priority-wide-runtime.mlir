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
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.5.11: x == id(y), x == 0 must solve y independently.
// Both function-priority masks and the return conversion span 64-bit words.
// Half the y choices cause the lower stage to fail, leaving both fields intact.
// Treating this as explicit solve-before always succeeds with x=y=0 instead.
// RUNTIME: function priority valid 1
// RUNTIME-NEXT: function priority failures 1

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.definition @top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 114 : i64} {}
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 118 : i64} {
    obelisk.sv.symbol.compilation_unit @unit attributes {hierarchical_name = "$unit", node_id = 115 : i64} {
      obelisk.sv.type.class_type @C attributes {bitstream_width = 131 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", semantic_type = !obelisk.class_handle<@root::@unit::@C>, this_variable_path = "C::this", this_variable_symbol = @root::@unit::@C::@this, node_id = 28 : i64} {
        obelisk.sv.symbol.class_property @x attributes {hierarchical_name = "C::x", name = "x", rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 12 : i64} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "1", semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 11 : i64} {}
        }
        obelisk.sv.symbol.class_property @y attributes {hierarchical_name = "C::y", name = "y", rand_mode = 1 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 14 : i64} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "1", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 13 : i64} {}
        }
        obelisk.sv.symbol.variable @this attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", semantic_type = !obelisk.class_handle<@root::@unit::@C>, node_id = 15 : i64} {}
        obelisk.sv.symbol.subroutine @id attributes {hierarchical_name = "C::id", name = "id", return_variable_path = "C::id.id", return_variable_symbol = @root::@unit::@C::@id::@result, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.integral<1, false, false, 0 : 0, bit>, false>, subroutine_kind = 0 : i32, this_variable_path = "C::id.this", this_variable_symbol = @root::@unit::@C::@id::@id_this, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 8 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 4 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 3 : i64} {
              obelisk.sv.expression.named_value attributes {referenced_path = "C::id.id", referenced_symbol = @root::@unit::@C::@id::@result, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 1 : i64} {}
              obelisk.sv.expression.named_value attributes {referenced_path = "C::id.value", referenced_symbol = @root::@unit::@C::@id::@value, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 2 : i64} {}
            }
          }
          obelisk.sv.symbol.formal_argument @value attributes {direction = 0 : i32, hierarchical_name = "C::id.value", name = "value", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 5 : i64} {}
          obelisk.sv.symbol.variable @result attributes {hierarchical_name = "C::id.id", is_compiler_generated, name = "id", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 6 : i64} {}
          obelisk.sv.symbol.variable @id_this attributes {hierarchical_name = "C::id.this", is_compiler_generated, is_const, name = "this", semantic_type = !obelisk.class_handle<@root::@unit::@C>, node_id = 7 : i64} {}
        }
        obelisk.sv.symbol.constraint_block @relation attributes {hierarchical_name = "C::relation", name = "relation", node_id = 21 : i64} {
          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 20 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 19 : i64} {
              obelisk.sv.expression.binary_op attributes {operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 18 : i64} {
                obelisk.sv.expression.named_value attributes {referenced_path = "C::x", referenced_symbol = @root::@unit::@C::@x, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 16 : i64} {}
                obelisk.sv.expression.conversion attributes {conversion_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 17 : i64} {
                  obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "id", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = false, referenced_path = "C::id", referenced_symbol = @root::@unit::@C::@id, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, subroutine_kind = 0 : i32, node_id = 10 : i64} {
                    obelisk.sv.expression.named_value attributes {referenced_path = "C::y", referenced_symbol = @root::@unit::@C::@y, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 9 : i64} {}
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.constraint_block @limit attributes {hierarchical_name = "C::limit", name = "limit", node_id = 27 : i64} {
          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 26 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 25 : i64} {
              obelisk.sv.expression.binary_op attributes {operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 24 : i64} {
                obelisk.sv.expression.named_value attributes {referenced_path = "C::x", referenced_symbol = @root::@unit::@C::@x, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 22 : i64} {}
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 23 : i64} {}
              }
            }
          }
        }
      }
    }
    obelisk.sv.symbol.instance @instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", referenced_path = "top", referenced_symbol = @top, node_id = 117 : i64} {
      obelisk.sv.symbol.instance_body @body attributes {hierarchical_name = "top", name = "top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 116 : i64} {
        obelisk.sv.symbol.variable @object attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", semantic_type = !obelisk.class_handle<@root::@unit::@C>, node_id = 91 : i64} {
          obelisk.sv.expression.new_class attributes {is_super_class = false, semantic_type = !obelisk.class_handle<@root::@unit::@C>, node_id = 90 : i64} {}
        }
        obelisk.sv.symbol.variable @valid attributes {hierarchical_name = "top.valid", lifetime = 1 : i32, name = "valid", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 93 : i64} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "1", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 92 : i64} {}
        }
        obelisk.sv.symbol.variable @accepted attributes {hierarchical_name = "top.accepted", lifetime = 1 : i32, name = "accepted", semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 95 : i64} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 94 : i64} {}
        }
        obelisk.sv.symbol.variable @status attributes {hierarchical_name = "top.status", lifetime = 1 : i32, name = "status", semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 97 : i64} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 96 : i64} {}
        }
        obelisk.sv.symbol.variable @oldx attributes {hierarchical_name = "top.oldx", lifetime = 1 : i32, name = "oldx", semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 99 : i64} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 98 : i64} {}
        }
        obelisk.sv.symbol.variable @oldy attributes {hierarchical_name = "top.oldy", lifetime = 1 : i32, name = "oldy", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 101 : i64} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 100 : i64} {}
        }
        obelisk.sv.symbol.procedural_block @initial attributes {hierarchical_name = "top", procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 113 : i64} {
          obelisk.sv.statement.list attributes {node_id = 112 : i64} {
            obelisk.sv.statement.repeat_loop attributes {node_id = 104 : i64} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "256", semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, node_id = 102 : i64} {}
              obelisk.sv.statement.list attributes {node_id = 103 : i64} {
                obelisk.sv.statement.expression_statement attributes {node_id = 61 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 60 : i64} {
                    obelisk.sv.expression.named_value attributes {referenced_path = "top.oldx", referenced_symbol = @root::@instance::@body::@oldx, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 59 : i64} {}
                    obelisk.sv.expression.member_access attributes {referenced_path = "C::x", referenced_symbol = @root::@unit::@C::@x, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 58 : i64} {
                      obelisk.sv.expression.named_value attributes {referenced_path = "top.object", referenced_symbol = @root::@instance::@body::@object, semantic_type = !obelisk.class_handle<@root::@unit::@C>, node_id = 57 : i64} {}
                    }
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 66 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 65 : i64} {
                    obelisk.sv.expression.named_value attributes {referenced_path = "top.oldy", referenced_symbol = @root::@instance::@body::@oldy, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 64 : i64} {}
                    obelisk.sv.expression.member_access attributes {referenced_path = "C::y", referenced_symbol = @root::@unit::@C::@y, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 63 : i64} {
                      obelisk.sv.expression.named_value attributes {referenced_path = "top.object", referenced_symbol = @root::@instance::@body::@object, semantic_type = !obelisk.class_handle<@root::@unit::@C>, node_id = 62 : i64} {}
                    }
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 71 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 70 : i64} {
                    obelisk.sv.expression.named_value attributes {referenced_path = "top.status", referenced_symbol = @root::@instance::@body::@status, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 69 : i64} {}
                    obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, subroutine_kind = 0 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, system_scope_path = "top", system_scope_symbol = @root::@instance::@body, node_id = 68 : i64} {
                      obelisk.sv.expression.named_value attributes {referenced_path = "top.object", referenced_symbol = @root::@instance::@body::@object, semantic_type = !obelisk.class_handle<@root::@unit::@C>, node_id = 67 : i64} {}
                    }
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 76 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 75 : i64} {
                    obelisk.sv.expression.named_value attributes {referenced_path = "top.valid", referenced_symbol = @root::@instance::@body::@valid, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 74 : i64} {}
                    obelisk.sv.expression.binary_op attributes {operator_kind = 19 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 73 : i64} {
                      obelisk.sv.expression.named_value attributes {referenced_path = "top.valid", referenced_symbol = @root::@instance::@body::@valid, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 72 : i64} {}
                      obelisk.sv.expression.binary_op attributes {operator_kind = 19 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 56 : i64} {
                        obelisk.sv.expression.binary_op attributes {operator_kind = 21 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 51 : i64} {
                          obelisk.sv.expression.unary_op attributes {operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 32 : i64} {
                            obelisk.sv.expression.binary_op attributes {operator_kind = 10 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 31 : i64} {
                              obelisk.sv.expression.named_value attributes {referenced_path = "top.status", referenced_symbol = @root::@instance::@body::@status, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 29 : i64} {}
                              obelisk.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 30 : i64} {}
                            }
                          }
                          obelisk.sv.expression.binary_op attributes {operator_kind = 19 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 41 : i64} {
                            obelisk.sv.expression.binary_op attributes {operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 36 : i64} {
                              obelisk.sv.expression.member_access attributes {referenced_path = "C::x", referenced_symbol = @root::@unit::@C::@x, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 34 : i64} {
                                obelisk.sv.expression.named_value attributes {referenced_path = "top.object", referenced_symbol = @root::@instance::@body::@object, semantic_type = !obelisk.class_handle<@root::@unit::@C>, node_id = 33 : i64} {}
                              }
                              obelisk.sv.expression.named_value attributes {referenced_path = "top.oldx", referenced_symbol = @root::@instance::@body::@oldx, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 35 : i64} {}
                            }
                            obelisk.sv.expression.binary_op attributes {operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 40 : i64} {
                              obelisk.sv.expression.member_access attributes {referenced_path = "C::y", referenced_symbol = @root::@unit::@C::@y, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 38 : i64} {
                                obelisk.sv.expression.named_value attributes {referenced_path = "top.object", referenced_symbol = @root::@instance::@body::@object, semantic_type = !obelisk.class_handle<@root::@unit::@C>, node_id = 37 : i64} {}
                              }
                              obelisk.sv.expression.named_value attributes {referenced_path = "top.oldy", referenced_symbol = @root::@instance::@body::@oldy, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 39 : i64} {}
                            }
                          }
                        }
                        obelisk.sv.expression.binary_op attributes {operator_kind = 21 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 55 : i64} {
                          obelisk.sv.expression.binary_op attributes {operator_kind = 10 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 54 : i64} {
                            obelisk.sv.expression.named_value attributes {referenced_path = "top.status", referenced_symbol = @root::@instance::@body::@status, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 52 : i64} {}
                            obelisk.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 53 : i64} {}
                          }
                          obelisk.sv.expression.binary_op attributes {operator_kind = 19 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 50 : i64} {
                            obelisk.sv.expression.binary_op attributes {operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 45 : i64} {
                              obelisk.sv.expression.member_access attributes {referenced_path = "C::x", referenced_symbol = @root::@unit::@C::@x, semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 43 : i64} {
                                obelisk.sv.expression.named_value attributes {referenced_path = "top.object", referenced_symbol = @root::@instance::@body::@object, semantic_type = !obelisk.class_handle<@root::@unit::@C>, node_id = 42 : i64} {}
                              }
                              obelisk.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !obelisk.ranged_packed_array<129 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 44 : i64} {}
                            }
                            obelisk.sv.expression.binary_op attributes {operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 49 : i64} {
                              obelisk.sv.expression.member_access attributes {referenced_path = "C::y", referenced_symbol = @root::@unit::@C::@y, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 47 : i64} {
                                obelisk.sv.expression.named_value attributes {referenced_path = "top.object", referenced_symbol = @root::@instance::@body::@object, semantic_type = !obelisk.class_handle<@root::@unit::@C>, node_id = 46 : i64} {}
                              }
                              obelisk.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 48 : i64} {}
                            }
                          }
                        }
                      }
                    }
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 82 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 81 : i64} {
                    obelisk.sv.expression.named_value attributes {referenced_path = "top.accepted", referenced_symbol = @root::@instance::@body::@accepted, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 80 : i64} {}
                    obelisk.sv.expression.binary_op attributes {operator_kind = 0 : i32, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 79 : i64} {
                      obelisk.sv.expression.named_value attributes {referenced_path = "top.accepted", referenced_symbol = @root::@instance::@body::@accepted, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 77 : i64} {}
                      obelisk.sv.expression.named_value attributes {referenced_path = "top.status", referenced_symbol = @root::@instance::@body::@status, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 78 : i64} {}
                    }
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 108 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, subroutine_kind = 1 : i32, semantic_type = !obelisk.void, system_scope_path = "top", system_scope_symbol = @root::@instance::@body, node_id = 107 : i64} {
                obelisk.sv.expression.string_literal attributes {constant_value = "function priority valid %0d", semantic_type = !obelisk.ranged_packed_array<223 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 106 : i64} {}
                obelisk.sv.expression.named_value attributes {referenced_path = "top.valid", referenced_symbol = @root::@instance::@body::@valid, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 105 : i64} {}
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 111 : i64} {
              obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, subroutine_kind = 1 : i32, semantic_type = !obelisk.void, system_scope_path = "top", system_scope_symbol = @root::@instance::@body, node_id = 110 : i64} {
                obelisk.sv.expression.string_literal attributes {constant_value = "function priority failures %0d", semantic_type = !obelisk.ranged_packed_array<247 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 109 : i64} {}
                obelisk.sv.expression.binary_op attributes {operator_kind = 19 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 89 : i64} {
                  obelisk.sv.expression.binary_op attributes {operator_kind = 14 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 85 : i64} {
                    obelisk.sv.expression.named_value attributes {referenced_path = "top.accepted", referenced_symbol = @root::@instance::@body::@accepted, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 83 : i64} {}
                    obelisk.sv.expression.integer_literal attributes {constant_value = "96", semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 84 : i64} {}
                  }
                  obelisk.sv.expression.binary_op attributes {operator_kind = 16 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 88 : i64} {
                    obelisk.sv.expression.named_value attributes {referenced_path = "top.accepted", referenced_symbol = @root::@instance::@body::@accepted, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 86 : i64} {}
                    obelisk.sv.expression.integer_literal attributes {constant_value = "160", semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 87 : i64} {}
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
