// RUN: obelisk-opt %s --split-input-file --verify-diagnostics \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph))'

// The compute-effect analysis distinguishes mutable variable storage from
// resolved nets and rejects multiple continuous writers to one variable.
module {
  simulation.design @multiple_continuous {
    simulation.code_unit.decl 9300001 in 0 continuous
        hierarchy "top.first"
    simulation.code_unit.decl 9300002 in 0 continuous
        hierarchy "top.second"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i32 design hierarchy "top.v"

    // expected-remark @below {{first continuous assignment is here}}
    simulation.func @first(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %value: !simulation.ref<i32>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9300001 : i64} {
      %constant = arith.constant 12 : i32
      simulation.ref.store %constant to %value : i32, !simulation.ref<i32>
      simulation.return
    }

    // expected-error @below {{variable 'top.v' is driven by multiple continuous assignments}}
    simulation.func @second(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %value: !simulation.ref<i32>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9300002 : i64} {
      %constant = arith.constant 13 : i32
      simulation.ref.store %constant to %value : i32, !simulation.ref<i32>
      simulation.return
    }
  }
}

// -----

// A procedural NBA is also a writer for the variable-driver legality rule.
module {
  simulation.design @mixed_writers {
    simulation.code_unit.decl 9300011 in 0 continuous
        hierarchy "top.continuous"
    simulation.code_unit.decl 9300012 in 0 always
        hierarchy "top.procedural"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i32 design hierarchy "top.v"

    // expected-remark @below {{continuous assignment is here}}
    simulation.func @continuous(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %value: !simulation.ref<i32>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9300011 : i64} {
      %constant = arith.constant 12 : i32
      simulation.ref.store %constant to %value : i32, !simulation.ref<i32>
      simulation.return
    }

    // expected-error @below {{variable 'top.v' is written by both continuous and procedural assignments}}
    simulation.func @procedural(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %value: !simulation.ref<i32>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 9300012 : i64} {
      %constant = arith.constant 13 : i32
      simulation.nba.enqueue %constant to %value :
          (i32, !simulation.ref<i32>) -> ()
      simulation.return
    }
  }
}

// -----

// Writes performed as a function-call side effect are not continuous drivers.
// Only each unit's lvalue_only binding participates in the variable-driver
// legality rule, so two continuous RHS evaluations may legally race on shared
// procedural state while driving distinct targets.
module {
  simulation.design @continuous_rhs_side_effects {
    simulation.code_unit.decl 9300021 in 0 continuous
        hierarchy "top.first"
    simulation.code_unit.decl 9300022 in 0 continuous
        hierarchy "top.second"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i32 design hierarchy "top.side"
    simulation.storage.decl 1 in 0 : i32 design hierarchy "top.first_target"
    simulation.storage.decl 2 in 0 : i32 design hierarchy "top.second_target"

    simulation.func @first_side_effect(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %side: !simulation.ref<i32>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64},
        %target: !simulation.ref<i32>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9300021 : i64,
                    simulation.bindings = [
                      #simulation.argument_binding<path = "top.side", argument = 1, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.first_target", argument = 2, kind = lvalue_only, copyOut = false>]} {
      %constant = arith.constant 12 : i32
      simulation.ref.store %constant to %side : i32, !simulation.ref<i32>
      simulation.ref.store %constant to %target : i32, !simulation.ref<i32>
      simulation.return
    }

    simulation.func @second_side_effect(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %side: !simulation.ref<i32>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64},
        %target: !simulation.ref<i32>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9300022 : i64,
                    simulation.bindings = [
                      #simulation.argument_binding<path = "top.side", argument = 1, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.second_target", argument = 2, kind = lvalue_only, copyOut = false>]} {
      %constant = arith.constant 13 : i32
      simulation.ref.store %constant to %side : i32, !simulation.ref<i32>
      simulation.ref.store %constant to %target : i32, !simulation.ref<i32>
      simulation.return
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
  simulation.design @disjoint_continuous {
    simulation.code_unit.decl 9300021 in 0 continuous
        hierarchy "top.low"
    simulation.code_unit.decl 9300022 in 0 continuous
        hierarchy "top.high"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 :
        !simulation.packed_array<7 : 0 x !simulation.logic<1>>
        design hierarchy "top.v"

    simulation.func @low(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %value: !simulation.ref<
            !simulation.packed_array<7 : 0 x !simulation.logic<1>>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9300021 : i64} {
      %constant = simulation.logic.constant 0 : i4, 0 : i4 :
          !simulation.logic<4>
      %packed = simulation.packed.unflatten %constant :
          (!simulation.logic<4>) ->
          !simulation.packed_array<3 : 0 x !simulation.logic<1>>
      %part = simulation.ref.extract %value from 0 :
          !simulation.ref<
              !simulation.packed_array<7 : 0 x !simulation.logic<1>>> ->
          !simulation.ref<
              !simulation.packed_array<3 : 0 x !simulation.logic<1>>>
      simulation.ref.store %packed to %part
          {simulation.continuous_store} :
          !simulation.packed_array<3 : 0 x !simulation.logic<1>>,
          !simulation.ref<
              !simulation.packed_array<3 : 0 x !simulation.logic<1>>>
      simulation.return
    }

    simulation.func @high(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %value: !simulation.ref<
            !simulation.packed_array<7 : 0 x !simulation.logic<1>>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9300022 : i64} {
      %constant = simulation.logic.constant 0 : i4, 0 : i4 :
          !simulation.logic<4>
      %packed = simulation.packed.unflatten %constant :
          (!simulation.logic<4>) ->
          !simulation.packed_array<7 : 4 x !simulation.logic<1>>
      %part = simulation.ref.extract %value from 4 :
          !simulation.ref<
              !simulation.packed_array<7 : 0 x !simulation.logic<1>>> ->
          !simulation.ref<
              !simulation.packed_array<7 : 4 x !simulation.logic<1>>>
      simulation.ref.store %packed to %part
          {simulation.continuous_store} :
          !simulation.packed_array<7 : 4 x !simulation.logic<1>>,
          !simulation.ref<
              !simulation.packed_array<7 : 4 x !simulation.logic<1>>>
      simulation.return
    }
  }
}
