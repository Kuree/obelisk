//===- RandomProgramSMT.cpp - Decode random constraints to SMT -*- C++ -*-===//

#include "obelisk/Solver/ConstraintSolver.h"

#ifdef OBELISK_ENABLE_Z3

#include "RandomProgramSMT.h"

#include "obelisk/Runtime/Runtime.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/Verifier.h"

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Support/MathExtras.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace obelisk::solver {
namespace {

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
  uint64_t value = 0;
  for (unsigned index = 0; index != 8; ++index)
    value |= static_cast<uint64_t>(bytes[index]) << (index * 8);
  return value;
}

struct StackValue {
  mlir::Value bits;
  unsigned width;
  std::optional<SMTVariable> directVariable;
  std::optional<SMTVariableEquality> directEquality;
  uint32_t instructionBegin = 0;
  std::optional<SMTVariableDefinition> directDefinition;
  std::optional<SMTVariableCaptureBound> directCaptureBound;
};

mlir::Value constant(mlir::OpBuilder &builder, mlir::Location location,
                     const llvm::APInt &value) {
  return mlir::smt::BVConstantOp::create(builder, location, value);
}

mlir::Value constant(mlir::OpBuilder &builder, mlir::Location location,
                     uint64_t value, unsigned width) {
  return constant(builder, location, llvm::APInt(width, value));
}

mlir::Value resize(mlir::OpBuilder &builder, mlir::Location location,
                   StackValue value, unsigned width, bool signExtend) {
  if (value.width == width)
    return value.bits;
  if (value.width > width)
    return mlir::smt::ExtractOp::create(
        builder, location,
        mlir::smt::BitVectorType::get(builder.getContext(), width), 0,
        value.bits);
  unsigned extension = width - value.width;
  mlir::Value prefix;
  if (signExtend) {
    mlir::Value sign = mlir::smt::ExtractOp::create(
        builder, location,
        mlir::smt::BitVectorType::get(builder.getContext(), 1), value.width - 1,
        value.bits);
    prefix = extension == 1 ? sign
                            : mlir::smt::RepeatOp::create(builder, location,
                                                          extension, sign)
                                  .getResult();
  } else {
    prefix = constant(builder, location, 0, extension);
  }
  return mlir::smt::ConcatOp::create(builder, location, prefix, value.bits);
}

mlir::Value truth(mlir::OpBuilder &builder, mlir::Location location,
                  const StackValue &value) {
  return mlir::smt::DistinctOp::create(
      builder, location, value.bits,
      constant(builder, location, 0, value.width));
}

StackValue booleanValue(
    mlir::OpBuilder &builder, mlir::Location location, mlir::Value predicate,
    uint32_t instructionBegin,
    std::optional<SMTVariableEquality> directEquality = std::nullopt,
    std::optional<SMTVariableDefinition> directDefinition = std::nullopt,
    std::optional<SMTVariableCaptureBound> directCaptureBound = std::nullopt) {
  return {mlir::smt::IteOp::create(builder, location, predicate,
                                   constant(builder, location, 1, 1),
                                   constant(builder, location, 0, 1)),
          1,
          std::nullopt,
          std::move(directEquality),
          instructionBegin,
          std::move(directDefinition),
          std::move(directCaptureBound)};
}

bool containsVariable(mlir::Value expression, const SMTVariable &variable) {
  auto target = mlir::dyn_cast_or_null<mlir::smt::ExtractOp>(
      variable.bits.getDefiningOp());
  if (!target)
    return true;
  llvm::SmallPtrSet<mlir::Operation *, 16> visited;
  std::function<bool(mlir::Value)> visit = [&](mlir::Value value) {
    mlir::Operation *operation = value.getDefiningOp();
    if (!operation || !visited.insert(operation).second)
      return false;
    if (auto extract = mlir::dyn_cast<mlir::smt::ExtractOp>(operation)) {
      unsigned width =
          mlir::cast<mlir::smt::BitVectorType>(value.getType()).getWidth();
      uint64_t low = extract.getLowBit();
      uint64_t high = low + width;
      uint64_t targetLow = target.getLowBit();
      uint64_t targetHigh = targetLow + variable.width;
      if (extract.getInput() == target.getInput() && low < targetHigh &&
          targetLow < high)
        return true;
    }
    return llvm::any_of(operation->getOperands(), visit);
  };
  return visit(expression);
}

bool containsValue(mlir::Value expression, mlir::Value target) {
  llvm::SmallPtrSet<mlir::Operation *, 16> visited;
  std::function<bool(mlir::Value)> visit = [&](mlir::Value value) {
    if (value == target)
      return true;
    mlir::Operation *operation = value.getDefiningOp();
    if (!operation || !visited.insert(operation).second)
      return false;
    return llvm::any_of(operation->getOperands(), visit);
  };
  return visit(expression);
}

bool isUnary(uint8_t opcode) {
  return opcode >= OBELISK_RT_RANDOM_CAST_V1 &&
         opcode <= OBELISK_RT_RANDOM_LOGICAL_NOT_V1;
}

bool isBinary(uint8_t opcode) {
  return (opcode >= OBELISK_RT_RANDOM_ADD_V1 &&
          opcode <= OBELISK_RT_RANDOM_LOGICAL_EQUIV_V1) ||
         (opcode >= OBELISK_RT_RANDOM_DIV_V1 &&
          opcode <= OBELISK_RT_RANDOM_POWER_V1);
}

} // namespace

std::optional<RandomProgramSMT> buildRandomProgramSMT(const uint8_t *program,
                                                      size_t programSize) {
  if (!program || programSize < OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE ||
      read32(program) != OBELISK_RT_RANDOM_PROGRAM_MAGIC)
    return std::nullopt;
  uint16_t version = read16(program + 4);
  bool version1 = version == OBELISK_RT_RANDOM_PROGRAM_VERSION_V1;
  bool version2 = version == OBELISK_RT_RANDOM_PROGRAM_VERSION_V2;
  uint16_t headerSize = read16(program + 6);
  if ((!version1 && !version2) ||
      (version1 && headerSize != OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE) ||
      (version2 &&
       headerSize != OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE_V2) ||
      programSize < headerSize)
    return std::nullopt;
  uint32_t aggregateWidth = read32(program + 8);
  uint32_t instructionCount = read32(program + 12);
  uint32_t captureCount = read32(program + 16);
  uint32_t programFlags = read32(program + 20);
  uint32_t literalWordCount = version2 ? read32(program + 24) : 0;
  if ((version1 && aggregateWidth > 64) ||
      (version2 && read32(program + 28) != 0) ||
      (programFlags & ~(OBELISK_RT_RANDOM_PROGRAM_HAS_SOFT |
                        OBELISK_RT_RANDOM_PROGRAM_HAS_SOLVE_BEFORE |
                        OBELISK_RT_RANDOM_PROGRAM_HAS_DIST |
                        OBELISK_RT_RANDOM_PROGRAM_HAS_DOMAINS)) != 0 ||
      instructionCount > (std::numeric_limits<size_t>::max() -
                          headerSize) /
                             OBELISK_RT_RANDOM_INSTRUCTION_SIZE_V2)
    return std::nullopt;
  size_t instructionBytes = static_cast<size_t>(instructionCount) *
                            OBELISK_RT_RANDOM_INSTRUCTION_SIZE_V2;
  size_t expectedSize = headerSize + instructionBytes;
  if (literalWordCount >
      (std::numeric_limits<size_t>::max() - expectedSize) / sizeof(uint64_t))
    return std::nullopt;
  size_t literalPoolOffset = expectedSize;
  expectedSize += static_cast<size_t>(literalWordCount) * sizeof(uint64_t);

  struct DecodedInstruction {
    uint8_t opcode;
    unsigned width;
    uint8_t flags;
    uint32_t operand;
    uint64_t auxiliary;
    std::optional<llvm::APInt> literal;
  };
  auto decodeInstruction = [&](uint32_t index)
      -> std::optional<DecodedInstruction> {
    const uint8_t *cursor = program + headerSize +
                            static_cast<size_t>(index) *
                                OBELISK_RT_RANDOM_INSTRUCTION_SIZE_V2;
    DecodedInstruction decoded{};
    decoded.opcode = cursor[0];
    if (version1) {
      decoded.width = cursor[1];
      decoded.flags = cursor[2];
      if (cursor[3] != 0)
        return std::nullopt;
      decoded.operand = read32(cursor + 4);
      decoded.auxiliary = read64(cursor + 8);
      if (decoded.opcode == OBELISK_RT_RANDOM_PUSH_LITERAL_V1 &&
          decoded.width != 0)
        decoded.literal = llvm::APInt(decoded.width, decoded.auxiliary);
      return decoded;
    }
    decoded.flags = cursor[1];
    if (read16(cursor + 2) != 0)
      return std::nullopt;
    decoded.width = read32(cursor + 4);
    decoded.operand = read32(cursor + 8);
    uint32_t auxiliary = read32(cursor + 12);
    decoded.auxiliary = auxiliary;
    if (decoded.opcode != OBELISK_RT_RANDOM_PUSH_LITERAL_V1)
      return decoded;
    if (decoded.width == 0)
      return std::nullopt;
    uint64_t wordCount = (static_cast<uint64_t>(decoded.width) + 63) / 64;
    if (auxiliary > literalWordCount ||
        wordCount > literalWordCount - auxiliary)
      return std::nullopt;
    llvm::SmallVector<uint64_t> words;
    words.reserve(wordCount);
    const uint8_t *word = program + literalPoolOffset +
                          static_cast<size_t>(auxiliary) * sizeof(uint64_t);
    for (uint64_t wordIndex = 0; wordIndex != wordCount; ++wordIndex) {
      words.push_back(read64(word));
      word += sizeof(uint64_t);
    }
    unsigned usedHighBits = decoded.width % 64;
    if (usedHighBits != 0 &&
        (words.back() >> usedHighBits) != 0)
      return std::nullopt;
    decoded.literal = llvm::APInt(decoded.width, words);
    return decoded;
  };
  bool hasSolveBefore =
      (programFlags & OBELISK_RT_RANDOM_PROGRAM_HAS_SOLVE_BEFORE) != 0;
  uint32_t solveEdgeCount = 0;
  size_t solveEdgesOffset = 0;
  if (hasSolveBefore) {
    if (expectedSize > std::numeric_limits<size_t>::max() -
                           OBELISK_RT_RANDOM_SOLVE_EDGE_HEADER_SIZE ||
        programSize < expectedSize + OBELISK_RT_RANDOM_SOLVE_EDGE_HEADER_SIZE)
      return std::nullopt;
    solveEdgeCount = read32(program + expectedSize);
    solveEdgesOffset = expectedSize + OBELISK_RT_RANDOM_SOLVE_EDGE_HEADER_SIZE;
    if (solveEdgeCount == 0 ||
        solveEdgeCount >
            (std::numeric_limits<size_t>::max() - solveEdgesOffset) /
                OBELISK_RT_RANDOM_SOLVE_EDGE_SIZE)
      return std::nullopt;
    expectedSize = solveEdgesOffset + static_cast<size_t>(solveEdgeCount) *
                                          OBELISK_RT_RANDOM_SOLVE_EDGE_SIZE;
  }
  bool hasDist = (programFlags & OBELISK_RT_RANDOM_PROGRAM_HAS_DIST) != 0;
  if (hasDist && hasSolveBefore)
    return std::nullopt;
  uint32_t distGroupCount = 0;
  uint32_t distRecordCount = 0;
  size_t distRecordsOffset = 0;
  if (hasDist) {
    if (expectedSize > std::numeric_limits<size_t>::max() -
                           OBELISK_RT_RANDOM_DIST_HEADER_SIZE ||
        programSize < expectedSize + OBELISK_RT_RANDOM_DIST_HEADER_SIZE)
      return std::nullopt;
    distGroupCount = read32(program + expectedSize);
    distRecordCount = read32(program + expectedSize + 4);
    distRecordsOffset = expectedSize + OBELISK_RT_RANDOM_DIST_HEADER_SIZE;
    if (distGroupCount == 0 || distRecordCount == 0 ||
        distRecordCount >
            (std::numeric_limits<size_t>::max() - distRecordsOffset) /
                OBELISK_RT_RANDOM_DIST_RECORD_SIZE)
      return std::nullopt;
    expectedSize = distRecordsOffset + static_cast<size_t>(distRecordCount) *
                                           OBELISK_RT_RANDOM_DIST_RECORD_SIZE;
  }
  bool hasDomains = (programFlags & OBELISK_RT_RANDOM_PROGRAM_HAS_DOMAINS) != 0;
  uint32_t domainGroupCount = 0;
  uint32_t domainRecordCount = 0;
  size_t domainRecordsOffset = 0;
  if (hasDomains) {
    if (expectedSize > std::numeric_limits<size_t>::max() -
                           OBELISK_RT_RANDOM_DOMAIN_HEADER_SIZE ||
        programSize < expectedSize + OBELISK_RT_RANDOM_DOMAIN_HEADER_SIZE)
      return std::nullopt;
    domainGroupCount = read32(program + expectedSize);
    domainRecordCount = read32(program + expectedSize + 4);
    domainRecordsOffset = expectedSize + OBELISK_RT_RANDOM_DOMAIN_HEADER_SIZE;
    if (domainGroupCount == 0 || domainRecordCount == 0 ||
        domainRecordCount >
            (std::numeric_limits<size_t>::max() - domainRecordsOffset) /
                OBELISK_RT_RANDOM_DOMAIN_RECORD_SIZE)
      return std::nullopt;
    expectedSize =
        domainRecordsOffset + static_cast<size_t>(domainRecordCount) *
                                  OBELISK_RT_RANDOM_DOMAIN_RECORD_SIZE;
  }
  if (programSize != expectedSize)
    return std::nullopt;
  if (version2) {
    uint64_t nextLiteralWord = 0;
    for (uint32_t index = 0; index != instructionCount; ++index) {
      std::optional<DecodedInstruction> decoded = decodeInstruction(index);
      if (!decoded)
        return std::nullopt;
      if (decoded->opcode != OBELISK_RT_RANDOM_PUSH_LITERAL_V1)
        continue;
      if (decoded->auxiliary != nextLiteralWord)
        return std::nullopt;
      nextLiteralWord +=
          (static_cast<uint64_t>(decoded->width) + 63) / 64;
    }
    if (nextLiteralWord != literalWordCount)
      return std::nullopt;
  }

  if (hasSolveBefore) {
    // Version 1 solve-order records carry aggregate masks. Version 2 retains
    // that section for compatibility, but a wide ordering needs the decomposed
    // plan representation introduced separately from wide expressions.
    if (aggregateWidth > 64)
      return std::nullopt;
    uint64_t aggregateMask =
        aggregateWidth == 64 ? UINT64_MAX : (uint64_t{1} << aggregateWidth) - 1;
    std::vector<uint64_t> propertyMasks;
    std::vector<std::pair<uint64_t, uint64_t>> solveEdges;
    const uint8_t *edge = program + solveEdgesOffset;
    for (uint32_t index = 0; index != solveEdgeCount; ++index) {
      uint64_t beforeMask = read64(edge);
      uint64_t afterMask = read64(edge + 8);
      uint32_t constraintBlock = read32(edge + 16);
      uint32_t reserved = read32(edge + 20);
      edge += OBELISK_RT_RANDOM_SOLVE_EDGE_SIZE;
      if (beforeMask == 0 || afterMask == 0 ||
          (beforeMask & ~aggregateMask) != 0 ||
          (afterMask & ~aggregateMask) != 0 || (beforeMask & afterMask) != 0 ||
          reserved != 0 ||
          (constraintBlock != OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1 &&
           constraintBlock >= 64))
        return std::nullopt;
      for (uint64_t propertyMask : {beforeMask, afterMask}) {
        for (uint64_t existing : propertyMasks)
          if (existing != propertyMask && (existing & propertyMask) != 0)
            return std::nullopt;
        if (!llvm::is_contained(propertyMasks, propertyMask))
          propertyMasks.push_back(propertyMask);
      }
      solveEdges.emplace_back(beforeMask, afterMask);
    }

    std::vector<uint64_t> remaining = propertyMasks;
    while (!remaining.empty()) {
      std::vector<uint64_t> layer;
      for (uint64_t propertyMask : remaining) {
        bool hasPredecessor = llvm::any_of(solveEdges, [&](const auto &edge) {
          return edge.second == propertyMask &&
                 llvm::is_contained(remaining, edge.first);
        });
        if (!hasPredecessor)
          layer.push_back(propertyMask);
      }
      if (layer.empty())
        return std::nullopt;
      llvm::erase_if(remaining, [&](uint64_t propertyMask) {
        return llvm::is_contained(layer, propertyMask);
      });
    }
  }

  if (hasDist) {
    struct DistGroupInfo {
      bool seen = false;
      uint32_t constraintBlock = 0;
      uint32_t targetOffset = 0;
      uint16_t width = 0;
      uint32_t targetFlags = 0;
    };
    std::vector<DistGroupInfo> groups(distGroupCount);
    const uint8_t *record = program + distRecordsOffset;
    for (uint32_t index = 0; index != distRecordCount; ++index) {
      uint32_t group = read32(record);
      uint32_t constraintBlock = read32(record + 4);
      uint32_t targetOffset = read32(record + 8);
      uint16_t width = read16(record + 12);
      uint16_t reserved = read16(record + 14);
      uint64_t lower = read64(record + 16);
      uint64_t cardinality = read64(record + 24);
      uint64_t coefficient = read64(record + 32);
      uint32_t capture = read32(record + 40);
      uint32_t flags = read32(record + 44);
      record += OBELISK_RT_RANDOM_DIST_RECORD_SIZE;
      uint64_t mask = width == 64 ? UINT64_MAX
                                  : width == 0 ? 0
                                               : (uint64_t{1} << width) - 1;
      bool fullDomain = cardinality == 0 && width == 64 && lower == 0;
      if (group >= distGroupCount || width == 0 || width > 64 ||
          targetOffset > aggregateWidth ||
          width > aggregateWidth - targetOffset || reserved != 0 ||
          lower > mask ||
          (!fullDomain &&
           (cardinality == 0 || cardinality - 1 > mask - lower)) ||
          coefficient == 0 || capture >= captureCount ||
          (flags & ~(OBELISK_RT_RANDOM_DIST_WEIGHT_SIGNED |
                     OBELISK_RT_RANDOM_DIST_TARGET_SIGNED)) != 0 ||
          (constraintBlock != OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1 &&
           constraintBlock >= 64))
        return std::nullopt;
      DistGroupInfo &info = groups[group];
      uint32_t targetFlags = flags & OBELISK_RT_RANDOM_DIST_TARGET_SIGNED;
      if (info.seen &&
          (info.constraintBlock != constraintBlock ||
           info.targetOffset != targetOffset || info.width != width ||
           info.targetFlags != targetFlags))
        return std::nullopt;
      info = {true, constraintBlock, targetOffset, width, targetFlags};
    }
    if (llvm::any_of(groups,
                     [](const DistGroupInfo &info) { return !info.seen; }))
      return std::nullopt;
  }

  struct DomainPattern {
    uint64_t mask;
    uint64_t value;
  };
  struct DomainGroup {
    bool seen = false;
    uint32_t targetOffset = 0;
    uint16_t width = 0;
    std::vector<DomainPattern> patterns;
  };
  std::vector<DomainGroup> domainGroups(domainGroupCount);
  if (hasDomains) {
    const uint8_t *record = program + domainRecordsOffset;
    for (uint32_t index = 0; index != domainRecordCount; ++index) {
      uint32_t group = read32(record);
      uint32_t targetOffset = read32(record + 4);
      uint16_t width = read16(record + 8);
      uint16_t reserved16 = read16(record + 10);
      uint32_t reserved32 = read32(record + 12);
      uint64_t mask = read64(record + 16);
      uint64_t value = read64(record + 24);
      record += OBELISK_RT_RANDOM_DOMAIN_RECORD_SIZE;
      uint64_t fieldMask = width == 64  ? UINT64_MAX
                           : width == 0 ? 0
                                        : (uint64_t{1} << width) - 1;
      if (group >= domainGroupCount || width == 0 || width > 64 ||
          targetOffset > aggregateWidth ||
          width > aggregateWidth - targetOffset || reserved16 != 0 ||
          reserved32 != 0 || (mask & ~fieldMask) != 0 ||
          (value & ~mask) != 0 || (width == 64 && mask == 0))
        return std::nullopt;
      DomainGroup &info = domainGroups[group];
      if (info.seen &&
          (info.targetOffset != targetOffset || info.width != width))
        return std::nullopt;
      info.seen = true;
      info.targetOffset = targetOffset;
      info.width = width;
      for (const DomainPattern &other : info.patterns)
        if (((value ^ other.value) & (mask & other.mask)) == 0)
          return std::nullopt;
      info.patterns.push_back({mask, value});
    }
    if (llvm::any_of(domainGroups,
                     [](const DomainGroup &group) { return !group.seen; }))
      return std::nullopt;
    for (auto [index, group] : llvm::enumerate(domainGroups)) {
      uint64_t groupEnd = static_cast<uint64_t>(group.targetOffset) + group.width;
      for (const DomainGroup &other :
           llvm::ArrayRef(domainGroups).drop_front(index + 1)) {
        uint64_t otherEnd =
            static_cast<uint64_t>(other.targetOffset) + other.width;
        if (group.targetOffset < otherEnd && other.targetOffset < groupEnd)
          return std::nullopt;
      }
    }
  }

  RandomProgramSMT result;
  result.context = std::make_unique<mlir::MLIRContext>();
  result.context->loadDialect<mlir::smt::SMTDialect>();
  mlir::OpBuilder builder(result.context.get());
  mlir::Location location = builder.getUnknownLoc();
  result.module = mlir::ModuleOp::create(location);
  builder.setInsertionPointToStart(result.module->getBody());
  result.solver = mlir::smt::SolverOp::create(
      builder, location, mlir::TypeRange{}, mlir::ValueRange{},
      llvm::ArrayRef<mlir::NamedAttribute>{});
  mlir::Block &body = result.solver.getBodyRegion().emplaceBlock();
  builder.setInsertionPointToStart(&body);

  auto bitVectorType = [&](unsigned width) {
    return mlir::smt::BitVectorType::get(result.context.get(), width);
  };
  mlir::Value assignment = mlir::smt::DeclareFunOp::create(
      builder, location,
      bitVectorType(aggregateWidth == 0 ? 1 : aggregateWidth),
      builder.getStringAttr("assignment"));
  std::vector<unsigned> captureWidths(captureCount, version1 ? 64 : 1);
  if (version2)
    for (uint32_t index = 0; index != instructionCount; ++index) {
      std::optional<DecodedInstruction> decoded = decodeInstruction(index);
      if (!decoded)
        return std::nullopt;
      if (decoded->opcode != OBELISK_RT_RANDOM_PUSH_CAPTURE_V1)
        continue;
      if (decoded->operand >= captureCount || decoded->width == 0)
        return std::nullopt;
      captureWidths[decoded->operand] =
          std::max(captureWidths[decoded->operand], decoded->width);
    }
  std::vector<mlir::Value> captures;
  captures.reserve(captureCount);
  for (uint32_t index = 0; index != captureCount; ++index)
    captures.push_back(mlir::smt::DeclareFunOp::create(
        builder, location, bitVectorType(captureWidths[index]),
        builder.getStringAttr("capture_" + std::to_string(index))));

  std::vector<StackValue> stack;
  stack.reserve(instructionCount);
  mlir::Value hard = mlir::smt::BoolConstantOp::create(builder, location, true);
  bool sawHard = false;
  bool sawSoft = false;
  uint64_t softPriorities = 0;
  for (uint32_t index = 0; index != instructionCount; ++index) {
    std::optional<DecodedInstruction> decoded = decodeInstruction(index);
    if (!decoded)
      return std::nullopt;
    uint8_t opcode = decoded->opcode;
    unsigned width = decoded->width;
    uint8_t flags = decoded->flags;
    uint32_t operand = decoded->operand;
    uint64_t immediate = decoded->auxiliary;
    if (width == 0 || (version1 && width > 64) ||
        (flags & ~OBELISK_RT_RANDOM_INSTRUCTION_SIGNED) != 0)
      return std::nullopt;
    if (version2 && opcode != OBELISK_RT_RANDOM_PUSH_LITERAL_V1 &&
        opcode != OBELISK_RT_RANDOM_END_SOFT_V1 && immediate != 0)
      return std::nullopt;
    bool signedOperation = (flags & OBELISK_RT_RANDOM_INSTRUCTION_SIGNED) != 0;
    if (opcode == OBELISK_RT_RANDOM_PUSH_VARIABLE_V1) {
      if (aggregateWidth == 0 || operand >= aggregateWidth ||
          width > aggregateWidth - operand)
        return std::nullopt;
      mlir::Value bits = mlir::smt::ExtractOp::create(
          builder, location, bitVectorType(width), operand, assignment);
      stack.push_back({bits, width, SMTVariable{operand, width, bits},
                       std::nullopt, index});
      auto found = std::find_if(
          result.variables.begin(), result.variables.end(),
          [&](const SMTVariable &variable) {
            return variable.offset == operand && variable.width == width;
          });
      if (found == result.variables.end())
        result.variables.push_back({operand, width, bits});
      continue;
    }
    if (opcode == OBELISK_RT_RANDOM_PUSH_CAPTURE_V1) {
      if (operand >= captures.size())
        return std::nullopt;
      stack.push_back(
          {mlir::smt::ExtractOp::create(builder, location, bitVectorType(width),
                                        0, captures[operand]),
           width, std::nullopt, std::nullopt, index});
      continue;
    }
    if (opcode == OBELISK_RT_RANDOM_PUSH_LITERAL_V1) {
      if (!decoded->literal)
        return std::nullopt;
      stack.push_back({constant(builder, location, *decoded->literal), width,
                       std::nullopt, std::nullopt, index});
      continue;
    }
    if (opcode == OBELISK_RT_RANDOM_END_HARD_V1 ||
        opcode == OBELISK_RT_RANDOM_END_SOFT_V1) {
      if (width != 1 || stack.size() != 1 ||
          (operand != OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1 &&
           operand >= 64) ||
          (opcode == OBELISK_RT_RANDOM_END_HARD_V1 && immediate != 0) ||
          (opcode == OBELISK_RT_RANDOM_END_SOFT_V1 && immediate >= 64))
        return std::nullopt;
      if (opcode == OBELISK_RT_RANDOM_END_HARD_V1) {
        mlir::Value constraint = truth(builder, location, stack.back());
        hard = mlir::smt::AndOp::create(builder, location, hard, constraint);
        result.hardConstraints.push_back({constraint, {}, false});
        if (stack.back().directEquality)
          result.directEqualities.push_back(*stack.back().directEquality);
        if (stack.back().directDefinition)
          result.directDefinitions.push_back(*stack.back().directDefinition);
        if (stack.back().directCaptureBound)
          result.directCaptureBounds.push_back(
              *stack.back().directCaptureBound);
      }
      if (opcode == OBELISK_RT_RANDOM_END_SOFT_V1) {
        result.softConstraints.push_back(
            {truth(builder, location, stack.back()),
             static_cast<unsigned>(immediate),
             {},
             false,
             stack.back().directEquality,
             stack.back().directDefinition,
             stack.back().directCaptureBound});
        softPriorities |= uint64_t{1} << immediate;
      }
      sawHard |= opcode == OBELISK_RT_RANDOM_END_HARD_V1;
      sawSoft |= opcode == OBELISK_RT_RANDOM_END_SOFT_V1;
      stack.clear();
      continue;
    }
    if (opcode == OBELISK_RT_RANDOM_SELECT_V1) {
      if (stack.size() < 3)
        return std::nullopt;
      StackValue falseValue = stack.back();
      stack.pop_back();
      StackValue trueValue = stack.back();
      stack.pop_back();
      StackValue condition = stack.back();
      stack.pop_back();
      stack.push_back(
          {mlir::smt::IteOp::create(
               builder, location, truth(builder, location, condition),
               resize(builder, location, trueValue, width, signedOperation),
               resize(builder, location, falseValue, width, signedOperation)),
           width, std::nullopt, std::nullopt, condition.instructionBegin});
      continue;
    }
    if (isUnary(opcode)) {
      if (stack.empty())
        return std::nullopt;
      StackValue input = stack.back();
      stack.pop_back();
      switch (opcode) {
      case OBELISK_RT_RANDOM_CAST_V1:
      case OBELISK_RT_RANDOM_POS_V1:
        stack.push_back(
            {resize(builder, location, input, width, signedOperation), width,
             width == input.width ? input.directVariable : std::nullopt,
             std::nullopt, input.instructionBegin});
        break;
      case OBELISK_RT_RANDOM_NEG_V1:
        stack.push_back(
            {mlir::smt::BVNegOp::create(
                 builder, location,
                 resize(builder, location, input, width, signedOperation)),
             width, std::nullopt, std::nullopt, input.instructionBegin});
        break;
      case OBELISK_RT_RANDOM_BIT_NOT_V1:
        stack.push_back(
            {mlir::smt::BVNotOp::create(
                 builder, location,
                 resize(builder, location, input, width, signedOperation)),
             width, std::nullopt, std::nullopt, input.instructionBegin});
        break;
      case OBELISK_RT_RANDOM_REDUCE_AND_V1:
        stack.push_back(booleanValue(
            builder, location,
            mlir::smt::EqOp::create(
                builder, location, input.bits,
                mlir::smt::BVConstantOp::create(
                    builder, location, llvm::APInt::getAllOnes(input.width))),
            input.instructionBegin));
        break;
      case OBELISK_RT_RANDOM_REDUCE_OR_V1:
        stack.push_back(booleanValue(builder, location,
                                     truth(builder, location, input),
                                     input.instructionBegin));
        break;
      case OBELISK_RT_RANDOM_REDUCE_XOR_V1:
      case OBELISK_RT_RANDOM_REDUCE_XNOR_V1: {
        mlir::Value parity =
            mlir::smt::BoolConstantOp::create(builder, location, false);
        for (unsigned bit = 0; bit != input.width; ++bit) {
          mlir::Value current = mlir::smt::EqOp::create(
              builder, location,
              mlir::smt::ExtractOp::create(builder, location, bitVectorType(1),
                                           bit, input.bits),
              constant(builder, location, 1, 1));
          parity = mlir::smt::XOrOp::create(builder, location, parity, current);
        }
        if (opcode == OBELISK_RT_RANDOM_REDUCE_XNOR_V1)
          parity = mlir::smt::NotOp::create(builder, location, parity);
        stack.push_back(
            booleanValue(builder, location, parity, input.instructionBegin));
        break;
      }
      case OBELISK_RT_RANDOM_REDUCE_NAND_V1:
        stack.push_back(booleanValue(
            builder, location,
            mlir::smt::DistinctOp::create(
                builder, location, input.bits,
                mlir::smt::BVConstantOp::create(
                    builder, location, llvm::APInt::getAllOnes(input.width))),
            input.instructionBegin));
        break;
      case OBELISK_RT_RANDOM_REDUCE_NOR_V1:
      case OBELISK_RT_RANDOM_LOGICAL_NOT_V1:
        stack.push_back(booleanValue(
            builder, location,
            mlir::smt::NotOp::create(builder, location,
                                     truth(builder, location, input)),
            input.instructionBegin));
        break;
      default:
        return std::nullopt;
      }
      continue;
    }
    if (!isBinary(opcode) || stack.size() < 2)
      return std::nullopt;
    StackValue rhs = stack.back();
    stack.pop_back();
    StackValue lhs = stack.back();
    stack.pop_back();
    if (opcode >= OBELISK_RT_RANDOM_EQ_V1 &&
        opcode <= OBELISK_RT_RANDOM_LT_V1) {
      unsigned compareWidth = std::max(lhs.width, rhs.width);
      mlir::Value left =
          resize(builder, location, lhs, compareWidth, signedOperation);
      mlir::Value right =
          resize(builder, location, rhs, compareWidth, signedOperation);
      mlir::Value predicate;
      switch (opcode) {
      case OBELISK_RT_RANDOM_EQ_V1:
        predicate = mlir::smt::EqOp::create(builder, location, left, right);
        break;
      case OBELISK_RT_RANDOM_NE_V1:
        predicate =
            mlir::smt::DistinctOp::create(builder, location, left, right);
        break;
      default: {
        mlir::smt::BVCmpPredicate comparison;
        if (signedOperation) {
          comparison = opcode == OBELISK_RT_RANDOM_GE_V1
                           ? mlir::smt::BVCmpPredicate::sge
                       : opcode == OBELISK_RT_RANDOM_GT_V1
                           ? mlir::smt::BVCmpPredicate::sgt
                       : opcode == OBELISK_RT_RANDOM_LE_V1
                           ? mlir::smt::BVCmpPredicate::sle
                           : mlir::smt::BVCmpPredicate::slt;
        } else {
          comparison = opcode == OBELISK_RT_RANDOM_GE_V1
                           ? mlir::smt::BVCmpPredicate::uge
                       : opcode == OBELISK_RT_RANDOM_GT_V1
                           ? mlir::smt::BVCmpPredicate::ugt
                       : opcode == OBELISK_RT_RANDOM_LE_V1
                           ? mlir::smt::BVCmpPredicate::ule
                           : mlir::smt::BVCmpPredicate::ult;
        }
        predicate = mlir::smt::BVCmpOp::create(builder, location, comparison,
                                               left, right);
        break;
      }
      }
      std::optional<SMTVariableEquality> directEquality;
      std::optional<SMTVariableDefinition> directDefinition;
      std::optional<SMTVariableCaptureBound> directCaptureBound;
      if (opcode == OBELISK_RT_RANDOM_EQ_V1 && lhs.width == rhs.width &&
          lhs.directVariable && rhs.directVariable &&
          lhs.directVariable->offset != rhs.directVariable->offset)
        directEquality =
            SMTVariableEquality{*lhs.directVariable, *rhs.directVariable};
      if (opcode == OBELISK_RT_RANDOM_EQ_V1 && lhs.width == rhs.width &&
          lhs.directVariable && !rhs.directVariable &&
          !containsVariable(rhs.bits, *lhs.directVariable))
        directDefinition = SMTVariableDefinition{*lhs.directVariable, rhs.bits,
                                                 rhs.instructionBegin, index};
      else if (opcode == OBELISK_RT_RANDOM_EQ_V1 && lhs.width == rhs.width &&
               rhs.directVariable && !lhs.directVariable &&
               !containsVariable(lhs.bits, *rhs.directVariable))
        directDefinition =
            SMTVariableDefinition{*rhs.directVariable, lhs.bits,
                                  lhs.instructionBegin, rhs.instructionBegin};

      // Direct comparisons against one capture describe a runtime interval.
      // Retain that shape so lowering can normalize strict endpoints and
      // signed coordinates, then sample it directly.
      auto directCaptureIndex = [&](const StackValue &value)
          -> std::optional<uint32_t> {
        auto extract = mlir::dyn_cast_or_null<mlir::smt::ExtractOp>(
            value.bits.getDefiningOp());
        if (!extract || extract.getLowBit() != 0)
          return std::nullopt;
        auto found = llvm::find(captures, extract.getInput());
        if (found == captures.end())
          return std::nullopt;
        return static_cast<uint32_t>(found - captures.begin());
      };
      if (lhs.width == rhs.width &&
          (opcode == OBELISK_RT_RANDOM_GE_V1 ||
           opcode == OBELISK_RT_RANDOM_GT_V1 ||
           opcode == OBELISK_RT_RANDOM_LE_V1 ||
           opcode == OBELISK_RT_RANDOM_LT_V1)) {
        std::optional<uint32_t> lhsCapture = directCaptureIndex(lhs);
        std::optional<uint32_t> rhsCapture = directCaptureIndex(rhs);
        if (lhs.directVariable && rhsCapture) {
          SMTCaptureBoundKind kind;
          switch (opcode) {
          case OBELISK_RT_RANDOM_GE_V1:
            kind = SMTCaptureBoundKind::LowerInclusive;
            break;
          case OBELISK_RT_RANDOM_GT_V1:
            kind = SMTCaptureBoundKind::LowerExclusive;
            break;
          case OBELISK_RT_RANDOM_LE_V1:
            kind = SMTCaptureBoundKind::UpperInclusive;
            break;
          default:
            kind = SMTCaptureBoundKind::UpperExclusive;
            break;
          }
          directCaptureBound = SMTVariableCaptureBound{
              *lhs.directVariable, *rhsCapture, kind, signedOperation,
              predicate};
        } else if (rhs.directVariable && lhsCapture) {
          SMTCaptureBoundKind kind;
          switch (opcode) {
          case OBELISK_RT_RANDOM_LE_V1:
            kind = SMTCaptureBoundKind::LowerInclusive;
            break;
          case OBELISK_RT_RANDOM_LT_V1:
            kind = SMTCaptureBoundKind::LowerExclusive;
            break;
          case OBELISK_RT_RANDOM_GE_V1:
            kind = SMTCaptureBoundKind::UpperInclusive;
            break;
          default:
            kind = SMTCaptureBoundKind::UpperExclusive;
            break;
          }
          directCaptureBound = SMTVariableCaptureBound{
              *rhs.directVariable, *lhsCapture, kind, signedOperation,
              predicate};
        }
      }
      stack.push_back(
          booleanValue(builder, location, predicate, lhs.instructionBegin,
                       std::move(directEquality), std::move(directDefinition),
                       std::move(directCaptureBound)));
      continue;
    }
    if (opcode >= OBELISK_RT_RANDOM_LOGICAL_AND_V1 &&
        opcode <= OBELISK_RT_RANDOM_LOGICAL_EQUIV_V1) {
      mlir::Value left = truth(builder, location, lhs);
      mlir::Value right = truth(builder, location, rhs);
      mlir::Value predicate;
      switch (opcode) {
      case OBELISK_RT_RANDOM_LOGICAL_AND_V1:
        predicate = mlir::smt::AndOp::create(builder, location, left, right);
        break;
      case OBELISK_RT_RANDOM_LOGICAL_OR_V1:
        predicate = mlir::smt::OrOp::create(builder, location, left, right);
        break;
      case OBELISK_RT_RANDOM_LOGICAL_IMPLIES_V1:
        predicate =
            mlir::smt::ImpliesOp::create(builder, location, left, right);
        break;
      case OBELISK_RT_RANDOM_LOGICAL_EQUIV_V1:
        predicate = mlir::smt::EqOp::create(builder, location, left, right);
        break;
      default:
        return std::nullopt;
      }
      stack.push_back(
          booleanValue(builder, location, predicate, lhs.instructionBegin));
      continue;
    }
    if (opcode == OBELISK_RT_RANDOM_SHIFT_LEFT_V1 ||
        opcode == OBELISK_RT_RANDOM_SHIFT_RIGHT_V1 ||
        opcode == OBELISK_RT_RANDOM_SHIFT_RIGHT_ARITH_V1) {
      unsigned operationWidth = std::max<unsigned>(width, rhs.width);
      StackValue resizedLhs{
          resize(builder, location, lhs, operationWidth,
                 opcode == OBELISK_RT_RANDOM_SHIFT_RIGHT_ARITH_V1),
          operationWidth};
      StackValue resizedRhs{
          resize(builder, location, rhs, operationWidth, false),
          operationWidth};
      mlir::Value shifted;
      if (opcode == OBELISK_RT_RANDOM_SHIFT_LEFT_V1)
        shifted = mlir::smt::BVShlOp::create(builder, location, resizedLhs.bits,
                                             resizedRhs.bits);
      else if (opcode == OBELISK_RT_RANDOM_SHIFT_RIGHT_V1)
        shifted = mlir::smt::BVLShrOp::create(builder, location,
                                              resizedLhs.bits, resizedRhs.bits);
      else
        shifted = mlir::smt::BVAShrOp::create(builder, location,
                                              resizedLhs.bits, resizedRhs.bits);
      stack.push_back(
          {resize(builder, location, {shifted, operationWidth}, width, false),
           width, std::nullopt, std::nullopt, lhs.instructionBegin});
      continue;
    }
    if (opcode == OBELISK_RT_RANDOM_POWER_V1) {
      mlir::Value base = resize(builder, location, lhs, width, signedOperation);
      mlir::Value result = constant(builder, location, 1, width);
      for (unsigned bit = 0; bit != rhs.width; ++bit) {
        mlir::Value exponentBit = mlir::smt::ExtractOp::create(
            builder, location, bitVectorType(1), bit, rhs.bits);
        mlir::Value selected =
            mlir::smt::BVMulOp::create(builder, location, result, base);
        result = mlir::smt::IteOp::create(
            builder, location,
            mlir::smt::EqOp::create(builder, location, exponentBit,
                                    constant(builder, location, 1, 1)),
            selected, result);
        if (bit + 1 != rhs.width)
          base = mlir::smt::BVMulOp::create(builder, location, base, base);
      }
      stack.push_back(
          {result, width, std::nullopt, std::nullopt, lhs.instructionBegin});
      continue;
    }
    mlir::Value left = resize(builder, location, lhs, width, signedOperation);
    mlir::Value right = resize(builder, location, rhs, width, signedOperation);
    mlir::Value bits;
    switch (opcode) {
    case OBELISK_RT_RANDOM_ADD_V1:
      bits = mlir::smt::BVAddOp::create(builder, location, left, right);
      break;
    case OBELISK_RT_RANDOM_SUB_V1:
      bits = mlir::smt::BVAddOp::create(
          builder, location, left,
          mlir::smt::BVNegOp::create(builder, location, right));
      break;
    case OBELISK_RT_RANDOM_MUL_V1:
      bits = mlir::smt::BVMulOp::create(builder, location, left, right);
      break;
    case OBELISK_RT_RANDOM_DIV_V1:
      bits = signedOperation
                 ? mlir::Value(mlir::smt::BVSDivOp::create(builder, location,
                                                           left, right))
                 : mlir::Value(mlir::smt::BVUDivOp::create(builder, location,
                                                           left, right));
      break;
    case OBELISK_RT_RANDOM_MOD_V1:
      bits = signedOperation
                 ? mlir::Value(mlir::smt::BVSRemOp::create(builder, location,
                                                           left, right))
                 : mlir::Value(mlir::smt::BVURemOp::create(builder, location,
                                                           left, right));
      break;
    case OBELISK_RT_RANDOM_BIT_AND_V1:
      bits = mlir::smt::BVAndOp::create(builder, location, left, right);
      break;
    case OBELISK_RT_RANDOM_BIT_OR_V1:
      bits = mlir::smt::BVOrOp::create(builder, location, left, right);
      break;
    case OBELISK_RT_RANDOM_BIT_XOR_V1:
      bits = mlir::smt::BVXOrOp::create(builder, location, left, right);
      break;
    case OBELISK_RT_RANDOM_BIT_XNOR_V1:
      bits = mlir::smt::BVNotOp::create(
          builder, location,
          mlir::smt::BVXOrOp::create(builder, location, left, right));
      break;
    default:
      return std::nullopt;
    }
    stack.push_back(
        {bits, width, std::nullopt, std::nullopt, lhs.instructionBegin});
  }

  bool encodedSoft = (programFlags & OBELISK_RT_RANDOM_PROGRAM_HAS_SOFT) != 0;
  unsigned softCount = softPriorities == 0
                           ? 0
                           : 64 - llvm::countl_zero(softPriorities);
  uint64_t expectedSoftPriorities =
      softCount == 64 ? UINT64_MAX : (uint64_t{1} << softCount) - 1;
  if (!stack.empty() || !sawHard || sawSoft != encodedSoft ||
      softPriorities != expectedSoftPriorities)
    return std::nullopt;
  for (const DomainGroup &group : domainGroups) {
    mlir::Value field = mlir::smt::ExtractOp::create(
        builder, location, bitVectorType(group.width), group.targetOffset,
        assignment);
    auto found = llvm::find_if(result.variables, [&](const SMTVariable &item) {
      return item.offset == group.targetOffset && item.width == group.width;
    });
    if (found == result.variables.end()) {
      result.variables.push_back({group.targetOffset, group.width, field});
    } else {
      field = found->bits;
    }
    mlir::Value member =
        mlir::smt::BoolConstantOp::create(builder, location, false);
    for (const DomainPattern &pattern : group.patterns) {
      mlir::Value masked = mlir::smt::BVAndOp::create(
          builder, location, field,
          constant(builder, location, pattern.mask, group.width));
      mlir::Value matches = mlir::smt::EqOp::create(
          builder, location, masked,
          constant(builder, location, pattern.value, group.width));
      member = mlir::smt::OrOp::create(builder, location, member, matches);
    }
    hard = mlir::smt::AndOp::create(builder, location, hard, member);
    result.hardConstraints.push_back({member, {}, false});
    result.finiteDomains.push_back(
        {{group.targetOffset, group.width, field}, member});
  }
  result.assignment = assignment;
  result.captures = std::move(captures);
  result.hard = hard;
  for (SMTHardConstraint &constraint : result.hardConstraints) {
    for (const SMTVariable &variable : result.variables)
      if (containsVariable(constraint.expression, variable))
        constraint.dependencies.push_back(variable);
    constraint.hasCapture =
        llvm::any_of(result.captures, [&](mlir::Value capture) {
          return containsValue(constraint.expression, capture);
        });
  }
  for (SMTSoftConstraint &constraint : result.softConstraints) {
    for (const SMTVariable &variable : result.variables)
      if (containsVariable(constraint.expression, variable))
        constraint.dependencies.push_back(variable);
    if (constraint.directDefinition)
      for (const SMTVariable &variable : result.variables)
        if (containsVariable(constraint.directDefinition->expression,
                             variable))
          constraint.directDefinition->dependencies.push_back(variable);
    constraint.hasCapture =
        llvm::any_of(result.captures, [&](mlir::Value capture) {
          return containsValue(constraint.expression, capture);
        });
  }
  for (SMTVariableDefinition &definition : result.directDefinitions)
    for (const SMTVariable &variable : result.variables)
      if (containsVariable(definition.expression, variable))
        definition.dependencies.push_back(variable);
  mlir::smt::AssertOp::create(builder, location, hard);
  mlir::smt::YieldOp::create(builder, location);
  if (mlir::failed(mlir::verify(*result.module)))
    return std::nullopt;
  return result;
}

} // namespace obelisk::solver

#endif
