// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines | \
// RUN:   FileCheck %s --check-prefix=NATIVE

// IEEE 1800-2017 Table 20-4 cross-lists every real math function with the C
// library function whose behavior it takes. The trigonometric, hyperbolic, and
// inverse entries are part of that table just as $ln and $sqrt are.

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition @s0.t attributes {definition_kind = 0 : i32, hierarchical_name = "t", name = "t", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    obelisk.sv.symbol.instance @s3.t attributes {hierarchical_name = "t", is_uninstantiated = false, name = "t", node_id = 3 : i64, referenced_path = "t", referenced_symbol = @s0.t} {
      obelisk.sv.symbol.instance_body @s4.t attributes {hierarchical_name = "t", name = "t", node_id = 4 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable @s5.x attributes {hierarchical_name = "t.x", lifetime = 1 : i32, name = "x", node_id = 5 : i64, semantic_type = !obelisk.real} {
        }
        obelisk.sv.symbol.variable @s6.y attributes {hierarchical_name = "t.y", lifetime = 1 : i32, name = "y", node_id = 6 : i64, semantic_type = !obelisk.real} {
        }
        obelisk.sv.symbol.procedural_block @s7 attributes {hierarchical_name = "t", node_id = 7 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 8 : i64} {
            obelisk.sv.statement.expression_statement attributes {node_id = 9 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 10 : i64, semantic_type = !obelisk.real} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 11 : i64, referenced_path = "t.y", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s6.y, semantic_type = !obelisk.real} {
                }
                obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 12 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.real} {
                  obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 13 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.real} {
                    obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 14 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.real} {
                      obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 15 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.real} {
                        obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 16 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.real} {
                          obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 17 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.real} {
                            obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 18 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.real} {
                              obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 19 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.real} {
                                obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 20 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.real} {
                                  obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 21 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.real} {
                                    obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 22 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.real} {
                                      obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$sin", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 23 : i64, semantic_type = !obelisk.real, subroutine_kind = 0 : i32, system_library_cell = "work.t", system_scope_path = "t", system_scope_symbol = @s1.$root::@s3.t::@s4.t} {
                                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 24 : i64, referenced_path = "t.x", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s5.x, semantic_type = !obelisk.real} {
                                        }
                                      }
                                      obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$cos", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 25 : i64, semantic_type = !obelisk.real, subroutine_kind = 0 : i32, system_library_cell = "work.t", system_scope_path = "t", system_scope_symbol = @s1.$root::@s3.t::@s4.t} {
                                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 26 : i64, referenced_path = "t.x", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s5.x, semantic_type = !obelisk.real} {
                                        }
                                      }
                                    }
                                    obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$tan", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 27 : i64, semantic_type = !obelisk.real, subroutine_kind = 0 : i32, system_library_cell = "work.t", system_scope_path = "t", system_scope_symbol = @s1.$root::@s3.t::@s4.t} {
                                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 28 : i64, referenced_path = "t.x", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s5.x, semantic_type = !obelisk.real} {
                                      }
                                    }
                                  }
                                  obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$asin", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 29 : i64, semantic_type = !obelisk.real, subroutine_kind = 0 : i32, system_library_cell = "work.t", system_scope_path = "t", system_scope_symbol = @s1.$root::@s3.t::@s4.t} {
                                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 30 : i64, referenced_path = "t.x", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s5.x, semantic_type = !obelisk.real} {
                                    }
                                  }
                                }
                                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$acos", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 31 : i64, semantic_type = !obelisk.real, subroutine_kind = 0 : i32, system_library_cell = "work.t", system_scope_path = "t", system_scope_symbol = @s1.$root::@s3.t::@s4.t} {
                                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 32 : i64, referenced_path = "t.x", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s5.x, semantic_type = !obelisk.real} {
                                  }
                                }
                              }
                              obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$atan", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 33 : i64, semantic_type = !obelisk.real, subroutine_kind = 0 : i32, system_library_cell = "work.t", system_scope_path = "t", system_scope_symbol = @s1.$root::@s3.t::@s4.t} {
                                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 34 : i64, referenced_path = "t.x", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s5.x, semantic_type = !obelisk.real} {
                                }
                              }
                            }
                            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$sinh", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 35 : i64, semantic_type = !obelisk.real, subroutine_kind = 0 : i32, system_library_cell = "work.t", system_scope_path = "t", system_scope_symbol = @s1.$root::@s3.t::@s4.t} {
                              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 36 : i64, referenced_path = "t.x", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s5.x, semantic_type = !obelisk.real} {
                              }
                            }
                          }
                          obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$cosh", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 37 : i64, semantic_type = !obelisk.real, subroutine_kind = 0 : i32, system_library_cell = "work.t", system_scope_path = "t", system_scope_symbol = @s1.$root::@s3.t::@s4.t} {
                            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 38 : i64, referenced_path = "t.x", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s5.x, semantic_type = !obelisk.real} {
                            }
                          }
                        }
                        obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$tanh", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 39 : i64, semantic_type = !obelisk.real, subroutine_kind = 0 : i32, system_library_cell = "work.t", system_scope_path = "t", system_scope_symbol = @s1.$root::@s3.t::@s4.t} {
                          obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 40 : i64, referenced_path = "t.x", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s5.x, semantic_type = !obelisk.real} {
                          }
                        }
                      }
                      obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$asinh", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 41 : i64, semantic_type = !obelisk.real, subroutine_kind = 0 : i32, system_library_cell = "work.t", system_scope_path = "t", system_scope_symbol = @s1.$root::@s3.t::@s4.t} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 42 : i64, referenced_path = "t.x", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s5.x, semantic_type = !obelisk.real} {
                        }
                      }
                    }
                    obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$acosh", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 43 : i64, semantic_type = !obelisk.real, subroutine_kind = 0 : i32, system_library_cell = "work.t", system_scope_path = "t", system_scope_symbol = @s1.$root::@s3.t::@s4.t} {
                      obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 44 : i64, referenced_path = "t.x", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s5.x, semantic_type = !obelisk.real} {
                      }
                    }
                  }
                  obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "$atanh", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 45 : i64, semantic_type = !obelisk.real, subroutine_kind = 0 : i32, system_library_cell = "work.t", system_scope_path = "t", system_scope_symbol = @s1.$root::@s3.t::@s4.t} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 46 : i64, referenced_path = "t.x", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s5.x, semantic_type = !obelisk.real} {
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


// CHECK-DAG: math.sin
// CHECK-DAG: math.cos
// CHECK-DAG: math.tan
// CHECK-DAG: math.asin
// CHECK-DAG: math.acos
// CHECK-DAG: math.atan
// CHECK-DAG: math.sinh
// CHECK-DAG: math.cosh
// CHECK-DAG: math.tanh
// CHECK-DAG: math.asinh
// CHECK-DAG: math.acosh
// CHECK-DAG: math.atanh

// The inverse hyperbolics have no LLVM intrinsic, so they reach the target
// through the math dialect's expansions rather than as themselves.
// NATIVE-NOT: math.
