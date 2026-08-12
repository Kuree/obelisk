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


// -----

// Two continuous assignments to disjoint parts of one variable are separate
// drivers of that variable, which IEEE 1800-2017 10.3.2 allows. Splitting a
// bus or an unpacked array across assignments is ordinary RTL, so the legality
// rule compares the bits each writer reaches, not just the descriptor they
// share. No diagnostic is expected here.
module {
  obelisk_sim.design @disjoint_continuous {
    obelisk_sim.code_unit.decl 9300021 in 0 continuous
        hierarchy "top.low"
    obelisk_sim.code_unit.decl 9300022 in 0 continuous
        hierarchy "top.high"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 :
        !obelisk_sim.packed_array<7 : 0 x !obelisk_sim.logic<1>>
        design hierarchy "top.v"

    obelisk_sim.func @low(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.ref<
            !obelisk_sim.packed_array<7 : 0 x !obelisk_sim.logic<1>>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9300021 : i64} {
      %constant = obelisk_sim.logic.constant 0 : i4, 0 : i4 :
          !obelisk_sim.logic<4>
      %packed = obelisk_sim.packed.unflatten %constant :
          (!obelisk_sim.logic<4>) ->
          !obelisk_sim.packed_array<3 : 0 x !obelisk_sim.logic<1>>
      %part = obelisk_sim.ref.extract %value from 0 :
          !obelisk_sim.ref<
              !obelisk_sim.packed_array<7 : 0 x !obelisk_sim.logic<1>>> ->
          !obelisk_sim.ref<
              !obelisk_sim.packed_array<3 : 0 x !obelisk_sim.logic<1>>>
      obelisk_sim.ref.store %packed to %part
          {obelisk_sim.continuous_store} :
          !obelisk_sim.packed_array<3 : 0 x !obelisk_sim.logic<1>>,
          !obelisk_sim.ref<
              !obelisk_sim.packed_array<3 : 0 x !obelisk_sim.logic<1>>>
      obelisk_sim.return
    }

    obelisk_sim.func @high(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.ref<
            !obelisk_sim.packed_array<7 : 0 x !obelisk_sim.logic<1>>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9300022 : i64} {
      %constant = obelisk_sim.logic.constant 0 : i4, 0 : i4 :
          !obelisk_sim.logic<4>
      %packed = obelisk_sim.packed.unflatten %constant :
          (!obelisk_sim.logic<4>) ->
          !obelisk_sim.packed_array<7 : 4 x !obelisk_sim.logic<1>>
      %part = obelisk_sim.ref.extract %value from 4 :
          !obelisk_sim.ref<
              !obelisk_sim.packed_array<7 : 0 x !obelisk_sim.logic<1>>> ->
          !obelisk_sim.ref<
              !obelisk_sim.packed_array<7 : 4 x !obelisk_sim.logic<1>>>
      obelisk_sim.ref.store %packed to %part
          {obelisk_sim.continuous_store} :
          !obelisk_sim.packed_array<7 : 4 x !obelisk_sim.logic<1>>,
          !obelisk_sim.ref<
              !obelisk_sim.packed_array<7 : 4 x !obelisk_sim.logic<1>>>
      obelisk_sim.return
    }
  }
}
