// RUN: obelisk-opt %s | FileCheck %s
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | FileCheck %s --check-prefix=BYTECODE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | %python %S/Inputs/dump-bytecode-instructions.py | FileCheck %s --check-prefix=INSTRUCTIONS
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE

!candidate = !simulation.unpacked_union<fields = [
  #simulation.field<name = "object", type = !simulation.class_handle<@Node>, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "bits", type = i64, ordinal = 1, packedOffset = 0>
], isTagged = false>
!path_holder = !simulation.unpacked_struct<[
  #simulation.field<name = "path", type = !simulation.reference_path<i64>, ordinal = 0, packedOffset = 0>
]>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  // A serially prepared native byte global may be reused only when its full
  // ABI shape and payload agree with the requested trace.
  llvm.mlir.global internal constant @__obelisk_element_trace_100("\00\00\00\00\00\00\00\00\04\00\00\00\00\00\00\00") {alignment = 1 : i64}

  simulation.design @managed_types {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 initial hierarchy "top.process"
    simulation.storage.decl 0 in 0 : !simulation.string design
        hierarchy "top.text"
    simulation.storage.decl 1 in 0 : !simulation.dynamic_array<!simulation.logic<4>> design
        hierarchy "top.values"
    simulation.storage.decl 2 in 0 : !simulation.queue<!simulation.string, 8> design
        hierarchy "top.names"
    simulation.storage.decl 3 in 0 : !simulation.assoc_array<!simulation.string, i64, false, false> design
        hierarchy "top.lookup"
    simulation.storage.decl 4 in 0 : !simulation.dynamic_array<!candidate> design
        hierarchy "top.candidates"
    simulation.class.decl @Node id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.func @process(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %empty = simulation.managed.null :
        !simulation.dynamic_array<!simulation.logic<4>>
      %fallback = simulation.managed.null :
        !simulation.dynamic_array<!simulation.logic<4>>
      %text = simulation.string.literal "live"
      %live = simulation.aggregate.construct %text :
        (!simulation.string) ->
        !simulation.unpacked_array<0 : 0 x !simulation.string>
      %size = simulation.container.size %empty :
        (!simulation.dynamic_array<!simulation.logic<4>>) -> i64
      %merged = simulation.container.create_like %empty, %fallback, %size :
        (!simulation.dynamic_array<!simulation.logic<4>>,
         !simulation.dynamic_array<!simulation.logic<4>>, i64) ->
        !simulation.dynamic_array<!simulation.logic<4>>
      %kept = simulation.aggregate.extract %live[0] :
        (!simulation.unpacked_array<0 : 0 x !simulation.string>) ->
        !simulation.string
      %length = simulation.string.length %kept :
        (!simulation.string) -> i64
      %index = arith.constant 0 : i64
      %element = simulation.container.read %merged, %index :
        (!simulation.dynamic_array<!simulation.logic<4>>, i64) ->
        !simulation.logic<4>
      simulation.container.write %merged, %index, %element :
        (!simulation.dynamic_array<!simulation.logic<4>>, i64,
         !simulation.logic<4>) -> ()
      %node = simulation.class.alloc %ctx :
          !simulation.context -> !simulation.class_handle<@Node>
      %candidate = simulation.union.construct %node as 0 :
          (!simulation.class_handle<@Node>) -> !candidate
      %one = arith.constant 1 : i64
      %candidates = simulation.container.create %one {
        type_id = 99 : i64, element_kind = #simulation.element_kind<aggregate>,
        element_flags = #simulation.element_flags<none>, value_size = 8 : i64,
        alignment = 8 : i64, bit_width = 64 : i64,
        trace_offsets = array<i64: 0>,
        trace_kinds = array<i32: -2147483647>,
        container_kind = #simulation.container_kind<dynamic_array>, bound = 0 : i64
      } : (i64) -> !simulation.dynamic_array<!candidate>
      simulation.container.write %candidates, %index, %candidate :
        (!simulation.dynamic_array<!candidate>, i64, !candidate) -> ()
      %paths = simulation.container.create %one {
        type_id = 100 : i64, element_kind = #simulation.element_kind<aggregate>,
        element_flags = #simulation.element_flags<none>, value_size = 8 : i64,
        alignment = 8 : i64, bit_width = 64 : i64,
        trace_offsets = array<i64: 0>, trace_kinds = array<i32: 4>,
        container_kind = #simulation.container_kind<dynamic_array>, bound = 0 : i64
      } : (i64) -> !simulation.dynamic_array<!path_holder>
      simulation.return
    }
  }
}

// CHECK: !simulation.string
// CHECK: !simulation.dynamic_array<!simulation.logic<4>>
// CHECK: !simulation.queue<!simulation.string, 8>
// CHECK: !simulation.assoc_array<!simulation.string, i64, false, false>
// CHECK: simulation.managed.null
// CHECK: simulation.aggregate.construct
// CHECK: simulation.container.size
// CHECK: simulation.container.create_like
// CHECK: simulation.aggregate.extract
// CHECK: simulation.container.read
// CHECK: simulation.container.write
// CHECK: trace_kinds = array<i32: -2147483647>
// CHECK: trace_kinds = array<i32: 4>

// A container whose element is logic remains one managed register. It must
// not acquire a second unknown plane from recursive containsLogic analysis.
// BYTECODE: obelisk.execution.state_bits = 320 : i64
// BYTECODE: simulation.storage.decl 0
// BYTECODE: simulation.storage.decl 1
// BYTECODE: simulation.storage.decl 2
// BYTECODE: simulation.storage.decl 3
// BYTECODE: simulation.storage.decl 4
// The live aggregate string across create_like needs one shadow-root slot.
// BYTECODE: obelisk.bytecode.scratch_size = 400 : i64

// The bytecode constants retain complete flattened trace-slot records:
// little-endian {offset = 0, candidate class kind = 0x80000001, reserved = 0}
// and {offset = 0, exact reference-path kind = 4, reserved = 0}.
// INSTRUCTIONS: constants: {{.*}}00000000000000000100008000000000{{.*}}00000000000000000400000000000000

// Candidate class roots retain the high candidate bit in the public trace
// record, and reference paths retain their distinct exact slot kind.
// NATIVE: llvm.mlir.global internal constant @__obelisk_element_trace_99("\00\00\00\00\00\00\00\00\01\00\00\80\00\00\00\00")
// NATIVE-COUNT-1: llvm.mlir.global internal constant @__obelisk_element_trace_100("\00\00\00\00\00\00\00\00\04\00\00\00\00\00\00\00")
// NATIVE: llvm.call @obelisk_rt_v1_container_size
// NATIVE: llvm.call @obelisk_rt_v1_container_create_like
// NATIVE: llvm.call @obelisk_rt_v1_container_read
// NATIVE: llvm.call @obelisk_rt_v1_container_write
// NATIVE: llvm.call @obelisk_rt_v1_container_create_typed
// NATIVE: llvm.call @obelisk_rt_v1_container_write
// NATIVE: llvm.call @obelisk_rt_v1_container_create_typed
