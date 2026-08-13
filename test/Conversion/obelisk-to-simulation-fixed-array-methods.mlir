// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' --verify-each --mlir-print-op-generic > /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | obelisk-opt --emit-bytecode -o /dev/null

// IEEE 1800-2017 7.12.1 and 7.12.3 define reductions and locator
// methods for every unpacked array.  A fixed array is traversed from its
// left bound, so find_index on [4:0] reports 4, 3, ..., 0.
module {
  obelisk.sv.symbol.definition attributes {
    definition_kind = 0 : i32,
    hierarchical_name = "fixed_array_methods",
    name = "fixed_array_methods",
    node_id = 0 : i64,
    sym_name = "s0.fixed_array_methods"
  } {
  }
  obelisk.sv.symbol.root attributes {
    hierarchical_name = "\\$root ",
    name = "$root",
    node_id = 1 : i64,
    sym_name = "s1.$root"
  } {
    obelisk.sv.symbol.compilation_unit attributes {
      hierarchical_name = "$unit",
      node_id = 2 : i64,
      sym_name = "s2"
    } {
    }
    obelisk.sv.symbol.instance attributes {
      hierarchical_name = "fixed_array_methods",
      is_uninstantiated = false,
      name = "fixed_array_methods",
      node_id = 3 : i64,
      referenced_path = "fixed_array_methods",
      referenced_symbol = @s0.fixed_array_methods,
      sym_name = "s3.fixed_array_methods"
    } {
      obelisk.sv.symbol.instance_body attributes {
        hierarchical_name = "fixed_array_methods",
        name = "fixed_array_methods",
        node_id = 4 : i64,
        sym_name = "s4.fixed_array_methods"
      } {
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "fixed_array_methods.runtime",
          lifetime = 1 : i32,
          name = "runtime",
          node_id = 5 : i64,
          semantic_type = !obelisk.ranged_unpacked_array<4 : 0 x !obelisk.integral<32, true, false, 31 : 0, int>>,
          sym_name = "s5.runtime"
        } {
        }
        obelisk.sv.symbol.parameter attributes {
          constant_value = "[3,9,-1,9,2]",
          hierarchical_name = "fixed_array_methods.CONSTANTS",
          name = "CONSTANTS",
          node_id = 6 : i64,
          semantic_type = !obelisk.ranged_unpacked_array<4 : 0 x !obelisk.integral<32, true, false, 31 : 0, int>>,
          sym_name = "s6.CONSTANTS"
        } {
        }
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "fixed_array_methods.runtime_max",
          lifetime = 1 : i32,
          name = "runtime_max",
          node_id = 7 : i64,
          semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>,
          sym_name = "s7.runtime_max"
        } {
        }
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "fixed_array_methods.indices",
          lifetime = 1 : i32,
          name = "indices",
          node_id = 8 : i64,
          semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>,
          sym_name = "s8.indices"
        } {
        }
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "fixed_array_methods.constant_max",
          lifetime = 1 : i32,
          name = "constant_max",
          node_id = 9 : i64,
          semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>,
          sym_name = "s9.constant_max"
        } {
        }
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "fixed_array_methods.constant_sum",
          lifetime = 1 : i32,
          name = "constant_sum",
          node_id = 10 : i64,
          semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
          sym_name = "s10.constant_sum"
        } {
        }
        obelisk.sv.symbol.procedural_block attributes {
          hierarchical_name = "fixed_array_methods",
          node_id = 11 : i64,
          procedure_kind = 0 : i32,
          sym_name = "s11",
          time_precision_fs = 1000000 : i64,
          time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.list attributes {node_id = 12 : i64} {
            obelisk.sv.statement.expression_statement attributes {node_id = 13 : i64} {
              obelisk.sv.expression.assignment attributes {
                assignment_kind = 0 : i32,
                node_id = 14 : i64,
                semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>
              } {
                obelisk.sv.expression.named_value attributes {
                  node_id = 15 : i64,
                  referenced_path = "fixed_array_methods.runtime_max",
                  referenced_symbol = @s1.$root::@s3.fixed_array_methods::@s4.fixed_array_methods::@s7.runtime_max,
                  semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>
                } {
                }
                obelisk.sv.expression.call attributes {
                  argument_count = 1 : i64,
                  callee_name = "max",
                  constraint_restrictions = [],
                  has_inline_constraints = false,
                  has_iterator_expression = false,
                  has_output_arguments = false,
                  has_this_class = false,
                  is_super_class = false,
                  is_system_call = true,
                  node_id = 16 : i64,
                  semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>,
                  subroutine_kind = 0 : i32,
                  system_library_cell = "work.fixed_array_methods",
                  system_scope_path = "fixed_array_methods",
                  system_scope_symbol = @s1.$root::@s3.fixed_array_methods::@s4.fixed_array_methods
                } {
                  obelisk.sv.expression.named_value attributes {
                    node_id = 17 : i64,
                    referenced_path = "fixed_array_methods.runtime",
                    referenced_symbol = @s1.$root::@s3.fixed_array_methods::@s4.fixed_array_methods::@s5.runtime,
                    semantic_type = !obelisk.ranged_unpacked_array<4 : 0 x !obelisk.integral<32, true, false, 31 : 0, int>>
                  } {
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 18 : i64} {
              obelisk.sv.expression.assignment attributes {
                assignment_kind = 0 : i32,
                node_id = 19 : i64,
                semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>
              } {
                obelisk.sv.expression.named_value attributes {
                  node_id = 20 : i64,
                  referenced_path = "fixed_array_methods.indices",
                  referenced_symbol = @s1.$root::@s3.fixed_array_methods::@s4.fixed_array_methods::@s8.indices,
                  semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>
                } {
                }
                obelisk.sv.expression.call attributes {
                  argument_count = 1 : i64,
                  callee_name = "find_index",
                  constraint_restrictions = [],
                  has_inline_constraints = false,
                  has_iterator_expression = true,
                  has_output_arguments = false,
                  has_this_class = false,
                  is_super_class = false,
                  is_system_call = true,
                  iterator_variable_path = "fixed_array_methods.item",
                  iterator_variable_symbol = @s1.$root::@s3.fixed_array_methods::@s4.fixed_array_methods::@s29.item,
                  node_id = 21 : i64,
                  semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>,
                  subroutine_kind = 0 : i32,
                  system_library_cell = "work.fixed_array_methods",
                  system_scope_path = "fixed_array_methods",
                  system_scope_symbol = @s1.$root::@s3.fixed_array_methods::@s4.fixed_array_methods
                } {
                  obelisk.sv.expression.binary_op attributes {
                    is_signed = false,
                    node_id = 22 : i64,
                    operator_kind = 14 : i32,
                    semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
                  } {
                    obelisk.sv.expression.named_value attributes {
                      is_signed = true,
                      node_id = 23 : i64,
                      referenced_path = "fixed_array_methods.item",
                      referenced_symbol = @s1.$root::@s3.fixed_array_methods::@s4.fixed_array_methods::@s29.item,
                      semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                    } {
                    }
                    obelisk.sv.expression.integer_literal attributes {
                      constant_value = "0",
                      is_signed = true,
                      node_id = 24 : i64,
                      semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                    } {
                    }
                  }
                  obelisk.sv.expression.named_value attributes {
                    node_id = 25 : i64,
                    referenced_path = "fixed_array_methods.runtime",
                    referenced_symbol = @s1.$root::@s3.fixed_array_methods::@s4.fixed_array_methods::@s5.runtime,
                    semantic_type = !obelisk.ranged_unpacked_array<4 : 0 x !obelisk.integral<32, true, false, 31 : 0, int>>
                  } {
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 30 : i64} {
              obelisk.sv.expression.assignment attributes {
                assignment_kind = 0 : i32,
                node_id = 31 : i64,
                semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>
              } {
                obelisk.sv.expression.named_value attributes {
                  node_id = 32 : i64,
                  referenced_path = "fixed_array_methods.constant_max",
                  referenced_symbol = @s1.$root::@s3.fixed_array_methods::@s4.fixed_array_methods::@s9.constant_max,
                  semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>
                } {
                }
                obelisk.sv.expression.call attributes {
                  argument_count = 1 : i64,
                  callee_name = "max",
                  constraint_restrictions = [],
                  has_inline_constraints = false,
                  has_iterator_expression = false,
                  has_output_arguments = false,
                  has_this_class = false,
                  is_super_class = false,
                  is_system_call = true,
                  node_id = 33 : i64,
                  semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>,
                  subroutine_kind = 0 : i32,
                  system_library_cell = "work.fixed_array_methods",
                  system_scope_path = "fixed_array_methods",
                  system_scope_symbol = @s1.$root::@s3.fixed_array_methods::@s4.fixed_array_methods
                } {
                  obelisk.sv.expression.named_value attributes {
                    node_id = 34 : i64,
                    referenced_path = "fixed_array_methods.CONSTANTS",
                    referenced_symbol = @s1.$root::@s3.fixed_array_methods::@s4.fixed_array_methods::@s6.CONSTANTS,
                    semantic_type = !obelisk.ranged_unpacked_array<4 : 0 x !obelisk.integral<32, true, false, 31 : 0, int>>
                  } {
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 35 : i64} {
              obelisk.sv.expression.assignment attributes {
                assignment_kind = 0 : i32,
                is_signed = true,
                node_id = 36 : i64,
                semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
              } {
                obelisk.sv.expression.named_value attributes {
                  is_signed = true,
                  node_id = 37 : i64,
                  referenced_path = "fixed_array_methods.constant_sum",
                  referenced_symbol = @s1.$root::@s3.fixed_array_methods::@s4.fixed_array_methods::@s10.constant_sum,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                } {
                }
                obelisk.sv.expression.call attributes {
                  argument_count = 1 : i64,
                  callee_name = "sum",
                  constraint_restrictions = [],
                  has_inline_constraints = false,
                  has_iterator_expression = false,
                  has_output_arguments = false,
                  has_this_class = false,
                  is_signed = true,
                  is_super_class = false,
                  is_system_call = true,
                  node_id = 38 : i64,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
                  subroutine_kind = 0 : i32,
                  system_library_cell = "work.fixed_array_methods",
                  system_scope_path = "fixed_array_methods",
                  system_scope_symbol = @s1.$root::@s3.fixed_array_methods::@s4.fixed_array_methods
                } {
                  obelisk.sv.expression.named_value attributes {
                    node_id = 39 : i64,
                    referenced_path = "fixed_array_methods.CONSTANTS",
                    referenced_symbol = @s1.$root::@s3.fixed_array_methods::@s4.fixed_array_methods::@s6.CONSTANTS,
                    semantic_type = !obelisk.ranged_unpacked_array<4 : 0 x !obelisk.integral<32, true, false, 31 : 0, int>>
                  } {
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.iterator attributes {
          array_type = !obelisk.ranged_unpacked_array<4 : 0 x !obelisk.integral<32, true, false, 31 : 0, int>>,
          hierarchical_name = "fixed_array_methods.item",
          index_method_name = "index",
          is_const,
          name = "item",
          node_id = 29 : i64,
          semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
          sym_name = "s29.item"
        } {
        }
      }
    }
  }
}

// Runtime max is unrolled through fixed aggregate positions and compares signed
// int elements.  No container-size loop is needed for the fixed extent.
// CHECK: obelisk_sim.ref.subelement {{.*}}{{\[\[0\]\]}}
// CHECK: obelisk_sim.ref.subelement {{.*}}{{\[\[4\]\]}}
// CHECK-COUNT-4: arith.cmpi sgt

// find_index keeps a loop, dynamically extracts by the declared [4:0] index,
// and writes that source index to the result queue.
// CHECK: %[[LEFT:.*]] = arith.constant {{.*}}4 : i64
// CHECK: %[[SOURCE_INDEX:.*]] = arith.subi %[[LEFT]], %{{.*}} : i64
// CHECK: obelisk_sim.array.extract_dynamic {{.*}}[%[[SOURCE_INDEX]]]
// CHECK: %[[INDEX_I32:.*]] = arith.trunci %[[SOURCE_INDEX]] : i64 to i32
// CHECK: obelisk_sim.container.write {{.*}}, {{.*}}, %[[INDEX_I32]]

// The lowering emits aggregate.extract + arith SSA for fixed constants.  The
// dialect canonicalizer and arith folders, not array-method lowering, reduce
// max and sum to their values.
// CHECK: %[[NINE:.*]] = arith.constant {{.*}}9 : i32
// CHECK: obelisk_sim.container.write {{.*}}, {{.*}}, %[[NINE]]
// CHECK: %[[TWENTY_TWO:.*]] = arith.constant {{.*}}22 : i32
// CHECK: obelisk_sim.ref.store %[[TWENTY_TWO]]
