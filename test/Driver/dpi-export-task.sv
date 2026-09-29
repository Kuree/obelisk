// RUN: %target_clang -fPIC -c \
// RUN:   %S/Inputs/dpi_export_task_impl.c \
// RUN:   -I%resource_dir/include -o %t.o
// RUN: obelisk %s %t.o -o %t.native
// RUN: %t.native | FileCheck %s --check-prefix=OUTPUT
// RUN: obelisk --execution-tier=bytecode %s %t.o -o %t.bytecode
// RUN: %t.bytecode | FileCheck %s --check-prefix=OUTPUT
// RUN: obelisk --emit-dpi-header %s -o %t.h
// RUN: FileCheck %s --check-prefix=HEADER < %t.h
// RUN: %llvm_dist/bin/clang -x c -fsyntax-only -include %t.h \
// RUN:   -I%resource_dir/include /dev/null
// RUN: not obelisk --target=wasm32 -O0 -emit-llvm %s -o %t.wasm.ll 2>&1 \
// RUN:   | FileCheck %s --check-prefix=WASM

module dpi_export_task;
  import "DPI-C" context task drive(output int value);
  import "DPI-C" context task drive_cancel_self(output int value);
  import "DPI-C" context task drive_cancel_parent(output int value);

  task automatic work(input int value, output int result);
    #1 result = value + 1;
  endtask
  export "DPI-C" c_work = task work;

  task automatic cancel_self(output int result);
    #10 result = 99;
  endtask
  export "DPI-C" c_cancel_self = task cancel_self;

  task automatic cancel_parent(output int result);
    #10 result = 100;
  endtask
  export "DPI-C" c_cancel_parent = task cancel_parent;

  initial begin : caller
    int value;
    drive(value);
    $display("value=%0d time=%0t", value, $time);
    drive_cancel_self(value);
    $display("after-self time=%0t", $time);
    drive_cancel_parent(value);
    $display("after-parent");
  end

  initial begin
    #2 disable dpi_export_task.cancel_self;
    #2 disable dpi_export_task.caller;
    #16 $display("survived time=%0t", $time);
  end
endmodule

// OUTPUT: value=42 time=1
// OUTPUT: self-disabled=0 api=0 c-value=0
// OUTPUT: after-self time=2
// OUTPUT: parent-disabled=1 api=1 c-value=0
// OUTPUT-NOT: after-parent
// OUTPUT: survived time=20
// HEADER: int c_cancel_parent(int32_t *arg0);
// HEADER: int c_cancel_self(int32_t *arg0);
// HEADER: int c_work(int32_t arg0, int32_t *arg1);
// WASM: DPI is unavailable for the wasm32 target; use --target=native
