// RUN: obelisk-opt %s -o /dev/null \
// RUN:   --pass-pipeline='builtin.module(test-obelisk-simulation-process-frame-analysis)' \
// RUN:   2>&1 | FileCheck %s

!untagged = !simulation.unpacked_union<fields = [
  #simulation.field<name = "object", type = !simulation.class_handle<@Node>, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "bits", type = !simulation.logic<64>, ordinal = 1, packedOffset = 0>
], isTagged = false>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128"
} {
  simulation.design @frame_analysis {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 initial hierarchy "untagged_frame"
    simulation.class.decl @Node id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.func @untagged_frame(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %value: !untagged
            {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      simulation.return
    }
  }
}

// IEEE 1800-2017 7.3: process frames preserve the overlapping union value and
// unknown planes while tracing the managed word only as a validated candidate.
// CHECK: frame @untagged_frame size=16 align=8 checksum=
// CHECK: capture1 value=0 unknown=8 size=8 align=8 roots=0 candidate-roots=0:1
// CHECK: field capture candidate-root offset=0 size=8 align=8 kinds=1
// CHECK: field capture four-state-unknown offset=8 size=8 align=8
