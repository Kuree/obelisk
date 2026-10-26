// RUN: %split-file %s %t
// RUN: obelisk --std=1800-2023 --coverage=functional -emit-obelisk %t/input.sv -o %t/input.mlir
// RUN: obelisk-opt %t/input.mlir '--lower-obelisk-to-sim=opt-level=0' '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t/lowered.mlir
// RUN: obelisk-opt %t/lowered.mlir --canonicalize --cse | FileCheck %s --check-prefix=SIM
// RUN: obelisk-opt %t/lowered.mlir --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %t/lowered.mlir --convert-obelisk-sim-processes-to-llvm-coroutines | obelisk-translate --mlir-to-llvmir | opt -S -passes=verify -o /dev/null
// RUN: obelisk-opt %t/lowered.mlir --encode-obelisk-sim-to-bytecode='vpi=off' --test-obelisk-print-bytecode -o /dev/null 2>&1 | FileCheck %s --check-prefix=BYTECODE
// RUN: obelisk --std=1800-2023 --coverage=functional -emit-obelisk %S/../Coverage/functional-option-structure-snapshots.sv | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' --canonicalize --cse | FileCheck %s --check-prefix=OWNER

// IEEE 19.10 structures reuse typed owner-local reads and normal aggregate
// construction. Each copy loads its receiver once, then reads declaration
// fields in order. Managed strings retain their own allocation effects.
// SIM: simulation.covergroup.create
// SIM: %[[NAME:[0-9]+]] = simulation.covergroup.get_string_option {{.*}}, %[[HANDLE:[0-9]+]] item 0 option name
// SIM: simulation.covergroup.get_integer_option {{.*}}, %[[HANDLE]] item 0 option weight
// SIM: simulation.covergroup.get_integer_option {{.*}}, %[[HANDLE]] item 0 option goal
// SIM: %[[COMMENT:[0-9]+]] = simulation.covergroup.get_string_option {{.*}}, %[[HANDLE]] item 0 option comment
// SIM: simulation.covergroup.get_integer_option {{.*}}, %[[HANDLE]] item 0 option at_least
// SIM: simulation.covergroup.get_integer_option {{.*}}, %[[HANDLE]] item 0 option auto_bin_max
// SIM: simulation.covergroup.get_integer_option {{.*}}, %[[HANDLE]] item 0 option cross_num_print_missing
// SIM: simulation.covergroup.get_integer_option {{.*}}, %[[HANDLE]] item 0 option cross_retain_auto_bins
// SIM: simulation.covergroup.get_integer_option {{.*}}, %[[HANDLE]] item 0 option detect_overlap
// SIM: simulation.covergroup.get_integer_option {{.*}}, %[[HANDLE]] item 0 option per_instance
// SIM: simulation.covergroup.get_integer_option {{.*}}, %[[HANDLE]] item 0 option get_inst_coverage
// SIM: simulation.aggregate.construct %[[NAME]], {{.*}} %[[COMMENT]],
// SIM: simulation.covergroup.get_integer_option {{.*}} item [[POINT:-?[0-9]+]] option weight
// SIM: simulation.covergroup.get_integer_option {{.*}} item [[POINT]] option goal
// SIM: simulation.covergroup.get_string_option {{.*}} item [[POINT]] option comment
// SIM: simulation.covergroup.get_integer_option {{.*}} item [[POINT]] option at_least
// SIM: simulation.covergroup.get_integer_option {{.*}} item [[POINT]] option auto_bin_max
// SIM: simulation.covergroup.get_integer_option {{.*}} item [[POINT]] option detect_overlap
// SIM: simulation.aggregate.construct
// SIM: simulation.covergroup.get_integer_option {{.*}} item [[CROSS:-?[0-9]+]] option weight
// SIM: simulation.covergroup.get_integer_option {{.*}} item [[CROSS]] option goal
// SIM: simulation.covergroup.get_string_option {{.*}} item [[CROSS]] option comment
// SIM: simulation.covergroup.get_integer_option {{.*}} item [[CROSS]] option at_least
// SIM: simulation.covergroup.get_integer_option {{.*}} item [[CROSS]] option cross_num_print_missing
// SIM: simulation.covergroup.get_integer_option {{.*}} item [[CROSS]] option cross_retain_auto_bins
// SIM: simulation.aggregate.construct
// SIM: simulation.covergroup.set_string_option {{.*}} item 0 option comment
// NATIVE-DAG: llvm.call @obelisk_rt_v1_covergroup_get_integer_option
// NATIVE-DAG: llvm.call @obelisk_rt_v1_covergroup_get_string_option
// BYTECODE-DAG: intrinsic {{.*}}id=0x0001047a inputs=3 outputs=1 flags=0
// BYTECODE-DAG: intrinsic {{.*}}id=0x0001047b inputs=3 outputs=1 flags=0

// Embedded group/point/cross copies each call the receiver function once.
// Every field in a copy uses the same loaded handle and stable item ID.
// OWNER: simulation.call @[[RECEIVER:unit_[0-9]+]]({{.*}}) {simulation.vpi_call_site = @{{[^}]+}}} : {{.*}} -> !simulation.class_handle<
// OWNER-NOT: simulation.call @[[RECEIVER]]
// OWNER: simulation.covergroup.get_string_option {{.*}}, %[[GROUP_HANDLE:[0-9]+]] item 0 option name
// OWNER-NOT: simulation.call @[[RECEIVER]]
// OWNER: simulation.covergroup.get_integer_option {{.*}}, %[[GROUP_HANDLE]] item 0 option get_inst_coverage
// OWNER-NOT: simulation.call @[[RECEIVER]]
// OWNER: simulation.aggregate.construct
// OWNER: simulation.call @[[RECEIVER]]
// OWNER-NOT: simulation.call @[[RECEIVER]]
// OWNER: simulation.covergroup.get_integer_option {{.*}}, %[[POINT_HANDLE:[0-9]+]] item [[OWNER_POINT:-?[0-9]+]] option weight
// OWNER-NOT: simulation.call @[[RECEIVER]]
// OWNER: simulation.covergroup.get_integer_option {{.*}}, %[[POINT_HANDLE]] item [[OWNER_POINT]] option detect_overlap
// OWNER-NOT: simulation.call @[[RECEIVER]]
// OWNER: simulation.aggregate.construct
// OWNER: simulation.call @[[RECEIVER]]
// OWNER-NOT: simulation.call @[[RECEIVER]]
// OWNER: simulation.covergroup.get_integer_option {{.*}}, %[[CROSS_HANDLE:[0-9]+]] item [[OWNER_CROSS:-?[0-9]+]] option weight
// OWNER-NOT: simulation.call @[[RECEIVER]]
// OWNER: simulation.covergroup.get_integer_option {{.*}}, %[[CROSS_HANDLE]] item [[OWNER_CROSS]] option cross_retain_auto_bins
// OWNER-NOT: simulation.call @[[RECEIVER]]
// OWNER: simulation.aggregate.construct
// OWNER-NOT: simulation.call @[[RECEIVER]]
// OWNER: simulation.finish

//--- input.sv
module top;
  bit value;
  covergroup cg;
    cp: coverpoint value;
    other: coverpoint value;
    cx: cross cp, other;
  endgroup
  cg c;
  typedef type(c.option) group_options_t;
  typedef type(c.cp.option) point_options_t;
  typedef type(c.cx.option) cross_options_t;
  group_options_t group_snapshot;
  point_options_t point_snapshot;
  cross_options_t cross_snapshot;
  initial begin
    c = new;
    group_snapshot = c.option;
    point_snapshot = c.cp.option;
    cross_snapshot = c.cx.option;
    c.option.comment = "edited";
    $display("%s %s %s", group_snapshot.comment, point_snapshot.comment,
             cross_snapshot.comment);
    $finish;
  end
endmodule
