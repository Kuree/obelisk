// RUN: %obelisk --std=1800-2023 -emit-obelisk %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=PLAN
// RUN: %obelisk --std=1800-2023 -O0 -o %t.exe %s
// RUN: %t.exe --seed=13 | FileCheck %s --check-prefix=RUNTIME
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.5.9: 130 ordered bit fields, repeated edges and multiple
// operands retain conditional uniform sampling, and disabling the order block
// restores joint sampling. A 192-cast candidate expression exercises memoized
// dependency discovery; a failed checker call preserves state.
// PLAN-COUNT-2: simulation.random.solve_wide{{.*}}SAMP
// RUNTIME: linear lowering valid 1

`define CAST4(e) byte'(byte'(byte'(byte'(e))))
`define CAST16(e) `CAST4(`CAST4(`CAST4(`CAST4(e))))
`define CAST64(e) `CAST16(`CAST16(`CAST16(`CAST16(e))))
`define CAST192(e) `CAST64(`CAST64(`CAST64(e)))

class C;
  rand bit [129:0] v;
  rand bit [7:0] x, y;
  // The first bit decides the rest: with ordering it is sampled alone and
  // uniformly, jointly it is almost never set.
  constraint relation { v[0] -> v[129:1] == '0; y == `CAST192(x); }
  constraint ordered {
    solve v[0] before v[1]; solve v[1] before v[2]; solve v[2] before v[3];
    solve v[3] before v[4]; solve v[4] before v[5]; solve v[5] before v[6];
    solve v[6] before v[7]; solve v[7] before v[8]; solve v[8] before v[9];
    solve v[9] before v[10]; solve v[10] before v[11]; solve v[11] before v[12];
    solve v[12] before v[13]; solve v[13] before v[14]; solve v[14] before v[15];
    solve v[15] before v[16]; solve v[16] before v[17]; solve v[17] before v[18];
    solve v[18] before v[19]; solve v[19] before v[20]; solve v[20] before v[21];
    solve v[21] before v[22]; solve v[22] before v[23]; solve v[23] before v[24];
    solve v[24] before v[25]; solve v[25] before v[26]; solve v[26] before v[27];
    solve v[27] before v[28]; solve v[28] before v[29]; solve v[29] before v[30];
    solve v[30] before v[31]; solve v[31] before v[32]; solve v[32] before v[33];
    solve v[33] before v[34]; solve v[34] before v[35]; solve v[35] before v[36];
    solve v[36] before v[37]; solve v[37] before v[38]; solve v[38] before v[39];
    solve v[39] before v[40]; solve v[40] before v[41]; solve v[41] before v[42];
    solve v[42] before v[43]; solve v[43] before v[44]; solve v[44] before v[45];
    solve v[45] before v[46]; solve v[46] before v[47]; solve v[47] before v[48];
    solve v[48] before v[49]; solve v[49] before v[50]; solve v[50] before v[51];
    solve v[51] before v[52]; solve v[52] before v[53]; solve v[53] before v[54];
    solve v[54] before v[55]; solve v[55] before v[56]; solve v[56] before v[57];
    solve v[57] before v[58]; solve v[58] before v[59]; solve v[59] before v[60];
    solve v[60] before v[61]; solve v[61] before v[62]; solve v[62] before v[63];
    solve v[63] before v[64]; solve v[64] before v[65]; solve v[65] before v[66];
    solve v[66] before v[67]; solve v[67] before v[68]; solve v[68] before v[69];
    solve v[69] before v[70]; solve v[70] before v[71]; solve v[71] before v[72];
    solve v[72] before v[73]; solve v[73] before v[74]; solve v[74] before v[75];
    solve v[75] before v[76]; solve v[76] before v[77]; solve v[77] before v[78];
    solve v[78] before v[79]; solve v[79] before v[80]; solve v[80] before v[81];
    solve v[81] before v[82]; solve v[82] before v[83]; solve v[83] before v[84];
    solve v[84] before v[85]; solve v[85] before v[86]; solve v[86] before v[87];
    solve v[87] before v[88]; solve v[88] before v[89]; solve v[89] before v[90];
    solve v[90] before v[91]; solve v[91] before v[92]; solve v[92] before v[93];
    solve v[93] before v[94]; solve v[94] before v[95]; solve v[95] before v[96];
    solve v[96] before v[97]; solve v[97] before v[98]; solve v[98] before v[99];
    solve v[99] before v[100]; solve v[100] before v[101]; solve v[101] before v[102];
    solve v[102] before v[103]; solve v[103] before v[104]; solve v[104] before v[105];
    solve v[105] before v[106]; solve v[106] before v[107]; solve v[107] before v[108];
    solve v[108] before v[109]; solve v[109] before v[110]; solve v[110] before v[111];
    solve v[111] before v[112]; solve v[112] before v[113]; solve v[113] before v[114];
    solve v[114] before v[115]; solve v[115] before v[116]; solve v[116] before v[117];
    solve v[117] before v[118]; solve v[118] before v[119]; solve v[119] before v[120];
    solve v[120] before v[121]; solve v[121] before v[122]; solve v[122] before v[123];
    solve v[123] before v[124]; solve v[124] before v[125]; solve v[125] before v[126];
    solve v[126] before v[127]; solve v[127] before v[128]; solve v[128] before v[129];
    // Repeated edges and multiple operands add no new ordering.
    solve v[0] before v[1]; solve v[0], v[1] before v[2], v[3];
  }
endclass

module top;
  C obj;
  int valid, ones;
  bit [129:0] kept;
  initial begin
    obj = new;
    valid = 1;
    ones = 0;
    repeat (400) begin
      valid &= obj.randomize();
      ones += obj.v[0];
      valid &= (obj.v[0] ? obj.v[129:1] == '0 : 1) && obj.y == obj.x;
    end
    // Conditional sampling picks v[0] uniformly: 400 draws stay within 6 sigma.
    valid &= ones > 140 && ones < 260;
    // Disabling the order restores joint sampling over all 2^129 + 1 solutions.
    obj.ordered.constraint_mode(0);
    ones = 0;
    repeat (400) begin
      valid &= obj.randomize();
      ones += obj.v[0];
    end
    valid &= ones == 0;
    // A failing checker call preserves state.
    kept = obj.v;
    obj.y = obj.x + 8'd1;
    valid &= obj.randomize(null) == 0 && obj.v == kept;
    $display("linear lowering valid %0d", valid);
  end
endmodule
