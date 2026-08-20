// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null

module attributes {
  llvm.data_layout = "e-p:64:64-i64:64-i32:32-i16:16-i8:8",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "swrite_test", name = "swrite_test", node_id = 0 : i64, sym_name = "s0.swrite_test"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "swrite_test", is_uninstantiated = false, name = "swrite_test", node_id = 3 : i64, referenced_path = "swrite_test", referenced_symbol = @s0.swrite_test, sym_name = "s3.swrite_test"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "swrite_test", name = "swrite_test", node_id = 4 : i64, sym_name = "s4.swrite_test", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "swrite_test.text", lifetime = 1 : i32, name = "text", node_id = 5 : i64, semantic_type = !obelisk.string, sym_name = "s5.text"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "swrite_test.bits", lifetime = 1 : i32, name = "bits", node_id = 6 : i64, semantic_type = !obelisk.ranged_packed_array<127 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s6.bits"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "swrite_test.bytes", lifetime = 1 : i32, name = "bytes", node_id = 7 : i64, semantic_type = !obelisk.ranged_unpacked_array<0 : 15 x !obelisk.integral<8, true, false, 7 : 0, byte>>, sym_name = "s7.bytes"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "swrite_test.value", lifetime = 1 : i32, name = "value", node_id = 8 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s8.value"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "swrite_test", node_id = 9 : i64, procedure_kind = 0 : i32, sym_name = "s9", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 10 : i64} {
            obelisk.sv.statement.list attributes {node_id = 11 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 12 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 3 : i64, callee_name = "$swrite", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 13 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.swrite_test", system_scope_path = "swrite_test", system_scope_symbol = @s1.$root::@s3.swrite_test::@s4.swrite_test} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 14 : i64, semantic_type = !obelisk.string} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 15 : i64, referenced_path = "swrite_test.text", referenced_symbol = @s1.$root::@s3.swrite_test::@s4.swrite_test::@s5.text, semantic_type = !obelisk.string} {
                    }
                    obelisk.sv.expression.empty_argument attributes {is_signed = false, node_id = 16 : i64, semantic_type = !obelisk.string} {
                    }
                  }
                  obelisk.sv.expression.string_literal attributes {constant_value = "%m value=%0d", is_signed = false, node_id = 17 : i64, semantic_type = !obelisk.ranged_packed_array<95 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 18 : i64, referenced_path = "swrite_test.value", referenced_symbol = @s1.$root::@s3.swrite_test::@s4.swrite_test::@s8.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 19 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 4 : i64, callee_name = "$swriteh", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 20 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.swrite_test", system_scope_path = "swrite_test", system_scope_symbol = @s1.$root::@s3.swrite_test::@s4.swrite_test} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 21 : i64, semantic_type = !obelisk.ranged_packed_array<127 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 22 : i64, referenced_path = "swrite_test.bits", referenced_symbol = @s1.$root::@s3.swrite_test::@s4.swrite_test::@s6.bits, semantic_type = !obelisk.ranged_packed_array<127 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.empty_argument attributes {is_signed = false, node_id = 23 : i64, semantic_type = !obelisk.ranged_packed_array<127 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 24 : i64, referenced_path = "swrite_test.value", referenced_symbol = @s1.$root::@s3.swrite_test::@s4.swrite_test::@s8.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.empty_argument attributes {is_signed = false, node_id = 25 : i64, semantic_type = !obelisk.void} {
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 26 : i64, referenced_path = "swrite_test.text", referenced_symbol = @s1.$root::@s3.swrite_test::@s4.swrite_test::@s5.text, semantic_type = !obelisk.string} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 27 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 3 : i64, callee_name = "$swrite", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 28 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.swrite_test", system_scope_path = "swrite_test", system_scope_symbol = @s1.$root::@s3.swrite_test::@s4.swrite_test} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 29 : i64, semantic_type = !obelisk.ranged_unpacked_array<0 : 15 x !obelisk.integral<8, true, false, 7 : 0, byte>>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 30 : i64, referenced_path = "swrite_test.bytes", referenced_symbol = @s1.$root::@s3.swrite_test::@s4.swrite_test::@s7.bytes, semantic_type = !obelisk.ranged_unpacked_array<0 : 15 x !obelisk.integral<8, true, false, 7 : 0, byte>>} {
                    }
                    obelisk.sv.expression.empty_argument attributes {is_signed = false, node_id = 31 : i64, semantic_type = !obelisk.ranged_unpacked_array<0 : 15 x !obelisk.integral<8, true, false, 7 : 0, byte>>} {
                    }
                  }
                  obelisk.sv.expression.string_literal attributes {constant_value = "%0s", is_signed = false, node_id = 32 : i64, semantic_type = !obelisk.ranged_packed_array<23 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 33 : i64, referenced_path = "swrite_test.bits", referenced_symbol = @s1.$root::@s3.swrite_test::@s4.swrite_test::@s6.bits, semantic_type = !obelisk.ranged_packed_array<127 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 34 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 3 : i64, callee_name = "$swrite", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 35 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.swrite_test", system_scope_path = "swrite_test", system_scope_symbol = @s1.$root::@s3.swrite_test::@s4.swrite_test} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 36 : i64, semantic_type = !obelisk.string} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 37 : i64, referenced_path = "swrite_test.text", referenced_symbol = @s1.$root::@s3.swrite_test::@s4.swrite_test::@s5.text, semantic_type = !obelisk.string} {
                    }
                    obelisk.sv.expression.empty_argument attributes {is_signed = false, node_id = 38 : i64, semantic_type = !obelisk.string} {
                    }
                  }
                  obelisk.sv.expression.string_literal attributes {constant_value = "%s", is_signed = false, node_id = 39 : i64, semantic_type = !obelisk.ranged_packed_array<15 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 40 : i64, referenced_path = "swrite_test.bytes", referenced_symbol = @s1.$root::@s3.swrite_test::@s4.swrite_test::@s7.bytes, semantic_type = !obelisk.ranged_unpacked_array<0 : 15 x !obelisk.integral<8, true, false, 7 : 0, byte>>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 41 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 4 : i64, callee_name = "$sformat", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 42 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.swrite_test", system_scope_path = "swrite_test", system_scope_symbol = @s1.$root::@s3.swrite_test::@s4.swrite_test} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 43 : i64, semantic_type = !obelisk.string} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 44 : i64, referenced_path = "swrite_test.text", referenced_symbol = @s1.$root::@s3.swrite_test::@s4.swrite_test::@s5.text, semantic_type = !obelisk.string} {
                    }
                    obelisk.sv.expression.empty_argument attributes {is_signed = false, node_id = 45 : i64, semantic_type = !obelisk.string} {
                    }
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 46 : i64, referenced_path = "swrite_test.text", referenced_symbol = @s1.$root::@s3.swrite_test::@s4.swrite_test::@s5.text, semantic_type = !obelisk.string} {
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 47 : i64, referenced_path = "swrite_test.value", referenced_symbol = @s1.$root::@s3.swrite_test::@s4.swrite_test::@s8.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.string_literal attributes {constant_value = "", is_signed = false, node_id = 48 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 49 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 3 : i64, callee_name = "$sformat", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 50 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.swrite_test", system_scope_path = "swrite_test", system_scope_symbol = @s1.$root::@s3.swrite_test::@s4.swrite_test} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 51 : i64, semantic_type = !obelisk.string} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 52 : i64, referenced_path = "swrite_test.text", referenced_symbol = @s1.$root::@s3.swrite_test::@s4.swrite_test::@s5.text, semantic_type = !obelisk.string} {
                    }
                    obelisk.sv.expression.empty_argument attributes {is_signed = false, node_id = 53 : i64, semantic_type = !obelisk.string} {
                    }
                  }
                  obelisk.sv.expression.string_literal attributes {constant_value = "%d", is_signed = false, node_id = 54 : i64, semantic_type = !obelisk.ranged_packed_array<15 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 55 : i64, referenced_path = "swrite_test.value", referenced_symbol = @s1.$root::@s3.swrite_test::@s4.swrite_test::@s8.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
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

// CHECK-DAG: %[[EMPTY_LITERAL:.*]] = arith.constant 0 : i8
// CHECK: %[[DECIMAL:.*]] = obelisk_sim.string.output_format {{.*}} radix = 10 flags = [0, 1] {library_cell = "work.swrite_test", scope = "swrite_test"
// CHECK-NEXT: obelisk_sim.ref.store %[[DECIMAL]]
// CHECK: %[[HEX:.*]] = obelisk_sim.string.output_format {{.*}} radix = 16 flags = [1, 2, 136] {library_cell = "work.swrite_test", scope = "swrite_test"
// CHECK-NEXT: %[[PACKED:.*]] = obelisk_sim.string.to_packed %[[HEX]] : (!obelisk_sim.string) -> i128
// CHECK: %[[BYTES:.*]] = obelisk_sim.string.output_format {{.*}} radix = 10 flags = [0, 0] {library_cell = "work.swrite_test", scope = "swrite_test"
// CHECK: %[[BYTE0:.*]] = obelisk_sim.string.getc %[[BYTES]], {{.*}} : (!obelisk_sim.string, i64) -> i8
// CHECK: obelisk_sim.aggregate.construct %[[BYTE0]], {{.*}} -> !obelisk_sim.unpacked_array<0 : 15 x i8>
// CHECK: %[[ARRAY_REF0:.*]] = obelisk_sim.ref.subelement {{.*}}{{\[\[0\]\]}}
// CHECK-NEXT: %[[ARRAY_BYTE0:.*]] = obelisk_sim.ref.load %[[ARRAY_REF0]] : !obelisk_sim.ref<i8> -> i8
// CHECK: %[[ARRAY_LOGIC0:.*]] = obelisk_sim.logic.from_bits %[[ARRAY_BYTE0]] : i8 -> !obelisk_sim.logic<8>
// CHECK: %[[ARRAY_PACKED:.*]] = obelisk_sim.logic.concat %[[ARRAY_LOGIC0]], {{.*}} -> !obelisk_sim.logic<128>
// CHECK: %[[ARRAY_STRING:.*]] = obelisk_sim.string.from_packed %[[ARRAY_PACKED]] : (!obelisk_sim.logic<128>) -> !obelisk_sim.string
// CHECK: obelisk_sim.string.output_format {{.*}}(%{{.*}}, %[[ARRAY_STRING]]) radix = 10 flags = [0, 8]
// CHECK: %[[DYNAMIC_FORMAT:.*]] = obelisk_sim.ref.load {{.*}} : !obelisk_sim.ref<!obelisk_sim.string> -> !obelisk_sim.string
// A non-format string literal remains its packed value. In particular, the
// empty literal is one null byte, so %s renders one space while %0s trims it.
// CHECK: %[[SFORMAT:.*]] = obelisk_sim.string.output_format {{.*}}(%[[DYNAMIC_FORMAT]], %{{.*}}, %[[EMPTY_LITERAL]]) radix = 10 flags = [40, 1, 0] {library_cell = "work.swrite_test", scope = "swrite_test"
// CHECK-NEXT: obelisk_sim.ref.store %[[SFORMAT]]
// CHECK: %[[LITERAL_SFORMAT:.*]] = obelisk_sim.string.output_format {{.*}}(%{{.*}}, %{{.*}}) radix = 10 flags = [32, 1] {library_cell = "work.swrite_test", scope = "swrite_test"
// CHECK-NEXT: obelisk_sim.ref.store %[[LITERAL_SFORMAT]]
