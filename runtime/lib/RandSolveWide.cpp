//===- RandSolveWide.cpp - Wide constrained-random fallback --------------===//

#include "ExceptionSupport.h"
#include "RandSamplePlan.h"
#include "RandSolveOrder.h"
#include "RandSolveWeighted.h"
#include "RuntimeInternal.h"
#include "obelisk/Runtime/RandomDomain.h"
#include "obelisk/Runtime/RandomSamplerPlan.h"
#include "obelisk/Runtime/Runtime.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>

namespace {

struct WideValue {
  uint32_t width = 1;
  std::vector<uint64_t> words{0};

  WideValue() = default;
  explicit WideValue(uint32_t width)
      : width(width), words((static_cast<uint64_t>(width) + 63) / 64, 0) {}
};

struct WideInstruction {
  uint8_t opcode = 0;
  uint8_t flags = 0;
  uint32_t width = 0;
  uint32_t operand = 0;
  uint32_t auxiliary = 0;
  WideValue literal;
};

struct SolveBeforeEdge {
  WideValue beforeMask;
  WideValue afterMask;
  uint32_t constraintBlock;
  bool functionPriority = false;
};

struct DomainPattern {
  obelisk::random::DomainRecord bits;
  uint64_t freeMask = 0;
  uint64_t cardinality = 0;
};

struct DomainGroup {
  uint32_t targetOffset = 0;
  uint32_t width = 0;
  std::vector<DomainPattern> patterns;
  uint64_t cardinality = 0;
  bool fullCardinality = false;
};

struct WideProgram {
  uint32_t aggregateWidth = 0;
  uint32_t maxDepth = 0;
  uint32_t temporaryCount = 0;
  bool hasSoft = false;
  std::vector<uint32_t> captureWidths;
  std::vector<uint64_t> captureOffsets;
  std::vector<WideInstruction> instructions;
  std::vector<DomainGroup> domainGroups;
  std::vector<SolveBeforeEdge> solveEdges;
};

uint16_t read16(const uint8_t *bytes) {
  return static_cast<uint16_t>(bytes[0]) |
         (static_cast<uint16_t>(bytes[1]) << 8);
}

uint32_t read32(const uint8_t *bytes) {
  return static_cast<uint32_t>(bytes[0]) |
         (static_cast<uint32_t>(bytes[1]) << 8) |
         (static_cast<uint32_t>(bytes[2]) << 16) |
         (static_cast<uint32_t>(bytes[3]) << 24);
}

uint64_t read64(const uint8_t *bytes) {
  uint64_t result = 0;
  for (unsigned index = 0; index != 8; ++index)
    result |= static_cast<uint64_t>(bytes[index]) << (index * 8);
  return result;
}

uint64_t highMask(uint32_t width) {
  unsigned used = width % 64;
  return used == 0 ? UINT64_MAX : (uint64_t{1} << used) - 1;
}

void normalize(WideValue &value) {
  value.words.back() &= highMask(value.width);
}

bool bit(const WideValue &value, uint32_t index) {
  return index < value.width &&
         ((value.words[index / 64] >> (index % 64)) & 1) != 0;
}

uint64_t widthMask(uint32_t width) {
  return width >= 64 ? UINT64_MAX : (uint64_t{1} << width) - 1;
}

uint64_t compressBits(uint64_t value, uint64_t mask) {
  uint64_t compressed = 0;
  unsigned output = 0;
  for (unsigned input = 0; input != 64; ++input) {
    uint64_t selected = uint64_t{1} << input;
    if ((mask & selected) == 0)
      continue;
    if ((value & selected) != 0)
      compressed |= uint64_t{1} << output;
    ++output;
  }
  return compressed;
}

uint64_t expandBits(uint64_t value, uint64_t mask) {
  uint64_t expanded = 0;
  unsigned input = 0;
  for (unsigned output = 0; output != 64; ++output) {
    uint64_t selected = uint64_t{1} << output;
    if ((mask & selected) == 0)
      continue;
    if ((value & (uint64_t{1} << input)) != 0)
      expanded |= selected;
    ++input;
  }
  return expanded;
}

unsigned countBits(uint64_t value) {
  return static_cast<unsigned>(__builtin_popcountll(value));
}

void setBit(WideValue &value, uint32_t index, bool selected) {
  uint64_t mask = uint64_t{1} << (index % 64);
  if (selected)
    value.words[index / 64] |= mask;
  else
    value.words[index / 64] &= ~mask;
}

void insert(WideValue &value, uint32_t offset, uint32_t width, uint64_t field) {
  for (uint32_t bitIndex = 0; bitIndex != width; ++bitIndex)
    setBit(value, offset + bitIndex, ((field >> bitIndex) & 1) != 0);
}

bool truth(const WideValue &value) {
  return std::any_of(value.words.begin(), value.words.end(),
                     [](uint64_t word) { return word != 0; });
}

WideValue booleanValue(bool value, uint32_t width = 1) {
  WideValue result(width);
  result.words.front() = value;
  return result;
}

WideValue resize(const WideValue &input, uint32_t width, bool signExtend) {
  WideValue result(width);
  size_t common = std::min(result.words.size(), input.words.size());
  std::copy_n(input.words.begin(), common, result.words.begin());
  if (signExtend && width > input.width && bit(input, input.width - 1)) {
    for (uint32_t index = input.width; index != width; ++index)
      setBit(result, index, true);
  }
  normalize(result);
  return result;
}

WideValue extract(const uint64_t *words, uint64_t wordCount, uint32_t offset,
                  uint32_t width) {
  WideValue result(width);
  uint64_t firstWord = offset / 64;
  unsigned shift = offset % 64;
  for (size_t index = 0; index != result.words.size(); ++index) {
    uint64_t source = firstWord + index;
    if (source < wordCount)
      result.words[index] = words[source] >> shift;
    if (shift != 0 && source + 1 < wordCount)
      result.words[index] |= words[source + 1] << (64 - shift);
  }
  normalize(result);
  return result;
}

int compareUnsigned(const WideValue &lhs, const WideValue &rhs) {
  for (size_t index = lhs.words.size(); index != 0; --index) {
    if (lhs.words[index - 1] == rhs.words[index - 1])
      continue;
    return lhs.words[index - 1] < rhs.words[index - 1] ? -1 : 1;
  }
  return 0;
}

WideValue bitwise(const WideValue &lhs, const WideValue &rhs, uint32_t width,
                  uint8_t opcode) {
  WideValue result(width);
  for (size_t index = 0; index != result.words.size(); ++index) {
    switch (opcode) {
    case OBELISK_RT_RANDOM_BIT_AND_V1:
      result.words[index] = lhs.words[index] & rhs.words[index];
      break;
    case OBELISK_RT_RANDOM_BIT_OR_V1:
      result.words[index] = lhs.words[index] | rhs.words[index];
      break;
    case OBELISK_RT_RANDOM_BIT_XOR_V1:
      result.words[index] = lhs.words[index] ^ rhs.words[index];
      break;
    default:
      result.words[index] = ~(lhs.words[index] ^ rhs.words[index]);
      break;
    }
  }
  normalize(result);
  return result;
}

WideValue add(const WideValue &lhs, const WideValue &rhs, uint32_t width) {
  WideValue result(width);
  uint64_t carry = 0;
  for (size_t index = 0; index != result.words.size(); ++index) {
    uint64_t partial = lhs.words[index] + carry;
    bool firstCarry = partial < lhs.words[index];
    uint64_t sum = partial + rhs.words[index];
    bool secondCarry = sum < partial;
    result.words[index] = sum;
    carry = firstCarry || secondCarry;
  }
  normalize(result);
  return result;
}

WideValue negate(const WideValue &input) {
  WideValue result(input.width);
  uint64_t carry = 1;
  for (size_t index = 0; index != result.words.size(); ++index) {
    uint64_t inverted = ~input.words[index];
    result.words[index] = inverted + carry;
    carry = carry && result.words[index] == 0;
  }
  normalize(result);
  return result;
}

WideValue subtract(const WideValue &lhs, const WideValue &rhs, uint32_t width) {
  return add(lhs, negate(rhs), width);
}

WideValue multiply(const WideValue &lhs, const WideValue &rhs, uint32_t width) {
  WideValue result(width);
  for (size_t left = 0; left != lhs.words.size(); ++left) {
    uint64_t carry = 0;
    for (size_t right = 0; right + left < result.words.size(); ++right) {
      size_t output = left + right;
      __uint128_t product =
          static_cast<__uint128_t>(lhs.words[left]) * rhs.words[right] +
          result.words[output] + carry;
      result.words[output] = static_cast<uint64_t>(product);
      carry = static_cast<uint64_t>(product >> 64);
    }
  }
  normalize(result);
  return result;
}

bool shiftAmount(const WideValue &input, uint32_t width, uint32_t &amount) {
  if (input.words.size() > 1 &&
      std::any_of(input.words.begin() + 1, input.words.end(),
                  [](uint64_t word) { return word != 0; }))
    return false;
  if (input.words.front() >= width)
    return false;
  amount = static_cast<uint32_t>(input.words.front());
  return true;
}

WideValue shiftLeft(const WideValue &input, uint32_t width, uint32_t amount) {
  WideValue result(width);
  size_t wordShift = amount / 64;
  unsigned bitShift = amount % 64;
  for (size_t output = result.words.size(); output != 0; --output) {
    size_t destination = output - 1;
    if (destination < wordShift)
      continue;
    size_t source = destination - wordShift;
    result.words[destination] |= input.words[source] << bitShift;
    if (bitShift != 0 && source != 0)
      result.words[destination] |= input.words[source - 1] >> (64 - bitShift);
  }
  normalize(result);
  return result;
}

WideValue shiftRight(const WideValue &input, uint32_t width, uint32_t amount,
                     bool arithmetic) {
  WideValue result(width);
  size_t wordShift = amount / 64;
  unsigned bitShift = amount % 64;
  for (size_t destination = 0; destination != result.words.size();
       ++destination) {
    size_t source = destination + wordShift;
    if (source >= input.words.size())
      continue;
    result.words[destination] |= input.words[source] >> bitShift;
    if (bitShift != 0 && source + 1 < input.words.size())
      result.words[destination] |= input.words[source + 1] << (64 - bitShift);
  }
  if (arithmetic && bit(input, width - 1))
    for (uint32_t index = width - amount; index != width; ++index)
      setBit(result, index, true);
  normalize(result);
  return result;
}

void unsignedDivMod(const WideValue &dividend, const WideValue &divisor,
                    WideValue &quotient, WideValue &remainder) {
  quotient = WideValue(dividend.width);
  remainder = WideValue(dividend.width);
  for (uint32_t index = dividend.width; index != 0; --index) {
    remainder = shiftLeft(remainder, remainder.width, 1);
    setBit(remainder, 0, bit(dividend, index - 1));
    if (compareUnsigned(remainder, divisor) >= 0) {
      remainder = subtract(remainder, divisor, remainder.width);
      setBit(quotient, index - 1, true);
    }
  }
}

WideValue power(WideValue base, const WideValue &exponent, uint32_t width) {
  WideValue result(width);
  result.words.front() = 1;
  for (uint32_t index = 0; index != exponent.width; ++index) {
    if (bit(exponent, index))
      result = multiply(result, base, width);
    if (index + 1 != exponent.width)
      base = multiply(base, base, width);
  }
  return result;
}

bool isUnary(uint8_t opcode) {
  return (opcode >= OBELISK_RT_RANDOM_CAST_V1 &&
          opcode <= OBELISK_RT_RANDOM_LOGICAL_NOT_V1) ||
         opcode == OBELISK_RT_RANDOM_COUNT_ONES_V1 ||
         opcode == OBELISK_RT_RANDOM_CLOG2_V1 ||
         obelisk::random::isRealInstruction(opcode);
}

bool isBinary(uint8_t opcode) {
  return (opcode >= OBELISK_RT_RANDOM_ADD_V1 &&
          opcode <= OBELISK_RT_RANDOM_LOGICAL_EQUIV_V1) ||
         (opcode >= OBELISK_RT_RANDOM_DIV_V1 &&
          opcode <= OBELISK_RT_RANDOM_POWER_V1);
}

bool intersects(const WideValue &left, const WideValue &right) {
  for (size_t index = 0; index != left.words.size(); ++index)
    if ((left.words[index] & right.words[index]) != 0)
      return true;
  return false;
}

bool buildSolveBeforeLayers(const std::vector<SolveBeforeEdge> &edges,
                            const WideValue &mutableMask,
                            uint64_t constraintMask,
                            std::vector<WideValue> &layers) {
  std::vector<obelisk::runtime::SolveOrderEdge> orderEdges;
  for (const SolveBeforeEdge &edge : edges)
    orderEdges.push_back({edge.beforeMask.words, edge.afterMask.words,
                          edge.constraintBlock, edge.functionPriority});
  std::vector<obelisk::runtime::OrderMask> masks;
  if (!obelisk::runtime::buildSolveOrderLayers(orderEdges, mutableMask.words,
                                               constraintMask, false, masks))
    return false;
  layers.clear();
  for (auto &mask : masks) {
    WideValue layer(mutableMask.width);
    layer.words = std::move(mask);
    layers.push_back(std::move(layer));
  }
  return true;
}

bool parseWideProgram(const uint8_t *bytes, size_t size, WideProgram &result,
                      const uint32_t *captureWidths, uint64_t captureCount,
                      uint64_t captureWordCount) {
  if (size < OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE_V2 ||
      read32(bytes) != OBELISK_RT_RANDOM_PROGRAM_MAGIC ||
      read16(bytes + 4) != OBELISK_RT_RANDOM_PROGRAM_VERSION_V2 ||
      read16(bytes + 6) != OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE_V2)
    return false;
  result.aggregateWidth = read32(bytes + 8);
  uint32_t instructionCount = read32(bytes + 12);
  uint32_t encodedCaptureCount = read32(bytes + 16);
  uint32_t flags = read32(bytes + 20);
  // The plan validator reads widths indexed by the encoded capture inventory.
  // Establish that the caller supplied that inventory before inspecting it.
  if (encodedCaptureCount != captureCount)
    return false;
  if (flags & OBELISK_RT_RANDOM_PROGRAM_HAS_SAMPLER_PLAN) {
    obelisk::random::SamplerPlanView plan;
    if (!obelisk::random::readSamplerPlan(bytes, size, captureWidths, plan))
      return false;
    size = plan.coreSize;
  }
  uint32_t literalWordCount = read32(bytes + 24);
  bool encodedDomains = (flags & OBELISK_RT_RANDOM_PROGRAM_HAS_DOMAINS) != 0;
  bool encodedOrder = (flags & OBELISK_RT_RANDOM_PROGRAM_HAS_SOLVE_BEFORE) != 0;
  bool encodedPriority =
      (flags & OBELISK_RT_RANDOM_PROGRAM_HAS_FUNCTION_PRIORITY) != 0;
  std::vector<uint32_t> maskCaptures;
  bool weighted = (flags & OBELISK_RT_RANDOM_PROGRAM_SMT_WEIGHTED) != 0;
  bool encodedDist = (flags & OBELISK_RT_RANDOM_PROGRAM_HAS_DIST) != 0;
  if (result.aggregateWidth == 0 || encodedCaptureCount != captureCount ||
      read32(bytes + 28) != 0 ||
      (flags & ~(OBELISK_RT_RANDOM_PROGRAM_HAS_SOFT |
                 OBELISK_RT_RANDOM_PROGRAM_HAS_DOMAINS |
                 OBELISK_RT_RANDOM_PROGRAM_HAS_SOLVE_BEFORE |
                 OBELISK_RT_RANDOM_PROGRAM_SMT_COMPLETION |
                 OBELISK_RT_RANDOM_PROGRAM_SMT_UNIFORM |
                 OBELISK_RT_RANDOM_PROGRAM_HAS_DIST |
                 OBELISK_RT_RANDOM_PROGRAM_SMT_WEIGHTED |
                 OBELISK_RT_RANDOM_PROGRAM_HAS_GUARD_INPUT_MASKS |
                 OBELISK_RT_RANDOM_PROGRAM_HAS_SAMPLER_PLAN |
                 OBELISK_RT_RANDOM_PROGRAM_HAS_REAL |
                 OBELISK_RT_RANDOM_PROGRAM_HAS_FUNCTION_PRIORITY)) != 0)
    return false;
  bool real = (flags & OBELISK_RT_RANDOM_PROGRAM_HAS_REAL) != 0;
  if (real && (!(flags & OBELISK_RT_RANDOM_PROGRAM_HAS_SAMPLER_PLAN) ||
                !(flags & OBELISK_RT_RANDOM_PROGRAM_SMT_UNIFORM)))
    return false;
  if ((encodedDist != weighted) || (weighted && encodedPriority))
    return false;
  bool dynamicMasks =
      (flags & OBELISK_RT_RANDOM_PROGRAM_HAS_GUARD_INPUT_MASKS) != 0;
  if (dynamicMasks && !encodedPriority)
    return false;
  if (encodedOrder && !(flags & (OBELISK_RT_RANDOM_PROGRAM_SMT_UNIFORM |
                                 OBELISK_RT_RANDOM_PROGRAM_SMT_WEIGHTED)))
    return false;
  if (encodedPriority && !encodedOrder)
    return false;
  result.hasSoft = (flags & OBELISK_RT_RANDOM_PROGRAM_HAS_SOFT) != 0;
  uint64_t instructionBytes = static_cast<uint64_t>(instructionCount) *
                              OBELISK_RT_RANDOM_INSTRUCTION_SIZE_V2;
  uint64_t literalBytes = static_cast<uint64_t>(literalWordCount) * 8;
  uint64_t expectedSize = OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE_V2;
  if (instructionBytes > UINT64_MAX - expectedSize)
    return false;
  expectedSize += instructionBytes;
  if (literalBytes > UINT64_MAX - expectedSize)
    return false;
  uint64_t literalOffset = expectedSize;
  expectedSize += literalBytes;
  uint64_t solveRecordsOffset = 0, solveRecordSize = 0;
  uint32_t solveEdgeCount = 0;
  if (encodedOrder) {
    if (expectedSize > size ||
        size - expectedSize < OBELISK_RT_RANDOM_SOLVE_EDGE_HEADER_SIZE)
      return false;
    solveEdgeCount = read32(bytes + expectedSize);
    solveRecordsOffset =
        expectedSize + OBELISK_RT_RANDOM_SOLVE_EDGE_HEADER_SIZE;
    solveRecordSize = ((uint64_t(result.aggregateWidth) + 63) / 64) * 16 +
                      OBELISK_RT_RANDOM_SOLVE_EDGE_TAIL_SIZE_V2;
    if ((!encodedPriority && solveEdgeCount == 0) ||
        solveEdgeCount > (size - solveRecordsOffset) / solveRecordSize)
      return false;
    expectedSize = solveRecordsOffset + solveEdgeCount * solveRecordSize;
  }
  bool sourceFunctionArguments = false;
  if (encodedPriority) {
    if (expectedSize > size)
      return false;
    size_t offset = expectedSize;
    obelisk::runtime::PriorityInputs inputs;
    if (!obelisk::runtime::readPriorityInputs(
            bytes, size, OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE_V2,
            instructionCount, result.aggregateWidth, offset, inputs,
            captureCount, dynamicMasks))
      return false;
    expectedSize = offset;
    for (const auto &input : inputs)
      sourceFunctionArguments |= std::any_of(
          input.second.arguments.begin(), input.second.arguments.end(),
          [](uint64_t word) { return word != 0; });
    for (const auto &input : inputs)
      if (input.second.maskCapture) {
        maskCaptures.push_back(input.second.maskCapture - 1);
        maskCaptures.push_back(input.second.maskCapture);
      }
  }
  std::vector<obelisk::runtime::WeightedGroup> weightedGroups;
  if (weighted) {
    if (expectedSize > size ||
        size - expectedSize < OBELISK_RT_RANDOM_DIST_HEADER_SIZE)
      return false;
    size_t records = read32(bytes + expectedSize + 4);
    if (records > (size - expectedSize - OBELISK_RT_RANDOM_DIST_HEADER_SIZE) /
                      OBELISK_RT_RANDOM_DIST_RECORD_SIZE)
      return false;
    size_t footerSize = OBELISK_RT_RANDOM_DIST_HEADER_SIZE +
                        records * OBELISK_RT_RANDOM_DIST_RECORD_SIZE;
    if (!obelisk::runtime::parseWeightedGroups(
            bytes + expectedSize, footerSize, result.aggregateWidth,
            encodedCaptureCount, weightedGroups))
      return false;
    expectedSize += footerSize;
  }
  uint32_t domainGroupCount = 0;
  uint32_t domainRecordCount = 0;
  uint64_t domainRecordsOffset = 0;
  if (encodedDomains) {
    if (expectedSize > UINT64_MAX - OBELISK_RT_RANDOM_DOMAIN_HEADER_SIZE ||
        size < expectedSize + OBELISK_RT_RANDOM_DOMAIN_HEADER_SIZE)
      return false;
    domainGroupCount = read32(bytes + expectedSize);
    domainRecordCount = read32(bytes + expectedSize + 4);
    domainRecordsOffset = expectedSize + OBELISK_RT_RANDOM_DOMAIN_HEADER_SIZE;
    if (domainRecordCount > (UINT64_MAX - domainRecordsOffset) /
                                OBELISK_RT_RANDOM_DOMAIN_RECORD_SIZE)
      return false;
    expectedSize =
        domainRecordsOffset + static_cast<uint64_t>(domainRecordCount) *
                                  OBELISK_RT_RANDOM_DOMAIN_RECORD_SIZE;
  }
  if (expectedSize != size)
    return false;

  result.captureWidths.assign(encodedCaptureCount, 0);
  result.captureOffsets.assign(encodedCaptureCount, 0);
  for (uint32_t capture : maskCaptures)
    result.captureWidths[capture] = result.aggregateWidth;
  for (const auto &group : weightedGroups)
    for (const auto &item : group.items)
      for (const auto &interval : item.intervals)
        if (interval.captured) {
          result.captureWidths[interval.lower] = 64;
          result.captureWidths[interval.cardinality] = 64;
        }
  result.instructions.reserve(instructionCount);
  std::vector<uint8_t> literalUse(literalWordCount, 0);
  size_t depth = 0;
  size_t maxDepth = 0;
  std::vector<uint32_t> stackWidths, temporaryWidths;
  bool sawHard = false;
  bool sawSoft = false;
  uint64_t softPriorities = 0;
  std::vector<bool> guardSeen(weightedGroups.size(), false);
  for (uint32_t index = 0; index != instructionCount; ++index) {
    const uint8_t *cursor =
        bytes + OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE_V2 +
        static_cast<uint64_t>(index) * OBELISK_RT_RANDOM_INSTRUCTION_SIZE_V2;
    WideInstruction instruction;
    instruction.opcode = cursor[0];
    instruction.flags = cursor[1];
    instruction.width = read32(cursor + 4);
    instruction.operand = read32(cursor + 8);
    instruction.auxiliary = read32(cursor + 12);
    if (obelisk::random::isRealInstruction(instruction.opcode) &&
        (!real || stackWidths.empty() ||
         !obelisk::random::validRealInstruction(
             instruction.opcode, instruction.width, instruction.flags,
             instruction.operand, instruction.auxiliary, stackWidths.back())))
      return false;
    if (read16(cursor + 2) != 0 || instruction.width == 0 ||
        (instruction.flags & ~OBELISK_RT_RANDOM_INSTRUCTION_SIGNED) != 0)
      return false;
    switch (instruction.opcode) {
    case OBELISK_RT_RANDOM_PUSH_VARIABLE_V1:
      if (instruction.operand >= result.aggregateWidth ||
          instruction.width > result.aggregateWidth - instruction.operand ||
          instruction.auxiliary != 0)
        return false;
      ++depth;
      break;
    case OBELISK_RT_RANDOM_PUSH_CAPTURE_V1:
      if (instruction.operand >= encodedCaptureCount ||
          instruction.auxiliary != 0)
        return false;
      result.captureWidths[instruction.operand] = std::max(
          result.captureWidths[instruction.operand], instruction.width);
      ++depth;
      break;
    case OBELISK_RT_RANDOM_PUSH_CONSTRAINT_ENABLED_V1:
      if (instruction.width != 1 || instruction.flags != 0 ||
          instruction.operand >= 64 || instruction.auxiliary != 0)
        return false;
      ++depth;
      break;
    case OBELISK_RT_RANDOM_STORE_VALUE_V1:
      if (depth == 0 || instruction.flags != 0 || instruction.auxiliary != 0 ||
          instruction.operand != temporaryWidths.size() ||
          instruction.width != stackWidths.back())
        return false;
      temporaryWidths.push_back(instruction.width);
      break;
    case OBELISK_RT_RANDOM_LOAD_VALUE_V1:
      if (instruction.flags != 0 || instruction.auxiliary != 0 ||
          instruction.operand >= temporaryWidths.size() ||
          instruction.width != temporaryWidths[instruction.operand])
        return false;
      ++depth;
      break;
    case OBELISK_RT_RANDOM_PUSH_LITERAL_V1: {
      uint64_t wordCount = (static_cast<uint64_t>(instruction.width) + 63) / 64;
      if (instruction.auxiliary > literalWordCount ||
          wordCount > literalWordCount - instruction.auxiliary)
        return false;
      instruction.literal = WideValue(instruction.width);
      for (uint64_t word = 0; word != wordCount; ++word) {
        uint64_t poolIndex = instruction.auxiliary + word;
        if (literalUse[poolIndex] != 0)
          return false;
        literalUse[poolIndex] = 1;
        instruction.literal.words[word] =
            read64(bytes + literalOffset + poolIndex * 8);
      }
      uint64_t final = instruction.literal.words.back();
      normalize(instruction.literal);
      if (final != instruction.literal.words.back())
        return false;
      ++depth;
      break;
    }
    case OBELISK_RT_RANDOM_END_HARD_V1:
    case OBELISK_RT_RANDOM_END_SOFT_V1:
      if (instruction.width != 1 || depth != 1 ||
          (instruction.operand != OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1 &&
           instruction.operand >= 64))
        return false;
      if (instruction.opcode == OBELISK_RT_RANDOM_END_HARD_V1) {
        if (instruction.auxiliary != 0)
          return false;
        sawHard = true;
      } else {
        if (instruction.auxiliary >= 64)
          return false;
        softPriorities |= uint64_t{1} << instruction.auxiliary;
        sawSoft = true;
      }
      depth = 0;
      break;
    case OBELISK_RT_RANDOM_END_DIST_GUARD_V1:
      if (!weighted || instruction.width != 1 || instruction.flags != 0 ||
          depth != 1 || instruction.auxiliary != 0 ||
          instruction.operand >= guardSeen.size() ||
          guardSeen[instruction.operand])
        return false;
      guardSeen[instruction.operand] = true;
      depth = 0;
      break;
    case OBELISK_RT_RANDOM_SELECT_V1:
      if (instruction.auxiliary != 0 || depth < 3)
        return false;
      depth -= 2;
      break;
    default:
      if (instruction.auxiliary != 0)
        return false;
      if (isUnary(instruction.opcode)) {
        if (depth < 1)
          return false;
      } else if (isBinary(instruction.opcode)) {
        if (depth < 2)
          return false;
        --depth;
      } else {
        return false;
      }
      break;
    }
    stackWidths.resize(depth);
    if (depth != 0)
      stackWidths.back() = instruction.width;
    maxDepth = std::max(maxDepth, depth);
    result.instructions.push_back(std::move(instruction));
  }
  unsigned softCount = 0;
  if (softPriorities != 0) {
    softCount = 64;
    while (((softPriorities >> (softCount - 1)) & 1) == 0)
      --softCount;
  }
  uint64_t expectedSoftPriorities =
      softCount == 64 ? UINT64_MAX : (uint64_t{1} << softCount) - 1;
  if (depth != 0 || !sawHard || sawSoft != result.hasSoft ||
      softPriorities != expectedSoftPriorities ||
      std::any_of(result.captureWidths.begin(), result.captureWidths.end(),
                  [](uint32_t width) { return width == 0; }))
    return false;

  for (const auto &group : weightedGroups)
    if (group.softPriority != UINT32_MAX &&
        !(softPriorities & (uint64_t{1} << group.softPriority)))
      return false;

  if (encodedOrder) {
    const uint8_t *cursor = bytes + solveRecordsOffset;
    bool sawFunctionEdge = false;
    for (uint32_t index = 0; index != solveEdgeCount; ++index) {
      SolveBeforeEdge edge{WideValue(result.aggregateWidth),
                           WideValue(result.aggregateWidth), 0};
      for (WideValue *mask : {&edge.beforeMask, &edge.afterMask})
        for (uint64_t &word : mask->words) {
          word = read64(cursor);
          cursor += 8;
        }
      edge.constraintBlock = read32(cursor);
      uint32_t reserved = read32(cursor + 4);
      edge.functionPriority =
          (reserved & OBELISK_RT_RANDOM_SOLVE_EDGE_FUNCTION_PRIORITY) != 0;
      sawFunctionEdge |= edge.functionPriority;
      cursor += OBELISK_RT_RANDOM_SOLVE_EDGE_TAIL_SIZE_V2;
      uint64_t upperMask = highMask(result.aggregateWidth);
      if (!truth(edge.beforeMask) || !truth(edge.afterMask) ||
          (edge.beforeMask.words.back() & ~upperMask) != 0 ||
          (edge.afterMask.words.back() & ~upperMask) != 0 ||
          intersects(edge.beforeMask, edge.afterMask) ||
          (reserved &
           ~(encodedPriority ? OBELISK_RT_RANDOM_SOLVE_EDGE_FUNCTION_PRIORITY
                             : 0u)) != 0 ||
          (edge.constraintBlock != OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1 &&
           edge.constraintBlock >= 64))
        return false;
      result.solveEdges.push_back(std::move(edge));
    }
    WideValue all(result.aggregateWidth);
    std::fill(all.words.begin(), all.words.end(), UINT64_MAX);
    normalize(all);
    std::vector<WideValue> validationLayers;
    if ((encodedPriority && !sawFunctionEdge && !sourceFunctionArguments) ||
        !buildSolveBeforeLayers(result.solveEdges, all, 0, validationLayers))
      return false;
  }

  if (encodedDomains) {
    if (domainGroupCount == 0 || domainRecordCount == 0 ||
        domainGroupCount > domainRecordCount)
      return false;
    result.domainGroups.resize(domainGroupCount);
    std::vector<uint8_t> seen(domainGroupCount, 0);
    WideValue encodedMask(result.aggregateWidth);
    const uint8_t *cursor = bytes + domainRecordsOffset;
    for (uint32_t index = 0; index != domainRecordCount; ++index) {
      obelisk::random::DomainRecord decoded;
      if (!obelisk::random::readDomainRecord(
              cursor, result.aggregateWidth, true, bytes + literalOffset,
              literalWordCount, literalUse.data(), decoded) ||
          decoded.group >= domainGroupCount)
        return false;
      cursor += OBELISK_RT_RANDOM_DOMAIN_RECORD_SIZE;
      uint32_t groupIndex = decoded.group;
      uint32_t targetOffset = decoded.targetOffset;
      uint32_t width = decoded.width;
      // Wide semantic domains are sampled by the compiled exact plan. The
      // legacy bounded radix traversal only stores narrow domain indices.
      if (width > 64 &&
          (!(flags & OBELISK_RT_RANDOM_PROGRAM_HAS_SAMPLER_PLAN) ||
           !(flags & (OBELISK_RT_RANDOM_PROGRAM_SMT_UNIFORM |
                      OBELISK_RT_RANDOM_PROGRAM_SMT_WEIGHTED))))
        return false;
      DomainGroup &group = result.domainGroups[groupIndex];
      if (seen[groupIndex] &&
          (group.targetOffset != targetOffset || group.width != width))
        return false;
      if (!seen[groupIndex]) {
        seen[groupIndex] = 1;
        group.targetOffset = targetOffset;
        group.width = width;
        for (uint32_t bitIndex = 0; bitIndex != width; ++bitIndex) {
          if (bit(encodedMask, targetOffset + bitIndex))
            return false;
          setBit(encodedMask, targetOffset + bitIndex, true);
        }
      }
      for (const DomainPattern &other : group.patterns)
        if (obelisk::random::domainPatternsOverlap(decoded, other.bits))
          return false;
      uint64_t freeMask = 0, cardinality = 0;
      if (width <= 64) {
        freeMask = widthMask(width) & ~decoded.maskWord(0);
        unsigned freeBits = countBits(freeMask);
        cardinality = uint64_t{1} << freeBits;
        uint64_t updated = 0;
        bool overflow =
            __builtin_add_overflow(group.cardinality, cardinality, &updated);
        if (group.fullCardinality || (overflow && updated != 0))
          return false;
        group.fullCardinality = overflow;
        group.cardinality = updated;
      }
      group.patterns.push_back({decoded, freeMask, cardinality});
    }
    if (std::any_of(seen.begin(), seen.end(),
                    [](uint8_t value) { return value == 0; }))
      return false;
  }
  if (std::any_of(literalUse.begin(), literalUse.end(),
                  [](uint8_t used) { return used != 1; }))
    return false;
  result.maxDepth = static_cast<uint32_t>(maxDepth);
  result.temporaryCount = static_cast<uint32_t>(temporaryWidths.size());
  uint64_t requiredCaptureWords = 0;
  for (size_t index = 0; index != result.captureWidths.size(); ++index) {
    uint32_t logicalWidth = result.captureWidths[index];
    uint32_t storageWidth = captureWidths[index];
    uint64_t logicalWords = (static_cast<uint64_t>(logicalWidth) + 63) / 64;
    uint64_t storageWords = (static_cast<uint64_t>(storageWidth) + 63) / 64;
    if (storageWidth < logicalWidth || storageWords != logicalWords)
      return false;
    result.captureOffsets[index] = requiredCaptureWords;
    if (storageWords > UINT64_MAX - requiredCaptureWords)
      return false;
    requiredCaptureWords += storageWords;
  }
  return requiredCaptureWords == captureWordCount;
}

bool compare(const WideValue &lhs, const WideValue &rhs, bool isSigned,
             uint8_t opcode) {
  uint32_t width = std::max(lhs.width, rhs.width);
  WideValue left = resize(lhs, width, isSigned);
  WideValue right = resize(rhs, width, isSigned);
  int relation = 0;
  if (isSigned && bit(left, width - 1) != bit(right, width - 1))
    relation = bit(left, width - 1) ? -1 : 1;
  else
    relation = compareUnsigned(left, right);
  switch (opcode) {
  case OBELISK_RT_RANDOM_EQ_V1:
    return relation == 0;
  case OBELISK_RT_RANDOM_NE_V1:
    return relation != 0;
  case OBELISK_RT_RANDOM_GE_V1:
    return relation >= 0;
  case OBELISK_RT_RANDOM_GT_V1:
    return relation > 0;
  case OBELISK_RT_RANDOM_LE_V1:
    return relation <= 0;
  default:
    return relation < 0;
  }
}

bool evaluate(const WideProgram &program, const WideValue &assignment,
              const uint64_t *captureWords, uint64_t constraintMask,
              std::vector<WideValue> &stack,
              std::vector<WideValue> &temporaries, bool &hard,
              std::array<uint8_t, 64> &soft) {
  stack.clear();
  temporaries.clear();
  hard = true;
  soft.fill(1);
  for (const WideInstruction &instruction : program.instructions) {
    bool signedOperation =
        (instruction.flags & OBELISK_RT_RANDOM_INSTRUCTION_SIGNED) != 0;
    if (instruction.opcode == OBELISK_RT_RANDOM_PUSH_VARIABLE_V1) {
      stack.push_back(extract(assignment.words.data(), assignment.words.size(),
                              instruction.operand, instruction.width));
      continue;
    }
    if (instruction.opcode == OBELISK_RT_RANDOM_PUSH_CAPTURE_V1) {
      stack.push_back(
          extract(captureWords + program.captureOffsets[instruction.operand],
                  (static_cast<uint64_t>(instruction.width) + 63) / 64, 0,
                  instruction.width));
      continue;
    }
    if (instruction.opcode == OBELISK_RT_RANDOM_PUSH_LITERAL_V1) {
      stack.push_back(instruction.literal);
      continue;
    }
    if (instruction.opcode == OBELISK_RT_RANDOM_STORE_VALUE_V1) {
      temporaries.push_back(stack.back());
      continue;
    }
    if (instruction.opcode == OBELISK_RT_RANDOM_LOAD_VALUE_V1) {
      stack.push_back(temporaries[instruction.operand]);
      continue;
    }
    if (instruction.opcode == OBELISK_RT_RANDOM_PUSH_CONSTRAINT_ENABLED_V1) {
      WideValue enabled(1);
      enabled.words[0] = ((constraintMask >> instruction.operand) & 1) == 0;
      stack.push_back(std::move(enabled));
      continue;
    }
    if (instruction.opcode == OBELISK_RT_RANDOM_END_HARD_V1 ||
        instruction.opcode == OBELISK_RT_RANDOM_END_SOFT_V1) {
      bool satisfied = truth(stack.back());
      stack.pop_back();
      bool enabled =
          instruction.operand == OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1 ||
          ((constraintMask >> instruction.operand) & 1) == 0;
      satisfied |= !enabled;
      if (instruction.opcode == OBELISK_RT_RANDOM_END_HARD_V1)
        hard &= satisfied;
      else
        soft[instruction.auxiliary] &= satisfied;
      continue;
    }
    if (instruction.opcode == OBELISK_RT_RANDOM_END_DIST_GUARD_V1) {
      stack.pop_back();
      continue;
    }
    if (instruction.opcode == OBELISK_RT_RANDOM_SELECT_V1) {
      WideValue falseValue = std::move(stack.back());
      stack.pop_back();
      WideValue trueValue = std::move(stack.back());
      stack.pop_back();
      WideValue condition = std::move(stack.back());
      stack.pop_back();
      stack.push_back(resize(truth(condition) ? trueValue : falseValue,
                             instruction.width, signedOperation));
      continue;
    }
    if (isUnary(instruction.opcode)) {
      WideValue input = std::move(stack.back());
      stack.pop_back();
      WideValue result;
      switch (instruction.opcode) {
      case OBELISK_RT_RANDOM_CAST_V1:
      case OBELISK_RT_RANDOM_POS_V1:
        result = resize(input, instruction.width, signedOperation);
        break;
      case OBELISK_RT_RANDOM_NEG_V1:
        result = negate(resize(input, instruction.width, signedOperation));
        break;
      case OBELISK_RT_RANDOM_BIT_NOT_V1:
        result = resize(input, instruction.width, signedOperation);
        for (uint64_t &word : result.words)
          word = ~word;
        normalize(result);
        break;
      case OBELISK_RT_RANDOM_REDUCE_AND_V1: {
        bool all = input.words.back() == highMask(input.width);
        for (size_t index = 0; all && index + 1 < input.words.size(); ++index)
          all = input.words[index] == UINT64_MAX;
        result = booleanValue(all, instruction.width);
        break;
      }
      case OBELISK_RT_RANDOM_REDUCE_OR_V1:
        result = booleanValue(truth(input), instruction.width);
        break;
      case OBELISK_RT_RANDOM_REDUCE_XOR_V1:
      case OBELISK_RT_RANDOM_REDUCE_XNOR_V1: {
        unsigned parity = 0;
        for (uint64_t word : input.words)
          parity ^= static_cast<unsigned>(__builtin_parityll(word));
        if (instruction.opcode == OBELISK_RT_RANDOM_REDUCE_XNOR_V1)
          parity ^= 1;
        result = booleanValue(parity != 0, instruction.width);
        break;
      }
      case OBELISK_RT_RANDOM_REDUCE_NAND_V1: {
        bool all = input.words.back() == highMask(input.width);
        for (size_t index = 0; all && index + 1 < input.words.size(); ++index)
          all = input.words[index] == UINT64_MAX;
        result = booleanValue(!all, instruction.width);
        break;
      }
      case OBELISK_RT_RANDOM_COUNT_ONES_V1: {
        uint64_t count = 0;
        for (uint64_t word : input.words)
          count += static_cast<uint64_t>(__builtin_popcountll(word));
        result = WideValue(instruction.width);
        result.words[0] = count;
        normalize(result);
        break;
      }
      case OBELISK_RT_RANDOM_CLOG2_V1: {
        uint64_t highest = 0;
        uint64_t count = 0;
        for (size_t index = 0; index != input.words.size(); ++index) {
          uint64_t word = input.words[index];
          count += static_cast<uint64_t>(__builtin_popcountll(word));
          if (word != 0)
            highest = index * 64 + 63 - __builtin_clzll(word);
        }
        result = WideValue(instruction.width);
        result.words[0] = highest + (count > 1);
        normalize(result);
        break;
      }
      default:
        result = booleanValue(!truth(input), instruction.width);
        break;
      }
      stack.push_back(std::move(result));
      continue;
    }

    WideValue rhs = std::move(stack.back());
    stack.pop_back();
    WideValue lhs = std::move(stack.back());
    stack.pop_back();
    WideValue result;
    if (instruction.opcode >= OBELISK_RT_RANDOM_EQ_V1 &&
        instruction.opcode <= OBELISK_RT_RANDOM_LT_V1) {
      result =
          booleanValue(compare(lhs, rhs, signedOperation, instruction.opcode),
                       instruction.width);
    } else if (instruction.opcode >= OBELISK_RT_RANDOM_LOGICAL_AND_V1 &&
               instruction.opcode <= OBELISK_RT_RANDOM_LOGICAL_EQUIV_V1) {
      bool left = truth(lhs), right = truth(rhs), value = false;
      switch (instruction.opcode) {
      case OBELISK_RT_RANDOM_LOGICAL_AND_V1:
        value = left && right;
        break;
      case OBELISK_RT_RANDOM_LOGICAL_OR_V1:
        value = left || right;
        break;
      case OBELISK_RT_RANDOM_LOGICAL_IMPLIES_V1:
        value = !left || right;
        break;
      default:
        value = left == right;
        break;
      }
      result = booleanValue(value, instruction.width);
    } else {
      WideValue left = resize(lhs, instruction.width, signedOperation);
      WideValue right = resize(rhs, instruction.width, signedOperation);
      switch (instruction.opcode) {
      case OBELISK_RT_RANDOM_ADD_V1:
        result = add(left, right, instruction.width);
        break;
      case OBELISK_RT_RANDOM_SUB_V1:
        result = subtract(left, right, instruction.width);
        break;
      case OBELISK_RT_RANDOM_MUL_V1:
        result = multiply(left, right, instruction.width);
        break;
      case OBELISK_RT_RANDOM_BIT_AND_V1:
      case OBELISK_RT_RANDOM_BIT_OR_V1:
      case OBELISK_RT_RANDOM_BIT_XOR_V1:
      case OBELISK_RT_RANDOM_BIT_XNOR_V1:
        result = bitwise(left, right, instruction.width, instruction.opcode);
        break;
      case OBELISK_RT_RANDOM_DIV_V1:
      case OBELISK_RT_RANDOM_MOD_V1: {
        if (!truth(right))
          return false;
        bool leftNegative = signedOperation && bit(left, left.width - 1);
        bool rightNegative = signedOperation && bit(right, right.width - 1);
        WideValue leftMagnitude = leftNegative ? negate(left) : left;
        WideValue rightMagnitude = rightNegative ? negate(right) : right;
        WideValue quotient, remainder;
        unsignedDivMod(leftMagnitude, rightMagnitude, quotient, remainder);
        if (instruction.opcode == OBELISK_RT_RANDOM_DIV_V1) {
          result = leftNegative != rightNegative ? negate(quotient) : quotient;
        } else {
          result = leftNegative ? negate(remainder) : remainder;
        }
        break;
      }
      case OBELISK_RT_RANDOM_SHIFT_LEFT_V1:
      case OBELISK_RT_RANDOM_SHIFT_RIGHT_V1:
      case OBELISK_RT_RANDOM_SHIFT_RIGHT_ARITH_V1: {
        uint32_t amount = 0;
        if (!shiftAmount(rhs, instruction.width, amount)) {
          bool fill =
              instruction.opcode == OBELISK_RT_RANDOM_SHIFT_RIGHT_ARITH_V1 &&
              bit(left, instruction.width - 1);
          result = WideValue(instruction.width);
          if (fill) {
            std::fill(result.words.begin(), result.words.end(), UINT64_MAX);
            normalize(result);
          }
        } else if (instruction.opcode == OBELISK_RT_RANDOM_SHIFT_LEFT_V1) {
          result = shiftLeft(left, instruction.width, amount);
        } else {
          result = shiftRight(left, instruction.width, amount,
                              instruction.opcode ==
                                  OBELISK_RT_RANDOM_SHIFT_RIGHT_ARITH_V1);
        }
        break;
      }
      case OBELISK_RT_RANDOM_POWER_V1:
        result = power(left, rhs, instruction.width);
        break;
      default:
        return false;
      }
    }
    stack.push_back(std::move(result));
  }
  return stack.empty();
}

bool allSoft(const std::array<uint8_t, 64> &soft) {
  return std::all_of(soft.begin(), soft.end(),
                     [](uint8_t value) { return value != 0; });
}

int compareSoft(const std::array<uint8_t, 64> &lhs,
                const std::array<uint8_t, 64> &rhs) {
  for (size_t index = lhs.size(); index != 0; --index) {
    if (lhs[index - 1] == rhs[index - 1])
      continue;
    return lhs[index - 1] > rhs[index - 1] ? 1 : -1;
  }
  return 0;
}

bool advanceMutable(WideValue &candidate, const WideValue &mutableMask) {
  for (uint32_t index = 0; index != candidate.width; ++index) {
    if (!bit(mutableMask, index))
      continue;
    bool current = bit(candidate, index);
    setBit(candidate, index, !current);
    if (!current)
      return true;
  }
  return false;
}

} // namespace

extern "C" obelisk_rt_status obelisk_rt_v1_random_cycle_sample(
    obelisk_rt_context *context, const uint8_t *programBytes,
    uint64_t programSize, const uint64_t *startWords,
    const uint64_t *mutableMaskWords, uint64_t assignmentWordCount,
    uint64_t constraintMask, uint64_t rngState, uint64_t rngIncrement,
    uint64_t key, uint64_t position, obelisk_rt_string_v1 domain,
    uint32_t fieldOffset, uint32_t fieldWidth, const uint64_t *captureWords,
    uint64_t captureWordCount, const uint32_t *captureWidths,
    uint64_t captureCount, uint64_t *outAssignmentWords, uint32_t *outSuccess,
    uint64_t *outRngState, uint64_t *outKey, uint64_t *outPosition,
    obelisk_rt_string_v1 *outDomain) {
  constexpr uint64_t maxWords =
      std::numeric_limits<size_t>::max() / sizeof(uint64_t);
  if (!context || !programBytes || !startWords || !mutableMaskWords ||
      !outAssignmentWords || !outSuccess || !outRngState || !outKey ||
      !outPosition || !outDomain || (captureWordCount && !captureWords) ||
      (captureCount && !captureWidths) || programSize > SIZE_MAX ||
      assignmentWordCount > maxWords || captureWordCount > maxWords ||
      captureCount > UINT32_MAX || !(rngIncrement & 1) || fieldWidth == 0 ||
      fieldWidth > 32)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outSuccess = 0;
  *outRngState = rngState;
  *outKey = key;
  *outPosition = position;
  *outDomain = domain;
  OBELISK_RT_TRY {
    WideProgram program;
    if (!parseWideProgram(programBytes, programSize, program, captureWidths,
                          captureCount, captureWordCount) ||
        assignmentWordCount != (uint64_t(program.aggregateWidth) + 63) / 64 ||
        fieldOffset > program.aggregateWidth ||
        fieldWidth > program.aggregateWidth - fieldOffset)
      return OBELISK_RT_INVALID_ARGUMENT;
    uint64_t padding = ~highMask(program.aggregateWidth);
    if ((startWords[assignmentWordCount - 1] & padding) ||
        (mutableMaskWords[assignmentWordCount - 1] & padding))
      return OBELISK_RT_INVALID_ARGUMENT;
    for (size_t index = 0; index != program.captureWidths.size(); ++index) {
      uint32_t width = program.captureWidths[index];
      size_t last =
          program.captureOffsets[index] + (uint64_t(width) + 63) / 64 - 1;
      if (captureWords[last] & ~highMask(width))
        return OBELISK_RT_INVALID_ARGUMENT;
    }
    for (const DomainGroup &group : program.domainGroups) {
      WideValue overlap = extract(mutableMaskWords, assignmentWordCount,
                                  group.targetOffset, group.width);
      if (truth(overlap))
        for (size_t word = 0; word != overlap.words.size(); ++word)
          if (overlap.words[word] != (word + 1 == overlap.words.size()
                                          ? highMask(group.width)
                                          : UINT64_MAX))
            return OBELISK_RT_INVALID_ARGUMENT;
    }
    std::copy_n(startWords, assignmentWordCount, outAssignmentWords);
    ManagedExecutionScope execution(context);
    if (execution.getStatus() != OBELISK_RT_OK)
      return execution.getStatus();
    obelisk_rt_status status = obelisk_rt_validate_string(context, domain);
    if (status != OBELISK_RT_OK)
      return status;
    char scratch[8];
    const char *oldBytes = nullptr;
    uint64_t oldSize = 0;
    status = obelisk_rt_v1_string_view(domain, scratch, &oldBytes, &oldSize);
    if (status != OBELISK_RT_OK)
      return status;
    obelisk::runtime::CyclicSampleRequest request{
        fieldOffset,
        fieldWidth,
        key,
        position,
        reinterpret_cast<const uint8_t *>(oldBytes),
        static_cast<size_t>(oldSize),
        {}};
    obelisk_rt_random_state_v1 state{rngState, rngIncrement};
    std::vector<uint64_t> assignment(assignmentWordCount);
    auto result = obelisk::runtime::sampleRandomPlan(
        programBytes, programSize, startWords, mutableMaskWords,
        assignmentWordCount, constraintMask, captureWords, captureWidths,
        captureCount, assignment.data(), &state, nullptr, 0, &request);
    if (result == obelisk::runtime::SamplerResult::Unsatisfiable)
      return OBELISK_RT_OK;
    if (result != obelisk::runtime::SamplerResult::Satisfiable)
      return OBELISK_RT_INVALID_ARGUMENT;
    obelisk_rt_string_v1 nextDomain = domain;
    if (oldSize != request.domain.size() ||
        !std::equal(request.domain.begin(), request.domain.end(),
                    reinterpret_cast<const uint8_t *>(oldBytes))) {
      // Comparison is complete before the allocating safepoint. No borrowed
      // view of the previous managed string survives that allocation.
      status = obelisk_rt_v1_string_create(
          execution.getLane(),
          reinterpret_cast<const char *>(request.domain.data()),
          request.domain.size(), &nextDomain);
      if (status != OBELISK_RT_OK)
        return status;
    }
    std::copy(assignment.begin(), assignment.end(), outAssignmentWords);
    *outSuccess = 1;
    *outRngState = state.state;
    *outKey = request.key;
    *outPosition = request.position;
    *outDomain = nextDomain;
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH(const std::length_error &) {
    return OBELISK_RT_OUT_OF_RESOURCES;
  }
  OBELISK_RT_CATCH_ALL { return OBELISK_RT_INVALID_ARGUMENT; }
}

extern "C" obelisk_rt_status obelisk_rt_v1_random_solve_wide_modes_state(
    obelisk_rt_context *context, const uint8_t *programBytes,
    uint64_t programSize, const uint64_t *startWords,
    const uint64_t *mutableMaskWords, uint64_t assignmentWordCount,
    uint64_t constraintMask, uint64_t maxAttempts, uint64_t rngState,
    uint64_t rngIncrement, const uint64_t *captureWords,
    uint64_t captureWordCount, const uint32_t *captureWidths,
    uint64_t captureCount, uint64_t *outAssignmentWords, uint32_t *outSuccess,
    uint64_t *outRngState) {
  constexpr uint64_t maxAddressableWords =
      std::numeric_limits<size_t>::max() / sizeof(uint64_t);
  if (!context || !programBytes || !startWords || !mutableMaskWords ||
      !outAssignmentWords || !outSuccess || !outRngState ||
      (captureWordCount != 0 && !captureWords) ||
      (captureCount != 0 && !captureWidths) ||
      programSize > std::numeric_limits<size_t>::max() ||
      assignmentWordCount > maxAddressableWords ||
      captureWordCount > maxAddressableWords || (rngIncrement & 1) == 0)
    return OBELISK_RT_INVALID_ARGUMENT;
  *outSuccess = 0;
  *outRngState = rngState;
  OBELISK_RT_TRY {
    WideProgram program;
    if (!parseWideProgram(programBytes, static_cast<size_t>(programSize),
                          program, captureWidths, captureCount,
                          captureWordCount) ||
        assignmentWordCount !=
            (static_cast<uint64_t>(program.aggregateWidth) + 63) / 64)
      return OBELISK_RT_INVALID_ARGUMENT;
    uint64_t aggregateHighMask = highMask(program.aggregateWidth);
    if ((startWords[assignmentWordCount - 1] & ~aggregateHighMask) != 0 ||
        (mutableMaskWords[assignmentWordCount - 1] & ~aggregateHighMask) != 0)
      return OBELISK_RT_INVALID_ARGUMENT;
    for (size_t index = 0; index != program.captureWidths.size(); ++index) {
      uint32_t width = program.captureWidths[index];
      uint64_t words = (static_cast<uint64_t>(width) + 63) / 64;
      if ((captureWords[program.captureOffsets[index] + words - 1] &
           ~highMask(width)) != 0)
        return OBELISK_RT_INVALID_ARGUMENT;
    }
    std::fill_n(outAssignmentWords, static_cast<size_t>(assignmentWordCount),
                0);
    WideValue start(program.aggregateWidth);
    WideValue mutableMask(program.aggregateWidth);
    std::copy_n(startWords, start.words.size(), start.words.begin());
    std::copy_n(mutableMaskWords, mutableMask.words.size(),
                mutableMask.words.begin());
    normalize(start);
    normalize(mutableMask);

    obelisk_rt_random_state_v1 randomState{rngState, rngIncrement};
    if (read32(programBytes + 20) & (OBELISK_RT_RANDOM_PROGRAM_SMT_UNIFORM |
                                     OBELISK_RT_RANDOM_PROGRAM_SMT_WEIGHTED)) {
      for (const DomainGroup &group : program.domainGroups) {
        WideValue overlap =
            extract(mutableMask.words.data(), mutableMask.words.size(),
                    group.targetOffset, group.width);
        if (truth(overlap))
          for (size_t word = 0; word != overlap.words.size(); ++word)
            if (overlap.words[word] != (word + 1 == overlap.words.size()
                                            ? highMask(group.width)
                                            : UINT64_MAX))
              return OBELISK_RT_INVALID_ARGUMENT;
      }
      if (maxAttempts == 0)
        return OBELISK_RT_OK;
      std::vector<WideValue> solveLayers;
      if (!buildSolveBeforeLayers(program.solveEdges, mutableMask,
                                  constraintMask, solveLayers))
        return OBELISK_RT_INVALID_ARGUMENT;
      std::vector<uint64_t> orderedMasks;
      for (const WideValue &layer : solveLayers)
        orderedMasks.insert(orderedMasks.end(), layer.words.begin(),
                            layer.words.end());
      auto result = obelisk::runtime::sampleRandomPlan(
          programBytes, static_cast<size_t>(programSize), startWords,
          mutableMaskWords, assignmentWordCount, constraintMask, captureWords,
          captureWidths, captureCount, outAssignmentWords, &randomState,
          orderedMasks.data(), solveLayers.size());
      if (result == obelisk::runtime::SamplerResult::Satisfiable)
        *outSuccess = 1;
      else
        std::fill_n(outAssignmentWords,
                    static_cast<size_t>(assignmentWordCount), 0);
      *outRngState = randomState.state;
      return OBELISK_RT_OK;
    }
    WideValue ordinaryMutableMask = mutableMask;
    std::vector<uint64_t> domainIndices(program.domainGroups.size(), 0);
    auto groupIndexForField = [](const DomainGroup &group, uint64_t field,
                                 uint64_t &groupIndex) {
      uint64_t base = 0;
      for (const DomainPattern &pattern : group.patterns) {
        if ((field & pattern.bits.maskWord(0)) == pattern.bits.valueWord(0)) {
          groupIndex = base + compressBits(field, pattern.freeMask);
          return true;
        }
        base += pattern.cardinality;
      }
      return false;
    };
    auto materializeGroup = [](const DomainGroup &group, uint64_t groupIndex) {
      uint64_t base = 0;
      for (const DomainPattern &pattern : group.patterns) {
        if (groupIndex - base < pattern.cardinality)
          return pattern.bits.valueWord(0) |
                 expandBits(groupIndex - base, pattern.freeMask);
        base += pattern.cardinality;
      }
      return uint64_t{0};
    };
    for (size_t index = 0; index != program.domainGroups.size(); ++index) {
      const DomainGroup &group = program.domainGroups[index];
      WideValue mutableField =
          extract(mutableMask.words.data(), mutableMask.words.size(),
                  group.targetOffset, group.width);
      uint64_t overlap = mutableField.words.front();
      uint64_t fieldMask = widthMask(group.width);
      if (overlap != 0 && overlap != fieldMask)
        return OBELISK_RT_INVALID_ARGUMENT;
      uint64_t field = extract(start.words.data(), start.words.size(),
                               group.targetOffset, group.width)
                           .words.front();
      uint64_t groupIndex = 0;
      bool legal = groupIndexForField(group, field, groupIndex);
      if (overlap == 0)
        continue;
      insert(ordinaryMutableMask, group.targetOffset, group.width, 0);
      if (!legal) {
        if (group.fullCardinality)
          return OBELISK_RT_INVALID_ARGUMENT;
        obelisk_rt_status status = obelisk_rt_v1_random_state_bounded(
            &randomState, group.cardinality, &groupIndex);
        if (status != OBELISK_RT_OK)
          return status;
      }
      domainIndices[index] = groupIndex;
      insert(start, group.targetOffset, group.width,
             materializeGroup(group, groupIndex));
    }

    unsigned ordinaryMutableBits = 0;
    for (uint64_t word : ordinaryMutableMask.words)
      ordinaryMutableBits += static_cast<unsigned>(__builtin_popcountll(word));
    bool finiteDomain = ordinaryMutableBits < 64;
    uint64_t domainSize = finiteDomain ? uint64_t{1} << ordinaryMutableBits : 0;
    for (const DomainGroup &group : program.domainGroups) {
      WideValue mutableField =
          extract(mutableMask.words.data(), mutableMask.words.size(),
                  group.targetOffset, group.width);
      if (!truth(mutableField))
        continue;
      uint64_t updated = 0;
      if (!finiteDomain || group.fullCardinality ||
          __builtin_mul_overflow(domainSize, group.cardinality, &updated)) {
        finiteDomain = false;
        domainSize = 0;
      } else {
        domainSize = updated;
      }
    }
    uint64_t attempts =
        finiteDomain ? std::min(maxAttempts, domainSize) : maxAttempts;
    bool complete = finiteDomain && attempts == domainSize;

    WideValue candidate = start;
    WideValue best(program.aggregateWidth);
    bool hasBest = false;
    std::array<uint8_t, 64> bestSoft{};
    std::array<uint8_t, 64> soft{};
    std::vector<WideValue> stack;
    stack.reserve(program.maxDepth);
    std::vector<WideValue> temporaries;
    temporaries.reserve(program.temporaryCount);
    for (uint64_t attempt = 0; attempt != attempts; ++attempt) {
      bool hard = false;
      if (!evaluate(program, candidate, captureWords, constraintMask, stack,
                    temporaries, hard, soft))
        return OBELISK_RT_INVALID_ARGUMENT;
      if (hard && (!program.hasSoft || allSoft(soft))) {
        std::copy(candidate.words.begin(), candidate.words.end(),
                  outAssignmentWords);
        *outSuccess = 1;
        *outRngState = randomState.state;
        return OBELISK_RT_OK;
      }
      if (hard && program.hasSoft &&
          (!hasBest || compareSoft(soft, bestSoft) > 0)) {
        best = candidate;
        bestSoft = soft;
        hasBest = true;
      }
      if (!advanceMutable(candidate, ordinaryMutableMask)) {
        for (size_t index = 0; index != program.domainGroups.size(); ++index) {
          const DomainGroup &group = program.domainGroups[index];
          WideValue mutableField =
              extract(mutableMask.words.data(), mutableMask.words.size(),
                      group.targetOffset, group.width);
          if (!truth(mutableField))
            continue;
          uint64_t next = domainIndices[index] + 1;
          bool carry =
              group.fullCardinality ? next == 0 : next == group.cardinality;
          domainIndices[index] = carry ? 0 : next;
          insert(candidate, group.targetOffset, group.width,
                 materializeGroup(group, domainIndices[index]));
          if (!carry)
            break;
        }
      }
    }
    if (program.hasSoft && complete && hasBest) {
      std::copy(best.words.begin(), best.words.end(), outAssignmentWords);
      *outSuccess = 1;
    }
    if (!complete && maxAttempts != 0 &&
        (read32(programBytes + 20) &
         OBELISK_RT_RANDOM_PROGRAM_SMT_COMPLETION)) {
      auto result = obelisk::runtime::sampleRandomPlan(
          programBytes, static_cast<size_t>(programSize), startWords,
          mutableMaskWords, assignmentWordCount, constraintMask, captureWords,
          captureWidths, captureCount, outAssignmentWords);
      if (result == obelisk::runtime::SamplerResult::Satisfiable)
        *outSuccess = 1;
      else
        std::fill_n(outAssignmentWords,
                    static_cast<size_t>(assignmentWordCount), 0);
    }
    *outRngState = randomState.state;
    return OBELISK_RT_OK;
  }
  OBELISK_RT_CATCH(const std::bad_alloc &) { return OBELISK_RT_OUT_OF_MEMORY; }
  OBELISK_RT_CATCH_ALL { return OBELISK_RT_INVALID_ARGUMENT; }
}
