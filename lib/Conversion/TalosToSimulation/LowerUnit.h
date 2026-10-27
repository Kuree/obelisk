//===- LowerUnit.h - Frozen semantic unit lowering internals ----*- C++ -*-===//
//
// Shared implementation state for the semantic groups that lower one frozen
// `simulation.func`. This is private to the Talos-to-simulation conversion.
//
//===----------------------------------------------------------------------===//

#ifndef TALOS_LIB_CONVERSION_TALOSTOSIMULATION_LOWERUNIT_H
#define TALOS_LIB_CONVERSION_TALOSTOSIMULATION_LOWERUNIT_H

#include "ContainerElementDescriptor.h"
#include "Detail.h"
#include "UnitLoweringInputs.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/IR/IRMapping.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"

#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace talos::simlowering {

uint64_t getStableTypeID(::mlir::Type type, uint32_t descriptorFlags = 0);

::mlir::FailureOr<::mlir::Value>
lowerStringLiteralValue(::mlir::OpBuilder &builder,
                        ::mlir::Operation *operation, ::mlir::Type type,
                        ::mlir::Location location);

class UnitLowering {
public:
  UnitLowering(sim::SimFuncOp function,
               const UnitLoweringInputs &loweringInputs);

  ::mlir::LogicalResult lower(::mlir::ArrayRef<::mlir::Operation *> roots);

  friend ::mlir::LogicalResult materializeCovergroupClockingSamplers(
      sim::SimDesignOp design, const UnitLoweringInputs &loweringInputs);

private:
  ::mlir::LogicalResult lowerRoots(::mlir::ArrayRef<::mlir::Operation *> roots);
  ::mlir::LogicalResult materializeProceduralAssertionFlushPoints();

  template <typename T>
  T lookupNearestSymbolFrom(::mlir::Operation *from,
                            ::mlir::SymbolRefAttr symbol) {
    // Source scopes and outlined clones inside this function are owned by this
    // worker. Keep their tables private so erasing those scopes cannot leave
    // stale operation pointers in the shared parent analysis.
    auto *scope = ::mlir::SymbolTable::getNearestSymbolTable(from);
    if (scope && function->isAncestor(scope))
      return localSymbols.lookupNearestSymbolFrom<T>(from, symbol);
    return loweringInputs.lookupNearestSymbolFrom<T>(from, symbol);
  }

  struct LoweredOutputList {
    ::mlir::SmallVector<::mlir::Value> items;
    ::mlir::SmallVector<int32_t> flags;
    /// The value produced directly from each semantic output operand. String
    /// literals and omitted operands have no entry value.
    ::mlir::SmallVector<::mlir::Value> sourceValues;
  };

  struct MonitorObservation {
    ::mlir::Value observer;
    ::mlir::Value initial;
    /// The exact declaration handle when the monitored expression is a
    /// direct named value. Such an expression changes iff this handle
    /// changes, so it needs no computed-observer subscription.
    ::mlir::Value directWatch;
  };

  struct CapturedLValue {
    enum class Kind {
      Reference,
      CovergroupOption,
      PackedDynamicSlice,
      PackedValueSlice,
      ContainerElement,
      ContainerSlice,
      AssociativeElement,
      AggregateElement,
      AggregateDynamicElement,
      AggregateSlice,
      StringCharacter,
      StringValueCharacter,
      Concatenation,
    };

    Kind kind = Kind::Reference;
    ::mlir::Operation *semanticNode = nullptr;
    ::mlir::Type type;
    ::mlir::Value reference;
    ::mlir::Value container;
    ::mlir::Value index;
    ::mlir::Value limit;
    /// Coverage members have logical storage in the runtime. Static members
    /// additionally retain their ordinary design reference for field reads.
    semantic::SVCovergroupTypeOp coverageGroup;
    ::mlir::Operation *coverageItem = nullptr;
    uint64_t coverageItemID = 0;
    sim::CovergroupInstanceOptionKind coverageOption =
        sim::CovergroupInstanceOptionKind::Weight;
    /// Dynamic source indices of an unpacked aggregate slice. They are
    /// captured once with the rest of the lvalue address and replayed in
    /// left-to-right result order.
    ::mlir::SmallVector<::mlir::Value> indices;
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

  struct QueueSliceAddress {
    ::mlir::Value start;
    ::mlir::Value count;
    ::mlir::Value valid;
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
  ::mlir::FailureOr<::mlir::Value>
  padSelectionWindow(::mlir::Value scalar, uint64_t padding,
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
  /// Resolve and clamp the two bounds of a queue slice. Unknown bounds and an
  /// empty or reversed interval produce a zero count, as required by IEEE
  /// 1800-2017 7.10.1.
  ::mlir::FailureOr<QueueSliceAddress>
  lowerQueueSliceAddress(semantic::SVRangeSelectExpressionOp range,
                         ::mlir::ArrayRef<::mlir::Operation *> bounds,
                         ::mlir::Value queue, ::mlir::Location location);
  ::mlir::FailureOr<::mlir::Value>
  lowerContextDeterminedExpression(::mlir::Operation *op);
  ::mlir::FailureOr<::mlir::Value>
  lowerNamedValue(semantic::SVNamedValueExpressionOp op, bool lvalue);
  ::mlir::FailureOr<::mlir::Value>
  lowerStaticClockingVariable(::mlir::Operation *op, bool lvalue);
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
  /// Appends the bit stream of `value` at `outputIndex` and returns the index
  /// after it. `allowHiddenRoot` exports a direct `this` class root with its
  /// local members.
  ::mlir::FailureOr<::mlir::Value>
  appendToBitStream(::mlir::Value value, ::mlir::Value stream,
                    ::mlir::Value outputIndex, bool fourState,
                    ::mlir::Location location, bool allowHiddenRoot = false);
  ::mlir::FailureOr<::mlir::Value> reorderBitStream(::mlir::Value stream,
                                                    uint64_t slice,
                                                    ::mlir::Location location,
                                                    ::mlir::Value limit = {});
  ::mlir::FailureOr<::mlir::Value> materializeDynamicBitStreamTarget(
      ::mlir::Value stream, ::mlir::Value totalWidth, ::mlir::Type targetType,
      ::mlir::Location location, ::mlir::Value packedSource = {},
      ::mlir::Value sourceStart = {});
  ::mlir::FailureOr<::mlir::Value> materializeCompositeBitStreamTarget(
      ::mlir::Value stream, ::mlir::Value totalWidth, ::mlir::Type targetType,
      ::mlir::Location location, ::mlir::Value packedSource = {});
  ::mlir::FailureOr<::mlir::Value>
  sliceStreamingContainer(::mlir::Value container, ::mlir::Operation *withRange,
                          ::mlir::Location location);
  ::mlir::FailureOr<::mlir::Value>
  lowerMember(semantic::SVMemberAccessExpressionOp op, bool lvalue);
  ::mlir::FailureOr<::mlir::Value>
  lowerVirtualInterfaceMember(semantic::SVMemberAccessExpressionOp op,
                              ::mlir::Value interface, ::mlir::Type elementType,
                              bool lvalue);
  ::mlir::FailureOr<::mlir::Value> lowerVirtualInterfaceInstanceMember(
      ::mlir::Value interface, sim::VirtualInterfaceType selectedInterface,
      ::mlir::StringAttr member, ::mlir::Location location);
  ::mlir::FailureOr<::mlir::Value>
  lowerVirtualInterfaceReceiverMembers(::mlir::Operation *expression,
                                       ::mlir::Value interface,
                                       ::mlir::Location location);
  /// The cached storage, net or driver handle of one virtual-interface target
  /// descriptor, created at function entry so every use is dominated.
  ::mlir::Value getVirtualInterfaceHandle(uint64_t descriptorID,
                                          ::mlir::Type handleType,
                                          ::mlir::Location location);
  /// The descriptors that reading virtual-interface member `op` can select at
  /// run time, with their handle type. Null when `op` reads no such storage
  /// or net.
  ::mlir::FailureOr<std::pair<
      const ::mlir::SmallVector<std::pair<uint64_t, uint64_t>> *, ::mlir::Type>>
  lookupVirtualInterfaceReadTargets(semantic::SVMemberAccessExpressionOp op);
  /// The storage and net handles that reading virtual-interface member `op`
  /// can select at run time.
  ::mlir::FailureOr<::mlir::SmallVector<::mlir::Value>>
  getVirtualInterfaceReadTargets(semantic::SVMemberAccessExpressionOp op);
  /// Whether `expression` reads a virtual-interface member that can select
  /// more than one storage, so its target changes with the interface handle.
  bool selectsAmongVirtualInterfaceTargets(::mlir::Operation *expression);
  /// Whether some elaborated interface instance has the type of `handle`.
  bool hasVirtualInterfaceInstance(sim::VirtualInterfaceType handle);
  /// Reports the runtime null-handle error of an access through a virtual
  /// interface that cannot select any instance, then continues in an
  /// unreachable block with a placeholder of `type`, if any.
  ::mlir::FailureOr<::mlir::Value>
  emitNullVirtualInterfaceAccess(::mlir::Type type, ::mlir::Location location);
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
  /// Lower one dynamic-array or queue index. Queue `$` expressions observe
  /// `container`'s current last index for reads and captured lvalues alike.
  ::mlir::FailureOr<::mlir::Value> lowerSequentialContainerIndex(
      ::mlir::Operation *index, ::mlir::Type containerType,
      ::mlir::Value container, ::mlir::Location location);
  ::mlir::FailureOr<::mlir::Value>
  lowerAssignment(semantic::SVAssignmentExpressionOp op);
  ::mlir::FailureOr<::mlir::Value> lowerStreamingAssignment(
      semantic::SVStreamingConcatenationExpressionOp destination,
      ::mlir::Value source);
  ::mlir::FailureOr<::mlir::Value>
  readBitStreamValue(::mlir::Value stream, ::mlir::Value start,
                     ::mlir::Type type, ::mlir::Location location);
  ::mlir::FailureOr<::mlir::Value>
  readBitStreamTarget(::mlir::Value stream, ::mlir::Value &cursor,
                      ::mlir::Type type, ::mlir::Location location);
  /// Lay the left-justified bit stream `packed` into a value of `type`, which
  /// may be a fixed unpacked aggregate. `highBit` is the position just above
  /// the next leaf's bits and drops as the stream is consumed.
  ::mlir::FailureOr<::mlir::Value>
  unflattenBitStreamValue(::mlir::Value packed, uint64_t &highBit,
                          ::mlir::Type type, ::mlir::Location location);
  std::optional<uint64_t> rawScanByteSize(::mlir::Type type, bool fourState);
  ::mlir::LogicalResult lowerClockingOutputAssignment(
      semantic::SVMemberAccessExpressionOp clockingVariable,
      ::mlir::Operation *destination, ::mlir::Value value,
      ::mlir::Location location,
      semantic::SVCycleDelayControlOp cycleDelay = {});
  ::mlir::LogicalResult lowerStaticClockingOutputAssignment(
      ::mlir::Operation *clockingVariable, ::mlir::Operation *destination,
      ::mlir::Value value, ::mlir::Location location,
      semantic::SVCycleDelayControlOp cycleDelay = {});
  ::mlir::FailureOr<::mlir::Value>
  lowerClockingOutputTarget(::mlir::Operation *destination,
                            ::mlir::Operation *clockingVariable,
                            ::mlir::Value target);
  ::mlir::LogicalResult emitCapturedClockingOutputDrive(
      ::mlir::Operation *destination, CapturedLValue &target,
      ::mlir::Value clock, ::mlir::Value value, ::mlir::Location location,
      semantic::SVCycleDelayControlOp cycleDelay, ::mlir::Value edgeSkewClock,
      unsigned &component);
  ::mlir::LogicalResult emitClockingOutputDrive(
      ::mlir::Operation *destination, ::mlir::Value target, ::mlir::Value clock,
      ::mlir::Value value, bool virtualInterface, ::mlir::Location location,
      ::mlir::Value virtualInterfaceHandle = {},
      ::mlir::Value edgeSkewClock = {},
      semantic::SVCycleDelayControlOp cycleDelay = {}, unsigned component = 0);
  ::mlir::FailureOr<CapturedLValue>
  captureLValue(::mlir::Operation *destination, ::mlir::Location location);
  ::mlir::FailureOr<::mlir::Value>
  loadCapturedLValue(const CapturedLValue &destination,
                     ::mlir::Location location);
  /// Whether a stored value may share sequential containers with other
  /// storage. Assignment copies a borrowed value, including containers nested
  /// in fixed aggregates (IEEE 1800-2023 7.6). A write-back rebuilt from the
  /// destination's own value and already copied parts is owned: storing it
  /// keeps the identity of the containers it holds.
  enum class StoredValue { Borrowed, Owned };
  ::mlir::LogicalResult writeCapturedLValue(CapturedLValue &destination,
                                            ::mlir::Value value,
                                            bool sourceSigned, bool nonblocking,
                                            ::mlir::Location location,
                                            ::mlir::Value delay = {}) {
    return writeCapturedLValue(destination, value, sourceSigned, nonblocking,
                               location, delay, StoredValue::Borrowed);
  }
  ::mlir::LogicalResult
  writeCapturedLValue(CapturedLValue &destination, ::mlir::Value value,
                      bool sourceSigned, bool nonblocking,
                      ::mlir::Location location, ::mlir::Value delay,
                      StoredValue stored);
  bool haveSameCapturedStorage(const CapturedLValue &lhs,
                               const CapturedLValue &rhs) const;
  void propagateCapturedContainers(const CapturedLValue &source,
                                   CapturedLValue &destination);
  /// Feeds the containers that `leaves` before `index` rebuild into that
  /// leaf, so sibling writes to one container compose in order.
  void
  propagatePreviousContainers(::llvm::MutableArrayRef<CapturedLValue> leaves,
                              size_t index);
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
  lowerConditionalExpression(semantic::SVConditionalExpressionOp op,
                             ::mlir::Type contextType = {});
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
  /// The [low, high] bounds of a tolerance range, possibly descending.
  ::mlir::FailureOr<std::pair<::mlir::Value, ::mlir::Value>>
  lowerToleranceBounds(semantic::SVValueRangeExpressionOp range,
                       ::mlir::Location location);
  ::mlir::FailureOr<::mlir::Value> lowerCall(semantic::SVCallExpressionOp op);
  /// Returns the variable expression an output or inout actual names, looking
  /// through the importer's `actual = <empty argument>` copy-out form.
  ::mlir::Operation *getCopyOutActual(::mlir::Operation *actual);
  /// Reads an inout actual on entry and converts it to the formal's type.
  ::mlir::FailureOr<::mlir::Value> copyInFromActual(::mlir::Operation *actual,
                                                    ::mlir::Type formalType,
                                                    bool formalSigned,
                                                    ::mlir::Location location);
  /// Assigns a returned output or inout formal to its actual. `scopeObject`
  /// is the receiver a defaulted actual is evaluated against.
  ::mlir::LogicalResult copyOutToActual(::mlir::Operation *actual,
                                        ::mlir::Value scopeObject,
                                        ::mlir::Value value, bool formalSigned,
                                        ::mlir::Location location);
  ::mlir::FailureOr<::mlir::Value>
  lowerNewCovergroup(semantic::SVNewCovergroupExpressionOp op,
                     semantic::SVCovergroupTypeOp covergroup = {},
                     bool dispatchInheritance = true);
  ::mlir::FailureOr<::mlir::Value>
  lowerCovergroupCall(semantic::SVCallExpressionOp op,
                      semantic::SVCovergroupTypeOp covergroup);
  ::mlir::FailureOr<std::optional<CapturedLValue>>
  captureCovergroupOption(semantic::SVMemberAccessExpressionOp field,
                          ::mlir::Location location);
  ::mlir::FailureOr<::mlir::Value>
  loadCapturedCovergroupOption(const CapturedLValue &destination,
                               ::mlir::Location location);
  ::mlir::LogicalResult
  storeCapturedCovergroupOption(const CapturedLValue &destination,
                                ::mlir::Value value, bool sourceSigned,
                                ::mlir::Location location);
  ::mlir::LogicalResult enqueueCapturedCovergroupOption(
      const CapturedLValue &destination, ::mlir::Value value,
      ::mlir::Value mask, ::mlir::Value index, bool character,
      ::mlir::Location location, ::mlir::Value delay);
  /// Record an assertion success for assertion coverage when `op` has an
  /// inventoried assertion point.
  void emitAssertionCoverageHit(::mlir::Operation *op,
                                ::mlir::Location location);
  ::mlir::FailureOr<::mlir::Value>
  insertCapturedPackedSlice(const CapturedLValue &selection, ::mlir::Value base,
                            ::mlir::Value replacement,
                            ::mlir::Location location);
  /// Zero or all-ones (in known bits) value of a fixed-bit-layout type.
  ::mlir::FailureOr<::mlir::Value>
  createFixedBitConstant(::mlir::Type type, bool ones,
                         ::mlir::Location location);
  /// Reference path to the element of the unpacked-array storage `base` at
  /// source index `index`.
  ::mlir::FailureOr<::mlir::Value>
  createAggregateElementPath(const CapturedLValue &base, ::mlir::Value index,
                             ::mlir::Location location);
  /// The class property, or the element of an unpacked-array property, under
  /// a chain of fixed selections ending at `destination`; `levels` collects
  /// the selections from `destination` upward. Null for any other target.
  static const CapturedLValue *getClassBackedSelectionRoot(
      const CapturedLValue &destination,
      ::llvm::SmallVectorImpl<const CapturedLValue *> &levels);
  /// Whether `destination` is a fixed selection of class-backed storage.
  static bool isClassBackedSelection(const CapturedLValue &destination);
  /// Enqueue a nonblocking update of a fixed selection of class-backed storage
  /// as a masked merge at commit.
  ::mlir::LogicalResult
  enqueueClassBackedSelectionNBA(const CapturedLValue &destination,
                                 ::mlir::Value value, ::mlir::Location location,
                                 ::mlir::Value delay);
  struct CovergroupOptionOwner {
    ::mlir::Operation *receiver;
    ::mlir::Operation *item = nullptr;
    uint64_t itemID = 0;
  };
  ::mlir::FailureOr<std::optional<CovergroupOptionOwner>>
  resolveCovergroupOptionOwner(::mlir::Operation *owner,
                               ::mlir::Location location);
  ::mlir::LogicalResult publishCovergroupTypeOption(
      semantic::SVCovergroupTypeOp covergroup, ::mlir::Operation *item,
      sim::CovergroupInstanceOptionKind kind, ::mlir::Value value,
      bool sourceSigned, ::mlir::Location location);
  ::mlir::FailureOr<std::optional<::mlir::Value>>
  lowerCovergroupOptionRead(semantic::SVMemberAccessExpressionOp op);
  ::mlir::FailureOr<::mlir::Value>
  lowerCovergroupSample(semantic::SVCallExpressionOp op,
                        semantic::SVCovergroupTypeOp covergroup,
                        ::mlir::Value handle, ::mlir::Value classOwner = {},
                        bool dispatchInheritance = true);
  ::mlir::FailureOr<::mlir::Value>
  emitCovergroupSample(semantic::SVCovergroupTypeOp covergroup,
                       ::mlir::Value handle, ::mlir::Value classOwner,
                       ::mlir::Location location,
                       ::mlir::ValueRange defaultValues = {},
                       ::mlir::ArrayRef<int64_t> defaultExpressionIDs = {},
                       ::mlir::ArrayRef<uint32_t> defaultResultOrdinals = {});
  ::mlir::LogicalResult deferCovergroupClockingSampler(
      semantic::SVNewCovergroupExpressionOp construct,
      semantic::SVCovergroupTypeOp covergroup, ::mlir::Value handle);
  ::mlir::LogicalResult deferCovergroupBlockEventSampler(
      semantic::SVNewCovergroupExpressionOp construct,
      semantic::SVCovergroupTypeOp covergroup, ::mlir::Value handle);
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
  ::mlir::FailureOr<::mlir::Value>
  lowerSampledValue(::mlir::Operation *expression, ::mlir::Location location);
  ::mlir::FailureOr<::mlir::Value>
  lowerAlternateClockSample(::mlir::Operation *expression,
                            ::mlir::Operation *gateExpression,
                            ::mlir::Operation *clock, uint64_t depth,
                            uint64_t age, ::mlir::Location location);
  /// The capture attributes of `value` when it is a descriptor-bound signal
  /// argument of this unit, which a clocked sampler can also capture.
  ::mlir::DictionaryAttr getSignalCapture(::mlir::Value value);
  bool canLowerClockedHistory(::mlir::Operation *expression,
                              ::mlir::Operation *gateExpression,
                              ::mlir::Operation *clock, bool callsAllowed);
  ::mlir::FailureOr<::mlir::Value>
  lowerClockedHistory(::mlir::Operation *expression,
                      ::mlir::Operation *gateExpression,
                      ::mlir::Operation *clock, uint64_t depth, uint64_t age,
                      ::mlir::Location location);
  ::mlir::FailureOr<::mlir::Value>
  lowerClockedHistoryOperand(::mlir::Operation *operand,
                             ::mlir::Location location);
  /// IEEE 1800-2023 16.5.1: the sampled value of a packed expression that
  /// reads a dynamic container, which the Preponed snapshot does not hold.
  ::mlir::FailureOr<::mlir::Value>
  lowerSampledDynamicValue(::mlir::Operation *expression);
  ::mlir::FailureOr<::mlir::Value> lowerClockingInputSample(
      ::mlir::Value source, uint64_t sourceDescriptor, ::mlir::Value clock,
      uint64_t clockDescriptor, sim::EdgeKind edge, bool oneStep,
      uint64_t skewTicks, ::mlir::Location location,
      ::mlir::Value sourceObserver = {}, ::mlir::Value primaryObserver = {},
      ::mlir::Value conditionObserver = {}, ::mlir::Value publication = {},
      uint64_t publicationDescriptor = 0);
  ::mlir::FailureOr<::mlir::Value>
  lowerArrayQuerySystemCall(semantic::SVCallExpressionOp op);
  ::mlir::FailureOr<::mlir::Value>
  lowerDisplaySystemCall(semantic::SVCallExpressionOp op);
  ::mlir::FailureOr<LoweredOutputList>
  lowerOutputListItems(::mlir::ArrayRef<::mlir::Operation *> operations,
                       bool interpretLiteralsAsFormats,
                       std::optional<unsigned> designatedFormat = std::nullopt);
  ::mlir::FailureOr<::mlir::Value>
  lowerEnumFormatName(::mlir::Value receiver, ::mlir::ArrayAttr values,
                      ::mlir::ArrayAttr names, ::mlir::Location location);
  ::mlir::FailureOr<::mlir::Value>
  lowerStringFormatSystemCall(semantic::SVCallExpressionOp op);
  /// Whether `type` is a dynamic array or queue of byte, which formatted
  /// output reads and writes as text (IEEE 1800-2023 21.2.1.7, 21.3.3).
  static bool isByteContainerType(::mlir::Type type);
  /// Whether `type` is an unpacked array of byte: fixed-size, dynamic, or a
  /// queue.
  static bool isByteArrayType(::mlir::Type type);
  /// The text of an unpacked array of byte, from its left bound to its right
  /// bound (IEEE 1800-2023 21.2.1.7), omitting NUL characters. Fails without
  /// a diagnostic when `value` is not one.
  ::mlir::FailureOr<::mlir::Value>
  lowerByteArrayString(::mlir::Value value, ::mlir::Location location);
  /// lowerByteArrayString for a dynamic array or queue, which the runtime
  /// renders in one pass.
  ::mlir::FailureOr<::mlir::Value>
  lowerByteContainerString(::mlir::Value container, ::mlir::Location location);
  /// Convert formatted text to an $sformat or $swrite destination of type
  /// `destinationType` by string literal assignment rules (5.9).
  ::mlir::FailureOr<::mlir::Value>
  lowerFormattedResult(::mlir::Value text, ::mlir::Type destinationType,
                       ::mlir::Location location);
  ::mlir::FailureOr<::mlir::Value>
  lowerDumpSystemCall(semantic::SVCallExpressionOp op);
  ::mlir::FailureOr<::mlir::Value>
  lowerFileSystemCall(semantic::SVCallExpressionOp op);
  ::mlir::FailureOr<::mlir::Value>
  lowerPlusargSystemCall(semantic::SVCallExpressionOp op);
  ::mlir::FailureOr<::mlir::Value>
  lowerPlaSystemCall(semantic::SVCallExpressionOp op);
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
                 ::mlir::ArrayRef<::mlir::Operation *> operations,
                 ::mlir::Value previousUdpInputs = {},
                 ::mlir::Value *nextUdpInputs = nullptr);
  ::mlir::LogicalResult
  lowerImmediateAssertion(semantic::SVImmediateAssertionStatementOp op);
  ::mlir::LogicalResult
  lowerConcurrentAssertion(semantic::SVConcurrentAssertionStatementOp op);
  ::mlir::LogicalResult lowerGlobalFutureAssertionResolver(
      semantic::SVConcurrentAssertionStatementOp op);
  ::mlir::LogicalResult
  lowerSequenceEndpointMonitor(::mlir::ArrayRef<::mlir::Operation *> roots);
  /// The default fail action of the assertion, expect, or wait_order
  /// statement `source`: a call to $error naming `description` (IEEE
  /// 1800-2023 20.10).
  ::mlir::LogicalResult
  emitDefaultAssertionFailure(::mlir::Operation *source,
                              ::mlir::Location location,
                              ::llvm::StringRef description);
  /// The hierarchical name of the innermost named scope enclosing `op`: a
  /// named block, or the procedure, subroutine, or instance containing it.
  ::mlir::StringAttr getLexicalScope(::mlir::Operation *op);
  /// The lexical scope of an outlined branch, whose clone has no semantic
  /// ancestors in this function.
  ::mlir::StringAttr outlinedLexicalScope;
  ::mlir::LogicalResult emitRuntimeFatal(::mlir::Location location,
                                         ::mlir::StringRef message);
  void emitRuntimeWarning(::mlir::Location location, ::mlir::StringRef message);
  void emitRuntimeError(::mlir::Location location, ::mlir::StringRef message);
  void emitRuntimeMessage(::mlir::Location location, ::mlir::StringRef severity,
                          ::mlir::StringRef message);
  ::mlir::LogicalResult lowerConditional(semantic::SVConditionalStatementOp op);
  ::mlir::LogicalResult
  lowerQualifiedConditional(semantic::SVConditionalStatementOp op);
  ::mlir::LogicalResult lowerCase(semantic::SVCaseStatementOp op);
  ::mlir::LogicalResult lowerPatternCase(semantic::SVPatternCaseStatementOp op);
  ::mlir::LogicalResult lowerRandCase(semantic::SVRandCaseStatementOp op);
  ::mlir::Value emitRandomBounded(::mlir::Value bound,
                                  ::mlir::Location location);
  ::mlir::LogicalResult
  lowerRandSequence(semantic::SVRandSequenceStatementOp op);
  struct RecursiveProductionBody;
  struct RandJoinStream;
  /// Lower one activation of `production`. Without `body` or `stream`, the
  /// production is expanded at the call site. With `body`, this emits the
  /// single body of a recursive production, whose formals and result live in
  /// that body's slots. With `stream`, the production's prologue runs here
  /// and each item of its selected rule becomes a step of a rand join.
  ::mlir::LogicalResult
  lowerRandSequenceProduction(semantic::SVFrozenRandSeqProductionOp production,
                              ::mlir::ArrayRef<::mlir::Operation *> actuals,
                              ::mlir::Value resultReference = {},
                              RecursiveProductionBody *body = nullptr,
                              RandJoinStream *stream = nullptr);
  ::mlir::FailureOr<RecursiveProductionBody *>
  getRecursiveProductionBody(semantic::SVFrozenRandSeqProductionOp production,
                             ::mlir::Location location);
  ::mlir::LogicalResult
  lowerRecursiveProductionCall(semantic::SVFrozenRandSeqProductionOp production,
                               ::mlir::ArrayRef<::mlir::Operation *> actuals,
                               ::mlir::Value resultReference);
  ::mlir::Value allocateRandSequenceSlot(::mlir::Type type,
                                         ::mlir::Location location);
  ::mlir::Value createRandSequenceStack(::mlir::Type element,
                                        ::mlir::Location location);
  /// After a randsequence with recursive bodies or rand join steps is
  /// emitted, rematerialize or move to slots the values that a dispatch
  /// leaves undominated or a reentering call would clobber, and save each
  /// reentered frame's slots around the call.
  ::mlir::LogicalResult repairRandSequenceFrames(::mlir::Block *first,
                                                 ::mlir::Location location);
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
  void emitQualifierWarning(::mlir::Operation *statement,
                            ::mlir::Location location,
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
          expressionCaptures = {},
      ::mlir::ArrayRef<std::pair<::mlir::Operation *, ::mlir::Value>>
          globalFutureCurrentCaptures = {},
      ::mlir::ArrayRef<std::pair<::llvm::StringRef, ::mlir::Value>>
          explicitCaptures = {},
      ::mlir::SmallVectorImpl<MonitorObservation> *monitorObservations =
          nullptr,
      bool *monitorObservationComplete = nullptr,
      ::mlir::ArrayRef<::llvm::StringRef> liveReferencePaths = {});
  ::mlir::FailureOr<
      std::pair<sim::SimFuncOp, ::mlir::SmallVector<::mlir::Value>>>
  outlinePostponedDisplay(semantic::SVCallExpressionOp call,
                          ::mlir::StringRef immediateName, bool persistent);
  ::mlir::FailureOr<
      std::pair<sim::SimFuncOp, ::mlir::SmallVector<::mlir::Value>>>
  outlineAsyncPla(semantic::SVCallExpressionOp call,
                  ::mlir::StringRef synchronousName);
  ::mlir::LogicalResult
  lowerVariableDeclaration(semantic::SVVariableDeclStatementOp op);
  ::mlir::LogicalResult lowerTiming(::mlir::Operation *control,
                                    ::mlir::Operation *statement);
  ::mlir::LogicalResult
  emitEventSuspend(::mlir::Operation *control, ::mlir::Block *continuation,
                   ::mlir::ValueRange continuationOperands = {});
  /// The endpoint event of a sequence instance used as a procedural event or
  /// through `.triggered` (IEEE 1800-2023 9.4.2.4, 16.13.6), or null when
  /// `expression` is not such an instance.
  ::mlir::FailureOr<::mlir::Value>
  lowerSequenceEndpoint(::mlir::Operation *expression);
  ::mlir::LogicalResult
  emitRepeatedEventSuspend(::mlir::Operation *control,
                           ::mlir::Block *continuation,
                           ::mlir::ValueRange continuationOperands = {});
  /// Collects an event list's members; true when any needs a computed
  /// observer rather than a direct signal-edge wait.
  ::mlir::FailureOr<bool> classifyEventList(
      semantic::SVEventListControlOp list,
      ::llvm::SmallVectorImpl<semantic::SVSignalEventControlOp> &events);
  /// Waits for `count` occurrences of OR'd direct signal events, counting
  /// every event that occurred by each resumption.
  ::mlir::LogicalResult emitCountedEventListSuspend(
      ::llvm::ArrayRef<semantic::SVSignalEventControlOp> events,
      ::mlir::Value count, ::mlir::Location location,
      ::mlir::Block *continuation, ::mlir::ValueRange continuationOperands);
  ::mlir::FailureOr<::mlir::Value>
  lowerRepeatedEventCount(::mlir::Operation *control);
  /// Lowers a repeat count to i64, mapping an unknown or high-impedance count
  /// to zero (IEEE 1800-2023 12.7.2, 9.4.5).
  ::mlir::FailureOr<::mlir::Value> lowerRepeatCount(::mlir::Operation *count);
  ::mlir::LogicalResult
  emitDeferredNBAEvent(semantic::SVAssignmentExpressionOp assignment,
                       ::mlir::Operation *control, CapturedLValue target,
                       ::mlir::Value value, ::mlir::Location location);
  /// A value evaluated when a deferred event action is encountered.
  struct DeferredPayload {
    ::mlir::Value value;
    sim::CaptureKind kind;
    bool retainReference;
  };
  /// Performs `action` once `control` occurs, without blocking the caller
  /// (IEEE 1800-2023 9.4.5, 15.5.1). `owner` carries the prepared code-unit
  /// identity, and `payload` is evaluated now and handed to `action`, which
  /// runs in the deferred process, or in the caller when a repeat count is
  /// not positive.
  ::mlir::LogicalResult emitDeferredEventAction(
      ::mlir::Operation *owner, ::mlir::Operation *control,
      ::llvm::ArrayRef<DeferredPayload> payload, ::mlir::Location location,
      ::llvm::function_ref<::mlir::LogicalResult(UnitLowering &,
                                                 ::mlir::ValueRange)>
          action);
  ::mlir::LogicalResult
  emitCycleDelaySuspend(semantic::SVCycleDelayControlOp control,
                        ::mlir::Block *continuation,
                        ::mlir::ValueRange continuationOperands = {});
  ::mlir::LogicalResult
  lowerClockingEventMonitor(::mlir::ArrayRef<::mlir::Operation *> roots);
  ::mlir::FailureOr<::mlir::Value> lowerDelayValue(::mlir::Operation *control);
  ::mlir::LogicalResult lowerWait(semantic::SVWaitStatementOp op);
  ::mlir::LogicalResult
  lowerEventTrigger(semantic::SVEventTriggerStatementOp op);
  ::mlir::FailureOr<::mlir::Value>
  bindObserver(::mlir::Operation *expression,
               ::mlir::ValueRange dynamicDependencies = {},
               bool includeStaticDependencies = true,
               const ::llvm::StringMap<::mlir::Value> *overrides = nullptr);
  /// Whether an addressable event expression also lowers to a handle the
  /// scheduler can watch. A net is viewed only through packed windows of its
  /// storage, so selecting an element of an unpacked array of nets has to be
  /// observed instead.
  bool hasWatchableSignalHandle(::mlir::Operation *expression);
  void recordSensitivity(::mlir::Value value);
  /// Whether the variables a called subroutine's body reads join the implicit
  /// sensitivity being collected; `task` names a task callee.
  bool calleeBodyReadsAreSensitive(bool task);
  /// Records a read through a constant `net.extract` view of `net`.
  void recordNetViewSensitivity(::mlir::Value view, ::mlir::Value net);
  void recordImplicitEventActivationLocal(::mlir::Value reference);
  void bindVPILocal(::mlir::FlatSymbolRefAttr declaration,
                    ::mlir::Value reference, ::mlir::Location location);
  /// Records a read of every source property of `object`'s class and its
  /// bases, which a method called on it may read.
  void recordObjectPropertyReads(::mlir::Value object,
                                 ::mlir::Location location);
  /// Rebuilds the managed path of a watch at the current insertion point, so
  /// it dominates a wait there and selects the object current at that point.
  /// A read that a short-circuit skipped yields a null, dormant handle.
  ::mlir::FailureOr<::mlir::Value>
  rematerializeManagedInput(::mlir::Value value);
  /// A managed watch rebuilt by rematerializeManagedInput; any other
  /// dependency is returned unchanged.
  ::mlir::FailureOr<::mlir::Value>
  rematerializeManagedWatch(::mlir::Value dependency);
  /// Whether `value` is available in the block being lowered.
  bool dominatesCurrent(::mlir::Value value);
  void recordManagedRead(::mlir::Value reference, ::mlir::Location location);
  void recordContainerSizeRead(::mlir::Value container,
                               ::mlir::Location location);

  ::mlir::FailureOr<::mlir::Value>
  convert(::mlir::Value value, ::mlir::Type targetType, bool sourceSigned,
          ::mlir::Location location, bool targetSigned = false);
  ::mlir::FailureOr<::mlir::Value> convertContainerBitstream(
      ::mlir::Value value, ::mlir::Type targetType, ::mlir::Type targetScalar,
      ::mlir::Type sourceElement, ::mlir::Location location);
  ::mlir::FailureOr<::mlir::Value>
  convertFixedAggregateBitstream(::mlir::Value value, ::mlir::Type targetType,
                                 ::mlir::Type targetScalar,
                                 ::mlir::Location location);
  ::mlir::FailureOr<::mlir::Value> convertRecursiveBitstream(
      ::mlir::Value value, ::mlir::Type targetType, ::mlir::Type targetScalar,
      ::mlir::Location location, bool allowHiddenRoot = false);
  ::mlir::FailureOr<::mlir::Value>
  convertStringBitstream(::mlir::Value value, ::mlir::Type targetType,
                         ::mlir::Type targetScalar, ::mlir::Location location);
  ::mlir::FailureOr<::mlir::Value> convertExplicitBitstreamToPacked(
      ::mlir::Value value, ::mlir::Type targetScalar, ::mlir::Location location,
      bool allowHiddenRoot = false);
  ::mlir::FailureOr<::mlir::Value> convertFixedBitstreamTarget(
      ::mlir::Value value, ::mlir::Type targetType, ::mlir::Location location,
      bool allowHiddenRoot = false, bool dpiLayout = false);
  /// Rounds an f64 toward zero, keeping NaN and infinities, with operations
  /// both execution tiers share.
  ::mlir::Value truncateReal(::mlir::Value real, ::mlir::Location location);
  /// The two-state bits of a packed scalar and whether it has no X or Z bit;
  /// a two-state scalar is returned unchanged and is always known.
  std::pair<::mlir::Value, ::mlir::Value>
  splitKnownBits(::mlir::Value scalar, ::mlir::Location location);
  ::mlir::FailureOr<::mlir::Value> toPackedScalar(::mlir::Value value,
                                                  ::mlir::Location location);
  /// Compare a floating-point selector against one `case` label or `inside`
  /// set member. IEEE 1800-2017 11.3.1 makes the comparison real whenever
  /// either operand is, so both sides widen to the larger float type first.
  /// `integerPredicate` names the ordering the caller wants; anything other
  /// than an upper or lower bound compares for equality.
  ::mlir::FailureOr<::mlir::Value>
  compareFloatMember(::mlir::Value selector, ::mlir::Value candidate,
                     ::mlir::arith::CmpIPredicate integerPredicate,
                     ::mlir::Location location);
  /// Convert an integral queue / dynamic-array index to the signed runtime
  /// representation without conflating an X/Z-containing four-state index
  /// with integer zero.  The container runtime already defines every negative
  /// index as invalid, so -1 is the common sentinel for an unknown index.
  ::mlir::FailureOr<::mlir::Value> toContainerIndex(::mlir::Value value,
                                                    bool sourceSigned,
                                                    ::mlir::Location location);
  /// Widen a fixed-array index to the form the dynamic array operations take:
  /// one bit wider than both the source index and the i64 the declared bounds
  /// are compared in, so a source-range index can neither wrap nor lose its
  /// sign. Four-state indices keep their unknown plane, which IEEE 1800-2017
  /// 7.4.6 makes an invalid index.
  ::mlir::FailureOr<::mlir::Value> toArrayIndex(::mlir::Value value,
                                                bool sourceSigned,
                                                ::mlir::Location location);
  ::mlir::FailureOr<::mlir::Value>
  formatTaggedUnionPattern(::mlir::Value value, ::mlir::Type semanticType,
                           ::mlir::ArrayAttr enumFormats,
                           ::mlir::Location location);
  // Render a value as the assignment pattern IEEE 1800-2023 21.2.1.6 gives
  // %p. Its source type `semanticType`, when known, names packed-structure
  // fields and, with `enumFormats`, enum elements. The shape is static, so
  // the pattern punctuation becomes a format string and the singular
  // elements become its arguments; a container whose elements need source
  // names is rendered element by element.
  ::mlir::FailureOr<::mlir::Value>
  formatAssignmentPattern(::mlir::Value value, ::mlir::Type semanticType,
                          ::mlir::ArrayAttr enumFormats,
                          ::mlir::Location location);
  ::mlir::FailureOr<::mlir::Value>
  formatContainerPattern(::mlir::Value container, ::mlir::Type semanticType,
                         ::mlir::ArrayAttr enumFormats,
                         ::mlir::Location location);
  ::mlir::FailureOr<std::pair<::mlir::Value, ::mlir::Value>>
  formatUnpackedAggregateRaw(::mlir::Value value, ::mlir::Location location,
                             char mode);
  ::mlir::IntegerAttr designTimePrecisionExponent();
  // One design-precision tick in femtoseconds, the unit every lowered
  // simulation delay is counted in.
  ::mlir::FailureOr<uint64_t>
  designTimePrecisionFemtoseconds(::mlir::Location location);
  // Map an assignment-pattern index key onto the element ordinal the aggregate
  // stores it at. IEEE 1800-2017 10.9.1 writes the key in the array's own
  // declared index range, which may descend and may start below zero.
  ::mlir::FailureOr<int64_t> declaredIndexOrdinal(::mlir::Type aggregate,
                                                  ::mlir::Operation *key);
  // The current simulation time expressed in the code unit's time units, the
  // scale IEEE 1800-2017 20.3.1 gives $time and 21.2.1.5 gives %t.
  ::mlir::FailureOr<::mlir::Value>
  currentTimeInUnits(::mlir::Location location);
  // Emit the IEEE 1800-2017 20.2 Table 20-1 diagnostic for a $finish or $stop
  // whose verbosity is not the constant 0.
  ::mlir::LogicalResult emitTerminationDiagnostic(::mlir::StringRef name,
                                                  ::mlir::Value verbosity,
                                                  ::mlir::Location location);
  ::mlir::FailureOr<::mlir::Value> truthValue(::mlir::Value value,
                                              ::mlir::Location location);
  ::mlir::FailureOr<::mlir::Value> toLogic(::mlir::Value value,
                                           ::mlir::Location location);
  ::mlir::FailureOr<::mlir::Value>
  captureCoverageAggregate(::mlir::Value value, ::mlir::Location location);
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
  /// Whether selections of \p base address storage this unit only borrows
  /// through a coverage snapshot or a managed, formal or escaping reference.
  bool isBorrowedAggregate(::mlir::Operation *base);
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
                     bool resultSigned = false, bool emitBlockEventEnd = true);
  // Return from the current function when a preceding call requested
  // termination, and continue lowering on the path where it did not.
  ::mlir::LogicalResult emitTerminationPoll(::mlir::Location location);
  void emitCovergroupBlockEvent(uint64_t targetID, uint32_t eventKind,
                                ::mlir::Location location);
  ::mlir::Block *addBlock();
  void setCurrent(::mlir::Block *block);
  // Whether `block` runs inside an occurrence of the clocking event given by
  // `clock` and `edge`: nullopt when it does not, a null Value when every path
  // into it does, and a predicate when only some do. A plain event control on
  // the same signal and edge counts as the occurrence, per IEEE 1800-2017
  // 14.16, unless `clockingBlockOnly` demands the clocking block itself --
  // which an event qualified by an iff does, because the plain control proves
  // nothing about the condition.
  std::optional<::mlir::Value>
  getCurrentClockingOccurrence(::mlir::Block *block, ::mlir::Value clock = {},
                               std::optional<sim::EdgeKind> edge = std::nullopt,
                               bool clockingBlockOnly = false) const;
  void emitBranch(::mlir::Block *destination);
  void emitControlLeaves(size_t first, ::mlir::Location location);
  bool hasVPIFrameInstrumentation() const;
  void registerVPIObservedRef(::mlir::Value reference);
  void enterVPIRootFrame();
  bool enterVPIStatementFrame(::mlir::Operation *statement);
  void emitVPIFrameLeaves(size_t first, ::mlir::Location location);
  ::mlir::SmallVector<::mlir::Value> getVPIObservedRefs(size_t first = 0) const;
  void emitVPICheckpoint(::mlir::Operation *statement);

  ::mlir::InFlightDiagnostic unsupported(::mlir::Operation *op);
  void recordImplicitWrite(::mlir::Value value);
  /// A view of bits [offset, offset + width) of a packed declaration whose
  /// packed scalar type is `scalar`, created at the current insertion point.
  ::mlir::Value createReadWindowView(::mlir::Value reference,
                                     ::mlir::Type scalar, uint64_t offset,
                                     uint64_t width);
  void ensureVirtualInterfaceInventory();
  ::mlir::FailureOr<::mlir::Value>
  lowerVirtualInterfaceSignal(::mlir::Value interface, ::mlir::StringRef member,
                              ::mlir::Location location,
                              std::optional<uint64_t> scopeID = {});
  ::mlir::FailureOr<::mlir::Value> bindVirtualClockingObserver(
      ::mlir::Operation *expression,
      semantic::SVMemberAccessExpressionOp access, ::mlir::Value interface,
      ::mlir::Value selectedClock = {}, std::optional<uint64_t> scopeID = {});

  static bool isSignedNode(::mlir::Operation *op) {
    if (auto isSigned = op->getAttrOfType<::mlir::BoolAttr>("is_signed"))
      return isSigned.getValue();
    auto type = op->getAttrOfType<::mlir::TypeAttr>("semantic_type");
    return type && isSignedSemanticType(type.getValue());
  }

  sim::SimFuncOp function;
  const UnitLoweringInputs &loweringInputs;
  ::mlir::SymbolTableCollection localSymbols;
  ::mlir::OpBuilder builder;
  ::mlir::Block *current;
  /// SSA remapping for the short prepared initializer fragment encountered
  /// between semantic constructor statements.
  ::mlir::IRMapping preparedInitializerMapping;
  /// Values captured before an outlined callback replaces the corresponding
  /// cloned expression. This is used when an expression's source-region value
  /// must not be recomputed in the callback's later scheduling region.
  ::llvm::DenseMap<::mlir::Operation *, ::mlir::Value> expressionCaptures;
  /// The procedural assertion queue epoch of this process (IEEE 1800-2023
  /// 16.14.6); pending instances queued under an older epoch do not mature.
  ::mlir::Value proceduralAssertionEpoch;
  /// Storage of this process's deferred assertion epoch (IEEE 1800-2023
  /// 16.4.2), and the report enqueues it guards.
  ::std::optional<uint64_t> deferredAssertionEpoch;
  ::llvm::SmallVector<::mlir::Operation *> deferredEpochEnqueues;
  ::mlir::Value loadDeferredAssertionEpoch(::mlir::OpBuilder &builder,
                                           ::mlir::Location location,
                                           ::mlir::Value context,
                                           uint64_t storage);
  /// Whether this function is the procedure that owns its deferred report
  /// epoch: a fork branch or subroutine has other flush points.
  bool ownsDeferredAssertionEpoch() {
    switch (function.getEntryKind()) {
    case sim::EntryKind::Initial:
    case sim::EntryKind::Final:
    case sim::EntryKind::Always:
    case sim::EntryKind::AlwaysComb:
    case sim::EntryKind::AlwaysFF:
    case sim::EntryKind::AlwaysLatch:
      return true;
    default:
      return false;
    }
  }
  /// Mark a report pending on the epoch (its low bit) and return the value
  /// the report captures.
  ::mlir::Value armDeferredAssertionEpoch(::mlir::OpBuilder &builder,
                                          ::mlir::Location location,
                                          ::mlir::Value context,
                                          uint64_t storage);
  /// Suspensions whose resumption is no procedural assertion flush point,
  /// although they wait on an event: an expect statement's completion.
  ::llvm::SmallPtrSet<::mlir::Operation *, 2> nonFlushingSuspensions;
  /// The label path of each labeled procedural concurrent assertion in this
  /// unit.
  /// Disabling a label clears only that assertion's pending instances
  /// (IEEE 1800-2023 16.14.6.4), through a per-label epoch.
  ::llvm::DenseMap<::mlir::Operation *, ::llvm::StringRef>
      proceduralAssertionLabels;
  ::llvm::StringMap<::mlir::Value> proceduralAssertionLabelEpochs;
  /// The exit of this process's outermost named block, and the local
  /// disable branches that reach it. Each is a procedural assertion flush
  /// point (IEEE 1800-2023 16.14.6.4). Another process may disable the block
  /// only when it is an external control target.
  ::mlir::Block *outermostScopeExit = nullptr;
  ::llvm::SmallVector<::mlir::Operation *, 2> outermostScopeDisables;
  ::mlir::Operation *externallyDisabledOutermostScope = nullptr;
  ::mlir::Value getProceduralAssertionLabelEpoch(::llvm::StringRef path,
                                                 ::mlir::Location location);
  /// Current-endpoint values paired with cloned global-future calls. The
  /// detached resolver compares these values with the next global tick.
  ::llvm::DenseMap<::mlir::Operation *, ::mlir::Value>
      globalFutureCurrentCaptures;
  /// Lvalue roots replaced while applying a clocking-output selection to its
  /// real source storage or persistent net driver.
  ::llvm::DenseMap<::mlir::Operation *, ::mlir::Value> lvalueExpressionCaptures;
  ::llvm::StringMap<::mlir::Value> values;
  ::llvm::StringMap<::mlir::Value> lvalues;
  struct CoverageInputBinding {
    ::mlir::Operation *formal = nullptr;
    ::mlir::Block *block = nullptr;
  };
  // Input formals start as SSA snapshots. Only address-taken inputs need a
  // local copy; aliases share the canonical formal's copy in its binding block.
  ::llvm::StringMap<CoverageInputBinding> coverageInputs;
  /// Stable lexical cells used only when binding outlined observers. Ordinary
  /// expressions and fork branches retain their value-oriented bindings.
  ::llvm::StringMap<::mlir::Value> observerCaptureOverrides;
  ::llvm::StringMap<uint64_t> descriptorIDs;
  ::llvm::DenseMap<uint64_t, ::mlir::Value> nodeLvalues;
  bool deferDriverResolution = false;
  uint64_t nextInertialDriveComponent = 0;
  struct TimingPathDelayGroup {
    std::array<::mlir::Value, 12> masks;
    ::mlir::Value delay;
    int64_t delayTicks = 0;
    int64_t pulseReject = -1;
    int64_t pulseError = -1;
    bool pulseOnDetect = false;
    bool pulseShowCancelled = false;
    bool pulseControlled = false;
  };
  struct TimingPathMaskedPlan {
    ::mlir::Value coverageMask;
    ::mlir::SmallVector<TimingPathDelayGroup, 4> groups;
    /// Every rule uses one delay for all destination transition classes. The
    /// write site can therefore pass the selected mask to all runtime banks
    /// without materializing twelve wide symbol-transition masks.
    bool transitionIndependent = false;
    bool proceduralStorage = false;
    /// Scheduler time at which an edge-sensitive procedural source qualified.
    /// Delayed destination writers subtract elapsed time from the path delay.
    ::mlir::Value qualificationTime;
    uint64_t siteID = 0;
    /// Qualification state for edge rules in this driver-local plan. The
    /// lvalue write consumes only bits that actually make a destination
    /// transition, leaving unmatched bits available to a later zero-time
    /// derived update in the same scheduler epoch.
    ::mlir::SmallVector<::mlir::Value, 2> edgePending;
  };
  /// Packed per-destination-bit applicability for a partial or overlapping
  /// path set. Distinct static delays are grouped once, with independently
  /// disjoint masks for all twelve four-state transition classes.
  std::optional<TimingPathMaskedPlan> timingPathMaskedPlan;
  /// Driver actors can publish several disjoint lvalue leaves (for example a
  /// concatenation assignment).  A split specify destination selects its
  /// driver-local plan by the stable semantic lvalue node ID.
  ::llvm::DenseMap<uint64_t, TimingPathMaskedPlan> timingPathMaskedPlans;
  ::llvm::DenseSet<uint64_t> usedTimingPathMaskedPlans;
  /// Procedural path qualification must execute after the outer event wait,
  /// immediately before the assignment body that consumes it.
  std::function<::mlir::LogicalResult()> prepareProceduralTimingPaths;
  std::function<::mlir::LogicalResult()> initializeProceduralTimingPaths;
  ::mlir::LogicalResult
  lowerSystemTimingCheck(::mlir::ArrayRef<::mlir::Operation *> roots);
  std::optional<::mlir::Value> proceduralTimingWriteMask;
  /// Per-activation rise, fall, and turnoff delays selected from statically
  /// frozen overlapping specify paths.
  std::optional<std::array<::mlir::Value, 3>> timingPathDelays;
  ::llvm::StringMap<std::array<::mlir::Value, 2>> strengthDriverLvalues;
  ::llvm::DenseMap<uint64_t, std::array<::mlir::Value, 2>> strengthNodeLvalues;
  ::llvm::StringMap<::mlir::Value> localDefaults;
  ::llvm::StringSet<> automaticLocals;
  ::llvm::StringMap<::mlir::FlatSymbolRefAttr> vpiLocalDeclarations;
  ::llvm::StringMap<::mlir::Value> copyOutDestinations;
  ::llvm::StringMap<::mlir::Value> iteratorIndices;
  bool virtualInterfaceInventoryReady = false;
  ::llvm::StringMap<uint64_t> scopeIDs;
  using VirtualMemberTargets =
      ::mlir::SmallVector<std::pair<uint64_t, uint64_t>>;
  ::llvm::StringMap<VirtualMemberTargets> virtualInterfaceInstanceMembers;
  /// Interface identities with at least one elaborated instance.
  ::llvm::StringSet<> virtualInterfaceInstanceTypes;
  ::llvm::StringMap<VirtualMemberTargets> virtualInterfaceStorageMembers;
  ::llvm::StringMap<VirtualMemberTargets> virtualInterfaceNetMembers;
  ::llvm::StringMap<VirtualMemberTargets> virtualInterfaceEventMembers;
  ::llvm::StringMap<VirtualMemberTargets> virtualInterfaceDriverMembers;
  ::llvm::DenseMap<uint64_t, ::mlir::Value> virtualInterfaceStorageHandles;
  ::llvm::DenseMap<uint64_t, ::mlir::Value> virtualInterfaceNetHandles;
  ::llvm::DenseMap<uint64_t, ::mlir::Value> virtualInterfaceEventHandles;
  ::llvm::DenseMap<uint64_t, ::mlir::Value> virtualInterfaceDriverHandles;
  ::llvm::DenseMap<uint64_t, ::mlir::Type> virtualInterfaceStorageTypes;
  ::llvm::DenseMap<uint64_t, ::mlir::Type> virtualInterfaceNetTypes;
  ::llvm::SetVector<::mlir::Value> virtualInterfaceReadSensitivity;
  ::llvm::SetVector<::mlir::Value> virtualInterfaceWrittenSensitivity;
  struct ClockingOccurrence {
    ::mlir::Value clock;
    /// Null means that every path into the block is clock-synchronized.
    ::mlir::Value predicate;
    /// The edge the block was reached on. Only consulted for an occurrence a
    /// plain event control established, where the edge is what decides whether
    /// the process is coincident with a clocking block's event.
    sim::EdgeKind edge = sim::EdgeKind::Change;
    /// Whether @(clocking_block) or ## established the occurrence, rather than
    /// a plain event control that happens to name the same signal and edge.
    bool fromClockingBlock = true;
  };
  ::llvm::DenseMap<::mlir::Block *, ClockingOccurrence>
      clockingEventContinuations;
  ::llvm::DenseSet<::mlir::Block *> timingBoundaryContinuations;
  bool coverageBlockEventStarted = false;
  ::llvm::DenseMap<::mlir::Operation *, semantic::SVCovergroupBodySymbolOp>
      covergroupOptionBodies;
  ::mlir::Value thisObject;
  // The object of the scope containing a randomize() with call, live
  // only while its inline constraints are lowered. IEEE 1800-2017 18.7
  // resolves a name that the randomize() with object class does not
  // declare in that scope instead, so both objects are addressable.
  ::mlir::Value enclosingThisObject;
  ::mlir::Value taskControlActivation;
  ::llvm::SetVector<::mlir::Value> sensitivity;
  /// A null container read is normalized to a temporary empty container by a
  /// merge block. Keep the pre-normalization handle so an implicit process can
  /// rebuild its mutation watch at the stable wait point instead of retaining
  /// a branch-local merge argument.
  ::llvm::DenseMap<::mlir::Value, ::mlir::Value> materializedContainerSources;
  ::llvm::SetVector<::mlir::Value> *observedDependencies = nullptr;
  /// References allocated while lowering the statement controlled by @*.
  /// They do not exist yet at that statement's pre-entry wait point.
  ::llvm::DenseSet<::mlir::Value> *implicitEventActivationLocals = nullptr;
  /// The dependencies of the statement controlled by @*, which are what the
  /// statement itself reads (IEEE 1800-2023 9.4.2.2).
  ::llvm::SetVector<::mlir::Value> *implicitEventDependencies = nullptr;
  /// Lowering code whose reads add no implicit sensitivity, such as the
  /// action blocks of an immediate assertion in always_comb (9.2.2.2.1).
  bool sensitivityExcluded = false;
  /// Provenance bit windows [offset, offset + width) accessed through a
  /// declaration-level reference; a whole or dynamic access records the full
  /// span as width UINT64_MAX. always_comb and always_latch compare the bits
  /// they read and write by longest static prefix (IEEE 1800-2023 9.2.2.2.1,
  /// 11.5.3).
  using ReferenceWindows =
      ::llvm::SmallVector<std::pair<uint64_t, uint64_t>, 1>;
  ::llvm::MapVector<::mlir::Value, ReferenceWindows> *observedWrites = nullptr;
  ::llvm::DenseMap<::mlir::Value, ReferenceWindows> sensitivityReadWindows;
  /// Set while lowering the root of a constant selection, which records its
  /// own read window instead of the whole reference.
  bool selectionRecordsReadWindow = false;
  ::mlir::Operation *topLevelWildcardControl = nullptr;
  ::mlir::Operation *topLevelAlwaysControl = nullptr;
  ::mlir::Operation *activeSampledClock = nullptr;
  bool observeNonblockingWrites = false;
  ::mlir::Value expressionPlaceholder;
  ::mlir::Value unboundedPlaceholder;
  ::mlir::Value lvalueReferencePlaceholder;
  bool continuousStore = false;
  bool sampleAssertionValues = false;
  bool sampleAssertionDefaults = false;
  /// IEEE 1800-2023 17.5: the statement controlled by a checker always_ff
  /// reads sampled values, except for the variables its event control names.
  ::mlir::Operation *checkerSampledStatement = nullptr;
  ::mlir::Operation *checkerSampledControl = nullptr;
  ::llvm::DenseSet<::mlir::Value> currentValueReferences;
  /// Whether reading `reference` while sampling assertion values reads its
  /// Preponed value. Only design storage is sampled; an automatic or local
  /// variable reads its current value (IEEE 1800-2023 16.5.1).
  bool isSampledReference(::mlir::Value reference);
  /// While set, a sampled static variable reads its sample from a strictly
  /// prior tick of `clock` instead of the current time slot (IEEE 1800-2023
  /// 16.9.3), so an expression over it denotes its past sampled value.
  struct ClockedHistoryRead {
    ::mlir::Operation *clock;
    ::mlir::Operation *gate;
    uint64_t depth;
    uint64_t age;
  };
  std::optional<ClockedHistoryRead> clockedHistoryRead;
  /// Whether a sampled rvalue selection addresses its base by reference, so
  /// that only the selected state range is sampled. A clocked sampler records
  /// whole variables, so clocked history selects from their values instead.
  bool selectsSampledReferences() const {
    return sampleAssertionValues && !clockedHistoryRead;
  }
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
    size_t vpiFrameDepth;
  };
  ::mlir::SmallVector<LoopTargets> loopTargets;
  /// The single emission of a production on a cycle of its randsequence's
  /// production graph (IEEE 1800-2023 18.17). A call stores the actuals in
  /// the formal slots, pushes the index of its resume block, and enters
  /// `entry`; `exit` pops that index and resumes the caller.
  struct RecursiveProductionBody {
    semantic::SVFrozenRandSeqProductionOp production;
    ::mlir::Block *entry = nullptr;
    ::mlir::Block *exit = nullptr;
    ::mlir::SmallVector<::mlir::Value> formalSlots;
    ::mlir::Value resultSlot;
    ::mlir::SmallVector<::mlir::Block *> resumes;
    unsigned frame = 0;
  };
  /// One operand of a rand join (IEEE 1800-2023 18.17.5). `next` holds the
  /// join-wide index of the stream's next step, or -1 once the stream has
  /// finished, and `remaining` the number of its steps not yet run. Every
  /// step returns to `scheduler`; a prologue that finishes its production
  /// before the join starts continues at `initialized`.
  struct RandJoinStream {
    ::mlir::Value next;
    ::mlir::Value remaining;
    ::mlir::Value started;
    ::mlir::Block *initialized;
    ::mlir::Block *scheduler;
    ::mlir::SmallVector<::mlir::Block *> *steps;
  };
  struct RandSequenceContext {
    ::llvm::StringMap<semantic::SVFrozenRandSeqProductionOp> productions;
    ::mlir::Block *breakTarget;
    size_t controlDepth;
    size_t loopDepth;
    size_t productionDepth;
    /// The recursion component of each production on a cycle.
    ::llvm::StringMap<unsigned> recursiveComponents;
    ::llvm::StringMap<RecursiveProductionBody> recursiveBodies;
    ::mlir::SmallVector<std::string> pendingBodies;
    /// Frame 0 is the statement; each recursive body has its own frame. A
    /// frame's slots hold its activation state, which a call that can reenter
    /// the frame's body saves and restores.
    struct Frame {
      unsigned component;
      ::mlir::SmallVector<::mlir::Value> slots;
    };
    ::mlir::SmallVector<Frame> frames;
    unsigned currentFrame = 0;
    ::llvm::DenseMap<::mlir::Block *, unsigned> blockFrames;
    struct RecursiveCall {
      unsigned frame;
      unsigned component;
      ::mlir::Block *save;
      ::mlir::Block *resume;
      ::mlir::Block *restore;
    };
    ::mlir::SmallVector<RecursiveCall> recursiveCalls;
    /// Dominates every block of the statement and holds its slots.
    ::mlir::Block *slotBlock = nullptr;
    ::mlir::Value returnStack;
  };
  ::mlir::SmallVector<RandSequenceContext, 1> randSequenceContexts;
  struct RandSequenceProductionReturn {
    ::mlir::Block *target;
    size_t controlDepth;
    ::mlir::Value resultReference{};
    struct CopyOut {
      CapturedLValue destination;
      ::mlir::Value formalReference;
      bool formalSigned;
      bool destinationSigned;
    };
    ::mlir::SmallVector<CopyOut> copyOuts;
  };
  ::mlir::LogicalResult
  copyRandSequenceOutputs(RandSequenceProductionReturn &production,
                          ::mlir::Location location);
  ::mlir::SmallVector<RandSequenceProductionReturn, 0>
      randSequenceProductionReturns;
  ::mlir::SmallVector<::llvm::DenseMap<::mlir::Operation *, ::mlir::Value>>
      randSequenceResultReferences;
  struct VPIFrameScope {
    ::mlir::Value activation;
    ::mlir::SmallVector<::mlir::Value> observedRefs;
  };
  ::mlir::SmallVector<VPIFrameScope> vpiFrameScopes;
  struct ControlScope {
    std::string path;
    uint64_t targetID;
    ::mlir::Value activation;
    ::mlir::Block *exit;
    std::optional<uint64_t> coverageBlockEventTargetID;
    size_t vpiFrameDepth;
  };
  ::mlir::SmallVector<ControlScope> controlScopes;
  ::llvm::StringMap<uint64_t> inheritedControlIDs;
  ::llvm::StringMap<uint64_t> assertionControlIDs;
  ::mlir::SmallVector<MonitorObservation> monitorObservations;
  bool monitorObservationComplete = false;
  uint64_t nextForkOrdinal = 0;
  uint64_t nextPostponedOrdinal = 0;
  uint64_t nextPlaOrdinal = 0;
  bool invalidBindings = false;
};

} // namespace talos::simlowering

#endif // TALOS_LIB_CONVERSION_TALOSTOSIMULATION_LOWERUNIT_H
