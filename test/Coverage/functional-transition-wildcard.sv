// RUN: %obelisk -fno-lto --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov \
// RUN:   --coverage-test=transition-wildcard > %t.native.out
// RUN: obelisk-cov report --format=text %t.native.obcov -o %t.native.txt
// RUN: %obelisk -fno-lto --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov \
// RUN:   --coverage-test=transition-wildcard > %t.bytecode.out
// RUN: obelisk-cov report --format=text %t.bytecode.obcov -o %t.bytecode.txt
// RUN: diff -u %t.native.out %t.bytecode.out
// RUN: diff -u %t.native.txt %t.bytecode.txt
// RUN: FileCheck %s --check-prefix=QUERY < %t.native.out
// RUN: FileCheck %s --check-prefix=REPORT < %t.native.txt

module top;
  logic [1:0] sampled;
  logic signed [4:0] signed_sampled;
  logic [1:0] narrowed_sampled;

  covergroup cg;
    cp: coverpoint sampled {
      // IEEE 1800-2023 19.5.4: x/z/? in a wildcard definition expand over
      // two-state values. The scalar form is one bin; [] is one bin per
      // concrete transition sequence.
      wildcard bins T0_3 = (2'b0x => 2'b1x);
      wildcard bins T0_3_array[] = (2'b0x => 2'b1x);
    }
    signed_cp: coverpoint signed_sampled {
      // A wildcard sign bit in a narrower signed literal splits when the
      // literal is converted to this wider signed coverpoint type.
      wildcard bins signed_paths = (4'sb?001 => 4'sb?010);
    }
    narrowed_cp: coverpoint narrowed_sampled {
      // Only value 1 from each source cube is representable by this unsigned
      // 2-bit coverpoint. Value 5 and signed value -3 are excluded without
      // discarding the valid subcube.
      wildcard bins unsigned_scalar = (3'b?01 => 2'b10);
      wildcard bins unsigned_array[] = (3'b?01 => 2'b10);
      wildcard bins signed_scalar = (3'sb?01 => 2'b10);
      wildcard bins signed_array[] = (3'sb?01 => 2'b10);
    }
  endgroup

  cg cov;
  int covered;
  int total;
  real percentage;

  task automatic sample_pair(logic [1:0] source,
                             logic [1:0] destination);
    sampled = source;
    cov.sample();
    sampled = destination;
    cov.sample();
  endtask

  task automatic sample_signed_pair(logic signed [4:0] source,
                                    logic signed [4:0] destination);
    signed_sampled = source;
    cov.sample();
    signed_sampled = destination;
    cov.sample();
  endtask

  task automatic sample_narrowed_pair(logic [1:0] source,
                                      logic [1:0] destination);
    narrowed_sampled = source;
    cov.sample();
    narrowed_sampled = destination;
    cov.sample();
  endtask

  initial begin
    cov = new;
    sample_pair(2'b00, 2'b10);
    sample_pair(2'b00, 2'b11);
    sample_pair(2'b01, 2'b10);
    sample_pair(2'b01, 2'b11);

    // Wildcard bins only consider two-state sampled values. These sequences
    // must not increment either the scalar bin or an expanded array bin.
    sample_pair(2'b0x, 2'b10);
    sample_pair(2'b00, 2'b1x);
    sample_pair(2'b0z, 2'b11);
    sample_pair(2'b01, 2'b1z);

    sample_signed_pair(1, 2);
    sample_signed_pair(1, -6);
    sample_signed_pair(-7, 2);
    sample_signed_pair(-7, -6);
    sample_narrowed_pair(1, 2);

    percentage = cov.cp.get_inst_coverage(covered, total);
    $display("transition-wildcard %.6f %0d %0d", percentage, covered,
             total);
    $finish;
  end
endmodule

// QUERY: transition-wildcard 100.000000 5 5
// REPORT: functional: 100.00% (10/10)
// REPORT-DAG: bin T0_3: 4 [covered]
// REPORT-DAG: bin T0_3_array[0=>2]: 1 [covered]
// REPORT-DAG: bin T0_3_array[0=>3]: 1 [covered]
// REPORT-DAG: bin T0_3_array[1=>2]: 1 [covered]
// REPORT-DAG: bin T0_3_array[1=>3]: 1 [covered]
// REPORT-DAG: bin signed_paths: 4 [covered]
// REPORT-DAG: bin unsigned_scalar: 1 [covered]
// REPORT-DAG: bin unsigned_array[1=>2]: 1 [covered]
// REPORT-DAG: bin signed_scalar: 1 [covered]
// REPORT-DAG: bin signed_array[1=>2]: 1 [covered]
