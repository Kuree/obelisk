// RUN: not obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=read' 2>&1 | FileCheck %s

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @conflicting_backed_property {
    simulation.scope.decl 0 hierarchy "$root"
    simulation.scope.decl 1 parent 0 hierarchy "top" vpi_kind 32 {
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 600 : i32, value = false>
      ]>
    }
    simulation.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0
        hierarchy "top" debug "top" {
      backing = #simulation.vpi_backing<kind = scope, id = 1 : i64>,
      vpi_properties = #simulation.vpi_properties<[
        #simulation.vpi_property<selector = 600 : i32, value = true>
      ]>
    }
    simulation.code_unit.decl 1 in 1 initial hierarchy "top.initial"
    simulation.func @initial(%ctx: !simulation.context
        {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      simulation.return
    }
  }
}

// CHECK: error: conflicting immutable VPI property values for one physical object
