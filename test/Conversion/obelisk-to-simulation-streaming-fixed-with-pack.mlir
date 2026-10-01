// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// A with range over a fixed unpacked array selects by the array's declared
// SystemVerilog indices and materializes the runtime-sized selection before
// flattening it into the generic bit stream.
// CHECK-LABEL: simulation.func private @unit_0
// CHECK: %[[STREAM:.*]] = simulation.container.create
// CHECK-SAME: -> !simulation.queue<i1, 0>
// CHECK: %[[R0:.*]] = simulation.ref.subelement {{.*}}[0]
// CHECK: %[[E0:.*]] = simulation.ref.load %[[R0]]
// CHECK: %[[R5:.*]] = simulation.ref.subelement {{.*}}[5]
// CHECK: %[[E5:.*]] = simulation.ref.load %[[R5]]
// CHECK: %[[SELECTED:.*]] = simulation.container.create
// CHECK-SAME: -> !simulation.dynamic_array<i8>
// CHECK: arith.select {{.*}}, %[[E0]],
// CHECK: arith.select {{.*}}, %[[E5]],
// CHECK: simulation.container.write %[[SELECTED]],
// CHECK: simulation.container.size %[[SELECTED]]
// CHECK: simulation.container.read %[[SELECTED]]
// CHECK: simulation.container.write %[[STREAM]],
// CHECK-NOT: obelisk.sv.

module {
  obelisk.sv.symbol.definition @s0.fixed_with_pack attributes {definition_kind = 0 : i32, hierarchical_name = "fixed_with_pack", name = "fixed_with_pack", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    obelisk.sv.symbol.instance @s3.fixed_with_pack attributes {hierarchical_name = "fixed_with_pack", is_uninstantiated = false, name = "fixed_with_pack", node_id = 3 : i64, referenced_path = "fixed_with_pack", referenced_symbol = @s0.fixed_with_pack} {
      obelisk.sv.symbol.instance_body @s4.fixed_with_pack attributes {hierarchical_name = "fixed_with_pack", name = "fixed_with_pack", node_id = 4 : i64} {
        obelisk.sv.symbol.variable @s5.packet attributes {hierarchical_name = "fixed_with_pack.packet", lifetime = 1 : i32, name = "packet", node_id = 5 : i64, semantic_type = !obelisk.queue<!obelisk.integral<8, true, false, 7 : 0, byte>, 0>} {
        }
        obelisk.sv.symbol.variable @s6.data attributes {hierarchical_name = "fixed_with_pack.data", lifetime = 1 : i32, name = "data", node_id = 6 : i64, semantic_type = !obelisk.ranged_unpacked_array<5 : 0 x !obelisk.integral<8, true, false, 7 : 0, byte>>} {
        }
        obelisk.sv.symbol.variable @s7.length attributes {hierarchical_name = "fixed_with_pack.length", lifetime = 1 : i32, name = "length", node_id = 7 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        }
        obelisk.sv.symbol.procedural_block @s8 attributes {hierarchical_name = "fixed_with_pack", node_id = 8 : i64, procedure_kind = 0 : i32} {
          obelisk.sv.statement.expression_statement attributes {node_id = 9 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 10 : i64, semantic_type = !obelisk.queue<!obelisk.integral<8, true, false, 7 : 0, byte>, 0>} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 11 : i64, referenced_path = "fixed_with_pack.packet", referenced_symbol = @s1.$root::@s3.fixed_with_pack::@s4.fixed_with_pack::@s5.packet, semantic_type = !obelisk.queue<!obelisk.integral<8, true, false, 7 : 0, byte>, 0>} {
              }
              obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 12 : i64, semantic_type = !obelisk.queue<!obelisk.integral<8, true, false, 7 : 0, byte>, 0>} {
                obelisk.sv.expression.streaming attributes {bitstream_width = 0 : i64, is_fixed_size = false, is_signed = false, node_id = 13 : i64, semantic_type = !obelisk.void, slice_size = 0 : i64, stream_count = 1 : i64, stream_with_flags = array<i64: 1>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 14 : i64, referenced_path = "fixed_with_pack.data", referenced_symbol = @s1.$root::@s3.fixed_with_pack::@s4.fixed_with_pack::@s6.data, semantic_type = !obelisk.ranged_unpacked_array<5 : 0 x !obelisk.integral<8, true, false, 7 : 0, byte>>} {
                  }
                  obelisk.sv.expression.range_select attributes {is_signed = false, node_id = 15 : i64, selection_kind = 2 : i32, semantic_type = !obelisk.queue<!obelisk.integral<8, true, false, 7 : 0, byte>, 0>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 16 : i64, referenced_path = "fixed_with_pack.data", referenced_symbol = @s1.$root::@s3.fixed_with_pack::@s4.fixed_with_pack::@s6.data, semantic_type = !obelisk.ranged_unpacked_array<5 : 0 x !obelisk.integral<8, true, false, 7 : 0, byte>>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "4", is_declared_unsized = true, is_signed = true, node_id = 17 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 18 : i64, referenced_path = "fixed_with_pack.length", referenced_symbol = @s1.$root::@s3.fixed_with_pack::@s4.fixed_with_pack::@s7.length, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
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
