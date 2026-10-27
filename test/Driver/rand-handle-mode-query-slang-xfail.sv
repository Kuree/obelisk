// XFAIL: *
// RUN: %talos -emit-slang %s -o /dev/null

// IEEE 1800-2023 18.8: object.random_variable.rand_mode() returns the mode of
// a random variable, including a rand class handle, whose mode is separate
// from the random variables of the object it refers to. Slang v11 binds the
// call to the contained class's object-wide rand_mode(bit) and requires an
// argument.
class child_t;
endclass
class parent_t;
  rand child_t child;
endclass
module top;
  parent_t p = new;
  int mode;
  initial mode = p.child.rand_mode();
endmodule
