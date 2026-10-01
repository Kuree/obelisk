// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=3' \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null

!choice = !obelisk.source_aggregate<"top", false, true, true, false,
  false, false, 0, 1, 0, 0, [
    {name = "text", ordinal = 0 : i32, packed_offset = 0 : i64,
      type = !obelisk.string},
    {name = "values", ordinal = 1 : i32, packed_offset = 0 : i64,
      type = !obelisk.queue<!obelisk.string, 0>}
  ]>
!payload = !obelisk.source_aggregate<"top", false, false, false, false,
  false, false, 0, 3, 0, 0, [
    {name = "names", ordinal = 0 : i32, packed_offset = 0 : i64,
      type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.string>},
    {name = "choice", ordinal = 1 : i32, packed_offset = 0 : i64,
      type = !choice},
    {name = "tag", ordinal = 2 : i32, packed_offset = 0 : i64,
      type = !obelisk.integral<32, true, false, 31 : 0, int>}
  ]>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition @top_def attributes {definition_kind = 0 : i32,
      hierarchical_name = "top", name = "top", node_id = 0 : i64
  } {
  }
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "\\$root ",
      name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.instance @top attributes {hierarchical_name = "top",
        is_uninstantiated = false, name = "top", node_id = 2 : i64,
        referenced_path = "top", referenced_symbol = @top_def
    } {
      obelisk.sv.symbol.instance_body @body attributes {hierarchical_name = "top",
          name = "top", node_id = 3 : i64} {
        obelisk.sv.symbol.variable @payload attributes {hierarchical_name = "top.payload",
            lifetime = 1 : i32, name = "payload", node_id = 4 : i64,
            semantic_type = !payload} {
        }
        obelisk.sv.symbol.variable @enabled attributes {hierarchical_name = "top.enabled",
            lifetime = 1 : i32, name = "enabled", node_id = 12 : i64,
            semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>
        } {
        }
        obelisk.sv.symbol.variable @result attributes {hierarchical_name = "top.result",
            lifetime = 1 : i32, name = "result", node_id = 9 : i64,
            semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>
        } {
        }
        obelisk.sv.symbol.procedural_block @always_comb attributes {hierarchical_name = "top",
            node_id = 5 : i64, procedure_kind = 3 : i32} {
          obelisk.sv.statement.conditional attributes {check_kind = 0 : i32,
              condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>,
              has_else = false, node_id = 13 : i64} {
            obelisk.sv.expression.named_value attributes {is_signed = false,
                node_id = 14 : i64, referenced_path = "top.enabled",
                referenced_symbol = @root::@top::@body::@enabled,
                semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 6 : i64} {
              obelisk.sv.expression.assignment attributes {
                  assignment_kind = 0 : i32, is_signed = true, node_id = 10 : i64,
                  semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true,
                    node_id = 11 : i64, referenced_path = "top.result",
                    referenced_symbol = @root::@top::@body::@result,
                    semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.call attributes {argument_count = 1 : i64,
                    callee_name = "$bits", constraint_restrictions = [],
                    defaulted_arguments = array<i64: 0>,
                    has_inline_constraints = false,
                    has_iterator_expression = false, has_output_arguments = false,
                    has_this_class = false, is_signed = true,
                    is_super_class = false, is_system_call = true, node_id = 7 : i64,
                    semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>,
                    subroutine_kind = 0 : i32, system_library_cell = "work.top",
                    system_scope_path = "top",
                    system_scope_symbol = @root::@top::@body} {
                  obelisk.sv.expression.named_value attributes {is_signed = false,
                      node_id = 8 : i64, referenced_path = "top.payload",
                      referenced_symbol = @root::@top::@body::@payload,
                      semantic_type = !payload} {
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

// CHECK-DAG: arith.constant 32 : i32
// CHECK: simulation.ref.subelement
// CHECK: simulation.string.length
// CHECK: simulation.union.is_active
// CHECK: simulation.managed.watch container_size
// CHECK: simulation.suspend.any
// CHECK: simulation.union.is_active
// CHECK: simulation.container.size
// CHECK: simulation.container.read
// CHECK: arith.muli {{.*}} : i32
