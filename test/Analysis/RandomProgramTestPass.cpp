//===- RandomProgramTestPass.cpp - Print constrained-random programs ------===//
//
// Decode the residual program of each random solve and cyclic sample op and
// print its header, instructions, distribution records and compiled sampler
// plan one per line, so tests can check the encoded program without matching
// escaped bytes.
//
//===----------------------------------------------------------------------===//

#include "AnalysisTestPasses.h"

#include "talos/Dialect/Simulation/SimulationOps.h"
#include "talos/Runtime/RandomSamplerPlan.h"
#include "talos/Runtime/Runtime.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"

#include "llvm/Support/raw_ostream.h"

#include <map>
#include <set>
#include <vector>

using namespace mlir;

namespace {

StringRef opcodeName(unsigned opcode) {
  switch (opcode) {
#define OPCODE(name)                                                           \
  case TALOS_RT_RANDOM_##name##_V1:                                          \
    return #name;
    OPCODE(PUSH_VARIABLE)
    OPCODE(PUSH_CAPTURE)
    OPCODE(CAST)
    OPCODE(POS)
    OPCODE(NEG)
    OPCODE(BIT_NOT)
    OPCODE(REDUCE_AND)
    OPCODE(REDUCE_OR)
    OPCODE(REDUCE_XOR)
    OPCODE(REDUCE_NAND)
    OPCODE(REDUCE_NOR)
    OPCODE(REDUCE_XNOR)
    OPCODE(LOGICAL_NOT)
    OPCODE(ADD)
    OPCODE(SUB)
    OPCODE(MUL)
    OPCODE(BIT_AND)
    OPCODE(BIT_OR)
    OPCODE(BIT_XOR)
    OPCODE(BIT_XNOR)
    OPCODE(EQ)
    OPCODE(NE)
    OPCODE(GE)
    OPCODE(GT)
    OPCODE(LE)
    OPCODE(LT)
    OPCODE(LOGICAL_AND)
    OPCODE(LOGICAL_OR)
    OPCODE(LOGICAL_IMPLIES)
    OPCODE(LOGICAL_EQUIV)
    OPCODE(SELECT)
    OPCODE(END_HARD)
    OPCODE(END_SOFT)
    OPCODE(PUSH_LITERAL)
    OPCODE(DIV)
    OPCODE(MOD)
    OPCODE(SHIFT_LEFT)
    OPCODE(SHIFT_RIGHT)
    OPCODE(SHIFT_RIGHT_ARITH)
    OPCODE(POWER)
    OPCODE(PUSH_CONSTRAINT_ENABLED)
    OPCODE(COUNT_ONES)
    OPCODE(CLOG2)
    OPCODE(END_DIST_GUARD)
    OPCODE(STORE_VALUE)
    OPCODE(LOAD_VALUE)
    OPCODE(REAL32_BITS)
    OPCODE(REAL64_BITS)
    OPCODE(REAL32_FROM_RANK)
    OPCODE(REAL64_FROM_RANK)
    OPCODE(REAL_CAST)
    OPCODE(REAL_RANK_VALID)
    OPCODE(REAL_TO_BITS)
    OPCODE(REAL_TO_INTEGER)
    OPCODE(REAL_TRUNCATE_INTEGER)
    OPCODE(REAL_IS_FINITE)
    OPCODE(END_DIST_WEIGHT_BIT)
    OPCODE(END_DIST_TARGET_BIT)
#undef OPCODE
  }
  return "UNKNOWN";
}

void printFlags(raw_ostream &os, uint32_t flags,
                ArrayRef<std::pair<uint32_t, StringRef>> names) {
  os << "[";
  bool first = true;
  for (auto [bit, name] : names) {
    if (!(flags & bit))
      continue;
    os << (first ? "" : ", ") << name;
    first = false;
    flags &= ~bit;
  }
  if (flags)
    os << (first ? "" : ", ") << flags;
  os << "]";
}

class RandomProgramReader {
public:
  explicit RandomProgramReader(StringRef bytes) : bytes(bytes) {}
  bool fits(size_t offset, size_t size) const {
    return offset <= bytes.size() && size <= bytes.size() - offset;
  }
  uint64_t read(size_t offset, unsigned size) const {
    uint64_t value = 0;
    for (unsigned index = 0; index != size; ++index)
      value |= uint64_t(uint8_t(bytes[offset + index])) << (index * 8);
    return value;
  }

private:
  StringRef bytes;
};

/// Print a compiled sampler plan's shape: its gate count, the top-level
/// conjuncts of each root, which the target samples apart when they read
/// disjoint inputs, and the free fields the compiler proved, by the root and
/// conjunct they belong to.
LogicalResult printPlan(Operation *op, const RandomProgramReader &reader,
                        size_t size, raw_ostream &os) {
  using namespace talos::random;
  if (size < samplerPlanTrailerSize)
    return op->emitError("random program sampler plan is truncated");
  size_t trailer = size - samplerPlanTrailerSize;
  uint64_t planSize = reader.read(trailer + 8, 8);
  if (reader.read(trailer, 4) != samplerTrailerMagic || planSize > trailer ||
      planSize < samplerPlanHeaderSize)
    return op->emitError("random program sampler plan is truncated");
  size_t plan = trailer - planSize;
  uint64_t nodes = reader.read(plan + 8, 4), roots = reader.read(plan + 12, 4);
  size_t nodeBase = plan + samplerPlanHeaderSize;
  size_t rootBase = nodeBase + nodes * samplerPlanNodeSize;
  size_t freeBase = rootBase + roots * samplerPlanRootSize;
  if (freeBase > trailer)
    return op->emitError("random program sampler plan is truncated");
  os << "  plan nodes " << nodes << " roots " << roots << "\n";
  std::map<uint64_t, std::pair<uint64_t, size_t>> conjuncts;
  for (uint64_t root = 0; root != roots; ++root) {
    uint64_t instruction =
        reader.read(rootBase + root * samplerPlanRootSize, 4);
    std::vector<uint64_t> pending{
        reader.read(rootBase + root * samplerPlanRootSize + 4, 4)};
    std::set<uint64_t> seen;
    size_t count = 0;
    while (!pending.empty()) {
      uint64_t id = pending.back();
      pending.pop_back();
      if (id == 1 || !seen.insert(id).second || id / 2 >= nodes)
        continue;
      size_t node = nodeBase + id / 2 * samplerPlanNodeSize;
      if (!(id & 1) &&
          reader.read(node, 4) == static_cast<uint32_t>(PlanOpcode::And)) {
        pending.push_back(reader.read(node + 8, 4));
        pending.push_back(reader.read(node + 4, 4));
      } else
        conjuncts.try_emplace(id, instruction, count++);
    }
    os << "  plan root " << instruction << " conjuncts " << count << "\n";
  }
  if (freeBase == trailer)
    return success();
  uint64_t count = reader.read(freeBase + 4, 4);
  if (reader.read(freeBase, 4) != samplerFreeFieldMagic ||
      freeBase + samplerFreeFieldHeaderSize + count * samplerFreeFieldSize !=
          trailer)
    return op->emitError("random program free fields are malformed");
  for (uint64_t index = 0; index != count; ++index) {
    size_t entry =
        freeBase + samplerFreeFieldHeaderSize + index * samplerFreeFieldSize;
    auto found = conjuncts.find(reader.read(entry, 4));
    if (found == conjuncts.end())
      return op->emitError("random program free field names no conjunct");
    os << "  plan free root " << found->second.first << " conjunct "
       << found->second.second << " offset " << reader.read(entry + 4, 4)
       << " width " << reader.read(entry + 8, 4) << "\n";
  }
  return success();
}

LogicalResult printProgram(Operation *op, StringRef bytes, raw_ostream &os) {
  RandomProgramReader reader(bytes);
  if (!reader.fits(0, TALOS_RT_RANDOM_PROGRAM_HEADER_SIZE) ||
      reader.read(0, 4) != TALOS_RT_RANDOM_PROGRAM_MAGIC)
    return op->emitError("random program has no valid header");
  unsigned version = reader.read(4, 2);
  bool wide = version == TALOS_RT_RANDOM_PROGRAM_VERSION_V2;
  size_t header = reader.read(6, 2);
  uint64_t width = reader.read(8, 4), instructions = reader.read(12, 4),
           captures = reader.read(16, 4), flags = reader.read(20, 4);
  uint64_t literals = wide ? reader.read(24, 4) : 0;
  os << op->getName() << ": version " << version << " width " << width
     << " instructions " << instructions << " captures " << captures;
  if (wide)
    os << " literals " << literals;
  os << " flags ";
  printFlags(
      os, flags,
      {{TALOS_RT_RANDOM_PROGRAM_HAS_SOFT, "HAS_SOFT"},
       {TALOS_RT_RANDOM_PROGRAM_HAS_SOLVE_BEFORE, "HAS_SOLVE_BEFORE"},
       {TALOS_RT_RANDOM_PROGRAM_HAS_DIST, "HAS_DIST"},
       {TALOS_RT_RANDOM_PROGRAM_HAS_DOMAINS, "HAS_DOMAINS"},
       {TALOS_RT_RANDOM_PROGRAM_EXACT_UNIFORM, "EXACT_UNIFORM"},
       {TALOS_RT_RANDOM_PROGRAM_HAS_FUNCTION_PRIORITY,
        "HAS_FUNCTION_PRIORITY"},
       {TALOS_RT_RANDOM_PROGRAM_EXACT_WEIGHTED, "EXACT_WEIGHTED"},
       {TALOS_RT_RANDOM_PROGRAM_HAS_GUARD_INPUT_MASKS,
        "HAS_GUARD_INPUT_MASKS"},
       {TALOS_RT_RANDOM_PROGRAM_HAS_SAMPLER_PLAN, "HAS_SAMPLER_PLAN"},
       {TALOS_RT_RANDOM_PROGRAM_HAS_REAL, "HAS_REAL"}});
  os << "\n";
  size_t offset = header;
  size_t pool = header + instructions * 16;
  if (!reader.fits(offset, instructions * 16) ||
      !reader.fits(pool, literals * 8))
    return op->emitError("random program instructions are truncated");
  for (uint64_t index = 0; index != instructions; ++index, offset += 16) {
    unsigned opcode = reader.read(offset, 1);
    uint64_t instructionWidth =
        wide ? reader.read(offset + 4, 4) : reader.read(offset + 1, 1);
    uint64_t instructionFlags = reader.read(offset + (wide ? 1 : 2), 1);
    uint64_t operand = reader.read(offset + (wide ? 8 : 4), 4);
    uint64_t auxiliary =
        wide ? reader.read(offset + 12, 4) : reader.read(offset + 8, 8);
    os << "  " << index << ": " << opcodeName(opcode) << " width "
       << instructionWidth;
    if (instructionFlags)
      os << " flags " << instructionFlags;
    if (operand)
      os << " operand " << operand;
    if (opcode == TALOS_RT_RANDOM_PUSH_LITERAL_V1) {
      // A version 2 literal is a pool index; print its low word.
      if (wide && auxiliary >= literals)
        return op->emitError("random program literal is out of range");
      os << " value " << (wide ? reader.read(pool + auxiliary * 8, 8)
                                : auxiliary);
    } else if (auxiliary) {
      os << " auxiliary " << auxiliary;
    }
    os << "\n";
  }
  offset += literals * 8;
  uint64_t words = std::max<uint64_t>(1, (width + 63) / 64);
  if (flags & TALOS_RT_RANDOM_PROGRAM_HAS_SOLVE_BEFORE) {
    if (!reader.fits(offset, 4))
      return op->emitError("random program solve edges are truncated");
    offset += 4 + reader.read(offset, 4) *
                      (wide ? words * 16 +
                                  TALOS_RT_RANDOM_SOLVE_EDGE_TAIL_SIZE_V2
                            : TALOS_RT_RANDOM_SOLVE_EDGE_SIZE);
  }
  if (flags & TALOS_RT_RANDOM_PROGRAM_HAS_FUNCTION_PRIORITY) {
    if (!reader.fits(offset, 4))
      return op->emitError("random program priority inputs are truncated");
    offset += 4 + reader.read(offset, 4) *
                      (TALOS_RT_RANDOM_PRIORITY_INPUT_RECORD_HEADER_SIZE +
                       2 * words * 8);
  }
  if (flags & TALOS_RT_RANDOM_PROGRAM_HAS_DIST) {
    if (!reader.fits(offset, TALOS_RT_RANDOM_DIST_HEADER_SIZE))
      return op->emitError("random program dist records are truncated");
    uint64_t records = reader.read(offset + 4, 4);
    offset += TALOS_RT_RANDOM_DIST_HEADER_SIZE;
    if (!reader.fits(offset, records * TALOS_RT_RANDOM_DIST_RECORD_SIZE))
      return op->emitError("random program dist records are truncated");
    for (uint64_t index = 0; index != records;
         ++index, offset += TALOS_RT_RANDOM_DIST_RECORD_SIZE) {
      uint32_t block = reader.read(offset + 4, 4);
      os << "  dist group " << reader.read(offset, 4) << " block ";
      if (block == TALOS_RT_RANDOM_UNMASKED_CONSTRAINT_V1)
        os << "unmasked";
      else
        os << block;
      os << " offset " << reader.read(offset + 8, 4) << " width "
         << reader.read(offset + 12, 2) << " soft "
         << reader.read(offset + 14, 2) << " lower "
         << reader.read(offset + 16, 8) << " cardinality "
         << reader.read(offset + 24, 8) << " item "
         << reader.read(offset + 32, 8) << " capture "
         << reader.read(offset + 40, 4) << " flags ";
      printFlags(
          os, reader.read(offset + 44, 4),
          {{TALOS_RT_RANDOM_DIST_WEIGHT_SIGNED, "WEIGHT_SIGNED"},
           {TALOS_RT_RANDOM_DIST_TARGET_SIGNED, "TARGET_SIGNED"},
           {TALOS_RT_RANDOM_DIST_RAW_WEIGHT, "RAW_WEIGHT"},
           {TALOS_RT_RANDOM_DIST_PER_RANGE, "PER_RANGE"},
           {TALOS_RT_RANDOM_DIST_CAPTURED_INTERVAL, "CAPTURED_INTERVAL"},
           {TALOS_RT_RANDOM_DIST_DEFAULT_COMPLEMENT, "DEFAULT_COMPLEMENT"},
           {TALOS_RT_RANDOM_DIST_EXPRESSION_WEIGHT, "EXPRESSION_WEIGHT"},
           {TALOS_RT_RANDOM_DIST_EXPRESSION_TARGET, "EXPRESSION_TARGET"}});
      os << "\n";
    }
  }
  if (flags & TALOS_RT_RANDOM_PROGRAM_HAS_SAMPLER_PLAN)
    return printPlan(op, reader, bytes.size(), os);
  return success();
}

class RandomProgramTestPass
    : public PassWrapper<RandomProgramTestPass, OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(RandomProgramTestPass)

  StringRef getArgument() const final {
    return "test-talos-print-random-programs";
  }
  StringRef getDescription() const final {
    return "Print the decoded residual programs of random solve operations";
  }

  void runOnOperation() override {
    WalkResult result = getOperation().walk([&](Operation *op) {
      StringAttr program;
      if (auto solve = dyn_cast<talos::sim::SimRandomSolveOp>(op))
        program = solve.getProgramAttr();
      else if (auto solve = dyn_cast<talos::sim::SimRandomSolveWideOp>(op))
        program = solve.getProgramAttr();
      else if (auto sample = dyn_cast<talos::sim::SimRandomCycleSampleOp>(op))
        program = sample.getProgramAttr();
      if (!program)
        return WalkResult::advance();
      return failed(printProgram(op, program.getValue(), llvm::errs()))
                 ? WalkResult::interrupt()
                 : WalkResult::advance();
    });
    if (result.wasInterrupted())
      signalPassFailure();
  }
};

} // namespace

namespace talos {
void registerRandomProgramTestPass() {
  PassRegistration<RandomProgramTestPass>();
}
} // namespace talos
