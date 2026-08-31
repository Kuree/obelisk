#include <stdint.h>
#include <stdio.h>

typedef struct obelisk_rt_context obelisk_rt_context;
extern obelisk_rt_context *obelisk_rt_v1_dpi_current_context(void);
extern int32_t obelisk_rt_v1_gc_set_threshold(obelisk_rt_context *context,
                                              uint64_t bytes);

typedef struct {
  const char *first;
  const char *second;
} string_pair_t;

extern int32_t c_check_aggregate_function(const string_pair_t *left,
                                          const string_pair_t *right,
                                          const char *scalar);
extern int c_check_aggregate_task(const string_pair_t *left,
                                  const string_pair_t *right,
                                  const char *scalar, int32_t *result);

int drive_aggregate_gc(void) {
  obelisk_rt_context *context = obelisk_rt_v1_dpi_current_context();
  if (!context || obelisk_rt_v1_gc_set_threshold(context, 1) != 0)
    return 2;
  string_pair_t left = {"left-first-heap", "left-second-heap"};
  string_pair_t right = {"right-first-heap", "right-second-heap"};
  int32_t functionResult =
      c_check_aggregate_function(&left, &right, "scalar-function-heap");
  int32_t taskResult = 0;
  int status =
      c_check_aggregate_task(&left, &right, "scalar-task-heap", &taskResult);
  printf("gc-function=%d gc-task=%d status=%d\n", functionResult, taskResult,
         status);
  return status;
}
