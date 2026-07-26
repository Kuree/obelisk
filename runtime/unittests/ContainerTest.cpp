//===- ContainerTest.cpp - Managed string and container runtime tests -----===//

#include "obelisk/Runtime/Runtime.h"

#include "gtest/gtest.h"

#include <cstring>
#include <string>
#include <vector>

namespace {

const obelisk_rt_trace_entry_v1 managedWordTraceEntry{
    0, 0, 1, OBELISK_RT_TRACE_STRONG, OBELISK_RT_MANAGED_SLOT_CONTAINER,
    nullptr};
const obelisk_rt_trace_layout_v1 managedWordTraceLayout{
    OBELISK_RT_VERSION,     0, sizeof(void *), alignof(void *),
    &managedWordTraceEntry, 1};
const obelisk_rt_trace_entry_v1 stringTraceEntry{
    0, 0, 1, OBELISK_RT_TRACE_STRONG, OBELISK_RT_MANAGED_SLOT_STRING,
    nullptr};
const obelisk_rt_trace_layout_v1 stringTraceLayout{
    OBELISK_RT_VERSION, 0, sizeof(void *), alignof(void *), &stringTraceEntry,
    1};
const obelisk_rt_element_type_v1 wordElement{
    OBELISK_RT_VERSION, OBELISK_RT_ELEMENT_BITS, 1,  0,      0,
    sizeof(uint64_t),   alignof(uint64_t),       64, nullptr};
const obelisk_rt_element_type_v1 byteElement{
    OBELISK_RT_VERSION, OBELISK_RT_ELEMENT_BITS, 4, 0,      0,
    sizeof(uint8_t),    alignof(uint8_t),        8, nullptr};
const obelisk_rt_element_type_v1 logicElement{OBELISK_RT_VERSION,
                                              OBELISK_RT_ELEMENT_LOGIC,
                                              5,
                                              OBELISK_RT_ELEMENT_FOUR_STATE,
                                              0,
                                              sizeof(uint8_t),
                                              alignof(uint8_t),
                                              4,
                                              nullptr};
const obelisk_rt_element_type_v1 stringElement{OBELISK_RT_VERSION,
                                               OBELISK_RT_ELEMENT_STRING,
                                               2,
                                               0,
                                               0,
                                               sizeof(void *),
                                               alignof(void *),
                                               0,
                                               &stringTraceLayout};
const obelisk_rt_element_type_v1 containerElement{
    OBELISK_RT_VERSION,
    OBELISK_RT_ELEMENT_CONTAINER_HANDLE,
    3,
    0,
    0,
    sizeof(void *),
    alignof(void *),
    0,
    &managedWordTraceLayout};

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
  ASSERT_EQ(obelisk_rt_v1_string_from_packed(
                lane, packed, unknown, 32, &string),
            OBELISK_RT_OK);
  char scratch[8] = {};
  const char *bytes = nullptr;
  uint64_t size = 0;
  ASSERT_EQ(obelisk_rt_v1_string_view(string, scratch, &bytes, &size),
            OBELISK_RT_OK);
  ASSERT_EQ(size, 4u);
  EXPECT_EQ(static_cast<uint8_t>(bytes[0]), 'A');
  EXPECT_EQ(static_cast<uint8_t>(bytes[1]), 'B');
  EXPECT_EQ(static_cast<uint8_t>(bytes[2]), 0);
  EXPECT_EQ(static_cast<uint8_t>(bytes[3]), 'D');

  obelisk_rt_string_v1 repeated = 0;
  ASSERT_EQ(obelisk_rt_v1_string_repeat(lane, string, 3, &repeated),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_string_view(repeated, scratch, &bytes, &size),
            OBELISK_RT_OK);
  ASSERT_EQ(size, 12u);
  EXPECT_EQ(std::memcmp(bytes, "AB\0DAB\0DAB\0D", 12), 0);

  uint8_t narrowed[3] = {0xff, 0xff, 0xff};
  uint8_t narrowedUnknown[3] = {0xff, 0xff, 0xff};
  ASSERT_EQ(obelisk_rt_v1_string_to_packed(repeated, narrowed,
                                           narrowedUnknown, 20),
            OBELISK_RT_OK);
  EXPECT_EQ(narrowed[0], 'D');
  EXPECT_EQ(narrowed[1], 0);
  EXPECT_EQ(narrowed[2], 2u);
  EXPECT_EQ(narrowedUnknown[0], 0u);
  EXPECT_EQ(narrowedUnknown[1], 0u);
  EXPECT_EQ(narrowedUnknown[2], 0u);

  obelisk_rt_string_v1 empty = UINT64_MAX;
  EXPECT_EQ(obelisk_rt_v1_string_repeat(lane, string, 0, &empty),
            OBELISK_RT_OK);
  EXPECT_EQ(empty, 0u);
  EXPECT_EQ(obelisk_rt_v1_string_repeat(
                lane, string, UINT64_MAX, &empty),
            OBELISK_RT_OUT_OF_RESOURCES);
}

TEST_F(ManagedValueTest, ParsesAndFormatsStringNumbers) {
  obelisk_rt_string_v1 input = 0;
  ASSERT_EQ(obelisk_rt_v1_string_create(lane, " -1_23tail", 10, &input),
            OBELISK_RT_OK);
  uint64_t integer = 0;
  ASSERT_EQ(obelisk_rt_v1_string_parse_integer(input, 10, &integer),
            OBELISK_RT_OK);
  EXPECT_EQ(static_cast<int64_t>(integer), -123);

  ASSERT_EQ(obelisk_rt_v1_string_create(nullptr, "f_f", 3, &input),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_string_parse_integer(input, 16, &integer),
            OBELISK_RT_OK);
  EXPECT_EQ(integer, 255u);

  ASSERT_EQ(obelisk_rt_v1_string_create(lane, " 3.2_5junk", 11, &input),
            OBELISK_RT_OK);
  double real = 0.0;
  ASSERT_EQ(obelisk_rt_v1_string_parse_real(input, &real), OBELISK_RT_OK);
  EXPECT_DOUBLE_EQ(real, 3.25);

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
  ASSERT_EQ(
      obelisk_rt_v1_string_format_integer(lane, 255, 16, 0, &output),
      OBELISK_RT_OK);
  expectString(output, "ff");
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
  obelisk_rt_arg_v1 argument{
      OBELISK_RT_ARG_MANAGED_STRING, 0, 0, &value, nullptr};
  obelisk_rt_format_env_v1 environment{};
  environment.time_multiplier = 1;
  obelisk_rt_buffer_v1 output{};
  char scratch[8] = {};
  const char *formatBytes = nullptr;
  uint64_t formatSize = 0;
  ASSERT_EQ(obelisk_rt_v1_string_view(format, scratch, &formatBytes,
                                      &formatSize),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_format(context, formatBytes, formatSize, &argument,
                                 1, &environment, &output),
            OBELISK_RT_OK);
  EXPECT_EQ(std::string(reinterpret_cast<const char *>(output.data),
                        output.size),
            "value=managed!");
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
      "'{32'sb00000000000000000000000000000011, "
      "32'sb00000000000000000000000000000001}");
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
  EXPECT_EQ(obelisk_rt_v1_gc_managed_root_pop(lane, &lowerRoot),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_managed_root_pop(lane, &joinedRoot),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_managed_root_pop(lane, &rightRoot),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_managed_root_pop(lane, &leftRoot),
            OBELISK_RT_OK);
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

  EXPECT_EQ(obelisk_rt_v1_gc_managed_root_pop(lane, &roots[1]),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_managed_root_pop(lane, &roots[0]),
            OBELISK_RT_OK);
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
      0, 0, 1, OBELISK_RT_TRACE_STRONG, OBELISK_RT_MANAGED_SLOT_STRING,
      nullptr};
  const obelisk_rt_trace_layout_v1 traceLayout{
      OBELISK_RT_VERSION, 0, sizeof(void *), alignof(void *), &traceEntry, 1};
  const obelisk_rt_element_type_v1 first{
      OBELISK_RT_VERSION, OBELISK_RT_ELEMENT_STRING, 42, 0,           0,
      sizeof(void *),     alignof(void *),           0,  &traceLayout};
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

  obelisk_rt_element_type_v1 conflicting = first;
  conflicting.kind = OBELISK_RT_ELEMENT_CLASS_HANDLE;
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
      OBELISK_RT_ASSOC_KEY_UNSIGNED, 0, 8, 3, 1, 0};
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
  ASSERT_EQ(obelisk_rt_v1_container_write_checked(
                lane, words, 0, &word, sizeof(word), nullptr, 0),
            OBELISK_RT_OK);
  word = 0;
  ASSERT_EQ(obelisk_rt_v1_container_read_checked(
                words, 0, &word, sizeof(word), nullptr, 0),
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
  EXPECT_EQ(obelisk_rt_v1_container_write_checked(
                lane, logic, 0, &value, sizeof(value), nullptr, 0),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_container_read_checked(
                logic, 0, &value, sizeof(value), &unknown, 0),
            OBELISK_RT_INVALID_ARGUMENT);
  ASSERT_EQ(obelisk_rt_v1_container_write_checked(
                lane, logic, 0, &value, sizeof(value), &unknown,
                sizeof(unknown)),
            OBELISK_RT_OK);
}

TEST_F(ManagedValueTest, CreateLikePreservesSequentialContainerMetadata) {
  obelisk_rt_object_v1 *array = nullptr;
  ASSERT_EQ(obelisk_rt_v1_dynamic_array_create(lane, &wordElement, 1, &array),
            OBELISK_RT_OK);
  obelisk_rt_object_v1 *created = nullptr;
  ASSERT_EQ(obelisk_rt_v1_container_create_like(lane, array, nullptr, 3,
                                                &created),
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
  EXPECT_EQ(obelisk_rt_v1_gc_managed_root_pop(lane, &textRoot),
            OBELISK_RT_OK);
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
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &queueRoot), OBELISK_RT_OK);
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
    obelisk_rt_assoc_key_v1 key{OBELISK_RT_ASSOC_KEY_SIGNED,      0, 16,
                                static_cast<uint16_t>(sourceKey), 0, 0};
    uint64_t value = static_cast<uint64_t>(sourceKey + 1000);
    ASSERT_EQ(obelisk_rt_v1_assoc_write(lane, array, &key, &value, nullptr),
              OBELISK_RT_OK);
  }
  EXPECT_EQ(obelisk_rt_v1_container_size(array), std::size(keys));

  obelisk_rt_assoc_key_v1 missing{
      OBELISK_RT_ASSOC_KEY_SIGNED, 0, 16, 88, 0, 0};
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

  obelisk_rt_assoc_key_v1 removed{OBELISK_RT_ASSOC_KEY_SIGNED, 0, 16,
                                  static_cast<uint16_t>(-3),   0, 0};
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
      OBELISK_RT_ASSOC_KEY_SIGNED, 0, 16, 7, 0, 0};
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

TEST_F(ManagedValueTest, AssociativeSlotsPreserveAlignmentForByteElements) {
  obelisk_rt_object_v1 *array = nullptr;
  ASSERT_EQ(obelisk_rt_v1_assoc_create(
                lane, &byteElement, OBELISK_RT_ASSOC_KEY_UNSIGNED, 32, &array),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 root{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &root, &array), OBELISK_RT_OK);
  for (uint64_t index = 0; index != 40; ++index) {
    obelisk_rt_assoc_key_v1 key{
        OBELISK_RT_ASSOC_KEY_UNSIGNED, 0, 32, index * 17, 0, 0};
    uint8_t value = static_cast<uint8_t>(index + 1);
    ASSERT_EQ(obelisk_rt_v1_assoc_write(lane, array, &key, &value, nullptr),
              OBELISK_RT_OK);
  }
  for (uint64_t index = 0; index != 40; ++index) {
    obelisk_rt_assoc_key_v1 key{
        OBELISK_RT_ASSOC_KEY_UNSIGNED, 0, 32, index * 17, 0, 0};
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
  obelisk_rt_assoc_key_v1 key{OBELISK_RT_ASSOC_KEY_STRING, 0, 0, 0, 0, keyText};
  uint64_t stored = 55;
  ASSERT_EQ(obelisk_rt_v1_assoc_write(lane, array, &key, &stored, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_root_pop(lane, &arrayRoot), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_gc_managed_root_pop(lane, &keyRoot),
            OBELISK_RT_OK);
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
        OBELISK_RT_ASSOC_KEY_STRING, 0, 0, 0, 0, keyString};
    uint64_t value = std::strlen(text);
    ASSERT_EQ(obelisk_rt_v1_assoc_write(lane, array, &key, &value, nullptr),
              OBELISK_RT_OK);
    ASSERT_EQ(obelisk_rt_v1_gc_managed_root_pop(lane, &keyRoot),
              OBELISK_RT_OK);
  }

  obelisk_rt_string_v1 cursorString = 0;
  ASSERT_EQ(obelisk_rt_v1_string_create(lane, "bravo", 5, &cursorString),
            OBELISK_RT_OK);
  obelisk_rt_assoc_key_v1 cursor{
      OBELISK_RT_ASSOC_KEY_STRING, 0, 0, 0, 0, cursorString};
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
      OBELISK_RT_ASSOC_KEY_STRING, 0, 0, 0, 0, foreignString};
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

TEST_F(ManagedValueTest, ReferencePathsResolveAgainAfterContainerMutation) {
  obelisk_rt_object_v1 *queue = nullptr;
  ASSERT_EQ(obelisk_rt_v1_queue_create(lane, &wordElement, UINT64_MAX, &queue),
            OBELISK_RT_OK);
  obelisk_rt_gc_root_v1 queueRoot{};
  ASSERT_EQ(obelisk_rt_v1_gc_root_push(lane, &queueRoot, &queue),
            OBELISK_RT_OK);
  obelisk_rt_object_v1 *path = nullptr;
  ASSERT_EQ(
      obelisk_rt_v1_reference_path_index_create(lane, queue, 0, 0, 0, &path),
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

  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &pathRoot), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_gc_root_pop(lane, &queueRoot), OBELISK_RT_OK);
}

} // namespace
