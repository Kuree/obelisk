// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,test-obelisk-repeatable-activations))' -o /dev/null 2>&1 | FileCheck %s

// A continuous driver of bits that no other strong contribution reaches writes
// the resolved value of those bits (IEEE 1800-2023 6.6.1): re-driving them with
// unchanged inputs changes no bit and is no update event (4.4.1, 4.9.1), so the
// activation is repeatable like a variable copy. Drivers that partition a wire
// or own a sparse slice qualify; overlapping drivers and nets collapsed into a
// connection keep their resolution and stay unproved.
!bits = !simulation.logic<4>
!word = !simulation.logic<8>
!in = !simulation.ref<!bits>
!wide = !simulation.ref<!word>
!n8 = !simulation.net<!word>
!d8 = !simulation.driver<!word>
!d4 = !simulation.driver<!bits>
!c = !simulation.context
module {
  simulation.design @net_drivers {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !bits design
    simulation.storage.decl 1 in 0 : !bits design
    simulation.storage.decl 2 in 0 : !word design
    simulation.net.decl 0 in 0 : !word design
    simulation.net.decl 1 in 0 : !word design
    simulation.net.decl 2 in 0 : !word design
    simulation.net.decl 3 in 0 : !word design
    simulation.net.decl 4 in 0 : !word design
    simulation.net.connect.decl 0 in 0 3[0] to 4[0] width 8 reversed = false provenance "ordered" rhs_dominates = false
    simulation.driver.decl 0 in 0 drives 0 : !word design {driven_low = 0 : i64, driven_width = 4 : i64}
    simulation.driver.decl 1 in 0 drives 0 : !word design {driven_low = 4 : i64, driven_width = 4 : i64}
    simulation.driver.decl 2 in 0 drives 1 : !word design {driven_low = 2 : i64, driven_width = 4 : i64}
    simulation.driver.decl 3 in 0 drives 2 : !word design
    simulation.driver.decl 4 in 0 drives 2 : !word design {driven_low = 0 : i64, driven_width = 4 : i64}
    simulation.driver.decl 5 in 0 drives 3 : !word design {driven_low = 0 : i64, driven_width = 4 : i64}
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 continuous hierarchy "partition_low"
    simulation.code_unit.decl 3 in 0 continuous hierarchy "partition_high"
    simulation.code_unit.decl 4 in 0 continuous hierarchy "sparse_slice"
    simulation.code_unit.decl 5 in 0 continuous hierarchy "overlap_whole"
    simulation.code_unit.decl 6 in 0 continuous hierarchy "overlap_low"
    simulation.code_unit.decl 7 in 0 continuous hierarchy "connected"
    simulation.func @root(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %a = simulation.spawn @partition_low(%ctx) : !c -> !simulation.process
      %b = simulation.spawn @partition_high(%ctx) : !c -> !simulation.process
      %c = simulation.spawn @sparse_slice(%ctx) : !c -> !simulation.process
      %d = simulation.spawn @overlap_whole(%ctx) : !c -> !simulation.process
      %e = simulation.spawn @overlap_low(%ctx) : !c -> !simulation.process
      %f = simulation.spawn @connected(%ctx) : !c -> !simulation.process
      simulation.return
    }
    // CHECK: repeatable @partition_low = 1
    simulation.func @partition_low(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 2 : i64} {
      %input = simulation.context.storage %ctx[0] : !in
      %whole = simulation.context.driver %ctx[0] : !d8
      %driver = simulation.driver.extract %whole from 0 : !d8 -> !d4
      cf.br ^body
    ^body:
      %v = simulation.ref.load %input : !in -> !bits
      simulation.driver.drive %driver = %v : !d4, !bits
      simulation.suspend.change %input to ^body : !in
    }
    // CHECK: repeatable @partition_high = 1
    simulation.func @partition_high(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 3 : i64} {
      %input = simulation.context.storage %ctx[1] : !in
      %whole = simulation.context.driver %ctx[1] : !d8
      %driver = simulation.driver.extract %whole from 4 : !d8 -> !d4
      cf.br ^body
    ^body:
      %v = simulation.ref.load %input : !in -> !bits
      simulation.driver.drive %driver = %v : !d4, !bits
      simulation.suspend.change %input to ^body : !in
    }
    // CHECK: repeatable @sparse_slice = 1
    simulation.func @sparse_slice(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 4 : i64} {
      %input = simulation.context.storage %ctx[0] : !in
      %whole = simulation.context.driver %ctx[2] : !d8
      %driver = simulation.driver.extract %whole from 2 : !d8 -> !d4
      cf.br ^body
    ^body:
      %v = simulation.ref.load %input : !in -> !bits
      simulation.driver.drive %driver = %v : !d4, !bits
      simulation.suspend.change %input to ^body : !in
    }
    // Two strong drivers share bits 0-3: resolution, not a write.
    // CHECK: repeatable @overlap_whole = 0
    simulation.func @overlap_whole(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 5 : i64} {
      %input = simulation.context.storage %ctx[2] : !wide
      %driver = simulation.context.driver %ctx[3] : !d8
      cf.br ^body
    ^body:
      %v = simulation.ref.load %input : !wide -> !word
      simulation.driver.drive %driver = %v : !d8, !word
      simulation.suspend.change %input to ^body : !wide
    }
    // CHECK: repeatable @overlap_low = 0
    simulation.func @overlap_low(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 6 : i64} {
      %input = simulation.context.storage %ctx[1] : !in
      %whole = simulation.context.driver %ctx[4] : !d8
      %driver = simulation.driver.extract %whole from 0 : !d8 -> !d4
      cf.br ^body
    ^body:
      %v = simulation.ref.load %input : !in -> !bits
      simulation.driver.drive %driver = %v : !d4, !bits
      simulation.suspend.change %input to ^body : !in
    }
    // A collapsed connection resolves across both nets (6.6.1, 23.3.3).
    // CHECK: repeatable @connected = 0
    simulation.func @connected(%ctx: !c {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 7 : i64} {
      %input = simulation.context.storage %ctx[0] : !in
      %whole = simulation.context.driver %ctx[5] : !d8
      %driver = simulation.driver.extract %whole from 0 : !d8 -> !d4
      cf.br ^body
    ^body:
      %v = simulation.ref.load %input : !in -> !bits
      simulation.driver.drive %driver = %v : !d4, !bits
      simulation.suspend.change %input to ^body : !in
    }
  }
}
