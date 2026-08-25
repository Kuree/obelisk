// RUN: %llvm_dist/bin/clang --target=x86_64-unknown-linux-gnu -fPIC -c \
// RUN:   %S/Inputs/dpi_integer_time.c \
// RUN:   -I$(obelisk --print-resource-dir)/include -o %t.o
// RUN: obelisk -fno-lto -O0 --vpi=off %s %t.o -o %t.o0.native
// RUN: %t.o0.native | FileCheck %s
// RUN: obelisk -fno-lto -O0 --vpi=off --execution-tier=bytecode %s %t.o -o %t.o0.bytecode
// RUN: %t.o0.bytecode | FileCheck %s
// RUN: obelisk -fno-lto -O3 --vpi=off %s %t.o -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk -fno-lto -O3 --vpi=off --execution-tier=bytecode %s %t.o -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s
// RUN: obelisk --emit-dpi-header %s -o %t.h
// RUN: FileCheck %s --check-prefix=HEADER < %t.h
// RUN: %llvm_dist/bin/clang -x c -fsyntax-only -include %t.h \
// RUN:   -I$(obelisk --print-resource-dir)/include /dev/null

module dpi_integer_time;
  import "DPI" function int dpi_legacy(input int value);
  import "DPI-C" function void dpi_integer_time_io(
      input integer integer_input, input time time_input,
      output integer integer_output, output time time_output,
      inout integer integer_inout, inout time time_inout);

  integer integer_input;
  integer integer_output;
  integer integer_inout;
  time time_input;
  time time_output;
  time time_inout;
  int legacy_result;

  initial begin
    integer_input = 32'b1010_xz01_0011_0101_0110_1001_1110_0001;
    time_input = 64'b10xz_0001_0010_0011_0100_0101_0110_0111_1000_1001_1010_1011_1100_1101_1110_1111;
    integer_inout = '0;
    time_inout = '0;
    legacy_result = dpi_legacy(41);
    dpi_integer_time_io(integer_input, time_input, integer_output, time_output,
                        integer_inout, time_inout);
    if (integer_output !== integer_input || integer_inout !== integer_input)
      $fatal(1, "integer DPI four-state transport mismatch");
    if (time_output !== time_input || time_inout !== time_input)
      $fatal(1, "time DPI four-state transport mismatch");
    if (legacy_result != 42)
      $fatal(1, "deprecated DPI spelling did not bind");
    $display("DPI INTEGER TIME PASS");
  end
endmodule

// CHECK: DPI INTEGER TIME PASS
// HEADER-DAG: int32_t dpi_legacy(int32_t arg0);
// HEADER-DAG: void dpi_integer_time_io(const svLogicVecVal *arg0, const svLogicVecVal *arg1, svLogicVecVal *arg2, svLogicVecVal *arg3, svLogicVecVal *arg4, svLogicVecVal *arg5);
