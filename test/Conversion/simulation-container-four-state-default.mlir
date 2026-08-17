// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s

// Invalid sequential-container reads and newly allocated four-state elements
// have the SystemVerilog default value X. A null handle is the representation
// of a default-initialized empty container and must produce the element default
// for four-state, real, and string elements in both execution tiers.
// CHECK: 1 1 1 1 1 1

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @container_four_state_default {
    obelisk_sim.scope.decl 0 hierarchy "container_four_state_default"
    obelisk_sim.code_unit.decl 9920000 in 0 root_initializer
        hierarchy "container_four_state_default.root"
    obelisk_sim.code_unit.decl 9920001 in 0 initial
        hierarchy "container_four_state_default.initial"

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9920000 : i64} {
      %process = obelisk_sim.spawn @initial(%ctx) :
          !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @initial(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9920001 : i64} {
      %zero = arith.constant 0 : i64
      %one = arith.constant 1 : i64
      %negative = arith.constant -1 : i64
      %queue = obelisk_sim.container.create %zero {
        type_id = 9920002 : i64, element_kind = 2 : i32,
        element_flags = 1 : i32, value_size = 4 : i64,
        alignment = 1 : i64, bit_width = 32 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        container_kind = 2 : i32, bound = -1 : i64
      } : (i64) -> !obelisk_sim.queue<!obelisk_sim.logic<32>, 0>
      %array = obelisk_sim.container.create %one {
        type_id = 9920002 : i64, element_kind = 2 : i32,
        element_flags = 1 : i32, value_size = 4 : i64,
        alignment = 1 : i64, bit_width = 32 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        container_kind = 1 : i32, bound = 0 : i64
      } : (i64) -> !obelisk_sim.dynamic_array<!obelisk_sim.logic<32>>
      %null_logic = obelisk_sim.managed.null :
          !obelisk_sim.queue<!obelisk_sim.logic<32>, 0>
      %null_real = obelisk_sim.managed.null : !obelisk_sim.queue<f64, 0>
      %null_string = obelisk_sim.managed.null :
          !obelisk_sim.queue<!obelisk_sim.string, 0>
      %empty = obelisk_sim.container.read %queue, %zero :
          (!obelisk_sim.queue<!obelisk_sim.logic<32>, 0>, i64) ->
          !obelisk_sim.logic<32>
      %invalid = obelisk_sim.container.read %queue, %negative :
          (!obelisk_sim.queue<!obelisk_sim.logic<32>, 0>, i64) ->
          !obelisk_sim.logic<32>
      %allocated = obelisk_sim.container.read %array, %zero :
          (!obelisk_sim.dynamic_array<!obelisk_sim.logic<32>>, i64) ->
          !obelisk_sim.logic<32>
      %null_logic_value = obelisk_sim.container.read %null_logic, %zero :
          (!obelisk_sim.queue<!obelisk_sim.logic<32>, 0>, i64) ->
          !obelisk_sim.logic<32>
      %null_real_value = obelisk_sim.container.read %null_real, %zero :
          (!obelisk_sim.queue<f64, 0>, i64) -> f64
      %null_string_value = obelisk_sim.container.read %null_string, %zero :
          (!obelisk_sim.queue<!obelisk_sim.string, 0>, i64) ->
          !obelisk_sim.string
      %x = obelisk_sim.logic.constant 0 : i32, -1 : i32 :
          !obelisk_sim.logic<32>
      %empty_ok = obelisk_sim.logic.compare case_eq %empty, %x :
          (!obelisk_sim.logic<32>, !obelisk_sim.logic<32>) -> i1
      %invalid_ok = obelisk_sim.logic.compare case_eq %invalid, %x :
          (!obelisk_sim.logic<32>, !obelisk_sim.logic<32>) -> i1
      %allocated_ok = obelisk_sim.logic.compare case_eq %allocated, %x :
          (!obelisk_sim.logic<32>, !obelisk_sim.logic<32>) -> i1
      %null_logic_ok = obelisk_sim.logic.compare case_eq %null_logic_value, %x :
          (!obelisk_sim.logic<32>, !obelisk_sim.logic<32>) -> i1
      %real_zero = arith.constant 0.000000e+00 : f64
      %null_real_ok = arith.cmpf oeq, %null_real_value, %real_zero : f64
      %null_string_length = obelisk_sim.string.length %null_string_value :
          (!obelisk_sim.string) -> i64
      %null_string_ok = arith.cmpi eq, %null_string_length, %zero : i64
      %queue_ok = arith.andi %empty_ok, %invalid_ok : i1
      %ok = arith.andi %queue_ok, %allocated_ok : i1
      %format = obelisk_sim.bytes.constant "%0d %0d %0d %0d %0d %0d"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(
          %format, %empty_ok, %invalid_ok, %allocated_ok, %null_logic_ok,
          %null_real_ok, %null_string_ok)
          newline = true radix = 10 flags = [0, 0, 0, 0, 0, 0, 0] :
          !obelisk_sim.bytes, i1, i1, i1, i1, i1, i1
      obelisk_sim.return
    }
  }
}
