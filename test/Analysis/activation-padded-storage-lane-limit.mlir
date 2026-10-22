// RUN: obelisk-opt %s '--test-obelisk-activation-regions=reaching-values=true inputs=true' -o /dev/null 2>&1 | FileCheck %s
// Every logical lane of a padded type is a separate relation at each node that
// touches it. Up to 1024 lanes the padding bits stay out of the dependencies;
// a larger type keeps every provenance lane, as an unrepresentable mask does.
!exact = !simulation.unpacked_array<1023 : 0 x i7, #simulation.storage_layout<size = 1024, alignment = 1, stride = 1, offsets = []>>
!whole = !simulation.unpacked_array<1024 : 0 x i7, #simulation.storage_layout<size = 1025, alignment = 1, stride = 1, offsets = []>>
module {
  simulation.design @lane_limit {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !exact design
    simulation.storage.decl 1 in 0 : !exact design
    simulation.storage.decl 2 in 0 : !whole design
    simulation.storage.decl 3 in 0 : !whole design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "exact_copy"
    // CHECK-LABEL: regions @exact_copy valid=1
    // CHECK: value-input s0[0,7) s0[8,15) {{.*}} s0[8184,8191){{$}}
    simulation.func @exact_copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<!exact>
      %output = simulation.context.storage %ctx[1] : !simulation.ref<!exact>
      cf.br ^body
    ^body:
      %v = simulation.ref.load %input : !simulation.ref<!exact> -> !exact
      simulation.ref.store %v to %output : !exact, !simulation.ref<!exact>
      simulation.suspend.change %input to ^body : !simulation.ref<!exact>
    }
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "whole_copy"
    // CHECK-LABEL: regions @whole_copy valid=1
    // CHECK: value-input s2[0,8200){{$}}
    simulation.func @whole_copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      %input = simulation.context.storage %ctx[2] : !simulation.ref<!whole>
      %output = simulation.context.storage %ctx[3] : !simulation.ref<!whole>
      cf.br ^body
    ^body:
      %v = simulation.ref.load %input : !simulation.ref<!whole> -> !whole
      simulation.ref.store %v to %output : !whole, !simulation.ref<!whole>
      simulation.suspend.change %input to ^body : !simulation.ref<!whole>
    }
  }
}
