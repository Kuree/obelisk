// RUN: %split-file %s %t
// RUN: %obelisk --std=1800-2023 -emit-obelisk %t/input.sv -o %t/input.mlir
// RUN: obelisk-opt %t/input.mlir --obelisk-sim-prepare | FileCheck %s --check-prefix=RETAIN
// RUN: obelisk-opt %t/input.mlir '--lower-obelisk-to-sim=opt-level=0' '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t/lowered.mlir
// RUN: FileCheck %s --check-prefix=CALL --implicit-check-not=coverage.retained_symbols < %t/lowered.mlir
// RUN: obelisk-opt %t/lowered.mlir --convert-obelisk-sim-processes-to-llvm-coroutines | obelisk-translate --mlir-to-llvmir | opt -S -passes=verify -o /dev/null
// RUN: obelisk-opt %t/lowered.mlir --encode-obelisk-sim-to-bytecode='vpi=off' -o /dev/null

// IEEE 19.4 and 19.5: embedded coverpoint expressions use their owning object.
// Preserve both the helper's executable unit and its method descriptor through
// early DCE; repeated calls contribute each symbol only once. Release the
// transient inventory after executable calls provide the real symbol uses.
// RETAIN: coverage.retained_symbols = [@[[IMPL:unit_[0-9]+]], @[[METHOD:__obelisk_class_[A-Za-z0-9_.$]+]]]
// CALL: %{{[0-9]+}} = simulation.class.direct_call @[[IMPL:unit_[0-9]+]] {{.*}} -> i32
// CALL: %{{[0-9]+}} = simulation.class.direct_call @[[IMPL]] {{.*}} -> i32
// CALL: simulation.covergroup.sample

//--- input.sv
class Box;
  int backing = 1;
  function int read_current();
    return backing;
  endfunction
  covergroup cg;
    cp: coverpoint read_current() { bins one = {1}; }
    again: coverpoint read_current() { bins one = {1}; }
  endgroup
  function new();
    cg = new;
  endfunction
endclass
module top;
  Box box;
  initial begin
    box = new;
    box.cg.sample();
  end
endmodule
