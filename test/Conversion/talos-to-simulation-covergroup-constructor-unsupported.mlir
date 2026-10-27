// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' | FileCheck %s

module {
  talos.sv.symbol.definition @m attributes {definition_kind = 0 : i32, hierarchical_name = "m", name = "m", node_id = 0 : i64} {
  }
  talos.sv.symbol.root @root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.instance @i attributes {hierarchical_name = "m", is_uninstantiated = false, name = "m", node_id = 3 : i64, referenced_path = "m", referenced_symbol = @m} {
      talos.sv.symbol.instance_body @body attributes {hierarchical_name = "m", name = "m", node_id = 4 : i64} {
        talos.sv.type.covergroup_type @cg attributes {constructor_argument_count = 1 : i64, constructor_formals = [@arg], coverage_event_kind = 0 : i32, has_coverage_event = false, hierarchical_name = "m.cg", name = "cg", node_id = 8 : i64, sample_formal_count = 0 : i64, sample_formals = [], semantic_type = !talos.covergroup_handle<@root::@body::@cg>} {
          talos.sv.symbol.formal_argument @arg attributes {direction = 0 : i32, hierarchical_name = "m.cg::arg", name = "arg", node_id = 9 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
          }
        }
      }
    }
  }
}

// CHECK: simulation.covergroup.decl
// CHECK-NOT: talos.sv.
