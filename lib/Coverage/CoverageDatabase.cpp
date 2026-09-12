//===- CoverageDatabase.cpp - portable .obcov codec ---------------------===//

#include "obelisk/Coverage/CoverageDatabase.h"

#include "CoverageFileSupport.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <new>
#include <optional>
#include <set>
#include <sstream>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
#define OBELISK_COVERAGE_HAS_EXCEPTIONS 1
#else
#define OBELISK_COVERAGE_HAS_EXCEPTIONS 0
#endif

namespace obelisk::coverage {
namespace {

struct SectionDescriptor {
  SectionKind kind;
  uint32_t recordSize;
  bool required;
  bool staticSchema;
};
struct FieldDescriptor {
  const char *name;
  uint32_t offset;
  uint32_t width;
  CoverageFieldEncoding encoding;
  CoverageFieldSemantic semantic;
  CoverageFieldDomain domain;
  bool nullable;
  int64_t validMask;
  const char *pairedField;
};
struct DomainDescriptor {
  CoverageFieldDomain domain;
  CoverageFieldDomainKind kind;
  uint32_t targetSection;
  const uint32_t *values;
  uint32_t valueCount;
};
struct RecordDescriptor {
  SectionKind section;
  const char *name;
  uint32_t size;
  const FieldDescriptor *fields;
  uint32_t fieldCount;
};
#include "obelisk/Coverage/CoverageFormatParser.h.inc"
#include "obelisk/Coverage/CoverageFormatSerializer.h.inc"

constexpr uint32_t RequiredSection = 1;
constexpr uint32_t KnownHeaderFlags = 0;
constexpr uint64_t NoRecord = UINT64_MAX;

void setDiagnostic(Diagnostic *diagnostic, Status status, uint64_t offset,
                   SectionKind section, uint64_t record, const char *field,
                   std::string detail = {}) {
  if (!diagnostic)
    return;
  diagnostic->status = status;
  diagnostic->offset = offset;
  diagnostic->section = section;
  diagnostic->record = record;
  diagnostic->field = field;
  diagnostic->detail = std::move(detail);
}

bool addOverflow(uint64_t left, uint64_t right, uint64_t &result) {
  if (right > UINT64_MAX - left)
    return true;
  result = left + right;
  return false;
}

bool mulOverflow(uint64_t left, uint64_t right, uint64_t &result) {
  if (left && right > UINT64_MAX / left)
    return true;
  result = left * right;
  return false;
}

using WideUnsigned = std::vector<uint64_t>;

void normalizeWide(WideUnsigned &value) {
  while (!value.empty() && !value.back())
    value.pop_back();
}

void addWide(WideUnsigned &destination, const WideUnsigned &source) {
  if (destination.size() < source.size())
    destination.resize(source.size());
  uint64_t carry = 0;
  size_t index = 0;
  for (; index != source.size(); ++index) {
    uint64_t old = destination[index];
    uint64_t sum = old + source[index];
    uint64_t firstCarry = sum < old;
    uint64_t withCarry = sum + carry;
    uint64_t secondCarry = withCarry < sum;
    destination[index] = withCarry;
    carry = firstCarry | secondCarry;
  }
  while (carry && index != destination.size()) {
    uint64_t old = destination[index];
    destination[index] = old + 1;
    carry = destination[index] == 0;
    ++index;
  }
  if (carry)
    destination.push_back(1);
  normalizeWide(destination);
}

bool equalWideRange(const WideUnsigned &value,
                    const std::vector<uint64_t> &limbs, uint32_t first,
                    uint32_t count) {
  if (value.size() != count)
    return false;
  return std::equal(value.begin(), value.end(), limbs.begin() + first);
}

uint64_t align8(uint64_t value) {
  return value > UINT64_MAX - 7 ? UINT64_MAX : (value + 7) & ~uint64_t{7};
}

uint32_t read32(const uint8_t *data) {
  return uint32_t(data[0]) | uint32_t(data[1]) << 8 | uint32_t(data[2]) << 16 |
         uint32_t(data[3]) << 24;
}

uint64_t read64(const uint8_t *data) {
  return uint64_t(read32(data)) | uint64_t(read32(data + 4)) << 32;
}

struct HeaderBytes {
  std::vector<uint8_t> value;

  bool reserve(uint64_t amount) {
    if (amount > value.max_size())
      return false;
    value.reserve(static_cast<size_t>(amount));
    return true;
  }
  void u32(uint32_t v) {
    value.push_back(uint8_t(v));
    value.push_back(uint8_t(v >> 8));
    value.push_back(uint8_t(v >> 16));
    value.push_back(uint8_t(v >> 24));
  }
  void u64(uint64_t v) {
    u32(uint32_t(v));
    u32(uint32_t(v >> 32));
  }
  void raw(const uint8_t *data, size_t size) {
    value.insert(value.end(), data, data + size);
  }
  template <size_t N> void raw(const std::array<uint8_t, N> &data) {
    raw(data.data(), data.size());
  }
  void zero(size_t size) { value.insert(value.end(), size, uint8_t{0}); }
  void pad8() {
    zero(static_cast<size_t>(align8(value.size()) - value.size()));
  }
};

struct Bytes {
  WriteCallback callback = nullptr;
  void *user = nullptr;
  uint64_t size = 0;
  bool good = true;
  Status status = Status::Ok;
  uint64_t recordIndex = 0;
  const char *field = nullptr;

  void reset(WriteCallback nextCallback, void *nextUser) {
    callback = nextCallback;
    user = nextUser;
    size = 0;
    good = true;
    status = Status::Ok;
    recordIndex = 0;
    field = nullptr;
  }
  void raw(const uint8_t *data, size_t count) {
    if (count > UINT64_MAX - size) {
      good = false;
      status = Status::IntegerOverflow;
      return;
    }
    size += count;
    if (good && callback && count && !callback(data, count, user)) {
      good = false;
      status = Status::IoError;
    }
  }
  template <size_t N> void raw(const std::array<uint8_t, N> &data) {
    raw(data.data(), data.size());
  }
  void u32(uint32_t value) {
    uint8_t data[4];
    for (uint32_t i = 0; i != 4; ++i)
      data[i] = static_cast<uint8_t>(value >> (i * 8));
    raw(data, sizeof(data));
  }
  void zero(size_t count) {
    static constexpr uint8_t zeros[8] = {};
    while (count) {
      size_t chunk = std::min(count, sizeof(zeros));
      raw(zeros, chunk);
      count -= chunk;
    }
  }
  template <typename Record, typename Populate> void record(Populate populate) {
    std::array<uint8_t, Record::Size> data{};
    populate(data.data());
    const RecordDescriptor *descriptor =
        recordDescriptor(SerializerRecordDescriptors, Record::Section);
    const FieldDescriptor *invalidField = nullptr;
    if (!descriptor || !SerializerValidatePhysicalRecord(
                           *descriptor, data.data(),
                           [](const FieldDescriptor &, uint64_t,
                              const uint8_t *) { return true; },
                           invalidField)) {
      good = false;
      status = invalidField &&
                       (invalidField->semantic == CoverageFieldSemantic::Enum ||
                        invalidField->semantic == CoverageFieldSemantic::Flags)
                   ? Status::InvalidEnum
                   : Status::InvalidDatabase;
      field = invalidField ? invalidField->name : "record";
      return;
    }
    raw(data);
    if (good)
      ++recordIndex;
  }
};

class SHA256 {
public:
  SHA256() { reset(); }
  void update(const uint8_t *data, size_t size) {
    total += size;
    while (size) {
      size_t take = std::min(size, sizeof(buffer) - used);
      std::memcpy(buffer + used, data, take);
      used += take;
      data += take;
      size -= take;
      if (used == sizeof(buffer)) {
        transform(buffer);
        used = 0;
      }
    }
  }
  Digest finish() {
    uint64_t bits = total * 8;
    buffer[used++] = 0x80;
    if (used > 56) {
      std::memset(buffer + used, 0, 64 - used);
      transform(buffer);
      used = 0;
    }
    std::memset(buffer + used, 0, 56 - used);
    for (unsigned i = 0; i != 8; ++i)
      buffer[63 - i] = uint8_t(bits >> (i * 8));
    transform(buffer);
    Digest digest{};
    for (unsigned i = 0; i != 8; ++i)
      for (unsigned j = 0; j != 4; ++j)
        digest[i * 4 + j] = uint8_t(state[i] >> (24 - j * 8));
    return digest;
  }

private:
  uint32_t state[8]{};
  uint8_t buffer[64]{};
  size_t used = 0;
  uint64_t total = 0;

  static uint32_t rotate(uint32_t value, unsigned amount) {
    return (value >> amount) | (value << (32 - amount));
  }
  void reset() {
    static constexpr uint32_t initial[] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372,
                                           0xa54ff53a, 0x510e527f, 0x9b05688c,
                                           0x1f83d9ab, 0x5be0cd19};
    std::copy(std::begin(initial), std::end(initial), state);
    used = 0;
    total = 0;
  }
  void transform(const uint8_t *block) {
    static constexpr uint32_t constants[] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
        0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
        0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
        0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
        0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
        0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t words[64];
    for (unsigned i = 0; i != 16; ++i)
      words[i] = uint32_t(block[i * 4]) << 24 |
                 uint32_t(block[i * 4 + 1]) << 16 |
                 uint32_t(block[i * 4 + 2]) << 8 | uint32_t(block[i * 4 + 3]);
    for (unsigned i = 16; i != 64; ++i) {
      uint32_t s0 = rotate(words[i - 15], 7) ^ rotate(words[i - 15], 18) ^
                    (words[i - 15] >> 3);
      uint32_t s1 = rotate(words[i - 2], 17) ^ rotate(words[i - 2], 19) ^
                    (words[i - 2] >> 10);
      words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for (unsigned i = 0; i != 64; ++i) {
      uint32_t s1 = rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25);
      uint32_t ch = (e & f) ^ ((~e) & g);
      uint32_t t1 = h + s1 + ch + constants[i] + words[i];
      uint32_t s0 = rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22);
      uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      uint32_t t2 = s0 + maj;
      h = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
  }
};

bool validUtf8(const std::string &text) {
  const auto *data = reinterpret_cast<const uint8_t *>(text.data());
  size_t index = 0;
  while (index < text.size()) {
    uint8_t first = data[index++];
    if (first < 0x80)
      continue;
    unsigned count = first >= 0xf0   ? 3
                     : first >= 0xe0 ? 2
                     : first >= 0xc2 ? 1
                                     : 0;
    if (!count || index + count > text.size())
      return false;
    uint32_t code = first & ((1u << (6 - count)) - 1);
    for (unsigned i = 0; i != count; ++i) {
      if ((data[index] & 0xc0) != 0x80)
        return false;
      code = (code << 6) | (data[index++] & 0x3f);
    }
    if ((count == 1 && code < 0x80) || (count == 2 && code < 0x800) ||
        (count == 3 && code < 0x10000) || code > 0x10ffff ||
        (code >= 0xd800 && code <= 0xdfff))
      return false;
  }
  return true;
}

struct StringTable {
  std::vector<std::string> strings;
  std::unordered_map<std::string, uint32_t> indexes;
  uint32_t get(const std::string &text) const { return indexes.at(text); }
};

Status collectStrings(const Database &database, StringTable &result) {
  std::set<std::string> unique;
  unique.insert("");
  unique.insert(database.producer);
  for (const auto &v : database.sourceFiles)
    unique.insert(v.path);
  for (const auto &v : database.linePoints)
    unique.insert(v.macroName);
  for (const auto &v : database.scopes) {
    unique.insert(v.name);
    unique.insert(v.definition);
  }
  for (const auto &v : database.toggleObjects)
    unique.insert(v.name);
  for (const auto &v : database.toggleDimensions)
    unique.insert(v.name);
  for (const auto &v : database.functionalTypes)
    unique.insert(v.name);
  for (const auto &v : database.functionalTypes)
    unique.insert(v.hierarchy);
  for (const auto &v : database.functionalItems) {
    unique.insert(v.name);
    unique.insert(v.hierarchy);
  }
  for (const auto &v : database.functionalBins) {
    unique.insert(v.name);
    unique.insert(v.hierarchy);
  }
  for (const auto &v : database.functionalFormals)
    unique.insert(v.name);
  for (const auto &v : database.resolvedFunctionalItems) {
    unique.insert(v.name);
    unique.insert(v.hierarchy);
  }
  for (const auto &v : database.resolvedFunctionalBins) {
    unique.insert(v.name);
    unique.insert(v.hierarchy);
  }
  for (const auto &v : database.functionalConfigurationOptions)
    unique.insert(v.stringValue);
  for (const auto &v : database.functionalSourceRanges)
    unique.insert(v.macroName);
  for (const auto &v : database.exclusions)
    unique.insert(v.reason);
  for (const auto &v : database.runs) {
    unique.insert(v.name);
    for (const auto &tag : v.tags) {
      unique.insert(tag.first);
      unique.insert(tag.second);
    }
  }
  for (const auto &v : database.resolvedInstances)
    unique.insert(v.name);
  for (const auto &v : database.resolvedInstanceOptions)
    unique.insert(v.stringValue);
  for (const auto &v : database.illegalBinDiagnostics)
    unique.insert(v.message);
  result.strings.assign(unique.begin(), unique.end());
  if (result.strings.size() > UINT32_MAX)
    return Status::IntegerOverflow;
  for (size_t i = 0; i != result.strings.size(); ++i)
    result.indexes.emplace(result.strings[i], static_cast<uint32_t>(i));
  return Status::Ok;
}

struct EncodedSection {
  SectionKind kind;
  uint32_t flags = 0;
  uint64_t count = 0;
  uint64_t size = 0;
};

template <typename T, typename Compare>
std::vector<T> sorted(const std::vector<T> &input, Compare compare) {
  std::vector<T> result = input;
  std::sort(result.begin(), result.end(), compare);
  return result;
}

Status planSections(const Database &db, const StringTable &strings,
                    std::vector<EncodedSection> &sections) {
  sections.clear();
  if (strings.strings.size() > UINT32_MAX)
    return Status::IntegerOverflow;
  uint64_t stringBytes = 0;
  for (const std::string &text : strings.strings) {
    if (text.size() > UINT32_MAX)
      return Status::IntegerOverflow;
    uint64_t entryBytes = 0;
    if (addOverflow(uint64_t{4}, text.size(), entryBytes) ||
        entryBytes > UINT64_MAX - 3 ||
        addOverflow(stringBytes, (entryBytes + 3) & ~uint64_t{3}, stringBytes))
      return Status::IntegerOverflow;
  }
  uint64_t tagCount = 0;
  for (const Run &run : db.runs) {
    if (run.tags.size() > UINT32_MAX ||
        addOverflow(tagCount, run.tags.size(), tagCount))
      return Status::IntegerOverflow;
  }
  if (tagCount == UINT64_MAX)
    return Status::IntegerOverflow;
  auto add = [&](SectionKind kind, uint64_t count, bool required = false) {
    const RecordDescriptor *record = nullptr;
    for (const RecordDescriptor &candidate : ParserRecordDescriptors)
      if (candidate.section == kind) {
        record = &candidate;
        break;
      }
    uint64_t bytes = 0;
    if ((kind != SectionKind::Strings && !record) ||
        (record && mulOverflow(count, record->size, bytes)))
      return false;
    if (kind == SectionKind::Strings)
      bytes = stringBytes;
    sections.push_back({kind, required ? RequiredSection : 0, count, bytes});
    return true;
  };
#define OBELISK_COVERAGE_PLAN(KIND, COUNT)                                     \
  if (!add(SectionKind::KIND, (COUNT)))                                        \
  return Status::IntegerOverflow
  if (!add(SectionKind::Strings, strings.strings.size(), true))
    return Status::IntegerOverflow;
  OBELISK_COVERAGE_PLAN(SourceFiles, db.sourceFiles.size());
  OBELISK_COVERAGE_PLAN(Scopes, db.scopes.size());
  OBELISK_COVERAGE_PLAN(LinePoints, db.linePoints.size());
  OBELISK_COVERAGE_PLAN(ToggleObjects, db.toggleObjects.size());
  OBELISK_COVERAGE_PLAN(ToggleTypeDimensions, db.toggleDimensions.size());
  OBELISK_COVERAGE_PLAN(FunctionalTypes, db.functionalTypes.size());
  OBELISK_COVERAGE_PLAN(FunctionalItems, db.functionalItems.size());
  OBELISK_COVERAGE_PLAN(FunctionalBins, db.functionalBins.size());
  OBELISK_COVERAGE_PLAN(TransitionPrograms, db.transitionPrograms.size());
  OBELISK_COVERAGE_PLAN(CrossPlans, db.crossPlans.size());
  OBELISK_COVERAGE_PLAN(Exclusions, db.exclusions.size());
  OBELISK_COVERAGE_PLAN(Runs, db.runs.size());
  OBELISK_COVERAGE_PLAN(ResolvedCovergroupInstances,
                        db.resolvedInstances.size());
  OBELISK_COVERAGE_PLAN(Counters, db.counters.size());
  OBELISK_COVERAGE_PLAN(SparseCrossTuples, db.sparseCrossTuples.size());
  OBELISK_COVERAGE_PLAN(IllegalBinDiagnostics, db.illegalBinDiagnostics.size());
  OBELISK_COVERAGE_PLAN(UserMetadata, tagCount + 1);
  OBELISK_COVERAGE_PLAN(FunctionalSourceRanges,
                        db.functionalSourceRanges.size());
  OBELISK_COVERAGE_PLAN(TransitionAlternatives,
                        db.transitionAlternatives.size());
  OBELISK_COVERAGE_PLAN(TransitionSteps, db.transitionSteps.size());
  OBELISK_COVERAGE_PLAN(CrossTargets, db.crossTargets.size());
  OBELISK_COVERAGE_PLAN(CrossBins, db.crossBins.size());
  OBELISK_COVERAGE_PLAN(CrossSelectorNodes, db.crossSelectorNodes.size());
  OBELISK_COVERAGE_PLAN(CrossSelectorOperands, db.crossSelectorOperands.size());
  OBELISK_COVERAGE_PLAN(SparseCrossTupleComponents,
                        db.sparseCrossTupleComponents.size());
  OBELISK_COVERAGE_PLAN(FunctionalValueSets, db.functionalValueSets.size());
  OBELISK_COVERAGE_PLAN(FunctionalValueAtoms, db.functionalValueAtoms.size());
  OBELISK_COVERAGE_PLAN(FunctionalValueLimbs, db.functionalValueLimbs.size());
  OBELISK_COVERAGE_PLAN(FunctionalExpressions, db.functionalExpressions.size());
  OBELISK_COVERAGE_PLAN(FunctionalConfigurations,
                        db.functionalConfigurations.size());
  OBELISK_COVERAGE_PLAN(ResolvedFunctionalItems,
                        db.resolvedFunctionalItems.size());
  OBELISK_COVERAGE_PLAN(ResolvedFunctionalBins,
                        db.resolvedFunctionalBins.size());
  OBELISK_COVERAGE_PLAN(ResolvedTransitionSteps,
                        db.resolvedTransitionSteps.size());
  OBELISK_COVERAGE_PLAN(ResolvedCrossPlans, db.resolvedCrossPlans.size());
  OBELISK_COVERAGE_PLAN(ResolvedCrossSelectorBindings,
                        db.resolvedCrossSelectorBindings.size());
  OBELISK_COVERAGE_PLAN(ResolvedFunctionalValueSets,
                        db.resolvedFunctionalValueSets.size());
  OBELISK_COVERAGE_PLAN(ResolvedFunctionalValueAtoms,
                        db.resolvedFunctionalValueAtoms.size());
  OBELISK_COVERAGE_PLAN(ResolvedFunctionalValueLimbs,
                        db.resolvedFunctionalValueLimbs.size());
  OBELISK_COVERAGE_PLAN(FunctionalBinPlans, db.functionalBinPlans.size());
  OBELISK_COVERAGE_PLAN(FunctionalTupleSets, db.functionalTupleSets.size());
  OBELISK_COVERAGE_PLAN(FunctionalTupleSetTuples,
                        db.functionalTupleSetTuples.size());
  OBELISK_COVERAGE_PLAN(FunctionalTupleSetComponents,
                        db.functionalTupleSetComponents.size());
  OBELISK_COVERAGE_PLAN(FunctionalOptionPlans, db.functionalOptionPlans.size());
  OBELISK_COVERAGE_PLAN(FunctionalConfigurationOptions,
                        db.functionalConfigurationOptions.size());
  OBELISK_COVERAGE_PLAN(ResolvedFunctionalBinPlans,
                        db.resolvedFunctionalBinPlans.size());
  OBELISK_COVERAGE_PLAN(ResolvedFunctionalTupleSets,
                        db.resolvedFunctionalTupleSets.size());
  OBELISK_COVERAGE_PLAN(ResolvedFunctionalTupleSetTuples,
                        db.resolvedFunctionalTupleSetTuples.size());
  OBELISK_COVERAGE_PLAN(ResolvedFunctionalTupleSetComponents,
                        db.resolvedFunctionalTupleSetComponents.size());
  OBELISK_COVERAGE_PLAN(ResolvedTransitionAlternatives,
                        db.resolvedTransitionAlternatives.size());
  OBELISK_COVERAGE_PLAN(ResolvedFunctionalBinGroups,
                        db.resolvedFunctionalBinGroups.size());
  OBELISK_COVERAGE_PLAN(ResolvedTransitionExpansionGroups,
                        db.resolvedTransitionExpansionGroups.size());
  OBELISK_COVERAGE_PLAN(ResolvedCrossAutomaticBinCountLimbs,
                        db.resolvedCrossAutomaticBinCountLimbs.size());
  OBELISK_COVERAGE_PLAN(ResolvedCrossAutomaticNodes,
                        db.resolvedCrossAutomaticNodes.size());
  OBELISK_COVERAGE_PLAN(ResolvedCrossAutomaticEdges,
                        db.resolvedCrossAutomaticEdges.size());
  OBELISK_COVERAGE_PLAN(FunctionalFormals, db.functionalFormals.size());
  OBELISK_COVERAGE_PLAN(ResolvedInstanceOptions,
                        db.resolvedInstanceOptions.size());
#undef OBELISK_COVERAGE_PLAN
  return Status::Ok;
}

Status encodeSections(const Database &db, const StringTable &strings,
                      WriteCallback callback, void *user,
                      const std::vector<EncodedSection> &sections,
                      SectionKind *failedSection = nullptr,
                      uint64_t *failedRecord = nullptr,
                      const char **failedField = nullptr) {
  size_t sectionIndex = 0;
  Bytes current;
  bool haveCurrent = false;
  bool allGood = true;
  Status failure = Status::Ok;
  SectionKind currentKind = SectionKind::Strings;
  auto finish = [&]() {
    if (!haveCurrent)
      return true;
    if (!current.good) {
      allGood = false;
      if (failure == Status::Ok) {
        failure = current.status;
        if (failedRecord)
          *failedRecord = current.recordIndex;
        if (failedField)
          *failedField = current.field;
      }
      if (failedSection)
        *failedSection = currentKind;
      return false;
    }
    if (sectionIndex == 0 || sections[sectionIndex - 1].size != current.size) {
      allGood = false;
      if (failure == Status::Ok)
        failure = Status::InvalidDatabase;
      if (failedSection)
        *failedSection = currentKind;
      return false;
    }
    if (align8(current.size) == UINT64_MAX) {
      allGood = false;
      if (failure == Status::Ok)
        failure = Status::IntegerOverflow;
      return false;
    }
    size_t padding = static_cast<size_t>(align8(current.size) - current.size);
    current.zero(padding);
    if (!current.good) {
      allGood = false;
      if (failure == Status::Ok)
        failure = current.status;
      if (failedSection)
        *failedSection = currentKind;
      return false;
    }
    return true;
  };
  auto add = [&](SectionKind kind, uint64_t count,
                 bool required = false) -> Bytes & {
    finish();
    uint32_t flags = required ? RequiredSection : 0;
    if (sectionIndex >= sections.size() ||
        sections[sectionIndex].kind != kind ||
        sections[sectionIndex].flags != flags ||
        sections[sectionIndex].count != count) {
      allGood = false;
      if (failedSection)
        *failedSection = kind;
      if (failure == Status::Ok)
        failure = Status::InvalidDatabase;
    }
    currentKind = kind;
    ++sectionIndex;
    haveCurrent = true;
    current.reset(allGood ? callback : nullptr, user);
    return current;
  };
  Bytes &stringBytes = add(SectionKind::Strings, strings.strings.size(), true);
  for (const std::string &text : strings.strings) {
    stringBytes.u32(static_cast<uint32_t>(text.size()));
    stringBytes.raw(reinterpret_cast<const uint8_t *>(text.data()),
                    text.size());
    stringBytes.zero(static_cast<size_t>((4 - stringBytes.size % 4) % 4));
  }
  const auto &files = db.sourceFiles;
  Bytes &fb = add(SectionKind::SourceFiles, files.size());
  for (const auto &v : files)
    fb.record<SourceFileRecord>([&](uint8_t *p) {
      SourceFileRecord::setId(p, v.id);
      SourceFileRecord::setPath(p, strings.get(v.path));
      SourceFileRecord::setDigest(p, v.digest.data());
    });
  const auto &scopes = db.scopes;
  Bytes &sb = add(SectionKind::Scopes, scopes.size());
  for (const auto &v : scopes)
    sb.record<ScopeRecord>([&](uint8_t *p) {
      ScopeRecord::setId(p, v.id);
      ScopeRecord::setParent(p, v.parent);
      ScopeRecord::setName(p, strings.get(v.name));
      ScopeRecord::setKind(p, v.kind);
      ScopeRecord::setDefinition(p, strings.get(v.definition));
    });
  const auto &points = db.linePoints;
  Bytes &lp = add(SectionKind::LinePoints, points.size());
  for (const auto &v : points)
    lp.record<LinePointRecord>([&](uint8_t *p) {
      LinePointRecord::setId(p, v.id);
      LinePointRecord::setFile(p, v.file);
      LinePointRecord::setEndFile(p, v.endFile);
      LinePointRecord::setScope(p, v.scope);
      LinePointRecord::setMacroName(p, strings.get(v.macroName));
      LinePointRecord::setLine(p, v.line);
      LinePointRecord::setColumn(p, v.column);
      LinePointRecord::setEndLine(p, v.endLine);
      LinePointRecord::setEndColumn(p, v.endColumn);
      LinePointRecord::setSemanticPhase(p, v.semanticPhase);
      LinePointRecord::setFlags(p, v.flags);
    });
  const auto &objects = db.toggleObjects;
  Bytes &to = add(SectionKind::ToggleObjects, objects.size());
  for (const auto &v : objects)
    to.record<ToggleObjectRecord>([&](uint8_t *p) {
      ToggleObjectRecord::setId(p, v.id);
      ToggleObjectRecord::setScope(p, v.scope);
      ToggleObjectRecord::setFile(p, v.file);
      ToggleObjectRecord::setName(p, strings.get(v.name));
      ToggleObjectRecord::setTypeRoot(p, v.typeRoot);
      ToggleObjectRecord::setBitWidth(p, v.bitWidth);
      ToggleObjectRecord::setLine(p, v.line);
      ToggleObjectRecord::setColumn(p, v.column);
      ToggleObjectRecord::setEndLine(p, v.endLine);
      ToggleObjectRecord::setEndColumn(p, v.endColumn);
      ToggleObjectRecord::setFlags(p, v.flags);
    });
  // Dimension indices are physical parent/child references. Preparation
  // emits a canonical object-ID/preorder sequence, so preserve it verbatim.
  Bytes &td =
      add(SectionKind::ToggleTypeDimensions, db.toggleDimensions.size());
  for (const auto &v : db.toggleDimensions)
    td.record<ToggleDimensionRecord>([&](uint8_t *p) {
      ToggleDimensionRecord::setObject(p, v.object);
      ToggleDimensionRecord::setParent(p, v.parent);
      ToggleDimensionRecord::setKind(p, static_cast<uint32_t>(v.kind));
      ToggleDimensionRecord::setFirstChild(p, v.firstChild);
      ToggleDimensionRecord::setChildCount(p, v.childCount);
      ToggleDimensionRecord::setLeft(p, v.left);
      ToggleDimensionRecord::setRight(p, v.right);
      ToggleDimensionRecord::setBitOffset(p, v.bitOffset);
      ToggleDimensionRecord::setBitWidth(p, v.bitWidth);
      ToggleDimensionRecord::setName(p, strings.get(v.name));
      ToggleDimensionRecord::setFlags(p, v.flags);
    });
  const auto &types = db.functionalTypes;
  Bytes &ft = add(SectionKind::FunctionalTypes, types.size());
  for (const auto &v : types)
    ft.record<FunctionalTypeRecord>([&](uint8_t *p) {
      FunctionalTypeRecord::setId(p, v.id);
      FunctionalTypeRecord::setScope(p, v.scope);
      FunctionalTypeRecord::setName(p, strings.get(v.name));
      FunctionalTypeRecord::setFlags(p, v.flags);
      FunctionalTypeRecord::setLanguageVersion(p, v.languageVersion);
      FunctionalTypeRecord::setHierarchy(p, strings.get(v.hierarchy));
    });
  const auto &items = db.functionalItems;
  Bytes &fi = add(SectionKind::FunctionalItems, items.size());
  for (const auto &v : items)
    fi.record<FunctionalItemRecord>([&](uint8_t *p) {
      FunctionalItemRecord::setId(p, v.id);
      FunctionalItemRecord::setType(p, v.type);
      FunctionalItemRecord::setName(p, strings.get(v.name));
      FunctionalItemRecord::setKind(p, static_cast<uint32_t>(v.kind));
      FunctionalItemRecord::setFlags(p, v.flags);
      FunctionalItemRecord::setGoal(p, v.goal);
      FunctionalItemRecord::setWeight(p, v.weight);
      FunctionalItemRecord::setOrdinal(p, v.ordinal);
      FunctionalItemRecord::setHierarchy(p, strings.get(v.hierarchy));
    });
  const auto &bins = db.functionalBins;
  Bytes &bn = add(SectionKind::FunctionalBins, bins.size());
  for (const auto &v : bins)
    bn.record<FunctionalBinRecord>([&](uint8_t *p) {
      FunctionalBinRecord::setId(p, v.id);
      FunctionalBinRecord::setItem(p, v.item);
      FunctionalBinRecord::setName(p, strings.get(v.name));
      FunctionalBinRecord::setKind(p, static_cast<uint32_t>(v.kind));
      FunctionalBinRecord::setFlags(p, v.flags);
      FunctionalBinRecord::setOrdinal(p, v.ordinal);
      FunctionalBinRecord::setAtLeast(p, v.atLeast);
      FunctionalBinRecord::setHierarchy(p, strings.get(v.hierarchy));
    });
  Bytes &tp =
      add(SectionKind::TransitionPrograms, db.transitionPrograms.size());
  for (const auto &v : db.transitionPrograms) {
    tp.record<TransitionProgramRecord>([&](uint8_t *p) {
      TransitionProgramRecord::setBin(p, v.bin);
      TransitionProgramRecord::setItem(p, v.item);
      TransitionProgramRecord::setFirstAlternative(p, v.firstAlternative);
      TransitionProgramRecord::setAlternativeCount(p, v.alternativeCount);
      TransitionProgramRecord::setFlags(p, v.flags);
    });
  }
  Bytes &cp = add(SectionKind::CrossPlans, db.crossPlans.size());
  for (const auto &v : db.crossPlans) {
    cp.record<CrossPlanRecord>([&](uint8_t *p) {
      CrossPlanRecord::setItem(p, v.item);
      CrossPlanRecord::setFirstTarget(p, v.firstTarget);
      CrossPlanRecord::setTargetCount(p, v.targetCount);
      CrossPlanRecord::setFirstBin(p, v.firstBin);
      CrossPlanRecord::setBinCount(p, v.binCount);
      CrossPlanRecord::setRetainAutoPolicy(
          p, static_cast<uint32_t>(v.retainAutoPolicy));
      CrossPlanRecord::setFlags(p, v.flags);
      CrossPlanRecord::setIffExpression(p, v.iffExpression);
      CrossPlanRecord::setTupleElementType(p, v.tupleElementType);
      CrossPlanRecord::setTupleProvenanceSpan(p, v.tupleProvenanceSpan);
      CrossPlanRecord::setTupleFlags(p, v.tupleFlags);
    });
  }
  const auto &exclusions = db.exclusions;
  Bytes &ex = add(SectionKind::Exclusions, exclusions.size());
  for (const auto &v : exclusions)
    ex.record<ExclusionRecord>([&](uint8_t *p) {
      ExclusionRecord::setEntity(p, v.entity);
      ExclusionRecord::setMetric(p, static_cast<uint32_t>(v.metric));
      ExclusionRecord::setReason(p, strings.get(v.reason));
      ExclusionRecord::setFlags(p, v.flags);
    });
  const auto &runs = db.runs;
  Bytes &rn = add(SectionKind::Runs, runs.size());
  for (const auto &v : runs)
    rn.record<RunRecord>([&](uint8_t *p) {
      RunRecord::setUUID(p, v.uuid.data());
      RunRecord::setName(p, strings.get(v.name));
      RunRecord::setStatus(p, v.status);
      RunRecord::setSimulationTime(p, v.simulationTime);
      RunRecord::setTimestamp(p, v.timestamp);
      RunRecord::setSeed(p, v.seed);
      RunRecord::setTagCount(p, static_cast<uint32_t>(v.tags.size()));
      RunRecord::setFlags(p, v.flags);
    });
  const auto &instances = db.resolvedInstances;
  Bytes &ri = add(SectionKind::ResolvedCovergroupInstances, instances.size());
  for (const auto &v : instances)
    ri.record<ResolvedInstanceRecord>([&](uint8_t *p) {
      ResolvedInstanceRecord::setRun(p, v.run.data());
      ResolvedInstanceRecord::setType(p, v.type);
      ResolvedInstanceRecord::setId(p, v.id);
      ResolvedInstanceRecord::setName(p, strings.get(v.name));
      ResolvedInstanceRecord::setFlags(p, v.flags);
      ResolvedInstanceRecord::setConfiguration(p, v.configuration.data());
    });
  const auto &counters = db.counters;
  Bytes &cn = add(SectionKind::Counters, counters.size());
  for (const auto &v : counters)
    cn.record<CounterRecord>([&](uint8_t *p) {
      CounterRecord::setRun(p, v.run.data());
      CounterRecord::setEntity(p, v.entity);
      CounterRecord::setInstance(p, v.instance);
      CounterRecord::setValue(p, v.value);
      CounterRecord::setMetric(p, static_cast<uint32_t>(v.metric));
      CounterRecord::setSubindex(p, v.subindex);
      CounterRecord::setFlags(p, v.flags);
    });
  Bytes &sx = add(SectionKind::SparseCrossTuples, db.sparseCrossTuples.size());
  for (const auto &v : db.sparseCrossTuples) {
    sx.record<SparseCrossTupleRecord>([&](uint8_t *p) {
      SparseCrossTupleRecord::setRun(p, v.run.data());
      SparseCrossTupleRecord::setInstance(p, v.instance);
      SparseCrossTupleRecord::setCross(p, v.cross);
      SparseCrossTupleRecord::setFirstComponent(p, v.firstComponent);
      SparseCrossTupleRecord::setComponentCount(p, v.componentCount);
      SparseCrossTupleRecord::setHits(p, v.hits);
      SparseCrossTupleRecord::setFlags(p, v.flags);
    });
  }
  const auto &illegal = db.illegalBinDiagnostics;
  Bytes &ib = add(SectionKind::IllegalBinDiagnostics, illegal.size());
  for (const auto &v : illegal) {
    ib.record<IllegalBinDiagnosticRecord>([&](uint8_t *p) {
      IllegalBinDiagnosticRecord::setRun(p, v.run.data());
      IllegalBinDiagnosticRecord::setBin(p, v.bin);
      IllegalBinDiagnosticRecord::setInstance(p, v.instance);
      IllegalBinDiagnosticRecord::setSimulationTime(p, v.simulationTime);
      IllegalBinDiagnosticRecord::setCount(p, v.count);
      IllegalBinDiagnosticRecord::setMessage(p, strings.get(v.message));
      IllegalBinDiagnosticRecord::setFlags(p, v.flags);
    });
  }
  uint64_t tagCount = 0;
  for (const auto &r : runs)
    tagCount += r.tags.size();
  Bytes &md = add(SectionKind::UserMetadata, tagCount + 1);
  UUID zero{};
  md.record<UserMetadataRecord>([&](uint8_t *p) {
    UserMetadataRecord::setRun(p, zero.data());
    UserMetadataRecord::setKey(p, strings.get(""));
    UserMetadataRecord::setValue(p, strings.get(db.producer));
  });
  for (const auto &r : runs) {
    for (const auto &t : r.tags) {
      md.record<UserMetadataRecord>([&](uint8_t *p) {
        UserMetadataRecord::setRun(p, r.uuid.data());
        UserMetadataRecord::setKey(p, strings.get(t.first));
        UserMetadataRecord::setValue(p, strings.get(t.second));
      });
    }
  }
  const auto &functionalSources = db.functionalSourceRanges;
  Bytes &fs =
      add(SectionKind::FunctionalSourceRanges, functionalSources.size());
  for (const auto &v : functionalSources)
    fs.record<FunctionalSourceRangeRecord>([&](uint8_t *p) {
      FunctionalSourceRangeRecord::setBin(p, v.bin);
      FunctionalSourceRangeRecord::setRole(p, static_cast<uint32_t>(v.role));
      FunctionalSourceRangeRecord::setOrdinal(p, v.ordinal);
      FunctionalSourceRangeRecord::setFile(p, v.file);
      FunctionalSourceRangeRecord::setEndFile(p, v.endFile);
      FunctionalSourceRangeRecord::setMacroName(p, strings.get(v.macroName));
      FunctionalSourceRangeRecord::setLine(p, v.line);
      FunctionalSourceRangeRecord::setColumn(p, v.column);
      FunctionalSourceRangeRecord::setEndLine(p, v.endLine);
      FunctionalSourceRangeRecord::setEndColumn(p, v.endColumn);
      FunctionalSourceRangeRecord::setFlags(p, v.flags);
    });
  Bytes &alternatives = add(SectionKind::TransitionAlternatives,
                            db.transitionAlternatives.size());
  for (const auto &v : db.transitionAlternatives) {
    alternatives.record<TransitionAlternativeRecord>([&](uint8_t *p) {
      TransitionAlternativeRecord::setBin(p, v.bin);
      TransitionAlternativeRecord::setTerminalValueSet(p, v.terminalValueSet);
      TransitionAlternativeRecord::setFirstStep(p, v.firstStep);
      TransitionAlternativeRecord::setStepCount(p, v.stepCount);
      TransitionAlternativeRecord::setOrdinal(p, v.ordinal);
      TransitionAlternativeRecord::setFlags(p, v.flags);
    });
  }
  Bytes &steps = add(SectionKind::TransitionSteps, db.transitionSteps.size());
  for (const auto &v : db.transitionSteps) {
    steps.record<TransitionStepRecord>([&](uint8_t *p) {
      TransitionStepRecord::setBin(p, v.bin);
      TransitionStepRecord::setValueSet(p, v.valueSet);
      TransitionStepRecord::setLowerExpression(p, v.lowerExpression);
      TransitionStepRecord::setUpperExpression(p, v.upperExpression);
      TransitionStepRecord::setLowerBound(p, v.lowerBound);
      TransitionStepRecord::setUpperBound(p, v.upperBound);
      TransitionStepRecord::setAlternativeOrdinal(p, v.alternativeOrdinal);
      TransitionStepRecord::setOrdinal(p, v.ordinal);
      TransitionStepRecord::setRepetition(p,
                                          static_cast<uint32_t>(v.repetition));
      TransitionStepRecord::setFlags(p, v.flags);
    });
  }
  Bytes &targets = add(SectionKind::CrossTargets, db.crossTargets.size());
  for (const auto &v : db.crossTargets) {
    targets.record<CrossTargetRecord>([&](uint8_t *p) {
      CrossTargetRecord::setCross(p, v.cross);
      CrossTargetRecord::setTarget(p, v.target);
      CrossTargetRecord::setOrdinal(p, v.ordinal);
      CrossTargetRecord::setTupleBitOffset(p, v.tupleBitOffset);
      CrossTargetRecord::setTupleBitWidth(p, v.tupleBitWidth);
      CrossTargetRecord::setTupleResultKind(
          p, static_cast<uint32_t>(v.tupleResultKind));
      CrossTargetRecord::setTupleSignedness(
          p, static_cast<uint32_t>(v.tupleSignedness));
      CrossTargetRecord::setTupleFlags(p, v.tupleFlags);
    });
  }
  Bytes &crossBins = add(SectionKind::CrossBins, db.crossBins.size());
  for (const auto &v : db.crossBins) {
    crossBins.record<CrossBinRecord>([&](uint8_t *p) {
      CrossBinRecord::setBin(p, v.bin);
      CrossBinRecord::setCross(p, v.cross);
      CrossBinRecord::setRootSelector(p, v.rootSelector);
      CrossBinRecord::setFlags(p, v.flags);
    });
  }
  Bytes &nodes =
      add(SectionKind::CrossSelectorNodes, db.crossSelectorNodes.size());
  for (const auto &v : db.crossSelectorNodes) {
    nodes.record<CrossSelectorNodeRecord>([&](uint8_t *p) {
      CrossSelectorNodeRecord::setId(p, v.id);
      CrossSelectorNodeRecord::setCross(p, v.cross);
      CrossSelectorNodeRecord::setTarget(p, v.target);
      CrossSelectorNodeRecord::setBin(p, v.bin);
      CrossSelectorNodeRecord::setValueSet(p, v.valueSet);
      CrossSelectorNodeRecord::setWithExpression(p, v.withExpression);
      CrossSelectorNodeRecord::setConstructionExpression(
          p, v.constructionExpression);
      CrossSelectorNodeRecord::setTupleSet(p, v.tupleSet);
      CrossSelectorNodeRecord::setMatchesExpression(p, v.matchesExpression);
      CrossSelectorNodeRecord::setFirstOperand(p, v.firstOperand);
      CrossSelectorNodeRecord::setOperandCount(p, v.operandCount);
      CrossSelectorNodeRecord::setKind(p, static_cast<uint32_t>(v.kind));
      CrossSelectorNodeRecord::setOrdinal(p, v.ordinal);
      CrossSelectorNodeRecord::setFlags(p, v.flags);
      CrossSelectorNodeRecord::setMatchesPolicy(
          p, static_cast<uint32_t>(v.matchesPolicy));
      CrossSelectorNodeRecord::setMatchesCount(p, v.matchesCount);
    });
  }
  Bytes &operands =
      add(SectionKind::CrossSelectorOperands, db.crossSelectorOperands.size());
  for (const auto &v : db.crossSelectorOperands) {
    operands.record<CrossSelectorOperandRecord>([&](uint8_t *p) {
      CrossSelectorOperandRecord::setNode(p, v.node);
      CrossSelectorOperandRecord::setOperand(p, v.operand);
      CrossSelectorOperandRecord::setOrdinal(p, v.ordinal);
    });
  }
  Bytes &tupleComponents = add(SectionKind::SparseCrossTupleComponents,
                               db.sparseCrossTupleComponents.size());
  for (uint64_t bin : db.sparseCrossTupleComponents)
    tupleComponents.record<SparseCrossTupleComponentRecord>(
        [&](uint8_t *p) { SparseCrossTupleComponentRecord::setBin(p, bin); });
  Bytes &valueSets =
      add(SectionKind::FunctionalValueSets, db.functionalValueSets.size());
  for (const auto &v : db.functionalValueSets) {
    valueSets.record<FunctionalValueSetRecord>([&](uint8_t *p) {
      FunctionalValueSetRecord::setId(p, v.id);
      FunctionalValueSetRecord::setItem(p, v.item);
      FunctionalValueSetRecord::setFirstAtom(p, v.firstAtom);
      FunctionalValueSetRecord::setAtomCount(p, v.atomCount);
      FunctionalValueSetRecord::setBitWidth(p, v.bitWidth);
      FunctionalValueSetRecord::setKind(p, static_cast<uint32_t>(v.kind));
      FunctionalValueSetRecord::setFlags(p, v.flags);
      FunctionalValueSetRecord::setSignedness(
          p, static_cast<uint32_t>(v.signedness));
      FunctionalValueSetRecord::setSetExpression(p, v.setExpression);
    });
  }
  Bytes &atoms =
      add(SectionKind::FunctionalValueAtoms, db.functionalValueAtoms.size());
  for (const auto &v : db.functionalValueAtoms) {
    atoms.record<FunctionalValueAtomRecord>([&](uint8_t *p) {
      FunctionalValueAtomRecord::setValueSet(p, v.valueSet);
      FunctionalValueAtomRecord::setRealLowBits(p, v.realLowBits);
      FunctionalValueAtomRecord::setRealHighBits(p, v.realHighBits);
      FunctionalValueAtomRecord::setFirstLimb(p, v.firstLimb);
      FunctionalValueAtomRecord::setLimbCount(p, v.limbCount);
      FunctionalValueAtomRecord::setOrdinal(p, v.ordinal);
      FunctionalValueAtomRecord::setKind(p, static_cast<uint32_t>(v.kind));
      FunctionalValueAtomRecord::setFlags(p, v.flags);
      FunctionalValueAtomRecord::setLowerExpression(p, v.lowerExpression);
      FunctionalValueAtomRecord::setUpperExpression(p, v.upperExpression);
    });
  }
  Bytes &limbs =
      add(SectionKind::FunctionalValueLimbs, db.functionalValueLimbs.size());
  for (const auto &v : db.functionalValueLimbs) {
    limbs.record<FunctionalValueLimbRecord>([&](uint8_t *p) {
      FunctionalValueLimbRecord::setValueSet(p, v.valueSet);
      FunctionalValueLimbRecord::setAtomOrdinal(p, v.atomOrdinal);
      FunctionalValueLimbRecord::setOrdinal(p, v.ordinal);
      FunctionalValueLimbRecord::setLowAval(p, v.lowAval);
      FunctionalValueLimbRecord::setLowBval(p, v.lowBval);
      FunctionalValueLimbRecord::setHighAval(p, v.highAval);
      FunctionalValueLimbRecord::setHighBval(p, v.highBval);
      FunctionalValueLimbRecord::setWildcardMask(p, v.wildcardMask);
    });
  }
  Bytes &expressions =
      add(SectionKind::FunctionalExpressions, db.functionalExpressions.size());
  for (const auto &v : db.functionalExpressions) {
    expressions.record<FunctionalExpressionRecord>([&](uint8_t *p) {
      FunctionalExpressionRecord::setId(p, v.id);
      FunctionalExpressionRecord::setOwner(p, v.owner);
      FunctionalExpressionRecord::setOwnerKind(
          p, static_cast<uint32_t>(v.ownerKind));
      FunctionalExpressionRecord::setRole(p, static_cast<uint32_t>(v.role));
      FunctionalExpressionRecord::setResultKind(
          p, static_cast<uint32_t>(v.resultKind));
      FunctionalExpressionRecord::setFlags(p, v.flags);
      FunctionalExpressionRecord::setSemanticDigest(p, v.semanticDigest.data());
      FunctionalExpressionRecord::setBitWidth(p, v.bitWidth);
      FunctionalExpressionRecord::setSignedness(
          p, static_cast<uint32_t>(v.signedness));
      FunctionalExpressionRecord::setOwnerOrdinal(p, v.ownerOrdinal);
      FunctionalExpressionRecord::setOwnerSubordinal(p, v.ownerSubordinal);
      FunctionalExpressionRecord::setEvaluationPhase(
          p, static_cast<uint32_t>(v.evaluationPhase));
      FunctionalExpressionRecord::setResultOrdinal(p, v.resultOrdinal);
    });
  }
  Bytes &configurations = add(SectionKind::FunctionalConfigurations,
                              db.functionalConfigurations.size());
  for (const auto &v : db.functionalConfigurations) {
    configurations.record<FunctionalConfigurationRecord>([&](uint8_t *p) {
      FunctionalConfigurationRecord::setType(p, v.type);
      FunctionalConfigurationRecord::setConfiguration(p,
                                                      v.configuration.data());
      FunctionalConfigurationRecord::setFlags(p, v.flags);
    });
  }
  Bytes &resolvedItems = add(SectionKind::ResolvedFunctionalItems,
                             db.resolvedFunctionalItems.size());
  for (const auto &v : db.resolvedFunctionalItems) {
    resolvedItems.record<ResolvedFunctionalItemRecord>([&](uint8_t *p) {
      ResolvedFunctionalItemRecord::setType(p, v.type);
      ResolvedFunctionalItemRecord::setConfiguration(p, v.configuration.data());
      ResolvedFunctionalItemRecord::setId(p, v.id);
      ResolvedFunctionalItemRecord::setTemplateItem(p, v.templateItem);
      ResolvedFunctionalItemRecord::setName(p, strings.get(v.name));
      ResolvedFunctionalItemRecord::setKind(p, static_cast<uint32_t>(v.kind));
      ResolvedFunctionalItemRecord::setFlags(p, v.flags);
      ResolvedFunctionalItemRecord::setGoal(p, v.goal);
      ResolvedFunctionalItemRecord::setWeight(p, v.weight);
      ResolvedFunctionalItemRecord::setOrdinal(p, v.ordinal);
      ResolvedFunctionalItemRecord::setHierarchy(p, strings.get(v.hierarchy));
    });
  }
  Bytes &resolvedBins = add(SectionKind::ResolvedFunctionalBins,
                            db.resolvedFunctionalBins.size());
  for (const auto &v : db.resolvedFunctionalBins) {
    resolvedBins.record<ResolvedFunctionalBinRecord>([&](uint8_t *p) {
      ResolvedFunctionalBinRecord::setType(p, v.type);
      ResolvedFunctionalBinRecord::setConfiguration(p, v.configuration.data());
      ResolvedFunctionalBinRecord::setId(p, v.id);
      ResolvedFunctionalBinRecord::setTemplateBin(p, v.templateBin);
      ResolvedFunctionalBinRecord::setItem(p, v.item);
      ResolvedFunctionalBinRecord::setName(p, strings.get(v.name));
      ResolvedFunctionalBinRecord::setKind(p, static_cast<uint32_t>(v.kind));
      ResolvedFunctionalBinRecord::setFlags(p, v.flags);
      ResolvedFunctionalBinRecord::setOrdinal(p, v.ordinal);
      ResolvedFunctionalBinRecord::setExpansionOrdinal(p, v.expansionOrdinal);
      ResolvedFunctionalBinRecord::setAtLeast(p, v.atLeast);
      ResolvedFunctionalBinRecord::setHierarchy(p, strings.get(v.hierarchy));
    });
  }
  Bytes &resolvedSteps = add(SectionKind::ResolvedTransitionSteps,
                             db.resolvedTransitionSteps.size());
  for (const auto &v : db.resolvedTransitionSteps) {
    resolvedSteps.record<ResolvedTransitionStepRecord>([&](uint8_t *p) {
      ResolvedTransitionStepRecord::setType(p, v.type);
      ResolvedTransitionStepRecord::setConfiguration(p, v.configuration.data());
      ResolvedTransitionStepRecord::setBin(p, v.bin);
      ResolvedTransitionStepRecord::setAlternative(p, v.alternative);
      ResolvedTransitionStepRecord::setValueSet(p, v.valueSet);
      ResolvedTransitionStepRecord::setLowerBound(p, v.lowerBound);
      ResolvedTransitionStepRecord::setUpperBound(p, v.upperBound);
      ResolvedTransitionStepRecord::setOrdinal(p, v.ordinal);
      ResolvedTransitionStepRecord::setFlags(p, v.flags);
    });
  }
  Bytes &resolvedCrosses =
      add(SectionKind::ResolvedCrossPlans, db.resolvedCrossPlans.size());
  for (const auto &v : db.resolvedCrossPlans) {
    resolvedCrosses.record<ResolvedCrossPlanRecord>([&](uint8_t *p) {
      ResolvedCrossPlanRecord::setType(p, v.type);
      ResolvedCrossPlanRecord::setConfiguration(p, v.configuration.data());
      ResolvedCrossPlanRecord::setCross(p, v.cross);
      ResolvedCrossPlanRecord::setRetainAutoPolicy(
          p, static_cast<uint32_t>(v.retainAutoPolicy));
      ResolvedCrossPlanRecord::setFlags(p, v.flags);
      ResolvedCrossPlanRecord::setFirstAutomaticBinCountLimb(
          p, v.firstAutomaticBinCountLimb);
      ResolvedCrossPlanRecord::setAutomaticBinCountLimbCount(
          p, v.automaticBinCountLimbCount);
      ResolvedCrossPlanRecord::setRootNode(p, v.rootNode);
    });
  }
  Bytes &bindings = add(SectionKind::ResolvedCrossSelectorBindings,
                        db.resolvedCrossSelectorBindings.size());
  for (const auto &v : db.resolvedCrossSelectorBindings) {
    bindings.record<ResolvedCrossSelectorBindingRecord>([&](uint8_t *p) {
      ResolvedCrossSelectorBindingRecord::setType(p, v.type);
      ResolvedCrossSelectorBindingRecord::setConfiguration(
          p, v.configuration.data());
      ResolvedCrossSelectorBindingRecord::setCross(p, v.cross);
      ResolvedCrossSelectorBindingRecord::setNode(p, v.node);
      ResolvedCrossSelectorBindingRecord::setValueSet(p, v.valueSet);
      ResolvedCrossSelectorBindingRecord::setWithExpression(p,
                                                            v.withExpression);
      ResolvedCrossSelectorBindingRecord::setTupleSet(p, v.tupleSet);
      ResolvedCrossSelectorBindingRecord::setMatchesPolicy(
          p, static_cast<uint32_t>(v.matchesPolicy));
      ResolvedCrossSelectorBindingRecord::setFlags(p, v.flags);
      ResolvedCrossSelectorBindingRecord::setMatchesCount(p, v.matchesCount);
    });
  }
  Bytes &resolvedValueSets = add(SectionKind::ResolvedFunctionalValueSets,
                                 db.resolvedFunctionalValueSets.size());
  for (const auto &v : db.resolvedFunctionalValueSets) {
    resolvedValueSets.record<ResolvedFunctionalValueSetRecord>([&](uint8_t *p) {
      ResolvedFunctionalValueSetRecord::setType(p, v.type);
      ResolvedFunctionalValueSetRecord::setConfiguration(
          p, v.configuration.data());
      ResolvedFunctionalValueSetRecord::setId(p, v.id);
      ResolvedFunctionalValueSetRecord::setTemplateValueSet(p,
                                                            v.templateValueSet);
      ResolvedFunctionalValueSetRecord::setItem(p, v.item);
      ResolvedFunctionalValueSetRecord::setFirstAtom(p, v.firstAtom);
      ResolvedFunctionalValueSetRecord::setAtomCount(p, v.atomCount);
      ResolvedFunctionalValueSetRecord::setBitWidth(p, v.bitWidth);
      ResolvedFunctionalValueSetRecord::setKind(p,
                                                static_cast<uint32_t>(v.kind));
      ResolvedFunctionalValueSetRecord::setFlags(p, v.flags);
      ResolvedFunctionalValueSetRecord::setSignedness(
          p, static_cast<uint32_t>(v.signedness));
      ResolvedFunctionalValueSetRecord::setOwnerBin(p, v.ownerBin);
      ResolvedFunctionalValueSetRecord::setOwnerSelector(p, v.ownerSelector);
      ResolvedFunctionalValueSetRecord::setOwnerOrdinal(p, v.ownerOrdinal);
      ResolvedFunctionalValueSetRecord::setOwnerSubordinal(p,
                                                           v.ownerSubordinal);
      ResolvedFunctionalValueSetRecord::setRole(p,
                                                static_cast<uint32_t>(v.role));
    });
  }
  Bytes &resolvedAtoms = add(SectionKind::ResolvedFunctionalValueAtoms,
                             db.resolvedFunctionalValueAtoms.size());
  for (const auto &v : db.resolvedFunctionalValueAtoms) {
    resolvedAtoms.record<ResolvedFunctionalValueAtomRecord>([&](uint8_t *p) {
      ResolvedFunctionalValueAtomRecord::setValueSet(p, v.valueSet);
      ResolvedFunctionalValueAtomRecord::setRealLowBits(p, v.realLowBits);
      ResolvedFunctionalValueAtomRecord::setRealHighBits(p, v.realHighBits);
      ResolvedFunctionalValueAtomRecord::setFirstLimb(p, v.firstLimb);
      ResolvedFunctionalValueAtomRecord::setLimbCount(p, v.limbCount);
      ResolvedFunctionalValueAtomRecord::setOrdinal(p, v.ordinal);
      ResolvedFunctionalValueAtomRecord::setKind(p,
                                                 static_cast<uint32_t>(v.kind));
      ResolvedFunctionalValueAtomRecord::setFlags(p, v.flags);
    });
  }
  Bytes &resolvedLimbs = add(SectionKind::ResolvedFunctionalValueLimbs,
                             db.resolvedFunctionalValueLimbs.size());
  for (const auto &v : db.resolvedFunctionalValueLimbs) {
    resolvedLimbs.record<ResolvedFunctionalValueLimbRecord>([&](uint8_t *p) {
      ResolvedFunctionalValueLimbRecord::setValueSet(p, v.valueSet);
      ResolvedFunctionalValueLimbRecord::setAtomOrdinal(p, v.atomOrdinal);
      ResolvedFunctionalValueLimbRecord::setOrdinal(p, v.ordinal);
      ResolvedFunctionalValueLimbRecord::setLowAval(p, v.lowAval);
      ResolvedFunctionalValueLimbRecord::setLowBval(p, v.lowBval);
      ResolvedFunctionalValueLimbRecord::setHighAval(p, v.highAval);
      ResolvedFunctionalValueLimbRecord::setHighBval(p, v.highBval);
      ResolvedFunctionalValueLimbRecord::setWildcardMask(p, v.wildcardMask);
    });
  }
  Bytes &binPlans =
      add(SectionKind::FunctionalBinPlans, db.functionalBinPlans.size());
  for (const auto &v : db.functionalBinPlans) {
    binPlans.record<FunctionalBinPlanRecord>([&](uint8_t *p) {
      FunctionalBinPlanRecord::setBin(p, v.bin);
      FunctionalBinPlanRecord::setValueSet(p, v.valueSet);
      FunctionalBinPlanRecord::setIffExpression(p, v.iffExpression);
      FunctionalBinPlanRecord::setCardinalityExpression(
          p, v.cardinalityExpression);
      FunctionalBinPlanRecord::setArrayCardinality(p, v.arrayCardinality);
      FunctionalBinPlanRecord::setArrayMode(p,
                                            static_cast<uint32_t>(v.arrayMode));
      FunctionalBinPlanRecord::setDistribution(
          p, static_cast<uint32_t>(v.distribution));
      FunctionalBinPlanRecord::setFlags(p, v.flags);
    });
  }
  Bytes &tupleSets =
      add(SectionKind::FunctionalTupleSets, db.functionalTupleSets.size());
  for (const auto &v : db.functionalTupleSets) {
    tupleSets.record<FunctionalTupleSetRecord>([&](uint8_t *p) {
      FunctionalTupleSetRecord::setId(p, v.id);
      FunctionalTupleSetRecord::setCross(p, v.cross);
      FunctionalTupleSetRecord::setSelector(p, v.selector);
      FunctionalTupleSetRecord::setFirstTuple(p, v.firstTuple);
      FunctionalTupleSetRecord::setTupleCount(p, v.tupleCount);
      FunctionalTupleSetRecord::setElementMode(
          p, static_cast<uint32_t>(v.elementMode));
      FunctionalTupleSetRecord::setFlags(p, v.flags);
    });
  }
  Bytes &tuples = add(SectionKind::FunctionalTupleSetTuples,
                      db.functionalTupleSetTuples.size());
  for (const auto &v : db.functionalTupleSetTuples) {
    tuples.record<FunctionalTupleRecord>([&](uint8_t *p) {
      FunctionalTupleRecord::setTupleSet(p, v.tupleSet);
      FunctionalTupleRecord::setId(p, v.id);
      FunctionalTupleRecord::setFirstComponent(p, v.firstComponent);
      FunctionalTupleRecord::setComponentCount(p, v.componentCount);
      FunctionalTupleRecord::setOrdinal(p, v.ordinal);
      FunctionalTupleRecord::setFlags(p, v.flags);
    });
  }
  Bytes &components = add(SectionKind::FunctionalTupleSetComponents,
                          db.functionalTupleSetComponents.size());
  for (const auto &v : db.functionalTupleSetComponents) {
    components.record<FunctionalTupleComponentRecord>([&](uint8_t *p) {
      FunctionalTupleComponentRecord::setTuple(p, v.tuple);
      FunctionalTupleComponentRecord::setTarget(p, v.target);
      FunctionalTupleComponentRecord::setBin(p, v.bin);
      FunctionalTupleComponentRecord::setValueSet(p, v.valueSet);
      FunctionalTupleComponentRecord::setOrdinal(p, v.ordinal);
      FunctionalTupleComponentRecord::setFlags(p, v.flags);
    });
  }
  Bytes &optionPlans =
      add(SectionKind::FunctionalOptionPlans, db.functionalOptionPlans.size());
  for (const auto &v : db.functionalOptionPlans) {
    optionPlans.record<FunctionalOptionPlanRecord>([&](uint8_t *p) {
      FunctionalOptionPlanRecord::setOwner(p, v.owner);
      FunctionalOptionPlanRecord::setExpression(p, v.expression);
      FunctionalOptionPlanRecord::setOwnerKind(
          p, static_cast<uint32_t>(v.ownerKind));
      FunctionalOptionPlanRecord::setScope(p, static_cast<uint32_t>(v.scope));
      FunctionalOptionPlanRecord::setOption(p, static_cast<uint32_t>(v.option));
      FunctionalOptionPlanRecord::setOrdinal(p, v.ordinal);
      FunctionalOptionPlanRecord::setFlags(p, v.flags);
    });
  }
  Bytes &configurationOptions = add(SectionKind::FunctionalConfigurationOptions,
                                    db.functionalConfigurationOptions.size());
  for (const auto &v : db.functionalConfigurationOptions) {
    configurationOptions.record<FunctionalConfigurationOptionRecord>(
        [&](uint8_t *p) {
          FunctionalConfigurationOptionRecord::setType(p, v.type);
          FunctionalConfigurationOptionRecord::setConfiguration(
              p, v.configuration.data());
          FunctionalConfigurationOptionRecord::setOwner(p, v.owner);
          FunctionalConfigurationOptionRecord::setOwnerKind(
              p, static_cast<uint32_t>(v.ownerKind));
          FunctionalConfigurationOptionRecord::setScope(
              p, static_cast<uint32_t>(v.scope));
          FunctionalConfigurationOptionRecord::setOption(
              p, static_cast<uint32_t>(v.option));
          FunctionalConfigurationOptionRecord::setValueKind(
              p, static_cast<uint32_t>(v.valueKind));
          FunctionalConfigurationOptionRecord::setFlags(p, v.flags);
          FunctionalConfigurationOptionRecord::setValue(p, v.value);
          FunctionalConfigurationOptionRecord::setString(
              p, strings.get(v.stringValue));
        });
  }
  Bytes &resolvedBinPlans = add(SectionKind::ResolvedFunctionalBinPlans,
                                db.resolvedFunctionalBinPlans.size());
  for (const auto &v : db.resolvedFunctionalBinPlans) {
    resolvedBinPlans.record<ResolvedFunctionalBinPlanRecord>([&](uint8_t *p) {
      ResolvedFunctionalBinPlanRecord::setType(p, v.type);
      ResolvedFunctionalBinPlanRecord::setConfiguration(p,
                                                        v.configuration.data());
      ResolvedFunctionalBinPlanRecord::setBin(p, v.bin);
      ResolvedFunctionalBinPlanRecord::setValueSet(p, v.valueSet);
      ResolvedFunctionalBinPlanRecord::setIffExpression(p, v.iffExpression);
      ResolvedFunctionalBinPlanRecord::setCardinalityExpression(
          p, v.cardinalityExpression);
      ResolvedFunctionalBinPlanRecord::setArrayCardinality(p,
                                                           v.arrayCardinality);
      ResolvedFunctionalBinPlanRecord::setArrayMode(
          p, static_cast<uint32_t>(v.arrayMode));
      ResolvedFunctionalBinPlanRecord::setDistribution(
          p, static_cast<uint32_t>(v.distribution));
      ResolvedFunctionalBinPlanRecord::setFlags(p, v.flags);
    });
  }
  Bytes &resolvedTupleSets = add(SectionKind::ResolvedFunctionalTupleSets,
                                 db.resolvedFunctionalTupleSets.size());
  for (const auto &v : db.resolvedFunctionalTupleSets) {
    resolvedTupleSets.record<ResolvedFunctionalTupleSetRecord>([&](uint8_t *p) {
      ResolvedFunctionalTupleSetRecord::setType(p, v.type);
      ResolvedFunctionalTupleSetRecord::setConfiguration(
          p, v.configuration.data());
      ResolvedFunctionalTupleSetRecord::setId(p, v.id);
      ResolvedFunctionalTupleSetRecord::setTemplateTupleSet(p,
                                                            v.templateTupleSet);
      ResolvedFunctionalTupleSetRecord::setCross(p, v.cross);
      ResolvedFunctionalTupleSetRecord::setSelector(p, v.selector);
      ResolvedFunctionalTupleSetRecord::setFirstTuple(p, v.firstTuple);
      ResolvedFunctionalTupleSetRecord::setTupleCount(p, v.tupleCount);
      ResolvedFunctionalTupleSetRecord::setElementMode(
          p, static_cast<uint32_t>(v.elementMode));
      ResolvedFunctionalTupleSetRecord::setFlags(p, v.flags);
    });
  }
  Bytes &resolvedTuples = add(SectionKind::ResolvedFunctionalTupleSetTuples,
                              db.resolvedFunctionalTupleSetTuples.size());
  for (const auto &v : db.resolvedFunctionalTupleSetTuples) {
    resolvedTuples.record<ResolvedFunctionalTupleRecord>([&](uint8_t *p) {
      ResolvedFunctionalTupleRecord::setTupleSet(p, v.tupleSet);
      ResolvedFunctionalTupleRecord::setId(p, v.id);
      ResolvedFunctionalTupleRecord::setFirstComponent(p, v.firstComponent);
      ResolvedFunctionalTupleRecord::setComponentCount(p, v.componentCount);
      ResolvedFunctionalTupleRecord::setOrdinal(p, v.ordinal);
      ResolvedFunctionalTupleRecord::setFlags(p, v.flags);
    });
  }
  Bytes &resolvedComponents =
      add(SectionKind::ResolvedFunctionalTupleSetComponents,
          db.resolvedFunctionalTupleSetComponents.size());
  for (const auto &v : db.resolvedFunctionalTupleSetComponents) {
    resolvedComponents.record<ResolvedFunctionalTupleComponentRecord>(
        [&](uint8_t *p) {
          ResolvedFunctionalTupleComponentRecord::setTuple(p, v.tuple);
          ResolvedFunctionalTupleComponentRecord::setTarget(p, v.target);
          ResolvedFunctionalTupleComponentRecord::setBin(p, v.bin);
          ResolvedFunctionalTupleComponentRecord::setValueSet(p, v.valueSet);
          ResolvedFunctionalTupleComponentRecord::setOrdinal(p, v.ordinal);
          ResolvedFunctionalTupleComponentRecord::setFlags(p, v.flags);
        });
  }
  Bytes &resolvedAlternatives = add(SectionKind::ResolvedTransitionAlternatives,
                                    db.resolvedTransitionAlternatives.size());
  for (const auto &v : db.resolvedTransitionAlternatives) {
    resolvedAlternatives.record<ResolvedTransitionAlternativeRecord>(
        [&](uint8_t *p) {
          ResolvedTransitionAlternativeRecord::setType(p, v.type);
          ResolvedTransitionAlternativeRecord::setConfiguration(
              p, v.configuration.data());
          ResolvedTransitionAlternativeRecord::setId(p, v.id);
          ResolvedTransitionAlternativeRecord::setBin(p, v.bin);
          ResolvedTransitionAlternativeRecord::setTemplateAlternativeOrdinal(
              p, v.templateAlternativeOrdinal);
          ResolvedTransitionAlternativeRecord::setExpansionOrdinal(
              p, v.expansionOrdinal);
          ResolvedTransitionAlternativeRecord::setFirstStep(p, v.firstStep);
          ResolvedTransitionAlternativeRecord::setStepCount(p, v.stepCount);
          ResolvedTransitionAlternativeRecord::setOrdinal(p, v.ordinal);
          ResolvedTransitionAlternativeRecord::setFlags(p, v.flags);
        });
  }
  Bytes &resolvedBinGroups = add(SectionKind::ResolvedFunctionalBinGroups,
                                 db.resolvedFunctionalBinGroups.size());
  for (const auto &v : db.resolvedFunctionalBinGroups) {
    resolvedBinGroups.record<ResolvedFunctionalBinGroupRecord>([&](uint8_t *p) {
      ResolvedFunctionalBinGroupRecord::setType(p, v.type);
      ResolvedFunctionalBinGroupRecord::setConfiguration(
          p, v.configuration.data());
      ResolvedFunctionalBinGroupRecord::setItem(p, v.item);
      ResolvedFunctionalBinGroupRecord::setTemplateBin(p, v.templateBin);
      ResolvedFunctionalBinGroupRecord::setFirstBin(p, v.firstBin);
      ResolvedFunctionalBinGroupRecord::setBinCount(p, v.binCount);
      ResolvedFunctionalBinGroupRecord::setArrayCardinality(p,
                                                            v.arrayCardinality);
      ResolvedFunctionalBinGroupRecord::setArrayMode(
          p, static_cast<uint32_t>(v.arrayMode));
      ResolvedFunctionalBinGroupRecord::setDistribution(
          p, static_cast<uint32_t>(v.distribution));
      ResolvedFunctionalBinGroupRecord::setFlags(p, v.flags);
      ResolvedFunctionalBinGroupRecord::setKind(p,
                                                static_cast<uint32_t>(v.kind));
    });
  }
  Bytes &resolvedExpansionGroups =
      add(SectionKind::ResolvedTransitionExpansionGroups,
          db.resolvedTransitionExpansionGroups.size());
  for (const auto &v : db.resolvedTransitionExpansionGroups) {
    resolvedExpansionGroups.record<ResolvedTransitionExpansionGroupRecord>(
        [&](uint8_t *p) {
          ResolvedTransitionExpansionGroupRecord::setType(p, v.type);
          ResolvedTransitionExpansionGroupRecord::setConfiguration(
              p, v.configuration.data());
          ResolvedTransitionExpansionGroupRecord::setItem(p, v.item);
          ResolvedTransitionExpansionGroupRecord::setTemplateBin(p,
                                                                 v.templateBin);
          ResolvedTransitionExpansionGroupRecord::setTemplateAlternativeOrdinal(
              p, v.templateAlternativeOrdinal);
          ResolvedTransitionExpansionGroupRecord::setFirstAlternative(
              p, v.firstAlternative);
          ResolvedTransitionExpansionGroupRecord::setAlternativeCount(
              p, v.alternativeCount);
          ResolvedTransitionExpansionGroupRecord::setFlags(p, v.flags);
          ResolvedTransitionExpansionGroupRecord::setExpansionCount(
              p, v.expansionCount);
        });
  }
  Bytes &resolvedCrossAutomaticBinCountLimbs =
      add(SectionKind::ResolvedCrossAutomaticBinCountLimbs,
          db.resolvedCrossAutomaticBinCountLimbs.size());
  for (uint64_t limb : db.resolvedCrossAutomaticBinCountLimbs)
    resolvedCrossAutomaticBinCountLimbs
        .record<ResolvedCrossAutomaticBinCountLimbRecord>([&](uint8_t *p) {
          ResolvedCrossAutomaticBinCountLimbRecord::setValue(p, limb);
        });
  Bytes &resolvedCrossAutomaticNodes =
      add(SectionKind::ResolvedCrossAutomaticNodes,
          db.resolvedCrossAutomaticNodes.size());
  for (const auto &v : db.resolvedCrossAutomaticNodes)
    resolvedCrossAutomaticNodes.record<ResolvedCrossAutomaticNodeRecord>(
        [&](uint8_t *p) {
          ResolvedCrossAutomaticNodeRecord::setType(p, v.type);
          ResolvedCrossAutomaticNodeRecord::setConfiguration(
              p, v.configuration.data());
          ResolvedCrossAutomaticNodeRecord::setCross(p, v.cross);
          ResolvedCrossAutomaticNodeRecord::setId(p, v.id);
          ResolvedCrossAutomaticNodeRecord::setTargetOrdinal(p,
                                                             v.targetOrdinal);
          ResolvedCrossAutomaticNodeRecord::setFirstEdge(p, v.firstEdge);
          ResolvedCrossAutomaticNodeRecord::setEdgeCount(p, v.edgeCount);
          ResolvedCrossAutomaticNodeRecord::setFlags(p, v.flags);
        });
  Bytes &resolvedCrossAutomaticEdges =
      add(SectionKind::ResolvedCrossAutomaticEdges,
          db.resolvedCrossAutomaticEdges.size());
  for (const auto &v : db.resolvedCrossAutomaticEdges)
    resolvedCrossAutomaticEdges.record<ResolvedCrossAutomaticEdgeRecord>(
        [&](uint8_t *p) {
          ResolvedCrossAutomaticEdgeRecord::setNode(p, v.node);
          ResolvedCrossAutomaticEdgeRecord::setBin(p, v.bin);
          ResolvedCrossAutomaticEdgeRecord::setChild(p, v.child);
          ResolvedCrossAutomaticEdgeRecord::setOrdinal(p, v.ordinal);
          ResolvedCrossAutomaticEdgeRecord::setFlags(p, v.flags);
        });
  Bytes &formals =
      add(SectionKind::FunctionalFormals, db.functionalFormals.size());
  for (const auto &v : db.functionalFormals) {
    formals.record<FunctionalFormalRecord>([&](uint8_t *p) {
      FunctionalFormalRecord::setId(p, v.id);
      FunctionalFormalRecord::setType(p, v.type);
      FunctionalFormalRecord::setName(p, strings.get(v.name));
      FunctionalFormalRecord::setKind(p, static_cast<uint32_t>(v.kind));
      FunctionalFormalRecord::setDirection(p,
                                           static_cast<uint32_t>(v.direction));
      FunctionalFormalRecord::setResultKind(
          p, static_cast<uint32_t>(v.resultKind));
      FunctionalFormalRecord::setFlags(p, v.flags);
      FunctionalFormalRecord::setBitWidth(p, v.bitWidth);
      FunctionalFormalRecord::setSignedness(
          p, static_cast<uint32_t>(v.signedness));
      FunctionalFormalRecord::setOrdinal(p, v.ordinal);
      FunctionalFormalRecord::setDefaultExpression(p, v.defaultExpression);
    });
  }
  Bytes &rio = add(SectionKind::ResolvedInstanceOptions,
                   db.resolvedInstanceOptions.size());
  for (const auto &v : db.resolvedInstanceOptions)
    rio.record<ResolvedInstanceOptionRecord>([&](uint8_t *p) {
      ResolvedInstanceOptionRecord::setRun(p, v.run.data());
      ResolvedInstanceOptionRecord::setInstance(p, v.instance);
      ResolvedInstanceOptionRecord::setOwner(p, v.owner);
      ResolvedInstanceOptionRecord::setOwnerKind(
          p, static_cast<uint32_t>(v.ownerKind));
      ResolvedInstanceOptionRecord::setOption(
          p, static_cast<uint32_t>(v.option));
      ResolvedInstanceOptionRecord::setValueKind(
          p, static_cast<uint32_t>(v.valueKind));
      ResolvedInstanceOptionRecord::setStringValue(
          p, strings.get(v.stringValue));
      ResolvedInstanceOptionRecord::setValue(p, v.value);
      ResolvedInstanceOptionRecord::setFlags(p, v.flags);
    });
  finish();
  if (!allGood || sectionIndex != sections.size())
    return failure == Status::Ok ? Status::InvalidDatabase : failure;
  return Status::Ok;
}

bool knownMetric(uint32_t value) { return value >= 1 && value <= 7; }

bool knownFunctionalSourceRole(uint32_t value) {
  return value >= static_cast<uint32_t>(FunctionalSourceRole::Expanded) &&
         value <= static_cast<uint32_t>(FunctionalSourceRole::MacroInvocation);
}

bool knownTransitionRepetition(uint32_t value) {
  return value >= static_cast<uint32_t>(TransitionRepetitionKind::Once) &&
         value <=
             static_cast<uint32_t>(TransitionRepetitionKind::Nonconsecutive);
}

bool knownValueSetKind(uint32_t value) {
  return value >= static_cast<uint32_t>(FunctionalValueSetKind::Integral) &&
         value <= static_cast<uint32_t>(FunctionalValueSetKind::Real);
}

bool knownValueAtomKind(uint32_t value) {
  return value >=
             static_cast<uint32_t>(FunctionalValueAtomKind::IntegralValue) &&
         value <= static_cast<uint32_t>(FunctionalValueAtomKind::RealInterval);
}

bool knownCrossRetainPolicy(uint32_t value) {
  return value >= static_cast<uint32_t>(CrossRetainAutoPolicy::Discard) &&
         value <= static_cast<uint32_t>(CrossRetainAutoPolicy::Deferred);
}

bool knownCrossSelectorKind(uint32_t value) {
  return value >= static_cast<uint32_t>(CrossSelectorKind::Binsof) &&
         value <= static_cast<uint32_t>(CrossSelectorKind::With);
}

template <typename Enum> bool enumBetween(Enum value, Enum first, Enum last) {
  return static_cast<uint32_t>(value) >= static_cast<uint32_t>(first) &&
         static_cast<uint32_t>(value) <= static_cast<uint32_t>(last);
}

template <typename T, typename GetID>
bool uniqueNonzero(const std::vector<T> &records, GetID getID) {
  std::unordered_set<uint64_t> ids;
  for (const auto &record : records)
    if (!getID(record) || !ids.insert(getID(record)).second)
      return false;
  return true;
}

template <typename T> bool hasID(const std::vector<T> &records, uint64_t id) {
  return id != 0 && std::any_of(records.begin(), records.end(),
                                [&](const T &v) { return v.id == id; });
}

bool isNil(const UUID &uuid) {
  return std::all_of(uuid.begin(), uuid.end(),
                     [](uint8_t byte) { return byte == 0; });
}

void hashString(SHA256 &hash, const std::string &text) {
  uint8_t size[8];
  uint64_t n = text.size();
  for (unsigned i = 0; i != 8; ++i)
    size[i] = uint8_t(n >> (i * 8));
  hash.update(size, 8);
  hash.update(reinterpret_cast<const uint8_t *>(text.data()), text.size());
}
template <typename T> void hashScalar(SHA256 &hash, T value) {
  uint8_t bytes[sizeof(T)];
  using U = typename std::make_unsigned<T>::type;
  U v = static_cast<U>(value);
  for (size_t i = 0; i != sizeof(T); ++i)
    bytes[i] = uint8_t(v >> (i * 8));
  hash.update(bytes, sizeof(bytes));
}

Digest checksumWithZeroDigest(const uint8_t *data, size_t size) {
  SHA256 hash;
  hash.update(data, 72);
  uint8_t zero[32]{};
  hash.update(zero, 32);
  if (size > 104)
    hash.update(data + 104, size - 104);
  return hash.finish();
}

struct DirectoryEntry {
  SectionKind kind;
  uint32_t flags;
  uint64_t offset;
  uint64_t size;
  uint64_t count;
};

const SectionDescriptor *descriptor(SectionKind kind) {
  for (const auto &d : ParserSectionDescriptors)
    if (d.kind == kind)
      return &d;
  return nullptr;
}

template <size_t N>
const RecordDescriptor *
recordDescriptor(const RecordDescriptor (&descriptors)[N],
                 SectionKind section) {
  for (const auto &record : descriptors)
    if (record.section == section)
      return &record;
  return nullptr;
}

} // namespace

bool isValidUtf8(const std::string &text) { return validUtf8(text); }

Digest sha256(const uint8_t *data, size_t size) {
  SHA256 hash;
  if (data && size)
    hash.update(data, size);
  return hash.finish();
}

Digest computeSchemaFingerprint(const Database &db) {
  SHA256 h;
  auto files = sorted(db.sourceFiles,
                      [](const auto &a, const auto &b) { return a.id < b.id; });
  for (const auto &v : files) {
    hashScalar(h, uint32_t(2));
    hashScalar(h, v.id);
    hashString(h, v.path);
    h.update(v.digest.data(), v.digest.size());
  }
  auto scopes = sorted(
      db.scopes, [](const auto &a, const auto &b) { return a.id < b.id; });
  for (const auto &v : scopes) {
    hashScalar(h, uint32_t(3));
    hashScalar(h, v.id);
    hashScalar(h, v.parent);
    hashString(h, v.name);
    hashScalar(h, v.kind);
    hashString(h, v.definition);
  }
  auto points = sorted(
      db.linePoints, [](const auto &a, const auto &b) { return a.id < b.id; });
  for (const auto &v : points) {
    hashScalar(h, uint32_t(4));
    hashScalar(h, v.id);
    hashScalar(h, v.file);
    hashScalar(h, v.endFile);
    hashScalar(h, v.scope);
    hashString(h, v.macroName);
    hashScalar(h, v.line);
    hashScalar(h, v.column);
    hashScalar(h, v.endLine);
    hashScalar(h, v.endColumn);
    hashScalar(h, v.semanticPhase);
    hashScalar(h, v.flags);
  }
  auto objects = sorted(db.toggleObjects, [](const auto &a, const auto &b) {
    return a.id < b.id;
  });
  for (const auto &v : objects) {
    hashScalar(h, uint32_t(5));
    hashScalar(h, v.id);
    hashScalar(h, v.scope);
    hashScalar(h, v.file);
    hashString(h, v.name);
    hashScalar(h, v.typeRoot);
    hashScalar(h, v.bitWidth);
    hashScalar(h, v.line);
    hashScalar(h, v.column);
    hashScalar(h, v.endLine);
    hashScalar(h, v.endColumn);
    hashScalar(h, v.flags);
  }
  for (const auto &v : db.toggleDimensions) {
    hashScalar(h, uint32_t(6));
    hashScalar(h, v.object);
    hashScalar(h, v.parent);
    hashScalar(h, static_cast<uint32_t>(v.kind));
    hashScalar(h, v.firstChild);
    hashScalar(h, v.childCount);
    hashScalar(h, v.left);
    hashScalar(h, v.right);
    hashScalar(h, v.bitOffset);
    hashScalar(h, v.bitWidth);
    hashString(h, v.name);
    hashScalar(h, v.flags);
  }
  auto types = sorted(db.functionalTypes,
                      [](const auto &a, const auto &b) { return a.id < b.id; });
  for (const auto &v : types) {
    hashScalar(h, uint32_t(7));
    hashScalar(h, v.id);
    hashScalar(h, v.scope);
    hashString(h, v.name);
    hashScalar(h, v.flags);
    hashScalar(h, v.languageVersion);
    hashString(h, v.hierarchy);
  }
  auto items = sorted(db.functionalItems,
                      [](const auto &a, const auto &b) { return a.id < b.id; });
  for (const auto &v : items) {
    hashScalar(h, uint32_t(8));
    hashScalar(h, v.id);
    hashScalar(h, v.type);
    hashString(h, v.name);
    hashScalar(h, v.kind);
    hashScalar(h, v.flags);
    hashScalar(h, v.goal);
    hashScalar(h, v.weight);
    hashScalar(h, v.ordinal);
    hashString(h, v.hierarchy);
  }
  auto bins = sorted(db.functionalBins,
                     [](const auto &a, const auto &b) { return a.id < b.id; });
  for (const auto &v : bins) {
    hashScalar(h, uint32_t(9));
    hashScalar(h, v.id);
    hashScalar(h, v.item);
    hashString(h, v.name);
    hashScalar(h, v.kind);
    hashScalar(h, v.flags);
    hashScalar(h, v.atLeast);
    hashScalar(h, v.ordinal);
    hashString(h, v.hierarchy);
  }
  for (const auto &v : db.transitionPrograms) {
    hashScalar(h, uint32_t(10));
    hashScalar(h, v.bin);
    hashScalar(h, v.item);
    hashScalar(h, v.alternativeCount);
    hashScalar(h, v.flags);
  }
  for (const auto &v : db.crossPlans) {
    hashScalar(h, uint32_t(11));
    hashScalar(h, v.item);
    hashScalar(h, v.targetCount);
    hashScalar(h, v.binCount);
    hashScalar(h, static_cast<uint32_t>(v.retainAutoPolicy));
    hashScalar(h, v.flags);
    hashScalar(h, v.iffExpression);
    hashScalar(h, v.tupleElementType);
    hashScalar(h, v.tupleProvenanceSpan);
    hashScalar(h, v.tupleFlags);
  }
  auto exclusions = sorted(db.exclusions, [](const auto &a, const auto &b) {
    return std::tie(a.metric, a.entity) < std::tie(b.metric, b.entity);
  });
  for (const auto &v : exclusions) {
    hashScalar(h, uint32_t(12));
    hashScalar(h, v.entity);
    hashScalar(h, static_cast<uint32_t>(v.metric));
    hashString(h, v.reason);
    hashScalar(h, v.flags);
  }
  auto functionalSources =
      sorted(db.functionalSourceRanges, [](const auto &a, const auto &b) {
        return std::tie(a.bin, a.role, a.ordinal) <
               std::tie(b.bin, b.role, b.ordinal);
      });
  for (const auto &v : functionalSources) {
    hashScalar(h, uint32_t(19));
    hashScalar(h, v.bin);
    hashScalar(h, static_cast<uint32_t>(v.role));
    hashScalar(h, v.ordinal);
    hashScalar(h, v.file);
    hashScalar(h, v.endFile);
    hashString(h, v.macroName);
    hashScalar(h, v.line);
    hashScalar(h, v.column);
    hashScalar(h, v.endLine);
    hashScalar(h, v.endColumn);
    hashScalar(h, v.flags);
  }
  for (const auto &v : db.transitionAlternatives) {
    hashScalar(h, uint32_t(20));
    hashScalar(h, v.bin);
    hashScalar(h, v.terminalValueSet);
    hashScalar(h, v.stepCount);
    hashScalar(h, v.ordinal);
    hashScalar(h, v.flags);
  }
  for (const auto &v : db.transitionSteps) {
    hashScalar(h, uint32_t(21));
    hashScalar(h, v.bin);
    hashScalar(h, v.valueSet);
    hashScalar(h, v.lowerExpression);
    hashScalar(h, v.upperExpression);
    hashScalar(h, v.lowerBound);
    hashScalar(h, v.upperBound);
    hashScalar(h, v.alternativeOrdinal);
    hashScalar(h, v.ordinal);
    hashScalar(h, static_cast<uint32_t>(v.repetition));
    hashScalar(h, v.flags);
  }
  for (const auto &v : db.crossTargets) {
    hashScalar(h, uint32_t(22));
    hashScalar(h, v.cross);
    hashScalar(h, v.target);
    hashScalar(h, v.ordinal);
    hashScalar(h, v.tupleBitOffset);
    hashScalar(h, v.tupleBitWidth);
    hashScalar(h, static_cast<uint32_t>(v.tupleResultKind));
    hashScalar(h, static_cast<uint32_t>(v.tupleSignedness));
    hashScalar(h, v.tupleFlags);
  }
  for (const auto &v : db.crossBins) {
    hashScalar(h, uint32_t(23));
    hashScalar(h, v.bin);
    hashScalar(h, v.cross);
    hashScalar(h, v.rootSelector);
    hashScalar(h, v.flags);
  }
  for (const auto &v : db.crossSelectorNodes) {
    hashScalar(h, uint32_t(24));
    hashScalar(h, v.id);
    hashScalar(h, v.cross);
    hashScalar(h, v.target);
    hashScalar(h, v.bin);
    hashScalar(h, v.valueSet);
    hashScalar(h, v.withExpression);
    hashScalar(h, v.constructionExpression);
    hashScalar(h, v.tupleSet);
    hashScalar(h, v.matchesExpression);
    hashScalar(h, v.operandCount);
    hashScalar(h, static_cast<uint32_t>(v.kind));
    hashScalar(h, v.ordinal);
    hashScalar(h, v.flags);
    hashScalar(h, static_cast<uint32_t>(v.matchesPolicy));
    hashScalar(h, v.matchesCount);
  }
  for (const auto &v : db.crossSelectorOperands) {
    hashScalar(h, uint32_t(25));
    hashScalar(h, v.node);
    hashScalar(h, v.operand);
    hashScalar(h, v.ordinal);
  }
  for (const auto &v : db.functionalValueSets) {
    hashScalar(h, uint32_t(27));
    hashScalar(h, v.id);
    hashScalar(h, v.item);
    hashScalar(h, v.atomCount);
    hashScalar(h, v.bitWidth);
    hashScalar(h, static_cast<uint32_t>(v.kind));
    hashScalar(h, v.flags);
    hashScalar(h, static_cast<uint32_t>(v.signedness));
    hashScalar(h, v.setExpression);
  }
  for (const auto &v : db.functionalValueAtoms) {
    hashScalar(h, uint32_t(28));
    hashScalar(h, v.valueSet);
    hashScalar(h, v.realLowBits);
    hashScalar(h, v.realHighBits);
    hashScalar(h, v.limbCount);
    hashScalar(h, v.ordinal);
    hashScalar(h, static_cast<uint32_t>(v.kind));
    hashScalar(h, v.flags);
    hashScalar(h, v.lowerExpression);
    hashScalar(h, v.upperExpression);
  }
  for (const auto &v : db.functionalValueLimbs) {
    hashScalar(h, uint32_t(29));
    hashScalar(h, v.valueSet);
    hashScalar(h, v.atomOrdinal);
    hashScalar(h, v.ordinal);
    hashScalar(h, v.lowAval);
    hashScalar(h, v.lowBval);
    hashScalar(h, v.highAval);
    hashScalar(h, v.highBval);
    hashScalar(h, v.wildcardMask);
  }
  for (const auto &v : db.functionalExpressions) {
    hashScalar(h, uint32_t(30));
    hashScalar(h, v.id);
    hashScalar(h, v.owner);
    hashScalar(h, static_cast<uint32_t>(v.ownerKind));
    hashScalar(h, static_cast<uint32_t>(v.role));
    hashScalar(h, static_cast<uint32_t>(v.resultKind));
    hashScalar(h, v.flags);
    h.update(v.semanticDigest.data(), v.semanticDigest.size());
    hashScalar(h, v.bitWidth);
    hashScalar(h, static_cast<uint32_t>(v.signedness));
    hashScalar(h, v.ownerOrdinal);
    hashScalar(h, v.ownerSubordinal);
    hashScalar(h, static_cast<uint32_t>(v.evaluationPhase));
    hashScalar(h, v.resultOrdinal);
  }
  for (const auto &v : db.functionalBinPlans) {
    hashScalar(h, uint32_t(40));
    hashScalar(h, v.bin);
    hashScalar(h, v.valueSet);
    hashScalar(h, v.iffExpression);
    hashScalar(h, v.cardinalityExpression);
    hashScalar(h, v.arrayCardinality);
    hashScalar(h, static_cast<uint32_t>(v.arrayMode));
    hashScalar(h, static_cast<uint32_t>(v.distribution));
    hashScalar(h, v.flags);
  }
  for (const auto &v : db.functionalTupleSets) {
    hashScalar(h, uint32_t(41));
    hashScalar(h, v.id);
    hashScalar(h, v.cross);
    hashScalar(h, v.selector);
    hashScalar(h, v.tupleCount);
    hashScalar(h, static_cast<uint32_t>(v.elementMode));
    hashScalar(h, v.flags);
  }
  for (const auto &v : db.functionalTupleSetTuples) {
    hashScalar(h, uint32_t(42));
    hashScalar(h, v.tupleSet);
    hashScalar(h, v.id);
    hashScalar(h, v.componentCount);
    hashScalar(h, v.ordinal);
    hashScalar(h, v.flags);
  }
  for (const auto &v : db.functionalTupleSetComponents) {
    hashScalar(h, uint32_t(43));
    hashScalar(h, v.tuple);
    hashScalar(h, v.target);
    hashScalar(h, v.bin);
    hashScalar(h, v.valueSet);
    hashScalar(h, v.ordinal);
    hashScalar(h, v.flags);
  }
  for (const auto &v : db.functionalOptionPlans) {
    hashScalar(h, uint32_t(44));
    hashScalar(h, v.owner);
    hashScalar(h, v.expression);
    hashScalar(h, static_cast<uint32_t>(v.ownerKind));
    hashScalar(h, static_cast<uint32_t>(v.scope));
    hashScalar(h, static_cast<uint32_t>(v.option));
    hashScalar(h, v.ordinal);
    hashScalar(h, v.flags);
  }
  for (const auto &v : db.functionalFormals) {
    hashScalar(h, uint32_t(56));
    hashScalar(h, v.id);
    hashScalar(h, v.type);
    hashString(h, v.name);
    hashScalar(h, static_cast<uint32_t>(v.kind));
    hashScalar(h, static_cast<uint32_t>(v.direction));
    hashScalar(h, static_cast<uint32_t>(v.resultKind));
    hashScalar(h, v.flags);
    hashScalar(h, v.bitWidth);
    hashScalar(h, static_cast<uint32_t>(v.signedness));
    hashScalar(h, v.ordinal);
    hashScalar(h, v.defaultExpression);
  }
  return h.finish();
}

static Digest computeConfigurationBundleDigest(const Database &db,
                                               uint64_t type,
                                               const Digest &configuration,
                                               bool includeResolvedIDs) {
  SHA256 h;
  static constexpr char domain[] = "obelisk.coverage.v1.configuration";
  h.update(reinterpret_cast<const uint8_t *>(domain), sizeof(domain) - 1);
  hashScalar(h, type);
  std::map<uint64_t, const ResolvedFunctionalItem *> items;
  std::map<uint64_t, const ResolvedFunctionalBin *> bins;
  std::map<uint64_t, const ResolvedFunctionalValueSet *> sets;
  for (const auto &v : db.resolvedFunctionalItems)
    if (v.type == type && v.configuration == configuration)
      items.emplace(v.id, &v);
  for (const auto &v : db.resolvedFunctionalBins)
    if (v.type == type && v.configuration == configuration)
      bins.emplace(v.id, &v);
  for (const auto &v : db.resolvedFunctionalValueSets)
    if (v.type == type && v.configuration == configuration)
      sets.emplace(v.id, &v);
  auto itemTemplate = [&](uint64_t id) {
    auto found = items.find(id);
    return found == items.end() ? uint64_t{0} : found->second->templateItem;
  };
  auto hashItemReference = [&](uint64_t id) {
    auto found = items.find(id);
    hashScalar(h, found == items.end() ? uint64_t{0}
                                       : found->second->templateItem);
    hashScalar(h, found == items.end() ? uint32_t{0} : found->second->ordinal);
  };
  auto hashBinReference = [&](uint64_t id) {
    auto found = bins.find(id);
    hashScalar(h,
               found == bins.end() ? uint64_t{0} : found->second->templateBin);
    hashScalar(h, found == bins.end() ? uint64_t{0}
                                      : itemTemplate(found->second->item));
    hashScalar(h, found == bins.end() ? uint32_t{0}
                                      : found->second->expansionOrdinal);
  };
  auto hashSetReference = [&](uint64_t id) {
    auto found = sets.find(id);
    hashScalar(h, found == sets.end() ? uint64_t{0}
                                      : found->second->templateValueSet);
    hashScalar(h, found == sets.end() ? uint64_t{0}
                                      : itemTemplate(found->second->item));
    hashScalar(h, found == sets.end()
                      ? uint32_t{0}
                      : static_cast<uint32_t>(found->second->role));
    if (found == sets.end()) {
      hashScalar(h, uint64_t{0});
      hashScalar(h, uint64_t{0});
      hashScalar(h, uint32_t{0});
      hashScalar(h, uint32_t{0});
    } else {
      hashBinReference(found->second->ownerBin);
      hashScalar(h, found->second->ownerSelector);
      hashScalar(h, found->second->ownerOrdinal);
      hashScalar(h, found->second->ownerSubordinal);
    }
  };
  std::vector<const ResolvedFunctionalItem *> itemOrder;
  std::vector<const ResolvedFunctionalBin *> binOrder;
  std::vector<const ResolvedFunctionalValueSet *> setOrder;
  for (const auto &[id, value] : items)
    itemOrder.push_back(value);
  for (const auto &[id, value] : bins)
    binOrder.push_back(value);
  for (const auto &[id, value] : sets)
    setOrder.push_back(value);
  if (!includeResolvedIDs) {
    std::sort(
        itemOrder.begin(), itemOrder.end(), [](const auto *a, const auto *b) {
          return std::tie(a->templateItem, a->ordinal, a->name, a->hierarchy) <
                 std::tie(b->templateItem, b->ordinal, b->name, b->hierarchy);
        });
    std::sort(
        binOrder.begin(), binOrder.end(), [&](const auto *a, const auto *b) {
          return std::make_tuple(a->templateBin, itemTemplate(a->item),
                                 a->expansionOrdinal, a->ordinal, a->name) <
                 std::make_tuple(b->templateBin, itemTemplate(b->item),
                                 b->expansionOrdinal, b->ordinal, b->name);
        });
    std::sort(
        setOrder.begin(), setOrder.end(), [&](const auto *a, const auto *b) {
          auto key = [&](const auto *value) {
            auto owner = bins.find(value->ownerBin);
            return std::make_tuple(
                value->templateValueSet, value->role,
                owner == bins.end() ? uint64_t{0} : owner->second->templateBin,
                owner == bins.end() ? uint32_t{0}
                                    : owner->second->expansionOrdinal,
                value->ownerSelector, value->ownerOrdinal,
                value->ownerSubordinal, itemTemplate(value->item), value->kind,
                value->bitWidth);
          };
          return key(a) < key(b);
        });
  }
  for (const auto *v : itemOrder) {
    hashScalar(h, uint32_t(1));
    if (includeResolvedIDs)
      hashScalar(h, v->id);
    hashScalar(h, v->templateItem);
    hashString(h, v->name);
    hashScalar(h, v->kind);
    hashScalar(h, v->flags);
    hashScalar(h, v->goal);
    hashScalar(h, v->weight);
    hashScalar(h, v->ordinal);
    hashString(h, v->hierarchy);
  }
  for (const auto *v : binOrder) {
    hashScalar(h, uint32_t(2));
    if (includeResolvedIDs)
      hashScalar(h, v->id);
    hashScalar(h, v->templateBin);
    hashItemReference(v->item);
    hashString(h, v->name);
    hashScalar(h, v->kind);
    hashScalar(h, v->flags);
    hashScalar(h, v->ordinal);
    hashScalar(h, v->expansionOrdinal);
    hashScalar(h, v->atLeast);
    hashString(h, v->hierarchy);
  }
  for (const auto *v : setOrder) {
    hashScalar(h, uint32_t(3));
    if (includeResolvedIDs)
      hashScalar(h, v->id);
    hashScalar(h, v->templateValueSet);
    hashItemReference(v->item);
    hashScalar(h, v->atomCount);
    hashScalar(h, v->bitWidth);
    hashScalar(h, v->kind);
    hashScalar(h, v->flags);
    hashScalar(h, v->signedness);
    hashScalar(h, v->role);
    hashBinReference(v->ownerBin);
    hashScalar(h, v->ownerSelector);
    hashScalar(h, v->ownerOrdinal);
    hashScalar(h, v->ownerSubordinal);
    for (uint32_t atomIndex = 0; atomIndex != v->atomCount; ++atomIndex) {
      const auto &atom =
          db.resolvedFunctionalValueAtoms[v->firstAtom + atomIndex];
      hashScalar(h, atom.realLowBits);
      hashScalar(h, atom.realHighBits);
      hashScalar(h, atom.limbCount);
      hashScalar(h, atom.ordinal);
      hashScalar(h, atom.kind);
      hashScalar(h, atom.flags);
      for (uint32_t limbIndex = 0; limbIndex != atom.limbCount; ++limbIndex) {
        const auto &limb =
            db.resolvedFunctionalValueLimbs[atom.firstLimb + limbIndex];
        hashScalar(h, limb.ordinal);
        hashScalar(h, limb.lowAval);
        hashScalar(h, limb.lowBval);
        hashScalar(h, limb.highAval);
        hashScalar(h, limb.highBval);
        hashScalar(h, limb.wildcardMask);
      }
    }
  }
  std::vector<const FunctionalConfigurationOption *> optionOrder;
  for (const auto &v : db.functionalConfigurationOptions)
    if (v.type == type && v.configuration == configuration)
      optionOrder.push_back(&v);
  std::sort(
      optionOrder.begin(), optionOrder.end(),
      [&](const auto *a, const auto *b) {
        auto owner = [&](const auto *v) {
          return v->ownerKind == FunctionalConfigurationOptionOwnerKind::Group
                     ? type
                     : itemTemplate(v->owner);
        };
        return std::make_tuple(a->ownerKind, owner(a), a->scope, a->option) <
               std::make_tuple(b->ownerKind, owner(b), b->scope, b->option);
      });
  for (const auto *row : optionOrder) {
    const auto &v = *row;
    hashScalar(h, uint32_t(4));
    hashScalar(h, v.ownerKind);
    if (v.ownerKind == FunctionalConfigurationOptionOwnerKind::Group)
      hashScalar(h, type);
    else
      hashItemReference(v.owner);
    hashScalar(h, v.scope);
    hashScalar(h, v.option);
    hashScalar(h, v.valueKind);
    hashScalar(h, v.flags);
    hashScalar(h, v.value);
    hashString(h, v.stringValue);
  }
  std::vector<const ResolvedFunctionalBinGroup *> binGroupOrder;
  for (const auto &v : db.resolvedFunctionalBinGroups)
    if (v.type == type && v.configuration == configuration)
      binGroupOrder.push_back(&v);
  auto templateBinOrdinal = [&](uint64_t id) {
    auto found =
        std::find_if(db.functionalBins.begin(), db.functionalBins.end(),
                     [&](const auto &bin) { return bin.id == id; });
    return found == db.functionalBins.end() ? UINT32_MAX : found->ordinal;
  };
  std::sort(binGroupOrder.begin(), binGroupOrder.end(),
            [&](const auto *a, const auto *b) {
              return std::make_tuple(itemTemplate(a->item),
                                     templateBinOrdinal(a->templateBin),
                                     a->templateBin, a->kind) <
                     std::make_tuple(itemTemplate(b->item),
                                     templateBinOrdinal(b->templateBin),
                                     b->templateBin, b->kind);
            });
  for (const auto *row : binGroupOrder) {
    hashScalar(h, uint32_t(10));
    hashItemReference(row->item);
    hashScalar(h, row->templateBin);
    hashScalar(h, row->binCount);
    hashScalar(h, row->arrayCardinality);
    hashScalar(h, row->arrayMode);
    hashScalar(h, row->distribution);
    hashScalar(h, row->flags);
    hashScalar(h, row->kind);
  }
  std::vector<const ResolvedFunctionalBinPlan *> binPlanOrder;
  for (const auto &v : db.resolvedFunctionalBinPlans)
    if (v.type == type && v.configuration == configuration)
      binPlanOrder.push_back(&v);
  std::sort(
      binPlanOrder.begin(), binPlanOrder.end(),
      [&](const auto *a, const auto *b) {
        const auto *left = bins.at(a->bin);
        const auto *right = bins.at(b->bin);
        return std::make_tuple(itemTemplate(left->item), left->templateBin,
                               left->ordinal, left->expansionOrdinal) <
               std::make_tuple(itemTemplate(right->item), right->templateBin,
                               right->ordinal, right->expansionOrdinal);
      });
  for (const auto *row : binPlanOrder) {
    const auto &v = *row;
    hashScalar(h, uint32_t(5));
    hashBinReference(v.bin);
    hashSetReference(v.valueSet);
    hashScalar(h, v.iffExpression);
    hashScalar(h, v.cardinalityExpression);
    hashScalar(h, v.arrayCardinality);
    hashScalar(h, v.arrayMode);
    hashScalar(h, v.distribution);
    hashScalar(h, v.flags);
  }
  std::vector<const ResolvedTransitionAlternative *> alternativeOrder;
  for (const auto &v : db.resolvedTransitionAlternatives)
    if (v.type == type && v.configuration == configuration)
      alternativeOrder.push_back(&v);
  std::sort(alternativeOrder.begin(), alternativeOrder.end(),
            [&](const auto *a, const auto *b) {
              const auto *left = bins.at(a->bin);
              const auto *right = bins.at(b->bin);
              return std::make_tuple(itemTemplate(left->item),
                                     left->templateBin, left->expansionOrdinal,
                                     a->templateAlternativeOrdinal,
                                     a->expansionOrdinal, a->ordinal) <
                     std::make_tuple(
                         itemTemplate(right->item), right->templateBin,
                         right->expansionOrdinal, b->templateAlternativeOrdinal,
                         b->expansionOrdinal, b->ordinal);
            });
  for (const auto *row : alternativeOrder) {
    const auto &alternative = *row;
    hashScalar(h, uint32_t(6));
    if (includeResolvedIDs)
      hashScalar(h, alternative.id);
    hashBinReference(alternative.bin);
    hashScalar(h, alternative.templateAlternativeOrdinal);
    hashScalar(h, alternative.expansionOrdinal);
    hashScalar(h, alternative.stepCount);
    hashScalar(h, alternative.ordinal);
    hashScalar(h, alternative.flags);
    for (uint32_t stepIndex = 0; stepIndex != alternative.stepCount;
         ++stepIndex) {
      const auto &step =
          db.resolvedTransitionSteps[alternative.firstStep + stepIndex];
      hashSetReference(step.valueSet);
      hashScalar(h, step.lowerBound);
      hashScalar(h, step.upperBound);
      hashScalar(h, step.ordinal);
      hashScalar(h, step.flags);
    }
  }
  std::vector<const ResolvedTransitionExpansionGroup *> expansionGroupOrder;
  for (const auto &v : db.resolvedTransitionExpansionGroups)
    if (v.type == type && v.configuration == configuration)
      expansionGroupOrder.push_back(&v);
  std::sort(expansionGroupOrder.begin(), expansionGroupOrder.end(),
            [&](const auto *a, const auto *b) {
              return std::make_tuple(itemTemplate(a->item), a->templateBin,
                                     a->templateAlternativeOrdinal) <
                     std::make_tuple(itemTemplate(b->item), b->templateBin,
                                     b->templateAlternativeOrdinal);
            });
  for (const auto *row : expansionGroupOrder) {
    hashScalar(h, uint32_t(11));
    hashItemReference(row->item);
    hashScalar(h, row->templateBin);
    hashScalar(h, row->templateAlternativeOrdinal);
    hashScalar(h, row->alternativeCount);
    hashScalar(h, row->flags);
    hashScalar(h, row->expansionCount);
  }
  std::vector<const ResolvedCrossPlan *> crossPlanOrder;
  for (const auto &v : db.resolvedCrossPlans)
    if (v.type == type && v.configuration == configuration)
      crossPlanOrder.push_back(&v);
  std::sort(crossPlanOrder.begin(), crossPlanOrder.end(),
            [&](const auto *a, const auto *b) {
              return itemTemplate(a->cross) < itemTemplate(b->cross);
            });
  for (const auto *row : crossPlanOrder) {
    const auto &v = *row;
    hashScalar(h, uint32_t(7));
    hashItemReference(v.cross);
    hashScalar(h, v.retainAutoPolicy);
    hashScalar(h, v.flags);
    hashScalar(h, v.automaticBinCountLimbCount);
    for (uint32_t index = 0; index != v.automaticBinCountLimbCount; ++index)
      hashScalar(
          h,
          db.resolvedCrossAutomaticBinCountLimbs[v.firstAutomaticBinCountLimb +
                                                 index]);
    std::map<uint64_t, const ResolvedCrossAutomaticNode *> automaticNodes;
    for (const auto &node : db.resolvedCrossAutomaticNodes)
      if (node.type == type && node.configuration == configuration &&
          node.cross == v.cross)
        automaticNodes.emplace(node.id, &node);
    std::map<uint64_t, Digest> nodeDigests;
    std::vector<const ResolvedCrossAutomaticNode *> reverseNodes;
    for (const auto &[id, node] : automaticNodes)
      reverseNodes.push_back(node);
    std::sort(reverseNodes.begin(), reverseNodes.end(),
              [](const auto *a, const auto *b) {
                return std::tie(a->targetOrdinal, a->id) >
                       std::tie(b->targetOrdinal, b->id);
              });
    for (const auto *nodePointer : reverseNodes) {
      SHA256 nodeHash;
      static constexpr char nodeDomain[] =
          "obelisk.coverage.v1.resolved-cross-automatic-node";
      nodeHash.update(reinterpret_cast<const uint8_t *>(nodeDomain),
                      sizeof(nodeDomain) - 1);
      const auto &node = *nodePointer;
      if (includeResolvedIDs)
        hashScalar(nodeHash, node.id);
      hashScalar(nodeHash, node.targetOrdinal);
      hashScalar(nodeHash, node.edgeCount);
      for (uint32_t index = 0; index != node.edgeCount; ++index) {
        const auto &edge =
            db.resolvedCrossAutomaticEdges[node.firstEdge + index];
        auto bin = bins.find(edge.bin);
        if (includeResolvedIDs) {
          hashScalar(nodeHash, edge.bin);
          hashScalar(nodeHash, edge.child);
        }
        hashScalar(nodeHash,
                   bin == bins.end() ? uint64_t{0} : bin->second->templateBin);
        hashScalar(nodeHash, bin == bins.end()
                                 ? uint64_t{0}
                                 : itemTemplate(bin->second->item));
        hashScalar(nodeHash, bin == bins.end() ? uint32_t{0}
                                               : bin->second->expansionOrdinal);
        hashString(nodeHash,
                   bin == bins.end() ? std::string{} : bin->second->name);
        auto child = nodeDigests.find(edge.child);
        Digest childDigest =
            edge.child && child != nodeDigests.end() ? child->second : Digest{};
        nodeHash.update(childDigest.data(), childDigest.size());
        hashScalar(nodeHash, edge.ordinal);
        hashScalar(nodeHash, edge.flags);
      }
      Digest digest = nodeHash.finish();
      nodeDigests.emplace(node.id, digest);
    }
    auto root = nodeDigests.find(v.rootNode);
    Digest rootDigest =
        v.rootNode && root != nodeDigests.end() ? root->second : Digest{};
    h.update(rootDigest.data(), rootDigest.size());
  }
  std::vector<const ResolvedCrossSelectorBinding *> bindingOrder;
  for (const auto &v : db.resolvedCrossSelectorBindings)
    if (v.type == type && v.configuration == configuration)
      bindingOrder.push_back(&v);
  std::sort(bindingOrder.begin(), bindingOrder.end(),
            [&](const auto *a, const auto *b) {
              return std::make_pair(itemTemplate(a->cross), a->node) <
                     std::make_pair(itemTemplate(b->cross), b->node);
            });
  for (const auto *row : bindingOrder) {
    const auto &v = *row;
    hashScalar(h, uint32_t(8));
    hashItemReference(v.cross);
    hashScalar(h, v.node);
    hashSetReference(v.valueSet);
    hashScalar(h, v.withExpression);
    auto tupleSet = std::find_if(db.resolvedFunctionalTupleSets.begin(),
                                 db.resolvedFunctionalTupleSets.end(),
                                 [&](const auto &set) {
                                   return set.type == type &&
                                          set.configuration == configuration &&
                                          set.id == v.tupleSet;
                                 });
    hashScalar(h, tupleSet == db.resolvedFunctionalTupleSets.end()
                      ? uint64_t{0}
                      : tupleSet->templateTupleSet);
    hashScalar(h, tupleSet == db.resolvedFunctionalTupleSets.end()
                      ? uint64_t{0}
                      : tupleSet->selector);
    hashScalar(h, tupleSet == db.resolvedFunctionalTupleSets.end()
                      ? uint32_t{0}
                      : static_cast<uint32_t>(tupleSet->elementMode));
    hashScalar(h, v.matchesPolicy);
    hashScalar(h, v.flags);
    hashScalar(h, v.matchesCount);
  }
  std::vector<const ResolvedFunctionalTupleSet *> tupleSetOrder;
  for (const auto &v : db.resolvedFunctionalTupleSets)
    if (v.type == type && v.configuration == configuration)
      tupleSetOrder.push_back(&v);
  std::sort(
      tupleSetOrder.begin(), tupleSetOrder.end(),
      [&](const auto *a, const auto *b) {
        return std::make_tuple(a->templateTupleSet, itemTemplate(a->cross),
                               a->selector, a->elementMode) <
               std::make_tuple(b->templateTupleSet, itemTemplate(b->cross),
                               b->selector, b->elementMode);
      });
  for (const auto *row : tupleSetOrder) {
    const auto &v = *row;
    hashScalar(h, uint32_t(9));
    if (includeResolvedIDs)
      hashScalar(h, v.id);
    hashScalar(h, v.templateTupleSet);
    hashItemReference(v.cross);
    hashScalar(h, v.selector);
    hashScalar(h, v.tupleCount);
    hashScalar(h, v.elementMode);
    hashScalar(h, v.flags);
    for (uint32_t tupleIndex = 0; tupleIndex != v.tupleCount; ++tupleIndex) {
      const auto &tuple =
          db.resolvedFunctionalTupleSetTuples[v.firstTuple + tupleIndex];
      if (includeResolvedIDs)
        hashScalar(h, tuple.id);
      hashScalar(h, tuple.componentCount);
      hashScalar(h, tuple.ordinal);
      hashScalar(h, tuple.flags);
      for (uint32_t componentIndex = 0; componentIndex != tuple.componentCount;
           ++componentIndex) {
        const auto &component =
            db.resolvedFunctionalTupleSetComponents[tuple.firstComponent +
                                                    componentIndex];
        hashItemReference(component.target);
        hashBinReference(component.bin);
        hashSetReference(component.valueSet);
        hashScalar(h, component.ordinal);
        hashScalar(h, component.flags);
      }
    }
  }
  return h.finish();
}

Digest
computeFunctionalConfigurationFingerprint(const Database &db, uint64_t type,
                                          const Digest &configurationKey) {
  return computeConfigurationBundleDigest(db, type, configurationKey,
                                          /*includeResolvedIDs=*/false);
}

bool isSchemaOnly(const Database &db) {
  return db.runs.empty() && db.resolvedInstances.empty() &&
         db.resolvedInstanceOptions.empty() &&
         db.counters.empty() && db.illegalBinDiagnostics.empty() &&
         db.functionalConfigurations.empty() &&
         db.functionalConfigurationOptions.empty() &&
         db.resolvedFunctionalItems.empty() &&
         db.resolvedFunctionalBins.empty() &&
         db.resolvedTransitionAlternatives.empty() &&
         db.resolvedTransitionExpansionGroups.empty() &&
         db.resolvedTransitionSteps.empty() && db.resolvedCrossPlans.empty() &&
         db.resolvedCrossAutomaticBinCountLimbs.empty() &&
         db.resolvedCrossAutomaticNodes.empty() &&
         db.resolvedCrossAutomaticEdges.empty() &&
         db.resolvedCrossSelectorBindings.empty() &&
         db.resolvedFunctionalValueSets.empty() &&
         db.resolvedFunctionalValueAtoms.empty() &&
         db.resolvedFunctionalValueLimbs.empty() &&
         db.resolvedFunctionalBinPlans.empty() &&
         db.resolvedFunctionalBinGroups.empty() &&
         db.resolvedFunctionalTupleSets.empty() &&
         db.resolvedFunctionalTupleSetTuples.empty() &&
         db.resolvedFunctionalTupleSetComponents.empty() &&
         db.sparseCrossTuples.empty() && db.sparseCrossTupleComponents.empty();
}

Status validate(const Database &db, Diagnostic *diagnostic) {
  auto fail = [&](Status s, const char *field, std::string detail = {}) {
    setDiagnostic(diagnostic, s, 0, SectionKind::Strings, NoRecord, field,
                  std::move(detail));
    return s;
  };
  auto text = [&](const std::string &s) { return validUtf8(s); };
  if ((db.flags & ~KnownHeaderFlags) != 0)
    return fail(Status::InvalidEnum, "header.flags");
  if (!text(db.producer))
    return fail(Status::InvalidUtf8, "producer");
  if (!uniqueNonzero(db.sourceFiles, [](const auto &v) { return v.id; }) ||
      !uniqueNonzero(db.scopes, [](const auto &v) { return v.id; }) ||
      !uniqueNonzero(db.linePoints, [](const auto &v) { return v.id; }) ||
      !uniqueNonzero(db.toggleObjects, [](const auto &v) { return v.id; }) ||
      !uniqueNonzero(db.functionalTypes, [](const auto &v) { return v.id; }) ||
      !uniqueNonzero(db.functionalItems, [](const auto &v) { return v.id; }) ||
      !uniqueNonzero(db.functionalBins, [](const auto &v) { return v.id; }))
    return fail(Status::UnsortedOrDuplicate, "entity_id",
                "entity IDs must be nonzero and unique within a table");
  if (!std::is_sorted(
          db.sourceFiles.begin(), db.sourceFiles.end(),
          [](const auto &a, const auto &b) { return a.id < b.id; }) ||
      !std::is_sorted(
          db.scopes.begin(), db.scopes.end(),
          [](const auto &a, const auto &b) { return a.id < b.id; }) ||
      !std::is_sorted(
          db.linePoints.begin(), db.linePoints.end(),
          [](const auto &a, const auto &b) { return a.id < b.id; }) ||
      !std::is_sorted(
          db.toggleObjects.begin(), db.toggleObjects.end(),
          [](const auto &a, const auto &b) { return a.id < b.id; }) ||
      !std::is_sorted(
          db.functionalTypes.begin(), db.functionalTypes.end(),
          [](const auto &a, const auto &b) { return a.id < b.id; }) ||
      !std::is_sorted(
          db.functionalItems.begin(), db.functionalItems.end(),
          [](const auto &a, const auto &b) { return a.id < b.id; }) ||
      !std::is_sorted(db.functionalBins.begin(), db.functionalBins.end(),
                      [](const auto &a, const auto &b) { return a.id < b.id; }))
    return fail(Status::UnsortedOrDuplicate, "entity_order");
  for (const auto &v : db.sourceFiles)
    if (!text(v.path))
      return fail(Status::InvalidUtf8, "source.path");
  std::unordered_map<uint64_t, uint64_t> scopeParents;
  scopeParents.reserve(db.scopes.size());
  for (const auto &v : db.scopes) {
    if (!text(v.name) || !text(v.definition))
      return fail(Status::InvalidUtf8, "scope");
    if (v.parent && !hasID(db.scopes, v.parent))
      return fail(Status::InvalidReference, "scope.parent");
    scopeParents.emplace(v.id, v.parent);
  }
  // Scope selection walks descendants at runtime. Reject cycles here so every
  // consumer can rely on a forest without revalidating hostile images.
  std::unordered_map<uint64_t, uint8_t> scopeColors;
  scopeColors.reserve(db.scopes.size());
  for (const auto &scope : db.scopes) {
    if (scopeColors[scope.id] == 2)
      continue;
    std::vector<uint64_t> path;
    uint64_t cursor = scope.id;
    while (cursor) {
      uint8_t &color = scopeColors[cursor];
      if (color == 2)
        break;
      if (color == 1)
        return fail(Status::InvalidReference, "scope.parent_cycle");
      color = 1;
      path.push_back(cursor);
      cursor = scopeParents.at(cursor);
    }
    for (uint64_t id : path)
      scopeColors[id] = 2;
  }
  for (const auto &v : db.linePoints)
    if (!hasID(db.sourceFiles, v.file) || !hasID(db.sourceFiles, v.endFile) ||
        !hasID(db.scopes, v.scope) || !v.line || !v.endLine ||
        !text(v.macroName))
      return fail(text(v.macroName) ? Status::InvalidReference
                                    : Status::InvalidUtf8,
                  "line_point");
  for (const auto &v : db.toggleObjects)
    if (!text(v.name) || !hasID(db.scopes, v.scope) ||
        !hasID(db.sourceFiles, v.file) || !v.bitWidth ||
        v.bitWidth > UINT32_MAX / 4 || !v.line || !v.endLine ||
        (v.flags & ~ToggleObjectFourState) != 0 || v.typeRoot == UINT32_MAX ||
        v.typeRoot >= db.toggleDimensions.size() ||
        db.toggleDimensions[v.typeRoot].object != v.id)
      return fail(Status::InvalidReference, "toggle_object");
  uint64_t previousObject = 0;
  struct ToggleSubtree {
    uint32_t node;
    size_t end;
  };
  std::vector<ToggleSubtree> toggleSubtrees;
  for (size_t index = 0; index != db.toggleDimensions.size(); ++index) {
    if (index > UINT32_MAX)
      return fail(Status::InvalidReference, "toggle_dimension.index");
    const ToggleDimension &v = db.toggleDimensions[index];
    while (!toggleSubtrees.empty() && toggleSubtrees.back().end == index)
      toggleSubtrees.pop_back();
    if (!toggleSubtrees.empty() && toggleSubtrees.back().end < index)
      return fail(Status::InvalidReference, "toggle_dimension.preorder_span");
    const uint32_t kind = static_cast<uint32_t>(v.kind);
    if (!hasID(db.toggleObjects, v.object) || !text(v.name) ||
        kind < static_cast<uint32_t>(ToggleDimensionKind::Root) ||
        kind > static_cast<uint32_t>(ToggleDimensionKind::Tag) ||
        v.flags != 0 || v.bitWidth == 0 ||
        v.bitOffset > UINT64_MAX - v.bitWidth)
      return fail(Status::InvalidReference, "toggle_dimension");
    if (index && v.object < previousObject)
      return fail(Status::UnsortedOrDuplicate, "toggle_dimension_order");
    previousObject = v.object;
    const uint32_t expectedParent =
        toggleSubtrees.empty() ? UINT32_MAX : toggleSubtrees.back().node;
    if (v.parent != expectedParent)
      return fail(Status::InvalidReference, "toggle_dimension.preorder_parent");
    if (v.parent == UINT32_MAX) {
      if (v.kind != ToggleDimensionKind::Root)
        return fail(Status::InvalidReference, "toggle_dimension.root");
    } else if (v.parent >= index ||
               db.toggleDimensions[v.parent].object != v.object) {
      return fail(Status::InvalidReference, "toggle_dimension.parent");
    } else {
      const ToggleDimension &parent = db.toggleDimensions[v.parent];
      if (parent.childCount == 0 ||
          index > static_cast<uint64_t>(v.parent) + parent.childCount ||
          v.bitOffset < parent.bitOffset ||
          v.bitOffset + v.bitWidth > parent.bitOffset + parent.bitWidth)
        return fail(Status::InvalidReference, "toggle_dimension.parent_span");
    }
    if (v.childCount == 0) {
      if (v.firstChild != UINT32_MAX)
        return fail(Status::InvalidReference, "toggle_dimension.children");
    } else if (v.firstChild != index + 1 ||
               v.firstChild >= db.toggleDimensions.size() ||
               v.childCount > db.toggleDimensions.size() - v.firstChild) {
      return fail(Status::InvalidReference, "toggle_dimension.children");
    }
    if (v.childCount != 0) {
      const size_t subtreeEnd = index + 1 + v.childCount;
      if (!toggleSubtrees.empty() && subtreeEnd > toggleSubtrees.back().end)
        return fail(Status::InvalidReference, "toggle_dimension.preorder_span");
      toggleSubtrees.push_back({static_cast<uint32_t>(index), subtreeEnd});
    }
    if (v.bitOffset + v.bitWidth >
        db.toggleObjects[std::lower_bound(
                             db.toggleObjects.begin(), db.toggleObjects.end(),
                             v.object,
                             [](const ToggleObject &object, uint64_t id) {
                               return object.id < id;
                             }) -
                         db.toggleObjects.begin()]
            .bitWidth)
      return fail(Status::InvalidReference, "toggle_dimension.span");
  }
  for (size_t objectIndex = 0; objectIndex != db.toggleObjects.size();
       ++objectIndex) {
    const ToggleObject &object = db.toggleObjects[objectIndex];
    const ToggleDimension &root = db.toggleDimensions[object.typeRoot];
    uint64_t end = objectIndex + 1 == db.toggleObjects.size()
                       ? db.toggleDimensions.size()
                       : db.toggleObjects[objectIndex + 1].typeRoot;
    if (root.parent != UINT32_MAX || root.kind != ToggleDimensionKind::Root ||
        root.bitOffset != 0 || root.bitWidth != object.bitWidth ||
        object.typeRoot > end || root.childCount != end - object.typeRoot - 1 ||
        (root.childCount != 0 && root.firstChild != object.typeRoot + 1))
      return fail(Status::InvalidReference, "toggle_dimension.object_root");
    for (uint64_t index = object.typeRoot; index != end; ++index)
      if (db.toggleDimensions[index].object != object.id)
        return fail(Status::InvalidReference, "toggle_dimension.object_group");
  }
  std::map<uint64_t, std::string> functionalTypeHierarchies;
  std::set<std::string> functionalHierarchies;
  for (const auto &v : db.functionalTypes)
    if (!text(v.name) || !text(v.hierarchy) || v.hierarchy.empty() ||
        !hasID(db.scopes, v.scope) || v.flags != 0 ||
        (v.languageVersion != 2017 && v.languageVersion != 2023) ||
        !functionalHierarchies.insert(v.hierarchy).second ||
        !functionalTypeHierarchies.emplace(v.id, v.hierarchy).second)
      return fail(Status::InvalidReference, "functional_type");
  std::map<uint64_t, std::string> functionalItemHierarchies;
  std::map<uint64_t, std::set<uint32_t>> itemOrdinals;
  std::map<uint64_t, uint64_t> itemCounts;
  for (const auto &v : db.functionalItems) {
    uint32_t kind = static_cast<uint32_t>(v.kind);
    auto typeHierarchy = functionalTypeHierarchies.find(v.type);
    if (!text(v.name) || !text(v.hierarchy) || v.hierarchy.empty() ||
        !hasID(db.functionalTypes, v.type) || v.goal > 100 ||
        (v.flags & ~FunctionalItemNonAggregating) != 0 ||
        typeHierarchy == functionalTypeHierarchies.end() ||
        v.hierarchy != typeHierarchy->second + "." + v.name ||
        !functionalHierarchies.insert(v.hierarchy).second ||
        !functionalItemHierarchies.emplace(v.id, v.hierarchy).second ||
        kind < static_cast<uint32_t>(FunctionalItemKind::Coverpoint) ||
        kind > static_cast<uint32_t>(FunctionalItemKind::Cross) ||
        !itemOrdinals[v.type].insert(v.ordinal).second)
      return fail(Status::InvalidReference, "functional_item");
    ++itemCounts[v.type];
  }
  for (const auto &[type, ordinals] : itemOrdinals)
    if (ordinals.size() != itemCounts[type] ||
        (!ordinals.empty() && *ordinals.rbegin() != ordinals.size() - 1))
      return fail(Status::InvalidReference, "functional_item.ordinal");
  std::map<uint64_t, std::set<uint32_t>> binOrdinals;
  std::map<uint64_t, uint64_t> binCounts;
  for (const auto &v : db.functionalBins) {
    uint32_t kind = static_cast<uint32_t>(v.kind);
    auto item = std::lower_bound(
        db.functionalItems.begin(), db.functionalItems.end(), v.item,
        [](const FunctionalItem &entry, uint64_t id) { return entry.id < id; });
    auto itemHierarchy = functionalItemHierarchies.find(v.item);
    bool typedOwner = item != db.functionalItems.end() && item->id == v.item &&
                      ((v.kind == FunctionalBinKind::Cross) ==
                       (item->kind == FunctionalItemKind::Cross));
    if (!text(v.name) || !text(v.hierarchy) || v.hierarchy.empty() ||
        !typedOwner || itemHierarchy == functionalItemHierarchies.end() ||
        v.hierarchy != itemHierarchy->second + "." + v.name ||
        !functionalHierarchies.insert(v.hierarchy).second || v.atLeast != 1 ||
        (v.flags &
         ~(FunctionalBinDefault | FunctionalBinDefaultSequence |
           FunctionalBinIgnore | FunctionalBinIllegal | FunctionalBinWildcard |
           FunctionalBinAutomatic | FunctionalBinEmpty)) != 0 ||
        ((v.flags & FunctionalBinIgnore) && (v.flags & FunctionalBinIllegal)) ||
        ((v.flags & FunctionalBinDefault) &&
         v.kind != FunctionalBinKind::State) ||
        ((v.flags & FunctionalBinDefaultSequence) &&
         v.kind != FunctionalBinKind::Transition) ||
        ((v.flags & FunctionalBinDefault) &&
         (v.flags & FunctionalBinDefaultSequence)) ||
        ((v.flags & FunctionalBinWildcard) &&
         v.kind == FunctionalBinKind::Cross) ||
        ((v.flags & FunctionalBinAutomatic) &&
         (v.kind != FunctionalBinKind::State ||
          (v.flags & (FunctionalBinDefault | FunctionalBinDefaultSequence |
                      FunctionalBinIgnore | FunctionalBinIllegal |
                      FunctionalBinWildcard)))) ||
        ((v.flags & FunctionalBinDefaultSequence) &&
         v.flags != FunctionalBinDefaultSequence) ||
        ((v.flags & FunctionalBinDefault) && (v.flags & FunctionalBinIgnore)) ||
        kind < static_cast<uint32_t>(FunctionalBinKind::State) ||
        kind > static_cast<uint32_t>(FunctionalBinKind::Cross) ||
        !binOrdinals[v.item].insert(v.ordinal).second)
      return fail(Status::InvalidReference, "functional_bin",
                  "bin id " + std::to_string(v.id) + " owned by item " +
                      std::to_string(v.item) + " (actual " + v.hierarchy +
                      ", expected prefix " +
                      (itemHierarchy == functionalItemHierarchies.end()
                           ? std::string("<missing>")
                           : itemHierarchy->second) +
                      ", name " + v.name + ")");
    ++binCounts[v.item];
  }
  for (const auto &[item, ordinals] : binOrdinals)
    if (ordinals.size() != binCounts[item] ||
        (!ordinals.empty() && *ordinals.rbegin() != ordinals.size() - 1))
      return fail(Status::InvalidReference, "functional_bin.ordinal");

  auto checkedRange = [](uint32_t first, uint32_t count, size_t size) {
    return first <= size && count <= size - first;
  };
  auto integralRangeOrdered = [](const auto &limbs, uint32_t first,
                                 uint32_t count, uint32_t bitWidth,
                                 CoverageSignedness signedness) {
    if (!count)
      return false;
    const auto &most = limbs[first + count - 1];
    bool lowNegative = false, highNegative = false;
    if (signedness == CoverageSignedness::Signed) {
      uint32_t signBit = (bitWidth - 1) % 64;
      lowNegative = ((most.lowAval >> signBit) & 1) != 0;
      highNegative = ((most.highAval >> signBit) & 1) != 0;
      if (lowNegative != highNegative)
        return lowNegative;
    }
    for (uint32_t index = count; index != 0; --index) {
      const auto &limb = limbs[first + index - 1];
      if (limb.lowAval != limb.highAval)
        return limb.lowAval < limb.highAval;
    }
    return true;
  };
  auto decodeReal = [](uint64_t bits) {
    double value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
  };
  auto findItem = [&](uint64_t id) -> const FunctionalItem * {
    auto found =
        std::lower_bound(db.functionalItems.begin(), db.functionalItems.end(),
                         id, [](const FunctionalItem &entry, uint64_t value) {
                           return entry.id < value;
                         });
    return found != db.functionalItems.end() && found->id == id ? &*found
                                                                : nullptr;
  };
  auto findBin = [&](uint64_t id) -> const FunctionalBin * {
    auto found =
        std::lower_bound(db.functionalBins.begin(), db.functionalBins.end(), id,
                         [](const FunctionalBin &entry, uint64_t value) {
                           return entry.id < value;
                         });
    return found != db.functionalBins.end() && found->id == id ? &*found
                                                               : nullptr;
  };
  auto findFunctionalType = [&](uint64_t id) -> const FunctionalType * {
    auto found =
        std::lower_bound(db.functionalTypes.begin(), db.functionalTypes.end(),
                         id, [](const FunctionalType &entry, uint64_t value) {
                           return entry.id < value;
                         });
    return found != db.functionalTypes.end() && found->id == id ? &*found
                                                                : nullptr;
  };
  if (!uniqueNonzero(db.functionalValueSets,
                     [](const auto &v) { return v.id; }) ||
      !std::is_sorted(db.functionalValueSets.begin(),
                      db.functionalValueSets.end(),
                      [](const auto &a, const auto &b) { return a.id < b.id; }))
    return fail(Status::UnsortedOrDuplicate, "functional_value_set_order");
  auto findValueSet = [&](uint64_t id) -> const FunctionalValueSet * {
    auto found = std::lower_bound(
        db.functionalValueSets.begin(), db.functionalValueSets.end(), id,
        [](const FunctionalValueSet &entry, uint64_t value) {
          return entry.id < value;
        });
    return found != db.functionalValueSets.end() && found->id == id ? &*found
                                                                    : nullptr;
  };
  size_t atomCursor = 0;
  size_t limbCursor = 0;
  std::set<uint64_t> wildcardValueSets;
  for (const auto &set : db.functionalValueSets) {
    const FunctionalItem *item = findItem(set.item);
    const FunctionalType *ownerType =
        item ? findFunctionalType(item->type) : nullptr;
    uint32_t kind = static_cast<uint32_t>(set.kind);
    bool integralSet = set.kind == FunctionalValueSetKind::Integral;
    bool needsResolution = (set.flags & FunctionalValueSetNeedsResolution) != 0;
    bool hasDeferredAtom = false;
    if (!item || item->kind != FunctionalItemKind::Coverpoint ||
        !knownValueSetKind(kind) ||
        (set.flags & ~FunctionalValueSetNeedsResolution) != 0 ||
        set.firstAtom != atomCursor ||
        !checkedRange(set.firstAtom, set.atomCount,
                      db.functionalValueAtoms.size()) ||
        (set.setExpression && set.atomCount) ||
        (set.setExpression && !needsResolution) ||
        (set.kind == FunctionalValueSetKind::Real &&
         (!ownerType || ownerType->languageVersion < 2023)) ||
        (integralSet &&
         (!set.bitWidth || (set.signedness != CoverageSignedness::Unsigned &&
                            set.signedness != CoverageSignedness::Signed))) ||
        (!integralSet && (set.bitWidth != 64 ||
                          set.signedness != CoverageSignedness::NotApplicable)))
      return fail(Status::InvalidReference, "functional_value_set");
    for (uint32_t atomIndex = 0; atomIndex != set.atomCount; ++atomIndex) {
      const FunctionalValueAtom &atom =
          db.functionalValueAtoms[set.firstAtom + atomIndex];
      uint32_t atomKind = static_cast<uint32_t>(atom.kind);
      const uint64_t expectedLimbs =
          (uint64_t{set.bitWidth} + uint64_t{63}) / uint64_t{64};
      bool integralAtom = atom.kind == FunctionalValueAtomKind::IntegralValue ||
                          atom.kind == FunctionalValueAtomKind::IntegralRange;
      const uint32_t unboundedFlags =
          atom.flags & (FunctionalValueAtomLowerUnbounded |
                        FunctionalValueAtomUpperUnbounded);
      bool deferredAtom =
          (integralAtom && atom.limbCount == 0) ||
          (!integralAtom &&
           (atom.lowerExpression || atom.upperExpression || unboundedFlags));
      hasDeferredAtom |= deferredAtom;
      constexpr uint32_t definitionAtomFlags =
          FunctionalValueAtomLowerInclusive |
          FunctionalValueAtomUpperInclusive |
          FunctionalValueAtomAbsoluteTolerance |
          FunctionalValueAtomRelativeTolerance |
          FunctionalValueAtomLowerUnbounded |
          FunctionalValueAtomUpperUnbounded | FunctionalValueAtomRealRange;
      const uint32_t toleranceFlags =
          atom.flags & (FunctionalValueAtomAbsoluteTolerance |
                        FunctionalValueAtomRelativeTolerance);
      const uint32_t integralAtomFlags = FunctionalValueAtomLowerInclusive |
                                         FunctionalValueAtomUpperInclusive |
                                         unboundedFlags;
      if (atom.valueSet != set.id || atom.ordinal != atomIndex ||
          !knownValueAtomKind(atomKind) ||
          (atom.flags & ~definitionAtomFlags) != 0 ||
          toleranceFlags == (FunctionalValueAtomAbsoluteTolerance |
                             FunctionalValueAtomRelativeTolerance) ||
          unboundedFlags == (FunctionalValueAtomLowerUnbounded |
                             FunctionalValueAtomUpperUnbounded) ||
          ((unboundedFlags & FunctionalValueAtomLowerUnbounded) &&
           !(atom.flags & FunctionalValueAtomLowerInclusive)) ||
          ((unboundedFlags & FunctionalValueAtomUpperUnbounded) &&
           !(atom.flags & FunctionalValueAtomUpperInclusive)) ||
          atom.firstLimb != limbCursor ||
          !checkedRange(atom.firstLimb, atom.limbCount,
                        db.functionalValueLimbs.size()) ||
          (integralAtom != (set.kind == FunctionalValueSetKind::Integral)) ||
          (integralAtom && atom.flags != integralAtomFlags) ||
          (toleranceFlags &&
           (integralAtom || !deferredAtom || !atom.lowerExpression ||
            !atom.upperExpression || unboundedFlags)) ||
          ((atom.flags & FunctionalValueAtomRealRange) && integralAtom) ||
          (!integralAtom && !(atom.flags & FunctionalValueAtomRealRange) &&
           (toleranceFlags || unboundedFlags || atom.upperExpression ||
            (!deferredAtom &&
             decodeReal(atom.realLowBits) != decodeReal(atom.realHighBits)))) ||
          (unboundedFlags &&
           (!deferredAtom || toleranceFlags ||
            (integralAtom &&
             atom.kind != FunctionalValueAtomKind::IntegralRange) ||
            ((unboundedFlags & FunctionalValueAtomLowerUnbounded) &&
             (atom.lowerExpression || atom.realLowBits)) ||
            ((unboundedFlags & FunctionalValueAtomUpperUnbounded) &&
             (atom.upperExpression || atom.realHighBits)))) ||
          (integralAtom &&
           (atom.realLowBits || atom.realHighBits ||
            (!deferredAtom && uint64_t{atom.limbCount} != expectedLimbs))) ||
          (deferredAtom && !needsResolution) ||
          (!integralAtom && atom.limbCount != 0) ||
          (!integralAtom && deferredAtom &&
           (atom.realLowBits || atom.realHighBits)))
        return fail(Status::InvalidReference, "functional_value_atom");
      for (uint32_t limbIndex = 0; limbIndex != atom.limbCount; ++limbIndex) {
        const FunctionalValueLimb &limb =
            db.functionalValueLimbs[atom.firstLimb + limbIndex];
        if (limb.wildcardMask)
          wildcardValueSets.insert(set.id);
        if (limb.valueSet != set.id || limb.atomOrdinal != atomIndex ||
            limb.ordinal != limbIndex)
          return fail(Status::InvalidReference, "functional_value_limb");
        if (atom.kind == FunctionalValueAtomKind::IntegralValue &&
            (limb.lowAval != limb.highAval || limb.lowBval != limb.highBval))
          return fail(Status::InvalidReference,
                      "functional_value_limb.scalar_range");
        if (limbIndex + 1 == atom.limbCount && set.bitWidth % 64) {
          uint64_t valid = (uint64_t{1} << (set.bitWidth % 64)) - 1;
          if (((limb.lowAval | limb.lowBval | limb.highAval | limb.highBval |
                limb.wildcardMask) &
               ~valid) != 0)
            return fail(Status::InvalidReference,
                        "functional_value_limb.unused_bits");
        }
      }
      if (!deferredAtom &&
          atom.kind == FunctionalValueAtomKind::IntegralRange) {
        bool knownRange = true;
        for (uint32_t limbIndex = 0; limbIndex != atom.limbCount; ++limbIndex) {
          const auto &limb =
              db.functionalValueLimbs[atom.firstLimb + limbIndex];
          knownRange &=
              limb.lowBval == 0 && limb.highBval == 0 && limb.wildcardMask == 0;
        }
        if (!knownRange ||
            !integralRangeOrdered(db.functionalValueLimbs, atom.firstLimb,
                                  atom.limbCount, set.bitWidth, set.signedness))
          return fail(Status::InvalidReference, "functional_value_atom.range");
      }
      if (!deferredAtom && !integralAtom) {
        double low = decodeReal(atom.realLowBits);
        double high = decodeReal(atom.realHighBits);
        if (!std::isfinite(low) || !std::isfinite(high) || low > high ||
            (low == high && atom.flags != (FunctionalValueAtomLowerInclusive |
                                           FunctionalValueAtomUpperInclusive)))
          return fail(Status::InvalidReference, "functional_value_atom.real");
      }
      limbCursor += atom.limbCount;
    }
    if (needsResolution != (set.setExpression || hasDeferredAtom) ||
        (set.setExpression && hasDeferredAtom))
      return fail(Status::InvalidReference, "functional_value_set.resolution");
    atomCursor += set.atomCount;
  }
  if (atomCursor != db.functionalValueAtoms.size() ||
      limbCursor != db.functionalValueLimbs.size())
    return fail(Status::InvalidReference, "functional_value_ranges");
  auto wildcardDefinitionValid = [&](const FunctionalValueSet &set) {
    for (uint32_t atomIndex = 0; atomIndex != set.atomCount; ++atomIndex) {
      const auto &atom = db.functionalValueAtoms[set.firstAtom + atomIndex];
      for (uint32_t limbIndex = 0; limbIndex != atom.limbCount; ++limbIndex) {
        const auto &limb = db.functionalValueLimbs[atom.firstLimb + limbIndex];
        if (limb.wildcardMask != limb.lowBval ||
            limb.wildcardMask != limb.highBval ||
            (limb.lowAval & limb.wildcardMask) ||
            (limb.highAval & limb.wildcardMask))
          return false;
      }
    }
    return true;
  };
  auto staticSetIsConcreteSingleton = [&](const FunctionalValueSet &set) {
    if (set.atomCount != 1)
      return false;
    const auto &atom = db.functionalValueAtoms[set.firstAtom];
    if (set.kind == FunctionalValueSetKind::Real)
      return decodeReal(atom.realLowBits) == decodeReal(atom.realHighBits);
    if (atom.kind != FunctionalValueAtomKind::IntegralValue)
      return false;
    for (uint32_t limbIndex = 0; limbIndex != atom.limbCount; ++limbIndex) {
      const auto &limb = db.functionalValueLimbs[atom.firstLimb + limbIndex];
      if (limb.lowAval != limb.highAval || limb.lowBval != limb.highBval ||
          limb.wildcardMask)
        return false;
    }
    return true;
  };

  if (!uniqueNonzero(db.functionalFormals,
                     [](const auto &v) { return v.id; }) ||
      !std::is_sorted(db.functionalFormals.begin(), db.functionalFormals.end(),
                      [](const auto &a, const auto &b) {
                        return std::make_tuple(a.type,
                                               static_cast<uint32_t>(a.kind),
                                               a.ordinal) <
                               std::make_tuple(b.type,
                                               static_cast<uint32_t>(b.kind),
                                               b.ordinal);
                      }))
    return fail(Status::UnsortedOrDuplicate, "functional_formal_order");
  std::map<std::pair<uint64_t, uint32_t>, uint32_t> nextFormalOrdinal;
  std::set<std::pair<uint64_t, std::string>> formalNames;
  std::map<uint64_t, const FunctionalFormal *> formalsByID;
  for (const auto &formal : db.functionalFormals) {
    const bool integral =
        formal.resultKind == FunctionalExpressionResultKind::Integral;
    const bool scalar =
        formal.resultKind == FunctionalExpressionResultKind::Boolean ||
        formal.resultKind == FunctionalExpressionResultKind::Integral ||
        formal.resultKind == FunctionalExpressionResultKind::Real ||
        formal.resultKind == FunctionalExpressionResultKind::String;
    auto key = std::make_pair(formal.type, static_cast<uint32_t>(formal.kind));
    if (!findFunctionalType(formal.type) || !text(formal.name) ||
        formal.name.empty() ||
        !enumBetween(formal.kind, FunctionalFormalKind::Constructor,
                     FunctionalFormalKind::Sample) ||
        !enumBetween(formal.direction, FunctionalFormalDirection::Input,
                     FunctionalFormalDirection::Ref) ||
        !scalar ||
        (formal.resultKind == FunctionalExpressionResultKind::String &&
         formal.kind == FunctionalFormalKind::Constructor &&
         formal.direction != FunctionalFormalDirection::Input) ||
        !enumBetween(formal.signedness, CoverageSignedness::Unsigned,
                     CoverageSignedness::NotApplicable) ||
        (integral &&
         (!formal.bitWidth ||
          formal.signedness == CoverageSignedness::NotApplicable)) ||
        (!integral &&
         (formal.bitWidth ||
          formal.signedness != CoverageSignedness::NotApplicable)) ||
        (formal.flags & ~FunctionalFormalHasDefault) ||
        (bool(formal.flags & FunctionalFormalHasDefault) !=
         bool(formal.defaultExpression)) ||
        formal.ordinal != nextFormalOrdinal[key]++ ||
        !formalNames.emplace(formal.type, formal.name).second ||
        !formalsByID.emplace(formal.id, &formal).second)
      return fail(Status::InvalidReference, "functional_formal");
  }
  auto findFormal = [&](uint64_t id) -> const FunctionalFormal * {
    auto found = formalsByID.find(id);
    return found == formalsByID.end() ? nullptr : found->second;
  };

  if (!uniqueNonzero(db.functionalExpressions,
                     [](const auto &v) { return v.id; }) ||
      !std::is_sorted(db.functionalExpressions.begin(),
                      db.functionalExpressions.end(),
                      [](const auto &a, const auto &b) { return a.id < b.id; }))
    return fail(Status::UnsortedOrDuplicate, "functional_expression_order");
  auto findExpression = [&](uint64_t id) -> const FunctionalExpression * {
    auto found = std::lower_bound(
        db.functionalExpressions.begin(), db.functionalExpressions.end(), id,
        [](const FunctionalExpression &entry, uint64_t value) {
          return entry.id < value;
        });
    return found != db.functionalExpressions.end() && found->id == id ? &*found
                                                                      : nullptr;
  };
  std::set<std::tuple<uint64_t, uint32_t, uint32_t, uint32_t, uint32_t>>
      expressionKeys;
  std::map<std::pair<uint64_t, uint32_t>, std::set<uint32_t>> resultOrdinals;
  for (const auto &expression : db.functionalExpressions) {
    bool ownerExists = false;
    uint64_t ownerType = 0;
    switch (expression.ownerKind) {
    case FunctionalExpressionOwnerKind::Type:
      ownerExists = hasID(db.functionalTypes, expression.owner);
      ownerType = expression.owner;
      break;
    case FunctionalExpressionOwnerKind::Item: {
      const FunctionalItem *owner = findItem(expression.owner);
      ownerExists = owner;
      ownerType = owner ? owner->type : 0;
      break;
    }
    case FunctionalExpressionOwnerKind::Bin: {
      const FunctionalBin *owner = findBin(expression.owner);
      const FunctionalItem *item = owner ? findItem(owner->item) : nullptr;
      ownerExists = owner;
      ownerType = item ? item->type : 0;
      break;
    }
    case FunctionalExpressionOwnerKind::Selector: {
      auto owner = std::find_if(
          db.crossSelectorNodes.begin(), db.crossSelectorNodes.end(),
          [&](const auto &v) { return v.id == expression.owner; });
      const FunctionalItem *item = owner == db.crossSelectorNodes.end()
                                       ? nullptr
                                       : findItem(owner->cross);
      ownerExists = owner != db.crossSelectorNodes.end();
      ownerType = item ? item->type : 0;
      break;
    }
    case FunctionalExpressionOwnerKind::ValueSet: {
      const FunctionalValueSet *owner = findValueSet(expression.owner);
      const FunctionalItem *item = owner ? findItem(owner->item) : nullptr;
      ownerExists = owner;
      ownerType = item ? item->type : 0;
      break;
    }
    case FunctionalExpressionOwnerKind::Formal: {
      const FunctionalFormal *owner = findFormal(expression.owner);
      ownerExists = owner;
      ownerType = owner ? owner->type : 0;
      break;
    }
    }
    bool validContract = false;
    switch (expression.role) {
    case FunctionalExpressionRole::SamplingEvent:
      validContract =
          expression.ownerKind == FunctionalExpressionOwnerKind::Type &&
          expression.resultKind == FunctionalExpressionResultKind::Boolean &&
          expression.evaluationPhase ==
              FunctionalExpressionEvaluationPhase::Event;
      break;
    case FunctionalExpressionRole::CoverpointSample:
      validContract =
          expression.ownerKind == FunctionalExpressionOwnerKind::Item &&
          findItem(expression.owner) &&
          findItem(expression.owner)->kind == FunctionalItemKind::Coverpoint &&
          expression.evaluationPhase ==
              FunctionalExpressionEvaluationPhase::Sample &&
          (expression.resultKind == FunctionalExpressionResultKind::Integral ||
           expression.resultKind == FunctionalExpressionResultKind::Real);
      break;
    case FunctionalExpressionRole::CoverpointIff:
      validContract =
          expression.resultKind == FunctionalExpressionResultKind::Boolean &&
          expression.ownerKind == FunctionalExpressionOwnerKind::Item &&
          findItem(expression.owner) &&
          findItem(expression.owner)->kind == FunctionalItemKind::Coverpoint &&
          expression.evaluationPhase ==
              FunctionalExpressionEvaluationPhase::Sample;
      break;
    case FunctionalExpressionRole::CrossIff:
      validContract =
          expression.resultKind == FunctionalExpressionResultKind::Boolean &&
          expression.ownerKind == FunctionalExpressionOwnerKind::Item &&
          findItem(expression.owner) &&
          findItem(expression.owner)->kind == FunctionalItemKind::Cross &&
          expression.evaluationPhase ==
              FunctionalExpressionEvaluationPhase::Sample;
      break;
    case FunctionalExpressionRole::BinIff:
      validContract =
          expression.resultKind == FunctionalExpressionResultKind::Boolean &&
          expression.ownerKind == FunctionalExpressionOwnerKind::Bin &&
          expression.evaluationPhase ==
              FunctionalExpressionEvaluationPhase::Sample;
      break;
    case FunctionalExpressionRole::BinSet:
      validContract =
          ((expression.ownerKind == FunctionalExpressionOwnerKind::ValueSet &&
            expression.resultKind == FunctionalExpressionResultKind::Set) ||
           (expression.ownerKind == FunctionalExpressionOwnerKind::Selector &&
            expression.resultKind ==
                FunctionalExpressionResultKind::TupleQueue)) &&
          expression.evaluationPhase ==
              FunctionalExpressionEvaluationPhase::Constructor;
      break;
    case FunctionalExpressionRole::BinWith: {
      const FunctionalBin *owner = findBin(expression.owner);
      validContract =
          expression.resultKind == FunctionalExpressionResultKind::Boolean &&
          expression.ownerKind == FunctionalExpressionOwnerKind::Bin && owner &&
          owner->kind == FunctionalBinKind::State &&
          expression.ownerSubordinal == 0 &&
          expression.evaluationPhase ==
              FunctionalExpressionEvaluationPhase::Constructor;
      break;
    }
    case FunctionalExpressionRole::SelectorWith:
      validContract =
          expression.resultKind == FunctionalExpressionResultKind::Boolean &&
          expression.ownerKind == FunctionalExpressionOwnerKind::Selector &&
          expression.ownerSubordinal == 0 &&
          expression.evaluationPhase ==
              FunctionalExpressionEvaluationPhase::Constructor;
      break;
    case FunctionalExpressionRole::ArrayCardinality:
      validContract =
          expression.ownerKind == FunctionalExpressionOwnerKind::Bin &&
          expression.resultKind == FunctionalExpressionResultKind::Integral &&
          expression.evaluationPhase ==
              FunctionalExpressionEvaluationPhase::Constructor;
      break;
    case FunctionalExpressionRole::RepeatLower:
    case FunctionalExpressionRole::RepeatUpper:
      validContract =
          expression.ownerKind == FunctionalExpressionOwnerKind::Bin &&
          expression.resultKind == FunctionalExpressionResultKind::Integral &&
          expression.evaluationPhase ==
              FunctionalExpressionEvaluationPhase::Constructor;
      break;
    case FunctionalExpressionRole::SelectorMatches:
      validContract =
          expression.ownerKind == FunctionalExpressionOwnerKind::Selector &&
          expression.resultKind == FunctionalExpressionResultKind::Integral &&
          expression.evaluationPhase ==
              FunctionalExpressionEvaluationPhase::Constructor;
      break;
    case FunctionalExpressionRole::OptionRHS:
      validContract =
          (expression.ownerKind == FunctionalExpressionOwnerKind::Type ||
           expression.ownerKind == FunctionalExpressionOwnerKind::Item) &&
          expression.evaluationPhase ==
              FunctionalExpressionEvaluationPhase::Option;
      break;
    case FunctionalExpressionRole::ValueAtomSingleton:
    case FunctionalExpressionRole::ValueAtomLower:
    case FunctionalExpressionRole::ValueAtomUpper: {
      const FunctionalValueSet *valueSet = findValueSet(expression.owner);
      validContract =
          expression.ownerKind == FunctionalExpressionOwnerKind::ValueSet &&
          valueSet &&
          ((valueSet->kind == FunctionalValueSetKind::Integral &&
            expression.resultKind ==
                FunctionalExpressionResultKind::Integral) ||
           (valueSet->kind == FunctionalValueSetKind::Real &&
            expression.resultKind == FunctionalExpressionResultKind::Real)) &&
          expression.evaluationPhase ==
              FunctionalExpressionEvaluationPhase::Constructor;
      break;
    }
    case FunctionalExpressionRole::FormalDefault: {
      const FunctionalFormal *formal = findFormal(expression.owner);
      validContract =
          formal &&
          expression.ownerKind == FunctionalExpressionOwnerKind::Formal &&
          formal->defaultExpression == expression.id &&
          expression.ownerOrdinal == 0 && expression.ownerSubordinal == 0 &&
          expression.resultKind == formal->resultKind &&
          expression.bitWidth == formal->bitWidth &&
          expression.signedness == formal->signedness &&
          expression.evaluationPhase ==
              (formal->kind == FunctionalFormalKind::Constructor
                   ? FunctionalExpressionEvaluationPhase::Constructor
                   : FunctionalExpressionEvaluationPhase::Sample);
      break;
    }
    }
    const bool scalarResult =
        expression.resultKind == FunctionalExpressionResultKind::Integral;
    const bool setResult =
        expression.resultKind == FunctionalExpressionResultKind::Set;
    const bool validSetType =
        !setResult ||
        (expression.bitWidth &&
         (expression.signedness != CoverageSignedness::NotApplicable ||
          ((expression.bitWidth == 32 || expression.bitWidth == 64) &&
           !(expression.flags & FunctionalExpressionSetElementFourState))));
    bool validFlags = expression.flags == 0;
    if (setResult)
      validFlags =
          !(expression.flags & ~FunctionalExpressionSetElementFourState);
    else if (expression.role == FunctionalExpressionRole::SamplingEvent) {
      constexpr uint32_t eventFlags = FunctionalExpressionSamplingEventBlock |
                                      FunctionalExpressionSamplingEventEnd;
      validFlags =
          !(expression.flags & ~eventFlags) &&
          (!(expression.flags & FunctionalExpressionSamplingEventEnd) ||
           (expression.flags & FunctionalExpressionSamplingEventBlock));
    }
    if (!ownerExists || !validFlags || !validContract || !ownerType ||
        (scalarResult &&
         (!expression.bitWidth ||
          expression.signedness == CoverageSignedness::NotApplicable)) ||
        !validSetType ||
        (!scalarResult && !setResult &&
         (expression.bitWidth ||
          expression.signedness != CoverageSignedness::NotApplicable)) ||
        !enumBetween(expression.ownerKind, FunctionalExpressionOwnerKind::Type,
                     FunctionalExpressionOwnerKind::Formal) ||
        !enumBetween(expression.role, FunctionalExpressionRole::SamplingEvent,
                     FunctionalExpressionRole::FormalDefault) ||
        !enumBetween(expression.resultKind,
                     FunctionalExpressionResultKind::Boolean,
                     FunctionalExpressionResultKind::TupleQueue) ||
        !enumBetween(expression.signedness, CoverageSignedness::Unsigned,
                     CoverageSignedness::NotApplicable) ||
        !enumBetween(expression.evaluationPhase,
                     FunctionalExpressionEvaluationPhase::Constructor,
                     FunctionalExpressionEvaluationPhase::Option) ||
        !expressionKeys
             .emplace(expression.owner,
                      static_cast<uint32_t>(expression.ownerKind),
                      static_cast<uint32_t>(expression.role),
                      expression.ownerOrdinal, expression.ownerSubordinal)
             .second ||
        !resultOrdinals[{ownerType,
                         static_cast<uint32_t>(expression.evaluationPhase)}]
             .insert(expression.resultOrdinal)
             .second)
      return fail(Status::InvalidReference, "functional_expression",
                  "expression " + std::to_string(expression.id));
  }
  for (const auto &formal : db.functionalFormals) {
    const FunctionalExpression *expression =
        formal.defaultExpression ? findExpression(formal.defaultExpression)
                                 : nullptr;
    if (formal.defaultExpression &&
        (!expression || expression->owner != formal.id ||
         expression->ownerKind != FunctionalExpressionOwnerKind::Formal ||
         expression->role != FunctionalExpressionRole::FormalDefault))
      return fail(Status::InvalidReference,
                  "functional_formal.default_expression");
  }
  for (const auto &[key, ordinals] : resultOrdinals)
    if (!ordinals.empty() &&
        (ordinals.size() != uint64_t(*ordinals.rbegin()) + 1))
      return fail(Status::InvalidReference,
                  "functional_expression.result_ordinal");
  auto expressionIs = [&](uint64_t id, uint64_t owner,
                          FunctionalExpressionOwnerKind ownerKind,
                          FunctionalExpressionRole role, uint32_t ordinal,
                          uint32_t subordinal) {
    if (!id)
      return true;
    const FunctionalExpression *expression = findExpression(id);
    return expression && expression->owner == owner &&
           expression->ownerKind == ownerKind && expression->role == role &&
           expression->ownerOrdinal == ordinal &&
           expression->ownerSubordinal == subordinal;
  };
  for (const auto &set : db.functionalValueSets) {
    if (!expressionIs(set.setExpression, set.id,
                      FunctionalExpressionOwnerKind::ValueSet,
                      FunctionalExpressionRole::BinSet, 0, 0))
      return fail(Status::InvalidReference, "functional_value_set.expression");
    for (uint32_t atomOrdinal = 0; atomOrdinal != set.atomCount;
         ++atomOrdinal) {
      const auto &atom = db.functionalValueAtoms[set.firstAtom + atomOrdinal];
      FunctionalExpressionRole lowerRole =
          atom.kind == FunctionalValueAtomKind::IntegralValue ||
                  (atom.kind == FunctionalValueAtomKind::RealInterval &&
                   !atom.upperExpression &&
                   !(atom.flags & FunctionalValueAtomUpperUnbounded))
              ? FunctionalExpressionRole::ValueAtomSingleton
              : FunctionalExpressionRole::ValueAtomLower;
      bool deferred = (set.kind == FunctionalValueSetKind::Integral &&
                       atom.limbCount == 0) ||
                      (set.kind == FunctionalValueSetKind::Real &&
                       (atom.lowerExpression || atom.upperExpression ||
                        (atom.flags & (FunctionalValueAtomLowerUnbounded |
                                       FunctionalValueAtomUpperUnbounded))));
      if (!expressionIs(atom.lowerExpression, set.id,
                        FunctionalExpressionOwnerKind::ValueSet, lowerRole,
                        atomOrdinal, 0) ||
          !expressionIs(atom.upperExpression, set.id,
                        FunctionalExpressionOwnerKind::ValueSet,
                        FunctionalExpressionRole::ValueAtomUpper, atomOrdinal,
                        0) ||
          (atom.kind == FunctionalValueAtomKind::IntegralValue &&
           atom.upperExpression) ||
          (deferred && !(atom.flags & FunctionalValueAtomLowerUnbounded) &&
           !atom.lowerExpression) ||
          ((atom.flags & FunctionalValueAtomLowerUnbounded) &&
           atom.lowerExpression) ||
          ((atom.flags & FunctionalValueAtomUpperUnbounded) &&
           atom.upperExpression) ||
          ((atom.flags & FunctionalValueAtomLowerUnbounded) &&
           !atom.upperExpression) ||
          (deferred && atom.kind == FunctionalValueAtomKind::IntegralRange &&
           !(atom.flags & FunctionalValueAtomUpperUnbounded) &&
           !atom.upperExpression) ||
          (!deferred && (atom.lowerExpression || atom.upperExpression)))
        return fail(Status::InvalidReference,
                    "functional_value_atom.expression");
    }
  }

  std::map<uint64_t, const FunctionalBinPlan *> binPlans;
  for (const auto &plan : db.functionalBinPlans) {
    const FunctionalBin *bin = findBin(plan.bin);
    const FunctionalValueSet *set =
        plan.valueSet ? findValueSet(plan.valueSet) : nullptr;
    bool scalar = plan.arrayMode == FunctionalBinArrayMode::Scalar;
    bool unsized = plan.arrayMode == FunctionalBinArrayMode::Unsized;
    bool fixed = plan.arrayMode == FunctionalBinArrayMode::Fixed;
    bool state = bin && bin->kind == FunctionalBinKind::State;
    bool transition = bin && bin->kind == FunctionalBinKind::Transition;
    bool stateNeedsSet = state && !(bin->flags & FunctionalBinDefault);
    std::vector<const FunctionalExpression *> withExpressions;
    for (const auto &expression : db.functionalExpressions)
      if (expression.owner == plan.bin &&
          expression.ownerKind == FunctionalExpressionOwnerKind::Bin &&
          expression.role == FunctionalExpressionRole::BinWith)
        withExpressions.push_back(&expression);
    std::sort(withExpressions.begin(), withExpressions.end(),
              [](const auto *lhs, const auto *rhs) {
                return lhs->ownerOrdinal < rhs->ownerOrdinal;
              });
    bool validWithOrdinals = true;
    for (uint32_t ordinal = 0; ordinal != withExpressions.size(); ++ordinal)
      validWithOrdinals &= withExpressions[ordinal]->ownerOrdinal == ordinal &&
                           withExpressions[ordinal]->semanticDigest ==
                               withExpressions.front()->semanticDigest;
    const FunctionalExpression *sampleExpression = nullptr;
    if (bin)
      for (const auto &expression : db.functionalExpressions)
        if (expression.owner == bin->item &&
            expression.ownerKind == FunctionalExpressionOwnerKind::Item &&
            expression.role == FunctionalExpressionRole::CoverpointSample) {
          sampleExpression = &expression;
          break;
        }
    bool realState =
        state && sampleExpression &&
        sampleExpression->resultKind == FunctionalExpressionResultKind::Real;
    bool wildcardEncodingValid = !bin ||
                                 !(bin->flags & FunctionalBinWildcard) ||
                                 !set || wildcardDefinitionValid(*set);
    if (!bin || (!state && !transition) ||
        (stateNeedsSet != (plan.valueSet != 0)) ||
        (plan.valueSet && (!set || set->item != bin->item)) ||
        (plan.valueSet && wildcardValueSets.count(plan.valueSet) &&
         !(bin->flags & FunctionalBinWildcard)) ||
        (!withExpressions.empty() &&
         (!validWithOrdinals ||
          withExpressions.size() > MaxFunctionalWithCandidates || !state ||
          !stateNeedsSet || !set ||
          set->kind != FunctionalValueSetKind::Integral || set->setExpression ||
          (bin->flags & FunctionalBinWildcard))) ||
        !wildcardEncodingValid ||
        !expressionIs(plan.iffExpression, plan.bin,
                      FunctionalExpressionOwnerKind::Bin,
                      FunctionalExpressionRole::BinIff, 0, 0) ||
        !expressionIs(plan.cardinalityExpression, plan.bin,
                      FunctionalExpressionOwnerKind::Bin,
                      FunctionalExpressionRole::ArrayCardinality, 0, 0) ||
        (!scalar && !unsized && !fixed) ||
        (scalar &&
         (plan.arrayCardinality || plan.cardinalityExpression ||
          plan.distribution != FunctionalBinDistributionKind::None)) ||
        (unsized &&
         (plan.arrayCardinality || plan.cardinalityExpression ||
          plan.distribution != FunctionalBinDistributionKind::PerValue)) ||
        (fixed &&
         ((bool(plan.arrayCardinality) == bool(plan.cardinalityExpression)) ||
          plan.distribution != FunctionalBinDistributionKind::Uniform)) ||
        (state && (bin->flags & FunctionalBinDefault) && fixed) ||
        (realState && (bin->flags & FunctionalBinDefault) && unsized) ||
        (transition && fixed) ||
        (transition && (bin->flags & FunctionalBinDefaultSequence) &&
         !scalar) ||
        plan.flags != 0 || !binPlans.emplace(plan.bin, &plan).second)
      return fail(Status::InvalidReference, "functional_bin_plan");
  }
  if (!std::is_sorted(
          db.functionalBinPlans.begin(), db.functionalBinPlans.end(),
          [](const auto &a, const auto &b) { return a.bin < b.bin; }))
    return fail(Status::UnsortedOrDuplicate, "functional_bin_plan_order");
  for (const auto &bin : db.functionalBins)
    if ((bin.kind != FunctionalBinKind::Cross) != (binPlans.count(bin.id) != 0))
      return fail(Status::InvalidReference, "functional_bin_plan.completeness");

  std::set<std::tuple<uint32_t, uint64_t, uint32_t, uint32_t>> optionPlanKeys;
  for (const auto &plan : db.functionalOptionPlans) {
    bool group =
        plan.ownerKind == FunctionalConfigurationOptionOwnerKind::Group;
    bool validOwner = group ? hasID(db.functionalTypes, plan.owner)
                            : findItem(plan.owner) != nullptr;
    FunctionalExpressionOwnerKind expressionOwner =
        group ? FunctionalExpressionOwnerKind::Type
              : FunctionalExpressionOwnerKind::Item;
    const FunctionalItem *ownerItem = group ? nullptr : findItem(plan.owner);
    const FunctionalType *ownerType = findFunctionalType(
        group ? plan.owner : (ownerItem ? ownerItem->type : 0));
    FunctionalItemKind ownerItemKind =
        ownerItem ? ownerItem->kind : FunctionalItemKind::Coverpoint;
    bool typeScope = plan.scope == FunctionalOptionScopeKind::Type;
    bool optionLegal = false;
    switch (plan.option) {
    case FunctionalConfigurationOptionKind::Name:
    case FunctionalConfigurationOptionKind::PerInstance:
    case FunctionalConfigurationOptionKind::GetInstCoverage:
      optionLegal = !typeScope && group;
      break;
    case FunctionalConfigurationOptionKind::Strobe:
    case FunctionalConfigurationOptionKind::MergeInstances:
    case FunctionalConfigurationOptionKind::DistributeFirst:
      optionLegal = typeScope && group;
      break;
    case FunctionalConfigurationOptionKind::Weight:
    case FunctionalConfigurationOptionKind::Goal:
    case FunctionalConfigurationOptionKind::Comment:
      optionLegal = true;
      break;
    case FunctionalConfigurationOptionKind::AtLeast:
      optionLegal = !typeScope;
      break;
    case FunctionalConfigurationOptionKind::AutoBinMax:
    case FunctionalConfigurationOptionKind::DetectOverlap:
      optionLegal = !typeScope &&
                    (group || ownerItemKind == FunctionalItemKind::Coverpoint);
      break;
    case FunctionalConfigurationOptionKind::CrossNumPrintMissing:
      optionLegal =
          !typeScope && (group || ownerItemKind == FunctionalItemKind::Cross);
      break;
    case FunctionalConfigurationOptionKind::RealInterval:
      optionLegal = ownerType && ownerType->languageVersion >= 2023 &&
                    typeScope &&
                    (group || ownerItemKind == FunctionalItemKind::Coverpoint);
      break;
    case FunctionalConfigurationOptionKind::CrossRetainAutoBins:
      optionLegal = ownerType && ownerType->languageVersion >= 2023 &&
                    !typeScope &&
                    (group || ownerItemKind == FunctionalItemKind::Cross);
      break;
    }
    const FunctionalExpression *rhs = findExpression(plan.expression);
    FunctionalExpressionResultKind expectedResult =
        FunctionalExpressionResultKind::Integral;
    switch (plan.option) {
    case FunctionalConfigurationOptionKind::Name:
    case FunctionalConfigurationOptionKind::Comment:
      expectedResult = FunctionalExpressionResultKind::String;
      break;
    case FunctionalConfigurationOptionKind::DetectOverlap:
    case FunctionalConfigurationOptionKind::PerInstance:
    case FunctionalConfigurationOptionKind::MergeInstances:
    case FunctionalConfigurationOptionKind::GetInstCoverage:
    case FunctionalConfigurationOptionKind::DistributeFirst:
    case FunctionalConfigurationOptionKind::Strobe:
    case FunctionalConfigurationOptionKind::CrossRetainAutoBins:
      expectedResult = FunctionalExpressionResultKind::Boolean;
      break;
    case FunctionalConfigurationOptionKind::RealInterval:
      expectedResult = FunctionalExpressionResultKind::Real;
      break;
    default:
      break;
    }
    if (!validOwner || !ownerType || !optionLegal || !rhs ||
        rhs->resultKind != expectedResult ||
        !enumBetween(plan.ownerKind,
                     FunctionalConfigurationOptionOwnerKind::Group,
                     FunctionalConfigurationOptionOwnerKind::Item) ||
        !enumBetween(plan.scope, FunctionalOptionScopeKind::Instance,
                     FunctionalOptionScopeKind::Type) ||
        !enumBetween(plan.option, FunctionalConfigurationOptionKind::Goal,
                     FunctionalConfigurationOptionKind::CrossRetainAutoBins) ||
        !expressionIs(plan.expression, plan.owner, expressionOwner,
                      FunctionalExpressionRole::OptionRHS, plan.ordinal,
                      static_cast<uint32_t>(plan.scope)) ||
        plan.flags != 0 ||
        !optionPlanKeys
             .emplace(static_cast<uint32_t>(plan.ownerKind), plan.owner,
                      static_cast<uint32_t>(plan.scope),
                      static_cast<uint32_t>(plan.option))
             .second)
      return fail(Status::InvalidReference, "functional_option_plan");
  }
  if (!std::is_sorted(
          db.functionalOptionPlans.begin(), db.functionalOptionPlans.end(),
          [](const auto &a, const auto &b) {
            return std::tie(a.ownerKind, a.owner, a.scope, a.option,
                            a.ordinal) <
                   std::tie(b.ownerKind, b.owner, b.scope, b.option, b.ordinal);
          }))
    return fail(Status::UnsortedOrDuplicate, "functional_option_plan_order");

  size_t alternativeCursor = 0;
  size_t stepCursor = 0;
  uint64_t previousProgramBin = 0;
  std::set<uint64_t> programmedBins;
  for (const auto &program : db.transitionPrograms) {
    const FunctionalBin *bin = findBin(program.bin);
    if (!bin || bin->kind != FunctionalBinKind::Transition ||
        bin->item != program.item || program.bin <= previousProgramBin ||
        program.flags != 0 || program.firstAlternative != alternativeCursor ||
        !checkedRange(program.firstAlternative, program.alternativeCount,
                      db.transitionAlternatives.size()) ||
        (!(bin->flags & FunctionalBinDefaultSequence) &&
         !program.alternativeCount) ||
        ((bin->flags & FunctionalBinDefaultSequence) &&
         program.alternativeCount) ||
        !programmedBins.insert(program.bin).second)
      return fail(Status::InvalidReference, "transition_program");
    previousProgramBin = program.bin;
    for (uint32_t alternativeIndex = 0;
         alternativeIndex != program.alternativeCount; ++alternativeIndex) {
      const TransitionAlternative &alternative =
          db.transitionAlternatives[program.firstAlternative +
                                    alternativeIndex];
      const FunctionalValueSet *terminal =
          findValueSet(alternative.terminalValueSet);
      if (alternative.bin != program.bin ||
          alternative.ordinal != alternativeIndex || alternative.flags != 0 ||
          !terminal || terminal->item != program.item ||
          alternative.firstStep != stepCursor || !alternative.stepCount ||
          !checkedRange(alternative.firstStep, alternative.stepCount,
                        db.transitionSteps.size()))
        return fail(Status::InvalidReference, "transition_alternative");
      for (uint32_t stepIndex = 0; stepIndex != alternative.stepCount;
           ++stepIndex) {
        const TransitionStep &step =
            db.transitionSteps[alternative.firstStep + stepIndex];
        const FunctionalValueSet *valueSet = findValueSet(step.valueSet);
        uint32_t repetition = static_cast<uint32_t>(step.repetition);
        bool unresolved = (step.flags & TransitionStepNeedsResolution) != 0;
        bool validBounds =
            unresolved ||
            (step.lowerBound >= 1 && step.upperBound >= step.lowerBound &&
             (step.repetition != TransitionRepetitionKind::Once ||
              (step.lowerBound == 1 && step.upperBound == 1)) &&
             (step.repetition != TransitionRepetitionKind::Consecutive ||
              step.upperBound != TransitionUnbounded));
        if (step.bin != program.bin ||
            step.alternativeOrdinal != alternativeIndex ||
            step.ordinal != stepIndex || !valueSet ||
            valueSet->item != program.item ||
            (wildcardValueSets.count(step.valueSet) &&
             !(bin->flags & FunctionalBinWildcard)) ||
            ((bin->flags & FunctionalBinWildcard) &&
             !wildcardDefinitionValid(*valueSet)) ||
            !expressionIs(step.lowerExpression, step.bin,
                          FunctionalExpressionOwnerKind::Bin,
                          FunctionalExpressionRole::RepeatLower,
                          alternativeIndex, stepIndex) ||
            !expressionIs(step.upperExpression, step.bin,
                          FunctionalExpressionOwnerKind::Bin,
                          FunctionalExpressionRole::RepeatUpper,
                          alternativeIndex, stepIndex) ||
            !knownTransitionRepetition(repetition) ||
            (step.flags & ~TransitionStepNeedsResolution) != 0 ||
            (unresolved && !step.lowerExpression) ||
            (unresolved && step.repetition == TransitionRepetitionKind::Once) ||
            (!unresolved && (step.lowerExpression || step.upperExpression)) ||
            !validBounds)
          return fail(Status::InvalidReference, "transition_step");
        bool variableLength =
            step.repetition == TransitionRepetitionKind::Goto ||
            step.repetition == TransitionRepetitionKind::Nonconsecutive ||
            step.upperBound == TransitionUnbounded;
        bool arrayed =
            binPlans.at(bin->id)->arrayMode == FunctionalBinArrayMode::Unsized;
        if (arrayed && step.repetition != TransitionRepetitionKind::Once &&
            step.repetition != TransitionRepetitionKind::Consecutive)
          return fail(Status::InvalidReference,
                      "transition_step.array_repetition");
        if (((variableLength || unresolved) &&
             (bin->flags & (FunctionalBinIgnore | FunctionalBinIllegal))) ||
            (variableLength && arrayed))
          return fail(Status::InvalidReference,
                      "transition_step.unbounded_role");
      }
      const TransitionStep &last =
          db.transitionSteps[alternative.firstStep + alternative.stepCount - 1];
      if (last.valueSet != alternative.terminalValueSet ||
          (alternative.stepCount == 1 &&
           !((last.flags & TransitionStepNeedsResolution) != 0) &&
           last.lowerBound == 1 && last.upperBound == 1))
        return fail(Status::InvalidReference, "transition_alternative.length");
      stepCursor += alternative.stepCount;
    }
    alternativeCursor += program.alternativeCount;
  }
  if (alternativeCursor != db.transitionAlternatives.size() ||
      stepCursor != db.transitionSteps.size())
    return fail(Status::InvalidReference, "transition_ranges");
  for (const auto &bin : db.functionalBins)
    if ((bin.kind == FunctionalBinKind::Transition) !=
        (programmedBins.count(bin.id) != 0))
      return fail(Status::InvalidReference, "transition_program.completeness");

  size_t targetCursor = 0;
  size_t crossBinCursor = 0;
  uint64_t previousCross = 0;
  std::map<uint64_t, std::vector<uint64_t>> targetsByCross;
  for (const auto &plan : db.crossPlans) {
    const FunctionalItem *cross = findItem(plan.item);
    uint32_t retain = static_cast<uint32_t>(plan.retainAutoPolicy);
    if (!cross || cross->kind != FunctionalItemKind::Cross ||
        plan.item <= previousCross || plan.targetCount < 2 ||
        plan.firstTarget != targetCursor ||
        !checkedRange(plan.firstTarget, plan.targetCount,
                      db.crossTargets.size()) ||
        plan.firstBin != crossBinCursor ||
        !checkedRange(plan.firstBin, plan.binCount, db.crossBins.size()) ||
        !knownCrossRetainPolicy(retain) || plan.flags != 0 ||
        !plan.tupleElementType || !plan.tupleProvenanceSpan ||
        plan.tupleProvenanceSpan > UINT64_MAX - 7 ||
        (plan.tupleProvenanceSpan + 7) / 8 > ParseLimits{}.maxSectionBytes ||
        (plan.tupleFlags & ~CrossTupleFourState) != 0 ||
        !expressionIs(plan.iffExpression, plan.item,
                      FunctionalExpressionOwnerKind::Item,
                      FunctionalExpressionRole::CrossIff, 0, 0))
      return fail(Status::InvalidReference, "cross_plan");
    previousCross = plan.item;
    std::set<uint64_t> uniqueTargets;
    auto &targetIDs = targetsByCross[plan.item];
    uint64_t previousTupleEnd = 0;
    bool tupleFourState = false;
    for (uint32_t targetIndex = 0; targetIndex != plan.targetCount;
         ++targetIndex) {
      const CrossTarget &target =
          db.crossTargets[plan.firstTarget + targetIndex];
      const FunctionalItem *targetItem = findItem(target.target);
      const FunctionalExpression *sample = nullptr;
      for (const FunctionalExpression &expression : db.functionalExpressions)
        if (expression.owner == target.target &&
            expression.ownerKind == FunctionalExpressionOwnerKind::Item &&
            expression.role == FunctionalExpressionRole::CoverpointSample) {
          sample = &expression;
          break;
        }
      const bool integral =
          target.tupleResultKind == FunctionalExpressionResultKind::Integral;
      const bool real =
          target.tupleResultKind == FunctionalExpressionResultKind::Real;
      const bool fieldFourState =
          (target.tupleFlags & CrossTupleFieldFourState) != 0;
      const bool domainFourState =
          (target.tupleFlags & CrossTargetDomainFourState) != 0;
      const bool validIntegral =
          integral && target.tupleBitWidth &&
          (target.tupleSignedness == CoverageSignedness::Signed ||
           target.tupleSignedness == CoverageSignedness::Unsigned);
      const bool validReal =
          real && target.tupleBitWidth == 64 &&
          target.tupleSignedness == CoverageSignedness::NotApplicable &&
          !fieldFourState && (target.tupleBitOffset & 63) == 0;
      if (target.cross != plan.item || target.ordinal != targetIndex ||
          !targetItem || targetItem->kind != FunctionalItemKind::Coverpoint ||
          targetItem->type != cross->type ||
          !uniqueTargets.insert(target.target).second || !sample ||
          sample->resultKind != target.tupleResultKind ||
          (integral && sample->bitWidth != target.tupleBitWidth) ||
          sample->signedness != target.tupleSignedness ||
          (target.tupleFlags &
           ~(CrossTupleFieldFourState | CrossTargetDomainFourState)) != 0 ||
          (domainFourState && !fieldFourState) ||
          (!validIntegral && !validReal) ||
          target.tupleBitOffset < previousTupleEnd ||
          target.tupleBitOffset > plan.tupleProvenanceSpan ||
          target.tupleBitWidth >
              plan.tupleProvenanceSpan - target.tupleBitOffset)
        return fail(Status::InvalidReference, "cross_target");
      previousTupleEnd = target.tupleBitOffset + target.tupleBitWidth;
      tupleFourState |= fieldFourState;
      targetIDs.push_back(target.target);
    }
    if (tupleFourState != ((plan.tupleFlags & CrossTupleFourState) != 0) ||
        previousTupleEnd > plan.tupleProvenanceSpan)
      return fail(Status::InvalidReference, "cross_plan.tuple_layout");
    targetCursor += plan.targetCount;
    crossBinCursor += plan.binCount;
  }
  if (targetCursor != db.crossTargets.size() ||
      crossBinCursor != db.crossBins.size())
    return fail(Status::InvalidReference, "cross_ranges");
  if (!uniqueNonzero(db.crossSelectorNodes, [](const auto &v) { return v.id; }))
    return fail(Status::UnsortedOrDuplicate, "cross_selector_node.id");
  std::map<uint64_t, const CrossSelectorNode *> nodesByID;
  size_t operandCursor = 0;
  uint64_t nodeCross = 0;
  uint32_t expectedNodeOrdinal = 0;
  for (const auto &node : db.crossSelectorNodes) {
    if (node.cross != nodeCross) {
      if (nodeCross && node.cross <= nodeCross)
        return fail(Status::UnsortedOrDuplicate, "cross_selector_node_order");
      nodeCross = node.cross;
      expectedNodeOrdinal = 0;
    }
    const FunctionalItem *cross = findItem(node.cross);
    auto targetList = targetsByCross.find(node.cross);
    uint32_t kind = static_cast<uint32_t>(node.kind);
    if (!cross || cross->kind != FunctionalItemKind::Cross ||
        targetList == targetsByCross.end() ||
        node.ordinal != expectedNodeOrdinal++ || node.flags != 0 ||
        !knownCrossSelectorKind(kind) || node.firstOperand != operandCursor ||
        !checkedRange(node.firstOperand, node.operandCount,
                      db.crossSelectorOperands.size()) ||
        !nodesByID.emplace(node.id, &node).second)
      return fail(Status::InvalidReference, "cross_selector_node");
    auto targetIsCrossed = [&](uint64_t target) {
      return std::find(targetList->second.begin(), targetList->second.end(),
                       target) != targetList->second.end();
    };
    auto validSet = [&](uint64_t setID) {
      const FunctionalValueSet *set = setID ? findValueSet(setID) : nullptr;
      return !setID || (set && set->item == node.target &&
                        !wildcardValueSets.count(setID));
    };
    unsigned expectedOperands = 0;
    bool validPayload = false;
    switch (node.kind) {
    case CrossSelectorKind::Binsof: {
      const FunctionalBin *selectedBin = node.bin ? findBin(node.bin) : nullptr;
      validPayload =
          targetIsCrossed(node.target) &&
          (!node.bin || (selectedBin && selectedBin->item == node.target)) &&
          validSet(node.valueSet) && !node.constructionExpression &&
          !node.tupleSet;
      break;
    }
    case CrossSelectorKind::Not:
      expectedOperands = 1;
      validPayload = !node.target && !node.bin && !node.valueSet &&
                     !node.constructionExpression && !node.tupleSet;
      break;
    case CrossSelectorKind::And:
    case CrossSelectorKind::Or:
      expectedOperands = 2;
      validPayload = !node.target && !node.bin && !node.valueSet &&
                     !node.constructionExpression && !node.tupleSet;
      break;
    case CrossSelectorKind::Set:
      expectedOperands = 0;
      validPayload =
          !node.target && !node.bin && !node.valueSet &&
          ((node.constructionExpression != 0) != (node.tupleSet != 0));
      break;
    case CrossSelectorKind::AllTuples:
      expectedOperands = 0;
      validPayload = !node.target && !node.bin && !node.valueSet &&
                     !node.constructionExpression && !node.tupleSet;
      break;
    case CrossSelectorKind::With:
      expectedOperands = 1;
      validPayload = !node.target && !node.bin && !node.valueSet &&
                     node.withExpression && !node.constructionExpression &&
                     !node.tupleSet;
      break;
    }
    bool hasMatchSubject =
        node.withExpression || node.constructionExpression || node.tupleSet;
    const FunctionalExpression *construction =
        node.constructionExpression
            ? findExpression(node.constructionExpression)
            : nullptr;
    bool validMatches =
        (node.matchesPolicy == CrossMatchesPolicy::None &&
         node.matchesCount == 0 && !node.matchesExpression &&
         !hasMatchSubject) ||
        (node.matchesPolicy == CrossMatchesPolicy::Count &&
         ((node.matchesExpression && node.matchesCount == 0) ||
          (!node.matchesExpression && node.matchesCount != 0))) ||
        (node.matchesPolicy == CrossMatchesPolicy::All &&
         node.matchesCount == 0 && !node.matchesExpression);
    if (node.constructionExpression &&
        node.matchesPolicy == CrossMatchesPolicy::Count &&
        !node.matchesExpression &&
        node.matchesCount > ParseLimits{}.maxRecords + 1)
      validMatches = false;
    if (!validPayload || node.operandCount != expectedOperands ||
        !validMatches ||
        ((node.matchesPolicy != CrossMatchesPolicy::None) != hasMatchSubject) ||
        !expressionIs(node.withExpression, node.id,
                      FunctionalExpressionOwnerKind::Selector,
                      FunctionalExpressionRole::SelectorWith, 0, 0) ||
        !expressionIs(node.constructionExpression, node.id,
                      FunctionalExpressionOwnerKind::Selector,
                      FunctionalExpressionRole::BinSet, 0, 0) ||
        (construction && construction->resultKind !=
                             FunctionalExpressionResultKind::TupleQueue) ||
        !expressionIs(node.matchesExpression, node.id,
                      FunctionalExpressionOwnerKind::Selector,
                      FunctionalExpressionRole::SelectorMatches, 0, 0))
      return fail(Status::InvalidReference, "cross_selector_node.payload");
    for (uint32_t operandIndex = 0; operandIndex != node.operandCount;
         ++operandIndex) {
      const CrossSelectorOperand &operand =
          db.crossSelectorOperands[node.firstOperand + operandIndex];
      auto referenced = nodesByID.find(operand.operand);
      if (operand.node != node.id || operand.ordinal != operandIndex ||
          referenced == nodesByID.end() ||
          referenced->second->cross != node.cross ||
          referenced->second->ordinal >= node.ordinal)
        return fail(Status::InvalidReference, "cross_selector_operand");
    }
    operandCursor += node.operandCount;
  }
  if (operandCursor != db.crossSelectorOperands.size())
    return fail(Status::InvalidReference, "cross_selector_operand_range");

  // A cross `with` expression is evaluated once for every value tuple in the
  // complete finite target domain.  The compiler records that constructor
  // batch as dense SelectorWith expressions owned by the decorated selector;
  // the first member is named by CrossSelectorNode::withExpression.  Keeping
  // this relationship structural makes malformed helper payloads fail before
  // the runtime can bind a partially resolved configuration.
  for (const auto &node : db.crossSelectorNodes) {
    std::vector<const FunctionalExpression *> withExpressions;
    for (const auto &expression : db.functionalExpressions)
      if (expression.owner == node.id &&
          expression.ownerKind == FunctionalExpressionOwnerKind::Selector &&
          expression.role == FunctionalExpressionRole::SelectorWith)
        withExpressions.push_back(&expression);
    std::sort(withExpressions.begin(), withExpressions.end(),
              [](const auto *left, const auto *right) {
                return left->ownerOrdinal < right->ownerOrdinal;
              });
    if (!node.withExpression) {
      if (!withExpressions.empty())
        return fail(Status::InvalidReference,
                    "cross_selector_node.with_expression_batch");
      continue;
    }
    auto targets = targetsByCross.find(node.cross);
    uint64_t candidateCount = 1;
    bool validBatch = targets != targetsByCross.end() &&
                      !withExpressions.empty() &&
                      withExpressions.front()->id == node.withExpression;
    Digest semanticDigest =
        validBatch ? withExpressions.front()->semanticDigest : Digest{};
    if (validBatch) {
      auto plan = std::find_if(db.crossPlans.begin(), db.crossPlans.end(),
                               [&](const CrossPlan &candidate) {
                                 return candidate.item == node.cross;
                               });
      validBatch = plan != db.crossPlans.end();
      for (uint32_t ordinal = 0; validBatch && ordinal != plan->targetCount;
           ++ordinal) {
        const CrossTarget &target =
            db.crossTargets[plan->firstTarget + ordinal];
        const FunctionalExpression *sample = nullptr;
        for (const auto &expression : db.functionalExpressions)
          if (expression.owner == target.target &&
              expression.ownerKind == FunctionalExpressionOwnerKind::Item &&
              expression.role == FunctionalExpressionRole::CoverpointSample) {
            sample = &expression;
            break;
          }
        const uint64_t domainBits =
            sample ? uint64_t{sample->bitWidth} *
                         ((target.tupleFlags & CrossTargetDomainFourState) ? 2u
                                                                           : 1u)
                   : 0;
        if (!sample ||
            sample->resultKind != FunctionalExpressionResultKind::Integral ||
            !sample->bitWidth || domainBits >= 64 ||
            candidateCount > uint64_t{MaxFunctionalCrossWithCandidates} >>
                domainBits) {
          validBatch = false;
          break;
        }
        candidateCount <<= domainBits;
      }
    }
    if (!validBatch || candidateCount > MaxFunctionalCrossWithCandidates ||
        withExpressions.size() != candidateCount)
      return fail(Status::InvalidReference,
                  "cross_selector_node.with_expression_batch");
    for (size_t ordinal = 0; ordinal != withExpressions.size(); ++ordinal) {
      const FunctionalExpression &expression = *withExpressions[ordinal];
      if (expression.ownerOrdinal != ordinal || expression.ownerSubordinal ||
          expression.semanticDigest != semanticDigest ||
          expression.resultKind != FunctionalExpressionResultKind::Boolean ||
          expression.evaluationPhase !=
              FunctionalExpressionEvaluationPhase::Constructor)
        return fail(Status::InvalidReference,
                    "cross_selector_node.with_expression_batch");
    }
  }
  size_t seenCrossBins = 0;
  std::set<uint64_t> plannedCrossBins;
  std::set<uint64_t> reachableSelectors;
  for (const auto &plan : db.crossPlans) {
    for (uint32_t binIndex = 0; binIndex != plan.binCount; ++binIndex) {
      const CrossBinPlan &crossBin = db.crossBins[plan.firstBin + binIndex];
      const FunctionalBin *bin = findBin(crossBin.bin);
      auto root = nodesByID.find(crossBin.rootSelector);
      if (!bin || bin->kind != FunctionalBinKind::Cross ||
          bin->item != plan.item || crossBin.cross != plan.item ||
          !crossBin.rootSelector || root == nodesByID.end() ||
          root->second->cross != plan.item || crossBin.flags != 0 ||
          !plannedCrossBins.insert(crossBin.bin).second)
        return fail(Status::InvalidReference, "cross_bin");
      std::vector<uint64_t> worklist{crossBin.rootSelector};
      while (!worklist.empty()) {
        uint64_t id = worklist.back();
        worklist.pop_back();
        if (!reachableSelectors.insert(id).second)
          continue;
        const CrossSelectorNode *selected = nodesByID.at(id);
        for (uint32_t operand = 0; operand != selected->operandCount; ++operand)
          worklist.push_back(
              db.crossSelectorOperands[selected->firstOperand + operand]
                  .operand);
      }
      ++seenCrossBins;
    }
  }
  if (seenCrossBins != db.crossBins.size())
    return fail(Status::InvalidReference, "cross_bin_range");
  for (const auto &bin : db.functionalBins)
    if ((bin.kind == FunctionalBinKind::Cross) !=
        (plannedCrossBins.count(bin.id) != 0))
      return fail(Status::InvalidReference, "cross_bin.completeness");
  if (reachableSelectors.size() != db.crossSelectorNodes.size())
    return fail(Status::InvalidReference, "cross_selector_node.orphan");

  if (!uniqueNonzero(db.functionalTupleSets,
                     [](const auto &v) { return v.id; }) ||
      !std::is_sorted(db.functionalTupleSets.begin(),
                      db.functionalTupleSets.end(),
                      [](const auto &a, const auto &b) { return a.id < b.id; }))
    return fail(Status::UnsortedOrDuplicate, "functional_tuple_set_order");
  std::map<uint64_t, const FunctionalTupleSet *> tupleSetsByID;
  std::map<uint64_t, const FunctionalTuple *> tuplesByID;
  size_t tupleCursor = 0;
  size_t tupleComponentCursor = 0;
  for (const auto &set : db.functionalTupleSets) {
    auto node = nodesByID.find(set.selector);
    auto targetList = targetsByCross.find(set.cross);
    if (!tupleSetsByID.emplace(set.id, &set).second ||
        node == nodesByID.end() || node->second->cross != set.cross ||
        node->second->tupleSet != set.id ||
        targetList == targetsByCross.end() || set.firstTuple != tupleCursor ||
        !checkedRange(set.firstTuple, set.tupleCount,
                      db.functionalTupleSetTuples.size()) ||
        !enumBetween(set.elementMode, FunctionalTupleElementMode::BinTuple,
                     FunctionalTupleElementMode::ValueTuple) ||
        set.flags != 0)
      return fail(Status::InvalidReference, "functional_tuple_set");
    for (uint32_t tupleOrdinal = 0; tupleOrdinal != set.tupleCount;
         ++tupleOrdinal) {
      const auto &tuple =
          db.functionalTupleSetTuples[set.firstTuple + tupleOrdinal];
      if (!tuple.id || !tuplesByID.emplace(tuple.id, &tuple).second ||
          tuple.tupleSet != set.id || tuple.ordinal != tupleOrdinal ||
          tuple.firstComponent != tupleComponentCursor ||
          tuple.componentCount != targetList->second.size() ||
          !checkedRange(tuple.firstComponent, tuple.componentCount,
                        db.functionalTupleSetComponents.size()) ||
          tuple.flags != 0)
        return fail(Status::InvalidReference, "functional_tuple");
      for (uint32_t componentOrdinal = 0;
           componentOrdinal != tuple.componentCount; ++componentOrdinal) {
        const auto &component =
            db.functionalTupleSetComponents[tuple.firstComponent +
                                            componentOrdinal];
        bool binMode = set.elementMode == FunctionalTupleElementMode::BinTuple;
        const FunctionalBin *bin =
            component.bin ? findBin(component.bin) : nullptr;
        const FunctionalValueSet *valueSet =
            component.valueSet ? findValueSet(component.valueSet) : nullptr;
        uint64_t expectedTarget = targetList->second[componentOrdinal];
        if (component.tuple != tuple.id ||
            component.ordinal != componentOrdinal ||
            component.target != expectedTarget || component.flags != 0 ||
            (binMode != (component.bin != 0)) ||
            (binMode == (component.valueSet != 0)) ||
            (binMode &&
             (!bin || bin->item != expectedTarget ||
              (bin->flags & (FunctionalBinDefault | FunctionalBinIgnore |
                             FunctionalBinDefaultSequence |
                             FunctionalBinIllegal | FunctionalBinEmpty)))) ||
            (!binMode && (!valueSet || valueSet->item != expectedTarget ||
                          wildcardValueSets.count(component.valueSet) ||
                          !staticSetIsConcreteSingleton(*valueSet))))
          return fail(Status::InvalidReference, "functional_tuple_component");
      }
      tupleComponentCursor += tuple.componentCount;
    }
    tupleCursor += set.tupleCount;
  }
  if (tupleCursor != db.functionalTupleSetTuples.size() ||
      tupleComponentCursor != db.functionalTupleSetComponents.size())
    return fail(Status::InvalidReference, "functional_tuple_ranges");
  std::set<std::tuple<uint64_t, uint32_t, uint32_t>> sourceKeys;
  std::set<uint64_t> expandedBins;
  for (const auto &v : db.functionalSourceRanges) {
    uint32_t role = static_cast<uint32_t>(v.role);
    if (!hasID(db.functionalBins, v.bin) || !knownFunctionalSourceRole(role) ||
        !hasID(db.sourceFiles, v.file) || !hasID(db.sourceFiles, v.endFile) ||
        !text(v.macroName) || !v.line || !v.endLine || v.flags != 0 ||
        ((v.role == FunctionalSourceRole::Expanded ||
          v.role == FunctionalSourceRole::Original) &&
         v.ordinal != 0) ||
        !sourceKeys.emplace(v.bin, role, v.ordinal).second)
      return fail(Status::InvalidReference, "functional_source_range");
    if (v.role == FunctionalSourceRole::Expanded)
      expandedBins.insert(v.bin);
  }
  std::set<uint64_t> sourcedBins;
  for (const auto &v : db.functionalSourceRanges)
    sourcedBins.insert(v.bin);
  for (uint64_t bin : sourcedBins)
    if (!expandedBins.count(bin))
      return fail(Status::InvalidReference, "functional_source_range.expanded");
  if (!std::is_sorted(db.functionalSourceRanges.begin(),
                      db.functionalSourceRanges.end(),
                      [](const auto &a, const auto &b) {
                        return std::tie(a.bin, a.role, a.ordinal) <
                               std::tie(b.bin, b.role, b.ordinal);
                      }))
    return fail(Status::UnsortedOrDuplicate, "functional_source_range_order");
  auto entityFor = [&](MetricKind metric, uint64_t id) {
    switch (metric) {
    case MetricKind::Line:
      return hasID(db.linePoints, id);
    case MetricKind::Toggle:
      return hasID(db.toggleObjects, id);
    case MetricKind::Functional:
      return hasID(db.functionalBins, id);
    default:
      return false;
    }
  };
  std::set<std::pair<uint32_t, uint64_t>> excluded;
  for (const auto &v : db.exclusions) {
    if (!knownMetric(static_cast<uint32_t>(v.metric)) || !text(v.reason) ||
        !entityFor(v.metric, v.entity) ||
        !excluded.emplace(static_cast<uint32_t>(v.metric), v.entity).second)
      return fail(Status::InvalidDatabase, "exclusion");
  }
  if (!std::is_sorted(db.exclusions.begin(), db.exclusions.end(),
                      [](const auto &a, const auto &b) {
                        return std::tie(a.metric, a.entity) <
                               std::tie(b.metric, b.entity);
                      }))
    return fail(Status::UnsortedOrDuplicate, "exclusion_order");
  std::set<UUID> runs;
  std::map<UUID, uint64_t> runFlags;
  for (const auto &v : db.runs) {
    if ((v.flags & ~RunContainsCoverageMask) != 0)
      return fail(Status::InvalidDatabase, "run.flags");
    if (isNil(v.uuid) || !text(v.name) || !runs.insert(v.uuid).second)
      return fail(Status::DuplicateRun, "run.uuid");
    runFlags.emplace(v.uuid, v.flags);
    std::set<std::string> keys;
    for (const auto &t : v.tags)
      if (t.first.empty() || !text(t.first) || !text(t.second) ||
          !keys.insert(t.first).second)
        return fail(Status::UnsortedOrDuplicate, "run.tags");
    if (!std::is_sorted(v.tags.begin(), v.tags.end()))
      return fail(Status::UnsortedOrDuplicate, "run.tag_order");
  }
  if (!std::is_sorted(
          db.runs.begin(), db.runs.end(),
          [](const auto &a, const auto &b) { return a.uuid < b.uuid; }))
    return fail(Status::UnsortedOrDuplicate, "run_order");
  std::map<std::pair<UUID, uint64_t>, const ResolvedInstance *> instances;
  for (const auto &v : db.resolvedInstances)
    if (!runs.count(v.run) || !(runFlags.at(v.run) & RunContainsFunctional) ||
        !hasID(db.functionalTypes, v.type) || !v.id || !text(v.name) ||
        (v.flags & ~ResolvedInstanceGeneratedName) != 0 ||
        ((v.flags & ResolvedInstanceGeneratedName) && v.name.empty()) ||
        !instances.emplace(std::make_pair(v.run, v.id), &v).second)
      return fail(Status::InvalidReference, "resolved_instance");
  if (!std::is_sorted(db.resolvedInstances.begin(), db.resolvedInstances.end(),
                      [](const auto &a, const auto &b) {
                        return std::tie(a.run, a.id) < std::tie(b.run, b.id);
                      }))
    return fail(Status::UnsortedOrDuplicate, "resolved_instance_order");

  using ConfigurationKey = std::pair<uint64_t, Digest>;
  std::set<ConfigurationKey> configurations;
  for (const auto &configuration : db.functionalConfigurations)
    if (!hasID(db.functionalTypes, configuration.type) ||
        configuration.flags != 0 ||
        !configurations.emplace(configuration.type, configuration.configuration)
             .second)
      return fail(Status::UnsortedOrDuplicate, "functional_configuration");
  if (!std::is_sorted(db.functionalConfigurations.begin(),
                      db.functionalConfigurations.end(),
                      [](const auto &a, const auto &b) {
                        return std::tie(a.type, a.configuration) <
                               std::tie(b.type, b.configuration);
                      }))
    return fail(Status::UnsortedOrDuplicate, "functional_configuration_order");
  auto hasConfiguration = [&](uint64_t type, const Digest &configuration) {
    return configurations.count({type, configuration}) != 0;
  };
  for (const auto &[key, instance] : instances)
    if (!hasConfiguration(instance->type, instance->configuration))
      return fail(Status::InvalidReference, "resolved_instance.configuration");
  using InstanceOwner = FunctionalConfigurationOptionOwnerKind;
  using InstanceOption = FunctionalConfigurationOptionKind;
  using InstanceValue = FunctionalConfigurationValueKind;
  for (const auto &option : db.resolvedInstanceOptions) {
    auto instance = instances.find({option.run, option.instance});
    const bool typeOption = option.instance == 0;
    if ((!typeOption && instance == instances.end()) || option.flags != 0)
      return fail(Status::InvalidReference,
                  "resolved_instance_option.instance");
    if (typeOption && (!runs.count(option.run) ||
                       !(runFlags.at(option.run) & RunContainsFunctional)))
      return fail(Status::InvalidReference, "resolved_instance_option.run");
    const bool group = option.ownerKind == InstanceOwner::Group;
    bool itemIsCross = false;
    if (!group && option.ownerKind != InstanceOwner::Item)
      return fail(Status::InvalidDatabase, "resolved_instance_option.owner");
    if (typeOption && group) {
      if (!hasID(db.functionalTypes, option.owner))
        return fail(Status::InvalidReference, "resolved_instance_option.owner");
    } else if (!typeOption && group) {
      if (option.owner != 0)
        return fail(Status::InvalidDatabase, "resolved_instance_option.owner");
    } else {
      auto item = std::find_if(db.functionalItems.begin(),
                               db.functionalItems.end(), [&](const auto &v) {
                                 return v.id == option.owner &&
                                        (typeOption ||
                                         v.type == instance->second->type);
                               });
      if (item == db.functionalItems.end())
        return fail(Status::InvalidReference, "resolved_instance_option.owner");
      itemIsCross = item->kind == FunctionalItemKind::Cross;
    }
    const bool comment = option.option == InstanceOption::Comment;
    const bool mergeInstances = option.option == InstanceOption::MergeInstances;
    const bool crossNumPrintMissing =
        option.option == InstanceOption::CrossNumPrintMissing;
    const bool allowed =
        option.option == InstanceOption::Weight ||
        option.option == InstanceOption::Goal || comment ||
        (!typeOption && option.option == InstanceOption::AtLeast) ||
        (!typeOption && crossNumPrintMissing && (group || itemIsCross)) ||
        (typeOption && group && mergeInstances);
    if (!allowed)
      return fail(Status::InvalidDatabase, "resolved_instance_option.option");
    if ((comment && option.valueKind != InstanceValue::String) ||
        (!comment && option.valueKind != InstanceValue::Unsigned) ||
        !text(option.stringValue) ||
        (!comment && !option.stringValue.empty()) ||
        (comment && option.value != 0) ||
        (option.option == InstanceOption::Goal && option.value > 100) ||
        (option.option == InstanceOption::Weight &&
         option.value > UINT32_MAX) ||
        (crossNumPrintMissing && option.value > INT32_MAX) ||
        (mergeInstances && option.value > 1))
      return fail(Status::InvalidDatabase, "resolved_instance_option.value");
  }
  if (!std::is_sorted(
          db.resolvedInstanceOptions.begin(), db.resolvedInstanceOptions.end(),
          [](const auto &a, const auto &b) {
            return std::tie(a.run, a.instance, a.ownerKind, a.owner, a.option) <
                   std::tie(b.run, b.instance, b.ownerKind, b.owner, b.option);
          }))
    return fail(Status::UnsortedOrDuplicate, "resolved_instance_option_order");
  for (size_t index = 1; index < db.resolvedInstanceOptions.size(); ++index) {
    const auto &a = db.resolvedInstanceOptions[index - 1];
    const auto &b = db.resolvedInstanceOptions[index];
    if (std::tie(a.run, a.instance, a.ownerKind, a.owner, a.option) ==
        std::tie(b.run, b.instance, b.ownerKind, b.owner, b.option))
      return fail(Status::UnsortedOrDuplicate,
                  "resolved_instance_option_duplicate");
  }

  std::map<std::tuple<uint64_t, Digest, uint64_t>,
           const ResolvedFunctionalItem *>
      resolvedItems;
  std::set<std::tuple<uint64_t, Digest, uint64_t>> resolvedItemLogicalKeys;
  for (const auto &item : db.resolvedFunctionalItems) {
    const FunctionalItem *templateItem = findItem(item.templateItem);
    uint32_t kind = static_cast<uint32_t>(item.kind);
    if (!hasConfiguration(item.type, item.configuration) || !item.id ||
        !templateItem || templateItem->type != item.type ||
        templateItem->kind != item.kind ||
        item.ordinal != templateItem->ordinal || !text(item.name) ||
        !text(item.hierarchy) || item.hierarchy.empty() || item.goal > 100 ||
        (item.flags & ~FunctionalItemNonAggregating) != 0 ||
        kind < static_cast<uint32_t>(FunctionalItemKind::Coverpoint) ||
        kind > static_cast<uint32_t>(FunctionalItemKind::Cross) ||
        !resolvedItems
             .emplace(std::make_tuple(item.type, item.configuration, item.id),
                      &item)
             .second ||
        !resolvedItemLogicalKeys
             .emplace(item.type, item.configuration, item.templateItem)
             .second)
      return fail(Status::InvalidReference, "resolved_functional_item");
  }
  if (!std::is_sorted(db.resolvedFunctionalItems.begin(),
                      db.resolvedFunctionalItems.end(),
                      [](const auto &a, const auto &b) {
                        return std::tie(a.type, a.configuration, a.id) <
                               std::tie(b.type, b.configuration, b.id);
                      }))
    return fail(Status::UnsortedOrDuplicate, "resolved_functional_item_order");
  for (const auto &configuration : db.functionalConfigurations)
    for (const auto &item : db.functionalItems)
      if (item.type == configuration.type &&
          !resolvedItemLogicalKeys.count(
              {configuration.type, configuration.configuration, item.id}))
        return fail(Status::InvalidReference,
                    "resolved_functional_item.completeness");

  std::map<std::tuple<uint64_t, Digest, uint64_t>,
           const ResolvedFunctionalBin *>
      resolvedBins;
  std::set<std::tuple<uint64_t, Digest, uint64_t, uint64_t, uint32_t>>
      resolvedBinLogicalKeys;
  for (const auto &bin : db.resolvedFunctionalBins) {
    auto owner = resolvedItems.find({bin.type, bin.configuration, bin.item});
    const FunctionalBin *templateBin =
        bin.templateBin ? findBin(bin.templateBin) : nullptr;
    uint32_t kind = static_cast<uint32_t>(bin.kind);
    if (!hasConfiguration(bin.type, bin.configuration) || !bin.id ||
        owner == resolvedItems.end() || !text(bin.name) ||
        !text(bin.hierarchy) || bin.hierarchy.empty() ||
        (bin.templateBin &&
         (!templateBin || templateBin->item != owner->second->templateItem ||
          templateBin->kind != bin.kind ||
          (bin.flags & ~FunctionalBinEmpty) != templateBin->flags)) ||
        (!bin.templateBin &&
         (bin.kind != FunctionalBinKind::State ||
          (bin.flags & ~FunctionalBinEmpty) != FunctionalBinAutomatic)) ||
        ((bin.kind == FunctionalBinKind::Cross) !=
         (owner->second->kind == FunctionalItemKind::Cross)) ||
        kind < static_cast<uint32_t>(FunctionalBinKind::State) ||
        kind > static_cast<uint32_t>(FunctionalBinKind::Cross) ||
        (bin.flags &
         ~(FunctionalBinDefault | FunctionalBinDefaultSequence |
           FunctionalBinIgnore | FunctionalBinIllegal | FunctionalBinWildcard |
           FunctionalBinAutomatic | FunctionalBinEmpty)) != 0 ||
        ((bin.flags & FunctionalBinIgnore) &&
         (bin.flags & FunctionalBinIllegal)) ||
        ((bin.flags & FunctionalBinDefault) &&
         bin.kind != FunctionalBinKind::State) ||
        ((bin.flags & FunctionalBinDefaultSequence) &&
         bin.kind != FunctionalBinKind::Transition) ||
        ((bin.flags & FunctionalBinDefault) &&
         (bin.flags & FunctionalBinDefaultSequence)) ||
        ((bin.flags & FunctionalBinWildcard) &&
         bin.kind == FunctionalBinKind::Cross) ||
        ((bin.flags & FunctionalBinDefaultSequence) &&
         bin.flags != FunctionalBinDefaultSequence) ||
        ((bin.flags & FunctionalBinDefault) &&
         (bin.flags & FunctionalBinIgnore)) ||
        ((bin.flags & FunctionalBinAutomatic) &&
         (bin.kind != FunctionalBinKind::State ||
          (bin.flags & (FunctionalBinDefault | FunctionalBinDefaultSequence |
                        FunctionalBinIgnore | FunctionalBinIllegal |
                        FunctionalBinWildcard)))) ||
        !resolvedBins
             .emplace(std::make_tuple(bin.type, bin.configuration, bin.id),
                      &bin)
             .second ||
        !resolvedBinLogicalKeys
             .emplace(bin.type, bin.configuration, bin.templateBin,
                      owner == resolvedItems.end()
                          ? uint64_t{0}
                          : owner->second->templateItem,
                      bin.expansionOrdinal)
             .second)
      return fail(Status::InvalidReference, "resolved_functional_bin");
  }
  if (!std::is_sorted(
          db.resolvedFunctionalBins.begin(), db.resolvedFunctionalBins.end(),
          [&](const auto &a, const auto &b) {
            auto key = [&](const auto &bin) {
              auto owner =
                  resolvedItems.find({bin.type, bin.configuration, bin.item});
              const FunctionalBin *templateBin =
                  bin.templateBin ? findBin(bin.templateBin) : nullptr;
              return std::make_tuple(
                  bin.type, bin.configuration,
                  bin.kind == FunctionalBinKind::Cross,
                  owner == resolvedItems.end() ? UINT32_MAX
                                               : owner->second->ordinal,
                  templateBin ? templateBin->ordinal : UINT32_MAX,
                  bin.templateBin, bin.expansionOrdinal);
            };
            return key(a) < key(b);
          }))
    return fail(Status::UnsortedOrDuplicate, "resolved_functional_bin_order");
  std::map<std::tuple<uint64_t, Digest, uint64_t>, std::set<uint32_t>>
      resolvedBinOrdinals;
  for (const auto &bin : db.resolvedFunctionalBins)
    if (!resolvedBinOrdinals[{bin.type, bin.configuration, bin.item}]
             .insert(bin.ordinal)
             .second)
      return fail(Status::UnsortedOrDuplicate,
                  "resolved_functional_bin.ordinal");
  for (const auto &[key, ordinals] : resolvedBinOrdinals)
    if (!ordinals.empty() &&
        (ordinals.size() != uint64_t(*ordinals.rbegin()) + 1))
      return fail(Status::InvalidReference,
                  "resolved_functional_bin.ordinal_order");
  auto optionAllowed = [](FunctionalConfigurationOptionKind option,
                          FunctionalOptionScopeKind scope, bool group,
                          FunctionalItemKind itemKind, uint32_t language) {
    bool typeScope = scope == FunctionalOptionScopeKind::Type;
    switch (option) {
    case FunctionalConfigurationOptionKind::Name:
      return false;
    case FunctionalConfigurationOptionKind::PerInstance:
    case FunctionalConfigurationOptionKind::GetInstCoverage:
      return !typeScope && group;
    case FunctionalConfigurationOptionKind::Strobe:
    case FunctionalConfigurationOptionKind::MergeInstances:
    case FunctionalConfigurationOptionKind::DistributeFirst:
      return typeScope && group;
    case FunctionalConfigurationOptionKind::Weight:
    case FunctionalConfigurationOptionKind::Goal:
    case FunctionalConfigurationOptionKind::Comment:
      return true;
    case FunctionalConfigurationOptionKind::AtLeast:
      return !typeScope;
    case FunctionalConfigurationOptionKind::AutoBinMax:
    case FunctionalConfigurationOptionKind::DetectOverlap:
      return !typeScope &&
             (group || itemKind == FunctionalItemKind::Coverpoint);
    case FunctionalConfigurationOptionKind::CrossNumPrintMissing:
      return !typeScope && (group || itemKind == FunctionalItemKind::Cross);
    case FunctionalConfigurationOptionKind::RealInterval:
      return language >= 2023 && typeScope &&
             (group || itemKind == FunctionalItemKind::Coverpoint);
    case FunctionalConfigurationOptionKind::CrossRetainAutoBins:
      return language >= 2023 && !typeScope &&
             (group || itemKind == FunctionalItemKind::Cross);
    }
    return false;
  };
  auto optionValueKind = [](FunctionalConfigurationOptionKind option) {
    switch (option) {
    case FunctionalConfigurationOptionKind::Name:
    case FunctionalConfigurationOptionKind::Comment:
      return FunctionalConfigurationValueKind::String;
    case FunctionalConfigurationOptionKind::DetectOverlap:
    case FunctionalConfigurationOptionKind::PerInstance:
    case FunctionalConfigurationOptionKind::MergeInstances:
    case FunctionalConfigurationOptionKind::GetInstCoverage:
    case FunctionalConfigurationOptionKind::DistributeFirst:
    case FunctionalConfigurationOptionKind::Strobe:
    case FunctionalConfigurationOptionKind::CrossRetainAutoBins:
      return FunctionalConfigurationValueKind::Boolean;
    case FunctionalConfigurationOptionKind::RealInterval:
      return FunctionalConfigurationValueKind::RealBits;
    default:
      return FunctionalConfigurationValueKind::Unsigned;
    }
  };
  std::set<std::tuple<uint64_t, Digest, uint32_t, uint64_t, uint32_t, uint32_t>>
      configurationOptionKeys;
  for (const auto &option : db.functionalConfigurationOptions) {
    bool group =
        option.ownerKind == FunctionalConfigurationOptionOwnerKind::Group;
    auto item =
        resolvedItems.find({option.type, option.configuration, option.owner});
    const FunctionalType *type = findFunctionalType(option.type);
    FunctionalItemKind itemKind = item == resolvedItems.end()
                                      ? FunctionalItemKind::Coverpoint
                                      : item->second->kind;
    bool ownerValid =
        group ? option.owner == option.type : item != resolvedItems.end();
    bool stringKind =
        option.valueKind == FunctionalConfigurationValueKind::String;
    if (!hasConfiguration(option.type, option.configuration) || !type ||
        !ownerValid ||
        !enumBetween(option.ownerKind,
                     FunctionalConfigurationOptionOwnerKind::Group,
                     FunctionalConfigurationOptionOwnerKind::Item) ||
        !enumBetween(option.scope, FunctionalOptionScopeKind::Instance,
                     FunctionalOptionScopeKind::Type) ||
        !enumBetween(option.option, FunctionalConfigurationOptionKind::Goal,
                     FunctionalConfigurationOptionKind::CrossRetainAutoBins) ||
        !enumBetween(option.valueKind,
                     FunctionalConfigurationValueKind::Unsigned,
                     FunctionalConfigurationValueKind::RealBits) ||
        !optionAllowed(option.option, option.scope, group, itemKind,
                       type->languageVersion) ||
        option.valueKind != optionValueKind(option.option) ||
        !text(option.stringValue) ||
        (!stringKind && !option.stringValue.empty()) ||
        (stringKind && option.value) ||
        (option.valueKind == FunctionalConfigurationValueKind::Boolean &&
         option.value > 1) ||
        (option.valueKind == FunctionalConfigurationValueKind::RealBits &&
         (!std::isfinite(decodeReal(option.value)) ||
          decodeReal(option.value) <= 0.0)) ||
        (option.option == FunctionalConfigurationOptionKind::Goal &&
         option.value > 100) ||
        (option.option == FunctionalConfigurationOptionKind::Weight &&
         option.value > UINT32_MAX) ||
        (option.option ==
             FunctionalConfigurationOptionKind::CrossNumPrintMissing &&
         option.value > INT32_MAX) ||
        option.flags != 0 ||
        !configurationOptionKeys
             .emplace(option.type, option.configuration,
                      static_cast<uint32_t>(option.ownerKind), option.owner,
                      static_cast<uint32_t>(option.scope),
                      static_cast<uint32_t>(option.option))
             .second)
      return fail(Status::InvalidReference, "functional_configuration_option");
  }
  if (!std::is_sorted(db.functionalConfigurationOptions.begin(),
                      db.functionalConfigurationOptions.end(),
                      [](const auto &a, const auto &b) {
                        return std::tie(a.type, a.configuration, a.ownerKind,
                                        a.owner, a.scope, a.option) <
                               std::tie(b.type, b.configuration, b.ownerKind,
                                        b.owner, b.scope, b.option);
                      }))
    return fail(Status::UnsortedOrDuplicate,
                "functional_configuration_option_order");
  for (const auto &configuration : db.functionalConfigurations)
    for (const auto &plan : db.functionalOptionPlans) {
      if (plan.option == FunctionalConfigurationOptionKind::Name)
        continue;
      const FunctionalItem *templateOwner =
          plan.ownerKind == FunctionalConfigurationOptionOwnerKind::Item
              ? findItem(plan.owner)
              : nullptr;
      uint64_t owner = configuration.type;
      if (templateOwner) {
        if (templateOwner->type != configuration.type)
          continue;
        auto resolvedOwner = std::find_if(
            db.resolvedFunctionalItems.begin(),
            db.resolvedFunctionalItems.end(), [&](const auto &item) {
              return item.type == configuration.type &&
                     item.configuration == configuration.configuration &&
                     item.templateItem == plan.owner;
            });
        if (resolvedOwner == db.resolvedFunctionalItems.end())
          return fail(Status::InvalidReference,
                      "functional_configuration_option.owner");
        owner = resolvedOwner->id;
      } else if (plan.owner != configuration.type) {
        continue;
      }
      if (!configurationOptionKeys.count(
              {configuration.type, configuration.configuration,
               static_cast<uint32_t>(plan.ownerKind), owner,
               static_cast<uint32_t>(plan.scope),
               static_cast<uint32_t>(plan.option)}))
        return fail(Status::InvalidReference,
                    "functional_configuration_option.completeness");
    }
  using TypeOptionValue =
      std::tuple<FunctionalConfigurationValueKind, uint64_t, std::string>;
  using TypeOptionKey = std::tuple<FunctionalConfigurationOptionOwnerKind,
                                   uint64_t, FunctionalConfigurationOptionKind>;
  std::map<std::tuple<uint64_t, Digest, TypeOptionKey>, TypeOptionValue>
      typeOptionValues;
  for (const auto &option : db.functionalConfigurationOptions) {
    if (option.scope != FunctionalOptionScopeKind::Type)
      continue;
    uint64_t templateOwner = option.type;
    if (option.ownerKind == FunctionalConfigurationOptionOwnerKind::Item) {
      auto owner =
          resolvedItems.find({option.type, option.configuration, option.owner});
      if (owner == resolvedItems.end())
        return fail(Status::InvalidReference,
                    "functional_configuration_option.type_owner");
      templateOwner = owner->second->templateItem;
    }
    TypeOptionKey optionKey{option.ownerKind, templateOwner, option.option};
    auto key = std::make_tuple(option.type, option.configuration, optionKey);
    TypeOptionValue value{option.valueKind, option.value, option.stringValue};
    typeOptionValues.emplace(key, std::move(value));
  }
  // IEEE 1800-2017 19.7.1 defines type options as static members of the
  // covergroup type. A retained database therefore has one type-option
  // profile even when constructor-dependent instance configurations differ
  // or originate in different runs.
  std::map<uint64_t, std::vector<Digest>> typeConfigs;
  for (const auto &configuration : db.functionalConfigurations)
    typeConfigs[configuration.type].push_back(configuration.configuration);
  for (auto &[instanceType, configList] : typeConfigs) {
    const uint64_t currentType = instanceType;
    std::sort(configList.begin(), configList.end());
    configList.erase(std::unique(configList.begin(), configList.end()),
                     configList.end());
    if (configList.size() < 2)
      continue;
    std::set<TypeOptionKey> optionKeys;
    for (const auto &[key, value] : typeOptionValues)
      if (std::get<0>(key) == instanceType &&
          std::binary_search(configList.begin(), configList.end(),
                             std::get<1>(key)))
        optionKeys.insert(std::get<2>(key));
    for (const TypeOptionKey &optionKey : optionKeys) {
      FunctionalConfigurationOptionKind option = std::get<2>(optionKey);
      TypeOptionValue fallback{optionValueKind(option), 0, {}};
      if (option == FunctionalConfigurationOptionKind::Goal)
        std::get<1>(fallback) = 100;
      else if (option == FunctionalConfigurationOptionKind::Weight)
        std::get<1>(fallback) = 1;
      auto valueFor = [&](const Digest &config) {
        auto found = typeOptionValues.find({currentType, config, optionKey});
        return found == typeOptionValues.end() ? fallback : found->second;
      };
      TypeOptionValue expected = valueFor(configList.front());
      for (size_t index = 1; index != configList.size(); ++index)
        if (valueFor(configList[index]) != expected)
          return fail(Status::InvalidReference,
                      "functional_configuration_option.type_consistency");
    }
  }
  for (const auto &option : db.functionalConfigurationOptions) {
    if (option.ownerKind != FunctionalConfigurationOptionOwnerKind::Item)
      continue;
    auto item =
        resolvedItems.find({option.type, option.configuration, option.owner});
    if (item == resolvedItems.end())
      continue;
    if (option.scope == FunctionalOptionScopeKind::Instance &&
        ((option.option == FunctionalConfigurationOptionKind::Goal &&
          item->second->goal != option.value) ||
         (option.option == FunctionalConfigurationOptionKind::Weight &&
          item->second->weight != option.value)))
      return fail(Status::InvalidReference,
                  "functional_configuration_option.item_value");
    if (option.scope == FunctionalOptionScopeKind::Instance &&
        option.option == FunctionalConfigurationOptionKind::AtLeast)
      for (const auto &bin : db.resolvedFunctionalBins)
        if (bin.type == option.type &&
            bin.configuration == option.configuration &&
            bin.item == option.owner && bin.atLeast != option.value)
          return fail(Status::InvalidReference,
                      "functional_configuration_option.bin_value");
  }
  auto effectiveUnsignedOption = [&](uint64_t type, const Digest &configuration,
                                     uint64_t item,
                                     FunctionalConfigurationOptionKind option,
                                     uint64_t fallback) -> uint64_t {
    auto valueFor =
        [&](FunctionalConfigurationOptionOwnerKind ownerKind,
            uint64_t owner) -> const FunctionalConfigurationOption * {
      auto found = std::find_if(
          db.functionalConfigurationOptions.begin(),
          db.functionalConfigurationOptions.end(), [&](const auto &row) {
            return row.type == type && row.configuration == configuration &&
                   row.ownerKind == ownerKind && row.owner == owner &&
                   row.scope == FunctionalOptionScopeKind::Instance &&
                   row.option == option;
          });
      return found == db.functionalConfigurationOptions.end() ? nullptr
                                                              : &*found;
    };
    if (const auto *itemValue =
            valueFor(FunctionalConfigurationOptionOwnerKind::Item, item))
      return itemValue->value;
    if (option == FunctionalConfigurationOptionKind::AtLeast ||
        option == FunctionalConfigurationOptionKind::AutoBinMax)
      if (const auto *groupValue =
              valueFor(FunctionalConfigurationOptionOwnerKind::Group, type))
        return groupValue->value;
    return fallback;
  };
  for (const auto &item : db.resolvedFunctionalItems) {
    if (item.goal != effectiveUnsignedOption(
                         item.type, item.configuration, item.id,
                         FunctionalConfigurationOptionKind::Goal, 100) ||
        item.weight != effectiveUnsignedOption(
                           item.type, item.configuration, item.id,
                           FunctionalConfigurationOptionKind::Weight, 1))
      return fail(Status::InvalidReference,
                  "functional_configuration_option.item_effective");
  }
  for (const auto &bin : db.resolvedFunctionalBins)
    if (bin.atLeast !=
        effectiveUnsignedOption(bin.type, bin.configuration, bin.item,
                                FunctionalConfigurationOptionKind::AtLeast, 1))
      return fail(Status::InvalidReference,
                  "functional_configuration_option.bin_effective");

  std::map<std::tuple<uint64_t, Digest, uint64_t>,
           const ResolvedFunctionalValueSet *>
      resolvedValueSets;
  std::set<std::tuple<uint64_t, Digest, uint32_t, uint64_t, uint32_t, uint32_t,
                      uint64_t>>
      resolvedValueSetLogicalKeys;
  std::map<std::tuple<uint64_t, Digest, uint64_t>, uint32_t>
      resolvedValueSetConsumers;
  size_t resolvedAtomCursor = 0;
  size_t resolvedLimbCursor = 0;
  std::set<std::tuple<uint64_t, Digest, uint64_t>> resolvedWildcardValueSets;
  for (const auto &set : db.resolvedFunctionalValueSets) {
    auto owner = resolvedItems.find({set.type, set.configuration, set.item});
    const FunctionalType *resolvedType = findFunctionalType(set.type);
    const FunctionalValueSet *templateSet = findValueSet(set.templateValueSet);
    uint32_t kind = static_cast<uint32_t>(set.kind);
    auto ownerBin =
        resolvedBins.find({set.type, set.configuration, set.ownerBin});
    auto ownerSelector = nodesByID.find(set.ownerSelector);
    bool stateRole = set.role == ResolvedFunctionalValueSetRole::StateBin;
    bool transitionRole =
        set.role == ResolvedFunctionalValueSetRole::TransitionStep;
    bool selectorRole =
        set.role == ResolvedFunctionalValueSetRole::SelectorIntersection;
    bool tupleRole = set.role == ResolvedFunctionalValueSetRole::TupleComponent;
    bool ownerValid =
        ((stateRole || transitionRole) && set.ownerBin && !set.ownerSelector &&
         ownerBin != resolvedBins.end() &&
         ((stateRole && ownerBin->second->kind == FunctionalBinKind::State) ||
          (transitionRole &&
           ownerBin->second->kind == FunctionalBinKind::Transition))) ||
        ((selectorRole || tupleRole) && !set.ownerBin && set.ownerSelector &&
         ownerSelector != nodesByID.end());
    bool templateRequired =
        transitionRole || selectorRole ||
        (stateRole && ownerBin != resolvedBins.end() &&
         !(ownerBin->second->flags &
           (FunctionalBinAutomatic | FunctionalBinDefault)));
    if (!hasConfiguration(set.type, set.configuration) || !set.id ||
        owner == resolvedItems.end() ||
        owner->second->kind != FunctionalItemKind::Coverpoint || !ownerValid ||
        (!stateRole && !transitionRole && !selectorRole && !tupleRole) ||
        (templateRequired && !templateSet) ||
        (templateSet && (templateSet->item != owner->second->templateItem ||
                         set.kind != templateSet->kind ||
                         set.bitWidth != templateSet->bitWidth ||
                         set.signedness != templateSet->signedness)) ||
        (stateRole && set.ownerSubordinal != 0) ||
        (stateRole && ownerBin != resolvedBins.end() &&
         set.ownerOrdinal != ownerBin->second->expansionOrdinal) ||
        (selectorRole && (set.ownerOrdinal || set.ownerSubordinal)) ||
        (selectorRole && ownerSelector != nodesByID.end() &&
         (ownerSelector->second->target != owner->second->templateItem ||
          ownerSelector->second->valueSet != set.templateValueSet)) ||
        !knownValueSetKind(kind) || set.flags != 0 ||
        (set.kind == FunctionalValueSetKind::Real &&
         (!resolvedType || resolvedType->languageVersion < 2023)) ||
        (set.kind == FunctionalValueSetKind::Integral &&
         (!set.bitWidth || (set.signedness != CoverageSignedness::Unsigned &&
                            set.signedness != CoverageSignedness::Signed))) ||
        (set.kind == FunctionalValueSetKind::Real &&
         (set.bitWidth != 64 ||
          set.signedness != CoverageSignedness::NotApplicable)) ||
        set.firstAtom != resolvedAtomCursor ||
        !checkedRange(set.firstAtom, set.atomCount,
                      db.resolvedFunctionalValueAtoms.size()) ||
        !resolvedValueSets
             .emplace(std::make_tuple(set.type, set.configuration, set.id),
                      &set)
             .second ||
        !resolvedValueSetLogicalKeys
             .emplace(
                 set.type, set.configuration, static_cast<uint32_t>(set.role),
                 set.ownerBin ? set.ownerBin : set.ownerSelector,
                 set.ownerOrdinal, set.ownerSubordinal, set.templateValueSet)
             .second)
      return fail(Status::InvalidReference, "resolved_functional_value_set");
    for (uint32_t atomIndex = 0; atomIndex != set.atomCount; ++atomIndex) {
      const ResolvedFunctionalValueAtom &atom =
          db.resolvedFunctionalValueAtoms[set.firstAtom + atomIndex];
      bool integralAtom = atom.kind == FunctionalValueAtomKind::IntegralValue ||
                          atom.kind == FunctionalValueAtomKind::IntegralRange;
      const uint32_t unboundedFlags =
          atom.flags & (FunctionalValueAtomLowerUnbounded |
                        FunctionalValueAtomUpperUnbounded);
      const uint64_t expectedLimbs =
          (uint64_t{set.bitWidth} + uint64_t{63}) / uint64_t{64};
      if (atom.valueSet != set.id || atom.ordinal != atomIndex ||
          !knownValueAtomKind(static_cast<uint32_t>(atom.kind)) ||
          (atom.flags & ~(FunctionalValueAtomLowerInclusive |
                          FunctionalValueAtomUpperInclusive |
                          FunctionalValueAtomLowerUnbounded |
                          FunctionalValueAtomUpperUnbounded |
                          FunctionalValueAtomRealRange)) != 0 ||
          unboundedFlags == (FunctionalValueAtomLowerUnbounded |
                             FunctionalValueAtomUpperUnbounded) ||
          ((unboundedFlags & FunctionalValueAtomLowerUnbounded) &&
           !(atom.flags & FunctionalValueAtomLowerInclusive)) ||
          ((unboundedFlags & FunctionalValueAtomUpperUnbounded) &&
           !(atom.flags & FunctionalValueAtomUpperInclusive)) ||
          atom.firstLimb != resolvedLimbCursor ||
          !checkedRange(atom.firstLimb, atom.limbCount,
                        db.resolvedFunctionalValueLimbs.size()) ||
          (integralAtom != (set.kind == FunctionalValueSetKind::Integral)) ||
          (integralAtom && atom.flags != (FunctionalValueAtomLowerInclusive |
                                          FunctionalValueAtomUpperInclusive)) ||
          ((atom.flags & FunctionalValueAtomRealRange) && integralAtom) ||
          (integralAtom && (atom.realLowBits || atom.realHighBits ||
                            uint64_t{atom.limbCount} != expectedLimbs)) ||
          (!integralAtom && atom.limbCount != 0) ||
          (!integralAtom && !(atom.flags & FunctionalValueAtomRealRange) &&
           (unboundedFlags ||
            decodeReal(atom.realLowBits) != decodeReal(atom.realHighBits))) ||
          (!integralAtom &&
           (((unboundedFlags & FunctionalValueAtomLowerUnbounded) &&
             atom.realLowBits) ||
            ((unboundedFlags & FunctionalValueAtomUpperUnbounded) &&
             atom.realHighBits))))
        return fail(Status::InvalidReference, "resolved_functional_value_atom");
      for (uint32_t limbIndex = 0; limbIndex != atom.limbCount; ++limbIndex) {
        const ResolvedFunctionalValueLimb &limb =
            db.resolvedFunctionalValueLimbs[atom.firstLimb + limbIndex];
        if (limb.wildcardMask)
          resolvedWildcardValueSets.emplace(set.type, set.configuration,
                                            set.id);
        bool wildcardOwner = (stateRole || transitionRole) &&
                             ownerBin != resolvedBins.end() &&
                             (ownerBin->second->flags & FunctionalBinWildcard);
        if (limb.valueSet != set.id || limb.atomOrdinal != atomIndex ||
            limb.ordinal != limbIndex ||
            (wildcardOwner && (limb.wildcardMask != limb.lowBval ||
                               limb.wildcardMask != limb.highBval ||
                               (limb.lowAval & limb.wildcardMask) ||
                               (limb.highAval & limb.wildcardMask))) ||
            ((selectorRole || tupleRole) && limb.wildcardMask) ||
            (stateRole && ownerBin != resolvedBins.end() &&
             (ownerBin->second->flags & FunctionalBinAutomatic) &&
             (limb.lowBval || limb.highBval || limb.wildcardMask)))
          return fail(Status::InvalidReference,
                      "resolved_functional_value_limb");
        if (atom.kind == FunctionalValueAtomKind::IntegralValue &&
            (limb.lowAval != limb.highAval || limb.lowBval != limb.highBval))
          return fail(Status::InvalidReference,
                      "resolved_functional_value_limb.scalar_range");
        if (limbIndex + 1 == atom.limbCount && set.bitWidth % 64) {
          uint64_t valid = (uint64_t{1} << (set.bitWidth % 64)) - 1;
          if (((limb.lowAval | limb.lowBval | limb.highAval | limb.highBval |
                limb.wildcardMask) &
               ~valid) != 0)
            return fail(Status::InvalidReference,
                        "resolved_functional_value_limb.unused_bits");
        }
      }
      if (atom.kind == FunctionalValueAtomKind::IntegralRange) {
        bool knownRange = true;
        for (uint32_t limbIndex = 0; limbIndex != atom.limbCount; ++limbIndex) {
          const auto &limb =
              db.resolvedFunctionalValueLimbs[atom.firstLimb + limbIndex];
          knownRange &=
              limb.lowBval == 0 && limb.highBval == 0 && limb.wildcardMask == 0;
        }
        if (!knownRange || !integralRangeOrdered(
                               db.resolvedFunctionalValueLimbs, atom.firstLimb,
                               atom.limbCount, set.bitWidth, set.signedness))
          return fail(Status::InvalidReference,
                      "resolved_functional_value_atom.range");
      }
      if (!integralAtom) {
        double low = decodeReal(atom.realLowBits);
        double high = decodeReal(atom.realHighBits);
        const bool lowerUnbounded =
            atom.flags & FunctionalValueAtomLowerUnbounded;
        const bool upperUnbounded =
            atom.flags & FunctionalValueAtomUpperUnbounded;
        if ((!lowerUnbounded && !std::isfinite(low)) ||
            (!upperUnbounded && !std::isfinite(high)) ||
            (!lowerUnbounded && !upperUnbounded &&
             (low > high ||
              (low == high && (atom.flags & ~FunctionalValueAtomRealRange) !=
                                  (FunctionalValueAtomLowerInclusive |
                                   FunctionalValueAtomUpperInclusive)))))
          return fail(Status::InvalidReference,
                      "resolved_functional_value_atom.real");
      }
      resolvedLimbCursor += atom.limbCount;
    }
    resolvedAtomCursor += set.atomCount;
  }
  auto resolvedValueSetOrderKey = [&](const ResolvedFunctionalValueSet &set) {
    auto item = resolvedItems.find({set.type, set.configuration, set.item});
    auto bin = resolvedBins.find({set.type, set.configuration, set.ownerBin});
    const FunctionalBin *templateBin =
        bin == resolvedBins.end() ? nullptr : findBin(bin->second->templateBin);
    return std::make_tuple(
        set.type, set.configuration, set.templateValueSet,
        static_cast<uint32_t>(set.role),
        item == resolvedItems.end() ? uint64_t{0} : item->second->templateItem,
        templateBin ? templateBin->id : uint64_t{0},
        bin == resolvedBins.end() ? uint32_t{0} : bin->second->expansionOrdinal,
        set.ownerSelector, set.ownerOrdinal, set.ownerSubordinal);
  };
  if (resolvedAtomCursor != db.resolvedFunctionalValueAtoms.size() ||
      resolvedLimbCursor != db.resolvedFunctionalValueLimbs.size() ||
      !std::is_sorted(db.resolvedFunctionalValueSets.begin(),
                      db.resolvedFunctionalValueSets.end(),
                      [&](const auto &a, const auto &b) {
                        return resolvedValueSetOrderKey(a) <
                               resolvedValueSetOrderKey(b);
                      }))
    return fail(Status::InvalidReference, "resolved_functional_value_ranges");

  for (const auto &[key, item] : resolvedItems)
    if (findItem(item->id))
      return fail(Status::InvalidReference,
                  "resolved_functional_item.id_namespace");
  for (const auto &[key, bin] : resolvedBins)
    if (findBin(bin->id))
      return fail(Status::InvalidReference,
                  "resolved_functional_bin.id_namespace");
  for (const auto &[key, set] : resolvedValueSets)
    if (findValueSet(set->id))
      return fail(Status::InvalidReference,
                  "resolved_functional_value_set.id_namespace");

  std::map<std::tuple<uint64_t, Digest, uint64_t>,
           const ResolvedFunctionalBinPlan *>
      resolvedBinPlans;
  for (const auto &plan : db.resolvedFunctionalBinPlans) {
    auto bin = resolvedBins.find({plan.type, plan.configuration, plan.bin});
    const FunctionalBinPlan *templatePlan = nullptr;
    if (bin != resolvedBins.end() && bin->second->templateBin &&
        binPlans.count(bin->second->templateBin))
      templatePlan = binPlans.at(bin->second->templateBin);
    auto valueSet =
        resolvedValueSets.find({plan.type, plan.configuration, plan.valueSet});
    bool state = bin != resolvedBins.end() &&
                 bin->second->kind == FunctionalBinKind::State;
    bool transition = bin != resolvedBins.end() &&
                      bin->second->kind == FunctionalBinKind::Transition;
    bool generatedAutomatic = state && !bin->second->templateBin &&
                              (bin->second->flags & FunctionalBinAutomatic);
    bool scalar = plan.arrayMode == FunctionalBinArrayMode::Scalar;
    bool unsized = plan.arrayMode == FunctionalBinArrayMode::Unsized;
    bool fixed = plan.arrayMode == FunctionalBinArrayMode::Fixed;
    bool stateNeedsSet =
        state && (!(bin->second->flags & FunctionalBinDefault) || !scalar);
    bool wildcardEncodingValid = true;
    if (bin != resolvedBins.end() &&
        (bin->second->flags & FunctionalBinWildcard) &&
        valueSet != resolvedValueSets.end())
      for (uint32_t atomIndex = 0; atomIndex != valueSet->second->atomCount;
           ++atomIndex) {
        const auto &atom =
            db.resolvedFunctionalValueAtoms[valueSet->second->firstAtom +
                                            atomIndex];
        for (uint32_t limbIndex = 0; limbIndex != atom.limbCount; ++limbIndex) {
          const auto &limb =
              db.resolvedFunctionalValueLimbs[atom.firstLimb + limbIndex];
          wildcardEncodingValid &= limb.wildcardMask == limb.lowBval &&
                                   limb.wildcardMask == limb.highBval &&
                                   !(limb.lowAval & limb.wildcardMask) &&
                                   !(limb.highAval & limb.wildcardMask);
        }
      }
    if (!hasConfiguration(plan.type, plan.configuration) ||
        bin == resolvedBins.end() || (!state && !transition) ||
        (!templatePlan && !generatedAutomatic) ||
        (templatePlan &&
         (templatePlan->iffExpression != plan.iffExpression ||
          templatePlan->cardinalityExpression != plan.cardinalityExpression ||
          templatePlan->arrayMode != plan.arrayMode ||
          templatePlan->distribution != plan.distribution)) ||
        (generatedAutomatic &&
         (plan.iffExpression || plan.cardinalityExpression ||
          plan.arrayMode != FunctionalBinArrayMode::Fixed ||
          plan.distribution != FunctionalBinDistributionKind::Uniform)) ||
        (stateNeedsSet != (plan.valueSet != 0)) ||
        (plan.valueSet &&
         (valueSet == resolvedValueSets.end() ||
          valueSet->second->item != bin->second->item ||
          valueSet->second->templateValueSet !=
              (templatePlan ? templatePlan->valueSet : uint64_t{0}) ||
          valueSet->second->role != ResolvedFunctionalValueSetRole::StateBin ||
          valueSet->second->ownerBin != plan.bin ||
          (resolvedWildcardValueSets.count(
               {plan.type, plan.configuration, plan.valueSet}) &&
           !(bin->second->flags & FunctionalBinWildcard)))) ||
        (state && plan.valueSet && !valueSet->second->atomCount &&
         !(bin->second->flags & FunctionalBinEmpty)) ||
        !wildcardEncodingValid || (!scalar && !unsized && !fixed) ||
        (transition && fixed) ||
        (transition && (bin->second->flags & FunctionalBinDefaultSequence) &&
         !scalar) ||
        (scalar && plan.arrayCardinality) ||
        ((unsized || fixed) && !plan.arrayCardinality) || plan.flags != 0 ||
        !resolvedBinPlans
             .emplace(std::make_tuple(plan.type, plan.configuration, plan.bin),
                      &plan)
             .second)
      return fail(Status::InvalidReference, "resolved_functional_bin_plan");
    if (plan.valueSet)
      ++resolvedValueSetConsumers[{plan.type, plan.configuration,
                                   plan.valueSet}];
  }
  if (!std::is_sorted(db.resolvedFunctionalBinPlans.begin(),
                      db.resolvedFunctionalBinPlans.end(),
                      [](const auto &a, const auto &b) {
                        return std::tie(a.type, a.configuration, a.bin) <
                               std::tie(b.type, b.configuration, b.bin);
                      }))
    return fail(Status::UnsortedOrDuplicate,
                "resolved_functional_bin_plan_order");
  for (const auto &[key, bin] : resolvedBins)
    if ((bin->kind != FunctionalBinKind::Cross) !=
        (resolvedBinPlans.count(key) != 0))
      return fail(Status::InvalidReference,
                  "resolved_functional_bin_plan.completeness");
  using ResolvedBinGroupKey = std::tuple<uint64_t, Digest, uint64_t, uint64_t>;
  std::map<ResolvedBinGroupKey, const ResolvedFunctionalBinGroup *> binGroups;
  std::set<std::tuple<uint64_t, Digest, uint64_t>> groupedBins;
  std::map<std::pair<uint64_t, Digest>, uint32_t> groupBinCursors;
  std::map<std::pair<uint64_t, Digest>, uint32_t> nonCrossBinEnds;
  for (uint32_t index = 0; index != db.resolvedFunctionalBins.size(); ++index) {
    const auto &bin = db.resolvedFunctionalBins[index];
    auto key = std::make_pair(bin.type, bin.configuration);
    groupBinCursors.try_emplace(key, index);
    if (bin.kind != FunctionalBinKind::Cross)
      nonCrossBinEnds[key] = index + 1;
  }
  for (const auto &configuration : db.functionalConfigurations) {
    auto position = std::lower_bound(
        db.resolvedFunctionalBins.begin(), db.resolvedFunctionalBins.end(),
        std::tie(configuration.type, configuration.configuration),
        [](const ResolvedFunctionalBin &bin, const auto &key) {
          return std::tie(bin.type, bin.configuration) < key;
        });
    groupBinCursors.try_emplace(
        std::make_pair(configuration.type, configuration.configuration),
        static_cast<uint32_t>(position - db.resolvedFunctionalBins.begin()));
  }
  std::map<std::tuple<uint64_t, Digest, uint64_t>, uint32_t>
      expectedResolvedBinOrdinal;
  std::optional<std::tuple<uint64_t, Digest, uint32_t, uint32_t, uint64_t>>
      previousBinGroupOrder;
  for (const auto &group : db.resolvedFunctionalBinGroups) {
    auto item =
        resolvedItems.find({group.type, group.configuration, group.item});
    const FunctionalBin *templateBin =
        group.templateBin ? findBin(group.templateBin) : nullptr;
    const FunctionalBinPlan *templatePlan =
        group.templateBin && binPlans.count(group.templateBin)
            ? binPlans.at(group.templateBin)
            : nullptr;
    bool generatedAutomatic =
        !group.templateBin && group.kind == FunctionalBinKind::State &&
        item != resolvedItems.end() &&
        item->second->kind == FunctionalItemKind::Coverpoint;
    bool automaticCardinalityValid = true;
    if (generatedAutomatic) {
      const FunctionalExpression *sample = nullptr;
      for (const auto &expression : db.functionalExpressions)
        if (expression.owner == item->second->templateItem &&
            expression.ownerKind == FunctionalExpressionOwnerKind::Item &&
            expression.role == FunctionalExpressionRole::CoverpointSample) {
          sample = &expression;
          break;
        }
      if (sample) {
        uint64_t maximum = effectiveUnsignedOption(
            group.type, group.configuration, group.item,
            FunctionalConfigurationOptionKind::AutoBinMax, 64);
        uint64_t expected =
            sample->bitWidth < 32
                ? std::min(uint64_t{1} << sample->bitWidth, maximum)
                : maximum;
        automaticCardinalityValid =
            sample->resultKind == FunctionalExpressionResultKind::Integral &&
            expected <= UINT32_MAX && group.binCount == expected;
      }
    }
    uint32_t templateOrdinal = templateBin ? templateBin->ordinal : UINT32_MAX;
    uint32_t itemOrdinal =
        item == resolvedItems.end() ? UINT32_MAX : item->second->ordinal;
    auto orderKey =
        std::make_tuple(group.type, group.configuration, itemOrdinal,
                        templateOrdinal, group.templateBin);
    bool scalar = group.arrayMode == FunctionalBinArrayMode::Scalar;
    bool unsized = group.arrayMode == FunctionalBinArrayMode::Unsized;
    bool fixed = group.arrayMode == FunctionalBinArrayMode::Fixed;
    if (!hasConfiguration(group.type, group.configuration) ||
        item == resolvedItems.end() || group.flags != 0 ||
        group.kind == FunctionalBinKind::Cross ||
        (!templateBin && !generatedAutomatic) ||
        (templateBin &&
         (templateBin->item != item->second->templateItem ||
          templateBin->kind != group.kind || !templatePlan ||
          templatePlan->arrayMode != group.arrayMode ||
          templatePlan->distribution != group.distribution ||
          (templatePlan->arrayMode == FunctionalBinArrayMode::Fixed &&
           !templatePlan->cardinalityExpression &&
           templatePlan->arrayCardinality != group.arrayCardinality) ||
          (templatePlan->arrayMode == FunctionalBinArrayMode::Fixed &&
           templatePlan->cardinalityExpression && !group.arrayCardinality))) ||
        (generatedAutomatic &&
         (group.arrayMode != FunctionalBinArrayMode::Fixed ||
          group.distribution != FunctionalBinDistributionKind::Uniform ||
          !automaticCardinalityValid)) ||
        (!scalar && !unsized && !fixed) ||
        (group.kind == FunctionalBinKind::Transition && fixed) ||
        (scalar &&
         (group.binCount != 1 || group.arrayCardinality != 0 ||
          group.distribution != FunctionalBinDistributionKind::None)) ||
        (unsized &&
         (group.binCount != group.arrayCardinality ||
          group.distribution != FunctionalBinDistributionKind::PerValue)) ||
        (fixed &&
         ((!group.arrayCardinality && !generatedAutomatic) ||
          group.binCount != group.arrayCardinality ||
          group.distribution != FunctionalBinDistributionKind::Uniform)) ||
        group.firstBin != groupBinCursors[{group.type, group.configuration}] ||
        !checkedRange(group.firstBin, group.binCount,
                      db.resolvedFunctionalBins.size()) ||
        !binGroups
             .emplace(ResolvedBinGroupKey{group.type, group.configuration,
                                          group.item, group.templateBin},
                      &group)
             .second ||
        (previousBinGroupOrder && !(*previousBinGroupOrder < orderKey)))
      return fail(Status::InvalidReference, "resolved_functional_bin_group");
    previousBinGroupOrder = orderKey;
    for (uint32_t ordinal = 0; ordinal != group.binCount; ++ordinal) {
      const auto &bin = db.resolvedFunctionalBins[group.firstBin + ordinal];
      auto plan =
          resolvedBinPlans.find({group.type, group.configuration, bin.id});
      if (bin.type != group.type || bin.configuration != group.configuration ||
          bin.item != group.item || bin.templateBin != group.templateBin ||
          bin.kind != group.kind || bin.expansionOrdinal != ordinal ||
          bin.ordinal != expectedResolvedBinOrdinal[{
                             bin.type, bin.configuration, bin.item}]++ ||
          plan == resolvedBinPlans.end() ||
          plan->second->arrayMode != group.arrayMode ||
          plan->second->arrayCardinality != group.arrayCardinality ||
          plan->second->distribution != group.distribution ||
          !groupedBins.emplace(bin.type, bin.configuration, bin.id).second)
        return fail(Status::InvalidReference,
                    "resolved_functional_bin_group.member");
    }
    groupBinCursors[{group.type, group.configuration}] += group.binCount;
  }
  for (const auto &[key, end] : nonCrossBinEnds)
    if (groupBinCursors[key] != end)
      return fail(Status::InvalidReference,
                  "resolved_functional_bin_group.range");
  for (const auto &[key, bin] : resolvedBins)
    if (bin->kind != FunctionalBinKind::Cross && !groupedBins.count(key))
      return fail(Status::InvalidReference,
                  "resolved_functional_bin_group.coverage");
  for (const auto &configuration : db.functionalConfigurations) {
    for (const auto &templateBin : db.functionalBins) {
      if (templateBin.kind == FunctionalBinKind::Cross)
        continue;
      const FunctionalItem *staticItem = findItem(templateBin.item);
      if (!staticItem || staticItem->type != configuration.type)
        continue;
      auto item = std::find_if(
          db.resolvedFunctionalItems.begin(), db.resolvedFunctionalItems.end(),
          [&](const auto &resolved) {
            return resolved.type == configuration.type &&
                   resolved.configuration == configuration.configuration &&
                   resolved.templateItem == staticItem->id;
          });
      if (item == db.resolvedFunctionalItems.end() ||
          !binGroups.count({configuration.type, configuration.configuration,
                            item->id, templateBin.id}))
        return fail(Status::InvalidReference,
                    "resolved_functional_bin_group.completeness");
    }
    for (const auto &item : db.resolvedFunctionalItems) {
      if (item.type != configuration.type ||
          item.configuration != configuration.configuration ||
          item.kind != FunctionalItemKind::Coverpoint)
        continue;
      bool hasNormalBin = std::any_of(
          db.functionalBins.begin(), db.functionalBins.end(),
          [&](const auto &bin) {
            return bin.item == item.templateItem &&
                   (bin.kind == FunctionalBinKind::State ||
                    bin.kind == FunctionalBinKind::Transition) &&
                   !(bin.flags & (FunctionalBinIgnore | FunctionalBinIllegal |
                                  FunctionalBinAutomatic));
          });
      bool hasAutomaticGroup = binGroups.count(
          {configuration.type, configuration.configuration, item.id, 0});
      bool hasStaticAutomatic =
          std::any_of(db.functionalBins.begin(), db.functionalBins.end(),
                      [&](const auto &bin) {
                        return bin.item == item.templateItem &&
                               (bin.flags & FunctionalBinAutomatic);
                      });
      if (hasNormalBin ? (hasAutomaticGroup || hasStaticAutomatic)
                       : (hasAutomaticGroup == hasStaticAutomatic))
        return fail(Status::InvalidReference,
                    "resolved_functional_bin_group.automatic");
    }
  }
  // Non-cross inventories are represented authoritatively by bin groups,
  // including explicit zero-cardinality unsized resolutions. Cross bins are
  // scalar and intentionally remain outside those groups.
  for (const auto &configuration : db.functionalConfigurations) {
    for (const auto &templateBin : db.functionalBins) {
      if (templateBin.kind != FunctionalBinKind::Cross)
        continue;
      const FunctionalItem *templateItem = findItem(templateBin.item);
      if (!templateItem || templateItem->type != configuration.type)
        continue;
      std::vector<const ResolvedFunctionalBin *> mirrors;
      for (const auto &bin : db.resolvedFunctionalBins)
        if (bin.type == configuration.type &&
            bin.configuration == configuration.configuration &&
            bin.templateBin == templateBin.id)
          mirrors.push_back(&bin);
      if (mirrors.size() != 1 || mirrors.front()->expansionOrdinal != 0 ||
          mirrors.front()->ordinal != templateBin.ordinal)
        return fail(Status::InvalidReference,
                    "resolved_functional_bin.cross_inventory");
    }
  }

  auto resolvedSetIsConcreteSingleton =
      [&](const ResolvedFunctionalValueSet &set) {
        if (set.atomCount != 1)
          return false;
        const auto &atom = db.resolvedFunctionalValueAtoms[set.firstAtom];
        if (set.kind == FunctionalValueSetKind::Real)
          return !(atom.flags & (FunctionalValueAtomLowerUnbounded |
                                 FunctionalValueAtomUpperUnbounded)) &&
                 decodeReal(atom.realLowBits) == decodeReal(atom.realHighBits);
        for (uint32_t limbIndex = 0; limbIndex != atom.limbCount; ++limbIndex) {
          const auto &limb =
              db.resolvedFunctionalValueLimbs[atom.firstLimb + limbIndex];
          if (limb.lowAval != limb.highAval || limb.lowBval != limb.highBval ||
              limb.wildcardMask)
            return false;
        }
        return true;
      };
  auto resolvedSetIsUnsizedArrayItem =
      [&](const ResolvedFunctionalValueSet &set) {
        if (set.kind != FunctionalValueSetKind::Real)
          return resolvedSetIsConcreteSingleton(set);
        return set.atomCount == 1 &&
               db.resolvedFunctionalValueAtoms[set.firstAtom].kind ==
                   FunctionalValueAtomKind::RealInterval;
      };
  std::map<std::tuple<uint64_t, Digest, uint64_t, uint64_t>,
           std::vector<const ResolvedFunctionalBin *>>
      stateBinGroups;
  for (const auto &[key, bin] : resolvedBins)
    if (bin->kind == FunctionalBinKind::State)
      stateBinGroups[{bin->type, bin->configuration, bin->item,
                      bin->templateBin}]
          .push_back(bin);
  for (auto &[groupKey, group] : stateBinGroups) {
    std::sort(group.begin(), group.end(), [](const auto *a, const auto *b) {
      return a->expansionOrdinal < b->expansionOrdinal;
    });
    const auto *plan = resolvedBinPlans.at(
        {group.front()->type, group.front()->configuration, group.front()->id});
    for (uint32_t index = 0; index != group.size(); ++index) {
      const auto *bin = group[index];
      const auto *other =
          resolvedBinPlans.at({bin->type, bin->configuration, bin->id});
      if (bin->expansionOrdinal != index ||
          other->arrayMode != plan->arrayMode ||
          other->arrayCardinality != plan->arrayCardinality ||
          other->distribution != plan->distribution ||
          other->iffExpression != plan->iffExpression ||
          other->cardinalityExpression != plan->cardinalityExpression)
        return fail(Status::InvalidReference, "resolved_state_bin.plan_group");
    }
    if (plan->arrayMode == FunctionalBinArrayMode::Scalar &&
        (group.size() != 1 || group.front()->expansionOrdinal != 0))
      return fail(Status::InvalidReference, "resolved_state_bin.scalar");
    if ((plan->arrayMode == FunctionalBinArrayMode::Unsized ||
         plan->arrayMode == FunctionalBinArrayMode::Fixed) &&
        group.size() != plan->arrayCardinality)
      return fail(Status::InvalidReference,
                  "resolved_state_bin.array_cardinality");
    if (plan->arrayMode == FunctionalBinArrayMode::Unsized)
      for (const auto *bin : group) {
        const auto *binPlan =
            resolvedBinPlans.at({bin->type, bin->configuration, bin->id});
        auto set = resolvedValueSets.find(
            {bin->type, bin->configuration, binPlan->valueSet});
        if (set == resolvedValueSets.end())
          return fail(Status::InvalidReference, "resolved_state_bin.unsized");
        const bool empty = !set->second->atomCount;
        if ((empty ? !(bin->flags & FunctionalBinEmpty)
                   : !resolvedSetIsUnsizedArrayItem(*set->second)) ||
            (!empty && (bin->flags & FunctionalBinWildcard) &&
             (resolvedWildcardValueSets.count(
                  {bin->type, bin->configuration, binPlan->valueSet}) ||
              [&] {
                const auto &atom =
                    db.resolvedFunctionalValueAtoms[set->second->firstAtom];
                for (uint32_t limbIndex = 0; limbIndex != atom.limbCount;
                     ++limbIndex)
                  if (db.resolvedFunctionalValueLimbs[atom.firstLimb +
                                                      limbIndex]
                          .lowBval)
                    return true;
                return false;
              }())))
          return fail(Status::InvalidReference, "resolved_state_bin.unsized");
      }
    if (plan->arrayMode == FunctionalBinArrayMode::Fixed)
      for (const auto *bin : group) {
        const auto *binPlan =
            resolvedBinPlans.at({bin->type, bin->configuration, bin->id});
        auto set = resolvedValueSets.find(
            {bin->type, bin->configuration, binPlan->valueSet});
        if (set == resolvedValueSets.end() ||
            (!set->second->atomCount && !(bin->flags & FunctionalBinEmpty)))
          return fail(Status::InvalidReference,
                      "resolved_state_bin.fixed_empty");
      }
  }

  std::map<std::tuple<uint64_t, Digest, uint64_t>, uint32_t>
      nextAlternativeOrdinal;
  std::set<std::tuple<uint64_t, Digest, uint64_t>> alternativeIDs;
  std::set<std::tuple<uint64_t, Digest, uint64_t, uint32_t, uint32_t>>
      expansionKeys;
  std::map<std::tuple<uint64_t, Digest, uint64_t>, uint32_t> alternativesByBin;
  std::map<std::tuple<uint64_t, Digest, uint64_t, uint32_t>, std::set<uint32_t>>
      expansionOrdinals;
  std::map<std::tuple<uint64_t, Digest, uint64_t, uint32_t>, uint64_t>
      expectedExpansionCounts;
  std::set<std::tuple<uint64_t, Digest, uint64_t, uint32_t>> expansionGroupKeys;
  std::optional<std::tuple<uint64_t, Digest, uint32_t, uint32_t, uint32_t>>
      previousExpansionGroupOrder;
  size_t expansionAlternativeCursor = 0;
  std::map<ResolvedBinGroupKey, uint64_t> transitionExpansionTotals;
  for (const auto &group : db.resolvedTransitionExpansionGroups) {
    auto item =
        resolvedItems.find({group.type, group.configuration, group.item});
    const FunctionalBin *templateBin = findBin(group.templateBin);
    auto binGroup = binGroups.find(
        {group.type, group.configuration, group.item, group.templateBin});
    auto program =
        std::find_if(db.transitionPrograms.begin(), db.transitionPrograms.end(),
                     [&](const auto &candidate) {
                       return candidate.bin == group.templateBin;
                     });
    const TransitionAlternative *templateAlternative = nullptr;
    if (program != db.transitionPrograms.end() &&
        group.templateAlternativeOrdinal < program->alternativeCount)
      templateAlternative =
          &db.transitionAlternatives[program->firstAlternative +
                                     group.templateAlternativeOrdinal];
    uint32_t itemOrdinal =
        item == resolvedItems.end() ? UINT32_MAX : item->second->ordinal;
    auto orderKey =
        std::make_tuple(group.type, group.configuration, itemOrdinal,
                        templateBin ? templateBin->ordinal : UINT32_MAX,
                        group.templateAlternativeOrdinal);
    if (!hasConfiguration(group.type, group.configuration) ||
        item == resolvedItems.end() || !templateBin ||
        templateBin->kind != FunctionalBinKind::Transition ||
        templateBin->item != item->second->templateItem ||
        binGroup == binGroups.end() ||
        binGroup->second->kind != FunctionalBinKind::Transition ||
        !templateAlternative || group.flags != 0 ||
        group.firstAlternative != expansionAlternativeCursor ||
        group.alternativeCount != group.expansionCount ||
        !checkedRange(group.firstAlternative, group.alternativeCount,
                      db.resolvedTransitionAlternatives.size()) ||
        !expansionGroupKeys
             .emplace(group.type, group.configuration, group.templateBin,
                      group.templateAlternativeOrdinal)
             .second ||
        (previousExpansionGroupOrder &&
         !(*previousExpansionGroupOrder < orderKey)))
      return fail(Status::InvalidReference,
                  "resolved_transition_expansion_group");
    previousExpansionGroupOrder = orderKey;
    auto expansionKey =
        std::make_tuple(group.type, group.configuration, group.templateBin,
                        group.templateAlternativeOrdinal);
    expectedExpansionCounts.emplace(expansionKey, group.expansionCount);
    auto &total = transitionExpansionTotals[{group.type, group.configuration,
                                             group.item, group.templateBin}];
    if (group.expansionCount > UINT64_MAX - total)
      return fail(Status::IntegerOverflow,
                  "resolved_transition_expansion_group.expansion_count");
    total += group.expansionCount;
    for (uint32_t ordinal = 0; ordinal != group.alternativeCount; ++ordinal) {
      const auto &alternative =
          db.resolvedTransitionAlternatives[group.firstAlternative + ordinal];
      auto bin =
          resolvedBins.find({group.type, group.configuration, alternative.bin});
      if (alternative.type != group.type ||
          alternative.configuration != group.configuration ||
          bin == resolvedBins.end() || bin->second->item != group.item ||
          bin->second->templateBin != group.templateBin ||
          alternative.templateAlternativeOrdinal !=
              group.templateAlternativeOrdinal ||
          alternative.expansionOrdinal != ordinal)
        return fail(Status::InvalidReference,
                    "resolved_transition_expansion_group.member");
    }
    expansionAlternativeCursor += group.alternativeCount;
  }
  if (expansionAlternativeCursor != db.resolvedTransitionAlternatives.size())
    return fail(Status::InvalidReference,
                "resolved_transition_expansion_group.range");
  for (const auto &configuration : db.functionalConfigurations)
    for (const auto &program : db.transitionPrograms) {
      const FunctionalItem *staticItem = findItem(program.item);
      if (!staticItem || staticItem->type != configuration.type)
        continue;
      auto item = std::find_if(
          db.resolvedFunctionalItems.begin(), db.resolvedFunctionalItems.end(),
          [&](const auto &resolved) {
            return resolved.type == configuration.type &&
                   resolved.configuration == configuration.configuration &&
                   resolved.templateItem == staticItem->id;
          });
      if (item == db.resolvedFunctionalItems.end())
        return fail(Status::InvalidReference,
                    "resolved_transition_expansion_group.item");
      auto resolvedGroup =
          binGroups.find({configuration.type, configuration.configuration,
                          item->id, program.bin});
      if (resolvedGroup == binGroups.end())
        return fail(Status::InvalidReference,
                    "resolved_transition_expansion_group.bin_group");
      for (uint32_t ordinal = 0; ordinal != program.alternativeCount; ++ordinal)
        if (!expansionGroupKeys.count({configuration.type,
                                       configuration.configuration, program.bin,
                                       ordinal}))
          return fail(Status::InvalidReference,
                      "resolved_transition_expansion_group.completeness");
      if (resolvedGroup->second->arrayMode == FunctionalBinArrayMode::Unsized &&
          transitionExpansionTotals[{
              configuration.type, configuration.configuration, item->id,
              program.bin}] != resolvedGroup->second->arrayCardinality)
        return fail(Status::InvalidReference,
                    "resolved_transition_expansion_group.cardinality");
      uint64_t total = transitionExpansionTotals[{configuration.type,
                                                  configuration.configuration,
                                                  item->id, program.bin}];
      if (resolvedGroup->second->arrayMode == FunctionalBinArrayMode::Scalar) {
        const auto &bin =
            db.resolvedFunctionalBins[resolvedGroup->second->firstBin];
        if (!total && !(bin.flags & FunctionalBinDefaultSequence) &&
            !(bin.flags & FunctionalBinEmpty))
          return fail(Status::InvalidReference,
                      "resolved_transition_expansion_group.empty");
      }
    }
  size_t resolvedStepCursor = 0;
  for (const auto &alternative : db.resolvedTransitionAlternatives) {
    auto resolvedBin = resolvedBins.find(
        {alternative.type, alternative.configuration, alternative.bin});
    const FunctionalBinPlan *templatePlan = nullptr;
    const TransitionProgram *program = nullptr;
    if (resolvedBin != resolvedBins.end()) {
      auto found = std::find_if(
          db.transitionPrograms.begin(), db.transitionPrograms.end(),
          [&](const auto &candidate) {
            return candidate.bin == resolvedBin->second->templateBin;
          });
      if (found != db.transitionPrograms.end())
        program = &*found;
      if (binPlans.count(resolvedBin->second->templateBin))
        templatePlan = binPlans.at(resolvedBin->second->templateBin);
    }
    const TransitionAlternative *templateAlternative = nullptr;
    if (program &&
        alternative.templateAlternativeOrdinal < program->alternativeCount)
      templateAlternative =
          &db.transitionAlternatives[program->firstAlternative +
                                     alternative.templateAlternativeOrdinal];
    auto key = std::make_tuple(alternative.type, alternative.configuration,
                               alternative.bin);
    if (!hasConfiguration(alternative.type, alternative.configuration) ||
        !alternative.id || resolvedBin == resolvedBins.end() ||
        resolvedBin->second->kind != FunctionalBinKind::Transition ||
        !templatePlan || !templateAlternative ||
        alternative.ordinal != nextAlternativeOrdinal[key]++ ||
        alternative.firstStep != resolvedStepCursor ||
        alternative.stepCount != templateAlternative->stepCount ||
        !checkedRange(alternative.firstStep, alternative.stepCount,
                      db.resolvedTransitionSteps.size()) ||
        alternative.flags != 0 ||
        !alternativeIDs
             .emplace(alternative.type, alternative.configuration,
                      alternative.id)
             .second ||
        !expansionKeys
             .emplace(alternative.type, alternative.configuration,
                      resolvedBin->second->templateBin,
                      alternative.templateAlternativeOrdinal,
                      alternative.expansionOrdinal)
             .second)
      return fail(Status::InvalidReference, "resolved_transition_alternative");
    bool hasZeroLengthExpansion = alternative.stepCount == 1;
    for (uint32_t stepOrdinal = 0; stepOrdinal != alternative.stepCount;
         ++stepOrdinal) {
      const auto &step =
          db.resolvedTransitionSteps[alternative.firstStep + stepOrdinal];
      const auto &templateStep =
          db.transitionSteps[templateAlternative->firstStep + stepOrdinal];
      auto resolvedSet = resolvedValueSets.find(
          {step.type, step.configuration, step.valueSet});
      bool variableLength =
          templateStep.repetition == TransitionRepetitionKind::Goto ||
          templateStep.repetition == TransitionRepetitionKind::Nonconsecutive ||
          step.upperBound == TransitionUnbounded;
      bool arrayed = templatePlan->arrayMode != FunctionalBinArrayMode::Scalar;
      hasZeroLengthExpansion &= step.lowerBound == 1;
      if (step.type != alternative.type ||
          step.configuration != alternative.configuration ||
          step.bin != alternative.bin || step.alternative != alternative.id ||
          step.ordinal != stepOrdinal ||
          resolvedSet == resolvedValueSets.end() ||
          resolvedSet->second->templateValueSet != templateStep.valueSet ||
          resolvedSet->second->role !=
              ResolvedFunctionalValueSetRole::TransitionStep ||
          resolvedSet->second->ownerBin != alternative.bin ||
          resolvedSet->second->ownerOrdinal != alternative.ordinal ||
          resolvedSet->second->ownerSubordinal != stepOrdinal ||
          !resolvedSet->second->atomCount ||
          (arrayed && (!resolvedSetIsConcreteSingleton(*resolvedSet->second) ||
                       step.lowerBound != step.upperBound)) ||
          (arrayed &&
           templateStep.repetition != TransitionRepetitionKind::Once &&
           templateStep.repetition != TransitionRepetitionKind::Consecutive) ||
          (arrayed && (resolvedBin->second->flags & FunctionalBinWildcard) &&
           [&] {
             const auto &atom =
                 db.resolvedFunctionalValueAtoms[resolvedSet->second
                                                     ->firstAtom];
             for (uint32_t limbIndex = 0; limbIndex != atom.limbCount;
                  ++limbIndex)
               if (db.resolvedFunctionalValueLimbs[atom.firstLimb + limbIndex]
                       .lowBval)
                 return true;
             return false;
           }()) ||
          step.lowerBound < 1 || step.upperBound < step.lowerBound ||
          (templateStep.repetition == TransitionRepetitionKind::Once &&
           (step.lowerBound != 1 || step.upperBound != 1)) ||
          (templateStep.repetition == TransitionRepetitionKind::Consecutive &&
           step.upperBound == TransitionUnbounded) ||
          (variableLength &&
           ((resolvedBin->second->flags &
             (FunctionalBinIgnore | FunctionalBinIllegal)) ||
            templatePlan->arrayMode != FunctionalBinArrayMode::Scalar)) ||
          (resolvedWildcardValueSets.count(
               {step.type, step.configuration, step.valueSet}) &&
           !(resolvedBin->second->flags & FunctionalBinWildcard)) ||
          step.flags != 0)
        return fail(Status::InvalidReference, "resolved_transition_step");
      ++resolvedValueSetConsumers[{step.type, step.configuration,
                                   step.valueSet}];
    }
    if (hasZeroLengthExpansion)
      return fail(Status::InvalidReference,
                  "resolved_transition_alternative.length");
    ++alternativesByBin[key];
    expansionOrdinals[{alternative.type, alternative.configuration,
                       resolvedBin->second->templateBin,
                       alternative.templateAlternativeOrdinal}]
        .insert(alternative.expansionOrdinal);
    resolvedStepCursor += alternative.stepCount;
  }
  if (resolvedStepCursor != db.resolvedTransitionSteps.size() ||
      !std::is_sorted(
          db.resolvedTransitionAlternatives.begin(),
          db.resolvedTransitionAlternatives.end(),
          [&](const auto &a, const auto &b) {
            auto key = [&](const auto &alternative) {
              const auto *bin =
                  resolvedBins.at({alternative.type, alternative.configuration,
                                   alternative.bin});
              const auto *item =
                  resolvedItems.at({bin->type, bin->configuration, bin->item});
              const FunctionalBin *templateBin = findBin(bin->templateBin);
              return std::make_tuple(
                  alternative.type, alternative.configuration, item->ordinal,
                  templateBin ? templateBin->ordinal : UINT32_MAX,
                  alternative.templateAlternativeOrdinal,
                  alternative.expansionOrdinal);
            };
            return key(a) < key(b);
          }))
    return fail(Status::UnsortedOrDuplicate,
                "resolved_transition_alternative_order");
  for (const auto &[key, bin] : resolvedBins) {
    if (bin->kind != FunctionalBinKind::Transition)
      continue;
    const auto *plan = resolvedBinPlans.at(key);
    uint32_t count = alternativesByBin[key];
    if ((bin->flags & FunctionalBinDefaultSequence) && count != 0)
      return fail(Status::InvalidReference,
                  "resolved_transition_alternative.default_sequence");
    if (plan->arrayMode == FunctionalBinArrayMode::Unsized && count != 1)
      return fail(Status::InvalidReference,
                  "resolved_transition_alternative.unsized");
  }
  for (const auto &[expansionKey, ordinals] : expansionOrdinals)
    if (ordinals.empty() || *ordinals.begin() != 0 ||
        uint64_t(*ordinals.rbegin()) + 1 != ordinals.size() ||
        ordinals.size() != expectedExpansionCounts.at(expansionKey))
      return fail(Status::InvalidReference,
                  "resolved_transition_alternative.expansion_order");
  std::map<std::tuple<uint64_t, Digest, uint64_t>,
           std::vector<const ResolvedFunctionalBin *>>
      transitionBinGroups;
  for (const auto &[key, bin] : resolvedBins)
    if (bin->kind == FunctionalBinKind::Transition)
      transitionBinGroups[{bin->type, bin->configuration, bin->templateBin}]
          .push_back(bin);
  for (auto &[groupKey, group] : transitionBinGroups) {
    std::sort(group.begin(), group.end(), [](const auto *a, const auto *b) {
      return a->expansionOrdinal < b->expansionOrdinal;
    });
    for (uint32_t index = 0; index != group.size(); ++index)
      if (group[index]->expansionOrdinal != index)
        return fail(Status::InvalidReference,
                    "resolved_transition_bin.expansion_order");
    auto firstKey = std::make_tuple(
        group.front()->type, group.front()->configuration, group.front()->id);
    const auto *plan = resolvedBinPlans.at(firstKey);
    for (const auto *bin : group) {
      const auto *other =
          resolvedBinPlans.at({bin->type, bin->configuration, bin->id});
      if (other->arrayMode != plan->arrayMode ||
          other->arrayCardinality != plan->arrayCardinality ||
          other->distribution != plan->distribution ||
          other->iffExpression != plan->iffExpression ||
          other->cardinalityExpression != plan->cardinalityExpression)
        return fail(Status::InvalidReference,
                    "resolved_transition_bin.plan_mismatch");
    }
    std::vector<std::pair<uint32_t, uint32_t>> expectedSequences;
    for (const auto &[expansionKey, ordinals] : expansionOrdinals) {
      if (std::get<0>(expansionKey) != group.front()->type ||
          std::get<1>(expansionKey) != group.front()->configuration ||
          std::get<2>(expansionKey) != group.front()->templateBin)
        continue;
      for (uint32_t expansion : ordinals)
        expectedSequences.emplace_back(std::get<3>(expansionKey), expansion);
    }
    std::sort(expectedSequences.begin(), expectedSequences.end());
    std::vector<std::pair<uint32_t, uint32_t>> assignedSequences;
    for (const auto *bin : group)
      for (const auto &alternative : db.resolvedTransitionAlternatives)
        if (alternative.type == bin->type &&
            alternative.configuration == bin->configuration &&
            alternative.bin == bin->id)
          assignedSequences.emplace_back(alternative.templateAlternativeOrdinal,
                                         alternative.expansionOrdinal);
    if (assignedSequences != expectedSequences)
      return fail(Status::InvalidReference,
                  "resolved_transition_bin.partition_order");
    if (plan->arrayMode == FunctionalBinArrayMode::Scalar && group.size() != 1)
      return fail(Status::InvalidReference,
                  "resolved_transition_bin.scalar_count");
    if (plan->arrayMode == FunctionalBinArrayMode::Unsized &&
        (group.size() != expectedSequences.size() ||
         group.size() != plan->arrayCardinality))
      return fail(Status::InvalidReference,
                  "resolved_transition_bin.unsized_count");
    {
      const uint64_t templateBin = group.front()->templateBin;
      auto program = std::find_if(
          db.transitionPrograms.begin(), db.transitionPrograms.end(),
          [&](const auto &candidate) { return candidate.bin == templateBin; });
      if (program != db.transitionPrograms.end())
        for (uint32_t templateOrdinal = 0;
             templateOrdinal != program->alternativeCount; ++templateOrdinal)
          if (!expansionGroupKeys.count(
                  {group.front()->type, group.front()->configuration,
                   group.front()->templateBin, templateOrdinal}))
            return fail(Status::InvalidReference,
                        "resolved_transition_bin.completeness");
    }
  }

  size_t automaticBinCountLimbCursor = 0;
  for (const auto &plan : db.resolvedCrossPlans) {
    auto cross =
        resolvedItems.find({plan.type, plan.configuration, plan.cross});
    if (!hasConfiguration(plan.type, plan.configuration) ||
        cross == resolvedItems.end() ||
        cross->second->kind != FunctionalItemKind::Cross ||
        (plan.retainAutoPolicy != CrossRetainAutoPolicy::Discard &&
         plan.retainAutoPolicy != CrossRetainAutoPolicy::Retain) ||
        plan.flags ||
        plan.firstAutomaticBinCountLimb != automaticBinCountLimbCursor ||
        !checkedRange(plan.firstAutomaticBinCountLimb,
                      plan.automaticBinCountLimbCount,
                      db.resolvedCrossAutomaticBinCountLimbs.size()) ||
        (plan.automaticBinCountLimbCount &&
         !db.resolvedCrossAutomaticBinCountLimbs
              [plan.firstAutomaticBinCountLimb +
               plan.automaticBinCountLimbCount - 1]) ||
        (plan.retainAutoPolicy == CrossRetainAutoPolicy::Discard &&
         (plan.automaticBinCountLimbCount || plan.rootNode)) ||
        (plan.retainAutoPolicy == CrossRetainAutoPolicy::Retain &&
         ((plan.automaticBinCountLimbCount == 0) != (plan.rootNode == 0))))
      return fail(Status::InvalidReference, "resolved_cross_plan");
    automaticBinCountLimbCursor += plan.automaticBinCountLimbCount;
  }
  if (automaticBinCountLimbCursor !=
      db.resolvedCrossAutomaticBinCountLimbs.size())
    return fail(Status::InvalidReference,
                "resolved_cross_plan.automatic_bin_count_range");
  if (!std::is_sorted(db.resolvedCrossPlans.begin(),
                      db.resolvedCrossPlans.end(),
                      [](const auto &a, const auto &b) {
                        return std::tie(a.type, a.configuration, a.cross) <
                               std::tie(b.type, b.configuration, b.cross);
                      }) ||
      std::adjacent_find(db.resolvedCrossPlans.begin(),
                         db.resolvedCrossPlans.end(),
                         [](const auto &a, const auto &b) {
                           return std::tie(a.type, a.configuration, a.cross) ==
                                  std::tie(b.type, b.configuration, b.cross);
                         }) != db.resolvedCrossPlans.end())
    return fail(Status::UnsortedOrDuplicate, "resolved_cross_plan_order");
  for (const auto &[key, item] : resolvedItems) {
    const auto resolvedKey = key;
    if ((item->kind == FunctionalItemKind::Cross) !=
        (std::find_if(db.resolvedCrossPlans.begin(),
                      db.resolvedCrossPlans.end(), [&](const auto &plan) {
                        return std::tie(plan.type, plan.configuration,
                                        plan.cross) == resolvedKey;
                      }) != db.resolvedCrossPlans.end()))
      return fail(Status::InvalidReference, "resolved_cross_plan.completeness");
  }

  using ResolvedCrossKey = std::tuple<uint64_t, Digest, uint64_t>;
  using ResolvedCrossNodeKey = std::tuple<uint64_t, Digest, uint64_t, uint64_t>;
  std::map<ResolvedCrossKey, const ResolvedCrossPlan *> resolvedCrossPlanMap;
  for (const auto &plan : db.resolvedCrossPlans)
    resolvedCrossPlanMap.emplace(
        std::make_tuple(plan.type, plan.configuration, plan.cross), &plan);
  std::map<ResolvedCrossNodeKey, const ResolvedCrossAutomaticNode *>
      automaticNodesByID;
  for (const auto &node : db.resolvedCrossAutomaticNodes)
    if (!node.id ||
        !automaticNodesByID
             .emplace(ResolvedCrossNodeKey{node.type, node.configuration,
                                           node.cross, node.id},
                      &node)
             .second)
      return fail(Status::UnsortedOrDuplicate,
                  "resolved_cross_automatic_node.id");
  if (!std::is_sorted(db.resolvedCrossAutomaticNodes.begin(),
                      db.resolvedCrossAutomaticNodes.end(),
                      [](const auto &a, const auto &b) {
                        return std::tie(a.type, a.configuration, a.cross,
                                        a.id) <
                               std::tie(b.type, b.configuration, b.cross, b.id);
                      }))
    return fail(Status::UnsortedOrDuplicate,
                "resolved_cross_automatic_node.order");
  size_t automaticEdgeCursor = 0;
  std::map<ResolvedCrossKey, std::vector<const ResolvedCrossAutomaticNode *>>
      automaticNodesByCross;
  for (const auto &node : db.resolvedCrossAutomaticNodes) {
    ResolvedCrossKey key{node.type, node.configuration, node.cross};
    auto plan = resolvedCrossPlanMap.find(key);
    auto cross = resolvedItems.find(key);
    auto staticTargets = cross == resolvedItems.end()
                             ? targetsByCross.end()
                             : targetsByCross.find(cross->second->templateItem);
    if (plan == resolvedCrossPlanMap.end() ||
        plan->second->retainAutoPolicy != CrossRetainAutoPolicy::Retain ||
        staticTargets == targetsByCross.end() ||
        node.targetOrdinal >= staticTargets->second.size() || !node.edgeCount ||
        node.firstEdge != automaticEdgeCursor ||
        !checkedRange(node.firstEdge, node.edgeCount,
                      db.resolvedCrossAutomaticEdges.size()) ||
        node.flags)
      return fail(Status::InvalidReference, "resolved_cross_automatic_node");
    uint64_t targetTemplate = staticTargets->second[node.targetOrdinal];
    auto target =
        std::find_if(db.resolvedFunctionalItems.begin(),
                     db.resolvedFunctionalItems.end(), [&](const auto &item) {
                       return item.type == node.type &&
                              item.configuration == node.configuration &&
                              item.templateItem == targetTemplate;
                     });
    if (target == db.resolvedFunctionalItems.end() ||
        target->kind != FunctionalItemKind::Coverpoint)
      return fail(Status::InvalidReference,
                  "resolved_cross_automatic_node.target");
    std::string previousName;
    bool firstEdge = true;
    for (uint32_t ordinal = 0; ordinal != node.edgeCount; ++ordinal) {
      const auto &edge =
          db.resolvedCrossAutomaticEdges[node.firstEdge + ordinal];
      auto bin = resolvedBins.find({node.type, node.configuration, edge.bin});
      auto child = automaticNodesByID.find(
          {node.type, node.configuration, node.cross, edge.child});
      bool finalTarget = node.targetOrdinal + 1 == staticTargets->second.size();
      if (edge.node != node.id || edge.ordinal != ordinal || edge.flags ||
          bin == resolvedBins.end() || bin->second->item != target->id ||
          (bin->second->flags &
           (FunctionalBinDefault | FunctionalBinDefaultSequence |
            FunctionalBinIgnore | FunctionalBinIllegal | FunctionalBinEmpty)) ||
          (finalTarget != (edge.child == 0)) ||
          (edge.child &&
           (child == automaticNodesByID.end() ||
            child->second->type != node.type ||
            child->second->configuration != node.configuration ||
            child->second->cross != node.cross ||
            child->second->targetOrdinal != node.targetOrdinal + 1)) ||
          (!firstEdge && previousName >= bin->second->name))
        return fail(Status::InvalidReference, "resolved_cross_automatic_edge");
      firstEdge = false;
      previousName = bin->second->name;
    }
    automaticEdgeCursor += node.edgeCount;
    automaticNodesByCross[key].push_back(&node);
  }
  if (automaticEdgeCursor != db.resolvedCrossAutomaticEdges.size())
    return fail(Status::InvalidReference,
                "resolved_cross_automatic_edge.range");

  for (const auto &plan : db.resolvedCrossPlans) {
    ResolvedCrossKey key{plan.type, plan.configuration, plan.cross};
    const auto &nodes = automaticNodesByCross[key];
    if (!plan.rootNode) {
      if (!nodes.empty())
        return fail(Status::InvalidReference,
                    "resolved_cross_automatic_node.empty");
      continue;
    }
    auto root = automaticNodesByID.find(
        {plan.type, plan.configuration, plan.cross, plan.rootNode});
    if (root == automaticNodesByID.end() || root->second->targetOrdinal != 0)
      return fail(Status::InvalidReference, "resolved_cross_plan.root_node");
    std::vector<const ResolvedCrossAutomaticNode *> reverseNodes = nodes;
    std::sort(reverseNodes.begin(), reverseNodes.end(),
              [](const auto *a, const auto *b) {
                return std::tie(a->targetOrdinal, a->id) >
                       std::tie(b->targetOrdinal, b->id);
              });
    std::map<uint64_t, WideUnsigned> pathCounts;
    std::map<uint64_t, uint64_t> canonicalClasses;
    std::map<std::pair<uint32_t, std::vector<std::pair<uint64_t, uint64_t>>>,
             uint64_t>
        canonicalNodes;
    uint64_t nextClass = 1;
    for (const auto *node : reverseNodes) {
      WideUnsigned count;
      std::vector<std::pair<uint64_t, uint64_t>> signature;
      for (uint32_t ordinal = 0; ordinal != node->edgeCount; ++ordinal) {
        const auto &edge =
            db.resolvedCrossAutomaticEdges[node->firstEdge + ordinal];
        if (edge.child) {
          auto childCount = pathCounts.find(edge.child);
          auto childClass = canonicalClasses.find(edge.child);
          if (childCount == pathCounts.end() ||
              childClass == canonicalClasses.end())
            return fail(Status::InvalidReference,
                        "resolved_cross_automatic_edge.child_order");
          addWide(count, childCount->second);
          signature.emplace_back(edge.bin, childClass->second);
        } else {
          addWide(count, WideUnsigned{1});
          signature.emplace_back(edge.bin, 0);
        }
      }
      auto [position, inserted] = canonicalNodes.emplace(
          std::make_pair(node->targetOrdinal, std::move(signature)), nextClass);
      if (!inserted)
        return fail(Status::InvalidReference,
                    "resolved_cross_automatic_node.not_reduced");
      canonicalClasses.emplace(node->id, nextClass++);
      pathCounts.emplace(node->id, std::move(count));
    }
    std::set<uint64_t> reachable;
    std::vector<uint64_t> worklist{plan.rootNode};
    while (!worklist.empty()) {
      uint64_t id = worklist.back();
      worklist.pop_back();
      if (!reachable.insert(id).second)
        continue;
      const auto *node = automaticNodesByID.at(
          {plan.type, plan.configuration, plan.cross, id});
      for (uint32_t ordinal = 0; ordinal != node->edgeCount; ++ordinal) {
        uint64_t child =
            db.resolvedCrossAutomaticEdges[node->firstEdge + ordinal].child;
        if (child)
          worklist.push_back(child);
      }
    }
    if (reachable.size() != nodes.size() ||
        !equalWideRange(pathCounts.at(plan.rootNode),
                        db.resolvedCrossAutomaticBinCountLimbs,
                        plan.firstAutomaticBinCountLimb,
                        plan.automaticBinCountLimbCount))
      return fail(Status::InvalidReference,
                  "resolved_cross_automatic_node.path_count");
  }

  std::map<std::tuple<uint64_t, Digest, uint64_t>,
           const ResolvedFunctionalTupleSet *>
      resolvedTupleSets;
  std::set<std::tuple<uint64_t, Digest, uint64_t>> resolvedTupleIDs;
  std::set<std::tuple<uint64_t, Digest, uint64_t>> resolvedTupleSelectors;
  size_t resolvedTupleCursor = 0;
  size_t resolvedTupleComponentCursor = 0;
  for (const auto &set : db.resolvedFunctionalTupleSets) {
    auto cross = resolvedItems.find({set.type, set.configuration, set.cross});
    auto node = nodesByID.find(set.selector);
    const FunctionalTupleSet *templateSet = nullptr;
    if (set.templateTupleSet) {
      auto found = tupleSetsByID.find(set.templateTupleSet);
      if (found != tupleSetsByID.end())
        templateSet = found->second;
    }
    if (!hasConfiguration(set.type, set.configuration) || !set.id ||
        cross == resolvedItems.end() ||
        cross->second->kind != FunctionalItemKind::Cross ||
        node == nodesByID.end() ||
        node->second->cross != cross->second->templateItem ||
        (set.templateTupleSet &&
         (!templateSet || templateSet->cross != node->second->cross ||
          templateSet->selector != set.selector ||
          templateSet->elementMode != set.elementMode)) ||
        (!set.templateTupleSet && !node->second->constructionExpression &&
         !node->second->withExpression) ||
        (node->second->withExpression &&
         (set.templateTupleSet ||
          set.elementMode != FunctionalTupleElementMode::ValueTuple ||
          set.tupleCount > MaxFunctionalCrossWithCandidates)) ||
        set.firstTuple != resolvedTupleCursor ||
        !checkedRange(set.firstTuple, set.tupleCount,
                      db.resolvedFunctionalTupleSetTuples.size()) ||
        !enumBetween(set.elementMode, FunctionalTupleElementMode::BinTuple,
                     FunctionalTupleElementMode::ValueTuple) ||
        set.flags != 0 ||
        !resolvedTupleSelectors
             .emplace(set.type, set.configuration, set.selector)
             .second ||
        !resolvedTupleSets
             .emplace(std::make_tuple(set.type, set.configuration, set.id),
                      &set)
             .second)
      return fail(Status::InvalidReference, "resolved_functional_tuple_set");
    auto staticTargets = targetsByCross.find(node->second->cross);
    if (staticTargets == targetsByCross.end())
      return fail(Status::InvalidReference,
                  "resolved_functional_tuple_set.cross");
    std::vector<uint64_t> resolvedTargets;
    std::vector<const FunctionalExpression *> resolvedTargetSamples;
    for (uint64_t target : staticTargets->second) {
      auto resolvedTarget =
          std::find_if(db.resolvedFunctionalItems.begin(),
                       db.resolvedFunctionalItems.end(), [&](const auto &item) {
                         return item.type == set.type &&
                                item.configuration == set.configuration &&
                                item.templateItem == target;
                       });
      if (resolvedTarget == db.resolvedFunctionalItems.end())
        return fail(Status::InvalidReference,
                    "resolved_functional_tuple_set.target");
      resolvedTargets.push_back(resolvedTarget->id);
      const FunctionalExpression *sample = nullptr;
      if (node->second->withExpression ||
          node->second->constructionExpression) {
        for (const auto &expression : db.functionalExpressions)
          if (expression.owner == target &&
              expression.ownerKind == FunctionalExpressionOwnerKind::Item &&
              expression.role == FunctionalExpressionRole::CoverpointSample) {
            sample = &expression;
            break;
          }
        if (!sample)
          return fail(Status::InvalidReference,
                      "resolved_functional_tuple_set.target_expression");
      }
      resolvedTargetSamples.push_back(sample);
    }
    for (uint32_t tupleOrdinal = 0; tupleOrdinal != set.tupleCount;
         ++tupleOrdinal) {
      const auto &tuple =
          db.resolvedFunctionalTupleSetTuples[set.firstTuple + tupleOrdinal];
      if (!tuple.id || tuple.tupleSet != set.id ||
          tuple.ordinal != tupleOrdinal ||
          tuple.firstComponent != resolvedTupleComponentCursor ||
          tuple.componentCount != resolvedTargets.size() ||
          !checkedRange(tuple.firstComponent, tuple.componentCount,
                        db.resolvedFunctionalTupleSetComponents.size()) ||
          tuple.flags != 0 ||
          !resolvedTupleIDs.emplace(set.type, set.configuration, tuple.id)
               .second)
        return fail(Status::InvalidReference, "resolved_functional_tuple");
      for (uint32_t componentOrdinal = 0;
           componentOrdinal != tuple.componentCount; ++componentOrdinal) {
        const auto &component =
            db.resolvedFunctionalTupleSetComponents[tuple.firstComponent +
                                                    componentOrdinal];
        bool binMode = set.elementMode == FunctionalTupleElementMode::BinTuple;
        auto bin =
            resolvedBins.find({set.type, set.configuration, component.bin});
        auto valueSet = resolvedValueSets.find(
            {set.type, set.configuration, component.valueSet});
        uint64_t expectedTarget = resolvedTargets[componentOrdinal];
        if (component.tuple != tuple.id || component.target != expectedTarget ||
            component.ordinal != componentOrdinal || component.flags != 0 ||
            (binMode != (component.bin != 0)) ||
            (binMode == (component.valueSet != 0)) ||
            (binMode && (bin == resolvedBins.end() ||
                         bin->second->item != expectedTarget ||
                         (bin->second->flags &
                          (FunctionalBinDefault | FunctionalBinDefaultSequence |
                           FunctionalBinIgnore | FunctionalBinIllegal |
                           FunctionalBinEmpty)))) ||
            (!binMode &&
             (valueSet == resolvedValueSets.end() ||
              valueSet->second->item != expectedTarget ||
              valueSet->second->role !=
                  ResolvedFunctionalValueSetRole::TupleComponent ||
              valueSet->second->ownerSelector != set.selector ||
              valueSet->second->ownerOrdinal != tupleOrdinal ||
              valueSet->second->ownerSubordinal != componentOrdinal ||
              ((node->second->withExpression ||
                node->second->constructionExpression) &&
               ((resolvedTargetSamples[componentOrdinal]->resultKind ==
                         FunctionalExpressionResultKind::Integral
                     ? valueSet->second->kind !=
                           FunctionalValueSetKind::Integral
                     : valueSet->second->kind !=
                           FunctionalValueSetKind::Real) ||
                valueSet->second->bitWidth !=
                    resolvedTargetSamples[componentOrdinal]->bitWidth ||
                valueSet->second->signedness !=
                    resolvedTargetSamples[componentOrdinal]->signedness)) ||
              !resolvedSetIsConcreteSingleton(*valueSet->second))))
          return fail(Status::InvalidReference,
                      "resolved_functional_tuple_component");
        if (component.valueSet)
          ++resolvedValueSetConsumers[{set.type, set.configuration,
                                       component.valueSet}];
      }
      resolvedTupleComponentCursor += tuple.componentCount;
    }
    resolvedTupleCursor += set.tupleCount;
  }
  if (resolvedTupleCursor != db.resolvedFunctionalTupleSetTuples.size() ||
      resolvedTupleComponentCursor !=
          db.resolvedFunctionalTupleSetComponents.size())
    return fail(Status::InvalidReference, "resolved_functional_tuple_ranges");
  if (!std::is_sorted(db.resolvedFunctionalTupleSets.begin(),
                      db.resolvedFunctionalTupleSets.end(),
                      [](const auto &a, const auto &b) {
                        return std::tie(a.type, a.configuration, a.id) <
                               std::tie(b.type, b.configuration, b.id);
                      }))
    return fail(Status::UnsortedOrDuplicate,
                "resolved_functional_tuple_set_order");

  std::set<std::tuple<uint64_t, Digest, uint64_t, uint64_t>> bindingKeys;
  for (const auto &binding : db.resolvedCrossSelectorBindings) {
    auto node = nodesByID.find(binding.node);
    auto set = resolvedValueSets.find(
        {binding.type, binding.configuration, binding.valueSet});
    auto cross = resolvedItems.find(
        {binding.type, binding.configuration, binding.cross});
    auto tupleSet = resolvedTupleSets.find(
        {binding.type, binding.configuration, binding.tupleSet});
    bool hasMatchSubject =
        node != nodesByID.end() &&
        (node->second->withExpression || node->second->constructionExpression ||
         node->second->tupleSet);
    bool validMatches = (binding.matchesPolicy == CrossMatchesPolicy::None &&
                         binding.matchesCount == 0 && !hasMatchSubject) ||
                        (binding.matchesPolicy == CrossMatchesPolicy::Count &&
                         binding.matchesCount != 0 && hasMatchSubject) ||
                        (binding.matchesPolicy == CrossMatchesPolicy::All &&
                         binding.matchesCount == 0 && hasMatchSubject);
    bool needsTuple =
        node != nodesByID.end() &&
        (node->second->withExpression || node->second->constructionExpression ||
         node->second->tupleSet);
    bool needsValueSet = node != nodesByID.end() && node->second->valueSet &&
                         (findValueSet(node->second->valueSet)->flags &
                          FunctionalValueSetNeedsResolution);
    if (!hasConfiguration(binding.type, binding.configuration) ||
        cross == resolvedItems.end() ||
        cross->second->kind != FunctionalItemKind::Cross ||
        node == nodesByID.end() ||
        node->second->cross != cross->second->templateItem ||
        (needsValueSet != (binding.valueSet != 0)) ||
        (binding.valueSet &&
         (set == resolvedValueSets.end() ||
          set->second->templateValueSet != node->second->valueSet ||
          set->second->role !=
              ResolvedFunctionalValueSetRole::SelectorIntersection ||
          set->second->ownerSelector != binding.node)) ||
        (needsTuple != (binding.tupleSet != 0)) ||
        (binding.tupleSet && tupleSet == resolvedTupleSets.end()) ||
        (binding.tupleSet && tupleSet->second->selector != binding.node) ||
        binding.withExpression != node->second->withExpression ||
        !validMatches ||
        (node->second->constructionExpression &&
         binding.matchesPolicy == CrossMatchesPolicy::Count &&
         binding.matchesCount > ParseLimits{}.maxRecords + 1) ||
        (node->second->matchesPolicy == CrossMatchesPolicy::Count &&
         !node->second->matchesExpression &&
         (binding.matchesPolicy != CrossMatchesPolicy::Count ||
          binding.matchesCount != node->second->matchesCount)) ||
        (node->second->matchesPolicy == CrossMatchesPolicy::All &&
         binding.matchesPolicy != CrossMatchesPolicy::All) ||
        binding.flags != 0 ||
        !bindingKeys
             .emplace(binding.type, binding.configuration, binding.cross,
                      binding.node)
             .second)
      return fail(Status::InvalidReference, "resolved_cross_selector_binding");
    if (binding.valueSet)
      ++resolvedValueSetConsumers[{binding.type, binding.configuration,
                                   binding.valueSet}];
  }
  if (!std::is_sorted(
          db.resolvedCrossSelectorBindings.begin(),
          db.resolvedCrossSelectorBindings.end(),
          [](const auto &a, const auto &b) {
            return std::tie(a.type, a.configuration, a.cross, a.node) <
                   std::tie(b.type, b.configuration, b.cross, b.node);
          }) ||
      std::adjacent_find(
          db.resolvedCrossSelectorBindings.begin(),
          db.resolvedCrossSelectorBindings.end(),
          [](const auto &a, const auto &b) {
            return std::tie(a.type, a.configuration, a.cross, a.node) ==
                   std::tie(b.type, b.configuration, b.cross, b.node);
          }) != db.resolvedCrossSelectorBindings.end())
    return fail(Status::UnsortedOrDuplicate,
                "resolved_cross_selector_binding_order");
  for (const auto &[key, cross] : resolvedItems) {
    if (cross->kind != FunctionalItemKind::Cross)
      continue;
    for (const auto &node : db.crossSelectorNodes) {
      if (node.cross != cross->templateItem)
        continue;
      const FunctionalValueSet *set =
          node.valueSet ? findValueSet(node.valueSet) : nullptr;
      bool needsBinding =
          node.withExpression || node.constructionExpression || node.tupleSet ||
          node.matchesExpression ||
          (set && (set->flags & FunctionalValueSetNeedsResolution));
      if (needsBinding != (bindingKeys.count({cross->type, cross->configuration,
                                              cross->id, node.id}) != 0))
        return fail(Status::InvalidReference,
                    "resolved_cross_selector_binding.completeness");
    }
  }

  for (const auto &[key, set] : resolvedValueSets)
    if (resolvedValueSetConsumers[key] != 1)
      return fail(Status::InvalidReference,
                  "resolved_functional_value_set.consumer");

  for (const auto &configuration : db.functionalConfigurations)
    if (computeConfigurationBundleDigest(
            db, configuration.type, configuration.configuration,
            /*includeResolvedIDs=*/false) != configuration.configuration)
      return fail(Status::FingerprintMismatch,
                  "functional_configuration.digest",
                  "type " + std::to_string(configuration.type));

  using SparseKey = std::tuple<UUID, uint64_t, uint64_t, std::vector<uint64_t>>;
  std::optional<SparseKey> previousSparseKey;
  std::map<std::tuple<UUID, uint64_t, uint64_t>, uint64_t>
      sparseAutomaticTupleCounts;
  size_t sparseComponentCursor = 0;
  for (const auto &tuple : db.sparseCrossTuples) {
    auto instance = instances.find({tuple.run, tuple.instance});
    const FunctionalItem *cross = findItem(tuple.cross);
    auto targets = targetsByCross.find(tuple.cross);
    auto resolvedCross =
        instance == instances.end()
            ? db.resolvedFunctionalItems.end()
            : std::find_if(db.resolvedFunctionalItems.begin(),
                           db.resolvedFunctionalItems.end(),
                           [&](const auto &item) {
                             return item.type == instance->second->type &&
                                    item.configuration ==
                                        instance->second->configuration &&
                                    item.templateItem == tuple.cross;
                           });
    auto resolvedPlan =
        resolvedCross == db.resolvedFunctionalItems.end()
            ? db.resolvedCrossPlans.end()
            : std::find_if(db.resolvedCrossPlans.begin(),
                           db.resolvedCrossPlans.end(), [&](const auto &plan) {
                             return plan.type == instance->second->type &&
                                    plan.configuration ==
                                        instance->second->configuration &&
                                    plan.cross == resolvedCross->id;
                           });
    if (instance == instances.end() || !cross ||
        cross->kind != FunctionalItemKind::Cross ||
        cross->type != instance->second->type ||
        resolvedCross == db.resolvedFunctionalItems.end() ||
        resolvedPlan == db.resolvedCrossPlans.end() ||
        resolvedPlan->retainAutoPolicy != CrossRetainAutoPolicy::Retain ||
        !resolvedPlan->automaticBinCountLimbCount ||
        targets == targetsByCross.end() ||
        tuple.firstComponent != sparseComponentCursor ||
        tuple.componentCount != targets->second.size() ||
        !checkedRange(tuple.firstComponent, tuple.componentCount,
                      db.sparseCrossTupleComponents.size()) ||
        (tuple.flags & ~SparseCrossTupleOverflow) != 0)
      return fail(Status::InvalidReference, "sparse_cross_tuple");
    std::vector<uint64_t> components;
    components.reserve(tuple.componentCount);
    for (uint32_t ordinal = 0; ordinal != tuple.componentCount; ++ordinal) {
      uint64_t binID =
          db.sparseCrossTupleComponents[tuple.firstComponent + ordinal];
      auto bin = resolvedBins.find(
          {instance->second->type, instance->second->configuration, binID});
      auto expectedTarget = std::find_if(
          db.resolvedFunctionalItems.begin(), db.resolvedFunctionalItems.end(),
          [&](const auto &item) {
            return item.type == instance->second->type &&
                   item.configuration == instance->second->configuration &&
                   item.templateItem == targets->second[ordinal];
          });
      if (bin == resolvedBins.end() ||
          expectedTarget == db.resolvedFunctionalItems.end() ||
          bin->second->item != expectedTarget->id ||
          (bin->second->flags &
           (FunctionalBinDefault | FunctionalBinDefaultSequence |
            FunctionalBinIgnore | FunctionalBinIllegal | FunctionalBinEmpty)))
        return fail(Status::InvalidReference, "sparse_cross_tuple.component");
      components.push_back(binID);
    }
    uint64_t automaticNode = resolvedPlan->rootNode;
    for (uint32_t ordinal = 0; ordinal != components.size(); ++ordinal) {
      auto node = automaticNodesByID.find({instance->second->type,
                                           instance->second->configuration,
                                           resolvedPlan->cross, automaticNode});
      if (node == automaticNodesByID.end())
        return fail(Status::InvalidReference,
                    "sparse_cross_tuple.automatic_membership");
      const ResolvedCrossAutomaticEdge *edge = nullptr;
      for (uint32_t edgeOrdinal = 0; edgeOrdinal != node->second->edgeCount;
           ++edgeOrdinal) {
        const auto &candidate =
            db.resolvedCrossAutomaticEdges[node->second->firstEdge +
                                           edgeOrdinal];
        if (candidate.bin == components[ordinal]) {
          edge = &candidate;
          break;
        }
      }
      bool finalComponent = ordinal + 1 == components.size();
      if (!edge || (finalComponent != (edge->child == 0)))
        return fail(Status::InvalidReference,
                    "sparse_cross_tuple.automatic_membership");
      automaticNode = edge->child;
    }
    SparseKey key{tuple.run, tuple.instance, tuple.cross,
                  std::move(components)};
    if (previousSparseKey && !(*previousSparseKey < key))
      return fail(Status::UnsortedOrDuplicate, "sparse_cross_tuple.order");
    previousSparseKey = std::move(key);
    ++sparseAutomaticTupleCounts[{tuple.run, tuple.instance, tuple.cross}];
    sparseComponentCursor += tuple.componentCount;
  }
  if (sparseComponentCursor != db.sparseCrossTupleComponents.size())
    return fail(Status::InvalidReference, "sparse_cross_tuple.component_range");
  for (const auto &[key, count] : sparseAutomaticTupleCounts) {
    auto instance = instances.find({std::get<0>(key), std::get<1>(key)});
    auto resolvedCross = std::find_if(
        db.resolvedFunctionalItems.begin(), db.resolvedFunctionalItems.end(),
        [&](const auto &item) {
          return item.type == instance->second->type &&
                 item.configuration == instance->second->configuration &&
                 item.templateItem == std::get<2>(key);
        });
    auto plan = std::find_if(
        db.resolvedCrossPlans.begin(), db.resolvedCrossPlans.end(),
        [&](const auto &candidate) {
          return candidate.type == instance->second->type &&
                 candidate.configuration == instance->second->configuration &&
                 candidate.cross == resolvedCross->id;
        });
    if (plan->automaticBinCountLimbCount == 1 &&
        count > db.resolvedCrossAutomaticBinCountLimbs
                    [plan->firstAutomaticBinCountLimb])
      return fail(Status::InvalidReference,
                  "sparse_cross_tuple.automatic_membership");
  }

  std::set<std::tuple<UUID, uint32_t, uint64_t, uint64_t, uint32_t>>
      counterKeys;
  for (const auto &v : db.counters) {
    auto toggle = std::find_if(
        db.toggleObjects.begin(), db.toggleObjects.end(),
        [&](const ToggleObject &object) { return object.id == v.entity; });
    bool validFunctionalInstance = false;
    if (v.metric == MetricKind::Functional) {
      auto instance = instances.find({v.run, v.instance});
      if (instance != instances.end()) {
        auto bin =
            resolvedBins.find({instance->second->type,
                               instance->second->configuration, v.entity});
        validFunctionalInstance = bin != resolvedBins.end();
      }
    }
    uint64_t requiredRunFlag =
        v.metric == MetricKind::Line         ? RunContainsLine
        : v.metric == MetricKind::Toggle     ? RunContainsToggle
        : v.metric == MetricKind::Functional ? RunContainsFunctional
                                             : uint64_t{0};
    if (!knownMetric(static_cast<uint32_t>(v.metric)) || !runs.count(v.run) ||
        !(runFlags.at(v.run) & requiredRunFlag) ||
        (v.metric != MetricKind::Functional &&
         !entityFor(v.metric, v.entity)) ||
        (v.metric == MetricKind::Line &&
         (v.subindex != 0 || v.instance != 0)) ||
        (v.metric == MetricKind::Toggle &&
         (toggle == db.toggleObjects.end() ||
          toggle->bitWidth > UINT32_MAX / 4 ||
          v.subindex >= toggle->bitWidth * 4 || v.instance != 0)) ||
        (v.metric == MetricKind::Functional &&
         (v.subindex != 0 || !v.instance || !validFunctionalInstance)) ||
        (v.flags & ~uint32_t{1}) != 0)
      return fail(Status::InvalidReference, "counter");
    if (!counterKeys
             .emplace(v.run, static_cast<uint32_t>(v.metric), v.entity,
                      v.instance, v.subindex)
             .second)
      return fail(Status::UnsortedOrDuplicate, "counter");
  }
  if (!std::is_sorted(
          db.counters.begin(), db.counters.end(),
          [](const auto &a, const auto &b) {
            return std::tie(a.run, a.metric, a.entity, a.instance, a.subindex) <
                   std::tie(b.run, b.metric, b.entity, b.instance, b.subindex);
          }))
    return fail(Status::UnsortedOrDuplicate, "counter_order");
  std::set<std::tuple<UUID, uint64_t, uint64_t, uint64_t>> illegalKeys;
  for (const auto &v : db.illegalBinDiagnostics) {
    auto instance = instances.find({v.run, v.instance});
    const ResolvedFunctionalBin *bin = nullptr;
    if (instance != instances.end()) {
      auto found = resolvedBins.find(
          {instance->second->type, instance->second->configuration, v.bin});
      if (found != resolvedBins.end())
        bin = found->second;
    }
    if (!runs.count(v.run) || !v.instance || !bin ||
        !(bin->flags & FunctionalBinIllegal) || !v.count || !text(v.message) ||
        (v.flags & ~IllegalBinDiagnosticOverflow) != 0 ||
        !illegalKeys.emplace(v.run, v.instance, v.bin, v.simulationTime).second)
      return fail(Status::InvalidReference, "illegal_bin");
  }
  if (!std::is_sorted(
          db.illegalBinDiagnostics.begin(), db.illegalBinDiagnostics.end(),
          [](const auto &a, const auto &b) {
            return std::tie(a.run, a.instance, a.bin, a.simulationTime) <
                   std::tie(b.run, b.instance, b.bin, b.simulationTime);
          }))
    return fail(Status::UnsortedOrDuplicate, "illegal_bin_order");
  Digest actual = computeSchemaFingerprint(db);
  Digest zero{};
  if (db.schemaFingerprint != zero && db.schemaFingerprint != actual)
    return fail(Status::FingerprintMismatch, "schema_fingerprint");
  if (diagnostic)
    *diagnostic = {};
  return Status::Ok;
}

Status serialize(const Database &database, WriteCallback callback, void *user,
                 Diagnostic *diagnostic) {
  if (!callback) {
    setDiagnostic(diagnostic, Status::InvalidArgument, 0, SectionKind::Strings,
                  NoRecord, "callback");
    return Status::InvalidArgument;
  }
#if OBELISK_COVERAGE_HAS_EXCEPTIONS
  try {
#endif
    Status checked = validate(database, diagnostic);
    if (checked != Status::Ok)
      return checked;
    StringTable strings;
    Status stringStatus = collectStrings(database, strings);
    if (stringStatus != Status::Ok) {
      setDiagnostic(diagnostic, stringStatus, 0, SectionKind::Strings, NoRecord,
                    "string_count");
      return stringStatus;
    }
    std::vector<EncodedSection> sections;
    SectionKind failedSection = SectionKind::Strings;
    uint64_t failedRecord = NoRecord;
    const char *failedField = nullptr;
    Status encoded = planSections(database, strings, sections);
    if (encoded != Status::Ok) {
      setDiagnostic(diagnostic, encoded, 0, failedSection, failedRecord,
                    "layout");
      return encoded;
    }
    uint64_t dirBytes = 0, start = 0;
    if (mulOverflow(sections.size(), DirectoryRecordSize, dirBytes) ||
        addOverflow(HeaderSize, dirBytes, start) ||
        align8(start) == UINT64_MAX) {
      setDiagnostic(diagnostic, Status::IntegerOverflow, HeaderSize,
                    SectionKind::Strings, NoRecord, "directory_size");
      return Status::IntegerOverflow;
    }
    start = align8(start);
    uint64_t total = start;
    for (const auto &s : sections) {
      if (addOverflow(total, s.size, total) || align8(total) == UINT64_MAX) {
        setDiagnostic(diagnostic, Status::IntegerOverflow, total, s.kind,
                      NoRecord, "section_size");
        return Status::IntegerOverflow;
      }
      total = align8(total);
    }
    HeaderBytes prefix;
    if (!prefix.reserve(start)) {
      setDiagnostic(diagnostic, Status::OutOfMemory, 0, SectionKind::Strings,
                    NoRecord, "header");
      return Status::OutOfMemory;
    }
    prefix.raw(Magic, 8);
    prefix.u32(CodecVersion);
    prefix.u32(database.flags);
    prefix.u64(total);
    prefix.u64(HeaderSize);
    prefix.u32(static_cast<uint32_t>(sections.size()));
    prefix.u32(HeaderSize);
    Digest fingerprint = computeSchemaFingerprint(database);
    prefix.raw(fingerprint);
    prefix.zero(32);
    uint64_t offset = start;
    for (const auto &s : sections) {
      prefix.u32(static_cast<uint32_t>(s.kind));
      prefix.u32(s.flags);
      prefix.u64(offset);
      prefix.u64(s.size);
      prefix.u64(s.count);
      uint64_t sectionEnd = 0;
      if (addOverflow(offset, s.size, sectionEnd) ||
          align8(sectionEnd) == UINT64_MAX) {
        setDiagnostic(diagnostic, Status::IntegerOverflow, 0, s.kind, NoRecord,
                      "section_size");
        return Status::IntegerOverflow;
      }
      offset = align8(sectionEnd);
    }
    prefix.pad8();

    SHA256 hash;
    hash.update(prefix.value.data(), prefix.value.size());
    auto hashSink = [](const uint8_t *data, size_t size, void *user) {
      static_cast<SHA256 *>(user)->update(data, size);
      return true;
    };
    encoded = encodeSections(database, strings, hashSink, &hash, sections,
                             &failedSection, &failedRecord, &failedField);
    if (encoded != Status::Ok) {
      setDiagnostic(diagnostic, encoded, 0, failedSection, failedRecord,
                    failedField ? failedField : "hash_pass");
      return encoded;
    }
    Digest checksum = hash.finish();
    std::copy(checksum.begin(), checksum.end(), prefix.value.begin() + 72);
    if (!callback(prefix.value.data(), prefix.value.size(), user)) {
      setDiagnostic(diagnostic, Status::IoError, 0, SectionKind::Strings,
                    NoRecord, "sink");
      return Status::IoError;
    }
    encoded = encodeSections(database, strings, callback, user, sections,
                             &failedSection, &failedRecord, &failedField);
    if (encoded != Status::Ok) {
      setDiagnostic(diagnostic, encoded, 0, failedSection, failedRecord,
                    failedField ? failedField : "sink");
      return encoded;
    }
    if (diagnostic)
      *diagnostic = {};
    return Status::Ok;
#if OBELISK_COVERAGE_HAS_EXCEPTIONS
  } catch (const std::bad_alloc &) {
    setDiagnostic(diagnostic, Status::OutOfMemory, 0, SectionKind::Strings,
                  NoRecord, "allocation");
    return Status::OutOfMemory;
  }
#endif
}

Status serialize(const Database &database, std::vector<uint8_t> &result,
                 Diagnostic *diagnostic) {
  std::vector<uint8_t> temporary;
  auto append = [](const uint8_t *data, size_t size, void *user) {
    auto &output = *static_cast<std::vector<uint8_t> *>(user);
    output.insert(output.end(), data, data + size);
    return true;
  };
  Status status = serialize(database, append, &temporary, diagnostic);
  if (status == Status::Ok)
    result.swap(temporary);
  return status;
}

Status parse(const uint8_t *data, size_t size, Database &output,
             const ParseLimits &limits, Diagnostic *diagnostic) {
  Database result;
  auto fail = [&](Status s, uint64_t off, SectionKind sec, uint64_t rec,
                  const char *field, std::string detail = {}) {
    setDiagnostic(diagnostic, s, off, sec, rec, field, std::move(detail));
    return s;
  };
#if OBELISK_COVERAGE_HAS_EXCEPTIONS
  try {
#endif
    if (!data && size)
      return fail(Status::InvalidArgument, 0, SectionKind::Strings, NoRecord,
                  "data");
    if (size < HeaderSize)
      return fail(Status::Truncated, size, SectionKind::Strings, NoRecord,
                  "header");
    if (std::memcmp(data, Magic, 8) != 0)
      return fail(Status::BadMagic, 0, SectionKind::Strings, NoRecord, "magic");
    if (read32(data + 8) != CodecVersion)
      return fail(Status::VersionMismatch, 8, SectionKind::Strings, NoRecord,
                  "version");
    if ((read32(data + 12) & ~KnownHeaderFlags) != 0)
      return fail(Status::InvalidEnum, 12, SectionKind::Strings, NoRecord,
                  "flags");
    if (read64(data + 16) != size || size > limits.maxImageBytes)
      return fail(size > limits.maxImageBytes ? Status::LimitExceeded
                                              : Status::BadCount,
                  16, SectionKind::Strings, NoRecord, "image_size");
    uint64_t dirOffset = read64(data + 24);
    uint32_t count = read32(data + 32);
    if (read32(data + 36) != HeaderSize || dirOffset != HeaderSize)
      return fail(Status::BadRecordSize, 24, SectionKind::Strings, NoRecord,
                  "header_size");
    uint64_t dirSize = 0, dirEnd = 0;
    if (count > limits.maxRecords)
      return fail(Status::LimitExceeded, 32, SectionKind::Strings, NoRecord,
                  "section_count");
    if (mulOverflow(count, DirectoryRecordSize, dirSize) ||
        addOverflow(dirOffset, dirSize, dirEnd) || dirEnd > size)
      return fail(Status::IntegerOverflow, 32, SectionKind::Strings, NoRecord,
                  "section_count");
    Digest expected{};
    std::copy(data + 72, data + 104, expected.begin());
    if (checksumWithZeroDigest(data, size) != expected)
      return fail(Status::ChecksumMismatch, 72, SectionKind::Strings, NoRecord,
                  "sha256");
    Digest fingerprint{};
    std::copy(data + 40, data + 72, fingerprint.begin());
    result.flags = read32(data + 12);
    result.schemaFingerprint = fingerprint;
    std::vector<DirectoryEntry> entries;
    std::set<uint32_t> kinds;
    uint64_t previousEnd = dirEnd;
    uint64_t previous = align8(previousEnd);
    uint32_t previousKind = 0;
    for (uint32_t i = 0; i != count; ++i) {
      const uint8_t *p = data + dirOffset + i * DirectoryRecordSize;
      uint32_t rawKind = read32(p);
      uint32_t flags = read32(p + 4);
      uint64_t off = read64(p + 8), bytes = read64(p + 16);
      uint64_t records = read64(p + 24), end = 0;
      SectionKind kind = static_cast<SectionKind>(rawKind);
      if (rawKind <= previousKind)
        return fail(Status::UnsortedOrDuplicate, dirOffset + i * 32, kind, i,
                    "kind");
      previousKind = rawKind;
      if ((flags & ~RequiredSection) != 0)
        return fail(Status::InvalidEnum, dirOffset + i * 32 + 4, kind, i,
                    "flags");
      if (off % 8 || addOverflow(off, bytes, end) || end > size ||
          off != previous)
        return fail(off % 8 ? Status::Misaligned : Status::SectionOverlap,
                    dirOffset + i * 32, kind, i, "offset");
      for (uint64_t padding = previousEnd; padding != off; ++padding)
        if (data[padding] != 0)
          return fail(Status::InvalidDatabase, padding, kind, i, "padding");
      if (bytes > limits.maxSectionBytes || records > limits.maxRecords)
        return fail(Status::LimitExceeded, off, kind, i, "section_limits");
      previousEnd = end;
      previous = align8(end);
      const auto *desc = descriptor(kind);
      if (!desc)
        return fail(Status::InvalidEnum, dirOffset + i * 32, kind, i, "kind");
      uint32_t requiredFlags = desc->required ? RequiredSection : 0;
      if (flags != requiredFlags)
        return fail(Status::InvalidEnum, dirOffset + i * 32 + 4, kind, i,
                    "flags");
      if (!kinds.insert(rawKind).second)
        return fail(Status::DuplicateSection, dirOffset + i * 32, kind, i,
                    "kind");
      if (desc->recordSize) {
        uint64_t expectedSize = 0;
        if (mulOverflow(records, desc->recordSize, expectedSize) ||
            expectedSize != bytes)
          return fail(Status::BadRecordSize, off, kind, i, "record_size");
      } else if (kind != SectionKind::Strings &&
                 ((bytes == 0) != (records == 0) || records > 1)) {
        return fail(Status::BadCount, off, kind, i, "record_count");
      }
      entries.push_back({kind, flags, off, bytes, records});
    }
    if (previous != size)
      return fail(Status::BadCount, previous, SectionKind::Strings, NoRecord,
                  "trailing_bytes");
    for (uint64_t padding = previousEnd; padding != size; ++padding)
      if (data[padding] != 0)
        return fail(Status::InvalidDatabase, padding, SectionKind::Strings,
                    NoRecord, "padding");
    for (const auto &desc : ParserSectionDescriptors)
      if (desc.required && !kinds.count(static_cast<uint32_t>(desc.kind)))
        return fail(Status::MissingSection, 0, desc.kind, NoRecord, "section");
    auto entry = [&](SectionKind kind) -> const DirectoryEntry * {
      for (const auto &e : entries)
        if (e.kind == kind)
          return &e;
      return nullptr;
    };
    std::vector<std::string> strings;
    const auto *se = entry(SectionKind::Strings);
    uint64_t cursor = se->offset, end = se->offset + se->size;
    if (se->count > limits.maxStrings)
      return fail(Status::LimitExceeded, cursor, se->kind, NoRecord,
                  "string_count");
    for (uint64_t i = 0; i != se->count; ++i) {
      if (end - cursor < 4)
        return fail(Status::Truncated, cursor, se->kind, i, "length");
      uint32_t n = read32(data + cursor);
      cursor += 4;
      if (n > limits.maxStringBytes || n > end - cursor)
        return fail(Status::LimitExceeded, cursor, se->kind, i, "string");
      std::string s(reinterpret_cast<const char *>(data + cursor), n);
      if (!validUtf8(s))
        return fail(Status::InvalidUtf8, cursor, se->kind, i, "string");
      if (!strings.empty() && strings.back() >= s)
        return fail(Status::UnsortedOrDuplicate, cursor, se->kind, i, "string");
      strings.push_back(std::move(s));
      uint64_t stringEnd = 0;
      if (addOverflow(cursor, n, stringEnd) || stringEnd > UINT64_MAX - 3)
        return fail(Status::IntegerOverflow, cursor, se->kind, i, "string");
      cursor = (stringEnd + 3) & ~uint64_t(3);
      if (cursor > end)
        return fail(Status::Truncated, cursor, se->kind, i, "padding");
      for (uint64_t padding = stringEnd; padding != cursor; ++padding)
        if (data[padding] != 0)
          return fail(Status::InvalidDatabase, padding, se->kind, i, "padding");
    }
    if (cursor != end)
      return fail(Status::BadCount, cursor, se->kind, NoRecord,
                  "trailing_bytes");
    auto str = [&](uint32_t index, const DirectoryEntry &e, uint64_t rec,
                   const char *field) -> const std::string * {
      if (index >= strings.size()) {
        setDiagnostic(diagnostic, Status::InvalidReference,
                      e.offset + rec * (descriptor(e.kind)->recordSize), e.kind,
                      rec, field);
        return nullptr;
      }
      return &strings[index];
    };
    std::map<SectionKind, std::unordered_set<uint64_t>> stableIdentities;
    std::map<SectionKind, std::set<UUID>> uuidIdentities;
    for (const RecordDescriptor &record : ParserRecordDescriptors) {
      const DirectoryEntry *section = entry(record.section);
      if (!section)
        continue;
      const FieldDescriptor *identity = nullptr;
      for (uint32_t fieldIndex = 0; fieldIndex != record.fieldCount;
           ++fieldIndex)
        if (record.fields[fieldIndex].semantic ==
                CoverageFieldSemantic::StableID ||
            record.fields[fieldIndex].semantic ==
                CoverageFieldSemantic::UUIDID) {
          identity = &record.fields[fieldIndex];
          break;
        }
      if (!identity)
        continue;
      for (uint64_t recordIndex = 0; recordIndex != section->count;
           ++recordIndex) {
        const uint8_t *recordData =
            data + section->offset + recordIndex * record.size;
        if (identity->semantic == CoverageFieldSemantic::StableID) {
          uint64_t id = 0;
          for (uint32_t byte = 0; byte != identity->width; ++byte)
            id |= uint64_t(recordData[identity->offset + byte]) << (byte * 8);
          stableIdentities[record.section].insert(id);
        } else {
          UUID uuid{};
          std::copy(recordData + identity->offset,
                    recordData + identity->offset + uuid.size(), uuid.begin());
          uuidIdentities[record.section].insert(uuid);
        }
      }
    }
    auto fixed = [&](SectionKind kind, auto decode) -> Status {
      const auto *e = entry(kind);
      if (!e)
        return Status::Ok;
      uint32_t rs = descriptor(kind)->recordSize;
      for (uint64_t i = 0; i != e->count; ++i) {
        const uint8_t *p = data + e->offset + i * rs;
        if (const RecordDescriptor *record =
                recordDescriptor(ParserRecordDescriptors, kind)) {
          const FieldDescriptor *invalidField = nullptr;
          bool physicalValid = ParserValidatePhysicalRecord(
              *record, p,
              [&](const FieldDescriptor &field, uint64_t value,
                  const uint8_t *fieldData) {
                const DomainDescriptor *domain = ParserFindDomain(field.domain);
                if (field.semantic == CoverageFieldSemantic::StringIndex)
                  return value < strings.size();
                if (field.semantic == CoverageFieldSemantic::RangeStart &&
                    domain) {
                  const DirectoryEntry *target =
                      entry(static_cast<SectionKind>(domain->targetSection));
                  uint64_t targetCount = target ? target->count : 0;
                  const FieldDescriptor *countField = nullptr;
                  for (uint32_t fieldIndex = 0;
                       fieldIndex != record->fieldCount; ++fieldIndex)
                    if (std::strcmp(record->fields[fieldIndex].name,
                                    field.pairedField) == 0) {
                      countField = &record->fields[fieldIndex];
                      break;
                    }
                  if (!countField)
                    return false;
                  const uint8_t *recordData = fieldData - field.offset;
                  uint64_t rangeCount = 0;
                  for (uint32_t byte = 0; byte != countField->width; ++byte)
                    rangeCount |=
                        uint64_t(recordData[countField->offset + byte])
                        << (byte * 8);
                  uint64_t rangeEnd = 0;
                  return !addOverflow(value, rangeCount, rangeEnd) &&
                         rangeEnd <= targetCount;
                }
                if (field.semantic == CoverageFieldSemantic::RangeCount)
                  return true;
                if (field.semantic == CoverageFieldSemantic::StableIDRef &&
                    domain &&
                    domain->kind == CoverageFieldDomainKind::StableRef) {
                  if (field.nullable && value == 0)
                    return true;
                  SectionKind targetKind =
                      static_cast<SectionKind>(domain->targetSection);
                  auto identities = stableIdentities.find(targetKind);
                  return identities != stableIdentities.end() &&
                         identities->second.count(value);
                }
                if (field.semantic == CoverageFieldSemantic::UUIDRef &&
                    domain) {
                  bool zero = true;
                  for (uint32_t byte = 0; byte != field.width; ++byte)
                    zero = zero && fieldData[byte] == 0;
                  if (field.nullable && zero)
                    return true;
                  SectionKind targetKind =
                      static_cast<SectionKind>(domain->targetSection);
                  UUID uuid{};
                  if (field.width != uuid.size())
                    return false;
                  std::copy(fieldData, fieldData + uuid.size(), uuid.begin());
                  auto identities = uuidIdentities.find(targetKind);
                  return identities != uuidIdentities.end() &&
                         identities->second.count(uuid);
                }
                return true;
              },
              invalidField);
          if (!physicalValid) {
            Status status = Status::InvalidReference;
            if (invalidField &&
                (invalidField->semantic == CoverageFieldSemantic::Enum ||
                 invalidField->semantic == CoverageFieldSemantic::Flags))
              status = Status::InvalidEnum;
            else if (invalidField && invalidField->semantic ==
                                         CoverageFieldSemantic::ReservedZero)
              status = Status::InvalidDatabase;
            return fail(status,
                        e->offset + i * rs +
                            (invalidField ? invalidField->offset : 0),
                        kind, i, invalidField ? invalidField->name : "record");
          }
        }
        Status s = decode(*e, i, p);
        if (s != Status::Ok)
          return s;
      }
      return Status::Ok;
    };
    Status st;
    st = fixed(SectionKind::SourceFiles,
               [&](const auto &e, uint64_t i, const uint8_t *p) {
                 const auto *s = str(SourceFileRecord::getPath(p), e, i,
                                     SourceFileRecord::PathName);
                 if (!s)
                   return Status::InvalidReference;
                 SourceFile v;
                 v.id = SourceFileRecord::getId(p);
                 v.path = *s;
                 std::copy(SourceFileRecord::getDigest(p),
                           SourceFileRecord::getDigest(p) + v.digest.size(),
                           v.digest.begin());
                 result.sourceFiles.push_back(std::move(v));
                 return Status::Ok;
               });
    if (st != Status::Ok)
      return st;
    st = fixed(
        SectionKind::ResolvedFunctionalTupleSets,
        [&](const auto &, uint64_t, const uint8_t *p) {
          ResolvedFunctionalTupleSet v;
          v.type = ResolvedFunctionalTupleSetRecord::getType(p);
          std::copy(ResolvedFunctionalTupleSetRecord::getConfiguration(p),
                    ResolvedFunctionalTupleSetRecord::getConfiguration(p) +
                        v.configuration.size(),
                    v.configuration.begin());
          v.id = ResolvedFunctionalTupleSetRecord::getId(p);
          v.templateTupleSet =
              ResolvedFunctionalTupleSetRecord::getTemplateTupleSet(p);
          v.cross = ResolvedFunctionalTupleSetRecord::getCross(p);
          v.selector = ResolvedFunctionalTupleSetRecord::getSelector(p);
          v.firstTuple = ResolvedFunctionalTupleSetRecord::getFirstTuple(p);
          v.tupleCount = ResolvedFunctionalTupleSetRecord::getTupleCount(p);
          v.elementMode = static_cast<FunctionalTupleElementMode>(
              ResolvedFunctionalTupleSetRecord::getElementMode(p));
          v.flags = ResolvedFunctionalTupleSetRecord::getFlags(p);
          result.resolvedFunctionalTupleSets.push_back(v);
          return Status::Ok;
        });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::ResolvedFunctionalTupleSetTuples,
               [&](const auto &, uint64_t, const uint8_t *p) {
                 result.resolvedFunctionalTupleSetTuples.push_back(
                     {ResolvedFunctionalTupleRecord::getTupleSet(p),
                      ResolvedFunctionalTupleRecord::getId(p),
                      ResolvedFunctionalTupleRecord::getFirstComponent(p),
                      ResolvedFunctionalTupleRecord::getComponentCount(p),
                      ResolvedFunctionalTupleRecord::getOrdinal(p),
                      ResolvedFunctionalTupleRecord::getFlags(p)});
                 return Status::Ok;
               });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::ResolvedFunctionalTupleSetComponents,
               [&](const auto &, uint64_t, const uint8_t *p) {
                 result.resolvedFunctionalTupleSetComponents.push_back(
                     {ResolvedFunctionalTupleComponentRecord::getTuple(p),
                      ResolvedFunctionalTupleComponentRecord::getTarget(p),
                      ResolvedFunctionalTupleComponentRecord::getBin(p),
                      ResolvedFunctionalTupleComponentRecord::getValueSet(p),
                      ResolvedFunctionalTupleComponentRecord::getOrdinal(p),
                      ResolvedFunctionalTupleComponentRecord::getFlags(p)});
                 return Status::Ok;
               });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::ResolvedFunctionalValueAtoms,
               [&](const auto &, uint64_t, const uint8_t *p) {
                 result.resolvedFunctionalValueAtoms.push_back(
                     {ResolvedFunctionalValueAtomRecord::getValueSet(p),
                      ResolvedFunctionalValueAtomRecord::getRealLowBits(p),
                      ResolvedFunctionalValueAtomRecord::getRealHighBits(p),
                      ResolvedFunctionalValueAtomRecord::getFirstLimb(p),
                      ResolvedFunctionalValueAtomRecord::getLimbCount(p),
                      ResolvedFunctionalValueAtomRecord::getOrdinal(p),
                      static_cast<FunctionalValueAtomKind>(
                          ResolvedFunctionalValueAtomRecord::getKind(p)),
                      ResolvedFunctionalValueAtomRecord::getFlags(p)});
                 return Status::Ok;
               });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::ResolvedFunctionalValueLimbs,
               [&](const auto &, uint64_t, const uint8_t *p) {
                 result.resolvedFunctionalValueLimbs.push_back(
                     {ResolvedFunctionalValueLimbRecord::getValueSet(p),
                      ResolvedFunctionalValueLimbRecord::getAtomOrdinal(p),
                      ResolvedFunctionalValueLimbRecord::getOrdinal(p),
                      ResolvedFunctionalValueLimbRecord::getLowAval(p),
                      ResolvedFunctionalValueLimbRecord::getLowBval(p),
                      ResolvedFunctionalValueLimbRecord::getHighAval(p),
                      ResolvedFunctionalValueLimbRecord::getHighBval(p),
                      ResolvedFunctionalValueLimbRecord::getWildcardMask(p)});
                 return Status::Ok;
               });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::FunctionalBinPlans,
               [&](const auto &, uint64_t, const uint8_t *p) {
                 result.functionalBinPlans.push_back(
                     {FunctionalBinPlanRecord::getBin(p),
                      FunctionalBinPlanRecord::getValueSet(p),
                      FunctionalBinPlanRecord::getIffExpression(p),
                      FunctionalBinPlanRecord::getCardinalityExpression(p),
                      FunctionalBinPlanRecord::getArrayCardinality(p),
                      static_cast<FunctionalBinArrayMode>(
                          FunctionalBinPlanRecord::getArrayMode(p)),
                      static_cast<FunctionalBinDistributionKind>(
                          FunctionalBinPlanRecord::getDistribution(p)),
                      FunctionalBinPlanRecord::getFlags(p)});
                 return Status::Ok;
               });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::FunctionalTupleSets,
               [&](const auto &, uint64_t, const uint8_t *p) {
                 result.functionalTupleSets.push_back(
                     {FunctionalTupleSetRecord::getId(p),
                      FunctionalTupleSetRecord::getCross(p),
                      FunctionalTupleSetRecord::getSelector(p),
                      FunctionalTupleSetRecord::getFirstTuple(p),
                      FunctionalTupleSetRecord::getTupleCount(p),
                      static_cast<FunctionalTupleElementMode>(
                          FunctionalTupleSetRecord::getElementMode(p)),
                      FunctionalTupleSetRecord::getFlags(p)});
                 return Status::Ok;
               });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::FunctionalTupleSetTuples,
               [&](const auto &, uint64_t, const uint8_t *p) {
                 result.functionalTupleSetTuples.push_back(
                     {FunctionalTupleRecord::getTupleSet(p),
                      FunctionalTupleRecord::getId(p),
                      FunctionalTupleRecord::getFirstComponent(p),
                      FunctionalTupleRecord::getComponentCount(p),
                      FunctionalTupleRecord::getOrdinal(p),
                      FunctionalTupleRecord::getFlags(p)});
                 return Status::Ok;
               });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::FunctionalTupleSetComponents,
               [&](const auto &, uint64_t, const uint8_t *p) {
                 result.functionalTupleSetComponents.push_back(
                     {FunctionalTupleComponentRecord::getTuple(p),
                      FunctionalTupleComponentRecord::getTarget(p),
                      FunctionalTupleComponentRecord::getBin(p),
                      FunctionalTupleComponentRecord::getValueSet(p),
                      FunctionalTupleComponentRecord::getOrdinal(p),
                      FunctionalTupleComponentRecord::getFlags(p)});
                 return Status::Ok;
               });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::FunctionalOptionPlans,
               [&](const auto &, uint64_t, const uint8_t *p) {
                 result.functionalOptionPlans.push_back(
                     {FunctionalOptionPlanRecord::getOwner(p),
                      FunctionalOptionPlanRecord::getExpression(p),
                      static_cast<FunctionalConfigurationOptionOwnerKind>(
                          FunctionalOptionPlanRecord::getOwnerKind(p)),
                      static_cast<FunctionalOptionScopeKind>(
                          FunctionalOptionPlanRecord::getScope(p)),
                      static_cast<FunctionalConfigurationOptionKind>(
                          FunctionalOptionPlanRecord::getOption(p)),
                      FunctionalOptionPlanRecord::getOrdinal(p),
                      FunctionalOptionPlanRecord::getFlags(p)});
                 return Status::Ok;
               });
    if (st != Status::Ok)
      return st;
    st = fixed(
        SectionKind::FunctionalConfigurationOptions,
        [&](const auto &e, uint64_t i, const uint8_t *p) {
          const auto *stringValue =
              str(FunctionalConfigurationOptionRecord::getString(p), e, i,
                  FunctionalConfigurationOptionRecord::StringName);
          if (!stringValue)
            return Status::InvalidReference;
          FunctionalConfigurationOption v;
          v.type = FunctionalConfigurationOptionRecord::getType(p);
          std::copy(FunctionalConfigurationOptionRecord::getConfiguration(p),
                    FunctionalConfigurationOptionRecord::getConfiguration(p) +
                        v.configuration.size(),
                    v.configuration.begin());
          v.owner = FunctionalConfigurationOptionRecord::getOwner(p);
          v.ownerKind = static_cast<FunctionalConfigurationOptionOwnerKind>(
              FunctionalConfigurationOptionRecord::getOwnerKind(p));
          v.scope = static_cast<FunctionalOptionScopeKind>(
              FunctionalConfigurationOptionRecord::getScope(p));
          v.option = static_cast<FunctionalConfigurationOptionKind>(
              FunctionalConfigurationOptionRecord::getOption(p));
          v.valueKind = static_cast<FunctionalConfigurationValueKind>(
              FunctionalConfigurationOptionRecord::getValueKind(p));
          v.flags = FunctionalConfigurationOptionRecord::getFlags(p);
          v.value = FunctionalConfigurationOptionRecord::getValue(p);
          v.stringValue = *stringValue;
          result.functionalConfigurationOptions.push_back(std::move(v));
          return Status::Ok;
        });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::ResolvedFunctionalBinPlans, [&](const auto &,
                                                            uint64_t,
                                                            const uint8_t *p) {
      ResolvedFunctionalBinPlan v;
      v.type = ResolvedFunctionalBinPlanRecord::getType(p);
      std::copy(ResolvedFunctionalBinPlanRecord::getConfiguration(p),
                ResolvedFunctionalBinPlanRecord::getConfiguration(p) +
                    v.configuration.size(),
                v.configuration.begin());
      v.bin = ResolvedFunctionalBinPlanRecord::getBin(p);
      v.valueSet = ResolvedFunctionalBinPlanRecord::getValueSet(p);
      v.iffExpression = ResolvedFunctionalBinPlanRecord::getIffExpression(p);
      v.cardinalityExpression =
          ResolvedFunctionalBinPlanRecord::getCardinalityExpression(p);
      v.arrayCardinality =
          ResolvedFunctionalBinPlanRecord::getArrayCardinality(p);
      v.arrayMode = static_cast<FunctionalBinArrayMode>(
          ResolvedFunctionalBinPlanRecord::getArrayMode(p));
      v.distribution = static_cast<FunctionalBinDistributionKind>(
          ResolvedFunctionalBinPlanRecord::getDistribution(p));
      v.flags = ResolvedFunctionalBinPlanRecord::getFlags(p);
      result.resolvedFunctionalBinPlans.push_back(v);
      return Status::Ok;
    });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::FunctionalConfigurations,
               [&](const auto &, uint64_t, const uint8_t *p) {
                 FunctionalConfiguration v;
                 v.type = FunctionalConfigurationRecord::getType(p);
                 std::copy(FunctionalConfigurationRecord::getConfiguration(p),
                           FunctionalConfigurationRecord::getConfiguration(p) +
                               v.configuration.size(),
                           v.configuration.begin());
                 v.flags = FunctionalConfigurationRecord::getFlags(p);
                 result.functionalConfigurations.push_back(v);
                 return Status::Ok;
               });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::ResolvedFunctionalItems,
               [&](const auto &e, uint64_t i, const uint8_t *p) {
                 const auto *name =
                     str(ResolvedFunctionalItemRecord::getName(p), e, i,
                         ResolvedFunctionalItemRecord::NameName);
                 const auto *hierarchy =
                     str(ResolvedFunctionalItemRecord::getHierarchy(p), e, i,
                         ResolvedFunctionalItemRecord::HierarchyName);
                 if (!name || !hierarchy)
                   return Status::InvalidReference;
                 ResolvedFunctionalItem v;
                 v.type = ResolvedFunctionalItemRecord::getType(p);
                 std::copy(ResolvedFunctionalItemRecord::getConfiguration(p),
                           ResolvedFunctionalItemRecord::getConfiguration(p) +
                               v.configuration.size(),
                           v.configuration.begin());
                 v.id = ResolvedFunctionalItemRecord::getId(p);
                 v.templateItem =
                     ResolvedFunctionalItemRecord::getTemplateItem(p);
                 v.name = *name;
                 v.kind = static_cast<FunctionalItemKind>(
                     ResolvedFunctionalItemRecord::getKind(p));
                 v.flags = ResolvedFunctionalItemRecord::getFlags(p);
                 v.goal = ResolvedFunctionalItemRecord::getGoal(p);
                 v.weight = ResolvedFunctionalItemRecord::getWeight(p);
                 v.ordinal = ResolvedFunctionalItemRecord::getOrdinal(p);
                 v.hierarchy = *hierarchy;
                 result.resolvedFunctionalItems.push_back(std::move(v));
                 return Status::Ok;
               });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::ResolvedFunctionalBins,
               [&](const auto &e, uint64_t i, const uint8_t *p) {
                 const auto *name =
                     str(ResolvedFunctionalBinRecord::getName(p), e, i,
                         ResolvedFunctionalBinRecord::NameName);
                 const auto *hierarchy =
                     str(ResolvedFunctionalBinRecord::getHierarchy(p), e, i,
                         ResolvedFunctionalBinRecord::HierarchyName);
                 if (!name || !hierarchy)
                   return Status::InvalidReference;
                 ResolvedFunctionalBin v;
                 v.type = ResolvedFunctionalBinRecord::getType(p);
                 std::copy(ResolvedFunctionalBinRecord::getConfiguration(p),
                           ResolvedFunctionalBinRecord::getConfiguration(p) +
                               v.configuration.size(),
                           v.configuration.begin());
                 v.id = ResolvedFunctionalBinRecord::getId(p);
                 v.templateBin = ResolvedFunctionalBinRecord::getTemplateBin(p);
                 v.item = ResolvedFunctionalBinRecord::getItem(p);
                 v.name = *name;
                 v.kind = static_cast<FunctionalBinKind>(
                     ResolvedFunctionalBinRecord::getKind(p));
                 v.flags = ResolvedFunctionalBinRecord::getFlags(p);
                 v.ordinal = ResolvedFunctionalBinRecord::getOrdinal(p);
                 v.expansionOrdinal =
                     ResolvedFunctionalBinRecord::getExpansionOrdinal(p);
                 v.atLeast = ResolvedFunctionalBinRecord::getAtLeast(p);
                 v.hierarchy = *hierarchy;
                 result.resolvedFunctionalBins.push_back(std::move(v));
                 return Status::Ok;
               });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::ResolvedTransitionSteps, [&](const auto &, uint64_t,
                                                         const uint8_t *p) {
      ResolvedTransitionStep v;
      v.type = ResolvedTransitionStepRecord::getType(p);
      std::copy(ResolvedTransitionStepRecord::getConfiguration(p),
                ResolvedTransitionStepRecord::getConfiguration(p) +
                    v.configuration.size(),
                v.configuration.begin());
      v.bin = ResolvedTransitionStepRecord::getBin(p);
      v.alternative = ResolvedTransitionStepRecord::getAlternative(p);
      v.valueSet = ResolvedTransitionStepRecord::getValueSet(p);
      v.lowerBound = ResolvedTransitionStepRecord::getLowerBound(p);
      v.upperBound = ResolvedTransitionStepRecord::getUpperBound(p);
      v.ordinal = ResolvedTransitionStepRecord::getOrdinal(p);
      v.flags = ResolvedTransitionStepRecord::getFlags(p);
      result.resolvedTransitionSteps.push_back(v);
      return Status::Ok;
    });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::ResolvedCrossPlans,
               [&](const auto &, uint64_t, const uint8_t *p) {
                 ResolvedCrossPlan v;
                 v.type = ResolvedCrossPlanRecord::getType(p);
                 std::copy(ResolvedCrossPlanRecord::getConfiguration(p),
                           ResolvedCrossPlanRecord::getConfiguration(p) +
                               v.configuration.size(),
                           v.configuration.begin());
                 v.cross = ResolvedCrossPlanRecord::getCross(p);
                 v.retainAutoPolicy = static_cast<CrossRetainAutoPolicy>(
                     ResolvedCrossPlanRecord::getRetainAutoPolicy(p));
                 v.flags = ResolvedCrossPlanRecord::getFlags(p);
                 v.firstAutomaticBinCountLimb =
                     ResolvedCrossPlanRecord::getFirstAutomaticBinCountLimb(p);
                 v.automaticBinCountLimbCount =
                     ResolvedCrossPlanRecord::getAutomaticBinCountLimbCount(p);
                 v.rootNode = ResolvedCrossPlanRecord::getRootNode(p);
                 result.resolvedCrossPlans.push_back(v);
                 return Status::Ok;
               });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::ResolvedCrossAutomaticBinCountLimbs,
               [&](const auto &, uint64_t, const uint8_t *p) {
                 result.resolvedCrossAutomaticBinCountLimbs.push_back(
                     ResolvedCrossAutomaticBinCountLimbRecord::getValue(p));
                 return Status::Ok;
               });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::ResolvedCrossAutomaticNodes, [&](const auto &,
                                                             uint64_t,
                                                             const uint8_t *p) {
      ResolvedCrossAutomaticNode v;
      v.type = ResolvedCrossAutomaticNodeRecord::getType(p);
      std::copy(ResolvedCrossAutomaticNodeRecord::getConfiguration(p),
                ResolvedCrossAutomaticNodeRecord::getConfiguration(p) +
                    v.configuration.size(),
                v.configuration.begin());
      v.cross = ResolvedCrossAutomaticNodeRecord::getCross(p);
      v.id = ResolvedCrossAutomaticNodeRecord::getId(p);
      v.targetOrdinal = ResolvedCrossAutomaticNodeRecord::getTargetOrdinal(p);
      v.firstEdge = ResolvedCrossAutomaticNodeRecord::getFirstEdge(p);
      v.edgeCount = ResolvedCrossAutomaticNodeRecord::getEdgeCount(p);
      v.flags = ResolvedCrossAutomaticNodeRecord::getFlags(p);
      result.resolvedCrossAutomaticNodes.push_back(v);
      return Status::Ok;
    });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::ResolvedCrossAutomaticEdges,
               [&](const auto &, uint64_t, const uint8_t *p) {
                 ResolvedCrossAutomaticEdge v;
                 v.node = ResolvedCrossAutomaticEdgeRecord::getNode(p);
                 v.bin = ResolvedCrossAutomaticEdgeRecord::getBin(p);
                 v.child = ResolvedCrossAutomaticEdgeRecord::getChild(p);
                 v.ordinal = ResolvedCrossAutomaticEdgeRecord::getOrdinal(p);
                 v.flags = ResolvedCrossAutomaticEdgeRecord::getFlags(p);
                 result.resolvedCrossAutomaticEdges.push_back(v);
                 return Status::Ok;
               });
    if (st != Status::Ok)
      return st;
    st = fixed(
        SectionKind::ResolvedCrossSelectorBindings,
        [&](const auto &, uint64_t, const uint8_t *p) {
          ResolvedCrossSelectorBinding v;
          v.type = ResolvedCrossSelectorBindingRecord::getType(p);
          std::copy(ResolvedCrossSelectorBindingRecord::getConfiguration(p),
                    ResolvedCrossSelectorBindingRecord::getConfiguration(p) +
                        v.configuration.size(),
                    v.configuration.begin());
          v.cross = ResolvedCrossSelectorBindingRecord::getCross(p);
          v.node = ResolvedCrossSelectorBindingRecord::getNode(p);
          v.valueSet = ResolvedCrossSelectorBindingRecord::getValueSet(p);
          v.withExpression =
              ResolvedCrossSelectorBindingRecord::getWithExpression(p);
          v.tupleSet = ResolvedCrossSelectorBindingRecord::getTupleSet(p);
          v.matchesPolicy = static_cast<CrossMatchesPolicy>(
              ResolvedCrossSelectorBindingRecord::getMatchesPolicy(p));
          v.flags = ResolvedCrossSelectorBindingRecord::getFlags(p);
          v.matchesCount =
              ResolvedCrossSelectorBindingRecord::getMatchesCount(p);
          result.resolvedCrossSelectorBindings.push_back(v);
          return Status::Ok;
        });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::ResolvedFunctionalValueSets, [&](const auto &,
                                                             uint64_t,
                                                             const uint8_t *p) {
      ResolvedFunctionalValueSet v;
      v.type = ResolvedFunctionalValueSetRecord::getType(p);
      std::copy(ResolvedFunctionalValueSetRecord::getConfiguration(p),
                ResolvedFunctionalValueSetRecord::getConfiguration(p) +
                    v.configuration.size(),
                v.configuration.begin());
      v.id = ResolvedFunctionalValueSetRecord::getId(p);
      v.templateValueSet =
          ResolvedFunctionalValueSetRecord::getTemplateValueSet(p);
      v.item = ResolvedFunctionalValueSetRecord::getItem(p);
      v.firstAtom = ResolvedFunctionalValueSetRecord::getFirstAtom(p);
      v.atomCount = ResolvedFunctionalValueSetRecord::getAtomCount(p);
      v.bitWidth = ResolvedFunctionalValueSetRecord::getBitWidth(p);
      v.kind = static_cast<FunctionalValueSetKind>(
          ResolvedFunctionalValueSetRecord::getKind(p));
      v.flags = ResolvedFunctionalValueSetRecord::getFlags(p);
      v.signedness = static_cast<CoverageSignedness>(
          ResolvedFunctionalValueSetRecord::getSignedness(p));
      v.ownerBin = ResolvedFunctionalValueSetRecord::getOwnerBin(p);
      v.ownerSelector = ResolvedFunctionalValueSetRecord::getOwnerSelector(p);
      v.ownerOrdinal = ResolvedFunctionalValueSetRecord::getOwnerOrdinal(p);
      v.ownerSubordinal =
          ResolvedFunctionalValueSetRecord::getOwnerSubordinal(p);
      v.role = static_cast<ResolvedFunctionalValueSetRole>(
          ResolvedFunctionalValueSetRecord::getRole(p));
      result.resolvedFunctionalValueSets.push_back(v);
      return Status::Ok;
    });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::ResolvedTransitionAlternatives, [&](const auto &,
                                                                uint64_t,
                                                                const uint8_t
                                                                    *p) {
      ResolvedTransitionAlternative v;
      v.type = ResolvedTransitionAlternativeRecord::getType(p);
      std::copy(ResolvedTransitionAlternativeRecord::getConfiguration(p),
                ResolvedTransitionAlternativeRecord::getConfiguration(p) +
                    v.configuration.size(),
                v.configuration.begin());
      v.id = ResolvedTransitionAlternativeRecord::getId(p);
      v.bin = ResolvedTransitionAlternativeRecord::getBin(p);
      v.templateAlternativeOrdinal =
          ResolvedTransitionAlternativeRecord::getTemplateAlternativeOrdinal(p);
      v.expansionOrdinal =
          ResolvedTransitionAlternativeRecord::getExpansionOrdinal(p);
      v.firstStep = ResolvedTransitionAlternativeRecord::getFirstStep(p);
      v.stepCount = ResolvedTransitionAlternativeRecord::getStepCount(p);
      v.ordinal = ResolvedTransitionAlternativeRecord::getOrdinal(p);
      v.flags = ResolvedTransitionAlternativeRecord::getFlags(p);
      result.resolvedTransitionAlternatives.push_back(v);
      return Status::Ok;
    });
    if (st != Status::Ok)
      return st;
    st = fixed(
        SectionKind::ResolvedFunctionalBinGroups,
        [&](const auto &, uint64_t, const uint8_t *p) {
          ResolvedFunctionalBinGroup v;
          v.type = ResolvedFunctionalBinGroupRecord::getType(p);
          std::copy(ResolvedFunctionalBinGroupRecord::getConfiguration(p),
                    ResolvedFunctionalBinGroupRecord::getConfiguration(p) +
                        v.configuration.size(),
                    v.configuration.begin());
          v.item = ResolvedFunctionalBinGroupRecord::getItem(p);
          v.templateBin = ResolvedFunctionalBinGroupRecord::getTemplateBin(p);
          v.firstBin = ResolvedFunctionalBinGroupRecord::getFirstBin(p);
          v.binCount = ResolvedFunctionalBinGroupRecord::getBinCount(p);
          v.arrayCardinality =
              ResolvedFunctionalBinGroupRecord::getArrayCardinality(p);
          v.arrayMode = static_cast<FunctionalBinArrayMode>(
              ResolvedFunctionalBinGroupRecord::getArrayMode(p));
          v.distribution = static_cast<FunctionalBinDistributionKind>(
              ResolvedFunctionalBinGroupRecord::getDistribution(p));
          v.flags = ResolvedFunctionalBinGroupRecord::getFlags(p);
          v.kind = static_cast<FunctionalBinKind>(
              ResolvedFunctionalBinGroupRecord::getKind(p));
          result.resolvedFunctionalBinGroups.push_back(v);
          return Status::Ok;
        });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::ResolvedTransitionExpansionGroups, [&](const auto &,
                                                                   uint64_t,
                                                                   const uint8_t
                                                                       *p) {
      ResolvedTransitionExpansionGroup v;
      v.type = ResolvedTransitionExpansionGroupRecord::getType(p);
      std::copy(ResolvedTransitionExpansionGroupRecord::getConfiguration(p),
                ResolvedTransitionExpansionGroupRecord::getConfiguration(p) +
                    v.configuration.size(),
                v.configuration.begin());
      v.item = ResolvedTransitionExpansionGroupRecord::getItem(p);
      v.templateBin = ResolvedTransitionExpansionGroupRecord::getTemplateBin(p);
      v.templateAlternativeOrdinal =
          ResolvedTransitionExpansionGroupRecord::getTemplateAlternativeOrdinal(
              p);
      v.firstAlternative =
          ResolvedTransitionExpansionGroupRecord::getFirstAlternative(p);
      v.alternativeCount =
          ResolvedTransitionExpansionGroupRecord::getAlternativeCount(p);
      v.flags = ResolvedTransitionExpansionGroupRecord::getFlags(p);
      v.expansionCount =
          ResolvedTransitionExpansionGroupRecord::getExpansionCount(p);
      result.resolvedTransitionExpansionGroups.push_back(v);
      return Status::Ok;
    });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::TransitionAlternatives,
               [&](const auto &, uint64_t, const uint8_t *p) {
                 result.transitionAlternatives.push_back(
                     {TransitionAlternativeRecord::getBin(p),
                      TransitionAlternativeRecord::getTerminalValueSet(p),
                      TransitionAlternativeRecord::getFirstStep(p),
                      TransitionAlternativeRecord::getStepCount(p),
                      TransitionAlternativeRecord::getOrdinal(p),
                      TransitionAlternativeRecord::getFlags(p)});
                 return Status::Ok;
               });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::TransitionSteps,
               [&](const auto &, uint64_t, const uint8_t *p) {
                 result.transitionSteps.push_back(
                     {TransitionStepRecord::getBin(p),
                      TransitionStepRecord::getValueSet(p),
                      TransitionStepRecord::getLowerExpression(p),
                      TransitionStepRecord::getUpperExpression(p),
                      TransitionStepRecord::getLowerBound(p),
                      TransitionStepRecord::getUpperBound(p),
                      TransitionStepRecord::getAlternativeOrdinal(p),
                      TransitionStepRecord::getOrdinal(p),
                      static_cast<TransitionRepetitionKind>(
                          TransitionStepRecord::getRepetition(p)),
                      TransitionStepRecord::getFlags(p)});
                 return Status::Ok;
               });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::CrossTargets, [&](const auto &, uint64_t,
                                              const uint8_t *p) {
      result.crossTargets.push_back(
          {CrossTargetRecord::getCross(p), CrossTargetRecord::getTarget(p),
           CrossTargetRecord::getOrdinal(p),
           CrossTargetRecord::getTupleBitOffset(p),
           CrossTargetRecord::getTupleBitWidth(p),
           static_cast<FunctionalExpressionResultKind>(
               CrossTargetRecord::getTupleResultKind(p)),
           static_cast<CoverageSignedness>(
               CrossTargetRecord::getTupleSignedness(p)),
           CrossTargetRecord::getTupleFlags(p)});
      return Status::Ok;
    });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::CrossBins, [&](const auto &, uint64_t,
                                           const uint8_t *p) {
      result.crossBins.push_back(
          {CrossBinRecord::getBin(p), CrossBinRecord::getCross(p),
           CrossBinRecord::getRootSelector(p), CrossBinRecord::getFlags(p)});
      return Status::Ok;
    });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::CrossSelectorNodes, [&](const auto &, uint64_t,
                                                    const uint8_t *p) {
      result.crossSelectorNodes.push_back(
          {CrossSelectorNodeRecord::getId(p),
           CrossSelectorNodeRecord::getCross(p),
           CrossSelectorNodeRecord::getTarget(p),
           CrossSelectorNodeRecord::getBin(p),
           CrossSelectorNodeRecord::getValueSet(p),
           CrossSelectorNodeRecord::getWithExpression(p),
           CrossSelectorNodeRecord::getConstructionExpression(p),
           CrossSelectorNodeRecord::getTupleSet(p),
           CrossSelectorNodeRecord::getMatchesExpression(p),
           CrossSelectorNodeRecord::getFirstOperand(p),
           CrossSelectorNodeRecord::getOperandCount(p),
           static_cast<CrossSelectorKind>(CrossSelectorNodeRecord::getKind(p)),
           CrossSelectorNodeRecord::getOrdinal(p),
           CrossSelectorNodeRecord::getFlags(p),
           static_cast<CrossMatchesPolicy>(
               CrossSelectorNodeRecord::getMatchesPolicy(p)),
           CrossSelectorNodeRecord::getMatchesCount(p)});
      return Status::Ok;
    });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::CrossSelectorOperands,
               [&](const auto &, uint64_t, const uint8_t *p) {
                 result.crossSelectorOperands.push_back(
                     {CrossSelectorOperandRecord::getNode(p),
                      CrossSelectorOperandRecord::getOperand(p),
                      CrossSelectorOperandRecord::getOrdinal(p)});
                 return Status::Ok;
               });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::SparseCrossTupleComponents,
               [&](const auto &, uint64_t, const uint8_t *p) {
                 result.sparseCrossTupleComponents.push_back(
                     SparseCrossTupleComponentRecord::getBin(p));
                 return Status::Ok;
               });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::FunctionalValueSets,
               [&](const auto &, uint64_t, const uint8_t *p) {
                 result.functionalValueSets.push_back(
                     {FunctionalValueSetRecord::getId(p),
                      FunctionalValueSetRecord::getItem(p),
                      FunctionalValueSetRecord::getFirstAtom(p),
                      FunctionalValueSetRecord::getAtomCount(p),
                      FunctionalValueSetRecord::getBitWidth(p),
                      static_cast<FunctionalValueSetKind>(
                          FunctionalValueSetRecord::getKind(p)),
                      FunctionalValueSetRecord::getFlags(p),
                      static_cast<CoverageSignedness>(
                          FunctionalValueSetRecord::getSignedness(p)),
                      FunctionalValueSetRecord::getSetExpression(p)});
                 return Status::Ok;
               });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::FunctionalValueAtoms,
               [&](const auto &, uint64_t, const uint8_t *p) {
                 result.functionalValueAtoms.push_back(
                     {FunctionalValueAtomRecord::getValueSet(p),
                      FunctionalValueAtomRecord::getRealLowBits(p),
                      FunctionalValueAtomRecord::getRealHighBits(p),
                      FunctionalValueAtomRecord::getFirstLimb(p),
                      FunctionalValueAtomRecord::getLimbCount(p),
                      FunctionalValueAtomRecord::getOrdinal(p),
                      static_cast<FunctionalValueAtomKind>(
                          FunctionalValueAtomRecord::getKind(p)),
                      FunctionalValueAtomRecord::getFlags(p),
                      FunctionalValueAtomRecord::getLowerExpression(p),
                      FunctionalValueAtomRecord::getUpperExpression(p)});
                 return Status::Ok;
               });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::FunctionalValueLimbs,
               [&](const auto &, uint64_t, const uint8_t *p) {
                 result.functionalValueLimbs.push_back(
                     {FunctionalValueLimbRecord::getValueSet(p),
                      FunctionalValueLimbRecord::getAtomOrdinal(p),
                      FunctionalValueLimbRecord::getOrdinal(p),
                      FunctionalValueLimbRecord::getLowAval(p),
                      FunctionalValueLimbRecord::getLowBval(p),
                      FunctionalValueLimbRecord::getHighAval(p),
                      FunctionalValueLimbRecord::getHighBval(p),
                      FunctionalValueLimbRecord::getWildcardMask(p)});
                 return Status::Ok;
               });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::FunctionalExpressions, [&](const auto &, uint64_t,
                                                       const uint8_t *p) {
      FunctionalExpression v;
      v.id = FunctionalExpressionRecord::getId(p);
      v.owner = FunctionalExpressionRecord::getOwner(p);
      v.ownerKind = static_cast<FunctionalExpressionOwnerKind>(
          FunctionalExpressionRecord::getOwnerKind(p));
      v.role = static_cast<FunctionalExpressionRole>(
          FunctionalExpressionRecord::getRole(p));
      v.resultKind = static_cast<FunctionalExpressionResultKind>(
          FunctionalExpressionRecord::getResultKind(p));
      v.flags = FunctionalExpressionRecord::getFlags(p);
      std::copy(FunctionalExpressionRecord::getSemanticDigest(p),
                FunctionalExpressionRecord::getSemanticDigest(p) +
                    v.semanticDigest.size(),
                v.semanticDigest.begin());
      v.bitWidth = FunctionalExpressionRecord::getBitWidth(p);
      v.signedness = static_cast<CoverageSignedness>(
          FunctionalExpressionRecord::getSignedness(p));
      v.ownerOrdinal = FunctionalExpressionRecord::getOwnerOrdinal(p);
      v.ownerSubordinal = FunctionalExpressionRecord::getOwnerSubordinal(p);
      v.evaluationPhase = static_cast<FunctionalExpressionEvaluationPhase>(
          FunctionalExpressionRecord::getEvaluationPhase(p));
      v.resultOrdinal = FunctionalExpressionRecord::getResultOrdinal(p);
      result.functionalExpressions.push_back(v);
      return Status::Ok;
    });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::FunctionalFormals,
               [&](const auto &e, uint64_t i, const uint8_t *p) {
                 const auto *name = str(FunctionalFormalRecord::getName(p), e,
                                        i, FunctionalFormalRecord::NameName);
                 if (!name)
                   return Status::InvalidReference;
                 result.functionalFormals.push_back(
                     {FunctionalFormalRecord::getId(p),
                      FunctionalFormalRecord::getType(p), *name,
                      static_cast<FunctionalFormalKind>(
                          FunctionalFormalRecord::getKind(p)),
                      static_cast<FunctionalFormalDirection>(
                          FunctionalFormalRecord::getDirection(p)),
                      static_cast<FunctionalExpressionResultKind>(
                          FunctionalFormalRecord::getResultKind(p)),
                      FunctionalFormalRecord::getFlags(p),
                      FunctionalFormalRecord::getBitWidth(p),
                      static_cast<CoverageSignedness>(
                          FunctionalFormalRecord::getSignedness(p)),
                      FunctionalFormalRecord::getOrdinal(p),
                      FunctionalFormalRecord::getDefaultExpression(p)});
                 return Status::Ok;
               });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::Scopes, [&](const auto &e, uint64_t i,
                                        const uint8_t *p) {
      const auto *s = str(ScopeRecord::getName(p), e, i, ScopeRecord::NameName);
      const auto *definition =
          str(ScopeRecord::getDefinition(p), e, i, ScopeRecord::DefinitionName);
      if (!s || !definition)
        return Status::InvalidReference;
      result.scopes.push_back({ScopeRecord::getId(p), ScopeRecord::getParent(p),
                               *s, ScopeRecord::getKind(p), *definition});
      return Status::Ok;
    });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::LinePoints, [&](const auto &e, uint64_t i,
                                            const uint8_t *p) {
      const auto *macro = str(LinePointRecord::getMacroName(p), e, i,
                              LinePointRecord::MacroNameName);
      if (!macro)
        return Status::InvalidReference;
      result.linePoints.push_back(
          {LinePointRecord::getId(p), LinePointRecord::getFile(p),
           LinePointRecord::getEndFile(p), LinePointRecord::getScope(p), *macro,
           LinePointRecord::getLine(p), LinePointRecord::getColumn(p),
           LinePointRecord::getEndLine(p), LinePointRecord::getEndColumn(p),
           LinePointRecord::getSemanticPhase(p), LinePointRecord::getFlags(p)});
      return Status::Ok;
    });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::ToggleObjects, [&](const auto &e, uint64_t i,
                                               const uint8_t *p) {
      const auto *s = str(ToggleObjectRecord::getName(p), e, i,
                          ToggleObjectRecord::NameName);
      if (!s)
        return Status::InvalidReference;
      result.toggleObjects.push_back(
          {ToggleObjectRecord::getId(p), ToggleObjectRecord::getScope(p),
           ToggleObjectRecord::getFile(p), *s,
           ToggleObjectRecord::getTypeRoot(p),
           ToggleObjectRecord::getBitWidth(p), ToggleObjectRecord::getLine(p),
           ToggleObjectRecord::getColumn(p), ToggleObjectRecord::getEndLine(p),
           ToggleObjectRecord::getEndColumn(p),
           ToggleObjectRecord::getFlags(p)});
      return Status::Ok;
    });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::ToggleTypeDimensions, [&](const auto &e, uint64_t i,
                                                      const uint8_t *p) {
      const auto *name = str(ToggleDimensionRecord::getName(p), e, i,
                             ToggleDimensionRecord::NameName);
      if (!name)
        return Status::InvalidReference;
      result.toggleDimensions.push_back(
          {ToggleDimensionRecord::getObject(p),
           ToggleDimensionRecord::getParent(p),
           static_cast<ToggleDimensionKind>(ToggleDimensionRecord::getKind(p)),
           ToggleDimensionRecord::getFirstChild(p),
           ToggleDimensionRecord::getChildCount(p),
           ToggleDimensionRecord::getLeft(p),
           ToggleDimensionRecord::getRight(p),
           ToggleDimensionRecord::getBitOffset(p),
           ToggleDimensionRecord::getBitWidth(p), *name,
           ToggleDimensionRecord::getFlags(p)});
      return Status::Ok;
    });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::FunctionalTypes, [&](const auto &e, uint64_t i,
                                                 const uint8_t *p) {
      const auto *s = str(FunctionalTypeRecord::getName(p), e, i,
                          FunctionalTypeRecord::NameName);
      const auto *hierarchy = str(FunctionalTypeRecord::getHierarchy(p), e, i,
                                  FunctionalTypeRecord::HierarchyName);
      if (!s || !hierarchy)
        return Status::InvalidReference;
      result.functionalTypes.push_back(
          {FunctionalTypeRecord::getId(p), FunctionalTypeRecord::getScope(p),
           *s, FunctionalTypeRecord::getFlags(p),
           FunctionalTypeRecord::getLanguageVersion(p), *hierarchy});
      return Status::Ok;
    });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::FunctionalItems, [&](const auto &e, uint64_t i,
                                                 const uint8_t *p) {
      const auto *s = str(FunctionalItemRecord::getName(p), e, i,
                          FunctionalItemRecord::NameName);
      const auto *hierarchy = str(FunctionalItemRecord::getHierarchy(p), e, i,
                                  FunctionalItemRecord::HierarchyName);
      if (!s || !hierarchy)
        return Status::InvalidReference;
      result.functionalItems.push_back(
          {FunctionalItemRecord::getId(p), FunctionalItemRecord::getType(p), *s,
           static_cast<FunctionalItemKind>(FunctionalItemRecord::getKind(p)),
           FunctionalItemRecord::getFlags(p), FunctionalItemRecord::getGoal(p),
           FunctionalItemRecord::getWeight(p),
           FunctionalItemRecord::getOrdinal(p), *hierarchy});
      return Status::Ok;
    });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::FunctionalBins, [&](const auto &e, uint64_t i,
                                                const uint8_t *p) {
      const auto *s = str(FunctionalBinRecord::getName(p), e, i,
                          FunctionalBinRecord::NameName);
      const auto *hierarchy = str(FunctionalBinRecord::getHierarchy(p), e, i,
                                  FunctionalBinRecord::HierarchyName);
      if (!s || !hierarchy)
        return Status::InvalidReference;
      result.functionalBins.push_back(
          {FunctionalBinRecord::getId(p), FunctionalBinRecord::getItem(p), *s,
           static_cast<FunctionalBinKind>(FunctionalBinRecord::getKind(p)),
           FunctionalBinRecord::getFlags(p), FunctionalBinRecord::getAtLeast(p),
           FunctionalBinRecord::getOrdinal(p), *hierarchy});
      return Status::Ok;
    });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::TransitionPrograms,
               [&](const auto &, uint64_t, const uint8_t *p) {
                 result.transitionPrograms.push_back(
                     {TransitionProgramRecord::getBin(p),
                      TransitionProgramRecord::getItem(p),
                      TransitionProgramRecord::getFirstAlternative(p),
                      TransitionProgramRecord::getAlternativeCount(p),
                      TransitionProgramRecord::getFlags(p)});
                 return Status::Ok;
               });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::CrossPlans, [&](const auto &, uint64_t,
                                            const uint8_t *p) {
      result.crossPlans.push_back(
          {CrossPlanRecord::getItem(p), CrossPlanRecord::getFirstTarget(p),
           CrossPlanRecord::getTargetCount(p), CrossPlanRecord::getFirstBin(p),
           CrossPlanRecord::getBinCount(p),
           static_cast<CrossRetainAutoPolicy>(
               CrossPlanRecord::getRetainAutoPolicy(p)),
           CrossPlanRecord::getFlags(p), CrossPlanRecord::getIffExpression(p),
           CrossPlanRecord::getTupleElementType(p),
           CrossPlanRecord::getTupleProvenanceSpan(p),
           CrossPlanRecord::getTupleFlags(p)});
      return Status::Ok;
    });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::Exclusions,
               [&](const auto &e, uint64_t i, const uint8_t *p) {
                 uint32_t m = ExclusionRecord::getMetric(p);
                 const auto *s = str(ExclusionRecord::getReason(p), e, i,
                                     ExclusionRecord::ReasonName);
                 if (!s)
                   return Status::InvalidReference;
                 result.exclusions.push_back({ExclusionRecord::getEntity(p),
                                              static_cast<MetricKind>(m), *s,
                                              ExclusionRecord::getFlags(p)});
                 return Status::Ok;
               });
    if (st != Status::Ok)
      return st;
    std::map<UUID, uint32_t> expectedTagCounts;
    st = fixed(
        SectionKind::Runs, [&](const auto &e, uint64_t i, const uint8_t *p) {
          const auto *s = str(RunRecord::getName(p), e, i, RunRecord::NameName);
          if (!s)
            return Status::InvalidReference;
          Run v;
          std::copy(RunRecord::getUUID(p),
                    RunRecord::getUUID(p) + v.uuid.size(), v.uuid.begin());
          v.name = *s;
          v.status = RunRecord::getStatus(p);
          v.simulationTime = RunRecord::getSimulationTime(p);
          v.timestamp = RunRecord::getTimestamp(p);
          v.seed = RunRecord::getSeed(p);
          expectedTagCounts.emplace(v.uuid, RunRecord::getTagCount(p));
          v.flags = RunRecord::getFlags(p);
          result.runs.push_back(std::move(v));
          return Status::Ok;
        });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::ResolvedCovergroupInstances,
               [&](const auto &e, uint64_t i, const uint8_t *p) {
                 const auto *s = str(ResolvedInstanceRecord::getName(p), e, i,
                                     ResolvedInstanceRecord::NameName);
                 if (!s)
                   return Status::InvalidReference;
                 ResolvedInstance v;
                 std::copy(ResolvedInstanceRecord::getRun(p),
                           ResolvedInstanceRecord::getRun(p) + v.run.size(),
                           v.run.begin());
                 v.type = ResolvedInstanceRecord::getType(p);
                 v.id = ResolvedInstanceRecord::getId(p);
                 v.name = *s;
                 v.flags = ResolvedInstanceRecord::getFlags(p);
                 std::copy(ResolvedInstanceRecord::getConfiguration(p),
                           ResolvedInstanceRecord::getConfiguration(p) +
                               v.configuration.size(),
                           v.configuration.begin());
                 result.resolvedInstances.push_back(std::move(v));
                 return Status::Ok;
               });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::ResolvedInstanceOptions,
               [&](const auto &e, uint64_t i, const uint8_t *p) {
                 const auto *s = str(
                     ResolvedInstanceOptionRecord::getStringValue(p), e, i,
                     ResolvedInstanceOptionRecord::StringValueName);
                 if (!s)
                   return Status::InvalidReference;
                 ResolvedInstanceOption v;
                 std::copy(ResolvedInstanceOptionRecord::getRun(p),
                           ResolvedInstanceOptionRecord::getRun(p) +
                               v.run.size(),
                           v.run.begin());
                 v.instance = ResolvedInstanceOptionRecord::getInstance(p);
                 v.owner = ResolvedInstanceOptionRecord::getOwner(p);
                 v.ownerKind = static_cast<
                     FunctionalConfigurationOptionOwnerKind>(
                     ResolvedInstanceOptionRecord::getOwnerKind(p));
                 v.option = static_cast<FunctionalConfigurationOptionKind>(
                     ResolvedInstanceOptionRecord::getOption(p));
                 v.valueKind = static_cast<FunctionalConfigurationValueKind>(
                     ResolvedInstanceOptionRecord::getValueKind(p));
                 v.stringValue = *s;
                 v.value = ResolvedInstanceOptionRecord::getValue(p);
                 v.flags = ResolvedInstanceOptionRecord::getFlags(p);
                 result.resolvedInstanceOptions.push_back(std::move(v));
                 return Status::Ok;
               });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::Counters, [&](const auto &e, uint64_t i,
                                          const uint8_t *p) {
      uint32_t m = CounterRecord::getMetric(p);
      Counter v;
      std::copy(CounterRecord::getRun(p),
                CounterRecord::getRun(p) + v.run.size(), v.run.begin());
      v.entity = CounterRecord::getEntity(p);
      v.instance = CounterRecord::getInstance(p);
      v.value = CounterRecord::getValue(p);
      v.metric = static_cast<MetricKind>(m);
      v.subindex = CounterRecord::getSubindex(p);
      v.flags = CounterRecord::getFlags(p);
      result.counters.push_back(v);
      return Status::Ok;
    });
    if (st != Status::Ok)
      return st;
    st = fixed(SectionKind::SparseCrossTuples, [&](const auto &, uint64_t,
                                                   const uint8_t *p) {
      SparseCrossTuple v;
      std::copy(SparseCrossTupleRecord::getRun(p),
                SparseCrossTupleRecord::getRun(p) + v.run.size(),
                v.run.begin());
      v.instance = SparseCrossTupleRecord::getInstance(p);
      v.cross = SparseCrossTupleRecord::getCross(p);
      v.firstComponent = SparseCrossTupleRecord::getFirstComponent(p);
      v.componentCount = SparseCrossTupleRecord::getComponentCount(p);
      v.hits = SparseCrossTupleRecord::getHits(p);
      v.flags = SparseCrossTupleRecord::getFlags(p);
      result.sparseCrossTuples.push_back(v);
      return Status::Ok;
    });
    if (st != Status::Ok)
      return st;
    st =
        fixed(SectionKind::IllegalBinDiagnostics, [&](const auto &e, uint64_t i,
                                                      const uint8_t *p) {
          const auto *s = str(IllegalBinDiagnosticRecord::getMessage(p), e, i,
                              IllegalBinDiagnosticRecord::MessageName);
          if (!s)
            return Status::InvalidReference;
          IllegalBinDiagnostic v;
          std::copy(IllegalBinDiagnosticRecord::getRun(p),
                    IllegalBinDiagnosticRecord::getRun(p) + v.run.size(),
                    v.run.begin());
          v.bin = IllegalBinDiagnosticRecord::getBin(p);
          v.instance = IllegalBinDiagnosticRecord::getInstance(p);
          v.simulationTime = IllegalBinDiagnosticRecord::getSimulationTime(p);
          v.count = IllegalBinDiagnosticRecord::getCount(p);
          v.message = *s;
          v.flags = IllegalBinDiagnosticRecord::getFlags(p);
          result.illegalBinDiagnostics.push_back(std::move(v));
          return Status::Ok;
        });
    if (st != Status::Ok)
      return st;
    bool sawProducer = false;
    std::optional<std::pair<UUID, std::string>> previousMetadataKey;
    st = fixed(SectionKind::UserMetadata, [&](const auto &e, uint64_t i,
                                              const uint8_t *p) {
      UUID run;
      std::copy(UserMetadataRecord::getRun(p),
                UserMetadataRecord::getRun(p) + run.size(), run.begin());
      const auto *k = str(UserMetadataRecord::getKey(p), e, i,
                          UserMetadataRecord::KeyName),
                 *v = str(UserMetadataRecord::getValue(p), e, i,
                          UserMetadataRecord::ValueName);
      if (!k || !v)
        return Status::InvalidReference;
      std::pair<UUID, std::string> physicalKey{run, *k};
      if (previousMetadataKey && !(*previousMetadataKey < physicalKey))
        return fail(Status::UnsortedOrDuplicate, e.offset + i * 24, e.kind, i,
                    "run_key");
      previousMetadataKey = std::move(physicalKey);
      UUID zero{};
      if (run == zero && k->empty()) {
        if (sawProducer)
          return fail(Status::UnsortedOrDuplicate, e.offset + i * 24, e.kind, i,
                      "producer");
        sawProducer = true;
        result.producer = *v;
        return Status::Ok;
      }
      auto found = std::find_if(result.runs.begin(), result.runs.end(),
                                [&](const Run &r) { return r.uuid == run; });
      if (found == result.runs.end())
        return fail(Status::InvalidReference, e.offset + i * 24, e.kind, i,
                    "run");
      found->tags.emplace_back(*k, *v);
      return Status::Ok;
    });
    if (st != Status::Ok)
      return st;
    if (!sawProducer)
      return fail(Status::MissingSection, 0, SectionKind::UserMetadata,
                  NoRecord, "producer");
    for (const Run &run : result.runs) {
      auto expected = expectedTagCounts.find(run.uuid);
      if (expected == expectedTagCounts.end() ||
          expected->second != run.tags.size())
        return fail(Status::BadCount, 0, SectionKind::Runs, NoRecord,
                    "tag_count");
    }
    st = fixed(SectionKind::FunctionalSourceRanges,
               [&](const auto &e, uint64_t i, const uint8_t *p) {
                 uint32_t role = FunctionalSourceRangeRecord::getRole(p);
                 const auto *macro =
                     str(FunctionalSourceRangeRecord::getMacroName(p), e, i,
                         FunctionalSourceRangeRecord::MacroNameName);
                 if (!macro)
                   return Status::InvalidReference;
                 result.functionalSourceRanges.push_back(
                     {FunctionalSourceRangeRecord::getBin(p),
                      static_cast<FunctionalSourceRole>(role),
                      FunctionalSourceRangeRecord::getOrdinal(p),
                      FunctionalSourceRangeRecord::getFile(p),
                      FunctionalSourceRangeRecord::getEndFile(p), *macro,
                      FunctionalSourceRangeRecord::getLine(p),
                      FunctionalSourceRangeRecord::getColumn(p),
                      FunctionalSourceRangeRecord::getEndLine(p),
                      FunctionalSourceRangeRecord::getEndColumn(p),
                      FunctionalSourceRangeRecord::getFlags(p)});
                 return Status::Ok;
               });
    if (st != Status::Ok)
      return st;
    st = validate(result, diagnostic);
    if (st != Status::Ok)
      return st;
    output = std::move(result);
    if (diagnostic)
      *diagnostic = {};
    return Status::Ok;
#if OBELISK_COVERAGE_HAS_EXCEPTIONS
  } catch (const std::bad_alloc &) {
    return fail(Status::OutOfMemory, 0, SectionKind::Strings, NoRecord,
                "allocation");
  }
#endif
}

static Status discardTestDetailInPlace(Database &destination,
                                       Diagnostic *diagnostic) {
  if (destination.runs.empty())
    return Status::Ok;
  Run aggregate;
  aggregate.name = "aggregate";
  SHA256 h;
  std::vector<UUID> aggregateRunIDs;
  aggregateRunIDs.reserve(destination.runs.size());
  for (const auto &r : destination.runs) {
    aggregateRunIDs.push_back(r.uuid);
    aggregate.flags |= r.flags & RunContainsCoverageMask;
  }
  std::sort(aggregateRunIDs.begin(), aggregateRunIDs.end());
  for (const UUID &uuid : aggregateRunIDs)
    h.update(uuid.data(), uuid.size());
  Digest d = h.finish();
  std::copy(d.begin(), d.begin() + 16, aggregate.uuid.begin());
  if (isNil(aggregate.uuid))
    aggregate.uuid.back() = 1;
  std::sort(destination.resolvedInstances.begin(),
            destination.resolvedInstances.end(),
            [](const auto &left, const auto &right) {
              return std::tie(left.run, left.id) <
                     std::tie(right.run, right.id);
            });
  std::map<std::tuple<uint64_t, std::string, Digest>, uint64_t> namedInstances;
  std::map<std::pair<UUID, uint64_t>, uint64_t> remappedInstances;
  uint64_t nextInstanceID = 1;
  std::vector<ResolvedInstance> aggregateInstances;
  for (auto instance : destination.resolvedInstances) {
    const auto oldKey = std::make_pair(instance.run, instance.id);
    uint64_t aggregateID = 0;
    instance.run = aggregate.uuid;
    if (!instance.name.empty() &&
        !(instance.flags & ResolvedInstanceGeneratedName)) {
      auto key =
          std::make_tuple(instance.type, instance.name, instance.configuration);
      auto [position, inserted] =
          namedInstances.emplace(std::move(key), nextInstanceID);
      if (!inserted) {
        aggregateID = position->second;
        remappedInstances.emplace(oldKey, aggregateID);
        continue;
      }
    }
    aggregateID = nextInstanceID++;
    instance.id = aggregateID;
    aggregateInstances.push_back(std::move(instance));
    remappedInstances.emplace(oldKey, aggregateID);
  }
  destination.resolvedInstances = std::move(aggregateInstances);

  std::map<std::tuple<uint64_t, FunctionalConfigurationOptionOwnerKind,
                      uint64_t, FunctionalConfigurationOptionKind>,
           ResolvedInstanceOption>
      aggregateOptions;
  for (auto option : destination.resolvedInstanceOptions) {
    if (option.instance) {
      auto remapped = remappedInstances.find({option.run, option.instance});
      if (remapped == remappedInstances.end()) {
        setDiagnostic(diagnostic, Status::InvalidReference, 0,
                      SectionKind::ResolvedInstanceOptions, NoRecord,
                      "instance");
        return Status::InvalidReference;
      }
      option.instance = remapped->second;
    }
    option.run = aggregate.uuid;
    auto key = std::make_tuple(option.instance, option.ownerKind, option.owner,
                               option.option);
    auto [position, inserted] = aggregateOptions.try_emplace(key, option);
    if (!inserted) {
      if (option.valueKind == FunctionalConfigurationValueKind::String)
        position->second.stringValue =
            std::max(position->second.stringValue, option.stringValue);
      else
        position->second.value = std::max(position->second.value, option.value);
    }
  }
  destination.resolvedInstanceOptions.clear();
  for (auto &[key, option] : aggregateOptions) {
    (void)key;
    destination.resolvedInstanceOptions.push_back(std::move(option));
  }

  std::map<std::tuple<uint32_t, uint64_t, uint64_t, uint32_t>, Counter> sums;
  for (auto counter : destination.counters) {
    if (counter.metric == MetricKind::Functional) {
      auto instance = remappedInstances.find({counter.run, counter.instance});
      if (instance == remappedInstances.end()) {
        setDiagnostic(diagnostic, Status::InvalidReference, 0,
                      SectionKind::Counters, NoRecord, "instance");
        return Status::InvalidReference;
      }
      counter.instance = instance->second;
    }
    auto key =
        std::make_tuple(static_cast<uint32_t>(counter.metric), counter.entity,
                        counter.instance, counter.subindex);
    auto [position, inserted] = sums.try_emplace(key, counter);
    Counter &value = position->second;
    value.run = aggregate.uuid;
    if (!inserted) {
      value.flags |= counter.flags;
      if (UINT64_MAX - value.value < counter.value) {
        value.value = UINT64_MAX;
        value.flags |= 1;
      } else
        value.value += counter.value;
    }
  }
  destination.runs = {aggregate};
  destination.counters.clear();
  for (auto &entry : sums)
    destination.counters.push_back(entry.second);

  std::map<std::tuple<uint64_t, uint64_t, std::vector<uint64_t>>,
           SparseCrossTuple>
      sparseSums;
  for (const auto &tuple : destination.sparseCrossTuples) {
    auto instance = remappedInstances.find({tuple.run, tuple.instance});
    if (instance == remappedInstances.end())
      return Status::InvalidReference;
    std::vector<uint64_t> components(
        destination.sparseCrossTupleComponents.begin() + tuple.firstComponent,
        destination.sparseCrossTupleComponents.begin() + tuple.firstComponent +
            tuple.componentCount);
    auto key = std::make_tuple(instance->second, tuple.cross, components);
    auto [position, inserted] = sparseSums.try_emplace(key, tuple);
    auto &value = position->second;
    value.run = aggregate.uuid;
    value.instance = instance->second;
    if (!inserted) {
      value.flags |= tuple.flags;
      if (UINT64_MAX - value.hits < tuple.hits) {
        value.hits = UINT64_MAX;
        value.flags |= SparseCrossTupleOverflow;
      } else
        value.hits += tuple.hits;
    }
  }
  destination.sparseCrossTuples.clear();
  destination.sparseCrossTupleComponents.clear();
  for (auto &[key, tuple] : sparseSums) {
    const auto &components = std::get<2>(key);
    tuple.firstComponent = destination.sparseCrossTupleComponents.size();
    destination.sparseCrossTuples.push_back(tuple);
    destination.sparseCrossTupleComponents.insert(
        destination.sparseCrossTupleComponents.end(), components.begin(),
        components.end());
  }

  std::map<std::tuple<uint64_t, uint64_t, uint64_t>, IllegalBinDiagnostic>
      illegalSums;
  for (auto diagnosticValue : destination.illegalBinDiagnostics) {
    auto instance =
        remappedInstances.find({diagnosticValue.run, diagnosticValue.instance});
    if (instance == remappedInstances.end())
      return Status::InvalidReference;
    diagnosticValue.run = aggregate.uuid;
    diagnosticValue.instance = instance->second;
    auto key = std::make_tuple(diagnosticValue.instance, diagnosticValue.bin,
                               diagnosticValue.simulationTime);
    auto [position, inserted] = illegalSums.try_emplace(key, diagnosticValue);
    auto &value = position->second;
    if (!inserted) {
      if (value.message != diagnosticValue.message)
        return Status::SchemaMismatch;
      value.flags |= diagnosticValue.flags;
      if (UINT64_MAX - value.count < diagnosticValue.count) {
        value.count = UINT64_MAX;
        value.flags |= IllegalBinDiagnosticOverflow;
      } else
        value.count += diagnosticValue.count;
    }
  }
  destination.illegalBinDiagnostics.clear();
  for (auto &entry : illegalSums)
    destination.illegalBinDiagnostics.push_back(std::move(entry.second));
  return Status::Ok;
}

static Status mergeInPlace(Database &destination, const Database &source,
                           bool discard, Diagnostic *diagnostic) {
  Status status = validate(destination, diagnostic);
  if (status != Status::Ok)
    return status;
  status = validate(source, diagnostic);
  if (status != Status::Ok)
    return status;
  Digest a = computeSchemaFingerprint(destination),
         b = computeSchemaFingerprint(source);
  if (a != b) {
    setDiagnostic(diagnostic, Status::SchemaMismatch, 0, SectionKind::Strings,
                  NoRecord, "schema_fingerprint");
    return Status::SchemaMismatch;
  }
  std::set<UUID> existingRuns;
  for (const auto &run : destination.runs)
    existingRuns.insert(run.uuid);
  for (const auto &r : source.runs) {
    if (!existingRuns.count(r.uuid))
      continue;
    setDiagnostic(diagnostic, Status::DuplicateRun, 0, SectionKind::Runs,
                  NoRecord, "uuid", formatUUID(r.uuid));
    return Status::DuplicateRun;
  }

  using ConfigurationKey = std::pair<uint64_t, Digest>;
  Database existingConfigurationData = destination;
  std::map<ConfigurationKey, const Database *> configurationSources;
  for (const auto &configuration :
       existingConfigurationData.functionalConfigurations)
    configurationSources.emplace(
        ConfigurationKey{configuration.type, configuration.configuration},
        &existingConfigurationData);
  for (const auto &configuration : source.functionalConfigurations) {
    ConfigurationKey key{configuration.type, configuration.configuration};
    auto [found, inserted] = configurationSources.emplace(key, &source);
    if (!inserted &&
        computeConfigurationBundleDigest(existingConfigurationData, key.first,
                                         key.second,
                                         /*includeResolvedIDs=*/false) !=
            computeConfigurationBundleDigest(source, key.first, key.second,
                                             /*includeResolvedIDs=*/false)) {
      setDiagnostic(diagnostic, Status::SchemaMismatch, 0,
                    SectionKind::FunctionalConfigurations, NoRecord,
                    "configuration", "conflicting resolved bundle");
      return Status::SchemaMismatch;
    }
    if (!inserted &&
        computeConfigurationBundleDigest(source, key.first, key.second,
                                         /*includeResolvedIDs=*/true) <
            computeConfigurationBundleDigest(existingConfigurationData,
                                             key.first, key.second,
                                             /*includeResolvedIDs=*/true))
      found->second = &source;
  }
  destination.functionalConfigurations.clear();
  destination.functionalConfigurationOptions.clear();
  destination.resolvedFunctionalItems.clear();
  destination.resolvedFunctionalBins.clear();
  destination.resolvedFunctionalValueSets.clear();
  destination.resolvedFunctionalValueAtoms.clear();
  destination.resolvedFunctionalValueLimbs.clear();
  destination.resolvedFunctionalBinPlans.clear();
  destination.resolvedFunctionalBinGroups.clear();
  destination.resolvedTransitionAlternatives.clear();
  destination.resolvedTransitionExpansionGroups.clear();
  destination.resolvedTransitionSteps.clear();
  destination.resolvedCrossPlans.clear();
  destination.resolvedCrossAutomaticBinCountLimbs.clear();
  destination.resolvedCrossAutomaticNodes.clear();
  destination.resolvedCrossAutomaticEdges.clear();
  destination.resolvedCrossSelectorBindings.clear();
  destination.resolvedFunctionalTupleSets.clear();
  destination.resolvedFunctionalTupleSetTuples.clear();
  destination.resolvedFunctionalTupleSetComponents.clear();
  auto appendRows = [](auto &to, const auto &from, uint64_t type,
                       const Digest &configuration) {
    for (const auto &row : from)
      if (row.type == type && row.configuration == configuration)
        to.push_back(row);
  };
  for (const auto &[key, provider] : configurationSources) {
    const ConfigurationKey configurationKey = key;
    auto configuration = std::find_if(
        provider->functionalConfigurations.begin(),
        provider->functionalConfigurations.end(), [&](const auto &row) {
          return row.type == configurationKey.first &&
                 row.configuration == configurationKey.second;
        });
    destination.functionalConfigurations.push_back(*configuration);
    appendRows(destination.functionalConfigurationOptions,
               provider->functionalConfigurationOptions, key.first, key.second);
    appendRows(destination.resolvedFunctionalItems,
               provider->resolvedFunctionalItems, key.first, key.second);
    size_t destinationBinBase = destination.resolvedFunctionalBins.size();
    size_t providerBinBase = static_cast<size_t>(
        std::lower_bound(provider->resolvedFunctionalBins.begin(),
                         provider->resolvedFunctionalBins.end(), key,
                         [](const ResolvedFunctionalBin &bin,
                            const ConfigurationKey &value) {
                           return std::make_pair(bin.type, bin.configuration) <
                                  value;
                         }) -
        provider->resolvedFunctionalBins.begin());
    appendRows(destination.resolvedFunctionalBins,
               provider->resolvedFunctionalBins, key.first, key.second);
    for (const auto &group : provider->resolvedFunctionalBinGroups) {
      if (group.type != key.first || group.configuration != key.second)
        continue;
      auto copy = group;
      copy.firstBin = destinationBinBase + (group.firstBin - providerBinBase);
      destination.resolvedFunctionalBinGroups.push_back(copy);
    }
    for (const auto &set : provider->resolvedFunctionalValueSets) {
      if (set.type != key.first || set.configuration != key.second)
        continue;
      auto copy = set;
      copy.firstAtom = destination.resolvedFunctionalValueAtoms.size();
      for (uint32_t atomIndex = 0; atomIndex != set.atomCount; ++atomIndex) {
        const auto &sourceAtom =
            provider->resolvedFunctionalValueAtoms[set.firstAtom + atomIndex];
        auto atom = sourceAtom;
        atom.firstLimb = destination.resolvedFunctionalValueLimbs.size();
        for (uint32_t limbIndex = 0; limbIndex != atom.limbCount; ++limbIndex)
          destination.resolvedFunctionalValueLimbs.push_back(
              provider->resolvedFunctionalValueLimbs[sourceAtom.firstLimb +
                                                     limbIndex]);
        destination.resolvedFunctionalValueAtoms.push_back(atom);
      }
      destination.resolvedFunctionalValueSets.push_back(copy);
    }
    appendRows(destination.resolvedFunctionalBinPlans,
               provider->resolvedFunctionalBinPlans, key.first, key.second);
    size_t destinationAlternativeBase =
        destination.resolvedTransitionAlternatives.size();
    size_t providerAlternativeBase = static_cast<size_t>(
        std::lower_bound(provider->resolvedTransitionAlternatives.begin(),
                         provider->resolvedTransitionAlternatives.end(), key,
                         [](const ResolvedTransitionAlternative &alternative,
                            const ConfigurationKey &value) {
                           return std::make_pair(alternative.type,
                                                 alternative.configuration) <
                                  value;
                         }) -
        provider->resolvedTransitionAlternatives.begin());
    for (const auto &alternative : provider->resolvedTransitionAlternatives) {
      if (alternative.type != key.first ||
          alternative.configuration != key.second)
        continue;
      auto copy = alternative;
      copy.firstStep = destination.resolvedTransitionSteps.size();
      destination.resolvedTransitionSteps.insert(
          destination.resolvedTransitionSteps.end(),
          provider->resolvedTransitionSteps.begin() + alternative.firstStep,
          provider->resolvedTransitionSteps.begin() + alternative.firstStep +
              alternative.stepCount);
      destination.resolvedTransitionAlternatives.push_back(copy);
    }
    for (const auto &group : provider->resolvedTransitionExpansionGroups) {
      if (group.type != key.first || group.configuration != key.second)
        continue;
      auto copy = group;
      copy.firstAlternative =
          destinationAlternativeBase +
          (group.firstAlternative - providerAlternativeBase);
      destination.resolvedTransitionExpansionGroups.push_back(copy);
    }
    for (const auto &plan : provider->resolvedCrossPlans) {
      if (plan.type != key.first || plan.configuration != key.second)
        continue;
      if (destination.resolvedCrossAutomaticBinCountLimbs.size() > UINT32_MAX)
        return Status::IntegerOverflow;
      auto copy = plan;
      copy.firstAutomaticBinCountLimb = static_cast<uint32_t>(
          destination.resolvedCrossAutomaticBinCountLimbs.size());
      destination.resolvedCrossAutomaticBinCountLimbs.insert(
          destination.resolvedCrossAutomaticBinCountLimbs.end(),
          provider->resolvedCrossAutomaticBinCountLimbs.begin() +
              plan.firstAutomaticBinCountLimb,
          provider->resolvedCrossAutomaticBinCountLimbs.begin() +
              plan.firstAutomaticBinCountLimb +
              plan.automaticBinCountLimbCount);
      destination.resolvedCrossPlans.push_back(copy);
    }
    for (const auto &node : provider->resolvedCrossAutomaticNodes) {
      if (node.type != key.first || node.configuration != key.second)
        continue;
      if (destination.resolvedCrossAutomaticEdges.size() > UINT32_MAX)
        return Status::IntegerOverflow;
      auto copy = node;
      copy.firstEdge =
          static_cast<uint32_t>(destination.resolvedCrossAutomaticEdges.size());
      destination.resolvedCrossAutomaticEdges.insert(
          destination.resolvedCrossAutomaticEdges.end(),
          provider->resolvedCrossAutomaticEdges.begin() + node.firstEdge,
          provider->resolvedCrossAutomaticEdges.begin() + node.firstEdge +
              node.edgeCount);
      destination.resolvedCrossAutomaticNodes.push_back(copy);
    }
    appendRows(destination.resolvedCrossSelectorBindings,
               provider->resolvedCrossSelectorBindings, key.first, key.second);
    for (const auto &set : provider->resolvedFunctionalTupleSets) {
      if (set.type != key.first || set.configuration != key.second)
        continue;
      auto copy = set;
      copy.firstTuple = destination.resolvedFunctionalTupleSetTuples.size();
      for (uint32_t tupleIndex = 0; tupleIndex != set.tupleCount;
           ++tupleIndex) {
        auto tuple =
            provider
                ->resolvedFunctionalTupleSetTuples[set.firstTuple + tupleIndex];
        tuple.firstComponent =
            destination.resolvedFunctionalTupleSetComponents.size();
        for (uint32_t componentIndex = 0;
             componentIndex != tuple.componentCount; ++componentIndex)
          destination.resolvedFunctionalTupleSetComponents.push_back(
              provider->resolvedFunctionalTupleSetComponents
                  [provider
                       ->resolvedFunctionalTupleSetTuples[set.firstTuple +
                                                          tupleIndex]
                       .firstComponent +
                   componentIndex]);
        destination.resolvedFunctionalTupleSetTuples.push_back(tuple);
      }
      destination.resolvedFunctionalTupleSets.push_back(copy);
    }
  }
  using ResolvedIDKey = std::tuple<uint64_t, Digest, uint64_t>;
  auto buildBinRemap = [&](const Database &input,
                           std::map<ResolvedIDKey, uint64_t> &result) {
    for (const auto &configuration : input.functionalConfigurations) {
      ConfigurationKey key{configuration.type, configuration.configuration};
      const Database *provider = configurationSources.at(key);
      for (const auto &inputBin : input.resolvedFunctionalBins) {
        if (inputBin.type != key.first || inputBin.configuration != key.second)
          continue;
        auto inputItem = std::find_if(
            input.resolvedFunctionalItems.begin(),
            input.resolvedFunctionalItems.end(), [&](const auto &item) {
              return item.type == key.first &&
                     item.configuration == key.second &&
                     item.id == inputBin.item;
            });
        if (inputItem == input.resolvedFunctionalItems.end())
          return Status::InvalidReference;
        auto providerItem = std::find_if(
            provider->resolvedFunctionalItems.begin(),
            provider->resolvedFunctionalItems.end(), [&](const auto &item) {
              return item.type == key.first &&
                     item.configuration == key.second &&
                     item.templateItem == inputItem->templateItem;
            });
        if (providerItem == provider->resolvedFunctionalItems.end())
          return Status::SchemaMismatch;
        auto providerBin = std::find_if(
            provider->resolvedFunctionalBins.begin(),
            provider->resolvedFunctionalBins.end(), [&](const auto &bin) {
              return bin.type == key.first && bin.configuration == key.second &&
                     bin.templateBin == inputBin.templateBin &&
                     bin.item == providerItem->id &&
                     bin.expansionOrdinal == inputBin.expansionOrdinal &&
                     bin.kind == inputBin.kind;
            });
        if (providerBin == provider->resolvedFunctionalBins.end())
          return Status::SchemaMismatch;
        result.emplace(ResolvedIDKey{key.first, key.second, inputBin.id},
                       providerBin->id);
      }
    }
    return Status::Ok;
  };
  std::map<ResolvedIDKey, uint64_t> existingBinRemap;
  std::map<ResolvedIDKey, uint64_t> sourceBinRemap;
  if ((status = buildBinRemap(existingConfigurationData, existingBinRemap)) !=
          Status::Ok ||
      (status = buildBinRemap(source, sourceBinRemap)) != Status::Ok)
    return status;
  auto findInstance = [](const Database &input, const UUID &run,
                         uint64_t instance) -> const ResolvedInstance * {
    auto found =
        std::lower_bound(input.resolvedInstances.begin(),
                         input.resolvedInstances.end(), std::tie(run, instance),
                         [](const ResolvedInstance &entry, const auto &key) {
                           return std::tie(entry.run, entry.id) < key;
                         });
    return found != input.resolvedInstances.end() && found->run == run &&
                   found->id == instance
               ? &*found
               : nullptr;
  };
  auto remapBin =
      [&](const Database &input, const std::map<ResolvedIDKey, uint64_t> &remap,
          const UUID &run, uint64_t instance, uint64_t bin) -> uint64_t {
    const ResolvedInstance *resolved = findInstance(input, run, instance);
    if (!resolved)
      return 0;
    auto found = remap.find({resolved->type, resolved->configuration, bin});
    return found == remap.end() ? uint64_t{0} : found->second;
  };
  for (auto &counter : destination.counters)
    if (counter.metric == MetricKind::Functional) {
      counter.entity = remapBin(existingConfigurationData, existingBinRemap,
                                counter.run, counter.instance, counter.entity);
      if (!counter.entity)
        return Status::SchemaMismatch;
    }
  for (auto &illegal : destination.illegalBinDiagnostics) {
    illegal.bin = remapBin(existingConfigurationData, existingBinRemap,
                           illegal.run, illegal.instance, illegal.bin);
    if (!illegal.bin)
      return Status::SchemaMismatch;
  }
  for (const auto &tuple : destination.sparseCrossTuples)
    for (uint32_t index = 0; index != tuple.componentCount; ++index) {
      auto &component =
          destination.sparseCrossTupleComponents[tuple.firstComponent + index];
      component = remapBin(existingConfigurationData, existingBinRemap,
                           tuple.run, tuple.instance, component);
      if (!component)
        return Status::SchemaMismatch;
    }
  destination.schemaFingerprint = a;
  destination.runs.insert(destination.runs.end(), source.runs.begin(),
                          source.runs.end());
  destination.resolvedInstances.insert(destination.resolvedInstances.end(),
                                       source.resolvedInstances.begin(),
                                       source.resolvedInstances.end());
  destination.resolvedInstanceOptions.insert(
      destination.resolvedInstanceOptions.end(),
      source.resolvedInstanceOptions.begin(), source.resolvedInstanceOptions.end());
  for (auto counter : source.counters) {
    if (counter.metric == MetricKind::Functional) {
      counter.entity = remapBin(source, sourceBinRemap, counter.run,
                                counter.instance, counter.entity);
      if (!counter.entity)
        return Status::SchemaMismatch;
    }
    destination.counters.push_back(counter);
  }
  for (auto illegal : source.illegalBinDiagnostics) {
    illegal.bin = remapBin(source, sourceBinRemap, illegal.run,
                           illegal.instance, illegal.bin);
    if (!illegal.bin)
      return Status::SchemaMismatch;
    destination.illegalBinDiagnostics.push_back(std::move(illegal));
  }
  using SparseEntry = std::pair<SparseCrossTuple, std::vector<uint64_t>>;
  std::vector<SparseEntry> sparseEntries;
  auto collectSparse = [&](const Database &db, bool remapSource) {
    for (const auto &tuple : db.sparseCrossTuples) {
      std::vector<uint64_t> components;
      components.reserve(tuple.componentCount);
      for (uint32_t index = 0; index != tuple.componentCount; ++index) {
        uint64_t component =
            db.sparseCrossTupleComponents[tuple.firstComponent + index];
        if (remapSource)
          component = remapBin(source, sourceBinRemap, tuple.run,
                               tuple.instance, component);
        if (!component)
          return false;
        components.push_back(component);
      }
      sparseEntries.emplace_back(tuple, std::move(components));
    }
    return true;
  };
  if (!collectSparse(destination, false) || !collectSparse(source, true))
    return Status::SchemaMismatch;
  std::sort(sparseEntries.begin(), sparseEntries.end(),
            [](const auto &a, const auto &b) {
              return std::tie(a.first.run, a.first.instance, a.first.cross,
                              a.second) < std::tie(b.first.run,
                                                   b.first.instance,
                                                   b.first.cross, b.second);
            });
  destination.sparseCrossTuples.clear();
  destination.sparseCrossTupleComponents.clear();
  for (auto &entry : sparseEntries) {
    entry.first.firstComponent = destination.sparseCrossTupleComponents.size();
    destination.sparseCrossTuples.push_back(entry.first);
    destination.sparseCrossTupleComponents.insert(
        destination.sparseCrossTupleComponents.end(), entry.second.begin(),
        entry.second.end());
  }
  if (discard) {
    status = discardTestDetailInPlace(destination, diagnostic);
    if (status != Status::Ok)
      return status;
  }
  std::sort(destination.runs.begin(), destination.runs.end(),
            [](const auto &left, const auto &right) {
              return left.uuid < right.uuid;
            });
  std::sort(destination.resolvedInstances.begin(),
            destination.resolvedInstances.end(),
            [](const auto &left, const auto &right) {
              return std::tie(left.run, left.id) <
                     std::tie(right.run, right.id);
            });
  std::sort(destination.resolvedInstanceOptions.begin(),
            destination.resolvedInstanceOptions.end(),
            [](const auto &left, const auto &right) {
              return std::tie(left.run, left.instance, left.ownerKind,
                              left.owner, left.option) <
                     std::tie(right.run, right.instance, right.ownerKind,
                              right.owner, right.option);
            });
  std::sort(destination.counters.begin(), destination.counters.end(),
            [](const auto &left, const auto &right) {
              return std::tie(left.run, left.metric, left.entity, left.instance,
                              left.subindex) <
                     std::tie(right.run, right.metric, right.entity,
                              right.instance, right.subindex);
            });
  std::sort(destination.illegalBinDiagnostics.begin(),
            destination.illegalBinDiagnostics.end(),
            [](const auto &left, const auto &right) {
              return std::tie(left.run, left.instance, left.bin,
                              left.simulationTime) <
                     std::tie(right.run, right.instance, right.bin,
                              right.simulationTime);
            });
  if (diagnostic)
    *diagnostic = {};
  return Status::Ok;
}

Status merge(Database &destination, const Database &source, bool discard,
             Diagnostic *diagnostic) {
#if OBELISK_COVERAGE_HAS_EXCEPTIONS
  try {
#endif
    Database result = destination;
    Status status = mergeInPlace(result, source, discard, diagnostic);
    if (status != Status::Ok)
      return status;
    status = validate(result, diagnostic);
    if (status != Status::Ok)
      return status;
    destination = std::move(result);
    return Status::Ok;
#if OBELISK_COVERAGE_HAS_EXCEPTIONS
  } catch (const std::bad_alloc &) {
    setDiagnostic(diagnostic, Status::OutOfMemory, 0, SectionKind::Strings,
                  NoRecord, "allocation");
    return Status::OutOfMemory;
  }
#endif
}

Status discardTestDetail(Database &database, Diagnostic *diagnostic) {
#if OBELISK_COVERAGE_HAS_EXCEPTIONS
  try {
#endif
    Database result = database;
    Status status = validate(result, diagnostic);
    if (status == Status::Ok)
      status = discardTestDetailInPlace(result, diagnostic);
    if (status != Status::Ok)
      return status;
    status = validate(result, diagnostic);
    if (status != Status::Ok)
      return status;
    database = std::move(result);
    return Status::Ok;
#if OBELISK_COVERAGE_HAS_EXCEPTIONS
  } catch (const std::bad_alloc &) {
    setDiagnostic(diagnostic, Status::OutOfMemory, 0, SectionKind::Strings,
                  NoRecord, "allocation");
    return Status::OutOfMemory;
  }
#endif
}

Status readFile(const std::string &path, Database &result,
                const ParseLimits &limits, Diagnostic *diagnostic) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    setDiagnostic(diagnostic, Status::IoError, 0, SectionKind::Strings,
                  NoRecord, "open", path);
    return Status::IoError;
  }
  input.seekg(0, std::ios::end);
  auto end = input.tellg();
  if (end < 0 || static_cast<uint64_t>(end) > limits.maxImageBytes) {
    setDiagnostic(diagnostic, Status::LimitExceeded, 0, SectionKind::Strings,
                  NoRecord, "file_size", path);
    return Status::LimitExceeded;
  }
  std::vector<uint8_t> bytes(static_cast<size_t>(end));
  input.seekg(0);
  if (!bytes.empty())
    input.read(reinterpret_cast<char *>(bytes.data()), bytes.size());
  if (!input && !bytes.empty()) {
    setDiagnostic(diagnostic, Status::IoError, 0, SectionKind::Strings,
                  NoRecord, "read", path);
    return Status::IoError;
  }
  return parse(bytes.data(), bytes.size(), result, limits, diagnostic);
}

Status writeFileAtomically(const std::string &path, const Database &db,
                           Diagnostic *diagnostic) {
  static std::atomic<uint64_t> sequence{0};
  std::string temp;
  std::FILE *out = nullptr;
  for (unsigned attempt = 0; attempt != 100 && !out; ++attempt) {
    std::ostringstream name;
    name << path << ".tmp." << std::hex
         << std::chrono::high_resolution_clock::now().time_since_epoch().count()
         << '.' << reinterpret_cast<uintptr_t>(&db) << '.'
         << sequence.fetch_add(1, std::memory_order_relaxed);
    temp = name.str();
    errno = 0;
    out = std::fopen(temp.c_str(), "wbx");
    if (!out && errno != EEXIST)
      break;
  }
  if (!out) {
    setDiagnostic(diagnostic, Status::IoError, 0, SectionKind::Strings,
                  NoRecord, "open", temp);
    return Status::IoError;
  }
  auto write = [](const uint8_t *data, size_t size, void *user) {
    auto *stream = static_cast<std::FILE *>(user);
    return !size || std::fwrite(data, 1, size, stream) == size;
  };
  Status status = serialize(db, write, out, diagnostic);
  bool flushed = std::fflush(out) == 0;
  bool closed = std::fclose(out) == 0;
  closed = flushed && closed;
  if (status != Status::Ok || !closed) {
    std::remove(temp.c_str());
    if (status != Status::Ok)
      return status;
    setDiagnostic(diagnostic, Status::IoError, 0, SectionKind::Strings,
                  NoRecord, "write", temp);
    return Status::IoError;
  }
  std::string renameError;
  if (!detail::atomicReplaceFile(temp, path, renameError)) {
    std::remove(temp.c_str());
    setDiagnostic(diagnostic, Status::IoError, 0, SectionKind::Strings,
                  NoRecord, "rename", std::move(renameError));
    return Status::IoError;
  }
  return Status::Ok;
}

const char *statusName(Status s) {
  switch (s) {
  case Status::Ok:
    return "ok";
  case Status::InvalidArgument:
    return "invalid argument";
  case Status::IoError:
    return "I/O error";
  case Status::OutOfMemory:
    return "out of memory";
  case Status::LimitExceeded:
    return "limit exceeded";
  case Status::BadMagic:
    return "bad magic";
  case Status::VersionMismatch:
    return "codec version mismatch";
  case Status::Truncated:
    return "truncated input";
  case Status::IntegerOverflow:
    return "integer overflow";
  case Status::Misaligned:
    return "misaligned section";
  case Status::SectionOverlap:
    return "overlapping or unordered section";
  case Status::MissingSection:
    return "missing required section";
  case Status::DuplicateSection:
    return "duplicate section";
  case Status::BadRecordSize:
    return "bad record size";
  case Status::BadCount:
    return "bad count";
  case Status::InvalidEnum:
    return "invalid enum";
  case Status::InvalidUtf8:
    return "invalid UTF-8";
  case Status::InvalidReference:
    return "invalid reference";
  case Status::UnsortedOrDuplicate:
    return "unsorted or duplicate key";
  case Status::ChecksumMismatch:
    return "checksum mismatch";
  case Status::FingerprintMismatch:
    return "schema fingerprint mismatch";
  case Status::SchemaMismatch:
    return "schemas do not match";
  case Status::DuplicateRun:
    return "duplicate run UUID";
  case Status::InvalidDatabase:
    return "invalid coverage database";
  }
  return "unknown error";
}
const char *metricName(MetricKind m) {
  switch (m) {
  case MetricKind::Line:
    return "line";
  case MetricKind::Toggle:
    return "toggle";
  case MetricKind::Functional:
    return "functional";
  case MetricKind::FSM:
    return "fsm";
  case MetricKind::Assertion:
    return "assertion";
  case MetricKind::Branch:
    return "branch";
  case MetricKind::MCDC:
    return "mcdc";
  }
  return "unknown";
}
bool parseMetric(const std::string &n, MetricKind &m) {
  for (uint32_t i = 1; i <= 7; ++i) {
    auto k = static_cast<MetricKind>(i);
    if (n == metricName(k)) {
      m = k;
      return true;
    }
  }
  return false;
}
std::string formatUUID(const UUID &uuid) {
  static constexpr char hex[] = "0123456789abcdef";
  std::string out;
  out.reserve(36);
  for (size_t i = 0; i != 16; ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10)
      out.push_back('-');
    out.push_back(hex[uuid[i] >> 4]);
    out.push_back(hex[uuid[i] & 15]);
  }
  return out;
}
bool parseUUID(const std::string &text, UUID &uuid) {
  std::string compact;
  for (char c : text)
    if (c != '-')
      compact.push_back(c);
  if (compact.size() != 32)
    return false;
  auto digit = [](char c) -> int {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
      return c - 'A' + 10;
    return -1;
  };
  for (size_t i = 0; i != 16; ++i) {
    int a = digit(compact[i * 2]), b = digit(compact[i * 2 + 1]);
    if (a < 0 || b < 0)
      return false;
    uuid[i] = uint8_t(a * 16 + b);
  }
  return true;
}

} // namespace obelisk::coverage
