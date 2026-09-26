// RUN: obelisk -O0 --vpi=off %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: obelisk -O0 --vpi=off --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.bytecode.out %t.native.out
// RUN: FileCheck %s < %t.native.out

// IEEE 1800-2017 12.7.3: omitted foreach variables suppress iteration over
// their dimensions; an all-omitted list therefore executes no statement.
module foreach_all_iterators_omitted;
  int fixed[2][3];
  int dynamic[];
  int count;

  initial begin
    dynamic = new[4];
    foreach (fixed[])
      ++count;
    foreach (fixed[,])
      ++count;
    foreach (dynamic[])
      ++count;
    assert (count == 0);
    $display("all foreach iterators omitted");
  end

  // CHECK: all foreach iterators omitted
endmodule
