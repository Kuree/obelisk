//===- DesignDatabase.cpp - Checked DWARF-like design reflection ----------===//

#include "RuntimeInternal.h"
#include "obelisk/Runtime/StableHash.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

using DatabaseHeader = obelisk_rt_design_database_header_v1;

constexpr char kMagic[] = OBELISK_RT_DESIGN_DATABASE_MAGIC;
static_assert(sizeof(kMagic) == sizeof(DatabaseHeader::magic));
constexpr uint64_t kHeaderSize = OBELISK_RT_DESIGN_DATABASE_HEADER_SIZE;
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
  uint64_t hash = OBELISK_STABLE_HASH_OFFSET_BASIS;
  for (uint64_t index = 0; index != size; ++index) {
    // The checksum field is treated as zero while hashing.
    uint8_t byte = index >= 32 && index < 40 ? 0 : data[index];
    hash = obelisk_stable_hash_append_byte(hash, byte);
  }
  return hash;
}

using Database = DesignDatabaseCache;

bool parseHeader(const obelisk_rt_execution_descriptor_v1 *execution,
                 Database &database) {
  if (!execution || execution->version != OBELISK_RT_VERSION ||
      execution->reserved != 0 ||
      (execution->flags & OBELISK_RT_EXECUTION_HAS_DESIGN_DATABASE) == 0 ||
      !execution->design_database ||
      execution->design_database_size < kHeaderSize)
    return false;
  const uint8_t *data = execution->design_database;
  if (std::memcmp(data + offsetof(DatabaseHeader, magic), kMagic,
                  sizeof(kMagic)) != 0 ||
      read32(data + offsetof(DatabaseHeader, version)) != OBELISK_RT_VERSION ||
      read32(data + offsetof(DatabaseHeader, reserved)) != 0 ||
      read32(data + offsetof(DatabaseHeader, header_size)) != kHeaderSize ||
      read64(data + offsetof(DatabaseHeader, image_size)) !=
          execution->design_database_size ||
      read64(data + offsetof(DatabaseHeader, checksum)) == 0 ||
      read64(data + offsetof(DatabaseHeader, checksum)) !=
          checksum(data, execution->design_database_size))
    return false;
  database = {data,
              execution->design_database_size,
              read32(data + offsetof(DatabaseHeader, profile)),
              read64(data + offsetof(DatabaseHeader, root_offset)),
              read64(data + offsetof(DatabaseHeader, scope_offset)),
              read64(data + offsetof(DatabaseHeader, scope_count)),
              read64(data + offsetof(DatabaseHeader, object_offset)),
              read64(data + offsetof(DatabaseHeader, object_count)),
              read64(data + offsetof(DatabaseHeader, type_offset)),
              read64(data + offsetof(DatabaseHeader, type_count)),
              read64(data + offsetof(DatabaseHeader, string_offset)),
              read64(data + offsetof(DatabaseHeader, string_size)),
              read64(data + offsetof(DatabaseHeader, index_offset)),
              read64(data + offsetof(DatabaseHeader, index_count)),
              execution->state_bit_count};
  uint32_t supportedProfile =
      OBELISK_RT_DESIGN_PROFILE_READ | OBELISK_RT_DESIGN_PROFILE_WRITE;
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
      !rangesDisjoint(database.strings, database.stringSize, 1, database.index,
                      database.indexCount, kIndexSize))
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
  uint64_t distance =
      left >= right
          ? static_cast<uint64_t>(left) - static_cast<uint64_t>(right)
          : static_cast<uint64_t>(right) - static_cast<uint64_t>(left);
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
      if (offset == database.root ? parent != 0
                                  : !isScopeOffset(database, parent))
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
        (flags &
         ~(OBELISK_RT_DESIGN_TYPE_FOUR_STATE | OBELISK_RT_DESIGN_TYPE_SIGNED |
           OBELISK_RT_DESIGN_TYPE_PACKED | OBELISK_RT_DESIGN_TYPE_TAGGED)) !=
            0 ||
        width == 0)
      return false;
    bool hasElement = element != 0;
    bool hasChildren = firstChild != 0 || childCount != 0;
    if ((hasElement && !isTypeOffset(database, element)) ||
        (hasChildren &&
         (!isTypeOffset(database, firstChild) || childCount == 0 ||
          childCount > database.typeCount ||
          (childCount - 1) >
              (std::numeric_limits<uint64_t>::max() - firstChild) / kTypeSize ||
          !isTypeOffset(database, firstChild + (childCount - 1) * kTypeSize))))
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
        if (elementKind == OBELISK_RT_DESIGN_TYPE_FIELD || elementWidth == 0 ||
            extent > UINT64_MAX / elementWidth ||
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
           (read64(record + 56) != 0)) ||
          !rangeExtent(record, extent) || extent != width)
        return false;
      break;
    case OBELISK_RT_DESIGN_TYPE_FIELD:
      if (!hasElement || hasChildren ||
          (flags &
           (OBELISK_RT_DESIGN_TYPE_SIGNED | OBELISK_RT_DESIGN_TYPE_PACKED |
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
      const uint8_t *field = database.data + firstChild + child * kTypeSize;
      uint64_t fieldWidth = read64(field + 8);
      uint64_t packedOffset = read64(field + 64);
      fourState |=
          ((read32(field + 4) >> 8) & OBELISK_RT_DESIGN_TYPE_FOUR_STATE) != 0;
      if (fieldWidth > UINT64_MAX - sum)
        return false;
      sum += fieldWidth;
      maximum = std::max(maximum, fieldWidth);
      if ((flags & OBELISK_RT_DESIGN_TYPE_PACKED) != 0) {
        uint64_t tagBits = read64(record + 56);
        if (kind == OBELISK_RT_DESIGN_TYPE_UNION && tagBits > width)
          return false;
        uint64_t payloadWidth =
            kind == OBELISK_RT_DESIGN_TYPE_UNION ? width - tagBits : width;
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
        (index != 0 && (hash < previousHash ||
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
obelisk_rt_status validateDatabase(const Database &database) noexcept {
  try {
    return validateDatabaseImpl(database) ? OBELISK_RT_OK
                                          : OBELISK_RT_INVALID_DESIGN;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_DESIGN;
  }
}

uint64_t nameHash(const uint8_t *name, uint64_t size) {
  return obelisk_stable_hash(name, size);
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

struct RegisteredDatabase {
  Database database;
  size_t contextCount = 0;
};

// Descriptor-only reflection calls can share validation with a live context,
// but the descriptor ABI has no cache slot or standalone lifetime token.
// Retain views only while contexts establish that the immutable descriptor and
// image are alive, and remove the final reference during context destruction.
struct DatabaseRegistry {
  std::mutex mutex;
  std::unordered_map<const obelisk_rt_execution_descriptor_v1 *,
                     RegisteredDatabase>
      databases;
};

DatabaseRegistry &databaseRegistry() {
  static DatabaseRegistry registry;
  return registry;
}

bool matchesExecution(
    const Database &database,
    const obelisk_rt_execution_descriptor_v1 *execution) noexcept {
  return execution && database.validated &&
         database.data == execution->design_database &&
         database.size == execution->design_database_size &&
         database.stateBitCount == execution->state_bit_count;
}

bool sameDatabase(const Database &left, const Database &right) noexcept {
  return left.data == right.data && left.size == right.size &&
         left.profile == right.profile && left.root == right.root &&
         left.scopes == right.scopes && left.scopeCount == right.scopeCount &&
         left.objects == right.objects &&
         left.objectCount == right.objectCount && left.types == right.types &&
         left.typeCount == right.typeCount && left.strings == right.strings &&
         left.stringSize == right.stringSize && left.index == right.index &&
         left.indexCount == right.indexCount &&
         left.stateBitCount == right.stateBitCount &&
         left.validated == right.validated;
}

bool registeredDatabase(const obelisk_rt_execution_descriptor_v1 *execution,
                        Database &database) {
  if (!execution)
    return false;
  DatabaseRegistry &registry = databaseRegistry();
  std::lock_guard<std::mutex> lock(registry.mutex);
  auto found = registry.databases.find(execution);
  if (found == registry.databases.end() ||
      !matchesExecution(found->second.database, execution))
    return false;
  database = found->second.database;
  return true;
}

obelisk_rt_status
loadValidatedDatabase(const obelisk_rt_execution_descriptor_v1 *execution,
                      Database &database) noexcept {
  try {
    if (registeredDatabase(execution, database))
      return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_DESIGN;
  }
  if (!parseHeader(execution, database))
    return OBELISK_RT_INVALID_DESIGN;
  return validateDatabase(database);
}

obelisk_rt_status mapInvalidDatabase(obelisk_rt_status status,
                                     obelisk_rt_status invalidStatus) noexcept {
  return status == OBELISK_RT_INVALID_DESIGN ? invalidStatus : status;
}

const Database *cachedDatabase(const obelisk_rt_context *context) {
  if (!context ||
      !matchesExecution(context->designDatabase, context->execution))
    return nullptr;
  return &context->designDatabase;
}

obelisk_rt_status designRoot(const Database &database,
                             obelisk_rt_design_cursor_v1 *outCursor) {
  outCursor->offset = database.root;
  return OBELISK_RT_OK;
}

obelisk_rt_status designChild(const Database &database,
                              obelisk_rt_design_cursor_v1 cursor,
                              obelisk_rt_design_cursor_v1 *outCursor) {
  const uint8_t *record;
  uint32_t kind;
  if (!getRecord(database, cursor.offset, record, kind) ||
      kind != OBELISK_RT_DESIGN_RECORD_SCOPE)
    return OBELISK_RT_INVALID_HANDLE;
  outCursor->offset = read64(record + 24);
  return outCursor->offset == 0 ? OBELISK_RT_EOF : OBELISK_RT_OK;
}

obelisk_rt_status designChildAt(const Database &database,
                                obelisk_rt_design_cursor_v1 cursor,
                                uint64_t index,
                                obelisk_rt_design_cursor_v1 *outCursor) {
  const uint8_t *record;
  uint32_t kind;
  if (!getRecord(database, cursor.offset, record, kind) ||
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

obelisk_rt_status designSibling(const Database &database,
                                obelisk_rt_design_cursor_v1 cursor,
                                obelisk_rt_design_cursor_v1 *outCursor) {
  const uint8_t *record;
  uint32_t kind;
  if (!getRecord(database, cursor.offset, record, kind) ||
      kind == OBELISK_RT_DESIGN_RECORD_TYPE)
    return OBELISK_RT_INVALID_HANDLE;
  outCursor->offset = nextOffset(record, kind);
  return outCursor->offset == 0 ? OBELISK_RT_EOF : OBELISK_RT_OK;
}

obelisk_rt_status designLookup(const Database &database, const uint8_t *name,
                               uint64_t nameSize,
                               obelisk_rt_design_cursor_v1 *outCursor) {
  uint64_t wantedHash = nameHash(name, nameSize);
  uint64_t low = 0, high = database.indexCount;
  while (low != high) {
    uint64_t middle = low + (high - low) / 2;
    uint64_t hash =
        read64(database.data + database.index + middle * kIndexSize);
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

obelisk_rt_status designInfo(const Database &database,
                             obelisk_rt_design_cursor_v1 cursor,
                             obelisk_rt_design_info_v1 *outInfo) {
  const uint8_t *record;
  uint32_t kind;
  if (!getRecord(database, cursor.offset, record, kind) ||
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

obelisk_rt_status designTypeInfo(const Database &database,
                                 obelisk_rt_design_cursor_v1 cursor,
                                 obelisk_rt_design_type_info_v1 *outInfo) {
  const uint8_t *record;
  uint32_t recordKind;
  if (!getRecord(database, cursor.offset, record, recordKind) ||
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

obelisk_rt_status designTypeChild(const Database &database,
                                  obelisk_rt_design_cursor_v1 cursor,
                                  uint64_t index,
                                  obelisk_rt_design_cursor_v1 *outCursor) {
  const uint8_t *record;
  uint32_t recordKind;
  if (!getRecord(database, cursor.offset, record, recordKind) ||
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

obelisk_rt_status designName(const Database &database,
                             obelisk_rt_design_cursor_v1 cursor,
                             const uint8_t **outData, uint64_t *outSize) {
  const uint8_t *record;
  uint32_t kind;
  std::string_view name;
  if (!getRecord(database, cursor.offset, record, kind) ||
      !getString(
          database,
          read64(record + (kind == OBELISK_RT_DESIGN_RECORD_TYPE ? 72 : 40)),
          name))
    return OBELISK_RT_INVALID_HANDLE;
  *outData = reinterpret_cast<const uint8_t *>(name.data());
  *outSize = name.size();
  return OBELISK_RT_OK;
}

} // namespace

obelisk_rt_status obelisk_rt_initialize_design_database(
    const obelisk_rt_execution_descriptor_v1 *execution,
    DesignDatabaseCache &cache) noexcept {
  if (!execution)
    return OBELISK_RT_INVALID_ARGUMENT;
  try {
    Database database;
    obelisk_rt_status status = loadValidatedDatabase(execution, database);
    if (status != OBELISK_RT_OK)
      return status;
    database.validated = true;
    cache = database;
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_DESIGN;
  }
}

obelisk_rt_status obelisk_rt_register_design_database(
    const obelisk_rt_execution_descriptor_v1 *execution,
    const DesignDatabaseCache &cache) noexcept {
  if (!matchesExecution(cache, execution))
    return OBELISK_RT_INVALID_ARGUMENT;
  try {
    DatabaseRegistry &registry = databaseRegistry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    auto [entry, inserted] = registry.databases.try_emplace(execution);
    if (inserted)
      entry->second.database = cache;
    else if (!sameDatabase(entry->second.database, cache))
      return OBELISK_RT_INVALID_DESIGN;
    if (entry->second.contextCount == std::numeric_limits<size_t>::max()) {
      if (inserted)
        registry.databases.erase(entry);
      return OBELISK_RT_OUT_OF_RESOURCES;
    }
    ++entry->second.contextCount;
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_DESIGN;
  }
}

void obelisk_rt_unregister_design_database(
    const obelisk_rt_execution_descriptor_v1 *execution) noexcept {
  if (!execution)
    return;
  try {
    DatabaseRegistry &registry = databaseRegistry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    auto found = registry.databases.find(execution);
    if (found == registry.databases.end())
      return;
    if (found->second.contextCount > 1) {
      --found->second.contextCount;
      return;
    }
    registry.databases.erase(found);
  } catch (...) {
    // Context teardown cannot recover from a registry synchronization failure.
  }
}

obelisk_rt_status
obelisk_rt_cached_design_root(const obelisk_rt_context *context,
                              obelisk_rt_design_cursor_v1 *outCursor) noexcept {
  if (!outCursor)
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  return database ? designRoot(*database, outCursor)
                  : OBELISK_RT_INVALID_DESIGN;
}

obelisk_rt_status obelisk_rt_cached_design_child(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 cursor,
    obelisk_rt_design_cursor_v1 *outCursor) noexcept {
  if (!outCursor)
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  return database ? designChild(*database, cursor, outCursor)
                  : OBELISK_RT_INVALID_HANDLE;
}

obelisk_rt_status obelisk_rt_cached_design_child_at(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 cursor,
    uint64_t index, obelisk_rt_design_cursor_v1 *outCursor) noexcept {
  if (!outCursor)
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  return database ? designChildAt(*database, cursor, index, outCursor)
                  : OBELISK_RT_INVALID_HANDLE;
}

obelisk_rt_status obelisk_rt_cached_design_sibling(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 cursor,
    obelisk_rt_design_cursor_v1 *outCursor) noexcept {
  if (!outCursor)
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  return database ? designSibling(*database, cursor, outCursor)
                  : OBELISK_RT_INVALID_HANDLE;
}

obelisk_rt_status obelisk_rt_cached_design_lookup(
    const obelisk_rt_context *context, const uint8_t *name, uint64_t nameSize,
    obelisk_rt_design_cursor_v1 *outCursor) noexcept {
  if (!outCursor || (nameSize != 0 && !name))
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  return database ? designLookup(*database, name, nameSize, outCursor)
                  : OBELISK_RT_INVALID_DESIGN;
}

obelisk_rt_status
obelisk_rt_cached_design_info(const obelisk_rt_context *context,
                              obelisk_rt_design_cursor_v1 cursor,
                              obelisk_rt_design_info_v1 *outInfo) noexcept {
  if (!outInfo)
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  return database ? designInfo(*database, cursor, outInfo)
                  : OBELISK_RT_INVALID_HANDLE;
}

obelisk_rt_status obelisk_rt_cached_design_type_info(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 cursor,
    obelisk_rt_design_type_info_v1 *outInfo) noexcept {
  if (!outInfo)
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  return database ? designTypeInfo(*database, cursor, outInfo)
                  : OBELISK_RT_INVALID_HANDLE;
}

obelisk_rt_status obelisk_rt_cached_design_type_child(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 cursor,
    uint64_t index, obelisk_rt_design_cursor_v1 *outCursor) noexcept {
  if (!outCursor)
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  return database ? designTypeChild(*database, cursor, index, outCursor)
                  : OBELISK_RT_INVALID_HANDLE;
}

obelisk_rt_status obelisk_rt_cached_design_name(
    const obelisk_rt_context *context, obelisk_rt_design_cursor_v1 cursor,
    const uint8_t **outData, uint64_t *outSize) noexcept {
  if (!outData || !outSize)
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  return database ? designName(*database, cursor, outData, outSize)
                  : OBELISK_RT_INVALID_HANDLE;
}

bool obelisk_rt_checked_design_record(
    const obelisk_rt_execution_descriptor_v1 *execution, uint64_t offset,
    const uint8_t *&record, uint32_t &kind) noexcept {
  try {
    Database database;
    return loadValidatedDatabase(execution, database) == OBELISK_RT_OK &&
           getRecord(database, offset, record, kind);
  } catch (...) {
    return false;
  }
}

extern "C" obelisk_rt_status obelisk_rt_v1_design_validate(
    const obelisk_rt_execution_descriptor_v1 *execution) {
  try {
    Database database;
    return loadValidatedDatabase(execution, database);
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_DESIGN;
  }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_design_root(const obelisk_rt_execution_descriptor_v1 *execution,
                          obelisk_rt_design_cursor_v1 *outCursor) {
  if (!outCursor)
    return OBELISK_RT_INVALID_ARGUMENT;
  Database database;
  obelisk_rt_status status = loadValidatedDatabase(execution, database);
  if (status != OBELISK_RT_OK)
    return status;
  return designRoot(database, outCursor);
}

extern "C" obelisk_rt_status
obelisk_rt_v1_design_child(const obelisk_rt_execution_descriptor_v1 *execution,
                           obelisk_rt_design_cursor_v1 cursor,
                           obelisk_rt_design_cursor_v1 *outCursor) {
  if (!outCursor)
    return OBELISK_RT_INVALID_ARGUMENT;
  Database database;
  obelisk_rt_status status = loadValidatedDatabase(execution, database);
  if (status != OBELISK_RT_OK)
    return mapInvalidDatabase(status, OBELISK_RT_INVALID_HANDLE);
  return designChild(database, cursor, outCursor);
}

extern "C" obelisk_rt_status obelisk_rt_v1_design_child_at(
    const obelisk_rt_execution_descriptor_v1 *execution,
    obelisk_rt_design_cursor_v1 cursor, uint64_t index,
    obelisk_rt_design_cursor_v1 *outCursor) {
  if (!outCursor)
    return OBELISK_RT_INVALID_ARGUMENT;
  Database database;
  obelisk_rt_status status = loadValidatedDatabase(execution, database);
  if (status != OBELISK_RT_OK)
    return mapInvalidDatabase(status, OBELISK_RT_INVALID_HANDLE);
  return designChildAt(database, cursor, index, outCursor);
}

extern "C" obelisk_rt_status obelisk_rt_v1_design_sibling(
    const obelisk_rt_execution_descriptor_v1 *execution,
    obelisk_rt_design_cursor_v1 cursor,
    obelisk_rt_design_cursor_v1 *outCursor) {
  if (!outCursor)
    return OBELISK_RT_INVALID_ARGUMENT;
  Database database;
  obelisk_rt_status status = loadValidatedDatabase(execution, database);
  if (status != OBELISK_RT_OK)
    return mapInvalidDatabase(status, OBELISK_RT_INVALID_HANDLE);
  return designSibling(database, cursor, outCursor);
}

extern "C" obelisk_rt_status
obelisk_rt_v1_design_lookup(const obelisk_rt_execution_descriptor_v1 *execution,
                            const uint8_t *name, uint64_t nameSize,
                            obelisk_rt_design_cursor_v1 *outCursor) {
  if (!outCursor || (nameSize != 0 && !name))
    return OBELISK_RT_INVALID_ARGUMENT;
  Database database;
  obelisk_rt_status status = loadValidatedDatabase(execution, database);
  if (status != OBELISK_RT_OK)
    return status;
  return designLookup(database, name, nameSize, outCursor);
}

extern "C" obelisk_rt_status
obelisk_rt_v1_design_info(const obelisk_rt_execution_descriptor_v1 *execution,
                          obelisk_rt_design_cursor_v1 cursor,
                          obelisk_rt_design_info_v1 *outInfo) {
  if (!outInfo)
    return OBELISK_RT_INVALID_ARGUMENT;
  Database database;
  obelisk_rt_status status = loadValidatedDatabase(execution, database);
  if (status != OBELISK_RT_OK)
    return mapInvalidDatabase(status, OBELISK_RT_INVALID_HANDLE);
  return designInfo(database, cursor, outInfo);
}

extern "C" obelisk_rt_status obelisk_rt_v1_design_type_info(
    const obelisk_rt_execution_descriptor_v1 *execution,
    obelisk_rt_design_cursor_v1 cursor,
    obelisk_rt_design_type_info_v1 *outInfo) {
  if (!outInfo)
    return OBELISK_RT_INVALID_ARGUMENT;
  Database database;
  obelisk_rt_status status = loadValidatedDatabase(execution, database);
  if (status != OBELISK_RT_OK)
    return mapInvalidDatabase(status, OBELISK_RT_INVALID_HANDLE);
  return designTypeInfo(database, cursor, outInfo);
}

extern "C" obelisk_rt_status obelisk_rt_v1_design_type_child(
    const obelisk_rt_execution_descriptor_v1 *execution,
    obelisk_rt_design_cursor_v1 cursor, uint64_t index,
    obelisk_rt_design_cursor_v1 *outCursor) {
  if (!outCursor)
    return OBELISK_RT_INVALID_ARGUMENT;
  Database database;
  obelisk_rt_status status = loadValidatedDatabase(execution, database);
  if (status != OBELISK_RT_OK)
    return mapInvalidDatabase(status, OBELISK_RT_INVALID_HANDLE);
  return designTypeChild(database, cursor, index, outCursor);
}

extern "C" obelisk_rt_status
obelisk_rt_v1_design_name(const obelisk_rt_execution_descriptor_v1 *execution,
                          obelisk_rt_design_cursor_v1 cursor,
                          const uint8_t **outData, uint64_t *outSize) {
  if (!outData || !outSize)
    return OBELISK_RT_INVALID_ARGUMENT;
  Database database;
  obelisk_rt_status status = loadValidatedDatabase(execution, database);
  if (status != OBELISK_RT_OK)
    return mapInvalidDatabase(status, OBELISK_RT_INVALID_HANDLE);
  return designName(database, cursor, outData, outSize);
}

uint64_t packedMask(uint64_t width) {
  return width == 64 ? UINT64_MAX : (uint64_t{1} << width) - 1;
}

uint64_t loadPackedState(const std::vector<uint64_t> &plane, uint64_t offset,
                         uint64_t width) {
  size_t word = static_cast<size_t>(offset / 64);
  unsigned shift = static_cast<unsigned>(offset % 64);
  uint64_t result = plane[word] >> shift;
  if (shift != 0 && width > 64 - shift)
    result |= plane[word + 1] << (64 - shift);
  return result & packedMask(width);
}

uint64_t loadPackedBytes(const uint8_t *plane, uint64_t offset,
                         uint64_t width) {
  size_t firstByte = static_cast<size_t>(offset / 8);
  unsigned shift = static_cast<unsigned>(offset % 8);
  size_t byteCount = static_cast<size_t>((shift + width + 7) / 8);
  unsigned __int128 bits = 0;
  for (size_t byte = 0; byte != byteCount; ++byte)
    bits |= static_cast<unsigned __int128>(plane[firstByte + byte])
            << (byte * 8);
  return static_cast<uint64_t>(bits >> shift) & packedMask(width);
}

void storePackedState(std::vector<uint64_t> &plane, uint64_t offset,
                      uint64_t width, uint64_t value) {
  size_t word = static_cast<size_t>(offset / 64);
  unsigned shift = static_cast<unsigned>(offset % 64);
  uint64_t mask = packedMask(width);
  value &= mask;
  uint64_t lowMask = mask << shift;
  plane[word] = (plane[word] & ~lowMask) | (value << shift);
  if (shift != 0 && width > 64 - shift) {
    unsigned lowBits = 64 - shift;
    uint64_t highMask = mask >> lowBits;
    plane[word + 1] = (plane[word + 1] & ~highMask) | (value >> lowBits);
  }
}

void storePackedBytes(uint8_t *bytes, uint64_t value) {
  for (unsigned byte = 0; byte != 8; ++byte)
    bytes[byte] = static_cast<uint8_t>(value >> (byte * 8));
}

void setPackedByte(uint8_t *bytes, uint64_t bit, bool value) {
  uint8_t mask = static_cast<uint8_t>(1u << (bit % 8));
  if (value)
    bytes[bit / 8] |= mask;
  else
    bytes[bit / 8] &= static_cast<uint8_t>(~mask);
}

static obelisk_rt_status accessState(obelisk_rt_context *context,
                                     obelisk_rt_design_cursor_v1 cursor,
                                     uint64_t *value, uint64_t *unknown,
                                     uint64_t bitWidth, bool write,
                                     bool overrideForce = false) {
  if (!context || !value || bitWidth == 0 || !context->execution)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  const Database *database = cachedDatabase(context);
  const uint8_t *record;
  uint32_t kind;
  if (!database || !getRecord(*database, cursor.offset, record, kind) ||
      kind < OBELISK_RT_DESIGN_RECORD_STORAGE ||
      kind > OBELISK_RT_DESIGN_RECORD_DRIVER)
    return OBELISK_RT_INVALID_HANDLE;
  uint32_t capabilities = read32(record + 4);
  if ((capabilities &
       (write ? OBELISK_RT_DESIGN_CAP_WRITE : OBELISK_RT_DESIGN_CAP_READ)) == 0)
    return OBELISK_RT_PERMISSION_DENIED;
  uint64_t stateOffset = read64(record + 80);
  uint64_t width = read64(record + 56);
  if (bitWidth != width || stateOffset > context->execution->state_bit_count ||
      width > context->execution->state_bit_count - stateOffset)
    return OBELISK_RT_INVALID_HANDLE;
  uint64_t limbs = (width + 63) / 64;
  const uint8_t *typeRecord = database->data + read64(record + 48);
  bool fourState =
      ((read32(typeRecord + 4) >> 8) & OBELISK_RT_DESIGN_TYPE_FOUR_STATE) != 0;
  if (write && !overrideForce && kind == OBELISK_RT_DESIGN_RECORD_NET) {
    bool connected = false;
    obelisk_rt_status status = obelisk_rt_design_net_is_connected(
        context, stateOffset, stateOffset + width, &connected);
    if (status != OBELISK_RT_OK)
      return status;
    // A direct deposit cannot update one logical alias in isolation.
    if (connected)
      return OBELISK_RT_PERMISSION_DENIED;
  }
  std::optional<PackedSignalTransitionBuffer> wideTransitions;
  constexpr size_t inlinePublishedBytes = 32;
  std::array<uint8_t, inlinePublishedBytes * 2> inlinePublishedPlanes;
  std::vector<uint8_t> overflowPublishedPlanes;
  uint8_t *publishedPlanes = nullptr;
  if (write && width > 64) {
    try {
      wideTransitions.emplace(width);
      size_t bytes = static_cast<size_t>((width + 7) / 8);
      if (bytes <= inlinePublishedBytes) {
        std::fill_n(inlinePublishedPlanes.data(), bytes * 2, uint8_t{0});
        publishedPlanes = inlinePublishedPlanes.data();
      } else {
        overflowPublishedPlanes.assign(bytes * 2, 0);
        publishedPlanes = overflowPublishedPlanes.data();
      }
    } catch (const std::bad_alloc &) {
      return OBELISK_RT_OUT_OF_MEMORY;
    } catch (...) {
      return OBELISK_RT_INVALID_DESIGN;
    }
  }
  bool stateChanged = false;
  bool runClockCoordinator = false;
  {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    uint64_t signalBase = UINT64_MAX;
    if (write) {
      signalBase = obelisk_rt_canonical_state_handle_unlocked(
          context, stateOffset, width);
      if (signalBase == UINT64_MAX)
        return OBELISK_RT_INVALID_HANDLE;
    }
    const uint8_t *canonicalValuePlane = nullptr;
    const uint8_t *canonicalUnknownPlane = nullptr;
    const obelisk_rt_native_schedule_plan *plan = context->nativeSchedulePlan;
    if (!write && plan &&
        (plan->flags & OBELISK_RT_NATIVE_SCHEDULE_DIRECT_STATE) != 0 &&
        !context->nativeScheduleDeoptimized &&
        plan->state_bit_count == context->execution->state_bit_count &&
        plan->state_value && plan->state_unknown) {
      bool dirty = context->nativeScheduleDirtyRootsPresent;
      if (dirty) {
        dirty = false;
        __int128 accessEnd = static_cast<__int128>(stateOffset) + width;
        for (const auto &[id, state] : context->nativeStaticStates)
          if (static_cast<__int128>(state.bitOffset) < accessEnd &&
              static_cast<__int128>(stateOffset) <
                  static_cast<__int128>(state.bitOffset) + state.bitWidth &&
              (context->nativeScheduleTransientDirtyRoots.find(id) !=
                   context->nativeScheduleTransientDirtyRoots.end() ||
               context->nativeSchedulePersistentDirtyRoots.find(id) !=
                   context->nativeSchedulePersistentDirtyRoots.end())) {
            dirty = true;
            break;
          }
      }
      if (!dirty) {
        canonicalValuePlane = plan->state_value;
        canonicalUnknownPlane = plan->state_unknown;
      }
    }
    if (!write && width <= 64) {
      value[0] = canonicalValuePlane
                     ? loadPackedBytes(canonicalValuePlane, stateOffset, width)
                     : loadPackedState(context->stateValue, stateOffset, width);
      if (unknown)
        unknown[0] = fourState ? (canonicalUnknownPlane
                                      ? loadPackedBytes(canonicalUnknownPlane,
                                                        stateOffset, width)
                                      : loadPackedState(context->stateUnknown,
                                                        stateOffset, width))
                               : 0;
      return OBELISK_RT_OK;
    }
    if (write && width <= 64) {
      uint64_t mask = packedMask(width);
      uint64_t oldValue =
          loadPackedState(context->stateValue, stateOffset, width);
      uint64_t oldUnknown =
          loadPackedState(context->stateUnknown, stateOffset, width);
      uint64_t blocked = 0;
      if (!overrideForce) {
        if (!context->forceMask.empty())
          blocked |= loadPackedState(context->forceMask, stateOffset, width);
        if (!context->assignMask.empty())
          blocked |= loadPackedState(context->assignMask, stateOffset, width);
      }
      uint64_t writable = mask & ~blocked;
      uint64_t newValue = (oldValue & ~writable) | (value[0] & writable);
      uint64_t inputUnknown = fourState && unknown ? unknown[0] : 0;
      uint64_t newUnknown =
          (oldUnknown & ~writable) | (inputUnknown & writable);
      uint64_t changed = (oldValue ^ newValue) | (oldUnknown ^ newUnknown);
      if (changed != 0) {
        uint64_t oldZero = ~oldUnknown & ~oldValue & mask;
        uint64_t oldOne = ~oldUnknown & oldValue & mask;
        uint64_t newZero = ~newUnknown & ~newValue & mask;
        uint64_t newOne = ~newUnknown & newValue & mask;
        uint64_t posedge =
            ((oldZero & ~newZero) | (oldUnknown & newOne)) & mask;
        uint64_t negedge = ((oldOne & ~newOne) | (oldUnknown & newZero)) & mask;
        std::array<uint8_t, 8> changedBytes{}, posedgeBytes{}, negedgeBytes{};
        std::array<uint8_t, 8> valueBytes{}, unknownBytes{};
        storePackedBytes(changedBytes.data(), changed);
        storePackedBytes(posedgeBytes.data(), posedge);
        storePackedBytes(negedgeBytes.data(), negedge);
        storePackedBytes(valueBytes.data(), newValue);
        storePackedBytes(unknownBytes.data(), newUnknown);
        storePackedState(context->stateValue, stateOffset, width, newValue);
        storePackedState(context->stateUnknown, stateOffset, width, newUnknown);
        // Force/release changes the persistent override state and must retain
        // the transactional generic handoff. Only an exact deposit may enter
        // the generated clock coordinator directly.
        bool synchronized =
            !overrideForce && obelisk_rt_aot_external_deposit_unlocked(
                                  context, signalBase, stateOffset, width);
        if (!synchronized)
          obelisk_rt_aot_external_write_handle_unlocked(
              context, signalBase, stateOffset, width, false);
        if (!obelisk_rt_publish_native_signal_transition_unlocked(
                context, signalBase, width, changedBytes.data(),
                posedgeBytes.data(), negedgeBytes.data(), valueBytes.data(),
                unknownBytes.data(), synchronized))
          return context->schedulerStatus;
        runClockCoordinator |=
            synchronized && context->nativeSchedulePlan &&
            context->nativeSchedulePlan->clock_kernel_count != 0;
        stateChanged = true;
      }
    } else {
      size_t publishedBytes = static_cast<size_t>((width + 7) / 8);
      uint8_t *publishedValue =
          write ? publishedPlanes : static_cast<uint8_t *>(nullptr);
      uint8_t *publishedUnknown = write ? publishedPlanes + publishedBytes
                                        : static_cast<uint8_t *>(nullptr);
      for (uint64_t bit = 0; bit != width; ++bit) {
        uint64_t sourceLimb = bit / 64;
        uint64_t sourceMask = uint64_t{1} << (bit % 64);
        uint64_t absolute = stateOffset + bit;
        uint64_t stateMask = uint64_t{1} << (absolute % 64);
        uint64_t &stateValue = context->stateValue[absolute / 64];
        uint64_t &stateUnknown = context->stateUnknown[absolute / 64];
        if (write) {
          bool forced = absolute / 64 < context->forceMask.size() &&
                        (context->forceMask[absolute / 64] & stateMask) != 0;
          bool assigned = absolute / 64 < context->assignMask.size() &&
                          (context->assignMask[absolute / 64] & stateMask) != 0;
          if ((forced || assigned) && !overrideForce)
            continue;
          bool oldValue = (stateValue & stateMask) != 0;
          bool oldUnknown = (stateUnknown & stateMask) != 0;
          bool newValue = (value[sourceLimb] & sourceMask) != 0;
          bool newUnknown =
              fourState && unknown && (unknown[sourceLimb] & sourceMask) != 0;
          stateValue =
              newValue ? stateValue | stateMask : stateValue & ~stateMask;
          stateUnknown =
              newUnknown ? stateUnknown | stateMask : stateUnknown & ~stateMask;
          uint32_t edges =
              transitionEdges(oldValue, oldUnknown, newValue, newUnknown);
          if (edges != 0) {
            wideTransitions->record(bit, edges);
            stateChanged = true;
          }
          setPackedByte(publishedValue, bit, newValue);
          setPackedByte(publishedUnknown, bit, newUnknown);
        } else {
          bool readValue =
              canonicalValuePlane
                  ? ((canonicalValuePlane[absolute / 8] >> (absolute % 8)) &
                     1) != 0
                  : (stateValue & stateMask) != 0;
          bool readUnknown =
              canonicalUnknownPlane
                  ? ((canonicalUnknownPlane[absolute / 8] >> (absolute % 8)) &
                     1) != 0
                  : (stateUnknown & stateMask) != 0;
          value[sourceLimb] =
              (value[sourceLimb] & ~sourceMask) | (readValue ? sourceMask : 0);
          if (unknown && fourState)
            unknown[sourceLimb] = (unknown[sourceLimb] & ~sourceMask) |
                                  (readUnknown ? sourceMask : 0);
          else if (unknown)
            unknown[sourceLimb] &= ~sourceMask;
        }
      }
      if (write && stateChanged) {
        bool synchronized =
            !overrideForce && obelisk_rt_aot_external_deposit_unlocked(
                                  context, signalBase, stateOffset, width);
        if (!synchronized)
          obelisk_rt_aot_external_write_handle_unlocked(
              context, signalBase, stateOffset, width, false);
        if (!obelisk_rt_publish_native_signal_transition_unlocked(
                context, signalBase, width, wideTransitions->changed(),
                wideTransitions->posedge(), wideTransitions->negedge(),
                publishedValue, publishedUnknown, synchronized))
          return context->schedulerStatus;
        runClockCoordinator |=
            synchronized && context->nativeSchedulePlan &&
            context->nativeSchedulePlan->clock_kernel_count != 0;
      }
    }
  }
  if (!write && width % 64 != 0) {
    uint64_t mask = (uint64_t{1} << (width % 64)) - 1;
    value[limbs - 1] &= mask;
    if (unknown)
      unknown[limbs - 1] &= mask;
  }
  if (runClockCoordinator) {
    obelisk_rt_status status =
        obelisk_rt_v1_scheduler_run_clock_coordinator(context);
    if (status != OBELISK_RT_OK)
      return status;
  }
  if (write && kind == OBELISK_RT_DESIGN_RECORD_DRIVER)
    return obelisk_rt_resolve_design_drivers(context, stateOffset,
                                             stateOffset + width);
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_design_read(obelisk_rt_context *context,
                          obelisk_rt_design_cursor_v1 cursor, uint64_t *value,
                          uint64_t *unknown, uint64_t bitWidth) {
  return accessState(context, cursor, value, unknown, bitWidth, false);
}

extern "C" obelisk_rt_status obelisk_rt_v1_design_write(
    obelisk_rt_context *context, obelisk_rt_design_cursor_v1 cursor,
    const uint64_t *value, const uint64_t *unknown, uint64_t bitWidth) {
  return accessState(context, cursor, const_cast<uint64_t *>(value),
                     const_cast<uint64_t *>(unknown), bitWidth, true);
}

extern "C" obelisk_rt_status obelisk_rt_v1_design_force(
    obelisk_rt_context *context, obelisk_rt_design_cursor_v1 cursor,
    const uint64_t *value, const uint64_t *unknown, uint64_t bitWidth) {
  if (!context || !value || bitWidth == 0 || !context->execution)
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  const uint8_t *record;
  uint32_t kind;
  if (!database || !getRecord(*database, cursor.offset, record, kind) ||
      (kind != OBELISK_RT_DESIGN_RECORD_STORAGE &&
       kind != OBELISK_RT_DESIGN_RECORD_NET) ||
      (read32(record + 4) & OBELISK_RT_DESIGN_CAP_WRITE) == 0 ||
      read64(record + 56) != bitWidth)
    return OBELISK_RT_PERMISSION_DENIED;
  uint64_t stateOffset = read64(record + 80);
  if (stateOffset > context->execution->state_bit_count ||
      bitWidth > context->execution->state_bit_count - stateOffset)
    return OBELISK_RT_INVALID_HANDLE;
  if (kind == OBELISK_RT_DESIGN_RECORD_NET)
    return obelisk_rt_force_design_nets(
        context, stateOffset, bitWidth,
        reinterpret_cast<const uint8_t *>(value),
        reinterpret_cast<const uint8_t *>(unknown));
  try {
    {
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      if (context->forceMask.empty())
        context->forceMask.assign(context->stateValue.size(), 0);
      for (uint64_t bit = 0; bit != bitWidth; ++bit) {
        uint64_t absolute = stateOffset + bit;
        context->forceMask[absolute / 64] |= uint64_t{1} << (absolute % 64);
      }
    }
    obelisk_rt_status status =
        accessState(context, cursor, const_cast<uint64_t *>(value),
                    const_cast<uint64_t *>(unknown), bitWidth, true, true);
    if (status == OBELISK_RT_OK) {
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      obelisk_rt_aot_external_write_range_unlocked(context, stateOffset,
                                                   bitWidth, true);
    }
    return status;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_DESIGN;
  }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_design_release(obelisk_rt_context *context,
                             obelisk_rt_design_cursor_v1 cursor) {
  if (!context || !context->execution)
    return OBELISK_RT_INVALID_ARGUMENT;
  const Database *database = cachedDatabase(context);
  const uint8_t *record;
  uint32_t kind;
  if (!database || !getRecord(*database, cursor.offset, record, kind) ||
      (kind != OBELISK_RT_DESIGN_RECORD_STORAGE &&
       kind != OBELISK_RT_DESIGN_RECORD_NET) ||
      (read32(record + 4) & OBELISK_RT_DESIGN_CAP_WRITE) == 0)
    return OBELISK_RT_INVALID_HANDLE;
  uint64_t stateOffset = read64(record + 80);
  uint64_t bitWidth = read64(record + 56);
  if (stateOffset > context->execution->state_bit_count ||
      bitWidth > context->execution->state_bit_count - stateOffset)
    return OBELISK_RT_INVALID_HANDLE;
  if (kind == OBELISK_RT_DESIGN_RECORD_NET)
    return obelisk_rt_release_design_nets(context, stateOffset, bitWidth);
  std::vector<std::pair<uint64_t, uint32_t>> transitions;
  try {
    transitions.reserve(static_cast<size_t>(bitWidth));
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  }
  uint64_t signalBase = UINT64_MAX;
  {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    signalBase = obelisk_rt_canonical_state_handle_unlocked(
        context, stateOffset, bitWidth);
    if (signalBase == UINT64_MAX)
      return OBELISK_RT_INVALID_HANDLE;
    for (uint64_t bit = 0; bit != bitWidth; ++bit) {
      uint64_t absolute = stateOffset + bit;
      uint64_t limb = absolute / 64;
      uint64_t mask = uint64_t{1} << (absolute % 64);
      if (limb < context->forceMask.size())
        context->forceMask[limb] &= ~mask;
      if (kind == OBELISK_RT_DESIGN_RECORD_STORAGE &&
          limb < context->assignMask.size() &&
          (context->assignMask[limb] & mask) != 0) {
        bool oldValue = (context->stateValue[limb] & mask) != 0;
        bool oldUnknown = (context->stateUnknown[limb] & mask) != 0;
        bool newValue = (context->assignValue[limb] & mask) != 0;
        bool newUnknown = (context->assignUnknown[limb] & mask) != 0;
        context->stateValue[limb] = newValue
                                        ? context->stateValue[limb] | mask
                                        : context->stateValue[limb] & ~mask;
        context->stateUnknown[limb] = newUnknown
                                          ? context->stateUnknown[limb] | mask
                                          : context->stateUnknown[limb] & ~mask;
        uint32_t edges =
            transitionEdges(oldValue, oldUnknown, newValue, newUnknown);
        if (edges) {
          uint64_t signal = obelisk_rt_v1_native_handle_offset(
              signalBase, static_cast<int64_t>(bit));
          if (signal == UINT64_MAX)
            return OBELISK_RT_INVALID_HANDLE;
          transitions.push_back({signal, edges});
        }
      }
    }
    if (!transitions.empty())
      obelisk_rt_invalidate_signal_snapshots_unlocked(context, signalBase,
                                                      bitWidth);
    if (!transitions.empty())
      obelisk_rt_aot_external_write_unlocked(context);
    obelisk_rt_aot_release_range_unlocked(context, stateOffset, bitWidth);
  }
  // A variable retains the forced value. A net is immediately republished
  // from its current driver slots (and becomes Z when the component is
  // undriven).
  for (auto [signal, edges] : transitions)
    obelisk_rt_v1_scheduler_signal(context, signal, 1, edges);
  if (!transitions.empty()) {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    if (!obelisk_rt_notify_observer_signal_unlocked(context, stateOffset,
                                                    bitWidth))
      return context->schedulerStatus;
  }
  return OBELISK_RT_OK;
}
