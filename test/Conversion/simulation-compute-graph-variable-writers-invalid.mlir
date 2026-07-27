// RUN: obelisk-opt %s --split-input-file --verify-diagnostics \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph))'

// The compute-effect analysis distinguishes mutable variable storage from
// resolved nets and rejects multiple continuous writers to one variable.
module {
  obelisk_sim.design @multiple_continuous {
    obelisk_sim.code_unit.decl 9300001 in 0 continuous
        hierarchy "top.first"
    obelisk_sim.code_unit.decl 9300002 in 0 continuous
        hierarchy "top.second"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i32 design hierarchy "top.v"

    // expected-remark @below {{first continuous assignment is here}}
    obelisk_sim.func @first(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.ref<i32>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9300001 : i64} {
      %constant = arith.constant 12 : i32
      obelisk_sim.ref.store %constant to %value : i32, !obelisk_sim.ref<i32>
      obelisk_sim.return
    }

    // expected-error @below {{variable 'top.v' is driven by multiple continuous assignments}}
    obelisk_sim.func @second(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.ref<i32>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9300002 : i64} {
      %constant = arith.constant 13 : i32
      obelisk_sim.ref.store %constant to %value : i32, !obelisk_sim.ref<i32>
      obelisk_sim.return
    }
  }
}

// -----

// A procedural NBA is also a writer for the variable-driver legality rule.
module {
  obelisk_sim.design @mixed_writers {
    obelisk_sim.code_unit.decl 9300011 in 0 continuous
        hierarchy "top.continuous"
    obelisk_sim.code_unit.decl 9300012 in 0 always
        hierarchy "top.procedural"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i32 design hierarchy "top.v"

    // expected-remark @below {{continuous assignment is here}}
    obelisk_sim.func @continuous(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.ref<i32>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9300011 : i64} {
      %constant = arith.constant 12 : i32
      obelisk_sim.ref.store %constant to %value : i32, !obelisk_sim.ref<i32>
      obelisk_sim.return
    }

    // expected-error @below {{variable 'top.v' is written by both continuous and procedural assignments}}
    obelisk_sim.func @procedural(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.ref<i32>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 9300012 : i64} {
      %constant = arith.constant 13 : i32
      obelisk_sim.nba.enqueue %constant to %value :
          (i32, !obelisk_sim.ref<i32>) -> ()
      obelisk_sim.return
    }
  }
}
