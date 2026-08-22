// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 7.6: a fixed-size unpacked array is assignment compatible
// with a dynamic array of an equivalent element type; the target is resized to
// the source's element count and the elements correspond left to right.  The
// source count is known, so the container is created at that size and filled
// by ordinal.

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
          hierarchical_name = "t.src", lifetime = 1 : i32, name = "src",
          node_id = 5 : i64,
          semantic_type = !obelisk.ranged_unpacked_array<0 : 2 x !obelisk.integral<32, true, false, 31 : 0, int>>,
          sym_name = "s5.src"
        } {
        }
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "t.dst", lifetime = 1 : i32, name = "dst",
          node_id = 6 : i64,
          semantic_type = !obelisk.dynarray<!obelisk.integral<32, true, false, 31 : 0, int>>,
          sym_name = "s6.dst"
        } {
        }
        obelisk.sv.symbol.procedural_block attributes {
          hierarchical_name = "t", node_id = 7 : i64, procedure_kind = 0 : i32,
          sym_name = "s7", time_precision_fs = 1000000 : i64,
          time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.expression_statement attributes {
            node_id = 8 : i64
          } {
            obelisk.sv.expression.assignment attributes {
              assignment_kind = 0 : i32, is_signed = false, node_id = 9 : i64,
              semantic_type = !obelisk.dynarray<!obelisk.integral<32, true, false, 31 : 0, int>>
            } {
              obelisk.sv.expression.named_value attributes {
                is_signed = false, node_id = 10 : i64,
                referenced_path = "t.dst",
                referenced_symbol = @s1.$root::@s3.t::@s4.t::@s6.dst,
                semantic_type = !obelisk.dynarray<!obelisk.integral<32, true, false, 31 : 0, int>>
              } {
              }
              obelisk.sv.expression.conversion attributes {
                is_signed = false, node_id = 11 : i64,
                semantic_type = !obelisk.dynarray<!obelisk.integral<32, true, false, 31 : 0, int>>
              } {
                obelisk.sv.expression.named_value attributes {
                  is_signed = false, node_id = 12 : i64,
                  referenced_path = "t.src",
                  referenced_symbol = @s1.$root::@s3.t::@s4.t::@s5.src,
                  semantic_type = !obelisk.ranged_unpacked_array<0 : 2 x !obelisk.integral<32, true, false, 31 : 0, int>>
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



// CHECK-LABEL: obelisk_sim.func private @unit_0
// CHECK-DAG: %[[C0:.*]] = arith.constant 0 : i64
// CHECK-DAG: %[[C1:.*]] = arith.constant 1 : i64
// CHECK-DAG: %[[C2:.*]] = arith.constant 2 : i64
// CHECK-DAG: %[[C3:.*]] = arith.constant 3 : i64
// CHECK: %[[E0:.*]] = obelisk_sim.ref.load %{{.*}} : !obelisk_sim.ref<i32> -> i32
// CHECK: %[[E1:.*]] = obelisk_sim.ref.load %{{.*}} : !obelisk_sim.ref<i32> -> i32
// CHECK: %[[E2:.*]] = obelisk_sim.ref.load %{{.*}} : !obelisk_sim.ref<i32> -> i32
// CHECK: %[[ARRAY:.*]] = obelisk_sim.container.create %[[C3]]
// CHECK-SAME: -> !obelisk_sim.dynamic_array<i32>
// CHECK: obelisk_sim.container.write %[[ARRAY]], %[[C0]], %[[E0]]
// CHECK: obelisk_sim.container.write %[[ARRAY]], %[[C1]], %[[E1]]
// CHECK: obelisk_sim.container.write %[[ARRAY]], %[[C2]], %[[E2]]
