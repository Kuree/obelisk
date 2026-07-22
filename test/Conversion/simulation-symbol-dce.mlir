// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(symbol-dce))' | FileCheck %s

module {
  obelisk_sim.design @dce {
    obelisk_sim.scope.decl 0

    // Keep dead symbols before the first expected symbol so CHECK-NOT covers
    // the entire prefix where they could survive in stable IR order.
    obelisk_sim.func private @unused_definition(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32} {
      obelisk_sim.return
    }
    obelisk_sim.func private @unused_declaration(
        !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32}

    // CHECK-NOT: @unused_definition
    // CHECK-NOT: @unused_declaration
    // CHECK-LABEL: obelisk_sim.func @__obelisk_root
    // CHECK: obelisk_sim.call @recursive_a
    // CHECK: obelisk_sim.spawn @live_process
    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      obelisk_sim.call @recursive_a(%ctx) : (!obelisk_sim.context) -> ()
      %process = obelisk_sim.spawn @live_process(%ctx) : !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }

    // The call closure is recursive and must remain complete.
    // CHECK-LABEL: obelisk_sim.func private @recursive_a
    // CHECK: obelisk_sim.call @recursive_b
    obelisk_sim.func private @recursive_a(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32} {
      obelisk_sim.call @recursive_b(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.return
    }

    // CHECK-LABEL: obelisk_sim.func private @recursive_b
    // CHECK: obelisk_sim.call @recursive_a
    obelisk_sim.func private @recursive_b(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32} {
      obelisk_sim.call @recursive_a(%ctx) : (!obelisk_sim.context) -> ()
      obelisk_sim.return
    }

    // Spawn edges root the complete process closure as well.
    // CHECK-LABEL: obelisk_sim.func private @live_process
    // CHECK: obelisk_sim.spawn @live_child
    obelisk_sim.func private @live_process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32} {
      %child = obelisk_sim.spawn @live_child(%ctx) : !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }

    // CHECK-LABEL: obelisk_sim.func private @live_child
    obelisk_sim.func private @live_child(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32} {
      obelisk_sim.return
    }
  }
}
