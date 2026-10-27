// RUN: not talos-opt %s '--lower-talos-to-sim=opt-level=0' 2>&1 | FileCheck %s

module {
  talos.sv.symbol.definition @m attributes {definition_kind = 0 : i32, hierarchical_name = "m", name = "m", node_id = 0 : i64} {
  }
  talos.sv.symbol.root @root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.instance @i attributes {hierarchical_name = "m", is_uninstantiated = false, name = "m", node_id = 3 : i64, referenced_path = "m", referenced_symbol = @m} {
      talos.sv.symbol.instance_body @body attributes {hierarchical_name = "m", name = "m", node_id = 4 : i64} {
        talos.sv.type.covergroup_type @cg attributes {constructor_argument_count = 0 : i64, constructor_formals = [], coverage_event_kind = 1 : i32, has_coverage_event = true, hierarchical_name = "m.cg", name = "cg", node_id = 8 : i64, sample_formal_count = 0 : i64, sample_formals = [], semantic_type = !talos.covergroup_handle<@root::@body::@cg>} {
          talos.sv.timing.signal_event attributes {edge_kind = 1 : i32, has_iff = false, node_id = 9 : i64} {
          }
        }
      }
    }
  }
}

// CHECK: supported automatic-sampling forms are a clocking event over a packed expression with static dependencies or a typed begin/end block event over a named block, task, function, or method
