// RUN: not obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-inline{opt-level=0}))' 2>&1 | FileCheck %s

module {
  obelisk_sim.design @invalid_process_control_boundaries {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.class.decl @Worker id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.class.method @Worker_control of @Worker slot 0 signature_id 7
        implemented_by @virtual_control :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@Worker>,
       !obelisk_sim.process) -> () {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = true
    }
    obelisk_sim.class.method @Worker_direct of @Worker
        implemented_by @direct_control :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@Worker>,
       !obelisk_sim.process) -> () {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = false
    }
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "top.public_control"
    obelisk_sim.code_unit.decl 2 in 0 function hierarchy "top.virtual_control"
    obelisk_sim.code_unit.decl 3 in 0 function hierarchy "top.direct_control"

    // Resume remains synchronous at a callable bytecode boundary.
    obelisk_sim.func @public_control(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %process: !obelisk_sim.process {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      obelisk_sim.process.control resume %process to ^continued
    ^continued:
      obelisk_sim.return
    }

    // A descriptor-reachable virtual method is likewise a callable boundary;
    // ordinary call inlining cannot replace dynamic descriptor dispatch.
    obelisk_sim.func private @virtual_control(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %this: !obelisk_sim.class_handle<@Worker> {obelisk_sim.capture_kind = 1 : i32},
        %process: !obelisk_sim.process {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      obelisk_sim.process.control suspend %process to ^continued
    ^continued:
      obelisk_sim.return
    }

    // Kill can unwind a callable bytecode stack without a resumable frame.
    obelisk_sim.func private @direct_control(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %this: !obelisk_sim.class_handle<@Worker> {obelisk_sim.capture_kind = 1 : i32},
        %process: !obelisk_sim.process {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      obelisk_sim.process.control kill %process to ^continued
    ^continued:
      obelisk_sim.return
    }
  }
}

// Only suspend needs a persistent callable CPS frame.
// CHECK-COUNT-1: 'obelisk_sim.process.control' op cannot remain in a zero-time function after mandatory process-control inlining
