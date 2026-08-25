#include "svdpi.h"

#include <stdint.h>

int32_t dpi_legacy(int32_t value) { return value + 1; }

void dpi_integer_time_io(const svLogicVecVal *integer_input,
                         const svLogicVecVal *time_input,
                         svLogicVecVal *integer_output,
                         svLogicVecVal *time_output,
                         svLogicVecVal *integer_inout,
                         svLogicVecVal *time_inout) {
  integer_output[0] = integer_input[0];
  time_output[0] = time_input[0];
  time_output[1] = time_input[1];
  integer_inout[0] = integer_input[0];
  time_inout[0] = time_input[0];
  time_inout[1] = time_input[1];
}
