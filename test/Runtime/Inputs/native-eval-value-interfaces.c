#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern uint8_t __obelisk_state_value[3], __obelisk_state_unknown[3];
extern bool lane_run(bool);
extern void lane_write(bool);
extern bool lane_read(uint16_t, uint16_t);

static uint16_t plane(const uint8_t *bytes) {
  uint16_t value;
  memcpy(&value, bytes, sizeof value);
  return value;
}

static void reset(void) {
  const uint16_t value = 0x1234, unknown = 0;
  memcpy(__obelisk_state_value, &value, sizeof value);
  memcpy(__obelisk_state_unknown, &unknown, sizeof unknown);
}

bool observer(uint16_t expected, uint16_t expected_x) {
  bool ok = plane(__obelisk_state_value) == expected &&
            plane(__obelisk_state_unknown) == expected_x;
  const uint16_t value = 0xfeed, unknown = 0x00c0;
  memcpy(__obelisk_state_value, &value, sizeof value);
  memcpy(__obelisk_state_unknown, &unknown, sizeof unknown);
  return ok;
}

int main(void) {
  for (unsigned take = 0; take != 2; ++take) {
    reset();
    if (!lane_run(take) || __obelisk_state_value[2] != 77 ||
        __obelisk_state_unknown[2] != 0)
      return 1;
    reset();
    // The original ABI is also the fine/cold path after local VPI fracture.
    lane_write(take);
    if (!lane_read(take ? 0xab3a : 0x1234, take ? 0x8000 : 0))
      return 2;
  }
  puts("packed partial, conditional, observer and canonical ABI passed");
  return 0;
}
