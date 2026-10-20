// RUN: %target_clang %target_pic -c \
// RUN:   %S/Inputs/dpi_export_task_invalid_impl.c \
// RUN:   -I%resource_dir/include -o %t.o
// RUN: obelisk %s %t.o -o %t.native
// RUN: not --status=19 %t.native > %t.native.out
// RUN: test ! -s %t.native.out
// RUN: obelisk --execution-tier=bytecode %s %t.o -o %t.bytecode
// RUN: not --status=19 %t.bytecode > %t.bytecode.out
// RUN: test ! -s %t.bytecode.out

module dpi_export_task_invalid;
  import "DPI-C" context function int call_task_from_function();

  task automatic illegal_task(output int result);
    result = 42;
  endtask
  export "DPI-C" c_illegal_task = task illegal_task;

  initial begin
    int status = call_task_from_function();
    $display("continued %0d", status);
  end
endmodule
