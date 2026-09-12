//===- FileIO.cpp - Obelisk runtime libc file operations ------------------===//

#include "RuntimeInternal.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
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
  if (index >= context->files.size() || !context->files[index].stream)
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

// A descriptor opened without read access can only ever hand back a byte that
// $ungetc pushed onto it. Reads never reach the host stream there: they would
// fail with EBADF, and turning that into a runtime I/O error would abort the
// simulation instead of letting $fgetc/$fgets/$fscanf report failure the way
// IEEE 1800-2017 21.3 expects.
int takePushback(FileEntry &entry) {
  int byte = entry.pushback;
  entry.pushback = -1;
  return byte;
}

bool scanSpace(int character) {
  return character == ' ' || character == '\t' || character == '\n' ||
         character == '\r' || character == '\f' || character == '\v';
}

bool scanDigit(int character, uint32_t radix) {
  if (character == '_')
    return true;
  if (radix != 10 && (character == 'x' || character == 'X' ||
                      character == 'z' || character == 'Z' || character == '?'))
    return true;
  uint32_t value = static_cast<unsigned char>(character);
  uint32_t digit = value >= '0' && value <= '9'   ? value - '0'
                   : value >= 'a' && value <= 'f' ? value - 'a' + 10
                   : value >= 'A' && value <= 'F' ? value - 'A' + 10
                                                  : UINT32_MAX;
  return digit < radix;
}

enum class ScanResult { Match, Mismatch, EndOfFile, Error };

// Keep ordinary formatted file input on a direct libc path while allowing the
// one byte held by $ungetc for a non-readable descriptor to use the same scan
// state machine. The template removes the synthetic checks from every hot
// readable-stream get/putback operation.
template <bool Synthetic> class ScanReader {
public:
  explicit ScanReader(FileEntry &entry) : entry(entry) {}

  int get() {
    if constexpr (Synthetic)
      return takePushback(entry);
    return std::fgetc(entry.stream);
  }

  ScanResult putBack(int character) {
    if (character == EOF)
      return ScanResult::Match;
    if constexpr (Synthetic) {
      if (entry.pushback >= 0)
        return ScanResult::Error;
      entry.pushback = static_cast<unsigned char>(character);
      return ScanResult::Match;
    }
    return std::ungetc(character, entry.stream) != EOF ? ScanResult::Match
                                                       : ScanResult::Error;
  }

  bool error() const {
    if constexpr (Synthetic)
      return false;
    return std::ferror(entry.stream) != 0;
  }

private:
  FileEntry &entry;
};

template <bool Synthetic>
ScanResult scanFileField(FileEntry &entry, const char *prefix,
                         uint64_t prefixSize, uint32_t specifier,
                         uint64_t width, std::string &field) {
  ScanReader<Synthetic> reader(entry);
  for (uint64_t position = 0; position != prefixSize; ++position) {
    unsigned char expected = static_cast<unsigned char>(prefix[position]);
    if (scanSpace(expected)) {
      int character;
      do {
        character = reader.get();
      } while (character != EOF && scanSpace(character));
      if (reader.putBack(character) == ScanResult::Error)
        return ScanResult::Error;
      continue;
    }
    int character = reader.get();
    if (character == EOF)
      return reader.error() ? ScanResult::Error : ScanResult::EndOfFile;
    if (character != expected) {
      if (reader.putBack(character) == ScanResult::Error)
        return ScanResult::Error;
      return ScanResult::Mismatch;
    }
  }

  char letter =
      static_cast<char>(std::tolower(static_cast<unsigned char>(specifier)));
  // IEEE 1800-2017 21.3.4.3: %m consumes no input field. The lexical scope is
  // materialized by lowering, while the runtime still owns prefix matching
  // and the descriptor position.
  if (letter == 'm')
    return ScanResult::Match;
  uint64_t remaining = width == 0 ? UINT64_MAX : width;
  auto readFieldCharacter = [&]() {
    if (remaining == 0)
      return EOF;
    int character = reader.get();
    if (character != EOF)
      --remaining;
    return character;
  };
  if (letter == 'c') {
    int character = readFieldCharacter();
    if (character == EOF)
      return reader.error() ? ScanResult::Error : ScanResult::EndOfFile;
    uint64_t count = width == 0 ? 1 : width;
    do {
      field.push_back(static_cast<char>(character));
      if (field.size() == count)
        break;
      character = readFieldCharacter();
    } while (character != EOF);
    return ScanResult::Match;
  }
  int character = reader.get();
  // Whitespace skipped by a conversion does not count toward its field
  // width, so read that prefix without the bounded helper.
  while (character != EOF && scanSpace(character))
    character = reader.get();
  if (character == EOF)
    return reader.error() ? ScanResult::Error : ScanResult::EndOfFile;

  remaining = width == 0 ? UINT64_MAX : width;
  if (remaining != UINT64_MAX)
    --remaining;

  if (letter == 's') {
    do {
      field.push_back(static_cast<char>(character));
      character = readFieldCharacter();
    } while (character != EOF && !scanSpace(character));
    return reader.putBack(character) == ScanResult::Error ? ScanResult::Error
                                                          : ScanResult::Match;
  }

  if (letter == 'v') {
    field.push_back(static_cast<char>(character));
    for (unsigned index = 1; index != 3; ++index) {
      character = readFieldCharacter();
      if (character == EOF)
        break;
      field.push_back(static_cast<char>(character));
    }
    char logic = 0;
    if (obelisk_rt_parse_strength_field(field.data(), field.size(), logic)) {
      field.assign(1, logic);
      return ScanResult::Match;
    }
    for (auto iterator = field.rbegin(); iterator != field.rend(); ++iterator)
      if (reader.putBack(static_cast<unsigned char>(*iterator)) ==
          ScanResult::Error)
        return ScanResult::Error;
    field.clear();
    return ScanResult::Mismatch;
  }

  bool real = letter == 'e' || letter == 'f' || letter == 'g' || letter == 't';
  if ((real || letter == 'd') && (character == '+' || character == '-')) {
    field.push_back(static_cast<char>(character));
    character = readFieldCharacter();
  }
  uint32_t radix = letter == 'b'   ? 2
                   : letter == 'o' ? 8
                   : letter == 'd' ? 10
                                   : 16;
  bool haveDigit = false;
  bool havePoint = false;
  bool wholeUnknown = false;
  // Decimal accepts one whole-value X/Z/? spelling, rather than unknown
  // digits mixed into an otherwise decimal number (Table 21-8).
  if (letter == 'd' && field.empty() &&
      (character == 'x' || character == 'X' || character == 'z' ||
       character == 'Z' || character == '?')) {
    field.push_back(static_cast<char>(character));
    haveDigit = true;
    wholeUnknown = true;
    character = readFieldCharacter();
  }
  while (!wholeUnknown && character != EOF) {
    if (scanDigit(character, real ? 10 : radix)) {
      field.push_back(static_cast<char>(character));
      haveDigit |= character != '_';
      character = readFieldCharacter();
      continue;
    }
    if (real && character == '.' && !havePoint) {
      havePoint = true;
      field.push_back('.');
      character = readFieldCharacter();
      continue;
    }
    break;
  }
  if (real && haveDigit && (character == 'e' || character == 'E')) {
    field.push_back(static_cast<char>(character));
    character = readFieldCharacter();
    if (character == '+' || character == '-') {
      field.push_back(static_cast<char>(character));
      character = readFieldCharacter();
    }
    while (character != EOF && scanDigit(character, 10)) {
      field.push_back(static_cast<char>(character));
      character = readFieldCharacter();
    }
  }
  if (reader.putBack(character) == ScanResult::Error)
    return ScanResult::Error;
  if (!haveDigit) {
    for (auto iterator = field.rbegin(); iterator != field.rend(); ++iterator)
      if (reader.putBack(static_cast<unsigned char>(*iterator)) ==
          ScanResult::Error)
        return ScanResult::Error;
    field.clear();
    return ScanResult::Mismatch;
  }
  return ScanResult::Match;
}

template <bool Synthetic>
ScanResult scanFileRaw(FileEntry &entry, const char *prefix,
                       uint64_t prefixSize, uint64_t rawSize, uint64_t maxWidth,
                       std::string &raw) {
  ScanReader<Synthetic> reader(entry);
  for (uint64_t position = 0; position != prefixSize; ++position) {
    unsigned char expected = static_cast<unsigned char>(prefix[position]);
    if (scanSpace(expected)) {
      int character;
      do {
        character = reader.get();
      } while (character != EOF && scanSpace(character));
      if (reader.putBack(character) == ScanResult::Error)
        return ScanResult::Error;
      continue;
    }
    int character = reader.get();
    if (character == EOF)
      return reader.error() ? ScanResult::Error : ScanResult::EndOfFile;
    if (character != expected) {
      if (reader.putBack(character) == ScanResult::Error)
        return ScanResult::Error;
      return ScanResult::Mismatch;
    }
  }
  if (maxWidth != 0 && rawSize > maxWidth)
    return ScanResult::Mismatch;
  for (uint64_t index = 0; index != rawSize; ++index) {
    int character = reader.get();
    if (character == EOF)
      return reader.error() ? ScanResult::Error : ScanResult::EndOfFile;
    raw.push_back(static_cast<char>(character));
  }
  return ScanResult::Match;
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

obelisk_rt_status
checkFileArguments(obelisk_rt_context *context, uint32_t descriptor,
                   FileEntry *&entry,
                   std::unique_lock<std::recursive_mutex> &lock) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  lock = std::unique_lock<std::recursive_mutex>(context->mutex);
  entry = getFileUnlocked(context, descriptor);
  if (!entry) {
    setLastErrorUnlocked(context, "invalid file descriptor");
    return OBELISK_RT_INVALID_HANDLE;
  }
  return OBELISK_RT_OK;
}

} // namespace

bool obelisk_rt_file_name_unlocked(obelisk_rt_context *context,
                                   uint32_t descriptor,
                                   std::string_view &name) noexcept {
  name = {};
  if (!context)
    return false;
  FileEntry *entry = nullptr;
  if (descriptor & kFDTag) {
    entry = getFileUnlocked(context, descriptor);
  } else {
    if (descriptor == 0 || (descriptor & (descriptor - 1)) != 0)
      return false;
    uint32_t bit = 0;
    while ((descriptor & (uint32_t{1} << bit)) == 0)
      ++bit;
    if (bit >= context->mcd.size() || !context->mcd[bit].stream)
      return false;
    entry = &context->mcd[bit];
  }
  if (!entry || entry->name.empty())
    return false;
  name = entry->name;
  return true;
}

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
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
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
    context->mcd[bit] = {stream, 0, true, false, -1,
                         std::move(*pathString)};
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
    std::unique_ptr<FILE, decltype(&std::fclose)> stream(
        std::fopen(pathString->c_str(), normalized.c_str()), &std::fclose);
    if (!stream) {
      int error = errno ? errno : EIO;
      setLastError(context, "fopen failed: " + hostErrorMessage(error));
      return OBELISK_RT_IO_ERROR;
    }
    // Access is tracked per descriptor so that reading a write-only file is
    // end-of-file, not a host I/O error that would abort the simulation.
    bool updating = normalized.find('+') != std::string::npos;
    bool writable = normalized.front() != 'r' || updating;
    bool readable = normalized.front() == 'r' || updating;
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    uint32_t index;
    if (!context->freeFiles.empty()) {
      index = context->freeFiles.back();
      context->freeFiles.pop_back();
      context->files[index] = {stream.get(), 0, writable, readable, -1,
                               std::move(*pathString)};
    } else {
      if (context->files.size() > kFDIndexMask) {
        setLastErrorUnlocked(context, "file descriptor table is full");
        return OBELISK_RT_OUT_OF_RESOURCES;
      }
      index = static_cast<uint32_t>(context->files.size());
      context->files.push_back({stream.get(), 0, writable, readable, -1,
                                std::move(*pathString)});
    }
    stream.release();
    *outDescriptor = kFDTag | index;
    return OBELISK_RT_OK;
  });
}

extern "C" obelisk_rt_status
obelisk_rt_v1_file_open_string_mcd(obelisk_rt_context *context,
                                   obelisk_rt_string_v1 path,
                                   uint32_t *outDescriptor) {
  char scratch[8] = {};
  const char *bytes = nullptr;
  uint64_t size = 0;
  obelisk_rt_status status =
      obelisk_rt_v1_string_view(path, scratch, &bytes, &size);
  return status == OBELISK_RT_OK
             ? obelisk_rt_v1_file_open_mcd(context, bytes, size, outDescriptor)
             : status;
}

extern "C" obelisk_rt_status obelisk_rt_v1_file_open_string(
    obelisk_rt_context *context, obelisk_rt_string_v1 path,
    obelisk_rt_string_v1 mode, uint32_t *outDescriptor) {
  char pathScratch[8] = {};
  char modeScratch[8] = {};
  const char *pathBytes = nullptr;
  const char *modeBytes = nullptr;
  uint64_t pathSize = 0;
  uint64_t modeSize = 0;
  obelisk_rt_status status =
      obelisk_rt_v1_string_view(path, pathScratch, &pathBytes, &pathSize);
  if (status != OBELISK_RT_OK)
    return status;
  status = obelisk_rt_v1_string_view(mode, modeScratch, &modeBytes, &modeSize);
  return status == OBELISK_RT_OK
             ? obelisk_rt_v1_file_open(context, pathBytes, pathSize, modeBytes,
                                       modeSize, outDescriptor)
             : status;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_file_close(obelisk_rt_context *context, uint32_t descriptor) {
  if (!context || descriptor == 0)
    return OBELISK_RT_INVALID_ARGUMENT;
  return guarded(context, [&] {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    if (descriptor & kFDTag) {
      uint32_t index = descriptor & kFDIndexMask;
      if (index < 3) {
        setLastErrorUnlocked(context,
                             "predefined file descriptors cannot be closed");
        return OBELISK_RT_INVALID_HANDLE;
      }
      FileEntry *entry = getFileUnlocked(context, descriptor);
      if (!entry) {
        setLastErrorUnlocked(context,
                             "invalid or already closed file descriptor");
        return OBELISK_RT_INVALID_HANDLE;
      }
      errno = 0;
      int result = std::fclose(entry->stream);
      entry->stream = nullptr;
      entry->name.clear();
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
      context->mcd[bit].name.clear();
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
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    std::vector<FileEntry *> outputs;
    if (descriptor == 0) {
      outputs.push_back(&context->mcd[0]);
      for (uint32_t bit = 1; bit < context->mcd.size(); ++bit)
        if (context->mcd[bit].stream && context->mcd[bit].writable)
          outputs.push_back(&context->mcd[bit]);
      // stdout is already represented by MCD bit zero. Include stderr and all
      // dynamically opened writable descriptors without flushing stdout twice.
      for (size_t index = 2; index < context->files.size(); ++index)
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
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
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
    std::unique_lock<std::recursive_mutex> lock;
    obelisk_rt_status status =
        checkFileArguments(context, descriptor, entry, lock);
    if (status != OBELISK_RT_OK)
      return status;
    if (size == 0)
      return OBELISK_RT_OK;
    if (!entry->readable) {
      int byte = takePushback(*entry);
      if (byte >= 0) {
        *static_cast<unsigned char *>(data) = static_cast<unsigned char>(byte);
        *outRead = 1;
      }
      return OBELISK_RT_OK;
    }
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

extern "C" obelisk_rt_status obelisk_rt_v1_file_readmem_token(
    obelisk_rt_context *context, uint32_t descriptor, uint32_t radix,
    uint64_t bitWidth, void *value, uint64_t valueSize, void *unknown,
    uint64_t unknownSize, uint32_t *outKind, uint64_t *outAddress) {
  uint64_t requiredBytes = bitWidth / 8 + (bitWidth % 8 != 0);
  if (!context || (radix != 2 && radix != 16) || bitWidth == 0 ||
      valueSize != requiredBytes || unknownSize != requiredBytes ||
      !validBytes(value, valueSize) || !validBytes(unknown, unknownSize) ||
      !outKind || !outAddress)
    return OBELISK_RT_INVALID_ARGUMENT;
  std::memset(value, 0, static_cast<size_t>(valueSize));
  std::memset(unknown, 0, static_cast<size_t>(unknownSize));
  *outKind = OBELISK_RT_READMEM_EOF;
  *outAddress = 0;
  return guarded(context, [&] {
    FileEntry *entry;
    std::unique_lock<std::recursive_mutex> lock;
    obelisk_rt_status status =
        checkFileArguments(context, descriptor, entry, lock);
    if (status != OBELISK_RT_OK)
      return status;
    if (!entry->readable) {
      setLastErrorUnlocked(context, "$readmem input is not readable");
      return OBELISK_RT_IO_ERROR;
    }
    auto next = [&]() { return std::fgetc(entry->stream); };
    auto malformed = [&](std::string message) {
      setLastErrorUnlocked(context, std::move(message));
      return OBELISK_RT_FORMAT_ERROR;
    };
    auto finishIO = [&]() {
      if (!std::ferror(entry->stream))
        return OBELISK_RT_OK;
      recordIOError(context, *entry, "$readmem read failed");
      return OBELISK_RT_IO_ERROR;
    };
    int character;
    for (;;) {
      character = next();
      while (character != EOF && scanSpace(character))
        character = next();
      if (character == EOF)
        return finishIO();
      if (character != '/')
        break;
      int second = next();
      if (second == '/') {
        do {
          character = next();
        } while (character != EOF && character != '\n' && character != '\r');
        if (character == EOF && std::ferror(entry->stream))
          return finishIO();
        continue;
      }
      if (second == '*') {
        int previous = 0;
        for (;;) {
          character = next();
          if (character == EOF)
            return std::ferror(entry->stream)
                       ? finishIO()
                       : malformed("unterminated $readmem block comment");
          if (previous == '*' && character == '/')
            break;
          previous = character;
        }
        continue;
      }
      if (second != EOF && std::ungetc(second, entry->stream) == EOF) {
        recordIOError(context, *entry, "$readmem pushback failed");
        return OBELISK_RT_IO_ERROR;
      }
      return malformed("invalid '/' in $readmem input");
    }
    if (character == '@') {
      uint64_t address = 0;
      bool haveDigit = false;
      for (;;) {
        character = next();
        unsigned digit =
            character >= '0' && character <= '9'   ? character - '0'
            : character >= 'a' && character <= 'f' ? character - 'a' + 10
            : character >= 'A' && character <= 'F' ? character - 'A' + 10
                                                   : 16;
        if (digit >= 16)
          break;
        haveDigit = true;
        if (address > (UINT64_MAX - digit) / 16)
          return malformed("$readmem address exceeds 64 bits");
        address = address * 16 + digit;
      }
      if (!haveDigit)
        return malformed("$readmem address marker has no hexadecimal digits");
      if (character != EOF && std::ungetc(character, entry->stream) == EOF) {
        recordIOError(context, *entry, "$readmem pushback failed");
        return OBELISK_RT_IO_ERROR;
      }
      if (character == EOF && std::ferror(entry->stream))
        return finishIO();
      *outKind = OBELISK_RT_READMEM_ADDRESS;
      *outAddress = address;
      return OBELISK_RT_OK;
    }
    std::string digits;
    for (;;) {
      bool valid = character == '_' || character == 'x' || character == 'X' ||
                   character == 'z' || character == 'Z' ||
                   (character >= '0' && character <= '1') ||
                   (radix == 16 && ((character >= '2' && character <= '9') ||
                                    (character >= 'a' && character <= 'f') ||
                                    (character >= 'A' && character <= 'F')));
      if (!valid)
        break;
      digits.push_back(static_cast<char>(character));
      character = next();
    }
    if (digits.empty())
      return malformed("invalid character in $readmem input");
    if (character != EOF && std::ungetc(character, entry->stream) == EOF) {
      recordIOError(context, *entry, "$readmem pushback failed");
      return OBELISK_RT_IO_ERROR;
    }
    if (character == EOF && std::ferror(entry->stream))
      return finishIO();
    auto *valueBytes = static_cast<uint8_t *>(value);
    auto *unknownBytes = static_cast<uint8_t *>(unknown);
    uint64_t outputBit = 0;
    unsigned bitsPerDigit = radix == 2 ? 1 : 4;
    for (auto position = digits.rbegin(); position != digits.rend();
         ++position) {
      char digitCharacter = *position;
      if (digitCharacter == '_')
        continue;
      unsigned digit = digitCharacter >= '0' && digitCharacter <= '9'
                           ? digitCharacter - '0'
                       : digitCharacter >= 'a' && digitCharacter <= 'f'
                           ? digitCharacter - 'a' + 10
                       : digitCharacter >= 'A' && digitCharacter <= 'F'
                           ? digitCharacter - 'A' + 10
                           : 0;
      bool isX = digitCharacter == 'x' || digitCharacter == 'X';
      bool isZ = digitCharacter == 'z' || digitCharacter == 'Z';
      for (unsigned bit = 0; bit != bitsPerDigit && outputBit < bitWidth;
           ++bit, ++outputBit) {
        uint8_t mask = uint8_t{1} << (outputBit % 8);
        if ((!isX && !isZ && ((digit >> bit) & 1)) || isZ)
          valueBytes[outputBit / 8] |= mask;
        if (isX || isZ)
          unknownBytes[outputBit / 8] |= mask;
      }
      if (outputBit == bitWidth)
        break;
    }
    *outKind = OBELISK_RT_READMEM_DATA;
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
    std::unique_lock<std::recursive_mutex> lock;
    obelisk_rt_status status =
        checkFileArguments(context, descriptor, entry, lock);
    if (status != OBELISK_RT_OK)
      return status;
    if (!entry->readable) {
      int byte = takePushback(*entry);
      if (byte < 0)
        return OBELISK_RT_EOF;
      *outByte = static_cast<uint8_t>(byte);
      return OBELISK_RT_OK;
    }
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
    std::unique_lock<std::recursive_mutex> lock;
    obelisk_rt_status status =
        checkFileArguments(context, descriptor, entry, lock);
    if (status != OBELISK_RT_OK)
      return status;
    if (!entry->readable) {
      if (entry->pushback >= 0)
        return OBELISK_RT_EOF;
      entry->pushback = byte;
      return OBELISK_RT_OK;
    }
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
                           uint64_t maxBytes, obelisk_rt_buffer_v1 *outLine) {
  if (!context || !outLine)
    return OBELISK_RT_INVALID_ARGUMENT;
  outLine->data = nullptr;
  outLine->size = 0;
  return guarded(context, [&] {
    FileEntry *entry;
    std::unique_lock<std::recursive_mutex> lock;
    obelisk_rt_status status =
        checkFileArguments(context, descriptor, entry, lock);
    if (status != OBELISK_RT_OK)
      return status;
    if (!entry->readable) {
      if (entry->pushback < 0)
        return OBELISK_RT_EOF;
      if (maxBytes == 0)
        return makeBuffer(std::string(), outLine);
      return makeBuffer(std::string(1, static_cast<char>(takePushback(*entry))),
                        outLine);
    }
    std::string line;
    errno = 0;
    while (line.size() < maxBytes) {
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

extern "C" obelisk_rt_status obelisk_rt_v1_file_getline_string(
    obelisk_rt_context *context, obelisk_rt_gc_lane_v1 *lane,
    uint32_t descriptor, obelisk_rt_string_v1 *outString, uint32_t *outCount) {
  if (!outString || !outCount)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outString = 0;
  *outCount = 0;
  obelisk_rt_buffer_v1 line{};
  obelisk_rt_status status =
      obelisk_rt_v1_file_getline(context, descriptor, UINT64_MAX, &line);
  if (status == OBELISK_RT_EOF)
    return OBELISK_RT_OK;
  if (status != OBELISK_RT_OK)
    return status;
  if (line.size > UINT32_MAX) {
    obelisk_rt_v1_buffer_release(&line);
    return OBELISK_RT_OUT_OF_RESOURCES;
  }
  status = obelisk_rt_v1_string_create(
      lane, reinterpret_cast<const char *>(line.data), line.size, outString);
  if (status == OBELISK_RT_OK)
    *outCount = static_cast<uint32_t>(line.size);
  obelisk_rt_v1_buffer_release(&line);
  return status;
}

extern "C" obelisk_rt_status obelisk_rt_v1_file_scan_field(
    obelisk_rt_context *context, obelisk_rt_gc_lane_v1 *lane,
    uint32_t descriptor, uint32_t enabled, const char *prefix,
    uint64_t prefixSize, uint32_t specifier, uint64_t width,
    obelisk_rt_string_v1 *outField, uint32_t *outOk, uint32_t *outEOF) {
  if (!context || !lane || !outField || !outOk || !outEOF || enabled > 1 ||
      (!prefix && prefixSize != 0))
    return OBELISK_RT_INVALID_ARGUMENT;
  *outField = 0;
  *outOk = 0;
  *outEOF = 0;
  if (!enabled)
    return OBELISK_RT_OK;
  return guarded(context, [&] {
    FileEntry *entry;
    std::unique_lock<std::recursive_mutex> lock;
    obelisk_rt_status status =
        checkFileArguments(context, descriptor, entry, lock);
    // IEEE 1800-2017 21.3.4.3 ends $fscanf with EOF when the input ends before
    // the first conversion, and a descriptor that was never opened has no
    // input to begin with -- $fopen hands back 0 for a file it could not open.
    // That is a failed conversion, not a failed simulation, so it reads as end
    // of file like every other read on a descriptor that cannot deliver.
    if (status == OBELISK_RT_INVALID_HANDLE) {
      *outEOF = 1;
      return OBELISK_RT_OK;
    }
    if (status != OBELISK_RT_OK)
      return status;
    std::string field;
    errno = 0;
    // Dispatch once so ordinary readable streams retain direct stdio calls in
    // their specialized scan loop. A non-readable stream can still supply the
    // one byte held by $ungetc, after which its synthetic reader reports EOF.
    ScanResult result = entry->readable
                            ? scanFileField<false>(*entry, prefix, prefixSize,
                                                   specifier, width, field)
                            : scanFileField<true>(*entry, prefix, prefixSize,
                                                  specifier, width, field);
    if (result == ScanResult::Error) {
      recordIOError(context, *entry, "formatted file read failed");
      // A host read error is an fscanf input failure, not a simulation
      // failure. The lowering returns EOF when no conversion was assigned and
      // otherwise preserves the preceding assignment count; $ferror exposes
      // the recorded host error independently from $feof.
      *outEOF = 1;
      return OBELISK_RT_OK;
    }
    if (result == ScanResult::EndOfFile) {
      *outEOF = 1;
      return OBELISK_RT_OK;
    }
    if (result == ScanResult::Mismatch)
      return OBELISK_RT_OK;
    status =
        obelisk_rt_v1_string_create(lane, field.data(), field.size(), outField);
    if (status == OBELISK_RT_OK)
      *outOk = 1;
    return status;
  });
}

extern "C" OBELISK_RT_FEATURE_TEXT obelisk_rt_status
obelisk_rt_v1_file_scan_dynamic(
    obelisk_rt_context *context, obelisk_rt_gc_lane_v1 *lane,
    uint32_t descriptor, obelisk_rt_string_v1 format, uint32_t planCursor,
    uint32_t enabled, uint32_t finalize, uint64_t allowedSpecifiers,
    uint64_t rawTwoStateBytes, uint64_t rawFourStateBytes,
    obelisk_rt_string_v1 *outField, uint32_t *outPlanCursor,
    uint32_t *outSpecifier, uint32_t *outOk, uint32_t *outEOF) {
  if (!context || !lane || !outField || !outPlanCursor || !outSpecifier ||
      !outOk || !outEOF || enabled > 1 || finalize > 1)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (obelisk_rt_managed_lane_context(lane) != context)
    return OBELISK_RT_INVALID_ARGUMENT;
  obelisk_rt_status ownership = obelisk_rt_validate_string(context, format);
  if (ownership != OBELISK_RT_OK)
    return ownership;
  *outField = 0;
  *outPlanCursor = planCursor;
  *outSpecifier = 0;
  *outOk = 0;
  *outEOF = 0;
  if (!enabled)
    return OBELISK_RT_OK;

  std::shared_ptr<const DynamicScanPlan> plan;
  obelisk_rt_status status =
      obelisk_rt_dynamic_scan_plan(context, format, plan);
  if (status != OBELISK_RT_OK)
    return status;
  if (!plan->error.empty()) {
    std::fprintf(stderr, "obelisk: malformed dynamic $fscanf conversion '%s'\n",
                 plan->error.c_str());
    return OBELISK_RT_INVALID_ARGUMENT;
  }
  if (planCursor > plan->conversions.size()) {
    std::fprintf(stderr, "obelisk: invalid dynamic $fscanf format cursor\n");
    return OBELISK_RT_INVALID_ARGUMENT;
  }

  return obelisk_rt_feature_guarded(context, [&]() OBELISK_RT_FEATURE_HELPER {
    FileEntry *entry;
    std::unique_lock<std::recursive_mutex> lock;
    obelisk_rt_status checked =
        checkFileArguments(context, descriptor, entry, lock);
    if (checked == OBELISK_RT_INVALID_HANDLE) {
      *outEOF = 1;
      return OBELISK_RT_OK;
    }
    if (checked != OBELISK_RT_OK)
      return checked;

    auto letter = [](uint32_t specifier) OBELISK_RT_FEATURE_HELPER {
      return static_cast<char>(std::tolower(
          static_cast<unsigned char>(static_cast<char>(specifier))));
    };
    auto allowed = [&](uint32_t specifier) OBELISK_RT_FEATURE_HELPER {
      char normalized = letter(specifier);
      return normalized >= 'a' && normalized <= 'z' &&
             (allowedSpecifiers &
              (UINT64_C(1) << static_cast<unsigned>(normalized - 'a'))) != 0;
    };
    auto conversionKind = [&](uint32_t specifier) OBELISK_RT_FEATURE_HELPER {
      switch (letter(specifier)) {
      case 's':
      case 'c':
        return OBELISK_RT_SCAN_DYNAMIC_TEXT;
      case 'm':
        return OBELISK_RT_SCAN_DYNAMIC_HIERARCHY;
      case 'e':
      case 'f':
      case 'g':
        return OBELISK_RT_SCAN_DYNAMIC_REAL;
      case 't':
        return OBELISK_RT_SCAN_DYNAMIC_TIME;
      case 'b':
      case 'v':
        return OBELISK_RT_SCAN_DYNAMIC_LOGIC2;
      case 'o':
        return OBELISK_RT_SCAN_DYNAMIC_LOGIC8;
      case 'd':
        return OBELISK_RT_SCAN_DYNAMIC_LOGIC10;
      case 'u':
        return OBELISK_RT_SCAN_DYNAMIC_RAW2;
      case 'z':
        return OBELISK_RT_SCAN_DYNAMIC_RAW4;
      default:
        return OBELISK_RT_SCAN_DYNAMIC_LOGIC16;
      }
    };
    auto interpret = [&](auto synthetic)
                         OBELISK_RT_FEATURE_HELPER -> obelisk_rt_status {
      constexpr bool Synthetic = decltype(synthetic)::value;
      for (size_t ordinal = planCursor; ordinal != plan->conversions.size();
           ++ordinal) {
        const DynamicScanConversion &conversion = plan->conversions[ordinal];
        char normalized = letter(conversion.specifier);
        if (!conversion.suppressed) {
          if (finalize) {
            std::fprintf(stderr, "obelisk: dynamic $fscanf format has more "
                                 "conversions than destinations\n");
            return OBELISK_RT_INVALID_ARGUMENT;
          }
          if (!allowed(conversion.specifier)) {
            std::fprintf(stderr,
                         "obelisk: dynamic $fscanf %%%c is incompatible with "
                         "destination %u\n",
                         static_cast<char>(conversion.specifier),
                         planCursor + 1);
            return OBELISK_RT_INVALID_ARGUMENT;
          }
        }

        ScanResult result;
        std::string field;
        if (normalized == 'u' || normalized == 'z') {
          uint64_t rawSize =
              normalized == 'u' ? rawTwoStateBytes : rawFourStateBytes;
          if (conversion.suppressed)
            rawSize = conversion.width;
          if (rawSize == 0) {
            std::fprintf(
                stderr,
                "obelisk: dynamic $fscanf assignment suppression for raw "
                "%%%c requires an explicit byte count\n",
                static_cast<char>(conversion.specifier));
            return OBELISK_RT_INVALID_ARGUMENT;
          }
          result = scanFileRaw<Synthetic>(*entry, conversion.prefix.data(),
                                          conversion.prefix.size(), rawSize,
                                          conversion.width, field);
        } else {
          result = scanFileField<Synthetic>(
              *entry, conversion.prefix.data(), conversion.prefix.size(),
              conversion.specifier, conversion.width, field);
        }
        if (result == ScanResult::Error) {
          recordIOError(context, *entry, "dynamic formatted file read failed");
          *outEOF = 1;
          return OBELISK_RT_OK;
        }
        if (result == ScanResult::EndOfFile) {
          *outEOF = 1;
          return OBELISK_RT_OK;
        }
        if (result == ScanResult::Mismatch)
          return OBELISK_RT_OK;
        *outPlanCursor = static_cast<uint32_t>(ordinal + 1);
        if (!conversion.suppressed) {
          obelisk_rt_status created = obelisk_rt_v1_string_create(
              lane, field.data(), field.size(), outField);
          if (created != OBELISK_RT_OK)
            return created;
          *outSpecifier = conversionKind(conversion.specifier);
          *outOk = 1;
          return OBELISK_RT_OK;
        }
      }

      if (!finalize) {
        std::fprintf(stderr,
                     "obelisk: dynamic $fscanf format has fewer conversions "
                     "than destinations\n");
        return OBELISK_RT_INVALID_ARGUMENT;
      }
      std::string ignored;
      ScanResult result = scanFileField<Synthetic>(
          *entry, plan->suffix.data(), plan->suffix.size(), 'm', 0, ignored);
      if (result == ScanResult::Error) {
        recordIOError(context, *entry, "dynamic formatted file read failed");
        *outEOF = 1;
        return OBELISK_RT_OK;
      }
      if (result == ScanResult::EndOfFile)
        *outEOF = 1;
      else if (result == ScanResult::Match)
        *outOk = 1;
      *outPlanCursor = static_cast<uint32_t>(plan->conversions.size());
      return OBELISK_RT_OK;
    };
    errno = 0;
    return entry->readable ? interpret(std::integral_constant<bool, false>{})
                           : interpret(std::integral_constant<bool, true>{});
  });
}

extern "C" obelisk_rt_status obelisk_rt_v1_file_scan_raw(
    obelisk_rt_context *context, uint32_t descriptor, uint32_t enabled,
    const char *prefix, uint64_t prefixSize, uint64_t rawSize,
    uint64_t bitWidth, uint32_t fourState, uint64_t maxWidth, void *value,
    uint64_t valueSize, void *unknown, uint64_t unknownSize, uint32_t *outOk,
    uint32_t *outEOF) {
  auto expectedSize = bitWidth
                          ? obelisk_rt_scan_raw_size(bitWidth, fourState != 0)
                          : std::optional<uint64_t>(rawSize);
  uint64_t packedSize = bitWidth / 8 + (bitWidth % 8 != 0);
  if (!context || !outOk || !outEOF || enabled > 1 || fourState > 1 ||
      !expectedSize || rawSize == 0 || *expectedSize != rawSize ||
      rawSize > std::numeric_limits<size_t>::max() ||
      rawSize > std::string{}.max_size() ||
      packedSize > std::numeric_limits<size_t>::max() ||
      !validBytes(prefix, prefixSize) ||
      (bitWidth &&
       (!validBytes(value, valueSize) || !validBytes(unknown, unknownSize) ||
        valueSize < packedSize || unknownSize < packedSize)))
    return OBELISK_RT_INVALID_ARGUMENT;
  *outOk = 0;
  *outEOF = 0;
  if (bitWidth) {
    std::memset(value, 0, static_cast<size_t>(packedSize));
    std::memset(unknown, 0, static_cast<size_t>(packedSize));
  }
  if (!enabled)
    return OBELISK_RT_OK;
  return guarded(context, [&] {
    FileEntry *entry;
    std::unique_lock<std::recursive_mutex> lock;
    obelisk_rt_status status =
        checkFileArguments(context, descriptor, entry, lock);
    if (status == OBELISK_RT_INVALID_HANDLE) {
      *outEOF = 1;
      return OBELISK_RT_OK;
    }
    if (status != OBELISK_RT_OK)
      return status;
    std::string raw;
    raw.reserve(static_cast<size_t>(rawSize));
    errno = 0;
    ScanResult result = entry->readable
                            ? scanFileRaw<false>(*entry, prefix, prefixSize,
                                                 rawSize, maxWidth, raw)
                            : scanFileRaw<true>(*entry, prefix, prefixSize,
                                                rawSize, maxWidth, raw);
    if (result == ScanResult::Error) {
      recordIOError(context, *entry, "formatted raw file read failed");
      *outEOF = 1;
      return OBELISK_RT_OK;
    }
    if (result == ScanResult::EndOfFile) {
      *outEOF = 1;
      return OBELISK_RT_OK;
    }
    if (result == ScanResult::Mismatch)
      return OBELISK_RT_OK;
    if (bitWidth && !obelisk_rt_decode_scan_raw(
                        raw.data(), raw.size(), bitWidth, fourState != 0, value,
                        valueSize, unknown, unknownSize))
      return OBELISK_RT_INVALID_ARGUMENT;
    *outOk = 1;
    return OBELISK_RT_OK;
  });
}

extern "C" obelisk_rt_status obelisk_rt_v1_file_eof(obelisk_rt_context *context,
                                                    uint32_t descriptor,
                                                    uint32_t *outIsEOF) {
  if (!context || !outIsEOF)
    return OBELISK_RT_INVALID_ARGUMENT;
  return guarded(context, [&] {
    FileEntry *entry;
    std::unique_lock<std::recursive_mutex> lock;
    obelisk_rt_status status =
        checkFileArguments(context, descriptor, entry, lock);
    if (status != OBELISK_RT_OK)
      return status;
    // A descriptor without read access is at end of file unless a $ungetc
    // byte is still pending: it can never deliver anything else.
    *outIsEOF =
        (entry->readable ? std::feof(entry->stream) != 0 : entry->pushback < 0)
            ? 1u
            : 0u;
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
    std::unique_lock<std::recursive_mutex> lock;
    obelisk_rt_status status =
        checkFileArguments(context, descriptor, entry, lock);
    if (status != OBELISK_RT_OK)
      return status;
    int error = std::ferror(entry->stream) ? entry->lastError : 0;
    *outErrorCode = error;
    return makeBuffer(error ? hostErrorMessage(error) : "", outMessage);
  });
}

extern "C" obelisk_rt_status obelisk_rt_v1_file_error_string(
    obelisk_rt_context *context, obelisk_rt_gc_lane_v1 *lane,
    uint32_t descriptor, obelisk_rt_string_v1 *outMessage,
    int32_t *outErrorCode) {
  if (!outMessage || !outErrorCode)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outMessage = 0;
  *outErrorCode = 0;
  obelisk_rt_buffer_v1 message{};
  int32_t code = 0;
  obelisk_rt_status status =
      obelisk_rt_v1_file_error(context, descriptor, &code, &message);
  if (status != OBELISK_RT_OK)
    return status;
  status = obelisk_rt_v1_string_create(
      lane, reinterpret_cast<const char *>(message.data), message.size,
      outMessage);
  if (status == OBELISK_RT_OK)
    *outErrorCode = code;
  obelisk_rt_v1_buffer_release(&message);
  return status;
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
    std::unique_lock<std::recursive_mutex> lock;
    obelisk_rt_status status =
        checkFileArguments(context, descriptor, entry, lock);
    if (status != OBELISK_RT_OK)
      return status;
    errno = 0;
    if (std::fseek(entry->stream, static_cast<long>(offset), hostOrigin) != 0) {
      recordIOError(context, *entry, "fseek failed");
      return OBELISK_RT_IO_ERROR;
    }
    entry->pushback = -1;
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
    std::unique_lock<std::recursive_mutex> lock;
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
