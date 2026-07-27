// RUN: not obelisk -emit-sim %s 2>&1 | FileCheck %s

module final_assertion_read_only;
  logic value;
  task set_value;
    value = 1'b1;
  endtask
  initial assert final (1'b1) set_value();
endmodule

// CHECK: is not permitted in a read-only postponed code unit
