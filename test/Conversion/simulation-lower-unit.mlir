// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-unit)))' | FileCheck %s

// Drives the unit lowering directly on a prepared code unit, without running
// the frontend. `!logic8` is the elaborated type of `logic [7:0]`.

!logic8 = !obelisk.integral<8, false, true, 7 : 0, logic>

module {
  obelisk_sim.design @units {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<8> design hierarchy "top.a"
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<8> design hierarchy "top.b"

    // CHECK-LABEL: obelisk_sim.func @unit_0
    // A blocking assignment of a literal stores directly.
    // CHECK: %[[ONE:.*]] = obelisk_sim.logic.constant 1 : i8, 0 : i8
    // CHECK: obelisk_sim.ref.store %[[ONE]] to %arg1
    // A delay suspends with the scaled tick count and resumes in a new block.
    // CHECK: %[[D:.*]] = obelisk_sim.time.constant 5
    // CHECK: obelisk_sim.suspend.delay %[[D]] to ^[[RESUME:.*]]
    // CHECK: ^[[RESUME]]:
    // The read of `a` after the delay is a load, and `b <= a` is an NBA.
    // CHECK: %[[A:.*]] = obelisk_sim.ref.load %arg1
    // CHECK: obelisk_sim.nba.enqueue %[[A]] to %arg2
    // CHECK: obelisk_sim.return
    obelisk_sim.func @unit_0(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %a: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %b: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 1 : i32, obelisk_sim.delay_scale = 1 : i64,
                    obelisk_sim.bindings = [{argument = 1 : i64, path = "top.a"},
                                            {argument = 2 : i64, path = "top.b"}]} {
      obelisk.sv.statement.list attributes {node_id = 1 : i64} {
        obelisk.sv.statement.expression_statement attributes {node_id = 2 : i64} {
          obelisk.sv.expression.assignment attributes {node_id = 3 : i64, assignment_kind = 0 : i32, semantic_type = !logic8} {
            obelisk.sv.expression.named_value attributes {node_id = 4 : i64, referenced_path = "top.a", referenced_symbol = @a, semantic_type = !logic8} {
            }
            obelisk.sv.expression.integer_literal attributes {node_id = 5 : i64, constant_value = "8'd1", semantic_type = !logic8} {
            }
          }
        }
        obelisk.sv.statement.timed attributes {node_id = 6 : i64} {
          obelisk.sv.timing.delay attributes {node_id = 7 : i64} {
            obelisk.sv.expression.integer_literal attributes {node_id = 8 : i64, constant_value = "5", semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            }
          }
          obelisk.sv.statement.expression_statement attributes {node_id = 9 : i64} {
            obelisk.sv.expression.assignment attributes {node_id = 10 : i64, assignment_kind = 1 : i32, semantic_type = !logic8} {
              obelisk.sv.expression.named_value attributes {node_id = 11 : i64, referenced_path = "top.b", referenced_symbol = @b, semantic_type = !logic8} {
              }
              obelisk.sv.expression.named_value attributes {node_id = 12 : i64, referenced_path = "top.a", referenced_symbol = @a, semantic_type = !logic8} {
              }
            }
          }
        }
      }
      obelisk_sim.return
    }

    // An always_comb unit closes its loop with a change suspension on exactly
    // the captures it read, and never on the ones it only wrote.
    // CHECK-LABEL: obelisk_sim.func @unit_1
    // CHECK: cf.br ^[[HDR:.*]]
    // CHECK: ^[[HDR]]:
    // CHECK: %[[V:.*]] = obelisk_sim.ref.load %arg1
    // CHECK: obelisk_sim.ref.store %[[V]] to %arg2
    // CHECK: obelisk_sim.suspend.change %arg1 to ^[[HDR]]
    obelisk_sim.func @unit_1(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %a: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %b: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 4 : i32, obelisk_sim.delay_scale = 1 : i64,
                    obelisk_sim.bindings = [{argument = 1 : i64, path = "top.a"},
                                            {argument = 2 : i64, path = "top.b"}]} {
      obelisk.sv.statement.expression_statement attributes {node_id = 20 : i64} {
        obelisk.sv.expression.assignment attributes {node_id = 21 : i64, assignment_kind = 0 : i32, semantic_type = !logic8} {
          obelisk.sv.expression.named_value attributes {node_id = 22 : i64, referenced_path = "top.b", referenced_symbol = @b, semantic_type = !logic8} {
          }
          obelisk.sv.expression.named_value attributes {node_id = 23 : i64, referenced_path = "top.a", referenced_symbol = @a, semantic_type = !logic8} {
          }
        }
      }
      obelisk_sim.return
    }
  }
}
