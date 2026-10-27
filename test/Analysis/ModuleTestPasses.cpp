//===- ModuleTestPasses.cpp - Module surgery for test inputs -------------===//
//
// Test-only module surgery that replaces textual input edits: a symbol
// prefix lets two compilations of one input share an executable, exported
// LLVM globals expose private model state to a C++ oracle, merged module
// attributes select a configuration of one input, matched attribute edits
// derive invalid variants of one input, a marked branch grows into a block
// chain deeper than any hand-written input, constant-trip loops unroll into
// straight-line code, and marked operations replicate into many processes,
// so scale tests keep a small input instead of a generated one.
//
//===----------------------------------------------------------------------===//

#include "AnalysisTestPasses.h"

#include "mlir/AsmParser/AsmParser.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Dialect/UB/IR/UBOps.h"
#include "mlir/Dialect/SCF/Utils/Utils.h"
#include "mlir/IR/AttrTypeSubElements.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/Iterators.h"
#include "mlir/Interfaces/FunctionInterfaces.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Pass/Pass.h"
#include "llvm/Support/MemoryBuffer.h"

using namespace mlir;

namespace {

class PrefixSymbolsTestPass
    : public PassWrapper<PrefixSymbolsTestPass, OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(PrefixSymbolsTestPass)

  PrefixSymbolsTestPass() = default;
  PrefixSymbolsTestPass(const PrefixSymbolsTestPass &other)
      : PassWrapper(other) {}

  StringRef getArgument() const final { return "test-talos-prefix-symbols"; }
  StringRef getDescription() const final {
    return "prefix every symbol defined in the top-level module";
  }

  Option<std::string> prefix{*this, "prefix",
                             llvm::cl::desc("prefix added to each symbol")};

  void runOnOperation() final {
    ModuleOp module = getOperation();
    SymbolTable table(module);
    SmallVector<Operation *> symbols;
    for (Operation &op : module.getBody()->getOperations())
      if (isa<SymbolOpInterface>(op))
        symbols.push_back(&op);
    for (Operation *op : symbols) {
      StringAttr name = SymbolTable::getSymbolName(op);
      if (failed(table.rename(
              op, StringAttr::get(&getContext(), prefix + name.getValue())))) {
        op->emitError("cannot rename symbol");
        return signalPassFailure();
      }
    }
  }
};

class ExportLLVMSymbolsTestPass
    : public PassWrapper<ExportLLVMSymbolsTestPass, OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(ExportLLVMSymbolsTestPass)

  ExportLLVMSymbolsTestPass() = default;
  ExportLLVMSymbolsTestPass(const ExportLLVMSymbolsTestPass &other)
      : PassWrapper(other) {}

  StringRef getArgument() const final {
    return "test-talos-export-llvm-symbols";
  }
  StringRef getDescription() const final {
    return "give named LLVM globals external linkage and rename main";
  }

  ListOption<std::string> globals{
      *this, "globals",
      llvm::cl::desc("internal LLVM globals to give external linkage")};
  Option<std::string> main{
      *this, "main",
      llvm::cl::desc("new name for the generated main function")};

  void runOnOperation() final {
    ModuleOp module = getOperation();
    SymbolTable table(module);
    for (const std::string &name : globals) {
      auto global = table.lookup<LLVM::GlobalOp>(name);
      if (!global || global.getLinkage() != LLVM::Linkage::Internal) {
        module.emitError() << "no internal LLVM global @" << name;
        return signalPassFailure();
      }
      global.setLinkage(LLVM::Linkage::External);
    }
    if (main.empty())
      return;
    auto function = table.lookup<LLVM::LLVMFuncOp>("main");
    if (!function ||
        failed(table.rename(function, StringAttr::get(&getContext(), main)))) {
      module.emitError("cannot rename the generated main function");
      return signalPassFailure();
    }
  }
};

/// Module attributes select native scheduling modes and budgets. Tests that
/// compare configurations of one input merge the variant's attributes here
/// instead of duplicating the module. A binary file, such as a generated
/// coverage schema, can also be embedded as an array<i8> module attribute.
class SetModuleAttributesTestPass
    : public PassWrapper<SetModuleAttributesTestPass, OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(SetModuleAttributesTestPass)

  SetModuleAttributesTestPass() = default;
  SetModuleAttributesTestPass(const SetModuleAttributesTestPass &other)
      : PassWrapper(other) {}

  StringRef getArgument() const final {
    return "test-talos-set-module-attributes";
  }
  StringRef getDescription() const final {
    return "merge a dictionary of attributes into the top-level module";
  }

  Option<std::string> attributes{
      *this, "attrs",
      llvm::cl::desc("dictionary attribute merged into the module, replacing "
                     "attributes of the same name; \\\" spells a quote "
                     "inside a quoted option value")};
  Option<std::string> bytesAttribute{
      *this, "bytes-attr",
      llvm::cl::desc("module attribute set to the contents of `bytes-file` "
                     "as an array<i8>")};
  Option<std::string> bytesFile{
      *this, "bytes-file", llvm::cl::desc("file read for `bytes-attr`")};

  void runOnOperation() final {
    ModuleOp module = getOperation();
    if (bytesAttribute.empty() != bytesFile.empty() ||
        (attributes.empty() && bytesAttribute.empty())) {
      module.emitError("give attrs, or bytes-attr with bytes-file");
      return signalPassFailure();
    }
    if (!bytesAttribute.empty()) {
      auto buffer = llvm::MemoryBuffer::getFile(bytesFile, /*IsText=*/false);
      if (!buffer) {
        module.emitError() << "cannot read " << bytesFile << ": "
                           << buffer.getError().message();
        return signalPassFailure();
      }
      StringRef bytes = (*buffer)->getBuffer();
      module->setAttr(bytesAttribute,
                      DenseI8ArrayAttr::get(
                          &getContext(),
                          ArrayRef<int8_t>(
                              reinterpret_cast<const int8_t *>(bytes.data()),
                              bytes.size())));
    }
    if (attributes.empty())
      return;
    std::string source = attributes;
    for (size_t at = source.find("\\\""); at != std::string::npos;
         at = source.find("\\\"", at + 1))
      source.erase(at, 1);
    auto dictionary = dyn_cast_or_null<DictionaryAttr>(
        parseAttribute(source, &getContext()));
    if (!dictionary) {
      module.emitError() << "attrs is not a dictionary attribute: "
                         << attributes;
      return signalPassFailure();
    }
    for (NamedAttribute attribute : dictionary)
      module->setAttr(attribute.getName(), attribute.getValue());
  }
};

/// Tests of invalid or uncommon configurations rewrite one checked-in input
/// instead of duplicating it. Every operation with all `match` attributes,
/// the `has` attribute names and, if given, the `op` name loses its `remove`
/// attributes, takes the `set` attributes, and has each `replace-from` element
/// in its attributes and types replaced by `replace-to` (an attribute, or a
/// type for a type), and has operand `operand` replaced by an `arith.constant`
/// of the typed `constant` attribute or a `ub.poison` of the `poison` type;
/// with `erase`, it is erased instead. With
/// `clone`, the edit applies to an unused copy inserted before each operation,
/// so a value whose users constrain its type can still be produced mistyped.
class EditAttributesTestPass
    : public PassWrapper<EditAttributesTestPass, OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(EditAttributesTestPass)

  EditAttributesTestPass() = default;
  EditAttributesTestPass(const EditAttributesTestPass &other)
      : PassWrapper(other) {}

  StringRef getArgument() const final {
    return "test-talos-edit-attributes";
  }
  StringRef getDescription() const final {
    return "edit, replace within, or erase operations that match attributes";
  }

  Option<std::string> opName{*this, "op",
                             llvm::cl::desc("only edit operations of this name")};
  Option<std::string> match{
      *this, "match",
      llvm::cl::desc("dictionary of attributes an operation must carry")};
  ListOption<std::string> has{
      *this, "has",
      llvm::cl::desc("attribute names an operation must carry, any value")};
  Option<std::string> set{*this, "set",
                          llvm::cl::desc("dictionary of attributes to set")};
  ListOption<std::string> remove{*this, "remove",
                                 llvm::cl::desc("attribute names to remove")};
  Option<bool> erase{*this, "erase",
                     llvm::cl::desc("erase the matching operations"),
                     llvm::cl::init(false)};
  Option<std::string> replaceFrom{
      *this, "replace-from",
      llvm::cl::desc("attribute or type to replace, also when nested")};
  Option<std::string> replaceTo{*this, "replace-to",
                                llvm::cl::desc("its replacement")};
  Option<int64_t> operandIndex{
      *this, "operand",
      llvm::cl::desc("index of the operand to replace with `constant`"),
      llvm::cl::init(-1)};
  Option<std::string> constant{
      *this, "constant",
      llvm::cl::desc("typed attribute of the replacement operand")};
  Option<std::string> poison{
      *this, "poison",
      llvm::cl::desc("type of a ub.poison replacement operand")};
  Option<bool> clone{*this, "clone",
                     llvm::cl::desc("edit an unused copy inserted before each "
                                    "matching operation"),
                     llvm::cl::init(false)};

  void getDependentDialects(DialectRegistry &registry) const final {
    registry.insert<arith::ArithDialect, ub::UBDialect>();
  }

  void runOnOperation() final {
    ModuleOp module = getOperation();
    DictionaryAttr required = parseDictionary(match, "match");
    DictionaryAttr assigned = parseDictionary(set, "set");
    if (!required || !assigned)
      return signalPassFailure();
    Attribute from, to;
    if (!replaceFrom.empty() || !replaceTo.empty()) {
      from = parseElement(replaceFrom, "replace-from");
      to = parseElement(replaceTo, "replace-to");
      if (!from || !to || isa<TypeAttr>(from) != isa<TypeAttr>(to)) {
        module.emitError("replace-from and replace-to must be two attributes "
                         "or two types");
        return signalPassFailure();
      }
    }
    TypedAttr operandValue;
    Type poisonType;
    if (operandIndex >= 0 || !constant.empty() || !poison.empty()) {
      if (!constant.empty())
        operandValue =
            dyn_cast_or_null<TypedAttr>(parseElement(constant, "constant"));
      if (auto type = dyn_cast_or_null<TypeAttr>(
              poison.empty() ? Attribute() : parseElement(poison, "poison")))
        poisonType = type.getValue();
      if (operandIndex < 0 || !operandValue == !poisonType) {
        module.emitError("operand needs an index and either a typed constant "
                         "attribute or a poison type");
        return signalPassFailure();
      }
    }
    if (clone && erase) {
      module.emitError("clone and erase cannot be combined");
      return signalPassFailure();
    }
    // An edit that changes nothing would silently test the unedited input.
    bool replaced = false;
    AttrTypeReplacer replacer;
    if (auto fromType = dyn_cast_or_null<TypeAttr>(from)) {
      Type toType = cast<TypeAttr>(to).getValue();
      replacer.addReplacement([&, fromType, toType](Type type) -> std::optional<Type> {
        if (type != fromType.getValue())
          return std::nullopt;
        replaced = true;
        return toType;
      });
    } else if (from) {
      replacer.addReplacement(
          [&](Attribute attribute) -> std::optional<Attribute> {
            if (attribute != from)
              return std::nullopt;
            replaced = true;
            return to;
          });
    }
    SmallVector<Operation *> matched;
    module.walk([&](Operation *operation) {
      if (operation == module)
        return;
      if (!opName.empty() && operation->getName().getStringRef() != opName)
        return;
      for (NamedAttribute attribute : required)
        if (operation->getAttr(attribute.getName()) != attribute.getValue())
          return;
      for (const std::string &name : has)
        if (!operation->hasAttr(name))
          return;
      matched.push_back(operation);
    });
    if (matched.empty()) {
      module.emitError("test-talos-edit-attributes matched no operation");
      return signalPassFailure();
    }
    for (Operation *&operation : matched) {
      if (erase)
        continue;
      if (clone) {
        OpBuilder builder(operation);
        operation = builder.clone(*operation);
      }
      if (operandValue || poisonType) {
        if (static_cast<uint64_t>(operandIndex) >= operation->getNumOperands()) {
          operation->emitError("test-talos-edit-attributes operand index is "
                               "out of range");
          return signalPassFailure();
        }
        OpBuilder builder(operation);
        Value replacement =
            operandValue
                ? Value(arith::ConstantOp::create(builder, operation->getLoc(),
                                                  operandValue))
                : Value(ub::PoisonOp::create(builder, operation->getLoc(),
                                             poisonType));
        operation->setOperand(operandIndex, replacement);
      }
      for (const std::string &name : remove)
        operation->removeAttr(name);
      for (NamedAttribute attribute : assigned)
        operation->setAttr(attribute.getName(), attribute.getValue());
      if (from)
        replacer.replaceElementsIn(operation, /*replaceAttrs=*/true,
                                   /*replaceLocs=*/false,
                                   /*replaceTypes=*/true);
    }
    // The walk is post-order, so a matched operation nested in another one is
    // erased before its ancestor.
    if (erase)
      for (Operation *operation : matched) {
        operation->dropAllUses();
        operation->erase();
      }
    if (from && !replaced) {
      module.emitError("test-talos-edit-attributes replaced nothing");
      return signalPassFailure();
    }
  }

private:
  std::string unescape(StringRef text) {
    std::string source = text.str();
    for (size_t at = source.find("\\\""); at != std::string::npos;
         at = source.find("\\\"", at + 1))
      source.erase(at, 1);
    return source;
  }

  DictionaryAttr parseDictionary(StringRef text, StringRef option) {
    if (text.empty())
      return DictionaryAttr::get(&getContext());
    // The pass-option parser strips the braces of a `{...}` value.
    std::string source = unescape(text);
    if (!StringRef(source).starts_with("{"))
      source = "{" + source + "}";
    auto dictionary = dyn_cast_or_null<DictionaryAttr>(
        parseAttribute(source, &getContext()));
    if (!dictionary)
      getOperation().emitError()
          << option << " is not a dictionary attribute: " << text;
    return dictionary;
  }

  /// A type parses as a TypeAttr, which selects type replacement.
  Attribute parseElement(StringRef text, StringRef option) {
    Attribute element = parseAttribute(unescape(text), &getContext());
    if (!element)
      getOperation().emitError()
          << option << " is not an attribute or type: " << text;
    return element;
  }
};

/// Graph traversals must stay iterative on generated CFGs with tens of
/// thousands of blocks. A `cf.br` carrying `test.chain_length = N` becomes a
/// chain of N empty blocks that ends at the branch's original successor.
class InsertBlockChainTestPass
    : public PassWrapper<InsertBlockChainTestPass, OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(InsertBlockChainTestPass)

  StringRef getArgument() const final {
    return "test-talos-insert-block-chain";
  }
  StringRef getDescription() const final {
    return "expand each branch marked test.chain_length into a block chain";
  }

  void runOnOperation() final {
    SmallVector<cf::BranchOp> branches;
    getOperation().walk([&](cf::BranchOp branch) {
      if (branch->hasAttr("test.chain_length"))
        branches.push_back(branch);
    });
    OpBuilder builder(&getContext());
    for (cf::BranchOp branch : branches) {
      auto length = branch->getAttrOfType<IntegerAttr>("test.chain_length");
      if (!length || length.getInt() < 0 ||
          !branch.getDestOperands().empty()) {
        branch.emitError("test.chain_length must be a non-negative integer on "
                         "a branch without operands");
        return signalPassFailure();
      }
      branch->removeAttr("test.chain_length");
      // Build backwards from the successor so block order follows the chain.
      Block *next = branch.getDest();
      for (int64_t index = 0; index < length.getInt(); ++index) {
        Block *block = builder.createBlock(next);
        cf::BranchOp::create(builder, branch.getLoc(), next);
        next = block;
      }
      branch.setDest(next);
    }
  }
};


class UnrollLoopsTestPass
    : public PassWrapper<UnrollLoopsTestPass, OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(UnrollLoopsTestPass)

  StringRef getArgument() const final { return "test-talos-unroll-loops"; }
  StringRef getDescription() const final {
    return "fully unroll every constant-trip scf.for";
  }

  void runOnOperation() final {
    // Post-order visits inner loops first, so an outer body is straight-line
    // code by the time it is copied.
    SmallVector<scf::ForOp> loops;
    getOperation().walk([&](scf::ForOp loop) { loops.push_back(loop); });
    SetVector<Operation *> parents;
    for (scf::ForOp loop : loops) {
      parents.insert(loop->getParentOp());
      if (failed(loopUnrollFull(loop))) {
        loop.emitError("test-talos-unroll-loops needs a constant trip count");
        return signalPassFailure();
      }
    }
    // The input should read as if written without loops, so drop the bounds
    // and induction arithmetic the unrolled loops no longer use.
    for (Operation *parent : parents)
      parent->walk<WalkOrder::PostOrder, ReverseIterator>([&](Operation *op) {
        if (op != parent && isOpTriviallyDead(op) &&
            llvm::all_of(op->getResultTypes(),
                         [](Type type) { return type.isIndex(); }))
          op->erase();
      });
  }
};

/// Replaces each operation carrying `test.replicate = N` with N copies in
/// place. Copy k replaces `#` with k in every string and symbol name inside
/// it, including those in types such as class handles, so the copies of a
/// symbol and of its references stay distinct and consistent, and adds k times each stride of a `test.replicate_stride`
/// dictionary to the integer attribute of that name on the same operation or
/// on its function arguments. Copy k uses copy k of an earlier operation
/// replicated as many times, as an unrolled loop body would. An optional
/// `test.replicate_first = F` numbers the names from F instead of zero.
class ReplicateTestPass
    : public PassWrapper<ReplicateTestPass, OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(ReplicateTestPass)

  StringRef getArgument() const final { return "test-talos-replicate"; }
  StringRef getDescription() const final {
    return "replicate operations marked test.replicate with indexed names";
  }

  void runOnOperation() final {
    SmallVector<Operation *> marked;
    WalkResult walk = getOperation().walk<WalkOrder::PreOrder>(
        [&](Operation *operation) {
          if (!operation->hasAttr("test.replicate"))
            return WalkResult::advance();
          auto count =
              operation->getAttrOfType<IntegerAttr>("test.replicate");
          if (!count || count.getInt() < 1) {
            operation->emitError("test.replicate must be a positive integer");
            return WalkResult::interrupt();
          }
          marked.push_back(operation);
          // A nested mark would be copied before it is expanded.
          return WalkResult::skip();
        });
    if (walk.wasInterrupted())
      return signalPassFailure();
    DenseMap<Operation *, SmallVector<Operation *>> replicas;
    for (Operation *operation : marked) {
      int64_t count =
          operation->getAttrOfType<IntegerAttr>("test.replicate").getInt();
      operation->removeAttr("test.replicate");
      int64_t first = 0;
      if (auto attribute =
              operation->getAttrOfType<IntegerAttr>("test.replicate_first"))
        first = attribute.getInt();
      operation->removeAttr("test.replicate_first");
      OpBuilder builder(operation);
      builder.setInsertionPointAfter(operation);
      SmallVector<Operation *> &copies = replicas[operation];
      copies.push_back(operation);
      for (int64_t index = 1; index < count; ++index) {
        IRMapping mapping;
        operation->walk([&](Operation *user) {
          for (Value operand : user->getOperands()) {
            auto found = replicas.find(operand.getDefiningOp());
            if (found != replicas.end() &&
                found->second.size() == static_cast<size_t>(count))
              mapping.map(operand,
                          found->second[index]->getResult(
                              cast<OpResult>(operand).getResultNumber()));
          }
        });
        copies.push_back(builder.insert(operation->clone(mapping)));
      }
      for (auto [index, copy] : llvm::enumerate(copies))
        if (failed(instantiate(copy, index, first + index)))
          return signalPassFailure();
    }
  }

private:
  LogicalResult instantiate(Operation *copy, uint64_t index, int64_t name) {
    WalkResult strides = copy->walk([&](Operation *operation) {
      auto stride =
          operation->getAttrOfType<DictionaryAttr>("test.replicate_stride");
      if (!stride)
        return WalkResult::advance();
      operation->removeAttr("test.replicate_stride");
      auto function = dyn_cast<FunctionOpInterface>(operation);
      for (NamedAttribute entry : stride) {
        auto step = dyn_cast<IntegerAttr>(entry.getValue());
        auto offset = [&](IntegerAttr base) {
          return IntegerAttr::get(base.getType(),
                                  base.getValue() + step.getValue() * index);
        };
        bool found = false;
        if (auto base = operation->getAttrOfType<IntegerAttr>(entry.getName());
            step && base) {
          operation->setAttr(entry.getName(), offset(base));
          found = true;
        }
        for (unsigned argument = 0;
             step && function && argument < function.getNumArguments();
             ++argument)
          if (auto base = function.getArgAttrOfType<IntegerAttr>(
                  argument, entry.getName())) {
            function.setArgAttr(argument, entry.getName(), offset(base));
            found = true;
          }
        if (!found) {
          operation->emitError("test.replicate_stride names a missing "
                               "integer attribute: ")
              << entry.getName();
          return WalkResult::interrupt();
        }
      }
      return WalkResult::advance();
    });
    if (strides.wasInterrupted())
      return failure();
    std::string text = std::to_string(name);
    AttrTypeReplacer replacer;
    replacer.addReplacement([&](StringAttr string) -> std::optional<Attribute> {
      if (!string.getValue().contains('#'))
        return std::nullopt;
      std::string value = string.getValue().str();
      for (size_t at = value.find('#'); at != std::string::npos;
           at = value.find('#', at + text.size()))
        value.replace(at, 1, text);
      return StringAttr::get(string.getContext(), value);
    });
    replacer.recursivelyReplaceElementsIn(copy, /*replaceAttrs=*/true,
                                          /*replaceLocs=*/false,
                                          /*replaceTypes=*/true);
    return success();
  }
};
} // namespace

namespace talos {
void registerModuleTestPasses() {
  PassRegistration<PrefixSymbolsTestPass>();
  PassRegistration<ExportLLVMSymbolsTestPass>();
  PassRegistration<SetModuleAttributesTestPass>();
  PassRegistration<EditAttributesTestPass>();
  PassRegistration<InsertBlockChainTestPass>();
  PassRegistration<UnrollLoopsTestPass>();
  PassRegistration<ReplicateTestPass>();
}
} // namespace talos
