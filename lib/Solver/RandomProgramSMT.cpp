//===- RandomProgramSMT.cpp - Decode random constraints to SMT -*- C++ -*-===//

#include "obelisk/Solver/ConstraintSolver.h"

#ifdef OBELISK_ENABLE_Z3

#include "RandomProgramSMT.h"

#include "obelisk/Runtime/RandomDomain.h"
#include "obelisk/Runtime/RandomSamplerPlan.h"
#include "obelisk/Runtime/Runtime.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/Verifier.h"

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/MathExtras.h"

#include <algorithm>
#include <climits>
#include <cstdint>
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

uint64_t variableKey(uint32_t offset, unsigned width) {
  return (static_cast<uint64_t>(offset) << 32) | width;
}

struct ReferenceBudget {
  uint64_t remaining;
  bool exceeded = false;

  bool consume() {
    if (remaining != 0) {
      --remaining;
      return true;
    }
    exceeded = true;
    return false;
  }
};

bool containsVariable(mlir::Value expression, const SMTVariable &variable,
                      ReferenceBudget &budget) {
  auto target = mlir::dyn_cast_or_null<mlir::smt::ExtractOp>(
      variable.bits.getDefiningOp());
  if (!target)
    return true;
  llvm::SmallPtrSet<mlir::Operation *, 16> visited;
  llvm::SmallVector<mlir::Value> pending{expression};
  while (!pending.empty()) {
    if (!budget.consume())
      return true;
    mlir::Value value = pending.pop_back_val();
    mlir::Operation *operation = value.getDefiningOp();
    if (!operation || !visited.insert(operation).second)
      continue;
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
    llvm::append_range(pending, operation->getOperands());
  }
  return false;
}

bool isUnary(uint8_t opcode) {
  return (opcode >= OBELISK_RT_RANDOM_CAST_V1 &&
          opcode <= OBELISK_RT_RANDOM_LOGICAL_NOT_V1) ||
         opcode == OBELISK_RT_RANDOM_COUNT_ONES_V1 ||
         opcode == OBELISK_RT_RANDOM_CLOG2_V1;
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
      (version2 && headerSize != OBELISK_RT_RANDOM_PROGRAM_HEADER_SIZE_V2) ||
      programSize < headerSize)
    return std::nullopt;
  uint32_t aggregateWidth = read32(program + 8);
  uint32_t instructionCount = read32(program + 12);
  uint32_t captureCount = read32(program + 16);
  uint32_t programFlags = read32(program + 20);
  // Function priority solves independent higher constraint sets first. A
  // table over the whole relation would incorrectly condition those choices
  // on lower-stage constraints. The compiled sampler handles the staged
  // program.
  if (programFlags & OBELISK_RT_RANDOM_PROGRAM_HAS_FUNCTION_PRIORITY)
    return std::nullopt;
  uint32_t literalWordCount = version2 ? read32(program + 24) : 0;
  if ((version1 && aggregateWidth > 64) ||
      (version2 && read32(program + 28) != 0) ||
      (programFlags & ~(OBELISK_RT_RANDOM_PROGRAM_HAS_SOFT |
                        OBELISK_RT_RANDOM_PROGRAM_HAS_SOLVE_BEFORE |
                        OBELISK_RT_RANDOM_PROGRAM_HAS_DIST |
                        OBELISK_RT_RANDOM_PROGRAM_HAS_DOMAINS |
                        OBELISK_RT_RANDOM_PROGRAM_SMT_COMPLETION |
                        OBELISK_RT_RANDOM_PROGRAM_SMT_UNIFORM |
                        OBELISK_RT_RANDOM_PROGRAM_SMT_WEIGHTED)) != 0 ||
      instructionCount > (std::numeric_limits<size_t>::max() - headerSize) /
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
  auto decodeInstruction =
      [&](uint32_t index) -> std::optional<DecodedInstruction> {
    const uint8_t *cursor =
        program + headerSize +
        static_cast<size_t>(index) * OBELISK_RT_RANDOM_INSTRUCTION_SIZE_V2;
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
    if (usedHighBits != 0 && (words.back() >> usedHighBits) != 0)
      return std::nullopt;
    decoded.literal = llvm::APInt(decoded.width, words);
    return decoded;
  };
  bool hasSolveBefore =
      (programFlags & OBELISK_RT_RANDOM_PROGRAM_HAS_SOLVE_BEFORE) != 0;
  uint32_t solveEdgeCount = 0;
  size_t solveEdgesOffset = 0;
  size_t solveMaskWords = version2 ? (uint64_t(aggregateWidth) + 63) / 64 : 1;
  size_t solveEdgeSize =
      solveMaskWords * 16 + OBELISK_RT_RANDOM_SOLVE_EDGE_TAIL_SIZE_V2;
  if (hasSolveBefore) {
    if (expectedSize > std::numeric_limits<size_t>::max() -
                           OBELISK_RT_RANDOM_SOLVE_EDGE_HEADER_SIZE ||
        programSize < expectedSize + OBELISK_RT_RANDOM_SOLVE_EDGE_HEADER_SIZE)
      return std::nullopt;
    solveEdgeCount = read32(program + expectedSize);
    solveEdgesOffset = expectedSize + OBELISK_RT_RANDOM_SOLVE_EDGE_HEADER_SIZE;
    if (aggregateWidth == 0 || solveEdgeCount == 0 ||
        solveEdgeCount >
            (std::numeric_limits<size_t>::max() - solveEdgesOffset) /
                solveEdgeSize)
      return std::nullopt;
    expectedSize =
        solveEdgesOffset + static_cast<size_t>(solveEdgeCount) * solveEdgeSize;
  }
  bool hasDist = (programFlags & OBELISK_RT_RANDOM_PROGRAM_HAS_DIST) != 0;
  bool weighted = (programFlags & OBELISK_RT_RANDOM_PROGRAM_SMT_WEIGHTED) != 0;
  if ((weighted && !hasDist) || (hasDist && hasSolveBefore && !weighted))
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
    if (distGroupCount == 0 || distGroupCount > distRecordCount ||
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
  std::vector<uint8_t> literalUse(literalWordCount, 0);
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
      nextLiteralWord += (static_cast<uint64_t>(decoded->width) + 63) / 64;
    }
    std::fill_n(literalUse.begin(), nextLiteralWord, 1);
  }

  if (hasSolveBefore) {
    llvm::DenseMap<llvm::APInt, unsigned> maskIds;
    std::vector<unsigned> bitOwners(aggregateWidth, UINT_MAX);
    std::vector<std::vector<unsigned>> successors;
    std::vector<unsigned> indegrees;
    const uint8_t *edge = program + solveEdgesOffset;
    auto readMask = [&]() -> std::optional<llvm::APInt> {
      llvm::SmallVector<uint64_t> words;
      for (size_t word = 0; word != solveMaskWords; ++word) {
        words.push_back(read64(edge));
        edge += 8;
      }
      if (aggregateWidth % 64 && (words.back() >> (aggregateWidth % 64)) != 0)
        return std::nullopt;
      return llvm::APInt(aggregateWidth, words);
    };
    auto registerMask =
        [&](const llvm::APInt &mask) -> std::optional<unsigned> {
      auto found = maskIds.find(mask);
      if (found != maskIds.end())
        return found->second;
      unsigned id = successors.size();
      for (unsigned word = 0; word != mask.getNumWords(); ++word) {
        uint64_t bits = mask.getRawData()[word];
        while (bits != 0) {
          uint64_t bit =
              static_cast<uint64_t>(word) * 64 + llvm::countr_zero(bits);
          if (bitOwners[bit] != UINT_MAX)
            return std::nullopt;
          bitOwners[bit] = id;
          bits &= bits - 1;
        }
      }
      maskIds.try_emplace(mask, id);
      successors.emplace_back();
      indegrees.push_back(0);
      return id;
    };
    for (uint32_t index = 0; index != solveEdgeCount; ++index) {
      auto before = readMask(), after = readMask();
      if (!before || !after)
        return std::nullopt;
      llvm::APInt beforeMask = *before, afterMask = *after;
      uint32_t constraintBlock = read32(edge);
      uint32_t reserved = read32(edge + 4);
      edge += OBELISK_RT_RANDOM_SOLVE_EDGE_TAIL_SIZE_V2;
      if (beforeMask.isZero() || afterMask.isZero() ||
          !(beforeMask & afterMask).isZero() || reserved != 0 ||
          (constraintBlock != OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1 &&
           constraintBlock >= 64))
        return std::nullopt;
      auto beforeId = registerMask(beforeMask),
           afterId = registerMask(afterMask);
      if (!beforeId || !afterId)
        return std::nullopt;
      successors[*beforeId].push_back(*afterId);
      ++indegrees[*afterId];
    }
    std::vector<unsigned> ready;
    for (unsigned id = 0; id != indegrees.size(); ++id)
      if (indegrees[id] == 0)
        ready.push_back(id);
    for (size_t next = 0; next != ready.size(); ++next)
      for (unsigned successor : successors[ready[next]])
        if (--indegrees[successor] == 0)
          ready.push_back(successor);
    if (ready.size() != successors.size())
      return std::nullopt;
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
      uint64_t mask = width >= 64  ? UINT64_MAX
                      : width == 0 ? 0
                                   : (uint64_t{1} << width) - 1;
      bool fullDomain = cardinality == 0 && width == 64 && lower == 0;
      bool captured = flags & OBELISK_RT_RANDOM_DIST_CAPTURED_INTERVAL;
      bool defaultComplement =
          flags & OBELISK_RT_RANDOM_DIST_DEFAULT_COMPLEMENT;
      if (group >= distGroupCount || width == 0 || width > 64 ||
          targetOffset > aggregateWidth ||
          width > aggregateWidth - targetOffset ||
          (weighted ? reserved > 64 : reserved != 0) ||
          (captured ? lower >= captureCount || cardinality >= captureCount ||
                          !(flags & OBELISK_RT_RANDOM_DIST_PER_RANGE)
                    : lower > mask ||
                          (!fullDomain && (cardinality == 0 ||
                                           cardinality - 1 > mask - lower))) ||
          (defaultComplement &&
           (captured || !(flags & OBELISK_RT_RANDOM_DIST_PER_RANGE) ||
            lower != 0 || cardinality != mask + 1)) ||
          (!weighted && coefficient == 0) || capture >= captureCount ||
          (weighted
               ? !(flags & OBELISK_RT_RANDOM_DIST_RAW_WEIGHT) ||
                     (flags & ~(OBELISK_RT_RANDOM_DIST_RAW_WEIGHT |
                                OBELISK_RT_RANDOM_DIST_PER_RANGE |
                                OBELISK_RT_RANDOM_DIST_TARGET_SIGNED |
                                OBELISK_RT_RANDOM_DIST_CAPTURED_INTERVAL |
                                OBELISK_RT_RANDOM_DIST_DEFAULT_COMPLEMENT |
                                OBELISK_RT_RANDOM_DIST_EXPRESSION_WEIGHT)) != 0
               : (flags & ~(OBELISK_RT_RANDOM_DIST_WEIGHT_SIGNED |
                            OBELISK_RT_RANDOM_DIST_TARGET_SIGNED)) != 0) ||
          (constraintBlock != OBELISK_RT_RANDOM_UNMASKED_CONSTRAINT_V1 &&
           constraintBlock >= 64))
        return std::nullopt;
      DistGroupInfo &info = groups[group];
      uint32_t targetFlags = flags & OBELISK_RT_RANDOM_DIST_TARGET_SIGNED;
      if (info.seen && (info.constraintBlock != constraintBlock ||
                        info.targetOffset != targetOffset ||
                        info.width != width || info.targetFlags != targetFlags))
        return std::nullopt;
      info = {true, constraintBlock, targetOffset, width, targetFlags};
    }
    if (llvm::any_of(groups,
                     [](const DistGroupInfo &info) { return !info.seen; }))
      return std::nullopt;
  }

  struct DomainPattern {
    llvm::APInt mask;
    llvm::APInt value;
  };
  struct DomainGroup {
    bool seen = false;
    uint32_t targetOffset = 0;
    uint32_t width = 0;
    std::vector<DomainPattern> patterns;
    llvm::DenseSet<llvm::APInt> equalMaskValues;
    bool needsOverlapProof = false;
  };
  std::vector<DomainGroup> domainGroups(domainGroupCount);
  if (hasDomains) {
    const uint8_t *record = program + domainRecordsOffset;
    for (uint32_t index = 0; index != domainRecordCount; ++index) {
      obelisk::random::DomainRecord decoded;
      if (!obelisk::random::readDomainRecord(
              record, aggregateWidth, version2, program + literalPoolOffset,
              literalWordCount, literalUse.data(), decoded) ||
          decoded.group >= domainGroupCount)
        return std::nullopt;
      record += OBELISK_RT_RANDOM_DOMAIN_RECORD_SIZE;
      uint32_t group = decoded.group;
      uint32_t targetOffset = decoded.targetOffset;
      uint32_t width = decoded.width;
      llvm::SmallVector<uint64_t> maskWords, valueWords;
      for (uint64_t word = 0; word != decoded.words(); ++word) {
        maskWords.push_back(decoded.maskWord(word));
        valueWords.push_back(decoded.valueWord(word));
      }
      llvm::APInt mask(width, maskWords), value(width, valueWords);
      DomainGroup &info = domainGroups[group];
      if (info.seen &&
          (info.targetOffset != targetOffset || info.width != width))
        return std::nullopt;
      info.seen = true;
      info.targetOffset = targetOffset;
      info.width = width;
      if (!info.patterns.empty() && mask != info.patterns.front().mask)
        info.needsOverlapProof = true;
      if (!info.needsOverlapProof && !info.equalMaskValues.insert(value).second)
        return std::nullopt;
      info.patterns.push_back({mask, value});
    }
    if (llvm::any_of(domainGroups,
                     [](const DomainGroup &group) { return !group.seen; }))
      return std::nullopt;
    std::vector<bool> domainBits(aggregateWidth);
    for (const DomainGroup &group : domainGroups) {
      uint64_t end = static_cast<uint64_t>(group.targetOffset) + group.width;
      for (uint64_t bit = group.targetOffset; bit != end; ++bit) {
        if (domainBits[bit])
          return std::nullopt;
        domainBits[bit] = true;
      }
    }
  }

  if (llvm::any_of(literalUse, [](uint8_t used) { return used != 1; }))
    return std::nullopt;

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
      if (decoded->opcode != OBELISK_RT_RANDOM_PUSH_CAPTURE_V1 &&
          decoded->opcode != OBELISK_RT_RANDOM_END_DIST_WEIGHT_BIT_V1)
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

  llvm::DenseMap<mlir::Value, uint32_t> captureIndices;
  for (auto [index, capture] : llvm::enumerate(captures))
    captureIndices.try_emplace(capture, index);
  llvm::DenseMap<uint64_t, size_t> variableIndices;
  auto registerVariable = [&](uint32_t offset, unsigned width) {
    uint64_t key = variableKey(offset, width);
    auto found = variableIndices.find(key);
    if (found != variableIndices.end())
      return result.variables[found->second];
    mlir::Value bits = mlir::smt::ExtractOp::create(
        builder, location, bitVectorType(width), offset, assignment);
    variableIndices.try_emplace(key, result.variables.size());
    result.variables.push_back({offset, width, bits});
    return result.variables.back();
  };
  // Dependency lists can themselves be quadratic for many overlapping views.
  // They optimize the legacy proposal only; the exact sampling plan does not
  // require them. Bound their discovery and output by the lowered bit count.
  uint64_t workUnits = programSize;
  auto addWorkUnits = [&](uint64_t units) {
    workUnits = units > UINT64_MAX - workUnits ? UINT64_MAX : workUnits + units;
  };
  addWorkUnits(aggregateWidth);
  for (uint32_t index = 0; index != instructionCount; ++index) {
    auto decoded = decodeInstruction(index);
    if (!decoded)
      return std::nullopt;
    addWorkUnits(decoded->width);
  }
  for (const DomainGroup &group : domainGroups)
    addWorkUnits(static_cast<uint64_t>(group.width) * group.patterns.size());
  result.proposalWorkLimit =
      workUnits > UINT64_MAX / 64 ? UINT64_MAX : workUnits * 64;
  ReferenceBudget referenceBudget{result.proposalWorkLimit};

  std::vector<StackValue> stack;
  stack.reserve(instructionCount);
  std::vector<StackValue> temporaries;
  mlir::Value hard = mlir::smt::BoolConstantOp::create(builder, location, true);
  bool sawHard = false;
  bool sawSoft = false;
  uint64_t softPriorities = 0;
  std::vector<bool> guardSeen(distGroupCount, false);
  random::WeightBitSequence weightBits(captureCount, weighted);
  for (uint32_t index = 0; index != instructionCount; ++index) {
    std::optional<DecodedInstruction> decoded = decodeInstruction(index);
    if (!decoded)
      return std::nullopt;
    uint8_t opcode = decoded->opcode;
    unsigned width = decoded->width;
    uint8_t flags = decoded->flags;
    uint32_t operand = decoded->operand;
    uint64_t immediate = decoded->auxiliary;
    if (!weightBits.visit(opcode, width, flags, operand, immediate))
      return std::nullopt;
    if (width == 0 || (version1 && width > 64) ||
        (flags & ~OBELISK_RT_RANDOM_INSTRUCTION_SIGNED) != 0)
      return std::nullopt;
    if (version2 && opcode != OBELISK_RT_RANDOM_PUSH_LITERAL_V1 &&
        opcode != OBELISK_RT_RANDOM_END_SOFT_V1 &&
        opcode != OBELISK_RT_RANDOM_END_DIST_WEIGHT_BIT_V1 && immediate != 0)
      return std::nullopt;
    bool signedOperation = (flags & OBELISK_RT_RANDOM_INSTRUCTION_SIGNED) != 0;
    if (opcode == OBELISK_RT_RANDOM_STORE_VALUE_V1) {
      if (stack.empty() || flags != 0 || immediate != 0 ||
          operand != temporaries.size() || width != stack.back().width)
        return std::nullopt;
      temporaries.push_back(stack.back());
      continue;
    }
    if (opcode == OBELISK_RT_RANDOM_LOAD_VALUE_V1) {
      if (flags != 0 || immediate != 0 || operand >= temporaries.size() ||
          width != temporaries[operand].width)
        return std::nullopt;
      stack.push_back(temporaries[operand]);
      // A reused expression may not form a standalone postfix slice. Keep
      // its SMT identity and direct-variable metadata; proposal validation
      // rejects slices whose temporaries were defined outside that slice.
      stack.back().instructionBegin = index;
      continue;
    }
    if (opcode == OBELISK_RT_RANDOM_PUSH_VARIABLE_V1) {
      if (aggregateWidth == 0 || operand >= aggregateWidth ||
          width > aggregateWidth - operand)
        return std::nullopt;
      SMTVariable variable = registerVariable(operand, width);
      stack.push_back({variable.bits, width, variable, std::nullopt, index});
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
    if (opcode == OBELISK_RT_RANDOM_PUSH_CONSTRAINT_ENABLED_V1) {
      if (width != 1 || flags != 0 || operand >= 64 || immediate != 0)
        return std::nullopt;
      // This formula plans only the all-enabled proposal. The runtime uses
      // the live block mask, and disabling any block bypasses that proposal.
      stack.push_back({constant(builder, location, llvm::APInt(1, 1)), 1,
                       std::nullopt, std::nullopt, index});
      continue;
    }
    if (opcode == OBELISK_RT_RANDOM_PUSH_LITERAL_V1) {
      if (!decoded->literal)
        return std::nullopt;
      stack.push_back({constant(builder, location, *decoded->literal), width,
                       std::nullopt, std::nullopt, index});
      continue;
    }
    if (opcode == OBELISK_RT_RANDOM_END_DIST_GUARD_V1) {
      if (!weighted || width != 1 || flags != 0 || stack.size() != 1 ||
          immediate != 0 || operand >= guardSeen.size() || guardSeen[operand])
        return std::nullopt;
      guardSeen[operand] = true;
      stack.clear();
      continue;
    }
    if (opcode == OBELISK_RT_RANDOM_END_DIST_WEIGHT_BIT_V1) {
      if (!weighted || flags || operand >= captureCount || stack.size() != 1 ||
          stack.back().width != width || immediate >= width)
        return std::nullopt;
      if (immediate + 1 == width)
        stack.clear();
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
      case OBELISK_RT_RANDOM_COUNT_ONES_V1: {
        unsigned countWidth = std::min(
            width, static_cast<unsigned>(llvm::Log2_64(input.width) + 1));
        llvm::SmallVector<mlir::Value> terms;
        for (unsigned bit = 0; bit != input.width; ++bit) {
          StackValue selected{
              mlir::smt::ExtractOp::create(builder, location, bitVectorType(1),
                                           bit, input.bits),
              1, std::nullopt, std::nullopt, input.instructionBegin};
          terms.push_back(
              resize(builder, location, selected, countWidth, false));
        }
        // A balanced sum at the minimum count precision avoids needlessly
        // bit-blasting one 32-bit adder for every source bit.
        while (terms.size() > 1) {
          llvm::SmallVector<mlir::Value> next;
          for (unsigned term = 0; term < terms.size(); term += 2)
            next.push_back(term + 1 == terms.size()
                               ? terms[term]
                               : mlir::smt::BVAddOp::create(builder, location,
                                                            terms[term],
                                                            terms[term + 1])
                                     .getResult());
          terms = std::move(next);
        }
        StackValue count{terms.front(), countWidth, std::nullopt, std::nullopt,
                         input.instructionBegin};
        stack.push_back({resize(builder, location, count, width, false), width,
                         std::nullopt, std::nullopt, input.instructionBegin});
        break;
      }
      case OBELISK_RT_RANDOM_CLOG2_V1: {
        unsigned resultWidth = std::min(
            width, static_cast<unsigned>(llvm::Log2_64(input.width) + 1));
        mlir::Value result = constant(builder, location, 0, resultWidth);
        mlir::Value predecessor = mlir::smt::BVAddOp::create(
            builder, location, input.bits,
            constant(builder, location, llvm::APInt::getAllOnes(input.width)));
        // ceil(log2(x)) is the bit length of x-1, with zero handled
        // separately. This uses one subtractor rather than width-linear
        // full-width comparisons; signed source bits are still unsigned.
        for (unsigned bit = 0; bit != input.width; ++bit) {
          mlir::Value selected = mlir::smt::ExtractOp::create(
              builder, location, bitVectorType(1), bit, predecessor);
          mlir::Value present = mlir::smt::EqOp::create(
              builder, location, selected, constant(builder, location, 1, 1));
          result = mlir::smt::IteOp::create(
              builder, location, present,
              constant(builder, location, bit + 1, resultWidth), result);
        }
        result = mlir::smt::IteOp::create(
            builder, location, truth(builder, location, input), result,
            constant(builder, location, 0, resultWidth));
        StackValue logarithm{result, resultWidth, std::nullopt, std::nullopt,
                             input.instructionBegin};
        stack.push_back({resize(builder, location, logarithm, width, false),
                         width, std::nullopt, std::nullopt,
                         input.instructionBegin});
        break;
      }
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
          !containsVariable(rhs.bits, *lhs.directVariable, referenceBudget))
        directDefinition = SMTVariableDefinition{*lhs.directVariable, rhs.bits,
                                                 rhs.instructionBegin, index};
      else if (opcode == OBELISK_RT_RANDOM_EQ_V1 && lhs.width == rhs.width &&
               rhs.directVariable && !lhs.directVariable &&
               !containsVariable(lhs.bits, *rhs.directVariable,
                                 referenceBudget))
        directDefinition =
            SMTVariableDefinition{*rhs.directVariable, lhs.bits,
                                  lhs.instructionBegin, rhs.instructionBegin};
      if (referenceBudget.exceeded)
        return std::nullopt;

      // Direct comparisons against one capture describe a runtime interval.
      // Retain that shape so lowering can normalize strict endpoints and
      // signed coordinates, then sample it directly.
      auto directCaptureIndex =
          [&](const StackValue &value) -> std::optional<uint32_t> {
        auto extract = mlir::dyn_cast_or_null<mlir::smt::ExtractOp>(
            value.bits.getDefiningOp());
        if (!extract || extract.getLowBit() != 0)
          return std::nullopt;
        auto found = captureIndices.find(extract.getInput());
        if (found == captureIndices.end())
          return std::nullopt;
        return found->second;
      };
      if (lhs.width == rhs.width && (opcode == OBELISK_RT_RANDOM_GE_V1 ||
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
          directCaptureBound =
              SMTVariableCaptureBound{*lhs.directVariable, *rhsCapture, kind,
                                      signedOperation, predicate};
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
          directCaptureBound =
              SMTVariableCaptureBound{*rhs.directVariable, *lhsCapture, kind,
                                      signedOperation, predicate};
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
      StackValue combined =
          booleanValue(builder, location, predicate, lhs.instructionBegin);
      if (opcode == OBELISK_RT_RANDOM_LOGICAL_AND_V1) {
        // Conjoining definedness does not weaken a direct equality or bound.
        // Retain an unambiguous candidate; Z3 still proves it from the complete
        // hard formula before generating a structural sampling proposal.
        if (bool(lhs.directDefinition) != bool(rhs.directDefinition))
          combined.directDefinition = lhs.directDefinition
                                          ? lhs.directDefinition
                                          : rhs.directDefinition;
        if (bool(lhs.directEquality) != bool(rhs.directEquality))
          combined.directEquality =
              lhs.directEquality ? lhs.directEquality : rhs.directEquality;
        if (bool(lhs.directCaptureBound) != bool(rhs.directCaptureBound))
          combined.directCaptureBound = lhs.directCaptureBound
                                            ? lhs.directCaptureBound
                                            : rhs.directCaptureBound;
      }
      stack.push_back(std::move(combined));
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
  unsigned softCount =
      softPriorities == 0 ? 0 : 64 - llvm::countl_zero(softPriorities);
  uint64_t expectedSoftPriorities =
      softCount == 64 ? UINT64_MAX : (uint64_t{1} << softCount) - 1;
  if (!stack.empty() || !sawHard || sawSoft != encodedSoft ||
      softPriorities != expectedSoftPriorities)
    return std::nullopt;
  for (const DomainGroup &group : domainGroups) {
    mlir::Value field = registerVariable(group.targetOffset, group.width).bits;
    mlir::Value member =
        mlir::smt::BoolConstantOp::create(builder, location, false);
    mlir::Value overlap = member;
    for (const DomainPattern &pattern : group.patterns) {
      mlir::Value masked = mlir::smt::BVAndOp::create(
          builder, location, field, constant(builder, location, pattern.mask));
      mlir::Value matches =
          mlir::smt::EqOp::create(builder, location, masked,
                                  constant(builder, location, pattern.value));
      if (group.needsOverlapProof)
        overlap = mlir::smt::OrOp::create(
            builder, location, overlap,
            mlir::smt::AndOp::create(builder, location, member, matches));
      member = mlir::smt::OrOp::create(builder, location, member, matches);
    }
    if (group.needsOverlapProof)
      result.domainOverlaps.push_back(overlap);
    hard = mlir::smt::AndOp::create(builder, location, hard, member);
    result.hardConstraints.push_back({member, {}, false});
    result.finiteDomains.push_back(
        {{group.targetOffset, group.width, field}, member});
  }
  result.assignment = assignment;
  result.captures = std::move(captures);
  result.hard = hard;
  std::vector<std::vector<size_t>> variablesForBit(aggregateWidth);
  for (auto [index, variable] : llvm::enumerate(result.variables)) {
    uint64_t end = static_cast<uint64_t>(variable.offset) + variable.width;
    for (uint64_t bit = variable.offset; bit != end; ++bit)
      variablesForBit[bit].push_back(index);
  }
  std::vector<uint64_t> variableGenerations(result.variables.size());
  uint64_t generation = 0;
  auto collectReferences = [&](mlir::Value expression,
                               std::vector<SMTVariable> &dependencies,
                               bool &hasCapture) {
    ++generation;
    llvm::SmallPtrSet<mlir::Operation *, 16> visited;
    llvm::SmallVector<mlir::Value> pending{expression};
    while (!pending.empty()) {
      if (!referenceBudget.consume())
        return false;
      mlir::Value value = pending.pop_back_val();
      if (captureIndices.contains(value))
        hasCapture = true;
      mlir::Operation *operation = value.getDefiningOp();
      if (!operation || !visited.insert(operation).second)
        continue;
      if (auto extract = mlir::dyn_cast<mlir::smt::ExtractOp>(operation);
          extract && extract.getInput() == assignment) {
        uint64_t begin = extract.getLowBit();
        unsigned width =
            mlir::cast<mlir::smt::BitVectorType>(value.getType()).getWidth();
        for (uint64_t bit = begin; bit != begin + width; ++bit) {
          if (!referenceBudget.consume())
            return false;
          for (size_t index : variablesForBit[bit]) {
            if (!referenceBudget.consume())
              return false;
            if (variableGenerations[index] == generation)
              continue;
            variableGenerations[index] = generation;
            dependencies.push_back(result.variables[index]);
          }
        }
      }
      llvm::append_range(pending, llvm::reverse(operation->getOperands()));
    }
    return true;
  };
  for (SMTHardConstraint &constraint : result.hardConstraints) {
    if (!collectReferences(constraint.expression, constraint.dependencies,
                           constraint.hasCapture))
      return std::nullopt;
  }
  for (SMTSoftConstraint &constraint : result.softConstraints) {
    if (!collectReferences(constraint.expression, constraint.dependencies,
                           constraint.hasCapture))
      return std::nullopt;
    bool unusedCapture = false;
    if (constraint.directDefinition &&
        !collectReferences(constraint.directDefinition->expression,
                           constraint.directDefinition->dependencies,
                           unusedCapture))
      return std::nullopt;
  }
  for (SMTVariableDefinition &definition : result.directDefinitions) {
    bool unusedCapture = false;
    if (!collectReferences(definition.expression, definition.dependencies,
                           unusedCapture))
      return std::nullopt;
  }
  mlir::smt::AssertOp::create(builder, location, hard);
  mlir::smt::YieldOp::create(builder, location);
  if (mlir::failed(mlir::verify(*result.module)))
    return std::nullopt;
  return result;
}

} // namespace obelisk::solver

#endif
