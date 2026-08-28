#include "svdpi.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
  int32_t id;
  svLogicVecVal state[1];
  const char *label;
} payload_t;

int inspect_composite(const svOpenArrayHandle matrix,
                      const svOpenArrayHandle dynamic_values,
                      const svOpenArrayHandle labels,
                      const svOpenArrayHandle tokens) {
  payload_t *first = (payload_t *)svGetArrElemPtr2(matrix, 1, -1);
  payload_t *dynamic_first =
      (payload_t *)svGetArrElemPtr1(dynamic_values, 0);
  const char **label_data = (const char **)svGetArrayPtr(labels);
  void **token_data = (void **)svGetArrayPtr(tokens);
  printf("matrix-dims=%d ranges=%d:%d,%d:%d first=%d/%s/%x\n",
         svDimensions(matrix), svLeft(matrix, 1), svRight(matrix, 1),
         svLeft(matrix, 2), svRight(matrix, 2), first->id, first->label,
         first->state[0].aval);
  printf("dynamic=%d first=%d/%s labels=%s,%s tokens=%" PRIxPTR ",%" PRIxPTR
         "\n",
         svSize(dynamic_values, 1), dynamic_first->id, dynamic_first->label,
         label_data[0], label_data[1], (uintptr_t)token_data[0],
         (uintptr_t)token_data[1]);
  first->id = 101;
  first->label = "c-matrix";
  label_data[0] = "c-right";
  label_data[1] = "c-left";
  token_data[0] = (void *)(uintptr_t)0x1234;
  return 0;
}

int inspect_open_packed(const svOpenArrayHandle packed_only,
                        const svOpenArrayHandle packed_elements) {
  const svBitVecVal *raw = (const svBitVecVal *)svGetArrayPtr(packed_only);
  svLogicVecVal value = {0, 0};
  svGetLogicArrElem1VecVal(&value, packed_elements, 1);
  printf("packed-dims=%d range=%d:%d raw=%x elements=%d elem-range=%d:%d "
         "value=%x/%x\n",
         svDimensions(packed_only), svLeft(packed_only, 0),
         svRight(packed_only, 0), raw[0], svSize(packed_elements, 1),
         svLeft(packed_elements, 0), svRight(packed_elements, 0), value.aval,
         value.bval);
  value.aval = 0x2b;
  value.bval = 0x0c;
  svPutLogicArrElem1VecVal(packed_elements, &value, 1);
  return 0;
}

int32_t token_is_1234(void *token) {
  return token == (void *)(uintptr_t)0x1234;
}
