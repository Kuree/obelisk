// RUN: obelisk -fno-lto -O0 --top=native_interface_array_handles %s -o %t.o0.native
// RUN: %t.o0.native | FileCheck %s
// RUN: obelisk -fno-lto -O0 --native-scheduler=generic --top=native_interface_array_handles %s -o %t.o0.generic
// RUN: %t.o0.generic | FileCheck %s
// RUN: obelisk -fno-lto -O0 --execution-tier=bytecode --top=native_interface_array_handles %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode | FileCheck %s
// RUN: obelisk -fno-lto -O3 --top=native_interface_array_handles %s -o %t.o3.native
// RUN: %t.o3.native | FileCheck %s
// RUN: obelisk -fno-lto -O3 --execution-tier=bytecode --top=native_interface_array_handles %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode | FileCheck %s

interface array_handle_if;
  logic [7:0] value;
endinterface

class array_handle_holder;
  virtual array_handle_if descending[2:0];
  virtual array_handle_if matrix[1:0][0:1];

  function new(virtual array_handle_if source[2:0],
               virtual array_handle_if nested[1:0][0:1]);
    descending = source;
    matrix = nested;
  endfunction

  function int checksum();
    return descending[2].value * 100 +
           descending[1].value * 10 +
           descending[0].value +
           matrix[1][0].value * 1000 +
           matrix[1][1].value * 100 +
           matrix[0][0].value * 10 +
           matrix[0][1].value;
  endfunction
endclass

module native_interface_array_handles;
  array_handle_if descending[2:0]();
  array_handle_if ascending[0:2]();
  array_handle_if matrix[1:0][0:1]();
  virtual array_handle_if nullable[2:0];
  array_handle_holder holder;

  function automatic int ascending_checksum(
      virtual array_handle_if source[0:2]);
    return source[0].value * 100 +
           source[1].value * 10 +
           source[2].value;
  endfunction

  function automatic int nested_checksum(
      virtual array_handle_if source[1:0][0:1]);
    return source[1][0].value * 1000 +
           source[1][1].value * 100 +
           source[0][0].value * 10 +
           source[0][1].value;
  endfunction

  initial begin
    descending[2].value = 1;
    descending[1].value = 2;
    descending[0].value = 3;
    ascending[0].value = 4;
    ascending[1].value = 5;
    ascending[2].value = 6;
    matrix[1][0].value = 7;
    matrix[1][1].value = 8;
    matrix[0][0].value = 9;
    matrix[0][1].value = 10;

    holder = new(descending, matrix);
    if (holder.checksum() != 8023)
      $fatal(1, "constructor array binding failed: %0d", holder.checksum());
    if (ascending_checksum(ascending) != 456)
      $fatal(1, "ascending array binding failed");
    if (nested_checksum(matrix) != 7900)
      $fatal(1, "nested array binding failed");

    nullable = descending;
    if (nullable[2] == nullable[1] || nullable[1] == nullable[0])
      $fatal(1, "distinct element identities collapsed");
    nullable[1] = null;
    nullable[0] = nullable[2];
    if (nullable[1] != null || nullable[0] != nullable[2])
      $fatal(1, "null or alias handle did not survive array assignment");

    $display("interface-array-handles-pass");
    $finish;
  end
endmodule

// CHECK: interface-array-handles-pass
