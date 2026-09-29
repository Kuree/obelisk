// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llc -filetype=obj -relocation-model=pic -o %t.o
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
  simulation.design @container_four_state_default {
    simulation.scope.decl 0 hierarchy "container_four_state_default"
    simulation.code_unit.decl 9920000 in 0 root_initializer
        hierarchy "container_four_state_default.root"
    simulation.code_unit.decl 9920001 in 0 initial
        hierarchy "container_four_state_default.initial"

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9920000 : i64} {
      %process = simulation.spawn @initial(%ctx) :
          !simulation.context -> !simulation.process
      simulation.return
    }

    simulation.func private @initial(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9920001 : i64} {
      %zero = arith.constant 0 : i64
      %one = arith.constant 1 : i64
      %negative = arith.constant -1 : i64
      %queue = simulation.container.create %zero {
        type_id = 9920002 : i64, element_kind = #simulation.element_kind<logic>,
        element_flags = #simulation.element_flags<four_state>, value_size = 4 : i64,
        alignment = 1 : i64, bit_width = 32 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        container_kind = #simulation.container_kind<queue>, bound = -1 : i64
      } : (i64) -> !simulation.queue<!simulation.logic<32>, 0>
      %array = simulation.container.create %one {
        type_id = 9920002 : i64, element_kind = #simulation.element_kind<logic>,
        element_flags = #simulation.element_flags<four_state>, value_size = 4 : i64,
        alignment = 1 : i64, bit_width = 32 : i64,
        trace_offsets = array<i64>, trace_kinds = array<i32>,
        container_kind = #simulation.container_kind<dynamic_array>, bound = 0 : i64
      } : (i64) -> !simulation.dynamic_array<!simulation.logic<32>>
      %null_logic = simulation.managed.null :
          !simulation.queue<!simulation.logic<32>, 0>
      %null_real = simulation.managed.null : !simulation.queue<f64, 0>
      %null_string = simulation.managed.null :
          !simulation.queue<!simulation.string, 0>
      %empty = simulation.container.read %queue, %zero :
          (!simulation.queue<!simulation.logic<32>, 0>, i64) ->
          !simulation.logic<32>
      %invalid = simulation.container.read %queue, %negative :
          (!simulation.queue<!simulation.logic<32>, 0>, i64) ->
          !simulation.logic<32>
      %allocated = simulation.container.read %array, %zero :
          (!simulation.dynamic_array<!simulation.logic<32>>, i64) ->
          !simulation.logic<32>
      %null_logic_value = simulation.container.read %null_logic, %zero :
          (!simulation.queue<!simulation.logic<32>, 0>, i64) ->
          !simulation.logic<32>
      %null_real_value = simulation.container.read %null_real, %zero :
          (!simulation.queue<f64, 0>, i64) -> f64
      %null_string_value = simulation.container.read %null_string, %zero :
          (!simulation.queue<!simulation.string, 0>, i64) ->
          !simulation.string
      %x = simulation.logic.constant 0 : i32, -1 : i32 :
          !simulation.logic<32>
      %empty_ok = simulation.logic.compare case_eq %empty, %x :
          (!simulation.logic<32>, !simulation.logic<32>) -> i1
      %invalid_ok = simulation.logic.compare case_eq %invalid, %x :
          (!simulation.logic<32>, !simulation.logic<32>) -> i1
      %allocated_ok = simulation.logic.compare case_eq %allocated, %x :
          (!simulation.logic<32>, !simulation.logic<32>) -> i1
      %null_logic_ok = simulation.logic.compare case_eq %null_logic_value, %x :
          (!simulation.logic<32>, !simulation.logic<32>) -> i1
      %real_zero = arith.constant 0.000000e+00 : f64
      %null_real_ok = arith.cmpf oeq, %null_real_value, %real_zero : f64
      %null_string_length = simulation.string.length %null_string_value :
          (!simulation.string) -> i64
      %null_string_ok = arith.cmpi eq, %null_string_length, %zero : i64
      %queue_ok = arith.andi %empty_ok, %invalid_ok : i1
      %ok = arith.andi %queue_ok, %allocated_ok : i1
      %format = simulation.bytes.constant "%0d %0d %0d %0d %0d %0d"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(
          %format, %empty_ok, %invalid_ok, %allocated_ok, %null_logic_ok,
          %null_real_ok, %null_string_ok)
          newline = true radix = <decimal> flags = [0, 0, 0, 0, 0, 0, 0] :
          !simulation.bytes, i1, i1, i1, i1, i1, i1
      simulation.return
    }
  }
}
