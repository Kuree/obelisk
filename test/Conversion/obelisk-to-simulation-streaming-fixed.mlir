// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2023 11.4.14.1/.2: fixed bit-stream operands are appended in
// source order.  >> retains that generic stream; << takes slices from its
// right edge, preserving the order within every slice.  The most-significant
// partial slice is not padded.  A wider assignment target instead pads the
// completed stream with zeros on its right (11.4.14), not on its left.
module {
  obelisk.sv.symbol.definition @s0.streaming_fixed attributes {definition_kind = 0 : i32, hierarchical_name = "streaming_fixed", name = "streaming_fixed", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    obelisk.sv.symbol.instance @s3.streaming_fixed attributes {hierarchical_name = "streaming_fixed", is_uninstantiated = false, name = "streaming_fixed", node_id = 3 : i64, referenced_path = "streaming_fixed", referenced_symbol = @s0.streaming_fixed} {
      obelisk.sv.symbol.instance_body @s4.streaming_fixed attributes {hierarchical_name = "streaming_fixed", name = "streaming_fixed", node_id = 4 : i64} {
        obelisk.sv.symbol.variable @s22.source32 attributes {hierarchical_name = "streaming_fixed.source32", lifetime = 1 : i32, name = "source32", node_id = 22 : i64, semantic_type = !obelisk.integral<32, false, false, 31 : 0, bit>} {
        }
        obelisk.sv.symbol.variable @s23.source6 attributes {hierarchical_name = "streaming_fixed.source6", lifetime = 1 : i32, name = "source6", node_id = 23 : i64, semantic_type = !obelisk.integral<6, false, false, 5 : 0, bit>} {
        }
        obelisk.sv.symbol.variable @s24.source3 attributes {hierarchical_name = "streaming_fixed.source3", lifetime = 1 : i32, name = "source3", node_id = 24 : i64, semantic_type = !obelisk.integral<3, false, false, 2 : 0, bit>} {
        }
        obelisk.sv.symbol.variable @s25.source_logic attributes {hierarchical_name = "streaming_fixed.source_logic", lifetime = 1 : i32, name = "source_logic", node_id = 25 : i64, semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>} {
        }
        obelisk.sv.symbol.variable @s26.source_array attributes {hierarchical_name = "streaming_fixed.source_array", lifetime = 1 : i32, name = "source_array", node_id = 26 : i64, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.integral<3, false, false, 2 : 0, bit>>} {
        }
        obelisk.sv.symbol.variable @s5.bytes attributes {hierarchical_name = "streaming_fixed.bytes", lifetime = 1 : i32, name = "bytes", node_id = 5 : i64, semantic_type = !obelisk.integral<32, false, false, 31 : 0, bit>} {
          obelisk.sv.expression.conversion attributes {node_id = 6 : i64, semantic_type = !obelisk.integral<32, false, false, 31 : 0, bit>} {
            obelisk.sv.expression.streaming attributes {bitstream_width = 32 : i64, is_fixed_size = true, node_id = 7 : i64, semantic_type = !obelisk.void, slice_size = 8 : i64, stream_count = 1 : i64, stream_with_flags = array<i64: 0>} {
              obelisk.sv.expression.named_value attributes {node_id = 8 : i64, referenced_path = "streaming_fixed.source32", referenced_symbol = @s1.$root::@s3.streaming_fixed::@s4.streaming_fixed::@s22.source32, semantic_type = !obelisk.integral<32, false, false, 31 : 0, bit>} {
              }
            }
          }
        }
        obelisk.sv.symbol.variable @s9.partial attributes {hierarchical_name = "streaming_fixed.partial", lifetime = 1 : i32, name = "partial", node_id = 9 : i64, semantic_type = !obelisk.integral<6, false, false, 5 : 0, bit>} {
          obelisk.sv.expression.conversion attributes {node_id = 10 : i64, semantic_type = !obelisk.integral<6, false, false, 5 : 0, bit>} {
            obelisk.sv.expression.streaming attributes {bitstream_width = 6 : i64, is_fixed_size = true, node_id = 11 : i64, semantic_type = !obelisk.void, slice_size = 4 : i64, stream_count = 1 : i64, stream_with_flags = array<i64: 0>} {
              obelisk.sv.expression.named_value attributes {node_id = 12 : i64, referenced_path = "streaming_fixed.source6", referenced_symbol = @s1.$root::@s3.streaming_fixed::@s4.streaming_fixed::@s23.source6, semantic_type = !obelisk.integral<6, false, false, 5 : 0, bit>} {
              }
            }
          }
        }
        obelisk.sv.symbol.variable @s13.padded attributes {hierarchical_name = "streaming_fixed.padded", lifetime = 1 : i32, name = "padded", node_id = 13 : i64, semantic_type = !obelisk.integral<10, false, false, 9 : 0, bit>} {
          obelisk.sv.expression.conversion attributes {node_id = 14 : i64, semantic_type = !obelisk.integral<10, false, false, 9 : 0, bit>} {
            obelisk.sv.expression.streaming attributes {bitstream_width = 6 : i64, is_fixed_size = true, node_id = 15 : i64, semantic_type = !obelisk.void, slice_size = 0 : i64, stream_count = 2 : i64, stream_with_flags = array<i64: 0, 0>} {
              obelisk.sv.expression.named_value attributes {node_id = 16 : i64, referenced_path = "streaming_fixed.source3", referenced_symbol = @s1.$root::@s3.streaming_fixed::@s4.streaming_fixed::@s24.source3, semantic_type = !obelisk.integral<3, false, false, 2 : 0, bit>} {
              }
              obelisk.sv.expression.named_value attributes {node_id = 17 : i64, referenced_path = "streaming_fixed.source3", referenced_symbol = @s1.$root::@s3.streaming_fixed::@s4.streaming_fixed::@s24.source3, semantic_type = !obelisk.integral<3, false, false, 2 : 0, bit>} {
              }
            }
          }
        }
        obelisk.sv.symbol.variable @s18.four_state attributes {hierarchical_name = "streaming_fixed.four_state", lifetime = 1 : i32, name = "four_state", node_id = 18 : i64, semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>} {
          obelisk.sv.expression.conversion attributes {node_id = 19 : i64, semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>} {
            obelisk.sv.expression.streaming attributes {bitstream_width = 4 : i64, is_fixed_size = true, node_id = 20 : i64, semantic_type = !obelisk.void, slice_size = 2 : i64, stream_count = 1 : i64, stream_with_flags = array<i64: 0>} {
              obelisk.sv.expression.named_value attributes {node_id = 21 : i64, referenced_path = "streaming_fixed.source_logic", referenced_symbol = @s1.$root::@s3.streaming_fixed::@s4.streaming_fixed::@s25.source_logic, semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>} {
              }
            }
          }
        }
        obelisk.sv.symbol.variable @s27.array_stream attributes {hierarchical_name = "streaming_fixed.array_stream", lifetime = 1 : i32, name = "array_stream", node_id = 27 : i64, semantic_type = !obelisk.integral<6, false, false, 5 : 0, bit>} {
          obelisk.sv.expression.conversion attributes {node_id = 28 : i64, semantic_type = !obelisk.integral<6, false, false, 5 : 0, bit>} {
            obelisk.sv.expression.streaming attributes {bitstream_width = 6 : i64, is_fixed_size = true, node_id = 29 : i64, semantic_type = !obelisk.void, slice_size = 0 : i64, stream_count = 1 : i64, stream_with_flags = array<i64: 0>} {
              obelisk.sv.expression.named_value attributes {node_id = 30 : i64, referenced_path = "streaming_fixed.source_array", referenced_symbol = @s1.$root::@s3.streaming_fixed::@s4.streaming_fixed::@s26.source_array, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.integral<3, false, false, 2 : 0, bit>>} {
              }
            }
          }
        }
      }
    }
  }
}

// CHECK-LABEL: simulation.func private @unit_0
// CHECK: arith.trunci {{%.*}} : i32 to i8
// CHECK: arith.shrui
// CHECK: arith.shli
// CHECK: simulation.ref.store

// CHECK-LABEL: simulation.func private @unit_1
// CHECK: arith.trunci {{%.*}} : i6 to i4
// CHECK: arith.trunci {{%.*}} : i6 to i2
// CHECK: arith.shli
// CHECK: simulation.ref.store

// CHECK-LABEL: simulation.func private @unit_2
// CHECK: arith.shli {{%.*}}, {{%.*}} : i10
// CHECK: simulation.ref.store

// CHECK-LABEL: simulation.func private @unit_3
// CHECK: simulation.logic.extract {{%.*}} from 0 : !simulation.logic<4> -> !simulation.logic<2>
// CHECK: simulation.logic.extract {{%.*}} from 2 : !simulation.logic<4> -> !simulation.logic<2>
// CHECK: simulation.logic.concat
// CHECK: simulation.ref.store

// CHECK-LABEL: simulation.func private @unit_4
// CHECK: simulation.ref.subelement {{%.*}}{{\[\[0\]\]}}
// CHECK: simulation.ref.subelement {{%.*}}{{\[\[1\]\]}}
// CHECK: arith.shli
// CHECK: simulation.ref.store
