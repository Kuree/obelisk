// RUN: obelisk-opt %s --allow-unregistered-dialect --test-obelisk-local-storage -o /dev/null 2>&1 | FileCheck %s
// Must-version availability, including backedge conflicts, recursive region
// interfaces, private alias families, opaque effects, and dead paths.
module {
  simulation.design @snapshots {
    func.func private @external()
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "multiple_uses"
    // CHECK: multiple_uses: borrow
    simulation.func @multiple_uses(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: i128 {simulation.capture_kind = 2 : i32},
        %condition: i1 {simulation.capture_kind = 2 : i32},
        %external: !simulation.ref<i128> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %local = simulation.ref.alloc %input : i128 -> !simulation.ref<i128>
      %old = simulation.ref.load %local {test.snapshot = "multiple_uses"} : !simulation.ref<i128> -> i128
      %unrelated = arith.addi %input, %input : i128
      %copy = simulation.ref.alloc %old : i128 -> !simulation.ref<i128>
      %second = simulation.ref.alloc %old : i128 -> !simulation.ref<i128>
      simulation.ref.store %input to %local : i128, !simulation.ref<i128>
      simulation.return
    }
    simulation.code_unit.decl 2 in 0 function hierarchy "live_old_version"
    // CHECK: live_old_version: copy
    simulation.func @live_old_version(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: i128 {simulation.capture_kind = 2 : i32},
        %condition: i1 {simulation.capture_kind = 2 : i32},
        %external: !simulation.ref<i128> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %local = simulation.ref.alloc %input : i128 -> !simulation.ref<i128>
      %old = simulation.ref.load %local {test.snapshot = "live_old_version"} : !simulation.ref<i128> -> i128
      simulation.ref.store %input to %local : i128, !simulation.ref<i128>
      %copy = simulation.ref.alloc %old : i128 -> !simulation.ref<i128>
      simulation.return
    }
    simulation.code_unit.decl 3 in 0 function hierarchy "field_clobber"
    // CHECK: field_clobber: copy
    simulation.func @field_clobber(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: i128 {simulation.capture_kind = 2 : i32},
        %condition: i1 {simulation.capture_kind = 2 : i32},
        %external: !simulation.ref<i128> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %local = simulation.ref.alloc %input : i128 -> !simulation.ref<i128>
      %old = simulation.ref.load %local {test.snapshot = "field_clobber"} : !simulation.ref<i128> -> i128
      %field = simulation.ref.extract %local from 64 : !simulation.ref<i128> -> !simulation.ref<i8>
      %zero = arith.constant 0 : i8
      simulation.ref.store %zero to %field : i8, !simulation.ref<i8>
      %copy = simulation.ref.alloc %old : i128 -> !simulation.ref<i128>
      simulation.return
    }
    simulation.code_unit.decl 4 in 0 function hierarchy "diamond_clean"
    // CHECK: diamond_clean: borrow
    simulation.func @diamond_clean(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: i128 {simulation.capture_kind = 2 : i32},
        %condition: i1 {simulation.capture_kind = 2 : i32},
        %external: !simulation.ref<i128> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %local = simulation.ref.alloc %input : i128 -> !simulation.ref<i128>
      %other = simulation.ref.alloc %input : i128 -> !simulation.ref<i128>
      %old = simulation.ref.load %local {test.snapshot = "diamond_clean"} : !simulation.ref<i128> -> i128
      cf.cond_br %condition, ^left, ^right
      ^left:
      simulation.ref.store %input to %other : i128, !simulation.ref<i128>
      cf.br ^join
      ^right:
      cf.br ^join
      ^join:
      %copy = simulation.ref.alloc %old : i128 -> !simulation.ref<i128>
      simulation.return
    }
    simulation.code_unit.decl 5 in 0 function hierarchy "diamond_clobber"
    // CHECK: diamond_clobber: copy
    simulation.func @diamond_clobber(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: i128 {simulation.capture_kind = 2 : i32},
        %condition: i1 {simulation.capture_kind = 2 : i32},
        %external: !simulation.ref<i128> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      %local = simulation.ref.alloc %input : i128 -> !simulation.ref<i128>
      %other = simulation.ref.alloc %input : i128 -> !simulation.ref<i128>
      %old = simulation.ref.load %local {test.snapshot = "diamond_clobber"} : !simulation.ref<i128> -> i128
      cf.cond_br %condition, ^left, ^right
      ^left:
      simulation.ref.store %input to %local : i128, !simulation.ref<i128>
      cf.br ^join
      ^right:
      cf.br ^join
      ^join:
      %copy = simulation.ref.alloc %old : i128 -> !simulation.ref<i128>
      simulation.return
    }
    simulation.code_unit.decl 6 in 0 function hierarchy "dead_clobber"
    // CHECK: dead_clobber: borrow
    simulation.func @dead_clobber(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: i128 {simulation.capture_kind = 2 : i32},
        %condition: i1 {simulation.capture_kind = 2 : i32},
        %external: !simulation.ref<i128> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64} {
      %local = simulation.ref.alloc %input : i128 -> !simulation.ref<i128>
      %old = simulation.ref.load %local {test.snapshot = "dead_clobber"} : !simulation.ref<i128> -> i128
      %false = arith.constant false
      cf.cond_br %false, ^write, ^join
      ^write:
      simulation.ref.store %input to %local : i128, !simulation.ref<i128>
      cf.br ^join
      ^join:
      %copy = simulation.ref.alloc %old : i128 -> !simulation.ref<i128>
      simulation.return
    }
    simulation.code_unit.decl 7 in 0 function hierarchy "loop_clean"
    // CHECK: loop_clean: borrow
    simulation.func @loop_clean(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: i128 {simulation.capture_kind = 2 : i32},
        %condition: i1 {simulation.capture_kind = 2 : i32},
        %external: !simulation.ref<i128> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 7 : i64} {
      %local = simulation.ref.alloc %input : i128 -> !simulation.ref<i128>
      %old = simulation.ref.load %local {test.snapshot = "loop_clean"} : !simulation.ref<i128> -> i128
      cf.br ^loop
      ^loop:
      %copy = simulation.ref.alloc %old : i128 -> !simulation.ref<i128>

      cf.cond_br %condition, ^loop, ^exit
      ^exit:
      simulation.return
    }
    simulation.code_unit.decl 8 in 0 function hierarchy "loop_clobber"
    // CHECK: loop_clobber: copy
    simulation.func @loop_clobber(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: i128 {simulation.capture_kind = 2 : i32},
        %condition: i1 {simulation.capture_kind = 2 : i32},
        %external: !simulation.ref<i128> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 8 : i64} {
      %local = simulation.ref.alloc %input : i128 -> !simulation.ref<i128>
      %old = simulation.ref.load %local {test.snapshot = "loop_clobber"} : !simulation.ref<i128> -> i128
      cf.br ^loop
      ^loop:
      %copy = simulation.ref.alloc %old : i128 -> !simulation.ref<i128>
      simulation.ref.store %input to %local : i128, !simulation.ref<i128>
      cf.cond_br %condition, ^loop, ^exit
      ^exit:
      simulation.return
    }
    simulation.code_unit.decl 9 in 0 function hierarchy "loop_fresh_version"
    // CHECK: loop_fresh_version: borrow
    simulation.func @loop_fresh_version(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: i128 {simulation.capture_kind = 2 : i32},
        %condition: i1 {simulation.capture_kind = 2 : i32},
        %external: !simulation.ref<i128> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 9 : i64} {
      %local = simulation.ref.alloc %input : i128 -> !simulation.ref<i128>
      cf.br ^loop
      ^loop:
      %old = simulation.ref.load %local {test.snapshot = "loop_fresh_version"} : !simulation.ref<i128> -> i128
      %copy = simulation.ref.alloc %old : i128 -> !simulation.ref<i128>
      simulation.ref.store %input to %local : i128, !simulation.ref<i128>
      cf.cond_br %condition, ^loop, ^exit
      ^exit:
      simulation.return
    }
    simulation.code_unit.decl 10 in 0 function hierarchy "region_clean"
    // CHECK: region_clean: borrow
    simulation.func @region_clean(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: i128 {simulation.capture_kind = 2 : i32},
        %condition: i1 {simulation.capture_kind = 2 : i32},
        %external: !simulation.ref<i128> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 10 : i64} {
      %local = simulation.ref.alloc %input : i128 -> !simulation.ref<i128>
      %other = simulation.ref.alloc %input : i128 -> !simulation.ref<i128>
      %old = simulation.ref.load %local {test.snapshot = "region_clean"} : !simulation.ref<i128> -> i128
      scf.if %condition {
        simulation.ref.store %input to %other : i128, !simulation.ref<i128>
      }
      %copy = simulation.ref.alloc %old : i128 -> !simulation.ref<i128>
      simulation.return
    }
    simulation.code_unit.decl 11 in 0 function hierarchy "region_clobber"
    // CHECK: region_clobber: copy
    simulation.func @region_clobber(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: i128 {simulation.capture_kind = 2 : i32},
        %condition: i1 {simulation.capture_kind = 2 : i32},
        %external: !simulation.ref<i128> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 11 : i64} {
      %local = simulation.ref.alloc %input : i128 -> !simulation.ref<i128>
      %other = simulation.ref.alloc %input : i128 -> !simulation.ref<i128>
      %old = simulation.ref.load %local {test.snapshot = "region_clobber"} : !simulation.ref<i128> -> i128
      scf.if %condition {
        simulation.ref.store %input to %local : i128, !simulation.ref<i128>
      }
      %copy = simulation.ref.alloc %old : i128 -> !simulation.ref<i128>
      simulation.return
    }
    simulation.code_unit.decl 12 in 0 function hierarchy "region_loop_clobber"
    // CHECK: region_loop_clobber: copy
    simulation.func @region_loop_clobber(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: i128 {simulation.capture_kind = 2 : i32},
        %condition: i1 {simulation.capture_kind = 2 : i32},
        %external: !simulation.ref<i128> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 12 : i64} {
      %local = simulation.ref.alloc %input : i128 -> !simulation.ref<i128>
      %old = simulation.ref.load %local {test.snapshot = "region_loop_clobber"} : !simulation.ref<i128> -> i128
      scf.while : () -> () {
        scf.condition(%condition)
      } do {
        %copy = simulation.ref.alloc %old : i128 -> !simulation.ref<i128>
        simulation.ref.store %input to %local : i128, !simulation.ref<i128>
        scf.yield
      }
      simulation.return
    }
    simulation.code_unit.decl 13 in 0 function hierarchy "region_loop_fresh_version"
    // CHECK: region_loop_fresh_version: borrow
    simulation.func @region_loop_fresh_version(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: i128 {simulation.capture_kind = 2 : i32},
        %condition: i1 {simulation.capture_kind = 2 : i32},
        %external: !simulation.ref<i128> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 13 : i64} {
      %local = simulation.ref.alloc %input : i128 -> !simulation.ref<i128>
      scf.while : () -> () {
        scf.condition(%condition)
      } do {
        %old = simulation.ref.load %local {test.snapshot = "region_loop_fresh_version"} : !simulation.ref<i128> -> i128
        %copy = simulation.ref.alloc %old : i128 -> !simulation.ref<i128>
        simulation.ref.store %input to %local : i128, !simulation.ref<i128>
        scf.yield
      }
      simulation.return
    }
    simulation.code_unit.decl 14 in 0 function hierarchy "private_call"
    // CHECK: private_call: borrow
    simulation.func @private_call(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: i128 {simulation.capture_kind = 2 : i32},
        %condition: i1 {simulation.capture_kind = 2 : i32},
        %external: !simulation.ref<i128> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 14 : i64} {
      %local = simulation.ref.alloc %input : i128 -> !simulation.ref<i128>
      %old = simulation.ref.load %local {test.snapshot = "private_call"} : !simulation.ref<i128> -> i128
      func.call @external() : () -> ()
      %copy = simulation.ref.alloc %old : i128 -> !simulation.ref<i128>
      simulation.return
    }
    simulation.code_unit.decl 15 in 0 function hierarchy "private_opaque"
    // CHECK: private_opaque: borrow
    simulation.func @private_opaque(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: i128 {simulation.capture_kind = 2 : i32},
        %condition: i1 {simulation.capture_kind = 2 : i32},
        %external: !simulation.ref<i128> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 15 : i64} {
      %local = simulation.ref.alloc %input : i128 -> !simulation.ref<i128>
      %old = simulation.ref.load %local {test.snapshot = "private_opaque"} : !simulation.ref<i128> -> i128
      "test.unknown"() : () -> ()
      %copy = simulation.ref.alloc %old : i128 -> !simulation.ref<i128>
      simulation.return
    }
    simulation.code_unit.decl 16 in 0 function hierarchy "external_call"
    // CHECK: external_call: copy
    simulation.func @external_call(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: i128 {simulation.capture_kind = 2 : i32},
        %condition: i1 {simulation.capture_kind = 2 : i32},
        %external: !simulation.ref<i128> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 16 : i64} {
      %local = simulation.ref.alloc %input : i128 -> !simulation.ref<i128>
      %old = simulation.ref.load %external {test.snapshot = "external_call"} : !simulation.ref<i128> -> i128
      func.call @external() : () -> ()
      %copy = simulation.ref.alloc %old : i128 -> !simulation.ref<i128>
      simulation.return
    }
    simulation.code_unit.decl 17 in 0 function hierarchy "external_opaque"
    // CHECK: external_opaque: copy
    simulation.func @external_opaque(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: i128 {simulation.capture_kind = 2 : i32},
        %condition: i1 {simulation.capture_kind = 2 : i32},
        %external: !simulation.ref<i128> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 17 : i64} {
      %local = simulation.ref.alloc %input : i128 -> !simulation.ref<i128>
      %old = simulation.ref.load %external {test.snapshot = "external_opaque"} : !simulation.ref<i128> -> i128
      "test.unknown"() : () -> ()
      %copy = simulation.ref.alloc %old : i128 -> !simulation.ref<i128>
      simulation.return
    }
    simulation.code_unit.decl 18 in 0 function hierarchy "self_copy"
    // CHECK: self_copy: copy
    simulation.func @self_copy(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: i128 {simulation.capture_kind = 2 : i32},
        %condition: i1 {simulation.capture_kind = 2 : i32},
        %external: !simulation.ref<i128> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 18 : i64} {
      %local = simulation.ref.alloc %input : i128 -> !simulation.ref<i128>
      %old = simulation.ref.load %local {test.snapshot = "self_copy"} : !simulation.ref<i128> -> i128
      simulation.ref.store %old to %local : i128, !simulation.ref<i128>
      simulation.return
    }
    simulation.code_unit.decl 19 in 0 function hierarchy "external_private_copies"
    // CHECK: external_private_copies: borrow
    simulation.func @external_private_copies(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: i128 {simulation.capture_kind = 2 : i32},
        %condition: i1 {simulation.capture_kind = 2 : i32},
        %external: !simulation.ref<i128> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 19 : i64} {
      %local = simulation.ref.alloc %input : i128 -> !simulation.ref<i128>
      %old = simulation.ref.load %external {test.snapshot = "external_private_copies"} : !simulation.ref<i128> -> i128
      cf.cond_br %condition, ^left, ^right
      ^left:
      simulation.ref.store %old to %local : i128, !simulation.ref<i128>
      cf.br ^join
      ^right:
      %first = simulation.ref.alloc %old : i128 -> !simulation.ref<i128>
      cf.br ^join
      ^join:
      %copy = simulation.ref.alloc %old : i128 -> !simulation.ref<i128>
      simulation.return
    }
    simulation.code_unit.decl 20 in 0 function hierarchy "external_possible_alias"
    // CHECK: external_possible_alias: copy
    simulation.func @external_possible_alias(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: i128 {simulation.capture_kind = 2 : i32},
        %condition: i1 {simulation.capture_kind = 2 : i32},
        %external: !simulation.ref<i128> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 20 : i64} {
      %local = simulation.ref.alloc %input : i128 -> !simulation.ref<i128>
      %old = simulation.ref.load %external {test.snapshot = "external_possible_alias"} : !simulation.ref<i128> -> i128
      simulation.ref.store %input to %external : i128, !simulation.ref<i128>
      %copy = simulation.ref.alloc %old : i128 -> !simulation.ref<i128>
      simulation.return
    }
    simulation.code_unit.decl 21 in 0 function hierarchy "forwarded_private_alias"
    // CHECK: forwarded_private_alias: copy
    simulation.func @forwarded_private_alias(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: i128 {simulation.capture_kind = 2 : i32},
        %condition: i1 {simulation.capture_kind = 2 : i32},
        %external: !simulation.ref<i128> {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 21 : i64} {
      %local = simulation.ref.alloc %input : i128 -> !simulation.ref<i128>
      %old = simulation.ref.load %local {test.snapshot = "forwarded_private_alias"} : !simulation.ref<i128> -> i128
      cf.br ^next(%local : !simulation.ref<i128>)
      ^next(%alias: !simulation.ref<i128>):
      simulation.ref.store %input to %alias : i128, !simulation.ref<i128>
      %copy = simulation.ref.alloc %old : i128 -> !simulation.ref<i128>
      simulation.return
    }
    simulation.code_unit.decl 22 in 0 function hierarchy "opaque_region"
    // CHECK: opaque_region: copy
    simulation.func @opaque_region(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %input: i128 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 22 : i64} {
      %local = simulation.ref.alloc %input : i128 -> !simulation.ref<i128>
      %old = simulation.ref.load %local {test.snapshot = "opaque_region"} : !simulation.ref<i128> -> i128
      "test.region"() ({
        simulation.ref.store %input to %local : i128, !simulation.ref<i128>
        "test.yield"() : () -> ()
      }) : () -> ()
      %copy = simulation.ref.alloc %old : i128 -> !simulation.ref<i128>
      simulation.return
    }
  }
}
