// RUN: %obelisk -fno-lto --std=1800-2023 -O0 --target=native \
// RUN:   --coverage=functional -o %t.native %s
// RUN: %t.native --coverage-output=%t.native.obcov \
// RUN:   --coverage-test=set-expression 2> %t.native.err
// RUN: obelisk-cov report --format=text %t.native.obcov -o %t.native.txt
// RUN: %obelisk -fno-lto --std=1800-2023 -O3 --execution-tier=bytecode \
// RUN:   --coverage=functional -o %t.bytecode %s
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov \
// RUN:   --coverage-test=set-expression 2> %t.bytecode.err
// RUN: obelisk-cov report --format=text %t.bytecode.obcov -o %t.bytecode.txt
// RUN: diff -u %t.native.err %t.bytecode.err
// RUN: diff -u %t.native.txt %t.bytecode.txt
// RUN: FileCheck %s --check-prefix=WARN < %t.native.err
// RUN: FileCheck %s < %t.native.txt

module top;
  bit [7:0] sampled;
  logic [3:0] logic_sampled;
  real real_sampled;

  typedef int fixed_values_t[4];
  function automatic fixed_values_t make_fixed();
    return '{1, 2, 2, 3};
  endfunction

  int dynamic_values[];
  int queue_values[$:3];
  int signed_values[2];
  logic [3:0] normal_logic_values[2];
  logic [3:0] wildcard_logic_values[2];
  real real_values[2];
  real real_to_integral_values[3];
  longint integral_to_real_values[3];
  bit [1:0][1:0] packed_values;
  shortreal shortreal_values[2];
  int empty_values[];

  covergroup cg;
    integral_cp: coverpoint sampled {
      // IEEE 1800-2017 19.5.1.2 evaluates each array expression when the
      // covergroup is constructed. Duplicates remain in the scalar bin.
      bins fixed = make_fixed();
      bins dynamic[] = dynamic_values;
      bins queue_bins[2] = queue_values;
      bins signed_cast[] = signed_values;
      bins real_cast[] = real_to_integral_values;
      bins packed_values[] = packed_values;
      bins empty[] = empty_values;
    }
    normal_logic_cp: coverpoint logic_sampled {
      bins values[] = normal_logic_values;
    }
    wildcard_logic_cp: coverpoint logic_sampled {
      wildcard bins values[] = wildcard_logic_values;
    }
    real_cp: coverpoint real_sampled {
      bins values[] = real_values;
      bins integral_values[] = integral_to_real_values;
      bins shortreal_values[] = shortreal_values;
    }
  endgroup

  cg cov;
  initial begin
    dynamic_values = '{4, 5};
    queue_values = '{6, 7};
    signed_values = '{-1, 8};
    normal_logic_values = '{4'hx, 4'h9};
    wildcard_logic_values = '{4'b10x1, 4'b01z0};
    real_values = '{1.25, 2.5};
    real_to_integral_values = '{3.0, 3.25, 10.0};
    integral_to_real_values = '{11, 12, 64'd9007199254740993};
    packed_values = 4'b0110;
    shortreal_values = '{3.5, 4.5};
    cov = new;

    // Mutating every source after construction must not change the resolved
    // bin schema stored by the instance.
    dynamic_values = '{99};
    queue_values = '{98};
    signed_values = '{97, 96};
    normal_logic_values = '{4'hf, 4'he};
    wildcard_logic_values = '{4'hd, 4'hc};
    real_values = '{9.0, 10.0};
    real_to_integral_values = '{13.0, 14.0, 15.0};
    integral_to_real_values = '{14, 15, 16};
    packed_values = 4'b1111;
    shortreal_values = '{5.5, 6.5};

    for (int i = 0; i <= 12; i++) begin
      sampled = i;
      logic_sampled = i[3:0];
      real_sampled = i;
      cov.sample();
    end
    real_sampled = 1.25; cov.sample();
    real_sampled = 2.5; cov.sample();
    real_sampled = 3.5; cov.sample();
    real_sampled = 4.5; cov.sample();
    $finish;
  end
endmodule

// IEEE 1800-2023 19.5.7 requires warnings and discards when conversion to the
// effective coverpoint type changes the value, and for X/Z in non-wildcard
// bins. Wildcard bins expand X/Z before conversion.
// WARN-DAG: warning: functional bin set element is outside the effective coverpoint type and is ignored
// WARN-DAG: warning: functional bin set real element changes value when converted to the effective coverpoint type and is ignored
// WARN-DAG: warning: functional bin set element contains X or Z and is ignored
// WARN-DAG: warning: functional bin set integral element changes value when converted to the effective coverpoint type and is ignored

// CHECK: functional: 100.00% (21/21)
// CHECK-DAG: coverpoint integral_cp: 10/10 (100.00%)
// CHECK-DAG: bin fixed: 3 [covered]
// CHECK-DAG: bin dynamic[4]: 1 [covered]
// CHECK-DAG: bin dynamic[5]: 1 [covered]
// CHECK-DAG: bin queue_bins[0]: 1 [covered]
// CHECK-DAG: bin queue_bins[1]: 1 [covered]
// CHECK-DAG: bin signed_cast[8]: 1 [covered]
// CHECK-DAG: bin real_cast[3]: 1 [covered]
// CHECK-DAG: bin real_cast[10]: 1 [covered]
// CHECK-DAG: bin packed_values[1]: 1 [covered]
// CHECK-DAG: bin packed_values[2]: 1 [covered]
// CHECK-DAG: coverpoint normal_logic_cp: 1/1 (100.00%)
// CHECK-DAG: bin values[9]: 1 [covered]
// CHECK-DAG: coverpoint wildcard_logic_cp: 4/4 (100.00%)
// CHECK-DAG: bin values[11]: 1 [covered] {wildcard}
// CHECK-DAG: bin values[4]: 1 [covered] {wildcard}
// CHECK-DAG: bin values[6]: 1 [covered] {wildcard}
// CHECK-DAG: coverpoint real_cp: 6/6 (100.00%)
// CHECK-DAG: bin values[1.25]: 1 [covered]
// CHECK-DAG: bin values[2.5]: 1 [covered]
// CHECK-DAG: bin integral_values[11.0]: 1 [covered]
// CHECK-DAG: bin integral_values[12.0]: 1 [covered]
// CHECK-DAG: bin shortreal_values[3.5]: 1 [covered]
// CHECK-DAG: bin shortreal_values[4.5]: 1 [covered]
// CHECK-NOT: dynamic[99]
// CHECK-NOT: queue_bins[98]
// CHECK-NOT: signed_cast[97]
// CHECK-NOT: values[9.0]
// CHECK-NOT: shortreal_values[5.5]
