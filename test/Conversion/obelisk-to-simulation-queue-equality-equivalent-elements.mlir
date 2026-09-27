// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 11.4.5: the equality operators compare their operands bit for
// bit.  Section 6.22.2(c) makes packed arrays and built-in integral types
// equivalent when they hold the same number of bits in the same domain with
// the same signedness, so `int q1[$]` and `bit signed [31:0] q2[$]` hold
// equivalent elements and compare -- each side is read in its own spelling and
// the right one is normalized before the element comparison.

module {
  obelisk.sv.symbol.definition attributes {
    definition_kind = 0 : i32, hierarchical_name = "t", name = "t",
    node_id = 0 : i64, sym_name = "s0.t"
  } {
  }
  obelisk.sv.symbol.root attributes {
    hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64,
    sym_name = "s1.$root"
  } {
    obelisk.sv.symbol.compilation_unit attributes {
      hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"
    } {
    }
    obelisk.sv.symbol.instance attributes {
      hierarchical_name = "t", is_uninstantiated = false, name = "t",
      node_id = 3 : i64, referenced_path = "t", referenced_symbol = @s0.t,
      sym_name = "s3.t"
    } {
      obelisk.sv.symbol.instance_body attributes {
        hierarchical_name = "t", name = "t", node_id = 4 : i64,
        sym_name = "s4.t", time_precision_fs = 1000000 : i64,
        time_unit_fs = 1000000 : i64
      } {
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "t.q1", lifetime = 1 : i32, name = "q1",
          node_id = 5 : i64,
          semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>,
          sym_name = "s5.q1"
        } {
        }
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "t.q2", lifetime = 1 : i32, name = "q2",
          node_id = 6 : i64,
          semantic_type = !obelisk.queue<!obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, false, 0 : 0, bit>>, 0>,
          sym_name = "s6.q2"
        } {
        }
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "t.r", lifetime = 1 : i32, name = "r",
          node_id = 7 : i64,
          semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>,
          sym_name = "s7.r"
        } {
        }
        obelisk.sv.symbol.procedural_block attributes {
          hierarchical_name = "t", node_id = 8 : i64, procedure_kind = 0 : i32,
          sym_name = "s8", time_precision_fs = 1000000 : i64,
          time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.expression_statement attributes {
            node_id = 9 : i64
          } {
            obelisk.sv.expression.assignment attributes {
              assignment_kind = 0 : i32, is_signed = false, node_id = 10 : i64,
              semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
            } {
              obelisk.sv.expression.named_value attributes {
                is_signed = false, node_id = 11 : i64, referenced_path = "t.r",
                referenced_symbol = @s1.$root::@s3.t::@s4.t::@s7.r,
                semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
              } {
              }
              obelisk.sv.expression.binary_op attributes {
                is_signed = false, node_id = 12 : i64, operator_kind = 9 : i32,
                semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
              } {
                obelisk.sv.expression.named_value attributes {
                  is_signed = false, node_id = 13 : i64,
                  referenced_path = "t.q1",
                  referenced_symbol = @s1.$root::@s3.t::@s4.t::@s5.q1,
                  semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>
                } {
                }
                obelisk.sv.expression.named_value attributes {
                  is_signed = false, node_id = 14 : i64,
                  referenced_path = "t.q2",
                  referenced_symbol = @s1.$root::@s3.t::@s4.t::@s6.q2,
                  semantic_type = !obelisk.queue<!obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, true, false, 0 : 0, bit>>, 0>
                } {
                }
              }
            }
          }
        }
      }
    }
  }
}



// CHECK-LABEL: simulation.func private @unit_0
// CHECK: %[[L:.*]] = simulation.ref.load %arg1
// CHECK: %[[R:.*]] = simulation.ref.load %arg2
// CHECK: %[[LSIZE:.*]] = simulation.container.size %[[L]]
// CHECK: %[[RSIZE:.*]] = simulation.container.size %[[R]]
// CHECK: %[[SAME:.*]] = arith.cmpi eq, %[[LSIZE]], %[[RSIZE]] : i64
// CHECK: ^bb2:
// CHECK: %[[LE:.*]] = simulation.container.read %[[L]], %{{.*}} -> i32
// CHECK: %[[RE:.*]] = simulation.container.read %[[R]], %{{.*}} -> !simulation.packed_array<31 : 0 x i1>
// CHECK: %[[RN:.*]] = simulation.packed.flatten %[[RE]] : (!simulation.packed_array<31 : 0 x i1>) -> i32
// CHECK: arith.cmpi eq, %[[LE]], %[[RN]] : i32
