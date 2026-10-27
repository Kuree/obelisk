// RUN: talos -emit-slang --std=1800-2023 %s -o %t.slang.mlir
// RUN: talos-opt %t.slang.mlir --test-talos-native-backend='scheduler=generic stop-after=lowering' | FileCheck %s --check-prefix=SIM
// RUN: talos-opt %t.slang.mlir --test-talos-native-backend='scheduler=generic' | FileCheck %s --check-prefix=LLVM

// IEEE 1800-2023 8.30 (std::weak_reference): new, get and clear
// lower to managed weak-reference operations, not to ordinary class methods.
//
// The collector clears a weak referent once nothing strongly reaches it; that
// is covered by ManagedHeapTest in runtime/unittests/RuntimeTest.cpp.
// SIM: %[[ITEM:.*]] = simulation.ref.load %arg1 : {{.*}} -> !simulation.class_handle<@__talos_class_s5_node>
// SIM-NEXT: %[[WEAK:.*]] = simulation.weak.create %arg0, %[[ITEM]] : !simulation.context, !simulation.class_handle<@__talos_class_s5_node> -> !simulation.class_handle<@__talos_class_s25_weak_reference>
// SIM-NEXT: simulation.ref.store %[[WEAK]] to %arg2
// SIM: %[[FIRST:.*]] = simulation.weak.get %{{.*}} : !simulation.class_handle<@__talos_class_s25_weak_reference> -> !simulation.class_handle<@__talos_class_s5_node>
// SIM-NEXT: simulation.class.field_ref %[[FIRST]][@__talos_class_s5_node_field_0]
// SIM: %[[SECOND:.*]] = simulation.weak.get
// SIM-NEXT: %[[SECOND_ID:.*]] = simulation.class.id %[[SECOND]]
// SIM: %[[NULL_ID:.*]] = simulation.class.id %{{.*}}
// SIM-NEXT: arith.cmpi eq, %[[SECOND_ID]], %[[NULL_ID]] : i64
// SIM: simulation.weak.clear %{{.*}} : !simulation.class_handle<@__talos_class_s25_weak_reference>
// LLVM: llvm.call @talos_rt_v1_weak_create(
// LLVM: llvm.call @talos_rt_v1_weak_get(
// LLVM: llvm.call @talos_rt_v1_weak_get(
// LLVM: llvm.call @talos_rt_v1_weak_clear(

module top;
  class node;
    int value;
    node next;
    function new(int value);
      this.value = value;
    endfunction
  endclass
  node item;
  std::weak_reference #(node) weak_ref;
  initial begin
    item = new(9);
    weak_ref = new(item);
    $display("%0d", weak_ref.get().value);
    item = null;
    $display("%0d", weak_ref.get() == null);
    weak_ref.clear();
  end
endmodule
