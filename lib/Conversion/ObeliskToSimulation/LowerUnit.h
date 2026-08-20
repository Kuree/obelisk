//===- LowerUnit.h - Frozen semantic unit lowering internals ----*- C++ -*-===//
//
// Shared implementation state for the semantic groups that lower one frozen
// `obelisk_sim.func`. This is private to the Obelisk-to-simulation conversion.
//
//===----------------------------------------------------------------------===//

#ifndef OBELISK_LIB_CONVERSION_OBELISKTOSIMULATION_LOWERUNIT_H
#define OBELISK_LIB_CONVERSION_OBELISKTOSIMULATION_LOWERUNIT_H

#include "Detail.h"

#include "mlir/IR/IRMapping.h"

#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"

#include <string>
#include <vector>

namespace obelisk::simlowering {

struct ContainerElementDescriptor {
  uint64_t typeID;
  uint32_t kind;
  uint32_t flags;
  uint64_t valueSize;
  uint64_t alignment;
  uint64_t bitWidth;
  ::mlir::SmallVector<int64_t, 2> traceOffsets;
  ::mlir::SmallVector<int32_t, 2> traceKinds;
};

::mlir::FailureOr<ContainerElementDescriptor>
describeContainerElement(::mlir::Type type, ::mlir::Location location);

::mlir::FailureOr<::mlir::Value>
lowerStringLiteralValue(::mlir::OpBuilder &builder,
                        ::mlir::Operation *operation, ::mlir::Type type,
                        ::mlir::Location location);

class UnitLowering {
public:
  explicit UnitLowering(sim::SimFuncOp function);

  ::mlir::LogicalResult lower(::mlir::ArrayRef<::mlir::Operation *> roots);

private:
  struct LoweredOutputList {
    ::mlir::SmallVector<::mlir::Value> items;
    ::mlir::SmallVector<int32_t> flags;
  };

  struct CapturedLValue {
    enum class Kind {
      Reference,
      PackedDynamicSlice,
      PackedValueSlice,
      ContainerElement,
      AssociativeElement,
      AggregateElement,
      AggregateSlice,
      StringCharacter,
      Concatenation,
    };

    Kind kind = Kind::Reference;
    ::mlir::Operation *semanticNode = nullptr;
    ::mlir::Type type;
    ::mlir::Value reference;
    ::mlir::Value container;
    ::mlir::Value index;
    uint64_t lowBit = 0;
    /// Out-of-range bits added on either side of a packed slice's base so that
    /// a select wider than the base still names one contiguous window.
    uint64_t padding = 0;
    unsigned ordinal = 0;
    std::vector<CapturedLValue> children;
  };

  struct PackedSelectionAddress {
    ::mlir::Type scalarResultType;
    bool constant = false;
    uint64_t lowBit = 0;
    ::mlir::Value dynamicLow;
    /// See CapturedLValue::padding. `dynamicLow` already counts it.
    uint64_t padding = 0;
  };

  ::mlir::FailureOr<::mlir::Value> lowerExpression(::mlir::Operation *op,
                                                   bool lvalue = false);
  ::mlir::FailureOr<PackedSelectionAddress>
  lowerPackedSelectionAddress(::mlir::Operation *op,
                              ::mlir::Type sourceValueType,
                              ::mlir::Type resultType);
  /// Surround `scalar` with `padding` out-of-range bits on either side. Reads
  /// of the padding see x, matching what IEEE 1800-2017 11.5.1 requires of a
  /// select position outside the value.
  ::mlir::FailureOr<::mlir::Value> padSelectionWindow(::mlir::Value scalar,
                                                      uint64_t padding,
                                                      ::mlir::Location location);
  /// Recover the original value from a window produced by padSelectionWindow,
  /// dropping whatever a write placed in the padding.
  ::mlir::FailureOr<::mlir::Value>
  unpadSelectionWindow(::mlir::Value padded, uint64_t padding,
                       ::mlir::Type scalarType, ::mlir::Location location);
  /// Emit the source index of every element an unpacked-array range selection
  /// of `source` produces, in result order. `bounds` are the selection's two
  /// index expressions and `count` the number of elements selected.
  ::mlir::FailureOr<::mlir::SmallVector<::mlir::Value>>
  unpackedSliceIndices(semantic::SVRangeSelectExpressionOp range,
                       ::mlir::ArrayRef<::mlir::Operation *> bounds,
                       sim::UnpackedArrayType source, unsigned count,
                       ::mlir::Location location);
  ::mlir::FailureOr<::mlir::Value>
  lowerContextDeterminedExpression(::mlir::Operation *op);
  ::mlir::FailureOr<::mlir::Value>
  lowerNamedValue(semantic::SVNamedValueExpressionOp op, bool lvalue);
  ::mlir::FailureOr<::mlir::Value> lowerReferencedValue(::mlir::Operation *op,
                                                        ::mlir::StringRef path,
                                                        bool lvalue);
  ::mlir::FailureOr<::mlir::Value> lowerLiteral(::mlir::Operation *op);
  ::mlir::FailureOr<::mlir::Value> lowerConcatenation(::mlir::Operation *op);
  ::mlir::FailureOr<::mlir::Value> lowerReplication(::mlir::Operation *op);
  ::mlir::FailureOr<::mlir::Value>
  lowerStreaming(semantic::SVStreamingConcatenationExpressionOp op,
                 ::mlir::Type assignmentType = {});
  bool streamContainsFourState(::mlir::Type type) const;
  bool streamNodeContainsFourState(::mlir::Operation *node) const;
  ::mlir::FailureOr<::mlir::Value> createBitStream(bool fourState,
                                                   ::mlir::Location location);
  ::mlir::FailureOr<::mlir::Value> appendToBitStream(::mlir::Value value,
                                                     ::mlir::Value stream,
                                                     ::mlir::Value outputIndex,
                                                     bool fourState,
                                                     ::mlir::Location location);
  ::mlir::FailureOr<::mlir::Value> reorderBitStream(::mlir::Value stream,
                                                    uint64_t slice,
                                                    ::mlir::Location location);
  ::mlir::FailureOr<::mlir::Value>
  sliceStreamingContainer(::mlir::Value container, ::mlir::Operation *withRange,
                          ::mlir::Location location);
  ::mlir::FailureOr<::mlir::Value>
  lowerMember(semantic::SVMemberAccessExpressionOp op, bool lvalue);
  ::mlir::FailureOr<::mlir::Value>
  lowerVirtualInterfaceMember(semantic::SVMemberAccessExpressionOp op,
                              ::mlir::Value interface, ::mlir::Type elementType,
                              bool lvalue);
  ::mlir::FailureOr<::mlir::Value>
  lowerVirtualInterfaceClock(semantic::SVMemberAccessExpressionOp op,
                             ::mlir::Value interface);
  ::mlir::LogicalResult guardTaggedUnionMember(::mlir::Value input,
                                               unsigned ordinal,
                                               ::mlir::Location location);
  ::mlir::FailureOr<::mlir::Value>
  lowerTaggedUnion(semantic::SVTaggedUnionExpressionOp op);
  ::mlir::FailureOr<::mlir::Value>
  lowerAssignmentPattern(::mlir::Operation *op);
  ::mlir::FailureOr<::mlir::Value> lowerNewArray(::mlir::Operation *op);
  ::mlir::FailureOr<::mlir::Value> lowerSelection(::mlir::Operation *op,
                                                  bool lvalue);
  ::mlir::FailureOr<::mlir::Value>
  lowerAssignment(semantic::SVAssignmentExpressionOp op);
  ::mlir::FailureOr<::mlir::Value> lowerStreamingAssignment(
      semantic::SVStreamingConcatenationExpressionOp destination,
      ::mlir::Value source);
  ::mlir::FailureOr<::mlir::Value>
  readBitStreamValue(::mlir::Value stream, ::mlir::Value start,
                     ::mlir::Type type, ::mlir::Location location);
  ::mlir::LogicalResult lowerClockingOutputAssignment(
      semantic::SVMemberAccessExpressionOp destination, ::mlir::Value value,
      ::mlir::Location location);
  ::mlir::FailureOr<CapturedLValue>
  captureLValue(::mlir::Operation *destination, ::mlir::Location location);
  ::mlir::FailureOr<::mlir::Value>
  loadCapturedLValue(const CapturedLValue &destination,
                     ::mlir::Location location);
  ::mlir::LogicalResult writeCapturedLValue(CapturedLValue &destination,
                                            ::mlir::Value value,
                                            bool sourceSigned, bool nonblocking,
                                            ::mlir::Location location,
                                            ::mlir::Value delay = {});
  bool haveSameCapturedStorage(const CapturedLValue &lhs,
                               const CapturedLValue &rhs) const;
  void propagateCapturedContainers(const CapturedLValue &source,
                                   CapturedLValue &destination);
  void appendCapturedValues(const CapturedLValue &destination,
                            ::mlir::SmallVectorImpl<::mlir::Value> &values);
  ::mlir::LogicalResult replaceCapturedValues(CapturedLValue &destination,
                                              ::mlir::ValueRange values,
                                              unsigned &next);
  ::mlir::LogicalResult writeLValue(::mlir::Operation *destination,
                                    ::mlir::Value value, bool sourceSigned,
                                    bool nonblocking, ::mlir::Location location,
                                    ::mlir::Value delay = {});
  ::mlir::FailureOr<::mlir::Value> lowerUnary(semantic::SVUnaryExpressionOp op);
  ::mlir::FailureOr<::mlir::Value>
  lowerBinary(semantic::SVBinaryExpressionOp op);
  ::mlir::FailureOr<::mlir::Value>
  lowerConditionalExpression(semantic::SVConditionalExpressionOp op);
  ::mlir::FailureOr<::mlir::Value>
  conditionalPredicate(::mlir::Value value, ::mlir::Location location);
  ::mlir::FailureOr<::mlir::Value>
  conditionalEqual(::mlir::Value lhs, ::mlir::Value rhs, ::mlir::Type type,
                   ::mlir::Location location, bool caseEquality = false);
  ::mlir::FailureOr<::mlir::Value> logicalEqual(::mlir::Value lhs,
                                                ::mlir::Value rhs,
                                                ::mlir::Type type,
                                                ::mlir::Location location);
  ::mlir::FailureOr<::mlir::Value>
  mergeConditionalValues(::mlir::Value condition, ::mlir::Value trueValue,
                         ::mlir::Value falseValue, ::mlir::Type type,
                         ::mlir::Location location);
  ::mlir::FailureOr<::mlir::Value>
  lowerInside(semantic::SVInsideExpressionOp op);
  ::mlir::FailureOr<::mlir::Value> lowerCall(semantic::SVCallExpressionOp op);
  ::mlir::FailureOr<::mlir::Value>
  lowerNewCovergroup(semantic::SVNewCovergroupExpressionOp op);
  ::mlir::FailureOr<::mlir::Value>
  lowerCovergroupCall(semantic::SVCallExpressionOp op,
                      semantic::SVCovergroupTypeOp covergroup);
  ::mlir::FailureOr<::mlir::Value>
  lowerCovergroupSample(semantic::SVCallExpressionOp op,
                        semantic::SVCovergroupTypeOp covergroup,
                        ::mlir::Value handle, ::mlir::Value classOwner = {});
  semantic::SVCovergroupTypeOp
  findSemanticCovergroup(::mlir::Operation *operation);
  ::mlir::FailureOr<::mlir::Value>
  lowerArrayMethod(semantic::SVCallExpressionOp op,
                   ::mlir::Value receiverOverride = {},
                   ::mlir::Value iteratorKeys = {});
  ::mlir::FailureOr<::mlir::Value>
  lowerAssociativeArrayMethod(semantic::SVCallExpressionOp op);
  ::mlir::FailureOr<::mlir::Value>
  lowerNewClass(semantic::SVNewClassExpressionOp op);
  ::mlir::FailureOr<::mlir::Value>
  lowerRandomize(semantic::SVCallExpressionOp op,
                 ::mlir::Value receiverOverride = {});
  ::mlir::FailureOr<::mlir::Value>
  lowerSystemCall(semantic::SVCallExpressionOp op);
  ::mlir::FailureOr<::mlir::Value> lowerAlternateClockSample(
      ::mlir::Operation *expression, ::mlir::Operation *gateExpression,
      semantic::SVSignalEventControlOp clock, uint64_t depth, uint64_t age,
      ::mlir::Location location);
  ::mlir::FailureOr<::mlir::Value>
  lowerClockingInputSample(::mlir::Value source, uint64_t sourceDescriptor,
                           ::mlir::Value clock, uint64_t clockDescriptor,
                           sim::EdgeKind edge, bool oneStep,
                           ::mlir::Location location);
  ::mlir::FailureOr<::mlir::Value>
  lowerArrayQuerySystemCall(semantic::SVCallExpressionOp op);
  ::mlir::FailureOr<::mlir::Value>
  lowerDisplaySystemCall(semantic::SVCallExpressionOp op);
  ::mlir::FailureOr<LoweredOutputList>
  lowerOutputListItems(::mlir::ArrayRef<::mlir::Operation *> operations,
                       bool interpretLiteralsAsFormats,
                       std::optional<unsigned> designatedFormat = std::nullopt);
  ::mlir::FailureOr<::mlir::Value>
  lowerStringFormatSystemCall(semantic::SVCallExpressionOp op);
  ::mlir::FailureOr<::mlir::Value>
  lowerDumpSystemCall(semantic::SVCallExpressionOp op);
  ::mlir::FailureOr<::mlir::Value>
  lowerFileSystemCall(semantic::SVCallExpressionOp op);
  ::mlir::FailureOr<::mlir::Value>
  lowerPlusargSystemCall(semantic::SVCallExpressionOp op);
  ::mlir::FailureOr<::mlir::Value>
  lowerScanSystemCall(semantic::SVCallExpressionOp op);
  ::mlir::FailureOr<::mlir::Value>
  lowerRealConversionSystemCall(semantic::SVCallExpressionOp op);
  ::mlir::FailureOr<::mlir::Value>
  lowerRealMathSystemCall(semantic::SVCallExpressionOp op);
  ::mlir::LogicalResult initializeObjectRandomStream(::mlir::Value object,
                                                     ::mlir::Location location);
  ::mlir::LogicalResult lowerPortConnection(semantic::SVPortConnectionOp op);

  ::mlir::LogicalResult lowerStatement(::mlir::Operation *op);
  ::mlir::LogicalResult
  lowerSequence(::mlir::ArrayRef<::mlir::Operation *> operations);
  ::mlir::LogicalResult
  lowerPrimitive(::mlir::StringRef name,
                 ::mlir::ArrayRef<::mlir::Operation *> operations);
  ::mlir::LogicalResult
  lowerImmediateAssertion(semantic::SVImmediateAssertionStatementOp op);
  ::mlir::LogicalResult
  lowerConcurrentAssertion(semantic::SVConcurrentAssertionStatementOp op);
  ::mlir::LogicalResult
  lowerSequenceEndpointMonitor(::mlir::ArrayRef<::mlir::Operation *> roots);
  void emitDefaultAssertionFailure(
      ::mlir::Location location,
      ::llvm::StringRef description = "immediate assertion");
  ::mlir::LogicalResult emitRuntimeFatal(::mlir::Location location,
                                         ::mlir::StringRef message);
  ::mlir::LogicalResult lowerConditional(semantic::SVConditionalStatementOp op);
  ::mlir::LogicalResult
  lowerQualifiedConditional(semantic::SVConditionalStatementOp op);
  ::mlir::LogicalResult lowerCase(semantic::SVCaseStatementOp op);
  ::mlir::LogicalResult lowerPatternCase(semantic::SVPatternCaseStatementOp op);
  ::mlir::LogicalResult lowerRandCase(semantic::SVRandCaseStatementOp op);
  ::mlir::LogicalResult
  lowerRandSequence(semantic::SVRandSequenceStatementOp op);
  ::mlir::LogicalResult
  lowerRandSequenceProduction(semantic::SVFrozenRandSeqProductionOp production,
                              ::mlir::ArrayRef<::mlir::Operation *> actuals);
  ::mlir::LogicalResult
  lowerRandSequenceProductionItem(semantic::SVProdItemOp item);
  ::mlir::LogicalResult lowerRandSequenceNode(::mlir::Operation *node);
  ::mlir::FailureOr<::mlir::Value>
  lowerPattern(::mlir::Value input, ::mlir::Operation *pattern,
               semantic::SVCaseCondition condition,
               ::llvm::StringMap<::mlir::Value> *captures = nullptr);
  ::mlir::FailureOr<::mlir::Value>
  lowerCaseLabel(::mlir::Value selector, ::mlir::Type selectorType,
                 ::mlir::Operation *selectorNode, ::mlir::Operation *label,
                 semantic::SVCaseCondition condition);
  void emitQualifierWarning(::mlir::Location location,
                            semantic::SVUniquePriorityCheck qualifier,
                            ::mlir::StringRef statementKind,
                            ::mlir::StringRef reason);
  ::mlir::LogicalResult lowerWhile(::mlir::Operation *op);
  ::mlir::LogicalResult lowerDoWhile(::mlir::Operation *op);
  ::mlir::LogicalResult lowerFor(semantic::SVForLoopStatementOp op);
  ::mlir::LogicalResult lowerForever(::mlir::Operation *op);
  ::mlir::LogicalResult lowerForeach(semantic::SVForeachLoopStatementOp op);
  ::mlir::LogicalResult lowerRepeat(::mlir::Operation *op);
  ::mlir::LogicalResult lowerFork(semantic::SVBlockStatementOp op);
  ::mlir::LogicalResult lowerBlock(semantic::SVBlockStatementOp op);
  ::mlir::LogicalResult lowerDisable(semantic::SVDisableStatementOp op);
  ::mlir::FailureOr<
      std::pair<sim::SimFuncOp, ::mlir::SmallVector<::mlir::Value>>>
  outlineForkBranch(
      ::mlir::Operation *branch, uint64_t forkNode, unsigned branchIndex,
      bool captureReferences = false,
      ::mlir::ArrayRef<std::pair<::mlir::Operation *, ::mlir::Value>>
          expressionCaptures = {});
  ::mlir::FailureOr<
      std::pair<sim::SimFuncOp, ::mlir::SmallVector<::mlir::Value>>>
  outlinePostponedDisplay(semantic::SVCallExpressionOp call,
                          ::mlir::StringRef immediateName, bool persistent);
  ::mlir::LogicalResult
  lowerVariableDeclaration(semantic::SVVariableDeclStatementOp op);
  ::mlir::LogicalResult lowerTiming(::mlir::Operation *control,
                                    ::mlir::Operation *statement);
  ::mlir::LogicalResult
  emitEventSuspend(::mlir::Operation *control, ::mlir::Block *continuation,
                   ::mlir::ValueRange continuationOperands = {});
  ::mlir::LogicalResult
  emitRepeatedEventSuspend(::mlir::Operation *control,
                           ::mlir::Block *continuation,
                           ::mlir::ValueRange continuationOperands = {});
  ::mlir::FailureOr<::mlir::Value> lowerDelayValue(::mlir::Operation *control);
  ::mlir::LogicalResult lowerWait(semantic::SVWaitStatementOp op);
  ::mlir::LogicalResult
  lowerEventTrigger(semantic::SVEventTriggerStatementOp op);
  ::mlir::FailureOr<::mlir::Value>
  bindObserver(::mlir::Operation *expression,
               ::mlir::ValueRange dynamicDependencies = {},
               bool includeStaticDependencies = true);
  /// Whether an addressable event expression also lowers to a handle the
  /// scheduler can watch. A net is viewed only through packed windows of its
  /// storage, so selecting an element of an unpacked array of nets has to be
  /// observed instead.
  bool hasWatchableSignalHandle(::mlir::Operation *expression);
  void recordSensitivity(::mlir::Value value);
  void recordManagedRead(::mlir::Value reference, ::mlir::Location location);
  void recordContainerSizeRead(::mlir::Value container,
                               ::mlir::Location location);

  ::mlir::FailureOr<::mlir::Value>
  convert(::mlir::Value value, ::mlir::Type targetType, bool sourceSigned,
          ::mlir::Location location, bool targetSigned = false);
  ::mlir::FailureOr<::mlir::Value> toPackedScalar(::mlir::Value value,
                                                  ::mlir::Location location);
  /// Convert an integral queue / dynamic-array index to the signed runtime
  /// representation without conflating an X/Z-containing four-state index
  /// with integer zero.  The container runtime already defines every negative
  /// index as invalid, so -1 is the common sentinel for an unknown index.
  ::mlir::FailureOr<::mlir::Value>
  toContainerIndex(::mlir::Value value, bool sourceSigned,
                   ::mlir::Location location);
  ::mlir::FailureOr<::mlir::Value>
  formatTaggedUnionPattern(::mlir::Value value, ::mlir::Type semanticType,
                           ::mlir::Location location);
  // Render an unpacked struct, array, or untagged union as the assignment
  // pattern IEEE 1800-2017 21.2.1.7 gives %p. The shape is static, so the
  // pattern punctuation becomes a format string and the singular elements
  // become its arguments.
  ::mlir::FailureOr<::mlir::Value>
  formatUnpackedAggregatePattern(::mlir::Value value,
                                 ::mlir::Location location);
  ::mlir::IntegerAttr designTimePrecisionExponent();
  // Map an assignment-pattern index key onto the element ordinal the aggregate
  // stores it at. IEEE 1800-2017 10.9.1 writes the key in the array's own
  // declared index range, which may descend and may start below zero.
  ::mlir::FailureOr<int64_t> declaredIndexOrdinal(::mlir::Type aggregate,
                                                  ::mlir::Operation *key);
  // Build one element from an assignment pattern's `default:` value. IEEE
  // 1800-2017 10.9.2 applies the value directly to a singular member and
  // recurses into an aggregate one.
  ::mlir::FailureOr<::mlir::Value>
  materializeDefaultElement(::mlir::Value value, ::mlir::Type elementType,
                            ::mlir::Operation *source,
                            ::mlir::Location location);
  // The current simulation time expressed in the code unit's time units, the
  // scale IEEE 1800-2017 20.3.1 gives $time and 21.2.1.5 gives %t.
  ::mlir::FailureOr<::mlir::Value>
  currentTimeInUnits(::mlir::Location location);
  // Emit the IEEE 1800-2017 20.2 Table 20-1 diagnostic for a $finish or $stop
  // whose verbosity is not the constant 0.
  ::mlir::LogicalResult
  emitTerminationDiagnostic(::mlir::StringRef name, ::mlir::Value verbosity,
                            ::mlir::Location location);
  ::mlir::FailureOr<::mlir::Value> truthValue(::mlir::Value value,
                                              ::mlir::Location location);
  ::mlir::FailureOr<::mlir::Value> toLogic(::mlir::Value value,
                                           ::mlir::Location location);
  ::mlir::Value cloneSequentialValue(::mlir::Value value,
                                     ::mlir::Location location);
  ::mlir::FailureOr<::mlir::Value>
  ensureSequentialContainer(::mlir::Value value, ::mlir::Location location);
  ::mlir::FailureOr<::mlir::Value> createAssocArray(sim::AssocArrayType type,
                                                    ::mlir::Location location);
  ::mlir::FailureOr<::mlir::Value> ensureAssocArray(::mlir::Value value,
                                                    ::mlir::Location location);
  ::mlir::FailureOr<std::pair<::mlir::Value, ::mlir::Value>>
  traverseAssoc(::mlir::Value array, ::mlir::Value key, int32_t direction,
                bool endpoint, ::mlir::Location location);
  ::mlir::Type getReferenceElementType(::mlir::Value reference) const;
  ::mlir::FailureOr<::mlir::Value> loadReference(::mlir::Value reference,
                                                 ::mlir::Location location);
  ::mlir::LogicalResult storeReference(::mlir::Value reference,
                                       ::mlir::Value value,
                                       ::mlir::Location location);
  ::mlir::FailureOr<::mlir::Value>
  toArgumentReference(::mlir::Value reference, ::mlir::Type elementType,
                      ::mlir::Location location);
  ::mlir::LogicalResult
  emitFunctionReturn(::mlir::Location location,
                     std::optional<::mlir::Value> explicitResult,
                     bool resultSigned = false);
  ::mlir::Block *addBlock();
  void setCurrent(::mlir::Block *block);
  bool isCurrentClockingOccurrence(::mlir::Block *block) const;
  void emitBranch(::mlir::Block *destination);
  void emitControlLeaves(size_t first, ::mlir::Location location);
  ::mlir::InFlightDiagnostic unsupported(::mlir::Operation *op);
  void recordImplicitWrite(::mlir::Value value);
  void ensureVirtualInterfaceInventory();
  void ensureCoverageInventory();

  static bool isSignedNode(::mlir::Operation *op) {
    if (auto isSigned = op->getAttrOfType<::mlir::BoolAttr>("is_signed"))
      return isSigned.getValue();
    auto type = op->getAttrOfType<::mlir::TypeAttr>("semantic_type");
    return type && isSignedSemanticType(type.getValue());
  }

  sim::SimFuncOp function;
  ::mlir::OpBuilder builder;
  ::mlir::Block *current;
  /// SSA remapping for the short prepared initializer fragment encountered
  /// between semantic constructor statements.
  ::mlir::IRMapping preparedInitializerMapping;
  /// Values captured before an outlined callback replaces the corresponding
  /// cloned expression. This is used when an expression's source-region value
  /// must not be recomputed in the callback's later scheduling region.
  ::llvm::DenseMap<::mlir::Operation *, ::mlir::Value> expressionCaptures;
  ::llvm::StringMap<::mlir::Value> values;
  ::llvm::StringMap<::mlir::Value> lvalues;
  ::llvm::DenseMap<uint64_t, ::mlir::Value> nodeLvalues;
  ::llvm::StringMap<::mlir::Value> localDefaults;
  ::llvm::StringSet<> automaticLocals;
  ::llvm::StringMap<::mlir::Value> copyOutDestinations;
  ::llvm::StringMap<::mlir::Value> iteratorIndices;
  bool virtualInterfaceInventoryReady = false;
  ::llvm::StringMap<uint64_t> scopeIDs;
  using VirtualMemberTargets =
      ::mlir::SmallVector<std::pair<uint64_t, uint64_t>>;
  ::llvm::StringMap<VirtualMemberTargets> virtualInterfaceStorageMembers;
  ::llvm::StringMap<VirtualMemberTargets> virtualInterfaceNetMembers;
  ::llvm::DenseMap<uint64_t, ::mlir::Value> virtualInterfaceStorageHandles;
  ::llvm::DenseMap<uint64_t, ::mlir::Value> virtualInterfaceNetHandles;
  ::llvm::DenseMap<uint64_t, ::mlir::Type> virtualInterfaceStorageTypes;
  ::llvm::DenseMap<uint64_t, ::mlir::Type> virtualInterfaceNetTypes;
  ::llvm::SetVector<::mlir::Value> virtualInterfaceReadSensitivity;
  ::llvm::SetVector<::mlir::Value> virtualInterfaceWrittenSensitivity;
  ::llvm::DenseSet<::mlir::Block *> clockingEventContinuations;
  ::llvm::DenseSet<::mlir::Block *> timingBoundaryContinuations;
  bool coverageInventoryReady = false;
  ::llvm::StringMap<semantic::SVCovergroupTypeOp> semanticCovergroups;
  ::llvm::StringSet<> coverageDefinitionNames;
  ::mlir::Value thisObject;
  // The object of the scope containing a randomize() with call, live
  // only while its inline constraints are lowered. IEEE 1800-2017 18.7
  // resolves a name that the randomize() with object class does not
  // declare in that scope instead, so both objects are addressable.
  ::mlir::Value enclosingThisObject;
  ::mlir::Value taskControlActivation;
  ::llvm::SetVector<::mlir::Value> sensitivity;
  ::llvm::SetVector<::mlir::Value> *observedDependencies = nullptr;
  ::llvm::SetVector<::mlir::Value> *observedWrites = nullptr;
  ::mlir::Operation *topLevelWildcardControl = nullptr;
  ::mlir::Operation *activeSampledClock = nullptr;
  bool observeNonblockingWrites = false;
  ::mlir::Value expressionPlaceholder;
  ::mlir::Value unboundedPlaceholder;
  ::mlir::Value lvalueReferencePlaceholder;
  bool continuousStore = false;
  bool sampleAssertionValues = false;
  struct AlternateClockSamplePlan {
    uint64_t id;
    uint64_t depth;
    ::mlir::Type type;
  };
  ::llvm::StringMap<AlternateClockSamplePlan> alternateClockSamplePlans;
  ::mlir::SmallVector<::mlir::Value> randomizeCandidateValues;
  std::string returnPath;
  ::mlir::SmallVector<std::string> copyOutPaths;
  struct LoopTargets {
    ::mlir::Block *breakTarget;
    ::mlir::Block *continueTarget;
    ::mlir::SmallVector<::mlir::Value> continueOperands;
    size_t controlDepth;
  };
  ::mlir::SmallVector<LoopTargets> loopTargets;
  struct RandSequenceContext {
    ::llvm::StringMap<semantic::SVFrozenRandSeqProductionOp> productions;
    ::mlir::Block *breakTarget;
    size_t controlDepth;
  };
  ::mlir::SmallVector<RandSequenceContext> randSequenceContexts;
  struct RandSequenceProductionReturn {
    ::mlir::Block *target;
    size_t controlDepth;
  };
  ::mlir::SmallVector<RandSequenceProductionReturn>
      randSequenceProductionReturns;
  struct ControlScope {
    std::string path;
    uint64_t targetID;
    ::mlir::Value activation;
    ::mlir::Block *exit;
  };
  ::mlir::SmallVector<ControlScope> controlScopes;
  ::llvm::StringMap<uint64_t> inheritedControlIDs;
  ::llvm::StringMap<uint64_t> assertionControlIDs;
  uint64_t nextForkOrdinal = 0;
  uint64_t nextPostponedOrdinal = 0;
  bool invalidBindings = false;
};

} // namespace obelisk::simlowering

#endif // OBELISK_LIB_CONVERSION_OBELISKTOSIMULATION_LOWERUNIT_H
