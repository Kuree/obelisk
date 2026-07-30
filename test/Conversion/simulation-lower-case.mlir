// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-unit)))' | FileCheck %s

// Direct coverage for ordinary case item grouping and casex comparison kind.

!logic1 = !obelisk.integral<1, false, true, 0 : 0, logic>
!logic2 = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>
!logic8 = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>
!bit8 = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>

module {
  obelisk_sim.design @casex {
    obelisk_sim.code_unit.decl 9800001 in 0 always_comb hierarchy "top.casex"
    obelisk_sim.code_unit.decl 9800002 in 0 always_comb
        hierarchy "top.case_shapes"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 :
        !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>
        design hierarchy "top.selector"
    obelisk_sim.storage.decl 1 in 0 :
        !obelisk_sim.packed_array<7 : 0 x !obelisk_sim.logic<1>>
        design hierarchy "top.value"
    obelisk_sim.storage.decl 2 in 0 :
        !obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>
        design hierarchy "top.case_selector"
    obelisk_sim.storage.decl 3 in 0 :
        !obelisk_sim.packed_array<7 : 0 x !obelisk_sim.logic<1>>
        design hierarchy "top.case_result"

    // CHECK-LABEL: obelisk_sim.func @unit
    // CHECK: obelisk_sim.logic.compare casexz_eq
    // CHECK-NOT: obelisk.sv.
    obelisk_sim.func @unit(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %selector: !obelisk_sim.ref<!obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %value: !obelisk_sim.ref<!obelisk_sim.packed_array<7 : 0 x !obelisk_sim.logic<1>>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 1 : i64})
        attributes {
          entry_kind = 4 : i32,
          obelisk_sim.bindings = [
            #obelisk_sim.argument_binding<path = "top.selector", argument = 1,
                kind = direct, copyOut = false>,
            #obelisk_sim.argument_binding<path = "top.value", argument = 2,
                kind = direct, copyOut = false>
          ],
          code_unit_id = 9800001 : i64
        } {
      obelisk.sv.statement.case attributes {
          check_kind = 0 : i32, condition_kind = 1 : i32,
          has_default = true, item_count = 1 : i64,
          item_label_counts = array<i64: 1>, node_id = 1 : i64} {
        obelisk.sv.expression.named_value attributes {
            node_id = 2 : i64, referenced_path = "top.selector",
            referenced_symbol = @selector, semantic_type = !logic2} {
        }
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "2'b0x", node_id = 3 : i64,
            semantic_type = !logic2} {
        }
        obelisk.sv.statement.expression_statement attributes {
            node_id = 4 : i64} {
          obelisk.sv.expression.assignment attributes {
              assignment_kind = 0 : i32, node_id = 5 : i64,
              semantic_type = !logic8} {
            obelisk.sv.expression.named_value attributes {
                node_id = 6 : i64, referenced_path = "top.value",
                referenced_symbol = @value, semantic_type = !logic8} {
            }
            obelisk.sv.expression.conversion attributes {
                node_id = 7 : i64, semantic_type = !logic8} {
              obelisk.sv.expression.integer_literal attributes {
                  constant_value = "8'd1", node_id = 8 : i64,
                  semantic_type = !bit8} {
              }
            }
          }
        }
        obelisk.sv.statement.expression_statement attributes {
            node_id = 9 : i64} {
          obelisk.sv.expression.assignment attributes {
              assignment_kind = 0 : i32, node_id = 10 : i64,
              semantic_type = !logic8} {
            obelisk.sv.expression.named_value attributes {
                node_id = 11 : i64, referenced_path = "top.value",
                referenced_symbol = @value, semantic_type = !logic8} {
            }
            obelisk.sv.expression.conversion attributes {
                node_id = 12 : i64, semantic_type = !logic8} {
              obelisk.sv.expression.integer_literal attributes {
                  constant_value = "8'd0", node_id = 13 : i64,
                  semantic_type = !bit8} {
              }
            }
          }
        }
      }
      obelisk_sim.return
    }

    // Labels are grouped by item even though all labels precede all bodies in
    // the semantic operation. The default body is the final child.
    // CHECK-LABEL: obelisk_sim.func @case_shapes
    // CHECK: %[[SEL_AGG:.*]] = obelisk_sim.ref.load %arg1
    // CHECK: %[[SEL:.*]] = obelisk_sim.packed.flatten %[[SEL_AGG]]
    // CHECK: %[[EQ0:.*]] = obelisk_sim.logic.compare case_eq %[[SEL]]
    // CHECK: cf.cond_br %[[EQ0]], ^[[ITEM0:.*]], ^[[NEXT0:.*]]
    // CHECK: ^[[ITEM0]]:
    // CHECK: obelisk_sim.ref.store
    // CHECK: ^[[NEXT0]]:
    // CHECK: %[[EQ1:.*]] = obelisk_sim.logic.compare case_eq
    // CHECK: cf.cond_br %[[EQ1]], ^[[ITEM1:.*]], ^[[LABEL1:.*]]
    // CHECK: ^[[ITEM1]]:
    // CHECK: obelisk_sim.ref.store
    // CHECK: ^[[DEFAULT:bb[0-9]+]]:
    // CHECK-NOT: obelisk_sim.logic.compare
    // CHECK: obelisk_sim.ref.store
    // CHECK: ^[[LABEL1]]:
    // CHECK: %[[EQ2:.*]] = obelisk_sim.logic.compare case_eq
    // CHECK: cf.cond_br %[[EQ2]], ^[[ITEM1]], ^[[DEFAULT]]
    obelisk_sim.func @case_shapes(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %selector: !obelisk_sim.ref<!obelisk_sim.packed_array<1 : 0 x !obelisk_sim.logic<1>>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 2 : i64},
        %result: !obelisk_sim.ref<!obelisk_sim.packed_array<7 : 0 x !obelisk_sim.logic<1>>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 3 : i64})
        attributes {
          entry_kind = 4 : i32,
          obelisk_sim.bindings = [
            #obelisk_sim.argument_binding<path = "top.case_selector",
                argument = 1, kind = direct, copyOut = false>,
            #obelisk_sim.argument_binding<path = "top.case_result",
                argument = 2, kind = direct, copyOut = false>
          ],
          code_unit_id = 9800002 : i64
        } {
      obelisk.sv.statement.case attributes {
          check_kind = 0 : i32, condition_kind = 0 : i32,
          has_default = true, item_count = 2 : i64,
          item_label_counts = array<i64: 1, 2>, node_id = 20 : i64} {
        obelisk.sv.expression.named_value attributes {
            node_id = 21 : i64, referenced_path = "top.case_selector",
            referenced_symbol = @case_selector, semantic_type = !logic2} {
        }
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "2'd0", node_id = 22 : i64,
            semantic_type = !logic2} {
        }
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "2'd1", node_id = 23 : i64,
            semantic_type = !logic2} {
        }
        obelisk.sv.expression.integer_literal attributes {
            constant_value = "2'd2", node_id = 24 : i64,
            semantic_type = !logic2} {
        }
        obelisk.sv.statement.expression_statement attributes {
            node_id = 25 : i64} {
          obelisk.sv.expression.assignment attributes {
              assignment_kind = 0 : i32, node_id = 26 : i64,
              semantic_type = !logic8} {
            obelisk.sv.expression.named_value attributes {
                node_id = 27 : i64, referenced_path = "top.case_result",
                referenced_symbol = @case_result, semantic_type = !logic8} {
            }
            obelisk.sv.expression.integer_literal attributes {
                constant_value = "8'h10", node_id = 28 : i64,
                semantic_type = !logic8} {
            }
          }
        }
        obelisk.sv.statement.expression_statement attributes {
            node_id = 29 : i64} {
          obelisk.sv.expression.assignment attributes {
              assignment_kind = 0 : i32, node_id = 30 : i64,
              semantic_type = !logic8} {
            obelisk.sv.expression.named_value attributes {
                node_id = 31 : i64, referenced_path = "top.case_result",
                referenced_symbol = @case_result, semantic_type = !logic8} {
            }
            obelisk.sv.expression.integer_literal attributes {
                constant_value = "8'h20", node_id = 32 : i64,
                semantic_type = !logic8} {
            }
          }
        }
        obelisk.sv.statement.expression_statement attributes {
            node_id = 33 : i64} {
          obelisk.sv.expression.assignment attributes {
              assignment_kind = 0 : i32, node_id = 34 : i64,
              semantic_type = !logic8} {
            obelisk.sv.expression.named_value attributes {
                node_id = 35 : i64, referenced_path = "top.case_result",
                referenced_symbol = @case_result, semantic_type = !logic8} {
            }
            obelisk.sv.expression.integer_literal attributes {
                constant_value = "8'hff", node_id = 36 : i64,
                semantic_type = !logic8} {
            }
          }
        }
      }
      obelisk_sim.return
    }
  }
}
