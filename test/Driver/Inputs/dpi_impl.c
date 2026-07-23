#include "svdpi.h"

#include <stdint.h>
#include <string.h>

int32_t dpi_add(int32_t value) {
  return value + 5;
}

int64_t dpi_scalars(int8_t byte_value, int16_t short_value,
                    int32_t int_value, int64_t long_value,
                    svBit bit_value, svLogic logic_value,
                    int32_t *output_value, int32_t *inout_value) {
  if (bit_value != sv_1 || logic_value != sv_x)
    return -1;
  *output_value = 40;
  *inout_value += 2;
  return byte_value + short_value + int_value + long_value;
}

int dpi_update(const svLogicVecVal *source, svBitVecVal *destination) {
  svScope scope = svGetScope();
  if (!scope || strcmp(svGetNameFromScope(scope), "dpi_driver") != 0)
    return 1;
  destination[0] = source[0].aval ^ source[0].bval;
  destination[1] = 1;
  return 0;
}

int32_t dpi_unused(int32_t value) {
  return value;
}

int dpi_logic_inout(svLogicVecVal *value) {
  memset(value, 0, 3 * sizeof(*value));
  value[0].aval = 12;
  value[0].bval = 6;
  return 0;
}

int dpi_fail(int32_t *value) {
  *value = 99;
  return 1;
}
