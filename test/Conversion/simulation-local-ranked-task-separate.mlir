// RUN: obelisk-opt %s --split-input-file --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' | FileCheck %s

// Variants of simulation-local-ranked-task.mlir that must keep both processes
// out of a ranked region kernel. Each is the reduced combinational chain
// first -> second, which forms a ranked region kernel, plus one change.

// IEEE 1800-2023 9.2.2.3: always_latch processes hold state, so they are not
// combinational region members.
// CHECK-LABEL: simulation.design @latch
// CHECK-NOT: __obelisk_region_kernel_
// CHECK: simulation.func @first(
// CHECK: simulation.func @second(
// CHECK-NOT: __obelisk_region_kernel_

!word = !simulation.logic<8>
!ref = !simulation.ref<!word>
module attributes {schedule.native_scheduler = 1 : i32} {
  simulation.design @latch {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !word design
    simulation.storage.decl 1 in 0 : !word design
    simulation.storage.decl 2 in 0 : !word design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always_latch hierarchy "first"
    simulation.code_unit.decl 3 in 0 always_latch hierarchy "second"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[0] : !ref
      %middle = simulation.context.storage %ctx[1] : !ref
      %output = simulation.context.storage %ctx[2] : !ref
      %a = simulation.spawn @first(%ctx, %input, %middle) : !simulation.context, !ref, !ref -> !simulation.process
      %b = simulation.spawn @second(%ctx, %input, %middle, %output) : !simulation.context, !ref, !ref, !ref -> !simulation.process
      simulation.return
    }
    simulation.func @first(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %output: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 6 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %value = simulation.ref.load %input : !ref -> !word
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !word
      %next = simulation.logic.binary add %value, %one : !word
      simulation.ref.store %next to %output : !word, !ref
      simulation.suspend.change %input to ^body : !ref
    }
    simulation.func @second(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %middle: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %output: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 6 : i32, code_unit_id = 3 : i64} {
      cf.br ^body
    ^body:
      %a = simulation.ref.load %input : !ref -> !word
      %b = simulation.ref.load %middle : !ref -> !word
      %next = simulation.logic.binary add %a, %b : !word
      simulation.ref.store %next to %output : !word, !ref
      simulation.suspend.any %input, %middle edges [0, 0] to ^body : !ref, !ref
    }
  }
}

// -----

// Processes that store their output twice per activation stay separate.
// CHECK-LABEL: simulation.design @repeated
// CHECK-NOT: __obelisk_region_kernel_
// CHECK: simulation.func @first(
// CHECK: simulation.func @second(
// CHECK-NOT: __obelisk_region_kernel_

!word = !simulation.logic<8>
!ref = !simulation.ref<!word>
module attributes {schedule.native_scheduler = 1 : i32} {
  simulation.design @repeated {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !word design
    simulation.storage.decl 1 in 0 : !word design
    simulation.storage.decl 2 in 0 : !word design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "first"
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "second"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[0] : !ref
      %middle = simulation.context.storage %ctx[1] : !ref
      %output = simulation.context.storage %ctx[2] : !ref
      %a = simulation.spawn @first(%ctx, %input, %middle) : !simulation.context, !ref, !ref -> !simulation.process
      %b = simulation.spawn @second(%ctx, %input, %middle, %output) : !simulation.context, !ref, !ref, !ref -> !simulation.process
      simulation.return
    }
    simulation.func @first(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %output: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %value = simulation.ref.load %input : !ref -> !word
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !word
      %next = simulation.logic.binary add %value, %one : !word
      simulation.ref.store %next to %output : !word, !ref
      simulation.ref.store %next to %output : !word, !ref
      simulation.suspend.change %input to ^body : !ref
    }
    simulation.func @second(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %middle: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %output: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      cf.br ^body
    ^body:
      %a = simulation.ref.load %input : !ref -> !word
      %b = simulation.ref.load %middle : !ref -> !word
      %next = simulation.logic.binary add %a, %b : !word
      simulation.ref.store %next to %output : !word, !ref
      simulation.ref.store %next to %output : !word, !ref
      simulation.suspend.any %input, %middle edges [0, 0] to ^body : !ref, !ref
    }
  }
}

// -----

// A process that reads its own output feeds back into itself.
// CHECK-LABEL: simulation.design @retained
// CHECK-NOT: __obelisk_region_kernel_
// CHECK: simulation.func @first(
// CHECK: simulation.func @second(
// CHECK-NOT: __obelisk_region_kernel_

!word = !simulation.logic<8>
!ref = !simulation.ref<!word>
module attributes {schedule.native_scheduler = 1 : i32} {
  simulation.design @retained {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !word design
    simulation.storage.decl 1 in 0 : !word design
    simulation.storage.decl 2 in 0 : !word design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "first"
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "second"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[0] : !ref
      %middle = simulation.context.storage %ctx[1] : !ref
      %output = simulation.context.storage %ctx[2] : !ref
      %a = simulation.spawn @first(%ctx, %input, %middle) : !simulation.context, !ref, !ref -> !simulation.process
      %b = simulation.spawn @second(%ctx, %input, %middle, %output) : !simulation.context, !ref, !ref, !ref -> !simulation.process
      simulation.return
    }
    simulation.func @first(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %output: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %value = simulation.ref.load %output : !ref -> !word
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !word
      %next = simulation.logic.binary add %value, %one : !word
      simulation.ref.store %next to %output : !word, !ref
      simulation.suspend.change %input to ^body : !ref
    }
    simulation.func @second(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %middle: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %output: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      cf.br ^body
    ^body:
      %a = simulation.ref.load %input : !ref -> !word
      %b = simulation.ref.load %middle : !ref -> !word
      %next = simulation.logic.binary add %a, %b : !word
      simulation.ref.store %next to %output : !word, !ref
      simulation.suspend.any %input, %middle edges [0, 0] to ^body : !ref, !ref
    }
  }
}

// -----

// A process that reads the chain's final output closes a feedback loop.
// CHECK-LABEL: simulation.design @feedback
// CHECK-NOT: __obelisk_region_kernel_
// CHECK: simulation.func @first(
// CHECK: simulation.func @second(
// CHECK-NOT: __obelisk_region_kernel_

!word = !simulation.logic<8>
!ref = !simulation.ref<!word>
module attributes {schedule.native_scheduler = 1 : i32} {
  simulation.design @feedback {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !word design
    simulation.storage.decl 1 in 0 : !word design
    simulation.storage.decl 2 in 0 : !word design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "first"
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "second"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[0] : !ref
      %middle = simulation.context.storage %ctx[1] : !ref
      %output = simulation.context.storage %ctx[2] : !ref
      %a = simulation.spawn @first(%ctx, %output, %middle) : !simulation.context, !ref, !ref -> !simulation.process
      %b = simulation.spawn @second(%ctx, %input, %middle, %output) : !simulation.context, !ref, !ref, !ref -> !simulation.process
      simulation.return
    }
    simulation.func @first(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}, %output: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %value = simulation.ref.load %input : !ref -> !word
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !word
      %next = simulation.logic.binary add %value, %one : !word
      simulation.ref.store %next to %output : !word, !ref
      simulation.suspend.change %input to ^body : !ref
    }
    simulation.func @second(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %middle: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %output: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      cf.br ^body
    ^body:
      %a = simulation.ref.load %input : !ref -> !word
      %b = simulation.ref.load %middle : !ref -> !word
      %next = simulation.logic.binary add %a, %b : !word
      simulation.ref.store %next to %output : !word, !ref
      simulation.suspend.any %input, %middle edges [0, 0] to ^body : !ref, !ref
    }
  }
}

// -----

// A task writing the chain's intermediate net is an unranked writer.
// CHECK-LABEL: simulation.design @task_writer
// CHECK-NOT: __obelisk_region_kernel_
// CHECK: simulation.func @first(
// CHECK: simulation.func @second(
// CHECK-NOT: __obelisk_region_kernel_

!word = !simulation.logic<8>
!ref = !simulation.ref<!word>
module attributes {schedule.native_scheduler = 1 : i32} {
  simulation.design @task_writer {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !word design
    simulation.storage.decl 1 in 0 : !word design
    simulation.storage.decl 2 in 0 : !word design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always_comb hierarchy "first"
    simulation.code_unit.decl 3 in 0 always_comb hierarchy "second"
    simulation.code_unit.decl 4 in 0 initial hierarchy "driver"
    simulation.code_unit.decl 5 in 0 task hierarchy "stimulus"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %input = simulation.context.storage %ctx[0] : !ref
      %middle = simulation.context.storage %ctx[1] : !ref
      %output = simulation.context.storage %ctx[2] : !ref
      %a = simulation.spawn @first(%ctx, %input, %middle) : !simulation.context, !ref, !ref -> !simulation.process
      %b = simulation.spawn @second(%ctx, %input, %middle, %output) : !simulation.context, !ref, !ref, !ref -> !simulation.process
      %d = simulation.spawn @driver(%ctx) : !simulation.context -> !simulation.process
      simulation.return
    }
    simulation.func @first(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %output: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %value = simulation.ref.load %input : !ref -> !word
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !word
      %next = simulation.logic.binary add %value, %one : !word
      simulation.ref.store %next to %output : !word, !ref
      simulation.suspend.change %input to ^body : !ref
    }
    simulation.func @second(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %input: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64}, %middle: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64}, %output: !ref {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      cf.br ^body
    ^body:
      %a = simulation.ref.load %input : !ref -> !word
      %b = simulation.ref.load %middle : !ref -> !word
      %next = simulation.logic.binary add %a, %b : !word
      simulation.ref.store %next to %output : !word, !ref
      simulation.suspend.any %input, %middle edges [0, 0] to ^body : !ref, !ref
    }
    simulation.func @driver(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %bufferDest = simulation.context.storage %ctx[1] : !ref
      simulation.task.call @stimulus(%ctx, %bufferDest) arguments 2 to ^done : !simulation.context, !ref
    ^done:
      simulation.return
    }
    simulation.func @stimulus(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}, %buffer: !ref {simulation.capture_kind = 1 : i32}) attributes {entry_kind = 12 : i32, code_unit_id = 5 : i64} {
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^write
    ^write:
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !word
      simulation.ref.store %one to %buffer : !word, !ref
      simulation.return
    }
  }
}
