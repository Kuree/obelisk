// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 13.4.1 permits an explicit cast to void to discard a
// function result. The child still has to be evaluated; declaration inventory
// must not mistake the void target for a class bit-stream conversion.

// CHECK-LABEL: simulation.func private @unit_0
// CHECK: simulation.random.next
// CHECK-NOT: simulation.class_bitstream_source_feature

module {
  obelisk.sv.symbol.root attributes {
      hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64,
      sym_name = "root"} {
    obelisk.sv.symbol.instance_body attributes {
        hierarchical_name = "top", name = "top", node_id = 2 : i64,
        sym_name = "body", time_precision_fs = 1000000 : i64,
        time_unit_fs = 1000000 : i64} {
      obelisk.sv.symbol.procedural_block attributes {
          hierarchical_name = "top", node_id = 3 : i64,
          procedure_kind = 0 : i32, sym_name = "initial",
          time_precision_fs = 1000000 : i64,
          time_unit_fs = 1000000 : i64} {
        obelisk.sv.statement.block attributes {node_id = 4 : i64} {
          obelisk.sv.statement.list attributes {node_id = 5 : i64} {
            obelisk.sv.statement.expression_statement attributes {
                node_id = 6 : i64} {
              obelisk.sv.expression.conversion attributes {
                  is_implicit = false, node_id = 7 : i64,
                  semantic_type = !obelisk.void} {
                obelisk.sv.expression.call attributes {
                    argument_count = 0 : i64, callee_name = "$urandom",
                    constraint_restrictions = [],
                    defaulted_arguments = array<i64>,
                    has_inline_constraints = false,
                    has_iterator_expression = false,
                    has_output_arguments = false, has_this_class = false,
                    is_super_class = false, is_system_call = true,
                    node_id = 8 : i64,
                    semantic_type = !obelisk.integral<32, false, false, 31 : 0, integer>,
                    subroutine_kind = 0 : i32} {
                }
              }
            }
          }
        }
      }
    }
  }
}
