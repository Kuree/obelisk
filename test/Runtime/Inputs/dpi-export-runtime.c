//===- dpi-export-runtime.c - C caller for DPI export lowering ------------===//

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint8_t svBit;
typedef uint8_t svLogic;
typedef struct {
  uint32_t aval;
  uint32_t bval;
} svLogicVecVal;
typedef void *svScope;

extern int8_t export_narrow(int8_t value, svBit bit, svLogic logic);
extern void export_vector(svLogicVecVal *result, const svLogicVecVal *value);
extern void export_output(int16_t *value);
extern void export_ping(void);
extern svLogic export_logic(svLogic value);
extern int8_t export_branch_capture(int8_t value);
extern const char *export_echo_string(const char *value);
extern const char *export_reenter_string(void);
extern int8_t export_scoped(void);
extern svScope svGetScopeFromName(const char *name);
extern svScope svSetScope(const svScope scope);

const char *nested_echo(void) { return export_echo_string("nested"); }

void host(void) {
  int8_t narrow = export_narrow(-2, 3, 2);
  svLogic logic = export_logic(2);
  int8_t branch = export_branch_capture(1);
  svScope scope0 = svGetScopeFromName("$root");
  svScope scope1 = svGetScopeFromName("exports.child");
  svScope previous = svSetScope(scope0);
  int8_t scoped0 = export_scoped();
  svSetScope(scope1);
  int8_t scoped1 = export_scoped();
  svSetScope(previous);
  const char *first = export_echo_string("alpha");
  const char *direct = export_echo_string(first);
  char directCopy[16];
  snprintf(directCopy, sizeof(directCopy), "%s", direct);
  const char *nested = export_reenter_string();
  char nestedCopy[16];
  snprintf(nestedCopy, sizeof(nestedCopy), "%s", nested);
  svLogicVecVal input[2] = {
      {UINT32_C(0x89abcdef), UINT32_C(0x12345678)},
      {UINT32_C(0xfffffff5), UINT32_C(0xffffffe2)},
  };
  svLogicVecVal output[2] = {{UINT32_MAX, UINT32_MAX},
                             {UINT32_MAX, UINT32_MAX}};
  export_vector(output, input);
  uint8_t misalignedStorage[sizeof(int16_t) + 1] = {0};
  export_output((int16_t *)(misalignedStorage + 1));
  int16_t copiedOutput = 0;
  memcpy(&copiedOutput, misalignedStorage + 1, sizeof(copiedOutput));
  export_ping();
  printf("narrow=%d logic=%u branch=%d scopes=%d/%d direct=%s nested=%s "
         "vector=%x/%x/%x/%x output=%x\n",
         narrow, logic, branch, scoped0, scoped1, directCopy, nestedCopy,
         output[0].aval, output[0].bval, output[1].aval, output[1].bval,
         (uint16_t)copiedOutput);
}
