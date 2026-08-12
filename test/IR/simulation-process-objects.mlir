// RUN: obelisk-opt %s | FileCheck %s

module {
  obelisk_sim.design @process_objects {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.process_objects"
    obelisk_sim.code_unit.decl 2 in 0 function hierarchy "top.function_control"

    // CHECK-LABEL: obelisk_sim.func @exercise_process_objects(
    obelisk_sim.func @exercise_process_objects(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      // CHECK: %[[NULL:.*]] = obelisk_sim.process.null
      %null = obelisk_sim.process.null
      // CHECK: %[[CURRENT:.*]] = obelisk_sim.process.current
      %current = obelisk_sim.process.current
      // CHECK: %[[EQUAL:.*]] = obelisk_sim.process.equal %[[NULL]], %[[CURRENT]]
      %equal = obelisk_sim.process.equal %null, %current
      // CHECK: %[[STATUS:.*]] = obelisk_sim.process.status %[[CURRENT]]
      %status = obelisk_sim.process.status %current
      // CHECK: %[[RNG_STATE:.*]], %[[RNG_INCREMENT:.*]] = obelisk_sim.process.random_state %[[CURRENT]]
      %rng_state, %rng_increment = obelisk_sim.process.random_state %current
      // CHECK: obelisk_sim.process.set_random_state %[[CURRENT]], %[[RNG_STATE]], %[[RNG_INCREMENT]]
      obelisk_sim.process.set_random_state %current, %rng_state, %rng_increment
      // CHECK: obelisk_sim.process.control suspend %[[CURRENT]] to ^[[SUSPEND_CONT:.*]](%[[STATUS]] : i32)
      obelisk_sim.process.control suspend %current to ^after_suspend(%status : i32)

    // CHECK: ^[[SUSPEND_CONT]](%[[FORWARDED:.*]]: i32):
    ^after_suspend(%forwarded: i32):
      // CHECK: obelisk_sim.process.control resume %[[CURRENT]] to ^[[RESUME_CONT:.*]]
      obelisk_sim.process.control resume %current to ^after_resume

    // CHECK: ^[[RESUME_CONT]]:
    ^after_resume:
      // CHECK: obelisk_sim.process.control kill %[[NULL]] to ^[[KILL_CONT:.*]]
      obelisk_sim.process.control kill %null to ^after_kill

    // CHECK: ^[[KILL_CONT]]:
    ^after_kill:
      obelisk_sim.return
    }

    // A zero-time function can dynamically control its caller. The explicit
    // successor makes the required control propagation visible before backend
    // lowering rewrites the call chain.
    // CHECK-LABEL: obelisk_sim.func @function_control(
    obelisk_sim.func @function_control(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %process: !obelisk_sim.process {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      // CHECK: obelisk_sim.process.control suspend %{{.*}} to ^[[FUNCTION_CONT:.*]]
      obelisk_sim.process.control suspend %process to ^continued
    // CHECK: ^[[FUNCTION_CONT]]:
    ^continued:
      obelisk_sim.return
    }
  }
}
