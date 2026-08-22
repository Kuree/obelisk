// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 11.4.13: an array operand of an `inside` set contributes
// every one of its elements to the membership disjunction.  A queue's element
// count is only known at run time, so the disjunction is accumulated by a loop
// over the container rather than by unrolling it.

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
          hierarchical_name = "t.q", lifetime = 1 : i32, name = "q",
          node_id = 5 : i64,
          semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>,
          sym_name = "s5.q"
        } {
        }
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "t.sel", lifetime = 1 : i32, name = "sel",
          node_id = 6 : i64,
          semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>,
          sym_name = "s6.sel"
        } {
        }
        obelisk.sv.symbol.variable attributes {
          hierarchical_name = "t.r", lifetime = 1 : i32, name = "r",
          node_id = 7 : i64,
          semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>,
          sym_name = "s7.r"
        } {
        }
        obelisk.sv.symbol.procedural_block attributes {
          hierarchical_name = "t", node_id = 8 : i64, procedure_kind = 0 : i32,
          sym_name = "s8", time_precision_fs = 1000000 : i64,
          time_unit_fs = 1000000 : i64
        } {
          obelisk.sv.statement.expression_statement attributes {
            node_id = 9 : i64
          } {
            obelisk.sv.expression.assignment attributes {
              assignment_kind = 0 : i32, is_signed = false, node_id = 10 : i64,
              semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
            } {
              obelisk.sv.expression.named_value attributes {
                is_signed = false, node_id = 11 : i64, referenced_path = "t.r",
                referenced_symbol = @s1.$root::@s3.t::@s4.t::@s7.r,
                semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
              } {
              }
              obelisk.sv.expression.conversion attributes {
                is_signed = false, node_id = 12 : i64,
                semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
              } {
                obelisk.sv.expression.inside attributes {
                  is_signed = false, item_count = 1 : i64, node_id = 13 : i64,
                  semantic_type = !obelisk.integral<1, false, true, 0 : 0, logic>
                } {
                  obelisk.sv.expression.named_value attributes {
                    is_signed = true, node_id = 14 : i64,
                    referenced_path = "t.sel",
                    referenced_symbol = @s1.$root::@s3.t::@s4.t::@s6.sel,
                    semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
                  } {
                  }
                  obelisk.sv.expression.named_value attributes {
                    is_signed = false, node_id = 15 : i64,
                    referenced_path = "t.q",
                    referenced_symbol = @s1.$root::@s3.t::@s4.t::@s5.q,
                    semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>
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



// CHECK-LABEL: obelisk_sim.func private @unit_0
// CHECK-DAG: %[[ZERO:.*]] = arith.constant 0 : i64
// CHECK-DAG: %[[FALSE:.*]] = arith.constant false
// CHECK: %[[SEL:.*]] = obelisk_sim.ref.load %arg2 : !obelisk_sim.ref<i32> -> i32
// CHECK: %[[QUEUE:.*]] = obelisk_sim.ref.load %arg1
// CHECK: %[[SIZE:.*]] = obelisk_sim.container.size %[[QUEUE]]
// CHECK: cf.br ^bb1(%[[ZERO]], %[[FALSE]] : i64, i1)
// CHECK: ^bb1(%[[INDEX:.*]]: i64, %[[ACC:.*]]: i1):
// CHECK: %[[MORE:.*]] = arith.cmpi ult, %[[INDEX]], %[[SIZE]] : i64
// CHECK: cf.cond_br %[[MORE]], ^bb2, ^bb3(%[[ACC]] : i1)
// CHECK: ^bb2:
// CHECK: %[[ELEM:.*]] = obelisk_sim.container.read %[[QUEUE]], %[[INDEX]]
// CHECK: %[[EQ:.*]] = arith.cmpi eq, %[[SEL]], %[[ELEM]] : i32
// CHECK: %[[NEXTACC:.*]] = arith.ori %[[ACC]], %[[EQ]] : i1
// CHECK: ^bb3(%[[RESULT:.*]]: i1):
// CHECK: obelisk_sim.ref.store %[[RESULT]] to %arg3
