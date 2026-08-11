//===- VCDTest.cpp - Waveform dump tests ---------------------------------===//

#include "../lib/RuntimeInternal.h"
#include "obelisk/Runtime/Runtime.h"
#include "obelisk/Runtime/StableHash.h"

#include "gtest/gtest.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

void put32(std::vector<uint8_t> &bytes, size_t offset, uint32_t value) {
  for (unsigned index = 0; index != 4; ++index)
    bytes[offset + index] = static_cast<uint8_t>(value >> (index * 8));
}

void put64(std::vector<uint8_t> &bytes, size_t offset, uint64_t value) {
  for (unsigned index = 0; index != 8; ++index)
    bytes[offset + index] = static_cast<uint8_t>(value >> (index * 8));
}

uint64_t nameHash(const std::string &name) {
  return obelisk_stable_hash(reinterpret_cast<const uint8_t *>(name.data()),
                             name.size());
}

uint64_t imageChecksum(const std::vector<uint8_t> &bytes) {
  uint64_t hash = OBELISK_STABLE_HASH_OFFSET_BASIS;
  for (size_t index = 0; index != bytes.size(); ++index)
    hash = obelisk_stable_hash_append_byte(
        hash, index >= 32 && index < 40 ? uint8_t{0} : bytes[index]);
  return hash;
}

// Canonical bit layout of the test design.
constexpr uint64_t kClkBit = 0;
constexpr uint64_t kDataBit = 8;
constexpr uint64_t kCountBit = 16;
constexpr uint64_t kRealBit = 64;
constexpr uint64_t kStructBit = 24;
constexpr uint64_t kArrayBit = 32;
constexpr uint64_t kStateBits = 128;

// A two-level design whose `bus` net and `data` variable share one canonical
// range, so they must collapse onto a single VCD identifier code.
//
//   top          clk   : logic       1 bit  @0
//                data  : logic [7:0] 8 bits @8
//                bus   : logic [7:0] 8 bits @8   (alias of data)
//   top.sub      count : logic [3:0] 4 bits @16
//   top          rv    : real        64 bits @64
//                \\x.y  : logic       1 bit  @23  (escaped name with a dot)
//                st    : packed struct of two nibbles, 8 bits  @24
//                mem   : logic [3:0] [0:2] unpacked, 12 bits   @32
struct DesignImage {
  std::vector<uint8_t> bytes;

  DesignImage() {
    constexpr uint64_t scopeOffset = 128;
    constexpr uint64_t subScopeOffset = scopeOffset + 64;
    constexpr uint64_t objectOffset = subScopeOffset + 64;
    constexpr uint64_t clkOffset = objectOffset;
    constexpr uint64_t dataOffset = objectOffset + 96;
    constexpr uint64_t busOffset = objectOffset + 192;
    constexpr uint64_t countOffset = objectOffset + 288;
    constexpr uint64_t realOffset = objectOffset + 384;
    constexpr uint64_t escapedOffset = objectOffset + 480;
    constexpr uint64_t structOffset = objectOffset + 576;
    constexpr uint64_t arrayOffset = objectOffset + 672;
    constexpr uint64_t typeOffset = objectOffset + 768;
    constexpr uint64_t scalarType = typeOffset;
    constexpr uint64_t byteType = typeOffset + 80;
    constexpr uint64_t nibbleType = typeOffset + 160;
    constexpr uint64_t realType = typeOffset + 240;
    constexpr uint64_t structType = typeOffset + 320;
    // A struct's field records must be contiguous and in ordinal order.
    constexpr uint64_t field0Type = typeOffset + 400;
    constexpr uint64_t field1Type = typeOffset + 480;
    constexpr uint64_t arrayType = typeOffset + 560;
    constexpr uint64_t stringOffset = typeOffset + 640;

    // Records carry the full hierarchical path; VCD declares the leaf.
    const std::string strings =
        std::string("top\0top.sub\0top.clk\0top.data\0top.bus\0top.sub.count\0top.rv\0top.\\x.y \0top.st\0top.mem\0logic\0real\0pair\0hi\0lo\0nibbles\0", 113);
    // Name offsets stored in records are absolute image offsets.
    const uint64_t topName = stringOffset + 0;
    const uint64_t subName = stringOffset + 4;
    const uint64_t clkName = stringOffset + 12;
    const uint64_t dataName = stringOffset + 20;
    const uint64_t busName = stringOffset + 29;
    const uint64_t countName = stringOffset + 37;
    const uint64_t realName = stringOffset + 51;
    const uint64_t structName = stringOffset + 68;
    const uint64_t arrayName = stringOffset + 75;
    const uint64_t pairTypeName = stringOffset + 94;
    const uint64_t hiName = stringOffset + 99;
    const uint64_t loName = stringOffset + 102;
    const uint64_t nibblesTypeName = stringOffset + 105;
    const uint64_t escapedName = stringOffset + 58;
    const uint64_t logicName = stringOffset + 83;
    const uint64_t realTypeName = stringOffset + 89;

    // The index is read with byte-wise loads, but keeping it aligned mirrors
    // what the compiler emits.
    const uint64_t indexOffset = (stringOffset + strings.size() + 7) / 8 * 8;
    struct Entry {
      std::string name;
      uint64_t nameOffset;
      uint64_t record;
    };
    std::vector<Entry> index{
        {"top", topName, scopeOffset},
        {"top.sub", subName, subScopeOffset},
        {"top.clk", clkName, clkOffset},
        {"top.data", dataName, dataOffset},
        {"top.bus", busName, busOffset},
        {"top.sub.count", countName, countOffset},
        {"top.rv", realName, realOffset},
        {"top.\\x.y ", escapedName, escapedOffset},
        {"top.st", structName, structOffset},
        {"top.mem", arrayName, arrayOffset},
    };
    std::sort(index.begin(), index.end(),
              [](const Entry &left, const Entry &right) {
                uint64_t leftHash = nameHash(left.name);
                uint64_t rightHash = nameHash(right.name);
                if (leftHash != rightHash)
                  return leftHash < rightHash;
                return left.name < right.name;
              });

    bytes.assign(static_cast<size_t>(indexOffset + index.size() * 24), 0);
    std::memcpy(bytes.data(), "OBDSGN1\0", 8);
    put32(bytes, 8, OBELISK_RT_VERSION);
    put32(bytes, 16, OBELISK_RT_DESIGN_PROFILE_READ);
    put32(bytes, 20, 128);
    put64(bytes, 24, bytes.size());
    put64(bytes, 40, scopeOffset);
    put64(bytes, 48, scopeOffset);
    put64(bytes, 56, 2);
    put64(bytes, 64, objectOffset);
    put64(bytes, 72, 8);
    put64(bytes, 80, typeOffset);
    put64(bytes, 88, 8);
    put64(bytes, 96, stringOffset);
    put64(bytes, 104, strings.size());
    put64(bytes, 112, indexOffset);
    put64(bytes, 120, index.size());

    // top: children are clk, data, bus, then the sub scope.
    put32(bytes, scopeOffset, OBELISK_RT_DESIGN_RECORD_SCOPE);
    put32(bytes, scopeOffset + 4, OBELISK_RT_DESIGN_CAP_ITERATE);
    put64(bytes, scopeOffset + 8, 1);
    put64(bytes, scopeOffset + 24, clkOffset);
    put64(bytes, scopeOffset + 40, topName);

    put32(bytes, subScopeOffset, OBELISK_RT_DESIGN_RECORD_SCOPE);
    put32(bytes, subScopeOffset + 4, OBELISK_RT_DESIGN_CAP_ITERATE);
    put64(bytes, subScopeOffset + 8, 2);
    put64(bytes, subScopeOffset + 16, scopeOffset);
    put64(bytes, subScopeOffset + 24, countOffset);
    put64(bytes, subScopeOffset + 32, realOffset);
    put64(bytes, subScopeOffset + 40, subName);

    auto object = [&](uint64_t offset, uint32_t kind, uint64_t stableID,
                      uint64_t parent, uint64_t next, uint64_t name,
                      uint64_t type, uint64_t width, int64_t left,
                      int64_t right, uint64_t stateBit) {
      put32(bytes, offset, kind);
      put32(bytes, offset + 4, OBELISK_RT_DESIGN_CAP_READ);
      put64(bytes, offset + 8, stableID);
      put64(bytes, offset + 16, parent);
      put64(bytes, offset + 24, next);
      put64(bytes, offset + 40, name);
      put64(bytes, offset + 48, type);
      put64(bytes, offset + 56, width);
      put64(bytes, offset + 64, static_cast<uint64_t>(left));
      put64(bytes, offset + 72, static_cast<uint64_t>(right));
      put64(bytes, offset + 80, stateBit);
    };
    object(clkOffset, OBELISK_RT_DESIGN_RECORD_NET, 1, scopeOffset, dataOffset,
           clkName, scalarType, 1, 0, 0, kClkBit);
    object(dataOffset, OBELISK_RT_DESIGN_RECORD_STORAGE, 2, scopeOffset,
           busOffset, dataName, byteType, 8, 7, 0, kDataBit);
    object(busOffset, OBELISK_RT_DESIGN_RECORD_NET, 3, scopeOffset,
           subScopeOffset, busName, byteType, 8, 7, 0, kDataBit);
    object(countOffset, OBELISK_RT_DESIGN_RECORD_STORAGE, 4, subScopeOffset, 0,
           countName, nibbleType, 4, 3, 0, kCountBit);
    object(realOffset, OBELISK_RT_DESIGN_RECORD_STORAGE, 5, scopeOffset,
           escapedOffset, realName, realType, 64, 63, 0, kRealBit);
    object(escapedOffset, OBELISK_RT_DESIGN_RECORD_STORAGE, 6, scopeOffset,
           structOffset, escapedName, scalarType, 1, 0, 0, 23);
    object(structOffset, OBELISK_RT_DESIGN_RECORD_STORAGE, 7, scopeOffset,
           arrayOffset, structName, structType, 8, 7, 0, kStructBit);
    // An unpacked array's declared range enumerates elements, so its bounds
    // deliberately disagree with its 12-bit extent.
    object(arrayOffset, OBELISK_RT_DESIGN_RECORD_STORAGE, 8, scopeOffset, 0,
           arrayName, arrayType, 12, 0, 2, kArrayBit);

    auto type = [&](uint64_t offset, uint64_t width, int64_t left) {
      put32(bytes, offset, OBELISK_RT_DESIGN_RECORD_TYPE);
      put32(bytes, offset + 4,
            OBELISK_RT_DESIGN_TYPE_SCALAR |
                ((OBELISK_RT_DESIGN_TYPE_FOUR_STATE |
                  OBELISK_RT_DESIGN_TYPE_PACKED)
                 << 8));
      put64(bytes, offset + 8, width);
      put64(bytes, offset + 16, static_cast<uint64_t>(left));
      put64(bytes, offset + 72, logicName);
    };
    type(scalarType, 1, 0);
    type(byteType, 8, 7);
    type(nibbleType, 4, 3);
    // A real is a scalar type whose only distinguishing mark is its name.
    put32(bytes, realType, OBELISK_RT_DESIGN_RECORD_TYPE);
    put32(bytes, realType + 4, OBELISK_RT_DESIGN_TYPE_SCALAR);
    put64(bytes, realType + 8, 64);
    put64(bytes, realType + 16, 63);
    put64(bytes, realType + 72, realTypeName);

    // A packed struct of two four-state nibbles.
    put32(bytes, structType, OBELISK_RT_DESIGN_RECORD_TYPE);
    put32(bytes, structType + 4,
          OBELISK_RT_DESIGN_TYPE_STRUCT |
              ((OBELISK_RT_DESIGN_TYPE_FOUR_STATE |
                OBELISK_RT_DESIGN_TYPE_PACKED)
               << 8));
    put64(bytes, structType + 8, 8);
    put64(bytes, structType + 16, 7);
    put64(bytes, structType + 40, field0Type);
    put64(bytes, structType + 48, 2);
    put64(bytes, structType + 72, pairTypeName);
    auto field = [&](uint64_t offset, uint64_t ordinal, uint64_t packedOffset,
                     uint64_t name) {
      put32(bytes, offset, OBELISK_RT_DESIGN_RECORD_TYPE);
      put32(bytes, offset + 4,
            OBELISK_RT_DESIGN_TYPE_FIELD |
                (OBELISK_RT_DESIGN_TYPE_FOUR_STATE << 8));
      put64(bytes, offset + 8, 4);
      put64(bytes, offset + 16, 3);
      put64(bytes, offset + 32, nibbleType);
      put64(bytes, offset + 56, ordinal);
      put64(bytes, offset + 64, packedOffset);
      put64(bytes, offset + 72, name);
    };
    field(field0Type, 0, 4, hiName);
    field(field1Type, 1, 0, loName);

    // An unpacked array of three nibbles: three elements, twelve bits.
    put32(bytes, arrayType, OBELISK_RT_DESIGN_RECORD_TYPE);
    put32(bytes, arrayType + 4,
          OBELISK_RT_DESIGN_TYPE_ARRAY |
              (OBELISK_RT_DESIGN_TYPE_FOUR_STATE << 8));
    put64(bytes, arrayType + 8, 12);
    put64(bytes, arrayType + 16, 0);
    put64(bytes, arrayType + 24, 2);
    put64(bytes, arrayType + 32, nibbleType);
    put64(bytes, arrayType + 72, nibblesTypeName);

    std::memcpy(bytes.data() + stringOffset, strings.data(), strings.size());
    for (size_t entry = 0; entry != index.size(); ++entry) {
      put64(bytes, indexOffset + entry * 24, nameHash(index[entry].name));
      put64(bytes, indexOffset + entry * 24 + 8, index[entry].nameOffset);
      put64(bytes, indexOffset + entry * 24 + 16, index[entry].record);
    }
    put64(bytes, 32, imageChecksum(bytes));
  }
};

struct Fixture {
  DesignImage image;
  obelisk_rt_execution_descriptor_v1 execution{};
  obelisk_rt_context *context = nullptr;
  std::string path;

  Fixture() {
    execution.version = OBELISK_RT_VERSION;
    execution.flags = OBELISK_RT_EXECUTION_HAS_DESIGN_DATABASE |
                      OBELISK_RT_EXECUTION_WAVEFORM_METADATA;
    execution.design_database = image.bytes.data();
    execution.design_database_size = image.bytes.size();
    execution.state_bit_count = kStateBits;
    path = std::string(std::tmpnam(nullptr)) + ".vcd";
  }

  ~Fixture() {
    if (context)
      obelisk_rt_v1_context_destroy(context);
    std::remove(path.c_str());
  }

  obelisk_rt_status create() {
    return obelisk_rt_v1_context_create_for_design(&execution, &context);
  }

  void setBits(uint64_t bit, uint64_t width, uint64_t value, uint64_t unknown) {
    for (uint64_t index = 0; index != width; ++index) {
      uint64_t absolute = bit + index;
      uint64_t mask = uint64_t{1} << (absolute % 64);
      if ((value >> index) & 1)
        context->stateValue[absolute / 64] |= mask;
      else
        context->stateValue[absolute / 64] &= ~mask;
      if ((unknown >> index) & 1)
        context->stateUnknown[absolute / 64] |= mask;
      else
        context->stateUnknown[absolute / 64] &= ~mask;
    }
  }

  // Close the slot that is ending and move to the next one, mirroring what the
  // scheduler does around each time advance.
  void advanceTo(uint64_t time) {
    ASSERT_EQ(obelisk_rt_dump_slot_unlocked(context), OBELISK_RT_OK);
    context->schedulerTime = time;
  }

  std::string read() {
    obelisk_rt_v1_dump_close(context);
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream text;
    text << stream.rdbuf();
    return text.str();
  }

  obelisk_rt_status openDump() {
    return obelisk_rt_v1_dump_open(
        context, reinterpret_cast<const uint8_t *>(path.data()), path.size());
  }
};

size_t countOccurrences(const std::string &text, const std::string &needle) {
  size_t count = 0;
  for (size_t at = text.find(needle); at != std::string::npos;
       at = text.find(needle, at + needle.size()))
    ++count;
  return count;
}

std::string identifierFor(const std::string &text, const std::string &name) {
  // "$var reg 8 " <code> " data [7:0] $end"
  std::istringstream lines(text);
  std::string line;
  while (std::getline(lines, line)) {
    if (line.rfind("$var ", 0) != 0)
      continue;
    std::istringstream fields(line);
    std::string keyword, kind, width, code, variable;
    fields >> keyword >> kind >> width >> code >> variable;
    if (variable == name)
      return code;
  }
  return {};
}

// Every value record naming `code`, in order, taken from the body of the dump.
// A scalar record is "<char><code>"; a vector record is "b<bits> <code>".
std::vector<std::string> valueRecords(const std::string &text,
                                      const std::string &code) {
  std::vector<std::string> records;
  size_t body = text.find("$enddefinitions $end");
  if (body == std::string::npos)
    return records;
  std::istringstream lines(text.substr(body));
  std::string line;
  while (std::getline(lines, line)) {
    if (line.empty() || line[0] == '$' || line[0] == '#')
      continue;
    if (line[0] == 'b' || line[0] == 'r') {
      size_t space = line.rfind(' ');
      if (space != std::string::npos && line.substr(space + 1) == code)
        records.push_back(line.substr(0, space));
      continue;
    }
    if (line.substr(1) == code)
      records.push_back(line.substr(0, 1));
  }
  return records;
}

// Names declared in the header, in order, that use `code`.
std::vector<std::string> namesUsingCode(const std::string &text,
                                        const std::string &code) {
  std::vector<std::string> names;
  std::istringstream lines(text);
  std::string line;
  while (std::getline(lines, line)) {
    if (line.rfind("$var ", 0) != 0)
      continue;
    std::istringstream fields(line);
    std::string keyword, kind, width, entry, variable;
    fields >> keyword >> kind >> width >> entry >> variable;
    if (entry == code)
      names.push_back(variable);
  }
  return names;
}

TEST(VCD, HeaderDeclaresHierarchy) {
  Fixture fixture;
  ASSERT_EQ(fixture.create(), OBELISK_RT_OK);
  ASSERT_EQ(fixture.openDump(), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_dump_timescale(fixture.context, -9), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_dump_vars(fixture.context, 0, nullptr, 0),
            OBELISK_RT_OK);
  fixture.advanceTo(10);
  std::string text = fixture.read();

  EXPECT_NE(text.find("$timescale\n\t1ns\n$end\n"), std::string::npos);
  EXPECT_NE(text.find("$scope module top $end"), std::string::npos);
  EXPECT_NE(text.find("$scope module sub $end"), std::string::npos);
  EXPECT_EQ(countOccurrences(text, "$upscope $end"), 2u);
  EXPECT_NE(text.find("$enddefinitions $end"), std::string::npos);
  EXPECT_NE(text.find("$var wire 1 "), std::string::npos);
  EXPECT_NE(text.find("$var reg 4 "), std::string::npos);
}

TEST(VCD, TimescaleFallsBackToTheDesignDefault) {
  Fixture fixture;
  ASSERT_EQ(fixture.create(), OBELISK_RT_OK);
  ASSERT_EQ(fixture.openDump(), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_dump_vars(fixture.context, 0, nullptr, 0),
            OBELISK_RT_OK);
  fixture.advanceTo(10);
  EXPECT_NE(fixture.read().find("$timescale\n\t1ns\n$end\n"),
            std::string::npos);
}

TEST(VCD, TimescaleUsesAScaledUnitWhenThePrecisionIsNotAPowerOfAThousand) {
  Fixture fixture;
  ASSERT_EQ(fixture.create(), OBELISK_RT_OK);
  ASSERT_EQ(fixture.openDump(), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_dump_timescale(fixture.context, -10), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_dump_vars(fixture.context, 0, nullptr, 0),
            OBELISK_RT_OK);
  fixture.advanceTo(10);
  EXPECT_NE(fixture.read().find("$timescale\n\t100ps\n$end\n"),
            std::string::npos);
}

//===----------------------------------------------------------------------===//
// Aliasing
//
// `data` and `bus` name the identical canonical four-state range, so IEEE 1364
// permits them to share one identifier code. Sharing must be complete: one
// declaration per name, but a single traced range, a single initial value, and
// a single record per change.
//===----------------------------------------------------------------------===//

TEST(VCD, AliasesShareOneIdentifierCode) {
  Fixture fixture;
  ASSERT_EQ(fixture.create(), OBELISK_RT_OK);
  ASSERT_EQ(fixture.openDump(), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_dump_vars(fixture.context, 0, nullptr, 0),
            OBELISK_RT_OK);
  fixture.advanceTo(10);
  std::string text = fixture.read();

  std::string data = identifierFor(text, "data");
  std::string bus = identifierFor(text, "bus");
  std::string clk = identifierFor(text, "clk");
  std::string count = identifierFor(text, "count");
  ASSERT_FALSE(data.empty());
  EXPECT_EQ(data, bus);
  // Variables over distinct ranges must stay distinct.
  EXPECT_NE(data, clk);
  EXPECT_NE(data, count);
  EXPECT_NE(clk, count);
}

TEST(VCD, AliasesAreEachDeclaredWithTheirOwnName) {
  Fixture fixture;
  ASSERT_EQ(fixture.create(), OBELISK_RT_OK);
  ASSERT_EQ(fixture.openDump(), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_dump_vars(fixture.context, 0, nullptr, 0),
            OBELISK_RT_OK);
  fixture.advanceTo(10);
  std::string text = fixture.read();

  std::string data = identifierFor(text, "data");
  ASSERT_FALSE(data.empty());
  // Both names remain visible to a waveform viewer, with their own kinds.
  EXPECT_EQ(namesUsingCode(text, data),
            (std::vector<std::string>{"data", "bus"}));
  EXPECT_NE(text.find("$var reg 8 " + data + " data [7:0] $end"),
            std::string::npos);
  EXPECT_NE(text.find("$var wire 8 " + data + " bus [7:0] $end"),
            std::string::npos);
}

TEST(VCD, AliasedRangeIsDumpedOnlyOnceInitially) {
  Fixture fixture;
  ASSERT_EQ(fixture.create(), OBELISK_RT_OK);
  ASSERT_EQ(fixture.openDump(), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_dump_vars(fixture.context, 0, nullptr, 0),
            OBELISK_RT_OK);
  fixture.setBits(kDataBit, 8, 0xa5, 0);
  fixture.advanceTo(10);
  std::string text = fixture.read();

  std::string data = identifierFor(text, "data");
  EXPECT_EQ(valueRecords(text, data),
            (std::vector<std::string>{"b10100101"}));
}

TEST(VCD, AliasedRangeProducesOneRecordPerChange) {
  Fixture fixture;
  ASSERT_EQ(fixture.create(), OBELISK_RT_OK);
  ASSERT_EQ(fixture.openDump(), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_dump_vars(fixture.context, 0, nullptr, 0),
            OBELISK_RT_OK);
  fixture.setBits(kDataBit, 8, 0x00, 0);
  fixture.advanceTo(10);
  fixture.setBits(kDataBit, 8, 0x03, 0);
  fixture.advanceTo(20);
  fixture.setBits(kDataBit, 8, 0x0c, 0);
  fixture.advanceTo(30);
  std::string text = fixture.read();

  std::string data = identifierFor(text, "data");
  // Three settled values, three records -- not six.
  EXPECT_EQ(valueRecords(text, data),
            (std::vector<std::string>{"b0", "b11", "b1100"}));
}

TEST(VCD, AliasedRangeIsDifferencedOnce) {
  Fixture fixture;
  ASSERT_EQ(fixture.create(), OBELISK_RT_OK);
  ASSERT_EQ(fixture.openDump(), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_dump_vars(fixture.context, 0, nullptr, 0),
            OBELISK_RT_OK);
  fixture.advanceTo(10);
  std::string text = fixture.read();

  // Eight declared objects, one of which expands into three array elements:
  // ten declarations over nine distinct ranges, since `data` and `bus` share.
  ASSERT_NE(fixture.context->vcdState, nullptr);
  EXPECT_EQ(countOccurrences(text, "$var "), 10u);
  EXPECT_EQ(obelisk_rt_dump_traced_range_count(fixture.context), 9u);
}

// A real is stored as an IEEE-754 bit pattern in the same planes as everything
// else. Dumping it as a bit vector would be silently wrong, so it must be
// declared `$var real` and emitted as an `r` record.
TEST(VCD, RealsAreDumpedAsNumbersNotBitPatterns) {
  Fixture fixture;
  ASSERT_EQ(fixture.create(), OBELISK_RT_OK);
  ASSERT_EQ(fixture.openDump(), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_dump_vars(fixture.context, 0, nullptr, 0),
            OBELISK_RT_OK);
  uint64_t bits = 0;
  double value = 1.5;
  std::memcpy(&bits, &value, sizeof(bits));
  fixture.setBits(kRealBit, 64, bits, 0);
  fixture.advanceTo(10);
  value = -2.25;
  std::memcpy(&bits, &value, sizeof(bits));
  fixture.setBits(kRealBit, 64, bits, 0);
  fixture.advanceTo(20);
  std::string text = fixture.read();

  std::string rv = identifierFor(text, "rv");
  ASSERT_FALSE(rv.empty());
  // No bit range is declared for a real.
  EXPECT_NE(text.find("$var real 64 " + rv + " rv $end"), std::string::npos);
  EXPECT_EQ(valueRecords(text, rv),
            (std::vector<std::string>{"r1.5", "r-2.25"}));
}

TEST(VCD, RealsAreNotCheckpointedAsXByDumpOff) {
  Fixture fixture;
  ASSERT_EQ(fixture.create(), OBELISK_RT_OK);
  ASSERT_EQ(fixture.openDump(), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_dump_vars(fixture.context, 0, nullptr, 0),
            OBELISK_RT_OK);
  fixture.advanceTo(10);
  ASSERT_EQ(obelisk_rt_v1_dump_control(fixture.context, 0), OBELISK_RT_OK);
  std::string text = fixture.read();

  std::string rv = identifierFor(text, "rv");
  size_t off = text.find("$dumpoff\n");
  ASSERT_NE(off, std::string::npos);
  // VCD has no unknown encoding for a real.
  EXPECT_EQ(text.find("bx " + rv, off), std::string::npos);
  EXPECT_EQ(text.find("x" + rv, off), std::string::npos);
}

// A packed struct occupies one contiguous canonical range, so it is traced and
// emitted as a single vector whose declared bounds match its bit extent.
TEST(VCD, PackedStructsAreDumpedAsOneVector) {
  Fixture fixture;
  ASSERT_EQ(fixture.create(), OBELISK_RT_OK);
  ASSERT_EQ(fixture.openDump(), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_dump_vars(fixture.context, 0, nullptr, 0),
            OBELISK_RT_OK);
  // hi = 1, lo = 2 across the two nibbles.
  fixture.setBits(kStructBit, 8, 0x12, 0);
  fixture.advanceTo(10);
  fixture.setBits(kStructBit, 8, 0x24, 0);
  fixture.advanceTo(20);
  std::string text = fixture.read();

  std::string st = identifierFor(text, "st");
  ASSERT_FALSE(st.empty());
  EXPECT_NE(text.find("$var reg 8 " + st + " st [7:0] $end"),
            std::string::npos);
  EXPECT_EQ(valueRecords(text, st),
            (std::vector<std::string>{"b10010", "b100100"}));
}

// A struct field changing must move the whole traced range exactly once, not
// once per field.
TEST(VCD, PackedStructEmitsOneRecordWhenAnyFieldChanges) {
  Fixture fixture;
  ASSERT_EQ(fixture.create(), OBELISK_RT_OK);
  ASSERT_EQ(fixture.openDump(), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_dump_vars(fixture.context, 0, nullptr, 0),
            OBELISK_RT_OK);
  fixture.setBits(kStructBit, 8, 0x00, 0);
  fixture.advanceTo(10);
  // Only the high nibble moves.
  fixture.setBits(kStructBit + 4, 4, 0x3, 0);
  fixture.advanceTo(20);
  std::string text = fixture.read();

  std::string st = identifierFor(text, "st");
  EXPECT_EQ(valueRecords(text, st),
            (std::vector<std::string>{"b0", "b110000"}));
}

// An unpacked array is not a value: each element is an independent signal, so
// it is declared per element the way a waveform viewer renders an array. The
// declared range on each element describes its bits, never the element bounds.
TEST(VCD, UnpackedArraysAreExpandedPerElement) {
  Fixture fixture;
  ASSERT_EQ(fixture.create(), OBELISK_RT_OK);
  ASSERT_EQ(fixture.openDump(), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_dump_vars(fixture.context, 0, nullptr, 0),
            OBELISK_RT_OK);
  fixture.advanceTo(10);
  std::string text = fixture.read();

  for (const char *element : {"mem[0]", "mem[1]", "mem[2]"}) {
    std::string code = identifierFor(text, element);
    ASSERT_FALSE(code.empty()) << element;
    EXPECT_NE(text.find("$var reg 4 " + code + " " + element + " [3:0] $end"),
              std::string::npos)
        << element;
  }
  // Neither the smashed vector nor the element bounds survive.
  EXPECT_EQ(text.find(" mem [11:0] $end"), std::string::npos);
  EXPECT_EQ(text.find(" mem [0:2] $end"), std::string::npos);
  // Elements are distinct signals, not aliases of one range.
  EXPECT_NE(identifierFor(text, "mem[0]"), identifierFor(text, "mem[1]"));
}

// Element ordinal zero occupies the lowest canonical bits and carries the
// leftmost declared index, so values land on the element that was written.
TEST(VCD, UnpackedArrayElementsAreDifferencedIndependently) {
  Fixture fixture;
  ASSERT_EQ(fixture.create(), OBELISK_RT_OK);
  ASSERT_EQ(fixture.openDump(), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_dump_vars(fixture.context, 0, nullptr, 0),
            OBELISK_RT_OK);
  // mem[0] = 0, mem[1] = 5, mem[2] = 0.
  fixture.setBits(kArrayBit, 12, 0x050, 0);
  fixture.advanceTo(10);
  // Only mem[2] moves.
  fixture.setBits(kArrayBit + 8, 4, 0x3, 0);
  fixture.advanceTo(20);
  std::string text = fixture.read();

  EXPECT_EQ(valueRecords(text, identifierFor(text, "mem[0]")),
            (std::vector<std::string>{"b0"}));
  EXPECT_EQ(valueRecords(text, identifierFor(text, "mem[1]")),
            (std::vector<std::string>{"b101"}));
  // The untouched elements contribute no second record.
  EXPECT_EQ(valueRecords(text, identifierFor(text, "mem[2]")),
            (std::vector<std::string>{"b0", "b11"}));
}

// VCD has no aggregate form, so an array too large to expand is left out
// entirely. Emitting it as one wide vector would be well-formed but would
// present three independent signals as a single value.
TEST(VCD, OversizedUnpackedArraysAreOmittedNotSmashed) {
  Fixture fixture;
  ASSERT_EQ(fixture.create(), OBELISK_RT_OK);
  ASSERT_EQ(setenv("OBELISK_RT_DUMP_MAX_ARRAY", "2", 1), 0);
  ASSERT_EQ(fixture.openDump(), OBELISK_RT_OK);
  ASSERT_EQ(unsetenv("OBELISK_RT_DUMP_MAX_ARRAY"), 0);
  ASSERT_EQ(obelisk_rt_v1_dump_vars(fixture.context, 0, nullptr, 0),
            OBELISK_RT_OK);
  fixture.advanceTo(10);
  std::string text = fixture.read();

  // The three-element array exceeds the limit and appears in no form.
  EXPECT_TRUE(identifierFor(text, "mem").empty());
  EXPECT_TRUE(identifierFor(text, "mem[0]").empty());
  EXPECT_EQ(text.find(" mem "), std::string::npos);
  // Everything else is still traced.
  EXPECT_FALSE(identifierFor(text, "clk").empty());
  EXPECT_FALSE(identifierFor(text, "st").empty());
  EXPECT_EQ(obelisk_rt_dump_traced_range_count(fixture.context), 6u);
}

// An escaped identifier may contain a dot, so the leaf name cannot be found by
// splitting the hierarchical path on its last separator.
TEST(VCD, EscapedNamesContainingDotsKeepTheirWholeLeaf) {
  Fixture fixture;
  ASSERT_EQ(fixture.create(), OBELISK_RT_OK);
  ASSERT_EQ(fixture.openDump(), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_dump_vars(fixture.context, 0, nullptr, 0),
            OBELISK_RT_OK);
  fixture.advanceTo(10);
  std::string text = fixture.read();

  EXPECT_NE(text.find("\\x.y "), std::string::npos);
  // The trailing fragment of the escaped name must not appear on its own.
  EXPECT_EQ(text.find(" y $end"), std::string::npos);
}

TEST(VCD, SelectingOnlyOneAliasStillTracesTheSharedRange) {
  Fixture fixture;
  ASSERT_EQ(fixture.create(), OBELISK_RT_OK);
  ASSERT_EQ(fixture.openDump(), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_dump_vars(fixture.context, 1, nullptr, 0),
            OBELISK_RT_OK);
  fixture.setBits(kDataBit, 8, 0x01, 0);
  fixture.advanceTo(10);
  std::string text = fixture.read();

  std::string data = identifierFor(text, "data");
  ASSERT_FALSE(data.empty());
  EXPECT_EQ(identifierFor(text, "bus"), data);
  EXPECT_EQ(valueRecords(text, data), (std::vector<std::string>{"b1"}));
}

TEST(VCD, InitialValuesAreDumpedOnceAtTheSelectingSlot) {
  Fixture fixture;
  ASSERT_EQ(fixture.create(), OBELISK_RT_OK);
  ASSERT_EQ(fixture.openDump(), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_dump_vars(fixture.context, 0, nullptr, 0),
            OBELISK_RT_OK);
  fixture.setBits(kClkBit, 1, 1, 0);
  fixture.setBits(kDataBit, 8, 0xa5, 0);
  fixture.advanceTo(5);
  std::string text = fixture.read();

  size_t dumpvars = text.find("$dumpvars\n");
  ASSERT_NE(dumpvars, std::string::npos);
  EXPECT_LT(text.find("#0\n"), dumpvars);
  std::string clk = identifierFor(text, "clk");
  std::string data = identifierFor(text, "data");
  EXPECT_NE(text.find("1" + clk + "\n", dumpvars), std::string::npos);
  EXPECT_NE(text.find("b10100101 " + data + "\n", dumpvars), std::string::npos);
}

TEST(VCD, OnlyChangedVariablesAreEmittedPerSlot) {
  Fixture fixture;
  ASSERT_EQ(fixture.create(), OBELISK_RT_OK);
  ASSERT_EQ(fixture.openDump(), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_dump_vars(fixture.context, 0, nullptr, 0),
            OBELISK_RT_OK);
  // Uninitialized canonical state is X, so drive known values into the slot
  // that carries the initial section.
  fixture.setBits(kClkBit, 1, 0, 0);
  fixture.setBits(kDataBit, 8, 0, 0);
  fixture.setBits(kCountBit, 4, 0, 0);
  fixture.advanceTo(10);

  // Only `clk` moves in this slot.
  fixture.setBits(kClkBit, 1, 1, 0);
  fixture.advanceTo(20);
  // Nothing moves.
  fixture.advanceTo(30);
  // `count` moves to X.
  fixture.setBits(kCountBit, 4, 0, 0xf);
  fixture.advanceTo(40);
  std::string text = fixture.read();

  std::string clk = identifierFor(text, "clk");
  std::string count = identifierFor(text, "count");
  size_t body = text.find("$enddefinitions $end");
  ASSERT_NE(body, std::string::npos);

  EXPECT_NE(text.find("#10\n1" + clk, body), std::string::npos);
  // A slot with no change produces no time marker at all.
  EXPECT_EQ(text.find("#20\n"), std::string::npos);
  EXPECT_NE(text.find("#30\nbx " + count, body), std::string::npos);
  // `data` never moved, so it appears only in the initial section.
  std::string data = identifierFor(text, "data");
  EXPECT_EQ(valueRecords(text, data), (std::vector<std::string>{"b0"}));
}

TEST(VCD, VectorsDropRedundantLeadingCharacters) {
  Fixture fixture;
  ASSERT_EQ(fixture.create(), OBELISK_RT_OK);
  ASSERT_EQ(fixture.openDump(), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_dump_vars(fixture.context, 0, nullptr, 0),
            OBELISK_RT_OK);
  fixture.advanceTo(10);
  fixture.setBits(kDataBit, 8, 0x05, 0);
  fixture.advanceTo(20);
  fixture.setBits(kDataBit, 8, 0x00, 0xff);
  fixture.advanceTo(30);
  fixture.setBits(kDataBit, 8, 0x80, 0);
  fixture.advanceTo(40);
  std::string text = fixture.read();

  std::string data = identifierFor(text, "data");
  // Leading zeros are implied by the left-extension rule.
  EXPECT_NE(text.find("b101 " + data), std::string::npos);
  // A repeated leading X collapses to one character.
  EXPECT_NE(text.find("bx " + data), std::string::npos);
  // A leading one must never be dropped.
  EXPECT_NE(text.find("b10000000 " + data), std::string::npos);
}

TEST(VCD, ScopeSelectionRestrictsTheTracedSet) {
  Fixture fixture;
  ASSERT_EQ(fixture.create(), OBELISK_RT_OK);
  ASSERT_EQ(fixture.openDump(), OBELISK_RT_OK);
  const std::string scope = "top.sub";
  ASSERT_EQ(obelisk_rt_v1_dump_vars(
                fixture.context, 0,
                reinterpret_cast<const uint8_t *>(scope.data()), scope.size()),
            OBELISK_RT_OK);
  fixture.advanceTo(10);
  std::string text = fixture.read();

  EXPECT_FALSE(identifierFor(text, "count").empty());
  EXPECT_TRUE(identifierFor(text, "clk").empty());
  EXPECT_TRUE(identifierFor(text, "data").empty());
  // The enclosing scope is still declared so the path stays well formed.
  EXPECT_NE(text.find("$scope module top $end"), std::string::npos);
  EXPECT_NE(text.find("$scope module sub $end"), std::string::npos);
}

TEST(VCD, VariableSelectionRestrictsTheTracedSetWithoutEnablingVPI) {
  Fixture fixture;
  ASSERT_EQ(fixture.create(), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_vpi_startup(fixture.context, nullptr, 0),
            OBELISK_RT_PERMISSION_DENIED);
  ASSERT_EQ(fixture.openDump(), OBELISK_RT_OK);
  const std::string variable = "top.sub.count";
  ASSERT_EQ(obelisk_rt_v1_dump_vars(
                fixture.context, 0,
                reinterpret_cast<const uint8_t *>(variable.data()),
                variable.size()),
            OBELISK_RT_OK);
  fixture.advanceTo(10);
  std::string text = fixture.read();

  EXPECT_FALSE(identifierFor(text, "count").empty());
  EXPECT_TRUE(identifierFor(text, "clk").empty());
  EXPECT_TRUE(identifierFor(text, "data").empty());
  EXPECT_NE(text.find("$scope module top $end"), std::string::npos);
  EXPECT_NE(text.find("$scope module sub $end"), std::string::npos);
}

TEST(VCD, DepthLimitedSelectionExcludesNestedScopes) {
  Fixture fixture;
  ASSERT_EQ(fixture.create(), OBELISK_RT_OK);
  ASSERT_EQ(fixture.openDump(), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_dump_vars(fixture.context, 1, nullptr, 0),
            OBELISK_RT_OK);
  fixture.advanceTo(10);
  std::string text = fixture.read();

  EXPECT_FALSE(identifierFor(text, "clk").empty());
  EXPECT_TRUE(identifierFor(text, "count").empty());
  EXPECT_EQ(text.find("$scope module sub $end"), std::string::npos);
}

TEST(VCD, DumpOffCheckpointsXAndDumpOnRepublishes) {
  Fixture fixture;
  ASSERT_EQ(fixture.create(), OBELISK_RT_OK);
  ASSERT_EQ(fixture.openDump(), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_dump_vars(fixture.context, 0, nullptr, 0),
            OBELISK_RT_OK);
  fixture.setBits(kClkBit, 1, 1, 0);
  fixture.advanceTo(10);
  ASSERT_EQ(obelisk_rt_v1_dump_control(fixture.context, 0), OBELISK_RT_OK);
  fixture.setBits(kClkBit, 1, 0, 0);
  fixture.advanceTo(20);
  ASSERT_EQ(obelisk_rt_v1_dump_control(fixture.context, 1), OBELISK_RT_OK);
  fixture.advanceTo(30);
  std::string text = fixture.read();

  std::string clk = identifierFor(text, "clk");
  size_t off = text.find("$dumpoff\n");
  size_t on = text.find("$dumpon\n");
  ASSERT_NE(off, std::string::npos);
  ASSERT_NE(on, std::string::npos);
  EXPECT_LT(off, on);
  EXPECT_NE(text.find("x" + clk, off), std::string::npos);
  // The change that happened while dumping was off is not recorded, but the
  // resumed value is.
  EXPECT_EQ(text.find("0" + clk, off), text.find("0" + clk, on));
  EXPECT_NE(text.find("0" + clk, on), std::string::npos);
}

TEST(VCD, DumpAllEmitsEverySelectedVariable) {
  Fixture fixture;
  ASSERT_EQ(fixture.create(), OBELISK_RT_OK);
  ASSERT_EQ(fixture.openDump(), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_dump_vars(fixture.context, 0, nullptr, 0),
            OBELISK_RT_OK);
  fixture.setBits(kClkBit, 1, 0, 0);
  fixture.advanceTo(10);
  // Nothing changed in this slot, so only $dumpall can republish the value.
  ASSERT_EQ(obelisk_rt_v1_dump_all(fixture.context), OBELISK_RT_OK);
  std::string text = fixture.read();

  std::string clk = identifierFor(text, "clk");
  EXPECT_EQ(valueRecords(text, clk), (std::vector<std::string>{"0", "0"}));
  EXPECT_NE(text.find("#10\n"), std::string::npos);
}

TEST(VCD, DumpLimitStopsWriting) {
  Fixture fixture;
  ASSERT_EQ(fixture.create(), OBELISK_RT_OK);
  ASSERT_EQ(fixture.openDump(), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_dump_limit(fixture.context, 64), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_dump_vars(fixture.context, 0, nullptr, 0),
            OBELISK_RT_OK);
  fixture.advanceTo(10);
  fixture.setBits(kDataBit, 8, 0xff, 0);
  fixture.advanceTo(20);
  std::string text = fixture.read();

  EXPECT_LE(text.size(), 64u);
  EXPECT_EQ(text.find("$enddefinitions"), std::string::npos);
}

TEST(VCD, SelectingAMissingScopeIsRejected) {
  Fixture fixture;
  ASSERT_EQ(fixture.create(), OBELISK_RT_OK);
  ASSERT_EQ(fixture.openDump(), OBELISK_RT_OK);
  const std::string scope = "top.absent";
  ASSERT_EQ(obelisk_rt_v1_dump_vars(
                fixture.context, 0,
                reinterpret_cast<const uint8_t *>(scope.data()), scope.size()),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_dump_slot_unlocked(fixture.context),
            OBELISK_RT_INVALID_HANDLE);
  // The failure is reported once; later slots neither retry nor write.
  EXPECT_EQ(obelisk_rt_dump_slot_unlocked(fixture.context), OBELISK_RT_OK);
  EXPECT_FALSE(obelisk_rt_dump_active_unlocked(fixture.context));
}

TEST(VCD, DumpingWithoutAnOpenFileIsInert) {
  Fixture fixture;
  ASSERT_EQ(fixture.create(), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_dump_slot_unlocked(fixture.context), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_dump_all(fixture.context), OBELISK_RT_OK);
  EXPECT_FALSE(obelisk_rt_dump_active_unlocked(fixture.context));
}

} // namespace
