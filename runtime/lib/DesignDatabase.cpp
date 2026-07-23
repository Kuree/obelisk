//===- DesignDatabase.cpp - Checked DWARF-like design reflection ----------===//

#include "RuntimeInternal.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace {

constexpr char kMagic[8] = {'O', 'B', 'D', 'S', 'G', 'N', '1', '\0'};
constexpr uint64_t kHeaderSize = 128;
constexpr uint64_t kScopeSize = 64;
constexpr uint64_t kObjectSize = 96;
constexpr uint64_t kTypeSize = 80;
constexpr uint64_t kIndexSize = 24;

uint32_t read32(const uint8_t *data) {
  uint32_t value = 0;
  for (unsigned byte = 0; byte != 4; ++byte)
    value |= uint32_t{data[byte]} << (byte * 8);
  return value;
}

uint64_t read64(const uint8_t *data) {
  uint64_t value = 0;
  for (unsigned byte = 0; byte != 8; ++byte)
    value |= uint64_t{data[byte]} << (byte * 8);
  return value;
}

int64_t readI64(const uint8_t *data) {
  return static_cast<int64_t>(read64(data));
}

bool validRange(uint64_t offset, uint64_t count, uint64_t stride,
                uint64_t size) {
  if (count != 0 && stride > std::numeric_limits<uint64_t>::max() / count)
    return false;
  uint64_t bytes = count * stride;
  return offset <= size && bytes <= size - offset;
}

bool rangesDisjoint(uint64_t leftOffset, uint64_t leftCount,
                    uint64_t leftStride, uint64_t rightOffset,
                    uint64_t rightCount, uint64_t rightStride) {
  if (leftCount == 0 || rightCount == 0)
    return true;
  uint64_t leftEnd = leftOffset + leftCount * leftStride;
  uint64_t rightEnd = rightOffset + rightCount * rightStride;
  return leftEnd <= rightOffset || rightEnd <= leftOffset;
}

uint64_t nameHash(const uint8_t *name, uint64_t size);

uint64_t checksum(const uint8_t *data, uint64_t size) {
  uint64_t hash = UINT64_C(14695981039346656037);
  for (uint64_t index = 0; index != size; ++index) {
    // The checksum field is treated as zero while hashing.
    uint8_t byte = index >= 32 && index < 40 ? 0 : data[index];
    hash ^= byte;
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

struct Database {
  const uint8_t *data = nullptr;
  uint64_t size = 0;
  uint32_t profile = 0;
  uint64_t root = 0;
  uint64_t scopes = 0;
  uint64_t scopeCount = 0;
  uint64_t objects = 0;
  uint64_t objectCount = 0;
  uint64_t types = 0;
  uint64_t typeCount = 0;
  uint64_t strings = 0;
  uint64_t stringSize = 0;
  uint64_t index = 0;
  uint64_t indexCount = 0;
  uint64_t stateBitCount = 0;
};

bool parseHeader(const obelisk_rt_execution_descriptor_v1 *execution,
                 Database &database) {
  if (!execution ||
      execution->version != OBELISK_RT_VERSION ||
      execution->reserved != 0 ||
      (execution->flags & OBELISK_RT_EXECUTION_HAS_DESIGN_DATABASE) == 0 ||
      !execution->design_database ||
      execution->design_database_size < kHeaderSize)
    return false;
  const uint8_t *data = execution->design_database;
  if (std::memcmp(data, kMagic, sizeof(kMagic)) != 0 ||
      read32(data + 8) != OBELISK_RT_VERSION ||
      read32(data + 12) != 0 ||
      read32(data + 20) != kHeaderSize ||
      read64(data + 24) != execution->design_database_size ||
      read64(data + 32) == 0 ||
      read64(data + 32) != checksum(data, execution->design_database_size))
    return false;
  database = {data,
              execution->design_database_size,
              read32(data + 16),
              read64(data + 40),
              read64(data + 48),
              read64(data + 56),
              read64(data + 64),
              read64(data + 72),
              read64(data + 80),
              read64(data + 88),
              read64(data + 96),
              read64(data + 104),
              read64(data + 112),
              read64(data + 120),
              execution->state_bit_count};
  uint32_t supportedProfile = OBELISK_RT_DESIGN_PROFILE_READ |
                              OBELISK_RT_DESIGN_PROFILE_WRITE;
  if ((database.profile & ~supportedProfile) != 0 ||
      (database.profile & OBELISK_RT_DESIGN_PROFILE_READ) == 0 ||
      ((database.profile & OBELISK_RT_DESIGN_PROFILE_WRITE) != 0 &&
       (database.profile & OBELISK_RT_DESIGN_PROFILE_READ) == 0) ||
      ((database.profile & OBELISK_RT_DESIGN_PROFILE_READ) != 0) !=
          ((execution->flags & OBELISK_RT_EXECUTION_VPI_READ) != 0) ||
      ((database.profile & OBELISK_RT_DESIGN_PROFILE_WRITE) != 0) !=
          ((execution->flags & OBELISK_RT_EXECUTION_VPI_WRITE) != 0) ||
      !validRange(database.scopes, database.scopeCount, kScopeSize,
                  database.size) ||
      !validRange(database.objects, database.objectCount, kObjectSize,
                  database.size) ||
      !validRange(database.types, database.typeCount, kTypeSize,
                  database.size) ||
      !validRange(database.strings, database.stringSize, 1, database.size) ||
      !validRange(database.index, database.indexCount, kIndexSize,
                  database.size) ||
      database.scopes < kHeaderSize || database.objects < kHeaderSize ||
      database.types < kHeaderSize || database.strings < kHeaderSize ||
      database.index < kHeaderSize || database.stringSize == 0 ||
      database.indexCount != database.scopeCount + database.objectCount ||
      !rangesDisjoint(database.scopes, database.scopeCount, kScopeSize,
                      database.objects, database.objectCount, kObjectSize) ||
      !rangesDisjoint(database.scopes, database.scopeCount, kScopeSize,
                      database.types, database.typeCount, kTypeSize) ||
      !rangesDisjoint(database.scopes, database.scopeCount, kScopeSize,
                      database.strings, database.stringSize, 1) ||
      !rangesDisjoint(database.scopes, database.scopeCount, kScopeSize,
                      database.index, database.indexCount, kIndexSize) ||
      !rangesDisjoint(database.objects, database.objectCount, kObjectSize,
                      database.types, database.typeCount, kTypeSize) ||
      !rangesDisjoint(database.objects, database.objectCount, kObjectSize,
                      database.strings, database.stringSize, 1) ||
      !rangesDisjoint(database.objects, database.objectCount, kObjectSize,
                      database.index, database.indexCount, kIndexSize) ||
      !rangesDisjoint(database.types, database.typeCount, kTypeSize,
                      database.strings, database.stringSize, 1) ||
      !rangesDisjoint(database.types, database.typeCount, kTypeSize,
                      database.index, database.indexCount, kIndexSize) ||
      !rangesDisjoint(database.strings, database.stringSize, 1,
                      database.index, database.indexCount, kIndexSize))
    return false;
  return true;
}

bool isScopeOffset(const Database &database, uint64_t offset) {
  return offset >= database.scopes &&
         offset - database.scopes < database.scopeCount * kScopeSize &&
         (offset - database.scopes) % kScopeSize == 0;
}

bool isObjectOffset(const Database &database, uint64_t offset) {
  return offset >= database.objects &&
         offset - database.objects < database.objectCount * kObjectSize &&
         (offset - database.objects) % kObjectSize == 0;
}

bool isTypeOffset(const Database &database, uint64_t offset) {
  return offset >= database.types &&
         offset - database.types < database.typeCount * kTypeSize &&
         (offset - database.types) % kTypeSize == 0;
}

bool getRecord(const Database &database, uint64_t offset,
               const uint8_t *&record, uint32_t &kind) {
  if (!isScopeOffset(database, offset) && !isObjectOffset(database, offset) &&
      !isTypeOffset(database, offset))
    return false;
  record = database.data + offset;
  kind = read32(record);
  if (isScopeOffset(database, offset))
    return kind == OBELISK_RT_DESIGN_RECORD_SCOPE;
  if (isTypeOffset(database, offset))
    return kind == OBELISK_RT_DESIGN_RECORD_TYPE;
  return (kind >= OBELISK_RT_DESIGN_RECORD_STORAGE &&
          kind <= OBELISK_RT_DESIGN_RECORD_PROCESS) ||
         kind == OBELISK_RT_DESIGN_RECORD_FUNCTION;
}

bool getString(const Database &database, uint64_t offset,
               std::string_view &result) {
  if (offset < database.strings ||
      offset - database.strings >= database.stringSize)
    return false;
  const char *begin = reinterpret_cast<const char *>(database.data + offset);
  uint64_t remaining = database.stringSize - (offset - database.strings);
  const void *end = std::memchr(begin, 0, static_cast<size_t>(remaining));
  if (!end)
    return false;
  result = std::string_view(begin, static_cast<const char *>(end) - begin);
  return true;
}

bool validSource(const Database &database, uint64_t fileOffset,
                 uint64_t lineColumn) {
  if (fileOffset == 0)
    return lineColumn == 0;
  std::string_view file;
  return getString(database, fileOffset, file) && !file.empty() &&
         static_cast<uint32_t>(lineColumn >> 32) != 0 &&
         static_cast<uint32_t>(lineColumn) != 0;
}

bool rangeExtent(const uint8_t *record, uint64_t &extent) {
  int64_t left = readI64(record + 16);
  int64_t right = readI64(record + 24);
  uint64_t distance = left >= right
                          ? static_cast<uint64_t>(left) -
                                static_cast<uint64_t>(right)
                          : static_cast<uint64_t>(right) -
                                static_cast<uint64_t>(left);
  if (distance == UINT64_MAX)
    return false;
  extent = distance + 1;
  return true;
}

uint64_t nextOffset(const uint8_t *record, uint32_t kind) {
  return read64(record + (kind == OBELISK_RT_DESIGN_RECORD_SCOPE ? 32 : 24));
}

bool validateDatabaseImpl(const Database &database) {
  if (database.scopeCount == 0 || !isScopeOffset(database, database.root))
    return false;
  std::array<std::unordered_set<uint64_t>,
             OBELISK_RT_DESIGN_RECORD_FUNCTION + 1>
      stableIDs;
  std::unordered_set<uint64_t> reached;
  std::vector<uint64_t> pending{database.root};
  while (!pending.empty()) {
    uint64_t offset = pending.back();
    pending.pop_back();
    if (!reached.insert(offset).second)
      return false;
    const uint8_t *record;
    uint32_t kind;
    if (!getRecord(database, offset, record, kind) ||
        kind == OBELISK_RT_DESIGN_RECORD_TYPE)
      return false;
    uint32_t caps = read32(record + 4);
    uint32_t supportedCaps = OBELISK_RT_DESIGN_CAP_READ |
                             OBELISK_RT_DESIGN_CAP_WRITE |
                             OBELISK_RT_DESIGN_CAP_ITERATE;
    if ((caps & ~supportedCaps) != 0 ||
        ((caps & OBELISK_RT_DESIGN_CAP_WRITE) != 0 &&
         (database.profile & OBELISK_RT_DESIGN_PROFILE_WRITE) == 0))
      return false;
    uint64_t stableID = read64(record + 8);
    if (!stableIDs[kind].insert(stableID).second)
      return false;
    std::string_view name;
    uint64_t nameOffset = read64(record + 40);
    if (!getString(database, nameOffset, name) || name.empty())
      return false;
    if (kind == OBELISK_RT_DESIGN_RECORD_SCOPE) {
      if (caps != OBELISK_RT_DESIGN_CAP_ITERATE ||
          !validSource(database, read64(record + 48), read64(record + 56)))
        return false;
      uint64_t parent = read64(record + 16);
      if (offset == database.root ? parent != 0 : !isScopeOffset(database, parent))
        return false;
      uint64_t child = read64(record + 24);
      uint64_t childCount = 0;
      while (child != 0) {
        if (++childCount > database.scopeCount + database.objectCount)
          return false;
        const uint8_t *childRecord;
        uint32_t childKind;
        if (!getRecord(database, child, childRecord, childKind) ||
            childKind == OBELISK_RT_DESIGN_RECORD_TYPE ||
            read64(childRecord + 16) != offset)
          return false;
        pending.push_back(child);
        child = nextOffset(childRecord, childKind);
      }
    } else {
      uint64_t typeOffset = read64(record + 48);
      if (!isScopeOffset(database, read64(record + 16)) ||
          !validSource(database, read64(record + 32), read64(record + 88)))
        return false;
      if (kind == OBELISK_RT_DESIGN_RECORD_PROCESS ||
          kind == OBELISK_RT_DESIGN_RECORD_FUNCTION) {
        if (caps != 0 || typeOffset != 0 || read64(record + 56) != 0 ||
            read64(record + 64) != 0 || read64(record + 72) != 0 ||
            read64(record + 80) != 0)
          return false;
      } else if ((caps & OBELISK_RT_DESIGN_CAP_READ) == 0 ||
                 (caps & OBELISK_RT_DESIGN_CAP_ITERATE) != 0 ||
                 !isTypeOffset(database, typeOffset) ||
                 read64(record + 56) == 0 ||
                 read64(database.data + typeOffset + 8) !=
                     read64(record + 56)) {
        return false;
      }
      uint64_t stateOffset = read64(record + 80);
      uint64_t width = read64(record + 56);
      if (stateOffset > database.stateBitCount ||
          width > database.stateBitCount - stateOffset)
        return false;
    }
  }
  if (reached.size() != database.scopeCount + database.objectCount)
    return false;

  for (uint64_t index = 0; index != database.typeCount; ++index) {
    const uint8_t *record = database.data + database.types + index * kTypeSize;
    uint32_t encoded = read32(record + 4);
    uint32_t typeKind = encoded & UINT32_C(0xff);
    uint32_t flags = encoded >> 8;
    uint64_t width = read64(record + 8);
    uint64_t element = read64(record + 32);
    uint64_t firstChild = read64(record + 40);
    uint64_t childCount = read64(record + 48);
    uint64_t extent = 0;
    if (read32(record) != OBELISK_RT_DESIGN_RECORD_TYPE ||
        typeKind < OBELISK_RT_DESIGN_TYPE_SCALAR ||
        typeKind > OBELISK_RT_DESIGN_TYPE_FIELD ||
        (flags & ~(OBELISK_RT_DESIGN_TYPE_FOUR_STATE |
                   OBELISK_RT_DESIGN_TYPE_SIGNED |
                   OBELISK_RT_DESIGN_TYPE_PACKED |
                   OBELISK_RT_DESIGN_TYPE_TAGGED)) != 0 ||
        width == 0)
      return false;
    bool hasElement = element != 0;
    bool hasChildren = firstChild != 0 || childCount != 0;
    if ((hasElement && !isTypeOffset(database, element)) ||
        (hasChildren &&
         (!isTypeOffset(database, firstChild) || childCount == 0 ||
          childCount > database.typeCount ||
          (childCount - 1) >
              (std::numeric_limits<uint64_t>::max() - firstChild) /
                  kTypeSize ||
          !isTypeOffset(database,
                        firstChild + (childCount - 1) * kTypeSize))))
      return false;
    switch (typeKind) {
    case OBELISK_RT_DESIGN_TYPE_SCALAR:
      if (hasElement || hasChildren || read64(record + 56) != 0 ||
          read64(record + 64) != 0 ||
          (flags & OBELISK_RT_DESIGN_TYPE_TAGGED) != 0 ||
          !rangeExtent(record, extent) || extent != width)
        return false;
      break;
    case OBELISK_RT_DESIGN_TYPE_ARRAY:
      if (!hasElement || hasChildren || read64(record + 56) != 0 ||
          read64(record + 64) != 0 ||
          (flags & (OBELISK_RT_DESIGN_TYPE_SIGNED |
                    OBELISK_RT_DESIGN_TYPE_TAGGED)) != 0 ||
          !rangeExtent(record, extent))
        return false;
      {
        const uint8_t *elementRecord = database.data + element;
        uint64_t elementWidth = read64(elementRecord + 8);
        uint32_t elementKind = read32(elementRecord + 4) & UINT32_C(0xff);
        uint32_t elementFlags = read32(elementRecord + 4) >> 8;
        if (elementKind == OBELISK_RT_DESIGN_TYPE_FIELD ||
            elementWidth == 0 || extent > UINT64_MAX / elementWidth ||
            extent * elementWidth != width ||
            ((flags & OBELISK_RT_DESIGN_TYPE_FOUR_STATE) != 0) !=
                ((elementFlags & OBELISK_RT_DESIGN_TYPE_FOUR_STATE) != 0))
          return false;
      }
      break;
    case OBELISK_RT_DESIGN_TYPE_STRUCT:
      if (hasElement || !hasChildren || read64(record + 56) != 0 ||
          read64(record + 64) != 0 ||
          (flags & (OBELISK_RT_DESIGN_TYPE_SIGNED |
                    OBELISK_RT_DESIGN_TYPE_TAGGED)) != 0 ||
          !rangeExtent(record, extent) || extent != width)
        return false;
      break;
    case OBELISK_RT_DESIGN_TYPE_UNION:
      if (hasElement || !hasChildren || read64(record + 64) != 0 ||
          (flags & OBELISK_RT_DESIGN_TYPE_SIGNED) != 0 ||
          (((flags & OBELISK_RT_DESIGN_TYPE_TAGGED) != 0) !=
           (read64(record + 56) != 0)) || !rangeExtent(record, extent) ||
          extent != width)
        return false;
      break;
    case OBELISK_RT_DESIGN_TYPE_FIELD:
      if (!hasElement || hasChildren ||
          (flags & (OBELISK_RT_DESIGN_TYPE_SIGNED |
                    OBELISK_RT_DESIGN_TYPE_PACKED |
                    OBELISK_RT_DESIGN_TYPE_TAGGED)) != 0 ||
          !rangeExtent(record, extent) || extent != width)
        return false;
      {
        const uint8_t *elementRecord = database.data + element;
        uint32_t elementKind = read32(elementRecord + 4) & UINT32_C(0xff);
        uint32_t elementFlags = read32(elementRecord + 4) >> 8;
        if (elementKind == OBELISK_RT_DESIGN_TYPE_FIELD ||
            read64(elementRecord + 8) != width ||
            ((flags & OBELISK_RT_DESIGN_TYPE_FOUR_STATE) != 0) !=
                ((elementFlags & OBELISK_RT_DESIGN_TYPE_FOUR_STATE) != 0))
          return false;
      }
      break;
    }
    std::string_view name;
    if (!getString(database, read64(record + 72), name) || name.empty())
      return false;
  }

  for (uint64_t index = 0; index != database.typeCount; ++index) {
    const uint8_t *record = database.data + database.types + index * kTypeSize;
    uint32_t encoded = read32(record + 4);
    uint32_t kind = encoded & UINT32_C(0xff);
    if (kind != OBELISK_RT_DESIGN_TYPE_STRUCT &&
        kind != OBELISK_RT_DESIGN_TYPE_UNION)
      continue;
    uint32_t flags = encoded >> 8;
    uint64_t width = read64(record + 8);
    uint64_t firstChild = read64(record + 40);
    uint64_t childCount = read64(record + 48);
    uint64_t sum = 0, maximum = 0;
    bool fourState = false;
    std::vector<std::pair<uint64_t, uint64_t>> packedRanges;
    packedRanges.reserve(static_cast<size_t>(childCount));
    for (uint64_t child = 0; child != childCount; ++child) {
      const uint8_t *field =
          database.data + firstChild + child * kTypeSize;
      uint64_t fieldWidth = read64(field + 8);
      uint64_t packedOffset = read64(field + 64);
      fourState |= ((read32(field + 4) >> 8) &
                    OBELISK_RT_DESIGN_TYPE_FOUR_STATE) != 0;
      if (fieldWidth > UINT64_MAX - sum)
        return false;
      sum += fieldWidth;
      maximum = std::max(maximum, fieldWidth);
      if ((flags & OBELISK_RT_DESIGN_TYPE_PACKED) != 0) {
        uint64_t tagBits = read64(record + 56);
        if (kind == OBELISK_RT_DESIGN_TYPE_UNION && tagBits > width)
          return false;
        uint64_t payloadWidth =
            kind == OBELISK_RT_DESIGN_TYPE_UNION
                ? width - tagBits
                : width;
        if (packedOffset > payloadWidth ||
            fieldWidth > payloadWidth - packedOffset)
          return false;
        packedRanges.emplace_back(packedOffset, packedOffset + fieldWidth);
      }
    }
    if (((flags & OBELISK_RT_DESIGN_TYPE_FOUR_STATE) != 0) != fourState)
      return false;
    if (kind == OBELISK_RT_DESIGN_TYPE_STRUCT) {
      if (sum != width)
        return false;
      if ((flags & OBELISK_RT_DESIGN_TYPE_PACKED) != 0) {
        std::sort(packedRanges.begin(), packedRanges.end());
        uint64_t cursor = 0;
        for (auto [begin, end] : packedRanges) {
          if (begin != cursor)
            return false;
          cursor = end;
        }
        if (cursor != width)
          return false;
      }
    } else {
      uint64_t tagBits = read64(record + 56);
      if (maximum > UINT64_MAX - tagBits || maximum + tagBits != width)
        return false;
    }
  }

  std::vector<uint8_t> typeState(static_cast<size_t>(database.typeCount));
  auto visitType = [&](uint64_t root) {
    struct WorkItem {
      uint64_t offset;
      bool finish;
    };
    std::vector<WorkItem> worklist{{root, false}};
    while (!worklist.empty()) {
      WorkItem item = worklist.back();
      worklist.pop_back();
      if (!isTypeOffset(database, item.offset))
        return false;
      size_t index =
          static_cast<size_t>((item.offset - database.types) / kTypeSize);
      if (item.finish) {
        typeState[index] = 2;
        continue;
      }
      if (typeState[index] == 1)
        return false;
      if (typeState[index] == 2)
        continue;
      typeState[index] = 1;
      worklist.push_back({item.offset, true});
      const uint8_t *record = database.data + item.offset;
      uint64_t firstChild = read64(record + 40);
      uint64_t childCount = read64(record + 48);
      for (uint64_t child = childCount; child != 0; --child) {
        uint64_t ordinal = child - 1;
        uint64_t childOffset = firstChild + ordinal * kTypeSize;
        const uint8_t *childRecord = database.data + childOffset;
        if ((read32(childRecord + 4) & UINT32_C(0xff)) !=
                OBELISK_RT_DESIGN_TYPE_FIELD ||
            read64(childRecord + 56) != ordinal)
          return false;
        worklist.push_back({childOffset, false});
      }
      uint64_t element = read64(record + 32);
      if (element != 0)
        worklist.push_back({element, false});
    }
    return true;
  };
  for (uint64_t index = 0; index != database.objectCount; ++index) {
    const uint8_t *record =
        database.data + database.objects + index * kObjectSize;
    uint32_t kind = read32(record);
    if (kind != OBELISK_RT_DESIGN_RECORD_PROCESS &&
        kind != OBELISK_RT_DESIGN_RECORD_FUNCTION &&
        !visitType(read64(record + 48)))
      return false;
  }
  if (std::any_of(typeState.begin(), typeState.end(),
                  [](uint8_t state) { return state != 2; }))
    return false;

  uint64_t previousHash = 0;
  std::string_view previousName;
  std::unordered_set<uint64_t> indexedRecords;
  for (uint64_t index = 0; index != database.indexCount; ++index) {
    const uint8_t *entry = database.data + database.index + index * kIndexSize;
    uint64_t hash = read64(entry);
    std::string_view name;
    if (!getString(database, read64(entry + 8), name))
      return false;
    const uint8_t *record;
    uint32_t kind;
    uint64_t recordOffset = read64(entry + 16);
    if (!getRecord(database, recordOffset, record, kind) ||
        kind == OBELISK_RT_DESIGN_RECORD_TYPE ||
        hash != nameHash(reinterpret_cast<const uint8_t *>(name.data()),
                         name.size()) ||
        !indexedRecords.insert(recordOffset).second ||
        read64(record + 40) != read64(entry + 8) ||
        (index != 0 &&
         (hash < previousHash ||
          (hash == previousHash && name <= previousName))))
      return false;
    previousHash = hash;
    previousName = name;
  }
  return true;
}

// Reflection entry points have a C ABI. Keep allocator failures and malformed
// graph corner cases from unwinding through callers that cannot catch C++
// exceptions; design_validate retains its explicit outer guard as well.
bool validateDatabase(const Database &database) noexcept {
  try {
    return validateDatabaseImpl(database);
  } catch (...) {
    return false;
  }
}

uint64_t nameHash(const uint8_t *name, uint64_t size) {
  uint64_t hash = UINT64_C(14695981039346656037);
  for (uint64_t index = 0; index != size; ++index) {
    hash ^= name[index];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

uint32_t descriptorKind(uint32_t recordKind) {
  switch (recordKind) {
  case OBELISK_RT_DESIGN_RECORD_SCOPE:
    return OBELISK_RT_DESCRIPTOR_SCOPE;
  case OBELISK_RT_DESIGN_RECORD_STORAGE:
    return OBELISK_RT_DESCRIPTOR_STORAGE;
  case OBELISK_RT_DESIGN_RECORD_NET:
    return OBELISK_RT_DESCRIPTOR_NET;
  case OBELISK_RT_DESIGN_RECORD_DRIVER:
    return OBELISK_RT_DESCRIPTOR_DRIVER;
  case OBELISK_RT_DESIGN_RECORD_PROCESS:
    return OBELISK_RT_DESCRIPTOR_PROCESS;
  case OBELISK_RT_DESIGN_RECORD_FUNCTION:
    return OBELISK_RT_DESCRIPTOR_FUNCTION;
  default:
    return OBELISK_RT_DESCRIPTOR_INVALID;
  }
}

uint32_t transitionEdges(bool oldValue, bool oldUnknown, bool newValue,
                         bool newUnknown) {
  if (oldValue == newValue && oldUnknown == newUnknown)
    return 0;
  uint32_t result = OBELISK_RT_SIGNAL_CHANGE;
  bool oldZero = !oldUnknown && !oldValue;
  bool oldOne = !oldUnknown && oldValue;
  bool newZero = !newUnknown && !newValue;
  bool newOne = !newUnknown && newValue;
  if ((oldZero && !newZero) || (oldUnknown && newOne))
    result |= OBELISK_RT_SIGNAL_POSEDGE;
  if ((oldOne && !newOne) || (oldUnknown && newZero))
    result |= OBELISK_RT_SIGNAL_NEGEDGE;
  return result;
}

} // namespace

bool obelisk_rt_checked_design_record(
    const obelisk_rt_execution_descriptor_v1 *execution, uint64_t offset,
    const uint8_t *&record, uint32_t &kind) noexcept {
  try {
    Database database;
    return parseHeader(execution, database) && validateDatabase(database) &&
           getRecord(database, offset, record, kind);
  } catch (...) {
    return false;
  }
}

extern "C" obelisk_rt_status obelisk_rt_v1_design_validate(
    const obelisk_rt_execution_descriptor_v1 *execution) {
  try {
    Database database;
    return parseHeader(execution, database) && validateDatabase(database)
               ? OBELISK_RT_OK
               : OBELISK_RT_INVALID_DESIGN;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_DESIGN;
  }
}

extern "C" obelisk_rt_status obelisk_rt_v1_design_root(
    const obelisk_rt_execution_descriptor_v1 *execution,
    obelisk_rt_design_cursor_v1 *outCursor) {
  if (!outCursor)
    return OBELISK_RT_INVALID_ARGUMENT;
  Database database;
  if (!parseHeader(execution, database) || !validateDatabase(database))
    return OBELISK_RT_INVALID_DESIGN;
  outCursor->offset = database.root;
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status obelisk_rt_v1_design_child(
    const obelisk_rt_execution_descriptor_v1 *execution,
    obelisk_rt_design_cursor_v1 cursor,
    obelisk_rt_design_cursor_v1 *outCursor) {
  if (!outCursor)
    return OBELISK_RT_INVALID_ARGUMENT;
  Database database;
  const uint8_t *record;
  uint32_t kind;
  if (!parseHeader(execution, database) || !validateDatabase(database) ||
      !getRecord(database, cursor.offset, record, kind) ||
      kind != OBELISK_RT_DESIGN_RECORD_SCOPE)
    return OBELISK_RT_INVALID_HANDLE;
  outCursor->offset = read64(record + 24);
  return outCursor->offset == 0 ? OBELISK_RT_EOF : OBELISK_RT_OK;
}

extern "C" obelisk_rt_status obelisk_rt_v1_design_child_at(
    const obelisk_rt_execution_descriptor_v1 *execution,
    obelisk_rt_design_cursor_v1 cursor, uint64_t index,
    obelisk_rt_design_cursor_v1 *outCursor) {
  if (!outCursor)
    return OBELISK_RT_INVALID_ARGUMENT;
  Database database;
  const uint8_t *record;
  uint32_t kind;
  if (!parseHeader(execution, database) || !validateDatabase(database) ||
      !getRecord(database, cursor.offset, record, kind) ||
      kind != OBELISK_RT_DESIGN_RECORD_SCOPE)
    return OBELISK_RT_INVALID_HANDLE;
  uint64_t child = read64(record + 24);
  for (uint64_t ordinal = 0; ordinal != index && child != 0; ++ordinal) {
    const uint8_t *childRecord;
    uint32_t childKind;
    if (!getRecord(database, child, childRecord, childKind) ||
        childKind == OBELISK_RT_DESIGN_RECORD_TYPE)
      return OBELISK_RT_INVALID_DESIGN;
    child = nextOffset(childRecord, childKind);
  }
  outCursor->offset = child;
  return child == 0 ? OBELISK_RT_EOF : OBELISK_RT_OK;
}

extern "C" obelisk_rt_status obelisk_rt_v1_design_sibling(
    const obelisk_rt_execution_descriptor_v1 *execution,
    obelisk_rt_design_cursor_v1 cursor,
    obelisk_rt_design_cursor_v1 *outCursor) {
  if (!outCursor)
    return OBELISK_RT_INVALID_ARGUMENT;
  Database database;
  const uint8_t *record;
  uint32_t kind;
  if (!parseHeader(execution, database) || !validateDatabase(database) ||
      !getRecord(database, cursor.offset, record, kind) ||
      kind == OBELISK_RT_DESIGN_RECORD_TYPE)
    return OBELISK_RT_INVALID_HANDLE;
  outCursor->offset = nextOffset(record, kind);
  return outCursor->offset == 0 ? OBELISK_RT_EOF : OBELISK_RT_OK;
}

extern "C" obelisk_rt_status obelisk_rt_v1_design_lookup(
    const obelisk_rt_execution_descriptor_v1 *execution, const uint8_t *name,
    uint64_t nameSize, obelisk_rt_design_cursor_v1 *outCursor) {
  if (!outCursor || (nameSize != 0 && !name))
    return OBELISK_RT_INVALID_ARGUMENT;
  Database database;
  if (!parseHeader(execution, database) || !validateDatabase(database))
    return OBELISK_RT_INVALID_DESIGN;
  uint64_t wantedHash = nameHash(name, nameSize);
  uint64_t low = 0, high = database.indexCount;
  while (low != high) {
    uint64_t middle = low + (high - low) / 2;
    uint64_t hash = read64(database.data + database.index + middle * kIndexSize);
    if (hash < wantedHash)
      low = middle + 1;
    else
      high = middle;
  }
  for (uint64_t index = low; index != database.indexCount; ++index) {
    const uint8_t *entry = database.data + database.index + index * kIndexSize;
    if (read64(entry) != wantedHash)
      break;
    std::string_view candidate;
    if (!getString(database, read64(entry + 8), candidate))
      return OBELISK_RT_INVALID_DESIGN;
    if (candidate.size() == nameSize &&
        (nameSize == 0 || std::memcmp(candidate.data(), name, nameSize) == 0)) {
      outCursor->offset = read64(entry + 16);
      return OBELISK_RT_OK;
    }
  }
  outCursor->offset = 0;
  return OBELISK_RT_INVALID_HANDLE;
}

extern "C" obelisk_rt_status obelisk_rt_v1_design_info(
    const obelisk_rt_execution_descriptor_v1 *execution,
    obelisk_rt_design_cursor_v1 cursor,
    obelisk_rt_design_info_v1 *outInfo) {
  if (!outInfo)
    return OBELISK_RT_INVALID_ARGUMENT;
  Database database;
  const uint8_t *record;
  uint32_t kind;
  if (!parseHeader(execution, database) || !validateDatabase(database) ||
      !getRecord(database, cursor.offset, record, kind) ||
      kind == OBELISK_RT_DESIGN_RECORD_TYPE)
    return OBELISK_RT_INVALID_HANDLE;
  *outInfo = {};
  outInfo->kind = kind;
  outInfo->capabilities = read32(record + 4);
  outInfo->handle = {descriptorKind(kind), 0, read64(record + 8)};
  if (kind != OBELISK_RT_DESIGN_RECORD_SCOPE) {
    outInfo->type_offset = read64(record + 48);
    outInfo->bit_width = read64(record + 56);
    outInfo->range_left = readI64(record + 64);
    outInfo->range_right = readI64(record + 72);
  }
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status obelisk_rt_v1_design_type_info(
    const obelisk_rt_execution_descriptor_v1 *execution,
    obelisk_rt_design_cursor_v1 cursor,
    obelisk_rt_design_type_info_v1 *outInfo) {
  if (!outInfo)
    return OBELISK_RT_INVALID_ARGUMENT;
  Database database;
  const uint8_t *record;
  uint32_t recordKind;
  if (!parseHeader(execution, database) || !validateDatabase(database) ||
      !getRecord(database, cursor.offset, record, recordKind) ||
      recordKind != OBELISK_RT_DESIGN_RECORD_TYPE)
    return OBELISK_RT_INVALID_HANDLE;
  uint32_t encoded = read32(record + 4);
  *outInfo = {};
  outInfo->kind = encoded & UINT32_C(0xff);
  outInfo->flags = encoded >> 8;
  outInfo->bit_width = read64(record + 8);
  outInfo->range_left = readI64(record + 16);
  outInfo->range_right = readI64(record + 24);
  outInfo->element_type.offset = read64(record + 32);
  outInfo->first_child.offset = read64(record + 40);
  outInfo->child_count = read64(record + 48);
  if (outInfo->kind == OBELISK_RT_DESIGN_TYPE_FIELD)
    outInfo->ordinal = read64(record + 56);
  if (outInfo->kind == OBELISK_RT_DESIGN_TYPE_UNION)
    outInfo->tag_bits = read64(record + 56);
  outInfo->packed_offset = read64(record + 64);
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status obelisk_rt_v1_design_type_child(
    const obelisk_rt_execution_descriptor_v1 *execution,
    obelisk_rt_design_cursor_v1 cursor, uint64_t index,
    obelisk_rt_design_cursor_v1 *outCursor) {
  if (!outCursor)
    return OBELISK_RT_INVALID_ARGUMENT;
  Database database;
  const uint8_t *record;
  uint32_t recordKind;
  if (!parseHeader(execution, database) || !validateDatabase(database) ||
      !getRecord(database, cursor.offset, record, recordKind) ||
      recordKind != OBELISK_RT_DESIGN_RECORD_TYPE)
    return OBELISK_RT_INVALID_HANDLE;
  uint64_t count = read64(record + 48);
  if (index >= count) {
    outCursor->offset = 0;
    return OBELISK_RT_EOF;
  }
  uint64_t child = read64(record + 40) + index * kTypeSize;
  if (!isTypeOffset(database, child) ||
      (read32(database.data + child + 4) & UINT32_C(0xff)) !=
          OBELISK_RT_DESIGN_TYPE_FIELD)
    return OBELISK_RT_INVALID_DESIGN;
  outCursor->offset = child;
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status obelisk_rt_v1_design_name(
    const obelisk_rt_execution_descriptor_v1 *execution,
    obelisk_rt_design_cursor_v1 cursor, const uint8_t **outData,
    uint64_t *outSize) {
  if (!outData || !outSize)
    return OBELISK_RT_INVALID_ARGUMENT;
  Database database;
  const uint8_t *record;
  uint32_t kind;
  std::string_view name;
  if (!parseHeader(execution, database) || !validateDatabase(database) ||
      !getRecord(database, cursor.offset, record, kind) ||
      !getString(database,
                 read64(record +
                        (kind == OBELISK_RT_DESIGN_RECORD_TYPE ? 72 : 40)),
                 name))
    return OBELISK_RT_INVALID_HANDLE;
  *outData = reinterpret_cast<const uint8_t *>(name.data());
  *outSize = name.size();
  return OBELISK_RT_OK;
}

static obelisk_rt_status accessState(obelisk_rt_context *context,
                                     obelisk_rt_design_cursor_v1 cursor,
                                     uint64_t *value, uint64_t *unknown,
                                     uint64_t bitWidth, bool write) {
  if (!context || !value || bitWidth == 0 || !context->execution)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  Database database;
  const uint8_t *record;
  uint32_t kind;
  if (!parseHeader(context->execution, database) ||
      !validateDatabase(database) ||
      !getRecord(database, cursor.offset, record, kind) ||
      kind < OBELISK_RT_DESIGN_RECORD_STORAGE ||
      kind > OBELISK_RT_DESIGN_RECORD_DRIVER)
    return OBELISK_RT_INVALID_HANDLE;
  uint32_t capabilities = read32(record + 4);
  if ((capabilities & (write ? OBELISK_RT_DESIGN_CAP_WRITE
                             : OBELISK_RT_DESIGN_CAP_READ)) == 0)
    return OBELISK_RT_PERMISSION_DENIED;
  uint64_t stateOffset = read64(record + 80);
  uint64_t width = read64(record + 56);
  if (bitWidth != width || stateOffset > context->execution->state_bit_count ||
      width > context->execution->state_bit_count - stateOffset)
    return OBELISK_RT_INVALID_HANDLE;
  uint64_t limbs = (width + 63) / 64;
  const uint8_t *typeRecord = database.data + read64(record + 48);
  bool fourState =
      ((read32(typeRecord + 4) >> 8) &
       OBELISK_RT_DESIGN_TYPE_FOUR_STATE) != 0;
  if (write && kind == OBELISK_RT_DESIGN_RECORD_NET) {
    bool connected = false;
    obelisk_rt_status status = obelisk_rt_design_net_is_connected(
        context, stateOffset, stateOffset + width, &connected);
    if (status != OBELISK_RT_OK)
      return status;
    // A direct deposit or force cannot update one logical alias in isolation.
    // Reject it until the public write API can express topology-wide writes.
    if (connected)
      return OBELISK_RT_PERMISSION_DENIED;
  }
  std::vector<std::pair<uint64_t, uint32_t>> transitions;
  if (write) {
    try {
      transitions.reserve(static_cast<size_t>(width));
    } catch (const std::bad_alloc &) {
      return OBELISK_RT_OUT_OF_MEMORY;
    } catch (...) {
      return OBELISK_RT_INVALID_DESIGN;
    }
  }
  {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    for (uint64_t bit = 0; bit != width; ++bit) {
      uint64_t sourceLimb = bit / 64;
      uint64_t sourceMask = uint64_t{1} << (bit % 64);
      uint64_t absolute = stateOffset + bit;
      uint64_t stateMask = uint64_t{1} << (absolute % 64);
      uint64_t &stateValue = context->stateValue[absolute / 64];
      uint64_t &stateUnknown = context->stateUnknown[absolute / 64];
      if (write) {
        bool oldValue = (stateValue & stateMask) != 0;
        bool oldUnknown = (stateUnknown & stateMask) != 0;
        bool newValue = (value[sourceLimb] & sourceMask) != 0;
        bool newUnknown = fourState && unknown &&
                          (unknown[sourceLimb] & sourceMask) != 0;
        stateValue = newValue ? stateValue | stateMask
                              : stateValue & ~stateMask;
        stateUnknown = newUnknown ? stateUnknown | stateMask
                                  : stateUnknown & ~stateMask;
        uint32_t edges =
            transitionEdges(oldValue, oldUnknown, newValue, newUnknown);
        if (edges != 0)
          transitions.push_back({absolute, edges});
      } else {
        value[sourceLimb] = (value[sourceLimb] & ~sourceMask) |
                            ((stateValue & stateMask) ? sourceMask : 0);
        if (unknown && fourState)
          unknown[sourceLimb] =
              (unknown[sourceLimb] & ~sourceMask) |
              ((stateUnknown & stateMask) ? sourceMask : 0);
        else if (unknown)
          unknown[sourceLimb] &= ~sourceMask;
      }
    }
  }
  if (!write && width % 64 != 0) {
    uint64_t mask = (uint64_t{1} << (width % 64)) - 1;
    value[limbs - 1] &= mask;
    if (unknown)
      unknown[limbs - 1] &= mask;
  }
  if (write) {
    for (auto [absolute, edges] : transitions)
      obelisk_rt_v1_scheduler_signal(context, absolute, 1, edges);
    if (!transitions.empty()) {
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      if (!obelisk_rt_notify_observer_signal_unlocked(context, stateOffset,
                                                      width))
        return context->schedulerStatus;
    }
    if (kind == OBELISK_RT_DESIGN_RECORD_DRIVER)
      return obelisk_rt_resolve_design_drivers(context, stateOffset,
                                                stateOffset + width);
  }
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status obelisk_rt_v1_design_read(
    obelisk_rt_context *context, obelisk_rt_design_cursor_v1 cursor,
    uint64_t *value, uint64_t *unknown, uint64_t bitWidth) {
  return accessState(context, cursor, value, unknown, bitWidth, false);
}

extern "C" obelisk_rt_status obelisk_rt_v1_design_write(
    obelisk_rt_context *context, obelisk_rt_design_cursor_v1 cursor,
    const uint64_t *value, const uint64_t *unknown, uint64_t bitWidth) {
  return accessState(context, cursor, const_cast<uint64_t *>(value),
                     const_cast<uint64_t *>(unknown), bitWidth, true);
}
