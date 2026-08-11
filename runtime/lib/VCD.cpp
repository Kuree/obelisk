//===- VCD.cpp - Value change dump over the canonical state planes -------===//
//
// Waveform collection is a once-per-time-slot difference over the canonical
// four-state planes, not a per-transition callback. No writer has to be
// instrumented, because the difference observes only the settled result of the
// slot -- which is also exactly VCD's required semantics. The traced set, its
// names, and its canonical bit ranges come from the design database.
//
//===----------------------------------------------------------------------===//

#include "ProcessContext.h"
#include "RuntimeInternal.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

constexpr uint64_t kScopeRecordSize = 64;
constexpr uint64_t kObjectRecordSize = 96;
constexpr uint64_t kTypeRecordSize = 80;

// Output is accumulated here and handed to libc in large pieces. VCD records
// are tiny and extremely numerous, so per-record stdio calls dominate the cost
// of the difference itself.
constexpr size_t kFlushThreshold = 1u << 16;

// Depth guard against a malformed type graph.
constexpr unsigned kMaxUnpackedDepth = 8;

// Default bound on unpacked-array expansion, matching the value Verilator
// settled on for the same trade-off: the cost of an expanded array is paid per
// element per time slot, in both file size and scan time. An array larger than
// this is omitted rather than degraded into some other shape -- VCD has no
// aggregate form, so the only alternatives are honest elements or a vector
// that misrepresents several signals as one.
constexpr uint64_t kDefaultMaxExpandedElements = 32;

// Byte gap below which two traced ranges are merged. Merging trades a few
// untraced bytes inside a memcmp for one fewer range in the scan loop.
constexpr uint64_t kRangeMergeGap = 32;

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

// The database was validated before registration, so these only re-establish
// that a cursor points at a record of the expected kind.
bool isScopeOffset(const DesignDatabaseCache &database, uint64_t offset) {
  return offset >= database.scopes &&
         offset - database.scopes < database.scopeCount * kScopeRecordSize &&
         (offset - database.scopes) % kScopeRecordSize == 0;
}

bool isObjectOffset(const DesignDatabaseCache &database, uint64_t offset) {
  return offset >= database.objects &&
         offset - database.objects < database.objectCount * kObjectRecordSize &&
         (offset - database.objects) % kObjectRecordSize == 0;
}

bool isTypeOffset(const DesignDatabaseCache &database, uint64_t offset) {
  return offset >= database.types &&
         offset - database.types < database.typeCount * kTypeRecordSize &&
         (offset - database.types) % kTypeRecordSize == 0;
}

bool getRecord(const DesignDatabaseCache &database, uint64_t offset,
               const uint8_t *&record, uint32_t &kind) {
  if (isScopeOffset(database, offset) || isObjectOffset(database, offset))
    record = database.data + offset;
  else
    return false;
  kind = read32(record);
  return true;
}

// String offsets stored in records are absolute image offsets, matching the
// reflection reader in DesignDatabase.cpp.
bool getString(const DesignDatabaseCache &database, uint64_t offset,
               std::string_view &text) {
  if (offset < database.strings ||
      offset - database.strings >= database.stringSize)
    return false;
  const char *begin = reinterpret_cast<const char *>(database.data + offset);
  uint64_t available = database.stringSize - (offset - database.strings);
  const void *end = std::memchr(begin, 0, static_cast<size_t>(available));
  if (!end)
    return false;
  text = std::string_view(
      begin, static_cast<size_t>(static_cast<const char *>(end) - begin));
  return true;
}

uint64_t firstChildOffset(const uint8_t *record) { return read64(record + 24); }

uint64_t nextSiblingOffset(const uint8_t *record, uint32_t kind) {
  return read64(record + (kind == OBELISK_RT_DESIGN_RECORD_SCOPE ? 32 : 24));
}

uint64_t parentOffset(const uint8_t *record) { return read64(record + 16); }

uint64_t nameOffset(const uint8_t *record) { return read64(record + 40); }

// Records store the full hierarchical path; VCD declares the leaf name inside
// its enclosing `$scope`. Splitting on the last dot is wrong -- an escaped
// identifier may contain one -- so the leaf is taken as the suffix past the
// enclosing scope's own full name.
std::string_view leafName(std::string_view path, std::string_view parent) {
  if (!parent.empty() && path.size() > parent.size() + 1 &&
      path.compare(0, parent.size(), parent) == 0 && path[parent.size()] == '.')
    return path.substr(parent.size() + 1);
  size_t dot = path.rfind('.');
  return dot == std::string_view::npos ? path : path.substr(dot + 1);
}

// Storage and net records carry the canonical four-state location that VPI
// reads and language writes both address.
uint64_t objectTypeOffset(const uint8_t *record) { return read64(record + 48); }
uint64_t objectBitWidth(const uint8_t *record) { return read64(record + 56); }
uint64_t objectStateBit(const uint8_t *record) { return read64(record + 80); }

bool traceableObject(uint32_t kind, const uint8_t *record) {
  if (kind != OBELISK_RT_DESIGN_RECORD_STORAGE &&
      kind != OBELISK_RT_DESIGN_RECORD_NET)
    return false;
  return (read32(record + 4) & OBELISK_RT_DESIGN_CAP_READ) != 0 &&
         objectBitWidth(record) != 0;
}

bool bitAt(const uint8_t *plane, uint64_t bit) {
  return (plane[bit / 8] & static_cast<uint8_t>(1u << (bit % 8))) != 0;
}

// Compare one bit range across two planes that use identical bit offsets. The
// shadow mirrors the live plane rather than holding an extracted copy, so no
// shifting is needed on the hot path and only the partial end bytes need
// masking.
bool bitRangeEqual(const uint8_t *left, const uint8_t *right, uint64_t bit,
                   uint64_t width) {
  uint64_t firstByte = bit / 8;
  uint64_t lastBit = bit + width - 1;
  uint64_t lastByte = lastBit / 8;
  unsigned lowShift = static_cast<unsigned>(bit % 8);
  unsigned highBit = static_cast<unsigned>(lastBit % 8);
  if (firstByte == lastByte) {
    uint8_t mask = static_cast<uint8_t>(
        ((highBit == 7 ? 0xffu : (1u << (highBit + 1)) - 1u)) &
        ~((1u << lowShift) - 1u));
    return ((left[firstByte] ^ right[firstByte]) & mask) == 0;
  }
  uint8_t firstMask = static_cast<uint8_t>(~((1u << lowShift) - 1u));
  if (((left[firstByte] ^ right[firstByte]) & firstMask) != 0)
    return false;
  uint8_t lastMask =
      static_cast<uint8_t>(highBit == 7 ? 0xffu : (1u << (highBit + 1)) - 1u);
  if (((left[lastByte] ^ right[lastByte]) & lastMask) != 0)
    return false;
  uint64_t middle = lastByte - firstByte - 1;
  return middle == 0 || std::memcmp(left + firstByte + 1, right + firstByte + 1,
                                    static_cast<size_t>(middle)) == 0;
}

char fourStateChar(bool value, bool unknown) {
  if (!unknown)
    return value ? '1' : '0';
  return value ? 'z' : 'x';
}

// VCD identifier codes are dense base-94 strings over the printable range.
std::string identifierCode(uint64_t index) {
  std::string code;
  do {
    code.push_back(static_cast<char>('!' + index % 94));
    index /= 94;
  } while (index != 0);
  return code;
}

// Resolve the byte-addressed canonical four-state planes.
//
// This deliberately matches the VPI backdoor rather than the Preponed
// snapshot. A generated schedule plan owns its state planes directly, so they
// are authoritative when one exists; otherwise the context's canonical store
// is, in every execution tier. `nativeStateValue` is only a lazily synced
// mirror -- reading it makes bytecode designs dump a waveform frozen at X.
obelisk_rt_status dumpPlanes(const obelisk_rt_context *context,
                             const uint8_t *&value, const uint8_t *&unknown) {
  const obelisk_rt_execution_descriptor_v1 &execution = *context->execution;
  const obelisk_rt_native_schedule_plan *plan = context->nativeSchedulePlan;
  if (plan && plan->state_bit_count == execution.state_bit_count &&
      plan->state_value && plan->state_unknown) {
    value = plan->state_value;
    unknown = plan->state_unknown;
    return OBELISK_RT_OK;
  }
  size_t stateWords =
      static_cast<size_t>((execution.state_bit_count + 63) / 64);
  if (context->stateValue.size() != stateWords ||
      context->stateUnknown.size() != stateWords)
    return OBELISK_RT_INVALID_DESIGN;
  value = reinterpret_cast<const uint8_t *>(context->stateValue.data());
  unknown = reinterpret_cast<const uint8_t *>(context->stateUnknown.data());
  return OBELISK_RT_OK;
}

// One traced canonical bit range. Several declared names may map onto it; the
// range is differenced and emitted once.
struct TraceVariable {
  uint64_t sourceBit = 0;
  uint64_t width = 0;
  // Index into VCDTraceState::codeText. Assigned in declaration order and
  // stable across the later sort into canonical-offset order.
  uint32_t code = 0;
  bool fourState = false;
  // Emitted as a VCD `r` record rather than a bit vector.
  bool isReal = false;
};

// One byte-aligned window of the canonical planes that covers a contiguous
// run of traced variables. The scan tests the window with memcmp and only
// descends to individual variables when the window moved.
struct TraceRange {
  uint64_t byteBegin = 0;
  uint64_t byteEnd = 0;
  uint32_t firstVariable = 0;
  uint32_t lastVariable = 0;
};

struct TraceSelection {
  std::string scope;
  uint64_t levels = 0;
};

} // namespace

// Defined outside the anonymous namespace so the context can hold a pointer to
// it without exposing the layout to every translation unit.
struct VCDTraceState {
  std::FILE *file = nullptr;
  std::string path;
  std::string buffer;
  bool timescaleSet = false;
  int32_t timescaleExponent = 0;

  std::vector<TraceSelection> selections;
  bool planBuilt = false;
  // A selection that never resolves is reported once and then abandoned, so a
  // mistyped scope cannot silently yield an empty waveform for the whole run.
  bool planFailed = false;
  bool headerWritten = false;
  bool pendingInitial = false;
  bool enabled = true;

  std::vector<TraceVariable> variables;
  // Identifier code per traced range, indexed by TraceVariable::code.
  std::vector<std::string> codeText;
  std::vector<TraceRange> ranges;
  std::vector<uint8_t> shadowValue;
  std::vector<uint8_t> shadowUnknown;

  uint64_t maxExpandedElements = kDefaultMaxExpandedElements;
  uint64_t limitBytes = 0;
  uint64_t writtenBytes = 0;
  bool limitReached = false;

  bool haveEmittedTime = false;
  uint64_t emittedTime = 0;

  // Reused across records so emission never allocates in steady state.
  std::string scratch;
};

namespace {

void appendRaw(VCDTraceState &state, const char *data, size_t size) {
  if (!state.file)
    return;
  state.buffer.append(data, size);
  if (state.buffer.size() >= kFlushThreshold) {
    std::fwrite(state.buffer.data(), 1, state.buffer.size(), state.file);
    state.buffer.clear();
  }
}

void append(VCDTraceState &state, std::string_view text) {
  if (state.limitReached)
    return;
  if (state.limitBytes != 0 &&
      state.writtenBytes + text.size() > state.limitBytes) {
    state.limitReached = true;
    return;
  }
  state.writtenBytes += text.size();
  appendRaw(state, text.data(), text.size());
}

void appendUnsigned(VCDTraceState &state, uint64_t value) {
  char digits[24];
  int length = std::snprintf(digits, sizeof(digits), "%llu",
                             static_cast<unsigned long long>(value));
  if (length > 0)
    append(state, std::string_view(digits, static_cast<size_t>(length)));
}

void appendSigned(VCDTraceState &state, int64_t value) {
  char digits[24];
  int length = std::snprintf(digits, sizeof(digits), "%lld",
                             static_cast<long long>(value));
  if (length > 0)
    append(state, std::string_view(digits, static_cast<size_t>(length)));
}

void flushBuffer(VCDTraceState &state) {
  if (state.file && !state.buffer.empty()) {
    std::fwrite(state.buffer.data(), 1, state.buffer.size(), state.file);
    state.buffer.clear();
  }
  if (state.file)
    std::fflush(state.file);
}

void closeFile(VCDTraceState &state) {
  if (!state.file)
    return;
  if (!state.buffer.empty()) {
    std::fwrite(state.buffer.data(), 1, state.buffer.size(), state.file);
    state.buffer.clear();
  }
  std::fclose(state.file);
  state.file = nullptr;
}

int32_t resolveTimescale(const obelisk_rt_context *context,
                         const VCDTraceState &state) {
  if (state.timescaleSet)
    return state.timescaleExponent;
  if (context->execution && context->execution->dpi_time_precision != 0)
    return context->execution->dpi_time_precision;
  // The elaborated default precision used by the compiler when a design
  // declares no timescale.
  return -9;
}

std::string timescaleText(int32_t exponent) {
  if (exponent > 0 || exponent < -15)
    exponent = -9;
  static const char *const units[] = {"s", "ms", "us", "ns", "ps", "fs"};
  int32_t magnitude = -exponent;
  const char *unit = units[magnitude / 3];
  switch (magnitude % 3) {
  case 0:
    return std::string("1") + unit;
  case 1:
    return std::string("100") + units[magnitude / 3 + 1];
  default:
    return std::string("10") + units[magnitude / 3 + 1];
  }
}

//===----------------------------------------------------------------------===//
// Plan construction
//===----------------------------------------------------------------------===//

// Resolve a selection to the scope or variable it names. An empty path selects
// the root. Records carry their full hierarchical name, so this is a direct
// match rather than a segment walk.
bool resolveSelection(const DesignDatabaseCache &database,
                      const std::string &path, uint64_t &offset,
                      uint32_t &kind) {
  if (path.empty()) {
    offset = database.root;
    kind = OBELISK_RT_DESIGN_RECORD_SCOPE;
    return true;
  }
  std::string_view wanted(path);
  if (wanted.rfind("$root.", 0) == 0)
    wanted.remove_prefix(6);
  auto find = [&](uint64_t begin, uint64_t count, uint64_t size) {
    for (uint64_t index = 0; index != count; ++index) {
      uint64_t candidate = begin + index * size;
      const uint8_t *record;
      uint32_t candidateKind;
      std::string_view name;
      if (!getRecord(database, candidate, record, candidateKind) ||
          !getString(database, nameOffset(record), name) || name != wanted)
        continue;
      if (candidateKind != OBELISK_RT_DESIGN_RECORD_SCOPE &&
          !traceableObject(candidateKind, record))
        continue;
      offset = candidate;
      kind = candidateKind;
      return true;
    }
    return false;
  };
  return find(database.scopes, database.scopeCount, kScopeRecordSize) ||
         find(database.objects, database.objectCount, kObjectRecordSize);
}

// Collect every traceable object at or below `scopeOffset`. `levels` follows
// the IEEE `$dumpvars` depth, where zero means unlimited and one means the
// named scope only.
void collectScope(const DesignDatabaseCache &database, uint64_t scopeOffset,
                  uint64_t levels, std::unordered_set<uint64_t> &objects) {
  struct Frame {
    uint64_t offset;
    uint64_t depth;
  };
  std::vector<Frame> pending{{scopeOffset, 1}};
  while (!pending.empty()) {
    Frame frame = pending.back();
    pending.pop_back();
    const uint8_t *record;
    uint32_t kind;
    if (!getRecord(database, frame.offset, record, kind) ||
        kind != OBELISK_RT_DESIGN_RECORD_SCOPE)
      continue;
    uint64_t child = firstChildOffset(record);
    while (child != 0) {
      const uint8_t *childRecord;
      uint32_t childKind;
      if (!getRecord(database, child, childRecord, childKind))
        break;
      if (childKind == OBELISK_RT_DESIGN_RECORD_SCOPE) {
        if (levels == 0 || frame.depth < levels)
          pending.push_back({child, frame.depth + 1});
      } else if (traceableObject(childKind, childRecord)) {
        objects.insert(child);
      }
      child = nextSiblingOffset(childRecord, childKind);
    }
  }
}

// Type record accessors. A type is reached through an object or through an
// enclosing array's element pointer, so these take the offset rather than the
// declaring object.
uint32_t typeKind(const DesignDatabaseCache &database, uint64_t typeOffset) {
  return read32(database.data + typeOffset + 4) & UINT32_C(0xff);
}

uint32_t typeFlags(const DesignDatabaseCache &database, uint64_t typeOffset) {
  return read32(database.data + typeOffset + 4) >> 8;
}

uint64_t typeWidth(const DesignDatabaseCache &database, uint64_t typeOffset) {
  return read64(database.data + typeOffset + 8);
}

int64_t typeLeft(const DesignDatabaseCache &database, uint64_t typeOffset) {
  return readI64(database.data + typeOffset + 16);
}

int64_t typeRight(const DesignDatabaseCache &database, uint64_t typeOffset) {
  return readI64(database.data + typeOffset + 24);
}

uint64_t typeElement(const DesignDatabaseCache &database, uint64_t typeOffset) {
  return read64(database.data + typeOffset + 32);
}

// f64 storage is serialized as a scalar type named "real"; nothing else in the
// type record distinguishes it from a 64-bit packed value.
bool typeIsReal(const DesignDatabaseCache &database, uint64_t typeOffset) {
  if (!isTypeOffset(database, typeOffset))
    return false;
  std::string_view name;
  return getString(database, read64(database.data + typeOffset + 72), name) &&
         name == "real";
}

bool typeFourState(const DesignDatabaseCache &database, uint64_t typeOffset) {
  if (!isTypeOffset(database, typeOffset))
    return true;
  return (typeFlags(database, typeOffset) &
          OBELISK_RT_DESIGN_TYPE_FOUR_STATE) != 0;
}

// An unpacked array occupies a contiguous canonical range but is not a value:
// each element is an independent signal and gets its own `$var`. A packed
// array is an ordinary vector and stays whole.
bool typeIsUnpackedArray(const DesignDatabaseCache &database,
                         uint64_t typeOffset) {
  return isTypeOffset(database, typeOffset) &&
         typeKind(database, typeOffset) == OBELISK_RT_DESIGN_TYPE_ARRAY &&
         (typeFlags(database, typeOffset) & OBELISK_RT_DESIGN_TYPE_PACKED) == 0;
}

// Write the `$scope`/`$var`/`$upscope` structure and assign identifier codes.
//
// IEEE 1364 lets several `$var` declarations share one identifier code when the
// variables always hold the same value. Declarations addressing the identical
// canonical range qualify unconditionally — they are the same storage — so they
// fold to one traced range with several `$var` lines, removing the duplicate
// from both the file and the per-slot difference. Merely-connected nets are not
// folded: that needs connectivity metadata only the bytecode tier carries, and
// would make one design dump differently per execution tier.
struct PlanBuilder {
  const obelisk_rt_context *context;
  VCDTraceState &state;
  const DesignDatabaseCache &database;
  const std::unordered_set<uint64_t> &selected;
  // Scopes on the path to at least one selected object.
  std::unordered_set<uint64_t> liveScopes;
  // Canonical (bit offset, width) to the traced range that already owns a code.
  std::map<std::pair<uint64_t, uint64_t>, uint32_t> codeByRange;

  void markLiveScopes() {
    for (uint64_t object : selected) {
      const uint8_t *record;
      uint32_t kind;
      if (!getRecord(database, object, record, kind))
        continue;
      uint64_t scope = parentOffset(record);
      while (scope != 0 && liveScopes.insert(scope).second) {
        const uint8_t *scopeRecord;
        uint32_t scopeKind;
        if (!getRecord(database, scope, scopeRecord, scopeKind))
          break;
        scope = parentOffset(scopeRecord);
      }
    }
  }

  // Determine whether expanding every unpacked dimension stays within one
  // object's declaration budget. Checking only each dimension admits an
  // exponential number of leaves for nested arrays.
  bool expandedLeafCount(uint64_t typeOffset, uint64_t width, unsigned depth,
                         uint64_t limit, uint64_t &count) const {
    if (depth == kMaxUnpackedDepth ||
        !typeIsUnpackedArray(database, typeOffset)) {
      count = 1;
      return true;
    }
    uint64_t element = typeElement(database, typeOffset);
    uint64_t elementWidth =
        isTypeOffset(database, element) ? typeWidth(database, element) : 0;
    int64_t left = typeLeft(database, typeOffset);
    int64_t right = typeRight(database, typeOffset);
    uint64_t extent = left >= right ? static_cast<uint64_t>(left - right) + 1
                                    : static_cast<uint64_t>(right - left) + 1;
    if (elementWidth == 0 || extent == 0 || extent > width / elementWidth ||
        extent * elementWidth != width) {
      count = 1;
      return true;
    }
    if (extent > limit)
      return false;
    uint64_t childCount = 0;
    if (!expandedLeafCount(element, elementWidth, depth + 1, limit / extent,
                           childCount))
      return false;
    count = extent * childCount;
    return true;
  }

  // Declare one traced range: a scalar, a vector, or one element of an
  // unpacked array. `name` is already the fully qualified leaf spelling.
  void declareLeaf(uint64_t typeOffset, uint64_t sourceBit, uint64_t width,
                   std::string_view name, bool isNet) {
    bool isReal = typeIsReal(database, typeOffset);
    auto [entry, inserted] =
        codeByRange.try_emplace(std::make_pair(sourceBit, width),
                                static_cast<uint32_t>(state.codeText.size()));
    uint32_t code = entry->second;
    bool fourState = !isReal && typeFourState(database, typeOffset);
    if (inserted) {
      TraceVariable variable;
      variable.sourceBit = sourceBit;
      variable.width = width;
      variable.code = code;
      variable.fourState = fourState;
      variable.isReal = isReal;
      state.variables.push_back(variable);
      state.codeText.push_back(identifierCode(code));
    } else if (fourState) {
      // Two names for one range may disagree on four-state-ness only if one is
      // a two-state view of four-state storage. Emit the wider interpretation
      // so an X deposited through the other name is still visible. Variables
      // are still in code order here; buildRanges sorts them afterwards.
      state.variables[code].fourState = true;
    }
    append(state, isReal ? "$var real " : isNet ? "$var wire " : "$var reg ");
    appendUnsigned(state, width);
    append(state, " ");
    append(state, state.codeText[code]);
    append(state, " ");
    append(state, name);
    // A real carries no bit range; a viewer reads its `r` records as numbers.
    // The declared range describes the bit extent only when it spans exactly
    // the traced width, so a range that enumerates anything else is replaced.
    if (width > 1 && !isReal) {
      int64_t left = 0;
      int64_t right = 0;
      uint64_t extent = 0;
      if (isTypeOffset(database, typeOffset)) {
        left = typeLeft(database, typeOffset);
        right = typeRight(database, typeOffset);
        extent = left >= right ? static_cast<uint64_t>(left - right) + 1
                               : static_cast<uint64_t>(right - left) + 1;
      }
      if (extent != width) {
        left = static_cast<int64_t>(width) - 1;
        right = 0;
      }
      append(state, " [");
      appendSigned(state, left);
      append(state, ":");
      appendSigned(state, right);
      append(state, "]");
    }
    append(state, " $end\n");
  }

  // Expand unpacked dimensions into one declaration per element, the form a
  // waveform viewer renders as an array. Element ordinal zero occupies the
  // lowest canonical bits and carries the leftmost declared index.
  void declareType(uint64_t typeOffset, uint64_t sourceBit, uint64_t width,
                   const std::string &name, bool isNet, unsigned depth) {
    if (depth == kMaxUnpackedDepth ||
        !typeIsUnpackedArray(database, typeOffset))
      return declareLeaf(typeOffset, sourceBit, width, name, isNet);
    uint64_t element = typeElement(database, typeOffset);
    uint64_t elementWidth =
        isTypeOffset(database, element) ? typeWidth(database, element) : 0;
    int64_t left = typeLeft(database, typeOffset);
    int64_t right = typeRight(database, typeOffset);
    uint64_t extent = left >= right ? static_cast<uint64_t>(left - right) + 1
                                    : static_cast<uint64_t>(right - left) + 1;
    if (elementWidth == 0 || extent == 0 || extent > width / elementWidth ||
        extent * elementWidth != width)
      return declareLeaf(typeOffset, sourceBit, width, name, isNet);
    int64_t step = right >= left ? 1 : -1;
    for (uint64_t ordinal = 0; ordinal != extent; ++ordinal) {
      std::string elementName = name;
      elementName.push_back('[');
      elementName.append(
          std::to_string(left + step * static_cast<int64_t>(ordinal)));
      elementName.push_back(']');
      declareType(element, sourceBit + ordinal * elementWidth, elementWidth,
                  elementName, isNet, depth + 1);
    }
  }

  void emitVariable(const uint8_t *record, std::string_view name,
                    std::string_view scopeName) {
    std::string leaf(leafName(name, scopeName));
    uint64_t leafCount = 0;
    if (!expandedLeafCount(objectTypeOffset(record), objectBitWidth(record), 0,
                           state.maxExpandedElements, leafCount)) {
      // Omit rather than smash. A single wide vector would be well-formed VCD
      // that silently presents several independent signals as one value, with
      // nothing in the file to say so.
      std::fprintf(stderr,
                   "obelisk: not dumping %s: the unpacked array exceeds the "
                   "waveform array limit of %llu "
                   "(raise OBELISK_RT_DUMP_MAX_ARRAY to include it)\n",
                   leaf.c_str(),
                   static_cast<unsigned long long>(state.maxExpandedElements));
      return;
    }
    declareType(objectTypeOffset(record), objectStateBit(record),
                objectBitWidth(record), leaf,
                read32(record) == OBELISK_RT_DESIGN_RECORD_NET, 0);
  }

  void emitScope(uint64_t scopeOffset, std::string_view parentName) {
    const uint8_t *record;
    uint32_t kind;
    if (!getRecord(database, scopeOffset, record, kind) ||
        kind != OBELISK_RT_DESIGN_RECORD_SCOPE)
      return;
    std::string_view name;
    if (!getString(database, nameOffset(record), name))
      return;
    append(state, "$scope module ");
    append(state, leafName(name, parentName));
    append(state, " $end\n");
    uint64_t child = firstChildOffset(record);
    while (child != 0) {
      const uint8_t *childRecord;
      uint32_t childKind;
      if (!getRecord(database, child, childRecord, childKind))
        break;
      if (childKind == OBELISK_RT_DESIGN_RECORD_SCOPE) {
        if (liveScopes.count(child) != 0)
          emitScope(child, name);
      } else if (selected.count(child) != 0) {
        std::string_view childName;
        if (getString(database, nameOffset(childRecord), childName))
          emitVariable(childRecord, childName, name);
      }
      child = nextSiblingOffset(childRecord, childKind);
    }
    append(state, "$upscope $end\n");
  }
};

// Coalesce the traced variables into byte-aligned windows and size the shadow
// planes. Variables are sorted by canonical offset so each window owns a
// contiguous index range.
void buildRanges(VCDTraceState &state, uint64_t stateBitCount) {
  std::sort(state.variables.begin(), state.variables.end(),
            [](const TraceVariable &left, const TraceVariable &right) {
              if (left.sourceBit != right.sourceBit)
                return left.sourceBit < right.sourceBit;
              return left.width < right.width;
            });
  uint64_t stateBytes = (stateBitCount + 7) / 8;
  state.shadowValue.assign(static_cast<size_t>(stateBytes), 0);
  state.shadowUnknown.assign(static_cast<size_t>(stateBytes), 0);
  state.ranges.clear();
  for (uint32_t index = 0; index != state.variables.size(); ++index) {
    const TraceVariable &variable = state.variables[index];
    uint64_t begin = variable.sourceBit / 8;
    uint64_t end = (variable.sourceBit + variable.width + 7) / 8;
    if (end > stateBytes)
      end = stateBytes;
    if (!state.ranges.empty() &&
        begin <= state.ranges.back().byteEnd + kRangeMergeGap) {
      TraceRange &last = state.ranges.back();
      last.byteEnd = std::max(last.byteEnd, end);
      last.lastVariable = index + 1;
      continue;
    }
    TraceRange range;
    range.byteBegin = begin;
    range.byteEnd = end;
    range.firstVariable = index;
    range.lastVariable = index + 1;
    state.ranges.push_back(range);
  }
}

//===----------------------------------------------------------------------===//
// Emission
//===----------------------------------------------------------------------===//

void emitTime(VCDTraceState &state, uint64_t time) {
  if (state.haveEmittedTime && state.emittedTime == time)
    return;
  append(state, "#");
  appendUnsigned(state, time);
  append(state, "\n");
  state.haveEmittedTime = true;
  state.emittedTime = time;
}

// Emit one value record. Vectors drop redundant leading characters under the
// IEEE left-extension rule: a leading `0` is always implied, a leading `x` or
// `z` repeats, and a leading `1` never may be dropped.
void emitValue(VCDTraceState &state, const TraceVariable &variable,
               const uint8_t *valuePlane, const uint8_t *unknownPlane) {
  if (variable.isReal) {
    uint64_t word = 0;
    for (uint64_t index = 0; index != variable.width && index != 64; ++index)
      if (bitAt(valuePlane, variable.sourceBit + index))
        word |= uint64_t{1} << index;
    double value = 0;
    std::memcpy(&value, &word, sizeof(value));
    char text[40];
    int length = std::snprintf(text, sizeof(text), "r%.16g", value);
    if (length > 0)
      append(state, std::string_view(text, static_cast<size_t>(length)));
    append(state, " ");
    append(state, state.codeText[variable.code]);
    append(state, "\n");
    return;
  }
  if (variable.width == 1) {
    char text[2] = {fourStateChar(bitAt(valuePlane, variable.sourceBit),
                                  variable.fourState &&
                                      bitAt(unknownPlane, variable.sourceBit)),
                    '\0'};
    append(state, std::string_view(text, 1));
    append(state, state.codeText[variable.code]);
    append(state, "\n");
    return;
  }
  std::string &bits = state.scratch;
  bits.clear();
  bits.reserve(static_cast<size_t>(variable.width));
  for (uint64_t index = variable.width; index-- != 0;) {
    uint64_t bit = variable.sourceBit + index;
    bits.push_back(
        fourStateChar(bitAt(valuePlane, bit),
                      variable.fourState && bitAt(unknownPlane, bit)));
  }
  size_t first = 0;
  char lead = bits[0];
  if (lead != '1') {
    while (first + 1 < bits.size() && bits[first + 1] == lead)
      ++first;
    if (lead == '0')
      while (first + 1 < bits.size() && bits[first] == '0')
        ++first;
  }
  append(state, "b");
  append(state, std::string_view(bits).substr(first));
  append(state, " ");
  append(state, state.codeText[variable.code]);
  append(state, "\n");
}

// `$dumpoff` records every variable as X without disturbing the shadow, so the
// following `$dumpon` republishes real values rather than a difference against
// the checkpoint.
void emitAllX(VCDTraceState &state) {
  for (const TraceVariable &variable : state.variables) {
    // VCD has no unknown encoding for a real, so a suspended real simply keeps
    // its last recorded value until $dumpon republishes it.
    if (variable.isReal)
      continue;
    if (variable.width == 1) {
      append(state, "x");
      append(state, state.codeText[variable.code]);
      append(state, "\n");
      continue;
    }
    append(state, "bx ");
    append(state, state.codeText[variable.code]);
    append(state, "\n");
  }
}

} // namespace

//===----------------------------------------------------------------------===//
// Context-facing entry points
//===----------------------------------------------------------------------===//

namespace {

VCDTraceState *traceState(obelisk_rt_context *context) {
  return context ? context->vcdState : nullptr;
}

obelisk_rt_status ensureState(obelisk_rt_context *context,
                              VCDTraceState *&state) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  if (!context->vcdState) {
    context->vcdState = new (std::nothrow) VCDTraceState();
    if (!context->vcdState)
      return OBELISK_RT_OUT_OF_MEMORY;
    // The traced shape is chosen at run time by $dumpvars, so the bound on
    // array expansion belongs here rather than in the compiled image.
    if (const char *limit = std::getenv("OBELISK_RT_DUMP_MAX_ARRAY")) {
      char *end = nullptr;
      unsigned long long parsed = std::strtoull(limit, &end, 10);
      if (end && *end == '\0')
        context->vcdState->maxExpandedElements = parsed;
    }
  }
  state = context->vcdState;
  return OBELISK_RT_OK;
}

obelisk_rt_status openFile(obelisk_rt_context *context, VCDTraceState &state,
                           const std::string &path) {
  closeFile(state);
  state.file = std::fopen(path.c_str(), "wb");
  if (!state.file)
    return OBELISK_RT_IO_ERROR;
  state.path = path;
  state.writtenBytes = 0;
  state.limitReached = false;
  state.buffer.clear();
  (void)context;
  return OBELISK_RT_OK;
}

void writeHeader(obelisk_rt_context *context, VCDTraceState &state) {
  if (state.headerWritten)
    return;
  state.headerWritten = true;
  std::time_t now = std::time(nullptr);
  char stamp[64];
  stamp[0] = '\0';
  std::tm broken{};
#if defined(_WIN32)
  if (localtime_s(&broken, &now) == 0)
#else
  if (localtime_r(&now, &broken) != nullptr)
#endif
    std::strftime(stamp, sizeof(stamp), "%a %b %e %H:%M:%S %Y", &broken);
  append(state, "$date\n\t");
  append(state, stamp);
  append(state, "\n$end\n");
  append(state, "$version\n\tObelisk\n$end\n");
  append(state, "$timescale\n\t");
  append(state, timescaleText(resolveTimescale(context, state)));
  append(state, "\n$end\n");
}

// Materialize the traced set, its VCD declarations, and the shadow planes.
obelisk_rt_status buildPlan(obelisk_rt_context *context, VCDTraceState &state) {
  if (state.planBuilt)
    return OBELISK_RT_OK;
  const DesignDatabaseCache &database = context->designDatabase;
  if (!database.validated || !context->execution)
    return OBELISK_RT_INVALID_DESIGN;

  std::unordered_set<uint64_t> selected;
  for (const TraceSelection &selection : state.selections) {
    uint64_t offset = 0;
    uint32_t kind = OBELISK_RT_DESIGN_RECORD_INVALID;
    if (!resolveSelection(database, selection.scope, offset, kind)) {
      std::fprintf(stderr,
                   "obelisk: $dumpvars selection '%s' does not name a design "
                   "scope or variable; no waveform will be written\n",
                   selection.scope.c_str());
      return OBELISK_RT_INVALID_HANDLE;
    }
    if (kind == OBELISK_RT_DESIGN_RECORD_SCOPE)
      collectScope(database, offset, selection.levels, selected);
    else
      selected.insert(offset);
  }

  writeHeader(context, state);
  PlanBuilder builder{context, state, database, selected, {}, {}};
  builder.markLiveScopes();
  builder.emitScope(database.root, {});
  append(state, "$enddefinitions $end\n");

  buildRanges(state, context->execution->state_bit_count);
  state.planBuilt = true;
  state.pendingInitial = true;
  return OBELISK_RT_OK;
}

// Difference the traced windows against the shadow and emit what moved.
obelisk_rt_status emitSlot(obelisk_rt_context *context, VCDTraceState &state,
                           bool forceAll) {
  if (!state.file || state.limitReached || !state.planBuilt)
    return OBELISK_RT_OK;
  const uint8_t *valuePlane = nullptr;
  const uint8_t *unknownPlane = nullptr;
  obelisk_rt_status status = dumpPlanes(context, valuePlane, unknownPlane);
  if (status != OBELISK_RT_OK)
    return status;

  uint64_t time = context->schedulerTime;

  if (state.pendingInitial) {
    emitTime(state, time);
    append(state, "$dumpvars\n");
    for (const TraceVariable &variable : state.variables)
      emitValue(state, variable, valuePlane, unknownPlane);
    append(state, "$end\n");
    for (const TraceRange &range : state.ranges) {
      size_t length = static_cast<size_t>(range.byteEnd - range.byteBegin);
      std::memcpy(state.shadowValue.data() + range.byteBegin,
                  valuePlane + range.byteBegin, length);
      std::memcpy(state.shadowUnknown.data() + range.byteBegin,
                  unknownPlane + range.byteBegin, length);
    }
    state.pendingInitial = false;
    return OBELISK_RT_OK;
  }

  if (!state.enabled)
    return OBELISK_RT_OK;

  for (const TraceRange &range : state.ranges) {
    size_t length = static_cast<size_t>(range.byteEnd - range.byteBegin);
    if (length == 0)
      continue;
    bool moved = forceAll ||
                 std::memcmp(state.shadowValue.data() + range.byteBegin,
                             valuePlane + range.byteBegin, length) != 0 ||
                 std::memcmp(state.shadowUnknown.data() + range.byteBegin,
                             unknownPlane + range.byteBegin, length) != 0;
    if (!moved)
      continue;
    for (uint32_t index = range.firstVariable; index != range.lastVariable;
         ++index) {
      const TraceVariable &variable = state.variables[index];
      if (!forceAll &&
          bitRangeEqual(state.shadowValue.data(), valuePlane,
                        variable.sourceBit, variable.width) &&
          (!variable.fourState ||
           bitRangeEqual(state.shadowUnknown.data(), unknownPlane,
                         variable.sourceBit, variable.width)))
        continue;
      emitTime(state, time);
      emitValue(state, variable, valuePlane, unknownPlane);
    }
    std::memcpy(state.shadowValue.data() + range.byteBegin,
                valuePlane + range.byteBegin, length);
    std::memcpy(state.shadowUnknown.data() + range.byteBegin,
                unknownPlane + range.byteBegin, length);
  }
  if (state.limitReached)
    closeFile(state);
  return OBELISK_RT_OK;
}

} // namespace

obelisk_rt_status obelisk_rt_dump_slot_unlocked(obelisk_rt_context *context) {
  VCDTraceState *state = traceState(context);
  if (!state || !state->file)
    return OBELISK_RT_OK;
  // The plan is materialized at the end of the slot that made the first
  // selection, so every $dumpvars issued during that slot is accounted for
  // before the header is closed.
  if (!state->planBuilt) {
    if (state->planFailed || state->selections.empty())
      return OBELISK_RT_OK;
    obelisk_rt_status status = buildPlan(context, *state);
    if (status != OBELISK_RT_OK) {
      state->planFailed = true;
      closeFile(*state);
      return status;
    }
  }
  return emitSlot(context, *state, false);
}

uint64_t obelisk_rt_dump_traced_range_count(const obelisk_rt_context *context) {
  if (!context || !context->vcdState)
    return 0;
  return context->vcdState->variables.size();
}

bool obelisk_rt_dump_active_unlocked(const obelisk_rt_context *context) {
  return context && context->vcdState && context->vcdState->file != nullptr;
}

void obelisk_rt_dump_destroy(obelisk_rt_context *context) noexcept {
  if (!context || !context->vcdState)
    return;
  VCDTraceState *state = context->vcdState;
  try {
    if (state->file && state->planBuilt)
      (void)emitSlot(context, *state, false);
  } catch (...) {
    // Teardown must not propagate; the file is still closed below.
  }
  closeFile(*state);
  delete state;
  context->vcdState = nullptr;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_dump_open(obelisk_rt_context *context, const uint8_t *path,
                        uint64_t pathSize) {
  if (!context || (!path && pathSize != 0) || pathSize == 0)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  try {
    ContextMutexLock lock(context);
    VCDTraceState *state = nullptr;
    obelisk_rt_status status = ensureState(context, state);
    if (status != OBELISK_RT_OK)
      return status;
    // IEEE 1800 leaves a second $dumpfile after dumping has started
    // unspecified; reject it rather than silently splitting the waveform.
    if (state->planBuilt)
      return OBELISK_RT_INVALID_LIFECYCLE;
    return openFile(context, *state,
                    std::string(reinterpret_cast<const char *>(path),
                                static_cast<size_t>(pathSize)));
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_IO_ERROR;
  }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_dump_open_string(obelisk_rt_context *context,
                               obelisk_rt_string_v1 path) {
  char scratch[8] = {};
  const char *bytes = nullptr;
  uint64_t size = 0;
  obelisk_rt_status status =
      obelisk_rt_v1_string_view(path, scratch, &bytes, &size);
  return status == OBELISK_RT_OK
             ? obelisk_rt_v1_dump_open(
                   context, reinterpret_cast<const uint8_t *>(bytes), size)
             : status;
}

extern "C" obelisk_rt_status
obelisk_rt_v1_dump_timescale(obelisk_rt_context *context, int32_t exponent) {
  if (!context || exponent > 0 || exponent < -15)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  try {
    ContextMutexLock lock(context);
    VCDTraceState *state = nullptr;
    obelisk_rt_status status = ensureState(context, state);
    if (status != OBELISK_RT_OK)
      return status;
    if (state->headerWritten)
      return OBELISK_RT_INVALID_LIFECYCLE;
    state->timescaleSet = true;
    state->timescaleExponent = exponent;
    return OBELISK_RT_OK;
  } catch (...) {
    return OBELISK_RT_INVALID_ARGUMENT;
  }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_dump_vars(obelisk_rt_context *context, uint64_t levels,
                        const uint8_t *scope, uint64_t scopeSize) {
  if (!context || (!scope && scopeSize != 0))
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  try {
    ContextMutexLock lock(context);
    VCDTraceState *state = nullptr;
    obelisk_rt_status status = ensureState(context, state);
    if (status != OBELISK_RT_OK)
      return status;
    if (!context->designDatabase.validated)
      return OBELISK_RT_INVALID_DESIGN;
    // Every selection must be registered before the plan is materialized,
    // because a VCD header is complete before the first value record.
    if (state->planBuilt)
      return OBELISK_RT_INVALID_LIFECYCLE;
    if (!state->file) {
      status = openFile(context, *state, "dump.vcd");
      if (status != OBELISK_RT_OK)
        return status;
    }
    TraceSelection selection;
    selection.levels = levels;
    if (scopeSize != 0)
      selection.scope.assign(reinterpret_cast<const char *>(scope),
                             static_cast<size_t>(scopeSize));
    state->selections.push_back(std::move(selection));
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_INVALID_ARGUMENT;
  }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_dump_all(obelisk_rt_context *context) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  try {
    ContextMutexLock lock(context);
    VCDTraceState *state = traceState(context);
    if (!state || !state->file ||
        (!state->planBuilt && state->selections.empty()))
      return OBELISK_RT_OK;
    obelisk_rt_status status = buildPlan(context, *state);
    if (status != OBELISK_RT_OK)
      return status;
    return emitSlot(context, *state, true);
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_IO_ERROR;
  }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_dump_control(obelisk_rt_context *context, uint32_t enabled) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  try {
    ContextMutexLock lock(context);
    VCDTraceState *state = traceState(context);
    if (!state || !state->file ||
        (!state->planBuilt && state->selections.empty()))
      return OBELISK_RT_OK;
    obelisk_rt_status status = buildPlan(context, *state);
    if (status != OBELISK_RT_OK)
      return status;
    bool wanted = enabled != 0;
    if (wanted == state->enabled)
      return OBELISK_RT_OK;
    // Settle the slot under the previous mode before switching.
    status = emitSlot(context, *state, false);
    if (status != OBELISK_RT_OK)
      return status;
    state->enabled = wanted;
    emitTime(*state, context->schedulerTime);
    if (!wanted) {
      append(*state, "$dumpoff\n");
      emitAllX(*state);
      append(*state, "$end\n");
      return OBELISK_RT_OK;
    }
    const uint8_t *valuePlane = nullptr;
    const uint8_t *unknownPlane = nullptr;
    status = dumpPlanes(context, valuePlane, unknownPlane);
    if (status != OBELISK_RT_OK)
      return status;
    append(*state, "$dumpon\n");
    for (const TraceVariable &variable : state->variables)
      emitValue(*state, variable, valuePlane, unknownPlane);
    append(*state, "$end\n");
    for (const TraceRange &range : state->ranges) {
      size_t length = static_cast<size_t>(range.byteEnd - range.byteBegin);
      std::memcpy(state->shadowValue.data() + range.byteBegin,
                  valuePlane + range.byteBegin, length);
      std::memcpy(state->shadowUnknown.data() + range.byteBegin,
                  unknownPlane + range.byteBegin, length);
    }
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  } catch (...) {
    return OBELISK_RT_IO_ERROR;
  }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_dump_limit(obelisk_rt_context *context, uint64_t bytes) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  try {
    ContextMutexLock lock(context);
    VCDTraceState *state = nullptr;
    obelisk_rt_status status = ensureState(context, state);
    if (status != OBELISK_RT_OK)
      return status;
    state->limitBytes = bytes;
    return OBELISK_RT_OK;
  } catch (...) {
    return OBELISK_RT_INVALID_ARGUMENT;
  }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_dump_flush(obelisk_rt_context *context) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  try {
    ContextMutexLock lock(context);
    VCDTraceState *state = traceState(context);
    if (!state)
      return OBELISK_RT_OK;
    flushBuffer(*state);
    return OBELISK_RT_OK;
  } catch (...) {
    return OBELISK_RT_IO_ERROR;
  }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_dump_close(obelisk_rt_context *context) {
  if (!context)
    return OBELISK_RT_INVALID_ARGUMENT;
  ContextTransaction transaction(context);
  try {
    ContextMutexLock lock(context);
    VCDTraceState *state = traceState(context);
    if (!state)
      return OBELISK_RT_OK;
    if (state->file && state->planBuilt)
      (void)emitSlot(context, *state, false);
    closeFile(*state);
    return OBELISK_RT_OK;
  } catch (...) {
    return OBELISK_RT_IO_ERROR;
  }
}
