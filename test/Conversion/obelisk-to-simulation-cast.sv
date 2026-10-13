// RUN: obelisk -emit-slang %s | obelisk-opt --convert-slang-to-obelisk '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-p:64:64-i64:64-i32:32-i16:16-i8:8\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.obelisk.mlir
// RUN: obelisk-opt %t.obelisk.mlir '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %t.obelisk.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null
// RUN: obelisk-opt %t.obelisk.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null

// CHECK-LABEL: simulation.func private @unit_0
// CHECK: %[[INTEGER:.*]] = simulation.ref.load {{.*}} : {{.*}} -> i32
// CHECK-DAG: %[[ENUM1:.*]] = arith.cmpi eq, %[[INTEGER]], {{.*}} : i32
// CHECK-DAG: %[[ENUM4:.*]] = arith.cmpi eq, %[[INTEGER]], {{.*}} : i32
// CHECK-DAG: %[[ENUM9:.*]] = arith.cmpi eq, %[[INTEGER]], {{.*}} : i32
// CHECK: %[[ENUM_OK:.*]] = arith.ori
// CHECK: cf.cond_br %[[ENUM_OK]], ^[[ENUM_STORE:bb[0-9]+]], ^[[ENUM_DONE:bb[0-9]+]]
// CHECK: ^[[ENUM_STORE]]:
// CHECK-NEXT: simulation.ref.store %[[INTEGER]] to %[[ENUM_DEST:[^ ]+]] :
// CHECK-NEXT: cf.br ^[[ENUM_DONE]]
// CHECK: ^[[ENUM_DONE]]:
// CHECK: %[[WIDE_SOURCE:.*]] = simulation.ref.load {{.*}} : {{.*}} -> i32
// CHECK-NEXT: %[[TRUNCATED:.*]] = arith.trunci %[[WIDE_SOURCE]] : i32 to i4
// CHECK: %[[NARROW_OK:.*]] = arith.cmpi eq, %[[WIDE_SOURCE]], {{.*}} : i32
// CHECK: cf.cond_br %[[NARROW_OK]], ^[[NARROW_STORE:bb[0-9]+]], ^[[NARROW_DONE:bb[0-9]+]]
// CHECK: ^[[NARROW_STORE]]:
// CHECK-NEXT: simulation.ref.store %[[TRUNCATED]]
// CHECK-NEXT: cf.br ^[[NARROW_DONE]]
// CHECK: ^[[NARROW_DONE]]:
// CHECK: %[[REAL:.*]] = simulation.ref.load {{.*}} : {{.*}} -> f64
// CHECK-NEXT: %[[INTEGER_VALUE:.*]] = simulation.real.to_integer %[[REAL]] signed = true : i32
// CHECK-NEXT: simulation.ref.store %[[INTEGER_VALUE]] to %[[ENUM_DEST]]
// CHECK: %[[LOGIC:.*]] = simulation.packed.flatten {{.*}} -> !simulation.logic<4>
// CHECK: %[[LOGIC1:.*]] = simulation.logic.compare case_eq %[[LOGIC]], {{.*}}
// CHECK: %[[LOGICX:.*]] = simulation.logic.compare case_eq %[[LOGIC]], {{.*}}
// CHECK: %[[LOGIC_OK:.*]] = arith.ori %[[LOGIC1]], %[[LOGICX]] : i1
// CHECK: cf.cond_br %[[LOGIC_OK]], ^[[LOGIC_STORE:bb[0-9]+]], ^[[LOGIC_DONE:bb[0-9]+]]
// CHECK: ^[[LOGIC_STORE]]:
// CHECK-NEXT: simulation.ref.store %[[LOGIC]]
// CHECK-NEXT: cf.br ^[[LOGIC_DONE]]
// CHECK: ^[[LOGIC_DONE]]:
// CHECK: %[[SAME_ENUM:.*]] = simulation.ref.load %[[ENUM_DEST]]
// CHECK: %[[SAME_PARTIAL:.*]] = arith.ori
// CHECK: arith.cmpi eq, %[[SAME_ENUM]],
// CHECK-NEXT: %[[SAME_OK:.*]] = arith.ori %[[SAME_PARTIAL]], {{.*}} : i1
// CHECK: cf.cond_br %[[SAME_OK]], ^[[SAME_STORE:bb[0-9]+]], ^[[SAME_DONE:bb[0-9]+]]
// CHECK: ^[[SAME_STORE]]:
// CHECK-NEXT: simulation.ref.store %[[SAME_ENUM]] to %[[ENUM_DEST]]
// CHECK-NEXT: cf.br ^[[SAME_DONE]]
// CHECK: ^[[SAME_DONE]]:
// CHECK: %[[OTHER_ENUM:.*]] = simulation.ref.load {{.*}} : {{.*}} -> !simulation.logic<4>
// CHECK-NEXT: %[[OTHER_BITS:.*]] = simulation.logic.to_bits %[[OTHER_ENUM]] : !simulation.logic<4> -> i4
// CHECK-NEXT: %[[OTHER_INTEGER:.*]] = arith.extui %[[OTHER_BITS]] : i4 to i32
// CHECK-NEXT: %[[OTHER_WIDE:.*]] = simulation.logic.resize %[[OTHER_ENUM]] signed = false : !simulation.logic<4> -> !simulation.logic<32>
// CHECK: simulation.logic.compare case_eq %[[OTHER_WIDE]], {{.*}} : (!simulation.logic<32>, !simulation.logic<32>) -> i1
// CHECK: cf.cond_br {{.*}}, ^[[OTHER_STORE:bb[0-9]+]], ^[[OTHER_DONE:bb[0-9]+]]
// CHECK: ^[[OTHER_STORE]]:
// CHECK: simulation.ref.store %[[OTHER_INTEGER]] to %[[ENUM_DEST]]
// CHECK: cf.br ^[[OTHER_DONE]]
// CHECK: ^[[OTHER_DONE]]:
// CHECK: %[[BASE:.*]] = simulation.ref.load {{.*}} : {{.*}} -> !simulation.class_handle<@[[BASE_CLASS:[^>]+]]>
// CHECK-NEXT: %[[DOWNCAST:.*]] = simulation.class.cast %[[BASE]] : !simulation.class_handle<@[[BASE_CLASS]]> to !simulation.class_handle<@[[DERIVED_CLASS:[^>]+]]>
// CHECK-NEXT: %[[INSTANCE:.*]] = simulation.class.is_instance %[[BASE]] is @[[DERIVED_CLASS]]
// CHECK-NEXT: %[[CLASS_ID:.*]] = simulation.class.id %[[BASE]]
// CHECK: %[[IS_NULL:.*]] = arith.cmpi eq, %[[CLASS_ID]], {{.*}} : i64
// CHECK-NEXT: %[[CLASS_OK:.*]] = arith.ori %[[INSTANCE]], %[[IS_NULL]] : i1
// CHECK-NEXT: cf.cond_br %[[CLASS_OK]], ^[[CLASS_STORE:bb[0-9]+]], ^[[CLASS_DONE:bb[0-9]+]]
// CHECK: ^[[CLASS_STORE]]:
// CHECK-NEXT: simulation.ref.store %[[DOWNCAST]] to %[[DERIVED_DEST:[^ ]+]]
// CHECK-NEXT: cf.br ^[[CLASS_DONE]]
// CHECK: ^[[CLASS_DONE]]:
// CHECK: simulation.ref.store {{.*}} to %[[CLASS_RESULT:[^ ]+]] : i32
// CHECK: %[[NULL:.*]] = simulation.class.null {{.*}} : !simulation.class_handle<@[[BASE_CLASS]]>
// CHECK-NEXT: simulation.ref.store %[[NULL]] to %[[BASE_DEST:[^ ]+]]
// CHECK: %[[TRUE:.*]] = arith.constant {{.*}}1 : i32
// CHECK-NEXT: simulation.ref.store %[[TRUE]] to %[[CLASS_RESULT]]
// CHECK-NOT: simulation.class.cast
// CHECK: %[[FALSE:.*]] = arith.constant {{.*}}0 : i32
// CHECK-NEXT: simulation.ref.store %[[FALSE]] to %[[CLASS_RESULT]]
// CHECK: %[[TASK_PARTIAL:.*]] = arith.ori
// CHECK: arith.cmpi eq
// CHECK-NEXT: %[[TASK_OK:.*]] = arith.ori %[[TASK_PARTIAL]], {{.*}} : i1
// CHECK: cf.cond_br %[[TASK_OK]], ^[[TASK_STORE:bb[0-9]+]], ^[[TASK_FAIL:bb[0-9]+]]
// CHECK: ^[[TASK_STORE]]:
// CHECK-NEXT: simulation.ref.store {{.*}} to %[[ENUM_DEST]]
// CHECK-NEXT: simulation.return
// CHECK: ^[[TASK_FAIL]]:
// CHECK: simulation.bytes.constant {{.*}}$cast failed when used as a task
// CHECK: simulation.display
// CHECK: simulation.fatal
// CHECK-NEXT: simulation.return

class A;
endclass
class B;
endclass
class D extends A;
endclass
module cast_fixture;
  typedef enum int {E1 = 1, E4 = 4, E9 = 9} E;
  typedef enum logic [3:0] {L1 = 4'b0001, LX = 4'bx} L;
  typedef enum bit [3:0] {N1 = 4'b0001} N;
  E enum_destination;
  int integer_source;
  int enum_result;
  int integer_destination;
  real real_source;
  int value_result;
  L logic_destination;
  logic [3:0] logic_source;
  int logic_result;
  N narrow_destination;
  int narrow_result;
  A class_destination;
  B class_source;
  int class_result;
  D derived_destination;
  initial begin
    enum_result = $cast(enum_destination, integer_source);
    narrow_result = $cast(narrow_destination, integer_source);
    value_result = $cast(enum_destination, real_source);
    logic_result = $cast(logic_destination, logic_source);
    value_result = $cast(enum_destination, enum_destination);
    value_result = $cast(enum_destination, logic_destination);
    class_result = $cast(derived_destination, class_destination);
    class_result = $cast(class_destination, null);
    class_result = $cast(class_destination, class_source);
    $cast(enum_destination, integer_source);
  end
endmodule
