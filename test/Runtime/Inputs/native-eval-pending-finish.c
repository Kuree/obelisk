#include <stdint.h>
#include <stdio.h>
extern unsigned char acc0[136];
extern unsigned char acc64[136];
extern unsigned char acc4096[136];
static unsigned char *const captures[] = {
acc0, acc64, acc4096};
uint32_t obelisk_rt_v1_static_nba_specialization_guard_write(
        void *context, uint32_t root) { return 1; }
uint32_t obelisk_rt_v1_scheduler_execute_aot_actor(void *context, uint32_t slot) {
  uint64_t sum = 0;
  for (unsigned i = 0; i != sizeof(captures) / sizeof(*captures); ++i) {
    unsigned char *p = captures[i];
    if (*(uint32_t *)(p + 96) != 1 || *(uint32_t *)(p + 100) != 2 ||
        *(uint64_t *)(p + 64) != UINT32_MAX) return 1;
    sum += *(uint64_t *)p + 128 * *(uint64_t *)(p + 32);
  }
  return sum == 134 ? 0 : 1;
}
uint32_t obelisk_rt_v1_scheduler_prepare_eval_nba_local_write(
        void *context, uint32_t local, uint32_t *region) {
  *region = UINT32_MAX;
  return local == 2 ? 0 : 1;
}
