// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {
    definition_kind = 0 : i32,
    hierarchical_name = "empty_container_concat",
    name = "empty_container_concat",
    node_id = 0 : i64,
    sym_name = "s0.empty_container_concat"
  } {
  }
  obelisk.sv.symbol.root attributes {
    hierarchical_name = "\\$root ",
    name = "$root",
    node_id = 1 : i64,
    sym_name = "s1.$root"
  } {
    obelisk.sv.symbol.compilation_unit attributes {
      hierarchical_name = "$unit",
      node_id = 2 : i64,
      sym_name = "s2"
    } {
    }
    obelisk.sv.symbol.instance attributes {
      hierarchical_name = "empty_container_concat",
      is_uninstantiated = false,
      name = "empty_container_concat",
      node_id = 3 : i64,
      referenced_path = "empty_container_concat",
      referenced_symbol = @s0.empty_container_concat,
      sym_name = "s3.empty_container_concat"
    } {
      obelisk.sv.symbol.instance_body attributes {
        hierarchical_name = "empty_container_concat",
        name = "empty_container_concat",
        node_id = 4 : i64,
        sym_name = "s4.empty_container_concat"
      } {
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "empty_container_concat.array",
          lifetime = 1 : i32,
          name = "array",
          node_id = 5 : i64,
          semantic_type = !obelisk.dynarray<!obelisk.integral<32, true, false, 31 : 0, int>>,
          sym_name = "s5.array"
        } {
          obelisk.sv.expression.concatenation attributes {
            node_id = 6 : i64,
            semantic_type = !obelisk.dynarray<!obelisk.integral<32, true, false, 31 : 0, int>>
          } {
          }
        }
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "empty_container_concat.queue",
          lifetime = 1 : i32,
          name = "queue",
          node_id = 7 : i64,
          semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>,
          sym_name = "s7.queue"
        } {
          obelisk.sv.expression.concatenation attributes {
            node_id = 8 : i64,
            semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>
          } {
          }
        }
      }
    }
  }
}

// CHECK-LABEL: obelisk_sim.func private @unit_0
// CHECK: %[[ZERO:.*]] = arith.constant 0 : i64
// CHECK: %[[ARRAY:.*]] = obelisk_sim.container.create %[[ZERO]]
// CHECK-SAME: container_kind = 1
// CHECK: obelisk_sim.ref.store %[[ARRAY]]

// CHECK-LABEL: obelisk_sim.func private @unit_1
// CHECK: %[[QUEUE_ZERO:.*]] = arith.constant 0 : i64
// CHECK: %[[QUEUE:.*]] = obelisk_sim.container.create %[[QUEUE_ZERO]]
// CHECK-SAME: container_kind = 2
// CHECK: obelisk_sim.ref.store %[[QUEUE]]
