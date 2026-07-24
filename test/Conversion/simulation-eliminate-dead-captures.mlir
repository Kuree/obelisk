// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-eliminate-dead-captures{missed-remarks=true},obelisk_sim.func(canonicalize,cse)))' > %t.threaded 2> %t.remarks
// RUN: obelisk-opt %s --mlir-disable-threading --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-eliminate-dead-captures{missed-remarks=true},obelisk_sim.func(canonicalize,cse)))' > %t.serial 2> %t.serial-remarks
// RUN: diff -u %t.serial %t.threaded
// RUN: diff -u %t.serial-remarks %t.remarks
// RUN: FileCheck %s --check-prefix=CHECK < %t.threaded
// RUN: FileCheck %s --check-prefix=REMARK < %t.remarks
// RUN: obelisk-opt %s --mlir-print-debuginfo --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-eliminate-dead-captures))' | FileCheck %s --check-prefix=LOC

module {
  obelisk_sim.design @captures {
    obelisk_sim.scope.decl 0
    // CHECK: obelisk_sim.code_unit.decl 42 in 0 function hierarchy "top.target"
    obelisk_sim.code_unit.decl 42 in 0 function hierarchy "top.target"
    obelisk_sim.code_unit.decl 43 in 0 function hierarchy "top.cycle_a"
    obelisk_sim.code_unit.decl 44 in 0 function hierarchy "top.cycle_b"
    obelisk_sim.code_unit.decl 45 in 0 initial hierarchy "top.process"
    obelisk_sim.code_unit.decl 46 in 0 function hierarchy "top.copy_out"
    obelisk_sim.code_unit.decl 47 in 0 function hierarchy "top.public_entry"
    obelisk_sim.code_unit.decl 48 in 0 function hierarchy "top.nested_entry"
    obelisk_sim.code_unit.decl 49 in 0 function hierarchy "top.address_taken"
    obelisk_sim.code_unit.decl 50 in 0 function hierarchy "top.unknown_metadata"
    obelisk_sim.code_unit.decl 51 in 0 function hierarchy "top.copy_out_caller"
    obelisk_sim.code_unit.decl 52 in 0 function hierarchy "top.live_forwarder"
    obelisk_sim.code_unit.decl 53 in 0 function hierarchy "top.live_mid"
    obelisk_sim.code_unit.decl 54 in 0 function hierarchy "top.live_sink"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<8> design
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<8> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<8> design

    // Every capture kind can be pruned. The surviving formal and its
    // descriptor provenance, result metadata, location, ID, and unrelated
    // attributes must survive. Bindings for removed arguments disappear,
    // while the surviving argument is renumbered and local bindings remain.
    // CHECK-LABEL: obelisk_sim.func private @target(
    // CHECK-SAME: %arg0: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
    // CHECK-SAME: %arg1: i32 {obelisk_sim.capture_kind = 1 : i32, test.provenance = "keep"},
    // CHECK-SAME: %arg2: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64, obelisk_sim.descriptor_low = 0 : i64, obelisk_sim.descriptor_root_type = !obelisk_sim.logic<8>})
    // CHECK-SAME: -> (i32 {test.result = "keep"})
    // CHECK-SAME: code_unit_id = 42 : i64
    // CHECK-SAME: obelisk_sim.bindings = [#obelisk_sim.argument_binding<path = "live", argument = 1, kind = direct, copyOut = false>, #obelisk_sim.argument_binding<path = "live_storage", argument = 2, kind = direct, copyOut = false>, #obelisk_sim.local_binding<path = "local", type = i32, automatic = false, patternVariable = false, isReturn = false>]
    // CHECK-SAME: test.function = "keep"
    obelisk_sim.func private @target(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %formal: i32 {obelisk_sim.capture_kind = 1 : i32},
        %value: i32 {obelisk_sim.capture_kind = 2 : i32},
        %storage: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64, obelisk_sim.descriptor_root_type = !obelisk_sim.logic<8>, obelisk_sim.descriptor_low = 0 : i64},
        %net: !obelisk_sim.net<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %driver: !obelisk_sim.driver<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %live: i32 {obelisk_sim.capture_kind = 1 : i32, test.provenance = "keep"},
        %live_storage: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64, obelisk_sim.descriptor_root_type = !obelisk_sim.logic<8>, obelisk_sim.descriptor_low = 0 : i64})
        -> (i32 {test.result = "keep"})
        attributes {entry_kind = 8 : i32, code_unit_id = 42 : i64,
                    obelisk_sim.bindings = [
                      #obelisk_sim.argument_binding<path = "formal", argument = 1, kind = direct, copyOut = false>,
                      #obelisk_sim.argument_binding<path = "value", argument = 2, kind = direct, copyOut = false>,
                      #obelisk_sim.argument_binding<path = "storage", argument = 3, kind = direct, copyOut = false>,
                      #obelisk_sim.argument_binding<path = "net", argument = 4, kind = direct, copyOut = false>,
                      #obelisk_sim.argument_binding<path = "driver", argument = 5, kind = direct, copyOut = false>,
                      #obelisk_sim.argument_binding<path = "live", argument = 6, kind = direct, copyOut = false>,
                      #obelisk_sim.argument_binding<path = "live_storage", argument = 7, kind = direct, copyOut = false>,
                      #obelisk_sim.local_binding<path = "local", type = i32, automatic = false, patternVariable = false, isReturn = false>],
                    test.function = "keep"} {
      %stored = obelisk_sim.logic.constant 1 : i8, 0 : i8
          : !obelisk_sim.logic<8>
      obelisk_sim.ref.store %stored to %live_storage
          : !obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.return %live : i32
    } loc("target.sv":42:7)

    // Direct forwarding alone is not a semantic use, including across a
    // recursive cycle.
    // CHECK-LABEL: obelisk_sim.func private @cycle_a(
    // CHECK-SAME: %arg0: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
    // CHECK: obelisk_sim.call @cycle_b(%arg0)
    obelisk_sim.func private @cycle_a(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %forwarded: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 43 : i64} {
      %result = obelisk_sim.call @cycle_b(%ctx, %forwarded)
          : (!obelisk_sim.context, i32) -> i32
      obelisk_sim.return %result : i32
    }

    // CHECK-LABEL: obelisk_sim.func private @cycle_b(
    // CHECK-SAME: %arg0: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
    // CHECK: obelisk_sim.call @cycle_a(%arg0)
    obelisk_sim.func private @cycle_b(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %forwarded: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 44 : i64} {
      %result = obelisk_sim.call @cycle_a(%ctx, %forwarded)
          : (!obelisk_sim.context, i32) -> i32
      obelisk_sim.return %result : i32
    }

    // A void process can lose all captures except context, and its spawn-site
    // argument dictionaries are filtered in lockstep.
    // CHECK-LABEL: obelisk_sim.func private @process(
    // CHECK-SAME: !obelisk_sim.context
    // CHECK-NOT: !obelisk_sim.ref
    obelisk_sim.func private @process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %storage: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 45 : i64} {
      obelisk_sim.return
    }

    // Dead output-formal inputs do not imply dead copy-out results.
    // CHECK-LABEL: obelisk_sim.func private @copy_out(
    // CHECK-SAME: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
    // CHECK-SAME: -> (i32 {test.copy_out = true})
    obelisk_sim.func private @copy_out(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %output_initial: i32 {obelisk_sim.capture_kind = 1 : i32})
        -> (i32 {test.copy_out = true})
        attributes {entry_kind = 8 : i32, code_unit_id = 46 : i64} {
      %constant = arith.constant 7 : i32
      obelisk_sim.return %constant : i32
    }

    // Public, nested, external, address-taken, and unknown-metadata ABIs are
    // all conservatively pinned.
    // CHECK-LABEL: obelisk_sim.func @public_entry(
    // CHECK-SAME: i32 {obelisk_sim.capture_kind = 1 : i32})
    obelisk_sim.func @public_entry(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %unused: i32 {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 47 : i64} {
      obelisk_sim.return
    }

    // CHECK-LABEL: obelisk_sim.func nested @nested_entry(
    // CHECK-SAME: i32 {obelisk_sim.capture_kind = 1 : i32})
    obelisk_sim.func nested @nested_entry(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %unused: i32 {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 48 : i64} {
      obelisk_sim.return
    }

    // CHECK-LABEL: obelisk_sim.func private @external_entry(
    // CHECK-SAME: i32 {obelisk_sim.capture_kind = 1 : i32})
    obelisk_sim.func private @external_entry(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %unused: i32 {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32}

    // CHECK-LABEL: obelisk_sim.func private @address_taken(
    // CHECK-SAME: i32 {obelisk_sim.capture_kind = 1 : i32})
    obelisk_sim.func private @address_taken(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %unused: i32 {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 49 : i64} {
      obelisk_sim.return
    }

    // CHECK-LABEL: obelisk_sim.func private @unknown_metadata(
    // CHECK-SAME: i32 {obelisk_sim.capture_kind = 1 : i32})
    obelisk_sim.func private @unknown_metadata(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %unused: i32 {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 50 : i64,
                    obelisk_sim.future_boundary = true} {
      obelisk_sim.return
    }

    // Multiple callers agree on the same pruned callee ABI. This public
    // caller retains its own formal even though the forwarded operand dies.
    // CHECK-LABEL: obelisk_sim.func @copy_out_caller(
    // CHECK-SAME: %arg1: i32 {obelisk_sim.capture_kind = 1 : i32})
    // CHECK: obelisk_sim.call @copy_out(%arg0) : (!obelisk_sim.context) -> i32
    obelisk_sim.func @copy_out_caller(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %initial: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 51 : i64} {
      %result = obelisk_sim.call @copy_out(%ctx, %initial)
          : (!obelisk_sim.context, i32) -> i32
      obelisk_sim.return %result : i32
    }

    // A live lower-index argument must not stop fixed-point scanning of later
    // forwarding arguments. Ordering this two-hop chain before its sink also
    // requires a second deterministic propagation wave.
    // CHECK-LABEL: obelisk_sim.func private @live_forwarder(
    // CHECK-SAME: %arg0: !obelisk_sim.context
    // CHECK-SAME: %arg1: i32
    // CHECK-SAME: %arg2: i32
    // CHECK: obelisk_sim.call @live_mid(%arg0, %arg2)
    obelisk_sim.func private @live_forwarder(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %locally_live: i32 {obelisk_sim.capture_kind = 1 : i32},
        %forwarded: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 52 : i64} {
      %nested = obelisk_sim.call @live_mid(%ctx, %forwarded)
          : (!obelisk_sim.context, i32) -> i32
      %sum = arith.addi %locally_live, %nested : i32
      obelisk_sim.return %sum : i32
    }

    // CHECK-LABEL: obelisk_sim.func private @live_mid(
    // CHECK-SAME: %arg0: !obelisk_sim.context
    // CHECK-SAME: %arg1: i32
    // CHECK: obelisk_sim.call @live_sink(%arg0, %arg1)
    obelisk_sim.func private @live_mid(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %forwarded: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 53 : i64} {
      %nested = obelisk_sim.call @live_sink(%ctx, %forwarded)
          : (!obelisk_sim.context, i32) -> i32
      obelisk_sim.return %nested : i32
    }

    // CHECK-LABEL: obelisk_sim.func private @live_sink(
    // CHECK-SAME: %arg0: !obelisk_sim.context
    // CHECK-SAME: %arg1: i32
    obelisk_sim.func private @live_sink(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %consumed: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 54 : i64} {
      obelisk_sim.return %consumed : i32
    }

    // CHECK-LABEL: obelisk_sim.func @root(
    // CHECK-NOT: obelisk_sim.context.net
    // CHECK-NOT: obelisk_sim.context.driver
    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %zero = "arith.constant"() {test.address = @address_taken,
                                   value = 0 : i32} : () -> i32
      %storage = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %net = obelisk_sim.context.net %ctx[0] : !obelisk_sim.net<!obelisk_sim.logic<8>>
      %driver = obelisk_sim.context.driver %ctx[0] : !obelisk_sim.driver<!obelisk_sim.logic<8>>
      // CHECK: obelisk_sim.call @target(%arg0, %c0_i32, %{{.*}}) {arg_attrs = [{test.actual = "ctx"}, {test.actual = "live"}, {test.actual = "live_storage"}], res_attrs = [{test.call_result = "keep"}], test.call = "keep"}
      %result = obelisk_sim.call @target(%ctx, %zero, %zero, %storage, %net, %driver, %zero, %storage)
          {arg_attrs = [{test.actual = "ctx"}, {test.actual = "formal"},
                        {test.actual = "value"}, {test.actual = "storage"},
                        {test.actual = "net"}, {test.actual = "driver"},
                        {test.actual = "live"},
                        {test.actual = "live_storage"}],
           res_attrs = [{test.call_result = "keep"}], test.call = "keep"}
          : (!obelisk_sim.context, i32, i32,
             !obelisk_sim.ref<!obelisk_sim.logic<8>>,
             !obelisk_sim.net<!obelisk_sim.logic<8>>,
             !obelisk_sim.driver<!obelisk_sim.logic<8>>, i32,
             !obelisk_sim.ref<!obelisk_sim.logic<8>>) -> i32 loc("caller.sv":9:3)
      // CHECK: obelisk_sim.call @copy_out(%arg0) : (!obelisk_sim.context) -> i32
      %copy = obelisk_sim.call @copy_out(%ctx, %zero)
          : (!obelisk_sim.context, i32) -> i32
      // CHECK: obelisk_sim.spawn @process(%arg0) {arg_attrs = [{test.spawn = "ctx"}]}
      %child = obelisk_sim.spawn @process(%ctx, %storage)
          {arg_attrs = [{test.spawn = "ctx"}, {test.spawn = "dead"}]}
          : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.process
      // CHECK: obelisk_sim.call @live_forwarder(%arg0, %c0_i32, %c0_i32)
      %forwarded = obelisk_sim.call @live_forwarder(%ctx, %zero, %zero)
          : (!obelisk_sim.context, i32, i32) -> i32
      obelisk_sim.return
    }
  }
}

// REMARK: dead capture elimination retained ABI: non-private ABI
// REMARK-COUNT-1: dead capture elimination retained ABI: nested visibility ABI
// REMARK-COUNT-1: dead capture elimination retained ABI: external declaration ABI
// REMARK-COUNT-1: dead capture elimination retained ABI: non-direct or address-taken symbol use
// REMARK-COUNT-1: dead capture elimination retained ABI: unknown obelisk_sim operation metadata
// REMARK: dead capture elimination retained ABI: non-private ABI
// REMARK-COUNT-1: dead capture elimination retained ABI: root initializer ABI

// LOC: obelisk_sim.func private @target
// LOC: } loc(#[[TARGET_LOC:loc[0-9]+]])
// LOC: obelisk_sim.call @target
// LOC-SAME: loc(#[[CALL_LOC:loc[0-9]+]])
// LOC: #[[TARGET_LOC]] = loc("target.sv":42:7)
// LOC: #[[CALL_LOC]] = loc("caller.sv":9:3)
