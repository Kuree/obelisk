// RUN: obelisk -fno-lto -O0 --vpi=off %s -o %t.o0.native
// RUN: %t.o0.native +INT=1234 '+IP%P101' +FOUR=10xz +REAL=1.25 +STR=hello | FileCheck %s
// RUN: obelisk -fno-lto -O0 --vpi=off --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode +INT=1234 '+IP%P101' +FOUR=10xz +REAL=1.25 +STR=hello | FileCheck %s
// RUN: obelisk -fno-lto -O3 --vpi=off %s -o %t.o3.native
// RUN: %t.o3.native +INT=1234 '+IP%P101' +FOUR=10xz +REAL=1.25 +STR=hello | FileCheck %s
// RUN: obelisk -fno-lto -O3 --vpi=off --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode +INT=1234 '+IP%P101' +FOUR=10xz +REAL=1.25 +STR=hello | FileCheck %s

// IEEE 1800-2017 21.6 accepts a string-like expression as the user string.
// A doubled percent in its prefix matches one literal percent.
module dynamic_value_plusargs;
  string dynamic_decimal;
  string dynamic_four_state;
  string dynamic_real;
  string dynamic_string;
  string invalid_format;
  logic [63:0] dynamic_binary;
  int decimal;
  logic [7:0] binary;
  logic [7:0] four_state;
  real real_value;
  string string_value;
  int preserved;
  int status_decimal;
  int status_binary;
  int status_literal;
  int status_four_state;
  int status_real;
  int status_string;
  int status_invalid;

  initial begin
    dynamic_decimal = "INT=%d";
    dynamic_binary = "IP%%P%b";
    dynamic_four_state = "FOUR=%b";
    dynamic_real = "REAL=%f";
    dynamic_string = "STR=%s";
    invalid_format = "INT=";
    decimal = 0;
    binary = 0;
    four_state = 0;
    real_value = 0.0;
    string_value = "none";
    preserved = 77;
    status_decimal = $value$plusargs(dynamic_decimal, decimal);
    status_binary = $value$plusargs(dynamic_binary, binary);
    status_literal = $value$plusargs("IP%%P%b", binary);
    status_four_state = $value$plusargs(dynamic_four_state, four_state);
    status_real = $value$plusargs(dynamic_real, real_value);
    status_string = $value$plusargs(dynamic_string, string_value);
    status_invalid = $value$plusargs(invalid_format, preserved);
    $display("dynamic=%0d:%0d:%0d:%0d:%0d", status_decimal, decimal,
             status_binary, binary, status_literal);
    $display("typed=%0d:%b:%0d:%.2f:%0d:%s:%0d:%0d", status_four_state,
             four_state, status_real, real_value, status_string, string_value,
             status_invalid, preserved);
  end
endmodule

// CHECK: dynamic=1:1234:1:5:1
// CHECK: typed=1:000010xz:1:1.25:1:hello:0:77
