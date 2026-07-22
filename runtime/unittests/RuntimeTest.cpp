//===- RuntimeTest.cpp - Tests for the Obelisk native runtime -------------===//

#include "obelisk/Runtime/Runtime.h"

#include "gtest/gtest.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <new>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

extern "C" int obelisk_runtime_c_api_smoke(void);

namespace {

class RuntimeBuffer {
public:
  RuntimeBuffer() = default;
  RuntimeBuffer(const RuntimeBuffer &) = delete;
  RuntimeBuffer &operator=(const RuntimeBuffer &) = delete;
  ~RuntimeBuffer() { obelisk_rt_v1_buffer_release(&buffer); }

  obelisk_rt_buffer_v1 *out() { return &buffer; }
  std::string str() const {
    if (buffer.size == 0)
      return {};
    return std::string(reinterpret_cast<const char *>(buffer.data),
                       static_cast<size_t>(buffer.size));
  }

private:
  obelisk_rt_buffer_v1 buffer{};
};

class LogicValue {
public:
  explicit LogicValue(std::string_view symbols, bool isSigned = false)
      : width(symbols.size()), value((width + 63) / 64),
        unknown((width + 63) / 64), isSigned(isSigned) {
    for (size_t index = 0; index < symbols.size(); ++index) {
      char symbol = symbols[symbols.size() - index - 1];
      if (symbol == '1' || symbol == 'z' || symbol == 'Z')
        value[index / 64] |= uint64_t{1} << (index % 64);
      if (symbol == 'x' || symbol == 'X' || symbol == 'z' || symbol == 'Z')
        unknown[index / 64] |= uint64_t{1} << (index % 64);
    }
  }

  LogicValue(uint64_t width, std::vector<uint64_t> value,
             std::vector<uint64_t> unknown = {}, bool isSigned = false)
      : width(width), value(std::move(value)), unknown(std::move(unknown)),
        isSigned(isSigned) {}

  obelisk_rt_arg_v1 arg(uint32_t extraFlags = 0) const {
    return {OBELISK_RT_ARG_LOGIC,
            extraFlags |
                (isSigned ? static_cast<uint32_t>(OBELISK_RT_ARG_SIGNED) : 0u),
            width, value.data(), unknown.empty() ? nullptr : unknown.data()};
  }

private:
  uint64_t width;
  std::vector<uint64_t> value;
  std::vector<uint64_t> unknown;
  bool isSigned;
};

obelisk_rt_arg_v1 stringArg(std::string_view value, uint32_t flags = 0) {
  return {OBELISK_RT_ARG_STRING, flags, value.size(), value.data(), nullptr};
}

obelisk_rt_arg_v1 realArg(const double &value) {
  return {OBELISK_RT_ARG_REAL, 0, 0, &value, nullptr};
}

obelisk_rt_arg_v1 timeArg(const uint64_t &value) {
  return {OBELISK_RT_ARG_TIME, 0, 64, &value, nullptr};
}

void appendInstruction(std::vector<uint8_t> &code, uint8_t opcode,
                       uint8_t type = OBELISK_RT_BC_TYPE_NONE,
                       uint16_t destination = 0, uint16_t source0 = 0,
                       uint16_t source1 = 0, uint64_t immediate = 0) {
  size_t offset = code.size();
  code.resize(offset + OBELISK_RT_BYTECODE_INSTRUCTION_SIZE, 0);
  code[offset] = opcode;
  code[offset + 1] = type;
  auto write16 = [&](size_t byte, uint16_t value) {
    code[offset + byte] = static_cast<uint8_t>(value);
    code[offset + byte + 1] = static_cast<uint8_t>(value >> 8);
  };
  write16(2, destination);
  write16(4, source0);
  write16(6, source1);
  for (unsigned byte = 0; byte != 8; ++byte)
    code[offset + 8 + byte] = static_cast<uint8_t>(immediate >> (byte * 8));
}

obelisk_rt_fragment_descriptor_v1
bytecodeDescriptor(const std::vector<uint8_t> &code, uint32_t registers) {
  static constexpr obelisk_rt_bytecode_entry_v1 defaultEntry{0, 0};
  obelisk_rt_fragment_descriptor_v1 descriptor{};
  descriptor.handle = {OBELISK_RT_DESCRIPTOR_FRAGMENT, 0, 7};
  descriptor.code_kind = OBELISK_RT_FRAGMENT_BYTECODE;
  descriptor.code.bytecode = {code.data(), code.size(), &defaultEntry, 1,
                              registers,   0,           nullptr};
  return descriptor;
}

obelisk_rt_status
executeBytecode(const obelisk_rt_fragment_descriptor_v1 &input, void *frame,
                uint64_t frameSize, uint32_t continuation,
                obelisk_rt_fragment_action_v1 *action,
                uint64_t instructionLimit = 0) {
  obelisk_rt_fragment_descriptor_v1 descriptor = input;
  descriptor.code.bytecode.register_offset = frameSize;
  uint64_t scratchSize =
      static_cast<uint64_t>(descriptor.code.bytecode.register_count) *
      OBELISK_RT_BYTECODE_REGISTER_SIZE;
  std::vector<uint8_t> storage(frameSize + scratchSize);
  if (frameSize != 0)
    std::memcpy(storage.data(), frame, frameSize);
  obelisk_rt_status status =
      instructionLimit == 0
          ? obelisk_rt_v1_fragment_execute(&descriptor, nullptr,
                                           storage.empty() ? nullptr
                                                           : storage.data(),
                                           storage.size(), continuation, action)
          : obelisk_rt_v1_bytecode_execute_bounded(
                &descriptor, nullptr,
                storage.empty() ? nullptr : storage.data(), storage.size(),
                continuation, instructionLimit, action);
  if (frameSize != 0)
    std::memcpy(frame, storage.data(), frameSize);
  return status;
}

std::string readHostFile(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

class TempDirectory {
public:
  TempDirectory() {
    static std::atomic<uint64_t> sequence{0};
    uint64_t stamp = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    path = std::filesystem::temp_directory_path() /
           ("obelisk-runtime-" + std::to_string(stamp) + "-" +
            std::to_string(sequence.fetch_add(1)));
    std::filesystem::create_directory(path);
  }
  ~TempDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }

  std::filesystem::path file(std::string_view name) const {
    return path / std::string(name);
  }

private:
  std::filesystem::path path;
};

class RuntimeTest : public testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(obelisk_rt_v1_context_create(&context), OBELISK_RT_OK);
    ASSERT_NE(context, nullptr);
  }

  void TearDown() override { obelisk_rt_v1_context_destroy(context); }

  std::pair<obelisk_rt_status, std::string>
  format(std::string_view formatString,
         const std::vector<obelisk_rt_arg_v1> &arguments,
         const obelisk_rt_format_env_v1 *environment = nullptr) {
    RuntimeBuffer output;
    obelisk_rt_status status = obelisk_rt_v1_format(
        context, formatString.data(), formatString.size(), arguments.data(),
        arguments.size(), environment, output.out());
    return {status, output.str()};
  }

  uint32_t open(const std::filesystem::path &path, std::string_view mode) {
    std::string pathString = path.string();
    uint32_t descriptor = 0;
    EXPECT_EQ(obelisk_rt_v1_file_open(context, pathString.data(),
                                      pathString.size(), mode.data(),
                                      mode.size(), &descriptor),
              OBELISK_RT_OK);
    EXPECT_NE(descriptor, 0u);
    return descriptor;
  }

  obelisk_rt_context *context = nullptr;
};

TEST(RuntimeABI, StableScalarLayout) {
  EXPECT_EQ(sizeof(obelisk_rt_status), 4u);
  EXPECT_EQ(sizeof(obelisk_rt_arg_kind), 4u);
  EXPECT_EQ(sizeof(obelisk_rt_arg_flags), 4u);
  EXPECT_EQ(offsetof(obelisk_rt_arg_v1, kind), 0u);
  EXPECT_EQ(offsetof(obelisk_rt_arg_v1, flags), 4u);
  EXPECT_EQ(offsetof(obelisk_rt_arg_v1, size), 8u);
  EXPECT_EQ(OBELISK_RT_ABI_VERSION, 1u);
  EXPECT_STREQ(obelisk_rt_v1_status_string(OBELISK_RT_FORMAT_ERROR),
               "format error");
}

TEST(RuntimeABI, CConsumerCompilesLinksAndRuns) {
  EXPECT_EQ(obelisk_runtime_c_api_smoke(), 0);
}

TEST(RuntimeABI, ReportsEveryStatusAndReleasesBuffersIdempotently) {
  static constexpr std::pair<obelisk_rt_status, const char *> statuses[] = {
      {OBELISK_RT_OK, "ok"},
      {OBELISK_RT_EOF, "end of file"},
      {OBELISK_RT_INVALID_ARGUMENT, "invalid argument"},
      {OBELISK_RT_INVALID_HANDLE, "invalid handle"},
      {OBELISK_RT_IO_ERROR, "I/O error"},
      {OBELISK_RT_OUT_OF_MEMORY, "out of memory"},
      {OBELISK_RT_OUT_OF_RESOURCES, "out of resources"},
      {OBELISK_RT_FORMAT_ERROR, "format error"},
      {OBELISK_RT_ARGUMENT_MISMATCH, "format argument mismatch"},
      {OBELISK_RT_INVALID_BYTECODE, "invalid bytecode"},
      {OBELISK_RT_STEP_LIMIT, "fragment step limit exceeded"},
  };
  for (const auto &[status, message] : statuses)
    EXPECT_STREQ(obelisk_rt_v1_status_string(status), message);
  EXPECT_STREQ(obelisk_rt_v1_status_string(-1), "unknown runtime status");

  obelisk_rt_buffer_v1 buffer{};
  obelisk_rt_v1_buffer_release(nullptr);
  obelisk_rt_v1_buffer_release(&buffer);
  obelisk_rt_v1_buffer_release(&buffer);
  EXPECT_EQ(buffer.data, nullptr);
  EXPECT_EQ(buffer.size, 0u);
}

TEST(RuntimeABI, RejectsNullPublicArguments) {
  EXPECT_EQ(obelisk_rt_v1_context_create(nullptr), OBELISK_RT_INVALID_ARGUMENT);
  obelisk_rt_v1_context_destroy(nullptr);
  EXPECT_EQ(obelisk_rt_v1_last_error(nullptr, nullptr),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(
      obelisk_rt_v1_format(nullptr, nullptr, 0, nullptr, 0, nullptr, nullptr),
      OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_display(nullptr, 0, 0, OBELISK_RT_RADIX_DECIMAL,
                                  nullptr, 0, nullptr),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_file_open_mcd(nullptr, nullptr, 0, nullptr),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_file_open(nullptr, nullptr, 0, nullptr, 0, nullptr),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_file_close(nullptr, 0), OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_file_flush(nullptr, 0), OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_file_write(nullptr, 0, nullptr, 0, nullptr),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_file_read(nullptr, 0, nullptr, 0, nullptr),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_file_getc(nullptr, 0, nullptr),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_file_ungetc(nullptr, 0, 0),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_file_getline(nullptr, 0, nullptr),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_file_eof(nullptr, 0, nullptr),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_file_error(nullptr, 0, nullptr, nullptr),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_file_seek(nullptr, 0, 0, OBELISK_RT_SEEK_SET),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_file_tell(nullptr, 0, nullptr),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_file_rewind(nullptr, 0), OBELISK_RT_INVALID_ARGUMENT);
}

TEST_F(RuntimeTest, FormatsExactFourStateRadices) {
  LogicValue value("10xz01xz");
  auto [status, output] =
      format("%b %o %h", {value.arg(), value.arg(), value.arg()});
  EXPECT_EQ(status, OBELISK_RT_OK);
  EXPECT_EQ(output, "10xz01xz 2XX XX");

  LogicValue leading("0000xxxx");
  auto [leadingStatus, leadingOutput] =
      format("%h %0h %4h %-4h",
             {leading.arg(), leading.arg(), leading.arg(), leading.arg()});
  EXPECT_EQ(leadingStatus, OBELISK_RT_OK);
  EXPECT_EQ(leadingOutput, "0x 0x 000x 0x00");
}

TEST_F(RuntimeTest, FormatsUnknownAndSignedDecimal) {
  LogicValue allX("xxxxxxxx");
  LogicValue allZ("zzzzzzzz");
  LogicValue mixedX("000x0001");
  LogicValue mixedZ("000z0001");
  auto [unknownStatus, unknownOutput] = format(
      "%0d %0d %0d %0d", {allX.arg(), allZ.arg(), mixedX.arg(), mixedZ.arg()});
  EXPECT_EQ(unknownStatus, OBELISK_RT_OK);
  EXPECT_EQ(unknownOutput, "x z X Z");

  LogicValue negativeFiveBits("11101", true);
  LogicValue unsignedFiveBits("00011");
  auto [smallStatus, smallOutput] =
      format("%d|%0d|%d", {negativeFiveBits.arg(), negativeFiveBits.arg(),
                           unsignedFiveBits.arg()});
  EXPECT_EQ(smallStatus, OBELISK_RT_OK);
  EXPECT_EQ(smallOutput, " -3|-3| 3");

  LogicValue sixtyFiveBits(65, {0, 1});
  auto [wideStatus, wideOutput] = format("%0d", {sixtyFiveBits.arg()});
  EXPECT_EQ(wideStatus, OBELISK_RT_OK);
  EXPECT_EQ(wideOutput, "18446744073709551616");
}

TEST_F(RuntimeTest, FormatsArbitraryNonPowerOfTwoWidths) {
  LogicValue oneBit("z");
  EXPECT_EQ(format("%b", {oneBit.arg()}).second, "z");

  std::string symbols = "1" + std::string(126, '0') + "xz0";
  ASSERT_EQ(symbols.size(), 130u);
  LogicValue wide(symbols);
  auto [status, output] = format("%b", {wide.arg()});
  EXPECT_EQ(status, OBELISK_RT_OK);
  EXPECT_EQ(output, symbols);

  LogicValue negativeSixtyFiveBits(65, {~uint64_t{2}, 1}, {},
                                   /*isSigned=*/true);
  LogicValue ignoredPaddingBits(65, {3, ~uint64_t{0}}, {0, ~uint64_t{1}});
  auto [decimalStatus, decimalOutput] = format(
      "%0d %0d", {negativeSixtyFiveBits.arg(), ignoredPaddingBits.arg()});
  EXPECT_EQ(decimalStatus, OBELISK_RT_OK);
  EXPECT_EQ(decimalOutput, "-3 18446744073709551619");
}

TEST_F(RuntimeTest, FormatsStringsRealsTimeAndEnvironment) {
  LogicValue hexValue("00001010");
  double real = 3.25;
  uint64_t time = 10;
  std::string text = "ok";
  std::string scope = "top.worker";
  std::string location = "work.top";
  std::string suffix = "ns";
  obelisk_rt_format_env_v1 environment{
      scope.data(),  scope.size(), location.data(), location.size(), 20, 0,
      suffix.data(), suffix.size()};

  auto [status, output] =
      format("[%4h][%-4h][%0h][%4s][%-4s] %.2f %m %l %0t%%",
             {hexValue.arg(), hexValue.arg(), hexValue.arg(), stringArg(text),
              stringArg(text), realArg(real), timeArg(time)},
             &environment);
  EXPECT_EQ(status, OBELISK_RT_OK);
  EXPECT_EQ(output,
            "[000a][a000][a][  ok][ok  ] 3.25 top.worker work.top 10ns%");
}

TEST_F(RuntimeTest, FormatsRemainingScalarFormsAndEmptyStrings) {
  LogicValue letter("01000001");
  double real = 12.5;
  std::string pattern = "pattern";
  obelisk_rt_arg_v1 emptyString{OBELISK_RT_ARG_STRING, 0, 0, nullptr, nullptr};

  auto [status, output] =
      format("%c|%.1e|%.1E|%.3g|%p|%p|[%s]",
             {letter.arg(), realArg(real), realArg(real), realArg(real),
              realArg(real), stringArg(pattern), emptyString});
  EXPECT_EQ(status, OBELISK_RT_OK);
  EXPECT_EQ(output, "A|1.2e+01|1.2E+01|12.5|12.5|pattern|[]");

  double padded = 3.25;
  auto [paddingStatus, paddingOutput] =
      format("[%08.2f][%-8.2f]", {realArg(padded), realArg(padded)});
  EXPECT_EQ(paddingStatus, OBELISK_RT_OK);
  EXPECT_EQ(paddingOutput, "[00003.25][3.25    ]");

  uint64_t time = 10;
  auto [timeStatus, timeOutput] = format("[%t]", {timeArg(time)});
  EXPECT_EQ(timeStatus, OBELISK_RT_OK);
  EXPECT_EQ(timeOutput, "[" + std::string(18, ' ') + "10]");
}

TEST_F(RuntimeTest, FormatsPackedStringsAndScalarPatterns) {
  LogicValue packed("010000010000000001000010"); // "A", NUL, "B"
  LogicValue pattern("10xz", true);
  auto [status, output] = format("%s %p", {packed.arg(), pattern.arg()});
  EXPECT_EQ(status, OBELISK_RT_OK);
  EXPECT_EQ(output, "AB 4'sb10xz");
}

TEST_F(RuntimeTest, EmitsRawTwoAndFourStateChunks) {
  LogicValue value("x00000000000000000000000000000001");
  auto [twoStatus, rawTwo] = format("%u", {value.arg()});
  ASSERT_EQ(twoStatus, OBELISK_RT_OK);
  ASSERT_EQ(rawTwo.size(), 8u);
  uint32_t twoWords[2];
  std::memcpy(twoWords, rawTwo.data(), sizeof(twoWords));
  EXPECT_EQ(twoWords[0], 1u);
  EXPECT_EQ(twoWords[1], 0u);

  auto [fourStatus, rawFour] = format("%z", {value.arg()});
  ASSERT_EQ(fourStatus, OBELISK_RT_OK);
  ASSERT_EQ(rawFour.size(), 16u);
  uint32_t fourWords[4];
  std::memcpy(fourWords, rawFour.data(), sizeof(fourWords));
  EXPECT_EQ(fourWords[0], 1u);
  EXPECT_EQ(fourWords[1], 0u);
  EXPECT_EQ(fourWords[2], 1u);
  EXPECT_EQ(fourWords[3], 1u);
}

TEST_F(RuntimeTest, RejectsMalformedFormatsAndArgumentMismatch) {
  LogicValue value("1");
  EXPECT_EQ(format("%q", {value.arg()}).first, OBELISK_RT_FORMAT_ERROR);
  EXPECT_EQ(format("%", {}).first, OBELISK_RT_FORMAT_ERROR);
  EXPECT_EQ(format("%42949672960d", {value.arg()}).first,
            OBELISK_RT_FORMAT_ERROR);
  EXPECT_EQ(format("%.42949672960f", {value.arg()}).first,
            OBELISK_RT_FORMAT_ERROR);
  EXPECT_EQ(format("%.2d", {value.arg()}).first, OBELISK_RT_FORMAT_ERROR);
  EXPECT_EQ(format("%2c", {value.arg()}).first, OBELISK_RT_FORMAT_ERROR);
  EXPECT_EQ(format("%d", {}).first, OBELISK_RT_ARGUMENT_MISMATCH);
  EXPECT_EQ(format("", {value.arg()}).first, OBELISK_RT_ARGUMENT_MISMATCH);
  EXPECT_EQ(format("%s", {value.arg()}).first, OBELISK_RT_OK);
  EXPECT_EQ(format("%f", {value.arg()}).first, OBELISK_RT_OK);

  obelisk_rt_format_env_v1 invalidEnvironment{};
  invalidEnvironment.scope_size = 1;
  EXPECT_EQ(format("%m", {}, &invalidEnvironment).first,
            OBELISK_RT_INVALID_ARGUMENT);
  invalidEnvironment = {};
  invalidEnvironment.time_suffix_size = 1;
  EXPECT_EQ(format("%t", {value.arg()}, &invalidEnvironment).first,
            OBELISK_RT_INVALID_ARGUMENT);

  obelisk_rt_arg_v1 zeroWidth{OBELISK_RT_ARG_LOGIC, 0, 0, nullptr, nullptr};
  EXPECT_EQ(format("%d", {zeroWidth}).first, OBELISK_RT_ARGUMENT_MISMATCH);

  RuntimeBuffer message;
  ASSERT_EQ(obelisk_rt_v1_last_error(context, message.out()), OBELISK_RT_OK);
  EXPECT_FALSE(message.str().empty());
}

TEST_F(RuntimeTest, DisplayHandlesFormatItemsDefaultsAndNewline) {
  TempDirectory temporary;
  uint32_t descriptor = open(temporary.file("display.bin"), "w+b");
  LogicValue value("00001010");
  std::string formatString = "v=%0h ";
  std::string text = "tail";
  std::vector<obelisk_rt_arg_v1> items = {
      stringArg(formatString, OBELISK_RT_ARG_FORMAT_STRING), value.arg(),
      stringArg(text), value.arg()};
  ASSERT_EQ(obelisk_rt_v1_display(context, descriptor, 1,
                                  OBELISK_RT_RADIX_BINARY, items.data(),
                                  items.size(), nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_file_flush(context, descriptor), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_file_rewind(context, descriptor), OBELISK_RT_OK);

  char bytes[64]{};
  uint64_t read = 0;
  ASSERT_EQ(
      obelisk_rt_v1_file_read(context, descriptor, bytes, sizeof(bytes), &read),
      OBELISK_RT_OK);
  EXPECT_EQ(std::string(bytes, static_cast<size_t>(read)),
            "v=a tail00001010\n");
  EXPECT_EQ(obelisk_rt_v1_file_close(context, descriptor), OBELISK_RT_OK);
}

TEST_F(RuntimeTest, DisplayValidatesItemsAndHandlesEmptyValues) {
  TempDirectory temporary;
  uint32_t descriptor = open(temporary.file("display-empty.bin"), "w+b");
  double real = 2.5;
  obelisk_rt_arg_v1 empty{OBELISK_RT_ARG_EMPTY, 0, 0, nullptr, nullptr};
  obelisk_rt_arg_v1 emptyString{OBELISK_RT_ARG_STRING, 0, 0, nullptr, nullptr};
  obelisk_rt_arg_v1 emptyFormat{
      OBELISK_RT_ARG_STRING, OBELISK_RT_ARG_FORMAT_STRING, 0, nullptr, nullptr};
  obelisk_rt_arg_v1 invalidFormat{
      OBELISK_RT_ARG_LOGIC, OBELISK_RT_ARG_FORMAT_STRING, 1, nullptr, nullptr};
  obelisk_rt_arg_v1 invalidKind{99, 0, 0, nullptr, nullptr};
  obelisk_rt_arg_v1 invalidString{OBELISK_RT_ARG_STRING, 0, 1, nullptr,
                                  nullptr};
  std::string malformed = "%q";
  obelisk_rt_arg_v1 malformedFormat =
      stringArg(malformed, OBELISK_RT_ARG_FORMAT_STRING);

  EXPECT_EQ(
      obelisk_rt_v1_display(context, descriptor, 0, 3, nullptr, 0, nullptr),
      OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_display(context, descriptor, 0,
                                  OBELISK_RT_RADIX_DECIMAL, &invalidFormat, 1,
                                  nullptr),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_display(context, descriptor, 0,
                                  OBELISK_RT_RADIX_DECIMAL, &invalidKind, 1,
                                  nullptr),
            OBELISK_RT_ARGUMENT_MISMATCH);
  EXPECT_EQ(obelisk_rt_v1_display(context, descriptor, 0,
                                  OBELISK_RT_RADIX_DECIMAL, &invalidString, 1,
                                  nullptr),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_display(context, descriptor, 0,
                                  OBELISK_RT_RADIX_DECIMAL, &malformedFormat, 1,
                                  nullptr),
            OBELISK_RT_FORMAT_ERROR);

  std::vector<obelisk_rt_arg_v1> items = {empty, realArg(real), emptyString,
                                          emptyFormat};
  ASSERT_EQ(obelisk_rt_v1_display(context, descriptor, 0,
                                  OBELISK_RT_RADIX_DECIMAL, items.data(),
                                  items.size(), nullptr),
            OBELISK_RT_OK);
  LogicValue value("00001010");
  obelisk_rt_arg_v1 logic = value.arg();
  ASSERT_EQ(obelisk_rt_v1_display(context, descriptor, 0,
                                  OBELISK_RT_RADIX_OCTAL, &logic, 1, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_display(context, descriptor, 0, OBELISK_RT_RADIX_HEX,
                                  &logic, 1, nullptr),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_file_flush(context, descriptor), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_file_rewind(context, descriptor), OBELISK_RT_OK);

  char bytes[32]{};
  uint64_t read = 0;
  ASSERT_EQ(
      obelisk_rt_v1_file_read(context, descriptor, bytes, sizeof(bytes), &read),
      OBELISK_RT_OK);
  EXPECT_EQ(std::string(bytes, static_cast<size_t>(read)), " 2.5000000120a");
  EXPECT_EQ(obelisk_rt_v1_file_close(context, descriptor), OBELISK_RT_OK);
}

TEST_F(RuntimeTest, ReadsWritesAndPositionsBinaryFiles) {
  TempDirectory temporary;
  uint32_t descriptor = open(temporary.file("roundtrip.bin"), "w+b");
  const std::string bytes("a\0bc", 4);
  uint64_t written = 0;
  ASSERT_EQ(obelisk_rt_v1_file_write(context, descriptor, nullptr, 0, &written),
            OBELISK_RT_OK);
  EXPECT_EQ(written, 0u);
  ASSERT_EQ(obelisk_rt_v1_file_write(context, descriptor, bytes.data(),
                                     bytes.size(), &written),
            OBELISK_RT_OK);
  EXPECT_EQ(written, bytes.size());

  int64_t offset = -1;
  ASSERT_EQ(obelisk_rt_v1_file_tell(context, descriptor, &offset),
            OBELISK_RT_OK);
  EXPECT_EQ(offset, 4);
  ASSERT_EQ(
      obelisk_rt_v1_file_seek(context, descriptor, -2, OBELISK_RT_SEEK_CUR),
      OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_file_tell(context, descriptor, &offset),
            OBELISK_RT_OK);
  EXPECT_EQ(offset, 2);
  ASSERT_EQ(obelisk_rt_v1_file_rewind(context, descriptor), OBELISK_RT_OK);

  char result[4]{};
  uint64_t read = 0;
  ASSERT_EQ(obelisk_rt_v1_file_read(context, descriptor, nullptr, 0, &read),
            OBELISK_RT_OK);
  EXPECT_EQ(read, 0u);
  ASSERT_EQ(obelisk_rt_v1_file_read(context, descriptor, result, sizeof(result),
                                    &read),
            OBELISK_RT_OK);
  EXPECT_EQ(read, 4u);
  EXPECT_EQ(std::string(result, sizeof(result)), bytes);
  EXPECT_EQ(obelisk_rt_v1_file_close(context, descriptor), OBELISK_RT_OK);
}

TEST_F(RuntimeTest, ReadsBytesLinesAndReportsEOF) {
  TempDirectory temporary;
  std::filesystem::path path = temporary.file("lines.bin");
  {
    std::ofstream output(path, std::ios::binary);
    output.write("a\0b\nlast", 8);
  }
  uint32_t descriptor = open(path, "rb");

  uint8_t byte = 0;
  ASSERT_EQ(obelisk_rt_v1_file_getc(context, descriptor, &byte), OBELISK_RT_OK);
  EXPECT_EQ(byte, 'a');
  ASSERT_EQ(obelisk_rt_v1_file_ungetc(context, descriptor, byte),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_file_getc(context, descriptor, &byte), OBELISK_RT_OK);
  EXPECT_EQ(byte, 'a');

  RuntimeBuffer firstLine;
  ASSERT_EQ(obelisk_rt_v1_file_getline(context, descriptor, firstLine.out()),
            OBELISK_RT_OK);
  EXPECT_EQ(firstLine.str(), std::string("\0b\n", 3));
  RuntimeBuffer secondLine;
  ASSERT_EQ(obelisk_rt_v1_file_getline(context, descriptor, secondLine.out()),
            OBELISK_RT_OK);
  EXPECT_EQ(secondLine.str(), "last");
  RuntimeBuffer eofLine;
  EXPECT_EQ(obelisk_rt_v1_file_getline(context, descriptor, eofLine.out()),
            OBELISK_RT_EOF);
  uint32_t isEOF = 0;
  ASSERT_EQ(obelisk_rt_v1_file_eof(context, descriptor, &isEOF), OBELISK_RT_OK);
  EXPECT_EQ(isEOF, 1u);
  EXPECT_EQ(obelisk_rt_v1_file_close(context, descriptor), OBELISK_RT_OK);
}

TEST_F(RuntimeTest, FlushesAllStreamsAndReportsByteEOF) {
  TempDirectory temporary;
  std::string mcdPath = temporary.file("flush-mcd.txt").string();
  uint32_t mcd = 0;
  ASSERT_EQ(obelisk_rt_v1_file_open_mcd(context, mcdPath.data(), mcdPath.size(),
                                        &mcd),
            OBELISK_RT_OK);
  uint32_t descriptor = open(temporary.file("flush-file.txt"), "w");
  uint64_t written = 0;
  ASSERT_EQ(obelisk_rt_v1_file_write(context, mcd, "mcd", 3, &written),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_file_write(context, descriptor, "file", 4, &written),
            OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_file_flush(context, 0), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_file_flush(context, 1), OBELISK_RT_OK);
  EXPECT_EQ(readHostFile(mcdPath), "mcd");
  EXPECT_EQ(readHostFile(temporary.file("flush-file.txt")), "file");
  EXPECT_EQ(obelisk_rt_v1_file_close(context, mcd), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_file_close(context, descriptor), OBELISK_RT_OK);

  uint32_t empty = open(temporary.file("empty.txt"), "w+b");
  uint8_t byte = 0xff;
  EXPECT_EQ(obelisk_rt_v1_file_getc(context, empty, &byte), OBELISK_RT_EOF);
  EXPECT_EQ(obelisk_rt_v1_file_close(context, empty), OBELISK_RT_OK);
}

TEST_F(RuntimeTest, RejectsInvalidFileArgumentsAndOpenFailures) {
  TempDirectory temporary;
  std::string missing = temporary.file("missing/child.txt").string();
  uint32_t descriptor = 123;
  EXPECT_EQ(obelisk_rt_v1_file_open(context, missing.data(), missing.size(),
                                    "r", 1, &descriptor),
            OBELISK_RT_IO_ERROR);
  EXPECT_EQ(descriptor, 0u);
  descriptor = 123;
  EXPECT_EQ(obelisk_rt_v1_file_open_mcd(context, missing.data(), missing.size(),
                                        &descriptor),
            OBELISK_RT_IO_ERROR);
  EXPECT_EQ(descriptor, 0u);

  constexpr uint32_t invalidDescriptor = 0x8000ffffu;
  char byte = 0;
  uint8_t unsignedByte = 0;
  uint64_t count = 0;
  uint32_t eof = 0;
  int32_t errorCode = 0;
  int64_t offset = 0;
  RuntimeBuffer line;
  RuntimeBuffer error;
  EXPECT_EQ(
      obelisk_rt_v1_file_write(context, invalidDescriptor, &byte, 1, &count),
      OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(
      obelisk_rt_v1_file_read(context, invalidDescriptor, &byte, 1, &count),
      OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(obelisk_rt_v1_file_getc(context, invalidDescriptor, &unsignedByte),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(obelisk_rt_v1_file_ungetc(context, invalidDescriptor, 'x'),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(obelisk_rt_v1_file_getline(context, invalidDescriptor, line.out()),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(obelisk_rt_v1_file_eof(context, invalidDescriptor, &eof),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(obelisk_rt_v1_file_error(context, invalidDescriptor, &errorCode,
                                     error.out()),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(obelisk_rt_v1_file_seek(context, invalidDescriptor, 0,
                                    OBELISK_RT_SEEK_SET),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(obelisk_rt_v1_file_tell(context, invalidDescriptor, &offset),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(obelisk_rt_v1_file_flush(context, invalidDescriptor),
            OBELISK_RT_INVALID_HANDLE);
  EXPECT_EQ(obelisk_rt_v1_file_close(context, invalidDescriptor),
            OBELISK_RT_INVALID_HANDLE);

  uint32_t valid = open(temporary.file("seek.txt"), "w+");
  EXPECT_EQ(obelisk_rt_v1_file_seek(context, valid, 0, 99),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_file_write(context, valid, nullptr, 1, &count),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_file_read(context, valid, nullptr, 1, &count),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(obelisk_rt_v1_file_close(context, valid), OBELISK_RT_OK);
}

TEST_F(RuntimeTest, SupportsAppendAndUpdateModes) {
  TempDirectory temporary;
  std::filesystem::path path = temporary.file("append.txt");
  uint32_t first = open(path, "w");
  uint64_t count = 0;
  ASSERT_EQ(obelisk_rt_v1_file_write(context, first, "one", 3, &count),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_file_close(context, first), OBELISK_RT_OK);

  uint32_t append = open(path, "a+b");
  ASSERT_EQ(obelisk_rt_v1_file_write(context, append, "two", 3, &count),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_file_rewind(context, append), OBELISK_RT_OK);
  char bytes[6]{};
  uint64_t read = 0;
  ASSERT_EQ(
      obelisk_rt_v1_file_read(context, append, bytes, sizeof(bytes), &read),
      OBELISK_RT_OK);
  EXPECT_EQ(std::string(bytes, static_cast<size_t>(read)), "onetwo");
  EXPECT_EQ(obelisk_rt_v1_file_close(context, append), OBELISK_RT_OK);
}

TEST_F(RuntimeTest, AcceptsEveryStandardFileMode) {
  static constexpr std::string_view modes[] = {
      "r",  "w",   "a",   "r+",  "w+",  "a+",  "rb", "wb",
      "ab", "r+b", "w+b", "a+b", "rb+", "wb+", "ab+"};
  TempDirectory temporary;
  for (size_t index = 0; index < std::size(modes); ++index) {
    std::filesystem::path path =
        temporary.file("mode-" + std::to_string(index));
    if (modes[index].front() == 'r') {
      std::ofstream seed(path, std::ios::binary);
      seed << "seed";
    }
    SCOPED_TRACE(modes[index]);
    uint32_t descriptor = open(path, modes[index]);
    bool readable = modes[index].front() == 'r' ||
                    modes[index].find('+') != std::string_view::npos;
    bool writable = modes[index].front() != 'r' ||
                    modes[index].find('+') != std::string_view::npos;
    if (writable) {
      uint64_t written = 0;
      EXPECT_EQ(obelisk_rt_v1_file_write(context, descriptor, "x", 1, &written),
                OBELISK_RT_OK);
      EXPECT_EQ(written, 1u);
    }
    if (readable) {
      ASSERT_EQ(obelisk_rt_v1_file_rewind(context, descriptor), OBELISK_RT_OK);
      char byte = 0;
      uint64_t read = 0;
      EXPECT_EQ(obelisk_rt_v1_file_read(context, descriptor, &byte, 1, &read),
                OBELISK_RT_OK);
      EXPECT_EQ(read, 1u);
      EXPECT_EQ(byte, writable ? 'x' : 's');
    }
    EXPECT_EQ(obelisk_rt_v1_file_close(context, descriptor), OBELISK_RT_OK);
  }
}

TEST_F(RuntimeTest, FansOutMultichannelDescriptors) {
  TempDirectory temporary;
  std::string firstPath = temporary.file("first.txt").string();
  std::string secondPath = temporary.file("second.txt").string();
  uint32_t first = 0;
  uint32_t second = 0;
  ASSERT_EQ(obelisk_rt_v1_file_open_mcd(context, firstPath.data(),
                                        firstPath.size(), &first),
            OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_file_open_mcd(context, secondPath.data(),
                                        secondPath.size(), &second),
            OBELISK_RT_OK);
  EXPECT_EQ(first & second, 0u);
  uint64_t written = 0;
  ASSERT_EQ(
      obelisk_rt_v1_file_write(context, first | second, "fanout", 6, &written),
      OBELISK_RT_OK);
  EXPECT_EQ(written, 6u);
  ASSERT_EQ(obelisk_rt_v1_file_close(context, first | second), OBELISK_RT_OK);
  EXPECT_EQ(readHostFile(firstPath), "fanout");
  EXPECT_EQ(readHostFile(secondPath), "fanout");
}

TEST_F(RuntimeTest, ReportsMCDExhaustionAndReusesChannels) {
  TempDirectory temporary;
  uint32_t combined = 0;
  for (uint32_t index = 0; index < 30; ++index) {
    std::string path = temporary.file("mcd-" + std::to_string(index)).string();
    uint32_t descriptor = 0;
    ASSERT_EQ(obelisk_rt_v1_file_open_mcd(context, path.data(), path.size(),
                                          &descriptor),
              OBELISK_RT_OK);
    EXPECT_EQ(combined & descriptor, 0u);
    combined |= descriptor;
  }
  EXPECT_EQ(combined, 0x7ffffffeu);
  std::string overflowPath = temporary.file("overflow").string();
  uint32_t overflow = 123;
  EXPECT_EQ(obelisk_rt_v1_file_open_mcd(context, overflowPath.data(),
                                        overflowPath.size(), &overflow),
            OBELISK_RT_OUT_OF_RESOURCES);
  EXPECT_EQ(overflow, 0u);
  ASSERT_EQ(obelisk_rt_v1_file_close(context, combined), OBELISK_RT_OK);

  uint32_t reused = 0;
  ASSERT_EQ(obelisk_rt_v1_file_open_mcd(context, overflowPath.data(),
                                        overflowPath.size(), &reused),
            OBELISK_RT_OK);
  EXPECT_NE(reused, 0u);
  EXPECT_EQ(obelisk_rt_v1_file_close(context, reused), OBELISK_RT_OK);
}

TEST_F(RuntimeTest, ValidatesModesHandlesAndFileErrors) {
  TempDirectory temporary;
  std::string path = temporary.file("errors.txt").string();
  uint32_t descriptor = 99;
  EXPECT_EQ(obelisk_rt_v1_file_open(context, path.data(), path.size(), "bad", 3,
                                    &descriptor),
            OBELISK_RT_INVALID_ARGUMENT);
  EXPECT_EQ(descriptor, 0u);

  const char pathWithNul[] = {'b', 'a', 'd', '\0', 'p', 'a', 't', 'h'};
  EXPECT_EQ(obelisk_rt_v1_file_open(context, pathWithNul, sizeof(pathWithNul),
                                    "w", 1, &descriptor),
            OBELISK_RT_INVALID_ARGUMENT);

  uint32_t readOnly = open(temporary.file("missing.txt"), "w+");
  EXPECT_EQ(obelisk_rt_v1_file_close(context, readOnly), OBELISK_RT_OK);
  EXPECT_EQ(obelisk_rt_v1_file_close(context, readOnly),
            OBELISK_RT_INVALID_HANDLE);

  readOnly = open(temporary.file("missing.txt"), "r");
  uint64_t written = 0;
  EXPECT_EQ(obelisk_rt_v1_file_write(context, readOnly, "x", 1, &written),
            OBELISK_RT_IO_ERROR);
  int32_t errorCode = 0;
  RuntimeBuffer errorMessage;
  ASSERT_EQ(obelisk_rt_v1_file_error(context, readOnly, &errorCode,
                                     errorMessage.out()),
            OBELISK_RT_OK);
  EXPECT_NE(errorCode, 0);
  EXPECT_FALSE(errorMessage.str().empty());
  EXPECT_EQ(obelisk_rt_v1_file_close(context, readOnly), OBELISK_RT_OK);
}

TEST_F(RuntimeTest, ReusesDescriptorsAndClosesOwnedFilesOnDestroy) {
  TempDirectory temporary;
  std::filesystem::path firstPath = temporary.file("first.txt");
  uint32_t first = open(firstPath, "w");
  ASSERT_EQ(obelisk_rt_v1_file_close(context, first), OBELISK_RT_OK);
  uint32_t reused = open(temporary.file("second.txt"), "w");
  EXPECT_EQ(first, reused);

  uint64_t written = 0;
  ASSERT_EQ(obelisk_rt_v1_file_write(context, reused, "saved", 5, &written),
            OBELISK_RT_OK);
  obelisk_rt_v1_context_destroy(context);
  context = nullptr;
  EXPECT_EQ(readHostFile(temporary.file("second.txt")), "saved");
}

TEST_F(RuntimeTest, KeepsLastErrorsIsolatedPerThread) {
  static constexpr std::array<std::string_view, 4> formats = {"%q", "x%q",
                                                              "xx%q", "xxx%q"};
  std::array<obelisk_rt_status, formats.size()> formatStatuses{};
  std::array<obelisk_rt_status, formats.size()> errorStatuses{};
  std::array<std::string, formats.size()> messages;
  std::atomic<size_t> ready{0};
  std::vector<std::thread> threads;

  for (size_t index = 0; index < formats.size(); ++index) {
    threads.emplace_back([&, index] {
      formatStatuses[index] = format(formats[index], {}).first;
      ready.fetch_add(1);
      while (ready.load() != formats.size())
        std::this_thread::yield();
      RuntimeBuffer message;
      errorStatuses[index] = obelisk_rt_v1_last_error(context, message.out());
      messages[index] = message.str();
    });
  }
  for (std::thread &thread : threads)
    thread.join();

  for (size_t index = 0; index < formats.size(); ++index) {
    EXPECT_EQ(formatStatuses[index], OBELISK_RT_FORMAT_ERROR);
    EXPECT_EQ(errorStatuses[index], OBELISK_RT_OK);
    EXPECT_EQ(messages[index],
              "unknown format specifier at byte " + std::to_string(index));
  }
}

TEST_F(RuntimeTest, SerializesConcurrentWholeMessageWrites) {
  TempDirectory temporary;
  uint32_t descriptor = open(temporary.file("threads.txt"), "w+");
  constexpr int threadCount = 4;
  constexpr int writesPerThread = 50;
  std::vector<std::thread> threads;
  for (int thread = 0; thread < threadCount; ++thread) {
    threads.emplace_back([&, thread] {
      std::string line = "thread-" + std::to_string(thread) + "\n";
      for (int write = 0; write < writesPerThread; ++write) {
        uint64_t count = 0;
        EXPECT_EQ(obelisk_rt_v1_file_write(context, descriptor, line.data(),
                                           line.size(), &count),
                  OBELISK_RT_OK);
        EXPECT_EQ(count, line.size());
      }
    });
  }
  for (std::thread &thread : threads)
    thread.join();
  ASSERT_EQ(obelisk_rt_v1_file_flush(context, descriptor), OBELISK_RT_OK);
  ASSERT_EQ(obelisk_rt_v1_file_rewind(context, descriptor), OBELISK_RT_OK);

  std::string contents(threadCount * writesPerThread * 9, '\0');
  uint64_t read = 0;
  ASSERT_EQ(obelisk_rt_v1_file_read(context, descriptor, contents.data(),
                                    contents.size(), &read),
            OBELISK_RT_OK);
  contents.resize(static_cast<size_t>(read));
  std::array<int, threadCount> counts{};
  size_t position = 0;
  while (position < contents.size()) {
    size_t newline = contents.find('\n', position);
    ASSERT_NE(newline, std::string::npos);
    std::string_view line(contents.data() + position, newline - position);
    ASSERT_EQ(line.size(), std::string_view("thread-0").size());
    ASSERT_TRUE(line.substr(0, 7) == "thread-");
    ASSERT_GE(line.back(), '0');
    ASSERT_LT(line.back(), static_cast<char>('0' + threadCount));
    ++counts[static_cast<size_t>(line.back() - '0')];
    position = newline + 1;
  }
  EXPECT_EQ(position, contents.size());
  for (int count : counts)
    EXPECT_EQ(count, writesPerThread);
  EXPECT_EQ(obelisk_rt_v1_file_close(context, descriptor), OBELISK_RT_OK);
}

TEST(RuntimeFragmentTest, ExecutesTypedBytecodeThroughSharedABI) {
  std::vector<uint8_t> code;
  appendInstruction(code, OBELISK_RT_BC_CONST, OBELISK_RT_BC_TYPE_U64, 0, 0, 0,
                    19);
  appendInstruction(code, OBELISK_RT_BC_CONST, OBELISK_RT_BC_TYPE_U64, 1, 0, 0,
                    23);
  appendInstruction(code, OBELISK_RT_BC_ADD, OBELISK_RT_BC_TYPE_U64, 2, 0, 1);
  appendInstruction(code, OBELISK_RT_BC_STORE_FRAME, OBELISK_RT_BC_TYPE_U64, 0,
                    2, 0, 8);
  appendInstruction(code, OBELISK_RT_BC_CONST, OBELISK_RT_BC_TYPE_U64, 3, 0, 0,
                    1234);
  appendInstruction(code, OBELISK_RT_BC_SUSPEND, OBELISK_RT_BC_TYPE_NONE, 0,
                    OBELISK_RT_SUSPEND_DELAY, 3, 0x89abcdefu);
  auto descriptor = bytecodeDescriptor(code, 4);
  std::array<uint64_t, 2> frame{};
  obelisk_rt_fragment_action_v1 action{};

  EXPECT_EQ(
      executeBytecode(descriptor, frame.data(), sizeof(frame), 0, &action),
      OBELISK_RT_OK);
  EXPECT_EQ(frame[1], 42u);
  EXPECT_EQ(action.kind, OBELISK_RT_FRAGMENT_SUSPEND);
  EXPECT_EQ(action.suspend_kind, OBELISK_RT_SUSPEND_DELAY);
  EXPECT_EQ(action.continuation, 0x89abcdefu);
  EXPECT_EQ(action.payload, 1234u);
}

TEST(RuntimeFragmentTest, ExecutesArithmeticComparisonAndControlOpcodes) {
  std::vector<uint8_t> code;
  appendInstruction(code, OBELISK_RT_BC_CONST, OBELISK_RT_BC_TYPE_U64, 0, 0, 0,
                    7);
  appendInstruction(code, OBELISK_RT_BC_MOVE, OBELISK_RT_BC_TYPE_U64, 1, 0);
  appendInstruction(code, OBELISK_RT_BC_CONST, OBELISK_RT_BC_TYPE_U64, 2, 0, 0,
                    3);
  appendInstruction(code, OBELISK_RT_BC_SUB, OBELISK_RT_BC_TYPE_U64, 3, 1, 2);
  appendInstruction(code, OBELISK_RT_BC_MUL, OBELISK_RT_BC_TYPE_U64, 4, 3, 2);
  appendInstruction(code, OBELISK_RT_BC_AND, OBELISK_RT_BC_TYPE_U64, 5, 4, 1);
  appendInstruction(code, OBELISK_RT_BC_OR, OBELISK_RT_BC_TYPE_U64, 6, 5, 2);
  appendInstruction(code, OBELISK_RT_BC_XOR, OBELISK_RT_BC_TYPE_U64, 7, 6, 2);
  appendInstruction(code, OBELISK_RT_BC_NOT, OBELISK_RT_BC_TYPE_U64, 8, 7);
  appendInstruction(code, OBELISK_RT_BC_EQ, OBELISK_RT_BC_TYPE_U64, 9, 7, 3);
  appendInstruction(code, OBELISK_RT_BC_ULT, OBELISK_RT_BC_TYPE_U64, 10, 2, 0);
  appendInstruction(code, OBELISK_RT_BC_CONST, OBELISK_RT_BC_TYPE_I64, 11, 0, 0,
                    UINT64_MAX);
  appendInstruction(code, OBELISK_RT_BC_CONST, OBELISK_RT_BC_TYPE_I64, 12, 0, 0,
                    0);
  appendInstruction(code, OBELISK_RT_BC_SLT, OBELISK_RT_BC_TYPE_I64, 13, 11,
                    12);
  appendInstruction(code, OBELISK_RT_BC_CONST, OBELISK_RT_BC_TYPE_BOOL, 14, 0,
                    0, 0);
  appendInstruction(code, OBELISK_RT_BC_BRANCH_ZERO, OBELISK_RT_BC_TYPE_BOOL, 0,
                    14, 0, 17);
  appendInstruction(code, OBELISK_RT_BC_TERMINATE);
  appendInstruction(code, OBELISK_RT_BC_JUMP, OBELISK_RT_BC_TYPE_NONE, 0, 0, 0,
                    19);
  appendInstruction(code, OBELISK_RT_BC_TERMINATE);
  appendInstruction(code, OBELISK_RT_BC_NOP);
  appendInstruction(code, OBELISK_RT_BC_STORE_FRAME, OBELISK_RT_BC_TYPE_U64, 0,
                    3, 0, 0);
  appendInstruction(code, OBELISK_RT_BC_STORE_FRAME, OBELISK_RT_BC_TYPE_U64, 0,
                    8, 0, 8);
  appendInstruction(code, OBELISK_RT_BC_STORE_FRAME, OBELISK_RT_BC_TYPE_BOOL, 0,
                    9, 0, 16);
  appendInstruction(code, OBELISK_RT_BC_STORE_FRAME, OBELISK_RT_BC_TYPE_BOOL, 0,
                    10, 0, 24);
  appendInstruction(code, OBELISK_RT_BC_STORE_FRAME, OBELISK_RT_BC_TYPE_BOOL, 0,
                    13, 0, 32);
  appendInstruction(code, OBELISK_RT_BC_TERMINATE, OBELISK_RT_BC_TYPE_NONE, 0,
                    0, 0, 99);
  auto descriptor = bytecodeDescriptor(code, 15);
  std::array<uint64_t, 5> frame{};
  obelisk_rt_fragment_action_v1 action{};

  ASSERT_EQ(
      executeBytecode(descriptor, frame.data(), sizeof(frame), 0, &action),
      OBELISK_RT_OK);
  EXPECT_EQ(frame[0], 4u);
  EXPECT_EQ(frame[1], ~uint64_t{4});
  EXPECT_EQ(frame[2], 1u);
  EXPECT_EQ(frame[3], 1u);
  EXPECT_EQ(frame[4], 1u);
  EXPECT_EQ(action.kind, OBELISK_RT_FRAGMENT_TERMINATE);
  EXPECT_EQ(action.payload, 99u);
}

TEST(RuntimeFragmentTest, BoundsRunawayBytecodeOnlyWhenAskedTo) {
  // A backward jump to itself is well-formed bytecode, so validation cannot
  // reject it. Only an explicit budget turns it into a reported failure.
  std::vector<uint8_t> code;
  appendInstruction(code, OBELISK_RT_BC_JUMP, OBELISK_RT_BC_TYPE_NONE, 0, 0, 0,
                    0);
  auto descriptor = bytecodeDescriptor(code, 0);
  obelisk_rt_fragment_action_v1 action{};
  EXPECT_EQ(executeBytecode(descriptor, nullptr, 0, 0, &action, 1000),
            OBELISK_RT_STEP_LIMIT);

  // A terminating fragment is unaffected by a budget it never reaches.
  std::vector<uint8_t> terminating;
  appendInstruction(terminating, OBELISK_RT_BC_TERMINATE);
  auto bounded = bytecodeDescriptor(terminating, 0);
  EXPECT_EQ(executeBytecode(bounded, nullptr, 0, 0, &action, 1), OBELISK_RT_OK);
  EXPECT_EQ(action.kind, OBELISK_RT_FRAGMENT_TERMINATE);
}

TEST(RuntimeFragmentTest, ContinuationSelectsBytecodeEntryInstruction) {
  std::vector<uint8_t> code;
  appendInstruction(code, OBELISK_RT_BC_TERMINATE, OBELISK_RT_BC_TYPE_NONE, 0,
                    0, 0, 1);
  appendInstruction(code, OBELISK_RT_BC_CONTINUE, OBELISK_RT_BC_TYPE_NONE, 0, 0,
                    0, 17);
  auto descriptor = bytecodeDescriptor(code, 0);
  constexpr obelisk_rt_bytecode_entry_v1 entries[] = {{3, 0}, {11, 1}};
  descriptor.code.bytecode.entries = entries;
  descriptor.code.bytecode.entry_count = std::size(entries);
  obelisk_rt_bytecode_validation_v1 validation{};
  descriptor.code.bytecode.validation = &validation;
  obelisk_rt_fragment_action_v1 action{};
  ASSERT_EQ(executeBytecode(descriptor, nullptr, 0, 11, &action),
            OBELISK_RT_OK);
  EXPECT_EQ(action.kind, OBELISK_RT_FRAGMENT_CONTINUE);
  EXPECT_EQ(action.continuation, 17u);
  EXPECT_NE(validation.state, 0u);
}

TEST(RuntimeFragmentTest, RejectsMalformedBytecodeAndFrameAccess) {
  auto rejects = [](const std::vector<uint8_t> &code, uint32_t registers) {
    auto descriptor = bytecodeDescriptor(code, registers);
    obelisk_rt_fragment_action_v1 action{};
    return executeBytecode(descriptor, nullptr, 0, 0, &action);
  };
  std::vector<uint8_t> empty;
  EXPECT_EQ(rejects(empty, 0), OBELISK_RT_INVALID_BYTECODE);
  std::vector<uint8_t> truncated(3, 0);
  auto malformed = bytecodeDescriptor(truncated, 1);
  obelisk_rt_fragment_action_v1 action{};
  EXPECT_EQ(executeBytecode(malformed, nullptr, 0, 0, &action),
            OBELISK_RT_INVALID_BYTECODE);

  std::vector<uint8_t> code;
  appendInstruction(code, OBELISK_RT_BC_LOAD_FRAME, OBELISK_RT_BC_TYPE_U64, 0,
                    0, 0, 8);
  appendInstruction(code, OBELISK_RT_BC_TERMINATE);
  auto badFrame = bytecodeDescriptor(code, 1);
  uint64_t frame = 0;
  EXPECT_EQ(executeBytecode(badFrame, &frame, sizeof(frame), 0, &action),
            OBELISK_RT_INVALID_BYTECODE);

  std::vector<uint8_t> invalidBool;
  appendInstruction(invalidBool, OBELISK_RT_BC_CONST, OBELISK_RT_BC_TYPE_BOOL,
                    0, 0, 0, 2);
  appendInstruction(invalidBool, OBELISK_RT_BC_TERMINATE);
  auto badBool = bytecodeDescriptor(invalidBool, 1);
  EXPECT_EQ(executeBytecode(badBool, nullptr, 0, 0, &action),
            OBELISK_RT_INVALID_BYTECODE);
  EXPECT_STREQ(obelisk_rt_v1_status_string(OBELISK_RT_INVALID_BYTECODE),
               "invalid bytecode");

  std::vector<uint8_t> noneStore;
  appendInstruction(noneStore, OBELISK_RT_BC_STORE_FRAME,
                    OBELISK_RT_BC_TYPE_NONE, 0, 0);
  auto badStore = bytecodeDescriptor(noneStore, 1);
  EXPECT_EQ(executeBytecode(badStore, &frame, sizeof(frame), 0, &action),
            OBELISK_RT_INVALID_BYTECODE);

  std::vector<uint8_t> noneBranch;
  appendInstruction(noneBranch, OBELISK_RT_BC_BRANCH_ZERO,
                    OBELISK_RT_BC_TYPE_NONE, 0, 0, 0, 0);
  auto badBranch = bytecodeDescriptor(noneBranch, 1);
  EXPECT_EQ(executeBytecode(badBranch, nullptr, 0, 0, &action),
            OBELISK_RT_INVALID_BYTECODE);

  std::vector<uint8_t> invalidOpcode;
  appendInstruction(invalidOpcode, 0xff);
  EXPECT_EQ(rejects(invalidOpcode, 0), OBELISK_RT_INVALID_BYTECODE);
  std::vector<uint8_t> invalidType;
  appendInstruction(invalidType, OBELISK_RT_BC_CONST, 0xff, 0);
  EXPECT_EQ(rejects(invalidType, 1), OBELISK_RT_INVALID_BYTECODE);
  std::vector<uint8_t> uninitializedMove;
  appendInstruction(uninitializedMove, OBELISK_RT_BC_MOVE,
                    OBELISK_RT_BC_TYPE_U64, 0, 0);
  EXPECT_EQ(rejects(uninitializedMove, 1), OBELISK_RT_INVALID_BYTECODE);
  std::vector<uint8_t> invalidJump;
  appendInstruction(invalidJump, OBELISK_RT_BC_JUMP, OBELISK_RT_BC_TYPE_NONE, 0,
                    0, 0, 1);
  EXPECT_EQ(rejects(invalidJump, 0), OBELISK_RT_INVALID_BYTECODE);
  std::vector<uint8_t> unterminated;
  appendInstruction(unterminated, OBELISK_RT_BC_NOP);
  EXPECT_EQ(rejects(unterminated, 0), OBELISK_RT_INVALID_BYTECODE);

  std::vector<uint8_t> terminate;
  appendInstruction(terminate, OBELISK_RT_BC_TERMINATE);
  auto badEntries = bytecodeDescriptor(terminate, 0);
  constexpr obelisk_rt_bytecode_entry_v1 duplicateEntries[] = {{0, 0}, {0, 0}};
  badEntries.code.bytecode.entries = duplicateEntries;
  badEntries.code.bytecode.entry_count = std::size(duplicateEntries);
  EXPECT_EQ(executeBytecode(badEntries, nullptr, 0, 0, &action),
            OBELISK_RT_INVALID_BYTECODE);
  constexpr obelisk_rt_bytecode_entry_v1 outOfRangeEntry{0, 1};
  badEntries.code.bytecode.entries = &outOfRangeEntry;
  badEntries.code.bytecode.entry_count = 1;
  EXPECT_EQ(executeBytecode(badEntries, nullptr, 0, 0, &action),
            OBELISK_RT_INVALID_BYTECODE);
  auto missingEntry = bytecodeDescriptor(terminate, 0);
  EXPECT_EQ(executeBytecode(missingEntry, nullptr, 0, 7, &action),
            OBELISK_RT_INVALID_BYTECODE);

  std::vector<uint8_t> fourInstructions;
  for (unsigned index = 0; index != 4; ++index)
    appendInstruction(fourInstructions, OBELISK_RT_BC_TERMINATE);
  auto unsorted = bytecodeDescriptor(fourInstructions, 0);
  constexpr obelisk_rt_bytecode_entry_v1 unsortedEntries[] = {
      {0, 0}, {100, 1}, {50, 2}, {200, 3}};
  unsorted.code.bytecode.entries = unsortedEntries;
  unsorted.code.bytecode.entry_count = std::size(unsortedEntries);
  EXPECT_EQ(executeBytecode(unsorted, nullptr, 0, 0, &action),
            OBELISK_RT_INVALID_BYTECODE);

  auto remoteBadInstruction = bytecodeDescriptor(terminate, 0);
  constexpr obelisk_rt_bytecode_entry_v1 remoteBadEntries[] = {{0, 0}, {1, 1}};
  remoteBadInstruction.code.bytecode.entries = remoteBadEntries;
  remoteBadInstruction.code.bytecode.entry_count = std::size(remoteBadEntries);
  EXPECT_EQ(executeBytecode(remoteBadInstruction, nullptr, 0, 0, &action),
            OBELISK_RT_INVALID_BYTECODE);
}

obelisk_rt_status nativeFragment(obelisk_rt_context *, void *frame,
                                 uint64_t frameSize, uint32_t continuation,
                                 obelisk_rt_fragment_action_v1 *action) {
  if (!frame || frameSize != sizeof(uint64_t))
    return OBELISK_RT_INVALID_ARGUMENT;
  ++*static_cast<uint64_t *>(frame);
  *action = {OBELISK_RT_FRAGMENT_CONTINUE,
             OBELISK_RT_SUSPEND_NONE,
             continuation + 1,
             0,
             0,
             0};
  return OBELISK_RT_OK;
}

obelisk_rt_status invalidNativeAction(obelisk_rt_context *, void *, uint64_t,
                                      uint32_t,
                                      obelisk_rt_fragment_action_v1 *action) {
  *action = {OBELISK_RT_FRAGMENT_CONTINUE, OBELISK_RT_SUSPEND_NONE, 1, 0, 1, 0};
  return OBELISK_RT_OK;
}

obelisk_rt_status throwingNativeFragment(obelisk_rt_context *, void *, uint64_t,
                                         uint32_t,
                                         obelisk_rt_fragment_action_v1 *) {
  throw std::bad_alloc();
}

TEST(RuntimeFragmentTest, ValidatesNativeDescriptorsAndActions) {
  obelisk_rt_fragment_descriptor_v1 descriptor{};
  descriptor.handle = {OBELISK_RT_DESCRIPTOR_FRAGMENT, 0, 3};
  descriptor.code_kind = OBELISK_RT_FRAGMENT_NATIVE;
  descriptor.code.native_entry = invalidNativeAction;
  obelisk_rt_fragment_action_v1 action{};
  EXPECT_EQ(obelisk_rt_v1_fragment_execute(&descriptor, nullptr, nullptr, 0, 0,
                                           &action),
            OBELISK_RT_INVALID_ARGUMENT);

  descriptor.code.native_entry = throwingNativeFragment;
  EXPECT_EQ(obelisk_rt_v1_fragment_execute(&descriptor, nullptr, nullptr, 0, 0,
                                           &action),
            OBELISK_RT_OUT_OF_MEMORY);

  descriptor.flags = 1;
  EXPECT_EQ(obelisk_rt_v1_fragment_execute(&descriptor, nullptr, nullptr, 0, 0,
                                           &action),
            OBELISK_RT_INVALID_ARGUMENT);
  descriptor.flags = 0;
  descriptor.code.native_entry = nullptr;
  EXPECT_EQ(obelisk_rt_v1_fragment_execute(&descriptor, nullptr, nullptr, 0, 0,
                                           &action),
            OBELISK_RT_INVALID_ARGUMENT);
  descriptor.handle.kind = OBELISK_RT_DESCRIPTOR_PROCESS;
  EXPECT_EQ(obelisk_rt_v1_fragment_execute(&descriptor, nullptr, nullptr, 0, 0,
                                           &action),
            OBELISK_RT_INVALID_ARGUMENT);
}

TEST(RuntimeFragmentTest, NativeAndBytecodeUseOneDispatchContract) {
  obelisk_rt_fragment_descriptor_v1 native{};
  native.handle = {OBELISK_RT_DESCRIPTOR_FRAGMENT, 0, 3};
  native.code_kind = OBELISK_RT_FRAGMENT_NATIVE;
  native.code.native_entry = nativeFragment;
  uint64_t nativeFrame = 4;
  obelisk_rt_fragment_action_v1 nativeAction{};
  EXPECT_EQ(obelisk_rt_v1_fragment_execute(&native, nullptr, &nativeFrame,
                                           sizeof(nativeFrame), 11,
                                           &nativeAction),
            OBELISK_RT_OK);
  EXPECT_EQ(nativeFrame, 5u);

  std::vector<uint8_t> code;
  appendInstruction(code, OBELISK_RT_BC_LOAD_FRAME, OBELISK_RT_BC_TYPE_U64, 0,
                    0, 0, 0);
  appendInstruction(code, OBELISK_RT_BC_CONST, OBELISK_RT_BC_TYPE_U64, 1, 0, 0,
                    1);
  appendInstruction(code, OBELISK_RT_BC_ADD, OBELISK_RT_BC_TYPE_U64, 2, 0, 1);
  appendInstruction(code, OBELISK_RT_BC_STORE_FRAME, OBELISK_RT_BC_TYPE_U64, 0,
                    2, 0, 0);
  appendInstruction(code, OBELISK_RT_BC_CONTINUE, OBELISK_RT_BC_TYPE_NONE, 0, 0,
                    0, 12);
  auto bytecode = bytecodeDescriptor(code, 3);
  constexpr obelisk_rt_bytecode_entry_v1 entry{11, 0};
  bytecode.code.bytecode.entries = &entry;
  bytecode.code.bytecode.entry_count = 1;
  uint64_t bytecodeFrame = 4;
  obelisk_rt_fragment_action_v1 bytecodeAction{};
  EXPECT_EQ(executeBytecode(bytecode, &bytecodeFrame, sizeof(bytecodeFrame), 11,
                            &bytecodeAction),
            OBELISK_RT_OK);
  EXPECT_EQ(bytecodeFrame, nativeFrame);
  EXPECT_EQ(bytecodeAction.kind, nativeAction.kind);
  EXPECT_EQ(bytecodeAction.suspend_kind, nativeAction.suspend_kind);
  EXPECT_EQ(bytecodeAction.continuation, nativeAction.continuation);
  EXPECT_EQ(bytecodeAction.flags, nativeAction.flags);
  EXPECT_EQ(bytecodeAction.payload, nativeAction.payload);
  EXPECT_EQ(bytecodeAction.auxiliary, nativeAction.auxiliary);
}

} // namespace
