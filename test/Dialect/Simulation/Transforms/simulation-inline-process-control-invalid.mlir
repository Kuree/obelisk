// RUN: not obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-inline{opt-level=0}))' 2>&1 | FileCheck %s

module {
  simulation.design @invalid_process_control_boundaries {
    simulation.scope.decl 0 hierarchy "top"
    simulation.class.decl @Worker id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.method @Worker_control of @Worker slot 0 signature_id 7
        implemented_by @virtual_control :
      (!simulation.context, !simulation.class_handle<@Worker>,
       !simulation.process) -> () {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = true
    }
    simulation.class.method @Worker_direct of @Worker
        implemented_by @direct_control :
      (!simulation.context, !simulation.class_handle<@Worker>,
       !simulation.process) -> () {
        is_final = false, is_pure = false, is_static = false,
        is_task = false, is_virtual = false
    }
    simulation.code_unit.decl 1 in 0 function hierarchy "top.public_control"
    simulation.code_unit.decl 2 in 0 function hierarchy "top.virtual_control"
    simulation.code_unit.decl 3 in 0 function hierarchy "top.direct_control"

    // Resume remains synchronous at a callable bytecode boundary.
    simulation.func @public_control(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %process: !simulation.process {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      simulation.process.control resume %process to ^continued
    ^continued:
      simulation.return
    }

    // A descriptor-reachable virtual method is likewise a callable boundary;
    // ordinary call inlining cannot replace dynamic descriptor dispatch.
    simulation.func private @virtual_control(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@Worker> {simulation.capture_kind = 1 : i32},
        %process: !simulation.process {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      simulation.process.control suspend %process to ^continued
    ^continued:
      simulation.return
    }

    // Kill can unwind a callable bytecode stack without a resumable frame.
    simulation.func private @direct_control(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@Worker> {simulation.capture_kind = 1 : i32},
        %process: !simulation.process {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      simulation.process.control kill %process to ^continued
    ^continued:
      simulation.return
    }
  }
}

// Only suspend needs a persistent callable CPS frame.
// CHECK-COUNT-1: 'simulation.process.control' op cannot remain in a zero-time function after mandatory process-control inlining
