#include <stdint.h>

extern int c_invalid_disable(int32_t *result);

int drive_invalid_disable(int32_t *result) {
  int status = c_invalid_disable(result);
  return status == 1 ? 2 : status;
}
