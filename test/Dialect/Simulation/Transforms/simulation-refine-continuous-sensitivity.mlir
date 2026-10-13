// RUN: obelisk-opt %s --split-input-file --pass-pipeline='builtin.module(simulation.design(obelisk-sim-refine-continuous-sensitivity{vpi=off}))' -o %t.threaded
// RUN: obelisk-opt %s --split-input-file --mlir-disable-threading --pass-pipeline='builtin.module(simulation.design(obelisk-sim-refine-continuous-sensitivity{vpi=off}))' -o %t.serial
// RUN: diff %t.threaded %t.serial
// RUN: obelisk-opt %s --split-input-file --pass-pipeline='builtin.module(simulation.design(obelisk-sim-refine-continuous-sensitivity{vpi=read}))' | FileCheck %s --check-prefix=KEEP
// RUN: obelisk-opt %s --split-input-file --pass-pipeline='builtin.module(simulation.design(obelisk-sim-refine-continuous-sensitivity{vpi=off}))' | FileCheck %s
// RUN: obelisk-opt %s --split-input-file --pass-pipeline='builtin.module(simulation.design(obelisk-sim-refine-continuous-sensitivity{vpi=full}))' | FileCheck %s --check-prefix=KEEP

// The dynamic inner selection watches the entire fixed outer element, plus
// the index. A runtime selector must never be frozen into the watch identity.
!inner = !simulation.unpacked_array<0 : 3 x i8>
!outer = !simulation.unpacked_array<0 : 1 x !inner>
module {
  simulation.design @prefix {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !outer design
    simulation.storage.decl 1 in 0 : i32 design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 continuous hierarchy "copy"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<!outer>
      %index = simulation.context.storage %ctx[1] : !simulation.ref<i32>
      %output = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %copy = simulation.spawn @copy(%ctx, %input, %index, %output) : !simulation.context, !simulation.ref<!outer>, !simulation.ref<i32>, !simulation.ref<i8> -> !simulation.process
      simulation.return
    }
    // CHECK-LABEL: simulation.func private @copy(
    // CHECK: simulation.ref.array_element
    // CHECK: simulation.ref.store
    // CHECK: %[[INDEX:.*]] = simulation.context.storage %arg0[1]
    // CHECK: %[[ROOT:.*]] = simulation.context.storage %arg0[0]
    // CHECK: %[[OUTER:.*]] = simulation.ref.subelement %[[ROOT]]{{\[\[}}1]]
    // CHECK: simulation.suspend.any %[[INDEX]], %[[OUTER]] edges [0, 0]
    // KEEP-LABEL: simulation.func private @copy(
    // KEEP: simulation.suspend.any %arg1, %arg2 edges [0, 0]
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.ref<!outer> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %index: !simulation.ref<i32> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %output: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %outer = simulation.ref.subelement %input[[1]] : !simulation.ref<!outer> -> !simulation.ref<!inner>
      %selector = simulation.ref.load %index : !simulation.ref<i32> -> i32
      %element = simulation.ref.array_element %outer[%selector] : (!simulation.ref<!inner>, i32) -> !simulation.ref<i8>
      %value = simulation.ref.load %element : !simulation.ref<i8> -> i8
      simulation.ref.store %value to %output : i8, !simulation.ref<i8>
      simulation.suspend.any %input, %index edges [0, 0] to ^body : !simulation.ref<!outer>, !simulation.ref<i32>
    }
  }
}

// -----

// CHECK-LABEL: simulation.design @procedural
// CHECK: simulation.func private @copy(
// CHECK: simulation.suspend.any %arg1, %arg2
// KEEP-LABEL: simulation.design @procedural
// KEEP: simulation.suspend.any %arg1, %arg2

!inner = !simulation.unpacked_array<0 : 3 x i8>
!outer = !simulation.unpacked_array<0 : 1 x !inner>
module {
  simulation.design @procedural {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !outer design
    simulation.storage.decl 1 in 0 : i32 design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "copy"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<!outer>
      %index = simulation.context.storage %ctx[1] : !simulation.ref<i32>
      %output = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %copy = simulation.spawn @copy(%ctx, %input, %index, %output) : !simulation.context, !simulation.ref<!outer>, !simulation.ref<i32>, !simulation.ref<i8> -> !simulation.process
      simulation.return
    }
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.ref<!outer> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %index: !simulation.ref<i32> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %output: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %outer = simulation.ref.subelement %input[[1]] : !simulation.ref<!outer> -> !simulation.ref<!inner>
      %selector = simulation.ref.load %index : !simulation.ref<i32> -> i32
      %element = simulation.ref.array_element %outer[%selector] : (!simulation.ref<!inner>, i32) -> !simulation.ref<i8>
      %value = simulation.ref.load %element : !simulation.ref<i8> -> i8
      simulation.ref.store %value to %output : i8, !simulation.ref<i8>
      simulation.suspend.any %input, %index edges [0, 0] to ^body : !simulation.ref<!outer>, !simulation.ref<i32>
    }
  }
}

// -----

// CHECK-LABEL: simulation.design @self_read
// CHECK: simulation.func private @copy(
// CHECK: simulation.suspend.any %arg1, %arg2
// KEEP-LABEL: simulation.design @self_read
// KEEP: simulation.suspend.any %arg1, %arg2

!inner = !simulation.unpacked_array<0 : 3 x i8>
!outer = !simulation.unpacked_array<0 : 1 x !inner>
module {
  simulation.design @self_read {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !outer design
    simulation.storage.decl 1 in 0 : i32 design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 continuous hierarchy "copy"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<!outer>
      %index = simulation.context.storage %ctx[1] : !simulation.ref<i32>
      %output = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %copy = simulation.spawn @copy(%ctx, %input, %index, %output) : !simulation.context, !simulation.ref<!outer>, !simulation.ref<i32>, !simulation.ref<i8> -> !simulation.process
      simulation.return
    }
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.ref<!outer> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %index: !simulation.ref<i32> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %output: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %outer = simulation.ref.subelement %input[[1]] : !simulation.ref<!outer> -> !simulation.ref<!inner>
      %selector = simulation.ref.load %index : !simulation.ref<i32> -> i32
      %element = simulation.ref.array_element %outer[%selector] : (!simulation.ref<!inner>, i32) -> !simulation.ref<i8>
      %value = simulation.ref.load %element : !simulation.ref<i8> -> i8
      %old = simulation.ref.load %output : !simulation.ref<i8> -> i8
      %sum = arith.addi %old, %value : i8
      simulation.ref.store %sum to %output : i8, !simulation.ref<i8>
      simulation.suspend.any %input, %index edges [0, 0] to ^body : !simulation.ref<!outer>, !simulation.ref<i32>
    }
  }
}

// -----

// CHECK-LABEL: simulation.design @multiple_instances
// CHECK: simulation.func private @copy(
// CHECK: simulation.suspend.any %arg1, %arg2
// KEEP-LABEL: simulation.design @multiple_instances
// KEEP: simulation.suspend.any %arg1, %arg2

!inner = !simulation.unpacked_array<0 : 3 x i8>
!outer = !simulation.unpacked_array<0 : 1 x !inner>
module {
  simulation.design @multiple_instances {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !outer design
    simulation.storage.decl 1 in 0 : i32 design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 continuous hierarchy "copy"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<!outer>
      %index = simulation.context.storage %ctx[1] : !simulation.ref<i32>
      %output = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %copy = simulation.spawn @copy(%ctx, %input, %index, %output) : !simulation.context, !simulation.ref<!outer>, !simulation.ref<i32>, !simulation.ref<i8> -> !simulation.process
      %second = simulation.spawn @copy(%ctx, %input, %index, %output) : !simulation.context, !simulation.ref<!outer>, !simulation.ref<i32>, !simulation.ref<i8> -> !simulation.process
      simulation.return
    }
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.ref<!outer> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %index: !simulation.ref<i32> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %output: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %outer = simulation.ref.subelement %input[[1]] : !simulation.ref<!outer> -> !simulation.ref<!inner>
      %selector = simulation.ref.load %index : !simulation.ref<i32> -> i32
      %element = simulation.ref.array_element %outer[%selector] : (!simulation.ref<!inner>, i32) -> !simulation.ref<i8>
      %value = simulation.ref.load %element : !simulation.ref<i8> -> i8
      simulation.ref.store %value to %output : i8, !simulation.ref<i8>
      simulation.suspend.any %input, %index edges [0, 0] to ^body : !simulation.ref<!outer>, !simulation.ref<i32>
    }
  }
}

// -----

// CHECK-LABEL: simulation.design @carried_state
// CHECK: simulation.func private @copy(
// CHECK: simulation.suspend.any %arg1, %arg2
// KEEP-LABEL: simulation.design @carried_state
// KEEP: simulation.suspend.any %arg1, %arg2

!inner = !simulation.unpacked_array<0 : 3 x i8>
!outer = !simulation.unpacked_array<0 : 1 x !inner>
module {
  simulation.design @carried_state {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !outer design
    simulation.storage.decl 1 in 0 : i32 design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 continuous hierarchy "copy"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<!outer>
      %index = simulation.context.storage %ctx[1] : !simulation.ref<i32>
      %output = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %copy = simulation.spawn @copy(%ctx, %input, %index, %output) : !simulation.context, !simulation.ref<!outer>, !simulation.ref<i32>, !simulation.ref<i8> -> !simulation.process
      simulation.return
    }
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.ref<!outer> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %index: !simulation.ref<i32> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %output: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 2 : i64} {
      %zero = arith.constant 0 : i8
      cf.br ^body(%zero : i8)
    ^body(%previous: i8):
      %outer = simulation.ref.subelement %input[[1]] : !simulation.ref<!outer> -> !simulation.ref<!inner>
      %selector = simulation.ref.load %index : !simulation.ref<i32> -> i32
      %element = simulation.ref.array_element %outer[%selector] : (!simulation.ref<!inner>, i32) -> !simulation.ref<i8>
      %value = simulation.ref.load %element : !simulation.ref<i8> -> i8
      %sum = arith.addi %value, %previous : i8
      simulation.ref.store %sum to %output : i8, !simulation.ref<i8>
      simulation.suspend.any %input, %index, %sum edges [0, 0] to ^body : !simulation.ref<!outer>, !simulation.ref<i32>, i8
    }
  }
}

// -----

// CHECK-LABEL: simulation.design @other_writer
// CHECK: simulation.func private @copy(
// CHECK: simulation.suspend.any %arg1, %arg2
// KEEP-LABEL: simulation.design @other_writer
// KEEP: simulation.suspend.any %arg1, %arg2

!inner = !simulation.unpacked_array<0 : 3 x i8>
!outer = !simulation.unpacked_array<0 : 1 x !inner>
module {
  simulation.design @other_writer {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !outer design
    simulation.storage.decl 1 in 0 : i32 design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 3 in 0 initial hierarchy "writer"
    simulation.code_unit.decl 2 in 0 continuous hierarchy "copy"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<!outer>
      %index = simulation.context.storage %ctx[1] : !simulation.ref<i32>
      %output = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %copy = simulation.spawn @copy(%ctx, %input, %index, %output) : !simulation.context, !simulation.ref<!outer>, !simulation.ref<i32>, !simulation.ref<i8> -> !simulation.process
      %writer = simulation.spawn @writer(%ctx, %output) : !simulation.context, !simulation.ref<i8> -> !simulation.process
      simulation.return
    }
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.ref<!outer> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %index: !simulation.ref<i32> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %output: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %outer = simulation.ref.subelement %input[[1]] : !simulation.ref<!outer> -> !simulation.ref<!inner>
      %selector = simulation.ref.load %index : !simulation.ref<i32> -> i32
      %element = simulation.ref.array_element %outer[%selector] : (!simulation.ref<!inner>, i32) -> !simulation.ref<i8>
      %value = simulation.ref.load %element : !simulation.ref<i8> -> i8
      simulation.ref.store %value to %output : i8, !simulation.ref<i8>
      simulation.suspend.any %input, %index edges [0, 0] to ^body : !simulation.ref<!outer>, !simulation.ref<i32>
    }
    simulation.func @writer(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %zero = arith.constant 0 : i8
      simulation.ref.store %zero to %out : i8, !simulation.ref<i8>
      simulation.return
    }
  }
}

// -----

// CHECK-LABEL: simulation.design @unrelated_formal
// CHECK: simulation.func private @copy(
// CHECK: simulation.context.storage %arg0[1]
// CHECK: simulation.ref.subelement {{.*}}{{\[\[}}1]]
// CHECK: simulation.suspend.any
// KEEP-LABEL: simulation.design @unrelated_formal
// KEEP: simulation.suspend.any %arg1, %arg2 edges [0, 0]

!inner = !simulation.unpacked_array<0 : 3 x i8>
!outer = !simulation.unpacked_array<0 : 1 x !inner>
module {
  simulation.design @unrelated_formal {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !outer design
    simulation.storage.decl 1 in 0 : i32 design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.storage.decl 3 in 0 : i8 design
    simulation.code_unit.decl 3 in 0 function hierarchy "helper"
    simulation.code_unit.decl 2 in 0 continuous hierarchy "copy"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<!outer>
      %index = simulation.context.storage %ctx[1] : !simulation.ref<i32>
      %output = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %copy = simulation.spawn @copy(%ctx, %input, %index, %output) : !simulation.context, !simulation.ref<!outer>, !simulation.ref<i32>, !simulation.ref<i8> -> !simulation.process
      %scratch = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      simulation.call @helper(%ctx, %scratch) : (!simulation.context, !simulation.ref<i8>) -> ()
      simulation.return
    }
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.ref<!outer> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %index: !simulation.ref<i32> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %output: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %outer = simulation.ref.subelement %input[[1]] : !simulation.ref<!outer> -> !simulation.ref<!inner>
      %selector = simulation.ref.load %index : !simulation.ref<i32> -> i32
      %element = simulation.ref.array_element %outer[%selector] : (!simulation.ref<!inner>, i32) -> !simulation.ref<i8>
      %value = simulation.ref.load %element : !simulation.ref<i8> -> i8
      simulation.ref.store %value to %output : i8, !simulation.ref<i8>
      simulation.suspend.any %input, %index edges [0, 0] to ^body : !simulation.ref<!outer>, !simulation.ref<i32>
    }
    simulation.func private @helper(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i8> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %one = arith.constant 1 : i8
      simulation.ref.store %one to %out : i8, !simulation.ref<i8>
      simulation.return
    }
  }
}

// -----

// CHECK-LABEL: simulation.design @alias_formal
// CHECK: simulation.func private @copy(
// CHECK: simulation.suspend.any %arg1, %arg2 edges [0, 0]
// KEEP-LABEL: simulation.design @alias_formal
// KEEP: simulation.suspend.any %arg1, %arg2 edges [0, 0]

!inner = !simulation.unpacked_array<0 : 3 x i8>
!outer = !simulation.unpacked_array<0 : 1 x !inner>
module {
  simulation.design @alias_formal {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !outer design
    simulation.storage.decl 1 in 0 : i32 design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.storage.decl 3 in 0 : i8 design
    simulation.code_unit.decl 3 in 0 function hierarchy "helper"
    simulation.code_unit.decl 2 in 0 continuous hierarchy "copy"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<!outer>
      %index = simulation.context.storage %ctx[1] : !simulation.ref<i32>
      %output = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %copy = simulation.spawn @copy(%ctx, %input, %index, %output) : !simulation.context, !simulation.ref<!outer>, !simulation.ref<i32>, !simulation.ref<i8> -> !simulation.process
      %scratch = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      simulation.call @helper(%ctx, %scratch) : (!simulation.context, !simulation.ref<i8>) -> ()
      simulation.return
    }
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.ref<!outer> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %index: !simulation.ref<i32> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %output: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %outer = simulation.ref.subelement %input[[1]] : !simulation.ref<!outer> -> !simulation.ref<!inner>
      %selector = simulation.ref.load %index : !simulation.ref<i32> -> i32
      %element = simulation.ref.array_element %outer[%selector] : (!simulation.ref<!inner>, i32) -> !simulation.ref<i8>
      %value = simulation.ref.load %element : !simulation.ref<i8> -> i8
      simulation.ref.store %value to %output : i8, !simulation.ref<i8>
      simulation.suspend.any %input, %index edges [0, 0] to ^body : !simulation.ref<!outer>, !simulation.ref<i32>
    }
    simulation.func private @helper(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i8> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %one = arith.constant 1 : i8
      simulation.ref.store %one to %out : i8, !simulation.ref<i8>
      simulation.return
    }
  }
}

// -----

// CHECK-LABEL: simulation.design @external_formal
// CHECK: simulation.func private @copy(
// CHECK: simulation.suspend.any %arg1, %arg2 edges [0, 0]
// KEEP-LABEL: simulation.design @external_formal
// KEEP: simulation.suspend.any %arg1, %arg2 edges [0, 0]

!inner = !simulation.unpacked_array<0 : 3 x i8>
!outer = !simulation.unpacked_array<0 : 1 x !inner>
module {
  simulation.design @external_formal {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !outer design
    simulation.storage.decl 1 in 0 : i32 design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.storage.decl 3 in 0 : i8 design
    simulation.code_unit.decl 3 in 0 function hierarchy "helper"
    simulation.code_unit.decl 2 in 0 continuous hierarchy "copy"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<!outer>
      %index = simulation.context.storage %ctx[1] : !simulation.ref<i32>
      %output = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %copy = simulation.spawn @copy(%ctx, %input, %index, %output) : !simulation.context, !simulation.ref<!outer>, !simulation.ref<i32>, !simulation.ref<i8> -> !simulation.process
      %scratch = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      simulation.call @helper(%ctx, %scratch) : (!simulation.context, !simulation.ref<i8>) -> ()
      simulation.return
    }
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.ref<!outer> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %index: !simulation.ref<i32> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %output: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %outer = simulation.ref.subelement %input[[1]] : !simulation.ref<!outer> -> !simulation.ref<!inner>
      %selector = simulation.ref.load %index : !simulation.ref<i32> -> i32
      %element = simulation.ref.array_element %outer[%selector] : (!simulation.ref<!inner>, i32) -> !simulation.ref<i8>
      %value = simulation.ref.load %element : !simulation.ref<i8> -> i8
      simulation.ref.store %value to %output : i8, !simulation.ref<i8>
      simulation.suspend.any %input, %index edges [0, 0] to ^body : !simulation.ref<!outer>, !simulation.ref<i32>
    }
    simulation.func  @helper(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i8> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %one = arith.constant 1 : i8
      simulation.ref.store %one to %out : i8, !simulation.ref<i8>
      simulation.return
    }
  }
}

// -----

// CHECK-LABEL: simulation.design @override
// CHECK: simulation.suspend.any %arg1, %arg2 edges [0, 0]
// KEEP-LABEL: simulation.design @override
// KEEP: simulation.suspend.any %arg1, %arg2 edges [0, 0]

!inner = !simulation.unpacked_array<0 : 3 x i8>
!outer = !simulation.unpacked_array<0 : 1 x !inner>
module {
  simulation.design @override {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !outer design
    simulation.storage.decl 1 in 0 : i32 design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 continuous hierarchy "copy"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<!outer>
      %index = simulation.context.storage %ctx[1] : !simulation.ref<i32>
      %output = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %copy = simulation.spawn @copy(%ctx, %input, %index, %output) : !simulation.context, !simulation.ref<!outer>, !simulation.ref<i32>, !simulation.ref<i8> -> !simulation.process
      %force = arith.constant 0 : i8
      simulation.override %output = %force assign false : !simulation.ref<i8>, i8
      simulation.return
    }
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.ref<!outer> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %index: !simulation.ref<i32> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %output: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %outer = simulation.ref.subelement %input[[1]] : !simulation.ref<!outer> -> !simulation.ref<!inner>
      %selector = simulation.ref.load %index : !simulation.ref<i32> -> i32
      %element = simulation.ref.array_element %outer[%selector] : (!simulation.ref<!inner>, i32) -> !simulation.ref<i8>
      %value = simulation.ref.load %element : !simulation.ref<i8> -> i8
      simulation.ref.store %value to %output : i8, !simulation.ref<i8>
      simulation.suspend.any %input, %index edges [0, 0] to ^body : !simulation.ref<!outer>, !simulation.ref<i32>
    }
  }
}

// -----

// CHECK-LABEL: simulation.design @erased_write
// CHECK: simulation.suspend.any %arg1, %arg2 edges [0, 0]
// KEEP-LABEL: simulation.design @erased_write
// KEEP: simulation.suspend.any %arg1, %arg2 edges [0, 0]

!inner = !simulation.unpacked_array<0 : 3 x i8>
!outer = !simulation.unpacked_array<0 : 1 x !inner>
module {
  simulation.design @erased_write {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !outer design
    simulation.storage.decl 1 in 0 : i32 design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.storage.decl 3 in 0 : i8 design
    simulation.code_unit.decl 3 in 0 function hierarchy "helper"
    simulation.code_unit.decl 2 in 0 continuous hierarchy "copy"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<!outer>
      %index = simulation.context.storage %ctx[1] : !simulation.ref<i32>
      %output = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %copy = simulation.spawn @copy(%ctx, %input, %index, %output) : !simulation.context, !simulation.ref<!outer>, !simulation.ref<i32>, !simulation.ref<i8> -> !simulation.process
      %scratch = simulation.context.storage %ctx[3] : !simulation.ref<i8>
      simulation.call @helper(%ctx, %scratch) : (!simulation.context, !simulation.ref<i8>) -> ()
      simulation.return
    }
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.ref<!outer> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %index: !simulation.ref<i32> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %output: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %outer = simulation.ref.subelement %input[[1]] : !simulation.ref<!outer> -> !simulation.ref<!inner>
      %selector = simulation.ref.load %index : !simulation.ref<i32> -> i32
      %element = simulation.ref.array_element %outer[%selector] : (!simulation.ref<!inner>, i32) -> !simulation.ref<i8>
      %value = simulation.ref.load %element : !simulation.ref<i8> -> i8
      simulation.ref.store %value to %output : i8, !simulation.ref<i8>
      simulation.suspend.any %input, %index edges [0, 0] to ^body : !simulation.ref<!outer>, !simulation.ref<i32>
    }
    simulation.func private @helper(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %out: !simulation.ref<i8> {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %one = arith.constant 1 : i8
      %erased = simulation.argument_ref.from_ref %out : !simulation.ref<i8> -> !simulation.argument_ref<i8>
      simulation.argument_ref.store %one to %erased : i8, !simulation.argument_ref<i8>
      simulation.return
    }
  }
}

// -----

// Retain this entry kind and its original watch contract.
// CHECK-LABEL: simulation.design @port_input
// CHECK: simulation.suspend.change %{{.*}} to ^bb{{[0-9]+}}
// KEEP-LABEL: simulation.design @port_input
// KEEP: simulation.suspend.change %{{.*}} to ^bb{{[0-9]+}} : !simulation.ref<i64>
module {
  simulation.design @port_input {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i64 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 port_input hierarchy "copy"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %copy = simulation.spawn @copy(%ctx) : !simulation.context -> !simulation.process

      simulation.return
    }
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 9 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i64>
      %output = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %part = simulation.ref.extract %input from 12 : !simulation.ref<i64> -> !simulation.ref<i8>
      %value = simulation.ref.load %part : !simulation.ref<i8> -> i8
      simulation.ref.store %value to %output : i8, !simulation.ref<i8>
      simulation.suspend.change %input to ^body : !simulation.ref<i64>
    }
  }
}

// -----

// Retain this entry kind and its original watch contract.
// CHECK-LABEL: simulation.design @port_output
// CHECK: simulation.suspend.change %{{.*}} to ^bb{{[0-9]+}}
// KEEP-LABEL: simulation.design @port_output
// KEEP: simulation.suspend.change %{{.*}} to ^bb{{[0-9]+}} : !simulation.ref<i64>
module {
  simulation.design @port_output {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i64 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 port_output hierarchy "copy"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %copy = simulation.spawn @copy(%ctx) : !simulation.context -> !simulation.process

      simulation.return
    }
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 10 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i64>
      %output = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %part = simulation.ref.extract %input from 12 : !simulation.ref<i64> -> !simulation.ref<i8>
      %value = simulation.ref.load %part : !simulation.ref<i8> -> i8
      simulation.ref.store %value to %output : i8, !simulation.ref<i8>
      simulation.suspend.change %input to ^body : !simulation.ref<i64>
    }
  }
}

// -----

// Retain this entry kind and its original watch contract.
// CHECK-LABEL: simulation.design @latch
// CHECK: simulation.suspend.change %{{.*}} to ^bb{{[0-9]+}}
// KEEP-LABEL: simulation.design @latch
// KEEP: simulation.suspend.change %{{.*}} to ^bb{{[0-9]+}} : !simulation.ref<i64>
module {
  simulation.design @latch {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i64 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always_latch hierarchy "copy"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %copy = simulation.spawn @copy(%ctx) : !simulation.context -> !simulation.process

      simulation.return
    }
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 6 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i64>
      %output = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %part = simulation.ref.extract %input from 12 : !simulation.ref<i64> -> !simulation.ref<i8>
      %value = simulation.ref.load %part : !simulation.ref<i8> -> i8
      simulation.ref.store %value to %output : i8, !simulation.ref<i8>
      simulation.suspend.change %input to ^body : !simulation.ref<i64>
    }
  }
}

// -----

// A packed slice becomes a typed view; change is replaced by any.
// CHECK-LABEL: simulation.design @packed_slice
// CHECK: simulation.ref.store
// CHECK: %[[BASE:.*]] = simulation.context.storage %arg0[0]
// CHECK: %[[SLICE:.*]] = simulation.ref.extract %[[BASE]] from 12 : !simulation.ref<i64> -> !simulation.ref<i8>
// CHECK: simulation.suspend.any %[[SLICE]] edges [0] to ^bb{{[0-9]+}}
// KEEP-LABEL: simulation.design @packed_slice
// KEEP: simulation.suspend.change %{{.*}} to ^bb{{[0-9]+}} : !simulation.ref<i64>
module {
  simulation.design @packed_slice {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i64 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 continuous hierarchy "copy"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %copy = simulation.spawn @copy(%ctx) : !simulation.context -> !simulation.process

      simulation.return
    }
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i64>
      %output = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %part = simulation.ref.extract %input from 12 : !simulation.ref<i64> -> !simulation.ref<i8>
      %value = simulation.ref.load %part : !simulation.ref<i8> -> i8
      simulation.ref.store %value to %output : i8, !simulation.ref<i8>
      simulation.suspend.change %input to ^body : !simulation.ref<i64>
    }
  }
}

// -----

// Merge adjacent and duplicate reads, retaining separated intervals.
// CHECK-LABEL: simulation.design @merged_ranges
// CHECK: simulation.ref.store
// CHECK: %[[BASE:.*]] = simulation.context.storage %arg0[0]
// CHECK: %[[AB:.*]] = simulation.ref.extract %[[BASE]] from 4 : !simulation.ref<i64> -> !simulation.ref<i16>
// CHECK: %[[C:.*]] = simulation.ref.extract %[[BASE]] from 40 : !simulation.ref<i64> -> !simulation.ref<i8>
// CHECK: simulation.suspend.any %[[AB]], %[[C]] edges [0, 0]
// KEEP-LABEL: simulation.design @merged_ranges
// KEEP: simulation.suspend.change %{{.*}} to ^bb{{[0-9]+}} : !simulation.ref<i64>
module {
  simulation.design @merged_ranges {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i64 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 continuous hierarchy "copy"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %copy = simulation.spawn @copy(%ctx) : !simulation.context -> !simulation.process

      simulation.return
    }
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i64>
      %output = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %a = simulation.ref.extract %input from 4 : !simulation.ref<i64> -> !simulation.ref<i8>
      %b = simulation.ref.extract %input from 12 : !simulation.ref<i64> -> !simulation.ref<i8>
      %c = simulation.ref.extract %input from 40 : !simulation.ref<i64> -> !simulation.ref<i8>
      %av = simulation.ref.load %a : !simulation.ref<i8> -> i8
      %bv = simulation.ref.load %b : !simulation.ref<i8> -> i8
      %cv = simulation.ref.load %c : !simulation.ref<i8> -> i8
      %again = simulation.ref.load %a : !simulation.ref<i8> -> i8
      %ab = arith.xori %av, %bv : i8
      %ac = arith.xori %cv, %again : i8
      %value = arith.xori %ab, %ac : i8
      simulation.ref.store %value to %output : i8, !simulation.ref<i8>
      simulation.suspend.change %input to ^body : !simulation.ref<i64>
    }
  }
}

// -----

// Multiple writes per activation require a separate ordering proof.
// CHECK-LABEL: simulation.design @two_stores
// CHECK: simulation.suspend.change %{{.*}} to ^bb{{[0-9]+}}
// KEEP-LABEL: simulation.design @two_stores
// KEEP: simulation.suspend.change %{{.*}} to ^bb{{[0-9]+}} : !simulation.ref<i64>
module {
  simulation.design @two_stores {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i64 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 continuous hierarchy "copy"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %copy = simulation.spawn @copy(%ctx) : !simulation.context -> !simulation.process

      simulation.return
    }
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i64>
      %output = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %part = simulation.ref.extract %input from 12 : !simulation.ref<i64> -> !simulation.ref<i8>
      %value = simulation.ref.load %part : !simulation.ref<i8> -> i8
      simulation.ref.store %value to %output : i8, !simulation.ref<i8>
      simulation.ref.store %value to %output : i8, !simulation.ref<i8>
      simulation.suspend.change %input to ^body : !simulation.ref<i64>
    }
  }
}

// -----

// A refinement must never add a dependency absent from the old watch.
// CHECK-LABEL: simulation.design @uncovered_read
// CHECK: simulation.suspend.change %{{.*}} to ^bb{{[0-9]+}}
// KEEP-LABEL: simulation.design @uncovered_read
// KEEP: simulation.suspend.change %{{.*}} to ^bb{{[0-9]+}} : !simulation.ref<i8>
module {
  simulation.design @uncovered_read {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i64 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 continuous hierarchy "copy"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %copy = simulation.spawn @copy(%ctx) : !simulation.context -> !simulation.process

      simulation.return
    }
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i64>
      %output = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %part = simulation.ref.extract %input from 12 : !simulation.ref<i64> -> !simulation.ref<i8>
      %value = simulation.ref.load %part : !simulation.ref<i8> -> i8
      simulation.ref.store %value to %output : i8, !simulation.ref<i8>
      simulation.suspend.change %output to ^body : !simulation.ref<i8>
    }
  }
}

// -----

// Edge-triggered waits cannot be replaced with change sensitivity.
// CHECK-LABEL: simulation.design @edge_watch
// CHECK: simulation.suspend.any %{{.*}} edges [1]
// KEEP-LABEL: simulation.design @edge_watch
// KEEP: simulation.suspend.any %{{.*}} edges [1] to ^bb{{[0-9]+}} : !simulation.ref<i64>
module {
  simulation.design @edge_watch {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i64 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 continuous hierarchy "copy"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %copy = simulation.spawn @copy(%ctx) : !simulation.context -> !simulation.process

      simulation.return
    }
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i64>
      %output = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %part = simulation.ref.extract %input from 12 : !simulation.ref<i64> -> !simulation.ref<i8>
      %value = simulation.ref.load %part : !simulation.ref<i8> -> i8
      simulation.ref.store %value to %output : i8, !simulation.ref<i8>
      simulation.suspend.any %input edges [1] to ^body : !simulation.ref<i64>
    }
  }
}

// -----

// A root initialization before any barrier does not add an active writer.
// CHECK-LABEL: simulation.design @initial_write
// CHECK: simulation.func private @copy
// CHECK: simulation.suspend.any
// KEEP-LABEL: simulation.design @initial_write
// KEEP: simulation.suspend.change %{{.*}} to ^bb{{[0-9]+}} : !simulation.ref<i64>
module {
  simulation.design @initial_write {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i64 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 continuous hierarchy "copy"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %copy = simulation.spawn @copy(%ctx) : !simulation.context -> !simulation.process
      %out = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %zero = arith.constant 0 : i8
      simulation.ref.store %zero to %out : i8, !simulation.ref<i8>
      simulation.return
    }
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i64>
      %output = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %part = simulation.ref.extract %input from 12 : !simulation.ref<i64> -> !simulation.ref<i8>
      %value = simulation.ref.load %part : !simulation.ref<i8> -> i8
      simulation.ref.store %value to %output : i8, !simulation.ref<i8>
      simulation.suspend.change %input to ^body : !simulation.ref<i64>
    }
  }
}

// -----

// A root store after a suspension can restore the output independently.
// CHECK-LABEL: simulation.design @late_root_write
// CHECK: simulation.suspend.change %{{.*}} to ^bb{{[0-9]+}}
// KEEP-LABEL: simulation.design @late_root_write
// KEEP: simulation.suspend.change %{{.*}} to ^bb{{[0-9]+}} : !simulation.ref<i64>
module {
  simulation.design @late_root_write {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i64 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 continuous hierarchy "copy"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %copy = simulation.spawn @copy(%ctx) : !simulation.context -> !simulation.process
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^late
    ^late:
      %out = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %zero = arith.constant 0 : i8
      simulation.ref.store %zero to %out : i8, !simulation.ref<i8>
      simulation.return
    }
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i64>
      %output = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %part = simulation.ref.extract %input from 12 : !simulation.ref<i64> -> !simulation.ref<i8>
      %value = simulation.ref.load %part : !simulation.ref<i8> -> i8
      simulation.ref.store %value to %output : i8, !simulation.ref<i8>
      simulation.suspend.change %input to ^body : !simulation.ref<i64>
    }
  }
}

// -----

// A single symbol use does not prove a mandatory initial activation.
// CHECK-LABEL: simulation.design @conditional_spawn
// CHECK: simulation.suspend.change %{{.*}} to ^bb{{[0-9]+}}
// KEEP-LABEL: simulation.design @conditional_spawn
// KEEP: simulation.suspend.change %{{.*}} to ^bb{{[0-9]+}} : !simulation.ref<i64>
module {
  simulation.design @conditional_spawn {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i64 design
    simulation.storage.decl 1 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 continuous hierarchy "copy"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i64>
      %value = simulation.ref.load %input : !simulation.ref<i64> -> i64
      %zero = arith.constant 0 : i64
      %condition = arith.cmpi eq, %value, %zero : i64
      cf.cond_br %condition, ^spawn, ^exit
    ^spawn:
      %copy = simulation.spawn @copy(%ctx) : !simulation.context -> !simulation.process
      cf.br ^exit
    ^exit:

      simulation.return
    }
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %input = simulation.context.storage %ctx[0] : !simulation.ref<i64>
      %output = simulation.context.storage %ctx[1] : !simulation.ref<i8>
      %part = simulation.ref.extract %input from 12 : !simulation.ref<i64> -> !simulation.ref<i8>
      %value = simulation.ref.load %part : !simulation.ref<i8> -> i8
      simulation.ref.store %value to %output : i8, !simulation.ref<i8>
      simulation.suspend.change %input to ^body : !simulation.ref<i64>
    }
  }
}

// -----

// Array view planning visits intersecting children, independent of root size.
// CHECK-LABEL: simulation.design @large_array
// KEEP-LABEL: simulation.design @large_array
// The dynamic inner selection watches the entire fixed outer element, plus
// the index. A runtime selector must never be frozen into the watch identity.
!inner = !simulation.unpacked_array<0 : 3 x i8>
!outer = !simulation.unpacked_array<0 : 999999 x !inner>
module {
  simulation.design @large_array {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !outer design
    simulation.storage.decl 1 in 0 : i32 design
    simulation.storage.decl 2 in 0 : i8 design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 continuous hierarchy "copy"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[0] : !simulation.ref<!outer>
      %index = simulation.context.storage %ctx[1] : !simulation.ref<i32>
      %output = simulation.context.storage %ctx[2] : !simulation.ref<i8>
      %copy = simulation.spawn @copy(%ctx, %input, %index, %output) : !simulation.context, !simulation.ref<!outer>, !simulation.ref<i32>, !simulation.ref<i8> -> !simulation.process
      simulation.return
    }
    // CHECK-LABEL: simulation.func private @copy(
    // CHECK: simulation.ref.array_element
    // CHECK: simulation.ref.store
    // CHECK: %[[INDEX:.*]] = simulation.context.storage %arg0[1]
    // CHECK: %[[ROOT:.*]] = simulation.context.storage %arg0[0]
    // CHECK: %[[OUTER:.*]] = simulation.ref.subelement %[[ROOT]]{{\[\[}}999999]]
    // CHECK: simulation.suspend.any %[[INDEX]], %[[OUTER]] edges [0, 0]
    // KEEP-LABEL: simulation.func private @copy(
    // KEEP: simulation.suspend.any %arg1, %arg2 edges [0, 0]
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !simulation.ref<!outer> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %index: !simulation.ref<i32> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %output: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 7 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %outer = simulation.ref.subelement %input[[999999]] : !simulation.ref<!outer> -> !simulation.ref<!inner>
      %selector = simulation.ref.load %index : !simulation.ref<i32> -> i32
      %element = simulation.ref.array_element %outer[%selector] : (!simulation.ref<!inner>, i32) -> !simulation.ref<i8>
      %value = simulation.ref.load %element : !simulation.ref<i8> -> i8
      simulation.ref.store %value to %output : i8, !simulation.ref<i8>
      simulation.suspend.any %input, %index edges [0, 0] to ^body : !simulation.ref<!outer>, !simulation.ref<i32>
    }
  }
}

// -----

// A packed slice becomes a typed view; change is replaced by any.
// CHECK-LABEL: simulation.design @logic_slice
// CHECK: simulation.ref.store
// CHECK: %[[BASE:.*]] = simulation.context.storage %arg0[0]
// CHECK: %[[SLICE:.*]] = simulation.ref.extract %[[BASE]] from 12 : !simulation.ref<!simulation.logic<64>> -> !simulation.ref<!simulation.logic<8>>
// CHECK: simulation.suspend.any %[[SLICE]] edges [0] to ^bb{{[0-9]+}}
// KEEP-LABEL: simulation.design @logic_slice
// KEEP: simulation.suspend.change %{{.*}} to ^bb{{[0-9]+}} : !simulation.ref<!simulation.logic<64>>
module {
  simulation.design @logic_slice {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.logic<64> design
    simulation.storage.decl 1 in 0 : !simulation.logic<8> design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 continuous hierarchy "copy"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %copy = simulation.spawn @copy(%ctx) : !simulation.context -> !simulation.process

      simulation.return
    }
    simulation.func private @copy(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 7 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %input = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<64>>
      %output = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<8>>
      %part = simulation.ref.extract %input from 12 : !simulation.ref<!simulation.logic<64>> -> !simulation.ref<!simulation.logic<8>>
      %value = simulation.ref.load %part : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.ref.store %value to %output : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      simulation.suspend.change %input to ^body : !simulation.ref<!simulation.logic<64>>
    }
  }
}
