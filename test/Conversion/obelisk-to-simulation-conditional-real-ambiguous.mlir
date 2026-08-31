// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 11.4.11: when the condition is x or z, both arms are
// evaluated and "compared for logical equivalence as described in 11.4.5. If
// that comparison is true (1), the operator shall return either the first or
// second expression." A real result reaches the same rule as an integral one --
// the clause's nonintegral list casts an integral arm to real and keeps the
// result real -- so an ambiguous condition over two equal reals yields that
// value, not the type's default.

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "conditional_real_ambiguous", name = "conditional_real_ambiguous", node_id = 0 : i64, sym_name = "s0.conditional_real_ambiguous"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "conditional_real_ambiguous", is_uninstantiated = false, name = "conditional_real_ambiguous", node_id = 3 : i64, referenced_path = "conditional_real_ambiguous", referenced_symbol = @s0.conditional_real_ambiguous, sym_name = "s3.conditional_real_ambiguous"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "conditional_real_ambiguous", name = "conditional_real_ambiguous", node_id = 4 : i64, sym_name = "s4.conditional_real_ambiguous", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "conditional_real_ambiguous.c", lifetime = 1 : i32, name = "c", node_id = 5 : i64, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>, sym_name = "s5.c"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "conditional_real_ambiguous.a", lifetime = 1 : i32, name = "a", node_id = 6 : i64, semantic_type = !obelisk.real, sym_name = "s6.a"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "conditional_real_ambiguous.b", lifetime = 1 : i32, name = "b", node_id = 7 : i64, semantic_type = !obelisk.real, sym_name = "s7.b"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "conditional_real_ambiguous.r", lifetime = 1 : i32, name = "r", node_id = 8 : i64, semantic_type = !obelisk.real, sym_name = "s8.r"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "conditional_real_ambiguous", node_id = 9 : i64, procedure_kind = 0 : i32, sym_name = "s9", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 10 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 11 : i64, semantic_type = !obelisk.real} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 12 : i64, referenced_path = "conditional_real_ambiguous.r", referenced_symbol = @s1.$root::@s3.conditional_real_ambiguous::@s4.conditional_real_ambiguous::@s8.r, semantic_type = !obelisk.real} {
              }
              obelisk.sv.expression.conditional_op attributes {condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, is_signed = false, node_id = 13 : i64, semantic_type = !obelisk.real} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 14 : i64, referenced_path = "conditional_real_ambiguous.c", referenced_symbol = @s1.$root::@s3.conditional_real_ambiguous::@s4.conditional_real_ambiguous::@s5.c, semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>} {
                }
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 15 : i64, referenced_path = "conditional_real_ambiguous.a", referenced_symbol = @s1.$root::@s3.conditional_real_ambiguous::@s4.conditional_real_ambiguous::@s6.a, semantic_type = !obelisk.real} {
                }
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 16 : i64, referenced_path = "conditional_real_ambiguous.b", referenced_symbol = @s1.$root::@s3.conditional_real_ambiguous::@s4.conditional_real_ambiguous::@s7.b, semantic_type = !obelisk.real} {
                }
              }
            }
          }
        }
      }
    }
  }
}


// The ambiguous arm compares the two reals and keeps a matching value; the
// 0.0 default is reached only when they differ.
// CHECK: arith.cmpf oeq
// CHECK: arith.select
// CHECK-NOT: obelisk.sv.
