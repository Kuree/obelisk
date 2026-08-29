//===- ProtectedEnvelopeTestProvider.cpp - Test-only fake provider -------===//

#include "ProtectedEnvelopeTestProvider.h"

#include "obelisk/Frontend/ProtectedEnvelope.h"

#include "llvm/ADT/StringRef.h"

#include <stdexcept>
#include <string>

using namespace obelisk::frontend;

namespace {

std::string unquote(llvm::StringRef value) {
  value = value.trim();
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
    value = value.drop_front().drop_back();
  return value.str();
}

class TestProvider final : public ProtectedEnvelopeProvider {
public:
  ProtectedEnvelopeResult
  decrypt(const ProtectedEnvelope &envelope) const override {
    std::string method;
    std::string keyName;
    bool began = false;
    bool ended = false;
    bool invalidOrder = false;
    ProtectedEnvelopeResult result;

    for (const ProtectedEnvelopeRecord &record : envelope.records) {
      if (record.recordKind == ProtectedRecordKind::Expression) {
        if (record.name == "data_method")
          method = unquote(record.value);
        else if (record.name == "data_keyname")
          keyName = unquote(record.value);
        else if (record.name == "begin_protected")
          began = true;
        else if (record.name == "end_protected")
          ended = true;
        continue;
      }

      // IEEE 1800-2017 34.2: the fake deliberately consumes the unified
      // source order and the encoding snapshot effective at each block.
      if (!began || ended || record.blockKind != ProtectedBlockKind::Data ||
          record.encoding != ProtectedEncoding::Raw) {
        invalidOrder = true;
        continue;
      }
      result.source.insert(result.source.end(), record.encodedText.begin(),
                           record.encodedText.end());
    }

    // IEEE 1800-2017 34.3.1 uses this illustrative extension. It exists only
    // in this test executable and cannot become a production fallback.
    bool throwAfterDecrypt = method == "x-throw";
    if ((!throwAfterDecrypt && method != "x-caesar") || keyName != "rot13") {
      result.status = ProtectedEnvelopeStatus::Rejected;
      return result;
    }
    if (invalidOrder || !began || !ended || result.source.empty()) {
      result.status = ProtectedEnvelopeStatus::InvalidData;
      return result;
    }

    for (char &character : result.source) {
      unsigned char value = static_cast<unsigned char>(character);
      if (value >= 'a' && value <= 'z')
        character = static_cast<char>('a' + (value - 'a' + 13) % 26);
      else if (value >= 'A' && value <= 'Z')
        character = static_cast<char>('A' + (value - 'A' + 13) % 26);
    }
    if (throwAfterDecrypt)
      throw std::runtime_error("SECRET_PROVIDER_EXCEPTION_91d3");
    result.status = ProtectedEnvelopeStatus::Success;
    return result;
  }
};

} // namespace

std::shared_ptr<const ProtectedEnvelopeProvider>
createProtectedEnvelopeTestProvider() {
  return std::make_shared<TestProvider>();
}
