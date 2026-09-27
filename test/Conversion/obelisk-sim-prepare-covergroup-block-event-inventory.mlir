// RUN: obelisk-opt %s '--obelisk-sim-prepare=prune-unused-coverage=false' \
// RUN:   | FileCheck %s

// IEEE 1800-2017 19.3 and IEEE 1800-2023 19.3 define one block-event
// identity for a target independently of whether a covergroup observes its
// begin, its end, or both.  Preparation freezes that identity onto the event
// expressions and target definitions before code units are lowered.

module attributes {obelisk.coverage.metrics = ["functional"]} {
  obelisk.sv.symbol.definition attributes {
      definition_kind = 0 : i32, hierarchical_name = "top", name = "top",
      node_id = 0 : i64, sym_name = "definition"} {
  }
  obelisk.sv.symbol.root attributes {
      hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64,
      sym_name = "root"} {
    obelisk.sv.symbol.instance attributes {
        hierarchical_name = "top", is_uninstantiated = false, name = "top",
        node_id = 2 : i64, referenced_path = "top",
        referenced_symbol = @definition, sym_name = "instance"} {
      obelisk.sv.symbol.instance_body attributes {
          hierarchical_name = "top", name = "top", node_id = 3 : i64,
          sym_name = "body"} {
        obelisk.sv.symbol.subroutine attributes {
            hierarchical_name = "top.work", name = "work", node_id = 4 : i64,
            semantic_type = !obelisk.subroutine<() -> (), true>,
            subroutine_kind = 1 : i32, sym_name = "work"} {
          obelisk.sv.statement.block attributes {
              block_path = "top.work.inner",
              block_symbol = @root::@instance::@body::@work::@inner_symbol,
              node_id = 5 : i64} {
            obelisk.sv.statement.list attributes {node_id = 6 : i64} {
            }
          }
          obelisk.sv.symbol.statement_block attributes {
              block_kind = 0 : i32, hierarchical_name = "top.work.inner",
              name = "inner", node_id = 7 : i64,
              sym_name = "inner_symbol"} {
          }
          obelisk.sv.statement.disable attributes {
              is_hierarchical = false, node_id = 16 : i64,
              target_path = "top.work",
              target_symbol = @root::@instance::@body::@work} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {
            hierarchical_name = "top.value", name = "value",
            node_id = 8 : i64,
            semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>,
            subroutine_kind = 0 : i32, sym_name = "value"} {
          obelisk.sv.statement.list attributes {node_id = 9 : i64} {
          }
        }
        obelisk.sv.type.covergroup_type attributes {
            constructor_argument_count = 0 : i64, constructor_formals = [],
            coverage_event_kind = 3 : i32, has_coverage_event = true,
            hierarchical_name = "top.cg", name = "cg", node_id = 10 : i64,
            sample_formal_count = 0 : i64, sample_formals = [],
            semantic_type = !obelisk.covergroup_handle<@root::@instance::@body::@cg>,
            sym_name = "cg"} {
          obelisk.sv.timing.block_event_list attributes {
              event_kinds = [0 : i32, 1 : i32, 0 : i32, 1 : i32],
              node_id = 11 : i64} {
            obelisk.sv.expression.arbitrary_symbol attributes {
                is_signed = false, node_id = 12 : i64,
                referenced_path = "top.work",
                referenced_symbol = @root::@instance::@body::@work,
                semantic_type = !obelisk.void} {
            }
            obelisk.sv.expression.arbitrary_symbol attributes {
                is_signed = false, node_id = 13 : i64,
                referenced_path = "top.work",
                referenced_symbol = @root::@instance::@body::@work,
                semantic_type = !obelisk.void} {
            }
            obelisk.sv.expression.arbitrary_symbol attributes {
                is_signed = false, node_id = 14 : i64,
                referenced_path = "top.work.inner",
                referenced_symbol = @root::@instance::@body::@work::@inner_symbol,
                semantic_type = !obelisk.void} {
            }
            obelisk.sv.expression.arbitrary_symbol attributes {
                is_signed = false, node_id = 15 : i64,
                referenced_path = "top.value",
                referenced_symbol = @root::@instance::@body::@value,
                semantic_type = !obelisk.void} {
            }
          }
        }
      }
    }
  }
}

// The task's begin and end clauses share one ID.
// CHECK-DAG: obelisk.sv.symbol.subroutine attributes {{.*}}hierarchical_name = "top.work"{{.*}}simulation.control_target_id = [[CONTROL:[1-9][0-9]*]] : i64{{.*}}simulation.coverage_block_event_target_id = [[WORK:[4-9][0-9]{18}]] : i64
// CHECK-DAG: obelisk.sv.expression.arbitrary_symbol attributes {{.*}}node_id = 12{{.*}}simulation.coverage_block_event_target_id = [[WORK]] : i64
// CHECK-DAG: obelisk.sv.expression.arbitrary_symbol attributes {{.*}}node_id = 13{{.*}}simulation.coverage_block_event_target_id = [[WORK]] : i64

// A named block's semantic symbol and executable statement share one ID.
// CHECK-DAG: obelisk.sv.statement.block attributes {{.*}}simulation.coverage_block_event_target_id = [[BLOCK:[4-9][0-9]{18}]] : i64
// CHECK-DAG: obelisk.sv.symbol.statement_block attributes {{.*}}simulation.coverage_block_event_target_id = [[BLOCK]] : i64
// CHECK-DAG: obelisk.sv.expression.arbitrary_symbol attributes {{.*}}node_id = 14{{.*}}simulation.coverage_block_event_target_id = [[BLOCK]] : i64

// Functions receive their own stable target identity.
// CHECK-DAG: obelisk.sv.symbol.subroutine attributes {{.*}}hierarchical_name = "top.value"{{.*}}simulation.coverage_block_event_target_id = [[VALUE:[4-9][0-9]{18}]] : i64
// CHECK-DAG: obelisk.sv.expression.arbitrary_symbol attributes {{.*}}node_id = 15{{.*}}simulation.coverage_block_event_target_id = [[VALUE]] : i64

// Subroutine IDs cross the semantic/simulation boundary.  Named blocks remain
// within their owning function and are consumed while that body is lowered.
// CHECK-DAG: simulation.func private @{{[^ ]+}}{{.*}}simulation.control_target_id = [[CONTROL]] : i64{{.*}}simulation.coverage_block_event_target_id = [[WORK]] : i64{{.*}}simulation.hierarchical_name = "top.work"
// CHECK-DAG: simulation.func private @{{[^ ]+}}{{.*}}simulation.coverage_block_event_target_id = [[VALUE]] : i64{{.*}}simulation.hierarchical_name = "top.value"
