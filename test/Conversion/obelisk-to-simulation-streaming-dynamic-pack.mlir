// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// A dynamically sized stream is flattened into a runtime-sized one-bit
// container, reordered in slice-sized blocks, and grouped into the smallest
// number of destination elements. This keeps the intermediate stream's length
// and state domain explicit in executable MLIR.
// CHECK-LABEL: obelisk_sim.func private @unit_0
// CHECK: %[[STREAM:.*]] = obelisk_sim.container.create
// CHECK-SAME: -> !obelisk_sim.queue<!obelisk_sim.logic<1>, 0>
// CHECK: obelisk_sim.bits.dyn_extract
// CHECK: obelisk_sim.container.write %[[STREAM]],
// CHECK: %[[DATA:.*]] = obelisk_sim.ref.load {{.*}} : !obelisk_sim.ref<!obelisk_sim.dynamic_array<i8>> -> !obelisk_sim.dynamic_array<i8>
// CHECK: %[[SLICE:.*]] = obelisk_sim.container.create
// CHECK-SAME: -> !obelisk_sim.dynamic_array<i8>
// CHECK: %[[SIZE:.*]] = obelisk_sim.container.size %[[DATA]]
// CHECK: obelisk_sim.container.read %[[DATA]]
// CHECK: obelisk_sim.container.write %[[SLICE]],
// CHECK: obelisk_sim.container.size %[[SLICE]]
// CHECK: obelisk_sim.container.read %[[SLICE]]
// CHECK: %[[NESTED:.*]] = obelisk_sim.ref.load {{.*}} : !obelisk_sim.ref<!obelisk_sim.dynamic_array<!obelisk_sim.dynamic_array<i8>>> -> !obelisk_sim.dynamic_array<!obelisk_sim.dynamic_array<i8>>
// CHECK: obelisk_sim.container.size %[[NESTED]]
// CHECK: obelisk_sim.bits.dyn_extract
// CHECK: obelisk_sim.container.write %[[STREAM]],
// CHECK: %[[INNER:.*]] = obelisk_sim.container.read %[[NESTED]]
// CHECK: obelisk_sim.container.size %[[INNER]]
// CHECK: obelisk_sim.bits.dyn_extract
// CHECK: obelisk_sim.container.write %[[STREAM]],
// CHECK: obelisk_sim.logic.dyn_extract
// CHECK: obelisk_sim.container.write %[[STREAM]],
// CHECK: %[[STREAM_WIDTH:.*]] = obelisk_sim.container.size %[[STREAM]]
// CHECK: %[[REORDERED:.*]] = obelisk_sim.container.create
// CHECK: %[[REORDER_WIDTH:.*]] = obelisk_sim.container.size %[[STREAM]]
// CHECK: arith.divui %[[REORDER_WIDTH]],
// CHECK: obelisk_sim.container.read %[[STREAM]]
// CHECK: obelisk_sim.container.write %[[REORDERED]],
// CHECK: arith.divui %[[STREAM_WIDTH]],
// CHECK: arith.remui %[[STREAM_WIDTH]],
// CHECK: arith.cmpi ne
// CHECK: %[[PACKET:.*]] = obelisk_sim.container.create
// CHECK: %[[PACKET_COPY:.*]] = obelisk_sim.container.clone %[[PACKET]]
// CHECK: obelisk_sim.ref.store %[[PACKET_COPY]]
// A fixed-width stream assigned to a queue takes the same dynamic-target path.
// CHECK: %[[PAD_STREAM:.*]] = obelisk_sim.container.create
// CHECK-SAME: -> !obelisk_sim.queue<!obelisk_sim.logic<1>, 0>
// CHECK: obelisk_sim.container.write %[[PACKET]],
// CHECK: obelisk_sim.logic.dyn_extract
// CHECK: obelisk_sim.container.write %[[PAD_STREAM]],
// CHECK: %[[PAD_WIDTH:.*]] = obelisk_sim.container.size %[[PAD_STREAM]]
// CHECK: %[[PAD_PACKET:.*]] = obelisk_sim.container.create
// CHECK: %[[PAD_COPY:.*]] = obelisk_sim.container.clone %[[PAD_PACKET]]
// CHECK: obelisk_sim.ref.store %[[PAD_COPY]]
// CHECK: obelisk_sim.container.read %[[PAD_STREAM]]
// CHECK: obelisk_sim.container.write %[[PAD_PACKET]],
// A one-bit destination reuses the converted bit directly; an equal-width
// arith.extui is invalid and must never be constructed.
// CHECK: %[[BIT_PACKET:.*]] = obelisk_sim.container.create
// CHECK-SAME: -> !obelisk_sim.queue<i1, 0>
// CHECK: %[[PACKED_BIT:.*]] = obelisk_sim.logic.to_bits
// CHECK-NOT: arith.extui {{.*}} : i1 to i1
// CHECK: obelisk_sim.container.write %[[BIT_PACKET]], {{.*}}, %[[PACKED_BIT]]
// CHECK-NOT: obelisk.sv.

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "dynamic_pack", name = "dynamic_pack", node_id = 0 : i64, sym_name = "s0.dynamic_pack"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "dynamic_pack", is_uninstantiated = false, name = "dynamic_pack", node_id = 3 : i64, referenced_path = "dynamic_pack", referenced_symbol = @s0.dynamic_pack, sym_name = "s3.dynamic_pack"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "dynamic_pack", name = "dynamic_pack", node_id = 4 : i64, sym_name = "s4.dynamic_pack"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "dynamic_pack.header", lifetime = 1 : i32, name = "header", node_id = 5 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s5.header"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "dynamic_pack.data", lifetime = 1 : i32, name = "data", node_id = 6 : i64, semantic_type = !obelisk.dynarray<!obelisk.integral<8, true, false, 7 : 0, byte>>, sym_name = "s6.data"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "dynamic_pack.nested", lifetime = 1 : i32, name = "nested", node_id = 16 : i64, semantic_type = !obelisk.dynarray<!obelisk.dynarray<!obelisk.integral<8, true, false, 7 : 0, byte>>>, sym_name = "s16.nested"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "dynamic_pack.state", lifetime = 1 : i32, name = "state", node_id = 18 : i64, semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>, sym_name = "s18.state"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "dynamic_pack.packet", lifetime = 1 : i32, name = "packet", node_id = 7 : i64, semantic_type = !obelisk.queue<!obelisk.integral<8, true, false, 7 : 0, byte>, 0>, sym_name = "s7.packet"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "dynamic_pack.bits", lifetime = 1 : i32, name = "bits", node_id = 30 : i64, semantic_type = !obelisk.queue<!obelisk.integral<1, false, false, 0 : 0, bit>, 0>, sym_name = "s30.bits"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "dynamic_pack", node_id = 8 : i64, procedure_kind = 0 : i32, sym_name = "s8"} {
          obelisk.sv.statement.expression_statement attributes {node_id = 9 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 10 : i64, semantic_type = !obelisk.queue<!obelisk.integral<8, true, false, 7 : 0, byte>, 0>} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 11 : i64, referenced_path = "dynamic_pack.packet", referenced_symbol = @s1.$root::@s3.dynamic_pack::@s4.dynamic_pack::@s7.packet, semantic_type = !obelisk.queue<!obelisk.integral<8, true, false, 7 : 0, byte>, 0>} {
              }
              obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 12 : i64, semantic_type = !obelisk.queue<!obelisk.integral<8, true, false, 7 : 0, byte>, 0>} {
                obelisk.sv.expression.streaming attributes {bitstream_width = 36 : i64, is_fixed_size = false, is_signed = false, node_id = 13 : i64, semantic_type = !obelisk.void, slice_size = 8 : i64, stream_count = 4 : i64, stream_with_flags = array<i64: 0, 1, 0, 0>} {
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 14 : i64, referenced_path = "dynamic_pack.header", referenced_symbol = @s1.$root::@s3.dynamic_pack::@s4.dynamic_pack::@s5.header, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 15 : i64, referenced_path = "dynamic_pack.data", referenced_symbol = @s1.$root::@s3.dynamic_pack::@s4.dynamic_pack::@s6.data, semantic_type = !obelisk.dynarray<!obelisk.integral<8, true, false, 7 : 0, byte>>} {
                  }
                  obelisk.sv.expression.range_select attributes {is_signed = false, node_id = 26 : i64, selection_kind = 1 : i32, semantic_type = !obelisk.queue<!obelisk.integral<8, true, false, 7 : 0, byte>, 0>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 27 : i64, referenced_path = "dynamic_pack.data", referenced_symbol = @s1.$root::@s3.dynamic_pack::@s4.dynamic_pack::@s6.data, semantic_type = !obelisk.dynarray<!obelisk.integral<8, true, false, 7 : 0, byte>>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 28 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "2", is_declared_unsized = true, is_signed = true, node_id = 29 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 17 : i64, referenced_path = "dynamic_pack.nested", referenced_symbol = @s1.$root::@s3.dynamic_pack::@s4.dynamic_pack::@s16.nested, semantic_type = !obelisk.dynarray<!obelisk.dynarray<!obelisk.integral<8, true, false, 7 : 0, byte>>>} {
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 19 : i64, referenced_path = "dynamic_pack.state", referenced_symbol = @s1.$root::@s3.dynamic_pack::@s4.dynamic_pack::@s18.state, semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>} {
                  }
                }
              }
            }
          }
          obelisk.sv.statement.expression_statement attributes {node_id = 20 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 21 : i64, semantic_type = !obelisk.queue<!obelisk.integral<8, true, false, 7 : 0, byte>, 0>} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 22 : i64, referenced_path = "dynamic_pack.packet", referenced_symbol = @s1.$root::@s3.dynamic_pack::@s4.dynamic_pack::@s7.packet, semantic_type = !obelisk.queue<!obelisk.integral<8, true, false, 7 : 0, byte>, 0>} {
              }
              obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 23 : i64, semantic_type = !obelisk.queue<!obelisk.integral<8, true, false, 7 : 0, byte>, 0>} {
                obelisk.sv.expression.streaming attributes {bitstream_width = 4 : i64, is_fixed_size = true, is_signed = false, node_id = 24 : i64, semantic_type = !obelisk.void, slice_size = 0 : i64, stream_count = 1 : i64, stream_with_flags = array<i64: 0>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 25 : i64, referenced_path = "dynamic_pack.state", referenced_symbol = @s1.$root::@s3.dynamic_pack::@s4.dynamic_pack::@s18.state, semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>} {
                  }
                }
              }
            }
          }
          obelisk.sv.statement.expression_statement attributes {node_id = 31 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 32 : i64, semantic_type = !obelisk.queue<!obelisk.integral<1, false, false, 0 : 0, bit>, 0>} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 33 : i64, referenced_path = "dynamic_pack.bits", referenced_symbol = @s1.$root::@s3.dynamic_pack::@s4.dynamic_pack::@s30.bits, semantic_type = !obelisk.queue<!obelisk.integral<1, false, false, 0 : 0, bit>, 0>} {
              }
              obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 34 : i64, semantic_type = !obelisk.queue<!obelisk.integral<1, false, false, 0 : 0, bit>, 0>} {
                obelisk.sv.expression.streaming attributes {bitstream_width = 4 : i64, is_fixed_size = true, is_signed = false, node_id = 35 : i64, semantic_type = !obelisk.void, slice_size = 0 : i64, stream_count = 1 : i64, stream_with_flags = array<i64: 0>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 36 : i64, referenced_path = "dynamic_pack.state", referenced_symbol = @s1.$root::@s3.dynamic_pack::@s4.dynamic_pack::@s18.state, semantic_type = !obelisk.integral<4, false, true, 3 : 0, logic>} {
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
