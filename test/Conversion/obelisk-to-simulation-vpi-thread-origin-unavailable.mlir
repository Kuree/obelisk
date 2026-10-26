// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --implicit-check-not=simulation.vpi_thread_origin_binding --implicit-check-not=simulation.statement.decl

// The current statement model has no exact atomic-operation route for ++.
// Do not substitute an Assignment kind or its variable operand as the origin.
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.definition @definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 47 : i64} {
  }
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 50 : i64} {
    obelisk.sv.symbol.instance @top attributes {hierarchical_name = "top", name = "top", is_uninstantiated = false, referenced_path = "top", referenced_symbol = @definition, node_id = 49 : i64} {
      obelisk.sv.symbol.instance_body @body attributes {hierarchical_name = "top", name = "top", time_precision_fs = 1 : i64, time_unit_fs = 1 : i64, node_id = 48 : i64} {
        obelisk.sv.symbol.variable @i attributes {hierarchical_name = "top.i", name = "i", lifetime = 1 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, node_id = 42 : i64} {
        }
        obelisk.sv.symbol.procedural_block @increment attributes {hierarchical_name = "top", procedure_kind = 0 : i32, time_precision_fs = 1 : i64, time_unit_fs = 1 : i64, node_id = 46 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 45 : i64} {
            obelisk.sv.expression.unary_op attributes {operator_kind = 12 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, node_id = 44 : i64} {
              obelisk.sv.expression.named_value attributes {referenced_symbol = @root::@top::@body::@i, referenced_path = "top.i", semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, node_id = 43 : i64} {
              }
            }
          }
        }
      }
    }
  }
}
// CHECK: simulation.code_unit.decl {{[0-9]+}} in {{[0-9]+}} initial
// CHECK: arith.addi
// CHECK: simulation.ref.store
