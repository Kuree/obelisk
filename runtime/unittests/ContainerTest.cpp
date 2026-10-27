//===- ContainerTest.cpp - Managed string and container runtime tests -----===//

#include "obelisk/Runtime/Runtime.h"

#include "../lib/RuntimeInternal.h"

#include "gtest/gtest.h"

#include <array>
#include <cstring>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

const obelisk_rt_trace_entry_v1 managedWordTraceEntry{
    0,      0, 1, OBELISK_RT_TRACE_STRONG, OBELISK_RT_MANAGED_SLOT_CONTAINER,
    nullptr};
const obelisk_rt_trace_layout_v1 managedWordTraceLayout{
    OBELISK_RT_VERSION,
    0,
    sizeof(obelisk_rt_managed_word_v1),
    alignof(obelisk_rt_managed_word_v1),
    &managedWordTraceEntry,
    1};
const obelisk_rt_trace_entry_v1 stringTraceEntry{
    0, 0, 1, OBELISK_RT_TRACE_STRONG, OBELISK_RT_MANAGED_SLOT_STRING, nullptr};
// A managed string is a tagged 64-bit word, not a pointer, so its slot stays
// eight bytes wide at any pointer width.
const obelisk_rt_trace_layout_v1 stringTraceLayout{
    OBELISK_RT_VERSION,
    0,
    sizeof(obelisk_rt_string_v1),
    alignof(obelisk_rt_string_v1),
    &stringTraceEntry,
    1};
const obelisk_rt_element_type_v1 wordElement{
    OBELISK_RT_VERSION, OBELISK_RT_ELEMENT_BITS, 1,  0,       0,
    sizeof(uint64_t),   alignof(uint64_t),       64, nullptr, nullptr};
const obelisk_rt_element_type_v1 byteElement{
    OBELISK_RT_VERSION, OBELISK_RT_ELEMENT_BITS, 4, 0,       0,
    sizeof(uint8_t),    alignof(uint8_t),        8, nullptr, nullptr};
const obelisk_rt_element_type_v1 logicElement{OBELISK_RT_VERSION,
                                              OBELISK_RT_ELEMENT_LOGIC,
                                              5,
                                              OBELISK_RT_ELEMENT_FOUR_STATE,
                                              0,
                                              sizeof(uint8_t),
                                              alignof(uint8_t),
                                              4,
                                              nullptr,
                                              nullptr};
const obelisk_rt_element_type_v1 stringElement{OBELISK_RT_VERSION,
                                               OBELISK_RT_ELEMENT_STRING,
                                               2,
                                               0,
                                               0,
                                               sizeof(obelisk_rt_string_v1),
                                               alignof(obelisk_rt_string_v1),
                                               0,
                                               &stringTraceLayout,
                                               nullptr};
const obelisk_rt_element_type_v1 containerElement{
    OBELISK_RT_VERSION,
    OBELISK_RT_ELEMENT_CONTAINER_HANDLE,
    3,
    0,
    0,
    sizeof(obelisk_rt_managed_word_v1),
    alignof(obelisk_rt_managed_word_v1),
    0,
    &managedWordTraceLayout,
    nullptr};

class ManagedValueTest : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_gc_lane_create(context, &lane), OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_gc_lane_enter(lane), OBELISK_RT_OK);
  }

  void TearDown() override {
    if (lane) {
      EXPECT_EQ(obelisk_rt_v1_gc_lane_leave(lane), OBELISK_RT_OK);
      EXPECT_EQ(obelisk_rt_v1_gc_lane_destroy(lane), OBELISK_RT_OK);
    }
    obelisk_rt_v1_context_destroy(context);
  }

  obelisk_rt_context *context = nullptr;
  obelisk_rt_gc_lane_v1 *lane = nullptr;
};

TEST_F(ManagedValueTest, StringsAreImmutableNonInternedManagedValues) {
  obelisk_rt_string_v1 first = 0;
  obelisk_rt_string_v1 second = 0;
  ASSERT_EQ(obelisk_rt_v1_string_create(lane, "Obelisk!", 8, &first),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_string_create(lane, "Obelisk!", 8, &second),
            OBELISK_RT_OK);
  EXPECT_NE(first, second);
  EXPECT_EQ(obelisk_rt_v1_string_length(first), 8u);
  EXPECT_EQ(obelisk_rt_v1_string_hash(first),
            obelisk_rt_v1_string_hash(second));
  EXPECT_EQ(obelisk_rt_v1_string_compare(first, second), 0);

  const char *bytes = nullptr;
  uint64_t size = 0;
  char scratch[8];
  ASSERT_EQ(obelisk_rt_v1_string_view(first, scratch, &bytes, &size),
            OBELISK_RT_OK);
  ASSERT_EQ(size, 8u);
  EXPECT_EQ(std::string(bytes, size), "Obelisk!");
  EXPECT_EQ(bytes[size], '\0');
  EXPECT_EQ(obelisk_rt_v1_string_getc(first, 0), 'O');
  EXPECT_EQ(obelisk_rt_v1_string_getc(first, -1), 0u);
  EXPECT_EQ(obelisk_rt_v1_string_getc(first, 8), 0u);

  obelisk_rt_string_v1 empty = first;
  ASSERT_EQ(obelisk_rt_v1_string_create(lane, nullptr, 0, &empty),
            OBELISK_RT_OK);
  EXPECT_EQ(empty, 0u);
  EXPECT_EQ(obelisk_rt_v1_string_length(0), 0u);
  ASSERT_EQ(obelisk_rt_v1_string_view(0, scratch, &bytes, &size),
            OBELISK_RT_OK);
  EXPECT_EQ(size, 0u);
  EXPECT_STREQ(bytes, "");
}

TEST_F(ManagedValueTest, StringSSOCoversEveryLengthAndEmbeddedNullBytes) {
  obelisk_rt_gc_statistics_v1 before{};
  ASSERT_EQ(obelisk_rt_v1_gc_statistics(context, &before), OBELISK_RT_OK);
  for (uint64_t length = 0; length <= 7; ++length) {
    const char source[7] = {'A', '\0', 'B', static_cast<char>(0xff),
                            'C', '\0', 'D'};
    obelisk_rt_string_v1 string = UINT64_MAX;
    ASSERT_EQ(obelisk_rt_v1_string_create(nullptr, source, length, &string),
              OBELISK_RT_OK);
    EXPECT_EQ(obelisk_rt_v1_string_length(string), length);
    char scratch[8] = {};
    const char *bytes = nullptr;
    uint64_t size = UINT64_MAX;
    ASSERT_EQ(obelisk_rt_v1_string_view(string, scratch, &bytes, &size),
              OBELISK_RT_OK);
    EXPECT_EQ(size, length);
    EXPECT_EQ(std::memcmp(bytes, source, static_cast<size_t>(length)), 0);
    EXPECT_EQ(bytes[length], '\0');
  }
  obelisk_rt_gc_statistics_v1 after{};
  ASSERT_EQ(obelisk_rt_v1_gc_statistics(context, &after), OBELISK_RT_OK);
  EXPECT_EQ(after.allocated_objects, before.allocated_objects);

  char scratch[8];
  const char *bytes = nullptr;
  uint64_t size = 0;
  EXPECT_EQ(obelisk_rt_v1_string_view(2, scratch, &bytes, &size),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(obelisk_rt_v1_string_view(3, scratch, &bytes, &size),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(obelisk_rt_v1_string_view(UINT64_C(0x21), scratch, &bytes, &size),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(obelisk_rt_v1_string_view(UINT64_C(0x1), scratch, &bytes, &size),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(obelisk_rt_v1_string_view(UINT64_C(0x0000000000420005), scratch,
                                      &bytes, &size),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(obelisk_rt_v1_string_view(UINT64_C(0x1000), scratch, &bytes, &size),
            OBELISK_RT_INVALID_HANDLE);
}

TEST_F(ManagedValueTest, ScansNativeRawWordsWithoutTextInterpretation) {
  // Three native s_vpi_vecval records encode a non-byte-aligned 69-bit value.
  // Runtime logic stores aval^bval in its value plane and bval in unknown.
  std::array<uint32_t, 6> records{UINT32_C(0x80000001), UINT32_C(0x00000000),
                                  UINT32_C(0x00000001), UINT32_C(0x00000003),
                                  UINT32_C(0x00000010), UINT32_C(0x00000000)};
  obelisk_rt_string_v1 input = 0;
  ASSERT_EQ(obelisk_rt_v1_string_create(
                lane, reinterpret_cast<const char *>(records.data()),
                sizeof(records), &input),
            OBELISK_RT_OK);
  std::array<uint64_t, 2> value{UINT64_MAX, UINT64_MAX};
  std::array<uint64_t, 2> unknown{UINT64_MAX, UINT64_MAX};
  uint32_t cursor = UINT32_MAX;
  uint32_t ok = 0;
  ASSERT_EQ(obelisk_rt_v1_string_scan_raw(
                input, 0, nullptr, 0, sizeof(records), 69, 1, 0, value.data(),
                sizeof(value), unknown.data(), sizeof(unknown), &cursor, &ok),
            OBELISK_RT_OK);
  EXPECT_EQ(ok, 1u);
  EXPECT_EQ(cursor, sizeof(records));
  EXPECT_EQ(value[0], UINT64_C(0x0000000280000001));
  EXPECT_EQ(value[1] & UINT64_C(0x1f), UINT64_C(0x10));
  EXPECT_EQ(unknown[0], UINT64_C(0x0000000300000000));
  EXPECT_EQ(unknown[1] & UINT64_C(0x1f), 0u);

  std::array<uint32_t, 3> twoStateWords{
      UINT32_C(0x80000001), UINT32_C(0x00000003), UINT32_C(0x00000010)};
  ASSERT_EQ(obelisk_rt_v1_string_create(
                lane, reinterpret_cast<const char *>(twoStateWords.data()),
                sizeof(twoStateWords), &input),
            OBELISK_RT_OK);
  value = {UINT64_MAX, UINT64_MAX};
  unknown = {UINT64_MAX, UINT64_MAX};
  ASSERT_EQ(obelisk_rt_v1_string_scan_raw(
                input, 0, nullptr, 0, sizeof(twoStateWords), 69, 0, 0,
                value.data(), sizeof(value), unknown.data(), sizeof(unknown),
                &cursor, &ok),
            OBELISK_RT_OK);
  EXPECT_EQ(value[0], UINT64_C(0x0000000380000001));
  EXPECT_EQ(value[1] & UINT64_C(0x1f), UINT64_C(0x10));
  EXPECT_EQ(unknown[0], 0u);
  EXPECT_EQ(unknown[1] & UINT64_C(0x1f), 0u);

  // Suppressed raw fields consume their explicit byte count with no output
  // storage, while impossible typed widths fail before touching output spans.
  cursor = UINT32_MAX;
  ok = 0;
  EXPECT_EQ(obelisk_rt_v1_string_scan_raw(input, 0, nullptr, 0, 4, 0, 0, 4,
                                          nullptr, 0, nullptr, 0, &cursor, &ok),
            OBELISK_RT_OK);
  EXPECT_EQ(cursor, 4u);
  EXPECT_EQ(ok, 1u);
  EXPECT_EQ(obelisk_rt_v1_string_scan_raw(
                input, 0, nullptr, 0, 4, UINT64_MAX, 0, 0, value.data(),
                sizeof(value), unknown.data(), sizeof(unknown), &cursor, &ok),
            OBELISK_RT_INVALID_ARGUMENT);
}

TEST_F(ManagedValueTest, ConcatManyFusesIntoOneManagedAllocation) {
  obelisk_rt_string_v1 strings[4]{};
  ASSERT_EQ(obelisk_rt_v1_string_create(nullptr, "ab", 2, &strings[0]),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_string_create(nullptr, "", 0, &strings[1]),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_string_create(nullptr, "cdef", 4, &strings[2]),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_string_create(nullptr, "ghij", 4, &strings[3]),
            OBELISK_RT_OK);
  obelisk_rt_string_span_v1 spans[4] = {
      {strings[0]}, {strings[1]}, {strings[2]}, {strings[3]}};
  obelisk_rt_gc_statistics_v1 before{};
  ASSERT_EQ(obelisk_rt_v1_gc_statistics(context, &before), OBELISK_RT_OK);
  obelisk_rt_string_v1 joined = 0;
  ASSERT_EQ(obelisk_rt_v1_string_concat_many(lane, spans, 4, &joined),
            OBELISK_RT_OK);
  obelisk_rt_gc_statistics_v1 after{};
  ASSERT_EQ(obelisk_rt_v1_gc_statistics(context, &after), OBELISK_RT_OK);
  EXPECT_EQ(after.allocated_objects, before.allocated_objects + 1);
  char scratch[8];
  const char *bytes = nullptr;
  uint64_t size = 0;
  ASSERT_EQ(obelisk_rt_v1_string_view(joined, scratch, &bytes, &size),
            OBELISK_RT_OK);
  EXPECT_EQ(std::string(bytes, size), "abcdefghij");
}

TEST_F(ManagedValueTest, RepeatsAndConvertsPackedByteSequences) {
  const uint8_t packed[] = {'D', 'C', 'B', 'A'};
  const uint8_t unknown[] = {0, 0xff, 0, 0};
  obelisk_rt_string_v1 string = 0;
  ASSERT_EQ(
      obelisk_rt_v1_string_from_packed(lane, packed, unknown, 32, &string),
      OBELISK_RT_OK);
  char scratch[8] = {};
  const char *bytes = nullptr;
  uint64_t size = 0;
  ASSERT_EQ(obelisk_rt_v1_string_view(string, scratch, &bytes, &size),
            OBELISK_RT_OK);
  ASSERT_EQ(size, 3u);
  EXPECT_EQ(static_cast<uint8_t>(bytes[0]), 'A');
  EXPECT_EQ(static_cast<uint8_t>(bytes[1]), 'B');
  EXPECT_EQ(static_cast<uint8_t>(bytes[2]), 'D');

  obelisk_rt_string_v1 repeated = 0;
  ASSERT_EQ(obelisk_rt_v1_string_repeat(lane, string, 3, &repeated),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_string_view(repeated, scratch, &bytes, &size),
            OBELISK_RT_OK);
  ASSERT_EQ(size, 9u);
  EXPECT_EQ(std::string(bytes, size), "ABDABDABD");

  uint8_t narrowed[3] = {0xff, 0xff, 0xff};
  uint8_t narrowedUnknown[3] = {0xff, 0xff, 0xff};
  ASSERT_EQ(
      obelisk_rt_v1_string_to_packed(repeated, narrowed, narrowedUnknown, 20),
      OBELISK_RT_OK);
  EXPECT_EQ(narrowed[0], 'D');
  EXPECT_EQ(narrowed[1], 'B');
  EXPECT_EQ(narrowed[2], 1u);
  EXPECT_EQ(narrowedUnknown[0], 0u);
  EXPECT_EQ(narrowedUnknown[1], 0u);
  EXPECT_EQ(narrowedUnknown[2], 0u);

  const uint8_t sparsePacked[] = {0, 'A', 0, 'B'};
  obelisk_rt_string_v1 sparse = 0;
  ASSERT_EQ(obelisk_rt_v1_string_from_packed(lane, sparsePacked, nullptr, 32,
                                             &sparse),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_string_view(sparse, scratch, &bytes, &size),
            OBELISK_RT_OK);
  EXPECT_EQ(std::string(bytes, size), "BA");

  const uint32_t zero = 0;
  obelisk_rt_string_v1 convertedEmpty = UINT64_MAX;
  ASSERT_EQ(obelisk_rt_v1_string_from_packed(lane, &zero, nullptr, 32,
                                             &convertedEmpty),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_string_length(convertedEmpty), 0u);

  obelisk_rt_string_v1 empty = UINT64_MAX;
  EXPECT_EQ(obelisk_rt_v1_string_repeat(lane, string, 0, &empty),
            OBELISK_RT_OK);
  EXPECT_EQ(empty, 0u);
  EXPECT_EQ(obelisk_rt_v1_string_repeat(lane, string, UINT64_MAX, &empty),
            OBELISK_RT_OUT_OF_RESOURCES);
}

TEST_F(ManagedValueTest, ParsesAndFormatsStringNumbers) {
  obelisk_rt_string_v1 input = 0;
  ASSERT_EQ(obelisk_rt_v1_string_create(lane, "1_23tail", 8, &input),
            OBELISK_RT_OK);
  uint64_t integer = 0;
  ASSERT_EQ(obelisk_rt_v1_string_parse_integer(input, 10, &integer),
            OBELISK_RT_OK);
  EXPECT_EQ(integer, 123u);

  // IEEE 1800-2023 6.16.9: the scan accepts neither a sign nor leading white
  // space, so both stop before the first digit and return zero.
  for (std::string_view text : {"-1", "+1", " 1"}) {
    ASSERT_EQ(obelisk_rt_v1_string_create(lane, text.data(), text.size(),
                                          &input),
              OBELISK_RT_OK);
    integer = UINT64_MAX;
    ASSERT_EQ(obelisk_rt_v1_string_parse_integer(input, 10, &integer),
              OBELISK_RT_OK);
    EXPECT_EQ(integer, 0u) << text;
  }

  ASSERT_EQ(obelisk_rt_v1_string_create(nullptr, "f_f", 3, &input),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_string_parse_integer(input, 16, &integer),
            OBELISK_RT_OK);
  EXPECT_EQ(integer, 255u);

  // IEEE 1800-2023 6.16.10: atoreal converts the longest prefix that is a
  // 5.7.2 real constant, which has a digit on each side of a decimal point, a
  // complete exponent, and no sign or leading white space.
  for (auto [text, expected] :
       std::initializer_list<std::pair<std::string_view, double>>{
           {"3.2_5junk", 3.25},
           {"236.123_763_e-12", 236.123763e-12},
           {"23E10", 23e10},
           {"1e1_0", 1e10},
           {"12", 12.0},
           {"9.", 9.0},
           {"4.E3", 4.0},
           {"1.5e", 1.5},
           {"1.5e+x", 1.5},
           {".5", 0.0},
           {" 3.25", 0.0},
           {"-1.5", 0.0},
           {"_1", 0.0},
           {"inf", 0.0},
           {"1e999", std::numeric_limits<double>::infinity()},
           {"1e-999", 0.0},
           {"0.0000000000000000000000000000000000000000000000000000000000000"
            "00000000000000000000000000000000000000000000000000000000000000"
            "00000000000000000000000000000000000000000000000000000000000000"
            "00000000000000000000000000000000000000000000000000000000000000"
            "00000000000000000000000000000000000000000000000000000000000000"
            "0000000000000000000000000000000000000000000000000000000000001",
            0.0}}) {
    ASSERT_EQ(
        obelisk_rt_v1_string_create(lane, text.data(), text.size(), &input),
        OBELISK_RT_OK);
    double real = -1.0;
    ASSERT_EQ(obelisk_rt_v1_string_parse_real_constant(input, &real),
              OBELISK_RT_OK);
    EXPECT_DOUBLE_EQ(real, expected) << text;
  }
  // A $sscanf floating-point field is not limited to real-constant syntax.
  for (auto [text, expected] :
       std::initializer_list<std::pair<std::string_view, double>>{
           {" 3.2_5junk", 3.25}, {"-1.5", -1.5}, {".5", 0.5}}) {
    ASSERT_EQ(
        obelisk_rt_v1_string_create(lane, text.data(), text.size(), &input),
        OBELISK_RT_OK);
    double real = 0.0;
    ASSERT_EQ(obelisk_rt_v1_string_parse_real(input, &real), OBELISK_RT_OK);
    EXPECT_DOUBLE_EQ(real, expected) << text;
  }

  auto expectString = [&](obelisk_rt_string_v1 string,
                          std::string_view expected) {
    char scratch[8] = {};
    const char *bytes = nullptr;
    uint64_t size = 0;
    ASSERT_EQ(obelisk_rt_v1_string_view(string, scratch, &bytes, &size),
              OBELISK_RT_OK);
    EXPECT_EQ(std::string_view(bytes, size), expected);
  };
  obelisk_rt_string_v1 output = 0;
  ASSERT_EQ(obelisk_rt_v1_string_format_integer(
                lane, static_cast<uint64_t>(int64_t{-42}), 10, 1, &output),
            OBELISK_RT_OK);
  expectString(output, "-42");
  ASSERT_EQ(obelisk_rt_v1_string_format_integer(lane, 255, 16, 0, &output),
            OBELISK_RT_OK);
  expectString(output, "ff");
  // IEEE 1800-2017 6.16.12 through 6.16.14 keep the signed `integer` of
  // 6.16.11 and change only the base, so a negative value stays signed in
  // hexadecimal, octal, and binary.
  ASSERT_EQ(obelisk_rt_v1_string_format_integer(
                lane, static_cast<uint64_t>(int64_t{-11}), 16, 1, &output),
            OBELISK_RT_OK);
  expectString(output, "-b");
  ASSERT_EQ(obelisk_rt_v1_string_format_integer(
                lane, static_cast<uint64_t>(int64_t{-11}), 8, 1, &output),
            OBELISK_RT_OK);
  expectString(output, "-13");
  ASSERT_EQ(obelisk_rt_v1_string_format_integer(
                lane, static_cast<uint64_t>(int64_t{-11}), 2, 1, &output),
            OBELISK_RT_OK);
  expectString(output, "-1011");
  ASSERT_EQ(obelisk_rt_v1_string_format_real(lane, 3.25, &output),
            OBELISK_RT_OK);
  expectString(output, "3.25");

  EXPECT_EQ(obelisk_rt_v1_string_parse_integer(input, 3, &integer),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_string_format_integer(lane, 0, 3, 0, &output),
            OBELISK_RT_INVALID_ARGUMENT);
}

TEST_F(ManagedValueTest, FormatsManagedStringArguments) {
  obelisk_rt_string_v1 format = 0;
  obelisk_rt_string_v1 value = 0;
  ASSERT_EQ(obelisk_rt_v1_string_create(lane, "value=%s", 8, &format),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_string_create(lane, "managed!", 8, &value),
            OBELISK_RT_OK);
  obelisk_rt_arg_v1 argument{OBELISK_RT_ARG_MANAGED_STRING, 0, 0, &value,
                             nullptr};
  obelisk_rt_format_env_v1 environment{};
  environment.time_multiplier = 1;
  obelisk_rt_buffer_v1 output{};
  char scratch[8] = {};
  const char *formatBytes = nullptr;
  uint64_t formatSize = 0;
  ASSERT_EQ(
      obelisk_rt_v1_string_view(format, scratch, &formatBytes, &formatSize),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_format(context, formatBytes, formatSize, &argument, 1,
                                 &environment, &output),
            OBELISK_RT_OK);
  EXPECT_EQ(
      std::string(reinterpret_cast<const char *>(output.data), output.size),
      "value=managed!");
  obelisk_rt_v1_buffer_release(&output);
}

TEST_F(ManagedValueTest, FormatsByteContainersAsStrings) {
  // IEEE 1800-2023 21.2.1.7: %s also takes an unpacked array of byte, in
  // index order. As in a string assigned from the array, NUL is dropped.
  obelisk_rt_object_v1 *bytes = nullptr;
  ASSERT_EQ(obelisk_rt_v1_container_create_typed(
                lane, OBELISK_RT_CONTAINER_DYNAMIC_ARRAY, 778,
                OBELISK_RT_ELEMENT_BITS, OBELISK_RT_ELEMENT_SIGNED, 1, 1, 8,
                nullptr, 0, 3, 0, &bytes),
            OBELISK_RT_OK);
  const uint8_t text[] = {'h', 0, 'i'};
  for (int64_t index = 0; index != 3; ++index)
    ASSERT_EQ(obelisk_rt_v1_container_write(lane, bytes, index, &text[index],
                                            nullptr),
              OBELISK_RT_OK);
  obelisk_rt_arg_v1 argument{OBELISK_RT_ARG_MANAGED_CONTAINER, 0, 0, &bytes,
                             nullptr};
  obelisk_rt_format_env_v1 environment{};
  environment.time_multiplier = 1;
  obelisk_rt_buffer_v1 output{};
  ASSERT_EQ(obelisk_rt_v1_format(context, "[%4s]", 5, &argument, 1,
                                 &environment, &output),
            OBELISK_RT_OK);
  EXPECT_EQ(
      std::string(reinterpret_cast<const char *>(output.data), output.size),
      "[  hi]");
  obelisk_rt_v1_buffer_release(&output);

  // Wider elements are not characters.
  obelisk_rt_object_v1 *words = nullptr;
  ASSERT_EQ(obelisk_rt_v1_container_create_typed(
                lane, OBELISK_RT_CONTAINER_DYNAMIC_ARRAY, 779,
                OBELISK_RT_ELEMENT_BITS, 0, sizeof(uint32_t), 1, 32, nullptr, 0,
                1, 0, &words),
            OBELISK_RT_OK);
  argument.data = &words;
  EXPECT_EQ(obelisk_rt_v1_format(context, "%s", 2, &argument, 1, &environment,
                                 &output),
            OBELISK_RT_ARGUMENT_MISMATCH);
  obelisk_rt_v1_buffer_release(&output);
}

TEST_F(ManagedValueTest, CreatesTypedContainersAndFormatsPatterns) {
  obelisk_rt_object_v1 *array = nullptr;
  ASSERT_EQ(obelisk_rt_v1_container_create_typed(
                lane, OBELISK_RT_CONTAINER_DYNAMIC_ARRAY, 777,
                OBELISK_RT_ELEMENT_BITS, OBELISK_RT_ELEMENT_SIGNED,
                sizeof(uint32_t), 1, 32, nullptr, 0, 2, 0, &array),
            OBELISK_RT_OK);
  uint32_t first = 3;
  uint32_t second = 1;
  ASSERT_EQ(obelisk_rt_v1_container_write(lane, array, 0, &first, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_container_write(lane, array, 1, &second, nullptr),
            OBELISK_RT_OK);

  obelisk_rt_arg_v1 argument{OBELISK_RT_ARG_MANAGED_CONTAINER, 0, 0, &array,
                             nullptr};
  obelisk_rt_format_env_v1 environment{};
  environment.time_multiplier = 1;
  obelisk_rt_buffer_v1 output{};
  ASSERT_EQ(obelisk_rt_v1_format(context, "%p", 2, &argument, 1, &environment,
                                 &output),
            OBELISK_RT_OK);
  EXPECT_EQ(
      std::string(reinterpret_cast<const char *>(output.data), output.size),
      // IEEE 1800-2017 21.2.1.7: an element that is a plain integral prints
      // "as they would unformatted", which 21.2.1 makes the default decimal
      // format of $display.
      "'{3, 1}");
  obelisk_rt_v1_buffer_release(&output);

  obelisk_rt_object_v1 *clone = nullptr;
  ASSERT_EQ(obelisk_rt_v1_container_clone(lane, array, &clone), OBELISK_RT_OK);
  uint32_t replacement = 9;
  ASSERT_EQ(
      obelisk_rt_v1_container_write(lane, clone, 0, &replacement, nullptr),
      OBELISK_RT_OK);
  uint32_t observed = 0;
  ASSERT_EQ(obelisk_rt_v1_container_read(array, 0, &observed, nullptr),
            OBELISK_RT_OK);
  EXPECT_EQ(observed, first);

  obelisk_rt_object_v1 *conflict = nullptr;
  EXPECT_EQ(obelisk_rt_v1_container_create_typed(
                lane, OBELISK_RT_CONTAINER_DYNAMIC_ARRAY, 777,
                OBELISK_RT_ELEMENT_REAL, 0, sizeof(double), 1, 64, nullptr, 0,
                1, 0, &conflict),
            OBELISK_RT_INVALID_DESIGN);
  obelisk_rt_object_v1 *negativeSize = nullptr;
  EXPECT_EQ(obelisk_rt_v1_container_create_typed(
                lane, OBELISK_RT_CONTAINER_DYNAMIC_ARRAY, 778,
                OBELISK_RT_ELEMENT_BITS, 0, sizeof(uint32_t), 1, 32, nullptr, 0,
                UINT64_MAX, 0, &negativeSize),
            OBELISK_RT_INVALID_ARGUMENT);
}

TEST_F(ManagedValueTest, CreatesElementsFromDeclaredDefaults) {
  // IEEE 1800-2023 7.2.2: struct member defaults make an element's default
  // differ from Table 6-7. Value plane {7, 0xa}, then a known unknown plane.
  const uint8_t image[] = {7, 0xa, 0, 0};
  ASSERT_EQ(obelisk_rt_v1_element_type_define_default(context, 779, image,
                                                      sizeof(image)),
            OBELISK_RT_OK);
  obelisk_rt_object_v1 *array = nullptr;
  ASSERT_EQ(obelisk_rt_v1_container_create_typed(
                lane, OBELISK_RT_CONTAINER_DYNAMIC_ARRAY, 779,
                OBELISK_RT_ELEMENT_AGGREGATE, OBELISK_RT_ELEMENT_FOUR_STATE, 2,
                1, 16, nullptr, 0, 2, 0, &array),
            OBELISK_RT_OK);
  std::array<uint8_t, 2> value{}, unknown{0xff, 0xff};
  ASSERT_EQ(
      obelisk_rt_v1_container_read(array, 1, value.data(), unknown.data()),
      OBELISK_RT_OK);
  EXPECT_EQ(value, (std::array<uint8_t, 2>{7, 0xa}));
  EXPECT_EQ(unknown, (std::array<uint8_t, 2>{0, 0}));

  // A conflicting default, or one declared after its type is in use, could
  // not apply to elements that already exist.
  const uint8_t other[] = {1, 0, 0, 0};
  EXPECT_EQ(obelisk_rt_v1_element_type_define_default(context, 779, other,
                                                      sizeof(other)),
            OBELISK_RT_INVALID_DESIGN);
  EXPECT_EQ(obelisk_rt_v1_element_type_define_default(context, 779, image,
                                                      sizeof(image)),
            OBELISK_RT_INVALID_DESIGN);
}

TEST_F(ManagedValueTest, FormatsFixedArrayElementsAsNestedPatterns) {
  constexpr std::array<uint64_t, 8> words{
      OBELISK_RT_CONTAINER_PATTERN_PLAN_VERSION,
      1,
      OBELISK_RT_ELEMENT_BITS,
      0,
      sizeof(uint32_t),
      32,
      2,
      sizeof(uint32_t),
  };
  auto writeWord = [](uint8_t *bytes, size_t index, uint64_t word) {
    for (size_t byte = 0; byte != sizeof(word); ++byte)
      bytes[index * sizeof(word) + byte] =
          static_cast<uint8_t>(word >> (byte * 8));
  };
  // Offset the plan by one byte to exercise the byte-oriented ABI's explicit
  // support for metadata globals that do not have native integer alignment.
  std::array<uint8_t, sizeof(words) + 1> patternStorage{};
  uint8_t *pattern = patternStorage.data() + 1;
  for (size_t index = 0; index != words.size(); ++index)
    writeWord(pattern, index, words[index]);
  obelisk_rt_object_v1 *array = nullptr;
  ASSERT_EQ(obelisk_rt_v1_container_create_typed_pattern(
                lane, OBELISK_RT_CONTAINER_DYNAMIC_ARRAY, 779,
                OBELISK_RT_ELEMENT_AGGREGATE, 0, 2 * sizeof(uint32_t), 1, 64,
                nullptr, 0, pattern, sizeof(words), 1, 0, &array),
            OBELISK_RT_OK);
  std::array<uint32_t, 2> value{1, 2};
  ASSERT_EQ(
      obelisk_rt_v1_container_write(lane, array, 0, value.data(), nullptr),
      OBELISK_RT_OK);

  obelisk_rt_arg_v1 argument{OBELISK_RT_ARG_MANAGED_CONTAINER, 0, 0, &array,
                             nullptr};
  obelisk_rt_format_env_v1 environment{};
  environment.time_multiplier = 1;
  obelisk_rt_buffer_v1 output{};
  ASSERT_EQ(obelisk_rt_v1_format(context, "%p", 2, &argument, 1, &environment,
                                 &output),
            OBELISK_RT_OK);
  EXPECT_EQ(
      std::string(reinterpret_cast<const char *>(output.data), output.size),
      "'{'{1, 2}}");
  obelisk_rt_v1_buffer_release(&output);

  std::array<uint8_t, sizeof(words) + 1> invalidStorage = patternStorage;
  uint8_t *invalid = invalidStorage.data() + 1;
  writeWord(invalid, 7, 1);
  obelisk_rt_object_v1 *rejected = nullptr;
  EXPECT_EQ(obelisk_rt_v1_container_create_typed_pattern(
                lane, OBELISK_RT_CONTAINER_DYNAMIC_ARRAY, 780,
                OBELISK_RT_ELEMENT_AGGREGATE, 0, 2 * sizeof(uint32_t), 1, 64,
                nullptr, 0, invalid, sizeof(words), 1, 0, &rejected),
            OBELISK_RT_INVALID_ARGUMENT);
}

TEST_F(ManagedValueTest, SeededBoundedRandomIsRepeatableAndBounded) {
  std::vector<uint64_t> first;
  std::vector<uint64_t> second;
  ASSERT_EQ(obelisk_rt_v1_context_seed(context, 12345), OBELISK_RT_OK);
  for (unsigned index = 0; index != 32; ++index) {
    uint64_t value = UINT64_MAX;
    ASSERT_EQ(obelisk_rt_v1_random_bounded(context, 7, &value), OBELISK_RT_OK);
    EXPECT_LT(value, 7u);
    first.push_back(value);
  }
  ASSERT_EQ(obelisk_rt_v1_context_seed(context, 12345), OBELISK_RT_OK);
  for (unsigned index = 0; index != first.size(); ++index) {
    uint64_t value = UINT64_MAX;
    ASSERT_EQ(obelisk_rt_v1_random_bounded(context, 7, &value), OBELISK_RT_OK);
    second.push_back(value);
  }
  EXPECT_EQ(first, second);
  uint64_t value = 0;
  EXPECT_EQ(obelisk_rt_v1_random_bounded(context, 0, &value),
            OBELISK_RT_INVALID_ARGUMENT);
  const char *valid[] = {"sim", "--seed=18446744073709551615"};
  EXPECT_EQ(obelisk_rt_v1_context_configure_argv(context, 2, valid),
            OBELISK_RT_OK);
  const char *invalid[] = {"sim", "--seed=18446744073709551616"};
  EXPECT_EQ(obelisk_rt_v1_context_configure_argv(context, 2, invalid),
            OBELISK_RT_INVALID_ARGUMENT);
}

TEST(RandomStateTest, MatchesPublishedPcgXshRrVector) {
  obelisk_rt_random_state_v1 state{};
  obelisk_rt_v1_random_state_seed(&state, 42, 54);
  constexpr uint32_t expected[] = {UINT32_C(0xa15c02b7), UINT32_C(0x7b47f409),
                                   UINT32_C(0xba1d3330), UINT32_C(0x83d2f293),
                                   UINT32_C(0xbfa4784b)};
  for (uint32_t value : expected)
    EXPECT_EQ(obelisk_rt_v1_random_state_next32(&state), value);
  EXPECT_EQ(state.increment, UINT64_C(109));
}

TEST(RandomStateTest, Next64CombinesConsecutivePublishedOutputs) {
  obelisk_rt_random_state_v1 state{};
  obelisk_rt_v1_random_state_seed(&state, 42, 54);
  EXPECT_EQ(obelisk_rt_v1_random_state_next64(&state),
            UINT64_C(0xa15c02b77b47f409));
  EXPECT_EQ(obelisk_rt_v1_random_state_next64(nullptr), 0u);
}

TEST(RandomStateTest, RandCCyclesAreBijectiveThroughWidth16) {
  for (uint32_t width = 1; width <= 16; ++width) {
    uint64_t cardinality = uint64_t{1} << width;
    std::vector<bool> seen(cardinality, false);
    uint64_t position = 0;
    for (uint64_t index = 0; index != cardinality; ++index) {
      uint64_t nextPosition = 0;
      uint64_t value = 0;
      ASSERT_EQ(obelisk_rt_v1_random_cycle_next(UINT64_C(0x0123456789abcdef),
                                                position, width, &nextPosition,
                                                &value),
                OBELISK_RT_OK)
          << "width " << width << " position " << position;
      ASSERT_LT(value, cardinality);
      ASSERT_FALSE(seen[value]) << "width " << width << " value " << value;
      seen[value] = true;
      position = nextPosition;
    }
    EXPECT_EQ(position, 0u);
  }
}

TEST(RandomStateTest, RandCKeySelectsTheCyclePermutation) {
  bool differs = false;
  for (uint64_t position = 0; position != 256; ++position) {
    uint64_t nextA = 0, nextB = 0, valueA = 0, valueB = 0;
    ASSERT_EQ(obelisk_rt_v1_random_cycle_next(1, position, 8, &nextA, &valueA),
              OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_random_cycle_next(2, position, 8, &nextB, &valueB),
              OBELISK_RT_OK);
    EXPECT_EQ(nextA, nextB);
    differs |= valueA != valueB;
  }
  EXPECT_TRUE(differs);
}

TEST(RandomStateTest, RandCRejectsMalformedState) {
  uint64_t next = 0;
  uint64_t value = 0;
  EXPECT_EQ(obelisk_rt_v1_random_cycle_next(0, UINT32_MAX, 32, &next, &value),
            OBELISK_RT_OK);
  EXPECT_EQ(next, 0u);
  EXPECT_EQ(obelisk_rt_v1_random_cycle_next(0, 0, 0, &next, &value),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_random_cycle_next(0, 0, 33, &next, &value),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_random_cycle_next(0, 16, 4, &next, &value),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_random_cycle_next(0, 0, 4, nullptr, &value),
            OBELISK_RT_INVALID_ARGUMENT);
}

TEST_F(ManagedValueTest, RandomSeedRestartsTheActiveStream) {
  EXPECT_EQ(obelisk_rt_v1_random_seed(nullptr, 123),
            OBELISK_RT_INVALID_ARGUMENT);
  ASSERT_EQ(obelisk_rt_v1_random_seed(context, 123), OBELISK_RT_OK);
  uint64_t first = 0;
  ASSERT_EQ(obelisk_rt_v1_random_next(context, &first), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_random_seed(context, 123), OBELISK_RT_OK);
  uint64_t second = 0;
  ASSERT_EQ(obelisk_rt_v1_random_next(context, &second), OBELISK_RT_OK);
  EXPECT_EQ(first, second);
}

TEST_F(ManagedValueTest, RandomStateSnapshotRestoresExactly) {
  ASSERT_EQ(obelisk_rt_v1_context_seed(context, 987654321), OBELISK_RT_OK);
  obelisk_rt_random_state_v1 snapshot{};
  ASSERT_EQ(obelisk_rt_v1_random_get_state(context, &snapshot), OBELISK_RT_OK);
  EXPECT_NE(snapshot.increment & 1, 0u);
  uint64_t first = 0;
  uint64_t second = 0;
  ASSERT_EQ(obelisk_rt_v1_random_next(context, &first), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_random_set_state(context, &snapshot), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_random_next(context, &second), OBELISK_RT_OK);
  EXPECT_EQ(first, second);
  snapshot.increment &= ~uint64_t{1};
  EXPECT_EQ(obelisk_rt_v1_random_set_state(context, &snapshot),
            OBELISK_RT_INVALID_ARGUMENT);
}

TEST(RandomStateTest, BoundedDrawCoversRangeDeterministically) {
  obelisk_rt_random_state_v1 left{};
  obelisk_rt_random_state_v1 right{};
  obelisk_rt_v1_random_state_seed(&left, 1234, 99);
  right = left;
  std::array<uint64_t, 7> counts{};
  for (unsigned index = 0; index != 7000; ++index) {
    uint64_t lhs = UINT64_MAX;
    uint64_t rhs = UINT64_MAX;
    ASSERT_EQ(obelisk_rt_v1_random_state_bounded(&left, counts.size(), &lhs),
              OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_random_state_bounded(&right, counts.size(), &rhs),
              OBELISK_RT_OK);
    ASSERT_EQ(lhs, rhs);
    ASSERT_LT(lhs, counts.size());
    ++counts[lhs];
  }
  for (uint64_t count : counts)
    EXPECT_GT(count, 850u);
}

TEST_F(ManagedValueTest, StringOperationsSurviveCollectionAtEveryAllocation) {
  ASSERT_EQ(obelisk_rt_v1_gc_set_threshold(context, 1), OBELISK_RT_OK);
  obelisk_rt_string_v1 left = 0;
  obelisk_rt_string_v1 right = 0;
  ASSERT_EQ(obelisk_rt_v1_string_create(lane, "hello", 5, &left),
            OBELISK_RT_OK);
  obelisk_rt_gc_managed_root_v1 leftRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_managed_root_push(lane, &leftRoot, &left),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_string_create(lane, " WORLD", 6, &right),
            OBELISK_RT_OK);
  obelisk_rt_gc_managed_root_v1 rightRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_managed_root_push(lane, &rightRoot, &right),
            OBELISK_RT_OK);

  obelisk_rt_string_v1 joined = 0;
  ASSERT_EQ(obelisk_rt_v1_string_concat(lane, left, right, &joined),
            OBELISK_RT_OK);
  obelisk_rt_gc_managed_root_v1 joinedRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_managed_root_push(lane, &joinedRoot, &joined),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_string_compare_insensitive(joined, 0), 1);

  obelisk_rt_string_v1 lower = 0;
  ASSERT_EQ(obelisk_rt_v1_string_case_convert(lane, joined, 0, &lower),
            OBELISK_RT_OK);
  obelisk_rt_gc_managed_root_v1 lowerRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_managed_root_push(lane, &lowerRoot, &lower),
            OBELISK_RT_OK);
  const char *bytes = nullptr;
  uint64_t size = 0;
  char scratch[8];
  ASSERT_EQ(obelisk_rt_v1_string_view(lower, scratch, &bytes, &size),
            OBELISK_RT_OK);
  EXPECT_EQ(std::string(bytes, size), "hello world");

  obelisk_rt_string_v1 substring = 0;
  ASSERT_EQ(obelisk_rt_v1_string_substr(lane, lower, 6, 10, &substring),
            OBELISK_RT_OK);
  obelisk_rt_gc_managed_root_v1 substringRoot{};
  ASSERT_EQ(
      obelisk_rt_v1_gc_managed_root_push(lane, &substringRoot, &substring),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_string_view(substring, scratch, &bytes, &size),
            OBELISK_RT_OK);
  EXPECT_EQ(std::string(bytes, size), "world");

  obelisk_rt_string_v1 modified = 0;
  ASSERT_EQ(obelisk_rt_v1_string_putc(lane, substring, 0, 'W', &modified),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_string_view(modified, scratch, &bytes, &size),
            OBELISK_RT_OK);
  EXPECT_EQ(std::string(bytes, size), "World");
  EXPECT_EQ(obelisk_rt_v1_string_getc(substring, 0), 'w');

  EXPECT_EQ(obelisk_rt_v1_gc_managed_root_pop(lane, &substringRoot),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_managed_root_pop(lane, &lowerRoot), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_managed_root_pop(lane, &joinedRoot),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_managed_root_pop(lane, &rightRoot), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_managed_root_pop(lane, &leftRoot), OBELISK_RT_OK);
}

TEST_F(ManagedValueTest, AccountsActualSmallAndLargeStringExtents) {
  ASSERT_EQ(obelisk_rt_v1_gc_set_threshold(context, UINT64_MAX), OBELISK_RT_OK);
  obelisk_rt_string_v1 small = 0;
  ASSERT_EQ(obelisk_rt_v1_string_create(lane, "abc", 3, &small), OBELISK_RT_OK);
  std::string largeBytes(40'000, 'x');
  obelisk_rt_string_v1 large = 0;
  ASSERT_EQ(obelisk_rt_v1_string_create(lane, largeBytes.data(),
                                        largeBytes.size(), &large),
            OBELISK_RT_OK);
  obelisk_rt_gc_managed_root_v1 roots[2]{};
  ASSERT_EQ(obelisk_rt_v1_gc_managed_root_push(lane, &roots[0], &small),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_managed_root_push(lane, &roots[1], &large),
            OBELISK_RT_OK);
  obelisk_rt_gc_statistics_v1 statistics{};
  ASSERT_EQ(obelisk_rt_v1_gc_statistics(context, &statistics), OBELISK_RT_OK);
  EXPECT_EQ(statistics.live_objects, 1u);
  EXPECT_EQ(statistics.live_bytes, 40'048u);
  EXPECT_EQ(statistics.large_allocation_count, 1u);

  EXPECT_EQ(obelisk_rt_v1_gc_managed_root_pop(lane, &roots[1]), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_managed_root_pop(lane, &roots[0]), OBELISK_RT_OK);
  small = 0;
  large = 0;
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_statistics(context, &statistics), OBELISK_RT_OK);
  EXPECT_EQ(statistics.live_objects, 0u);
  EXPECT_EQ(statistics.live_bytes, 0u);
  EXPECT_EQ(statistics.reclaimed_objects, 1u);
}

TEST_F(ManagedValueTest, RegistersEquivalentElementDescriptorsByStableID) {
  const obelisk_rt_trace_entry_v1 traceEntry{
      0,      0, 1, OBELISK_RT_TRACE_STRONG, OBELISK_RT_MANAGED_SLOT_STRING,
      nullptr};
  const obelisk_rt_trace_layout_v1 traceLayout{OBELISK_RT_VERSION,
                                               0,
                                               sizeof(obelisk_rt_string_v1),
                                               alignof(obelisk_rt_string_v1),
                                               &traceEntry,
                                               1};
  const obelisk_rt_element_type_v1 first{OBELISK_RT_VERSION,
                                         OBELISK_RT_ELEMENT_STRING,
                                         42,
                                         0,
                                         0,
                                         sizeof(obelisk_rt_string_v1),
                                         alignof(obelisk_rt_string_v1),
                                         0,
                                         &traceLayout,
                                         nullptr};
  obelisk_rt_trace_entry_v1 equivalentEntry = traceEntry;
  obelisk_rt_trace_layout_v1 equivalentLayout = traceLayout;
  equivalentLayout.entries = &equivalentEntry;
  obelisk_rt_element_type_v1 equivalent = first;
  equivalent.trace = &equivalentLayout;
  EXPECT_EQ(obelisk_rt_v1_element_type_validate(&first), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_element_type_register(context, &first),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_element_type_register(context, &equivalent),
            OBELISK_RT_OK);

  // Reusing the ID for a different kind must be rejected as a conflict, so the
  // descriptor has to be valid on its own first.
  obelisk_rt_element_type_v1 conflicting = first;
  conflicting.kind = OBELISK_RT_ELEMENT_CLASS_HANDLE;
  conflicting.value_size = sizeof(obelisk_rt_managed_word_v1);
  conflicting.alignment = alignof(obelisk_rt_managed_word_v1);
  obelisk_rt_trace_layout_v1 conflictingLayout = traceLayout;
  conflictingLayout.size = conflicting.value_size;
  conflictingLayout.alignment = conflicting.alignment;
  obelisk_rt_trace_entry_v1 conflictingEntry = traceEntry;
  conflictingEntry.kind = OBELISK_RT_TRACE_STRONG;
  conflictingEntry.slot_kind = OBELISK_RT_MANAGED_SLOT_CLASS;
  conflictingLayout.entries = &conflictingEntry;
  conflicting.trace = &conflictingLayout;
  ASSERT_EQ(obelisk_rt_v1_element_type_validate(&conflicting), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_element_type_register(context, &conflicting),
            OBELISK_RT_INVALID_DESIGN);
  conflicting = first;
  conflicting.type_id = 0;
  EXPECT_EQ(obelisk_rt_v1_element_type_validate(&conflicting),
            OBELISK_RT_INVALID_DESIGN);
}

TEST_F(ManagedValueTest, ContainerConstructionUsesRegisteredDescriptorIDs) {
  obelisk_rt_object_v1 *array = nullptr;
  ASSERT_EQ(obelisk_rt_v1_dynamic_array_create(lane, &wordElement, 1, &array),
            OBELISK_RT_OK);

  obelisk_rt_element_type_v1 equivalent = wordElement;
  obelisk_rt_object_v1 *equivalentArray = nullptr;
  EXPECT_EQ(obelisk_rt_v1_dynamic_array_create(lane, &equivalent, 1,
                                               &equivalentArray),
            OBELISK_RT_OK);

  obelisk_rt_element_type_v1 conflicting = wordElement;
  conflicting.kind = OBELISK_RT_ELEMENT_REAL;
  obelisk_rt_object_v1 *conflictingArray = nullptr;
  EXPECT_EQ(obelisk_rt_v1_dynamic_array_create(lane, &conflicting, 1,
                                               &conflictingArray),
            OBELISK_RT_INVALID_DESIGN);
  EXPECT_EQ(conflictingArray, nullptr);
}

TEST_F(ManagedValueTest, FourStateDefaultsKeepPaddingClear) {
  // Native code loads a packed element as an iN value, so the X default sets
  // only the element's own unknown bits.
  obelisk_rt_element_type_v1 tenBits = logicElement;
  tenBits.type_id = 92;
  tenBits.value_size = 2;
  tenBits.bit_width = 10;
  obelisk_rt_object_v1 *array = nullptr;
  ASSERT_EQ(obelisk_rt_v1_dynamic_array_create(lane, &tenBits, 1, &array),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 root{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &root, &array), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_dynamic_array_resize(lane, array, 2), OBELISK_RT_OK);
  for (int64_t index : {0, 1}) {
    std::array<uint8_t, 2> value{0xaa, 0xaa}, unknown{};
    ASSERT_EQ(obelisk_rt_v1_container_read(array, index, value.data(),
                                           unknown.data()),
              OBELISK_RT_OK);
    EXPECT_EQ(value, (std::array<uint8_t, 2>{0, 0})) << index;
    EXPECT_EQ(unknown, (std::array<uint8_t, 2>{0xff, 0x03})) << index;
  }
  ASSERT_EQ(obelisk_rt_v1_gc_root_pop(lane, &root), OBELISK_RT_OK);
}

TEST_F(ManagedValueTest, StoredSemaphoresKeepTheirIdentity) {
  // A semaphore is a built-in class (IEEE 1800-2023 15.3): storing its handle
  // in a container and copying that container share the one object.
  obelisk_rt_object_v1 *semaphore = nullptr, *array = nullptr, *copy = nullptr;
  ASSERT_EQ(obelisk_rt_v1_semaphore_create(lane, 1, &semaphore),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 semaphoreRoot{}, arrayRoot{}, copyRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &semaphoreRoot, &semaphore),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_dynamic_array_create(lane, &containerElement, 1,
                                               &array),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &arrayRoot, &array),
            OBELISK_RT_OK);
  obelisk_rt_managed_word_v1 word =
      obelisk_rt_managed_word_from_object(semaphore);
  ASSERT_EQ(obelisk_rt_v1_container_write(lane, array, 0, &word, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_container_clone(lane, array, &copy), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &copyRoot, &copy), OBELISK_RT_OK);
  for (obelisk_rt_object_v1 *container : {array, copy}) {
    obelisk_rt_managed_word_v1 stored = 0;
    ASSERT_EQ(obelisk_rt_v1_container_read(container, 0, &stored, nullptr),
              OBELISK_RT_OK);
    EXPECT_EQ(stored, word);
  }
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &copyRoot), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &arrayRoot), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &semaphoreRoot), OBELISK_RT_OK);
}

TEST_F(ManagedValueTest, ElementDescriptorsRejectWeakManagedSlots) {
  obelisk_rt_trace_entry_v1 weakEntry = managedWordTraceEntry;
  weakEntry.kind = OBELISK_RT_TRACE_WEAK;
  obelisk_rt_trace_layout_v1 weakLayout = managedWordTraceLayout;
  weakLayout.entries = &weakEntry;
  obelisk_rt_element_type_v1 weakElement = stringElement;
  weakElement.type_id = 91;
  weakElement.trace = &weakLayout;
  EXPECT_EQ(obelisk_rt_v1_element_type_validate(&weakElement),
            OBELISK_RT_INVALID_DESIGN);
}

TEST_F(ManagedValueTest, DynamicArraysResizeAndCloneWithValueSemantics) {
  obelisk_rt_object_v1 *array = nullptr;
  ASSERT_EQ(obelisk_rt_v1_dynamic_array_create(lane, &wordElement, 2, &array),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 arrayRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &arrayRoot, &array),
            OBELISK_RT_OK);
  uint64_t first = 11;
  uint64_t second = 22;
  ASSERT_EQ(obelisk_rt_v1_container_write(lane, array, 0, &first, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_container_write(lane, array, 1, &second, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_dynamic_array_resize(lane, array, 5), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_container_size(array), 5u);
  uint64_t value = 0;
  ASSERT_EQ(obelisk_rt_v1_container_read(array, 0, &value, nullptr),
            OBELISK_RT_OK);
  EXPECT_EQ(value, first);
  ASSERT_EQ(obelisk_rt_v1_container_read(array, 1, &value, nullptr),
            OBELISK_RT_OK);
  EXPECT_EQ(value, second);
  value = 99;
  ASSERT_EQ(obelisk_rt_v1_container_read(array, 4, &value, nullptr),
            OBELISK_RT_OK);
  EXPECT_EQ(value, 0u);

  obelisk_rt_object_v1 *copy = nullptr;
  ASSERT_EQ(obelisk_rt_v1_container_clone(lane, array, &copy), OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 copyRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &copyRoot, &copy), OBELISK_RT_OK);
  uint64_t replacement = 77;
  ASSERT_EQ(obelisk_rt_v1_container_write(lane, copy, 0, &replacement, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_container_read(array, 0, &value, nullptr),
            OBELISK_RT_OK);
  EXPECT_EQ(value, first);
  ASSERT_EQ(obelisk_rt_v1_container_read(copy, 0, &value, nullptr),
            OBELISK_RT_OK);
  EXPECT_EQ(value, replacement);

  EXPECT_EQ(obelisk_rt_v1_container_delete(copy), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_container_size(copy), 0u);
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &copyRoot), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &arrayRoot), OBELISK_RT_OK);
}

TEST_F(ManagedValueTest, FourStateElementsPreserveBothPlanes) {
  obelisk_rt_object_v1 *array = nullptr;
  ASSERT_EQ(obelisk_rt_v1_dynamic_array_create(lane, &logicElement, 1, &array),
            OBELISK_RT_OK);
  uint8_t value = 0xa;
  uint8_t unknown = 0x4;
  ASSERT_EQ(obelisk_rt_v1_container_write(lane, array, 0, &value, &unknown),
            OBELISK_RT_OK);
  value = 0;
  unknown = 0;
  ASSERT_EQ(obelisk_rt_v1_container_read(array, 0, &value, &unknown),
            OBELISK_RT_OK);
  EXPECT_EQ(value, 0xa);
  EXPECT_EQ(unknown, 0x4);

  obelisk_rt_object_v1 *assoc = nullptr;
  ASSERT_EQ(obelisk_rt_v1_assoc_create(
                lane, &wordElement, OBELISK_RT_ASSOC_KEY_UNSIGNED, 8, &assoc),
            OBELISK_RT_OK);
  obelisk_rt_assoc_key_v1 xKey{
      OBELISK_RT_ASSOC_KEY_UNSIGNED, 0, 8, {3}, {1}, 0};
  uint64_t stored = 12;
  ASSERT_EQ(obelisk_rt_v1_assoc_write(lane, assoc, &xKey, &stored, nullptr),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_container_size(assoc), 0u);
}

TEST_F(ManagedValueTest, CheckedContainerAccessRejectsMismatchedPlanes) {
  obelisk_rt_object_v1 *words = nullptr;
  ASSERT_EQ(obelisk_rt_v1_dynamic_array_create(lane, &wordElement, 1, &words),
            OBELISK_RT_OK);
  uint8_t undersized[2] = {0xa5, 0x5a};
  EXPECT_EQ(obelisk_rt_v1_container_read_checked(
                words, 0, undersized, sizeof(undersized), nullptr, 0),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(undersized[0], 0xa5);
  EXPECT_EQ(undersized[1], 0x5a);
  EXPECT_EQ(obelisk_rt_v1_container_write_checked(
                lane, words, 0, undersized, sizeof(undersized), nullptr, 0),
            OBELISK_RT_INVALID_ARGUMENT);

  uint64_t word = 42;
  ASSERT_EQ(obelisk_rt_v1_container_write_checked(lane, words, 0, &word,
                                                  sizeof(word), nullptr, 0),
            OBELISK_RT_OK);
  word = 0;
  ASSERT_EQ(obelisk_rt_v1_container_read_checked(words, 0, &word, sizeof(word),
                                                 nullptr, 0),
            OBELISK_RT_OK);
  EXPECT_EQ(word, 42u);
  uint64_t paddedWord[2] = {84, UINT64_C(0xfeedfacecafebeef)};
  ASSERT_EQ(obelisk_rt_v1_container_write_checked(
                lane, words, 0, paddedWord, sizeof(paddedWord), nullptr, 0),
            OBELISK_RT_OK);
  paddedWord[0] = 0;
  ASSERT_EQ(obelisk_rt_v1_container_read_checked(
                words, 0, paddedWord, sizeof(paddedWord), nullptr, 0),
            OBELISK_RT_OK);
  EXPECT_EQ(paddedWord[0], 84u);
  EXPECT_EQ(paddedWord[1], UINT64_C(0xfeedfacecafebeef));

  obelisk_rt_object_v1 *logic = nullptr;
  ASSERT_EQ(obelisk_rt_v1_dynamic_array_create(lane, &logicElement, 1, &logic),
            OBELISK_RT_OK);
  uint8_t value = 0xa;
  uint8_t unknown = 0x4;
  EXPECT_EQ(obelisk_rt_v1_container_write_checked(lane, logic, 0, &value,
                                                  sizeof(value), nullptr, 0),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_container_read_checked(logic, 0, &value,
                                                 sizeof(value), &unknown, 0),
            OBELISK_RT_INVALID_ARGUMENT);
  ASSERT_EQ(obelisk_rt_v1_container_write_checked(lane, logic, 0, &value,
                                                  sizeof(value), &unknown,
                                                  sizeof(unknown)),
            OBELISK_RT_OK);
}

TEST(AggregateBitstreamTest, ValidatesPlansBeforeWriting) {
  constexpr std::array<uint64_t, 16> words{
      uint64_t(OBELISK_RT_AGGREGATE_BITSTREAM_PLAN_MAGIC) |
          (uint64_t(OBELISK_RT_AGGREGATE_BITSTREAM_PLAN_VERSION) << 32),
      2,
      16,
      16,
      uint64_t(OBELISK_RT_AGGREGATE_BITSTREAM_REPEAT) | (uint64_t(1) << 32),
      0,
      2,
      8,
      8,
      8,
      OBELISK_RT_AGGREGATE_BITSTREAM_COPY,
      0,
      8,
      0,
      0,
      8};
  std::array<uint8_t, words.size() * sizeof(uint64_t)> plan{};
  for (size_t index = 0; index != words.size(); ++index)
    for (unsigned byte = 0; byte != sizeof(uint64_t); ++byte)
      plan[index * sizeof(uint64_t) + byte] =
          static_cast<uint8_t>(words[index] >> (byte * 8));
  std::array<uint8_t, 2> input{0x12, 0x34};
  std::array<uint8_t, 2> output{0xa5, 0x5a};
  ASSERT_EQ(obelisk_rt_v1_aggregate_export_bitstream(
                input.data(), nullptr, input.size(), 16, 0, output.data(),
                nullptr, output.size(), 16, 0, plan.data(), plan.size()),
            OBELISK_RT_OK);
  EXPECT_EQ(output, (std::array<uint8_t, 2>{0x34, 0x12}));

  auto corrupt = plan;
  corrupt[7 * sizeof(uint64_t)] = 7; // REPEAT stride must be eight bits.
  output = {0xa5, 0x5a};
  EXPECT_EQ(obelisk_rt_v1_aggregate_export_bitstream(
                input.data(), nullptr, input.size(), 16, 0, output.data(),
                nullptr, output.size(), 16, 0, corrupt.data(), corrupt.size()),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(output, (std::array<uint8_t, 2>{0xa5, 0x5a}));

  input = {0x12, 0x34};
  EXPECT_EQ(obelisk_rt_v1_aggregate_export_bitstream(
                input.data(), nullptr, input.size(), 16, 0, input.data(),
                nullptr, input.size(), 16, 0, plan.data(), plan.size()),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(input, (std::array<uint8_t, 2>{0x12, 0x34}));
}

TEST_F(ManagedValueTest, CreateLikePreservesSequentialContainerMetadata) {
  obelisk_rt_object_v1 *array = nullptr;
  ASSERT_EQ(obelisk_rt_v1_dynamic_array_create(lane, &wordElement, 1, &array),
            OBELISK_RT_OK);
  obelisk_rt_object_v1 *created = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_container_create_like(lane, array, nullptr, 3, &created),
      OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_container_size(created), 3u);
  uint64_t value = 17;
  ASSERT_EQ(obelisk_rt_v1_container_write(lane, created, 2, &value, nullptr),
            OBELISK_RT_OK);
  value = 0;
  ASSERT_EQ(obelisk_rt_v1_container_read(created, 2, &value, nullptr),
            OBELISK_RT_OK);
  EXPECT_EQ(value, 17u);

  obelisk_rt_object_v1 *nullResult =
      reinterpret_cast<obelisk_rt_object_v1 *>(UINTPTR_MAX);
  EXPECT_EQ(obelisk_rt_v1_container_create_like(lane, nullptr, nullptr, 0,
                                                &nullResult),
            OBELISK_RT_OK);
  EXPECT_EQ(nullResult, nullptr);
}

TEST_F(ManagedValueTest, ContainersTraceStringsAndRecursivelyCloneContainers) {
  ASSERT_EQ(obelisk_rt_v1_gc_set_threshold(context, 1), OBELISK_RT_OK);
  obelisk_rt_string_v1 text = 0;
  ASSERT_EQ(obelisk_rt_v1_string_create(lane, "kept", 4, &text), OBELISK_RT_OK);
  obelisk_rt_gc_managed_root_v1 textRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_managed_root_push(lane, &textRoot, &text),
            OBELISK_RT_OK);
  obelisk_rt_object_v1 *strings = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_dynamic_array_create(lane, &stringElement, 1, &strings),
      OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 stringsRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &stringsRoot, &strings),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_container_write(lane, strings, 0, &text, nullptr),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &stringsRoot), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_managed_root_pop(lane, &textRoot), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &stringsRoot, &strings),
            OBELISK_RT_OK);
  text = 0;
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_container_read(strings, 0, &text, nullptr),
            OBELISK_RT_OK);
  const char *bytes = nullptr;
  uint64_t size = 0;
  char scratch[8];
  ASSERT_EQ(obelisk_rt_v1_string_view(text, scratch, &bytes, &size),
            OBELISK_RT_OK);
  EXPECT_EQ(std::string(bytes, size), "kept");

  obelisk_rt_object_v1 *inner = nullptr;
  ASSERT_EQ(obelisk_rt_v1_dynamic_array_create(lane, &wordElement, 1, &inner),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 innerRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &innerRoot, &inner),
            OBELISK_RT_OK);
  uint64_t original = 12;
  ASSERT_EQ(obelisk_rt_v1_container_write(lane, inner, 0, &original, nullptr),
            OBELISK_RT_OK);
  obelisk_rt_object_v1 *outer = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_dynamic_array_create(lane, &containerElement, 1, &outer),
      OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 outerRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &outerRoot, &outer),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_container_write(lane, outer, 0, &inner, nullptr),
            OBELISK_RT_OK);
  uint64_t changed = 34;
  ASSERT_EQ(obelisk_rt_v1_container_write(lane, inner, 0, &changed, nullptr),
            OBELISK_RT_OK);
  obelisk_rt_object_v1 *stored = nullptr;
  ASSERT_EQ(obelisk_rt_v1_container_read(outer, 0, &stored, nullptr),
            OBELISK_RT_OK);
  EXPECT_NE(stored, inner);
  uint64_t storedValue = 0;
  ASSERT_EQ(obelisk_rt_v1_container_read(stored, 0, &storedValue, nullptr),
            OBELISK_RT_OK);
  EXPECT_EQ(storedValue, original);

  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &outerRoot), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &innerRoot), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &stringsRoot), OBELISK_RT_OK);
}

TEST_F(ManagedValueTest, DPIAggregateRootsTraceMultipleStringsWithScalarRoot) {
  ASSERT_EQ(obelisk_rt_v1_gc_lane_leave(lane), OBELISK_RT_OK);
  obelisk_rt_gc_lane_v1 *executionLane = nullptr;
  bool executionEntered = false;
  ASSERT_EQ(obelisk_rt_managed_execution_enter(context, &executionLane,
                                               &executionEntered),
            OBELISK_RT_OK);
  ASSERT_NE(executionLane, nullptr);
  ASSERT_EQ(obelisk_rt_v1_gc_set_threshold(context, 1), OBELISK_RT_OK);
  constexpr int64_t stringPlan[] = {0, 0, 0, 8, 64, 0, 0, 0};

  obelisk_rt_string_v1 aggregateStrings[2] = {};
  void *aggregateRoots[2] = {};
  ASSERT_EQ(obelisk_rt_v1_string_create(executionLane, "first-aggregate", 15,
                                        &aggregateStrings[0]),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_dpi_aggregate_roots_push(
                context, &aggregateStrings[0], sizeof(aggregateStrings[0]), 64,
                stringPlan, std::size(stringPlan), &aggregateRoots[0]),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_string_create(executionLane, "second-aggregate", 16,
                                        &aggregateStrings[1]),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_dpi_aggregate_roots_push(
                context, &aggregateStrings[1], sizeof(aggregateStrings[1]), 64,
                stringPlan, std::size(stringPlan), &aggregateRoots[1]),
            OBELISK_RT_OK);

  obelisk_rt_string_v1 scalarString = 0;
  ASSERT_EQ(obelisk_rt_v1_string_create(executionLane, "scalar-string", 13,
                                        &scalarString),
            OBELISK_RT_OK);
  obelisk_rt_gc_managed_root_v1 scalarRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_managed_root_push(executionLane, &scalarRoot,
                                               &scalarString),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_collect(executionLane), OBELISK_RT_OK);

  auto expectString = [](obelisk_rt_string_v1 value, std::string expected) {
    char scratch[8];
    const char *bytes = nullptr;
    uint64_t size = 0;
    ASSERT_EQ(obelisk_rt_v1_string_view(value, scratch, &bytes, &size),
              OBELISK_RT_OK);
    EXPECT_EQ(std::string(bytes, size), expected);
  };
  expectString(aggregateStrings[0], "first-aggregate");
  expectString(aggregateStrings[1], "second-aggregate");
  expectString(scalarString, "scalar-string");

  EXPECT_EQ(obelisk_rt_v1_gc_managed_root_pop(executionLane, &scalarRoot),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_dpi_aggregate_roots_pop(context, aggregateRoots[1]),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_dpi_aggregate_roots_pop(context, aggregateRoots[0]),
            OBELISK_RT_OK);
  obelisk_rt_managed_execution_leave(executionLane, executionEntered);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_enter(lane), OBELISK_RT_OK);
}

TEST_F(ManagedValueTest,
       RegisteredAggregateContainersTraceAndCloneManagedMembers) {
  struct Aggregate {
    obelisk_rt_string_v1 text;
    obelisk_rt_object_v1 *nested;
    uint64_t number;
  };
  const obelisk_rt_element_trace_slot_v1 traceSlots[] = {
      {offsetof(Aggregate, text), OBELISK_RT_MANAGED_SLOT_STRING, 0},
      {offsetof(Aggregate, nested), OBELISK_RT_MANAGED_SLOT_CONTAINER, 0},
  };

  obelisk_rt_object_v1 *outer = nullptr;
  ASSERT_EQ(obelisk_rt_v1_container_create_typed(
                lane, OBELISK_RT_CONTAINER_DYNAMIC_ARRAY, 778,
                OBELISK_RT_ELEMENT_AGGREGATE, 0, sizeof(Aggregate),
                alignof(Aggregate), 0, traceSlots, std::size(traceSlots), 1, 0,
                &outer),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 outerRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &outerRoot, &outer),
            OBELISK_RT_OK);

  obelisk_rt_object_v1 *inner = nullptr;
  ASSERT_EQ(obelisk_rt_v1_dynamic_array_create(lane, &wordElement, 1, &inner),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 innerRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &innerRoot, &inner),
            OBELISK_RT_OK);
  uint64_t original = 12;
  ASSERT_EQ(obelisk_rt_v1_container_write(lane, inner, 0, &original, nullptr),
            OBELISK_RT_OK);

  obelisk_rt_string_v1 text = 0;
  ASSERT_EQ(obelisk_rt_v1_string_create(lane, "kept", 4, &text), OBELISK_RT_OK);
  obelisk_rt_gc_managed_root_v1 textRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_managed_root_push(lane, &textRoot, &text),
            OBELISK_RT_OK);
  Aggregate input{text, inner, 7};
  ASSERT_EQ(obelisk_rt_v1_container_write(lane, outer, 0, &input, nullptr),
            OBELISK_RT_OK);

  uint64_t changed = 34;
  ASSERT_EQ(obelisk_rt_v1_container_write(lane, inner, 0, &changed, nullptr),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_managed_root_pop(lane, &textRoot), OBELISK_RT_OK);
  text = 0;
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);

  Aggregate stored{};
  ASSERT_EQ(obelisk_rt_v1_container_read(outer, 0, &stored, nullptr),
            OBELISK_RT_OK);
  EXPECT_EQ(stored.number, 7u);
  char scratch[8];
  const char *bytes = nullptr;
  uint64_t size = 0;
  ASSERT_EQ(obelisk_rt_v1_string_view(stored.text, scratch, &bytes, &size),
            OBELISK_RT_OK);
  EXPECT_EQ(std::string(bytes, size), "kept");
  EXPECT_NE(stored.nested, inner);
  uint64_t storedValue = 0;
  ASSERT_EQ(
      obelisk_rt_v1_container_read(stored.nested, 0, &storedValue, nullptr),
      OBELISK_RT_OK);
  EXPECT_EQ(storedValue, original);

  obelisk_rt_object_v1 *copy = nullptr;
  ASSERT_EQ(obelisk_rt_v1_container_clone(lane, outer, &copy), OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 copyRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &copyRoot, &copy), OBELISK_RT_OK);
  Aggregate copied{};
  ASSERT_EQ(obelisk_rt_v1_container_read(copy, 0, &copied, nullptr),
            OBELISK_RT_OK);
  EXPECT_NE(copied.nested, stored.nested);
  ASSERT_EQ(
      obelisk_rt_v1_container_write(lane, copied.nested, 0, &changed, nullptr),
      OBELISK_RT_OK);
  storedValue = 0;
  ASSERT_EQ(
      obelisk_rt_v1_container_read(stored.nested, 0, &storedValue, nullptr),
      OBELISK_RT_OK);
  EXPECT_EQ(storedValue, original);

  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &copyRoot), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &innerRoot), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &outerRoot), OBELISK_RT_OK);
}

TEST_F(ManagedValueTest, QueueRingOperationsPreserveLogicalOrder) {
  obelisk_rt_object_v1 *queue = nullptr;
  ASSERT_EQ(obelisk_rt_v1_queue_create(lane, &wordElement, UINT64_MAX, &queue),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 queueRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &queueRoot, &queue),
            OBELISK_RT_OK);
  for (uint64_t value = 1; value <= 5; ++value)
    ASSERT_EQ(obelisk_rt_v1_queue_push(lane, queue, 0, &value, nullptr),
              OBELISK_RT_OK);
  uint64_t value = 0;
  uint32_t present = 0;
  ASSERT_EQ(obelisk_rt_v1_queue_pop(queue, 1, &value, nullptr, &present),
            OBELISK_RT_OK);
  EXPECT_EQ(present, 1u);
  EXPECT_EQ(value, 1u);
  uint64_t zero = 0;
  ASSERT_EQ(obelisk_rt_v1_queue_push(lane, queue, 1, &zero, nullptr),
            OBELISK_RT_OK);
  uint64_t inserted = 99;
  ASSERT_EQ(obelisk_rt_v1_queue_insert(lane, queue, 3, &inserted, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_queue_delete_index(queue, 1), OBELISK_RT_OK);
  const uint64_t expected[] = {0, 3, 99, 4, 5};
  ASSERT_EQ(obelisk_rt_v1_container_size(queue), std::size(expected));
  for (uint64_t index = 0; index != std::size(expected); ++index) {
    value = UINT64_MAX;
    ASSERT_EQ(obelisk_rt_v1_container_read(queue, index, &value, nullptr),
              OBELISK_RT_OK);
    EXPECT_EQ(value, expected[index]);
  }
  uint64_t appended = 6;
  ASSERT_EQ(obelisk_rt_v1_container_write(lane, queue,
                                          obelisk_rt_v1_container_size(queue),
                                          &appended, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_queue_pop(queue, 0, &value, nullptr, &present),
            OBELISK_RT_OK);
  EXPECT_EQ(value, appended);
  ASSERT_EQ(obelisk_rt_v1_container_delete(queue), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_container_size(queue), 0u);
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &queueRoot), OBELISK_RT_OK);
}

TEST_F(ManagedValueTest, CoverageSetSnapshotUsesLogicalContainerOrder) {
  obelisk_rt_object_v1 *queue = nullptr;
  ASSERT_EQ(obelisk_rt_v1_queue_create(lane, &wordElement, UINT64_MAX, &queue),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 root{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &root, &queue), OBELISK_RT_OK);
  for (uint64_t value = 1; value <= 5; ++value)
    ASSERT_EQ(obelisk_rt_v1_queue_push(lane, queue, 0, &value, nullptr),
              OBELISK_RT_OK);
  uint64_t discarded = 0;
  uint32_t present = 0;
  ASSERT_EQ(obelisk_rt_v1_queue_pop(queue, 1, &discarded, nullptr, &present),
            OBELISK_RT_OK);
  ASSERT_EQ(present, 1u);
  uint64_t six = 6;
  ASSERT_EQ(obelisk_rt_v1_queue_push(lane, queue, 0, &six, nullptr),
            OBELISK_RT_OK);

  CoverageSetSnapshot snapshot;
  ASSERT_EQ(obelisk_rt_coverage_set_snapshot(context, queue, 64,
                                             OBELISK_RT_ELEMENT_BITS, false, 5,
                                             snapshot),
            OBELISK_RT_OK);
  ASSERT_EQ(snapshot.count, 5u);
  ASSERT_EQ(snapshot.valueSize, sizeof(uint64_t));
  ASSERT_EQ(snapshot.value.size(), 5 * sizeof(uint64_t));
  const uint64_t expected[] = {2, 3, 4, 5, 6};
  for (size_t index = 0; index != std::size(expected); ++index) {
    uint64_t observed = 0;
    std::memcpy(&observed, snapshot.value.data() + index * sizeof(uint64_t),
                sizeof(observed));
    EXPECT_EQ(observed, expected[index]);
  }
  EXPECT_TRUE(snapshot.unknown.empty());
  EXPECT_EQ(obelisk_rt_coverage_set_snapshot(context, queue, 64,
                                             OBELISK_RT_ELEMENT_BITS, false, 4,
                                             snapshot),
            OBELISK_RT_INVALID_DESIGN);
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &root), OBELISK_RT_OK);
}

TEST_F(ManagedValueTest, CoverageSetSnapshotPreservesLogicAndRealValues) {
  obelisk_rt_object_v1 *logic = nullptr;
  ASSERT_EQ(obelisk_rt_v1_dynamic_array_create(lane, &logicElement, 2, &logic),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 logicRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &logicRoot, &logic),
            OBELISK_RT_OK);
  const uint8_t logicValues[] = {0x5, 0xa};
  const uint8_t logicUnknown[] = {0x2, 0x4};
  for (int64_t index = 0; index != 2; ++index)
    ASSERT_EQ(obelisk_rt_v1_container_write(lane, logic, index,
                                            &logicValues[index],
                                            &logicUnknown[index]),
              OBELISK_RT_OK);

  CoverageSetSnapshot snapshot;
  ASSERT_EQ(obelisk_rt_coverage_set_snapshot(
                context, logic, 4, OBELISK_RT_ELEMENT_LOGIC, true, 2, snapshot),
            OBELISK_RT_OK);
  EXPECT_EQ(snapshot.value, std::vector<uint8_t>(std::begin(logicValues),
                                                 std::end(logicValues)));
  EXPECT_EQ(snapshot.unknown, std::vector<uint8_t>(std::begin(logicUnknown),
                                                   std::end(logicUnknown)));

  const obelisk_rt_element_type_v1 realElement{
      OBELISK_RT_VERSION, OBELISK_RT_ELEMENT_REAL, 901, 0,       0,
      sizeof(double),     alignof(double),         64,  nullptr, nullptr};
  obelisk_rt_object_v1 *reals = nullptr;
  ASSERT_EQ(obelisk_rt_v1_dynamic_array_create(lane, &realElement, 2, &reals),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 realRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &realRoot, &reals), OBELISK_RT_OK);
  const double realValues[] = {1.25, -2.5};
  for (int64_t index = 0; index != 2; ++index)
    ASSERT_EQ(obelisk_rt_v1_container_write(lane, reals, index,
                                            &realValues[index], nullptr),
              OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_coverage_set_snapshot(context, reals, 64,
                                             OBELISK_RT_ELEMENT_REAL, false, 2,
                                             snapshot),
            OBELISK_RT_OK);
  ASSERT_EQ(snapshot.value.size(), sizeof(realValues));
  EXPECT_EQ(std::memcmp(snapshot.value.data(), realValues, sizeof(realValues)),
            0);

  const obelisk_rt_element_type_v1 shortRealElement{
      OBELISK_RT_VERSION, OBELISK_RT_ELEMENT_REAL, 902, 0,       0,
      sizeof(float),      alignof(float),          32,  nullptr, nullptr};
  obelisk_rt_object_v1 *shortReals = nullptr;
  ASSERT_EQ(obelisk_rt_v1_dynamic_array_create(lane, &shortRealElement, 2,
                                               &shortReals),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 shortRealRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &shortRealRoot, &shortReals),
            OBELISK_RT_OK);
  const float shortRealValues[] = {3.5f, -4.5f};
  for (int64_t index = 0; index != 2; ++index)
    ASSERT_EQ(obelisk_rt_v1_container_write(lane, shortReals, index,
                                            &shortRealValues[index], nullptr),
              OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_coverage_set_snapshot(context, shortReals, 32,
                                             OBELISK_RT_ELEMENT_REAL, false, 2,
                                             snapshot),
            OBELISK_RT_OK);
  ASSERT_EQ(snapshot.value.size(), sizeof(shortRealValues));
  EXPECT_EQ(std::memcmp(snapshot.value.data(), shortRealValues,
                        sizeof(shortRealValues)),
            0);

  CoverageSetSnapshot empty;
  EXPECT_EQ(obelisk_rt_coverage_set_snapshot(
                context, nullptr, 9, OBELISK_RT_ELEMENT_BITS, false, 2, empty),
            OBELISK_RT_OK);
  EXPECT_EQ(empty.count, 0u);
  EXPECT_EQ(empty.valueSize, 2u);
  EXPECT_EQ(obelisk_rt_coverage_set_snapshot(
                context, logic, 4, OBELISK_RT_ELEMENT_BITS, false, 2, snapshot),
            OBELISK_RT_INVALID_DESIGN);
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &shortRealRoot), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &realRoot), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &logicRoot), OBELISK_RT_OK);
}

TEST_F(ManagedValueTest, QueueBoundsAreMaximumLegalIndices) {
  for (uint64_t bound : {UINT64_C(0), UINT64_C(2)}) {
    obelisk_rt_object_v1 *queue = nullptr;
    ASSERT_EQ(obelisk_rt_v1_queue_create(lane, &wordElement, bound, &queue),
              OBELISK_RT_OK);
    obelisk_rt_gc_root_v1 root{};
    ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &root, &queue), OBELISK_RT_OK);
    for (uint64_t value = 0; value != bound + 3; ++value)
      ASSERT_EQ(obelisk_rt_v1_queue_push(lane, queue, 0, &value, nullptr),
                OBELISK_RT_OK);
    EXPECT_EQ(obelisk_rt_v1_container_size(queue), bound + 1);
    uint64_t front = 97;
    ASSERT_EQ(obelisk_rt_v1_queue_push(lane, queue, 1, &front, nullptr),
              OBELISK_RT_OK);
    uint64_t inserted = 98;
    ASSERT_EQ(obelisk_rt_v1_queue_insert(lane, queue, 0, &inserted, nullptr),
              OBELISK_RT_OK);
    EXPECT_EQ(obelisk_rt_v1_container_size(queue), bound + 1);
    for (uint64_t index = 0; index <= bound; ++index) {
      uint64_t value = UINT64_MAX;
      ASSERT_EQ(obelisk_rt_v1_container_read(queue, index, &value, nullptr),
                OBELISK_RT_OK);
      EXPECT_EQ(value, index);
    }
    uint64_t replacement = 99;
    ASSERT_EQ(obelisk_rt_v1_container_write(lane, queue,
                                            static_cast<int64_t>(bound + 1),
                                            &replacement, nullptr),
              OBELISK_RT_OK);
    EXPECT_EQ(obelisk_rt_v1_container_size(queue), bound + 1);
    EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &root), OBELISK_RT_OK);
  }
}

TEST_F(ManagedValueTest,
       AssociativeArraysGrowDeleteCloneAndTraverseInKeyOrder) {
  obelisk_rt_object_v1 *array = nullptr;
  ASSERT_EQ(obelisk_rt_v1_assoc_create(lane, &wordElement,
                                       OBELISK_RT_ASSOC_KEY_SIGNED, 16, &array),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 arrayRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &arrayRoot, &array),
            OBELISK_RT_OK);
  const int16_t keys[] = {7, -3, 40, 2, -9, 18, 99, 1, -1, 63};
  for (int16_t sourceKey : keys) {
    obelisk_rt_assoc_key_v1 key{OBELISK_RT_ASSOC_KEY_SIGNED,        0,   16,
                                {static_cast<uint16_t>(sourceKey)}, {0}, 0};
    uint64_t value = static_cast<uint64_t>(sourceKey + 1000);
    ASSERT_EQ(obelisk_rt_v1_assoc_write(lane, array, &key, &value, nullptr),
              OBELISK_RT_OK);
  }
  EXPECT_EQ(obelisk_rt_v1_container_size(array), std::size(keys));

  obelisk_rt_assoc_key_v1 missing{
      OBELISK_RT_ASSOC_KEY_SIGNED, 0, 16, {88}, {0}, 0};
  uint64_t value = UINT64_MAX;
  uint32_t present = 1;
  ASSERT_EQ(
      obelisk_rt_v1_assoc_read(array, &missing, &value, nullptr, &present),
      OBELISK_RT_OK);
  EXPECT_EQ(present, 0u);
  EXPECT_EQ(value, 0u);

  std::vector<int16_t> ordered;
  obelisk_rt_assoc_key_v1 cursor{};
  uint32_t success = 0;
  ASSERT_EQ(obelisk_rt_v1_assoc_first(lane, array, &cursor, &success),
            OBELISK_RT_OK);
  while (success) {
    ordered.push_back(static_cast<int16_t>(cursor.value));
    ASSERT_EQ(obelisk_rt_v1_assoc_next(lane, array, &cursor, &success),
              OBELISK_RT_OK);
  }
  EXPECT_EQ(ordered,
            (std::vector<int16_t>{-9, -3, -1, 1, 2, 7, 18, 40, 63, 99}));

  obelisk_rt_assoc_key_v1 removed{OBELISK_RT_ASSOC_KEY_SIGNED, 0,   16,
                                  {static_cast<uint16_t>(-3)}, {0}, 0};
  ASSERT_EQ(obelisk_rt_v1_assoc_delete(array, &removed), OBELISK_RT_OK);
  uint32_t exists = 1;
  ASSERT_EQ(obelisk_rt_v1_assoc_exists(array, &removed, &exists),
            OBELISK_RT_OK);
  EXPECT_EQ(exists, 0u);
  cursor = removed;
  ASSERT_EQ(obelisk_rt_v1_assoc_next(lane, array, &cursor, &success),
            OBELISK_RT_OK);
  ASSERT_EQ(success, 1u);
  EXPECT_EQ(static_cast<int16_t>(cursor.value), -1);
  cursor = removed;
  ASSERT_EQ(obelisk_rt_v1_assoc_prev(lane, array, &cursor, &success),
            OBELISK_RT_OK);
  ASSERT_EQ(success, 1u);
  EXPECT_EQ(static_cast<int16_t>(cursor.value), -9);

  obelisk_rt_object_v1 *copy = nullptr;
  ASSERT_EQ(obelisk_rt_v1_container_clone(lane, array, &copy), OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 copyRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &copyRoot, &copy), OBELISK_RT_OK);
  obelisk_rt_assoc_key_v1 changed{
      OBELISK_RT_ASSOC_KEY_SIGNED, 0, 16, {7}, {0}, 0};
  uint64_t replacement = 12345;
  ASSERT_EQ(
      obelisk_rt_v1_assoc_write(lane, copy, &changed, &replacement, nullptr),
      OBELISK_RT_OK);
  ASSERT_EQ(
      obelisk_rt_v1_assoc_read(array, &changed, &value, nullptr, &present),
      OBELISK_RT_OK);
  EXPECT_EQ(value, 1007u);
  ASSERT_EQ(obelisk_rt_v1_assoc_read(copy, &changed, &value, nullptr, &present),
            OBELISK_RT_OK);
  EXPECT_EQ(value, replacement);

  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &copyRoot), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &arrayRoot), OBELISK_RT_OK);
}

TEST_F(ManagedValueTest,
       WideIntegralAssociativeKeysPreserveBitsAndSignedOrder) {
  constexpr uint64_t width = 129;
  constexpr size_t bytes = 17;
  obelisk_rt_object_v1 *array = nullptr;
  ASSERT_EQ(obelisk_rt_v1_assoc_create(
                lane, &wordElement, OBELISK_RT_ASSOC_KEY_SIGNED, width, &array),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 root{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &root, &array), OBELISK_RT_OK);

  std::array<std::array<uint8_t, bytes>, 5> keys{};
  keys[0][16] = 1; // -2**128
  keys[1].fill(0xff);
  keys[1][16] = 1; // -1
  keys[3][8] = 1;  // 2**64
  keys[4].fill(0xff);
  keys[4][16] = 0; // 2**128 - 1
  for (size_t index = 0; index != keys.size(); ++index) {
    SCOPED_TRACE(index);
    obelisk_rt_assoc_key_v1 key{
        OBELISK_RT_ASSOC_KEY_SIGNED, 0, width, {}, {}, 0};
    key.value_data = keys[index].data();
    uint64_t value = index + 100;
    ASSERT_EQ(obelisk_rt_v1_assoc_write(lane, array, &key, &value, nullptr),
              OBELISK_RT_OK);
  }

  std::array<uint8_t, bytes> highKey{};
  highKey[8] = 1;
  obelisk_rt_assoc_key_v1 lookup{
      OBELISK_RT_ASSOC_KEY_SIGNED, 0, width, {}, {}, 0};
  lookup.value_data = highKey.data();
  uint64_t value = 0;
  uint32_t present = 0;
  ASSERT_EQ(obelisk_rt_v1_assoc_read(array, &lookup, &value, nullptr, &present),
            OBELISK_RT_OK);
  EXPECT_EQ(present, 1u);
  EXPECT_EQ(value, 103u);

  obelisk_rt_object_v1 *copy = nullptr;
  ASSERT_EQ(obelisk_rt_v1_container_clone(lane, array, &copy), OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 copyRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &copyRoot, &copy), OBELISK_RT_OK);
  value = 0;
  ASSERT_EQ(obelisk_rt_v1_assoc_read(copy, &lookup, &value, nullptr, &present),
            OBELISK_RT_OK);
  EXPECT_EQ(present, 1u);
  EXPECT_EQ(value, 103u);
  ASSERT_EQ(obelisk_rt_v1_gc_root_pop(lane, &copyRoot), OBELISK_RT_OK);

  obelisk_rt_object_v1 *path = nullptr;
  ASSERT_EQ(obelisk_rt_v1_reference_path_assoc_create(lane, array, &lookup,
                                                      nullptr, 0, 0, nullptr,
                                                      nullptr, 0, &path),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 pathRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &pathRoot, &path), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  uint64_t replacement = 909;
  ASSERT_EQ(
      obelisk_rt_v1_reference_path_store(lane, path, &replacement, nullptr),
      OBELISK_RT_OK);
  value = 0;
  ASSERT_EQ(obelisk_rt_v1_reference_path_load(path, &value, nullptr, &present),
            OBELISK_RT_OK);
  EXPECT_EQ(present, 1u);
  EXPECT_EQ(value, replacement);
  ASSERT_EQ(obelisk_rt_v1_gc_root_pop(lane, &pathRoot), OBELISK_RT_OK);

  std::array<uint8_t, bytes> cursorBytes{};
  obelisk_rt_assoc_key_v1 cursor{
      OBELISK_RT_ASSOC_KEY_SIGNED, 0, width, {}, {}, 0};
  cursor.value_data = cursorBytes.data();
  uint32_t success = 0;
  for (const auto &expected : keys) {
    ASSERT_EQ(obelisk_rt_v1_assoc_first(lane, array, &cursor, &success),
              OBELISK_RT_OK);
    ASSERT_EQ(success, 1u);
    EXPECT_EQ(cursorBytes, expected);
    ASSERT_EQ(obelisk_rt_v1_assoc_delete(array, &cursor), OBELISK_RT_OK);
  }
  EXPECT_EQ(obelisk_rt_v1_container_size(array), 0u);

  std::array<uint8_t, bytes> unknown{};
  unknown[16] = 0x80; // Padding above bit 128 is not part of the index.
  lookup.unknown_data = unknown.data();
  value = 77;
  ASSERT_EQ(obelisk_rt_v1_assoc_write(lane, array, &lookup, &value, nullptr),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_container_size(array), 1u);
  unknown[16] = 1; // Bit 128 is X/Z and therefore invalid.
  ASSERT_EQ(obelisk_rt_v1_assoc_write(lane, array, &lookup, &value, nullptr),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_container_size(array), 1u);

  ASSERT_EQ(obelisk_rt_v1_gc_root_pop(lane, &root), OBELISK_RT_OK);
}

TEST_F(ManagedValueTest,
       AssociativeDefaultsTypedCreationCloneDeleteAndPatternFormatting) {
  obelisk_rt_object_v1 *array = nullptr;
  ASSERT_EQ(obelisk_rt_v1_assoc_create_typed(
                lane, wordElement.type_id, wordElement.kind, wordElement.flags,
                wordElement.value_size, wordElement.alignment,
                wordElement.bit_width, nullptr, 0, OBELISK_RT_ASSOC_KEY_SIGNED,
                12, &array),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 root{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &root, &array), OBELISK_RT_OK);

  uint64_t fallback = 77;
  ASSERT_EQ(obelisk_rt_v1_assoc_set_default_checked(
                lane, array, &fallback, sizeof(fallback), nullptr, 0),
            OBELISK_RT_OK);
  obelisk_rt_assoc_key_v1 missing{
      OBELISK_RT_ASSOC_KEY_SIGNED, 0, 12, {3}, {0}, 0};
  uint64_t value = 0;
  uint32_t present = 1;
  ASSERT_EQ(obelisk_rt_v1_assoc_read_checked(
                array, &missing, &value, sizeof(value), nullptr, 0, &present),
            OBELISK_RT_OK);
  EXPECT_EQ(present, 0u);
  EXPECT_EQ(value, fallback);

  // IEEE 1800-2023 7.8.6: an X or Z key reads the user-specified default
  // without a warning.
  obelisk_rt_assoc_key_v1 unknownKey{
      OBELISK_RT_ASSOC_KEY_SIGNED, 0, 12, {3}, {1}, 0};
  value = 0;
  present = 1;
  testing::internal::CaptureStderr();
  ASSERT_EQ(obelisk_rt_v1_assoc_read_checked(array, &unknownKey, &value,
                                             sizeof(value), nullptr, 0,
                                             &present),
            OBELISK_RT_OK);
  EXPECT_EQ(testing::internal::GetCapturedStderr(), "");
  EXPECT_EQ(present, 0u);
  EXPECT_EQ(value, fallback);

  for (auto [keyValue, stored] : {std::pair<int16_t, uint64_t>{4, 9},
                                  std::pair<int16_t, uint64_t>{-2, 5}}) {
    obelisk_rt_assoc_key_v1 key{OBELISK_RT_ASSOC_KEY_SIGNED,       0,   12,
                                {static_cast<uint16_t>(keyValue)}, {0}, 0};
    ASSERT_EQ(obelisk_rt_v1_assoc_write_checked(lane, array, &key, &stored,
                                                sizeof(stored), nullptr, 0),
              OBELISK_RT_OK);
  }
  obelisk_rt_arg_v1 argument{OBELISK_RT_ARG_MANAGED_CONTAINER, 0, 0, &array,
                             nullptr};
  obelisk_rt_format_env_v1 environment{};
  environment.time_multiplier = 1;
  obelisk_rt_buffer_v1 output{};
  ASSERT_EQ(obelisk_rt_v1_format(context, "%p", 2, &argument, 1, &environment,
                                 &output),
            OBELISK_RT_OK);
  EXPECT_EQ(
      std::string(reinterpret_cast<const char *>(output.data), output.size),
      "'{-2:5, 4:9}");
  obelisk_rt_v1_buffer_release(&output);

  obelisk_rt_object_v1 *copy = nullptr;
  ASSERT_EQ(obelisk_rt_v1_container_clone(lane, array, &copy), OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 copyRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &copyRoot, &copy), OBELISK_RT_OK);
  value = 0;
  ASSERT_EQ(obelisk_rt_v1_assoc_read(copy, &missing, &value, nullptr, &present),
            OBELISK_RT_OK);
  EXPECT_EQ(value, fallback);

  obelisk_rt_assoc_key_v1 explicitKey{
      OBELISK_RT_ASSOC_KEY_SIGNED, 0, 12, {4}, {0}, 0};
  ASSERT_EQ(obelisk_rt_v1_assoc_delete(copy, &explicitKey), OBELISK_RT_OK);
  value = 0;
  ASSERT_EQ(obelisk_rt_v1_assoc_read(copy, &missing, &value, nullptr, &present),
            OBELISK_RT_OK);
  EXPECT_EQ(value, fallback);
  // IEEE 1800-2023 7.9.2 and 7.9.11: delete() removes the elements but not
  // the default.
  ASSERT_EQ(obelisk_rt_v1_container_delete(copy), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_container_size(copy), 0u);
  value = UINT64_MAX;
  ASSERT_EQ(
      obelisk_rt_v1_assoc_read(copy, &explicitKey, &value, nullptr, &present),
      OBELISK_RT_OK);
  EXPECT_EQ(present, 0u);
  EXPECT_EQ(value, fallback);

  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &copyRoot), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &root), OBELISK_RT_OK);
}

TEST_F(ManagedValueTest,
       AssociativeManagedDefaultRemainsRootedDuringPublication) {
  ASSERT_EQ(obelisk_rt_v1_gc_set_threshold(context, 1), OBELISK_RT_OK);
  obelisk_rt_object_v1 *inner = nullptr;
  ASSERT_EQ(obelisk_rt_v1_dynamic_array_create(lane, &wordElement, 1, &inner),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 innerRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &innerRoot, &inner),
            OBELISK_RT_OK);
  uint64_t stored = 91;
  ASSERT_EQ(obelisk_rt_v1_container_write(lane, inner, 0, &stored, nullptr),
            OBELISK_RT_OK);

  obelisk_rt_object_v1 *outer = nullptr;
  ASSERT_EQ(obelisk_rt_v1_assoc_create(lane, &containerElement,
                                       OBELISK_RT_ASSOC_KEY_UNSIGNED, 32,
                                       &outer),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 outerRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &outerRoot, &outer),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_assoc_set_default(lane, outer, &inner, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_root_pop(lane, &outerRoot), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_root_pop(lane, &innerRoot), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &outerRoot, &outer),
            OBELISK_RT_OK);
  inner = nullptr;
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);

  obelisk_rt_assoc_key_v1 missing{
      OBELISK_RT_ASSOC_KEY_UNSIGNED, 0, 32, {17}, {0}, 0};
  obelisk_rt_object_v1 *fallback = nullptr;
  uint32_t present = 1;
  ASSERT_EQ(
      obelisk_rt_v1_assoc_read(outer, &missing, &fallback, nullptr, &present),
      OBELISK_RT_OK);
  EXPECT_EQ(present, 0u);
  ASSERT_NE(fallback, nullptr);
  uint64_t value = 0;
  ASSERT_EQ(obelisk_rt_v1_container_read(fallback, 0, &value, nullptr),
            OBELISK_RT_OK);
  EXPECT_EQ(value, stored);

  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &outerRoot), OBELISK_RT_OK);
}

TEST_F(ManagedValueTest, AssociativeSlotsPreserveAlignmentForByteElements) {
  obelisk_rt_object_v1 *array = nullptr;
  ASSERT_EQ(obelisk_rt_v1_assoc_create(
                lane, &byteElement, OBELISK_RT_ASSOC_KEY_UNSIGNED, 32, &array),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 root{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &root, &array), OBELISK_RT_OK);
  for (uint64_t index = 0; index != 40; ++index) {
    obelisk_rt_assoc_key_v1 key{
        OBELISK_RT_ASSOC_KEY_UNSIGNED, 0, 32, {index * 17}, {0}, 0};
    uint8_t value = static_cast<uint8_t>(index + 1);
    ASSERT_EQ(obelisk_rt_v1_assoc_write(lane, array, &key, &value, nullptr),
              OBELISK_RT_OK);
  }
  for (uint64_t index = 0; index != 40; ++index) {
    obelisk_rt_assoc_key_v1 key{
        OBELISK_RT_ASSOC_KEY_UNSIGNED, 0, 32, {index * 17}, {0}, 0};
    uint8_t value = 0;
    uint32_t present = 0;
    ASSERT_EQ(obelisk_rt_v1_assoc_read(array, &key, &value, nullptr, &present),
              OBELISK_RT_OK);
    EXPECT_EQ(present, 1u);
    EXPECT_EQ(value, static_cast<uint8_t>(index + 1));
  }
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &root), OBELISK_RT_OK);
}

TEST_F(ManagedValueTest, AssociativeStringKeysRemainTracedByValue) {
  ASSERT_EQ(obelisk_rt_v1_gc_set_threshold(context, 1), OBELISK_RT_OK);
  obelisk_rt_string_v1 keyText = 0;
  ASSERT_EQ(obelisk_rt_v1_string_create(lane, "alpha", 5, &keyText),
            OBELISK_RT_OK);
  obelisk_rt_gc_managed_root_v1 keyRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_managed_root_push(lane, &keyRoot, &keyText),
            OBELISK_RT_OK);
  obelisk_rt_object_v1 *array = nullptr;
  ASSERT_EQ(obelisk_rt_v1_assoc_create(lane, &wordElement,
                                       OBELISK_RT_ASSOC_KEY_STRING, 0, &array),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 arrayRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &arrayRoot, &array),
            OBELISK_RT_OK);
  obelisk_rt_assoc_key_v1 key{
      OBELISK_RT_ASSOC_KEY_STRING, 0, 0, {0}, {0}, keyText};
  uint64_t stored = 55;
  ASSERT_EQ(obelisk_rt_v1_assoc_write(lane, array, &key, &stored, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_root_pop(lane, &arrayRoot), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_managed_root_pop(lane, &keyRoot), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &arrayRoot, &array),
            OBELISK_RT_OK);
  keyText = 0;
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);

  obelisk_rt_string_v1 equivalent = 0;
  ASSERT_EQ(obelisk_rt_v1_string_create(lane, "alpha", 5, &equivalent),
            OBELISK_RT_OK);
  obelisk_rt_gc_managed_root_v1 equivalentRoot{};
  ASSERT_EQ(
      obelisk_rt_v1_gc_managed_root_push(lane, &equivalentRoot, &equivalent),
      OBELISK_RT_OK);
  key.string = equivalent;
  uint64_t value = 0;
  uint32_t present = 0;
  ASSERT_EQ(obelisk_rt_v1_assoc_read(array, &key, &value, nullptr, &present),
            OBELISK_RT_OK);
  EXPECT_EQ(present, 1u);
  EXPECT_EQ(value, stored);

  obelisk_rt_assoc_key_v1 cursor{};
  uint32_t success = 0;
  ASSERT_EQ(obelisk_rt_v1_assoc_first(lane, array, &cursor, &success),
            OBELISK_RT_OK);
  ASSERT_EQ(success, 1u);
  EXPECT_EQ(obelisk_rt_v1_string_compare(cursor.string, equivalent), 0);
  EXPECT_EQ(obelisk_rt_v1_gc_managed_root_pop(lane, &equivalentRoot),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &arrayRoot), OBELISK_RT_OK);
}

TEST_F(ManagedValueTest, AssociativeTraversalRootsStringCursorDuringRebuild) {
  ASSERT_EQ(obelisk_rt_v1_gc_set_threshold(context, 1), OBELISK_RT_OK);
  obelisk_rt_object_v1 *array = nullptr;
  ASSERT_EQ(obelisk_rt_v1_assoc_create(lane, &wordElement,
                                       OBELISK_RT_ASSOC_KEY_STRING, 0, &array),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 arrayRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &arrayRoot, &array),
            OBELISK_RT_OK);

  for (const char *text : {"alpha", "charlie"}) {
    obelisk_rt_string_v1 keyString = 0;
    ASSERT_EQ(
        obelisk_rt_v1_string_create(lane, text, std::strlen(text), &keyString),
        OBELISK_RT_OK);
    obelisk_rt_gc_managed_root_v1 keyRoot{};
    ASSERT_EQ(obelisk_rt_v1_gc_managed_root_push(lane, &keyRoot, &keyString),
              OBELISK_RT_OK);
    obelisk_rt_assoc_key_v1 key{
        OBELISK_RT_ASSOC_KEY_STRING, 0, 0, {0}, {0}, keyString};
    uint64_t value = std::strlen(text);
    ASSERT_EQ(obelisk_rt_v1_assoc_write(lane, array, &key, &value, nullptr),
              OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_gc_managed_root_pop(lane, &keyRoot), OBELISK_RT_OK);
  }

  obelisk_rt_string_v1 cursorString = 0;
  ASSERT_EQ(obelisk_rt_v1_string_create(lane, "bravo", 5, &cursorString),
            OBELISK_RT_OK);
  obelisk_rt_assoc_key_v1 cursor{
      OBELISK_RT_ASSOC_KEY_STRING, 0, 0, {0}, {0}, cursorString};
  uint32_t success = 0;
  ASSERT_EQ(obelisk_rt_v1_assoc_next(lane, array, &cursor, &success),
            OBELISK_RT_OK);
  ASSERT_EQ(success, 1u);
  const char *bytes = nullptr;
  uint64_t size = 0;
  char scratch[8];
  ASSERT_EQ(obelisk_rt_v1_string_view(cursor.string, scratch, &bytes, &size),
            OBELISK_RT_OK);
  EXPECT_EQ(std::string(bytes, size), "charlie");
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &arrayRoot), OBELISK_RT_OK);
}

TEST_F(ManagedValueTest, ManagedWritesRejectCrossContextHandles) {
  obelisk_rt_object_v1 *values = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_dynamic_array_create(lane, &stringElement, 1, &values),
      OBELISK_RT_OK);
  obelisk_rt_object_v1 *keys = nullptr;
  ASSERT_EQ(obelisk_rt_v1_assoc_create(lane, &wordElement,
                                       OBELISK_RT_ASSOC_KEY_STRING, 0, &keys),
            OBELISK_RT_OK);

  ASSERT_EQ(obelisk_rt_v1_gc_lane_leave(lane), OBELISK_RT_OK);
  obelisk_rt_context *foreignContext = nullptr;
  obelisk_rt_gc_lane_v1 *foreignLane = nullptr;
  ASSERT_EQ(obelisk_rt_v1_context_create(&foreignContext), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_create(foreignContext, &foreignLane),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_enter(foreignLane), OBELISK_RT_OK);
  obelisk_rt_string_v1 foreignString = 0;
  ASSERT_EQ(
      obelisk_rt_v1_string_create(foreignLane, "foreign!", 8, &foreignString),
      OBELISK_RT_OK);
  obelisk_rt_object_v1 *foreignArray = nullptr;
  ASSERT_EQ(obelisk_rt_v1_dynamic_array_create(foreignLane, &wordElement, 1,
                                               &foreignArray),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_leave(foreignLane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_lane_enter(lane), OBELISK_RT_OK);

  EXPECT_EQ(
      obelisk_rt_v1_container_write(lane, values, 0, &foreignString, nullptr),
      OBELISK_RT_INVALID_HANDLE);
  obelisk_rt_assoc_key_v1 key{
      OBELISK_RT_ASSOC_KEY_STRING, 0, 0, {0}, {0}, foreignString};
  uint64_t value = 1;
  EXPECT_EQ(obelisk_rt_v1_assoc_write(lane, keys, &key, &value, nullptr),
            OBELISK_RT_INVALID_HANDLE);
  obelisk_rt_object_v1 *clone = nullptr;
  EXPECT_EQ(obelisk_rt_v1_container_clone(lane, foreignArray, &clone),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(obelisk_rt_v1_dynamic_array_resize(lane, foreignArray, 2),
            OBELISK_RT_INVALID_HANDLE);

  EXPECT_EQ(obelisk_rt_v1_gc_lane_destroy(foreignLane), OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(foreignContext);
}

TEST_F(ManagedValueTest, RealReferencePathsPreservePrecisionAndPayloads) {
  for (uint64_t size : {sizeof(float), sizeof(double)}) {
    const uint64_t width = size * 8;
    const obelisk_rt_element_type_v1 element{OBELISK_RT_VERSION,
                                             OBELISK_RT_ELEMENT_REAL,
                                             910 + size,
                                             0,
                                             0,
                                             size,
                                             size,
                                             width,
                                             nullptr,
                                             nullptr};
    obelisk_rt_object_v1 *array = nullptr;
    ASSERT_EQ(obelisk_rt_v1_dynamic_array_create(lane, &element, 1, &array),
              OBELISK_RT_OK);
    obelisk_rt_gc_root_v1 arrayRoot{};
    ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &arrayRoot, &array),
              OBELISK_RT_OK);
    std::array<uint8_t, sizeof(double)> bits{};
    bits[size - 1] = 0x80;
    ASSERT_EQ(
        obelisk_rt_v1_container_write(lane, array, 0, bits.data(), nullptr),
        OBELISK_RT_OK);
    obelisk_rt_object_v1 *path = nullptr;
    ASSERT_EQ(obelisk_rt_v1_reference_path_index_create(
                  lane, array, 0, nullptr, 0, 0, nullptr, nullptr, 0, &path),
              OBELISK_RT_OK);
    obelisk_rt_gc_root_v1 pathRoot{};
    ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &pathRoot, &path),
              OBELISK_RT_OK);
    std::array<uint8_t, 16> loaded;
    loaded.fill(0x5a);
    ASSERT_EQ(obelisk_rt_v1_argument_ref_load(context, nullptr, nullptr, 0,
                                              path, 0, 2, width, size, 0, 0,
                                              loaded.data(), nullptr),
              OBELISK_RT_OK);
    EXPECT_EQ(std::memcmp(loaded.data(), bits.data(), size), 0);
    for (size_t index = size; index != loaded.size(); ++index)
      EXPECT_EQ(loaded[index], 0x5a);
    loaded.fill(0x5a);
    EXPECT_EQ(obelisk_rt_v1_argument_ref_load(
                  context, nullptr, nullptr, 0, path, 0, 2,
                  width == 32 ? 64 : 32, size, 0, 0, loaded.data(), nullptr),
              OBELISK_RT_ARGUMENT_MISMATCH);
    for (size_t index = 0; index != loaded.size(); ++index)
      EXPECT_EQ(loaded[index], index < size ? 0 : 0x5a);
    const uint32_t shortNaN = 0x7fc12345;
    const uint64_t realNaN = UINT64_C(0x7ff81234deadbeef);
    if (size == sizeof(float))
      std::memcpy(bits.data(), &shortNaN, size);
    else
      std::memcpy(bits.data(), &realNaN, size);
    ASSERT_EQ(
        obelisk_rt_v1_reference_path_store(lane, path, bits.data(), nullptr),
        OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_argument_ref_load(context, nullptr, nullptr, 0,
                                              path, 0, 2, width, size, 0, 0,
                                              loaded.data(), nullptr),
              OBELISK_RT_OK);
    EXPECT_EQ(std::memcmp(loaded.data(), bits.data(), size), 0);
    EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &pathRoot), OBELISK_RT_OK);
    EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &arrayRoot), OBELISK_RT_OK);
  }
}

TEST_F(ManagedValueTest, ReferencePathsResolveAgainAfterContainerMutation) {
  obelisk_rt_object_v1 *queue = nullptr;
  ASSERT_EQ(obelisk_rt_v1_queue_create(lane, &wordElement, UINT64_MAX, &queue),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 queueRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &queueRoot, &queue),
            OBELISK_RT_OK);
  obelisk_rt_object_v1 *path = nullptr;
  ASSERT_EQ(obelisk_rt_v1_reference_path_index_create(
                lane, queue, 0, nullptr, 0, 0, nullptr, nullptr, 0, &path),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 pathRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &pathRoot, &path), OBELISK_RT_OK);
  uint64_t value = 41;
  ASSERT_EQ(obelisk_rt_v1_reference_path_store(lane, path, &value, nullptr),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_container_size(queue), 1u);
  uint32_t present = 0;
  value = 0;
  ASSERT_EQ(obelisk_rt_v1_reference_path_load(path, &value, nullptr, &present),
            OBELISK_RT_OK);
  EXPECT_EQ(present, 1u);
  EXPECT_EQ(value, 41u);
  uint8_t stateValue[8]{};
  uint8_t stateUnknown[8]{};
  EXPECT_EQ(obelisk_rt_v1_argument_ref_load(context, stateValue, stateUnknown,
                                            64, path, 0, 2, 32, sizeof(value),
                                            0, 0, &value, nullptr),
            OBELISK_RT_ARGUMENT_MISMATCH);
  EXPECT_EQ(obelisk_rt_v1_argument_ref_load(context, stateValue, stateUnknown,
                                            64, path, 0, 2, 64, sizeof(value),
                                            0, 1, &value, nullptr),
            OBELISK_RT_ARGUMENT_MISMATCH);

  for (uint64_t next = 0; next != 20; ++next)
    ASSERT_EQ(obelisk_rt_v1_queue_push(lane, queue, 0, &next, nullptr),
              OBELISK_RT_OK);
  value = 77;
  ASSERT_EQ(obelisk_rt_v1_reference_path_store(lane, path, &value, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_container_read(queue, 0, &value, nullptr),
            OBELISK_RT_OK);
  EXPECT_EQ(value, 77u);

  // IEEE 1800-2017 7.10.3 preserves the identity of every queue element that
  // survives a method update, including insertions and ordering-method swaps.
  uint64_t front = 100;
  ASSERT_EQ(obelisk_rt_v1_queue_push(lane, queue, 1, &front, nullptr),
            OBELISK_RT_OK);
  value = 78;
  ASSERT_EQ(obelisk_rt_v1_reference_path_store(lane, path, &value, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_container_read(queue, 1, &value, nullptr),
            OBELISK_RT_OK);
  EXPECT_EQ(value, 78u);

  ASSERT_EQ(obelisk_rt_v1_container_swap(queue, 1, 5), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  value = 79;
  ASSERT_EQ(obelisk_rt_v1_reference_path_store(lane, path, &value, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_container_read(queue, 5, &value, nullptr),
            OBELISK_RT_OK);
  EXPECT_EQ(value, 79u);

  ASSERT_EQ(obelisk_rt_v1_queue_delete_index(queue, 5), OBELISK_RT_OK);
  value = 80;
  ASSERT_EQ(obelisk_rt_v1_reference_path_store(lane, path, &value, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_reference_path_load(path, &value, nullptr, &present),
            OBELISK_RT_OK);
  EXPECT_EQ(present, 1u);
  EXPECT_EQ(value, 80u);
  for (uint64_t index = 0; index != obelisk_rt_v1_container_size(queue);
       ++index) {
    value = 0;
    ASSERT_EQ(obelisk_rt_v1_container_read(queue, index, &value, nullptr),
              OBELISK_RT_OK);
    EXPECT_NE(value, 80u);
  }

  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &pathRoot), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &queueRoot), OBELISK_RT_OK);
}

TEST_F(ManagedValueTest, RepeatedCapturesShareStorageAfterQueueRemoval) {
  obelisk_rt_object_v1 *queue = nullptr;
  ASSERT_EQ(obelisk_rt_v1_queue_create(lane, &wordElement, UINT64_MAX, &queue),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 queueRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &queueRoot, &queue),
            OBELISK_RT_OK);
  uint64_t value = 41;
  ASSERT_EQ(obelisk_rt_v1_queue_push(lane, queue, 0, &value, nullptr),
            OBELISK_RT_OK);
  obelisk_rt_object_v1 *path = nullptr;
  ASSERT_EQ(obelisk_rt_v1_reference_path_index_create(
                lane, queue, 0, nullptr, 0, 0, nullptr, nullptr, 0, &path),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 pathRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &pathRoot, &path), OBELISK_RT_OK);
  obelisk_rt_object_v1 *alias = nullptr;
  for (unsigned capture = 0; capture != 32; ++capture) {
    ASSERT_EQ(obelisk_rt_v1_reference_path_index_create(
                  lane, queue, 0, nullptr, 0, 0, nullptr, nullptr, 0, &alias),
              OBELISK_RT_OK);
    EXPECT_EQ(alias, path); // Repeated captures allocate one element snapshot.
  }
  ASSERT_EQ(obelisk_rt_v1_queue_push(lane, queue, 1, &value, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_reference_path_index_create(
                lane, queue, 1, nullptr, 0, 0, nullptr, nullptr, 0, &alias),
            OBELISK_RT_OK);
  EXPECT_EQ(alias, path);
  uint32_t present = 0;
  ASSERT_EQ(obelisk_rt_v1_queue_pop(queue, 0, &value, nullptr, &present),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  value = 99;
  ASSERT_EQ(obelisk_rt_v1_reference_path_store(lane, alias, &value, nullptr),
            OBELISK_RT_OK);
  value = 0;
  ASSERT_EQ(obelisk_rt_v1_reference_path_load(path, &value, nullptr, &present),
            OBELISK_RT_OK);
  EXPECT_EQ(value, 99u);
  EXPECT_EQ(present, 1u);
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &pathRoot), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &queueRoot), OBELISK_RT_OK);
}

TEST_F(ManagedValueTest, EqualAssociativeKeysShareOnlyTheLiveCapture) {
  obelisk_rt_object_v1 *array = nullptr;
  ASSERT_EQ(obelisk_rt_v1_assoc_create(lane, &wordElement,
                                       OBELISK_RT_ASSOC_KEY_STRING, 0, &array),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 arrayRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &arrayRoot, &array),
            OBELISK_RT_OK);
  constexpr char text[] = "equal keys with distinct non-interned storage";
  obelisk_rt_string_v1 keys[2] = {};
  obelisk_rt_object_v1 *paths[2] = {};
  for (unsigned index = 0; index != 2; ++index) {
    ASSERT_EQ(
        obelisk_rt_v1_string_create(lane, text, sizeof(text) - 1, &keys[index]),
        OBELISK_RT_OK);
    obelisk_rt_assoc_key_v1 key{};
    key.kind = OBELISK_RT_ASSOC_KEY_STRING;
    key.string = keys[index];
    ASSERT_EQ(obelisk_rt_v1_reference_path_assoc_create(
                  lane, array, &key, nullptr, 0, 0, nullptr, nullptr, 0,
                  &paths[index]),
              OBELISK_RT_OK);
  }
  EXPECT_EQ(paths[0], paths[1]);
  obelisk_rt_gc_root_v1 pathRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &pathRoot, &paths[0]),
            OBELISK_RT_OK);
  uint64_t value = 41;
  ASSERT_EQ(obelisk_rt_v1_reference_path_store(lane, paths[0], &value, nullptr),
            OBELISK_RT_OK);
  obelisk_rt_assoc_key_v1 key{};
  key.kind = OBELISK_RT_ASSOC_KEY_STRING;
  key.string = keys[0];
  ASSERT_EQ(obelisk_rt_v1_assoc_delete(array, &key), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  value = 99;
  ASSERT_EQ(obelisk_rt_v1_reference_path_store(lane, paths[1], &value, nullptr),
            OBELISK_RT_OK);
  uint32_t present = 0;
  value = 0;
  ASSERT_EQ(
      obelisk_rt_v1_reference_path_load(paths[0], &value, nullptr, &present),
      OBELISK_RT_OK);
  EXPECT_EQ(value, 99u);
  obelisk_rt_object_v1 *fresh = nullptr;
  ASSERT_EQ(obelisk_rt_v1_reference_path_assoc_create(
                lane, array, &key, nullptr, 0, 0, nullptr, nullptr, 0, &fresh),
            OBELISK_RT_OK);
  EXPECT_NE(fresh, paths[0]);
  value = 1;
  ASSERT_EQ(obelisk_rt_v1_reference_path_load(fresh, &value, nullptr, &present),
            OBELISK_RT_OK);
  EXPECT_EQ(value, 0u);
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &pathRoot), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &arrayRoot), OBELISK_RT_OK);
}

TEST_F(ManagedValueTest, AggregateReferencesAcceptPartialFinalBytes) {
  const obelisk_rt_element_type_v1 element{OBELISK_RT_VERSION,
                                           OBELISK_RT_ELEMENT_AGGREGATE,
                                           9400,
                                           OBELISK_RT_ELEMENT_FOUR_STATE,
                                           0,
                                           6,
                                           1,
                                           48,
                                           nullptr,
                                           nullptr};
  obelisk_rt_object_v1 *array = nullptr, *path = nullptr;
  ASSERT_EQ(obelisk_rt_v1_dynamic_array_create(lane, &element, 1, &array),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_reference_path_index_create(
                lane, array, 0, nullptr, 0, 0, nullptr, nullptr, 0, &path),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_reference_path_shape(path, 6, 42, 1, 0), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_reference_path_shape(path, 6, 40, 1, 0),
            OBELISK_RT_ARGUMENT_MISMATCH);
  EXPECT_EQ(obelisk_rt_reference_path_shape(path, 6, 49, 1, 0),
            OBELISK_RT_ARGUMENT_MISMATCH);
  EXPECT_EQ(obelisk_rt_reference_path_shape(path, 6, 42, 0, 0),
            OBELISK_RT_ARGUMENT_MISMATCH);
}

TEST_F(ManagedValueTest, AggregateWindowsPreservePlanesAndDetachedAliases) {
  const obelisk_rt_element_type_v1 recordElement{OBELISK_RT_VERSION,
                                                 OBELISK_RT_ELEMENT_AGGREGATE,
                                                 9300,
                                                 OBELISK_RT_ELEMENT_FOUR_STATE,
                                                 0,
                                                 8,
                                                 1,
                                                 64,
                                                 nullptr,
                                                 nullptr};
  obelisk_rt_object_v1 *queue = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_queue_create(lane, &recordElement, UINT64_MAX, &queue),
      OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 queueRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &queueRoot, &queue),
            OBELISK_RT_OK);
  uint64_t original = UINT64_C(0x12345678abcd9876);
  uint64_t unknown = UINT64_C(0x5555555555555555);
  ASSERT_EQ(obelisk_rt_v1_queue_push(lane, queue, 0, &original, &unknown),
            OBELISK_RT_OK);
  obelisk_rt_object_v1 *parent = nullptr;
  ASSERT_EQ(obelisk_rt_v1_reference_path_index_create(
                lane, queue, 0, nullptr, 0, 0, nullptr, nullptr, 0, &parent),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 parentRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &parentRoot, &parent),
            OBELISK_RT_OK);
  obelisk_rt_object_v1 *views[2] = {};
  obelisk_rt_gc_root_v1 viewRoots[2] = {};
  for (unsigned index = 0; index != 2; ++index) {
    ASSERT_EQ(obelisk_rt_v1_reference_path_aggregate_element_create(
                  lane, parent, 0, 2, nullptr, nullptr, 0, 5, 0, 63, 1, 64, 8,
                  1, 9301, OBELISK_RT_ELEMENT_BITS, 0, 1, 1, 7, nullptr, 0,
                  &views[index]),
              OBELISK_RT_OK);
    ASSERT_EQ(
        obelisk_rt_v1_gc_root_push(lane, &viewRoots[index], &views[index]),
        OBELISK_RT_OK);
  }
  uint8_t selected = 0x65;
  ASSERT_EQ(
      obelisk_rt_v1_reference_path_store(lane, views[0], &selected, nullptr),
      OBELISK_RT_OK);
  uint64_t actual = 0, actualUnknown = 0;
  ASSERT_EQ(obelisk_rt_v1_container_read(queue, 0, &actual, &actualUnknown),
            OBELISK_RT_OK);
  uint64_t mask = UINT64_C(0x7f) << 5;
  EXPECT_EQ(actual, (original & ~mask) | (uint64_t{selected} << 5));
  EXPECT_EQ(actualUnknown, unknown & ~mask);
  uint32_t present = 0;
  ASSERT_EQ(
      obelisk_rt_v1_queue_pop(queue, 0, &actual, &actualUnknown, &present),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  selected = 0x36;
  ASSERT_EQ(
      obelisk_rt_v1_reference_path_store(lane, views[0], &selected, nullptr),
      OBELISK_RT_OK);
  selected = 0;
  ASSERT_EQ(
      obelisk_rt_v1_reference_path_load(views[1], &selected, nullptr, &present),
      OBELISK_RT_OK);
  EXPECT_EQ(selected, 0x36);
  for (unsigned index = 2; index != 0; --index)
    EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &viewRoots[index - 1]),
              OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &parentRoot), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &queueRoot), OBELISK_RT_OK);
}

// Equality-plan records for element-keyed associative arrays.
std::array<uint64_t, 4> keyPlanRecord(uint32_t opcode, uint64_t offset,
                                      uint64_t operand0 = 0,
                                      uint64_t operand1 = 0,
                                      uint64_t body = 0) {
  return {uint64_t{opcode} | (body << 32), offset, operand0, operand1};
}

std::vector<uint8_t>
keyPlanBytes(std::initializer_list<std::array<uint64_t, 4>> records) {
  std::vector<uint64_t> words{
      uint64_t{OBELISK_RT_ASSOC_KEY_PLAN_MAGIC} |
          (uint64_t{OBELISK_RT_ASSOC_KEY_PLAN_VERSION} << 32),
      records.size()};
  for (const auto &record : records)
    words.insert(words.end(), record.begin(), record.end());
  std::vector<uint8_t> bytes(words.size() * sizeof(uint64_t));
  for (size_t word = 0; word != words.size(); ++word)
    for (unsigned byte = 0; byte != 8; ++byte)
      bytes[word * 8 + byte] = static_cast<uint8_t>(words[word] >> (byte * 8));
  return bytes;
}

// struct { logic [3:0] l; string s; }: l in bits 0..3, s in the word at
// byte 8. Bits 4..63 are padding.
constexpr uint64_t structKeyTypeID = 900;
const obelisk_rt_element_trace_slot_v1 structKeyTrace{
    8, OBELISK_RT_MANAGED_SLOT_STRING, 0};

obelisk_rt_status createStructKeyed(obelisk_rt_gc_lane_v1 *lane,
                                    const std::vector<uint8_t> &plan,
                                    obelisk_rt_object_v1 **array) {
  return obelisk_rt_v1_assoc_create_element_keyed(
      lane, wordElement.type_id, wordElement.kind, wordElement.flags,
      wordElement.value_size, wordElement.alignment, wordElement.bit_width,
      nullptr, 0, structKeyTypeID, OBELISK_RT_ELEMENT_AGGREGATE,
      OBELISK_RT_ELEMENT_FOUR_STATE, 16, 1, 128, &structKeyTrace, 1,
      plan.data(), plan.size(), array);
}

const std::vector<uint8_t> structKeyPlan =
    keyPlanBytes({keyPlanRecord(OBELISK_RT_ASSOC_KEY_PLAN_BITS, 0, 4, 1),
                  keyPlanRecord(OBELISK_RT_ASSOC_KEY_PLAN_STRING, 64)});

struct StructKey {
  std::array<uint8_t, 16> value{};
  std::array<uint8_t, 16> unknown{};
  obelisk_rt_assoc_key_v1 key{};

  StructKey(uint8_t bits, obelisk_rt_string_v1 string) {
    value[0] = bits;
    std::memcpy(value.data() + 8, &string, sizeof(string));
    key.kind = OBELISK_RT_ASSOC_KEY_ELEMENT;
    key.width = 128;
    key.value_data = value.data();
    key.unknown_data = unknown.data();
  }
};

TEST_F(ManagedValueTest, AssociativeElementKeysCompareThroughTheirPlan) {
  ASSERT_EQ(obelisk_rt_v1_gc_set_threshold(context, 1), OBELISK_RT_OK);
  obelisk_rt_object_v1 *array = nullptr;
  ASSERT_EQ(createStructKeyed(lane, structKeyPlan, &array), OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 arrayRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &arrayRoot, &array),
            OBELISK_RT_OK);
  obelisk_rt_assoc_key_kind_v1 kind = 0;
  uint64_t width = 0;
  ASSERT_EQ(obelisk_rt_v1_assoc_key_info(array, &kind, &width), OBELISK_RT_OK);
  EXPECT_EQ(kind, uint32_t{OBELISK_RT_ASSOC_KEY_ELEMENT});
  EXPECT_EQ(width, 128u);

  obelisk_rt_string_v1 text = 0;
  ASSERT_EQ(obelisk_rt_v1_string_create(lane, "a long key", 10, &text),
            OBELISK_RT_OK);
  obelisk_rt_gc_managed_root_v1 textRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_managed_root_push(lane, &textRoot, &text),
            OBELISK_RT_OK);
  StructKey inserted(5, text);
  uint64_t stored = 10;
  ASSERT_EQ(
      obelisk_rt_v1_assoc_write(lane, array, &inserted.key, &stored, nullptr),
      OBELISK_RT_OK);
  // The array keeps its own key: the caller's string may be collected.
  ASSERT_EQ(obelisk_rt_v1_gc_managed_root_pop(lane, &textRoot), OBELISK_RT_OK);
  text = 0;
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);

  // An equal key compares its string by value and ignores padding bits and
  // their unknown plane.
  obelisk_rt_string_v1 equal = 0;
  ASSERT_EQ(obelisk_rt_v1_string_create(lane, "a long key", 10, &equal),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_managed_root_push(lane, &textRoot, &equal),
            OBELISK_RT_OK);
  StructKey lookup(0xf5, equal);
  lookup.value[1] = 0xab;
  lookup.unknown[0] = 0xf0;
  lookup.unknown[2] = 0xff;
  uint64_t value = 0;
  uint32_t present = 0;
  ASSERT_EQ(
      obelisk_rt_v1_assoc_read(array, &lookup.key, &value, nullptr, &present),
      OBELISK_RT_OK);
  EXPECT_EQ(present, 1u);
  EXPECT_EQ(value, stored);
  StructKey other(6, equal);
  ASSERT_EQ(
      obelisk_rt_v1_assoc_read(array, &other.key, &value, nullptr, &present),
      OBELISK_RT_OK);
  EXPECT_EQ(present, 0u);

  // IEEE 1800-2023 7.8.5: an X or Z bit in any compared part makes the index
  // invalid.
  StructKey unknownKey(5, equal);
  unknownKey.unknown[0] = 0x01;
  testing::internal::CaptureStderr();
  ASSERT_EQ(
      obelisk_rt_v1_assoc_write(lane, array, &unknownKey.key, &stored, nullptr),
      OBELISK_RT_OK);
  EXPECT_NE(testing::internal::GetCapturedStderr().find("X or Z"),
            std::string::npos);
  EXPECT_EQ(obelisk_rt_v1_container_size(array), 1u);

  // Traversal returns the stored key's planes.
  StructKey cursor(0, 0);
  uint32_t success = 0;
  ASSERT_EQ(obelisk_rt_v1_assoc_first(lane, array, &cursor.key, &success),
            OBELISK_RT_OK);
  ASSERT_EQ(success, 1u);
  EXPECT_EQ(cursor.value[0] & 0xf, 5);
  obelisk_rt_string_v1 cursorText = 0;
  std::memcpy(&cursorText, cursor.value.data() + 8, sizeof(cursorText));
  EXPECT_EQ(obelisk_rt_v1_string_compare(cursorText, equal), 0);
  ASSERT_EQ(obelisk_rt_v1_assoc_next(lane, array, &cursor.key, &success),
            OBELISK_RT_OK);
  EXPECT_EQ(success, 0u);

  obelisk_rt_object_v1 *copy = nullptr;
  ASSERT_EQ(obelisk_rt_v1_container_clone(lane, array, &copy), OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 copyRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &copyRoot, &copy), OBELISK_RT_OK);
  value = 0;
  ASSERT_EQ(
      obelisk_rt_v1_assoc_read(copy, &lookup.key, &value, nullptr, &present),
      OBELISK_RT_OK);
  EXPECT_EQ(present, 1u);
  EXPECT_EQ(value, stored);
  ASSERT_EQ(obelisk_rt_v1_assoc_delete(copy, &lookup.key), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_container_size(copy), 0u);
  EXPECT_EQ(obelisk_rt_v1_container_size(array), 1u);

  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &copyRoot), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_managed_root_pop(lane, &textRoot), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &arrayRoot), OBELISK_RT_OK);
}

TEST_F(ManagedValueTest, AssociativeElementKeysOwnTheirDynamicMembers) {
  // int k[] indexing int a[k]: the key is a dynamic array compared by size and
  // then element by element; a null array is the empty array.
  std::vector<uint8_t> plan = keyPlanBytes(
      {keyPlanRecord(OBELISK_RT_ASSOC_KEY_PLAN_SEQUENCE, 0, 8, 0, 1),
       keyPlanRecord(OBELISK_RT_ASSOC_KEY_PLAN_BITS, 0, 64, 0)});
  obelisk_rt_object_v1 *array = nullptr;
  ASSERT_EQ(obelisk_rt_v1_assoc_create_element_keyed(
                lane, wordElement.type_id, wordElement.kind, wordElement.flags,
                wordElement.value_size, wordElement.alignment,
                wordElement.bit_width, nullptr, 0, 901,
                OBELISK_RT_ELEMENT_CONTAINER_HANDLE, 0, 8, 1, 0, nullptr, 0,
                plan.data(), plan.size(), &array),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 arrayRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &arrayRoot, &array),
            OBELISK_RT_OK);
  obelisk_rt_object_v1 *keyArray = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_dynamic_array_create(lane, &wordElement, 2, &keyArray),
      OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 keyRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &keyRoot, &keyArray),
            OBELISK_RT_OK);
  auto setElement = [&](int64_t index, uint64_t element) {
    ASSERT_EQ(
        obelisk_rt_v1_container_write(lane, keyArray, index, &element, nullptr),
        OBELISK_RT_OK);
  };
  auto keyFor = [](obelisk_rt_object_v1 *const &handle) {
    obelisk_rt_assoc_key_v1 key{};
    key.kind = OBELISK_RT_ASSOC_KEY_ELEMENT;
    key.width = 64;
    key.value_data = const_cast<obelisk_rt_object_v1 **>(&handle);
    return key;
  };
  setElement(0, 1);
  setElement(1, 2);
  obelisk_rt_assoc_key_v1 key = keyFor(keyArray);
  uint64_t stored = 12;
  ASSERT_EQ(obelisk_rt_v1_assoc_write(lane, array, &key, &stored, nullptr),
            OBELISK_RT_OK);
  obelisk_rt_object_v1 *none = nullptr;
  obelisk_rt_assoc_key_v1 nullKey = keyFor(none);
  uint64_t empty = 3;
  ASSERT_EQ(obelisk_rt_v1_assoc_write(lane, array, &nullKey, &empty, nullptr),
            OBELISK_RT_OK);

  // Changing the caller's array leaves the stored key '{1, 2} intact.
  setElement(0, 9);
  uint64_t value = 0;
  uint32_t present = 0;
  ASSERT_EQ(obelisk_rt_v1_assoc_read(array, &key, &value, nullptr, &present),
            OBELISK_RT_OK);
  EXPECT_EQ(present, 0u);
  setElement(0, 1);
  ASSERT_EQ(obelisk_rt_v1_assoc_read(array, &key, &value, nullptr, &present),
            OBELISK_RT_OK);
  EXPECT_EQ(present, 1u);
  EXPECT_EQ(value, stored);

  obelisk_rt_object_v1 *emptyArray = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_dynamic_array_create(lane, &wordElement, 0, &emptyArray),
      OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 emptyRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &emptyRoot, &emptyArray),
            OBELISK_RT_OK);
  obelisk_rt_assoc_key_v1 emptyKey = keyFor(emptyArray);
  ASSERT_EQ(
      obelisk_rt_v1_assoc_read(array, &emptyKey, &value, nullptr, &present),
      OBELISK_RT_OK);
  EXPECT_EQ(present, 1u);
  EXPECT_EQ(value, empty);

  // The order is fixed by the canonical bytes: the empty array, then '{1, 2}.
  // The returned key's array is the stored key's own.
  obelisk_rt_object_v1 *cursorArray = nullptr;
  obelisk_rt_assoc_key_v1 cursor = keyFor(cursorArray);
  uint32_t success = 0;
  ASSERT_EQ(obelisk_rt_v1_assoc_first(lane, array, &cursor, &success),
            OBELISK_RT_OK);
  ASSERT_EQ(success, 1u);
  EXPECT_EQ(obelisk_rt_v1_container_size(cursorArray), 0u);
  ASSERT_EQ(obelisk_rt_v1_assoc_next(lane, array, &cursor, &success),
            OBELISK_RT_OK);
  ASSERT_EQ(success, 1u);
  ASSERT_NE(cursorArray, keyArray);
  EXPECT_EQ(obelisk_rt_v1_container_size(cursorArray), 2u);
  ASSERT_EQ(obelisk_rt_v1_assoc_prev(lane, array, &cursor, &success),
            OBELISK_RT_OK);
  ASSERT_EQ(success, 1u);
  EXPECT_EQ(obelisk_rt_v1_container_size(cursorArray), 0u);

  obelisk_rt_arg_v1 argument{OBELISK_RT_ARG_MANAGED_CONTAINER, 0, 0, &array,
                             nullptr};
  obelisk_rt_format_env_v1 environment{};
  environment.time_multiplier = 1;
  obelisk_rt_buffer_v1 output{};
  ASSERT_EQ(obelisk_rt_v1_format(context, "%p", 2, &argument, 1, &environment,
                                 &output),
            OBELISK_RT_OK);
  EXPECT_EQ(
      std::string(reinterpret_cast<const char *>(output.data), output.size),
      "'{'{}:3, '{1, 2}:12}");
  obelisk_rt_v1_buffer_release(&output);

  // Paths through equal keys share one capture, and deleting the entry
  // detaches it.
  obelisk_rt_object_v1 *paths[2] = {};
  ASSERT_EQ(obelisk_rt_v1_reference_path_assoc_create(lane, array, &key,
                                                      nullptr, 0, 0, nullptr,
                                                      nullptr, 0, &paths[0]),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 pathRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &pathRoot, &paths[0]),
            OBELISK_RT_OK);
  obelisk_rt_object_v1 *equalArray = nullptr;
  ASSERT_EQ(obelisk_rt_v1_container_clone(lane, keyArray, &equalArray),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 equalRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &equalRoot, &equalArray),
            OBELISK_RT_OK);
  obelisk_rt_assoc_key_v1 equalKey = keyFor(equalArray);
  ASSERT_EQ(obelisk_rt_v1_reference_path_assoc_create(lane, array, &equalKey,
                                                      nullptr, 0, 0, nullptr,
                                                      nullptr, 0, &paths[1]),
            OBELISK_RT_OK);
  EXPECT_EQ(paths[0], paths[1]);
  value = 0;
  ASSERT_EQ(
      obelisk_rt_v1_reference_path_load(paths[0], &value, nullptr, &present),
      OBELISK_RT_OK);
  EXPECT_EQ(value, stored);
  uint64_t replaced = 21;
  ASSERT_EQ(
      obelisk_rt_v1_reference_path_store(lane, paths[0], &replaced, nullptr),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_assoc_read(array, &key, &value, nullptr, &present),
            OBELISK_RT_OK);
  EXPECT_EQ(value, replaced);
  ASSERT_EQ(obelisk_rt_v1_assoc_delete(array, &equalKey), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_container_size(array), 1u);
  value = 0;
  ASSERT_EQ(
      obelisk_rt_v1_reference_path_load(paths[0], &value, nullptr, &present),
      OBELISK_RT_OK);
  EXPECT_EQ(value, replaced);
  ASSERT_EQ(obelisk_rt_v1_assoc_read(array, &key, &value, nullptr, &present),
            OBELISK_RT_OK);
  EXPECT_EQ(present, 0u);

  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &equalRoot), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &pathRoot), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &emptyRoot), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &keyRoot), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &arrayRoot), OBELISK_RT_OK);
}

TEST_F(ManagedValueTest, AssociativeElementKeysCompareActiveTaggedMembers) {
  // union tagged { int a; int b; }: a in bits 0..31, b in bits 32..63, and a
  // two-bit tag at bit 64 that is one for a and two for b.
  std::vector<uint8_t> plan = keyPlanBytes(
      {keyPlanRecord(OBELISK_RT_ASSOC_KEY_PLAN_TAGGED, 0, 64, 2, 4),
       keyPlanRecord(OBELISK_RT_ASSOC_KEY_PLAN_ALTERNATIVE, 0, 1, 0, 1),
       keyPlanRecord(OBELISK_RT_ASSOC_KEY_PLAN_BITS, 0, 32, 0),
       keyPlanRecord(OBELISK_RT_ASSOC_KEY_PLAN_ALTERNATIVE, 32, 2, 0, 1),
       keyPlanRecord(OBELISK_RT_ASSOC_KEY_PLAN_BITS, 0, 32, 0)});
  obelisk_rt_object_v1 *array = nullptr;
  ASSERT_EQ(obelisk_rt_v1_assoc_create_element_keyed(
                lane, wordElement.type_id, wordElement.kind, wordElement.flags,
                wordElement.value_size, wordElement.alignment,
                wordElement.bit_width, nullptr, 0, 902,
                OBELISK_RT_ELEMENT_AGGREGATE, 0, 9, 1, 72, nullptr, 0,
                plan.data(), plan.size(), &array),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 arrayRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &arrayRoot, &array),
            OBELISK_RT_OK);
  auto keyed = [&](uint8_t tag, uint32_t a, uint32_t b, uint64_t stored) {
    std::array<uint8_t, 9> value{};
    std::memcpy(value.data(), &a, sizeof(a));
    std::memcpy(value.data() + 4, &b, sizeof(b));
    value[8] = tag;
    obelisk_rt_assoc_key_v1 key{};
    key.kind = OBELISK_RT_ASSOC_KEY_ELEMENT;
    key.width = 72;
    key.value_data = value.data();
    EXPECT_EQ(obelisk_rt_v1_assoc_write(lane, array, &key, &stored, nullptr),
              OBELISK_RT_OK);
  };
  // The inactive member's stale bits are not part of the value.
  keyed(1, 7, 100, 1);
  keyed(1, 7, 200, 2);
  EXPECT_EQ(obelisk_rt_v1_container_size(array), 1u);
  keyed(2, 7, 7, 3);
  keyed(2, 8, 7, 4);
  EXPECT_EQ(obelisk_rt_v1_container_size(array), 2u);
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &arrayRoot), OBELISK_RT_OK);
}

TEST_F(ManagedValueTest, AssociativeElementKeyPlansAreValidatedAndInterned) {
  auto create = [&](const std::vector<uint8_t> &plan) {
    obelisk_rt_object_v1 *array = nullptr;
    return createStructKeyed(lane, plan, &array);
  };
  // Bits beyond the 128-bit key, an alternative outside a tagged union, and a
  // bad header are all rejected.
  EXPECT_EQ(create(keyPlanBytes(
                {keyPlanRecord(OBELISK_RT_ASSOC_KEY_PLAN_BITS, 120, 9, 0)})),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(create(keyPlanBytes({keyPlanRecord(
                OBELISK_RT_ASSOC_KEY_PLAN_ALTERNATIVE, 0, 1, 0, 0)})),
            OBELISK_RT_INVALID_ARGUMENT);
  std::vector<uint8_t> truncated = structKeyPlan;
  truncated.resize(truncated.size() - 8);
  EXPECT_EQ(create(truncated), OBELISK_RT_INVALID_ARGUMENT);
  // One key type has one plan.
  EXPECT_EQ(create(structKeyPlan), OBELISK_RT_OK);
  EXPECT_EQ(create(keyPlanBytes(
                {keyPlanRecord(OBELISK_RT_ASSOC_KEY_PLAN_BITS, 0, 4, 1)})),
            OBELISK_RT_INVALID_DESIGN);
}

TEST_F(ManagedValueTest, BuiltinClassQueriesDistinguishMailboxFromQueue) {
  obelisk_rt_object_v1 *mailbox = nullptr, *queue = nullptr,
                       *semaphore = nullptr;
  ASSERT_EQ(obelisk_rt_v1_mailbox_create_typed(
                lane, wordElement.type_id, wordElement.kind, wordElement.flags,
                wordElement.value_size, wordElement.alignment,
                wordElement.bit_width, nullptr, 0, 0, &mailbox),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 mailboxRoot{}, queueRoot{}, semaphoreRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &mailboxRoot, &mailbox),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_queue_create(lane, &wordElement, UINT64_MAX, &queue),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &queueRoot, &queue),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_semaphore_create(lane, 2, &semaphore), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &semaphoreRoot, &semaphore),
            OBELISK_RT_OK);
  for (auto [object, expected] :
       {std::pair{mailbox, BuiltinClassKind::Mailbox},
        std::pair{semaphore, BuiltinClassKind::Semaphore},
        std::pair{queue, BuiltinClassKind::None}}) {
    ManagedObjectLease lease;
    ASSERT_EQ(obelisk_rt_managed_object_acquire(
                  lane, object, OBELISK_RT_MANAGED_INVALID, &lease),
              OBELISK_RT_OK);
    EXPECT_EQ(obelisk_rt_builtin_class_kind(lease), expected);
  }
  ManagedObjectLease emptyLease;
  EXPECT_EQ(obelisk_rt_builtin_class_kind(emptyLease), BuiltinClassKind::None);
  const obelisk_rt_element_type_v1 *element = &wordElement;
  std::vector<uint8_t> values{42};
  EXPECT_EQ(obelisk_rt_mailbox_snapshot(queue, &element, values),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(element, nullptr);
  EXPECT_TRUE(values.empty());
  EXPECT_EQ(obelisk_rt_mailbox_snapshot(mailbox, nullptr, values),
            OBELISK_RT_INVALID_ARGUMENT);
  ASSERT_EQ(obelisk_rt_mailbox_snapshot(mailbox, &element, values),
            OBELISK_RT_OK);
  ASSERT_NE(element, nullptr);
  EXPECT_EQ(element->kind, OBELISK_RT_ELEMENT_BITS);
  EXPECT_TRUE(values.empty());
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &semaphoreRoot), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &queueRoot), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &mailboxRoot), OBELISK_RT_OK);
}

TEST_F(ManagedValueTest, MailboxSnapshotPreservesFourStateFIFOWithoutMutation) {
  obelisk_rt_object_v1 *mailbox = nullptr;
  ASSERT_EQ(obelisk_rt_v1_mailbox_create_typed(
                lane, logicElement.type_id, logicElement.kind,
                logicElement.flags, logicElement.value_size,
                logicElement.alignment, logicElement.bit_width, nullptr, 0, 0,
                &mailbox),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 root{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &root, &mailbox), OBELISK_RT_OK);
  auto put = [&](uint8_t value) {
    uint8_t unknown = value & 1;
    uint32_t success = 0;
    EXPECT_EQ(obelisk_rt_v1_mailbox_try_put(lane, mailbox, &value, &unknown,
                                            &success),
              OBELISK_RT_OK);
    EXPECT_EQ(success, 1u);
  };
  auto get = [&](uint8_t expected) {
    uint8_t value = 0, unknown = 0;
    uint32_t present = 0;
    EXPECT_EQ(
        obelisk_rt_v1_mailbox_try_get(mailbox, &value, &unknown, &present),
        OBELISK_RT_OK);
    EXPECT_EQ(present, 1u);
    EXPECT_EQ(value, expected);
    EXPECT_EQ(unknown, expected & 1);
  };
  for (uint8_t value : {1, 2, 3, 4})
    put(value);
  get(1);
  get(2);
  put(5);
  put(6);
  const obelisk_rt_element_type_v1 *element = nullptr;
  std::vector<uint8_t> values;
  ASSERT_EQ(obelisk_rt_mailbox_snapshot(mailbox, &element, values),
            OBELISK_RT_OK);
  ASSERT_NE(element, nullptr);
  EXPECT_EQ(element->kind, OBELISK_RT_ELEMENT_LOGIC);
  EXPECT_EQ(element->bit_width, 4u);
  EXPECT_EQ(element->value_size, 1u);
  const std::vector<uint8_t> expected{3, 1, 4, 0, 5, 1, 6, 0};
  EXPECT_EQ(values, expected);
  uint32_t count = 0;
  ASSERT_EQ(obelisk_rt_v1_mailbox_num(mailbox, &count), OBELISK_RT_OK);
  EXPECT_EQ(count, 4u);
  get(3);
  put(7);
  EXPECT_EQ(values, expected);
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &root), OBELISK_RT_OK);
}

class BuiltinInheritanceTest
    : public ManagedValueTest,
      public ::testing::WithParamInterface<BuiltinClassKind> {
protected:
  static constexpr uint64_t slotOffset = sizeof(obelisk_rt_managed_word_v1);
  static constexpr uint64_t instanceSize =
      slotOffset + sizeof(obelisk_rt_managed_word_v1);

  void SetUp() override {
    ManagedValueTest::SetUp();
    if (HasFatalFailure())
      return;
    kind = GetParam();
    slotKind = kind == BuiltinClassKind::Mailbox
                   ? OBELISK_RT_MANAGED_SLOT_MAILBOX
                   : OBELISK_RT_MANAGED_SLOT_SEMAPHORE;
    payloadTrace = {slotOffset, 0,      1, OBELISK_RT_TRACE_STRONG,
                    slotKind,   nullptr};
    classLayout = {OBELISK_RT_VERSION, 0,
                   instanceSize,       alignof(obelisk_rt_managed_word_v1),
                   &payloadTrace,      1};
    base.version = OBELISK_RT_VERSION;
    base.class_id = 1;
    base.instance_size = instanceSize;
    base.instance_alignment = alignof(obelisk_rt_managed_word_v1);
    base.layout = &classLayout;
    base.builtin_base_kind = kind == BuiltinClassKind::Mailbox
                                 ? OBELISK_RT_BUILTIN_CLASS_MAILBOX
                                 : OBELISK_RT_BUILTIN_CLASS_SEMAPHORE;
    base.builtin_base_offset = slotOffset;
    derived = base;
    derived.class_id = 2;
    derived.base = &base;
    derived.builtin_base_kind = OBELISK_RT_BUILTIN_CLASS_NONE;
    derived.builtin_base_offset = 0;
    holder = derived;
    holder.class_id = 3;
    holder.base = nullptr;
    ordinary = holder;
    ordinary.class_id = 4;
    ordinary.instance_size = sizeof(void *);
    ordinary.layout = nullptr;
    for (auto *descriptor : {&base, &derived, &holder, &ordinary})
      ASSERT_EQ(obelisk_rt_v1_class_register(context, descriptor),
                OBELISK_RT_OK);
    elementTrace = {0, 0, 1, OBELISK_RT_TRACE_STRONG, slotKind, nullptr};
    elementLayout = {OBELISK_RT_VERSION,
                     0,
                     sizeof(obelisk_rt_managed_word_v1),
                     alignof(obelisk_rt_managed_word_v1),
                     &elementTrace,
                     1};
    builtinElement = {OBELISK_RT_VERSION,
                      OBELISK_RT_ELEMENT_CONTAINER_HANDLE,
                      10,
                      0,
                      0,
                      sizeof(obelisk_rt_managed_word_v1),
                      alignof(obelisk_rt_managed_word_v1),
                      0,
                      &elementLayout,
                      nullptr};
  }

  obelisk_rt_status createPayload(obelisk_rt_object_v1 **out) {
    if (kind == BuiltinClassKind::Semaphore)
      return obelisk_rt_v1_semaphore_create(lane, 2, out);
    return obelisk_rt_v1_mailbox_create_typed(
        lane, wordElement.type_id, wordElement.kind, wordElement.flags,
        wordElement.value_size, wordElement.alignment, wordElement.bit_width,
        nullptr, 0, 0, out);
  }

  void expectPayloadOperations(obelisk_rt_object_v1 *outer,
                               obelisk_rt_object_v1 *payload) {
    obelisk_rt_object_v1 *resolved = nullptr;
    ASSERT_EQ(obelisk_rt_builtin_class_payload(outer, kind, &resolved),
              OBELISK_RT_OK);
    EXPECT_EQ(resolved, payload);
    ASSERT_EQ(obelisk_rt_builtin_class_payload(payload, kind, &resolved),
              OBELISK_RT_OK);
    EXPECT_EQ(resolved, payload);
    {
      ManagedObjectLease lease;
      ASSERT_EQ(obelisk_rt_managed_object_acquire(
                    lane, outer, OBELISK_RT_MANAGED_CLASS, &lease),
                OBELISK_RT_OK);
      EXPECT_EQ(obelisk_rt_builtin_class_kind(lease), kind);
    }
    uint32_t success = 0;
    if (kind == BuiltinClassKind::Semaphore) {
      ASSERT_EQ(obelisk_rt_v1_semaphore_try_get(outer, 1, &success),
                OBELISK_RT_OK);
      EXPECT_EQ(success, 1u);
      ASSERT_EQ(obelisk_rt_v1_semaphore_put(outer, 3), OBELISK_RT_OK);
      ASSERT_EQ(obelisk_rt_v1_semaphore_try_get(payload, 4, &success),
                OBELISK_RT_OK);
      EXPECT_EQ(success, 1u);
      ASSERT_EQ(obelisk_rt_v1_semaphore_try_get(outer, 1, &success),
                OBELISK_RT_OK);
      EXPECT_EQ(success, 0u);
    } else {
      uint64_t message = 73;
      ASSERT_EQ(obelisk_rt_v1_mailbox_try_put(lane, outer, &message, nullptr,
                                              &success),
                OBELISK_RT_OK);
      EXPECT_EQ(success, 1u);
      uint32_t count = 0;
      ASSERT_EQ(obelisk_rt_v1_mailbox_num(outer, &count), OBELISK_RT_OK);
      EXPECT_EQ(count, 1u);
      uint64_t received = 0;
      ASSERT_EQ(
          obelisk_rt_v1_mailbox_try_get(payload, &received, nullptr, &success),
          OBELISK_RT_OK);
      EXPECT_EQ(success, 1u);
      EXPECT_EQ(received, message);
      ASSERT_EQ(obelisk_rt_v1_mailbox_num(outer, &count), OBELISK_RT_OK);
      EXPECT_EQ(count, 0u);
    }
  }

  BuiltinClassKind kind = BuiltinClassKind::None;
  obelisk_rt_managed_slot_kind_v1 slotKind = OBELISK_RT_MANAGED_SLOT_INVALID;
  obelisk_rt_trace_entry_v1 payloadTrace{}, elementTrace{};
  obelisk_rt_trace_layout_v1 classLayout{}, elementLayout{};
  obelisk_rt_class_descriptor_v1 base{}, derived{}, holder{}, ordinary{};
  obelisk_rt_element_type_v1 builtinElement{};
};

TEST_P(BuiltinInheritanceTest, TypedClassSlotRetainsDerivedObjectAndPayload) {
  obelisk_rt_object_v1 *owner = nullptr, *payload = nullptr, *outer = nullptr;
  ASSERT_EQ(obelisk_rt_v1_object_allocate(lane, &holder, &owner),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 ownerRoot{}, payloadRoot{}, outerRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &ownerRoot, &owner),
            OBELISK_RT_OK);
  ASSERT_EQ(createPayload(&payload), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &payloadRoot, &payload),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_object_allocate(lane, &derived, &outer),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &outerRoot, &outer),
            OBELISK_RT_OK);
  uint64_t payloadWord = obelisk_rt_managed_word_from_object(payload);
  uint64_t outerWord = obelisk_rt_managed_word_from_object(outer);
  ASSERT_EQ(obelisk_rt_v1_object_write(outer, slotOffset, &payloadWord,
                                       sizeof(payloadWord)),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_object_write(owner, slotOffset, &outerWord,
                                       sizeof(outerWord)),
            OBELISK_RT_OK);
  const uint64_t identity = obelisk_rt_v1_object_id(outer);
  ASSERT_NE(identity, 0u);
  EXPECT_NE(identity, obelisk_rt_v1_object_id(payload));
  ASSERT_EQ(obelisk_rt_v1_gc_root_pop(lane, &outerRoot), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_root_pop(lane, &payloadRoot), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_object_id(outer), identity);
  expectPayloadOperations(outer, payload);
  EXPECT_EQ(obelisk_rt_v1_object_is_instance(outer, &base), 1u);
  EXPECT_EQ(obelisk_rt_v1_object_is_instance(outer, &derived), 1u);
  EXPECT_EQ(obelisk_rt_v1_object_is_instance(outer, &ordinary), 0u);
  obelisk_rt_object_v1 *cast = nullptr;
  ASSERT_EQ(obelisk_rt_v1_object_cast(outer, &base, &cast), OBELISK_RT_OK);
  EXPECT_EQ(cast, outer);
  ASSERT_EQ(obelisk_rt_v1_object_cast(payload, &base, &cast), OBELISK_RT_OK);
  EXPECT_EQ(cast, nullptr);
  ASSERT_EQ(obelisk_rt_v1_object_cast(nullptr, &base, &cast), OBELISK_RT_OK);
  EXPECT_EQ(cast, nullptr);

  // A typed builtin slot is not a candidate slot accepting arbitrary class
  // or container addresses, even though both heap kinds may implement it.
  obelisk_rt_object_v1 *wrong = nullptr;
  ASSERT_EQ(obelisk_rt_v1_queue_create(lane, &wordElement, UINT64_MAX, &wrong),
            OBELISK_RT_OK);
  uint64_t wrongWord = obelisk_rt_managed_word_from_object(wrong);
  EXPECT_EQ(obelisk_rt_v1_object_write(owner, slotOffset, &wrongWord,
                                       sizeof(wrongWord)),
            OBELISK_RT_INVALID_ARGUMENT);
  ASSERT_EQ(obelisk_rt_v1_object_allocate(lane, &ordinary, &wrong),
            OBELISK_RT_OK);
  wrongWord = obelisk_rt_managed_word_from_object(wrong);
  EXPECT_EQ(obelisk_rt_v1_object_write(owner, slotOffset, &wrongWord,
                                       sizeof(wrongWord)),
            OBELISK_RT_INVALID_ARGUMENT);
  if (kind == BuiltinClassKind::Mailbox) {
    ASSERT_EQ(obelisk_rt_v1_semaphore_create(lane, 0, &wrong), OBELISK_RT_OK);
  } else {
    ASSERT_EQ(obelisk_rt_v1_mailbox_create_typed(
                  lane, wordElement.type_id, wordElement.kind,
                  wordElement.flags, wordElement.value_size,
                  wordElement.alignment, wordElement.bit_width, nullptr, 0, 0,
                  &wrong),
              OBELISK_RT_OK);
  }
  wrongWord = obelisk_rt_managed_word_from_object(wrong);
  EXPECT_EQ(obelisk_rt_v1_object_write(owner, slotOffset, &wrongWord,
                                       sizeof(wrongWord)),
            OBELISK_RT_INVALID_ARGUMENT);
  uint64_t stored = 0;
  ASSERT_EQ(
      obelisk_rt_v1_object_read(owner, slotOffset, &stored, sizeof(stored)),
      OBELISK_RT_OK);
  EXPECT_EQ(stored, outerWord);
  stored = 0;
  ASSERT_EQ(
      obelisk_rt_v1_object_write(owner, slotOffset, &stored, sizeof(stored)),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  obelisk_rt_gc_statistics_v1 statistics{};
  ASSERT_EQ(obelisk_rt_v1_gc_statistics(context, &statistics), OBELISK_RT_OK);
  EXPECT_EQ(statistics.live_objects, 1u);
  ASSERT_EQ(obelisk_rt_v1_gc_root_pop(lane, &ownerRoot), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_statistics(context, &statistics), OBELISK_RT_OK);
  EXPECT_EQ(statistics.live_objects, 0u);
}

TEST_P(BuiltinInheritanceTest, TypedContainerElementRetainsDerivedIdentity) {
  obelisk_rt_object_v1 *queue = nullptr, *payload = nullptr, *outer = nullptr;
  ASSERT_EQ(obelisk_rt_v1_element_type_validate(&builtinElement),
            OBELISK_RT_OK);
  const obelisk_rt_element_trace_slot_v1 traceSlot{0, slotKind, 0};
  ASSERT_EQ(obelisk_rt_v1_container_create_typed(
                lane, OBELISK_RT_CONTAINER_QUEUE, builtinElement.type_id,
                builtinElement.kind, builtinElement.flags,
                builtinElement.value_size, builtinElement.alignment,
                builtinElement.bit_width, &traceSlot, 1, 0, UINT64_MAX, &queue),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 queueRoot{}, payloadRoot{}, outerRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &queueRoot, &queue),
            OBELISK_RT_OK);
  ASSERT_EQ(createPayload(&payload), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &payloadRoot, &payload),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_object_allocate(lane, &derived, &outer),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &outerRoot, &outer),
            OBELISK_RT_OK);
  uint64_t payloadWord = obelisk_rt_managed_word_from_object(payload);
  uint64_t outerWord = obelisk_rt_managed_word_from_object(outer);
  ASSERT_EQ(obelisk_rt_v1_object_write(outer, slotOffset, &payloadWord,
                                       sizeof(payloadWord)),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_queue_push(lane, queue, 1, &outerWord, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_root_pop(lane, &outerRoot), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_root_pop(lane, &payloadRoot), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  uint64_t stored = 0;
  ASSERT_EQ(obelisk_rt_v1_container_read(queue, 0, &stored, nullptr),
            OBELISK_RT_OK);
  EXPECT_EQ(stored, outerWord);
  expectPayloadOperations(outer, payload);
  // An ordinary container-handle slot keeps its original, stricter meaning.
  obelisk_rt_object_v1 *untyped = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_queue_create(lane, &containerElement, UINT64_MAX, &untyped),
      OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 untypedRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &untypedRoot, &untyped),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_queue_push(lane, untyped, 1, &outerWord, nullptr),
            OBELISK_RT_INVALID_HANDLE);
  ASSERT_EQ(obelisk_rt_v1_gc_root_pop(lane, &untypedRoot), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_container_delete(queue), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_root_pop(lane, &queueRoot), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_collect(lane), OBELISK_RT_OK);
  obelisk_rt_gc_statistics_v1 statistics{};
  ASSERT_EQ(obelisk_rt_v1_gc_statistics(context, &statistics), OBELISK_RT_OK);
  EXPECT_EQ(statistics.live_objects, 0u);
}

TEST_P(BuiltinInheritanceTest, RejectsMalformedBuiltinBaseDescriptors) {
  auto invalid = base;
  invalid.builtin_base_offset = 0;
  EXPECT_EQ(obelisk_rt_v1_class_validate(&invalid), OBELISK_RT_INVALID_DESIGN);
  invalid = base;
  invalid.builtin_base_kind = UINT32_MAX;
  EXPECT_EQ(obelisk_rt_v1_class_validate(&invalid), OBELISK_RT_INVALID_DESIGN);
  invalid = base;
  invalid.builtin_base_reserved = 1;
  EXPECT_EQ(obelisk_rt_v1_class_validate(&invalid), OBELISK_RT_INVALID_DESIGN);
  invalid = base;
  invalid.builtin_base_offset = instanceSize;
  EXPECT_EQ(obelisk_rt_v1_class_validate(&invalid), OBELISK_RT_INVALID_DESIGN);
  invalid = base;
  invalid.layout = nullptr;
  EXPECT_EQ(obelisk_rt_v1_class_validate(&invalid), OBELISK_RT_INVALID_DESIGN);
  auto wrongTrace = payloadTrace;
  wrongTrace.slot_kind = OBELISK_RT_MANAGED_SLOT_CONTAINER;
  auto wrongLayout = classLayout;
  wrongLayout.entries = &wrongTrace;
  invalid = base;
  invalid.layout = &wrongLayout;
  EXPECT_EQ(obelisk_rt_v1_class_validate(&invalid), OBELISK_RT_INVALID_DESIGN);
  invalid = derived;
  invalid.builtin_base_kind = base.builtin_base_kind;
  invalid.builtin_base_offset = base.builtin_base_offset;
  EXPECT_EQ(obelisk_rt_v1_class_validate(&invalid), OBELISK_RT_INVALID_DESIGN);
}

INSTANTIATE_TEST_SUITE_P(
    BuiltinClasses, BuiltinInheritanceTest,
    ::testing::Values(BuiltinClassKind::Mailbox, BuiltinClassKind::Semaphore),
    [](const ::testing::TestParamInfo<BuiltinClassKind> &info) {
      return info.param == BuiltinClassKind::Mailbox ? "Mailbox" : "Semaphore";
    });

TEST_F(ManagedValueTest, SequentialElementSnapshotsTrackResizeAndQueueOrder) {
  for (bool queueKind : {false, true}) {
    obelisk_rt_object_v1 *container = nullptr;
    ASSERT_EQ(queueKind ? obelisk_rt_v1_queue_create(lane, &logicElement,
                                                     UINT64_MAX, &container)
                        : obelisk_rt_v1_dynamic_array_create(
                              lane, &logicElement, 4, &container),
              OBELISK_RT_OK);
    obelisk_rt_gc_root_v1 root{};
    ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &root, &container),
              OBELISK_RT_OK);
    for (uint8_t i = 0; i != 4; ++i) {
      uint8_t value = i + 1, unknown = i & 1;
      ASSERT_EQ(queueKind ? obelisk_rt_v1_queue_push(lane, container, 0, &value,
                                                     &unknown)
                          : obelisk_rt_v1_container_write(lane, container, i,
                                                          &value, &unknown),
                OBELISK_RT_OK);
    }
    if (queueKind) {
      for (unsigned i = 0; i != 2; ++i) {
        uint8_t value = 0, unknown = 0;
        uint32_t present = 0;
        ASSERT_EQ(
            obelisk_rt_v1_queue_pop(container, 1, &value, &unknown, &present),
            OBELISK_RT_OK);
        ASSERT_EQ(present, 1u);
      }
      for (uint8_t value : {5, 6}) {
        uint8_t unknown = value & 1;
        ASSERT_EQ(
            obelisk_rt_v1_queue_push(lane, container, 0, &value, &unknown),
            OBELISK_RT_OK);
      }
    }
    VPIContainerElementSnapshot metadata;
    ASSERT_EQ(obelisk_rt_container_element_snapshot(container, std::nullopt,
                                                    &metadata),
              OBELISK_RT_OK);
    EXPECT_EQ(metadata.kind, queueKind ? OBELISK_RT_CONTAINER_QUEUE
                                       : OBELISK_RT_CONTAINER_DYNAMIC_ARRAY);
    EXPECT_EQ(metadata.size, 4u);
    ASSERT_NE(metadata.element, nullptr);
    EXPECT_EQ(metadata.element->kind, OBELISK_RT_ELEMENT_LOGIC);
    EXPECT_TRUE(metadata.value.empty());
    VPIContainerElementSnapshot selected;
    ASSERT_EQ(obelisk_rt_container_element_snapshot(container, 2, &selected),
              OBELISK_RT_OK);
    EXPECT_EQ(selected.value, queueKind ? (std::vector<uint8_t>{5, 1})
                                        : (std::vector<uint8_t>{3, 0}));
    EXPECT_EQ(obelisk_rt_v1_container_size(container), 4u);
    auto copied = selected.value;
    ASSERT_EQ(queueKind
                  ? obelisk_rt_v1_container_delete(container)
                  : obelisk_rt_v1_dynamic_array_resize(lane, container, 1),
              OBELISK_RT_OK);
    EXPECT_EQ(selected.value, copied);
    EXPECT_EQ(obelisk_rt_container_element_snapshot(container, 2, &selected),
              OBELISK_RT_EOF);
    EXPECT_EQ(selected.kind, 0u);
    EXPECT_EQ(selected.element, nullptr);
    EXPECT_TRUE(selected.value.empty());
    ASSERT_EQ(obelisk_rt_container_element_snapshot(container, std::nullopt,
                                                    &selected),
              OBELISK_RT_OK);
    EXPECT_EQ(selected.size, queueKind ? 0u : 1u);
    EXPECT_EQ(obelisk_rt_container_element_snapshot(container, 0, nullptr),
              OBELISK_RT_INVALID_ARGUMENT);
    ASSERT_EQ(obelisk_rt_v1_gc_root_pop(lane, &root), OBELISK_RT_OK);
  }
}

TEST_F(ManagedValueTest, SequentialElementSnapshotsRejectMailboxIdentity) {
  obelisk_rt_object_v1 *mailbox = nullptr;
  ASSERT_EQ(obelisk_rt_v1_mailbox_create_typed(
                lane, wordElement.type_id, wordElement.kind, wordElement.flags,
                wordElement.value_size, wordElement.alignment,
                wordElement.bit_width, nullptr, 0, 0, &mailbox),
            OBELISK_RT_OK);
  VPIContainerElementSnapshot snapshot;
  snapshot.kind = OBELISK_RT_CONTAINER_QUEUE;
  snapshot.value = {42};
  EXPECT_EQ(
      obelisk_rt_container_element_snapshot(mailbox, std::nullopt, &snapshot),
      OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(snapshot.kind, 0u);
  EXPECT_EQ(snapshot.element, nullptr);
  EXPECT_TRUE(snapshot.value.empty());
}

} // namespace
