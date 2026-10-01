// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition @s0.queue_slice attributes {
    definition_kind = 0 : i32,
    hierarchical_name = "queue_slice",
    name = "queue_slice",
    node_id = 0 : i64
  } {
  }
  obelisk.sv.symbol.root @s1.$root attributes {
    hierarchical_name = "\\$root ",
    name = "$root",
    node_id = 1 : i64
  } {
    obelisk.sv.symbol.compilation_unit @s2 attributes {
      hierarchical_name = "$unit",
      node_id = 2 : i64
    } {
    }
    obelisk.sv.symbol.instance @s3.queue_slice attributes {
      hierarchical_name = "queue_slice",
      is_uninstantiated = false,
      name = "queue_slice",
      node_id = 3 : i64,
      referenced_path = "queue_slice",
      referenced_symbol = @s0.queue_slice
    } {
      obelisk.sv.symbol.instance_body @s4.queue_slice attributes {
        hierarchical_name = "queue_slice",
        name = "queue_slice",
        node_id = 4 : i64
      } {
        obelisk.sv.symbol.variable @s5.queue attributes {
          hierarchical_name = "queue_slice.queue",
          lifetime = 1 : i32,
          name = "queue",
          node_id = 5 : i64,
          semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>
        } {
        }
        obelisk.sv.symbol.variable @s21.element attributes {
          hierarchical_name = "queue_slice.element",
          lifetime = 1 : i32,
          name = "element",
          node_id = 21 : i64,
          semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
        } {
        }
        obelisk.sv.symbol.procedural_block @s6 attributes {
          hierarchical_name = "queue_slice",
          node_id = 6 : i64,
          procedure_kind = 0 : i32,
          time_precision_fs = 1000000 : i64,
          time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.expression_statement attributes {
            node_id = 17 : i64
          } {
            obelisk.sv.expression.assignment attributes {
              assignment_kind = 0 : i32,
              node_id = 18 : i64,
              semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
            } {
              obelisk.sv.expression.named_value attributes {
                node_id = 19 : i64,
                referenced_path = "queue_slice.element",
                referenced_symbol = @s1.$root::@s3.queue_slice::@s4.queue_slice::@s21.element,
                semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
              } {
              }
              obelisk.sv.expression.element_select attributes {
                node_id = 20 : i64,
                semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
              } {
                obelisk.sv.expression.named_value attributes {
                  node_id = 22 : i64,
                  referenced_path = "queue_slice.queue",
                  referenced_symbol = @s1.$root::@s3.queue_slice::@s4.queue_slice::@s5.queue,
                  semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>
                } {
                }
                obelisk.sv.expression.unbounded_literal attributes {
                  node_id = 23 : i64,
                  semantic_type = !obelisk.unbounded
                } {
                }
              }
            }
          }
          obelisk.sv.statement.expression_statement attributes {
            node_id = 7 : i64
          } {
            obelisk.sv.expression.assignment attributes {
              assignment_kind = 0 : i32,
              node_id = 8 : i64,
              semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>
            } {
              obelisk.sv.expression.named_value attributes {
                node_id = 9 : i64,
                referenced_path = "queue_slice.queue",
                referenced_symbol = @s1.$root::@s3.queue_slice::@s4.queue_slice::@s5.queue,
                semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>
              } {
              }
              obelisk.sv.expression.range_select attributes {
                node_id = 10 : i64,
                selection_kind = 0 : i32,
                semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>
              } {
                obelisk.sv.expression.named_value attributes {
                  node_id = 11 : i64,
                  referenced_path = "queue_slice.queue",
                  referenced_symbol = @s1.$root::@s3.queue_slice::@s4.queue_slice::@s5.queue,
                  semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>
                } {
                }
                obelisk.sv.expression.integer_literal attributes {
                  constant_value = "1",
                  node_id = 12 : i64,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                } {
                }
                obelisk.sv.expression.binary_op attributes {
                  node_id = 13 : i64,
                  operator_kind = 1 : i32,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                } {
                  obelisk.sv.expression.conversion attributes {
                    node_id = 14 : i64,
                    semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                  } {
                    obelisk.sv.expression.unbounded_literal attributes {
                      node_id = 15 : i64,
                      semantic_type = !obelisk.unbounded
                    } {
                    }
                  }
                  obelisk.sv.expression.integer_literal attributes {
                    constant_value = "1",
                    node_id = 16 : i64,
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
}

// CHECK-LABEL: simulation.func private @unit_0
// CHECK: %[[ELEMENT_SOURCE:.*]] = simulation.ref.load
// CHECK: %[[ELEMENT_SIZE:.*]] = simulation.container.size %[[ELEMENT_SOURCE]]
// CHECK: %[[ELEMENT_LAST:.*]] = arith.subi %[[ELEMENT_SIZE]],
// CHECK: simulation.container.read %[[ELEMENT_SOURCE]], %[[ELEMENT_LAST]]
// CHECK: %[[SOURCE:.*]] = simulation.ref.load
// CHECK: %[[SIZE:.*]] = simulation.container.size %[[SOURCE]]
// CHECK: %[[LAST:.*]] = arith.subi %[[SIZE]],
// CHECK: %[[NARROW_LAST:.*]] = arith.trunci %[[LAST]]
// CHECK: %[[BOUND32:.*]] = arith.subi %[[NARROW_LAST]],
// CHECK: %[[BOUND:.*]] = arith.extsi %[[BOUND32]]
// CHECK: %[[RESULT:.*]] = simulation.container.create
// CHECK: cf.cond_br
// CHECK: %[[VALUE:.*]] = simulation.container.read %[[SOURCE]]
// CHECK: simulation.container.write %[[RESULT]]
// CHECK: cf.br
// CHECK: %[[CLONE:.*]] = simulation.container.clone %[[RESULT]]
// CHECK: simulation.ref.store %[[CLONE]]
