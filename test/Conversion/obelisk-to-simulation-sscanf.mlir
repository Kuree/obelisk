// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// The format is split at compile time: each conversion becomes one scan-field
// op carrying the literal text before it, and the field it returns is parsed
// with the same primitives the string conversion methods use. The running
// success flag branches around each store and gates the cursor, so a failed
// conversion leaves every later destination untouched.

// CHECK-LABEL: simulation.func private @unit_0(
// CHECK: %[[FIELD0:.*]], %[[CURSOR0:.*]], %[[OK0:.*]] = simulation.string.scan_field {{.*}} {prefix = "", specifier = 100 : i32, width = 0 : i64}
// CHECK: arith.cmpi ne, %[[OK0]]
// CHECK: simulation.string.parse_logic %[[FIELD0]] radix = <decimal> : !simulation.logic<32>
// CHECK: cf.cond_br
// CHECK: %[[FIELD1:.*]], %[[CURSOR1:.*]], %[[OK1:.*]] = simulation.string.scan_field {{.*}} {prefix = " ", specifier = 102 : i32, width = 0 : i64}
// CHECK: simulation.string.parse_real %[[FIELD1]]
// CHECK: %[[FIELD2:.*]], %[[CURSOR2:.*]], %[[OK2:.*]] = simulation.string.scan_field {{.*}} {prefix = " ", specifier = 115 : i32, width = 0 : i64}
// CHECK: %[[MATCHED2:.*]] = arith.cmpi ne, %[[OK2]]
// CHECK: %[[LIVE2:.*]] = arith.andi {{.*}}, %[[MATCHED2]]
// CHECK: cf.cond_br %[[LIVE2]]
// CHECK: simulation.ref.store %[[FIELD2]]
// CHECK: %[[SUPPRESSED:.*]], %[[SUPPRESSED_CURSOR:.*]], %[[SUPPRESSED_OK:.*]] = simulation.string.scan_field {{.*}} {prefix = "", specifier = 77 : i32, width = 0 : i64}
// CHECK: %[[FIELD3:.*]], %[[CURSOR3:.*]], %[[OK3:.*]] = simulation.string.scan_field {{.*}} {prefix = "", specifier = 109 : i32, width = 0 : i64}
// CHECK: %[[HIERARCHY:.*]] = simulation.string.literal "top"
// CHECK: cf.cond_br
// CHECK: simulation.ref.store %[[HIERARCHY]]
// CHECK: %[[TIME_FIELD:.*]], %[[TIME_CURSOR:.*]], %[[TIME_OK:.*]] = simulation.string.scan_field {{.*}} {prefix = "", specifier = 84 : i32, width = 0 : i64}
// CHECK: %[[TIME_REAL:.*]] = simulation.string.parse_real %[[TIME_FIELD]]
// CHECK: %[[SCALED_TIME:.*]] = simulation.time.scan_scale {{.*}}, %[[TIME_REAL]] time_multiplier = 1 time_precision = -9
// CHECK: simulation.ref.store %[[SCALED_TIME]]
// CHECK: %[[STRENGTH_FIELD:.*]], %[[STRENGTH_CURSOR:.*]], %[[STRENGTH_OK:.*]] = simulation.string.scan_field {{.*}} {prefix = "", specifier = 86 : i32, width = 0 : i64}
// CHECK: %[[STRENGTH:.*]] = simulation.string.parse_logic %[[STRENGTH_FIELD]] radix = <binary> : !simulation.logic<1>
// CHECK: simulation.ref.store %[[STRENGTH]]

module {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    obelisk.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable @s5.count attributes {hierarchical_name = "top.count", lifetime = 1 : i32, name = "count", node_id = 5 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
        }
        obelisk.sv.symbol.variable @s6.first attributes {hierarchical_name = "top.first", lifetime = 1 : i32, name = "first", node_id = 6 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
        }
        obelisk.sv.symbol.variable @s7.second attributes {hierarchical_name = "top.second", lifetime = 1 : i32, name = "second", node_id = 7 : i64, semantic_type = !obelisk.real} {
        }
        obelisk.sv.symbol.variable @s8.word attributes {hierarchical_name = "top.word", lifetime = 1 : i32, name = "word", node_id = 8 : i64, semantic_type = !obelisk.string} {
        }
        obelisk.sv.symbol.variable @s26.hierarchy attributes {hierarchical_name = "top.hierarchy", lifetime = 1 : i32, name = "hierarchy", node_id = 26 : i64, semantic_type = !obelisk.string} {
        }
        obelisk.sv.symbol.variable @s30.scanned_time attributes {hierarchical_name = "top.scanned_time", lifetime = 1 : i32, name = "scanned_time", node_id = 30 : i64, semantic_type = !obelisk.real} {
        }
        obelisk.sv.symbol.variable @s34.scanned_strength attributes {hierarchical_name = "top.scanned_strength", lifetime = 1 : i32, name = "scanned_strength", node_id = 34 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
        }
        obelisk.sv.symbol.procedural_block @s9 attributes {hierarchical_name = "top", node_id = 9 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 10 : i64} {
            obelisk.sv.statement.expression_statement attributes {node_id = 11 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 12 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                obelisk.sv.expression.named_value attributes {node_id = 13 : i64, referenced_path = "top.count", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.count, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                }
                obelisk.sv.expression.call attributes {argument_count = 8 : i64, callee_name = "$sscanf", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0, 0, 0, 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 14 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                  obelisk.sv.expression.string_literal attributes {constant_value = "1 2.5 hi", node_id = 15 : i64, semantic_type = !obelisk.ranged_packed_array<63 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.expression.string_literal attributes {constant_value = "%d %f %s%*M%m%T%V", node_id = 16 : i64, semantic_type = !obelisk.ranged_packed_array<135 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 17 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                    obelisk.sv.expression.named_value attributes {node_id = 18 : i64, referenced_path = "top.first", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.first, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                    }
                    obelisk.sv.expression.empty_argument attributes {node_id = 19 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                    }
                  }
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 20 : i64, semantic_type = !obelisk.real} {
                    obelisk.sv.expression.named_value attributes {node_id = 21 : i64, referenced_path = "top.second", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.second, semantic_type = !obelisk.real} {
                    }
                    obelisk.sv.expression.empty_argument attributes {node_id = 22 : i64, semantic_type = !obelisk.real} {
                    }
                  }
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 23 : i64, semantic_type = !obelisk.string} {
                    obelisk.sv.expression.named_value attributes {node_id = 24 : i64, referenced_path = "top.word", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.word, semantic_type = !obelisk.string} {
                    }
                    obelisk.sv.expression.empty_argument attributes {node_id = 25 : i64, semantic_type = !obelisk.string} {
                    }
                  }
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 27 : i64, semantic_type = !obelisk.string} {
                    obelisk.sv.expression.named_value attributes {node_id = 28 : i64, referenced_path = "top.hierarchy", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s26.hierarchy, semantic_type = !obelisk.string} {
                    }
                    obelisk.sv.expression.empty_argument attributes {node_id = 29 : i64, semantic_type = !obelisk.string} {
                    }
                  }
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 31 : i64, semantic_type = !obelisk.real} {
                    obelisk.sv.expression.named_value attributes {node_id = 32 : i64, referenced_path = "top.scanned_time", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s30.scanned_time, semantic_type = !obelisk.real} {
                    }
                    obelisk.sv.expression.empty_argument attributes {node_id = 33 : i64, semantic_type = !obelisk.real} {
                    }
                  }
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 35 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    obelisk.sv.expression.named_value attributes {node_id = 36 : i64, referenced_path = "top.scanned_strength", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s34.scanned_strength, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                    }
                    obelisk.sv.expression.empty_argument attributes {node_id = 37 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
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
