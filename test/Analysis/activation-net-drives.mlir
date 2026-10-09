// RUN: obelisk-opt %s --test-obelisk-activation-regions='publications=true reaching-values=true' -o /dev/null 2>&1 | FileCheck %s
// IEEE 1800-2023 6.6.1, 4.9.1: only a plain single-driver wire may use the
// driver's publication as its resolved value. Delays, strengths, user
// resolution, connected aliases and competing drivers retain resolution.
module {
  simulation.design @isolated {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : !simulation.logic<8> design
    simulation.storage.decl 0 in 0 : !simulation.logic<8> design
    simulation.storage.decl 1 in 0 : !simulation.logic<8> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<8> design
    simulation.code_unit.decl 1 in 0 continuous hierarchy "isolated"
    // CHECK-LABEL: regions @isolated valid=1
    // CHECK-NEXT: node simulation.ref.load read:s0[0,8)
    // CHECK-NEXT: node simulation.driver.drive write:s4611686018427387904[0,8)
    // CHECK-NEXT: node simulation.net.read read:s4611686018427387904[0,8)
    // CHECK-NEXT: node simulation.ref.store write:s1[0,8)
    // CHECK-NEXT: publication read:s0[0,8) write:s4611686018427387904[0,8)
    // CHECK-NEXT: publication read:s0[0,8) write:s1[0,8)
    simulation.func @isolated(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 1 : i64} {
      %net = simulation.context.net %ctx[0] : !simulation.net<!simulation.logic<8>>
      %driver = simulation.context.driver %ctx[0] : !simulation.driver<!simulation.logic<8>>
      %in = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<8>>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<8>>
      cf.br ^body
    ^body:
      %value = simulation.ref.load %in : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.driver.drive %driver = %value : !simulation.driver<!simulation.logic<8>>, !simulation.logic<8>
      %resolved = simulation.net.read %net : !simulation.net<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.ref.store %resolved to %out : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.suspend.change %in to ^body : !simulation.ref<!simulation.logic<8>>
    }
  }
  simulation.design @two_drivers {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : !simulation.logic<8> design
    simulation.storage.decl 0 in 0 : !simulation.logic<8> design
    simulation.storage.decl 1 in 0 : !simulation.logic<8> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<8> design
    simulation.driver.decl 1 in 0 drives 0 : !simulation.logic<8> design
    simulation.code_unit.decl 1 in 0 continuous hierarchy "two_drivers"
    // CHECK-LABEL: regions @two_drivers valid=0 reason=memory effects
    simulation.func @two_drivers(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 1 : i64} {
      %net = simulation.context.net %ctx[0] : !simulation.net<!simulation.logic<8>>
      %driver = simulation.context.driver %ctx[0] : !simulation.driver<!simulation.logic<8>>
      %in = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<8>>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<8>>
      cf.br ^body
    ^body:
      %value = simulation.ref.load %in : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.driver.drive %driver = %value : !simulation.driver<!simulation.logic<8>>, !simulation.logic<8>
      %resolved = simulation.net.read %net : !simulation.net<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.ref.store %resolved to %out : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.suspend.change %in to ^body : !simulation.ref<!simulation.logic<8>>
    }
  }
  simulation.design @delayed {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : !simulation.logic<8> design {propagation_delays = array<i64: 1, 1, 1>}
    simulation.storage.decl 0 in 0 : !simulation.logic<8> design
    simulation.storage.decl 1 in 0 : !simulation.logic<8> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<8> design
    simulation.code_unit.decl 1 in 0 continuous hierarchy "delayed"
    // CHECK-LABEL: regions @delayed valid=0 reason=memory effects
    simulation.func @delayed(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 1 : i64} {
      %net = simulation.context.net %ctx[0] : !simulation.net<!simulation.logic<8>>
      %driver = simulation.context.driver %ctx[0] : !simulation.driver<!simulation.logic<8>>
      %in = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<8>>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<8>>
      cf.br ^body
    ^body:
      %value = simulation.ref.load %in : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.driver.drive %driver = %value : !simulation.driver<!simulation.logic<8>>, !simulation.logic<8>
      %resolved = simulation.net.read %net : !simulation.net<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.ref.store %resolved to %out : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.suspend.change %in to ^body : !simulation.ref<!simulation.logic<8>>
    }
  }
  simulation.design @weak {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : !simulation.logic<8> design
    simulation.storage.decl 0 in 0 : !simulation.logic<8> design
    simulation.storage.decl 1 in 0 : !simulation.logic<8> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<8> design {strength0 = 3 : i32, strength1 = 3 : i32}
    simulation.code_unit.decl 1 in 0 continuous hierarchy "weak"
    // CHECK-LABEL: regions @weak valid=0 reason=memory effects
    simulation.func @weak(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 1 : i64} {
      %net = simulation.context.net %ctx[0] : !simulation.net<!simulation.logic<8>>
      %driver = simulation.context.driver %ctx[0] : !simulation.driver<!simulation.logic<8>>
      %in = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<8>>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<8>>
      cf.br ^body
    ^body:
      %value = simulation.ref.load %in : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.driver.drive %driver = %value : !simulation.driver<!simulation.logic<8>>, !simulation.logic<8>
      %resolved = simulation.net.read %net : !simulation.net<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.ref.store %resolved to %out : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.suspend.change %in to ^body : !simulation.ref<!simulation.logic<8>>
    }
  }
  simulation.design @wired_and {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : !simulation.logic<8> design {resolution_kind = 3 : i32}
    simulation.storage.decl 0 in 0 : !simulation.logic<8> design
    simulation.storage.decl 1 in 0 : !simulation.logic<8> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<8> design
    simulation.code_unit.decl 1 in 0 continuous hierarchy "wired_and"
    // CHECK-LABEL: regions @wired_and valid=0 reason=memory effects
    simulation.func @wired_and(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 1 : i64} {
      %net = simulation.context.net %ctx[0] : !simulation.net<!simulation.logic<8>>
      %driver = simulation.context.driver %ctx[0] : !simulation.driver<!simulation.logic<8>>
      %in = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<8>>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<8>>
      cf.br ^body
    ^body:
      %value = simulation.ref.load %in : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.driver.drive %driver = %value : !simulation.driver<!simulation.logic<8>>, !simulation.logic<8>
      %resolved = simulation.net.read %net : !simulation.net<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.ref.store %resolved to %out : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.suspend.change %in to ^body : !simulation.ref<!simulation.logic<8>>
    }
  }
  simulation.design @explicit_zero_delay {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : !simulation.logic<8> design {propagation_delays = array<i64: 0, 0, 0>}
    simulation.storage.decl 0 in 0 : !simulation.logic<8> design
    simulation.storage.decl 1 in 0 : !simulation.logic<8> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<8> design
    simulation.code_unit.decl 1 in 0 continuous hierarchy "explicit_zero_delay"
    // CHECK-LABEL: regions @explicit_zero_delay valid=0 reason=memory effects
    simulation.func @explicit_zero_delay(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 1 : i64} {
      %net = simulation.context.net %ctx[0] : !simulation.net<!simulation.logic<8>>
      %driver = simulation.context.driver %ctx[0] : !simulation.driver<!simulation.logic<8>>
      %in = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<8>>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<8>>
      cf.br ^body
    ^body:
      %value = simulation.ref.load %in : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.driver.drive %driver = %value : !simulation.driver<!simulation.logic<8>>, !simulation.logic<8>
      %resolved = simulation.net.read %net : !simulation.net<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.ref.store %resolved to %out : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.suspend.change %in to ^body : !simulation.ref<!simulation.logic<8>>
    }
  }
  simulation.design @connected {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : !simulation.logic<8> design
    simulation.net.decl 1 in 0 : !simulation.logic<8> design
    simulation.net.connect.decl 0 in 0 0[0] to 1[0] width 8 reversed = false
    simulation.storage.decl 0 in 0 : !simulation.logic<8> design
    simulation.storage.decl 1 in 0 : !simulation.logic<8> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<8> design
    simulation.code_unit.decl 1 in 0 continuous hierarchy "connected"
    // CHECK-LABEL: regions @connected valid=0 reason=memory effects
    simulation.func @connected(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 1 : i64} {
      %net = simulation.context.net %ctx[0] : !simulation.net<!simulation.logic<8>>
      %driver = simulation.context.driver %ctx[0] : !simulation.driver<!simulation.logic<8>>
      %in = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<8>>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<8>>
      cf.br ^body
    ^body:
      %value = simulation.ref.load %in : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.driver.drive %driver = %value : !simulation.driver<!simulation.logic<8>>, !simulation.logic<8>
      %resolved = simulation.net.read %net : !simulation.net<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.ref.store %resolved to %out : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.suspend.change %in to ^body : !simulation.ref<!simulation.logic<8>>
    }
  }
  simulation.design @pass_switch {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : !simulation.logic<8> design
    simulation.net.decl 1 in 0 : !simulation.logic<8> design
    simulation.net.pass.decl 0 in 0 0[0] to 1[0] width 8 reversed = false
    simulation.storage.decl 0 in 0 : !simulation.logic<8> design
    simulation.storage.decl 1 in 0 : !simulation.logic<8> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<8> design
    simulation.code_unit.decl 1 in 0 continuous hierarchy "pass_switch"
    // CHECK-LABEL: regions @pass_switch valid=0 reason=memory effects
    simulation.func @pass_switch(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 1 : i64} {
      %net = simulation.context.net %ctx[0] : !simulation.net<!simulation.logic<8>>
      %driver = simulation.context.driver %ctx[0] : !simulation.driver<!simulation.logic<8>>
      %in = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<8>>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<8>>
      cf.br ^body
    ^body:
      %value = simulation.ref.load %in : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.driver.drive %driver = %value : !simulation.driver<!simulation.logic<8>>, !simulation.logic<8>
      %resolved = simulation.net.read %net : !simulation.net<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.ref.store %resolved to %out : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.suspend.change %in to ^body : !simulation.ref<!simulation.logic<8>>
    }
  }
  simulation.design @partial_driver {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : !simulation.logic<8> design
    simulation.storage.decl 0 in 0 : !simulation.logic<8> design
    simulation.storage.decl 1 in 0 : !simulation.logic<8> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<8> design {driven_low = 0 : i64, driven_width = 4 : i64}
    simulation.code_unit.decl 1 in 0 continuous hierarchy "partial_driver"
    // CHECK-LABEL: regions @partial_driver valid=0 reason=memory effects
    simulation.func @partial_driver(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 1 : i64} {
      %net = simulation.context.net %ctx[0] : !simulation.net<!simulation.logic<8>>
      %driver = simulation.context.driver %ctx[0] : !simulation.driver<!simulation.logic<8>>
      %in = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<8>>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<8>>
      cf.br ^body
    ^body:
      %value = simulation.ref.load %in : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.driver.drive %driver = %value : !simulation.driver<!simulation.logic<8>>, !simulation.logic<8>
      %resolved = simulation.net.read %net : !simulation.net<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.ref.store %resolved to %out : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.suspend.change %in to ^body : !simulation.ref<!simulation.logic<8>>
    }
  }
}
