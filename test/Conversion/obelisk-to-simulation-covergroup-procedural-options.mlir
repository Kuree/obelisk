// RUN: %split-file %s %t
// RUN: obelisk -emit-obelisk %t/input.sv -o %t/input.mlir
// RUN: obelisk-opt %t/input.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | %python %S/Inputs/mutate-functional-batch.py add-layout \
// RUN:   > %t/lowered.mlir
// RUN: FileCheck %s --check-prefix=SIM < %t/lowered.mlir
// RUN: obelisk-opt %t/lowered.mlir \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' > /dev/null
// RUN: obelisk-opt %t/lowered.mlir \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s --check-prefix=NATIVE

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
// SIM: simulation.covergroup.set_integer_option {{.*}} item [[CROSS:-?[0-9]+]] option at_least
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

// NATIVE-DAG: llvm.call @obelisk_rt_v1_covergroup_set_name
// NATIVE-DAG: llvm.call @obelisk_rt_v1_covergroup_set_integer_option
// NATIVE-DAG: llvm.call @obelisk_rt_v1_covergroup_get_integer_option
// NATIVE-DAG: llvm.call @obelisk_rt_v1_covergroup_set_string_option
// NATIVE-DAG: llvm.call @obelisk_rt_v1_covergroup_set_type_integer_option
// NATIVE-DAG: llvm.call @obelisk_rt_v1_covergroup_set_type_string_option

//--- input.sv
module top;
  int value;
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
  end
endmodule
