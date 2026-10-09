// RUN: obelisk-opt %s --test-obelisk-activation-regions='functions=a,b publications=true reaching-values=true' -o %t.analyzed 2>&1 | FileCheck %s
// RUN: obelisk-opt %s -o %t.original
// RUN: diff %t.original %t.analyzed
// Static mux configuration can prove acyclicity. A runtime configuration
// retains both possible cones; it cannot be assumed to break real feedback.
module {
  simulation.design @static_config {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.storage.decl 2 in 0 : i1 design
    simulation.storage.decl 3 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "a"
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "b"
    // CHECK-LABEL: regions @a valid=1
    simulation.func @a(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      cf.br ^body
    ^body:
      %out = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %feedback = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %config = simulation.context.storage %ctx[2] : !simulation.ref<i1>
      %in = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %c = arith.constant false
      %old = simulation.ref.load %feedback : !simulation.ref<i8> -> i8
      %v = simulation.ref.load %in : !simulation.ref<i8> -> i8
      %result = arith.select %c, %old, %v : i8
      simulation.ref.store %result to %out : i8, !simulation.ref<i8>
      simulation.suspend.any %feedback, %config, %in edges [0, 0, 0] to ^body : !simulation.ref<i8>, !simulation.ref<i1>, !simulation.ref<i8>
    }
    // CHECK-LABEL: regions @b valid=1
    simulation.func @b(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %in = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %v = simulation.ref.load %in : !simulation.ref<i8> -> i8
      simulation.ref.store %v to %out : i8, !simulation.ref<i8>
      simulation.suspend.change %in to ^body : !simulation.ref<i8>
    }
    // CHECK: publication graph: acyclic
  }
  simulation.design @runtime_config {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.storage.decl 2 in 0 : i1 design
    simulation.storage.decl 3 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "a"
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "b"
    // CHECK-LABEL: regions @a valid=1
    simulation.func @a(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      cf.br ^body
    ^body:
      %out = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %feedback = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %config = simulation.context.storage %ctx[2] : !simulation.ref<i1>
      %in = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %c = simulation.ref.load %config : !simulation.ref<i1> -> i1
      %old = simulation.ref.load %feedback : !simulation.ref<i8> -> i8
      %v = simulation.ref.load %in : !simulation.ref<i8> -> i8
      %result = arith.select %c, %old, %v : i8
      simulation.ref.store %result to %out : i8, !simulation.ref<i8>
      simulation.suspend.any %feedback, %config, %in edges [0, 0, 0] to ^body : !simulation.ref<i8>, !simulation.ref<i1>, !simulation.ref<i8>
    }
    // CHECK-LABEL: regions @b valid=1
    simulation.func @b(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %in = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %v = simulation.ref.load %in : !simulation.ref<i8> -> i8
      simulation.ref.store %v to %out : i8, !simulation.ref<i8>
      simulation.suspend.change %in to ^body : !simulation.ref<i8>
    }
    // CHECK: publication graph: cyclic
  }
  // A cross-root pointer choice must not become an empty dependency: the
  // access depends on every root it may select and on the selector.
  simulation.design @unresolved_alias {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.storage.decl 2 in 0 : i1 design
    simulation.storage.decl 3 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "a"
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "b"
    // CHECK-LABEL: regions @a valid=1
    // CHECK: node simulation.ref.load read:s1[0,8) read:s3[0,8)
    // CHECK: publication read:s1[0,8) read:s2[0,1) read:s3[0,8) write:s0[0,8)
    simulation.func @a(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      cf.br ^body
    ^body:
      %out = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %feedback = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %config = simulation.context.storage %ctx[2] : !simulation.ref<i1>
      %in = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      %c = simulation.ref.load %config : !simulation.ref<i1> -> i1
      %selected = arith.select %c, %feedback, %in : !simulation.ref<i8>
      %result = simulation.ref.load %selected : !simulation.ref<i8> -> i8
      simulation.ref.store %result to %out : i8, !simulation.ref<i8>
      simulation.suspend.any %feedback, %config, %in edges [0, 0, 0] to ^body : !simulation.ref<i8>, !simulation.ref<i1>, !simulation.ref<i8>
    }
    // CHECK-LABEL: regions @b valid=1
    simulation.func @b(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %in = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %v = simulation.ref.load %in : !simulation.ref<i8> -> i8
      simulation.ref.store %v to %out : i8, !simulation.ref<i8>
      simulation.suspend.change %in to ^body : !simulation.ref<i8>
    }
    // CHECK: publication graph: cyclic
  }
}
