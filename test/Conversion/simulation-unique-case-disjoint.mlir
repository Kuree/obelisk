// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk-sim-prepare-unit-lowering,simulation.design(simulation.func(obelisk-sim-lower-unit)))' | FileCheck %s
// Exact constant item sets are disjoint even with X/Z. Duplicates within one
// item are legal. Wildcard selectors can match several distinct labels.
!logic2 = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>
module {
  simulation.design @disjoint {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.packed_array<1 : 0 x !simulation.logic<1>> design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "disjoint"
    // CHECK-LABEL: simulation.func @disjoint
    simulation.func @disjoint(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %selector: !simulation.ref<!simulation.packed_array<1 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64, simulation.bindings = [#simulation.argument_binding<path = "top.selector", argument = 1, kind = direct, copyOut = false>]} {
      obelisk.sv.statement.case attributes {check_kind = 1 : i32, condition_kind = 0 : i32, has_default = true, item_count = 2 : i64, item_label_counts = array<i64: 1, 1>, node_id = 1 : i64} {
        obelisk.sv.expression.named_value attributes {node_id = 2 : i64, referenced_path = "top.selector", referenced_symbol = @selector, semantic_type = !logic2} {}
        obelisk.sv.expression.integer_literal attributes {constant_value = "2'b00", node_id = 3 : i64, semantic_type = !logic2} {}
        obelisk.sv.expression.integer_literal attributes {constant_value = "2'b01", node_id = 4 : i64, semantic_type = !logic2} {}
        obelisk.sv.statement.empty attributes {node_id = 5 : i64} {}
        obelisk.sv.statement.empty attributes {node_id = 6 : i64} {}
        obelisk.sv.statement.empty attributes {node_id = 7 : i64} {}
      }
      simulation.return
    }
  }
// CHECK-NOT: simulation.display
// CHECK-NOT: multiple matches
  simulation.design @same_item {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.packed_array<1 : 0 x !simulation.logic<1>> design
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "same_item"
    // CHECK-LABEL: simulation.func @same_item
    simulation.func @same_item(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %selector: !simulation.ref<!simulation.packed_array<1 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64, simulation.bindings = [#simulation.argument_binding<path = "top.selector", argument = 1, kind = direct, copyOut = false>]} {
      obelisk.sv.statement.case attributes {check_kind = 1 : i32, condition_kind = 0 : i32, has_default = true, item_count = 2 : i64, item_label_counts = array<i64: 2, 1>, node_id = 1 : i64} {
        obelisk.sv.expression.named_value attributes {node_id = 2 : i64, referenced_path = "top.selector", referenced_symbol = @selector, semantic_type = !logic2} {}
        obelisk.sv.expression.integer_literal attributes {constant_value = "2'b00", node_id = 3 : i64, semantic_type = !logic2} {}
        obelisk.sv.expression.integer_literal attributes {constant_value = "2'b00", node_id = 4 : i64, semantic_type = !logic2} {}
        obelisk.sv.expression.integer_literal attributes {constant_value = "2'b01", node_id = 5 : i64, semantic_type = !logic2} {}
        obelisk.sv.statement.empty attributes {node_id = 6 : i64} {}
        obelisk.sv.statement.empty attributes {node_id = 7 : i64} {}
        obelisk.sv.statement.empty attributes {node_id = 8 : i64} {}
      }
      simulation.return
    }
  }
// CHECK-NOT: simulation.display
// CHECK-NOT: multiple matches
  simulation.design @overlap {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.packed_array<1 : 0 x !simulation.logic<1>> design
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "overlap"
    // CHECK-LABEL: simulation.func @overlap
    simulation.func @overlap(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %selector: !simulation.ref<!simulation.packed_array<1 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64, simulation.bindings = [#simulation.argument_binding<path = "top.selector", argument = 1, kind = direct, copyOut = false>]} {
      obelisk.sv.statement.case attributes {check_kind = 1 : i32, condition_kind = 0 : i32, has_default = true, item_count = 2 : i64, item_label_counts = array<i64: 1, 1>, node_id = 1 : i64} {
        obelisk.sv.expression.named_value attributes {node_id = 2 : i64, referenced_path = "top.selector", referenced_symbol = @selector, semantic_type = !logic2} {}
        obelisk.sv.expression.integer_literal attributes {constant_value = "2'b00", node_id = 3 : i64, semantic_type = !logic2} {}
        obelisk.sv.expression.integer_literal attributes {constant_value = "2'b00", node_id = 4 : i64, semantic_type = !logic2} {}
        obelisk.sv.statement.empty attributes {node_id = 5 : i64} {}
        obelisk.sv.statement.empty attributes {node_id = 6 : i64} {}
        obelisk.sv.statement.empty attributes {node_id = 7 : i64} {}
      }
      simulation.return
    }
  }
// CHECK: multiple matches
  simulation.design @unknown_distinct {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.packed_array<1 : 0 x !simulation.logic<1>> design
    simulation.code_unit.decl 4 in 0 always_comb hierarchy "unknown_distinct"
    // CHECK-LABEL: simulation.func @unknown_distinct
    simulation.func @unknown_distinct(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %selector: !simulation.ref<!simulation.packed_array<1 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 4 : i64, simulation.bindings = [#simulation.argument_binding<path = "top.selector", argument = 1, kind = direct, copyOut = false>]} {
      obelisk.sv.statement.case attributes {check_kind = 1 : i32, condition_kind = 0 : i32, has_default = true, item_count = 2 : i64, item_label_counts = array<i64: 1, 1>, node_id = 1 : i64} {
        obelisk.sv.expression.named_value attributes {node_id = 2 : i64, referenced_path = "top.selector", referenced_symbol = @selector, semantic_type = !logic2} {}
        obelisk.sv.expression.integer_literal attributes {constant_value = "2'bx0", node_id = 3 : i64, semantic_type = !logic2} {}
        obelisk.sv.expression.integer_literal attributes {constant_value = "2'bz0", node_id = 4 : i64, semantic_type = !logic2} {}
        obelisk.sv.statement.empty attributes {node_id = 5 : i64} {}
        obelisk.sv.statement.empty attributes {node_id = 6 : i64} {}
        obelisk.sv.statement.empty attributes {node_id = 7 : i64} {}
      }
      simulation.return
    }
  }
// CHECK-NOT: simulation.display
// CHECK-NOT: multiple matches
  simulation.design @casez {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.packed_array<1 : 0 x !simulation.logic<1>> design
    simulation.code_unit.decl 5 in 0 always_comb hierarchy "casez"
    // CHECK-LABEL: simulation.func @casez
    simulation.func @casez(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %selector: !simulation.ref<!simulation.packed_array<1 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 5 : i64, simulation.bindings = [#simulation.argument_binding<path = "top.selector", argument = 1, kind = direct, copyOut = false>]} {
      obelisk.sv.statement.case attributes {check_kind = 1 : i32, condition_kind = 2 : i32, has_default = true, item_count = 2 : i64, item_label_counts = array<i64: 1, 1>, node_id = 1 : i64} {
        obelisk.sv.expression.named_value attributes {node_id = 2 : i64, referenced_path = "top.selector", referenced_symbol = @selector, semantic_type = !logic2} {}
        obelisk.sv.expression.integer_literal attributes {constant_value = "2'b00", node_id = 3 : i64, semantic_type = !logic2} {}
        obelisk.sv.expression.integer_literal attributes {constant_value = "2'b01", node_id = 4 : i64, semantic_type = !logic2} {}
        obelisk.sv.statement.empty attributes {node_id = 5 : i64} {}
        obelisk.sv.statement.empty attributes {node_id = 6 : i64} {}
        obelisk.sv.statement.empty attributes {node_id = 7 : i64} {}
      }
      simulation.return
    }
  }
// CHECK: multiple matches
  simulation.design @casex {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.packed_array<1 : 0 x !simulation.logic<1>> design
    simulation.code_unit.decl 6 in 0 always_comb hierarchy "casex"
    // CHECK-LABEL: simulation.func @casex
    simulation.func @casex(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %selector: !simulation.ref<!simulation.packed_array<1 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 6 : i64, simulation.bindings = [#simulation.argument_binding<path = "top.selector", argument = 1, kind = direct, copyOut = false>]} {
      obelisk.sv.statement.case attributes {check_kind = 1 : i32, condition_kind = 1 : i32, has_default = true, item_count = 2 : i64, item_label_counts = array<i64: 1, 1>, node_id = 1 : i64} {
        obelisk.sv.expression.named_value attributes {node_id = 2 : i64, referenced_path = "top.selector", referenced_symbol = @selector, semantic_type = !logic2} {}
        obelisk.sv.expression.integer_literal attributes {constant_value = "2'b00", node_id = 3 : i64, semantic_type = !logic2} {}
        obelisk.sv.expression.integer_literal attributes {constant_value = "2'b01", node_id = 4 : i64, semantic_type = !logic2} {}
        obelisk.sv.statement.empty attributes {node_id = 5 : i64} {}
        obelisk.sv.statement.empty attributes {node_id = 6 : i64} {}
        obelisk.sv.statement.empty attributes {node_id = 7 : i64} {}
      }
      simulation.return
    }
  }
// CHECK: multiple matches
  simulation.design @no_default {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.packed_array<1 : 0 x !simulation.logic<1>> design
    simulation.code_unit.decl 7 in 0 always_comb hierarchy "no_default"
    // CHECK-LABEL: simulation.func @no_default
    simulation.func @no_default(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %selector: !simulation.ref<!simulation.packed_array<1 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 7 : i64, simulation.bindings = [#simulation.argument_binding<path = "top.selector", argument = 1, kind = direct, copyOut = false>]} {
      obelisk.sv.statement.case attributes {check_kind = 1 : i32, condition_kind = 0 : i32, has_default = false, item_count = 2 : i64, item_label_counts = array<i64: 1, 1>, node_id = 1 : i64} {
        obelisk.sv.expression.named_value attributes {node_id = 2 : i64, referenced_path = "top.selector", referenced_symbol = @selector, semantic_type = !logic2} {}
        obelisk.sv.expression.integer_literal attributes {constant_value = "2'b00", node_id = 3 : i64, semantic_type = !logic2} {}
        obelisk.sv.expression.integer_literal attributes {constant_value = "2'b01", node_id = 4 : i64, semantic_type = !logic2} {}
        obelisk.sv.statement.empty attributes {node_id = 5 : i64} {}
        obelisk.sv.statement.empty attributes {node_id = 6 : i64} {}
      }
      simulation.return
    }
  }
// CHECK-NOT: multiple matches
// CHECK: unique case violation: no match
  simulation.design @unique0 {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.packed_array<1 : 0 x !simulation.logic<1>> design
    simulation.code_unit.decl 8 in 0 always_comb hierarchy "unique0"
    // CHECK-LABEL: simulation.func @unique0
    simulation.func @unique0(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %selector: !simulation.ref<!simulation.packed_array<1 : 0 x !simulation.logic<1>>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 8 : i64, simulation.bindings = [#simulation.argument_binding<path = "top.selector", argument = 1, kind = direct, copyOut = false>]} {
      obelisk.sv.statement.case attributes {check_kind = 2 : i32, condition_kind = 0 : i32, has_default = false, item_count = 2 : i64, item_label_counts = array<i64: 1, 1>, node_id = 1 : i64} {
        obelisk.sv.expression.named_value attributes {node_id = 2 : i64, referenced_path = "top.selector", referenced_symbol = @selector, semantic_type = !logic2} {}
        obelisk.sv.expression.integer_literal attributes {constant_value = "2'b00", node_id = 3 : i64, semantic_type = !logic2} {}
        obelisk.sv.expression.integer_literal attributes {constant_value = "2'b01", node_id = 4 : i64, semantic_type = !logic2} {}
        obelisk.sv.statement.empty attributes {node_id = 5 : i64} {}
        obelisk.sv.statement.empty attributes {node_id = 6 : i64} {}
      }
      simulation.return
    }
  }
// CHECK-NOT: simulation.display
// CHECK-NOT: multiple matches
}
