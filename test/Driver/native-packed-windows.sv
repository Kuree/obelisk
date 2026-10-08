// RUN: obelisk -O0 --vpi=off --native-scheduler=eval %s -o %t.o0
// RUN: %t.o0 | FileCheck %s
// RUN: obelisk -O3 --vpi=off --native-scheduler=eval %s -o %t.o3
// RUN: %t.o3 | FileCheck %s
// RUN: obelisk -O3 --vpi=full %s -o %t.full
// RUN: %t.full | FileCheck %s
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | FileCheck %s
// CHECK: packed windows passed

module window_case #(parameter W = 3)(output bit done);
  logic [W-1:0] data[25];
  logic [W*25-1:0] packed_data;
  logic [W*25-1:0] expected;
  logic [31:0] index;
  int i, j;
  initial begin
    done = 0;
    for (i = 0; i < 25; ++i) data[i] = 'z;
    for (i = 0; i < 25; ++i) begin
      index = i;
      data[index] = W'(i * 37 + 5);
      #1;
      for (j = 0; j < 25; ++j)
        if (data[j] !== (j <= i ? W'(j * 37 + 5) : {W{1'bz}}))
          $fatal(1, "array window W=%0d i=%0d j=%0d", W, i, j);
    end
    data[index] = 'x;
    #1;
    if (data[index] !== 'x) $fatal(1, "X at array tail");
    index = 25;
    data[index] = '0;
    if (data[index] !== 'x || data[24] !== 'x) $fatal(1, "array bound");
    index = 'x;
    data[index] = '0;
    if (data[index] !== 'x || data[0] !== W'(5)) $fatal(1, "X selector");
    // Cover every byte alignment, both clipped ends, and invalid selects.
    for (i = -W; i <= W*25; ++i) begin
      packed_data = '1;
      expected = '1;
      index = i;
      packed_data[$signed(index) +: W] = 'z;
      for (j = 0; j < W*25; ++j)
        if (j >= i && j < i+W) expected[j] = 1'bz;
      #1;
      if (packed_data !== expected) $fatal(1, "clipped window W=%0d i=%0d", W, i);
      for (j = 0; j < W; ++j)
        if (i+j >= 0 && i+j < W*25)
          expected[j] = 1'bz;
        else
          expected[j] = 1'bx;
      if (packed_data[$signed(index) +: W] !== expected[W-1:0])
        $fatal(1, "clipped read W=%0d i=%0d", W, i);
    end
    done = 1;
  end
endmodule

module native_packed_windows;
  wire [5:0] done;
  window_case #(3) w3(done[0]);
  window_case #(17) w17(done[1]);
  window_case #(33) w33(done[2]);
  window_case #(57) w57(done[3]);
  window_case #(58) w58(done[4]);
  window_case #(64) w64(done[5]);
  initial begin
    #2000;
    if (done !== 6'b111111) $fatal(1, "window checks unfinished");
    $display("packed windows passed");
    $finish;
  end
endmodule
