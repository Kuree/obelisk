// XFAIL: *
// Slang v11 does not bind recursive contextual assignment-pattern defaults.
// RUN: obelisk -fno-lto -O0 --vpi=off %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: obelisk -fno-lto -O0 --vpi=off --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.bytecode.out %t.native.out
// RUN: FileCheck %s < %t.native.out

module recursive_assignment_pattern_default;
  int scalar_default[2][3];
  int row[3];
  int array_default[2][3];
  int mixed_default[2][3];

  initial begin
    scalar_default = '{default: 7};
    row = '{1, 2, 3};
    array_default = '{default: row};
    mixed_default = '{0: '{0: 4, default: 5},
                      default: '{default: 6}};

    foreach (scalar_default[i, j])
      assert (scalar_default[i][j] == 7);
    foreach (array_default[i, j])
      assert (array_default[i][j] == row[j]);
    assert (mixed_default[0][0] == 4);
    assert (mixed_default[0][1] == 5);
    assert (mixed_default[0][2] == 5);
    assert (mixed_default[1][0] == 6);
    assert (mixed_default[1][1] == 6);
    assert (mixed_default[1][2] == 6);
    $display("recursive assignment pattern defaults passed");
  end

  // CHECK: recursive assignment pattern defaults passed
endmodule
