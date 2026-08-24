// RUN: obelisk -fno-lto -O0 --vpi=off %s -o %t.o0.native
// RUN: %t.o0.native +OUT=%t.o0.native.mem | FileCheck %s
// RUN: obelisk -fno-lto -O0 --vpi=off --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode +OUT=%t.o0.bytecode.mem | FileCheck %s
// RUN: obelisk -fno-lto -O3 --vpi=off %s -o %t.o3.native
// RUN: %t.o3.native +OUT=%t.o3.native.mem | FileCheck %s
// RUN: obelisk -fno-lto -O3 --vpi=off --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode +OUT=%t.o3.bytecode.mem | FileCheck %s

// IEEE 1800-2017 21.4 writes one packed memory word per line. Explicit
// descending ranges retain their direction, four-state digits survive a
// round trip, and associative memories carry their sparse addresses.
module writemem_roundtrip;
  string base;
  logic [7:0] fixed [2:5];
  logic [7:0] fixed_read [0:2];
  logic [5:0] dynamic [];
  logic [5:0] dynamic_read [];
  logic [3:0] queue [$];
  logic [3:0] queue_read [$];
  logic [3:0] matrix [1:2][4:5];
  logic [3:0] matrix_read [1:2][4:5];
  logic [7:0] sparse [int];
  int descriptor;
  int character;

  initial begin
    if (!$value$plusargs("OUT=%s", base))
      $fatal(0, "missing output path");

    fixed[2] = 8'h12;
    fixed[3] = 8'b10xz0011;
    fixed[4] = 8'h45;
    fixed[5] = 8'h67;
    $writememb({base, ".fixed"}, fixed, 5, 3);
    $readmemb({base, ".fixed"}, fixed_read);
    if (fixed_read[0] !== 8'h67 || fixed_read[1] !== 8'h45 ||
        fixed_read[2] !== 8'b10xz0011)
      $fatal(0, "fixed round trip failed");

    dynamic = new[3];
    dynamic_read = new[3];
    dynamic[0] = 6'h02;
    dynamic[1] = 6'bxx0001;
    dynamic[2] = 6'h3e;
    $writememh({base, ".dynamic"}, dynamic);
    $readmemh({base, ".dynamic"}, dynamic_read);
    if (dynamic_read[0] !== 6'h02 || dynamic_read[1] !== 6'bxx0001 ||
        dynamic_read[2] !== 6'h3e)
      $fatal(0, "dynamic round trip failed");
    dynamic = new[0];
    $writememh({base, ".empty"}, dynamic);
    descriptor = $fopen({base, ".empty"}, "r");
    character = $fgetc(descriptor);
    $fclose(descriptor);
    if (character != -1)
      $fatal(0, "empty dynamic memory produced data");

    queue.push_back(4'h1);
    queue.push_back(4'bzz00);
    queue.push_back(4'he);
    queue_read = '{0, 0, 0};
    $writememb({base, ".queue"}, queue);
    $readmemb({base, ".queue"}, queue_read);
    if (queue_read.size() != 3 || queue_read[0] !== 4'h1 ||
        queue_read[1] !== 4'bzz00 || queue_read[2] !== 4'he)
      $fatal(0, "queue round trip failed");

    matrix[1][4] = 4'h1;
    matrix[1][5] = 4'h2;
    matrix[2][4] = 4'hd;
    matrix[2][5] = 4'he;
    $writememh({base, ".matrix"}, matrix);
    $readmemh({base, ".matrix"}, matrix_read);
    if (matrix_read[1][4] !== 4'h1 || matrix_read[1][5] !== 4'h2 ||
        matrix_read[2][4] !== 4'hd || matrix_read[2][5] !== 4'he)
      $fatal(0, "multidimensional round trip failed");

    sparse[-2] = 8'h12;
    sparse[5] = 8'hx5;
    sparse[300] = 8'hfe;
    $writememh({base, ".sparse"}, sparse);
    sparse.delete();
    $readmemh({base, ".sparse"}, sparse);
    if (sparse.size() != 3 || sparse[-2] !== 8'h12 ||
        sparse[5] !== 8'hx5 || sparse[300] !== 8'hfe)
      $fatal(0, "associative round trip failed");
    $writememh({base, ".sparse_range"}, sparse, 300, 5);
    sparse.delete();
    $readmemh({base, ".sparse_range"}, sparse);
    if (sparse.size() != 2 || sparse[5] !== 8'hx5 ||
        sparse[300] !== 8'hfe || sparse.exists(-2))
      $fatal(0, "associative range round trip failed");

    $display("WRITEMEM PASS");
  end
endmodule

// CHECK: WRITEMEM PASS
