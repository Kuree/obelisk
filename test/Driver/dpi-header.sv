// RUN: obelisk --emit-dpi-header %s -o %t.h
// RUN: FileCheck %s --check-prefix=HEADER < %t.h
// RUN: %llvm_dist/bin/clang -x c -fsyntax-only -include %t.h \
// RUN:   -I$(obelisk --print-resource-dir)/include /dev/null
// RUN: %llvm_dist/bin/clang++ -x c++ -fsyntax-only -include %t.h \
// RUN:   -I$(obelisk --print-resource-dir)/include /dev/null
// RUN: test -f "$(obelisk --print-resource-dir)/include/svdpi.h"
// RUN: not obelisk --emit-dpi-header --dpi-link=%t.o %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=LINK-ONLY

module dpi_header;
  typedef struct packed {
    logic [2:0] state;
    bit valid;
  } packed_struct_t;
  typedef union packed {
    bit [7:0] left;
    bit [7:0] right;
  } packed_union_t;
  typedef enum int unsigned { enum_zero = 0 } unsigned_enum_t;

  import "DPI-C" pure renamed = function longint calculate(
      input byte small_value, input logic flag);
  import "DPI-C" context task transfer(
      input logic [64:0] source, output bit [32:0] destination);
  import "DPI-C" function byte unsigned unsigned_types(
      input byte unsigned small_value, input unsigned_enum_t enum_value,
      input packed_struct_t structure, output packed_union_t union_value);
endmodule

// HEADER: #include <svdpi.h>
// HEADER: extern "C" {
// HEADER: int64_t renamed(int8_t arg0, svLogic arg1);
// HEADER: int transfer(const svLogicVecVal *arg0, svBitVecVal *arg1);
// HEADER: uint8_t unsigned_types(uint8_t arg0, uint32_t arg1, const svLogicVecVal *arg2, svBitVecVal *arg3);
// LINK-ONLY: --dpi-link is only valid when linking a native executable
