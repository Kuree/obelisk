// RUN: %split-file %s %t
// RUN: obelisk --std=1800-2023 --coverage=functional -emit-obelisk %t/input.sv -o %t/input.mlir
// RUN: obelisk-opt %t/input.mlir '--lower-obelisk-to-sim=opt-level=0' '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t/lowered.mlir
// RUN: obelisk-opt %t/lowered.mlir --canonicalize --cse | FileCheck %s --check-prefix=SIM
// RUN: obelisk-opt %t/lowered.mlir --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %t/lowered.mlir --convert-obelisk-sim-processes-to-llvm-coroutines | mlir-translate --mlir-to-llvmir | opt -S -passes=verify -o /dev/null
// RUN: obelisk-opt %t/lowered.mlir --encode-obelisk-sim-to-bytecode='vpi=off' --test-obelisk-print-bytecode -o /dev/null 2>&1 | FileCheck %s --check-prefix=BYTECODE

// IEEE 11.3.6 and 11.4.1-2: updates use a captured logical lvalue, never an
// interior reference into coverage runtime storage. Observe post/pre values,
// one receiver/index evaluation, member-width conversion and effect order.
// SIM: simulation.spawn @[[PROCESS:unit_[0-9]+]]
// SIM: simulation.func private @[[PROCESS]](
// SIM: simulation.covergroup.create
// SIM: %[[HANDLE:[0-9]+]] = simulation.call @[[RECEIVER:unit_[0-9]+]]
// SIM-NOT: simulation.call @[[RECEIVER]]
// SIM: %[[OLD64:[0-9]+]] = simulation.covergroup.get_integer_option {{.*}}, %[[HANDLE]] item 0 option weight
// SIM: %[[OLD:[0-9]+]] = arith.trunci %[[OLD64]] : i64 to i32
// SIM: %[[NEW:[0-9]+]] = arith.addi %[[OLD]], {{.*}} : i32
// SIM: %[[NEW64:[0-9]+]] = arith.extsi %[[NEW]] : i32 to i64
// SIM: simulation.covergroup.set_integer_option {{.*}}, %[[HANDLE]] item 0 option weight value %[[NEW64]]
// SIM: simulation.ref.store %[[OLD]] to
// SIM: %[[POINT_HANDLE:[0-9]+]] = simulation.call @[[RECEIVER]]
// SIM-NOT: simulation.call @[[RECEIVER]]
// SIM: simulation.covergroup.get_integer_option {{.*}}, %[[POINT_HANDLE]] item [[POINT:-?[0-9]+]] option weight
// SIM: %[[PRE:[0-9]+]] = arith.addi {{.*}} : i32
// SIM: simulation.covergroup.set_integer_option {{.*}}, %[[POINT_HANDLE]] item [[POINT]] option weight
// SIM: simulation.ref.store %[[PRE]] to
// SIM: %[[WIDE_HANDLE:[0-9]+]] = simulation.call @[[RECEIVER]]
// SIM-NOT: simulation.call @[[RECEIVER]]
// SIM: simulation.covergroup.get_integer_option {{.*}}, %[[WIDE_HANDLE]] item 0 option weight
// SIM: %[[SUM:[0-9]+]] = arith.addi {{.*}} : i64
// SIM: %[[FIELD:[0-9]+]] = arith.trunci %[[SUM]] : i64 to i32
// SIM: %[[PUBLISHED:[0-9]+]] = arith.extsi %[[FIELD]] : i32 to i64
// SIM: simulation.covergroup.set_integer_option {{.*}}, %[[WIDE_HANDLE]] item 0 option weight value %[[PUBLISHED]]
// SIM: %[[BIT_HANDLE:[0-9]+]] = simulation.call @[[RECEIVER]]
// SIM: simulation.call @[[INDEX:unit_[0-9]+]]
// SIM-NOT: simulation.call
// SIM: simulation.covergroup.get_integer_option {{.*}}, %[[BIT_HANDLE]] item 0 option weight
// SIM-NOT: simulation.call
// SIM: simulation.covergroup.set_integer_option {{.*}}, %[[BIT_HANDLE]] item 0 option weight
// SIM: %[[STRING_HANDLE:[0-9]+]] = simulation.call @[[RECEIVER]]
// SIM-NOT: simulation.call @[[RECEIVER]]
// SIM: simulation.covergroup.get_string_option {{.*}}, %[[STRING_HANDLE]] item 0 option comment
// SIM: simulation.string.putc
// SIM: simulation.covergroup.set_string_option {{.*}}, %[[STRING_HANDLE]] item 0 option comment
// SIM: simulation.call @[[RECEIVER]]
// SIM: simulation.call @[[INDEX]]
// SIM-NOT: simulation.call
// SIM: simulation.covergroup.get_string_option
// SIM: simulation.string.putc
// SIM: simulation.covergroup.set_string_option
// SIM: simulation.covergroup.set_type_integer_option {{.*}} type [[TYPE:-?[0-9]+]] item 0 option weight
// SIM: %[[STATIC_OLD:[0-9]+]] = simulation.ref.load {{.*}} : !simulation.ref<i32> -> i32
// SIM: %[[STATIC_NEW:[0-9]+]] = arith.addi %[[STATIC_OLD]], {{.*}} : i32
// SIM: simulation.covergroup.set_type_integer_option {{.*}} type [[TYPE]] item 0 option weight
// SIM: simulation.ref.store %[[STATIC_NEW]] to
// SIM: simulation.ref.store %[[STATIC_OLD]] to
// SIM: simulation.covergroup.set_type_integer_option {{.*}} type [[TYPE]] item 0 option merge_instances
// SIM: simulation.covergroup.set_type_string_option {{.*}} type [[TYPE]] item 0 option comment
// Captured logical roots, string indices and static references cross a
// suspension as typed continuation arguments, with no receiver re-evaluation.
// SIM: simulation.call @[[RECEIVER]]
// SIM: simulation.covergroup.get_integer_option
// SIM: simulation.suspend.delay {{.*}}!simulation.covergroup_handle<
// SIM-NOT: simulation.call
// SIM: simulation.covergroup.set_integer_option
// SIM: simulation.call @[[RECEIVER]]
// SIM: simulation.call @[[INDEX]]
// SIM: simulation.covergroup.get_string_option
// SIM: simulation.string.getc
// SIM: simulation.suspend.delay {{.*}}!simulation.covergroup_handle<
// SIM-NOT: simulation.call
// SIM: simulation.covergroup.get_string_option
// SIM: simulation.string.putc
// SIM: simulation.covergroup.set_string_option
// SIM: simulation.ref.load
// SIM: simulation.suspend.delay {{.*}}!simulation.ref<i32>
// SIM-NOT: simulation.call
// SIM: simulation.covergroup.set_type_integer_option {{.*}} type [[TYPE]] item 0 option weight
// SIM: simulation.ref.store
// SIM-NOT: simulation.call @[[RECEIVER]]
// SIM: simulation.finish

// NATIVE-DAG: llvm.call @obelisk_rt_v1_covergroup_get_integer_option
// NATIVE-DAG: llvm.call @obelisk_rt_v1_covergroup_get_string_option
// NATIVE-DAG: llvm.call @obelisk_rt_v1_covergroup_set_integer_option
// NATIVE-DAG: llvm.call @obelisk_rt_v1_covergroup_set_string_option
// NATIVE-DAG: llvm.call @obelisk_rt_v1_covergroup_set_type_integer_option
// NATIVE-DAG: llvm.call @obelisk_rt_v1_covergroup_set_type_string_option
// BYTECODE-DAG: intrinsic {{.*}}id=0x00010476 inputs=4 outputs=0 flags=0
// BYTECODE-DAG: intrinsic {{.*}}id=0x00010477 inputs=4 outputs=0 flags=0
// BYTECODE-DAG: intrinsic {{.*}}id=0x00010478 inputs=4 outputs=0 flags=0
// BYTECODE-DAG: intrinsic {{.*}}id=0x00010479 inputs=4 outputs=0 flags=0
// BYTECODE-DAG: intrinsic {{.*}}id=0x0001047a inputs=3 outputs=1 flags=0
// BYTECODE-DAG: intrinsic {{.*}}id=0x0001047b inputs=3 outputs=1 flags=0

//--- input.sv
module top;
  bit value;
  int calls, indices, prior_weight, updated;
  covergroup cg;
    cp: coverpoint value;
  endgroup
  cg current;
  function automatic cg receiver();
    calls++;
    return current;
  endfunction
  function automatic int index_once();
    indices++;
    return 0;
  endfunction
  initial begin
    current = new;
    prior_weight = receiver().option.weight++;
    updated = ++receiver().cp.option.weight;
    receiver().option.weight += 64'h100000000;
    receiver().option.weight[index_once()] ^= 1;
    receiver().option.comment[0] = "X";
    receiver().option.comment[index_once()] = "Y";
    cg::type_option.weight = 0;
    prior_weight = cg::type_option.weight++;
    cg::type_option.merge_instances ^= 1;
    cg::type_option.comment[0] = "T";
    receiver().option.weight += #1 2;
    receiver().option.comment[index_once()] ^= #1 8'h20;
    cg::type_option.weight += #1 2;
    $finish;
  end
endmodule
