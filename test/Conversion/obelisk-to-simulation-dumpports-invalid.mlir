// RUN: obelisk-opt %s \
// RUN:   '--pass-pipeline=builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-unit)))' \
// RUN:   --verify-diagnostics

module {
  obelisk_sim.design @bad_dumpports_scope {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.bad"
    obelisk_sim.func @bad(%ctx: !obelisk_sim.context
        {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      obelisk.sv.statement.expression_statement attributes {node_id = 1 : i64} {
        obelisk.sv.expression.call attributes {
          argument_count = 1 : i64, callee_name = "$dumpports",
          constraint_restrictions = [], has_inline_constraints = false,
          has_iterator_expression = false, has_output_arguments = false,
          has_this_class = false, is_super_class = false,
          is_system_call = true, node_id = 2 : i64,
          semantic_type = !obelisk.void, subroutine_kind = 1 : i32,
          system_scope_path = "top"
        } {
          // expected-error @+1 {{$dumpports selection must name a module instance}}
          obelisk.sv.expression.arbitrary_symbol attributes {
            node_id = 3 : i64, referenced_path = "top.value",
            referenced_symbol = @missing, semantic_type = !obelisk.void
          } {
          }
        }
      }
      obelisk_sim.return
    }
  }
}
