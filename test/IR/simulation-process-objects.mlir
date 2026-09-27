// RUN: obelisk-opt %s | FileCheck %s

module {
  simulation.design @process_objects {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.process_objects"
    simulation.code_unit.decl 2 in 0 function hierarchy "top.function_control"

    // CHECK-LABEL: simulation.func @exercise_process_objects(
    simulation.func @exercise_process_objects(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      // CHECK: %[[NULL:.*]] = simulation.process.null
      %null = simulation.process.null
      // CHECK: %[[CURRENT:.*]] = simulation.process.current
      %current = simulation.process.current
      // CHECK: %[[EQUAL:.*]] = simulation.process.equal %[[NULL]], %[[CURRENT]]
      %equal = simulation.process.equal %null, %current
      // CHECK: %[[STATUS:.*]] = simulation.process.status %[[CURRENT]]
      %status = simulation.process.status %current
      // CHECK: %[[RNG_STATE:.*]], %[[RNG_INCREMENT:.*]] = simulation.process.random_state %[[CURRENT]]
      %rng_state, %rng_increment = simulation.process.random_state %current
      // CHECK: simulation.process.set_random_state %[[CURRENT]], %[[RNG_STATE]], %[[RNG_INCREMENT]]
      simulation.process.set_random_state %current, %rng_state, %rng_increment
      // CHECK: simulation.process.control suspend %[[CURRENT]] to ^[[SUSPEND_CONT:.*]](%[[STATUS]] : i32)
      simulation.process.control suspend %current to ^after_suspend(%status : i32)

    // CHECK: ^[[SUSPEND_CONT]](%[[FORWARDED:.*]]: i32):
    ^after_suspend(%forwarded: i32):
      // CHECK: simulation.process.control resume %[[CURRENT]] to ^[[RESUME_CONT:.*]]
      simulation.process.control resume %current to ^after_resume

    // CHECK: ^[[RESUME_CONT]]:
    ^after_resume:
      // CHECK: simulation.process.control kill %[[NULL]] to ^[[KILL_CONT:.*]]
      simulation.process.control kill %null to ^after_kill

    // CHECK: ^[[KILL_CONT]]:
    ^after_kill:
      simulation.return
    }

    // A zero-time function can dynamically control its caller. The explicit
    // successor makes the required control propagation visible before backend
    // lowering rewrites the call chain.
    // CHECK-LABEL: simulation.func @function_control(
    simulation.func @function_control(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %process: !simulation.process {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      // CHECK: simulation.process.control suspend %{{.*}} to ^[[FUNCTION_CONT:.*]]
      simulation.process.control suspend %process to ^continued
    // CHECK: ^[[FUNCTION_CONT]]:
    ^continued:
      simulation.return
    }
  }
}
