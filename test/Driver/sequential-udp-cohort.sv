// RUN: obelisk -fno-lto -O0 --vpi=off --native-scheduler=generic %s -o %t
// RUN: %t --execution-tier=native | FileCheck %s
// RUN: %t --execution-tier=bytecode | FileCheck %s
// RUN: obelisk -fno-lto -O3 --vpi=off --native-scheduler=generic \
// RUN:   -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM

// Independent carried lanes must survive skipped cohort members.  The second
// primitive gives the planner a distinct two-input (two-bit previous-input)
// cohort, and the delayed cohort proves publication stays scheduler-owned.
primitive udp_cohort_one(q, a);
  output q;
  reg q;
  input a;
  initial q = 0;
  table
    (x0) : ? : -;
    r    : ? : 1;
    f    : ? : 0;
  endtable
endprimitive

primitive udp_cohort_two(q, a, b);
  output q;
  reg q;
  input a, b;
  initial q = 0;
  table
    (x0) 0 : ? : -;
    r 0  : ? : 1;
    f 0  : ? : 0;
  endtable
endprimitive

primitive udp_cohort_delayed(q, a);
  output q;
  reg q;
  input a;
  initial q = 0;
  table
    (x0) : ? : -;
    r    : ? : 1;
    f    : ? : 0;
  endtable
endprimitive

module sequential_udp_cohort;
  reg [1:0] one_input = 0;
  reg [1:0] two_a = 0;
  reg [1:0] two_b = 0;
  reg [1:0] delayed_input = 0;
  reg [1:0] held_input = 0;
  wire [1:0] one_q;
  wire [1:0] two_q;
  wire [1:0] delayed_q;
  wire [1:0] held_q;
  reg [1:0] delayed_observed;
  reg [1:0] held_observed;
  integer delayed_wakes = 0;
  integer held_wakes = 0;
  integer delayed_baseline;
  integer held_baseline;

  udp_cohort_one one[1:0](one_q, one_input);
  udp_cohort_two two[1:0](two_q, two_a, two_b);
  udp_cohort_delayed #(3) delayed[1:0](delayed_q, delayed_input);
  udp_cohort_one (weak0, weak1) held[1:0](held_q, held_input);
  assign (strong0, strong1) held_q = 2'b00;

  always @(delayed_q) begin
    delayed_observed = delayed_q;
    delayed_wakes = delayed_wakes + 1;
  end
  always @(held_q) begin
    held_observed = held_q;
    held_wakes = held_wakes + 1;
  end

  initial begin
    #1;
    if (one_q !== 2'b00 || two_q !== 2'b00 || held_q !== 2'b00)
      $fatal(0, "cohort initialization mismatch %b %b %b",
             one_q, two_q, held_q);
    #3 if (delayed_q !== 2'b00)
      $fatal(0, "delayed cohort initialization mismatch %b", delayed_q);
    delayed_baseline = delayed_wakes;
    held_baseline = held_wakes;

    // A wakes, then only B wakes, then A wakes again.  A's final falling edge
    // is recognized only if B's activation preserved A's previous input lane.
    one_input[0] = 1;
    #1 if (one_q !== 2'b01) $fatal(0, "cohort A rise mismatch");
    one_input[1] = 1;
    #1 if (one_q !== 2'b11) $fatal(0, "cohort B rise mismatch");
    one_input[0] = 0;
    #1 if (one_q !== 2'b10) $fatal(0, "cohort A retained-state mismatch");

    // A distinct cohort carries a two-bit normalized previous-input value.
    two_a[0] = 1;
    #1 if (two_q !== 2'b01) $fatal(0, "two-input A rise mismatch");
    two_a[1] = 1;
    #1 if (two_q !== 2'b11) $fatal(0, "two-input B rise mismatch");
    two_a[0] = 0;
    #1 if (two_q !== 2'b10) $fatal(0, "two-input A fall mismatch");

    // A schedules a rise, B wakes independently, then A schedules the
    // opposite value before maturity. The per-instance inertial site must
    // cancel A's pending rise; B's keyed publication still matures normally.
    delayed_input[0] = 1;
    #1 delayed_input[1] = 1;
    #1 delayed_input[0] = 0;
    #1 if (delayed_q !== 2'b00 || delayed_observed !== 2'b00 ||
           delayed_wakes != delayed_baseline)
      $fatal(0, "delayed cohort fired or woke consumer early");
    #1;
    #0;
    if (delayed_q !== 2'b10 || delayed_observed !== 2'b10 ||
        delayed_wakes != delayed_baseline + 1)
      $fatal(0, "delayed cohort B maturity mismatch %b %b %0d %0d",
             delayed_q, delayed_observed, delayed_wakes, delayed_baseline);
    #1 if (delayed_q !== 2'b10 || delayed_wakes != delayed_baseline + 1)
      $fatal(0, "delayed cohort cancellation mismatch");

    // A raw weak UDP contribution changes under a strong competing driver,
    // but the resolved publication does not. Downstream logic must not wake.
    held_input = 2'b11;
    #1 if (held_q !== 2'b00 || held_observed !== 2'b00 ||
           held_wakes != held_baseline)
      $fatal(0, "hidden raw change caused a false publication");

    $display("SEQUENTIAL UDP COHORT PASS");
    $finish;
  end
endmodule

// CHECK: SEQUENTIAL UDP COHORT PASS

// The outlined member is a hard optimization boundary even at O3.
// LLVM: call {{.*}}@__obelisk_region_kernel_{{.*}}.__member
// LLVM: define {{.*}}@__obelisk_region_kernel_{{.*}}.__member{{.*}}#[[NOINLINE:[0-9]+]] {
// LLVM: attributes #[[NOINLINE]] = { noinline{{.*}} }
