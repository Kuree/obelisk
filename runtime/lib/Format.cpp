//===- Format.cpp - Obelisk runtime formatting and display ----------------===//

#include "RuntimeInternal.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

// Snapshot of the context's $timeformat override, taken under the context lock
// so a concurrent $timeformat cannot tear the suffix out from under %t.
struct TimeOverride {
  bool active = false;
  // Decimal exponent, in seconds, of the unit %t reports in.
  int32_t units = 0;
  uint32_t fractionDigits = 0;
  uint32_t width = 20;
  std::string suffix;
};

struct FormatOptions {
  std::optional<uint32_t> width;
  std::optional<uint32_t> precision;
  bool left = false;
  bool zero = false;
};

struct LogicView {
  uint64_t width = 0;
  bool isSigned = false;
  const uint64_t *value = nullptr;
  const uint64_t *unknown = nullptr;
};

uint64_t wordCount(uint64_t width) { return (width + 63) / 64; }

uint64_t lowMask(unsigned bits) {
  if (bits == 0 || bits == 64)
    return std::numeric_limits<uint64_t>::max();
  return (uint64_t{1} << bits) - 1;
}

uint64_t finalWordMask(uint64_t width) {
  return lowMask(static_cast<unsigned>(width % 64));
}

bool getLogicView(const obelisk_rt_arg_v1 &argument, LogicView &view) {
  if (argument.kind == OBELISK_RT_ARG_LOGIC) {
    if (argument.size == 0 ||
        argument.size > std::numeric_limits<uint32_t>::max() || !argument.data)
      return false;
    view = {argument.size, (argument.flags & OBELISK_RT_ARG_SIGNED) != 0,
            static_cast<const uint64_t *>(argument.data), argument.unknown};
    return true;
  }
  if (argument.kind == OBELISK_RT_ARG_TIME) {
    if (!argument.data)
      return false;
    view = {64, false, static_cast<const uint64_t *>(argument.data), nullptr};
    return true;
  }
  return false;
}

bool getStringBytes(const obelisk_rt_arg_v1 &argument, char scratch[8],
                    const char *&data, uint64_t &size) {
  data = "";
  size = 0;
  if (argument.kind == OBELISK_RT_ARG_STRING) {
    if (!validBytes(argument.data, argument.size))
      return false;
    data = argument.data ? static_cast<const char *>(argument.data) : "";
    size = argument.size;
    return true;
  }
  if (argument.kind != OBELISK_RT_ARG_MANAGED_STRING || argument.size != 0 ||
      !argument.data)
    return false;
  obelisk_rt_string_v1 string = 0;
  std::memcpy(&string, argument.data, sizeof(string));
  return obelisk_rt_v1_string_view(string, scratch, &data, &size) ==
         OBELISK_RT_OK;
}

bool getStringLogicView(const obelisk_rt_arg_v1 &argument,
                        std::vector<uint64_t> &storage, LogicView &view) {
  char scratch[8];
  const char *bytes = nullptr;
  uint64_t size = 0;
  if (!getStringBytes(argument, scratch, bytes, size) || size == 0 ||
      size > std::numeric_limits<uint32_t>::max() / 8)
    return false;
  uint64_t width = size * 8;
  storage.assign(static_cast<size_t>(wordCount(width)), 0);
  // A SystemVerilog string's first byte is its most-significant byte when it
  // participates in an integral format conversion.
  for (uint64_t index = 0; index != size; ++index) {
    uint64_t bit = (size - index - 1) * 8;
    storage[bit / 64] |=
        uint64_t{static_cast<unsigned char>(bytes[index])} << (bit % 64);
  }
  view = {width, false, storage.data(), nullptr};
  return true;
}

bool valueBit(const LogicView &view, uint64_t bit) {
  return ((view.value[bit / 64] >> (bit % 64)) & 1) != 0;
}

bool unknownBit(const LogicView &view, uint64_t bit) {
  return view.unknown && ((view.unknown[bit / 64] >> (bit % 64)) & 1) != 0;
}

char logicSymbol(const LogicView &view, uint64_t bit) {
  if (!unknownBit(view, bit))
    return valueBit(view, bit) ? '1' : '0';
  return valueBit(view, bit) ? 'z' : 'x';
}

void applyPadding(std::string &output, std::string field, uint32_t width,
                  bool left, char padding) {
  if (field.size() >= width) {
    output += field;
    return;
  }
  // Append the pad and field directly into the output rather than building a
  // separate pad string and a `pad + field` temporary. For pathological field
  // widths this keeps peak memory to a single copy of the padded field.
  size_t padCount = width - field.size();
  if (left) {
    output += field;
    output.append(padCount, padding);
  } else {
    output.append(padCount, padding);
    output += field;
  }
}

char groupDigit(const LogicView &view, uint64_t lowBit, unsigned groupBits) {
  static constexpr char digits[] = "0123456789abcdef";
  unsigned validBits =
      static_cast<unsigned>(std::min<uint64_t>(groupBits, view.width - lowBit));
  unsigned value = 0;
  unsigned unknown = 0;
  for (unsigned index = 0; index < validBits; ++index) {
    uint64_t bit = lowBit + index;
    if (valueBit(view, bit))
      value |= 1u << index;
    if (unknownBit(view, bit))
      unknown |= 1u << index;
  }
  if (!unknown)
    return digits[value];
  unsigned mask = (1u << validBits) - 1;
  if (unknown == mask && value == 0)
    return 'x';
  if (unknown == mask && value == mask)
    return 'z';
  if ((unknown & ~value) != 0)
    return 'X';
  return 'Z';
}

std::string baseDigits(const LogicView &view, unsigned groupBits) {
  uint64_t groups = (view.width + groupBits - 1) / groupBits;
  std::string result;
  if (groups <= std::numeric_limits<size_t>::max())
    result.reserve(static_cast<size_t>(groups));
  for (uint64_t group = groups; group > 0; --group)
    result.push_back(groupDigit(view, (group - 1) * groupBits, groupBits));

  size_t leading = 0;
  while (leading + 1 < result.size() && result[leading] == '0')
    ++leading;
  bool stripped = leading != 0;
  result.erase(0, leading);
  if (stripped && !result.empty() &&
      (result.front() == 'x' || result.front() == 'z'))
    result.insert(result.begin(), '0');
  return result;
}

std::vector<uint64_t> magnitudeWords(const LogicView &view, bool &negative) {
  uint64_t words = wordCount(view.width);
  std::vector<uint64_t> result(view.value, view.value + words);
  result.back() &= finalWordMask(view.width);
  negative = view.isSigned && valueBit(view, view.width - 1);
  if (!negative)
    return result;

  for (uint64_t &word : result)
    word = ~word;
  result.back() &= finalWordMask(view.width);
  uint64_t carry = 1;
  for (uint64_t &word : result) {
    uint64_t old = word;
    word += carry;
    carry = carry && word < old;
    if (!carry)
      break;
  }
  result.back() &= finalWordMask(view.width);
  return result;
}

bool wordsAreZero(const std::vector<uint64_t> &words) {
  return std::all_of(words.begin(), words.end(),
                     [](uint64_t word) { return word == 0; });
}

unsigned divideWordsBy10(std::vector<uint64_t> &words) {
  unsigned remainder = 0;
  for (size_t index = words.size(); index > 0; --index) {
#if defined(__SIZEOF_INT128__)
    __uint128_t dividend =
        (static_cast<__uint128_t>(remainder) << 64) | words[index - 1];
    words[index - 1] = static_cast<uint64_t>(dividend / 10);
    remainder = static_cast<unsigned>(dividend % 10);
#else
    uint64_t quotient = 0;
    for (int bit = 63; bit >= 0; --bit) {
      remainder = remainder * 2 + ((words[index - 1] >> bit) & 1);
      if (remainder >= 10) {
        remainder -= 10;
        quotient |= uint64_t{1} << bit;
      }
    }
    words[index - 1] = quotient;
#endif
  }
  return remainder;
}

uint64_t multiplyHigh(uint64_t left, uint64_t right) {
#if defined(__SIZEOF_INT128__)
  return static_cast<uint64_t>((static_cast<__uint128_t>(left) * right) >> 64);
#else
  uint64_t leftLow = static_cast<uint32_t>(left);
  uint64_t leftHigh = left >> 32;
  uint64_t rightLow = static_cast<uint32_t>(right);
  uint64_t rightHigh = right >> 32;
  uint64_t lowProduct = leftLow * rightLow;
  uint64_t crossLow = leftLow * rightHigh;
  uint64_t crossHigh = leftHigh * rightLow;
  uint64_t middle = (lowProduct >> 32) + static_cast<uint32_t>(crossLow) +
                    static_cast<uint32_t>(crossHigh);
  return leftHigh * rightHigh + (crossLow >> 32) + (crossHigh >> 32) +
         (middle >> 32);
#endif
}

void multiplyWords(std::vector<uint64_t> &words, uint64_t multiplier) {
  uint64_t carry = 0;
  for (uint64_t &word : words) {
    uint64_t high = multiplyHigh(word, multiplier);
    uint64_t low = word * multiplier;
    uint64_t scaled = low + carry;
    high += scaled < low;
    word = scaled;
    carry = high;
  }
  if (carry)
    words.push_back(carry);
}

std::string knownDecimal(const LogicView &view, uint64_t multiplier = 1) {
  bool negative = false;
  std::vector<uint64_t> words = magnitudeWords(view, negative);
  multiplyWords(words, multiplier);
  if (wordsAreZero(words))
    return "0";
  std::string result;
  while (!wordsAreZero(words))
    result.push_back(static_cast<char>('0' + divideWordsBy10(words)));
  if (negative)
    result.push_back('-');
  std::reverse(result.begin(), result.end());
  return result;
}

std::optional<char> decimalUnknown(const LogicView &view) {
  if (!view.unknown)
    return std::nullopt;
  bool anyUnknown = false;
  bool anyX = false;
  bool allUnknown = true;
  bool allUnknownValuesZero = true;
  bool allUnknownValuesOne = true;
  for (uint64_t bit = 0; bit < view.width; ++bit) {
    bool unknown = unknownBit(view, bit);
    bool value = valueBit(view, bit);
    anyUnknown |= unknown;
    allUnknown &= unknown;
    if (unknown) {
      anyX |= !value;
      allUnknownValuesZero &= !value;
      allUnknownValuesOne &= value;
    } else {
      allUnknownValuesZero = false;
      allUnknownValuesOne = false;
    }
  }
  if (!anyUnknown)
    return std::nullopt;
  if (allUnknown && allUnknownValuesZero)
    return 'x';
  if (allUnknown && allUnknownValuesOne)
    return 'z';
  return anyX ? 'X' : 'Z';
}

uint32_t defaultDecimalWidth(const LogicView &view) {
  // The field holds the largest value the operand can represent. A signed
  // operand spends its most significant bit on the sign, so it contributes one
  // magnitude bit fewer and one character more.
  uint64_t magnitudeBits =
      view.isSigned && view.width > 0 ? view.width - 1 : view.width;
  double digits =
      std::ceil(static_cast<double>(magnitudeBits) * 0.30102999566398119521);
  uint64_t width = std::max<uint64_t>(static_cast<uint64_t>(digits), 1) +
                   (view.isSigned ? 1 : 0);
  return static_cast<uint32_t>(
      std::min<uint64_t>(width, std::numeric_limits<uint32_t>::max()));
}

obelisk_rt_status formatDecimal(std::string &output, const LogicView &view,
                                const FormatOptions &options,
                                uint64_t multiplier = 1) {
  std::string field;
  if (std::optional<char> unknown = decimalUnknown(view))
    field.push_back(*unknown);
  else
    field = knownDecimal(view, multiplier);
  uint32_t width = options.width.value_or(defaultDecimalWidth(view));
  applyPadding(output, std::move(field), width, options.left, ' ');
  return OBELISK_RT_OK;
}

obelisk_rt_status formatInteger(std::string &output, const LogicView &view,
                                char specifier, const FormatOptions &options) {
  char spec =
      static_cast<char>(std::tolower(static_cast<unsigned char>(specifier)));
  if (spec == 'd')
    return formatDecimal(output, view, options);

  unsigned groupBits = spec == 'b' ? 1 : spec == 'o' ? 3 : 4;
  std::string field = baseDigits(view, groupBits);
  uint64_t defaultWidth64 = (view.width + groupBits - 1) / groupBits;
  uint32_t width =
      options.width.value_or(static_cast<uint32_t>(std::min<uint64_t>(
          defaultWidth64, std::numeric_limits<uint32_t>::max())));
  applyPadding(output, std::move(field), width, options.left, '0');
  return OBELISK_RT_OK;
}

std::string logicToString(const LogicView &view, bool trimLeadingNulls) {
  std::string result;
  uint64_t bytes = (view.width + 7) / 8;
  result.reserve(static_cast<size_t>(
      std::min<uint64_t>(bytes, std::numeric_limits<size_t>::max())));
  unsigned leadingBits = static_cast<unsigned>(view.width % 8);
  bool seenNonNull = false;
  for (uint64_t byteIndex = bytes; byteIndex > 0; --byteIndex) {
    unsigned bits = byteIndex == bytes && leadingBits ? leadingBits : 8;
    uint64_t low = (byteIndex - 1) * 8;
    uint8_t character = 0;
    for (unsigned bit = 0; bit < bits; ++bit) {
      uint64_t sourceBit = low + bit;
      if (!unknownBit(view, sourceBit) && valueBit(view, sourceBit))
        character |= static_cast<uint8_t>(1u << bit);
    }
    if (trimLeadingNulls && !seenNonNull && character == 0)
      continue;
    seenNonNull |= character != 0;
    // IEEE string formatting preserves the remaining packed byte positions.
    // A null byte is rendered as a space instead of terminating the field.
    result.push_back(character == 0 ? ' ' : static_cast<char>(character));
  }
  return result;
}

obelisk_rt_status formatStringValue(std::string &output, std::string field,
                                    const FormatOptions &options) {
  uint32_t width = options.width.value_or(0);
  applyPadding(output, std::move(field), width, options.left, ' ');
  return OBELISK_RT_OK;
}

obelisk_rt_status formatFloat(std::string &output, double value, char specifier,
                              const FormatOptions &options) {
  std::string format = "%";
  if (options.left)
    format.push_back('-');
  if (options.zero)
    format.push_back('0');
  if (options.width)
    format += std::to_string(*options.width);
  if (options.precision) {
    format.push_back('.');
    format += std::to_string(*options.precision);
  }
  format.push_back(specifier);
  int required = std::snprintf(nullptr, 0, format.c_str(), value);
  if (required < 0)
    return OBELISK_RT_FORMAT_ERROR;
  size_t offset = output.size();
  output.resize(offset + static_cast<size_t>(required) + 1);
  int written =
      std::snprintf(output.data() + offset, static_cast<size_t>(required) + 1,
                    format.c_str(), value);
  if (written != required)
    return OBELISK_RT_FORMAT_ERROR;
  output.pop_back();
  return OBELISK_RT_OK;
}

double logicToDouble(const LogicView &view) {
  // IEEE 1800-2017 6.12.2: an x or z bit converts to zero, per bit rather
  // than poisoning the whole value.
  std::vector<uint64_t> known;
  LogicView defined = view;
  if (view.unknown) {
    uint64_t words = wordCount(view.width);
    known.assign(view.value, view.value + words);
    for (uint64_t index = 0; index < words; ++index)
      known[index] &= ~view.unknown[index];
    defined.value = known.data();
    defined.unknown = nullptr;
  }
  bool negative = false;
  std::vector<uint64_t> words = magnitudeWords(defined, negative);
  long double value = 0;
  for (size_t index = words.size(); index > 0; --index)
    value = std::ldexp(value, 64) + words[index - 1];
  return static_cast<double>(negative ? -value : value);
}

obelisk_rt_status appendRaw(std::string &output, const LogicView &view,
                            bool fourState) {
  uint64_t chunks = (view.width + 31) / 32;
  if (chunks > std::numeric_limits<size_t>::max() / (fourState ? 8 : 4))
    return OBELISK_RT_OUT_OF_MEMORY;
  for (uint64_t chunk = 0; chunk < chunks; ++chunk) {
    uint32_t value = 0;
    uint32_t unknown = 0;
    for (unsigned bit = 0; bit < 32; ++bit) {
      uint64_t sourceBit = chunk * 32 + bit;
      if (sourceBit >= view.width)
        break;
      if (valueBit(view, sourceBit))
        value |= uint32_t{1} << bit;
      if (unknownBit(view, sourceBit))
        unknown |= uint32_t{1} << bit;
    }
    uint32_t aval = fourState ? value ^ unknown : value & ~unknown;
    output.append(reinterpret_cast<const char *>(&aval), sizeof(aval));
    if (fourState)
      output.append(reinterpret_cast<const char *>(&unknown), sizeof(unknown));
  }
  return OBELISK_RT_OK;
}

std::string scalarPattern(const obelisk_rt_arg_v1 &argument) {
  LogicView view;
  if (getLogicView(argument, view)) {
    std::string result = std::to_string(view.width);
    result.push_back('\'');
    if (view.isSigned)
      result.push_back('s');
    result.push_back('b');
    for (uint64_t bit = view.width; bit > 0; --bit)
      result.push_back(logicSymbol(view, bit - 1));
    return result;
  }
  char scratch[8];
  const char *bytes = nullptr;
  uint64_t size = 0;
  if (getStringBytes(argument, scratch, bytes, size) &&
      size <= std::numeric_limits<size_t>::max())
    return std::string(bytes, static_cast<size_t>(size));
  if (argument.kind == OBELISK_RT_ARG_REAL && argument.data) {
    char buffer[64];
    int length = std::snprintf(buffer, sizeof(buffer), "%g",
                               *static_cast<const double *>(argument.data));
    if (length > 0 && static_cast<size_t>(length) < sizeof(buffer))
      return std::string(buffer, static_cast<size_t>(length));
  }
  return {};
}

TimeOverride snapshotTimeFormat(obelisk_rt_context *context) {
  TimeOverride result;
  std::lock_guard<std::recursive_mutex> lock(context->mutex);
  if (!context->timeFormat.active)
    return result;
  result.active = true;
  result.units = context->timeFormat.units;
  result.fractionDigits = context->timeFormat.fractionDigits;
  result.width = context->timeFormat.width;
  result.suffix = context->timeFormat.suffix;
  return result;
}

// Render a time in the units $timeformat selected. `ticks` counts design
// precision units, whose own magnitude comes from the format environment, so
// the two exponents give the factor between them.
obelisk_rt_status formatOverriddenTime(std::string &output, long double ticks,
                                       int32_t precisionExponent,
                                       const TimeOverride &timeFormat) {
  long double scaled =
      ticks * std::pow(10.0L, static_cast<long double>(
                                  precisionExponent - timeFormat.units));
  char buffer[512];
  int length =
      std::snprintf(buffer, sizeof(buffer), "%.*Lf",
                    static_cast<int>(timeFormat.fractionDigits), scaled);
  if (length < 0 || static_cast<size_t>(length) >= sizeof(buffer))
    return OBELISK_RT_ARGUMENT_MISMATCH;
  std::string rendered(buffer, static_cast<size_t>(length));
  rendered += timeFormat.suffix;
  // IEEE gives the minimum width for the whole field, suffix included.
  if (rendered.size() < timeFormat.width)
    output.append(timeFormat.width - rendered.size(), ' ');
  output += rendered;
  return OBELISK_RT_OK;
}

obelisk_rt_status formatArgument(std::string &output,
                                 const obelisk_rt_arg_v1 &argument,
                                 char specifier, const FormatOptions &options,
                                 const obelisk_rt_format_env_v1 *environment,
                                 const TimeOverride &timeFormat) {
  char spec =
      static_cast<char>(std::tolower(static_cast<unsigned char>(specifier)));
  LogicView view;
  std::vector<uint64_t> stringLogic;
  switch (spec) {
  case 'b':
  case 'o':
  case 'd':
  case 'h':
  case 'x':
    if (!getLogicView(argument, view) &&
        !getStringLogicView(argument, stringLogic, view)) {
      if (argument.kind != OBELISK_RT_ARG_REAL || !argument.data)
        return OBELISK_RT_ARGUMENT_MISMATCH;
      double real = *static_cast<const double *>(argument.data);
      // A rounded infinity or NaN has no integral rendering. Name it instead
      // of failing the format, which would discard the whole output.
      if (std::isnan(real)) {
        output += "nan";
        return OBELISK_RT_OK;
      }
      if (std::isinf(real)) {
        output += real < 0 ? "-inf" : "inf";
        return OBELISK_RT_OK;
      }
      long double rounded = std::round(static_cast<long double>(real));
      if (rounded <
              static_cast<long double>(std::numeric_limits<int64_t>::min()) ||
          rounded >
              static_cast<long double>(std::numeric_limits<int64_t>::max()))
        return OBELISK_RT_ARGUMENT_MISMATCH;
      uint64_t integer =
          static_cast<uint64_t>(static_cast<int64_t>(rounded));
      view = LogicView{64, true, &integer, nullptr};
      return formatInteger(output, view, spec, options);
    }
    return formatInteger(output, view, spec, options);
  case 'c': {
    if (!getLogicView(argument, view) &&
        !getStringLogicView(argument, stringLogic, view))
      return OBELISK_RT_ARGUMENT_MISMATCH;
    uint8_t character = 0;
    for (unsigned bit = 0; bit < 8 && bit < view.width; ++bit)
      if (!unknownBit(view, bit) && valueBit(view, bit))
        character |= static_cast<uint8_t>(1u << bit);
    output.push_back(static_cast<char>(character));
    return OBELISK_RT_OK;
  }
  case 's':
    if (argument.kind == OBELISK_RT_ARG_STRING ||
        argument.kind == OBELISK_RT_ARG_MANAGED_STRING) {
      char scratch[8];
      const char *bytes = nullptr;
      uint64_t size = 0;
      if (!getStringBytes(argument, scratch, bytes, size) ||
          size > std::numeric_limits<size_t>::max())
        return OBELISK_RT_INVALID_ARGUMENT;
      std::string_view source(bytes, static_cast<size_t>(size));
      if (options.zero) {
        size_t first = source.find_first_not_of('\0');
        source.remove_prefix(first == std::string_view::npos ? source.size()
                                                             : first);
      }
      std::string field(source);
      std::replace(field.begin(), field.end(), '\0', ' ');
      return formatStringValue(output, std::move(field), options);
    }
    if (!getLogicView(argument, view))
      return OBELISK_RT_ARGUMENT_MISMATCH;
    return formatStringValue(output, logicToString(view, options.zero), options);
  case 'e':
  case 'f':
  case 'g': {
    double value;
    if (argument.kind == OBELISK_RT_ARG_REAL && argument.data)
      value = *static_cast<const double *>(argument.data);
    else if (getLogicView(argument, view))
      value = logicToDouble(view);
    else
      return OBELISK_RT_ARGUMENT_MISMATCH;
    return formatFloat(output, value, specifier, options);
  }
  case 't': {
    FormatOptions timeOptions = options;
    if (!timeOptions.width)
      timeOptions.width =
          environment && environment->time_width ? environment->time_width : 20;
    uint64_t multiplier = environment && environment->time_multiplier
                              ? environment->time_multiplier
                              : 1;
    // An executed $timeformat replaces the width, units, and suffix the site
    // was compiled with. Values carrying unknown bits have no numeric
    // rendering, so those keep the default path's x-fill.
    if (timeFormat.active) {
      std::optional<long double> ticks;
      if (argument.kind == OBELISK_RT_ARG_REAL && argument.data) {
        double value = *static_cast<const double *>(argument.data);
        if (std::isfinite(value))
          ticks = static_cast<long double>(value) *
                  static_cast<long double>(multiplier);
      } else if (getLogicView(argument, view) && !decimalUnknown(view)) {
        ticks = static_cast<long double>(logicToDouble(view)) *
                static_cast<long double>(multiplier);
      }
      if (ticks)
        return formatOverriddenTime(
            output, *ticks, environment ? environment->time_precision : 0,
            timeFormat);
    }
    obelisk_rt_status status;
    if (argument.kind == OBELISK_RT_ARG_REAL && argument.data) {
      double value = *static_cast<const double *>(argument.data);
      long double scaled = static_cast<long double>(value) *
                           static_cast<long double>(multiplier);
      if (!std::isfinite(value) || scaled < 0 ||
          scaled >
              static_cast<long double>(std::numeric_limits<uint64_t>::max()))
        return OBELISK_RT_ARGUMENT_MISMATCH;
      uint64_t rounded = static_cast<uint64_t>(std::round(scaled));
      LogicView realTime{64, false, &rounded, nullptr};
      status = formatDecimal(output, realTime, timeOptions);
    } else {
      if (!getLogicView(argument, view))
        return OBELISK_RT_ARGUMENT_MISMATCH;
      status = formatDecimal(output, view, timeOptions, multiplier);
    }
    if (status != OBELISK_RT_OK)
      return status;
    if (environment &&
        validBytes(environment->time_suffix, environment->time_suffix_size) &&
        environment->time_suffix_size <= std::numeric_limits<size_t>::max())
      output.append(environment->time_suffix,
                    static_cast<size_t>(environment->time_suffix_size));
    else if (environment && environment->time_suffix_size != 0)
      return OBELISK_RT_INVALID_ARGUMENT;
    return OBELISK_RT_OK;
  }
  case 'u':
  case 'z':
    if (!getLogicView(argument, view))
      return OBELISK_RT_ARGUMENT_MISMATCH;
    return appendRaw(output, view, spec == 'z');
  case 'p': {
    if (argument.kind == OBELISK_RT_ARG_MANAGED_CONTAINER) {
      if (argument.size != 0 || !argument.data || argument.unknown)
        return OBELISK_RT_INVALID_ARGUMENT;
      obelisk_rt_object_v1 *container = nullptr;
      std::memcpy(&container, argument.data, sizeof(container));
      std::string value;
      obelisk_rt_status status =
          obelisk_rt_container_pattern(container, value, 0);
      if (status != OBELISK_RT_OK)
        return status;
      return formatStringValue(output, std::move(value), options);
    }
    if (argument.kind == OBELISK_RT_ARG_MANAGED_OBJECT) {
      if (argument.size != 0 || !argument.data || argument.unknown)
        return OBELISK_RT_INVALID_ARGUMENT;
      obelisk_rt_object_v1 *object = nullptr;
      std::memcpy(&object, argument.data, sizeof(object));
      if (!object)
        return formatStringValue(output, "null", options);
      if (obelisk_rt_managed_object_kind(object) != OBELISK_RT_MANAGED_CLASS)
        return OBELISK_RT_INVALID_HANDLE;
      uint64_t identity = obelisk_rt_v1_object_id(object);
      if (identity == 0)
        return OBELISK_RT_INVALID_HANDLE;
      // IEEE 1800-2017 leaves non-null handle rendering implementation
      // dependent. Use the stable object identity, not a host address, so
      // output remains deterministic across native and bytecode execution.
      std::string value = "class@" + std::to_string(identity);
      return formatStringValue(output, std::move(value), options);
    }
    if (argument.kind == OBELISK_RT_ARG_VIRTUAL_INTERFACE) {
      if (argument.size != 0 || !argument.data || argument.unknown)
        return OBELISK_RT_INVALID_ARGUMENT;
      uint64_t identity = 0;
      std::memcpy(&identity, argument.data, sizeof(identity));
      if (identity == 0)
        return formatStringValue(output, "null", options);
      // A virtual interface is represented by the stable identity of its
      // elaborated interface scope. Avoid host addresses so native and
      // bytecode execution produce the same deterministic rendering.
      std::string value = "virtual_interface@" + std::to_string(identity);
      return formatStringValue(output, std::move(value), options);
    }
    std::string value = scalarPattern(argument);
    if (value.empty() && argument.kind != OBELISK_RT_ARG_STRING &&
        argument.kind != OBELISK_RT_ARG_MANAGED_STRING)
      return OBELISK_RT_ARGUMENT_MISMATCH;
    return formatStringValue(output, std::move(value), options);
  }
  default:
    return OBELISK_RT_FORMAT_ERROR;
  }
}

bool parseUnsigned(std::string_view format, size_t &position,
                   uint32_t &result) {
  uint64_t value = 0;
  size_t start = position;
  while (position < format.size() && format[position] >= '0' &&
         format[position] <= '9') {
    value = value * 10 + static_cast<unsigned>(format[position] - '0');
    if (value > std::numeric_limits<uint32_t>::max())
      return false;
    ++position;
  }
  if (position == start)
    return false;
  result = static_cast<uint32_t>(value);
  return true;
}

obelisk_rt_status formatSequence(std::string &output, std::string_view format,
                                 const obelisk_rt_arg_v1 *arguments,
                                 uint64_t argumentCount,
                                 uint64_t &argumentIndex,
                                 const obelisk_rt_format_env_v1 *environment,
                                 const TimeOverride &timeFormat,
                                 std::string &error,
                                 std::vector<std::string> *warnings = nullptr) {
  for (size_t position = 0; position < format.size();) {
    if (format[position] != '%') {
      output.push_back(format[position++]);
      continue;
    }
    size_t formatOffset = position++;
    if (position < format.size() && format[position] == '%') {
      output.push_back('%');
      ++position;
      continue;
    }

    FormatOptions options;
    while (position < format.size()) {
      if (format[position] == '-' && !options.left) {
        options.left = true;
        ++position;
      } else if (format[position] == '0' && !options.zero) {
        options.zero = true;
        ++position;
      } else {
        break;
      }
    }

    if (position < format.size() && format[position] >= '0' &&
        format[position] <= '9') {
      uint32_t width;
      if (!parseUnsigned(format, position, width)) {
        error = "format width is too large";
        return OBELISK_RT_FORMAT_ERROR;
      }
      options.width = width;
    }
    if (position < format.size() && format[position] == '.') {
      ++position;
      uint32_t precision = 0;
      if (position < format.size() && format[position] >= '0' &&
          format[position] <= '9') {
        if (!parseUnsigned(format, position, precision)) {
          error = "format precision is too large";
          return OBELISK_RT_FORMAT_ERROR;
        }
      }
      options.precision = precision;
    }
    if (position == format.size()) {
      error =
          "missing format specifier at byte " + std::to_string(formatOffset);
      return OBELISK_RT_FORMAT_ERROR;
    }

    char specifier = format[position++];
    char spec =
        static_cast<char>(std::tolower(static_cast<unsigned char>(specifier)));
    bool integer =
        spec == 'b' || spec == 'o' || spec == 'd' || spec == 'h' || spec == 'x';
    bool floating = spec == 'e' || spec == 'f' || spec == 'g';
    bool widthAllowed = integer || floating || spec == 's' || spec == 't';
    bool nonConsuming = spec == 'm' || spec == 'l';
    bool recognized = widthAllowed || nonConsuming || spec == 'c' ||
                      spec == 'u' || spec == 'z' || spec == 'p';
    if (!recognized) {
      error =
          "unknown format specifier at byte " + std::to_string(formatOffset);
      return OBELISK_RT_FORMAT_ERROR;
    }
    if ((options.width || options.left) && !widthAllowed && spec != 'p') {
      error = "field width is not allowed for this format specifier";
      return OBELISK_RT_FORMAT_ERROR;
    }
    if (options.precision && !floating) {
      error = "precision is only allowed for floating-point formats";
      return OBELISK_RT_FORMAT_ERROR;
    }
    if ((integer || spec == 't') && options.zero) {
      if (!options.width)
        options.width = 0;
      options.zero = false;
    }

    if (nonConsuming) {
      const char *data = nullptr;
      uint64_t size = 0;
      if (environment) {
        if (spec == 'm') {
          data = environment->scope;
          size = environment->scope_size;
        } else {
          data = environment->library_cell;
          size = environment->library_cell_size;
        }
      }
      if (!validBytes(data, size) ||
          size > std::numeric_limits<size_t>::max()) {
        error = "invalid formatting environment";
        return OBELISK_RT_INVALID_ARGUMENT;
      }
      if (data)
        output.append(data, static_cast<size_t>(size));
      continue;
    }

    if (argumentIndex >= argumentCount) {
      if (!warnings) {
        error = "not enough arguments for format string";
        return OBELISK_RT_ARGUMENT_MISMATCH;
      }
      warnings->push_back("not enough arguments for format string");
      output.push_back('<');
      output.append(format.substr(formatOffset, position - formatOffset));
      output.push_back('>');
      continue;
    }
    obelisk_rt_status status =
        formatArgument(output, arguments[argumentIndex++], specifier, options,
                       environment, timeFormat);
    if (status != OBELISK_RT_OK) {
      error = status == OBELISK_RT_ARGUMENT_MISMATCH
                  ? "argument type does not match format specifier"
                  : "failed to format argument";
      return status;
    }
  }
  return OBELISK_RT_OK;
}

char defaultSpecifier(const obelisk_rt_arg_v1 &argument,
                      obelisk_rt_radix radix) {
  switch (argument.kind) {
  case OBELISK_RT_ARG_LOGIC:
  case OBELISK_RT_ARG_TIME:
    return radix == OBELISK_RT_RADIX_BINARY  ? 'b'
           : radix == OBELISK_RT_RADIX_OCTAL ? 'o'
           : radix == OBELISK_RT_RADIX_HEX   ? 'h'
                                             : 'd';
  case OBELISK_RT_ARG_STRING:
  case OBELISK_RT_ARG_MANAGED_STRING:
    return 's';
  case OBELISK_RT_ARG_REAL:
    return 'f';
  case OBELISK_RT_ARG_MANAGED_CONTAINER:
    return 'p';
  case OBELISK_RT_ARG_MANAGED_OBJECT:
  case OBELISK_RT_ARG_VIRTUAL_INTERFACE:
    return 'p';
  default:
    return 0;
  }
}

obelisk_rt_status buildDisplay(std::string &output, obelisk_rt_radix radix,
                               const obelisk_rt_arg_v1 *items,
                               uint64_t itemCount,
                               const obelisk_rt_format_env_v1 *environment,
                               const TimeOverride &timeFormat,
                               std::string &error,
                               std::vector<std::string> &warnings) {
  if (radix != OBELISK_RT_RADIX_BINARY && radix != OBELISK_RT_RADIX_OCTAL &&
      radix != OBELISK_RT_RADIX_DECIMAL && radix != OBELISK_RT_RADIX_HEX) {
    error = "invalid default display radix";
    return OBELISK_RT_INVALID_ARGUMENT;
  }
  uint64_t index = 0;
  while (index < itemCount) {
    const obelisk_rt_arg_v1 &item = items[index++];
    constexpr uint32_t validFlags = OBELISK_RT_ARG_SIGNED |
                                    OBELISK_RT_ARG_FORMAT_STRING |
                                    OBELISK_RT_ARG_DESIGNATED_FORMAT;
    if ((item.flags & ~validFlags) != 0 ||
        ((item.flags & OBELISK_RT_ARG_DESIGNATED_FORMAT) != 0 &&
         ((item.flags & OBELISK_RT_ARG_FORMAT_STRING) == 0 || index != 1))) {
      error = "invalid display item flags";
      return OBELISK_RT_INVALID_ARGUMENT;
    }
    if (item.kind == OBELISK_RT_ARG_EMPTY) {
      output.push_back(' ');
      continue;
    }
    if ((item.flags & OBELISK_RT_ARG_FORMAT_STRING) != 0) {
      char scratch[8];
      const char *bytes = nullptr;
      uint64_t size = 0;
      if ((item.kind != OBELISK_RT_ARG_STRING &&
           item.kind != OBELISK_RT_ARG_MANAGED_STRING) ||
          !getStringBytes(item, scratch, bytes, size) ||
          size > std::numeric_limits<size_t>::max()) {
        error = "format-string display item is not a valid string";
        return OBELISK_RT_INVALID_ARGUMENT;
      }
      obelisk_rt_status status = formatSequence(
          output, std::string_view(bytes, static_cast<size_t>(size)), items,
          itemCount, index, environment, timeFormat, error, &warnings);
      if (status != OBELISK_RT_OK)
        return status;
      if ((item.flags & OBELISK_RT_ARG_DESIGNATED_FORMAT) != 0) {
        if (index != itemCount)
          warnings.push_back(std::to_string(itemCount - index) +
                             " extra argument(s) for format string");
        index = itemCount;
      }
      continue;
    }
    char specifier = defaultSpecifier(item, radix);
    if (!specifier) {
      error = "display item has no default scalar format";
      return OBELISK_RT_ARGUMENT_MISMATCH;
    }
    obelisk_rt_status status =
        formatArgument(output, item, specifier, {}, environment, timeFormat);
    if (status != OBELISK_RT_OK) {
      error = "failed to format display item";
      return status;
    }
  }
  return OBELISK_RT_OK;
}

void reportFormatWarnings(obelisk_rt_context *context,
                          const std::vector<std::string> &warnings) {
  if (warnings.empty())
    return;
  std::lock_guard<std::recursive_mutex> lock(context->mutex);
  for (const std::string &warning : warnings)
    std::fprintf(stderr, "warning: %s\n", warning.c_str());
}

} // namespace

extern "C" obelisk_rt_status
obelisk_rt_v1_format(obelisk_rt_context *context, const char *format,
                     uint64_t formatSize, const obelisk_rt_arg_v1 *arguments,
                     uint64_t argumentCount,
                     const obelisk_rt_format_env_v1 *environment,
                     obelisk_rt_buffer_v1 *outBuffer) {
  if (!context || !outBuffer || !validBytes(format, formatSize) ||
      (argumentCount != 0 && !arguments) ||
      formatSize > std::numeric_limits<size_t>::max())
    return OBELISK_RT_INVALID_ARGUMENT;
  outBuffer->data = nullptr;
  outBuffer->size = 0;
  return guarded(context, [&] {
    std::string output;
    std::string error;
    uint64_t index = 0;
    TimeOverride timeFormat = snapshotTimeFormat(context);
    obelisk_rt_status status = formatSequence(
        output, std::string_view(format ? format : "", formatSize), arguments,
        argumentCount, index, environment, timeFormat, error);
    if (status == OBELISK_RT_OK && index != argumentCount) {
      status = OBELISK_RT_ARGUMENT_MISMATCH;
      error = "too many arguments for format string";
    }
    if (status != OBELISK_RT_OK) {
      setLastError(context, std::move(error));
      return status;
    }
    return makeBuffer(output, outBuffer);
  });
}

extern "C" obelisk_rt_status
obelisk_rt_v1_time_format(obelisk_rt_context *context, int32_t units,
                          uint32_t fractionDigits, const char *suffix,
                          uint64_t suffixSize, uint32_t width) {
  // The buffer %t renders through is fixed, so a precision that could not fit
  // is rejected rather than silently truncated.
  if (!context || !validBytes(suffix, suffixSize) || fractionDigits > 128 ||
      suffixSize > std::numeric_limits<size_t>::max())
    return OBELISK_RT_INVALID_ARGUMENT;
  return guarded(context, [&] {
    std::string text(suffix ? suffix : "", static_cast<size_t>(suffixSize));
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    context->timeFormat.active = true;
    context->timeFormat.units = units;
    context->timeFormat.fractionDigits = fractionDigits;
    context->timeFormat.width = width;
    context->timeFormat.suffix = std::move(text);
    return OBELISK_RT_OK;
  });
}

extern "C" obelisk_rt_status obelisk_rt_v1_string_output_format(
    obelisk_rt_context *context, obelisk_rt_radix defaultRadix,
    const obelisk_rt_arg_v1 *items, uint64_t itemCount,
    const obelisk_rt_format_env_v1 *environment,
    obelisk_rt_string_v1 *outString) {
  if (!context || !outString || (itemCount != 0 && !items))
    return OBELISK_RT_INVALID_ARGUMENT;
  *outString = 0;
  return guarded(context, [&] {
    std::string output;
    std::string error;
    std::vector<std::string> warnings;
    TimeOverride timeFormat = snapshotTimeFormat(context);
    obelisk_rt_status status =
        buildDisplay(output, defaultRadix, items, itemCount, environment,
                     timeFormat, error, warnings);
    if (status != OBELISK_RT_OK) {
      setLastError(context, std::move(error));
      return status;
    }
    reportFormatWarnings(context, warnings);
    obelisk_rt_gc_lane_v1 *lane = obelisk_rt_v1_gc_current_lane(context);
    if (!lane)
      return OBELISK_RT_INVALID_LIFECYCLE;
    return obelisk_rt_v1_string_create(lane, output.data(), output.size(),
                                       outString);
  });
}

extern "C" obelisk_rt_status
obelisk_rt_v1_display(obelisk_rt_context *context, uint32_t descriptor,
                      uint32_t appendNewline, obelisk_rt_radix defaultRadix,
                      const obelisk_rt_arg_v1 *items, uint64_t itemCount,
                      const obelisk_rt_format_env_v1 *environment) {
  if (!context || (itemCount != 0 && !items))
    return OBELISK_RT_INVALID_ARGUMENT;
  return guarded(context, [&] {
    {
      std::lock_guard<std::recursive_mutex> lock(context->mutex);
      // A monitor callback is the only display executed by the registered
      // monitor process. Suppress it while monitoring is disabled without
      // stealing a bit from the descriptor: all 30 MCD bits are public ABI.
      if (!context->monitorEnabled &&
          context->activeLogicalProcessToken != 0 &&
          context->activeLogicalProcessToken ==
              context->monitorLogicalProcessToken)
        return OBELISK_RT_OK;
    }
    std::string output;
    std::string error;
    std::vector<std::string> warnings;
    TimeOverride timeFormat = snapshotTimeFormat(context);
    obelisk_rt_status status =
        buildDisplay(output, defaultRadix, items, itemCount, environment,
                     timeFormat, error, warnings);
    if (status != OBELISK_RT_OK) {
      setLastError(context, std::move(error));
      return status;
    }
    reportFormatWarnings(context, warnings);
    if (appendNewline)
      output.push_back('\n');
    // An MCD with no selected bits writes nowhere. Formatting and argument
    // evaluation still occur, but the void display task succeeds.
    if (descriptor == 0)
      return OBELISK_RT_OK;
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    return writeUnlocked(context, descriptor, output.data(), output.size(),
                         nullptr);
  });
}
