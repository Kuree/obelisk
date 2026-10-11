// RUN: %split-file %s %t
// RUN: obelisk -emit-slang %t/input.sv | obelisk-opt --convert-slang-to-obelisk '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' '--lower-obelisk-to-sim=opt-level=0' -o %t/lowered.mlir
// RUN: FileCheck %s --check-prefix=SIM < %t/lowered.mlir
// RUN: obelisk-opt %t/lowered.mlir '--encode-obelisk-sim-to-bytecode=vpi=off' --test-obelisk-print-bytecode -o /dev/null 2>&1 | FileCheck %s --check-prefix=BYTECODE
// RUN: obelisk-opt %t/lowered.mlir --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE

// IEEE 1800-2023 19.7 permits these owner-local instance options to be
// assigned after construction. The item selector is a stable template ID,
// never a physical schema-table index.
// SIM: simulation.covergroup.set_name
// SIM: simulation.covergroup.set_integer_option {{.*}} item 0 option weight
// SIM: simulation.covergroup.set_integer_option {{.*}} item 0 option goal
// SIM: simulation.covergroup.set_string_option {{.*}} item 0 option comment
// SIM: simulation.covergroup.set_integer_option {{.*}} item 0 option at_least
// SIM: simulation.covergroup.set_integer_option {{.*}} item 0 option cross_num_print_missing
// SIM: simulation.covergroup.set_integer_option {{.*}} item [[POINT:-?[0-9]+]] option weight
// SIM: simulation.covergroup.set_integer_option {{.*}} item [[POINT]] option goal
// SIM: simulation.covergroup.set_string_option {{.*}} item [[POINT]] option comment
// SIM: simulation.covergroup.set_integer_option {{.*}} item [[POINT]] option at_least
// SIM: simulation.covergroup.set_integer_option {{.*}} item [[CROSS:-?[0-9]+]] option weight
// SIM: simulation.covergroup.set_integer_option {{.*}} item [[CROSS]] option at_least
// SIM: simulation.covergroup.set_integer_option {{.*}} item [[CROSS]] option cross_num_print_missing
// SIM: simulation.covergroup.get_integer_option {{.*}} item 0 option cross_num_print_missing
// SIM: simulation.covergroup.get_integer_option {{.*}} item [[CROSS]] option cross_num_print_missing
// SIM: simulation.covergroup.set_type_integer_option {{.*}} type [[TYPE:-?[0-9]+]] item 0 option weight
// SIM: simulation.covergroup.set_type_integer_option {{.*}} type [[TYPE]] item 0 option goal
// SIM: simulation.covergroup.set_type_string_option {{.*}} type [[TYPE]] item 0 option comment
// SIM: simulation.covergroup.set_type_integer_option {{.*}} type [[TYPE]] item 0 option merge_instances
// SIM: simulation.covergroup.set_type_integer_option {{.*}} type [[TYPE]] item [[TYPE_POINT:-?[0-9]+]] option weight
// SIM: simulation.covergroup.set_type_integer_option {{.*}} type [[TYPE]] item [[TYPE_POINT]] option goal
// SIM: simulation.covergroup.set_type_string_option {{.*}} type [[TYPE]] item [[TYPE_POINT]] option comment
// SIM: simulation.covergroup.type_query {{.*}} item [[POINT]]
// SIM: simulation.covergroup.type_query {{.*}} item [[CROSS]]

// NATIVE-DAG: llvm.call @obelisk_rt_v1_covergroup_set_name
// NATIVE-DAG: llvm.call @obelisk_rt_v1_covergroup_set_integer_option
// NATIVE-DAG: llvm.call @obelisk_rt_v1_covergroup_get_integer_option
// NATIVE-DAG: llvm.call @obelisk_rt_v1_covergroup_set_string_option
// NATIVE-DAG: llvm.call @obelisk_rt_v1_covergroup_set_type_integer_option
// NATIVE-DAG: llvm.call @obelisk_rt_v1_covergroup_set_type_string_option
// NATIVE: llvm.call @obelisk_rt_v1_covergroup_type_query
// NATIVE: llvm.call @obelisk_rt_v1_covergroup_type_query
// BYTECODE-DAG: intrinsic {{.*}}id=0x00010455 inputs=2 outputs=3 flags=0
// BYTECODE-DAG: intrinsic {{.*}}id=0x00010476 inputs=4 outputs=0 flags=0

//--- input.sv
module top;
  int value;
  int covered, total;
  real percentage;
  covergroup cg;
    cp: coverpoint value { bins zero = {0}; bins one = {1}; }
    cp2: coverpoint value { bins zero = {0}; bins one = {1}; }
    cx: cross cp, cp2;
  endgroup
  cg c;
  initial begin
    c = new;
    c.option.name = "mutated";
    c.option.weight = 2;
    c.option.goal = 75;
    c.option.comment = "group";
    c.option.at_least = 3;
    c.option.cross_num_print_missing = 5;
    c.cp.option.weight = 4;
    c.cp.option.goal = 50;
    c.cp.option.comment = "point";
    c.cp.option.at_least = 2;
    c.cx.option.weight = 5;
    c.cx.option.at_least = 4;
    c.cx.option.cross_num_print_missing = 2;
    $display("%0d %0d", c.option.cross_num_print_missing,
             c.cx.option.cross_num_print_missing);
    cg::type_option.weight = 2;
    cg::type_option.goal = 75;
    cg::type_option.comment = "group type";
    cg::type_option.merge_instances = 1;
    cg::cp::type_option.weight = 3;
    cg::cp::type_option.goal = 80;
    cg::cp::type_option.comment = "point type";
    percentage = cg::cp::get_coverage(covered, total);
    percentage = cg::cx::get_coverage(covered, total);
  end
endmodule
