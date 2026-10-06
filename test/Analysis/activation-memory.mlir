// RUN: obelisk-opt %s --test-obelisk-activation-memory -o /dev/null 2>&1 | FileCheck %s

module {
  simulation.design @coverage {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design
    simulation.storage.decl 1 in 0 : i1 design
    simulation.storage.decl 2 in 0 : !simulation.unpacked_array<0 : 2 x i8> design
    simulation.storage.decl 3 in 0 : !simulation.unpacked_array<0 : 2 x !simulation.unpacked_array<0 : 2 x i8>> design

    // Neither store dominates the exit. Together they definitely assign s0.
    // CHECK: activation @diamond valid=1 complete=1
    // CHECK-NEXT: live-in s1[0,1)
    // CHECK-NEXT: must-write s0[0,8)
    // CHECK-NEXT: may-write s0[0,8)
    // CHECK-NEXT: repeated{{$}}
    simulation.code_unit.decl 1 in 0 function hierarchy "diamond"
    simulation.func @diamond(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %out = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %flag = simulation.context.storage %ctx[1] : !simulation.ref<i1>
      %test = simulation.ref.load %flag : !simulation.ref<i1> -> i1
      %zero = arith.constant 0 : i8
      %one = arith.constant 1 : i8
      cf.cond_br %test, ^yes, ^no
    ^yes:
      simulation.ref.store %one to %out : i8, !simulation.ref<i8>
      cf.br ^exit
    ^no:
      simulation.ref.store %zero to %out : i8, !simulation.ref<i8>
      cf.br ^exit
    ^exit:
      simulation.return
    }

    // A real missing assignment remains incomplete.
    // CHECK: activation @latch valid=1 complete=0
    // CHECK-NEXT: live-in s1[0,1)
    // CHECK-NEXT: must-write{{$}}
    // CHECK-NEXT: may-write s0[0,8)
    simulation.code_unit.decl 2 in 0 function hierarchy "latch"
    simulation.func @latch(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %out = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %flag = simulation.context.storage %ctx[1] : !simulation.ref<i1>
      %test = simulation.ref.load %flag : !simulation.ref<i1> -> i1
      %one = arith.constant 1 : i8
      cf.cond_br %test, ^yes, ^exit
    ^yes:
      simulation.ref.store %one to %out : i8, !simulation.ref<i8>
      cf.br ^exit
    ^exit:
      simulation.return
    }

    // LVT-shaped initialized reduction: finite nested loops, a skipped
    // diagonal, and source-ordered read/modify/write of each output lane.
    // The accumulator is not an input and every output lane is initialized.
    // CHECK: activation @reduction valid=1 complete=1
    // CHECK-NEXT: live-in s3[8,32) s3[40,64)
    // CHECK-NEXT: must-write s2[0,24)
    // CHECK-NEXT: may-write s2[0,24)
    // CHECK-NEXT: repeated s2[0,24)
    simulation.code_unit.decl 3 in 0 function hierarchy "reduction"
    simulation.func @reduction(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %out = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.unpacked_array<0 : 2 x i8>>
      %in = simulation.context.storage %ctx[3] : !simulation.ref<!simulation.unpacked_array<0 : 2 x !simulation.unpacked_array<0 : 2 x i8>>>
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %limit = arith.constant 3 : i32
      %seed = arith.constant 0 : i8
      cf.br ^outer(%zero : i32)
    ^outer(%i: i32):
      %more = arith.cmpi slt, %i, %limit : i32
      cf.cond_br %more, ^initialize, ^exit
    ^initialize:
      %dst = simulation.ref.array_element %out[%i] : (!simulation.ref<!simulation.unpacked_array<0 : 2 x i8>>, i32) -> !simulation.ref<i8>
      simulation.ref.store %seed to %dst : i8, !simulation.ref<i8>
      %row = simulation.ref.array_element %in[%i] : (!simulation.ref<!simulation.unpacked_array<0 : 2 x !simulation.unpacked_array<0 : 2 x i8>>>, i32) -> !simulation.ref<!simulation.unpacked_array<0 : 2 x i8>>
      cf.br ^inner(%zero : i32)
    ^inner(%j: i32):
      %again = arith.cmpi slt, %j, %limit : i32
      cf.cond_br %again, ^select, ^outer_next
    ^select:
      %different = arith.cmpi ne, %i, %j : i32
      cf.cond_br %different, ^accumulate, ^inner_next
    ^accumulate:
      %src = simulation.ref.array_element %row[%j] : (!simulation.ref<!simulation.unpacked_array<0 : 2 x i8>>, i32) -> !simulation.ref<i8>
      %value = simulation.ref.load %src : !simulation.ref<i8> -> i8
      %old = simulation.ref.load %dst : !simulation.ref<i8> -> i8
      %new = arith.xori %old, %value : i8
      simulation.ref.store %new to %dst : i8, !simulation.ref<i8>
      cf.br ^inner_next
    ^inner_next:
      %next_j = arith.addi %j, %one : i32
      cf.br ^inner(%next_j : i32)
    ^outer_next:
      %next_i = arith.addi %i, %one : i32
      cf.br ^outer(%next_i : i32)
    ^exit:
      simulation.return
    }

    // An incoming value is still live when it is read before the initializer.
    // CHECK: activation @read_before_write valid=1 complete=1
    // CHECK-NEXT: live-in s0[0,8)
    // CHECK-NEXT: must-write s0[0,8)
    simulation.code_unit.decl 4 in 0 function hierarchy "read_before_write"
    simulation.func @read_before_write(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %out = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %old = simulation.ref.load %out : !simulation.ref<i8> -> i8
      %one = arith.constant 1 : i8
      %new = arith.addi %old, %one : i8
      simulation.ref.store %new to %out : i8, !simulation.ref<i8>
      simulation.return
    }

    // Early exit can prevent complete array coverage even for a bounded loop.
    // CHECK: activation @early_exit valid=1 complete=0
    // CHECK-NEXT: live-in s1[0,1)
    // CHECK-NEXT: must-write s2[0,8)
    // CHECK-NEXT: may-write s2[0,24)
    simulation.code_unit.decl 5 in 0 function hierarchy "early_exit"
    simulation.func @early_exit(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      %out = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.unpacked_array<0 : 2 x i8>>
      %flag = simulation.context.storage %ctx[1] : !simulation.ref<i1>
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %limit = arith.constant 3 : i32
      %seed = arith.constant 0 : i8
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %more = arith.cmpi slt, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %dst = simulation.ref.array_element %out[%i] : (!simulation.ref<!simulation.unpacked_array<0 : 2 x i8>>, i32) -> !simulation.ref<i8>
      simulation.ref.store %seed to %dst : i8, !simulation.ref<i8>
      %stop = simulation.ref.load %flag : !simulation.ref<i1> -> i1
      cf.cond_br %stop, ^exit, ^latch
    ^latch:
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }

    // ActiveList-style four-state index arithmetic. Reuse dialect folding,
    // including the signed resize, rather than assuming selectors are i32.
    // CHECK: activation @logic_index valid=1 complete=1
    // CHECK-NEXT: live-in{{$}}
    // CHECK-NEXT: must-write s2[0,24)
    // CHECK-NEXT: may-write s2[0,24)
    // CHECK-NEXT: repeated{{$}}
    simulation.code_unit.decl 6 in 0 function hierarchy "logic_index"
    simulation.func @logic_index(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64} {
      %out = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.unpacked_array<0 : 2 x i8>>
      %first = simulation.ref.subelement %out[[0]] : !simulation.ref<!simulation.unpacked_array<0 : 2 x i8>> -> !simulation.ref<i8>
      %seed = arith.constant 0 : i8
      simulation.ref.store %seed to %first : i8, !simulation.ref<i8>
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %limit = arith.constant 2 : i32
      %offset = simulation.logic.constant 1 : i32, 0 : i32 : !simulation.logic<32>
      cf.br ^head(%zero : i32)
    ^head(%i: i32):
      %more = arith.cmpi slt, %i, %limit : i32
      cf.cond_br %more, ^body, ^exit
    ^body:
      %logic = simulation.logic.from_bits %i : i32 -> !simulation.logic<32>
      %sum = simulation.logic.binary add %logic, %offset : !simulation.logic<32>
      %wide = simulation.logic.resize %sum signed = true : !simulation.logic<32> -> !simulation.logic<65>
      %dst = simulation.ref.array_element %out[%wide] : (!simulation.ref<!simulation.unpacked_array<0 : 2 x i8>>, !simulation.logic<65>) -> !simulation.ref<i8>
      simulation.ref.store %seed to %dst : i8, !simulation.ref<i8>
      %next = arith.addi %i, %one : i32
      cf.br ^head(%next : i32)
    ^exit:
      simulation.return
    }

    // No induction proof is needed to retain a procedural while intact. The
    // memory lattice still proves initialization before every accumulator read.
    // This does not itself certify that the activation terminates.
    // CHECK: activation @procedural_while valid=1 complete=1
    // CHECK-NEXT: live-in s1[0,1)
    // CHECK-NEXT: must-write s0[0,8)
    // CHECK-NEXT: may-write s0[0,8)
    // CHECK-NEXT: repeated s0[0,8)
    simulation.code_unit.decl 7 in 0 function hierarchy "procedural_while"
    simulation.func @procedural_while(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 7 : i64} {
      %out = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %flag = simulation.context.storage %ctx[1] : !simulation.ref<i1>
      %input = simulation.ref.load %flag : !simulation.ref<i1> -> i1
      %seed = arith.extui %input : i1 to i8
      %one = arith.constant 1 : i8
      %zero = arith.constant 0 : i8
      simulation.ref.store %seed to %out : i8, !simulation.ref<i8>
      cf.br ^head
    ^head:
      %old = simulation.ref.load %out : !simulation.ref<i8> -> i8
      %more = arith.cmpi ne, %old, %zero : i8
      cf.cond_br %more, ^body, ^exit
    ^body:
      %next = arith.shrui %old, %one : i8
      simulation.ref.store %next to %out : i8, !simulation.ref<i8>
      cf.br ^head
    ^exit:
      simulation.return
    }
  }
}
