// RUN: obelisk -O0 --native-scheduler=generic %s -o %t.generic
// RUN: %t.generic | FileCheck %s
// RUN: obelisk -O3 --native-scheduler=auto %s -o %t.auto
// RUN: %t.auto | FileCheck %s
// RUN: obelisk --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | FileCheck %s
// RUN: obelisk -O3 --compile-threads=1 -emit-llvm %s -o %t.serial.ll
// RUN: obelisk -O3 --compile-threads=8 -emit-llvm %s -o %t.threaded.ll
// RUN: diff -u %t.serial.ll %t.threaded.ll

// Dynamic child captures include padding and separate wide value/XZ planes.
// The child must retain the automatic values until its first delayed use.
module native_spawn_captures;
  task automatic check(input byte tag, input bit [64:0] bits,
                       input logic [64:0] four);
    fork
      begin
        #1;
        if (tag !== 8'h5a || bits !== 65'h1123456789abcdef0 ||
            four !== 65'h1xz3456789abcdef0)
          $fatal(1, "bad child captures: %h %h %h", tag, bits, four);
        $display("captures preserved");
      end
    join
  endtask
  initial begin
    check(8'h5a, 65'h1123456789abcdef0, 65'h1xz3456789abcdef0);
    $finish;
  end
endmodule
// CHECK: captures preserved
