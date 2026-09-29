// RUN: %target_clang -fPIC -c \
// RUN:   %S/Inputs/dpi_aggregate_gc_impl.c -I%resource_dir/include -o %t.o
// RUN: obelisk %s %t.o -o %t.native
// RUN: %t.native | FileCheck %s
// RUN: obelisk --execution-tier=bytecode %s %t.o -o %t.bytecode
// RUN: %t.bytecode | FileCheck %s

module dpi_aggregate_gc;
  typedef struct {
    string first;
    string second;
  } string_pair_t;

  import "DPI-C" context task drive_aggregate_gc();

  function int check_aggregate_function(
      input string_pair_t left, input string_pair_t right,
      input string scalar);
    return left.first == "left-first-heap" &&
           left.second == "left-second-heap" &&
           right.first == "right-first-heap" &&
           right.second == "right-second-heap" &&
           scalar == "scalar-function-heap";
  endfunction
  export "DPI-C" c_check_aggregate_function = function
      check_aggregate_function;

  task automatic check_aggregate_task(
      input string_pair_t left, input string_pair_t right,
      input string scalar, output int result);
    #1;
    result = left.first == "left-first-heap" &&
             left.second == "left-second-heap" &&
             right.first == "right-first-heap" &&
             right.second == "right-second-heap" &&
             scalar == "scalar-task-heap";
  endtask
  export "DPI-C" c_check_aggregate_task = task check_aggregate_task;

  initial drive_aggregate_gc();
endmodule

// CHECK: gc-function=1 gc-task=1 status=0
