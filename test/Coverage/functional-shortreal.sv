// RUN: %obelisk --std=1800-2023 --coverage=functional -O0 -o %t %s
// RUN: %t --coverage-output=%t.obcov | FileCheck %s
// RUN: obelisk-cov inspect %t.obcov | FileCheck %s --check-prefix=INSPECT
// RUN: obelisk-cov report --format=json %t.obcov | FileCheck %s --check-prefix=JSON
// RUN: obelisk-cov report --format=json %t.obcov | FileCheck %s --check-prefix=ROWS

// LRM 6.12, 19.3, 19.5 and 19.8.1: shortreal captures preserve IEEE
// binary32 bits. Ref arguments follow storage and input arguments are local.
module top;
  shortreal actual, signed_zero;
  real wide;
  function automatic bit replace_local(ref shortreal selected,
                                        input shortreal next);
    selected = next;
    return $shortrealtobits(selected) == $shortrealtobits(next);
  endfunction
  covergroup capture(input shortreal frozen, ref shortreal live)
      with function sample(input shortreal current);
    cp: coverpoint current {
      bins tenth = {shortreal'(0.1)}; bins half = {0.5}; bins high = {1.5};
    }
    snapshot: coverpoint ($shortrealtobits(frozen) == 32'h3dcccccd &&
                         live == current) { bins yes = {1}; }
  endgroup
  covergroup local_copy with function sample(input shortreal selected,
                                             input shortreal next);
    cp: coverpoint replace_local(selected, next) { bins yes = {1}; }
  endgroup
  covergroup live_sample with function sample(ref shortreal selected,
                                              input shortreal next);
    cp: coverpoint replace_local(selected, next) { bins yes = {1}; }
  endgroup
  covergroup defaults(input shortreal frozen = 0.125)
      with function sample(input shortreal current = 0.25);
    cp: coverpoint (frozen == 0.125 && current == 0.25) { bins yes = {1}; }
  endgroup
  covergroup bits(input shortreal frozen)
      with function sample(input shortreal current);
    zero: coverpoint $shortrealtobits(frozen) { bins negative = {32'h80000000}; }
    nan: coverpoint $shortrealtobits(current) { bins payload = {32'h7fc12345}; }
  endgroup
  covergroup real_ref(ref real live);
    cp: coverpoint (live == 1.5) { bins yes = {1}; }
  endgroup
  capture captured;
  local_copy copied;
  live_sample live;
  defaults defaulted;
  bits raw_bits;
  real_ref wide_reader;
  initial begin
    actual = 0.1;
    captured = new(actual, actual);
    captured.sample(actual);
    actual = 0.5; captured.sample(actual);
    actual = 1.5; captured.sample(actual);
    $display("capture=%0.0f", captured.get_inst_coverage());
    actual = 0.5;
    copied = new; copied.sample(actual, 1.5);
    $display("copy=%0.0f retained=%0d", copied.get_inst_coverage(), actual == 0.5);
    live = new; live.sample(actual, 1.5);
    $display("ref=%0.0f replaced=%0d", live.get_inst_coverage(), actual == 1.5);
    defaulted = new; defaulted.sample();
    $display("defaults=%0.0f", defaulted.get_inst_coverage());
    signed_zero = $bitstoshortreal(32'h80000000);
    raw_bits = new(signed_zero);
    raw_bits.sample($bitstoshortreal(32'h7fc12345));
    $display("bits=%0.0f", raw_bits.get_inst_coverage());
    wide = 0.5;
    wide_reader = new(wide);
    wide = 1.5; wide_reader.sample();
    $display("real_ref=%0.0f", wide_reader.get_inst_coverage());
    $finish;
  end
endmodule

// CHECK: capture=100
// CHECK: copy=100 retained=1
// CHECK: ref=100 replaced=1
// CHECK: defaults=100
// CHECK: bits=100
// CHECK: real_ref=100

// INSPECT: functional-formals: 12

// Formal rows keep shortreal as result_kind 18 with no integer width: eleven
// shortreal formals (two of them with default expressions) plus the one real
// ref formal (result_kind 3).
// JSON: "functional_formals":[
// JSON-COUNT-11: "result_kind":18,"flags":{{[01]}},"bit_width":0,
// JSON-NOT: "result_kind":18
// JSON: "functional_expressions"

// The real ref formal stays result_kind 3, and the two defaulted formals carry a
// nonzero default expression (flags 1).
// ROWS: "functional_formals":[
// ROWS-DAG: "name":"live","kind":1,"direction":2,"result_kind":3,"flags":0,"bit_width":0
// ROWS-DAG: "name":"frozen","kind":1,"direction":1,"result_kind":18,"flags":1,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":{{[1-9][0-9]*}}
// ROWS-DAG: "name":"current","kind":2,"direction":1,"result_kind":18,"flags":1,"bit_width":0,"signedness":3,"ordinal":0,"default_expression":{{[1-9][0-9]*}}
// ROWS: "functional_container_types"
