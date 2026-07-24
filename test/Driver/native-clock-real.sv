// RUN: obelisk -O0 %s -o %t.o0.native
// RUN: %t.o0.native > %t.o0.native.out
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.o0.bytecode
// RUN: %t.o0.bytecode > %t.o0.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o0.bytecode.out
// RUN: obelisk -O3 %s -o %t.o3.native
// RUN: %t.o3.native > %t.o3.native.out
// RUN: obelisk -O3 --execution-tier=bytecode %s -o %t.o3.bytecode
// RUN: %t.o3.bytecode > %t.o3.bytecode.out
// RUN: diff -u %t.o3.native.out %t.o3.bytecode.out
// RUN: diff -u %t.o0.native.out %t.o3.native.out
// RUN: FileCheck %s < %t.o3.native.out

module native_clock_real;
  timeunit 10ns;
  timeprecision 1ns;

  realtime saved;
  real copied;
  real nba_saved;
  time whole;
  integer rounded;

  function automatic realtime read_clock();
    return $realtime;
  endfunction

  function automatic real identity(input real value);
    if ($realtime == value)
      return value;
    return value;
  endfunction

  function automatic bit copy_negative(
      output real signed_value, output real unsigned_value);
    integer source;
    source = -3;
    signed_value = source;
    unsigned_value = source;
    return 1;
  endfunction

  initial begin : body
    automatic realtime local_value;
    automatic real integral_copy;
    automatic integer negative;
    automatic real negative_real;
    automatic integer negative_roundtrip;
    automatic integer copyout_signed;
    automatic logic [7:0] copyout_unsigned;
    automatic bit copyout_ok;
    automatic logic [79:0] wide;
    automatic logic [79:0] wide_roundtrip;
    automatic real wide_real;
    automatic logic [79:0] tricky;
    automatic logic [79:0] tricky_roundtrip;
    automatic real tricky_real;
    automatic logic [1087:0] enormous;
    automatic real infinite_delay;
    #1.6;
    saved = read_clock();
    copied = identity(saved);
    local_value = copied;
    whole = saved;
    rounded = saved;
    integral_copy = rounded;
    negative = -3;
    negative_real = negative;
    negative_roundtrip = negative_real;
    copyout_ok = copy_negative(copyout_signed, copyout_unsigned);
    wide = 80'h00400000000000000000;
    wide_real = wide;
    wide_roundtrip = wide_real;
    tricky = 80'd18446744073709553665;
    tricky_real = tricky;
    tricky_roundtrip = tricky_real;
    $display("clock time=%0t real-time=%0t raw=%0d stime=%0d realtime=%0.1f saved=%0.1f copied=%0.1f whole=%0d rounded=%0d integral=%0.1f negative=%0.1f roundtrip=%0d",
             $time, $realtime, $time, $stime, $realtime, saved, copied, whole,
             rounded, integral_copy, negative_real, negative_roundtrip);
    $display("paren time=%0d stime=%0d realtime=%0.1f",
             $time(), $stime(), $realtime());
    $display("copyout ok=%0d signed=%0d unsigned=%0d",
             copyout_ok, copyout_signed, copyout_unsigned);
    $display("wide bit=%0d tricky-low=%0d",
             wide_roundtrip[70], tricky_roundtrip[15:0]);
    nba_saved <= saved;
    #(local_value);
    $display("later time=%0d realtime=%0.1f local=%0.1f nba=%0.1f",
             $time, $realtime, local_value, nba_saved);
    if (saved && saved == copied)
      $display("compare-ok");
    #4294967294;
    $display("wrap time=%0d stime=%0d realtime=%0.1f",
             $time, $stime, $realtime);
    enormous = 0;
    enormous[1087] = 1;
    infinite_delay = enormous;
    #(infinite_delay);
    $display("infinite-delay time=%0d", $time);
    #(wide_real);
    $display("overflow-delay time=%0d", $time);
  end
endmodule

// CHECK: clock time=20 real-time=16 raw=2 stime=2 realtime=1.6 saved=1.6 copied=1.6 whole=2 rounded=2 integral=2.0 negative=-3.0 roundtrip=-3
// CHECK-NEXT: paren time=2 stime=2 realtime=1.6
// CHECK-NEXT: copyout ok=1 signed=-3 unsigned=253
// CHECK-NEXT: wide bit=1 tricky-low=4096
// CHECK-NEXT: later time=3 realtime=3.2 local=1.6 nba=1.6
// CHECK-NEXT: compare-ok
// CHECK-NEXT: wrap time=4294967297 stime=1 realtime=4294967297.2
// CHECK-NEXT: coarse-overflow time=1844674407370955161
// CHECK-NEXT: infinite-delay time=1844674407370955162
// CHECK-NEXT: overflow-delay time=1844674407370955162

module coarse_real_delay;
  timeunit 10ns;
  timeprecision 10ns;

  initial begin
    automatic logic [79:0] wide;
    automatic real delay_value;
    wide = 80'h00400000000000000000;
    delay_value = wide;
    #(delay_value);
    $display("coarse-overflow time=%0d", $time);
  end
endmodule
