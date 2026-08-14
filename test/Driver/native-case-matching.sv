// RUN: obelisk -fno-lto -O0 %s -o %t.o0.native
// RUN: %t.o0.native > %t.o0.native.out 2> %t.o0.native.err
// RUN: obelisk -fno-lto -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode > %t.o0.bytecode.out 2> %t.o0.bytecode.err
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.err %t.o0.bytecode.err
// RUN: obelisk -fno-lto -O3 %s -o %t.o3.native
// RUN: %t.o3.native > %t.o3.native.out 2> %t.o3.native.err
// RUN: obelisk -fno-lto -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode > %t.o3.bytecode.out 2> %t.o3.bytecode.err
// RUN: diff -u %t.o3.native.out %t.o3.bytecode.out
// RUN: diff -u %t.o3.native.err %t.o3.bytecode.err
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: diff -u %t.o0.native.err %t.o3.native.err
// RUN: FileCheck %s --check-prefix=STDOUT < %t.o3.native.out
// RUN: FileCheck %s --check-prefix=STDERR < %t.o3.native.err

module native_case_matching;
  typedef struct packed {
    logic [3:0] left;
    logic [3:0] right;
  } pair_t;

  logic [3:0] value;
  logic [3:0] choices [0:1];
  pair_t pair;
  int side;

  function automatic logic [3:0] label(input logic [3:0] candidate);
    side++;
    return candidate;
  endfunction

  function automatic bit normal_match(input logic lhs, input logic rhs);
    normal_match = 1'b0;
    case (lhs)
      rhs: normal_match = 1'b1;
    endcase
  endfunction

  function automatic bit casez_match(input logic lhs, input logic rhs);
    casez_match = 1'b0;
    casez (lhs)
      rhs: casez_match = 1'b1;
    endcase
  endfunction

  function automatic bit casex_match(input logic lhs, input logic rhs);
    casex_match = 1'b0;
    casex (lhs)
      rhs: casex_match = 1'b1;
    endcase
  endfunction

  function automatic logic wild_eq_match(input logic lhs, input logic rhs);
    return lhs ==? rhs;
  endfunction

  function automatic logic wild_ne_match(input logic lhs, input logic rhs);
    return lhs !=? rhs;
  endfunction

  initial begin
    $display("normal-matrix=%b", {
      normal_match(1'b0, 1'b0), normal_match(1'b0, 1'b1),
      normal_match(1'b0, 1'bx), normal_match(1'b0, 1'bz),
      normal_match(1'b1, 1'b0), normal_match(1'b1, 1'b1),
      normal_match(1'b1, 1'bx), normal_match(1'b1, 1'bz),
      normal_match(1'bx, 1'b0), normal_match(1'bx, 1'b1),
      normal_match(1'bx, 1'bx), normal_match(1'bx, 1'bz),
      normal_match(1'bz, 1'b0), normal_match(1'bz, 1'b1),
      normal_match(1'bz, 1'bx), normal_match(1'bz, 1'bz)});
    $display("casez-matrix=%b", {
      casez_match(1'b0, 1'b0), casez_match(1'b0, 1'b1),
      casez_match(1'b0, 1'bx), casez_match(1'b0, 1'bz),
      casez_match(1'b1, 1'b0), casez_match(1'b1, 1'b1),
      casez_match(1'b1, 1'bx), casez_match(1'b1, 1'bz),
      casez_match(1'bx, 1'b0), casez_match(1'bx, 1'b1),
      casez_match(1'bx, 1'bx), casez_match(1'bx, 1'bz),
      casez_match(1'bz, 1'b0), casez_match(1'bz, 1'b1),
      casez_match(1'bz, 1'bx), casez_match(1'bz, 1'bz)});
    $display("casex-matrix=%b", {
      casex_match(1'b0, 1'b0), casex_match(1'b0, 1'b1),
      casex_match(1'b0, 1'bx), casex_match(1'b0, 1'bz),
      casex_match(1'b1, 1'b0), casex_match(1'b1, 1'b1),
      casex_match(1'b1, 1'bx), casex_match(1'b1, 1'bz),
      casex_match(1'bx, 1'b0), casex_match(1'bx, 1'b1),
      casex_match(1'bx, 1'bx), casex_match(1'bx, 1'bz),
      casex_match(1'bz, 1'b0), casex_match(1'bz, 1'b1),
      casex_match(1'bz, 1'bx), casex_match(1'bz, 1'bz)});
    $display("wild-eq-matrix=%b", {
      wild_eq_match(1'b0, 1'b0), wild_eq_match(1'b0, 1'b1),
      wild_eq_match(1'b0, 1'bx), wild_eq_match(1'b0, 1'bz),
      wild_eq_match(1'b1, 1'b0), wild_eq_match(1'b1, 1'b1),
      wild_eq_match(1'b1, 1'bx), wild_eq_match(1'b1, 1'bz),
      wild_eq_match(1'bx, 1'b0), wild_eq_match(1'bx, 1'b1),
      wild_eq_match(1'bx, 1'bx), wild_eq_match(1'bx, 1'bz),
      wild_eq_match(1'bz, 1'b0), wild_eq_match(1'bz, 1'b1),
      wild_eq_match(1'bz, 1'bx), wild_eq_match(1'bz, 1'bz)});
    $display("wild-ne-matrix=%b", {
      wild_ne_match(1'b0, 1'b0), wild_ne_match(1'b0, 1'b1),
      wild_ne_match(1'b0, 1'bx), wild_ne_match(1'b0, 1'bz),
      wild_ne_match(1'b1, 1'b0), wild_ne_match(1'b1, 1'b1),
      wild_ne_match(1'b1, 1'bx), wild_ne_match(1'b1, 1'bz),
      wild_ne_match(1'bx, 1'b0), wild_ne_match(1'bx, 1'b1),
      wild_ne_match(1'bx, 1'bx), wild_ne_match(1'bx, 1'bz),
      wild_ne_match(1'bz, 1'b0), wild_ne_match(1'bz, 1'b1),
      wild_ne_match(1'bz, 1'bx), wild_ne_match(1'bz, 1'bz)});

    value = 4'b10xz;
    $display("wild=%b,%b,%b", value ==? 4'b10??,
             value !=? 4'b11??, 4'b10x1 ==? 4'b10?1);
    $display("wild-x=%b,%b", 4'b10x1 ==? 4'b1001,
             4'b10x1 !=? 4'b1001);

    case (value)
      4'b10xz: $display("case=exact");
      default: $display("case=default");
    endcase

    value = 4'b10x1;
    casez (value)
      4'b1001: $display("casez=wrong-known");
      4'b10x1: $display("casez=x-exact");
      default: $display("casez=default");
    endcase

    value = 4'b10z1;
    casez (value)
      4'b1001: $display("casez=z-wild");
      default: $display("casez=z-default");
    endcase

    value = 4'b10x1;
    casex (value)
      4'b1001: $display("casex=x-wild");
      default: $display("casex=default");
    endcase

    value = 4'b10xz;
    case (value) inside
      [4'b1000:4'b1011]: $display("case-inside=range");
      4'b10??: $display("case-inside=value");
      default: $display("case-inside=default");
    endcase

    choices[0] = 4'd3;
    choices[1] = 4'd9;
    $display("inside=%b,%b,%b,%b,%b",
             4'd6 inside {[4'd4:4'd8]},
             4'bx001 inside {[4'd0:4'd15]},
             4'd3 inside {choices},
             4'd2 inside {[$:4'd3]},
             4'd12 inside {[4'd10:$]});

    side = 0;
    case (4'd1)
      label(4'd1), label(4'd2): $display("order=first:%0d", side);
      label(4'd1): $display("order=second:%0d", side);
      default: $display("order=default:%0d", side);
    endcase

    side = 0;
    unique case (4'd1)
      label(4'd1), label(4'd1): $display("unique=first:%0d", side);
      label(4'd1): $display("unique=second:%0d", side);
      label(4'd1): $display("unique=third:%0d", side);
    endcase

    unique case (4'd7)
      4'd1: $display("unique-no-match=bad");
    endcase

    unique0 case (4'd7)
      4'd1: $display("unique0-no-match=bad");
    endcase

    unique case (4'd7)
      4'd1: $display("unique-default=bad");
      default: $display("unique-default=selected");
    endcase

    priority case (4'd7)
      4'd1: $display("priority-default=bad");
      default: $display("priority-default=selected");
    endcase

    unique0 if (1) $display("unique-if=first");
    else if (1) $display("unique-if=second");

    priority if (0) $display("priority-if=bad");

    priority if (0) $display("priority-else=bad");
    else $display("priority-else=selected");

    pair = {4'd1, 4'd2};
    unique case (pair) matches
      '{left: .pattern_left, right: 4'd2}:
        $display("unique-pattern-case=first:%0d", pattern_left);
      '{left: 4'd1, right: .*}:
        $display("unique-pattern-case=second");
    endcase

    priority case (pair) matches
      '{left: 4'd0, right: .*}:
        $display("priority-pattern-case=bad");
    endcase
  end
endmodule

// STDOUT: normal-matrix=1000010000100001
// STDOUT-NEXT: casez-matrix=1001010100111111
// STDOUT-NEXT: casex-matrix=1011011111111111
// STDOUT-NEXT: wild-eq-matrix=10110111xx11xx11
// STDOUT-NEXT: wild-ne-matrix=01001000xx00xx00
// STDOUT-NEXT: wild=1,1,1
// STDOUT-NEXT: wild-x=x,x
// STDOUT-NEXT: case=exact
// STDOUT-NEXT: casez=x-exact
// STDOUT-NEXT: casez=z-wild
// STDOUT-NEXT: casex=x-wild
// STDOUT-NEXT: case-inside=value
// STDOUT-NEXT: inside=1,x,1,1,1
// STDOUT-NEXT: order=first:1
// STDOUT-NEXT: unique=first:3
// STDOUT-NEXT: unique-default=selected
// STDOUT-NEXT: priority-default=selected
// STDOUT-NEXT: unique-if=first
// STDOUT-NEXT: priority-else=selected
// STDOUT-NEXT: unique-pattern-case=first:1

// STDERR: native-case-matching.sv:{{[0-9]+}}:5: warning: unique case violation: multiple matches
// STDERR-NEXT: {{.*}}native-case-matching.sv:{{[0-9]+}}:5: warning: unique case violation: no match
// STDERR-NEXT: {{.*}}native-case-matching.sv:{{[0-9]+}}:5: warning: unique0 if violation: multiple matches
// STDERR-NEXT: {{.*}}native-case-matching.sv:{{[0-9]+}}:5: warning: priority if violation: no match
// STDERR-NEXT: {{.*}}native-case-matching.sv:{{[0-9]+}}:5: warning: unique case violation: multiple matches
// STDERR-NEXT: {{.*}}native-case-matching.sv:{{[0-9]+}}:5: warning: priority case violation: no match
// STDERR-NOT: warning:
