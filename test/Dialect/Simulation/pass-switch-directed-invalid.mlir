// RUN: obelisk-opt %s --split-input-file --verify-diagnostics

module {
  simulation.design @bad_directed_type {
    // expected-error @+1 {{directed attribute must be boolean}}
    simulation.net.pass.decl 0 in 0 0[0] to 1[0] width 1 reversed = false {directed = "yes"}
  }
}

// -----

module {
  simulation.design @delayed_without_direction {
    // expected-error @+1 {{delayed pass switch requires directed = true and controlled = true}}
    simulation.net.pass.decl 0 in 0 0[0] to 1[0] width 1 reversed = false {controlled = true, delayed = true}
  }
}

// -----

module {
  simulation.design @negative_delayed_mos_id {
    func.func @bad(%control: !simulation.logic<1>) {
      %delay = simulation.time.constant 1
      // expected-error @+1 {{pass-switch ID must be nonnegative}}
      simulation.net.mos.drive_delayed -1 = %control after[%delay, %delay, %delay] : !simulation.logic<1>
      return
    }
  }
}

// -----

module {
  simulation.design @reserved_pass_switch_id {
    func.func @bad(%control: !simulation.logic<1>) {
      // expected-error @+1 {{pass-switch ID must be less than 4294967295}}
      simulation.net.pass.control 4294967295 = %control : !simulation.logic<1>
      return
    }
  }
}

// -----

module {
  simulation.design @uncontrolled_directed {
    // expected-error @+1 {{directed pass switch requires controlled = true}}
    simulation.net.pass.decl 0 in 0 0[0] to 1[0] width 1 reversed = false {directed = true}
  }
}
