#include "svdpi.h"
#include <stdint.h>

int32_t two_state_dpi(const svLogicVecVal *source,
                      const svLogicVecVal *timestamp,
                      svLogicVecVal *destination, svLogicVecVal *number,
                      svLogicVecVal *packet) {
  int valid = source[0].aval == 0xabcdef01 && source[1].aval == 0x12345678 &&
              source[2].aval == 0 && source[0].bval == 0 &&
              source[1].bval == 0 && source[2].bval == 0 &&
              timestamp[0].aval == 0x9abcdef0 &&
              timestamp[1].aval == 0x12345678 && timestamp[0].bval == 0 &&
              timestamp[1].bval == 0 && packet[0].aval == 0x12345678 &&
              packet[1].aval == 0x98765432 && packet[0].bval == 0 &&
              packet[1].bval == 0;
  destination[0] = (svLogicVecVal){0xffffffff, 0xfffffffe};
  destination[1] = (svLogicVecVal){0xffffffff, 0xffffffff};
  destination[2] = (svLogicVecVal){1, 0};
  number[0] = (svLogicVecVal){0x1234ffff, 0xffff};
  packet[0] = (svLogicVecVal){0xaaaaaaaa, 0};
  packet[1] = (svLogicVecVal){0xffffffff, 0xaaaaaaaa};
  return valid;
}

svLogic two_state_scalar(svLogic source, svLogic *destination) {
  *destination = sv_z;
  return source == sv_0 ? sv_x : sv_1;
}

typedef struct {
  svLogicVecVal vector[1];
  svLogic flag;
} record_t;

int32_t two_state_aggregate(const record_t *source, record_t *destination) {
  int valid = source->vector[0].aval == 0x12345678 &&
              source->vector[0].bval == 0 && source->flag == sv_0;
  destination->vector[0] = (svLogicVecVal){0x1234ffff, 0xffff};
  destination->flag = sv_z;
  return valid;
}

extern svLogic two_state_export(svLogic, const svLogicVecVal *,
                                svLogicVecVal *);

int32_t two_state_export_check(void) {
  svLogicVecVal source[3] = {
      {0xffffffff, 0xfffffffe}, {0xffffffff, 0xffffffff}, {1, 0}};
  svLogicVecVal destination[3] = {
      {0, 0xffffffff}, {0, 0xffffffff}, {0, 0xffffffff}};
  svLogic result = two_state_export(sv_x, source, destination);
  return result == sv_0 && destination[0].aval == 2 &&
         destination[1].aval == 0 && destination[2].aval == 1 &&
         destination[0].bval == 0 && destination[1].bval == 0 &&
         destination[2].bval == 0;
}

int32_t two_state_open(const svOpenArrayHandle values) {
  if (svSize(values, 1) != 2)
    return 0;
  svLogicVecVal first[3] = {{0, 1}, {0, 1}, {0, 1}};
  svGetLogicArrElem1VecVal(first, values, 0);
  const svLogicVecVal *second = svGetArrElemPtr1(values, 1);
  int valid = first[0].aval == 1 && first[1].aval == 0 && first[2].aval == 0 &&
              first[0].bval == 0 && first[1].bval == 0 && first[2].bval == 0 &&
              second && second[0].aval == 2 && second[1].aval == 0 &&
              second[2].aval == 0 && second[0].bval == 0 &&
              second[1].bval == 0 && second[2].bval == 0;
  svLogicVecVal replacement[3] = {
      {0xffffffff, 0xfffffff8}, {0xffffffff, 0xffffffff}, {1, 0}};
  svPutLogicArrElem1VecVal(values, replacement, 0);
  return valid;
}
