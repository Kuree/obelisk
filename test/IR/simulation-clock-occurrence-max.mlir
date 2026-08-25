// RUN: obelisk-opt %s -o /dev/null
// RUN: obelisk-opt %s '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
                   llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk_sim.design @max_clock_set {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 always hierarchy "max_clock_set"
    obelisk_sim.func private @max_clock_set(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 3 : i32, code_unit_id = 1 : i64,
                    domain = 0 : i32, home_region = 8 : i32,
                    obelisk_sim.multiclock_sequence_coordinator} {
      obelisk_sim.suspend.clock_set %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock, %clock conditions 0
          edges [1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1]
          indices [-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1] site 1 to ^done :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>,
        !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^done:
      obelisk_sim.return
    }
  }
}
