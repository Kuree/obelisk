// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-eliminate-dead-captures{missed-remarks=true},simulation.func(canonicalize,cse)))' > %t.threaded 2> %t.remarks
// RUN: obelisk-opt %s --mlir-disable-threading --pass-pipeline='builtin.module(simulation.design(obelisk-sim-eliminate-dead-captures{missed-remarks=true},simulation.func(canonicalize,cse)))' > %t.serial 2> %t.serial-remarks
// RUN: diff -u %t.serial %t.threaded
// RUN: diff -u %t.serial-remarks %t.remarks
// RUN: FileCheck %s --check-prefix=CHECK < %t.threaded
// RUN: FileCheck %s --check-prefix=REMARK < %t.remarks
// RUN: obelisk-opt %s --mlir-print-debuginfo --pass-pipeline='builtin.module(simulation.design(obelisk-sim-eliminate-dead-captures))' | FileCheck %s --check-prefix=LOC

module {
  simulation.design @captures {
    simulation.scope.decl 0
    // CHECK: simulation.code_unit.decl 42 in 0 function hierarchy "top.target"
    simulation.code_unit.decl 42 in 0 function hierarchy "top.target"
    simulation.code_unit.decl 43 in 0 function hierarchy "top.cycle_a"
    simulation.code_unit.decl 44 in 0 function hierarchy "top.cycle_b"
    simulation.code_unit.decl 45 in 0 initial hierarchy "top.process"
    simulation.code_unit.decl 46 in 0 function hierarchy "top.copy_out"
    simulation.code_unit.decl 47 in 0 function hierarchy "top.public_entry"
    simulation.code_unit.decl 48 in 0 function hierarchy "top.nested_entry"
    simulation.code_unit.decl 49 in 0 function hierarchy "top.address_taken"
    simulation.code_unit.decl 50 in 0 function hierarchy "top.unknown_metadata"
    simulation.code_unit.decl 51 in 0 function hierarchy "top.copy_out_caller"
    simulation.code_unit.decl 52 in 0 function hierarchy "top.live_forwarder"
    simulation.code_unit.decl 53 in 0 function hierarchy "top.live_mid"
    simulation.code_unit.decl 54 in 0 function hierarchy "top.live_sink"
    simulation.code_unit.decl 55 in 0 task hierarchy "top.task_sink"
    simulation.code_unit.decl 56 in 0 initial hierarchy "top.task_caller"
    simulation.storage.decl 0 in 0 : !simulation.logic<8> design
    simulation.net.decl 0 in 0 : !simulation.logic<8> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<8> design

    // Every capture kind can be pruned. The surviving formal and its
    // descriptor provenance, result metadata, location, ID, and unrelated
    // attributes must survive. Bindings for removed arguments disappear,
    // while the surviving argument is renumbered and local bindings remain.
    // CHECK-LABEL: simulation.func private @target(
    // CHECK-SAME: %arg0: !simulation.context {simulation.capture_kind = 0 : i32},
    // CHECK-SAME: %arg1: i32 {simulation.capture_kind = 1 : i32, test.provenance = "keep"},
    // CHECK-SAME: %arg2: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64, simulation.descriptor_low = 0 : i64, simulation.descriptor_root_type = !simulation.logic<8>})
    // CHECK-SAME: -> (i32 {test.result = "keep"})
    // CHECK-SAME: code_unit_id = 42 : i64
    // CHECK-SAME: simulation.bindings = [#simulation.argument_binding<path = "live", argument = 1, kind = direct, copyOut = false>, #simulation.argument_binding<path = "live_storage", argument = 2, kind = direct, copyOut = false>, #simulation.local_binding<path = "local", type = i32, automatic = false, patternVariable = false, isReturn = false>]
    // CHECK-SAME: test.function = "keep"
    simulation.func private @target(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %formal: i32 {simulation.capture_kind = 1 : i32},
        %value: i32 {simulation.capture_kind = 2 : i32},
        %storage: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64, simulation.descriptor_root_type = !simulation.logic<8>, simulation.descriptor_low = 0 : i64},
        %net: !simulation.net<!simulation.logic<8>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 0 : i64},
        %driver: !simulation.driver<!simulation.logic<8>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 0 : i64},
        %live: i32 {simulation.capture_kind = 1 : i32, test.provenance = "keep"},
        %live_storage: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64, simulation.descriptor_root_type = !simulation.logic<8>, simulation.descriptor_low = 0 : i64})
        -> (i32 {test.result = "keep"})
        attributes {entry_kind = 8 : i32, code_unit_id = 42 : i64,
                    simulation.bindings = [
                      #simulation.argument_binding<path = "formal", argument = 1, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "value", argument = 2, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "storage", argument = 3, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "net", argument = 4, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "driver", argument = 5, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "live", argument = 6, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "live_storage", argument = 7, kind = direct, copyOut = false>,
                      #simulation.local_binding<path = "local", type = i32, automatic = false, patternVariable = false, isReturn = false>],
                    test.function = "keep"} {
      %stored = simulation.logic.constant 1 : i8, 0 : i8
          : !simulation.logic<8>
      simulation.ref.store %stored to %live_storage
          : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.return %live : i32
    } loc("target.sv":42:7)

    // Direct forwarding alone is not a semantic use, including across a
    // recursive cycle.
    // CHECK-LABEL: simulation.func private @cycle_a(
    // CHECK-SAME: %arg0: !simulation.context {simulation.capture_kind = 0 : i32})
    // CHECK: simulation.call @cycle_b(%arg0)
    simulation.func private @cycle_a(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %forwarded: i32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 43 : i64} {
      %result = simulation.call @cycle_b(%ctx, %forwarded)
          : (!simulation.context, i32) -> i32
      simulation.return %result : i32
    }

    // CHECK-LABEL: simulation.func private @cycle_b(
    // CHECK-SAME: %arg0: !simulation.context {simulation.capture_kind = 0 : i32})
    // CHECK: simulation.call @cycle_a(%arg0)
    simulation.func private @cycle_b(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %forwarded: i32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 44 : i64} {
      %result = simulation.call @cycle_a(%ctx, %forwarded)
          : (!simulation.context, i32) -> i32
      simulation.return %result : i32
    }

    // A void process can lose all captures except context, and its spawn-site
    // argument dictionaries are filtered in lockstep.
    // CHECK-LABEL: simulation.func private @process(
    // CHECK-SAME: !simulation.context
    // CHECK-NOT: !simulation.ref
    simulation.func private @process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %storage: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 45 : i64} {
      simulation.return
    }

    // Dead output-formal inputs do not imply dead copy-out results.
    // CHECK-LABEL: simulation.func private @copy_out(
    // CHECK-SAME: !simulation.context {simulation.capture_kind = 0 : i32})
    // CHECK-SAME: -> (i32 {test.copy_out = true})
    simulation.func private @copy_out(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %output_initial: i32 {simulation.capture_kind = 1 : i32})
        -> (i32 {test.copy_out = true})
        attributes {entry_kind = 8 : i32, code_unit_id = 46 : i64} {
      %constant = arith.constant 7 : i32
      simulation.return %constant : i32
    }

    // Public, nested, external, address-taken, and unknown-metadata ABIs are
    // all conservatively pinned.
    // CHECK-LABEL: simulation.func @public_entry(
    // CHECK-SAME: i32 {simulation.capture_kind = 1 : i32})
    simulation.func @public_entry(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %unused: i32 {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 47 : i64} {
      simulation.return
    }

    // CHECK-LABEL: simulation.func nested @nested_entry(
    // CHECK-SAME: i32 {simulation.capture_kind = 1 : i32})
    simulation.func nested @nested_entry(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %unused: i32 {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 48 : i64} {
      simulation.return
    }

    // CHECK-LABEL: simulation.func private @external_entry(
    // CHECK-SAME: i32 {simulation.capture_kind = 1 : i32})
    simulation.func private @external_entry(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %unused: i32 {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32}

    // CHECK-LABEL: simulation.func private @address_taken(
    // CHECK-SAME: i32 {simulation.capture_kind = 1 : i32})
    simulation.func private @address_taken(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %unused: i32 {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 49 : i64} {
      simulation.return
    }

    // CHECK-LABEL: simulation.func private @unknown_metadata(
    // CHECK-SAME: i32 {simulation.capture_kind = 1 : i32})
    simulation.func private @unknown_metadata(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %unused: i32 {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 50 : i64,
                    simulation.future_boundary = true} {
      simulation.return
    }

    // Multiple callers agree on the same pruned callee ABI. This public
    // caller retains its own formal even though the forwarded operand dies.
    // CHECK-LABEL: simulation.func @copy_out_caller(
    // CHECK-SAME: %arg1: i32 {simulation.capture_kind = 1 : i32})
    // CHECK: simulation.call @copy_out(%arg0) : (!simulation.context) -> i32
    simulation.func @copy_out_caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %initial: i32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 51 : i64} {
      %result = simulation.call @copy_out(%ctx, %initial)
          : (!simulation.context, i32) -> i32
      simulation.return %result : i32
    }

    // A live lower-index argument must not stop fixed-point scanning of later
    // forwarding arguments. Ordering this two-hop chain before its sink also
    // requires a second deterministic propagation wave.
    // CHECK-LABEL: simulation.func private @live_forwarder(
    // CHECK-SAME: %arg0: !simulation.context
    // CHECK-SAME: %arg1: i32
    // CHECK-SAME: %arg2: i32
    // CHECK: simulation.call @live_mid(%arg0, %arg2)
    simulation.func private @live_forwarder(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %locally_live: i32 {simulation.capture_kind = 1 : i32},
        %forwarded: i32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 52 : i64} {
      %nested = simulation.call @live_mid(%ctx, %forwarded)
          : (!simulation.context, i32) -> i32
      %sum = arith.addi %locally_live, %nested : i32
      simulation.return %sum : i32
    }

    // CHECK-LABEL: simulation.func private @live_mid(
    // CHECK-SAME: %arg0: !simulation.context
    // CHECK-SAME: %arg1: i32
    // CHECK: simulation.call @live_sink(%arg0, %arg1)
    simulation.func private @live_mid(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %forwarded: i32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 53 : i64} {
      %nested = simulation.call @live_sink(%ctx, %forwarded)
          : (!simulation.context, i32) -> i32
      simulation.return %nested : i32
    }

    // CHECK-LABEL: simulation.func private @live_sink(
    // CHECK-SAME: %arg0: !simulation.context
    // CHECK-SAME: %arg1: i32
    simulation.func private @live_sink(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %consumed: i32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 54 : i64} {
      simulation.return %consumed : i32
    }

    // A dead task argument is removed without consuming or renumbering the
    // continuation operand that follows the argument prefix.
    // CHECK-LABEL: simulation.func private @task_sink(
    // CHECK-SAME: %arg0: !simulation.context
    // CHECK-SAME: %arg1: !simulation.ref<i32>
    // CHECK-SAME: %arg2: i32
    simulation.func private @task_sink(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %unused: i32 {simulation.capture_kind = 1 : i32},
        %storage: !simulation.ref<i32> {simulation.capture_kind = 1 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 12 : i32, code_unit_id = 55 : i64} {
      simulation.ref.store %value to %storage : i32, !simulation.ref<i32>
      simulation.return
    }

    // CHECK-LABEL: simulation.func @task_caller(
    // CHECK: simulation.task.call @task_sink(%arg0, %arg2, %arg3, %arg3) arguments 3 to ^bb1 : !simulation.context, !simulation.ref<i32>, i32, i32
    simulation.func @task_caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %forwarded_dead: i32 {simulation.capture_kind = 1 : i32},
        %storage: !simulation.ref<i32> {simulation.capture_kind = 1 : i32},
        %live: i32 {simulation.capture_kind = 1 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 56 : i64} {
      simulation.task.call @task_sink(
          %ctx, %forwarded_dead, %storage, %live, %live)
          arguments 4 to ^done : !simulation.context, i32,
          !simulation.ref<i32>, i32, i32
    ^done(%resumed: i32):
      simulation.ref.store %resumed to %storage : i32, !simulation.ref<i32>
      simulation.return
    }

    // CHECK-LABEL: simulation.func @root(
    // CHECK-NOT: simulation.context.net
    // CHECK-NOT: simulation.context.driver
    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %zero = "arith.constant"() {test.address = @address_taken,
                                   value = 0 : i32} : () -> i32
      %storage = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<8>>
      %net = simulation.context.net %ctx[0] : !simulation.net<!simulation.logic<8>>
      %driver = simulation.context.driver %ctx[0] : !simulation.driver<!simulation.logic<8>>
      // CHECK: simulation.call @target(%arg0, %c0_i32, %{{.*}}) {arg_attrs = [{test.actual = "ctx"}, {test.actual = "live"}, {test.actual = "live_storage"}], res_attrs = [{test.call_result = "keep"}], test.call = "keep"}
      %result = simulation.call @target(%ctx, %zero, %zero, %storage, %net, %driver, %zero, %storage)
          {arg_attrs = [{test.actual = "ctx"}, {test.actual = "formal"},
                        {test.actual = "value"}, {test.actual = "storage"},
                        {test.actual = "net"}, {test.actual = "driver"},
                        {test.actual = "live"},
                        {test.actual = "live_storage"}],
           res_attrs = [{test.call_result = "keep"}], test.call = "keep"}
          : (!simulation.context, i32, i32,
             !simulation.ref<!simulation.logic<8>>,
             !simulation.net<!simulation.logic<8>>,
             !simulation.driver<!simulation.logic<8>>, i32,
             !simulation.ref<!simulation.logic<8>>) -> i32 loc("caller.sv":9:3)
      // CHECK: simulation.call @copy_out(%arg0) : (!simulation.context) -> i32
      %copy = simulation.call @copy_out(%ctx, %zero)
          : (!simulation.context, i32) -> i32
      // CHECK: simulation.spawn @process(%arg0) {arg_attrs = [{test.spawn = "ctx"}]}
      %child = simulation.spawn @process(%ctx, %storage)
          {arg_attrs = [{test.spawn = "ctx"}, {test.spawn = "dead"}]}
          : !simulation.context, !simulation.ref<!simulation.logic<8>> -> !simulation.process
      // CHECK: simulation.call @live_forwarder(%arg0, %c0_i32, %c0_i32)
      %forwarded = simulation.call @live_forwarder(%ctx, %zero, %zero)
          : (!simulation.context, i32, i32) -> i32
      simulation.return
    }
  }
}

// REMARK: dead capture elimination retained ABI: non-private ABI
// REMARK-COUNT-1: dead capture elimination retained ABI: nested visibility ABI
// REMARK-COUNT-1: dead capture elimination retained ABI: external declaration ABI
// REMARK-COUNT-1: dead capture elimination retained ABI: non-direct or address-taken symbol use
// REMARK-COUNT-1: dead capture elimination retained ABI: unknown simulation operation metadata
// REMARK: dead capture elimination retained ABI: non-private ABI
// REMARK-COUNT-1: dead capture elimination retained ABI: root initializer ABI

// LOC: simulation.func private @target
// LOC: } loc(#[[TARGET_LOC:loc[0-9]+]])
// LOC: simulation.call @target
// LOC-SAME: loc(#[[CALL_LOC:loc[0-9]+]])
// LOC: #[[TARGET_LOC]] = loc("target.sv":42:7)
// LOC: #[[CALL_LOC]] = loc("caller.sv":9:3)
