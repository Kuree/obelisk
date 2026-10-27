// RUN: %talos --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov | FileCheck %s

// IEEE 1800-2023 19.3: event sampling preserves constructor input snapshots and live
// const-ref aliases. Aggregate members retain binary32 precision as well.
module top;
  typedef struct { shortreal number; int tag; } payload_t;
  payload_t original;
  bit clock;
  shortreal actual;
  real wide;
  task automatic trigger;
    actual = 1.5;
  endtask
  covergroup aggregate(input payload_t frozen);
    cp: coverpoint ($shortrealtobits(frozen.number) == 32'h3dcccccd &&
                    frozen.tag == 7) { bins yes = {1}; }
  endgroup
  covergroup clocked(input shortreal frozen, ref shortreal live)
      @(posedge clock iff $shortrealtobits(frozen) == 32'h3dcccccd);
    cp: coverpoint live { bins half = {0.5}; bins high = {1.5}; }
    captured: coverpoint ($shortrealtobits(frozen) == 32'h3dcccccd) {
      bins yes = {1};
    }
  endgroup
  covergroup blocked(input shortreal frozen, ref shortreal live)
      @@(begin trigger or end trigger);
    cp: coverpoint live { bins half = {0.5}; bins high = {1.5}; }
    captured: coverpoint ($shortrealtobits(frozen) == 32'h3dcccccd) {
      bins yes = {1};
    }
  endgroup
  covergroup real_clocked(input real frozen, ref real live)
      @(posedge clock iff frozen == 1.25);
    cp: coverpoint live { bins low = {0.5}; bins high = {1.5}; }
  endgroup
  aggregate saved;
  clocked observed;
  blocked synchronous;
  real_clocked wide_observed;
  initial begin
    original.number = 0.1; original.tag = 7;
    saved = new(original);
    original.number = 1.5; original.tag = 9;
    saved.sample();
    $display("aggregate=%0.0f", saved.get_inst_coverage());
    actual = 0.1;
    observed = new(actual, actual);
    synchronous = new(actual, actual);
    wide = 1.25; wide_observed = new(wide, wide);
    actual = 0.5; trigger();
    $display("block=%0.0f", synchronous.get_inst_coverage());
    actual = 0.5; wide = 0.5;
    #1; clock = 1; #1; clock = 0;
    actual = 1.5; wide = 1.5;
    #1; clock = 1; #1;
    $display("clock=%0.0f", observed.get_inst_coverage());
    $display("real_clock=%0.0f", wide_observed.get_inst_coverage());
    $finish;
  end
endmodule

// CHECK: aggregate=100
// CHECK: block=100
// CHECK: clock=100
// CHECK: real_clock=100
