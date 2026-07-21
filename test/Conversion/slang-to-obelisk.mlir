// RUN: obelisk-opt %s | obelisk-opt --convert-slang-to-obelisk \
// RUN:   | FileCheck %s

module {
  %zero = arith.constant 0 : i32
  slang.symbol.root attributes {
    hierarchical_name = "$root", node_id = 0 : i64, sym_name = "root"
  } {
    slang.symbol.definition attributes {
      definition_kind = 1 : i32, node_id = 17 : i64, sym_name = "bus"
    } {
    }
    slang.symbol.variable attributes {
      lifetime = 0 : i32, name = "word", node_id = 1 : i64,
      rand_mode = 0 : i32, sym_name = "word",
      semantic_type = !slang.integral<8, false, true, 7 : 0, generic>
    } {
    }
    slang.type.enum_type attributes {
      node_id = 2 : i64, sym_name = "state_t",
      semantic_type = !slang.enum<"state_t", !slang.integral<2, false, false, 1 : 0, generic>>
    } {
    }
    slang.statement.conditional attributes {
      check_kind = 0 : i32, condition_count = 0 : i64,
      has_else = false, node_id = 3 : i64
    } {
    }
    slang.timing.delay attributes {node_id = 4 : i64} {
    }
    slang.expression.binary_op attributes {
      node_id = 5 : i64, operator_kind = 0 : i32,
      semantic_type = !slang.packed_array<15 : 8 x !slang.integral<1, false, true, 0 : 0, logic>>
    } {
    }
    slang.constraint.expression attributes {is_soft = true, node_id = 6 : i64} {
    }
    slang.assertion.simple attributes {
      has_repetition = false, is_null = false, node_id = 7 : i64,
      repetition_is_unbounded = false, semantic_type = !slang.sequence
    } {
    }
    slang.bins.condition attributes {node_id = 8 : i64} {
    }
    slang.pattern.constant attributes {node_id = 9 : i64} {
    }
    slang.rand_seq.item attributes {node_id = 10 : i64} {
    }
    slang.symbol.variable attributes {
      lifetime = 0 : i32, node_id = 11 : i64, rand_mode = 0 : i32,
      sym_name = "assoc_word",
      semantic_type = !slang.associative_array<!slang.string, !slang.real, false>
    } {
    }
    slang.symbol.variable attributes {
      lifetime = 0 : i32, node_id = 12 : i64, rand_mode = 0 : i32,
      sym_name = "virtual_bus",
      semantic_type = !slang.virtual_interface<@bus, "master">
    } {
    }
    slang.symbol.subroutine attributes {
      default_lifetime = 0 : i32, is_virtual, node_id = 13 : i64,
      member_visibility = 0 : i32, sym_name = "convert",
      subroutine_kind = 0 : i32,
      semantic_type = !slang.subroutine<(!slang.string) -> !slang.shortreal, false>
    } {
    }
    slang.type.type_alias attributes {
      name = "byte_t", node_id = 14 : i64, sym_name = "byte_t",
      semantic_type = !slang.integral<8, false, true, 7 : 0, generic>
    } {
    }
    slang.expression.assignment attributes {
      assignment_kind = 1 : i32, node_id = 15 : i64,
      semantic_type = !slang.integral<8, false, true, 7 : 0, generic>
    } {
    }
    slang.symbol.variable attributes {
      lifetime = 0 : i32, node_id = 16 : i64, rand_mode = 0 : i32,
      sym_name = "nested",
      semantic_type = tensor<1x!slang.integral<1, false, true, 0 : 0, logic>>
    } {
    }
  }
}

// CHECK: %{{.*}} = arith.constant 0 : i32
// CHECK: obelisk.sv.symbol.root
// CHECK: semantic_type = !obelisk.integral<8, false, true, 7 : 0, generic>
// CHECK: obelisk.sv.type.enum_type
// CHECK: !obelisk.enum<"state_t", !obelisk.integral<2, false, false, 1 : 0, generic>>
// CHECK: obelisk.sv.statement.conditional
// CHECK: obelisk.sv.timing.delay
// CHECK: obelisk.sv.expression.binary_op
// CHECK: !obelisk.ranged_packed_array<15 : 8 x !obelisk.integral<1, false, true, 0 : 0, logic>>
// CHECK: obelisk.sv.constraint.expression
// CHECK: obelisk.sv.assertion.simple
// CHECK: semantic_type = !obelisk.sequence
// CHECK: obelisk.sv.bins.condition
// CHECK: obelisk.sv.pattern.constant
// CHECK: obelisk.sv.rand_seq.item
// CHECK: !obelisk.assoc<!obelisk.string, !obelisk.real, false>
// CHECK: !obelisk.virtual_interface<@bus, "master">
// CHECK: !obelisk.subroutine<(!obelisk.string) -> !obelisk.shortreal, false>
// CHECK-SAME: subroutine_kind = 0 : i32
// CHECK: obelisk.sv.type.type_alias
// CHECK: obelisk.sv.expression.assignment
// CHECK-SAME: assignment_kind = 1 : i32
// CHECK: tensor<1x!obelisk.integral<1, false, true, 0 : 0, logic>>
// CHECK-NOT: slang.
