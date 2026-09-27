// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off' | %python %S/Inputs/dump-bytecode-instructions.py | FileCheck %s

!tagged = !simulation.unpacked_union<fields = [
  #simulation.field<name = "object", type = !simulation.class_handle<@Node>, ordinal = 0, packedOffset = 0>,
  #simulation.field<name = "bits", type = i32, ordinal = 1, packedOffset = 0>
], isTagged = true>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-i32:32-i16:16-i8:8-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @aggregate_root_flow {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"

    simulation.class.decl @Node id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %node = simulation.class.alloc %ctx :
          !simulation.context -> !simulation.class_handle<@Node>
      %object_arm = simulation.union.construct %node as 0 :
          (!simulation.class_handle<@Node>) -> !tagged
      %bits = arith.constant 17 : i32
      %bits_arm = simulation.union.construct %bits as 1 : (i32) -> !tagged
      %condition = arith.constant false
      cf.cond_br %condition, ^object_path, ^bits_path

    ^object_path:
      simulation.gc.safepoint %ctx : !simulation.context
      cf.br ^join(%object_arm : !tagged)

    ^bits_path:
      simulation.gc.safepoint %ctx : !simulation.context
      cf.br ^join(%bits_arm : !tagged)

    ^join(%merged: !tagged):
      simulation.gc.safepoint %ctx : !simulation.context
      %object = simulation.union.extract %merged[0] :
          (!tagged) -> !simulation.class_handle<@Node>
      simulation.gc.safepoint %ctx : !simulation.context
      simulation.gc.safepoint %ctx : !simulation.context
      simulation.return
    }
  }
}

// Each branch populates only its live aggregate shadow. At the join, the
// encoder clears the path-local shadow and extracts the merged block argument.
// Once the merged value dies, its shadow is cleared exactly once; the final
// safepoint must not emit a repeated clear.
// CHECK: 24: opcode=1 flags=0 dst=8
// CHECK-NEXT: 25: opcode=1 flags=0 dst=9
// CHECK: 30: opcode=1 flags=0 dst=7
// CHECK-NEXT: 31: opcode=1 flags=0 dst=10
// CHECK-NEXT: 32: opcode=36
// CHECK-NEXT: 33: opcode=36
// CHECK: intrinsic {{.*}}id=0x00010411 inputs=2 outputs=1 flags=0
// CHECK: site 1: {{.*}} id=0x00010411 {{.*}} outputs=[8]
// CHECK: site 3: {{.*}} id=0x00010411 {{.*}} outputs=[9]
// CHECK: site 5: {{.*}} id=0x00010411 {{.*}} outputs=[10]
