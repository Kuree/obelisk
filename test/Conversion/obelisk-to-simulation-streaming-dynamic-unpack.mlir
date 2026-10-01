// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// CHECK-LABEL: simulation.func private @unit_0
// CHECK: %[[SOURCE:.*]] = simulation.ref.load {{.*}} : !simulation.ref<!simulation.queue<i8, 0>> -> !simulation.queue<i8, 0>
// CHECK: %[[GENERIC:.*]] = simulation.container.create
// CHECK: simulation.container.size %[[SOURCE]]
// CHECK: simulation.container.read %[[SOURCE]]
// The source has to hold every fixed target's bits before the reordering runs,
// because that is what the reordering spans (IEEE 1800-2017 11.4.14.3).
// CHECK: simulation.container.size %[[GENERIC]]
// CHECK: arith.cmpi uge
// CHECK: simulation.bits.dyn_extract
// CHECK: simulation.container.write %[[GENERIC]],
// CHECK: %[[REORDERED:.*]] = simulation.container.create
// CHECK: arith.divui
// CHECK: arith.remui
// CHECK: simulation.container.read %[[GENERIC]]
// CHECK: simulation.container.write %[[REORDERED]],
// CHECK: simulation.container.read %[[REORDERED]]
// CHECK: arith.cmpi eq
// CHECK: %[[DYNAMIC:.*]] = simulation.container.create
// CHECK: simulation.container.write %[[DYNAMIC]],
// CHECK: simulation.ref.store
// A one-bit dynamic destination stores the converted i1 directly. Equal-width
// extensions are invalid MLIR and must not be constructed.
// CHECK: %[[BIT_ARRAY:.*]] = simulation.container.create {{.*}}container_kind = #simulation.container_kind<dynamic_array>{{.*}} -> !simulation.dynamic_array<i1>
// CHECK-NOT: arith.extui {{.*}} : i1 to i1
// CHECK: simulation.container.write %[[BIT_ARRAY]],
// CHECK: %[[BIT_ARRAY_COPY:.*]] = simulation.container.clone %[[BIT_ARRAY]]
// CHECK: simulation.ref.store %[[BIT_ARRAY_COPY]]
// CHECK-NOT: obelisk.sv.

module {
  obelisk.sv.symbol.definition @s0.dynamic_unpack attributes {definition_kind = 0 : i32, hierarchical_name = "dynamic_unpack", name = "dynamic_unpack", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    obelisk.sv.symbol.instance @s3.dynamic_unpack attributes {hierarchical_name = "dynamic_unpack", is_uninstantiated = false, name = "dynamic_unpack", node_id = 3 : i64, referenced_path = "dynamic_unpack", referenced_symbol = @s0.dynamic_unpack} {
      obelisk.sv.symbol.instance_body @s4.dynamic_unpack attributes {hierarchical_name = "dynamic_unpack", name = "dynamic_unpack", node_id = 4 : i64} {
        obelisk.sv.symbol.variable @s5.source attributes {hierarchical_name = "dynamic_unpack.source", lifetime = 1 : i32, name = "source", node_id = 5 : i64, semantic_type = !obelisk.queue<!obelisk.integral<8, false, false, 7 : 0, byte>, 0>} {
        }
        obelisk.sv.symbol.variable @s6.header attributes {hierarchical_name = "dynamic_unpack.header", lifetime = 1 : i32, name = "header", node_id = 6 : i64, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>} {
        }
        obelisk.sv.symbol.variable @s7.data attributes {hierarchical_name = "dynamic_unpack.data", lifetime = 1 : i32, name = "data", node_id = 7 : i64, semantic_type = !obelisk.dynarray<!obelisk.integral<8, false, false, 7 : 0, byte>>} {
        }
        obelisk.sv.symbol.variable @s8.crc attributes {hierarchical_name = "dynamic_unpack.crc", lifetime = 1 : i32, name = "crc", node_id = 8 : i64, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>} {
        }
        obelisk.sv.symbol.variable @s17.bits attributes {hierarchical_name = "dynamic_unpack.bits", lifetime = 1 : i32, name = "bits", node_id = 17 : i64, semantic_type = !obelisk.dynarray<!obelisk.integral<1, false, false, 0 : 0, bit>>} {
        }
        obelisk.sv.symbol.procedural_block @s9 attributes {hierarchical_name = "dynamic_unpack", node_id = 9 : i64, procedure_kind = 0 : i32} {
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
