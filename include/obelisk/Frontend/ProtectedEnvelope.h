//===- ProtectedEnvelope.h - Protected source provider ---------*- C++ -*-===//

#ifndef OBELISK_FRONTEND_PROTECTEDENVELOPE_H
#define OBELISK_FRONTEND_PROTECTEDENVELOPE_H

#include "llvm/ADT/ArrayRef.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace obelisk::frontend {

enum class ProtectedEncoding : uint8_t {
  UUEncode,
  Base64,
  QuotedPrintable,
  Raw,
};

enum class ProtectedBlockKind : uint8_t {
  Data,
  Digest,
  Key,
  DataPublicKey,
  DataDecryptKey,
  DigestPublicKey,
  DigestDecryptKey,
  KeyPublicKey,
};

enum class ProtectedRecordKind : uint8_t {
  Expression,
  EncodedBlock,
};

/// One source-ordered Clause 34 record. Expression fields are populated for
/// Expression records; block fields hold the effective encoding state captured
/// when an EncodedBlock record was encountered.
struct ProtectedEnvelopeRecord {
  ProtectedRecordKind recordKind = ProtectedRecordKind::Expression;
  std::string name;
  std::string value;
  ProtectedBlockKind blockKind = ProtectedBlockKind::Data;
  ProtectedEncoding encoding = ProtectedEncoding::Raw;
  uint32_t expectedBytes = 0;
  std::string_view encodedText;
};

struct ProtectedEnvelope {
  llvm::ArrayRef<ProtectedEnvelopeRecord> records;
};

enum class ProtectedEnvelopeStatus : uint8_t {
  Success,
  ProviderUnavailable,
  Rejected,
  InvalidData,
  ResourceLimit,
};

namespace detail {

template <typename T> class SecureAllocator {
public:
  using value_type = T;
  using propagate_on_container_move_assignment = std::true_type;
  using is_always_equal = std::true_type;

  SecureAllocator() = default;
  template <typename U>
  constexpr SecureAllocator(const SecureAllocator<U> &) noexcept {}

  [[nodiscard]] T *allocate(size_t count) {
    return std::allocator<T>{}.allocate(count);
  }

  void deallocate(T *data, size_t count) noexcept {
    // IEEE 1800-2017 34.3.2 replacement text is plaintext. Wipe the entire
    // allocation, not just live vector elements, before reserve / shrink /
    // move operations can return it to the allocator.
    auto *bytes = reinterpret_cast<volatile unsigned char *>(data);
    for (size_t index = 0; index < count * sizeof(T); ++index)
      bytes[index] = 0;
    std::allocator<T>{}.deallocate(data, count);
  }
};

template <typename T, typename U>
constexpr bool operator==(const SecureAllocator<T> &,
                          const SecureAllocator<U> &) noexcept {
  return true;
}

} // namespace detail

/// Capacity-aware storage for decrypted replacement source. Reallocation and
/// destruction securely erase the complete allocation; clear() also erases
/// unused capacity that can retain bytes after resize.
class ProtectedSourceBuffer {
  using Storage = std::vector<char, detail::SecureAllocator<char>>;

public:
  using iterator = Storage::iterator;
  using const_iterator = Storage::const_iterator;

  ProtectedSourceBuffer() = default;
  ProtectedSourceBuffer(const ProtectedSourceBuffer &) = delete;
  ProtectedSourceBuffer &operator=(const ProtectedSourceBuffer &) = delete;
  ProtectedSourceBuffer(ProtectedSourceBuffer &&) noexcept = default;
  ProtectedSourceBuffer &operator=(ProtectedSourceBuffer &&other) noexcept {
    if (this != &other) {
      clear();
      storage = std::move(other.storage);
    }
    return *this;
  }
  ~ProtectedSourceBuffer() { clear(); }

  [[nodiscard]] bool empty() const noexcept { return storage.empty(); }
  [[nodiscard]] size_t size() const noexcept { return storage.size(); }
  [[nodiscard]] size_t capacity() const noexcept { return storage.capacity(); }
  [[nodiscard]] char *data() noexcept { return storage.data(); }
  [[nodiscard]] const char *data() const noexcept { return storage.data(); }
  iterator begin() noexcept { return storage.begin(); }
  const_iterator begin() const noexcept { return storage.begin(); }
  iterator end() noexcept { return storage.end(); }
  const_iterator end() const noexcept { return storage.end(); }
  char &back() noexcept { return storage.back(); }
  const char &back() const noexcept { return storage.back(); }

  void reserve(size_t count) { storage.reserve(count); }
  void resize(size_t count) {
    if (count < storage.size())
      wipe(count, storage.size());
    storage.resize(count);
  }
  void resize(size_t count, char value) {
    if (count < storage.size())
      wipe(count, storage.size());
    storage.resize(count, value);
  }
  void push_back(char value) { storage.push_back(value); }
  template <typename Iterator>
  iterator insert(const_iterator position, Iterator first, Iterator last) {
    return storage.insert(position, first, last);
  }

  void shrink_to_fit() {
    wipe(storage.size(), storage.capacity());
    storage.shrink_to_fit();
  }

  void clear() noexcept {
    wipe(0, storage.capacity());
    storage.clear();
  }

private:
  void wipe(size_t first, size_t last) noexcept {
    auto *bytes = reinterpret_cast<volatile unsigned char *>(storage.data());
    for (size_t index = first; index < last; ++index)
      bytes[index] = 0;
  }

  Storage storage;
};

struct ProtectedEnvelopeResult {
  ProtectedEnvelopeStatus status = ProtectedEnvelopeStatus::ProviderUnavailable;
  ProtectedSourceBuffer source;

  ProtectedEnvelopeResult() = default;
  ProtectedEnvelopeResult(const ProtectedEnvelopeResult &) = delete;
  ProtectedEnvelopeResult &operator=(const ProtectedEnvelopeResult &) = delete;
  ProtectedEnvelopeResult(ProtectedEnvelopeResult &&other) noexcept
      : status(other.status), source(std::move(other.source)) {
    other.status = ProtectedEnvelopeStatus::ProviderUnavailable;
  }
  ProtectedEnvelopeResult &operator=(ProtectedEnvelopeResult &&other) noexcept {
    if (this != &other) {
      clear();
      status = other.status;
      source = std::move(other.source);
      other.status = ProtectedEnvelopeStatus::ProviderUnavailable;
    }
    return *this;
  }
  ~ProtectedEnvelopeResult() { clear(); }

  void clear() noexcept { source.clear(); }
};

/// Host policy for decrypting one complete IEEE 1800 protected envelope.
/// Implementations must be thread-safe: independent compilation units can be
/// preprocessed concurrently. Error details must not contain key material,
/// ciphertext, or decrypted source; the frontend reports only `status`.
class ProtectedEnvelopeProvider {
public:
  virtual ~ProtectedEnvelopeProvider() = default;
  virtual ProtectedEnvelopeResult
  decrypt(const ProtectedEnvelope &envelope) const = 0;
};

} // namespace obelisk::frontend

#endif // OBELISK_FRONTEND_PROTECTEDENVELOPE_H
