#include "svdpi.h"

#include <stdio.h>

int inspect_arrays(const svOpenArrayHandle ints,
                   const svOpenArrayHandle logic_values,
                   const svOpenArrayHandle bytes) {
  int *raw = (int *)svGetArrayPtr(ints);
  printf("dims=%d int-range=%d:%d packed=%d:%d/%d raw=%d,%d,%d\n",
         svDimensions(ints), svLeft(ints, 1), svRight(ints, 1), svLeft(ints, 0),
         svRight(ints, 0), svSize(ints, 0), raw[0], raw[1], raw[2]);

  svLogicVecVal value;
  svGetLogicArrElem1VecVal(&value, logic_values, 2);
  printf("logic=%x/%x\n", value.aval, value.bval);
  value.aval = 0x16;
  value.bval = 0x0c;
  svPutLogicArrElem1VecVal(logic_values, &value, 2);

  unsigned char *first = (unsigned char *)svGetArrElemPtr1(bytes, 1);
  unsigned char *second = (unsigned char *)svGetArrElemPtr1(bytes, 0);
  *first = 0x41;
  *second = 0x42;
  return 0;
}

int inspect_dynamic(const svOpenArrayHandle values,
                    const svOpenArrayHandle empty) {
  int *raw = (int *)svGetArrayPtr(values);
  printf("dynamic=%d range=%d:%d raw=%d,%d,%d empty=%d range=%d:%d "
         "low=%d high=%d inc=%d ptr=%s\n",
         svSize(values, 1), svLeft(values, 1), svRight(values, 1), raw[0],
         raw[1], raw[2], svSize(empty, 1), svLeft(empty, 1), svRight(empty, 1),
         svLow(empty, 1), svHigh(empty, 1), svIncrement(empty, 1),
         svGetArrayPtr(empty) ? "nonnull" : "null");
  raw[1] = 42;
  return 0;
}
