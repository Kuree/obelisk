// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// CHECK-LABEL: obelisk_sim.func private @unit_0
// CHECK: %[[SOURCE:.*]] = obelisk_sim.ref.load {{.*}} : !obelisk_sim.ref<!obelisk_sim.queue<i8, 0>> -> !obelisk_sim.queue<i8, 0>
// CHECK: %[[GENERIC:.*]] = obelisk_sim.container.create
// CHECK: obelisk_sim.container.size %[[SOURCE]]
// CHECK: obelisk_sim.container.read %[[SOURCE]]
// CHECK: %[[REORDERED:.*]] = obelisk_sim.container.create
// CHECK: obelisk_sim.bits.dyn_extract
// CHECK: obelisk_sim.container.write %[[GENERIC]],
// CHECK: obelisk_sim.container.read %[[GENERIC]]
// CHECK: obelisk_sim.container.write %[[REORDERED]],
// CHECK: arith.cmpi uge
// CHECK: arith.remui
// CHECK: arith.cmpi eq
// CHECK: arith.divui
// CHECK: %[[DYNAMIC:.*]] = obelisk_sim.container.create
// CHECK: obelisk_sim.container.read %[[REORDERED]]
// CHECK: obelisk_sim.container.write %[[DYNAMIC]],
// CHECK: obelisk_sim.ref.store
// A one-bit dynamic destination stores the converted i1 directly. Equal-width
// extensions are invalid MLIR and must not be constructed.
// CHECK: %[[BIT_ARRAY:.*]] = obelisk_sim.container.create {{.*}}container_kind = 1{{.*}} -> !obelisk_sim.dynamic_array<i1>
// CHECK-NOT: arith.extui {{.*}} : i1 to i1
// CHECK: obelisk_sim.container.write %[[BIT_ARRAY]],
// CHECK: %[[BIT_ARRAY_COPY:.*]] = obelisk_sim.container.clone %[[BIT_ARRAY]]
// CHECK: obelisk_sim.ref.store %[[BIT_ARRAY_COPY]]
// CHECK-NOT: obelisk.sv.

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "dynamic_unpack", name = "dynamic_unpack", node_id = 0 : i64, sym_name = "s0.dynamic_unpack"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "dynamic_unpack", is_uninstantiated = false, name = "dynamic_unpack", node_id = 3 : i64, referenced_path = "dynamic_unpack", referenced_symbol = @s0.dynamic_unpack, sym_name = "s3.dynamic_unpack"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "dynamic_unpack", name = "dynamic_unpack", node_id = 4 : i64, sym_name = "s4.dynamic_unpack"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "dynamic_unpack.source", lifetime = 1 : i32, name = "source", node_id = 5 : i64, semantic_type = !obelisk.queue<!obelisk.integral<8, false, false, 7 : 0, byte>, 0>, sym_name = "s5.source"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "dynamic_unpack.header", lifetime = 1 : i32, name = "header", node_id = 6 : i64, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>, sym_name = "s6.header"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "dynamic_unpack.data", lifetime = 1 : i32, name = "data", node_id = 7 : i64, semantic_type = !obelisk.dynarray<!obelisk.integral<8, false, false, 7 : 0, byte>>, sym_name = "s7.data"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "dynamic_unpack.crc", lifetime = 1 : i32, name = "crc", node_id = 8 : i64, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>, sym_name = "s8.crc"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "dynamic_unpack.bits", lifetime = 1 : i32, name = "bits", node_id = 17 : i64, semantic_type = !obelisk.dynarray<!obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s17.bits"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "dynamic_unpack", node_id = 9 : i64, procedure_kind = 0 : i32, sym_name = "s9"} {
          obelisk.sv.statement.expression_statement attributes {node_id = 10 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 11 : i64, semantic_type = !obelisk.void} {
              obelisk.sv.expression.streaming attributes {bitstream_width = 64 : i64, is_fixed_size = false, is_signed = false, node_id = 12 : i64, semantic_type = !obelisk.void, slice_size = 8 : i64, stream_count = 3 : i64, stream_with_flags = array<i64: 0, 0, 0>} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 13 : i64, referenced_path = "dynamic_unpack.header", referenced_symbol = @s1.$root::@s3.dynamic_unpack::@s4.dynamic_unpack::@s6.header, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 14 : i64, referenced_path = "dynamic_unpack.data", referenced_symbol = @s1.$root::@s3.dynamic_unpack::@s4.dynamic_unpack::@s7.data, semantic_type = !obelisk.dynarray<!obelisk.integral<8, false, false, 7 : 0, byte>>} {
                }
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 15 : i64, referenced_path = "dynamic_unpack.crc", referenced_symbol = @s1.$root::@s3.dynamic_unpack::@s4.dynamic_unpack::@s8.crc, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>} {
                }
              }
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 16 : i64, referenced_path = "dynamic_unpack.source", referenced_symbol = @s1.$root::@s3.dynamic_unpack::@s4.dynamic_unpack::@s5.source, semantic_type = !obelisk.queue<!obelisk.integral<8, false, false, 7 : 0, byte>, 0>} {
              }
            }
          }
          obelisk.sv.statement.expression_statement attributes {node_id = 18 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 19 : i64, semantic_type = !obelisk.void} {
              obelisk.sv.expression.streaming attributes {bitstream_width = 0 : i64, is_fixed_size = false, is_signed = false, node_id = 20 : i64, semantic_type = !obelisk.void, slice_size = 1 : i64, stream_count = 1 : i64, stream_with_flags = array<i64: 0>} {
                obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 21 : i64, referenced_path = "dynamic_unpack.bits", referenced_symbol = @s1.$root::@s3.dynamic_unpack::@s4.dynamic_unpack::@s17.bits, semantic_type = !obelisk.dynarray<!obelisk.integral<1, false, false, 0 : 0, bit>>} {
                }
              }
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 22 : i64, referenced_path = "dynamic_unpack.header", referenced_symbol = @s1.$root::@s3.dynamic_unpack::@s4.dynamic_unpack::@s6.header, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>} {
              }
            }
          }
        }
      }
    }
  }
}
