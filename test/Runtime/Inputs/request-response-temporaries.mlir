// A cyclic request/response cohort publishes through an isolated wire.
// The producer reads the resolved value and the transform drives it.
// A source-written temporary feeds both sides of the cyclic request/response
// split. Its saved value must be reconstructed in source order in each slice.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 1 : i32
} {
  simulation.design @address_then_result {
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : !simulation.logic<16> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<16> design
    simulation.storage.decl 0 in 0 : !simulation.logic<8> design
    simulation.storage.decl 1 in 0 : !simulation.logic<16> design
    simulation.storage.decl 2 in 0 : !simulation.logic<16> design
    simulation.storage.decl 3 in 0 : !simulation.logic<16> design
    simulation.storage.decl 4 in 0 : !simulation.logic<1> design
    simulation.storage.decl 5 in 0 : !simulation.logic<16> design
    simulation.storage.decl 6 in 0 : !simulation.logic<8> design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "producer_and_consumer"
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "transform"
    simulation.code_unit.decl 3 in 0 always hierarchy "sample"
    simulation.code_unit.decl 4 in 0 initial hierarchy "driver"
    simulation.code_unit.decl 5 in 0 always hierarchy "clock"
    simulation.code_unit.decl 6 in 0 root_initializer hierarchy "root"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 6 : i64} {
      %clock = simulation.context.storage %ctx[4] : !simulation.ref<!simulation.logic<1>>
      %zero = simulation.logic.constant false, false : !simulation.logic<1>
      simulation.ref.store %zero to %clock : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      %c = simulation.spawn @clock(%ctx) : !simulation.context -> !simulation.process
      %a = simulation.spawn @producer_and_consumer(%ctx) : !simulation.context -> !simulation.process
      %b = simulation.spawn @transform(%ctx) : !simulation.context -> !simulation.process
      %s = simulation.spawn @sample(%ctx, %clock) : !simulation.context, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %d = simulation.spawn @driver(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @producer_and_consumer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      cf.br ^body
    ^body:
      %input = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<8>>
      %request = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<16>>
      %response = simulation.context.net %ctx[0] : !simulation.net<!simulation.logic<16>>
      %output = simulation.context.storage %ctx[3] : !simulation.ref<!simulation.logic<16>>
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %limit = arith.constant 2 : i32
      %eight = arith.constant 8 : i32
      %v = simulation.ref.load %input : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      cf.br ^loop(%zero : i32)
    ^loop(%i: i32):
      %more = arith.cmpi slt, %i, %limit : i32
      cf.cond_br %more, ^element, ^wait
    ^element:
      %offset = arith.muli %i, %eight : i32
      %index_bits = arith.trunci %i : i32 to i8
      %index = simulation.logic.from_bits %index_bits : i8 -> !simulation.logic<8>
      %q = simulation.logic.binary add %v, %index : !simulation.logic<8>
      %temp = simulation.context.storage %ctx[6] : !simulation.ref<!simulation.logic<8>>
      simulation.ref.store %q to %temp : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      %snapshot = simulation.ref.load %temp : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %req = simulation.ref.dyn_extract %request from %offset : (!simulation.ref<!simulation.logic<16>>, i32) -> !simulation.ref<!simulation.logic<8>>
      simulation.ref.store %snapshot to %req : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      %resolved = simulation.net.read %response : !simulation.net<!simulation.logic<16>> -> !simulation.logic<16>
      %r = simulation.logic.dyn_extract %resolved from %offset : (!simulation.logic<16>, i32) -> !simulation.logic<8>
      %out = simulation.ref.dyn_extract %output from %offset : (!simulation.ref<!simulation.logic<16>>, i32) -> !simulation.ref<!simulation.logic<8>>
      %difference = simulation.logic.binary sub %r, %snapshot : !simulation.logic<8>
      %restored = simulation.logic.binary add %difference, %snapshot : !simulation.logic<8>
      simulation.ref.store %restored to %out : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      %next = arith.addi %i, %one : i32
      cf.br ^loop(%next : i32)
    ^wait:
      simulation.suspend.any %input, %response edges [0, 0] to ^body : !simulation.ref<!simulation.logic<8>>, !simulation.net<!simulation.logic<16>>
    }
    simulation.func @transform(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %request = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<16>>
      %response = simulation.context.net %ctx[0] : !simulation.net<!simulation.logic<16>>
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %limit = arith.constant 2 : i32
      %eight = arith.constant 8 : i32
      %empty = simulation.logic.constant 0 : i16, 0 : i16 : !simulation.logic<16>
      %seed = simulation.packed.unflatten %empty : (!simulation.logic<16>) -> !simulation.packed_array<1 : 0 x !simulation.logic<8>>
      %initial = simulation.logic.constant 0 : i8, 0 : i8 : !simulation.logic<8>
      cf.br ^loop(%zero, %initial, %seed : i32, !simulation.logic<8>, !simulation.packed_array<1 : 0 x !simulation.logic<8>>)
    ^loop(%i: i32, %prefix: !simulation.logic<8>, %array: !simulation.packed_array<1 : 0 x !simulation.logic<8>>):
      %more = arith.cmpi slt, %i, %limit : i32
      cf.cond_br %more, ^element, ^publish
    ^element:
      %offset = arith.muli %i, %eight : i32
      %req = simulation.ref.dyn_extract %request from %offset : (!simulation.ref<!simulation.logic<16>>, i32) -> !simulation.ref<!simulation.logic<8>>
      %q = simulation.ref.load %req : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %value = simulation.logic.binary add %prefix, %q : !simulation.logic<8>
      %updated = simulation.array.insert_dynamic %value into %array[%i] : (!simulation.packed_array<1 : 0 x !simulation.logic<8>>, !simulation.logic<8>, i32) -> !simulation.packed_array<1 : 0 x !simulation.logic<8>>
      %next = arith.addi %i, %one : i32
      cf.br ^loop(%next, %value, %updated : i32, !simulation.logic<8>, !simulation.packed_array<1 : 0 x !simulation.logic<8>>)
    ^publish:
      %flat = simulation.packed.flatten %array : (!simulation.packed_array<1 : 0 x !simulation.logic<8>>) -> !simulation.logic<16>
      %driver = simulation.context.driver %ctx[0] : !simulation.driver<!simulation.logic<16>>
      simulation.driver.drive %driver = %flat : !simulation.driver<!simulation.logic<16>>, !simulation.logic<16>
      simulation.suspend.change %request to ^body : !simulation.ref<!simulation.logic<16>>
    }
    simulation.func @sample(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %clock: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clock to ^body : !simulation.ref<!simulation.logic<1>>
    ^body:
      %result = simulation.context.storage %ctx[3] : !simulation.ref<!simulation.logic<16>>
      %sample = simulation.context.storage %ctx[5] : !simulation.ref<!simulation.logic<16>>
      %r = simulation.ref.load %result : !simulation.ref<!simulation.logic<16>> -> !simulation.logic<16>
      simulation.nba.enqueue %r to %sample : (!simulation.logic<16>, !simulation.ref<!simulation.logic<16>>) -> ()
      cf.br ^wait
    }
    simulation.func @clock(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 3 : i32, code_unit_id = 5 : i64} {
      %clock = simulation.context.storage %ctx[4] : !simulation.ref<!simulation.logic<1>>
      %delay = simulation.time.constant 1
      cf.br ^wait
    ^wait:
      simulation.suspend.delay %delay to ^toggle
    ^toggle:
      %old = simulation.ref.load %clock : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %next = simulation.logic.unary bit_not %old : (!simulation.logic<1>) -> !simulation.logic<1>
      simulation.ref.store %next to %clock : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      cf.br ^wait
    }
    simulation.func @driver(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<8>>
      %output = simulation.context.storage %ctx[3] : !simulation.ref<!simulation.logic<16>>
      %sample = simulation.context.storage %ctx[5] : !simulation.ref<!simulation.logic<16>>
      %fmt = simulation.bytes.constant "result %h sample %h"
      %channel = arith.constant 1 : i32
      %delay = simulation.time.constant 2
      simulation.suspend.delay %delay to ^step0
    ^step0:
      %read0 = simulation.ref.load %output : !simulation.ref<!simulation.logic<16>> -> !simulation.logic<16>
      %sample0 = simulation.ref.load %sample : !simulation.ref<!simulation.logic<16>> -> !simulation.logic<16>
      simulation.display %ctx to %channel(%fmt, %read0, %sample0) newline = true radix = <hex> flags = [0, 0, 0] : !simulation.bytes, !simulation.logic<16>, !simulation.logic<16>
      %value0 = simulation.logic.constant 7 : i8, 0 : i8 : !simulation.logic<8>
      simulation.ref.store %value0 to %input : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.suspend.delay %delay to ^step1
    ^step1:
      %read1 = simulation.ref.load %output : !simulation.ref<!simulation.logic<16>> -> !simulation.logic<16>
      %sample1 = simulation.ref.load %sample : !simulation.ref<!simulation.logic<16>> -> !simulation.logic<16>
      simulation.display %ctx to %channel(%fmt, %read1, %sample1) newline = true radix = <hex> flags = [0, 0, 0] : !simulation.bytes, !simulation.logic<16>, !simulation.logic<16>
      %value1 = simulation.logic.constant 0 : i8, 255 : i8 : !simulation.logic<8>
      simulation.ref.store %value1 to %input : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.suspend.delay %delay to ^step2
    ^step2:
      %read2 = simulation.ref.load %output : !simulation.ref<!simulation.logic<16>> -> !simulation.logic<16>
      %sample2 = simulation.ref.load %sample : !simulation.ref<!simulation.logic<16>> -> !simulation.logic<16>
      simulation.display %ctx to %channel(%fmt, %read2, %sample2) newline = true radix = <hex> flags = [0, 0, 0] : !simulation.bytes, !simulation.logic<16>, !simulation.logic<16>
      %value2 = simulation.logic.constant 255 : i8, 255 : i8 : !simulation.logic<8>
      simulation.ref.store %value2 to %input : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.suspend.delay %delay to ^step3
    ^step3:
      %read3 = simulation.ref.load %output : !simulation.ref<!simulation.logic<16>> -> !simulation.logic<16>
      %sample3 = simulation.ref.load %sample : !simulation.ref<!simulation.logic<16>> -> !simulation.logic<16>
      simulation.display %ctx to %channel(%fmt, %read3, %sample3) newline = true radix = <hex> flags = [0, 0, 0] : !simulation.bytes, !simulation.logic<16>, !simulation.logic<16>
      %value3 = simulation.logic.constant 255 : i8, 0 : i8 : !simulation.logic<8>
      simulation.ref.store %value3 to %input : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.suspend.delay %delay to ^step4
    ^step4:
      %read4 = simulation.ref.load %output : !simulation.ref<!simulation.logic<16>> -> !simulation.logic<16>
      %sample4 = simulation.ref.load %sample : !simulation.ref<!simulation.logic<16>> -> !simulation.logic<16>
      simulation.display %ctx to %channel(%fmt, %read4, %sample4) newline = true radix = <hex> flags = [0, 0, 0] : !simulation.bytes, !simulation.logic<16>, !simulation.logic<16>
      %value4 = simulation.logic.constant 0 : i8, 0 : i8 : !simulation.logic<8>
      simulation.ref.store %value4 to %input : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.suspend.delay %delay to ^step5
    ^step5:
      %read5 = simulation.ref.load %output : !simulation.ref<!simulation.logic<16>> -> !simulation.logic<16>
      %sample5 = simulation.ref.load %sample : !simulation.ref<!simulation.logic<16>> -> !simulation.logic<16>
      simulation.display %ctx to %channel(%fmt, %read5, %sample5) newline = true radix = <hex> flags = [0, 0, 0] : !simulation.bytes, !simulation.logic<16>, !simulation.logic<16>
      %zero = arith.constant 0 : i32
      simulation.finish %ctx, %zero
      simulation.return
    }
  }
}
