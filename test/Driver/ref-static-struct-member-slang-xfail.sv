// XFAIL: *
// RUN: %obelisk -emit-slang %s -o /dev/null

// IEEE 1800-2023 13.5.2: a ref static argument may be passed a member of an
// unpacked structure with static lifetime. Slang v11 treats the member as an
// automatic variable before checking its static containing variable.
module top;
  typedef struct {
    int value;
  } S;
  S data;
  task automatic set(ref static int target);
    target = 7;
  endtask
  initial set(data.value);
endmodule
