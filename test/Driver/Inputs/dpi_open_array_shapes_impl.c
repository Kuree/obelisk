#include "svdpi.h"

#include <stdint.h>
#include <stdio.h>

typedef struct {
  int32_t id;
  svLogic state;
  const char *label;
} payload_t;

typedef struct obelisk_rt_context obelisk_rt_context;
extern obelisk_rt_context *obelisk_rt_v1_dpi_current_context(void);
extern int32_t obelisk_rt_v1_gc_set_threshold(obelisk_rt_context *context,
                                               uint64_t bytes);

int inspect_shapes(const svOpenArrayHandle nested,
                   const svOpenArrayHandle mixed,
                   const svOpenArrayHandle logic_nested,
                   const svOpenArrayHandle payloads,
                   const svOpenArrayHandle nested_strings,
                   const svOpenArrayHandle nested_tokens) {
  const int32_t *n = (const int32_t *)svGetArrayPtr(nested);
  const int32_t *m = (const int32_t *)svGetArrayPtr(mixed);
  svLogic l0 = svGetLogicArrElem2(logic_nested, 0, 0);
  svLogic l1 = svGetLogicArrElem2(logic_nested, 0, 1);
  const payload_t *payload =
      (const payload_t *)svGetArrElemPtr2(payloads, 0, 0);
  const char **strings = (const char **)svGetArrayPtr(nested_strings);
  void **tokens = (void **)svGetArrayPtr(nested_tokens);

  printf("nested=%dx%d data=%d,%d,%d,%d\n", svSize(nested, 1),
         svSize(nested, 2), n[0], n[1], n[2], n[3]);
  printf("mixed=%dx%d ranges=%d:%d,%d:%d data=%d,%d,%d,%d\n",
         svSize(mixed, 1), svSize(mixed, 2), svLeft(mixed, 1),
         svRight(mixed, 1), svLeft(mixed, 2), svRight(mixed, 2), m[0], m[1],
         m[2], m[3]);
  printf("logic=%x,%x payload=%d/%s/%x\n", l0, l1, payload->id,
         payload->label, payload->state);
  *(int32_t *)svGetArrElemPtr2(nested, 0, 1) = 42;
  obelisk_rt_context *context = obelisk_rt_v1_dpi_current_context();
  if (!context || obelisk_rt_v1_gc_set_threshold(context, 1) != 0)
    return 2;
  strings[0] = "copy-00-heap";
  strings[1] = "copy-01-heap";
  strings[2] = "copy-10-heap";
  strings[3] = "copy-11-heap";
  tokens[2] = (void *)(uintptr_t)0x5678;
  return 0;
}

int inspect_sized(const svOpenArrayHandle values) {
  svBitVecVal replacement = 0x5a;
  const svBitVecVal *data = (const svBitVecVal *)svGetArrayPtr(values);
  printf("sized-ranges=%d:%d packed=%d:%d data=%x,%x,%x\n",
         svLeft(values, 1), svRight(values, 1), svLeft(values, 0),
         svRight(values, 0), data[0], data[1], data[2]);
  svPutBitArrElem1VecVal(values, &replacement, 0);
  return 0;
}

int32_t token_is_5678(void *token) {
  return token == (void *)(uintptr_t)0x5678;
}
