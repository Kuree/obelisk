// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-unit)))' | FileCheck %s

// Drives the unit lowering directly on a prepared code unit, without running
// the frontend. `!logic8` is the elaborated type of `logic [7:0]`.

!logic8 = !obelisk.integral<8, false, true, 7 : 0, logic>
!logic1 = !obelisk.integral<1, false, true, 0 : 0, logic>
!logic8_declared = !obelisk.ranged_packed_array<15 : 8 x !obelisk.integral<1, false, true, 0 : 0, logic>>
!logic64 = !obelisk.integral<64, false, true, 63 : 0, logic>
!packed_logic8 = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>
!packed_logic32 = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>

module {
  obelisk_sim.design @units {
    obelisk_sim.code_unit.decl 9000001 in 0 initial hierarchy "test.units.unit_0.9000001"
    obelisk_sim.code_unit.decl 9000002 in 0 always_comb hierarchy "test.units.unit_1.9000002"
    obelisk_sim.code_unit.decl 9000003 in 0 initial hierarchy "test.units.unit_2.9000003"
    obelisk_sim.code_unit.decl 9000004 in 0 initial hierarchy "test.units.unit_3.9000004"
    obelisk_sim.code_unit.decl 9000005 in 0 function hierarchy "test.units.compare.9000005"
    obelisk_sim.code_unit.decl 9000006 in 0 initial hierarchy "test.units.compound.9000006"
    obelisk_sim.code_unit.decl 9000007 in 0 always_latch hierarchy "test.units.latch.9000007"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<8> design hierarchy "top.a"
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<8> design hierarchy "top.b"
    obelisk_sim.storage.decl 2 in 0 : !obelisk_sim.logic<1> design hierarchy "top.selected"
    obelisk_sim.storage.decl 3 in 0 :
        !obelisk_sim.packed_array<7 : 0 x !obelisk_sim.logic<1>>
        design hierarchy "top.value"

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
                    obelisk_sim.bindings = [
                      #obelisk_sim.argument_binding<path = "top.a", argument = 1, kind = direct, copyOut = false>,
                      #obelisk_sim.argument_binding<path = "top.b", argument = 2, kind = direct, copyOut = false>],
                    code_unit_id = 9000001 : i64} {
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
    // An always_comb publication remains an active driver beneath force, so
    // release can reveal its latest value without waiting for another input
    // transition.
    // CHECK: obelisk_sim.ref.store %[[V]] to %arg2 {obelisk_sim.continuous_store}
    // CHECK: obelisk_sim.suspend.change %arg1 to ^[[HDR]]
    obelisk_sim.func @unit_1(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %a: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %b: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 4 : i32, obelisk_sim.delay_scale = 1 : i64,
                    obelisk_sim.bindings = [
                      #obelisk_sim.argument_binding<path = "top.a", argument = 1, kind = direct, copyOut = false>,
                      #obelisk_sim.argument_binding<path = "top.b", argument = 2, kind = direct, copyOut = false>],
                    code_unit_id = 9000002 : i64} {
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

    // always_latch has the same persistent procedural-continuous publication
    // semantics as always_comb, while still suspending on its read set.
    // CHECK-LABEL: obelisk_sim.func @latch
    // CHECK: %[[LATCH_V:.*]] = obelisk_sim.ref.load %arg1
    // CHECK: obelisk_sim.ref.store %[[LATCH_V]] to %arg2 {obelisk_sim.continuous_store}
    // CHECK: obelisk_sim.suspend.change %arg1
    obelisk_sim.func @latch(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %a: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %b: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 6 : i32, obelisk_sim.delay_scale = 1 : i64,
                    obelisk_sim.bindings = [
                      #obelisk_sim.argument_binding<path = "top.a", argument = 1, kind = direct, copyOut = false>,
                      #obelisk_sim.argument_binding<path = "top.b", argument = 2, kind = direct, copyOut = false>],
                    code_unit_id = 9000007 : i64} {
      obelisk.sv.statement.expression_statement attributes {node_id = 70 : i64} {
        obelisk.sv.expression.assignment attributes {node_id = 71 : i64, assignment_kind = 0 : i32, semantic_type = !logic8} {
          obelisk.sv.expression.named_value attributes {node_id = 72 : i64, referenced_path = "top.b", referenced_symbol = @b, semantic_type = !logic8} {
          }
          obelisk.sv.expression.named_value attributes {node_id = 73 : i64, referenced_path = "top.a", referenced_symbol = @a, semantic_type = !logic8} {
          }
        }
      }
      obelisk_sim.return
    }

    // Constant selection offsets use APInt rather than host signed arithmetic.
    // The unsigned index 2^64-1 minus declared right bound 8 requires 65 value
    // bits and remains explicitly out of range in the dynamic selection.
    // CHECK-LABEL: obelisk_sim.func @unit_2
    // CHECK: %[[WIDE_LOW:.*]] = obelisk_sim.logic.constant 18446744073709551607 : i66, 0 : i66 : !obelisk_sim.logic<66>
    // CHECK: obelisk_sim.logic.dyn_extract {{.*}} from %[[WIDE_LOW]]
    obelisk_sim.func @unit_2(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %a: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %selected: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64})
        attributes {entry_kind = 1 : i32, obelisk_sim.delay_scale = 1 : i64,
                    obelisk_sim.bindings = [
                      #obelisk_sim.argument_binding<path = "top.a", argument = 1, kind = direct, copyOut = false>,
                      #obelisk_sim.argument_binding<path = "top.selected", argument = 2, kind = direct, copyOut = false>],
                    code_unit_id = 9000003 : i64} {
      obelisk.sv.statement.expression_statement attributes {node_id = 30 : i64} {
        obelisk.sv.expression.assignment attributes {node_id = 31 : i64, assignment_kind = 0 : i32, semantic_type = !logic1} {
          obelisk.sv.expression.named_value attributes {node_id = 32 : i64, referenced_path = "top.selected", referenced_symbol = @selected, semantic_type = !logic1} {
          }
          obelisk.sv.expression.element_select attributes {node_id = 33 : i64, semantic_type = !logic1} {
            obelisk.sv.expression.named_value attributes {node_id = 34 : i64, referenced_path = "top.a", referenced_symbol = @a, semantic_type = !logic8_declared} {
            }
            obelisk.sv.expression.integer_literal attributes {node_id = 35 : i64, constant_value = "64'hffffffffffffffff", semantic_type = !logic64} {
            }
          }
        }
      }
      obelisk_sim.return
    }

    // Prepared elaborated constants materialize as ordinary simulation SSA;
    // they do not consume a runtime function argument.
    // CHECK-LABEL: obelisk_sim.func @unit_3
    // CHECK: %[[PARAM:.*]] = obelisk_sim.logic.constant -91 : i8, 0 : i8
    // CHECK: obelisk_sim.ref.store %[[PARAM]] to %arg1
    obelisk_sim.func @unit_3(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %b: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 1 : i32,
                    obelisk_sim.bindings = [
                      #obelisk_sim.argument_binding<path = "top.b", argument = 1, kind = direct, copyOut = false>,
                      #obelisk_sim.constant_binding<path = "top.P", value = #obelisk_sim.frozen_constant<value = [-91 : i8, 0 : i8], isSigned = false> : !obelisk_sim.logic<8>>],
                    code_unit_id = 9000004 : i64} {
      obelisk.sv.statement.expression_statement attributes {node_id = 40 : i64} {
        obelisk.sv.expression.assignment attributes {node_id = 41 : i64, assignment_kind = 0 : i32, semantic_type = !logic8} {
          obelisk.sv.expression.named_value attributes {node_id = 42 : i64, referenced_path = "top.b", referenced_symbol = @b, semantic_type = !logic8} {
          }
          obelisk.sv.expression.named_value attributes {node_id = 43 : i64, referenced_path = "top.P", referenced_symbol = @P, semantic_type = !logic8} {
          }
        }
      }
      obelisk_sim.return
    }

    // Source equality and inequality map directly to four-state simulation
    // comparisons. Their known-dominance and unresolved truth tables are
    // exercised by simulation-to-standard-exec.mlir.
    // CHECK-LABEL: obelisk_sim.func @compare
    // CHECK: %[[EQ_LHS:.*]] = obelisk_sim.ref.load %arg1
    // CHECK: %[[EQ_RHS:.*]] = obelisk_sim.ref.load %arg2
    // CHECK: obelisk_sim.logic.compare eq %[[EQ_LHS]], %[[EQ_RHS]]
    // CHECK: %[[NE_LHS:.*]] = obelisk_sim.ref.load %arg1
    // CHECK: %[[NE_RHS:.*]] = obelisk_sim.ref.load %arg2
    // CHECK: obelisk_sim.logic.compare ne %[[NE_LHS]], %[[NE_RHS]]
    obelisk_sim.func @compare(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %a: !obelisk_sim.ref<!obelisk_sim.logic<8>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %b: !obelisk_sim.ref<!obelisk_sim.logic<8>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 1 : i64})
        attributes {
          entry_kind = 8 : i32,
          obelisk_sim.bindings = [
            #obelisk_sim.argument_binding<path = "top.a", argument = 1,
                kind = direct, copyOut = false>,
            #obelisk_sim.argument_binding<path = "top.b", argument = 2,
                kind = direct, copyOut = false>
          ],
          code_unit_id = 9000005 : i64,
          obelisk_sim.void_function
        } {
      obelisk.sv.statement.expression_statement attributes {node_id = 50 : i64} {
        obelisk.sv.expression.binary_op attributes {
            node_id = 51 : i64, operator_kind = 9 : i32,
            semantic_type = !logic1} {
          obelisk.sv.expression.named_value attributes {
              node_id = 52 : i64, referenced_path = "top.a",
              referenced_symbol = @a, semantic_type = !logic8} {
          }
          obelisk.sv.expression.named_value attributes {
              node_id = 53 : i64, referenced_path = "top.b",
              referenced_symbol = @b, semantic_type = !logic8} {
          }
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 54 : i64} {
        obelisk.sv.expression.binary_op attributes {
            node_id = 55 : i64, operator_kind = 10 : i32,
            semantic_type = !logic1} {
          obelisk.sv.expression.named_value attributes {
              node_id = 56 : i64, referenced_path = "top.a",
              referenced_symbol = @a, semantic_type = !logic8} {
          }
          obelisk.sv.expression.named_value attributes {
              node_id = 57 : i64, referenced_path = "top.b",
              referenced_symbol = @b, semantic_type = !logic8} {
          }
        }
      }
      obelisk_sim.return
    }

    // Compound assignments carry the destination through a semantic
    // lvalue-reference placeholder. Lowering resolves it to the old value,
    // applies the usual arithmetic conversions, and stores back through the
    // original reference.
    // CHECK-LABEL: obelisk_sim.func @compound
    // CHECK: %[[OLD:.*]] = obelisk_sim.ref.load [[DEST:%[a-zA-Z0-9]+]]
    // CHECK: %[[FLAT:.*]] = obelisk_sim.packed.flatten %[[OLD]]
    // CHECK: %[[WIDE:.*]] = obelisk_sim.logic.resize %[[FLAT]]
    // CHECK: %[[SUM:.*]] = obelisk_sim.logic.binary add
    // CHECK: %[[SUM_FLAT:.*]] = obelisk_sim.packed.flatten
    // CHECK: %[[NARROW:.*]] = obelisk_sim.logic.resize %[[SUM_FLAT]]
    // CHECK: %[[STORED:.*]] = obelisk_sim.packed.unflatten %[[NARROW]]
    // CHECK: obelisk_sim.ref.store %[[STORED]] to [[DEST]]
    obelisk_sim.func @compound(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.ref<!obelisk_sim.packed_array<7 : 0 x !obelisk_sim.logic<1>>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 3 : i64})
        attributes {
          entry_kind = 1 : i32,
          obelisk_sim.bindings = [
            #obelisk_sim.argument_binding<path = "top.value", argument = 1,
                kind = direct, copyOut = false>
          ],
          code_unit_id = 9000006 : i64
        } {
      obelisk.sv.statement.expression_statement attributes {
          node_id = 60 : i64} {
        obelisk.sv.expression.assignment attributes {
            assignment_kind = 0 : i32, node_id = 61 : i64,
            operator_kind = 0 : i32, semantic_type = !packed_logic8} {
          obelisk.sv.expression.named_value attributes {
              node_id = 62 : i64, referenced_path = "top.value",
              referenced_symbol = @value,
              semantic_type = !packed_logic8} {
          }
          obelisk.sv.expression.conversion attributes {
              node_id = 63 : i64, semantic_type = !packed_logic8} {
            obelisk.sv.expression.binary_op attributes {
                node_id = 64 : i64, operator_kind = 0 : i32,
                semantic_type = !packed_logic32} {
              obelisk.sv.expression.conversion attributes {
                  node_id = 65 : i64,
                  semantic_type = !packed_logic32} {
                obelisk.sv.expression.l_value_reference attributes {
                    node_id = 66 : i64,
                    semantic_type = !packed_logic8} {
                }
              }
              obelisk.sv.expression.conversion attributes {
                  node_id = 67 : i64,
                  semantic_type = !packed_logic32} {
                obelisk.sv.expression.integer_literal attributes {
                    constant_value = "1", node_id = 68 : i64,
                    semantic_type =
                        !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
        }
      }
      obelisk_sim.return
    }
  }
}
