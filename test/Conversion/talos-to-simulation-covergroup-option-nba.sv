// RUN: talos --std=1800-2023 --coverage=functional -emit-talos %s | talos-opt '--lower-talos-to-sim=opt-level=0' '--test-talos-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.mlir
// RUN: talos-opt %t.mlir --canonicalize --cse | FileCheck %s --check-prefix=SIM
// RUN: talos-opt %t.mlir --convert-talos-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: talos-opt %t.mlir --convert-talos-sim-processes-to-llvm-coroutines | talos-translate --mlir-to-llvmir | opt -S -passes=verify -o /dev/null
// RUN: talos-opt %t.mlir --encode-talos-sim-to-bytecode='vpi=off' --test-talos-print-bytecode -o /dev/null 2>&1 | FileCheck %s --check-prefix=BYTECODE
// RUN: not talos-opt %t.mlir '--test-talos-edit-attributes=op=simulation.covergroup.option_reference match={option = 1 : i32} set={item = 999 : i64}' --encode-talos-sim-to-bytecode='vpi=off' -o /dev/null 2>&1 | FileCheck %s --check-prefix=ITEM
// RUN: not talos-opt %t.mlir '--test-talos-edit-attributes=op=simulation.covergroup.type_option_reference set={typeId = 999 : i64}' --convert-talos-sim-processes-to-llvm-coroutines -o /dev/null 2>&1 | FileCheck %s --check-prefix=SCHEMA-TYPE
// RUN: not talos-opt %t.mlir '--test-talos-edit-attributes=op=simulation.covergroup.option_reference match={option = 1 : i32} set={option = 6 : i32}' --encode-talos-sim-to-bytecode='vpi=off' -o /dev/null 2>&1 | FileCheck %s --check-prefix=SCOPE

// IEEE 10.4.2 and 19.7: capture the receiver/index and converted RHS once.
// Pure patch positioning stays in SSA. No option read or write is performed
// before the NBA commits; path construction has explicit allocation effects.
// SIM: simulation.spawn @[[PROCESS:unit_[0-9]+]]
// SIM: simulation.func private @[[PROCESS]](
// SIM: simulation.covergroup.create
// SIM: %[[HANDLE:[0-9]+]] = simulation.call @[[RECEIVER:unit_[0-9]+]]
// SIM-NOT: simulation.call @[[RECEIVER]]
// SIM-NOT: simulation.covergroup.get_integer_option
// SIM: %[[WHOLE:[0-9]+]] = simulation.covergroup.option_reference {{.*}}, %[[HANDLE]] item 0 option weight mask {{.*}} index {{.*}} character false
// SIM: simulation.reference_path.nba.enqueue {{.*}} to %[[WHOLE]]
// SIM: %[[BIT:[0-9]+]] = simulation.covergroup.option_reference {{.*}} item 0 option weight
// SIM: simulation.reference_path.nba.enqueue {{.*}} to %[[BIT]]
// SIM: %[[SLICE:[0-9]+]] = simulation.covergroup.option_reference {{.*}} item 0 option weight
// SIM: simulation.reference_path.nba.enqueue {{.*}} to %[[SLICE]]
// SIM-NOT: simulation.covergroup.get_string_option
// SIM: %[[STRING:[0-9]+]] = simulation.covergroup.option_reference {{.*}} item 0 option comment {{.*}} character false
// SIM: simulation.reference_path.nba.enqueue {{.*}} to %[[STRING]] {{.*}}(!simulation.string, !simulation.reference_path<!simulation.string>)
// SIM: %[[CHAR_HANDLE:[0-9]+]] = simulation.call @[[RECEIVER]]
// SIM: %[[INDEX_VALUE:[0-9]+]] = simulation.call @[[INDEX:unit_[0-9]+]]
// SIM-NOT: simulation.call
// SIM-NOT: simulation.covergroup.get_string_option
// SIM: %[[INDEX_SCALAR:[0-9]+]] = simulation.packed.flatten %[[INDEX_VALUE]]
// SIM: %[[KNOWN:[0-9]+]] = simulation.logic.compare case_eq %[[INDEX_SCALAR]],
// SIM: %[[CAPTURED_INDEX:[0-9]+]] = arith.select %[[KNOWN]], {{.*}}, %c-1_i64
// SIM: %[[CHAR:[0-9]+]] = simulation.covergroup.option_reference {{.*}}, %[[CHAR_HANDLE]] item 0 option comment {{.*}} index %[[CAPTURED_INDEX]] character true
// SIM: simulation.reference_path.nba.enqueue {{.*}} to %[[CHAR]] after {{.*}}(i8, !simulation.reference_path<i8>, !simulation.time)
// SIM: simulation.argument_ref.from_ref {{.*}}!simulation.ref<i32> -> !simulation.argument_ref<i32>
// SIM: %[[STATIC:[0-9]+]] = simulation.covergroup.type_option_reference {{.*}} type [[TYPE:-?[0-9]+]] item 0 option goal
// SIM: simulation.reference_path.nba.enqueue {{.*}} to %[[STATIC]]
// SIM: simulation.argument_ref.from_ref {{.*}}!simulation.ref<i1> -> !simulation.argument_ref<i1>
// SIM: simulation.covergroup.type_option_reference {{.*}} type [[TYPE]] item 0 option merge_instances
// SIM: simulation.reference_path.nba.enqueue
// SIM: simulation.argument_ref.from_ref {{.*}}!simulation.ref<!simulation.string> -> !simulation.argument_ref<!simulation.string>
// SIM: simulation.covergroup.type_option_reference {{.*}} type [[TYPE]] item 0 option comment {{.*}} character true
// SIM: simulation.reference_path.nba.enqueue
// SIM-NOT: simulation.covergroup.get_integer_option
// SIM-NOT: simulation.covergroup.get_string_option
// SIM-NOT: simulation.covergroup.set_integer_option
// SIM-NOT: simulation.covergroup.set_type_integer_option
// SIM: simulation.finish

// NATIVE-DAG: llvm.call @talos_rt_v1_reference_path_covergroup_option_create
// NATIVE-DAG: llvm.call @talos_rt_v1_scheduler_managed_nba
// BYTECODE-DAG: intrinsic {{.*}}id=0x0001047c inputs=6 outputs=1 flags=0
// BYTECODE-DAG: intrinsic {{.*}}id=0x0001047d inputs=7 outputs=1 flags=0
// BYTECODE-DAG: intrinsic {{.*}}id=0x0001040c inputs=4 outputs=0 flags=0
// ITEM: has a nonexistent or wrong-owner functional item ID
// SCHEMA-TYPE: declaration schema type is absent from the embedded functional schema
// SCOPE: requires a covergroup or cross option owner

module top;
  bit value;
  int calls, indices;
  string source_text = "a captured string";
  covergroup cg;
    cp: coverpoint value;
  endgroup
  cg current;
  function automatic cg receiver();
    calls++;
    return current;
  endfunction
  function automatic logic [31:0] index_once();
    indices++;
    return 0;
  endfunction
  initial begin
    current = new;
    receiver().option.weight <= 64'h100000005;
    current.option.weight[0] <= 1;
    current.option.weight[1 +: 2] <= 2;
    current.cp.option.weight <= 3;
    current.option.comment <= source_text;
    receiver().option.comment[index_once()] <= #1 "N";
    cg::type_option.goal <= 64'h100000050;
    cg::type_option.merge_instances <= 0;
    cg::type_option.comment[0] <= "T";
    #2 $finish;
  end
endmodule
