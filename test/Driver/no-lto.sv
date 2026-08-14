// -fno-lto links the prebuilt runtime archive instead of running full LTO over
// runtime bitcode. The resulting simulator must behave identically; only the
// link cost differs.

// RUN: obelisk -O3 %s -o %t.lto
// RUN: %t.lto > %t.lto.out
// RUN: obelisk -O3 -fno-lto %s -o %t.nolto
// RUN: %t.nolto > %t.nolto.out
// RUN: diff -u %t.lto.out %t.nolto.out
// RUN: FileCheck %s --check-prefix=STDOUT < %t.nolto.out

// -flto restores the default, and the last of the pair wins.
// RUN: obelisk -O3 -fno-lto -flto %s -o %t.relto
// RUN: %t.relto > %t.relto.out
// RUN: diff -u %t.lto.out %t.relto.out

// The bytecode tier pays the same link, and opts out the same way.
// RUN: obelisk -O3 -fno-lto --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode > %t.bytecode.out
// RUN: diff -u %t.lto.out %t.bytecode.out

// -O0 never ran LTO, so the flag is accepted and redundant there.
// RUN: obelisk -O0 -fno-lto %s -o %t.o0
// RUN: %t.o0 > %t.o0.out
// RUN: diff -u %t.lto.out %t.o0.out

// A command file can carry it, which is the point of expanding `-f` into argv.
// RUN: echo "-fno-lto" > %t.flags.f
// RUN: obelisk -O3 -f %t.flags.f %s -o %t.viaflags
// RUN: %t.viaflags > %t.viaflags.out
// RUN: diff -u %t.lto.out %t.viaflags.out

module no_lto;
  int accumulator;
  initial begin
    for (int index = 0; index < 8; index++)
      accumulator += index * index;
    $display("accumulator=%0d", accumulator);
    #5 $display("time=%0t", $time);
    $finish;
  end
endmodule

// STDOUT: accumulator=140
// STDOUT: time=5
