// RUN: %talos --std=1800-2023 -O0 --target=native --coverage=functional %s -o %t.native
// RUN: %t.native --coverage-output=%t.native.obcov | FileCheck %s
// RUN: %talos --std=1800-2023 -O3 --execution-tier=bytecode --coverage=functional %s -o %t.bytecode
// RUN: %t.bytecode --coverage-output=%t.bytecode.obcov | FileCheck %s
// RUN: talos-cov report --format=text %t.native.obcov -o %t.native.txt
// RUN: talos-cov report --format=text %t.bytecode.obcov -o %t.bytecode.txt
// RUN: diff -u %t.native.txt %t.bytecode.txt

// LRM 15.4.9, G.4 and 19.8.1: mailbox payloads can themselves be mailbox
// handles. Copying the containing message or queue shares each mailbox.
module top;
  typedef mailbox #(int) int_box;
  int_box first, read;
  int_box owners[$], copied[$];
  mailbox #(int_box) envelopes;
  covergroup nested with function sample(input mailbox #(int_box) selected,
                                         input int_box handles[$]);
    cp: coverpoint selected.num() + handles.size() { bins two = {2}; }
  endgroup
  nested captured;
  initial begin
    first = new; first.put(7);
    owners.push_back(first);
    copied = owners;
    envelopes = new; envelopes.put(first);
    captured = new; captured.sample(envelopes, owners);
    envelopes.get(read);
    $display("nested=%0.0f count=%0d same=%0d copy=%0d", captured.get_inst_coverage(),
             read.num(), read == owners[0], copied[0] == first);
    read.put(8);
    $display("shared=%0d,%0d,%0d", first.num(), owners[0].num(), copied[0].num());
    $finish;
  end
endmodule

// CHECK: nested=100 count=1 same=1 copy=1
// CHECK: shared=2,2,2
