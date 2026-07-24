// RUN: obelisk-opt %s | obelisk-opt --convert-slang-to-obelisk \
// RUN:   | FileCheck %s
//
// Exhaustive check that the Slang-to-Obelisk conversion maps every semantic
// type to its concrete Obelisk counterpart and leaves no source dialect entity
// behind. Operations from other dialects survive untouched when they carry no
// Slang type.

module {
  // A foreign operation with no Slang type must be preserved verbatim.
  %zero = arith.constant 0 : i32

  slang.symbol.root attributes {
    hierarchical_name = "$root", node_id = 0 : i64, sym_name = "root"
  } {
    slang.symbol.definition attributes {
      definition_kind = 1 : i32, node_id = 1 : i64, sym_name = "bus"
    } {
    }

    // Integral, enum, and alias types.
    slang.symbol.variable attributes {
      lifetime = 0 : i32, node_id = 2 : i64, rand_mode = 0 : i32,
      sym_name = "word",
      semantic_type = !slang.integral<8, false, true, 7 : 0, generic>
    } {
    }
    slang.type.enum_type attributes {
      node_id = 3 : i64, sym_name = "state_t",
      semantic_type = !slang.enum<"state_t", !slang.integral<2, false, false, 1 : 0, generic>>
    } {
    }
    slang.type.type_alias attributes {
      name = "byte_t", node_id = 4 : i64, sym_name = "byte_t",
      semantic_type = !slang.integral<8, false, true, 7 : 0, generic>
    } {
    }

    // Every scalar semantic type.
    slang.symbol.variable attributes {
      lifetime = 0 : i32, node_id = 5 : i64, rand_mode = 0 : i32,
      sym_name = "str", semantic_type = !slang.string
    } {
    }
    slang.symbol.variable attributes {
      lifetime = 0 : i32, node_id = 6 : i64, rand_mode = 0 : i32,
      sym_name = "r", semantic_type = !slang.real
    } {
    }
    slang.symbol.variable attributes {
      lifetime = 0 : i32, node_id = 7 : i64, rand_mode = 0 : i32,
      sym_name = "rt", semantic_type = !slang.realtime
    } {
    }
    slang.symbol.variable attributes {
      lifetime = 0 : i32, node_id = 8 : i64, rand_mode = 0 : i32,
      sym_name = "sr", semantic_type = !slang.shortreal
    } {
    }
    slang.symbol.variable attributes {
      lifetime = 0 : i32, node_id = 9 : i64, rand_mode = 0 : i32,
      sym_name = "tm", semantic_type = !slang.time
    } {
    }
    slang.symbol.variable attributes {
      lifetime = 0 : i32, node_id = 10 : i64, rand_mode = 0 : i32,
      sym_name = "ch", semantic_type = !slang.chandle
    } {
    }
    slang.symbol.variable attributes {
      lifetime = 0 : i32, node_id = 11 : i64, rand_mode = 0 : i32,
      sym_name = "ev", semantic_type = !slang.event
    } {
    }
    slang.symbol.variable attributes {
      lifetime = 0 : i32, node_id = 12 : i64, rand_mode = 0 : i32,
      sym_name = "vd", semantic_type = !slang.void
    } {
    }
    slang.symbol.variable attributes {
      lifetime = 0 : i32, node_id = 13 : i64, rand_mode = 0 : i32,
      sym_name = "nl", semantic_type = !slang.null
    } {
    }
    slang.symbol.variable attributes {
      lifetime = 0 : i32, node_id = 14 : i64, rand_mode = 0 : i32,
      sym_name = "ub", semantic_type = !slang.unbounded
    } {
    }
    slang.symbol.variable attributes {
      lifetime = 0 : i32, node_id = 15 : i64, rand_mode = 0 : i32,
      sym_name = "ut", semantic_type = !slang.untyped
    } {
    }
    slang.symbol.variable attributes {
      lifetime = 0 : i32, node_id = 16 : i64, rand_mode = 0 : i32,
      sym_name = "tr", semantic_type = !slang.type_reference
    } {
    }
    slang.symbol.variable attributes {
      lifetime = 0 : i32, node_id = 17 : i64, rand_mode = 0 : i32,
      sym_name = "prop", semantic_type = !slang.property
    } {
    }

    // Every aggregate and container semantic type.
    slang.symbol.variable attributes {
      lifetime = 0 : i32, node_id = 18 : i64, rand_mode = 0 : i32,
      sym_name = "packed_word",
      semantic_type = !slang.packed_array<15 : 0 x !slang.integral<1, false, true, 0 : 0, logic>>
    } {
    }
    slang.symbol.variable attributes {
      lifetime = 0 : i32, node_id = 19 : i64, rand_mode = 0 : i32,
      sym_name = "unpacked_word",
      semantic_type = !slang.unpacked_array<3 : 0 x !slang.integral<8, false, true, 7 : 0, generic>>
    } {
    }
    slang.symbol.variable attributes {
      lifetime = 0 : i32, node_id = 20 : i64, rand_mode = 0 : i32,
      sym_name = "dyn_word",
      semantic_type = !slang.dynamic_array<!slang.integral<8, false, true, 7 : 0, generic>>
    } {
    }
    slang.symbol.variable attributes {
      lifetime = 0 : i32, node_id = 21 : i64, rand_mode = 0 : i32,
      sym_name = "open_word",
      semantic_type = !slang.open_array<!slang.integral<8, false, true, 7 : 0, generic>, true>
    } {
    }
    slang.symbol.variable attributes {
      lifetime = 0 : i32, node_id = 22 : i64, rand_mode = 0 : i32,
      sym_name = "queue_word",
      semantic_type = !slang.queue<!slang.integral<8, false, true, 7 : 0, generic>, 0>
    } {
    }
    slang.symbol.variable attributes {
      lifetime = 0 : i32, node_id = 23 : i64, rand_mode = 0 : i32,
      sym_name = "assoc_word",
      semantic_type = !slang.associative_array<!slang.string, !slang.real, false>
    } {
    }
    slang.symbol.variable attributes {
      lifetime = 0 : i32, node_id = 24 : i64, rand_mode = 0 : i32,
      sym_name = "record",
      semantic_type = !slang.aggregate<"record_t", true, false, false, false, true, false, 16, 16, 16, 0, [{name = "high", ordinal = 0 : i32, packed_offset = 8 : i64, type = !slang.packed_array<7 : 0 x !slang.integral<1, false, true, 0 : 0, logic>>}, {name = "low", ordinal = 1 : i32, packed_offset = 0 : i64, type = !slang.integral<8, false, false, 7 : 0, bit>}]>
    } {
    }

    // Symbol-referencing handle types.
    slang.symbol.variable attributes {
      lifetime = 0 : i32, node_id = 25 : i64, rand_mode = 0 : i32,
      sym_name = "virtual_bus",
      semantic_type = !slang.virtual_interface<@bus, "master">
    } {
    }
    slang.type.covergroup_type attributes {
      node_id = 26 : i64, sym_name = "cg_t",
      semantic_type = !slang.covergroup_handle<@cg_t>
    } {
    }
    slang.symbol.variable attributes {
      lifetime = 0 : i32, node_id = 27 : i64, rand_mode = 0 : i32,
      sym_name = "cg_handle",
      semantic_type = !slang.covergroup_handle<@cg_t>
    } {
    }
    slang.symbol.subroutine attributes {
      default_lifetime = 0 : i32, is_virtual, node_id = 28 : i64,
      member_visibility = 0 : i32, sym_name = "convert",
      subroutine_kind = 0 : i32,
      semantic_type = !slang.subroutine<(!slang.string) -> !slang.shortreal, false>
    } {
    }

    // A cross-section of operation categories beyond declarations.
    slang.statement.conditional attributes {
      check_kind = 0 : i32, condition_count = 1 : i64,
      condition_pattern_flags = array<i64: 0>,
      has_else = false, node_id = 29 : i64
    } {
      slang.expression.integer_literal attributes {
        constant_value = "1", node_id = 40 : i64,
        semantic_type = !slang.integral<1, false, true, 0 : 0, logic>
      } {
      }
      slang.statement.list attributes {node_id = 41 : i64} {
      }
    }
    slang.statement.for_loop attributes {
      has_condition = false, initializer_count = 0 : i64,
      node_id = 42 : i64, step_count = 0 : i64
    } {
      slang.statement.empty attributes {node_id = 43 : i64} {
      }
    }
    slang.expression.integer_literal attributes {
      constant_value = "42", node_id = 30 : i64,
      semantic_type = !slang.integral<32, true, false, 31 : 0, int>
    } {
    }
    slang.expression.binary_op attributes {
      node_id = 31 : i64, operator_kind = 0 : i32,
      semantic_type = !slang.packed_array<15 : 8 x !slang.integral<1, false, true, 0 : 0, logic>>
    } {
    }
    slang.expression.assignment attributes {
      assignment_kind = 1 : i32, node_id = 32 : i64,
      semantic_type = !slang.integral<8, false, true, 7 : 0, generic>
    } {
    }
    slang.timing.delay attributes {node_id = 33 : i64} {
    }
    slang.constraint.expression attributes {is_soft = true, node_id = 34 : i64} {
    }
    slang.assertion.simple attributes {
      has_repetition = false, is_null = false, node_id = 35 : i64,
      repetition_is_unbounded = false, semantic_type = !slang.sequence
    } {
    }
    slang.bins.condition attributes {node_id = 36 : i64} {
    }
    slang.pattern.constant attributes {node_id = 37 : i64} {
    }
    slang.rand_seq.item attributes {node_id = 38 : i64} {
    }

    // A nested aggregate value in a builtin container attribute must also be
    // rewritten, proving attribute walking is recursive.
    slang.symbol.variable attributes {
      lifetime = 0 : i32, node_id = 39 : i64, rand_mode = 0 : i32,
      sym_name = "nested",
      semantic_type = tensor<1x!slang.integral<1, false, true, 0 : 0, logic>>
    } {
    }
  }
}

// CHECK: %{{.*}} = arith.constant 0 : i32
// CHECK: obelisk.sv.symbol.root
// CHECK: obelisk.sv.symbol.definition

// CHECK: obelisk.sv.symbol.variable
// CHECK-SAME: semantic_type = !obelisk.integral<8, false, true, 7 : 0, generic>
// CHECK: obelisk.sv.type.enum_type
// CHECK-SAME: !obelisk.enum<"state_t", !obelisk.integral<2, false, false, 1 : 0, generic>>
// CHECK: obelisk.sv.type.type_alias

// CHECK: semantic_type = !obelisk.string
// CHECK: semantic_type = !obelisk.real
// CHECK: semantic_type = !obelisk.realtime
// CHECK: semantic_type = !obelisk.shortreal
// CHECK: semantic_type = !obelisk.time
// CHECK: semantic_type = !obelisk.chandle
// CHECK: semantic_type = !obelisk.event
// CHECK: semantic_type = !obelisk.void
// CHECK: semantic_type = !obelisk.null
// CHECK: semantic_type = !obelisk.unbounded
// CHECK: semantic_type = !obelisk.untyped
// CHECK: semantic_type = !obelisk.type_reference
// CHECK: semantic_type = !obelisk.property

// CHECK: !obelisk.ranged_packed_array<15 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>
// CHECK: !obelisk.ranged_unpacked_array<3 : 0 x !obelisk.integral<8, false, true, 7 : 0, generic>>
// CHECK: !obelisk.dynarray<!obelisk.integral<8, false, true, 7 : 0, generic>>
// CHECK: !obelisk.open_array<!obelisk.integral<8, false, true, 7 : 0, generic>, true>
// CHECK: !obelisk.queue<!obelisk.integral<8, false, true, 7 : 0, generic>, 0>
// CHECK: !obelisk.assoc<!obelisk.string, !obelisk.real, false>
// CHECK: !obelisk.source_aggregate<"record_t", true, false, false, false, true, false, 16, 16, 16, 0, [{name = "high", ordinal = 0 : i32, packed_offset = 8 : i64, type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>}, {name = "low", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.integral<8, false, false, 7 : 0, bit>}]>

// CHECK: !obelisk.virtual_interface<@bus, "master">
// CHECK: obelisk.sv.type.covergroup_type
// CHECK: !obelisk.covergroup_handle<@cg_t>
// CHECK: !obelisk.subroutine<(!obelisk.string) -> !obelisk.shortreal, false>

// CHECK: obelisk.sv.statement.conditional
// CHECK: obelisk.sv.statement.for_loop
// CHECK-SAME: has_condition = false
// CHECK-SAME: initializer_count = 0 : i64
// CHECK-SAME: step_count = 0 : i64
// CHECK: obelisk.sv.expression.integer_literal attributes {constant_value = "42"
// CHECK: obelisk.sv.expression.binary_op
// CHECK: obelisk.sv.expression.assignment
// CHECK-SAME: assignment_kind = 1 : i32
// CHECK: obelisk.sv.timing.delay
// CHECK: obelisk.sv.constraint.expression
// CHECK: obelisk.sv.assertion.simple
// CHECK-SAME: semantic_type = !obelisk.sequence
// CHECK: obelisk.sv.bins.condition
// CHECK: obelisk.sv.pattern.constant
// CHECK: obelisk.sv.rand_seq.item
// CHECK: tensor<1x!obelisk.integral<1, false, true, 0 : 0, logic>>
// CHECK-NOT: slang.
