// RUN: obelisk -fno-lto -O0 --vpi=off %s -o %t.o0.native
// RUN: %t.o0.native | FileCheck %s
// RUN: obelisk -fno-lto -O0 --vpi=off --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode | FileCheck %s
// RUN: obelisk -fno-lto -O3 --vpi=off %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk -fno-lto -O3 --vpi=off --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s

// IEEE 1800-2017 20.18 permits both task and function use, an omitted command,
// and a runtime string expression. Host wait statuses are returned as shell
// exit codes rather than their encoded POSIX representation.
module system_command;
  string dynamic_command;
  int empty_status;
  int literal_status;
  int dynamic_status;

  initial begin
    dynamic_command = "exit 9";
    empty_status = $system();
    $system("exit 0");
    literal_status = $system("exit 7");
    dynamic_status = $system(dynamic_command);
    $display("system=%0d:%0d:%0d", empty_status, literal_status,
             dynamic_status);
  end
endmodule

// CHECK: system=0:7:9
