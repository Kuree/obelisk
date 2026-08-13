// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// A with range over a fixed unpacked array selects by the array's declared
// SystemVerilog indices and materializes the runtime-sized selection before
// flattening it into the generic bit stream.
// CHECK-LABEL: obelisk_sim.func private @unit_0
// CHECK: %[[STREAM:.*]] = obelisk_sim.container.create
// CHECK-SAME: -> !obelisk_sim.queue<i1, 0>
// CHECK: %[[R0:.*]] = obelisk_sim.ref.subelement {{.*}}[0]
// CHECK: %[[E0:.*]] = obelisk_sim.ref.load %[[R0]]
// CHECK: %[[R5:.*]] = obelisk_sim.ref.subelement {{.*}}[5]
// CHECK: %[[E5:.*]] = obelisk_sim.ref.load %[[R5]]
// CHECK: %[[SELECTED:.*]] = obelisk_sim.container.create
// CHECK-SAME: -> !obelisk_sim.dynamic_array<i8>
// CHECK: arith.select {{.*}}, %[[E0]],
// CHECK: arith.select {{.*}}, %[[E5]],
// CHECK: obelisk_sim.container.write %[[SELECTED]],
// CHECK: obelisk_sim.container.size %[[SELECTED]]
// CHECK: obelisk_sim.container.read %[[SELECTED]]
// CHECK: obelisk_sim.container.write %[[STREAM]],
// CHECK-NOT: obelisk.sv.

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "fixed_with_pack", name = "fixed_with_pack", node_id = 0 : i64, sym_name = "s0.fixed_with_pack"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "fixed_with_pack", is_uninstantiated = false, name = "fixed_with_pack", node_id = 3 : i64, referenced_path = "fixed_with_pack", referenced_symbol = @s0.fixed_with_pack, sym_name = "s3.fixed_with_pack"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "fixed_with_pack", name = "fixed_with_pack", node_id = 4 : i64, sym_name = "s4.fixed_with_pack"} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "fixed_with_pack.packet", lifetime = 1 : i32, name = "packet", node_id = 5 : i64, semantic_type = !obelisk.queue<!obelisk.integral<8, true, false, 7 : 0, byte>, 0>, sym_name = "s5.packet"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "fixed_with_pack.data", lifetime = 1 : i32, name = "data", node_id = 6 : i64, semantic_type = !obelisk.ranged_unpacked_array<5 : 0 x !obelisk.integral<8, true, false, 7 : 0, byte>>, sym_name = "s6.data"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "fixed_with_pack.length", lifetime = 1 : i32, name = "length", node_id = 7 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s7.length"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "fixed_with_pack", node_id = 8 : i64, procedure_kind = 0 : i32, sym_name = "s8"} {
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
