// RUN: obelisk -fno-lto -O0 --vpi=off %s -o %t.o0.native
// RUN: %t.o0.native +OUT=%t.o0.native 2>&1 | FileCheck %s
// RUN: obelisk -fno-lto -O0 --vpi=off --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode +OUT=%t.o0.bytecode 2>&1 | FileCheck %s
// RUN: obelisk -fno-lto -O3 --vpi=off %s -o %t.o3.native
// RUN: %t.o3.native +OUT=%t.o3.native 2>&1 | FileCheck %s
// RUN: obelisk -fno-lto -O3 --vpi=off --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode +OUT=%t.o3.bytecode 2>&1 | FileCheck %s

// IEEE 1800-2017 21.4 and 21.5: memory ranges use numerical address order,
// independently of the declaration direction. Explicit start/finish bounds
// may select either direction, while @ records reposition within that range.
module readwritemem_ranges;
  string base;
  logic [7:0] ascending [2:5];
  logic [7:0] descending [5:2];
  logic [7:0] start_only [5:2];
  logic [7:0] extra [2:5];
  logic [7:0] short [5:2];
  logic [7:0] addressed [5:2];
  logic [7:0] bad_bounds [2:5];
  logic [7:0] bad_address [2:5];
  logic [7:0] matrix [2:1][5:4];
  logic [7:0] dynamic [];
  logic [7:0] queue [$];
  logic [7:0] output_memory [5:2];
  int descriptor;
  int converted;
  int words [0:3];

  task automatic write_file(input string path, input string contents);
    int fd;
    fd = $fopen(path, "w");
    if (!fd)
      $fatal(0, "cannot create %s", path);
    $fwrite(fd, "%s", contents);
    $fclose(fd);
  endtask

  initial begin
    if (!$value$plusargs("OUT=%s", base))
      $fatal(0, "missing output path");

    foreach (ascending[index]) ascending[index] = 8'hee;
    foreach (descending[index]) descending[index] = 8'hee;
    foreach (start_only[index]) start_only[index] = 8'hee;
    foreach (extra[index]) extra[index] = 8'hee;
    foreach (short[index]) short[index] = 8'hee;
    foreach (addressed[index]) addressed[index] = 8'hee;
    foreach (bad_bounds[index]) bad_bounds[index] = 8'hee;
    foreach (bad_address[index]) bad_address[index] = 8'hee;
    foreach (matrix[outer, inner]) matrix[outer][inner] = 8'hee;

    write_file({base, ".default"}, "11 22 33 44 55\n");
    $readmemh({base, ".default"}, ascending);
    $readmemh({base, ".default"}, descending);
    if (ascending[2] !== 8'h11 || ascending[3] !== 8'h22 ||
        ascending[4] !== 8'h33 || ascending[5] !== 8'h44 ||
        descending[2] !== 8'h11 || descending[3] !== 8'h22 ||
        descending[4] !== 8'h33 || descending[5] !== 8'h44)
      $fatal(0, "default numerical order failed");

    write_file({base, ".start"}, "a1 a2 a3\n");
    $readmemh({base, ".start"}, start_only, 4);
    if (start_only[2] !== 8'hee || start_only[3] !== 8'hee ||
        start_only[4] !== 8'ha1 || start_only[5] !== 8'ha2)
      $fatal(0, "start-only range failed");

    // The binary spelling shares the same range loop and must diagnose the
    // first surplus word exactly as the hexadecimal spelling does.
    write_file({base, ".extra"},
               "00000001 00000010 00000011\n");
    $readmemb({base, ".extra"}, extra, 2, 3);
    if (extra[2] !== 8'h01 || extra[3] !== 8'h02 ||
        extra[4] !== 8'hee || extra[5] !== 8'hee)
      $fatal(0, "extra-data range failed");

    write_file({base, ".short"}, "aa bb\n");
    $readmemh({base, ".short"}, short, 5, 3);
    if (short[5] !== 8'haa || short[4] !== 8'hbb ||
        short[3] !== 8'hee || short[2] !== 8'hee)
      $fatal(0, "insufficient-data range failed");

    write_file({base, ".address"}, "@4 0a 0b @5 0c\n");
    $readmemh({base, ".address"}, addressed, 5, 2);
    if (addressed[5] !== 8'h0c || addressed[4] !== 8'h0a ||
        addressed[3] !== 8'h0b || addressed[2] !== 8'hee)
      $fatal(0, "address redirection failed");

    write_file({base, ".matrix"}, "31 32 41 42\n");
    $readmemh({base, ".matrix"}, matrix);
    if (matrix[1][4] !== 8'h31 || matrix[1][5] !== 8'h32 ||
        matrix[2][4] !== 8'h41 || matrix[2][5] !== 8'h42)
      $fatal(0, "multidimensional numerical order failed");

    // Empty variable-size memories have no default address to visit. They do
    // not resize and do not reject an otherwise valid omitted range.
    write_file({base, ".empty"}, "de ad\n");
    dynamic = new[0];
    queue.delete();
    $readmemb({base, ".empty"}, dynamic);
    $readmemh({base, ".empty"}, queue);
    if (dynamic.size() != 0 || queue.size() != 0)
      $fatal(0, "empty containers resized");

    output_memory[2] = 8'h12;
    output_memory[3] = 8'h23;
    output_memory[4] = 8'h34;
    output_memory[5] = 8'h45;
    $writememh({base, ".write-default"}, output_memory);
    descriptor = $fopen({base, ".write-default"}, "r");
    converted = $fscanf(descriptor, "%h %h %h %h",
                        words[0], words[1], words[2], words[3]);
    $fclose(descriptor);
    if (converted != 4 || words[0] != 'h12 || words[1] != 'h23 ||
        words[2] != 'h34 || words[3] != 'h45)
      $fatal(0, "default write order failed");

    $writememh({base, ".write-start"}, output_memory, 4);
    descriptor = $fopen({base, ".write-start"}, "r");
    converted = $fscanf(descriptor, "%h %h %h",
                        words[0], words[1], words[2]);
    $fclose(descriptor);
    if (converted != 2 || words[0] != 'h34 || words[1] != 'h45)
      $fatal(0, "start-only write failed");

    $writememh({base, ".write-descending"}, output_memory, 5, 3);
    descriptor = $fopen({base, ".write-descending"}, "r");
    converted = $fscanf(descriptor, "%h %h %h %h",
                        words[0], words[1], words[2], words[3]);
    $fclose(descriptor);
    if (converted != 3 || words[0] != 'h45 || words[1] != 'h34 ||
        words[2] != 'h23)
      $fatal(0, "descending write failed");

    // Task bounds, @ records, and explicit bounds on empty containers each
    // report an address error and leave every not-yet-selected word alone.
    write_file({base, ".bad-bounds"}, "77\n");
    $readmemh({base, ".bad-bounds"}, bad_bounds, 1, 2);
    write_file({base, ".bad-address"}, "0a @6 0b\n");
    $readmemh({base, ".bad-address"}, bad_address, 2, 5);
    $readmemh({base, ".empty"}, dynamic, 0, 0);
    if (bad_bounds[2] !== 8'hee || bad_bounds[5] !== 8'hee ||
        bad_address[2] !== 8'h0a || bad_address[3] !== 8'hee)
      $fatal(0, "out-of-range update behavior failed");

    $display("MEMORY RANGE PASS");
  end
endmodule

// CHECK-COUNT-1: WARNING: $readmemb: data word count does not match address range
// CHECK-COUNT-1: WARNING: $readmemh: data word count does not match address range
// CHECK-NOT: WARNING: $readmem
// CHECK-COUNT-3: ERROR: $readmemh: address is outside the selected memory range
// CHECK-NOT: ERROR: $readmemh
// CHECK: MEMORY RANGE PASS
