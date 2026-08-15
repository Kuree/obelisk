// RUN: obelisk-opt %s | FileCheck %s
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | FileCheck %s --check-prefix=BYTECODE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | %python %S/Inputs/dump-bytecode-instructions.py | FileCheck %s --check-prefix=INSTRUCTIONS
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE

!candidate = !obelisk_sim.unpacked_union<fields = [
  #obelisk_sim.field<name = "object", type = !obelisk_sim.class_handle<@Node>, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "bits", type = i64, ordinal = 1, packedOffset = 0>
], isTagged = false>
!path_holder = !obelisk_sim.unpacked_struct<[
  #obelisk_sim.field<name = "path", type = !obelisk_sim.reference_path<i64>, ordinal = 0, packedOffset = 0>
]>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @managed_types {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "top.process"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.string design
        hierarchy "top.text"
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.dynamic_array<!obelisk_sim.logic<4>> design
        hierarchy "top.values"
    obelisk_sim.storage.decl 2 in 0 : !obelisk_sim.queue<!obelisk_sim.string, 8> design
        hierarchy "top.names"
    obelisk_sim.storage.decl 3 in 0 : !obelisk_sim.assoc_array<!obelisk_sim.string, i64, false, false> design
        hierarchy "top.lookup"
    obelisk_sim.storage.decl 4 in 0 : !obelisk_sim.dynamic_array<!candidate> design
        hierarchy "top.candidates"
    obelisk_sim.class.decl @Node id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.func @process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32} {
      %empty = obelisk_sim.managed.null :
        !obelisk_sim.dynamic_array<!obelisk_sim.logic<4>>
      %fallback = obelisk_sim.managed.null :
        !obelisk_sim.dynamic_array<!obelisk_sim.logic<4>>
      %text = obelisk_sim.string.literal "live"
      %live = obelisk_sim.aggregate.construct %text :
        (!obelisk_sim.string) ->
        !obelisk_sim.unpacked_array<0 : 0 x !obelisk_sim.string>
      %size = "obelisk_sim.container.size"(%empty) :
        (!obelisk_sim.dynamic_array<!obelisk_sim.logic<4>>) -> i64
      %merged = "obelisk_sim.container.create_like"(%empty, %fallback, %size) :
        (!obelisk_sim.dynamic_array<!obelisk_sim.logic<4>>,
         !obelisk_sim.dynamic_array<!obelisk_sim.logic<4>>, i64) ->
        !obelisk_sim.dynamic_array<!obelisk_sim.logic<4>>
      %kept = obelisk_sim.aggregate.extract %live[0] :
        (!obelisk_sim.unpacked_array<0 : 0 x !obelisk_sim.string>) ->
        !obelisk_sim.string
      %length = obelisk_sim.string.length %kept :
        (!obelisk_sim.string) -> i64
      %index = arith.constant 0 : i64
      %element = "obelisk_sim.container.read"(%merged, %index) :
        (!obelisk_sim.dynamic_array<!obelisk_sim.logic<4>>, i64) ->
        !obelisk_sim.logic<4>
      "obelisk_sim.container.write"(%merged, %index, %element) :
        (!obelisk_sim.dynamic_array<!obelisk_sim.logic<4>>, i64,
         !obelisk_sim.logic<4>) -> ()
      %node = obelisk_sim.class.alloc %ctx :
          !obelisk_sim.context -> !obelisk_sim.class_handle<@Node>
      %candidate = obelisk_sim.union.construct %node as 0 :
          (!obelisk_sim.class_handle<@Node>) -> !candidate
      %one = arith.constant 1 : i64
      %candidates = obelisk_sim.container.create %one {
        type_id = 99 : i64, element_kind = 7 : i32,
        element_flags = 0 : i32, value_size = 8 : i64,
        alignment = 8 : i64, bit_width = 64 : i64,
        trace_offsets = array<i64: 0>,
        trace_kinds = array<i32: -2147483647>,
        container_kind = 1 : i32, bound = 0 : i64
      } : (i64) -> !obelisk_sim.dynamic_array<!candidate>
      "obelisk_sim.container.write"(%candidates, %index, %candidate) :
        (!obelisk_sim.dynamic_array<!candidate>, i64, !candidate) -> ()
      %paths = obelisk_sim.container.create %one {
        type_id = 100 : i64, element_kind = 7 : i32,
        element_flags = 0 : i32, value_size = 8 : i64,
        alignment = 8 : i64, bit_width = 64 : i64,
        trace_offsets = array<i64: 0>, trace_kinds = array<i32: 4>,
        container_kind = 1 : i32, bound = 0 : i64
      } : (i64) -> !obelisk_sim.dynamic_array<!path_holder>
      obelisk_sim.return
    }
  }
}

// CHECK: !obelisk_sim.string
// CHECK: !obelisk_sim.dynamic_array<!obelisk_sim.logic<4>>
// CHECK: !obelisk_sim.queue<!obelisk_sim.string, 8>
// CHECK: !obelisk_sim.assoc_array<!obelisk_sim.string, i64, false, false>
// CHECK: obelisk_sim.managed.null
// CHECK: obelisk_sim.aggregate.construct
// CHECK: obelisk_sim.container.size
// CHECK: obelisk_sim.container.create_like
// CHECK: obelisk_sim.aggregate.extract
// CHECK: obelisk_sim.container.read
// CHECK: obelisk_sim.container.write
// CHECK: trace_kinds = array<i32: -2147483647>
// CHECK: trace_kinds = array<i32: 4>

// A container whose element is logic remains one managed register. It must
// not acquire a second unknown plane from recursive containsLogic analysis.
// BYTECODE: obelisk.execution.state_bits = 320 : i64
// BYTECODE: obelisk_sim.storage.decl 0
// BYTECODE: obelisk_sim.storage.decl 1
// BYTECODE: obelisk_sim.storage.decl 2
// BYTECODE: obelisk_sim.storage.decl 3
// BYTECODE: obelisk_sim.storage.decl 4
// The live aggregate string across create_like needs one shadow-root slot.
// BYTECODE: obelisk.bytecode.scratch_size = 400 : i64

// The bytecode constants retain complete flattened trace-slot records:
// little-endian {offset = 0, candidate class kind = 0x80000001, reserved = 0}
// and {offset = 0, exact reference-path kind = 4, reserved = 0}.
// INSTRUCTIONS: constants: {{.*}}00000000000000000100008000000000{{.*}}00000000000000000400000000000000

// Candidate class roots retain the high candidate bit in the public trace
// record, and reference paths retain their distinct exact slot kind.
// NATIVE: llvm.mlir.global internal constant @__obelisk_element_trace_100("\00\00\00\00\00\00\00\00\04\00\00\00\00\00\00\00")
// NATIVE: llvm.mlir.global internal constant @__obelisk_element_trace_99("\00\00\00\00\00\00\00\00\01\00\00\80\00\00\00\00")
// NATIVE: llvm.call @obelisk_rt_v1_container_size
// NATIVE: llvm.call @obelisk_rt_v1_container_create_like
// NATIVE: llvm.call @obelisk_rt_v1_container_read
// NATIVE: llvm.call @obelisk_rt_v1_container_write
// NATIVE: llvm.call @obelisk_rt_v1_container_create_typed
// NATIVE: llvm.call @obelisk_rt_v1_container_write
// NATIVE: llvm.call @obelisk_rt_v1_container_create_typed
