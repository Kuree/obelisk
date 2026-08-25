// RUN: obelisk -fno-lto -O0 --vpi=off %s -o %t.o0.native
// RUN: %t.o0.native '+DUP=12junk' '+DUP=42' +HELLO +B=10xz +O=7z +D=-1 '+H=123456789abcdef0123456789abcdef0' +X=dead +E=1.25e2 +F=-3.5 +G=.625 +S=hello_world '+PCT%KEY=101' +EMPTY= +BADREAL=1.25junk +TWOSTATE=12junk +DYN=255 +REALINT=12 +PS=xy + | FileCheck %s
// RUN: obelisk -fno-lto -O0 --vpi=off --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode '+DUP=12junk' '+DUP=42' +HELLO +B=10xz +O=7z +D=-1 '+H=123456789abcdef0123456789abcdef0' +X=dead +E=1.25e2 +F=-3.5 +G=.625 +S=hello_world '+PCT%KEY=101' +EMPTY= +BADREAL=1.25junk +TWOSTATE=12junk +DYN=255 +REALINT=12 +PS=xy + | FileCheck %s
// RUN: obelisk -fno-lto -O3 --vpi=off %s -o %t.o3.native
// RUN: %t.o3.native '+DUP=12junk' '+DUP=42' +HELLO +B=10xz +O=7z +D=-1 '+H=123456789abcdef0123456789abcdef0' +X=dead +E=1.25e2 +F=-3.5 +G=.625 +S=hello_world '+PCT%KEY=101' +EMPTY= +BADREAL=1.25junk +TWOSTATE=12junk +DYN=255 +REALINT=12 +PS=xy + | FileCheck %s
// RUN: obelisk -fno-lto -O3 --vpi=off --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode '+DUP=12junk' '+DUP=42' +HELLO +B=10xz +O=7z +D=-1 '+H=123456789abcdef0123456789abcdef0' +X=dead +E=1.25e2 +F=-3.5 +G=.625 +S=hello_world '+PCT%KEY=101' +EMPTY= +BADREAL=1.25junk +TWOSTATE=12junk +DYN=255 +REALINT=12 +PS=xy + | FileCheck %s

// IEEE 1800-2017 21.6: argv order selects the first prefix match, every
// conversion consumes the complete remainder, and a matched malformed field
// still returns one while writing X (or zero in a 2-state/real destination).
module plusargs_conformance;
  logic [3:0] binary;
  logic [5:0] octal;
  logic [11:0] decimal;
  logic [128:0] wide_hex;
  logic [15:0] x_hex;
  logic [2:0] percent_binary;
  logic [31:0] malformed;
  bit [31:0] malformed_two_state;
  logic [15:0] empty_number;
  logic [31:0] missed;
  logic [31:0] packed_string;
  real exponential;
  real fixed_real;
  real general_real;
  real malformed_real;
  real integral_to_real;
  string string_value;
  string empty_string;
  string first_plusarg;
  string invalid_format;
  logic [8*16-1:0] packed_format;
  logic [8*8-1:0] packed_test_prefix;
  int status [0:22];

  initial begin
    binary = '0;
    octal = '0;
    decimal = '0;
    wide_hex = '0;
    x_hex = '0;
    percent_binary = '0;
    malformed = '0;
    malformed_two_state = '1;
    empty_number = '1;
    missed = 32'h12345678;
    packed_string = '0;
    exponential = 0.0;
    fixed_real = 0.0;
    general_real = 0.0;
    malformed_real = 9.0;
    integral_to_real = 0.0;
    string_value = "old";
    empty_string = "old";
    first_plusarg = "old";
    invalid_format = "DYN=";
    packed_format = "DYN=%000D";
    packed_test_prefix = "HEL";

    status[0] = $value$plusargs("B=%B", binary);
    status[1] = $value$plusargs("O=%o", octal);
    status[2] = $value$plusargs("D=%d", decimal);
    status[3] = $value$plusargs("H=%H", wide_hex);
    status[4] = $value$plusargs("X=%x", x_hex);
    status[5] = $value$plusargs("E=%E", exponential);
    status[6] = $value$plusargs("F=%f", fixed_real);
    status[7] = $value$plusargs("G=%G", general_real);
    status[8] = $value$plusargs("S=%S", string_value);
    status[9] = $value$plusargs("PCT%%KEY=%b", percent_binary);
    status[10] = $value$plusargs("DUP=%d", malformed);
    status[11] = $value$plusargs("TWOSTATE=%d", malformed_two_state);
    status[12] = $value$plusargs("EMPTY=%d", empty_number);
    status[13] = $value$plusargs("EMPTY=%s", empty_string);
    status[14] = $value$plusargs("MISSING=%h", missed);
    status[15] = $value$plusargs("BADREAL=%f", malformed_real);
    status[16] = $value$plusargs(packed_format, status[17]);
    status[18] = $value$plusargs("REALINT=%d", integral_to_real);
    status[19] = $value$plusargs("PS=%s", packed_string);
    status[20] = $value$plusargs("%s", first_plusarg);
    status[21] = $value$plusargs(invalid_format, status[22]);

    if (status[0] != 1 || binary !== 4'b10xz ||
        status[1] != 1 || octal !== 6'b111zzz ||
        status[2] != 1 || decimal !== 12'hfff ||
        status[3] != 1 ||
        wide_hex !== 129'h123456789abcdef0123456789abcdef0 ||
        status[4] != 1 || x_hex !== 16'hdead)
      $fatal(0, "integral conversions failed");
    if (status[5] != 1 || exponential != 125.0 ||
        status[6] != 1 || fixed_real != -3.5 ||
        status[7] != 1 || general_real != 0.625)
      $fatal(0, "real conversions failed");
    if (status[8] != 1 || string_value != "hello_world" ||
        status[9] != 1 || percent_binary !== 3'b101)
      $fatal(0, "string or percent conversion failed");
    if (status[10] != 1 || malformed !== 'x ||
        status[11] != 1 || malformed_two_state !== '0 ||
        status[15] != 1 || malformed_real != 0.0)
      $fatal(0, "malformed conversion failed");
    if (status[12] != 1 || empty_number !== '0 ||
        status[13] != 1 || empty_string != "" ||
        status[14] != 0 || missed !== 32'h12345678)
      $fatal(0, "empty or missing conversion failed");
    if (status[16] != 1 || status[17] != 255 ||
        status[18] != 1 || integral_to_real != 12.0 ||
        status[19] != 1 || packed_string !== 32'h00007879)
      $fatal(0, "dynamic or destination conversion failed");
    if (status[20] != 1 || first_plusarg != "DUP=12junk" ||
        status[21] != 0 || status[22] != 0)
      $fatal(0, "first match or invalid dynamic format failed");
    if (!$test$plusargs("DUP") || !$test$plusargs("") ||
        $test$plusargs("dup") ||
        !$test$plusargs(string'(packed_test_prefix)))
      $fatal(0, "prefix matching failed");

    $display("PLUSARGS PASS");
  end
endmodule

// CHECK: PLUSARGS PASS
