//===- Runtime.cpp - Obelisk native runtime context and buffers
//------------===//

#include "RuntimeInternal.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <utility>

namespace {

std::mutex hostErrorMutex;

} // namespace

obelisk_rt_context::obelisk_rt_context() {
  mcd[0].stream = stdout;
  mcd[0].writable = true;
  files.resize(3);
  files[0] = {stdin, 0, false};
  files[1] = {stdout, 0, true};
  files[2] = {stderr, 0, true};
  for (uint32_t bit = 30; bit >= 1; --bit)
    freeMCDs.push_back(bit);
}

void setLastErrorUnlocked(obelisk_rt_context *context, std::string message) {
  context->lastErrors[std::this_thread::get_id()] = std::move(message);
}

void setLastError(obelisk_rt_context *context, std::string message) {
  if (!context)
    return;
  try {
    std::lock_guard<std::mutex> lock(context->mutex);
    setLastErrorUnlocked(context, std::move(message));
  } catch (...) {
  }
}

obelisk_rt_status makeBuffer(std::string_view source,
                             obelisk_rt_buffer_v1 *output) {
  if (!output)
    return OBELISK_RT_INVALID_ARGUMENT;
  output->data = nullptr;
  output->size = 0;
  if (source.empty())
    return OBELISK_RT_OK;
  void *memory = std::malloc(source.size());
  if (!memory)
    return OBELISK_RT_OUT_OF_MEMORY;
  std::memcpy(memory, source.data(), source.size());
  output->data = static_cast<uint8_t *>(memory);
  output->size = source.size();
  return OBELISK_RT_OK;
}

bool validBytes(const void *data, uint64_t size) {
  return size == 0 || data != nullptr;
}

std::string hostErrorMessage(int error) {
  std::lock_guard<std::mutex> lock(hostErrorMutex);
  const char *message = std::strerror(error);
  return message ? message : "unknown host error";
}

extern "C" obelisk_rt_status
obelisk_rt_v1_context_create(obelisk_rt_context **outContext) {
  if (!outContext)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outContext = nullptr;
  try {
    *outContext = new obelisk_rt_context();
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_IO_ERROR;
  }
}

extern "C" void obelisk_rt_v1_context_destroy(obelisk_rt_context *context) {
  if (!context)
    return;
  try {
    std::lock_guard<std::mutex> lock(context->mutex);
    for (uint32_t bit = 1; bit < context->mcd.size(); ++bit) {
      if (context->mcd[bit].stream)
        std::fclose(context->mcd[bit].stream);
    }
    for (size_t index = 3; index < context->files.size(); ++index) {
      if (context->files[index].stream)
        std::fclose(context->files[index].stream);
    }
    std::fflush(stdout);
  } catch (...) {
  }
  delete context;
}

extern "C" const char *obelisk_rt_v1_status_string(obelisk_rt_status status) {
  switch (status) {
  case OBELISK_RT_OK:
    return "ok";
  case OBELISK_RT_EOF:
    return "end of file";
  case OBELISK_RT_INVALID_ARGUMENT:
    return "invalid argument";
  case OBELISK_RT_INVALID_HANDLE:
    return "invalid handle";
  case OBELISK_RT_IO_ERROR:
    return "I/O error";
  case OBELISK_RT_OUT_OF_MEMORY:
    return "out of memory";
  case OBELISK_RT_OUT_OF_RESOURCES:
    return "out of resources";
  case OBELISK_RT_FORMAT_ERROR:
    return "format error";
  case OBELISK_RT_ARGUMENT_MISMATCH:
    return "format argument mismatch";
  case OBELISK_RT_INVALID_BYTECODE:
    return "invalid bytecode";
  case OBELISK_RT_STEP_LIMIT:
    return "fragment step limit exceeded";
  case OBELISK_RT_LAYOUT_MISMATCH:
    return "process frame layout mismatch";
  case OBELISK_RT_INVALID_CONTINUATION:
    return "invalid process continuation";
  case OBELISK_RT_TIER_UNAVAILABLE:
    return "requested process tier unavailable";
  case OBELISK_RT_INVALID_LIFECYCLE:
    return "invalid process lifecycle transition";
  case OBELISK_RT_INVALID_FRAME:
    return "invalid process frame record";
  default:
    return "unknown runtime status";
  }
}

extern "C" void obelisk_rt_v1_buffer_release(obelisk_rt_buffer_v1 *buffer) {
  if (!buffer)
    return;
  std::free(buffer->data);
  buffer->data = nullptr;
  buffer->size = 0;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_last_error(obelisk_rt_context *context,
                         obelisk_rt_buffer_v1 *outMessage) {
  if (!context || !outMessage)
    return OBELISK_RT_INVALID_ARGUMENT;
  return guarded(context, [&] {
    std::lock_guard<std::mutex> lock(context->mutex);
    auto error = context->lastErrors.find(std::this_thread::get_id());
    return makeBuffer(error == context->lastErrors.end() ? std::string_view{}
                                                         : error->second,
                      outMessage);
  });
}
