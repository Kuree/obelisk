// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// The indexed range is evaluated after the preceding length field is stored.
// The dynamic array is resized, its unaffected prefix is copied, and stream
// elements are written at the selected indices.
// CHECK-LABEL: simulation.func private @unit_0
// CHECK: simulation.ref.store
// CHECK: %[[LENGTH:.*]] = simulation.ref.load
// CHECK: %[[LENGTH64:.*]] = arith.extui %[[LENGTH]]
// CHECK: arith.cmpi sgt
// CHECK: %[[RESIZED:.*]] = arith.addi %[[LENGTH64]],
// CHECK: arith.muli %[[LENGTH64]],
// CHECK: arith.cmpi ule
// CHECK: %[[OLD:.*]] = simulation.ref.load {{.*}} : !simulation.ref<!simulation.dynamic_array<i8>> -> !simulation.dynamic_array<i8>
// CHECK: %[[OLD_SIZE:.*]] = simulation.container.size %[[OLD]]
// CHECK: %[[GROW:.*]] = arith.cmpi ult, %[[OLD_SIZE]], %[[RESIZED]]
// CHECK: %[[NEW_SIZE:.*]] = arith.select %[[GROW]], %[[RESIZED]], %[[OLD_SIZE]]
// CHECK: %[[RESULT:.*]] = simulation.container.create %[[NEW_SIZE]]
// CHECK: simulation.container.read %[[OLD]]
// CHECK: simulation.container.write %[[RESULT]],
// CHECK: simulation.container.read
// CHECK: simulation.container.write %[[RESULT]],
// CHECK: simulation.ref.store
// CHECK-NOT: obelisk.sv.

module {
  obelisk.sv.symbol.definition @s0.with_unpack attributes {definition_kind = 0 : i32, hierarchical_name = "with_unpack", name = "with_unpack", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    obelisk.sv.symbol.instance @s3.with_unpack attributes {hierarchical_name = "with_unpack", is_uninstantiated = false, name = "with_unpack", node_id = 3 : i64, referenced_path = "with_unpack", referenced_symbol = @s0.with_unpack} {
      obelisk.sv.symbol.instance_body @s4.with_unpack attributes {hierarchical_name = "with_unpack", name = "with_unpack", node_id = 4 : i64} {
        obelisk.sv.symbol.variable @s5.source attributes {hierarchical_name = "with_unpack.source", lifetime = 1 : i32, name = "source", node_id = 5 : i64, semantic_type = !obelisk.queue<!obelisk.integral<8, false, false, 7 : 0, byte>, 0>} {
        }
        obelisk.sv.symbol.variable @s6.header attributes {hierarchical_name = "with_unpack.header", lifetime = 1 : i32, name = "header", node_id = 6 : i64, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>} {
        }
        obelisk.sv.symbol.variable @s7.length attributes {hierarchical_name = "with_unpack.length", lifetime = 1 : i32, name = "length", node_id = 7 : i64, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>} {
        }
        obelisk.sv.symbol.variable @s8.data attributes {hierarchical_name = "with_unpack.data", lifetime = 1 : i32, name = "data", node_id = 8 : i64, semantic_type = !obelisk.dynarray<!obelisk.integral<8, false, false, 7 : 0, byte>>} {
        }
        obelisk.sv.symbol.variable @s9.crc attributes {hierarchical_name = "with_unpack.crc", lifetime = 1 : i32, name = "crc", node_id = 9 : i64, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>} {
        }
        obelisk.sv.symbol.procedural_block @s10 attributes {hierarchical_name = "with_unpack", node_id = 10 : i64, procedure_kind = 0 : i32} {
          obelisk.sv.statement.expression_statement attributes {node_id = 11 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 12 : i64, semantic_type = !obelisk.void} {
              obelisk.sv.expression.streaming attributes {bitstream_width = 96 : i64, is_fixed_size = false, is_signed = false, node_id = 13 : i64, semantic_type = !obelisk.void, slice_size = 0 : i64, stream_count = 4 : i64, stream_with_flags = array<i64: 0, 0, 1, 0>} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 14 : i64, referenced_path = "with_unpack.header", referenced_symbol = @s1.$root::@s3.with_unpack::@s4.with_unpack::@s6.header, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 15 : i64, referenced_path = "with_unpack.length", referenced_symbol = @s1.$root::@s3.with_unpack::@s4.with_unpack::@s7.length, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 16 : i64, referenced_path = "with_unpack.data", referenced_symbol = @s1.$root::@s3.with_unpack::@s4.with_unpack::@s8.data, semantic_type = !obelisk.dynarray<!obelisk.integral<8, false, false, 7 : 0, byte>>} {
                }
                obelisk.sv.expression.range_select attributes {is_signed = false, node_id = 17 : i64, selection_kind = 1 : i32, semantic_type = !obelisk.queue<!obelisk.integral<8, false, false, 7 : 0, byte>, 0>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 18 : i64, referenced_path = "with_unpack.data", referenced_symbol = @s1.$root::@s3.with_unpack::@s4.with_unpack::@s8.data, semantic_type = !obelisk.dynarray<!obelisk.integral<8, false, false, 7 : 0, byte>>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "2", is_declared_unsized = true, is_signed = true, node_id = 19 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 20 : i64, referenced_path = "with_unpack.length", referenced_symbol = @s1.$root::@s3.with_unpack::@s4.with_unpack::@s7.length, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>} {
                  }
                }
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 21 : i64, referenced_path = "with_unpack.crc", referenced_symbol = @s1.$root::@s3.with_unpack::@s4.with_unpack::@s9.crc, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>} {
                }
              }
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 22 : i64, referenced_path = "with_unpack.source", referenced_symbol = @s1.$root::@s3.with_unpack::@s4.with_unpack::@s5.source, semantic_type = !obelisk.queue<!obelisk.integral<8, false, false, 7 : 0, byte>, 0>} {
              }
            }
          }
        }
      }
    }
  }
}
