//===- RuntimeCAPISmoke.c - Compile and exercise the runtime C ABI --------===//

#include "obelisk/Runtime/Runtime.h"

#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(obelisk_rt_status) == 4, "status ABI changed");
_Static_assert(sizeof(obelisk_rt_arg_kind) == 4, "argument kind ABI changed");
_Static_assert(sizeof(obelisk_rt_arg_flags) == 4, "argument flags ABI changed");
_Static_assert(offsetof(obelisk_rt_arg_v1, kind) == 0,
               "argument kind offset changed");
_Static_assert(offsetof(obelisk_rt_arg_v1, flags) == 4,
               "argument flags offset changed");
_Static_assert(offsetof(obelisk_rt_arg_v1, size) == 8,
               "argument size offset changed");
_Static_assert(offsetof(obelisk_rt_arg_v1, data) == 16,
               "argument data offset changed");
_Static_assert(offsetof(obelisk_rt_arg_v1, unknown) == 16 + sizeof(void *),
               "argument unknown offset changed");
_Static_assert(offsetof(obelisk_rt_buffer_v1, data) == 0,
               "buffer data offset changed");
_Static_assert(offsetof(obelisk_rt_format_env_v1, scope) == 0,
               "environment scope offset changed");

int obelisk_runtime_c_api_smoke(void) {
  obelisk_rt_context *context = NULL;
  obelisk_rt_buffer_v1 output = {NULL, 0};
  obelisk_rt_arg_v1 empty_string = {OBELISK_RT_ARG_STRING, 0, 0, NULL, NULL};

  if (OBELISK_RT_ABI_VERSION != 1u)
    return 1;
  if (obelisk_rt_v1_context_create(&context) != OBELISK_RT_OK || !context)
    return 2;
  if (obelisk_rt_v1_format(context, "%s", 2, &empty_string, 1, NULL, &output) !=
      OBELISK_RT_OK) {
    obelisk_rt_v1_context_destroy(context);
    return 3;
  }
  if (output.data != NULL || output.size != 0) {
    obelisk_rt_v1_buffer_release(&output);
    obelisk_rt_v1_context_destroy(context);
    return 4;
  }
  if (strcmp(obelisk_rt_v1_status_string(OBELISK_RT_OK), "ok") != 0) {
    obelisk_rt_v1_context_destroy(context);
    return 5;
  }

  obelisk_rt_v1_buffer_release(&output);
  obelisk_rt_v1_context_destroy(context);
  return 0;
}
