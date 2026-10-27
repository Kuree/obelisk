// RUN: talos-opt --split-input-file --verify-diagnostics %s

// IEEE 1800-2023 4.4.2.3: a wait may resume in the Inactive region that
// follows its process's home region, as `@(event) #0` would. Re-Inactive
// follows only the Reactive home. A Post-Observed resumption (9.4.2.4, 14.10)
// continues a procedure after the Observed region, so it needs an Active or
// Reactive home.

!bit = !simulation.logic<1>
!ref = !simulation.ref<!bit>
simulation.design @wrong_inactive {
  simulation.func @program(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, home_region = 10 : i32} {
    cf.br ^wait
  ^wait:
    // expected-error @+1 {{resume region must be an executable process home region, the Inactive region of the process home, or Post-Observed}}
    simulation.suspend.edge posedge %clk to ^wait {resume_region = 3 : i32} : !ref
  }
}

// -----

!bit = !simulation.logic<1>
!ref = !simulation.ref<!bit>
simulation.design @wrong_re_inactive {
  simulation.func @module(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32} {
    cf.br ^wait
  ^wait:
    // expected-error @+1 {{resume region must be an executable process home region, the Inactive region of the process home, or Post-Observed}}
    simulation.suspend.edge posedge %clk to ^wait {resume_region = 11 : i32} : !ref
  }
}

// -----

!bit = !simulation.logic<1>
!ref = !simulation.ref<!bit>
simulation.design @observed_post_observed {
  simulation.func @monitor(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, home_region = 8 : i32} {
    cf.br ^wait
  ^wait:
    // expected-error @+1 {{resume region must be an executable process home region, the Inactive region of the process home, or Post-Observed}}
    simulation.suspend.edge posedge %clk to ^wait {resume_region = 9 : i32} : !ref
  }
}

// -----

!bit = !simulation.logic<1>
!ref = !simulation.ref<!bit>
simulation.design @inactive {
  simulation.scope.decl 0
  simulation.storage.decl 0 in 0 : !bit design
  simulation.code_unit.decl 1 in 0 initial hierarchy "module"
  simulation.func @module(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clk: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
    cf.br ^wait
  ^wait:
    simulation.suspend.edge posedge %clk to ^post {resume_region = 3 : i32} : !ref
  ^post:
    simulation.suspend.edge posedge %clk to ^wait {resume_region = 9 : i32} : !ref
  }
}
