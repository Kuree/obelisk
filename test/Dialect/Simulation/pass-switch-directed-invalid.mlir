// RUN: obelisk-opt %s --split-input-file --verify-diagnostics

module {
  obelisk_sim.design @bad_directed_type {
    // expected-error @+1 {{directed attribute must be boolean}}
    obelisk_sim.net.pass.decl 0 in 0 0[0] to 1[0] width 1 reversed = false {directed = "yes"}
  }
}

// -----

module {
  obelisk_sim.design @uncontrolled_directed {
    // expected-error @+1 {{directed pass switch requires controlled = true}}
    obelisk_sim.net.pass.decl 0 in 0 0[0] to 1[0] width 1 reversed = false {directed = true}
  }
}
