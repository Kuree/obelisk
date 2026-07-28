// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {
    definition_kind = 0 : i32,
    hierarchical_name = "container_concat",
    name = "container_concat",
    node_id = 0 : i64,
    sym_name = "s0.container_concat"
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
      hierarchical_name = "container_concat",
      is_uninstantiated = false,
      name = "container_concat",
      node_id = 3 : i64,
      referenced_path = "container_concat",
      referenced_symbol = @s0.container_concat,
      sym_name = "s3.container_concat"
    } {
      obelisk.sv.symbol.instance_body attributes {
        hierarchical_name = "container_concat",
        name = "container_concat",
        node_id = 4 : i64,
        sym_name = "s4.container_concat"
      } {
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "container_concat.array",
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
            obelisk.sv.expression.integer_literal attributes {
              constant_value = "1",
              node_id = 7 : i64,
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
            } {
            }
            obelisk.sv.expression.integer_literal attributes {
              constant_value = "2",
              node_id = 8 : i64,
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
            } {
            }
            obelisk.sv.expression.integer_literal attributes {
              constant_value = "3",
              node_id = 9 : i64,
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
            } {
            }
            obelisk.sv.expression.integer_literal attributes {
              constant_value = "4",
              node_id = 10 : i64,
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
            } {
            }
          }
        }
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "container_concat.queue",
          lifetime = 1 : i32,
          name = "queue",
          node_id = 11 : i64,
          semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>,
          sym_name = "s11.queue"
        } {
        }
        obelisk.sv.symbol.procedural_block attributes {
          hierarchical_name = "container_concat",
          node_id = 12 : i64,
          procedure_kind = 0 : i32,
          sym_name = "s12",
          time_precision_fs = 1000000 : i64,
          time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.expression_statement attributes {
            node_id = 13 : i64
          } {
            obelisk.sv.expression.assignment attributes {
              assignment_kind = 0 : i32,
              node_id = 14 : i64,
              semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>
            } {
              obelisk.sv.expression.named_value attributes {
                node_id = 15 : i64,
                referenced_path = "container_concat.queue",
                referenced_symbol = @s1.$root::@s3.container_concat::@s4.container_concat::@s11.queue,
                semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>
              } {
              }
              obelisk.sv.expression.concatenation attributes {
                node_id = 16 : i64,
                semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>
              } {
                obelisk.sv.expression.named_value attributes {
                  node_id = 17 : i64,
                  referenced_path = "container_concat.queue",
                  referenced_symbol = @s1.$root::@s3.container_concat::@s4.container_concat::@s11.queue,
                  semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>
                } {
                }
                obelisk.sv.expression.integer_literal attributes {
                  constant_value = "42",
                  node_id = 18 : i64,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
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

// Scalar elements determine the dynamic-array allocation size and are written
// in source order.
// CHECK-LABEL: obelisk_sim.func private @unit_0
// CHECK: %[[ARRAY_SIZE:.*]] = arith.constant 4 : i64
// CHECK: %[[ARRAY:.*]] = obelisk_sim.container.create %[[ARRAY_SIZE]]
// CHECK-COUNT-4: obelisk_sim.container.write %[[ARRAY]]

// A container operand is copied element-by-element before the following scalar
// is appended to the new queue.
// CHECK-LABEL: obelisk_sim.func private @unit_1
// CHECK: %[[SOURCE:.*]] = obelisk_sim.ref.load
// CHECK: %[[SOURCE_SIZE:.*]] = obelisk_sim.container.size %[[SOURCE]]
// CHECK: %[[QUEUE:.*]] = obelisk_sim.container.create
// CHECK: cf.cond_br
// CHECK: %[[ELEMENT:.*]] = obelisk_sim.container.read %[[SOURCE]]
// CHECK: obelisk_sim.container.write %[[QUEUE]]
// CHECK: cf.br
// CHECK: obelisk_sim.container.write %[[QUEUE]]
// CHECK: %[[CLONE:.*]] = obelisk_sim.container.clone %[[QUEUE]]
// CHECK: obelisk_sim.ref.store %[[CLONE]]
