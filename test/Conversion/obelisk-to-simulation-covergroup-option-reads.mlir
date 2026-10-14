// RUN: %split-file %s %t
// RUN: obelisk --std=1800-2023 --coverage=functional -emit-obelisk %t/input.sv -o %t/input.mlir
// RUN: obelisk-opt %t/input.mlir '--lower-obelisk-to-sim=opt-level=0' '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t/lowered.mlir
// RUN: obelisk-opt %t/lowered.mlir --canonicalize --cse | FileCheck %s --check-prefix=SIM
// RUN: obelisk-opt %t/lowered.mlir --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE
// RUN: obelisk-opt %t/lowered.mlir --convert-obelisk-sim-processes-to-llvm-coroutines | obelisk-translate --mlir-to-llvmir | opt -S -passes=verify -o /dev/null
// RUN: obelisk-opt %t/lowered.mlir --encode-obelisk-sim-to-bytecode='vpi=off' --test-obelisk-print-bytecode -o /dev/null 2>&1 | FileCheck %s --check-prefix=BYTECODE

// IEEE 19.10 defines every option field as a readable primitive member.
// Stable item IDs and the typed receiver survive both backend conversions.
// String snapshots allocate managed storage; reads around coverage writes
// must retain their order through canonicalization and CSE.
// SIM: simulation.covergroup.create
// SIM: simulation.covergroup.get_string_option {{.*}} item 0 option name
// SIM: simulation.covergroup.get_string_option {{.*}} item 0 option comment
// SIM: simulation.covergroup.get_integer_option {{.*}} item 0 option weight
// SIM: simulation.covergroup.get_integer_option {{.*}} item 0 option goal
// SIM: simulation.covergroup.get_integer_option {{.*}} item 0 option at_least
// SIM: simulation.covergroup.get_integer_option {{.*}} item 0 option auto_bin_max
// SIM: simulation.covergroup.get_integer_option {{.*}} item 0 option cross_num_print_missing
// SIM: simulation.covergroup.get_integer_option {{.*}} item 0 option cross_retain_auto_bins
// SIM: simulation.covergroup.get_integer_option {{.*}} item 0 option detect_overlap
// SIM: simulation.covergroup.get_integer_option {{.*}} item 0 option per_instance
// SIM: simulation.covergroup.get_integer_option {{.*}} item 0 option get_inst_coverage
// SIM: simulation.covergroup.get_string_option {{.*}} item [[POINT:-?[0-9]+]] option comment
// SIM: simulation.covergroup.get_integer_option {{.*}} item [[POINT]] option weight
// SIM: simulation.covergroup.get_integer_option {{.*}} item [[POINT]] option goal
// SIM: simulation.covergroup.get_integer_option {{.*}} item [[POINT]] option at_least
// SIM: simulation.covergroup.get_integer_option {{.*}} item [[POINT]] option auto_bin_max
// SIM: simulation.covergroup.get_integer_option {{.*}} item [[POINT]] option detect_overlap
// SIM: simulation.covergroup.get_string_option {{.*}} item [[CROSS:-?[0-9]+]] option comment
// SIM: simulation.covergroup.get_integer_option {{.*}} item [[CROSS]] option weight
// SIM: simulation.covergroup.get_integer_option {{.*}} item [[CROSS]] option goal
// SIM: simulation.covergroup.get_integer_option {{.*}} item [[CROSS]] option at_least
// SIM: simulation.covergroup.get_integer_option {{.*}} item [[CROSS]] option cross_num_print_missing
// SIM: simulation.covergroup.get_integer_option {{.*}} item [[CROSS]] option cross_retain_auto_bins
// SIM: simulation.covergroup.set_string_option {{.*}} item 0 option comment
// SIM: simulation.covergroup.get_string_option {{.*}} item 0 option comment
// SIM: simulation.covergroup.set_name
// SIM: simulation.covergroup.get_string_option {{.*}} item 0 option name
// NATIVE-DAG: llvm.call @obelisk_rt_v1_covergroup_get_integer_option
// NATIVE-DAG: llvm.call @obelisk_rt_v1_covergroup_get_string_option
// BYTECODE-DAG: intrinsic {{.*}}id=0x0001047a inputs=3 outputs=1 flags=0
// BYTECODE-DAG: intrinsic {{.*}}id=0x0001047b inputs=3 outputs=1 flags=0

//--- input.sv
module top;
  bit value;
  covergroup cg;
    cp: coverpoint value;
    other: coverpoint value;
    cx: cross cp, other;
  endgroup
  cg c;
  initial begin
    c = new;
    $display("%s %s %0d %0d %0d %0d %0d %0d %0d %0d %0d",
             c.option.name, c.option.comment, c.option.weight, c.option.goal,
             c.option.at_least, c.option.auto_bin_max,
             c.option.cross_num_print_missing, c.option.cross_retain_auto_bins,
             c.option.detect_overlap, c.option.per_instance,
             c.option.get_inst_coverage);
    $display("%s %0d %0d %0d %0d %0d", c.cp.option.comment,
             c.cp.option.weight, c.cp.option.goal, c.cp.option.at_least,
             c.cp.option.auto_bin_max, c.cp.option.detect_overlap);
    $display("%s %0d %0d %0d %0d %0d", c.cx.option.comment,
             c.cx.option.weight, c.cx.option.goal, c.cx.option.at_least,
             c.cx.option.cross_num_print_missing, c.cx.option.cross_retain_auto_bins);
    c.option.comment = "edited";
    $display("%s", c.option.comment);
    c.set_inst_name("renamed");
    $display("%s", c.option.name);
    $finish;
  end
endmodule
