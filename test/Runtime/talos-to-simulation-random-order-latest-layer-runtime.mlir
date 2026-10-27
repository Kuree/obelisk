// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' \
// RUN:   '--encode-talos-sim-to-bytecode=vpi=off' \
// RUN:   --convert-talos-sim-processes-to-llvm-coroutines \
// RUN:   | talos-translate --mlir-to-llvmir \
// RUN:   | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %target_cxx %t.o %runtime_link_flags -o %t.exe
// RUN: %t.exe --seed=13 | FileCheck %s --check-prefix=RUNTIME
// RUN: %t.exe --execution-tier=bytecode --seed=13 | FileCheck %s --check-prefix=RUNTIME
// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=3' \
// RUN:   '--encode-talos-sim-to-bytecode=vpi=off' \
// RUN:   --convert-talos-sim-processes-to-llvm-coroutines \
// RUN:   | talos-translate --mlir-to-llvmir \
// RUN:   | %llc -filetype=obj -relocation-model=pic -o %t.opt.o
// RUN: %target_cxx %t.opt.o %runtime_link_flags -o %t.opt.exe
// RUN: %t.opt.exe --seed=13 | FileCheck %s --check-prefix=RUNTIME
// RUN: %t.opt.exe --execution-tier=bytecode --seed=13 | FileCheck %s --check-prefix=RUNTIME
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.5.9: defer partially ordered fields as late as
// possible, and solve unordered fields jointly with the last ordered group.
// In both classes, the final implication has three legal joint values. Its
// antecedent therefore has probability 1/3; choosing it earlier gives 1/2.
// These complete small tables exercise compiler sampling. The separate
// ordered residual test covers exact runtime projections.
// RUNTIME: partial latest 1
// RUNTIME-NEXT: unordered joint 1
// RUNTIME-NEXT: latest valid 1

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  talos.sv.symbol.definition @top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 114 : i64} {}
  talos.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 118 : i64} {
    talos.sv.symbol.compilation_unit @unit attributes {hierarchical_name = "$unit", node_id = 115 : i64} {
      talos.sv.type.class_type @P attributes {bitstream_width = 4 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "P", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "P", semantic_type = !talos.class_handle<@root::@unit::@P>, this_variable_path = "P::this", this_variable_symbol = @root::@unit::@P::@P_this, node_id = 22 : i64} {
        talos.sv.symbol.class_property @P_a attributes {hierarchical_name = "P::a", name = "a", rand_mode = 1 : i32, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>, node_id = 1 : i64} {}
        talos.sv.symbol.class_property @P_b attributes {hierarchical_name = "P::b", name = "b", rand_mode = 1 : i32, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>, node_id = 2 : i64} {}
        talos.sv.symbol.class_property @P_c attributes {hierarchical_name = "P::c", name = "c", rand_mode = 1 : i32, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>, node_id = 3 : i64} {}
        talos.sv.symbol.class_property @P_d attributes {hierarchical_name = "P::d", name = "d", rand_mode = 1 : i32, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>, node_id = 4 : i64} {}
        talos.sv.symbol.variable @P_this attributes {hierarchical_name = "P::this", is_compiler_generated, is_const, name = "this", semantic_type = !talos.class_handle<@root::@unit::@P>, node_id = 5 : i64} {}
        talos.sv.symbol.constraint_block @P_rules attributes {hierarchical_name = "P::rules", name = "rules", node_id = 21 : i64} {
          talos.sv.constraint.list attributes {item_count = 4 : i64, node_id = 20 : i64} {
            talos.sv.constraint.expression attributes {is_soft = false, node_id = 10 : i64} {
              talos.sv.expression.binary_op attributes {operator_kind = 21 : i32, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>, node_id = 9 : i64} {
                talos.sv.expression.named_value attributes {referenced_path = "P::b", referenced_symbol = @root::@unit::@P::@P_b, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>, node_id = 6 : i64} {}
                talos.sv.expression.unary_op attributes {operator_kind = 9 : i32, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>, node_id = 8 : i64} {
                  talos.sv.expression.named_value attributes {referenced_path = "P::d", referenced_symbol = @root::@unit::@P::@P_d, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>, node_id = 7 : i64} {}
                }
              }
            }
            talos.sv.constraint.solve_before attributes {solve_count = 1 : i64, after_count = 1 : i64, node_id = 13 : i64} {
              talos.sv.expression.named_value attributes {referenced_path = "P::a", referenced_symbol = @root::@unit::@P::@P_a, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>, node_id = 11 : i64} {}
              talos.sv.expression.named_value attributes {referenced_path = "P::b", referenced_symbol = @root::@unit::@P::@P_b, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>, node_id = 12 : i64} {}
            }
            talos.sv.constraint.solve_before attributes {solve_count = 1 : i64, after_count = 1 : i64, node_id = 16 : i64} {
              talos.sv.expression.named_value attributes {referenced_path = "P::a", referenced_symbol = @root::@unit::@P::@P_a, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>, node_id = 14 : i64} {}
              talos.sv.expression.named_value attributes {referenced_path = "P::c", referenced_symbol = @root::@unit::@P::@P_c, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>, node_id = 15 : i64} {}
            }
            talos.sv.constraint.solve_before attributes {solve_count = 1 : i64, after_count = 1 : i64, node_id = 19 : i64} {
              talos.sv.expression.named_value attributes {referenced_path = "P::c", referenced_symbol = @root::@unit::@P::@P_c, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>, node_id = 17 : i64} {}
              talos.sv.expression.named_value attributes {referenced_path = "P::d", referenced_symbol = @root::@unit::@P::@P_d, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>, node_id = 18 : i64} {}
            }
          }
        }
      }
      talos.sv.type.class_type @U attributes {bitstream_width = 3 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "U", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "U", semantic_type = !talos.class_handle<@root::@unit::@U>, this_variable_path = "U::this", this_variable_symbol = @root::@unit::@U::@U_this, node_id = 37 : i64} {
        talos.sv.symbol.class_property @U_a attributes {hierarchical_name = "U::a", name = "a", rand_mode = 1 : i32, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>, node_id = 23 : i64} {}
        talos.sv.symbol.class_property @U_b attributes {hierarchical_name = "U::b", name = "b", rand_mode = 1 : i32, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>, node_id = 24 : i64} {}
        talos.sv.symbol.class_property @U_c attributes {hierarchical_name = "U::c", name = "c", rand_mode = 1 : i32, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>, node_id = 25 : i64} {}
        talos.sv.symbol.variable @U_this attributes {hierarchical_name = "U::this", is_compiler_generated, is_const, name = "this", semantic_type = !talos.class_handle<@root::@unit::@U>, node_id = 26 : i64} {}
        talos.sv.symbol.constraint_block @U_rules attributes {hierarchical_name = "U::rules", name = "rules", node_id = 36 : i64} {
          talos.sv.constraint.list attributes {item_count = 2 : i64, node_id = 35 : i64} {
            talos.sv.constraint.expression attributes {is_soft = false, node_id = 31 : i64} {
              talos.sv.expression.binary_op attributes {operator_kind = 21 : i32, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>, node_id = 30 : i64} {
                talos.sv.expression.named_value attributes {referenced_path = "U::b", referenced_symbol = @root::@unit::@U::@U_b, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>, node_id = 27 : i64} {}
                talos.sv.expression.unary_op attributes {operator_kind = 9 : i32, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>, node_id = 29 : i64} {
                  talos.sv.expression.named_value attributes {referenced_path = "U::c", referenced_symbol = @root::@unit::@U::@U_c, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>, node_id = 28 : i64} {}
                }
              }
            }
            talos.sv.constraint.solve_before attributes {solve_count = 1 : i64, after_count = 1 : i64, node_id = 34 : i64} {
              talos.sv.expression.named_value attributes {referenced_path = "U::a", referenced_symbol = @root::@unit::@U::@U_a, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>, node_id = 32 : i64} {}
              talos.sv.expression.named_value attributes {referenced_path = "U::b", referenced_symbol = @root::@unit::@U::@U_b, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>, node_id = 33 : i64} {}
            }
          }
        }
      }
    }
    talos.sv.symbol.instance @instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", referenced_path = "top", referenced_symbol = @top, node_id = 117 : i64} {
      talos.sv.symbol.instance_body @body attributes {hierarchical_name = "top", name = "top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 116 : i64} {
        talos.sv.symbol.variable @valid attributes {hierarchical_name = "top.valid", lifetime = 1 : i32, name = "valid", semantic_type = !talos.integral<1, false, false, 0 : 0, bit>, node_id = 39 : i64} {
          talos.sv.expression.integer_literal attributes {constant_value = "1", semantic_type = !talos.integral<1, false, false, 0 : 0, bit>, node_id = 38 : i64} {}
        }
        talos.sv.symbol.variable @objectP attributes {hierarchical_name = "top.objectP", lifetime = 1 : i32, name = "objectP", semantic_type = !talos.class_handle<@root::@unit::@P>, node_id = 41 : i64} {
          talos.sv.expression.new_class attributes {is_super_class = false, semantic_type = !talos.class_handle<@root::@unit::@P>, node_id = 40 : i64} {}
        }
        talos.sv.symbol.variable @countP attributes {hierarchical_name = "top.countP", lifetime = 1 : i32, name = "countP", semantic_type = !talos.ranged_packed_array<31 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>, node_id = 43 : i64} {
          talos.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !talos.ranged_packed_array<31 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>, node_id = 42 : i64} {}
        }
        talos.sv.symbol.variable @objectU attributes {hierarchical_name = "top.objectU", lifetime = 1 : i32, name = "objectU", semantic_type = !talos.class_handle<@root::@unit::@U>, node_id = 75 : i64} {
          talos.sv.expression.new_class attributes {is_super_class = false, semantic_type = !talos.class_handle<@root::@unit::@U>, node_id = 74 : i64} {}
        }
        talos.sv.symbol.variable @countU attributes {hierarchical_name = "top.countU", lifetime = 1 : i32, name = "countU", semantic_type = !talos.ranged_packed_array<31 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>, node_id = 77 : i64} {
          talos.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !talos.ranged_packed_array<31 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>, node_id = 76 : i64} {}
        }
        talos.sv.symbol.procedural_block @initial attributes {hierarchical_name = "top", procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 113 : i64} {
          talos.sv.statement.list attributes {node_id = 112 : i64} {
            talos.sv.statement.repeat_loop attributes {node_id = 63 : i64} {
              talos.sv.expression.integer_literal attributes {constant_value = "1536", semantic_type = !talos.integral<32, true, false, 31 : 0, int>, node_id = 61 : i64} {}
              talos.sv.statement.list attributes {node_id = 62 : i64} {
                talos.sv.statement.expression_statement attributes {node_id = 55 : i64} {
                  talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>, node_id = 54 : i64} {
                    talos.sv.expression.named_value attributes {referenced_path = "top.valid", referenced_symbol = @root::@instance::@body::@valid, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>, node_id = 47 : i64} {}
                    talos.sv.expression.binary_op attributes {operator_kind = 19 : i32, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>, node_id = 53 : i64} {
                      talos.sv.expression.named_value attributes {referenced_path = "top.valid", referenced_symbol = @root::@instance::@body::@valid, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>, node_id = 48 : i64} {}
                      talos.sv.expression.binary_op attributes {operator_kind = 9 : i32, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>, node_id = 52 : i64} {
                        talos.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, subroutine_kind = 0 : i32, semantic_type = !talos.integral<32, true, false, 31 : 0, int>, system_scope_path = "top", system_scope_symbol = @root::@instance::@body, node_id = 50 : i64} {
                          talos.sv.expression.named_value attributes {referenced_path = "top.objectP", referenced_symbol = @root::@instance::@body::@objectP, semantic_type = !talos.class_handle<@root::@unit::@P>, node_id = 49 : i64} {}
                        }
                        talos.sv.expression.integer_literal attributes {constant_value = "1", semantic_type = !talos.integral<32, true, false, 31 : 0, int>, node_id = 51 : i64} {}
                      }
                    }
                  }
                }
                talos.sv.statement.expression_statement attributes {node_id = 60 : i64} {
                  talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !talos.ranged_packed_array<31 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>, node_id = 59 : i64} {
                    talos.sv.expression.named_value attributes {referenced_path = "top.countP", referenced_symbol = @root::@instance::@body::@countP, semantic_type = !talos.ranged_packed_array<31 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>, node_id = 56 : i64} {}
                    talos.sv.expression.binary_op attributes {operator_kind = 0 : i32, semantic_type = !talos.ranged_packed_array<31 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>, node_id = 58 : i64} {
                      talos.sv.expression.named_value attributes {referenced_path = "top.countP", referenced_symbol = @root::@instance::@body::@countP, semantic_type = !talos.ranged_packed_array<31 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>, node_id = 57 : i64} {}
                      talos.sv.expression.conversion attributes {conversion_kind = 0 : i32, semantic_type = !talos.ranged_packed_array<31 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>, node_id = 46 : i64} {
                        talos.sv.expression.member_access attributes {referenced_path = "P::b", referenced_symbol = @root::@unit::@P::@P_b, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>, node_id = 45 : i64} {
                          talos.sv.expression.named_value attributes {referenced_path = "top.objectP", referenced_symbol = @root::@instance::@body::@objectP, semantic_type = !talos.class_handle<@root::@unit::@P>, node_id = 44 : i64} {}
                        }
                      }
                    }
                  }
                }
              }
            }
            talos.sv.statement.expression_statement attributes {node_id = 73 : i64} {
              talos.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, subroutine_kind = 1 : i32, semantic_type = !talos.void, system_scope_path = "top", system_scope_symbol = @root::@instance::@body, node_id = 72 : i64} {
                talos.sv.expression.string_literal attributes {constant_value = "partial latest %0d", semantic_type = !talos.ranged_packed_array<151 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>, node_id = 71 : i64} {}
                talos.sv.expression.binary_op attributes {operator_kind = 19 : i32, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>, node_id = 70 : i64} {
                  talos.sv.expression.binary_op attributes {operator_kind = 14 : i32, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>, node_id = 66 : i64} {
                    talos.sv.expression.named_value attributes {referenced_path = "top.countP", referenced_symbol = @root::@instance::@body::@countP, semantic_type = !talos.ranged_packed_array<31 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>, node_id = 64 : i64} {}
                    talos.sv.expression.integer_literal attributes {constant_value = "400", semantic_type = !talos.ranged_packed_array<31 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>, node_id = 65 : i64} {}
                  }
                  talos.sv.expression.binary_op attributes {operator_kind = 16 : i32, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>, node_id = 69 : i64} {
                    talos.sv.expression.named_value attributes {referenced_path = "top.countP", referenced_symbol = @root::@instance::@body::@countP, semantic_type = !talos.ranged_packed_array<31 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>, node_id = 67 : i64} {}
                    talos.sv.expression.integer_literal attributes {constant_value = "624", semantic_type = !talos.ranged_packed_array<31 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>, node_id = 68 : i64} {}
                  }
                }
              }
            }
            talos.sv.statement.repeat_loop attributes {node_id = 97 : i64} {
              talos.sv.expression.integer_literal attributes {constant_value = "1536", semantic_type = !talos.integral<32, true, false, 31 : 0, int>, node_id = 95 : i64} {}
              talos.sv.statement.list attributes {node_id = 96 : i64} {
                talos.sv.statement.expression_statement attributes {node_id = 89 : i64} {
                  talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>, node_id = 88 : i64} {
                    talos.sv.expression.named_value attributes {referenced_path = "top.valid", referenced_symbol = @root::@instance::@body::@valid, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>, node_id = 81 : i64} {}
                    talos.sv.expression.binary_op attributes {operator_kind = 19 : i32, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>, node_id = 87 : i64} {
                      talos.sv.expression.named_value attributes {referenced_path = "top.valid", referenced_symbol = @root::@instance::@body::@valid, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>, node_id = 82 : i64} {}
                      talos.sv.expression.binary_op attributes {operator_kind = 9 : i32, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>, node_id = 86 : i64} {
                        talos.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, subroutine_kind = 0 : i32, semantic_type = !talos.integral<32, true, false, 31 : 0, int>, system_scope_path = "top", system_scope_symbol = @root::@instance::@body, node_id = 84 : i64} {
                          talos.sv.expression.named_value attributes {referenced_path = "top.objectU", referenced_symbol = @root::@instance::@body::@objectU, semantic_type = !talos.class_handle<@root::@unit::@U>, node_id = 83 : i64} {}
                        }
                        talos.sv.expression.integer_literal attributes {constant_value = "1", semantic_type = !talos.integral<32, true, false, 31 : 0, int>, node_id = 85 : i64} {}
                      }
                    }
                  }
                }
                talos.sv.statement.expression_statement attributes {node_id = 94 : i64} {
                  talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !talos.ranged_packed_array<31 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>, node_id = 93 : i64} {
                    talos.sv.expression.named_value attributes {referenced_path = "top.countU", referenced_symbol = @root::@instance::@body::@countU, semantic_type = !talos.ranged_packed_array<31 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>, node_id = 90 : i64} {}
                    talos.sv.expression.binary_op attributes {operator_kind = 0 : i32, semantic_type = !talos.ranged_packed_array<31 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>, node_id = 92 : i64} {
                      talos.sv.expression.named_value attributes {referenced_path = "top.countU", referenced_symbol = @root::@instance::@body::@countU, semantic_type = !talos.ranged_packed_array<31 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>, node_id = 91 : i64} {}
                      talos.sv.expression.conversion attributes {conversion_kind = 0 : i32, semantic_type = !talos.ranged_packed_array<31 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>, node_id = 80 : i64} {
                        talos.sv.expression.member_access attributes {referenced_path = "U::b", referenced_symbol = @root::@unit::@U::@U_b, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>, node_id = 79 : i64} {
                          talos.sv.expression.named_value attributes {referenced_path = "top.objectU", referenced_symbol = @root::@instance::@body::@objectU, semantic_type = !talos.class_handle<@root::@unit::@U>, node_id = 78 : i64} {}
                        }
                      }
                    }
                  }
                }
              }
            }
            talos.sv.statement.expression_statement attributes {node_id = 107 : i64} {
              talos.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, subroutine_kind = 1 : i32, semantic_type = !talos.void, system_scope_path = "top", system_scope_symbol = @root::@instance::@body, node_id = 106 : i64} {
                talos.sv.expression.string_literal attributes {constant_value = "unordered joint %0d", semantic_type = !talos.ranged_packed_array<159 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>, node_id = 105 : i64} {}
                talos.sv.expression.binary_op attributes {operator_kind = 19 : i32, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>, node_id = 104 : i64} {
                  talos.sv.expression.binary_op attributes {operator_kind = 14 : i32, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>, node_id = 100 : i64} {
                    talos.sv.expression.named_value attributes {referenced_path = "top.countU", referenced_symbol = @root::@instance::@body::@countU, semantic_type = !talos.ranged_packed_array<31 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>, node_id = 98 : i64} {}
                    talos.sv.expression.integer_literal attributes {constant_value = "400", semantic_type = !talos.ranged_packed_array<31 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>, node_id = 99 : i64} {}
                  }
                  talos.sv.expression.binary_op attributes {operator_kind = 16 : i32, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>, node_id = 103 : i64} {
                    talos.sv.expression.named_value attributes {referenced_path = "top.countU", referenced_symbol = @root::@instance::@body::@countU, semantic_type = !talos.ranged_packed_array<31 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>, node_id = 101 : i64} {}
                    talos.sv.expression.integer_literal attributes {constant_value = "624", semantic_type = !talos.ranged_packed_array<31 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>, node_id = 102 : i64} {}
                  }
                }
              }
            }
            talos.sv.statement.expression_statement attributes {node_id = 111 : i64} {
              talos.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, subroutine_kind = 1 : i32, semantic_type = !talos.void, system_scope_path = "top", system_scope_symbol = @root::@instance::@body, node_id = 110 : i64} {
                talos.sv.expression.string_literal attributes {constant_value = "latest valid %0d", semantic_type = !talos.ranged_packed_array<135 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>, node_id = 109 : i64} {}
                talos.sv.expression.named_value attributes {referenced_path = "top.valid", referenced_symbol = @root::@instance::@body::@valid, semantic_type = !talos.integral<1, false, false, 0 : 0, bit>, node_id = 108 : i64} {}
              }
            }
          }
        }
      }
    }
  }
}
