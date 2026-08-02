// RUN: obelisk -O2 --native-scheduler=auto %s -o %t.auto
// RUN: obelisk -O2 --native-scheduler=generic %s -o %t.generic
// RUN: %t.auto > %t.auto.out
// RUN: %t.generic > %t.generic.out
// RUN: diff -u %t.generic.out %t.auto.out
// RUN: FileCheck %s < %t.auto.out

// Periods 4 and 6 produce coincident edges at time 6, 18, and 30.  The shared
// NBA root makes same-slot process ordering observable, while the independent
// counters catch lost or duplicated edges.  Auto must preserve the generic
// scheduler oracle when more than one periodic generator is present.
module native_periodic_multiclock;
  bit clock2;
  bit clock3;
  int count2;
  int count3;
  int shared;

  always #2 clock2 = ~clock2;
  always #3 clock3 = ~clock3;

  always @(posedge clock2) begin
    count2 <= count2 + 1;
    shared <= shared + 1;
  end

  always @(posedge clock3) begin
    count3 <= count3 + 1;
    shared <= shared + 10;
  end

  initial begin
    #31;
    $display("multiclock count2=%0d count3=%0d shared=%0d",
             count2, count3, shared);
    $finish;
  end
endmodule

// CHECK: multiclock count2=8 count3=5
