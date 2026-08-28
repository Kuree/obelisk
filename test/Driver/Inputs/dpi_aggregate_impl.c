#include "svdpi.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
  int32_t id;
  svLogicVecVal state[1];
  int8_t bytes[3];
  const char *label;
  void *token;
} payload_t;

extern int c_aggregate_work(payload_t *payload, int32_t values[3]);

int mutate_aggregate(payload_t *payload, int32_t values[3]) {
  printf("raw=%d/%x/%x/%x,%x,%x/%s/%" PRIxPTR " values=%d,%d,%d\n", payload->id,
         payload->state[0].aval, payload->state[0].bval,
         (unsigned char)payload->bytes[0], (unsigned char)payload->bytes[1],
         (unsigned char)payload->bytes[2], payload->label,
         (uintptr_t)payload->token, values[0], values[1], values[2]);
  payload->id = 42;
  payload->state[0].aval = 0x16;
  payload->state[0].bval = 0x0c;
  payload->bytes[0] = 0x43;
  payload->bytes[1] = 0x42;
  payload->bytes[2] = 0x41;
  payload->label = "from-c";
  payload->token = (void *)(uintptr_t)0x1234;
  values[0] = 103;
  values[1] = 102;
  values[2] = 101;
  int status = c_aggregate_work(payload, values);
  printf("export-raw=%d/%x,%x,%x/%s/%" PRIxPTR " values=%d,%d,%d\n",
         payload->id, (unsigned char)payload->bytes[0],
         (unsigned char)payload->bytes[1], (unsigned char)payload->bytes[2],
         payload->label, (uintptr_t)payload->token, values[0], values[1],
         values[2]);
  return status;
}
