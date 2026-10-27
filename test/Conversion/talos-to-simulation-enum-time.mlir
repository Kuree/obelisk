// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' | FileCheck %s
// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=3' --emit-bytecode -o /dev/null

module {
  talos.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  talos.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    talos.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      talos.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64} {
        talos.sv.symbol.variable @s5.value attributes {hierarchical_name = "top.value", lifetime = 1 : i32, name = "value", node_id = 5 : i64, semantic_type = !talos.enum<"time_enum", !talos.time>} {
        }
      }
    }
  }
}

// CHECK: simulation.storage.decl {{.*}} : !simulation.logic<64> design hierarchy "top.value"
