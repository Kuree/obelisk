//===- System.cpp - Host command execution (IEEE 1800 20.18) ------------===//

#include "RuntimeInternal.h"

#include <cstdlib>
#include <string>

#if !defined(_WIN32)
#include <sys/wait.h>
#endif

extern "C" obelisk_rt_status obelisk_rt_v1_system(obelisk_rt_context *context,
                                                  obelisk_rt_string_v1 command,
                                                  int32_t *outStatus) {
  if (!context || !outStatus)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outStatus = -1;

  char scratch[8] = {};
  const char *bytes = nullptr;
  uint64_t size = 0;
  obelisk_rt_status status =
      obelisk_rt_v1_string_view(command, scratch, &bytes, &size);
  if (status != OBELISK_RT_OK)
    return status;

  return guarded(context, [&] {
    // Do not hold the context lock while the child command runs: $system is a
    // blocking host operation, and the shell may itself invoke an Obelisk
    // executable. std::string supplies the terminating NUL required by libc.
    int shellStatus = std::system(std::string(bytes, size).c_str());
#if defined(_WIN32)
    *outStatus = shellStatus;
#else
    if (shellStatus == -1)
      *outStatus = -1;
    else if (WIFEXITED(shellStatus))
      *outStatus = WEXITSTATUS(shellStatus);
    else if (WIFSIGNALED(shellStatus))
      *outStatus = 128 + WTERMSIG(shellStatus);
    else
      *outStatus = shellStatus;
#endif
    return OBELISK_RT_OK;
  });
}
