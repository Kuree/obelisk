#include "svdpi.h"
#include <stdint.h>
#include <stdio.h>

extern int c_cancel_parent(int32_t *result);
extern int c_cancel_self(int32_t *result);
extern int c_work(int32_t value, int32_t *result);

int drive(int32_t *result) { return c_work(41, result); }

int drive_cancel_self(int32_t *result) {
  int status = c_cancel_self(result);
  printf("self-disabled=%d api=%d c-value=%d\n", status, svIsDisabledState(),
         *result);
  return status;
}

int drive_cancel_parent(int32_t *result) {
  int status = c_cancel_parent(result);
  printf("parent-disabled=%d api=%d c-value=%d\n", status, svIsDisabledState(),
         *result);
  return status;
}
