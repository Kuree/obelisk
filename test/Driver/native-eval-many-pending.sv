// RUN: obelisk --clocked-kernels=immediate -O3 --vpi=off --native-scheduler=eval --mlir-timing %s -o %t.eval 2> %t.plan
// RUN: FileCheck %s --check-prefix=PLAN < %t.plan
// RUN: obelisk --clocked-kernels=immediate -O3 --vpi=off --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode > %t.reference
// RUN: %t.eval | diff -u %t.reference -

// LRM 4.6(b), 4.9.4/10.4.2: last captures, partial targets, X/Z and skipped
// captures keep their semantics when more than 64 roots share an activation.
// A dynamic target stays canonical alongside private roots in its leaf word.
// Seventy independent captures span two readiness words.
// PLAN: obelisk eval local pending roots: {{(6[5-9]|[7-9][0-9]|[1-9][0-9][0-9]+)}} cores=
// PLAN-SAME: bitmap-words=2

module many_pending;
  bit clk = 0;
  int phase = 0;
  always #5 clk = ~clk;
  always @(posedge clk) phase <= phase + 1;
  int r0 = 0;
  always @(posedge clk) begin
    r0 <= r0 + 1;
  end
  int r1 = 1;
  always @(posedge clk) begin
    r1 <= r1 + 1;
  end
  int r2 = 2;
  always @(posedge clk) begin
    r2 <= r2 + 1;
  end
  int r3 = 3;
  always @(posedge clk) begin
    r3 <= r3 + 1;
  end
  int r4 = 4;
  always @(posedge clk) begin
    r4 <= r4 + 1;
  end
  int r5 = 5;
  always @(posedge clk) begin
    if (phase == 1) r5 <= 'x;
    else if (phase == 2) r5 <= 'z;
    else if (phase == 3) r5 <= 8;
    else r5 <= r5 + 1;
  end
  int r6 = 6;
  always @(posedge clk) begin
    r6 <= r6 + 1;
  end
  int r7 = 7;
  always @(posedge clk) begin
    r7 <= r7 + 1;
  end
  int r8 = 8;
  always @(posedge clk) begin
    r8 <= r8 + 1;
  end
  int r9 = 9;
  always @(posedge clk) begin
    r9 <= r9 + 1;
  end
  int r10 = 10;
  always @(posedge clk) begin
    r10 <= r10 + 1;
  end
  int r11 = 11;
  always @(posedge clk) begin
    r11 <= r11 + 1;
  end
  int r12 = 12;
  always @(posedge clk) begin
    r12 <= r12 + 1;
  end
  int r13 = 13;
  always @(posedge clk) begin
    r13 <= r13 + 1;
  end
  int r14 = 14;
  always @(posedge clk) begin
    r14 <= r14 + 1;
  end
  int r15 = 15;
  always @(posedge clk) begin
    r15 <= r15 + 1;
  end
  int r16 = 16;
  always @(posedge clk) begin
    r16 <= r16 + 1;
  end
  int r17 = 17;
  always @(posedge clk) begin
    r17 <= r17 + 1;
    r17 <= r17 + 2;
  end
  int r18 = 18;
  always @(posedge clk) begin
    r18 <= r18 + 1;
  end
  int r19 = 19;
  always @(posedge clk) begin
    r19 <= r19 + 1;
  end
  int r20 = 20;
  always @(posedge clk) begin
    r20 <= r20 + 1;
  end
  int r21 = 21;
  always @(posedge clk) begin
    r21 <= r21 + 1;
  end
  int r22 = 22;
  always @(posedge clk) begin
    r22 <= r22 + 1;
  end
  int r23 = 23;
  always @(posedge clk) begin
    r23 <= r23 + 1;
  end
  int r24 = 24;
  always @(posedge clk) begin
    r24 <= r24 + 1;
  end
  int r25 = 25;
  always @(posedge clk) begin
    r25 <= r25 + 1;
  end
  int r26 = 26;
  always @(posedge clk) begin
    r26 <= r26 + 1;
  end
  int r27 = 27;
  always @(posedge clk) begin
    r27 <= r27 + 1;
  end
  int r28 = 28;
  always @(posedge clk) begin
    r28 <= r28 + 1;
  end
  int r29 = 29;
  always @(posedge clk) begin
    r29 <= r29 + 1;
  end
  int r30 = 30;
  always @(posedge clk) begin
    r30 <= r30 + 1;
  end
  int r31 = 31;
  always @(posedge clk) begin
    r31 <= r31 + 1;
  end
  int r32 = 32;
  always @(posedge clk) begin
    r32 <= r32 + 1;
  end
  logic [15:0] r33 = 16'h1200;
  always @(posedge clk) begin
    r33[15:8] = r33[15:8] + 1;
    r33[3:0] <= phase;
  end
  int r34 = 34;
  always @(posedge clk) begin
    r34 <= r34 + 1;
  end
  int r35 = 35;
  always @(posedge clk) begin
    r35 <= r35 + 1;
  end
  int r36 = 36;
  always @(posedge clk) begin
    r36 <= r36 + 1;
  end
  int r37 = 37;
  always @(posedge clk) begin
    r37 <= r37 + 1;
  end
  int r38 = 38;
  always @(posedge clk) begin
    r38 <= r38 + 1;
  end
  int r39 = 39;
  always @(posedge clk) begin
    r39 <= r39 + 1;
  end
  int r40 = 40;
  always @(posedge clk) begin
    r40 <= r40 + 1;
  end
  int r41 = 41;
  always @(posedge clk) begin
    r41 <= r41 + 1;
  end
  int r42 = 42;
  always @(posedge clk) begin
    r42 <= r42 + 1;
  end
  int r43 = 43;
  always @(posedge clk) begin
    r43 <= r43 + 1;
  end
  int r44 = 44;
  always @(posedge clk) begin
    r44 <= r44 + 1;
  end
  int r45 = 45;
  always @(posedge clk) begin
    r45 <= r45 + 1;
  end
  int r46 = 46;
  always @(posedge clk) begin
    r46 <= r46 + 1;
  end
  int r47 = 47;
  always @(posedge clk) begin
    r47 <= r47 + 1;
  end
  int r48 = 48;
  always @(posedge clk) begin
    r48 <= r48 + 1;
  end
  int r49 = 49;
  always @(posedge clk) begin
    r49 <= r49 + 1;
  end
  int r50 = 50;
  always @(posedge clk) begin
    r50 <= r50 + 1;
  end
  int r51 = 51;
  always @(posedge clk) begin
    r51 <= r51 + 1;
  end
  int r52 = 52;
  always @(posedge clk) begin
    r52 <= r52 + 1;
  end
  int r53 = 53;
  always @(posedge clk) begin
    r53 <= r53 + 1;
  end
  int r54 = 54;
  always @(posedge clk) begin
    r54 <= r54 + 1;
  end
  int r55 = 55;
  always @(posedge clk) begin
    r55 <= r55 + 1;
  end
  int r56 = 56;
  always @(posedge clk) begin
    r56 <= r56 + 1;
  end
  int r57 = 57;
  always @(posedge clk) begin
    r57 <= r57 + 1;
  end
  int r58 = 58;
  always @(posedge clk) begin
    r58 <= r58 + 1;
  end
  int r59 = 59;
  always @(posedge clk) begin
    r59 <= r59 + 1;
  end
  int r60 = 60;
  always @(posedge clk) begin
    r60 <= r60 + 1;
  end
  int r61 = 61;
  always @(posedge clk) begin
    r61 <= r61 + 1;
  end
  int r62 = 62;
  always @(posedge clk) begin
    r62 <= r62 + 1;
  end
  int r63 = 63;
  always @(posedge clk) begin
    r63 <= r63 + 1;
  end
  int r64 = 64;
  always @(posedge clk) begin
    if (phase != 3) r64 <= r64 + 1;
  end
  int r65 = 65;
  always @(posedge clk) begin
    r65 <= r65 + 1;
  end
  int r66 = 66;
  always @(posedge clk) begin
    r66 <= r66 + 1;
  end
  int r67 = 67;
  always @(posedge clk) begin
    r67 <= r67 + 1;
  end
  int r68 = 68;
  always @(posedge clk) begin
    r68 <= r68 + 1;
  end
  int r69 = 69;
  always @(posedge clk) begin
    r69[phase % 32] <= ~r69[phase % 32];
  end
  initial begin
    automatic string label = "many";
    repeat (6) begin
      @(posedge clk);
      $display("active %0d %0d %h", phase, r17, r33);
      #1;
      $display("%s %0d %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h %h", label, phase, r0, r1, r2, r3, r4, r5, r6, r7, r8, r9, r10, r11, r12, r13, r14, r15, r16, r17, r18, r19, r20, r21, r22, r23, r24, r25, r26, r27, r28, r29, r30, r31, r32, r33, r34, r35, r36, r37, r38, r39, r40, r41, r42, r43, r44, r45, r46, r47, r48, r49, r50, r51, r52, r53, r54, r55, r56, r57, r58, r59, r60, r61, r62, r63, r64, r65, r66, r67, r68, r69);
    end
    $finish;
  end
endmodule
