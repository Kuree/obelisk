// RUN: %obelisk --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: %t.native --no-coverage-dump > %t.native.out
// RUN: %obelisk --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --no-coverage-dump > %t.bytecode.out
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: FileCheck %s --check-prefix=QUERY < %t.native.out

// IEEE 1800-2023 19.4: each embedded covergroup is instantiated with its
// containing class object. Its clocking event and coverpoint expressions
// therefore observe that specific object, not a shared or construction-time
// value.
module top;
  class monitor;
    bit clock;
    bit sampled;

    covergroup cov @(posedge clock);
      point: coverpoint sampled {
        bins one = {1};
      }
    endgroup

    function new;
      cov = new;
    endfunction
  endclass

  monitor first;
  monitor second;

  initial begin
    first = new;
    second = new;
    #0;

    first.sampled = 1;
    first.clock = 1;
    #0;
    $display("after first %.6f %.6f", first.cov.get_inst_coverage(),
             second.cov.get_inst_coverage());

    second.sampled = 1;
    second.clock = 1;
    #0;
    $display("after second %.6f %.6f", first.cov.get_inst_coverage(),
             second.cov.get_inst_coverage());
    $finish;
  end
endmodule

// QUERY: after first 100.000000 0.000000
// QUERY: after second 100.000000 100.000000
