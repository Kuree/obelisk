// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' | FileCheck %s

// The wide rank never narrows to a 64-bit bounded RNG operand. The compiler
// emits a significant-bit mask and rejects ranks outside the complete range.
// CHECK: arith.extui {{.*}} : i65 to i257
// CHECK: %[[TOTAL:.*]] = arith.addi {{.*}} : i257
// CHECK: arith.subi %[[TOTAL]], {{.*}} : i257
// CHECK: arith.shrui {{.*}} : i257
// CHECK-COUNT-5: simulation.random.next
// CHECK: %[[DRAW:.*]] = arith.andi {{.*}} : i257
// CHECK: %[[VALID:.*]] = arith.cmpi ult, %[[DRAW]], %[[TOTAL]] : i257
// CHECK: cf.cond_br %[[VALID]], ^[[ACCEPT:[^ (,]*]](%[[DRAW]] : i257), ^{{[^ ,]*}}
// CHECK: ^[[ACCEPT]](%{{.*}}: i257):
// CHECK: arith.cmpi ult, {{.*}} : i257

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  talos.sv.symbol.definition @top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 60368 : i64} {}
  talos.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 60367 : i64} {
    talos.sv.symbol.instance @instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", referenced_path = "top", referenced_symbol = @top, node_id = 60366 : i64} {
      talos.sv.symbol.instance_body @body attributes {hierarchical_name = "top", name = "top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 60365 : i64} {
        talos.sv.symbol.variable @wa attributes {hierarchical_name = "top.wa", lifetime = 1 : i32, name = "wa", semantic_type = !talos.ranged_packed_array<64 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>, node_id = 60345 : i64} {
          talos.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !talos.ranged_packed_array<64 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>, node_id = 60344 : i64} {}
        }
        talos.sv.symbol.variable @wb attributes {hierarchical_name = "top.wb", lifetime = 1 : i32, name = "wb", semantic_type = !talos.ranged_packed_array<256 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>, node_id = 60347 : i64} {
          talos.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !talos.ranged_packed_array<256 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>, node_id = 60346 : i64} {}
        }
        talos.sv.symbol.variable @selected attributes {hierarchical_name = "top.selected", lifetime = 1 : i32, name = "selected", semantic_type = !talos.integral<32, true, false, 31 : 0, int>, node_id = 60349 : i64} {
          talos.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !talos.integral<32, true, false, 31 : 0, int>, node_id = 60348 : i64} {}
        }
        talos.sv.symbol.procedural_block @initial attributes {hierarchical_name = "top", procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 60364 : i64} {
          talos.sv.statement.list attributes {node_id = 60363 : i64} {
            talos.sv.statement.rand_case attributes {item_count = 2 : i64, node_id = 60362 : i64} {
              talos.sv.expression.named_value attributes {referenced_path = "top.wa", referenced_symbol = @root::@instance::@body::@wa, semantic_type = !talos.ranged_packed_array<64 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>, node_id = 60350 : i64} {}
              talos.sv.expression.named_value attributes {referenced_path = "top.wb", referenced_symbol = @root::@instance::@body::@wb, semantic_type = !talos.ranged_packed_array<256 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>, node_id = 60351 : i64} {}
              talos.sv.statement.list attributes {node_id = 60360 : i64} {
                talos.sv.statement.expression_statement attributes {node_id = 60355 : i64} {
                  talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !talos.integral<32, true, false, 31 : 0, int>, node_id = 60354 : i64} {
                    talos.sv.expression.named_value attributes {referenced_path = "top.selected", referenced_symbol = @root::@instance::@body::@selected, semantic_type = !talos.integral<32, true, false, 31 : 0, int>, node_id = 60353 : i64} {}
                    talos.sv.expression.integer_literal attributes {constant_value = "1", semantic_type = !talos.integral<32, true, false, 31 : 0, int>, node_id = 60352 : i64} {}
                  }
                }
              }
              talos.sv.statement.list attributes {node_id = 60361 : i64} {
                talos.sv.statement.expression_statement attributes {node_id = 60359 : i64} {
                  talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !talos.integral<32, true, false, 31 : 0, int>, node_id = 60358 : i64} {
                    talos.sv.expression.named_value attributes {referenced_path = "top.selected", referenced_symbol = @root::@instance::@body::@selected, semantic_type = !talos.integral<32, true, false, 31 : 0, int>, node_id = 60357 : i64} {}
                    talos.sv.expression.integer_literal attributes {constant_value = "2", semantic_type = !talos.integral<32, true, false, 31 : 0, int>, node_id = 60356 : i64} {}
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
