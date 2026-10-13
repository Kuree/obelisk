// RUN: obelisk -O3 %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: FileCheck %s < %t.native.out
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | diff -u %t.native.out -

module native_function_fork;
  function bit launch;
    launch = 0;
    fork
      $display("function-child");
    join_none
  endfunction

  initial begin
    bit ignored;
    ignored = launch();
    $display("function-parent");
    wait fork;
    $display("function-done");
  end
endmodule

// CHECK: function-parent
// CHECK-NEXT: function-child
// CHECK-NEXT: function-done
