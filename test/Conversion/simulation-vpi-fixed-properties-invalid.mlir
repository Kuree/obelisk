// RUN: obelisk-opt --split-input-file --verify-diagnostics %s \
// RUN:   --encode-obelisk-sim-to-bytecode='vpi=read'

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @conflicting_backed_anchor {
    obelisk_sim.scope.decl 0 hierarchy "$root"
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top" {
      vpi_kind = 32 : i32,
      definition_loc = loc("backing.sv":3:1)
    }
    // expected-error @+1 {{conflicting immutable VPI property values for one physical object}}
    obelisk_sim.vpi_object.anchor @top id 0 type 32 in 1 ordinal 0
        hierarchy "top" debug "top" {
      backing = #obelisk_sim.vpi_backing<kind = scope, id = 1 : i64>,
      definition_loc = loc("anchor.sv":4:1)
    }
    obelisk_sim.code_unit.decl 1 in 1 initial hierarchy "top.initial"
    obelisk_sim.func @initial(%ctx: !obelisk_sim.context
        {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      obelisk_sim.return
    }
  }
}

// -----

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @driver_metadata {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design hierarchy "top.value"
    // expected-error @+1 {{fixed VPI metadata has no concrete serialized property source}}
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<1> design {
      definition_loc = loc("driver.sv":9:2)
    }
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    obelisk_sim.func @initial(%ctx: !obelisk_sim.context
        {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      obelisk_sim.return
    }
  }
}

// -----

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @nul_definition_file {
  // expected-error @+1 {{definition_loc filename contains an embedded NUL}}
    obelisk_sim.scope.decl 0 hierarchy "top" vpi_kind 32 {
      definition_loc = loc("bad\00file.sv":7:1)
    }
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.initial"
    obelisk_sim.func @initial(%ctx: !obelisk_sim.context
        {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      obelisk_sim.return
    }
  }
}
