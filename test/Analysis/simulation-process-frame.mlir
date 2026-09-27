// RUN: obelisk-opt %s -o /dev/null \
// RUN:   --pass-pipeline='builtin.module(test-obelisk-simulation-process-frame-analysis)' \
// RUN:   2>&1 | FileCheck %s
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | %python %S/../Conversion/Inputs/dump-bytecode-instructions.py | FileCheck %s --check-prefix=BYTECODE

// This test owns the process-frame contract directly; the native RUN also
// checks that lowering preserves the analysis's alignment guarantees.

!tagged = !simulation.unpacked_union<fields = [
  #simulation.field<name = "object", type = !simulation.class_handle<@Node>, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "bits", type = i32, ordinal = 1, packedOffset = 0>
], isTagged = true>

!untagged = !simulation.unpacked_union<fields = [
  #simulation.field<name = "object", type = !simulation.class_handle<@Node>, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "text", type = !simulation.string, ordinal = 1, packedOffset = 0>,
  #simulation.field<name = "bits", type = i64, ordinal = 2, packedOffset = 0>
], isTagged = false>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @frame_analysis {
    simulation.code_unit.decl 1 in 0 initial hierarchy "frame"
    simulation.code_unit.decl 2 in 0 initial hierarchy "tagged_frame"
    simulation.code_unit.decl 3 in 0 initial hierarchy "wide_logic_frame"
    simulation.code_unit.decl 4 in 0 initial hierarchy "untagged_frame"
    simulation.scope.decl 0
    simulation.class.decl @Node id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }

    simulation.func @frame(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %logic: !simulation.logic<5>
            {simulation.capture_kind = 2 : i32},
        %wide: i64
            {simulation.capture_kind = 2 : i32},
        %choose: i1
            {simulation.capture_kind = 2 : i32},
        %ref: !simulation.ref<i8>
            {simulation.capture_kind = 1 : i32},
        %text: !simulation.string
            {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %delay = simulation.time.constant 1
      cf.cond_br %choose, ^wait_logic, ^wait_wide
    ^wait_logic:
      simulation.suspend.delay %delay to ^resume_logic(
          %logic : !simulation.logic<5>)
    ^wait_wide:
      simulation.suspend.change %ref to ^resume_wide(
          %wide : i64) : !simulation.ref<i8>
    ^resume_logic(%logic_value: !simulation.logic<5>):
      simulation.return
    ^resume_wide(%wide_value: i64):
      simulation.return
    }

    simulation.func @tagged_frame(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %tagged: !tagged
            {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^resume(%tagged : !tagged)
    ^resume(%value: !tagged):
      simulation.return
    }

    simulation.func @wide_logic_frame(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %wide: !simulation.logic<130>
            {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      simulation.return
    }

    simulation.func @untagged_frame(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %value: !untagged
            {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^resume(%value : !untagged)
    ^resume(%continued: !untagged):
      simulation.return
    }
  }
}

// CHECK: frame @frame size=
// CHECK-NEXT: capture0 context
// CHECK-NEXT: capture1 value=0 unknown=1 size=1 align=1
// CHECK-NEXT: capture2 value=8 size=8 align=8
// CHECK-NEXT: capture3 value=16 size=1 align=1
// CHECK-NEXT: capture4 value=24 size=8 align=8
// CHECK-NEXT: capture5 value=32 size=8 align=8 roots=0
// CHECK-NEXT: field capture four-state-value offset=0 size=1 align=1
// CHECK-NEXT: field capture four-state-unknown offset=1 size=1 align=1
// CHECK-NEXT: field capture none offset=8 size=8 align=8
// CHECK-NEXT: field capture none offset=16 size=1 align=1
// CHECK-NEXT: field capture none offset=24 size=8 align=8
// CHECK-NEXT: field capture managed-root offset=32 size=8 align=8
// CHECK-NEXT: field continuation four-state-value offset=40 size=8 align=8
// CHECK-NEXT: field continuation four-state-unknown offset=48 size=8 align=8
// CHECK-NEXT: field wait none offset=56 size=
// CHECK-NEXT: continuations=0, 1, 2
// CHECK-NEXT: suspend simulation.suspend.delay id=1 bb=3 wait=56+
// CHECK-NEXT: arg0 value=40 unknown=48 size=1 align=1
// CHECK-NEXT: suspend simulation.suspend.change id=2 bb=4 wait=56+
// CHECK-NEXT: arg0 value=40 size=8 align=8
// CHECK-NEXT: frame @tagged_frame size=
// CHECK-NEXT: capture0 context
// CHECK-NEXT: capture1 value=0 size=17 align=8 roots=0
// CHECK-NEXT: field capture managed-root offset=0 size=8 align=8
// CHECK-NEXT: field continuation managed-root offset={{[0-9]+}} size=8 align=8
// CHECK-NEXT: field wait none offset={{[0-9]+}} size=
// CHECK-NEXT: continuations=0, 1
// CHECK-NEXT: suspend simulation.suspend.delay id=1 bb=1 wait={{[0-9]+}}+
// CHECK-NEXT: arg0 value={{[0-9]+}} size=17 align=8 roots=0
// CHECK-NEXT: frame @untagged_frame size=
// CHECK-NEXT: capture0 context
// CHECK-NEXT: capture1 value=0 size=8 align=8 roots=0 candidate-roots=0:3
// CHECK-NEXT: field capture candidate-root offset=0 size=8 align=8 kinds=3
// CHECK-NEXT: field continuation candidate-root offset={{[0-9]+}} size=8 align=8 kinds=3
// CHECK-NEXT: field wait none offset={{[0-9]+}} size=
// CHECK-NEXT: continuations=0, 1
// CHECK-NEXT: suspend simulation.suspend.delay id=1 bb=1 wait={{[0-9]+}}+
// CHECK-NEXT: arg0 value={{[0-9]+}} size=8 align=8 roots=0 candidate-roots=0:3
// CHECK-NEXT: frame @wide_logic_frame size=48 align=8 checksum=
// CHECK-NEXT: capture0 context
// CHECK-NEXT: capture1 value=0 unknown=24 size=17 align=8
// CHECK-NEXT: field capture four-state-value offset=0 size=17 align=8
// CHECK-NEXT: field capture four-state-unknown offset=24 size=17 align=8
// CHECK-NEXT: continuations=0

// NATIVE-LABEL: llvm.func @wide_logic_frame(
// NATIVE: llvm.load %{{.*}} {alignment = 8 : i64} : !llvm.ptr -> i130

// Candidate roots remain visible while the untagged union is captured and
// while its continuation value resides in a suspended caller frame. The
// destination field carries the allowed class|string kind mask.
// BYTECODE: opcode=55 flags=1 dst=3 {{.*}} imm=0
// BYTECODE: opcode=42 flags=1 dst=3 {{.*}} imm=8
// NATIVE: llvm.getelementptr %{{.*}}[24] : (!llvm.ptr) -> !llvm.ptr, i8
// NATIVE: llvm.load %{{.*}} {alignment = 8 : i64} : !llvm.ptr -> i130
