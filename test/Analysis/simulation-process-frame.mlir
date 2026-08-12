// RUN: obelisk-opt %s -o /dev/null \
// RUN:   --pass-pipeline='builtin.module(test-obelisk-simulation-process-frame-analysis)' \
// RUN:   2>&1 | FileCheck %s
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE

// This test owns the process-frame contract directly; the native RUN also
// checks that lowering preserves the analysis's alignment guarantees.

!tagged = !obelisk_sim.unpacked_union<fields = [
  #obelisk_sim.field<name = "object", type = !obelisk_sim.class_handle<@Node>, ordinal = 0, packedOffset = 0>,
  #obelisk_sim.field<name = "bits", type = i32, ordinal = 1, packedOffset = 0>
], isTagged = true>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @frame_analysis {
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "frame"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "tagged_frame"
    obelisk_sim.code_unit.decl 3 in 0 initial hierarchy "wide_logic_frame"
    obelisk_sim.scope.decl 0
    obelisk_sim.class.decl @Node id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }

    obelisk_sim.func @frame(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %logic: !obelisk_sim.logic<5>
            {obelisk_sim.capture_kind = 2 : i32},
        %wide: i64
            {obelisk_sim.capture_kind = 2 : i32},
        %choose: i1
            {obelisk_sim.capture_kind = 2 : i32},
        %ref: !obelisk_sim.ref<i8>
            {obelisk_sim.capture_kind = 1 : i32},
        %text: !obelisk_sim.string
            {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      %delay = obelisk_sim.time.constant 1
      cf.cond_br %choose, ^wait_logic, ^wait_wide
    ^wait_logic:
      obelisk_sim.suspend.delay %delay to ^resume_logic(
          %logic : !obelisk_sim.logic<5>)
    ^wait_wide:
      obelisk_sim.suspend.change %ref to ^resume_wide(
          %wide : i64) : !obelisk_sim.ref<i8>
    ^resume_logic(%logic_value: !obelisk_sim.logic<5>):
      obelisk_sim.return
    ^resume_wide(%wide_value: i64):
      obelisk_sim.return
    }

    obelisk_sim.func @tagged_frame(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %tagged: !tagged
            {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^resume(%tagged : !tagged)
    ^resume(%value: !tagged):
      obelisk_sim.return
    }

    obelisk_sim.func @wide_logic_frame(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %wide: !obelisk_sim.logic<130>
            {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      obelisk_sim.return
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
// CHECK-NEXT: suspend obelisk_sim.suspend.delay id=1 bb=3 wait=56+
// CHECK-NEXT: arg0 value=40 unknown=48 size=1 align=1
// CHECK-NEXT: suspend obelisk_sim.suspend.change id=2 bb=4 wait=56+
// CHECK-NEXT: arg0 value=40 size=8 align=8
// CHECK-NEXT: frame @tagged_frame size=
// CHECK-NEXT: capture0 context
// CHECK-NEXT: capture1 value=0 size=17 align=8 roots=0
// CHECK-NEXT: field capture managed-root offset=0 size=8 align=8
// CHECK-NEXT: field continuation managed-root offset={{[0-9]+}} size=8 align=8
// CHECK-NEXT: field wait none offset={{[0-9]+}} size=
// CHECK-NEXT: continuations=0, 1
// CHECK-NEXT: suspend obelisk_sim.suspend.delay id=1 bb=1 wait={{[0-9]+}}+
// CHECK-NEXT: arg0 value={{[0-9]+}} size=17 align=8 roots=0
// CHECK-NEXT: frame @wide_logic_frame size=48 align=8 checksum=
// CHECK-NEXT: capture0 context
// CHECK-NEXT: capture1 value=0 unknown=24 size=17 align=8
// CHECK-NEXT: field capture four-state-value offset=0 size=17 align=8
// CHECK-NEXT: field capture four-state-unknown offset=24 size=17 align=8
// CHECK-NEXT: continuations=0

// NATIVE-LABEL: llvm.func @wide_logic_frame(
// NATIVE: llvm.load %{{.*}} {alignment = 8 : i64} : !llvm.ptr -> i130
// NATIVE: llvm.getelementptr %{{.*}}[24] : (!llvm.ptr) -> !llvm.ptr, i8
// NATIVE: llvm.load %{{.*}} {alignment = 8 : i64} : !llvm.ptr -> i130
