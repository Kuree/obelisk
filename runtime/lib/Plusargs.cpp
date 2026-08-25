//===- Plusargs.cpp - Command-line plusarg queries (IEEE 1800 21.6) -------===//
//
// The generated executable hands its argv to the context at startup, which
// keeps every '+'-introduced argument with that prefix stripped. Both queries
// below match a caller-supplied prefix against those entries in the order they
// were given, which is what $test$plusargs and $value$plusargs are specified
// to do.
//
//===---------------------------------------------------------------------===//

#include "RuntimeInternal.h"

#include <charconv>
#include <cstring>
#include <string>
#include <string_view>

namespace {

obelisk_rt_status buildPlusargIndex(obelisk_rt_context *context) {
  if (context->plusargIndexBuilt)
    return OBELISK_RT_OK;
  if (context->plusargs.empty()) {
    context->plusargIndexBuilt = true;
    return OBELISK_RT_OK;
  }
  if (context->plusargs.size() > UINT32_MAX)
    return OBELISK_RT_OUT_OF_RESOURCES;
  size_t characterCount = 0;
  for (const std::string &argument : context->plusargs) {
    if (argument.size() > UINT32_MAX ||
        characterCount > UINT32_MAX - argument.size())
      return OBELISK_RT_OUT_OF_RESOURCES;
    characterCount += argument.size();
  }
  if (characterCount == UINT32_MAX)
    return OBELISK_RT_OUT_OF_RESOURCES;

  std::vector<PlusargIndexNode> nodes;
  std::vector<PlusargIndexEdge> edges;
  nodes.reserve(characterCount + 1);
  edges.reserve(characterCount);
  nodes.emplace_back();
  for (uint32_t argumentIndex = 0;
       argumentIndex != static_cast<uint32_t>(context->plusargs.size());
       ++argumentIndex) {
    const std::string &argument = context->plusargs[argumentIndex];
    uint32_t node = 0;
    if (nodes[node].firstArgument == UINT32_MAX)
      nodes[node].firstArgument = argumentIndex;
    for (unsigned char character : argument) {
      uint32_t edge = nodes[node].firstEdge;
      while (edge != UINT32_MAX && edges[edge].character != character)
        edge = edges[edge].nextEdge;
      if (edge == UINT32_MAX) {
        uint32_t child = static_cast<uint32_t>(nodes.size());
        nodes.emplace_back();
        edge = static_cast<uint32_t>(edges.size());
        edges.push_back(
            {child, nodes[node].firstEdge, static_cast<uint8_t>(character)});
        nodes[node].firstEdge = edge;
      }
      node = edges[edge].nextNode;
      if (nodes[node].firstArgument == UINT32_MAX)
        nodes[node].firstArgument = argumentIndex;
    }
  }
  context->plusargIndexNodes.swap(nodes);
  context->plusargIndexEdges.swap(edges);
  context->plusargIndexBuilt = true;
  return OBELISK_RT_OK;
}

// The matched argument, or nullptr when no plusarg starts with `prefix`.
obelisk_rt_status findPlusarg(obelisk_rt_context *context,
                              std::string_view prefix,
                              const std::string *&match) {
  match = nullptr;
  obelisk_rt_status status = buildPlusargIndex(context);
  if (status != OBELISK_RT_OK || context->plusargIndexNodes.empty())
    return status;
  uint32_t node = 0;
  for (unsigned char character : prefix) {
    uint32_t edge = context->plusargIndexNodes[node].firstEdge;
    while (edge != UINT32_MAX &&
           context->plusargIndexEdges[edge].character != character)
      edge = context->plusargIndexEdges[edge].nextEdge;
    if (edge == UINT32_MAX)
      return OBELISK_RT_OK;
    node = context->plusargIndexEdges[edge].nextNode;
  }
  uint32_t argument = context->plusargIndexNodes[node].firstArgument;
  if (argument != UINT32_MAX)
    match = &context->plusargs[argument];
  return OBELISK_RT_OK;
}

bool splitValueFormat(std::string_view format, std::string &prefix,
                      uint32_t &conversion) {
  size_t percent = format.rfind('%');
  if (percent == std::string_view::npos)
    return false;
  size_t specifier = percent + 1;
  while (specifier < format.size() && format[specifier] == '0')
    ++specifier;
  if (specifier + 1 != format.size())
    return false;
  switch (format[specifier]) {
  case 'b':
  case 'B':
    conversion = 2;
    break;
  case 'o':
  case 'O':
    conversion = 8;
    break;
  case 'd':
  case 'D':
    conversion = 10;
    break;
  case 'h':
  case 'H':
  case 'x':
  case 'X':
    conversion = 16;
    break;
  case 'e':
  case 'E':
  case 'f':
  case 'F':
  case 'g':
  case 'G':
    conversion = 1;
    break;
  case 's':
  case 'S':
    conversion = 0;
    break;
  default:
    return false;
  }

  prefix.clear();
  prefix.reserve(percent);
  for (size_t index = 0; index < percent; ++index) {
    if (format[index] == '%' && index + 1 < percent &&
        format[index + 1] == '%')
      ++index;
    prefix.push_back(format[index]);
  }
  return true;
}

bool scanSpace(char character) {
  return character == ' ' || character == '\t' || character == '\n' ||
         character == '\r' || character == '\f' || character == '\v';
}

void fillUnknown(uint8_t *value, uint8_t *unknown, uint64_t byteCount,
                 uint64_t bitWidth) {
  std::memset(value, 0, static_cast<size_t>(byteCount));
  std::memset(unknown, 0xff, static_cast<size_t>(byteCount));
  if ((bitWidth & 7) != 0)
    unknown[byteCount - 1] =
        static_cast<uint8_t>((UINT32_C(1) << (bitWidth & 7)) - 1);
}

void setPlaneBit(uint8_t *plane, uint64_t bit) {
  plane[bit / 8] |= static_cast<uint8_t>(UINT32_C(1) << (bit % 8));
}

} // namespace

extern "C" obelisk_rt_status obelisk_rt_v1_plusarg_parse_logic(
    obelisk_rt_string_v1 string, uint32_t radix, uint64_t bitWidth,
    void *value, uint64_t valueSize, void *unknown, uint64_t unknownSize) {
  if (!value || !unknown || bitWidth == 0 || bitWidth > UINT64_MAX - 7 ||
      (radix != 2 && radix != 8 && radix != 10 && radix != 16))
    return OBELISK_RT_INVALID_ARGUMENT;
  uint64_t byteCount = (bitWidth + 7) / 8;
  if (byteCount > valueSize || byteCount > unknownSize ||
      byteCount > SIZE_MAX)
    return OBELISK_RT_INVALID_ARGUMENT;
  auto *valueBytes = static_cast<uint8_t *>(value);
  auto *unknownBytes = static_cast<uint8_t *>(unknown);
  std::memset(valueBytes, 0, static_cast<size_t>(byteCount));
  std::memset(unknownBytes, 0, static_cast<size_t>(byteCount));

  char scratch[8] = {};
  const char *bytes = nullptr;
  uint64_t size = 0;
  obelisk_rt_status status =
      obelisk_rt_v1_string_view(string, scratch, &bytes, &size);
  if (status != OBELISK_RT_OK)
    return status;
  uint64_t begin = 0;
  while (begin != size && scanSpace(bytes[begin]))
    ++begin;
  bool negative = false;
  bool hadSign = false;
  if (begin != size && (bytes[begin] == '+' || bytes[begin] == '-')) {
    hadSign = true;
    negative = bytes[begin] == '-';
    ++begin;
  }

  uint64_t digitCount = 0;
  bool hasUnknown = false;
  char decimalUnknown = 0;
  bool valid = true;
  for (uint64_t index = begin; index != size; ++index) {
    unsigned char character = static_cast<unsigned char>(bytes[index]);
    if (character == '_')
      continue;
    bool isX = character == 'x' || character == 'X';
    bool isZ = character == 'z' || character == 'Z' || character == '?';
    uint32_t digit =
        character >= '0' && character <= '9'   ? character - '0'
        : character >= 'a' && character <= 'f' ? character - 'a' + 10
        : character >= 'A' && character <= 'F' ? character - 'A' + 10
                                                : UINT32_MAX;
    if (isX || isZ) {
      if (radix != 10) {
        hasUnknown = true;
      } else if (digitCount == 0 && index + 1 == size && !hadSign) {
        decimalUnknown = static_cast<char>(character);
      } else {
        valid = false;
        break;
      }
    } else if (digit >= radix) {
      valid = false;
      break;
    }
    ++digitCount;
  }
  // A genuinely empty remainder is the one special zero-valued case in
  // 21.6. Signs, separators, or whitespace without a digit are malformed.
  if (!valid || (digitCount == 0 && size != 0)) {
    fillUnknown(valueBytes, unknownBytes, byteCount, bitWidth);
    return OBELISK_RT_OK;
  }
  if (digitCount == 0)
    return OBELISK_RT_OK;

  if (decimalUnknown) {
    bool highImpedance = decimalUnknown == 'z' || decimalUnknown == 'Z' ||
                         decimalUnknown == '?';
    std::memset(valueBytes, highImpedance ? 0xff : 0,
                static_cast<size_t>(byteCount));
    std::memset(unknownBytes, 0xff, static_cast<size_t>(byteCount));
    if ((bitWidth & 7) != 0) {
      uint8_t mask =
          static_cast<uint8_t>((UINT32_C(1) << (bitWidth & 7)) - 1);
      valueBytes[byteCount - 1] &= mask;
      unknownBytes[byteCount - 1] &= mask;
    }
    return OBELISK_RT_OK;
  }

  // Preserve the common scalar path: a native accumulator avoids walking up
  // to eight destination bytes for every decimal digit or expanding every
  // power-of-two digit into individual bits. Wider values use the linear
  // direct-placement / word-wise paths below.
  if (bitWidth <= 64) {
    uint64_t scalarValue = 0;
    uint64_t scalarUnknown = 0;
    for (uint64_t index = begin; index != size; ++index) {
      unsigned char character = static_cast<unsigned char>(bytes[index]);
      if (character == '_')
        continue;
      bool isX = character == 'x' || character == 'X';
      bool isZ = character == 'z' || character == 'Z' || character == '?';
      uint32_t digit =
          character >= '0' && character <= '9'   ? character - '0'
          : character >= 'a' && character <= 'f' ? character - 'a' + 10
                                                  : character - 'A' + 10;
      scalarValue *= radix;
      scalarUnknown *= radix;
      if (isX || isZ) {
        scalarUnknown += radix - 1;
        if (isZ)
          scalarValue += radix - 1;
      } else {
        scalarValue += digit;
      }
    }
    for (uint64_t byte = 0; byte != byteCount; ++byte) {
      valueBytes[byte] = static_cast<uint8_t>(scalarValue >> (byte * 8));
      unknownBytes[byte] =
          static_cast<uint8_t>(scalarUnknown >> (byte * 8));
    }
  } else if (radix == 10) {
    for (uint64_t index = begin; index != size; ++index) {
      unsigned char character = static_cast<unsigned char>(bytes[index]);
      if (character == '_')
        continue;
      uint32_t carry = character - '0';
      for (uint64_t byte = 0; byte != byteCount; ++byte) {
        uint32_t accumulated =
            static_cast<uint32_t>(valueBytes[byte]) * 10 + carry;
        valueBytes[byte] = static_cast<uint8_t>(accumulated);
        carry = accumulated >> 8;
      }
    }
  } else {
    unsigned digitBits = radix == 2 ? 1 : radix == 8 ? 3 : 4;
    uint64_t ordinal = 0;
    for (uint64_t index = size; index != begin;) {
      unsigned char character = static_cast<unsigned char>(bytes[--index]);
      if (character == '_')
        continue;
      bool isX = character == 'x' || character == 'X';
      bool isZ = character == 'z' || character == 'Z' || character == '?';
      uint32_t digit =
          character >= '0' && character <= '9'   ? character - '0'
          : character >= 'a' && character <= 'f' ? character - 'a' + 10
                                                  : character - 'A' + 10;
      if (ordinal <= (bitWidth - 1) / digitBits) {
        uint64_t base = ordinal * digitBits;
        for (unsigned bit = 0; bit != digitBits && base + bit < bitWidth;
             ++bit) {
          if (isX || isZ)
            setPlaneBit(unknownBytes, base + bit);
          if (isZ || (!isX && ((digit >> bit) & 1) != 0))
            setPlaneBit(valueBytes, base + bit);
        }
      }
      ++ordinal;
    }
  }

  if (negative) {
    if (hasUnknown) {
      fillUnknown(valueBytes, unknownBytes, byteCount, bitWidth);
      return OBELISK_RT_OK;
    }
    uint32_t carry = 1;
    for (uint64_t byte = 0; byte != byteCount; ++byte) {
      uint32_t complemented = static_cast<uint8_t>(~valueBytes[byte]) + carry;
      valueBytes[byte] = static_cast<uint8_t>(complemented);
      carry = complemented >> 8;
    }
  }
  if ((bitWidth & 7) != 0) {
    uint8_t mask =
        static_cast<uint8_t>((UINT32_C(1) << (bitWidth & 7)) - 1);
    valueBytes[byteCount - 1] &= mask;
    unknownBytes[byteCount - 1] &= mask;
  }
  return OBELISK_RT_OK;
}

extern "C" obelisk_rt_status obelisk_rt_v1_plusarg_parse_real(
    obelisk_rt_string_v1 string, double *outValue) {
  if (!outValue)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outValue = 0.0;
  char scratch[8] = {};
  const char *bytes = nullptr;
  uint64_t size = 0;
  obelisk_rt_status status =
      obelisk_rt_v1_string_view(string, scratch, &bytes, &size);
  if (status != OBELISK_RT_OK)
    return status;
  if (size == 0)
    return OBELISK_RT_OK;
  if (size > SIZE_MAX)
    return OBELISK_RT_OUT_OF_RESOURCES;
  try {
    std::string spelling;
    spelling.reserve(static_cast<size_t>(size));
    for (uint64_t index = 0; index != size; ++index)
      if (bytes[index] != '_')
        spelling.push_back(bytes[index]);
    const char *begin = spelling.data();
    const char *end = begin + spelling.size();
    while (begin != end && scanSpace(*begin))
      ++begin;
    if (begin != end && *begin == '+')
      ++begin;
    double value = 0.0;
    auto parsed =
        std::from_chars(begin, end, value, std::chars_format::general);
    if (parsed.ec == std::errc{} && parsed.ptr == end)
      *outValue = value;
    return OBELISK_RT_OK;
  } catch (const std::bad_alloc &) {
    return OBELISK_RT_OUT_OF_MEMORY;
  }
}

extern "C" obelisk_rt_status
obelisk_rt_v1_plusarg_test(obelisk_rt_context *context,
                           obelisk_rt_string_v1 name, uint32_t *outFound) {
  if (!context || !outFound)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outFound = 0;
  char scratch[8] = {};
  const char *bytes = nullptr;
  uint64_t size = 0;
  obelisk_rt_status status =
      obelisk_rt_v1_string_view(name, scratch, &bytes, &size);
  if (status != OBELISK_RT_OK)
    return status;
  return guarded(context, [&] {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    const std::string *match = nullptr;
    obelisk_rt_status findStatus =
        findPlusarg(context, std::string_view(bytes, size), match);
    *outFound = match ? 1u : 0u;
    return findStatus;
  });
}

extern "C" obelisk_rt_status obelisk_rt_v1_plusarg_value(
    obelisk_rt_context *context, obelisk_rt_gc_lane_v1 *lane,
    obelisk_rt_string_v1 prefix, obelisk_rt_string_v1 *outTail,
    uint32_t *outFound) {
  if (!context || !outTail || !outFound)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outTail = 0;
  *outFound = 0;
  char scratch[8] = {};
  const char *bytes = nullptr;
  uint64_t size = 0;
  obelisk_rt_status status =
      obelisk_rt_v1_string_view(prefix, scratch, &bytes, &size);
  if (status != OBELISK_RT_OK)
    return status;
  std::string tail;
  status = guarded(context, [&] {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    const std::string *match = nullptr;
    obelisk_rt_status findStatus =
        findPlusarg(context, std::string_view(bytes, size), match);
    if (findStatus != OBELISK_RT_OK)
      return findStatus;
    if (!match)
      return OBELISK_RT_OK;
    tail = match->substr(static_cast<size_t>(size));
    *outFound = 1;
    return OBELISK_RT_OK;
  });
  if (status != OBELISK_RT_OK || !*outFound)
    return status;
  return obelisk_rt_v1_string_create(lane, tail.data(), tail.size(), outTail);
}

extern "C" obelisk_rt_status obelisk_rt_v1_plusarg_scan(
    obelisk_rt_context *context, obelisk_rt_gc_lane_v1 *lane,
    obelisk_rt_string_v1 format, obelisk_rt_string_v1 *outTail,
    uint32_t *outConversion, uint32_t *outFound) {
  if (!context || !lane || !outTail || !outConversion || !outFound)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outTail = 0;
  *outConversion = 0;
  *outFound = 0;
  char scratch[8] = {};
  const char *bytes = nullptr;
  uint64_t size = 0;
  obelisk_rt_status status =
      obelisk_rt_v1_string_view(format, scratch, &bytes, &size);
  if (status != OBELISK_RT_OK)
    return status;
  std::string prefix;
  if (!splitValueFormat(std::string_view(bytes, size), prefix, *outConversion))
    return OBELISK_RT_OK;

  std::string tail;
  status = guarded(context, [&] {
    std::lock_guard<std::recursive_mutex> lock(context->mutex);
    const std::string *match = nullptr;
    obelisk_rt_status findStatus = findPlusarg(context, prefix, match);
    if (findStatus != OBELISK_RT_OK)
      return findStatus;
    if (!match)
      return OBELISK_RT_OK;
    tail = match->substr(prefix.size());
    *outFound = 1;
    return OBELISK_RT_OK;
  });
  if (status != OBELISK_RT_OK || !*outFound)
    return status;
  return obelisk_rt_v1_string_create(lane, tail.data(), tail.size(), outTail);
}
