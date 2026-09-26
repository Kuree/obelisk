// XFAIL: *
// Slang v11 cannot infer assignment-pattern target types from comparisons.
// RUN: obelisk -O0 --vpi=off %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: obelisk -O0 --vpi=off --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.bytecode.out %t.native.out
// RUN: FileCheck %s < %t.native.out

module assignment_pattern_comparison;
  typedef struct packed {
    logic [7:0] first;
    logic [7:0] second;
  } pair_t;

  int fixed_array[3];
  int queue[$];
  pair_t pair;

  initial begin
    fixed_array = '{10, 20, 30};
    queue = '{4, 5, 6};
    pair = '{first: 8'haa, second: 8'h55};

    assert (fixed_array == '{10, 20, 30});
    assert ('{10, 20, 30} == fixed_array);
    assert (fixed_array != '{10, 20, 31});
    assert (queue == '{4, 5, 6});
    assert ('{4, 5, 7} != queue);
    assert (pair == '{first: 8'haa, second: 8'h55});
    assert ('{first: 8'haa, second: 8'h54} != pair);
    $display("assignment pattern comparisons passed");
  end

  // CHECK: assignment pattern comparisons passed
endmodule
