// REQUIRES: native-elf
// RUN: %talos -O0 -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --check-prefix=MAIN < %t.ll
// RUN: FileCheck %s --check-prefix=RAMP < %t.ll

// Generated code never unwinds, so on ELF its functions are nounwind and
// need no .eh_frame entries.
// MAIN: define {{.*}}@main({{.*}}) #[[MAIN:[0-9]+]] {
// MAIN: attributes #[[MAIN]] = { {{.*}}nounwind
// RAMP: define {{.*}}@{{.*}}.__talos_coro_ramp({{.*}}) #[[RAMP:[0-9]+]] {
// RAMP: attributes #[[RAMP]] = { {{.*}}nounwind

module native_elf_nounwind;
  int count;
  initial begin
    #1 count = count + 1;
    $display("%0d", count);
  end
endmodule
