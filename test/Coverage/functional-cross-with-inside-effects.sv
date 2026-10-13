// RUN: %obelisk --std=1800-2023 -O0 --target=native --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov > %t.native.out
// RUN: FileCheck %s < %t.native.out
// RUN: %obelisk --std=1800-2023 -O3 --execution-tier=bytecode --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov > %t.bytecode.out
// RUN: diff -u %t.native.out %t.bytecode.out

// The searched value is evaluated once per candidate tuple. A singular member
// is evaluated once for its comparison. Preserve calls in the tuple evaluator.
module top;
  bit value;
  int calls;
  function automatic bit tick();
    ++calls;
    return 0;
  endfunction
  covergroup cg;
    option.cross_retain_auto_bins = 0;
    bit ca: coverpoint value { bins all = {[0:1]}; }
    bit cb: coverpoint 1'b0 { bins zero = {0}; }
    subject: cross ca, cb {
      bins selected = subject with (tick() inside {[ca:ca]});
    }
    member: cross ca, cb {
      bins selected = member with (ca inside {tick()});
    }
    pattern: cross ca, cb {
      bins selected = pattern with (ca ==? tick());
    }
  endgroup
  cg cov;
  initial begin
    cov = new;
    cov.sample();
    $display("calls %0d coverage %0.0f %0.0f %0.0f", calls,
             cov.subject.get_inst_coverage(), cov.member.get_inst_coverage(), cov.pattern.get_inst_coverage());
    $finish;
  end
endmodule
// CHECK: calls 6 coverage 100 100 100
