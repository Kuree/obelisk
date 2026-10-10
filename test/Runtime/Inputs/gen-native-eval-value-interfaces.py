#!/usr/bin/env python3
"""Keep helpers outlined while exercising packed current-state interfaces."""

import sys

print('''module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", schedule.native.max_inline_ops = 256 : i64} {
  llvm.mlir.global @__obelisk_state_value(dense<[52, 18, 77]> : tensor<3xi8>) : !llvm.array<3 x i8>
  llvm.mlir.global @__obelisk_state_unknown(dense<0> : tensor<3xi8>) : !llvm.array<3 x i8>
  llvm.mlir.global internal @checksum(0 : i16) : i16
  llvm.mlir.global internal @context() : !llvm.ptr {
    %null = llvm.mlir.zero : !llvm.ptr
    llvm.return %null : !llvm.ptr
  }
  llvm.mlir.global internal @seen_context() : !llvm.ptr {
    %null = llvm.mlir.zero : !llvm.ptr
    llvm.return %null : !llvm.ptr
  }
  llvm.func @observer(i16, i16) -> i1
  llvm.func @lane_write(%take: i1 {simulation.capture_kind = 1 : i32}) attributes {schedule.eval.raw_captures, passthrough = ["noinline"]} {
    %v = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
    %x = llvm.mlir.addressof @__obelisk_state_unknown : !llvm.ptr
    %old = llvm.load %v {alignment = 1 : i64} : !llvm.ptr -> i16
    %oldx = llvm.load %x {alignment = 1 : i64} : !llvm.ptr -> i16
    %ctx = llvm.mlir.addressof @context : !llvm.ptr
    %context = llvm.load %ctx : !llvm.ptr -> !llvm.ptr
    %seen = llvm.mlir.addressof @seen_context : !llvm.ptr
    llvm.store %context, %seen : !llvm.ptr, !llvm.ptr
    %one = llvm.mlir.constant(1 : i16) : i16''')

if "--dynamic-cut" in sys.argv:
    print('''    %index = llvm.zext %take : i1 to i64
    %dynamic = llvm.getelementptr %v[%index] : (!llvm.ptr, i64) -> !llvm.ptr, i8
    %dynamic_byte = llvm.mlir.constant(0 : i8) : i8
    llvm.store %dynamic_byte, %dynamic {schedule.eval.state_access_bytes = array<i64: 0, 2>} : i8, !llvm.ptr''')


def outline_work(first):
    previous = first
    for i in range(300):
        name = f"%work{i}"
        print(f"    {name} = llvm.add {previous}, %one : i16")
        previous = name
    print(f'''    %checksum = llvm.mlir.addressof @checksum : !llvm.ptr
    llvm.store {previous}, %checksum : i16, !llvm.ptr''')


outline_work("%old")
print('''    llvm.cond_br %take, ^update, ^done
  ^update:
    %keep = llvm.mlir.constant(-16 : i16) : i16
    %low = llvm.mlir.constant(10 : i16) : i16
    %clear = llvm.and %old, %keep : i16
    %next = llvm.or %clear, %low : i16
    llvm.store %next, %v {alignment = 1 : i64} : i16, !llvm.ptr
    %high = llvm.getelementptr %v[1] : (!llvm.ptr) -> !llvm.ptr, i8
    %byte = llvm.mlir.constant(171 : i8) : i8
    llvm.store %byte, %high : i8, !llvm.ptr
    %xz = llvm.mlir.constant(-32768 : i16) : i16
    %newx = llvm.or %oldx, %xz : i16
    llvm.store %newx, %x {alignment = 1 : i64} : i16, !llvm.ptr
    llvm.br ^done
  ^done:
    llvm.return
  }
  llvm.func @lane_read(%expected: i16 {simulation.capture_kind = 1 : i32}, %expected_x: i16) -> i1 attributes {schedule.eval.infallible, passthrough = ["noinline"]} {
    %v = llvm.mlir.addressof @__obelisk_state_value : !llvm.ptr
    %x = llvm.mlir.addressof @__obelisk_state_unknown : !llvm.ptr
    %current = llvm.load %v {alignment = 1 : i64} : !llvm.ptr -> i16
    %current_x = llvm.load %x {alignment = 1 : i64} : !llvm.ptr -> i16
    %one = llvm.mlir.constant(1 : i16) : i16''')
outline_work("%current")
print('''    %okv = llvm.icmp "eq" %current, %expected : i16
    %okx = llvm.icmp "eq" %current_x, %expected_x : i16
    %ok = llvm.and %okv, %okx : i1
    llvm.return %ok : i1
  }
  llvm.func @lane_run(%take: i1) -> i1 attributes {schedule.eval.ranked_members = array<i32: 0, 1, 2>} {
    llvm.call @lane_write(%take) {arg_attrs = [{llvm.noundef}], schedule.eval.group_member = 0 : i32} : (i1) -> ()
    %old = llvm.mlir.constant(4660 : i16) : i16
    %new = llvm.mlir.constant(-21702 : i16) : i16
    %zero = llvm.mlir.constant(0 : i16) : i16
    %unknown = llvm.mlir.constant(-32768 : i16) : i16
    %expected = llvm.select %take, %new, %old : i1, i16
    %expected_x = llvm.select %take, %unknown, %zero : i1, i16
    %first = llvm.call @lane_read(%expected, %expected_x) {schedule.eval.group_member = 1 : i32} : (i16, i16) -> i1
    %seen = llvm.call @observer(%expected, %expected_x) : (i16, i16) -> i1
    %mutated = llvm.mlir.constant(-275 : i16) : i16
    %mutated_x = llvm.mlir.constant(192 : i16) : i16
    %last = llvm.call @lane_read(%mutated, %mutated_x) {schedule.eval.group_member = 2 : i32} : (i16, i16) -> i1
    %reads = llvm.and %first, %last : i1
    %all = llvm.and %reads, %seen : i1
    llvm.return %all : i1
  }
}''')
