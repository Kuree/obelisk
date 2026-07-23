// RUN: not obelisk -emit-sim %s 2>&1 | FileCheck %s

module function_fork_invalid;
  function bit launch;
    launch = 0;
    fork
      $display("child");
    join
  endfunction
endmodule

// CHECK: statements that pass time are not allowed in this context
