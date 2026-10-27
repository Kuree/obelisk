// RUN: %talos --std=1800-2023 -O0 --coverage=functional -o %t %s
// RUN: %t --coverage-output=%t.obcov | FileCheck %s
// RUN: %t --coverage-output=%t.second.obcov > /dev/null
// RUN: talos-cov merge -o %t.merged.obcov %t.obcov %t.second.obcov
// RUN: talos-cov report --format=text %t.obcov | FileCheck %s --check-prefix=TEXT
// RUN: talos-cov report --format=json %t.merged.obcov | FileCheck %s --check-prefix=JSON
// RUN: talos-cov report --format=json %t.merged.obcov | FileCheck %s --check-prefix=VALUES
// RUN: talos-cov inspect %t.merged.obcov | FileCheck %s --check-prefix=MERGED

// IEEE 1800-2023 19.6.1.2 permits every positive integral matches count.
// Equality has exactly 2**65 witnesses; the full scalar predicate has 2**130.
// Each constructor captures its own threshold, including a 512-bit value
// above the physical domain bound. The wide value domains are not enumerated.
module top;
  bit [64:0] a, b;
  bit [1:0] narrow;
  bit side;
  covergroup wide_group(bit [511:0] need);
    option.cross_retain_auto_bins = 0;
    ca: coverpoint a { bins all = {[0:65'h1_ffff_ffff_ffff_ffff]}; }
    cb: coverpoint b { bins all = {[0:65'h1_ffff_ffff_ffff_ffff]}; }
    equality: cross ca, cb {
      bins selected = equality with (ca == cb) matches need;
    }
    full: cross ca, cb {
      bins selected = full with (1) matches need;
    }
    literal: cross ca, cb {
      bins selected = literal with (ca == cb)
          matches 128'h2_0000_0000_0000_0000;
    }
  endgroup
  covergroup per_bin_group(bit [127:0] need);
    option.cross_retain_auto_bins = 0;
    ca: coverpoint a {
      bins low = {[0:65'h0_ffff_ffff_ffff_ffff]};
      bins high = {[65'h1_0000_0000_0000_0000:65'h1_ffff_ffff_ffff_ffff]};
    }
    cb: coverpoint side { bins all = {[0:1]}; }
    product: cross ca, cb {
      bins selected = product with (1) matches need;
    }
  endgroup
  covergroup small_group;
    option.cross_retain_auto_bins = 0;
    ca: coverpoint narrow { bins all = {[0:3]}; }
    cb: coverpoint side { bins zero = {0}; }
    product: cross ca, cb {
      bins queued = product with (ca[0] == 0)
          matches 128'h1_0000_0000_0000_0000;
      bins nested = (product with (1)
          matches 128'h1_0000_0000_0000_0000) with (ca[0] == 0);
      bins set_count = '{'{0, 0}}
          matches 128'h1_0000_0000_0000_0000;
    }
  endgroup
  wide_group accepted, insufficient, enormous;
  per_bin_group per_bin, per_bin_insufficient;
  small_group narrow_cov;
  int covered, total;
  real percentage;
  initial begin
    accepted = new(512'h2_0000_0000_0000_0000);
    insufficient = new(512'h2_0000_0000_0000_0001);
    enormous = new(512'h1 << 511);
    per_bin = new(128'h2_0000_0000_0000_0000);
    per_bin_insufficient = new(128'h2_0000_0000_0000_0001);
    narrow_cov = new;
    accepted.sample();
    insufficient.sample();
    enormous.sample();
    per_bin.sample();
    per_bin_insufficient.sample();
    narrow_cov.sample();
    percentage = accepted.equality.get_inst_coverage(covered, total);
    $display("equal %.6f %0d %0d", percentage, covered, total);
    percentage = insufficient.equality.get_inst_coverage(covered, total);
    $display("one more %.6f %0d %0d", percentage, covered, total);
    percentage = accepted.full.get_inst_coverage(covered, total);
    $display("scalar %.6f %0d %0d", percentage, covered, total);
    percentage = insufficient.full.get_inst_coverage(covered, total);
    $display("scalar more %.6f %0d %0d", percentage, covered, total);
    percentage = enormous.full.get_inst_coverage(covered, total);
    $display("too large %.6f %0d %0d", percentage, covered, total);
    percentage = enormous.literal.get_inst_coverage(covered, total);
    $display("literal %.6f %0d %0d", percentage, covered, total);
    percentage = per_bin.product.get_inst_coverage(covered, total);
    $display("per bin %.6f %0d %0d", percentage, covered, total);
    percentage = per_bin_insufficient.product.get_inst_coverage(covered, total);
    $display("per bin more %.6f %0d %0d", percentage, covered, total);
    percentage = narrow_cov.product.get_inst_coverage(covered, total);
    $display("queued %.6f %0d %0d", percentage, covered, total);
    $finish;
  end
endmodule

// Value tuples per bin tuple: equality 2**65, full scalar 2**130, per_bin
// 2**65 per tuple (two 2**64-value bins by two side values), small_group 4.
// A tuple set of one tuple (set_count) and every queue count 2**64 exceed what
// the 4-tuple bin can hold, so those bins are empty. 512-bit 2**511 exceeds
// the 2**130 domain; 2**65 + 1 exceeds the 2**65 equality witnesses but not
// 2**130.
// CHECK: equal 100.000000 1 1
// CHECK-NEXT: one more 0.000000 0 0
// CHECK-NEXT: scalar 100.000000 1 1
// CHECK-NEXT: scalar more 100.000000 1 1
// CHECK-NEXT: too large 0.000000 0 0
// CHECK-NEXT: literal 100.000000 1 1
// CHECK-NEXT: per bin 100.000000 1 1
// CHECK-NEXT: per bin more 0.000000 0 0
// CHECK-NEXT: queued 0.000000 0 0

// Report bins (type order depends on the source path, so DAG). Instances
// 1-3 of wide_group are accepted, insufficient and enormous; $auto$4/5 are
// per_bin and per_bin_insufficient; $auto$6 is small_group with all three
// bins empty.
// TEXT-DAG: instance $auto$1: 100.00% (5/5)
// TEXT-DAG: instance $auto$2: 100.00% (4/4)
// TEXT-DAG: instance $auto$3: 100.00% (3/3)
// TEXT-DAG: instance $auto$4: 83.33% (3/4)
// TEXT-DAG: instance $auto$5: 75.00% (2/3)
// TEXT-DAG: instance $auto$6: 100.00% (2/2)
// TEXT-DAG: cross equality: 1/1 (100.00%)
// TEXT-DAG: cross equality: 0/0 (0.00%)
// TEXT-DAG: cross equality: 0/0 (0.00%)
// TEXT-DAG: cross full: 1/1 (100.00%)
// TEXT-DAG: cross full: 1/1 (100.00%)
// TEXT-DAG: cross full: 0/0 (0.00%)
// TEXT-DAG: cross literal: 1/1 (100.00%)
// TEXT-DAG: cross literal: 1/1 (100.00%)
// TEXT-DAG: cross literal: 1/1 (100.00%)
// TEXT-DAG: cross product: 1/1 (100.00%)
// TEXT-DAG: cross product: 0/0 (0.00%)
// TEXT-DAG: cross product: 0/0 (0.00%)
// TEXT-DAG: bin queued: 0 [excluded] {empty}
// TEXT-DAG: bin nested: 0 [excluded] {empty}
// TEXT-DAG: bin set_count: 0 [excluded] {empty}

// Every wide count (> 64 bits) is stored as a resolved role 6 snapshot: one
// single-atom value set per binding with the wide flag (8), 14 of them, each
// 128 or 512 bits wide, and the binding's matches_count stays 0. The merged
// file of two identical runs keeps exactly one copy of each table.
// JSON: "resolved_functional_value_sets":[
// JSON: "bit_width":{{(128|512)}},"kind":1,"signedness":1,"owner_bin":0,{{[^]]*}}"role":6,"flags":0,"atoms":[{"ordinal":0,{{[^]]*}}]}]
// JSON: "bit_width":{{(128|512)}},"kind":1,"signedness":1,"owner_bin":0,{{[^]]*}}"role":6,"flags":0,"atoms":[{"ordinal":0,{{[^]]*}}]}]
// JSON: "bit_width":{{(128|512)}},"kind":1,"signedness":1,"owner_bin":0,{{[^]]*}}"role":6,"flags":0,"atoms":[{"ordinal":0,{{[^]]*}}]}]
// JSON: "bit_width":{{(128|512)}},"kind":1,"signedness":1,"owner_bin":0,{{[^]]*}}"role":6,"flags":0,"atoms":[{"ordinal":0,{{[^]]*}}]}]
// JSON: "bit_width":{{(128|512)}},"kind":1,"signedness":1,"owner_bin":0,{{[^]]*}}"role":6,"flags":0,"atoms":[{"ordinal":0,{{[^]]*}}]}]
// JSON: "bit_width":{{(128|512)}},"kind":1,"signedness":1,"owner_bin":0,{{[^]]*}}"role":6,"flags":0,"atoms":[{"ordinal":0,{{[^]]*}}]}]
// JSON: "bit_width":{{(128|512)}},"kind":1,"signedness":1,"owner_bin":0,{{[^]]*}}"role":6,"flags":0,"atoms":[{"ordinal":0,{{[^]]*}}]}]
// JSON: "bit_width":{{(128|512)}},"kind":1,"signedness":1,"owner_bin":0,{{[^]]*}}"role":6,"flags":0,"atoms":[{"ordinal":0,{{[^]]*}}]}]
// JSON: "bit_width":{{(128|512)}},"kind":1,"signedness":1,"owner_bin":0,{{[^]]*}}"role":6,"flags":0,"atoms":[{"ordinal":0,{{[^]]*}}]}]
// JSON: "bit_width":{{(128|512)}},"kind":1,"signedness":1,"owner_bin":0,{{[^]]*}}"role":6,"flags":0,"atoms":[{"ordinal":0,{{[^]]*}}]}]
// JSON: "bit_width":{{(128|512)}},"kind":1,"signedness":1,"owner_bin":0,{{[^]]*}}"role":6,"flags":0,"atoms":[{"ordinal":0,{{[^]]*}}]}]
// JSON: "bit_width":{{(128|512)}},"kind":1,"signedness":1,"owner_bin":0,{{[^]]*}}"role":6,"flags":0,"atoms":[{"ordinal":0,{{[^]]*}}]}]
// JSON: "bit_width":{{(128|512)}},"kind":1,"signedness":1,"owner_bin":0,{{[^]]*}}"role":6,"flags":0,"atoms":[{"ordinal":0,{{[^]]*}}]}]
// JSON: "bit_width":{{(128|512)}},"kind":1,"signedness":1,"owner_bin":0,{{[^]]*}}"role":6,"flags":0,"atoms":[{"ordinal":0,{{[^]]*}}]}]
// JSON-NOT: "role":6,
// JSON: "resolved_cross_selector_bindings":[
// JSON: "matches_policy":2,"matches_count":0,"flags":{{(8|11|12)}}}
// JSON: "matches_policy":2,"matches_count":0,"flags":{{(8|11|12)}}}
// JSON: "matches_policy":2,"matches_count":0,"flags":{{(8|11|12)}}}
// JSON: "matches_policy":2,"matches_count":0,"flags":{{(8|11|12)}}}
// JSON: "matches_policy":2,"matches_count":0,"flags":{{(8|11|12)}}}
// JSON: "matches_policy":2,"matches_count":0,"flags":{{(8|11|12)}}}
// JSON: "matches_policy":2,"matches_count":0,"flags":{{(8|11|12)}}}
// JSON: "matches_policy":2,"matches_count":0,"flags":{{(8|11|12)}}}
// JSON: "matches_policy":2,"matches_count":0,"flags":{{(8|11|12)}}}
// JSON: "matches_policy":2,"matches_count":0,"flags":{{(8|11|12)}}}
// JSON: "matches_policy":2,"matches_count":0,"flags":{{(8|11|12)}}}
// JSON: "matches_policy":2,"matches_count":0,"flags":{{(8|11|12)}}}
// JSON: "matches_policy":2,"matches_count":0,"flags":{{(8|11|12)}}}
// JSON: "matches_policy":2,"matches_count":0,"flags":{{(8|11|12)}}}
// JSON-NOT: "matches_count":{{[0-9]+}},"flags":{{(9|10|13|14|15)}}}
// JSON: "resolved_functional_tuple_sets":[

// Snapshot limbs keep the full constructor value: 2**511 is limb 7 bit 63 of
// a 512-bit value; 2**65 and 2**65 + 1 are limbs 1 (=2) and 0 (=1); the folded
// 128-bit literal 2**65 and the 2**64 queue count (limb 1 = 1) are retained.
// VALUES-DAG: "bit_width":512,"kind":1,"signedness":1,"owner_bin":0,"owner_selector":{{[0-9]+}},"owner_ordinal":{{[0-9]+}},"owner_subordinal":0,"role":6,"flags":0,"atoms":[{"ordinal":0,"kind":1,"real_low_bits":0,"real_high_bits":0,"flags":3,"limbs":[{"ordinal":0,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0},{"ordinal":1,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0},{"ordinal":2,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0},{"ordinal":3,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0},{"ordinal":4,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0},{"ordinal":5,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0},{"ordinal":6,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0},{"ordinal":7,"low_aval":9223372036854775808,"low_bval":0,"high_aval":9223372036854775808,"high_bval":0,"wildcard_mask":0}]}]
// VALUES-DAG: "bit_width":512,"kind":1,"signedness":1,"owner_bin":0,"owner_selector":{{[0-9]+}},"owner_ordinal":{{[0-9]+}},"owner_subordinal":0,"role":6,"flags":0,"atoms":[{"ordinal":0,"kind":1,"real_low_bits":0,"real_high_bits":0,"flags":3,"limbs":[{"ordinal":0,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0},{"ordinal":1,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0},{"ordinal":2,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0},{"ordinal":3,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0},{"ordinal":4,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0},{"ordinal":5,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0},{"ordinal":6,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0},{"ordinal":7,"low_aval":9223372036854775808,"low_bval":0,"high_aval":9223372036854775808,"high_bval":0,"wildcard_mask":0}]}]
// VALUES-DAG: "bit_width":512,"kind":1,"signedness":1,"owner_bin":0,"owner_selector":{{[0-9]+}},"owner_ordinal":{{[0-9]+}},"owner_subordinal":0,"role":6,"flags":0,"atoms":[{"ordinal":0,"kind":1,"real_low_bits":0,"real_high_bits":0,"flags":3,"limbs":[{"ordinal":0,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0},{"ordinal":1,"low_aval":2,"low_bval":0,"high_aval":2,"high_bval":0,"wildcard_mask":0},{"ordinal":2,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0},{"ordinal":3,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0},{"ordinal":4,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0},{"ordinal":5,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0},{"ordinal":6,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0},{"ordinal":7,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0}]}]
// VALUES-DAG: "bit_width":512,"kind":1,"signedness":1,"owner_bin":0,"owner_selector":{{[0-9]+}},"owner_ordinal":{{[0-9]+}},"owner_subordinal":0,"role":6,"flags":0,"atoms":[{"ordinal":0,"kind":1,"real_low_bits":0,"real_high_bits":0,"flags":3,"limbs":[{"ordinal":0,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0},{"ordinal":1,"low_aval":2,"low_bval":0,"high_aval":2,"high_bval":0,"wildcard_mask":0},{"ordinal":2,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0},{"ordinal":3,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0},{"ordinal":4,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0},{"ordinal":5,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0},{"ordinal":6,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0},{"ordinal":7,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0}]}]
// VALUES-DAG: "bit_width":512,"kind":1,"signedness":1,"owner_bin":0,"owner_selector":{{[0-9]+}},"owner_ordinal":{{[0-9]+}},"owner_subordinal":0,"role":6,"flags":0,"atoms":[{"ordinal":0,"kind":1,"real_low_bits":0,"real_high_bits":0,"flags":3,"limbs":[{"ordinal":0,"low_aval":1,"low_bval":0,"high_aval":1,"high_bval":0,"wildcard_mask":0},{"ordinal":1,"low_aval":2,"low_bval":0,"high_aval":2,"high_bval":0,"wildcard_mask":0},{"ordinal":2,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0},{"ordinal":3,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0},{"ordinal":4,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0},{"ordinal":5,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0},{"ordinal":6,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0},{"ordinal":7,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0}]}]
// VALUES-DAG: "bit_width":512,"kind":1,"signedness":1,"owner_bin":0,"owner_selector":{{[0-9]+}},"owner_ordinal":{{[0-9]+}},"owner_subordinal":0,"role":6,"flags":0,"atoms":[{"ordinal":0,"kind":1,"real_low_bits":0,"real_high_bits":0,"flags":3,"limbs":[{"ordinal":0,"low_aval":1,"low_bval":0,"high_aval":1,"high_bval":0,"wildcard_mask":0},{"ordinal":1,"low_aval":2,"low_bval":0,"high_aval":2,"high_bval":0,"wildcard_mask":0},{"ordinal":2,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0},{"ordinal":3,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0},{"ordinal":4,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0},{"ordinal":5,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0},{"ordinal":6,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0},{"ordinal":7,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0}]}]
// VALUES-DAG: "bit_width":128,"kind":1,"signedness":1,"owner_bin":0,"owner_selector":{{[0-9]+}},"owner_ordinal":{{[0-9]+}},"owner_subordinal":0,"role":6,"flags":0,"atoms":[{"ordinal":0,"kind":1,"real_low_bits":0,"real_high_bits":0,"flags":3,"limbs":[{"ordinal":0,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0},{"ordinal":1,"low_aval":2,"low_bval":0,"high_aval":2,"high_bval":0,"wildcard_mask":0}]}]
// VALUES-DAG: "bit_width":128,"kind":1,"signedness":1,"owner_bin":0,"owner_selector":{{[0-9]+}},"owner_ordinal":{{[0-9]+}},"owner_subordinal":0,"role":6,"flags":0,"atoms":[{"ordinal":0,"kind":1,"real_low_bits":0,"real_high_bits":0,"flags":3,"limbs":[{"ordinal":0,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0},{"ordinal":1,"low_aval":2,"low_bval":0,"high_aval":2,"high_bval":0,"wildcard_mask":0}]}]
// VALUES-DAG: "bit_width":128,"kind":1,"signedness":1,"owner_bin":0,"owner_selector":{{[0-9]+}},"owner_ordinal":{{[0-9]+}},"owner_subordinal":0,"role":6,"flags":0,"atoms":[{"ordinal":0,"kind":1,"real_low_bits":0,"real_high_bits":0,"flags":3,"limbs":[{"ordinal":0,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0},{"ordinal":1,"low_aval":2,"low_bval":0,"high_aval":2,"high_bval":0,"wildcard_mask":0}]}]
// VALUES-DAG: "bit_width":128,"kind":1,"signedness":1,"owner_bin":0,"owner_selector":{{[0-9]+}},"owner_ordinal":{{[0-9]+}},"owner_subordinal":0,"role":6,"flags":0,"atoms":[{"ordinal":0,"kind":1,"real_low_bits":0,"real_high_bits":0,"flags":3,"limbs":[{"ordinal":0,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0},{"ordinal":1,"low_aval":2,"low_bval":0,"high_aval":2,"high_bval":0,"wildcard_mask":0}]}]
// VALUES-DAG: "bit_width":128,"kind":1,"signedness":1,"owner_bin":0,"owner_selector":{{[0-9]+}},"owner_ordinal":{{[0-9]+}},"owner_subordinal":0,"role":6,"flags":0,"atoms":[{"ordinal":0,"kind":1,"real_low_bits":0,"real_high_bits":0,"flags":3,"limbs":[{"ordinal":0,"low_aval":1,"low_bval":0,"high_aval":1,"high_bval":0,"wildcard_mask":0},{"ordinal":1,"low_aval":2,"low_bval":0,"high_aval":2,"high_bval":0,"wildcard_mask":0}]}]
// VALUES-DAG: "bit_width":128,"kind":1,"signedness":1,"owner_bin":0,"owner_selector":{{[0-9]+}},"owner_ordinal":{{[0-9]+}},"owner_subordinal":0,"role":6,"flags":0,"atoms":[{"ordinal":0,"kind":1,"real_low_bits":0,"real_high_bits":0,"flags":3,"limbs":[{"ordinal":0,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0},{"ordinal":1,"low_aval":1,"low_bval":0,"high_aval":1,"high_bval":0,"wildcard_mask":0}]}]
// VALUES-DAG: "bit_width":128,"kind":1,"signedness":1,"owner_bin":0,"owner_selector":{{[0-9]+}},"owner_ordinal":{{[0-9]+}},"owner_subordinal":0,"role":6,"flags":0,"atoms":[{"ordinal":0,"kind":1,"real_low_bits":0,"real_high_bits":0,"flags":3,"limbs":[{"ordinal":0,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0},{"ordinal":1,"low_aval":1,"low_bval":0,"high_aval":1,"high_bval":0,"wildcard_mask":0}]}]
// VALUES-DAG: "bit_width":128,"kind":1,"signedness":1,"owner_bin":0,"owner_selector":{{[0-9]+}},"owner_ordinal":{{[0-9]+}},"owner_subordinal":0,"role":6,"flags":0,"atoms":[{"ordinal":0,"kind":1,"real_low_bits":0,"real_high_bits":0,"flags":3,"limbs":[{"ordinal":0,"low_aval":0,"low_bval":0,"high_aval":0,"high_bval":0,"wildcard_mask":0},{"ordinal":1,"low_aval":1,"low_bval":0,"high_aval":1,"high_bval":0,"wildcard_mask":0}]}]

// The scalar bins with wide counts (flags 11 = scalar, true, wide), the
// symbolic ones (flags 12 = symbolic, wide), the tuple-set bin (flags 8), and
// the narrow nested parent (flags 4, matches 1) keep per-selector flags.
// VALUES-DAG: "with_expression":{{[1-9][0-9]*}},"tuple_set":0,"matches_policy":2,"matches_count":0,"flags":11}
// VALUES-DAG: "with_expression":{{[1-9][0-9]*}},"tuple_set":0,"matches_policy":2,"matches_count":0,"flags":11}
// VALUES-DAG: "with_expression":{{[1-9][0-9]*}},"tuple_set":0,"matches_policy":2,"matches_count":0,"flags":11}
// VALUES-DAG: "with_expression":{{[1-9][0-9]*}},"tuple_set":0,"matches_policy":2,"matches_count":0,"flags":11}
// VALUES-DAG: "with_expression":{{[1-9][0-9]*}},"tuple_set":0,"matches_policy":2,"matches_count":0,"flags":11}
// VALUES-DAG: "with_expression":{{[1-9][0-9]*}},"tuple_set":0,"matches_policy":2,"matches_count":0,"flags":11}
// VALUES-DAG: "with_expression":0,"tuple_set":0,"matches_policy":2,"matches_count":0,"flags":12}
// VALUES-DAG: "with_expression":0,"tuple_set":0,"matches_policy":2,"matches_count":0,"flags":12}
// VALUES-DAG: "with_expression":0,"tuple_set":0,"matches_policy":2,"matches_count":0,"flags":12}
// VALUES-DAG: "with_expression":0,"tuple_set":0,"matches_policy":2,"matches_count":0,"flags":12}
// VALUES-DAG: "with_expression":0,"tuple_set":0,"matches_policy":2,"matches_count":0,"flags":12}
// VALUES-DAG: "with_expression":0,"tuple_set":0,"matches_policy":2,"matches_count":0,"flags":12}
// VALUES-DAG: "with_expression":0,"tuple_set":0,"matches_policy":2,"matches_count":0,"flags":12}
// VALUES-DAG: "matches_policy":2,"matches_count":0,"flags":8}
// VALUES-DAG: "matches_policy":2,"matches_count":1,"flags":4}

// MERGED: runs: 2
// MERGED: resolved-instances: 12
