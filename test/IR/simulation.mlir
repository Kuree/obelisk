// RUN: obelisk-opt %s | obelisk-opt | FileCheck %s

module {
  simulation.design @roundtrip attributes {time_precision_fs = 1000 : i64} {
    simulation.scope.decl 0 hierarchy "top" debug "top"
    simulation.scope.decl 1 parent 0 hierarchy "top.child"
    simulation.code_unit.decl 10 in 0 root_initializer hierarchy "__obelisk_root" debug "root initializer"
    simulation.code_unit.decl 11 in 0 function hierarchy "top.callee" debug "callee"
    simulation.code_unit.decl 12 in 1 initial hierarchy "top.child.initial"
    simulation.code_unit.decl 13 in 1 initial hierarchy "top.child.process"
    simulation.code_unit.decl 14 in 1 observer hierarchy "top.child.observer"
    simulation.storage.decl 0 in 1 : !simulation.logic<8> design hierarchy "top.child.state"
    simulation.net.decl 0 in 1 : !simulation.logic<8> design hierarchy "top.child.wire"
    simulation.net.decl 1 in 1 : !simulation.logic<1> design hierarchy "top.child.cap" {
      charge_strength = 4 : i32,
      resolution_kind = 9 : i32
    }
    simulation.driver.decl 0 in 1 drives 0 : !simulation.logic<8> design
        {strength0 = 5 : i32, strength1 = 3 : i32}

    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 10 : i64, entry_kind = 0 : i32} {
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<8>>
      %net = simulation.context.net %ctx[0] : !simulation.net<!simulation.logic<8>>
      %driver = simulation.context.driver %ctx[0] : !simulation.driver<!simulation.logic<8>>
      %process = simulation.spawn @process(%ctx, %ref, %net, %driver) : !simulation.context, !simulation.ref<!simulation.logic<8>>, !simulation.net<!simulation.logic<8>>, !simulation.driver<!simulation.logic<8>> -> !simulation.process
      simulation.return
    }

    simulation.func @callee(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %value: !simulation.logic<8> {simulation.capture_kind = 1 : i32}) -> !simulation.logic<8> attributes {code_unit_id = 11 : i64, entry_kind = 8 : i32, simulation.bindings = [#simulation.argument_binding<path = "value", argument = 1, kind = direct, copyOut = false>, #simulation.constant_binding<path = "P", value = #simulation.frozen_constant<value = [-3 : i8, 0 : i8], isSigned = true> : !simulation.logic<8>>]} {
      simulation.return %value : !simulation.logic<8>
    }

    simulation.func @child(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {code_unit_id = 12 : i64, entry_kind = 1 : i32} {
      simulation.return
    }

    simulation.func private @observer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %ref: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}) -> !simulation.logic<8> attributes {code_unit_id = 14 : i64, entry_kind = 14 : i32} {
      %value = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.return %value : !simulation.logic<8>
    }

    simulation.func @process(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %ref: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %net: !simulation.net<!simulation.logic<8>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 0 : i64}, %driver: !simulation.driver<!simulation.logic<8>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 0 : i64}) attributes {code_unit_id = 13 : i64, entry_kind = 1 : i32} {
      %bits = arith.constant 5 : i8
      %index = arith.constant 1 : i32
      %logic_index = simulation.logic.constant 1 : i32, 0 : i32 : !simulation.logic<32>
      %logic = simulation.logic.from_bits %bits : i8 -> !simulation.logic<8>
      %planes = simulation.logic.constant 3 : i8, 4 : i8 : !simulation.logic<8>
      %truth = simulation.logic.is_true %planes : !simulation.logic<8>
      %resized = simulation.logic.resize %logic signed = false : !simulation.logic<8> -> !simulation.logic<16>
      %unary = simulation.logic.unary bit_not %logic : (!simulation.logic<8>) -> !simulation.logic<8>
      %not = simulation.logic.unary logical_not %logic : (!simulation.logic<8>) -> !simulation.logic<1>
      %reduced = simulation.logic.reduction xor %logic : !simulation.logic<8> -> !simulation.logic<1>
      %binary = simulation.logic.binary add %logic, %unary : !simulation.logic<8>
      %logical = simulation.logic.logical and %logic, %planes : (!simulation.logic<8>, !simulation.logic<8>) -> !simulation.logic<1>
      %shifted = simulation.logic.shift left %logic by %index : (!simulation.logic<8>, i32) -> !simulation.logic<8>
      %compare = simulation.logic.compare eq %logic, %planes : (!simulation.logic<8>, !simulation.logic<8>) -> !simulation.logic<1>
      %case_compare = simulation.logic.compare case_eq %logic, %planes : (!simulation.logic<8>, !simulation.logic<8>) -> i1
      %concat = simulation.logic.concat %logic, %planes : (!simulation.logic<8>, !simulation.logic<8>) -> !simulation.logic<16>
      %replicated = simulation.logic.replicate %logic times 2 : !simulation.logic<8> -> !simulation.logic<16>
      %part = simulation.logic.extract %logic from 2 : !simulation.logic<8> -> !simulation.logic<4>
      %dynamic_part = simulation.logic.dyn_extract %logic from %index : (!simulation.logic<8>, i32) -> !simulation.logic<4>
      %dynamic_logic_index = simulation.logic.dyn_extract %logic from %logic_index : (!simulation.logic<8>, !simulation.logic<32>) -> !simulation.logic<4>
      %dynamic_bits = simulation.bits.dyn_extract %bits from %logic_index : (i8, !simulation.logic<32>) -> i4
      %inserted = simulation.logic.insert %part into %logic at 2 : (!simulation.logic<8>, !simulation.logic<4>) -> !simulation.logic<8>
      %dynamic_inserted = simulation.logic.dyn_insert %part into %logic at %logic_index : (!simulation.logic<8>, !simulation.logic<4>, !simulation.logic<32>) -> !simulation.logic<8>
      %dynamic_bits_inserted = simulation.bits.dyn_insert %dynamic_bits into %bits at %index : (i8, i4, i32) -> i8
      %back_to_bits = simulation.logic.to_bits %inserted : !simulation.logic<8> -> i8
      %local = simulation.ref.alloc %logic : !simulation.logic<8> -> !simulation.ref<!simulation.logic<8>>
      simulation.ref.store %binary to %local : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      %loaded = simulation.ref.load %local : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %ref_part = simulation.ref.extract %ref from 2 : !simulation.ref<!simulation.logic<8>> -> !simulation.ref<!simulation.logic<4>>
      %ref_dynamic = simulation.ref.dyn_extract %ref from %index : (!simulation.ref<!simulation.logic<8>>, i32) -> !simulation.ref<!simulation.logic<4>>
      %ref_dynamic_logic_index = simulation.ref.dyn_extract %ref from %logic_index : (!simulation.ref<!simulation.logic<8>>, !simulation.logic<32>) -> !simulation.ref<!simulation.logic<4>>
      %driver_part = simulation.driver.extract %driver from 2 : !simulation.driver<!simulation.logic<8>> -> !simulation.driver<!simulation.logic<4>>
      %driver_dynamic = simulation.driver.dyn_extract %driver from %index : (!simulation.driver<!simulation.logic<8>>, i32) -> !simulation.driver<!simulation.logic<4>>
      %driver_dynamic_logic_index = simulation.driver.dyn_extract %driver from %logic_index : (!simulation.driver<!simulation.logic<8>>, !simulation.logic<32>) -> !simulation.driver<!simulation.logic<4>>
      %net_value = simulation.net.read %net : !simulation.net<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.driver.drive %driver = %net_value : !simulation.driver<!simulation.logic<8>>, !simulation.logic<8>
      %time = simulation.time.constant 7
      %index64 = arith.extsi %index : i32 to i64
      %scaled_time = simulation.time.scale %index64 by 4 signed = true : i64
      %sum = simulation.time.add %time, %time
      simulation.nba.enqueue %loaded to %ref after %sum : (!simulation.logic<8>, !simulation.ref<!simulation.logic<8>>, !simulation.time) -> ()
      %event = simulation.context.event %ctx[0] : !simulation.event
      simulation.event.trigger %event nonblocking = false
      simulation.event.trigger %event after %time nonblocking = true
      %event_triggered = simulation.event.triggered %event
      %event_equal = simulation.event.equal %event, %event
      %observer = simulation.observer.bind @observer values(%ref, %ref : !simulation.ref<!simulation.logic<8>>, !simulation.ref<!simulation.logic<8>>) captures 1 : !simulation.observer<!simulation.logic<8>>
      %spawned = simulation.spawn @child(%ctx) : !simulation.context -> !simulation.process
      %called = simulation.call @callee(%ctx, %loaded) : (!simulation.context, !simulation.logic<8>) -> !simulation.logic<8>
      simulation.suspend.delay %time to ^bb1(%called : !simulation.logic<8>)
    ^bb1(%live1: !simulation.logic<8>):
      simulation.suspend.change %ref to ^bb2(%live1 : !simulation.logic<8>) : !simulation.ref<!simulation.logic<8>>
    ^bb2(%live2: !simulation.logic<8>):
      simulation.suspend.edge posedge %net to ^bb_iff(%live2 : !simulation.logic<8>) : !simulation.net<!simulation.logic<8>>
    ^bb_iff(%live_iff: !simulation.logic<8>):
      simulation.suspend.edge_iff posedge %ref iff %net to ^bb_level(%live_iff : !simulation.logic<8>) : !simulation.ref<!simulation.logic<8>>, !simulation.net<!simulation.logic<8>>
    ^bb_level(%live_level: !simulation.logic<8>):
      simulation.suspend.level %ref to ^bb3(%live_level : !simulation.logic<8>) : !simulation.ref<!simulation.logic<8>>
    ^bb3(%live3: !simulation.logic<8>):
      simulation.suspend.any %ref, %net, %live3 edges [0, 1] to ^bb_any : !simulation.ref<!simulation.logic<8>>, !simulation.net<!simulation.logic<8>>, !simulation.logic<8>
    ^bb_any(%live_any: !simulation.logic<8>):
      simulation.suspend.observe %observer, %live_any, %live_any conditions 0 edges [0] indices [-1] to ^bb_observe : !simulation.observer<!simulation.logic<8>>, !simulation.logic<8>, !simulation.logic<8>
    ^bb_observe(%live_observe: !simulation.logic<8>):
      simulation.suspend.event %event to ^bb4(%live_observe : !simulation.logic<8>)
    ^bb4(%live4: !simulation.logic<8>):
      simulation.suspend.await %spawned to ^bb5(%live4 : !simulation.logic<8>)
    ^bb5(%live5: !simulation.logic<8>):
      %spawned2 = simulation.spawn @child(%ctx) : !simulation.context -> !simulation.process
      simulation.suspend.join any %spawned, %spawned2, %live5 processes 2 to ^bb_forever : !simulation.process, !simulation.process, !simulation.logic<8>
    ^bb_forever(%live6: !simulation.logic<8>):
      simulation.suspend.forever to ^bb6(%live6 : !simulation.logic<8>)
    ^bb6(%live7: !simulation.logic<8>):
      simulation.return
    }
  }
}

// CHECK: simulation.design @roundtrip attributes {time_precision_fs = 1000 : i64}
// CHECK: simulation.code_unit.decl 10 in 0 root_initializer hierarchy "__obelisk_root"
// CHECK: simulation.code_unit.decl 11 in 0 function hierarchy "top.callee"
// CHECK: simulation.storage.decl 0 in 1 : !simulation.logic<8>
// CHECK: simulation.net.decl 0 in 1 : !simulation.logic<8>
// CHECK: simulation.net.decl 1 in 1 : !simulation.logic<1>
// CHECK-SAME: charge_strength = 4 : i32
// CHECK-SAME: resolution_kind = 9 : i32
// CHECK: simulation.driver.decl 0 in 1 drives 0
// CHECK-SAME: strength0 = 5 : i32
// CHECK-SAME: strength1 = 3 : i32
// CHECK: simulation.func @callee
// CHECK-SAME: simulation.bindings = [#simulation.argument_binding<path = "value", argument = 1, kind = direct, copyOut = false>, #simulation.constant_binding<path = "P", value = #simulation.frozen_constant<value = [-3 : i8, 0 : i8], isSigned = true> : !simulation.logic<8>>]
// CHECK: simulation.logic.is_true
// CHECK: simulation.logic.unary logical_not
// CHECK: simulation.logic.reduction xor
// CHECK: simulation.logic.binary add
// CHECK: simulation.logic.logical and
// CHECK: simulation.logic.shift left
// CHECK: simulation.bits.dyn_extract
// CHECK: simulation.logic.dyn_insert
// CHECK: simulation.bits.dyn_insert
// CHECK: simulation.ref.alloc
// CHECK: simulation.ref.dyn_extract {{.*}}!simulation.logic<32>
// CHECK: simulation.driver.extract
// CHECK: simulation.driver.dyn_extract
// CHECK: simulation.time.scale
// CHECK: simulation.nba.enqueue
// CHECK: simulation.event.trigger {{.*}} after
// CHECK: simulation.event.triggered
// CHECK: simulation.event.equal
// CHECK: simulation.observer.bind
// CHECK: simulation.suspend.delay
// CHECK: simulation.suspend.change
// CHECK: simulation.suspend.edge posedge
// CHECK: simulation.suspend.edge_iff posedge
// CHECK: simulation.suspend.level
// CHECK: simulation.suspend.any
// CHECK: simulation.suspend.observe
// CHECK: simulation.suspend.event
// CHECK: simulation.suspend.await
// CHECK: simulation.suspend.join any
// CHECK: simulation.suspend.forever
