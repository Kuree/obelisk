//===- FileIO.cpp - Obelisk runtime libc file operations ------------------===//

#include "RuntimeInternal.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr uint32_t kFDTag = uint32_t{1} << 31;
constexpr uint32_t kFDIndexMask = kFDTag - 1;

std::optional<std::string> copyCStringBytes(const char *data, uint64_t size) {
  if (!validBytes(data, size) || size > std::numeric_limits<size_t>::max())
    return std::nullopt;
  std::string result(data ? data : "", static_cast<size_t>(size));
  if (result.find('\0') != std::string::npos)
    return std::nullopt;
  return result;
}

bool normalizeMode(std::string_view mode, std::string &normalized) {
  static constexpr std::array<std::string_view, 15> modes = {
      "r",  "w",   "a",   "r+",  "w+",  "a+",  "rb", "wb",
      "ab", "r+b", "w+b", "a+b", "rb+", "wb+", "ab+"};
  if (std::find(modes.begin(), modes.end(), mode) == modes.end())
    return false;
  normalized.assign(mode);
  return true;
}

FileEntry *getFileUnlocked(obelisk_rt_context *context, uint32_t descriptor) {
  if ((descriptor & kFDTag) == 0)
    return nullptr;
  uint32_t index = descriptor & kFDIndexMask;
  if (index == 0 || index >= context->files.size() ||
      !context->files[index].stream)
    return nullptr;
  return &context->files[index];
}

bool getOutputsUnlocked(obelisk_rt_context *context, uint32_t descriptor,
                        std::vector<FileEntry *> &outputs) {
  if (descriptor & kFDTag) {
    FileEntry *file = getFileUnlocked(context, descriptor);
    if (!file)
      return false;
    outputs.push_back(file);
    return true;
  }
  if (descriptor == 0)
    return false;
  for (uint32_t bit = 0; bit < 31; ++bit) {
    if ((descriptor & (uint32_t{1} << bit)) == 0)
      continue;
    if (!context->mcd[bit].stream)
      return false;
    outputs.push_back(&context->mcd[bit]);
  }
  return !outputs.empty();
}

void recordIOError(obelisk_rt_context *context, FileEntry &entry,
                   std::string_view operation) {
  entry.lastError = errno ? errno : EIO;
  setLastErrorUnlocked(context, std::string(operation) + ": " +
                                    hostErrorMessage(entry.lastError));
}

} // namespace

obelisk_rt_status writeUnlocked(obelisk_rt_context *context,
                                uint32_t descriptor, const void *data,
                                uint64_t size, uint64_t *outWritten) {
  if (!validBytes(data, size) || size > std::numeric_limits<size_t>::max())
    return OBELISK_RT_INVALID_ARGUMENT;
  std::vector<FileEntry *> outputs;
  if (!getOutputsUnlocked(context, descriptor, outputs)) {
    setLastErrorUnlocked(context, "invalid output descriptor");
    return OBELISK_RT_INVALID_HANDLE;
  }
  if (size == 0) {
    if (outWritten)
      *outWritten = 0;
    return OBELISK_RT_OK;
  }
  uint64_t minimum = size;
  for (FileEntry *entry : outputs) {
    size_t written =
        std::fwrite(data, 1, static_cast<size_t>(size), entry->stream);
    minimum = std::min<uint64_t>(minimum, written);
    if (written != size) {
      recordIOError(context, *entry, "file write failed");
      if (outWritten)
        *outWritten = minimum;
      return OBELISK_RT_IO_ERROR;
    }
  }
  if (outWritten)
    *outWritten = minimum;
  return OBELISK_RT_OK;
}

namespace {

obelisk_rt_status checkFileArguments(obelisk_rt_context *context,
                                     uint32_t descriptor, FileEntry *&entry,
                                     std::unique_lock<std::mutex> &lock) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  lock = std::unique_lock<std::mutex>(context->mutex);
  entry = getFileUnlocked(context, descriptor);
  if (!entry) {
    setLastErrorUnlocked(context, "invalid file descriptor");
    return OBELISK_RT_INVALID_HANDLE;
  }
  return OBELISK_RT_OK;
}

} // namespace

extern "C" obelisk_rt_status
obelisk_rt_v1_file_open_mcd(obelisk_rt_context *context, const char *path,
                            uint64_t pathSize, uint32_t *outDescriptor) {
  if (!context || !outDescriptor)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outDescriptor = 0;
  return guarded(context, [&] {
    std::optional<std::string> pathString = copyCStringBytes(path, pathSize);
    if (!pathString) {
      setLastError(context, "file path is invalid or contains a NUL byte");
      return OBELISK_RT_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock(context->mutex);
    if (context->freeMCDs.empty()) {
      setLastErrorUnlocked(context, "no multichannel descriptor bits remain");
      return OBELISK_RT_OUT_OF_RESOURCES;
    }
    errno = 0;
    FILE *stream = std::fopen(pathString->c_str(), "w");
    if (!stream) {
      int error = errno ? errno : EIO;
      setLastErrorUnlocked(context, "fopen failed: " + hostErrorMessage(error));
      return OBELISK_RT_IO_ERROR;
    }
    uint32_t bit = context->freeMCDs.back();
    context->freeMCDs.pop_back();
    context->mcd[bit] = {stream, 0, true};
    *outDescriptor = uint32_t{1} << bit;
    return OBELISK_RT_OK;
  });
}

extern "C" obelisk_rt_status
obelisk_rt_v1_file_open(obelisk_rt_context *context, const char *path,
                        uint64_t pathSize, const char *mode, uint64_t modeSize,
                        uint32_t *outDescriptor) {
  if (!context || !outDescriptor)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outDescriptor = 0;
  return guarded(context, [&] {
    std::optional<std::string> pathString = copyCStringBytes(path, pathSize);
    std::optional<std::string> modeString = copyCStringBytes(mode, modeSize);
    std::string normalized;
    if (!pathString) {
      setLastError(context, "file path is invalid or contains a NUL byte");
      return OBELISK_RT_INVALID_ARGUMENT;
    }
    if (!modeString || !normalizeMode(*modeString, normalized)) {
      setLastError(context, "invalid SystemVerilog file open mode");
      return OBELISK_RT_INVALID_ARGUMENT;
    }
    errno = 0;
    FILE *stream = std::fopen(pathString->c_str(), normalized.c_str());
    if (!stream) {
      int error = errno ? errno : EIO;
      setLastError(context, "fopen failed: " + hostErrorMessage(error));
      return OBELISK_RT_IO_ERROR;
    }
    bool writable =
        normalized.front() != 'r' || normalized.find('+') != std::string::npos;
    std::lock_guard<std::mutex> lock(context->mutex);
    uint32_t index;
    if (!context->freeFiles.empty()) {
      index = context->freeFiles.back();
      context->freeFiles.pop_back();
      context->files[index] = {stream, 0, writable};
    } else {
      if (context->files.size() > kFDIndexMask) {
        std::fclose(stream);
        setLastErrorUnlocked(context, "file descriptor table is full");
        return OBELISK_RT_OUT_OF_RESOURCES;
      }
      index = static_cast<uint32_t>(context->files.size());
      context->files.push_back({stream, 0, writable});
    }
    *outDescriptor = kFDTag | index;
    return OBELISK_RT_OK;
  });
}

extern "C" obelisk_rt_status
obelisk_rt_v1_file_close(obelisk_rt_context *context, uint32_t descriptor) {
  if (!context || descriptor == 0)
    return OBELISK_RT_INVALID_ARGUMENT;
  return guarded(context, [&] {
    std::lock_guard<std::mutex> lock(context->mutex);
    if (descriptor & kFDTag) {
      uint32_t index = descriptor & kFDIndexMask;
      FileEntry *entry = getFileUnlocked(context, descriptor);
      if (!entry) {
        setLastErrorUnlocked(context,
                             "invalid or already closed file descriptor");
        return OBELISK_RT_INVALID_HANDLE;
      }
      errno = 0;
      int result = std::fclose(entry->stream);
      entry->stream = nullptr;
      context->freeFiles.push_back(index);
      if (result != 0) {
        recordIOError(context, *entry, "fclose failed");
        return OBELISK_RT_IO_ERROR;
      }
      return OBELISK_RT_OK;
    }

    for (uint32_t bit = 1; bit < 31; ++bit)
      if ((descriptor & (uint32_t{1} << bit)) && !context->mcd[bit].stream) {
        setLastErrorUnlocked(context, "invalid or already closed MCD channel");
        return OBELISK_RT_INVALID_HANDLE;
      }
    obelisk_rt_status status = OBELISK_RT_OK;
    for (uint32_t bit = 1; bit < 31; ++bit) {
      if ((descriptor & (uint32_t{1} << bit)) == 0)
        continue;
      errno = 0;
      if (std::fclose(context->mcd[bit].stream) != 0) {
        recordIOError(context, context->mcd[bit], "fclose failed");
        status = OBELISK_RT_IO_ERROR;
      }
      context->mcd[bit].stream = nullptr;
      context->freeMCDs.push_back(bit);
    }
    return status;
  });
}

extern "C" obelisk_rt_status
obelisk_rt_v1_file_flush(obelisk_rt_context *context, uint32_t descriptor) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  return guarded(context, [&] {
    std::lock_guard<std::mutex> lock(context->mutex);
    std::vector<FileEntry *> outputs;
    if (descriptor == 0) {
      outputs.push_back(&context->mcd[0]);
      for (uint32_t bit = 1; bit < context->mcd.size(); ++bit)
        if (context->mcd[bit].stream && context->mcd[bit].writable)
          outputs.push_back(&context->mcd[bit]);
      for (size_t index = 1; index < context->files.size(); ++index)
        if (context->files[index].stream && context->files[index].writable)
          outputs.push_back(&context->files[index]);
    } else if (!getOutputsUnlocked(context, descriptor, outputs)) {
      setLastErrorUnlocked(context, "invalid descriptor passed to flush");
      return OBELISK_RT_INVALID_HANDLE;
    }
    for (FileEntry *entry : outputs) {
      errno = 0;
      if (std::fflush(entry->stream) != 0) {
        recordIOError(context, *entry, "fflush failed");
        return OBELISK_RT_IO_ERROR;
      }
    }
    return OBELISK_RT_OK;
  });
}

extern "C" obelisk_rt_status
obelisk_rt_v1_file_write(obelisk_rt_context *context, uint32_t descriptor,
                         const void *data, uint64_t size,
                         uint64_t *outWritten) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (outWritten)
    *outWritten = 0;
  return guarded(context, [&] {
    std::lock_guard<std::mutex> lock(context->mutex);
    return writeUnlocked(context, descriptor, data, size, outWritten);
  });
}

extern "C" obelisk_rt_status
obelisk_rt_v1_file_read(obelisk_rt_context *context, uint32_t descriptor,
                        void *data, uint64_t size, uint64_t *outRead) {
  if (!context || !outRead || !validBytes(data, size) ||
      size > std::numeric_limits<size_t>::max())
    return OBELISK_RT_INVALID_ARGUMENT;
  *outRead = 0;
  return guarded(context, [&] {
    FileEntry *entry;
    std::unique_lock<std::mutex> lock;
    obelisk_rt_status status =
        checkFileArguments(context, descriptor, entry, lock);
    if (status != OBELISK_RT_OK)
      return status;
    if (size == 0)
      return OBELISK_RT_OK;
    errno = 0;
    size_t count =
        std::fread(data, 1, static_cast<size_t>(size), entry->stream);
    *outRead = count;
    if (count != size && std::ferror(entry->stream)) {
      recordIOError(context, *entry, "file read failed");
      return OBELISK_RT_IO_ERROR;
    }
    return OBELISK_RT_OK;
  });
}

extern "C" obelisk_rt_status
obelisk_rt_v1_file_getc(obelisk_rt_context *context, uint32_t descriptor,
                        uint8_t *outByte) {
  if (!context || !outByte)
    return OBELISK_RT_INVALID_ARGUMENT;
  return guarded(context, [&] {
    FileEntry *entry;
    std::unique_lock<std::mutex> lock;
    obelisk_rt_status status =
        checkFileArguments(context, descriptor, entry, lock);
    if (status != OBELISK_RT_OK)
      return status;
    errno = 0;
    int character = std::fgetc(entry->stream);
    if (character != EOF) {
      *outByte = static_cast<uint8_t>(character);
      return OBELISK_RT_OK;
    }
    if (std::ferror(entry->stream)) {
      recordIOError(context, *entry, "fgetc failed");
      return OBELISK_RT_IO_ERROR;
    }
    return OBELISK_RT_EOF;
  });
}

extern "C" obelisk_rt_status
obelisk_rt_v1_file_ungetc(obelisk_rt_context *context, uint32_t descriptor,
                          uint8_t byte) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  return guarded(context, [&] {
    FileEntry *entry;
    std::unique_lock<std::mutex> lock;
    obelisk_rt_status status =
        checkFileArguments(context, descriptor, entry, lock);
    if (status != OBELISK_RT_OK)
      return status;
    errno = 0;
    if (std::ungetc(byte, entry->stream) == EOF) {
      recordIOError(context, *entry, "ungetc failed");
      return OBELISK_RT_IO_ERROR;
    }
    return OBELISK_RT_OK;
  });
}

extern "C" obelisk_rt_status
obelisk_rt_v1_file_getline(obelisk_rt_context *context, uint32_t descriptor,
                           obelisk_rt_buffer_v1 *outLine) {
  if (!context || !outLine)
    return OBELISK_RT_INVALID_ARGUMENT;
  outLine->data = nullptr;
  outLine->size = 0;
  return guarded(context, [&] {
    FileEntry *entry;
    std::unique_lock<std::mutex> lock;
    obelisk_rt_status status =
        checkFileArguments(context, descriptor, entry, lock);
    if (status != OBELISK_RT_OK)
      return status;
    std::string line;
    errno = 0;
    while (true) {
      int character = std::fgetc(entry->stream);
      if (character == EOF)
        break;
      line.push_back(static_cast<char>(character));
      if (character == '\n')
        break;
    }
    if (line.empty() && std::feof(entry->stream))
      return OBELISK_RT_EOF;
    if (std::ferror(entry->stream)) {
      recordIOError(context, *entry, "line read failed");
      return OBELISK_RT_IO_ERROR;
    }
    return makeBuffer(line, outLine);
  });
}

extern "C" obelisk_rt_status obelisk_rt_v1_file_eof(obelisk_rt_context *context,
                                                    uint32_t descriptor,
                                                    uint32_t *outIsEOF) {
  if (!context || !outIsEOF)
    return OBELISK_RT_INVALID_ARGUMENT;
  return guarded(context, [&] {
    FileEntry *entry;
    std::unique_lock<std::mutex> lock;
    obelisk_rt_status status =
        checkFileArguments(context, descriptor, entry, lock);
    if (status != OBELISK_RT_OK)
      return status;
    *outIsEOF = std::feof(entry->stream) ? 1u : 0u;
    return OBELISK_RT_OK;
  });
}

extern "C" obelisk_rt_status
obelisk_rt_v1_file_error(obelisk_rt_context *context, uint32_t descriptor,
                         int32_t *outErrorCode,
                         obelisk_rt_buffer_v1 *outMessage) {
  if (!context || !outErrorCode || !outMessage)
    return OBELISK_RT_INVALID_ARGUMENT;
  outMessage->data = nullptr;
  outMessage->size = 0;
  return guarded(context, [&] {
    FileEntry *entry;
    std::unique_lock<std::mutex> lock;
    obelisk_rt_status status =
        checkFileArguments(context, descriptor, entry, lock);
    if (status != OBELISK_RT_OK)
      return status;
    int error = std::ferror(entry->stream) ? entry->lastError : 0;
    *outErrorCode = error;
    return makeBuffer(error ? hostErrorMessage(error) : "", outMessage);
  });
}

extern "C" obelisk_rt_status
obelisk_rt_v1_file_seek(obelisk_rt_context *context, uint32_t descriptor,
                        int64_t offset, obelisk_rt_seek_origin origin) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  int hostOrigin = origin == OBELISK_RT_SEEK_SET   ? SEEK_SET
                   : origin == OBELISK_RT_SEEK_CUR ? SEEK_CUR
                   : origin == OBELISK_RT_SEEK_END ? SEEK_END
                                                   : -1;
  if (hostOrigin == -1 || offset < std::numeric_limits<long>::min() ||
      offset > std::numeric_limits<long>::max())
    return OBELISK_RT_INVALID_ARGUMENT;
  return guarded(context, [&] {
    FileEntry *entry;
    std::unique_lock<std::mutex> lock;
    obelisk_rt_status status =
        checkFileArguments(context, descriptor, entry, lock);
    if (status != OBELISK_RT_OK)
      return status;
    errno = 0;
    if (std::fseek(entry->stream, static_cast<long>(offset), hostOrigin) != 0) {
      recordIOError(context, *entry, "fseek failed");
      return OBELISK_RT_IO_ERROR;
    }
    return OBELISK_RT_OK;
  });
}

extern "C" obelisk_rt_status
obelisk_rt_v1_file_tell(obelisk_rt_context *context, uint32_t descriptor,
                        int64_t *outOffset) {
  if (!context || !outOffset)
    return OBELISK_RT_INVALID_ARGUMENT;
  return guarded(context, [&] {
    FileEntry *entry;
    std::unique_lock<std::mutex> lock;
    obelisk_rt_status status =
        checkFileArguments(context, descriptor, entry, lock);
    if (status != OBELISK_RT_OK)
      return status;
    errno = 0;
    long offset = std::ftell(entry->stream);
    if (offset < 0) {
      recordIOError(context, *entry, "ftell failed");
      return OBELISK_RT_IO_ERROR;
    }
    *outOffset = offset;
    return OBELISK_RT_OK;
  });
}

extern "C" obelisk_rt_status
obelisk_rt_v1_file_rewind(obelisk_rt_context *context, uint32_t descriptor) {
  return obelisk_rt_v1_file_seek(context, descriptor, 0, OBELISK_RT_SEEK_SET);
}
