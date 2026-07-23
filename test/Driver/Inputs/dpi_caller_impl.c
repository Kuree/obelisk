#include "svdpi.h"

#include <stdint.h>
#include <string.h>

int32_t dpi_caller(void) {
  const char *file = 0;
  int line = 0;
  if (!svGetCallerInfo(&file, &line) || !file || line != 4)
    return -1;
  if (strstr(file, "dpi-caller-a.sv"))
    return 11;
  if (strstr(file, "dpi-caller-b.sv"))
    return 22;
  return -2;
}
