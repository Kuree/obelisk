// RUN: obelisk -O0 %s -o %t.native
// RUN: %t.native > %t.native.out
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.bytecode.out %t.native.out
// RUN: FileCheck %s < %t.native.out

module native_nested_block_disable_loop_state;
  int a;
  int b;

  initial begin
    for (int exit_a = 0; exit_a < 2; ++exit_a) begin
      for (int exit_b = 0; exit_b < 3; ++exit_b) begin
        for (a = 0; a < 3; ++a) begin : a_loop
          for (b = 0; b < 3; ++b) begin : b_loop
            $write("%0d%0d:%0d%0d ", exit_a, exit_b, a, b);
            if (exit_b == 1 && b == 1)
              disable b_loop;
            if (exit_b == 2 && a == 1)
              disable a_loop;
          end
          if (exit_a == 1 && a == 1)
            disable a_loop;
        end
        $display;
      end
    end
    $display("PASS");
  end

  // CHECK: 00:00 00:01 00:02 00:10 00:11 00:12 00:20 00:21 00:22
  // CHECK: 01:00 01:01 01:02 01:10 01:11 01:12 01:20 01:21 01:22
  // CHECK: 02:00 02:01 02:02 02:10 02:20 02:21 02:22
  // CHECK: 10:00 10:01 10:02 10:10 10:11 10:12 10:20 10:21 10:22
  // CHECK: 11:00 11:01 11:02 11:10 11:11 11:12 11:20 11:21 11:22
  // CHECK: 12:00 12:01 12:02 12:10 12:20 12:21 12:22
  // CHECK: PASS
endmodule
