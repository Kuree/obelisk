// RUN: obelisk --std=1800-2023 -O0 %s -o %t.o0.native
// RUN: obelisk --std=1800-2023 -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: obelisk --std=1800-2023 -O3 %s -o %t.o3.native
// RUN: obelisk --std=1800-2023 -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: obelisk --std=1800-2023 -O3 --native-scheduler=aot %s -o %t.o3.aot
// RUN: %t.o0.native > %t.o0.native.out 2>&1
// RUN: %t.o0.bytecode > %t.o0.bytecode.out 2>&1
// RUN: %t.o3.native > %t.o3.native.out 2>&1
// RUN: %t.o3.bytecode > %t.o3.bytecode.out 2>&1
// RUN: %t.o3.aot > %t.o3.aot.out 2>&1
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: diff -u %t.o0.native.out %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.aot.out
// RUN: FileCheck %s < %t.o0.native.out

module native_concurrent_sva_multiclock_final_runtime;
  logic weak_source = 0;
  logic weak_destination = 0;
  logic strong_source = 0;
  logic strong_middle = 0;
  logic strong_destination = 0;
  logic off_source = 0;
  logic off_destination = 0;

  weak_leading: assert property (@(posedge weak_source)
                                 1'b1 ##1
                                 @(posedge weak_destination) 1'b1)
    $display("weak-leading-final-pass");
  else
    $display("BAD-weak-leading-final-fail");

  strong_intermediate: assert property (@(posedge strong_source)
      strong(1'b1 ##0 @(posedge strong_middle) 1'b1 ##1
             @(posedge strong_destination) 1'b1))
    $display("BAD-strong-intermediate-final-pass");
  else
    $display("strong-intermediate-final-fail");

  weak_assume: assume property (@(posedge weak_source)
                                1'b1 ##1
                                @(posedge weak_destination) 1'b1)
    $display("weak-assume-final-pass");
  else
    $display("BAD-weak-assume-final-fail");

  strong_cover: cover property (@(posedge strong_source)
      1'b1 ##0 @(posedge strong_middle) 1'b1 ##1
      @(posedge strong_destination) 1'b1)
    $display("BAD-strong-cover-final-pass");

  restrict property (@(posedge strong_source)
      1'b1 ##0 @(posedge strong_middle) 1'b1 ##1
      @(posedge strong_destination) 1'b1);

  off_preserves: assert property (@(posedge off_source)
                                  1'b1 ##1 @(posedge off_destination) 1'b1)
    $display("off-preserved-admitted-token");
  else
    $display("BAD-off-token-fail");

  initial begin
    #1 begin
      weak_source = 1;
      strong_source = 1;
      strong_middle = 1;
      off_source = 1;
    end
    #1 $assertoff(0, off_preserves);
    #1 $asserton(0, off_preserves);
    off_destination = 1;
    #1;
    $finish;
  end
endmodule

// CHECK-DAG: weak-leading-final-pass
// CHECK-DAG: weak-assume-final-pass
// CHECK-DAG: strong-intermediate-final-fail
// CHECK-DAG: off-preserved-admitted-token
// CHECK-NOT: BAD-
