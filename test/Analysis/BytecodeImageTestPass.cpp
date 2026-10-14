//===- BytecodeImageTestPass.cpp - Print design-bytecode records ---------===//
//
// Decode the module's serialized design-bytecode image and print its
// instruction stream and side tables, one record per line.
//
//===----------------------------------------------------------------------===//

#include "AnalysisTestPasses.h"

#include "obelisk/Runtime/Runtime.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"

#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"

#include <cstddef>

using namespace mlir;

namespace {

using Header = obelisk_rt_design_bytecode_header_v1;

/// Bounds-checked little-endian reader over one serialized image.
class ImageReader {
public:
  explicit ImageReader(ArrayRef<uint8_t> image) : image(image) {}

  template <typename T> T read(uint64_t offset) const {
    return llvm::support::endian::read<T, llvm::endianness::little>(
        image.data() + offset);
  }
  uint64_t header(size_t field) const { return read<uint64_t>(field); }

  /// Return whether `count` records of `size` bytes fit at `offset`.
  bool fits(uint64_t offset, uint64_t count, uint64_t size) const {
    return offset <= image.size() && count <= (image.size() - offset) / size;
  }

  ArrayRef<uint8_t> image;
};

class BytecodeImageTestPass
    : public PassWrapper<BytecodeImageTestPass, OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(BytecodeImageTestPass)

  BytecodeImageTestPass() = default;
  BytecodeImageTestPass(const BytecodeImageTestPass &other)
      : PassWrapper(other) {}

  StringRef getArgument() const final { return "test-obelisk-print-bytecode"; }
  StringRef getDescription() const final {
    return "print the serialized design-bytecode image records";
  }

  Option<bool> metadata{*this, "metadata",
                        llvm::cl::desc("print function, register-layout, and "
                                       "operand-map metadata"),
                        llvm::cl::init(false)};
  Option<bool> state{*this, "state",
                     llvm::cl::desc("print static net and driver state "
                                    "descriptors"),
                     llvm::cl::init(false)};

  void runOnOperation() final {
    ModuleOp module = getOperation();
    auto attr = module->getAttrOfType<DenseI8ArrayAttr>("bytecode.image");
    if (!attr) {
      module.emitError("missing bytecode.image attribute");
      return signalPassFailure();
    }
    ArrayRef<int8_t> signedImage = attr.asArrayRef();
    ArrayRef<uint8_t> image(reinterpret_cast<const uint8_t *>(signedImage.data()),
                            signedImage.size());
    if (image.size() < OBELISK_RT_DESIGN_BYTECODE_HEADER_SIZE ||
        StringRef(reinterpret_cast<const char *>(image.data()), 8) !=
            StringRef(OBELISK_RT_DESIGN_BYTECODE_MAGIC, 8)) {
      module.emitError("invalid Obelisk design-bytecode header");
      return signalPassFailure();
    }
    if (failed(print(ImageReader(image), llvm::errs()))) {
      module.emitError("malformed Obelisk design-bytecode image");
      return signalPassFailure();
    }
  }

private:
  LogicalResult print(const ImageReader &reader, raw_ostream &os) const {
    if (state) {
      uint64_t offset = reader.header(offsetof(Header, state_offset));
      uint64_t count = reader.header(offsetof(Header, state_count));
      if (!reader.fits(offset, count, 32))
        return failure();
      for (uint64_t index = 0; index < count; ++index) {
        uint64_t record = offset + index * 32;
        uint32_t kind = reader.read<uint32_t>(record);
        uint32_t flags = reader.read<uint32_t>(record + 4);
        os << "state " << index << ": kind=";
        if (kind == 0xFFFFFFFEu) {
          os << "net";
        } else if (kind == 0xFFFFFFFFu) {
          os << "driver";
        } else {
          os << "capture";
        }
        os << " flags=" << flags << " value=" << reader.read<uint64_t>(record + 8)
           << " target=" << reader.read<uint64_t>(record + 16)
           << " width=" << reader.read<uint64_t>(record + 24);
        if (kind == 0xFFFFFFFFu) {
          // Strength code 0 is the default strong drive (6); other codes are
          // offset by one from the strength ordinal.
          auto strength = [](uint32_t code) { return code ? code - 1 : 6; };
          os << " strength0=" << strength((flags >> 3) & 0xF)
             << " strength1=" << strength((flags >> 7) & 0xF);
        }
        os << '\n';
      }
    }

    uint64_t codeOffset = reader.header(offsetof(Header, code_offset));
    uint64_t instructionCount = reader.header(offsetof(Header, instruction_count));
    if (!reader.fits(codeOffset, instructionCount,
                     OBELISK_RT_DESIGN_BYTECODE_INSTRUCTION_SIZE))
      return failure();

    uint64_t constantOffset = reader.header(offsetof(Header, constant_offset));
    uint64_t constantSize = reader.header(offsetof(Header, constant_size));
    if (!reader.fits(constantOffset, constantSize, 1))
      return failure();
    os << "constants: "
       << llvm::toHex(reader.image.slice(constantOffset, constantSize),
                      /*LowerCase=*/true)
       << '\n';

    uint64_t operandOffset = reader.header(offsetof(Header, operand_offset));
    uint64_t operandCount = reader.header(offsetof(Header, operand_count));
    if (!reader.fits(operandOffset, operandCount, 8))
      return failure();
    auto operandDestination = [&](uint64_t index) {
      return reader.read<uint32_t>(operandOffset + index * 8);
    };
    auto operandSource = [&](uint64_t index) {
      return reader.read<uint32_t>(operandOffset + index * 8 + 4);
    };

    if (metadata) {
      uint64_t functionOffset = reader.header(offsetof(Header, function_offset));
      uint64_t functionCount = reader.header(offsetof(Header, function_count));
      uint64_t layoutOffset = reader.header(offsetof(Header, layout_offset));
      uint64_t layoutCount = reader.header(offsetof(Header, layout_count));
      if (!reader.fits(functionOffset, functionCount, 96) ||
          !reader.fits(layoutOffset, layoutCount, 40))
        return failure();
      for (uint64_t index = 0; index < functionCount; ++index) {
        uint64_t record = functionOffset + index * 96;
        uint64_t firstLayout = reader.read<uint64_t>(record + 32);
        uint64_t functionLayoutCount = reader.read<uint64_t>(record + 40);
        if (firstLayout > layoutCount ||
            functionLayoutCount > layoutCount - firstLayout)
          return failure();
        os << "function " << index << ": id=" << reader.read<uint64_t>(record)
           << " rank=" << reader.read<uint64_t>(record + 8)
           << " first_instruction=" << reader.read<uint64_t>(record + 16)
           << " instruction_count=" << reader.read<uint64_t>(record + 24)
           << " arguments=" << reader.read<uint32_t>(record + 48)
           << " results=" << reader.read<uint32_t>(record + 52) << '\n';
        for (uint64_t reg = 0; reg < functionLayoutCount; ++reg) {
          uint64_t layout = layoutOffset + (firstLayout + reg) * 40;
          os << "layout function=" << index << " register=" << reg
             << " kind=" << unsigned(reader.read<uint8_t>(layout))
             << " flags=" << unsigned(reader.read<uint8_t>(layout + 1))
             << " width=" << reader.read<uint32_t>(layout + 4)
             << " offset=" << reader.read<uint64_t>(layout + 8)
             << " size=" << reader.read<uint64_t>(layout + 16)
             << " auxiliary=" << reader.read<uint64_t>(layout + 24) << '\n';
        }
      }
      for (uint64_t index = 0; index < operandCount; ++index)
        os << "operand " << index << ": dst=" << operandDestination(index)
           << " src=" << operandSource(index) << '\n';
    }

    for (uint64_t index = 0; index < instructionCount; ++index) {
      uint64_t record =
          codeOffset + index * OBELISK_RT_DESIGN_BYTECODE_INSTRUCTION_SIZE;
      os << index << ": opcode=" << reader.read<uint16_t>(record)
         << " flags=" << reader.read<uint16_t>(record + 2)
         << " dst=" << reader.read<uint32_t>(record + 4)
         << " src0=" << reader.read<uint32_t>(record + 8)
         << " src1=" << reader.read<uint32_t>(record + 12)
         << " src2=" << reader.read<uint32_t>(record + 16)
         << " aux=" << reader.read<uint32_t>(record + 20)
         << " imm=" << reader.read<uint64_t>(record + 24) << '\n';
    }

    uint64_t continuationOffset =
        reader.header(offsetof(Header, continuation_offset));
    uint64_t continuationCount =
        reader.header(offsetof(Header, continuation_count));
    if (!reader.fits(continuationOffset, continuationCount, 24))
      return failure();
    for (uint64_t index = 0; index < continuationCount; ++index) {
      uint64_t record = continuationOffset + index * 24;
      os << "continuation " << index
         << ": function=" << reader.read<uint32_t>(record)
         << " id=" << reader.read<uint32_t>(record + 4)
         << " instruction=" << reader.read<uint64_t>(record + 8)
         << " rank=" << reader.read<uint32_t>(record + 16)
         << " reserved=" << reader.read<uint32_t>(record + 20) << '\n';
    }

    uint64_t intrinsicOffset = reader.header(offsetof(Header, intrinsic_offset));
    uint64_t intrinsicCount = reader.header(offsetof(Header, intrinsic_count));
    if (!reader.fits(intrinsicOffset, intrinsicCount, 16))
      return failure();
    auto intrinsicId = [&](uint64_t index) {
      return reader.read<uint32_t>(intrinsicOffset + index * 16);
    };
    for (uint64_t index = 0; index < intrinsicCount; ++index) {
      uint64_t record = intrinsicOffset + index * 16;
      os << "intrinsic " << index << ": id="
         << llvm::format_hex(intrinsicId(index), 10)
         << " inputs=" << reader.read<uint32_t>(record + 4)
         << " outputs=" << reader.read<uint32_t>(record + 8)
         << " flags=" << reader.read<uint32_t>(record + 12) << '\n';
    }

    uint64_t siteOffset = reader.header(offsetof(Header, site_offset));
    uint64_t siteCount = reader.header(offsetof(Header, site_count));
    if (!reader.fits(siteOffset, siteCount, 16))
      return failure();
    for (uint64_t index = 0; index < siteCount; ++index) {
      uint64_t record = siteOffset + index * 16;
      uint32_t signature = reader.read<uint32_t>(record);
      uint64_t firstOperand = reader.read<uint32_t>(record + 4);
      uint64_t inputCount = reader.read<uint32_t>(record + 8);
      uint64_t outputCount = reader.read<uint32_t>(record + 12);
      if (signature >= intrinsicCount || firstOperand > operandCount ||
          inputCount + outputCount > operandCount - firstOperand)
        return failure();
      os << "site " << index << ": signature=" << signature
         << " id=" << llvm::format_hex(intrinsicId(signature), 10)
         << " inputs=[";
      for (uint64_t input = 0; input < inputCount; ++input)
        os << (input ? ", " : "") << operandSource(firstOperand + input);
      os << "] outputs=[";
      for (uint64_t output = 0; output < outputCount; ++output)
        os << (output ? ", " : "")
           << operandDestination(firstOperand + inputCount + output);
      os << "]\n";
    }
    return success();
  }
};

} // namespace

namespace obelisk {
void registerBytecodeImageTestPass() {
  PassRegistration<BytecodeImageTestPass>();
}
} // namespace obelisk
