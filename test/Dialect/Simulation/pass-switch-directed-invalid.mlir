// RUN: obelisk-opt %s --split-input-file --verify-diagnostics

module {
  obelisk_sim.design @bad_directed_type {
    // expected-error @+1 {{directed attribute must be boolean}}
    obelisk_sim.net.pass.decl 0 in 0 0[0] to 1[0] width 1 reversed = false {directed = "yes"}
  }
}

// -----

module {
  obelisk_sim.design @delayed_without_direction {
    // expected-error @+1 {{delayed pass switch requires directed = true and controlled = true}}
    obelisk_sim.net.pass.decl 0 in 0 0[0] to 1[0] width 1 reversed = false {controlled = true, delayed = true}
  }
}

// -----

module {
  obelisk_sim.design @negative_delayed_mos_id {
    func.func @bad(%control: !obelisk_sim.logic<1>) {
      %delay = obelisk_sim.time.constant 1
      // expected-error @+1 {{pass-switch ID must be nonnegative}}
      obelisk_sim.net.mos.drive_delayed -1 = %control after[%delay, %delay, %delay] : !obelisk_sim.logic<1>
      return
    }
  }
}

// -----

module {
  obelisk_sim.design @reserved_pass_switch_id {
    func.func @bad(%control: !obelisk_sim.logic<1>) {
      // expected-error @+1 {{pass-switch ID must be less than 4294967295}}
      obelisk_sim.net.pass.control 4294967295 = %control : !obelisk_sim.logic<1>
      return
    }
  }
}

// -----

module {
  obelisk_sim.design @uncontrolled_directed {
    // expected-error @+1 {{directed pass switch requires controlled = true}}
    obelisk_sim.net.pass.decl 0 in 0 0[0] to 1[0] width 1 reversed = false {directed = true}
  }
}
