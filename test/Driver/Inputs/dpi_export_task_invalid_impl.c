#include <stdint.h>

extern int c_illegal_task(int32_t *result);

int call_task_from_function(void) {
  int32_t result = 0;
  return c_illegal_task(&result);
}
