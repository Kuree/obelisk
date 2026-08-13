// RUN: obelisk-opt %s | obelisk-opt | FileCheck %s

module {
  obelisk_sim.design @roundtrip attributes {time_precision_fs = 1000 : i64} {
    obelisk_sim.scope.decl 0 hierarchy "top" debug "top"
    obelisk_sim.scope.decl 1 parent 0 hierarchy "top.child"
    obelisk_sim.code_unit.decl 10 in 0 root_initializer hierarchy "__obelisk_root" debug "root initializer"
    obelisk_sim.code_unit.decl 11 in 0 function hierarchy "top.callee" debug "callee"
    obelisk_sim.code_unit.decl 12 in 1 initial hierarchy "top.child.initial"
    obelisk_sim.code_unit.decl 13 in 1 initial hierarchy "top.child.process"
    obelisk_sim.code_unit.decl 14 in 1 observer hierarchy "top.child.observer"
    obelisk_sim.storage.decl 0 in 1 : !obelisk_sim.logic<8> design hierarchy "top.child.state"
    obelisk_sim.net.decl 0 in 1 : !obelisk_sim.logic<8> design hierarchy "top.child.wire"
    obelisk_sim.driver.decl 0 in 1 drives 0 : !obelisk_sim.logic<8> design

    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {code_unit_id = 10 : i64, entry_kind = 0 : i32} {
      %ref = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %net = obelisk_sim.context.net %ctx[0] : !obelisk_sim.net<!obelisk_sim.logic<8>>
      %driver = obelisk_sim.context.driver %ctx[0] : !obelisk_sim.driver<!obelisk_sim.logic<8>>
      %process = obelisk_sim.spawn @process(%ctx, %ref, %net, %driver) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<8>>, !obelisk_sim.net<!obelisk_sim.logic<8>>, !obelisk_sim.driver<!obelisk_sim.logic<8>> -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func @callee(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %value: !obelisk_sim.logic<8> {obelisk_sim.capture_kind = 1 : i32}) -> !obelisk_sim.logic<8> attributes {code_unit_id = 11 : i64, entry_kind = 8 : i32, obelisk_sim.bindings = [#obelisk_sim.argument_binding<path = "value", argument = 1, kind = direct, copyOut = false>, #obelisk_sim.constant_binding<path = "P", value = #obelisk_sim.frozen_constant<value = [-3 : i8, 0 : i8], isSigned = true> : !obelisk_sim.logic<8>>]} {
      obelisk_sim.return %value : !obelisk_sim.logic<8>
    }

    obelisk_sim.func @child(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {code_unit_id = 12 : i64, entry_kind = 1 : i32} {
      obelisk_sim.return
    }

    obelisk_sim.func private @observer(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %ref: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}) -> !obelisk_sim.logic<8> attributes {code_unit_id = 14 : i64, entry_kind = 14 : i32} {
      %value = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      obelisk_sim.return %value : !obelisk_sim.logic<8>
    }

    obelisk_sim.func @process(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %ref: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}, %net: !obelisk_sim.net<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 0 : i64}, %driver: !obelisk_sim.driver<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {code_unit_id = 13 : i64, entry_kind = 1 : i32} {
      %bits = arith.constant 5 : i8
      %index = arith.constant 1 : i32
      %logic_index = obelisk_sim.logic.constant 1 : i32, 0 : i32 : !obelisk_sim.logic<32>
      %logic = obelisk_sim.logic.from_bits %bits : i8 -> !obelisk_sim.logic<8>
      %planes = obelisk_sim.logic.constant 3 : i8, 4 : i8 : !obelisk_sim.logic<8>
      %truth = obelisk_sim.logic.is_true %planes : !obelisk_sim.logic<8>
      %resized = obelisk_sim.logic.resize %logic signed = false : !obelisk_sim.logic<8> -> !obelisk_sim.logic<16>
      %unary = obelisk_sim.logic.unary bit_not %logic : (!obelisk_sim.logic<8>) -> !obelisk_sim.logic<8>
      %not = obelisk_sim.logic.unary logical_not %logic : (!obelisk_sim.logic<8>) -> !obelisk_sim.logic<1>
      %reduced = obelisk_sim.logic.reduction xor %logic : !obelisk_sim.logic<8> -> !obelisk_sim.logic<1>
      %binary = obelisk_sim.logic.binary add %logic, %unary : !obelisk_sim.logic<8>
      %logical = obelisk_sim.logic.logical and %logic, %planes : (!obelisk_sim.logic<8>, !obelisk_sim.logic<8>) -> !obelisk_sim.logic<1>
      %shifted = obelisk_sim.logic.shift left %logic by %index : (!obelisk_sim.logic<8>, i32) -> !obelisk_sim.logic<8>
      %compare = obelisk_sim.logic.compare eq %logic, %planes : (!obelisk_sim.logic<8>, !obelisk_sim.logic<8>) -> !obelisk_sim.logic<1>
      %case_compare = obelisk_sim.logic.compare case_eq %logic, %planes : (!obelisk_sim.logic<8>, !obelisk_sim.logic<8>) -> i1
      %concat = obelisk_sim.logic.concat %logic, %planes : (!obelisk_sim.logic<8>, !obelisk_sim.logic<8>) -> !obelisk_sim.logic<16>
      %replicated = obelisk_sim.logic.replicate %logic times 2 : !obelisk_sim.logic<8> -> !obelisk_sim.logic<16>
      %part = obelisk_sim.logic.extract %logic from 2 : !obelisk_sim.logic<8> -> !obelisk_sim.logic<4>
      %dynamic_part = obelisk_sim.logic.dyn_extract %logic from %index : (!obelisk_sim.logic<8>, i32) -> !obelisk_sim.logic<4>
      %dynamic_logic_index = obelisk_sim.logic.dyn_extract %logic from %logic_index : (!obelisk_sim.logic<8>, !obelisk_sim.logic<32>) -> !obelisk_sim.logic<4>
      %dynamic_bits = obelisk_sim.bits.dyn_extract %bits from %logic_index : (i8, !obelisk_sim.logic<32>) -> i4
      %inserted = obelisk_sim.logic.insert %part into %logic at 2 : (!obelisk_sim.logic<8>, !obelisk_sim.logic<4>) -> !obelisk_sim.logic<8>
      %dynamic_inserted = obelisk_sim.logic.dyn_insert %part into %logic at %logic_index : (!obelisk_sim.logic<8>, !obelisk_sim.logic<4>, !obelisk_sim.logic<32>) -> !obelisk_sim.logic<8>
      %dynamic_bits_inserted = obelisk_sim.bits.dyn_insert %dynamic_bits into %bits at %index : (i8, i4, i32) -> i8
      %back_to_bits = obelisk_sim.logic.to_bits %inserted : !obelisk_sim.logic<8> -> i8
      %local = obelisk_sim.ref.alloc %logic : !obelisk_sim.logic<8> -> !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.ref.store %binary to %local : !obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %loaded = obelisk_sim.ref.load %local : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %ref_part = obelisk_sim.ref.extract %ref from 2 : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.ref<!obelisk_sim.logic<4>>
      %ref_dynamic = obelisk_sim.ref.dyn_extract %ref from %index : (!obelisk_sim.ref<!obelisk_sim.logic<8>>, i32) -> !obelisk_sim.ref<!obelisk_sim.logic<4>>
      %ref_dynamic_logic_index = obelisk_sim.ref.dyn_extract %ref from %logic_index : (!obelisk_sim.ref<!obelisk_sim.logic<8>>, !obelisk_sim.logic<32>) -> !obelisk_sim.ref<!obelisk_sim.logic<4>>
      %driver_part = obelisk_sim.driver.extract %driver from 2 : !obelisk_sim.driver<!obelisk_sim.logic<8>> -> !obelisk_sim.driver<!obelisk_sim.logic<4>>
      %driver_dynamic = obelisk_sim.driver.dyn_extract %driver from %index : (!obelisk_sim.driver<!obelisk_sim.logic<8>>, i32) -> !obelisk_sim.driver<!obelisk_sim.logic<4>>
      %driver_dynamic_logic_index = obelisk_sim.driver.dyn_extract %driver from %logic_index : (!obelisk_sim.driver<!obelisk_sim.logic<8>>, !obelisk_sim.logic<32>) -> !obelisk_sim.driver<!obelisk_sim.logic<4>>
      %net_value = obelisk_sim.net.read %net : !obelisk_sim.net<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      obelisk_sim.driver.drive %driver = %net_value : !obelisk_sim.driver<!obelisk_sim.logic<8>>, !obelisk_sim.logic<8>
      %time = obelisk_sim.time.constant 7
      %index64 = arith.extsi %index : i32 to i64
      %scaled_time = obelisk_sim.time.scale %index64 by 4 signed = true : i64
      %sum = obelisk_sim.time.add %time, %time
      obelisk_sim.nba.enqueue %loaded to %ref after %sum : (!obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>, !obelisk_sim.time) -> ()
      %event = obelisk_sim.context.event %ctx[0] : !obelisk_sim.event
      obelisk_sim.event.trigger %event nonblocking = false
      obelisk_sim.event.trigger %event after %time nonblocking = true
      %event_triggered = obelisk_sim.event.triggered %event
      %event_equal = obelisk_sim.event.equal %event, %event
      %observer = obelisk_sim.observer.bind @observer values(%ref, %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>>, !obelisk_sim.ref<!obelisk_sim.logic<8>>) captures 1 : !obelisk_sim.observer<!obelisk_sim.logic<8>>
      %spawned = obelisk_sim.spawn @child(%ctx) : !obelisk_sim.context -> !obelisk_sim.process
      %called = obelisk_sim.call @callee(%ctx, %loaded) : (!obelisk_sim.context, !obelisk_sim.logic<8>) -> !obelisk_sim.logic<8>
      obelisk_sim.suspend.delay %time to ^bb1(%called : !obelisk_sim.logic<8>)
    ^bb1(%live1: !obelisk_sim.logic<8>):
      obelisk_sim.suspend.change %ref to ^bb2(%live1 : !obelisk_sim.logic<8>) : !obelisk_sim.ref<!obelisk_sim.logic<8>>
    ^bb2(%live2: !obelisk_sim.logic<8>):
      obelisk_sim.suspend.edge posedge %net to ^bb_iff(%live2 : !obelisk_sim.logic<8>) : !obelisk_sim.net<!obelisk_sim.logic<8>>
    ^bb_iff(%live_iff: !obelisk_sim.logic<8>):
      obelisk_sim.suspend.edge_iff posedge %ref iff %net to ^bb_level(%live_iff : !obelisk_sim.logic<8>) : !obelisk_sim.ref<!obelisk_sim.logic<8>>, !obelisk_sim.net<!obelisk_sim.logic<8>>
    ^bb_level(%live_level: !obelisk_sim.logic<8>):
      obelisk_sim.suspend.level %ref to ^bb3(%live_level : !obelisk_sim.logic<8>) : !obelisk_sim.ref<!obelisk_sim.logic<8>>
    ^bb3(%live3: !obelisk_sim.logic<8>):
      obelisk_sim.suspend.any %ref, %net, %live3 edges [0, 1] to ^bb_any : !obelisk_sim.ref<!obelisk_sim.logic<8>>, !obelisk_sim.net<!obelisk_sim.logic<8>>, !obelisk_sim.logic<8>
    ^bb_any(%live_any: !obelisk_sim.logic<8>):
      obelisk_sim.suspend.observe %observer, %live_any, %live_any conditions 0 edges [0] indices [-1] to ^bb_observe : !obelisk_sim.observer<!obelisk_sim.logic<8>>, !obelisk_sim.logic<8>, !obelisk_sim.logic<8>
    ^bb_observe(%live_observe: !obelisk_sim.logic<8>):
      obelisk_sim.suspend.event %event to ^bb4(%live_observe : !obelisk_sim.logic<8>)
    ^bb4(%live4: !obelisk_sim.logic<8>):
      obelisk_sim.suspend.await %spawned to ^bb5(%live4 : !obelisk_sim.logic<8>)
    ^bb5(%live5: !obelisk_sim.logic<8>):
      %spawned2 = obelisk_sim.spawn @child(%ctx) : !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.suspend.join any %spawned, %spawned2, %live5 processes 2 to ^bb_forever : !obelisk_sim.process, !obelisk_sim.process, !obelisk_sim.logic<8>
    ^bb_forever(%live6: !obelisk_sim.logic<8>):
      obelisk_sim.suspend.forever to ^bb6(%live6 : !obelisk_sim.logic<8>)
    ^bb6(%live7: !obelisk_sim.logic<8>):
      obelisk_sim.return
    }
  }
}

// CHECK: obelisk_sim.design @roundtrip attributes {time_precision_fs = 1000 : i64}
// CHECK: obelisk_sim.code_unit.decl 10 in 0 root_initializer hierarchy "__obelisk_root"
// CHECK: obelisk_sim.code_unit.decl 11 in 0 function hierarchy "top.callee"
// CHECK: obelisk_sim.storage.decl 0 in 1 : !obelisk_sim.logic<8>
// CHECK: obelisk_sim.net.decl 0 in 1 : !obelisk_sim.logic<8>
// CHECK: obelisk_sim.driver.decl 0 in 1 drives 0
// CHECK: obelisk_sim.func @callee
// CHECK-SAME: obelisk_sim.bindings = [#obelisk_sim.argument_binding<path = "value", argument = 1, kind = direct, copyOut = false>, #obelisk_sim.constant_binding<path = "P", value = #obelisk_sim.frozen_constant<value = [-3 : i8, 0 : i8], isSigned = true> : !obelisk_sim.logic<8>>]
// CHECK: obelisk_sim.logic.is_true
// CHECK: obelisk_sim.logic.unary logical_not
// CHECK: obelisk_sim.logic.reduction xor
// CHECK: obelisk_sim.logic.binary add
// CHECK: obelisk_sim.logic.logical and
// CHECK: obelisk_sim.logic.shift left
// CHECK: obelisk_sim.bits.dyn_extract
// CHECK: obelisk_sim.logic.dyn_insert
// CHECK: obelisk_sim.bits.dyn_insert
// CHECK: obelisk_sim.ref.alloc
// CHECK: obelisk_sim.ref.dyn_extract {{.*}}!obelisk_sim.logic<32>
// CHECK: obelisk_sim.driver.extract
// CHECK: obelisk_sim.driver.dyn_extract
// CHECK: obelisk_sim.time.scale
// CHECK: obelisk_sim.nba.enqueue
// CHECK: obelisk_sim.event.trigger {{.*}} after
// CHECK: obelisk_sim.event.triggered
// CHECK: obelisk_sim.event.equal
// CHECK: obelisk_sim.observer.bind
// CHECK: obelisk_sim.suspend.delay
// CHECK: obelisk_sim.suspend.change
// CHECK: obelisk_sim.suspend.edge posedge
// CHECK: obelisk_sim.suspend.edge_iff posedge
// CHECK: obelisk_sim.suspend.level
// CHECK: obelisk_sim.suspend.any
// CHECK: obelisk_sim.suspend.observe
// CHECK: obelisk_sim.suspend.event
// CHECK: obelisk_sim.suspend.await
// CHECK: obelisk_sim.suspend.join any
// CHECK: obelisk_sim.suspend.forever
