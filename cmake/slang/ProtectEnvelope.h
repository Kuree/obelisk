//------------------------------------------------------------------------------
//! @file ProtectEnvelope.h
//! @brief Host callback for decrypting IEEE 1800 protected envelopes
//
// SPDX-License-Identifier: MIT
//------------------------------------------------------------------------------
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "slang/parsing/Lexer.h"
#include "slang/text/SourceLocation.h"
#include "slang/util/SmallVector.h"

namespace slang::parsing {

enum class ProtectBlockKind : uint8_t {
  Data,
  Digest,
  Key,
  DataPublicKey,
  DataDecryptKey,
  DigestPublicKey,
  DigestDecryptKey,
  KeyPublicKey
};

enum class ProtectRecordKind : uint8_t { Expression, EncodedBlock };

struct ProtectEnvelopeRecord {
  ProtectRecordKind recordKind = ProtectRecordKind::Expression;
  std::string name;
  std::string value;
  ProtectBlockKind blockKind = ProtectBlockKind::Data;
  ProtectEncoding encoding = ProtectEncoding::Raw;
  uint32_t expectedBytes = 0;
  std::string_view encodedText;
  SourceRange range;
};

struct ProtectEnvelope {
  SmallVector<ProtectEnvelopeRecord> records;
  SourceRange range;
};

enum class ProtectEnvelopeStatus : uint8_t {
  Success,
  ProviderUnavailable,
  Rejected,
  InvalidData,
  ResourceLimit
};

struct ProtectEnvelopeResult {
  ProtectEnvelopeStatus status = ProtectEnvelopeStatus::ProviderUnavailable;
  SmallVector<char> source;

  ProtectEnvelopeResult() = default;
  ProtectEnvelopeResult(const ProtectEnvelopeResult &) = delete;
  ProtectEnvelopeResult &operator=(const ProtectEnvelopeResult &) = delete;
  ProtectEnvelopeResult(ProtectEnvelopeResult &&other) noexcept
      : status(other.status), source(std::move(other.source)) {
    other.status = ProtectEnvelopeStatus::ProviderUnavailable;
  }
  ProtectEnvelopeResult &operator=(ProtectEnvelopeResult &&other) noexcept {
    if (this != &other) {
      clear();
      status = other.status;
      source = std::move(other.source);
      other.status = ProtectEnvelopeStatus::ProviderUnavailable;
    }
    return *this;
  }
  ~ProtectEnvelopeResult() { clear(); }

  void clear() noexcept {
    auto *data = reinterpret_cast<volatile char *>(source.data());
    for (size_t index = 0; index < source.capacity(); index++)
      data[index] = 0;
    source.clear();
  }
};

/// A synchronous, thread-safe host callback. Implementations must not place
/// secrets or decrypted source in externally visible error text.
class ProtectEnvelopeDecryptor {
public:
  virtual ~ProtectEnvelopeDecryptor() = default;
  virtual ProtectEnvelopeResult
  decrypt(const ProtectEnvelope &envelope) const = 0;
};

} // namespace slang::parsing
