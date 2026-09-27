//===- SimulationTypes.h - Executable simulation types ---------*- C++ -*-===//

#ifndef OBELISK_DIALECT_SIMULATION_SIMULATIONTYPES_H
#define OBELISK_DIALECT_SIMULATION_SIMULATIONTYPES_H

#include "obelisk/Dialect/Simulation/SimulationDialect.h"
#include "obelisk/Dialect/Simulation/SimulationEnums.h"

#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/DialectImplementation.h"
#include "mlir/IR/Types.h"
#include "mlir/Interfaces/MemorySlotInterfaces.h"

#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/TypeSwitch.h"

#define GET_TYPEDEF_CLASSES
#include "obelisk/Dialect/Simulation/SimulationTypes.h.inc"

namespace obelisk::sim {

/// Bit width of a normalized packed simulation value, which is either a
/// signless builtin integer or an exact four-state `!simulation.logic`.
/// Returns `std::nullopt` for every other type.
std::optional<unsigned> getPackedWidth(::mlir::Type type);

/// Scalar representation used by packed-value operators. Packed aggregates
/// map to an integer or logic value of the same width and state domain;
/// unpacked values have no scalar representation.
::mlir::Type getPackedScalarType(::mlir::Type type);

/// Whether two normalized types can share an argument-reference alias. Exact
/// types always can. Distinct packed types can when source signedness was
/// checked before normalization and the remaining IEEE 1800-2017 6.22.2(c)
/// requirements -- total width and state domain -- match.
bool haveCompatibleArgumentRefLayout(::mlir::Type lhs, ::mlir::Type rhs);

/// Whether `type` is one of the fixed first-class aggregate types.
bool isAggregateType(::mlir::Type type);

/// Number and element type of declaration-order aggregate subelements.
unsigned getAggregateNumElements(::mlir::Type type);
::mlir::Type getAggregateElementType(::mlir::Type type, unsigned index);

/// Convert a source array index into its zero-based declaration ordinal.
std::optional<unsigned> getArrayElementOrdinal(::mlir::Type type,
                                               int64_t sourceIndex);

/// Structural span used only by descriptor provenance analysis. Unlike packed
/// width, this assigns ABI-stable, naturally aligned intervals to unpacked
/// struct/array children. Managed handles occupy one 64-bit aligned word.
std::optional<uint64_t> getProvenanceSpan(::mlir::Type type);

/// Natural bit alignment used by structural provenance layout.
std::optional<uint64_t> getProvenanceAlignment(::mlir::Type type);

/// Whether the immediate fixed provenance representation owns an unknown
/// plane. Managed-container handles are leaves here; their element type does
/// not make the handle word four-state.
bool containsFourStateLeaf(::mlir::Type type);

/// Structural offset/span for one declaration-order child. Union children all
/// overlap at offset zero.
std::optional<std::pair<uint64_t, uint64_t>>
getAggregateProvenanceSubelement(::mlir::Type type, unsigned index);

/// Build the compact, versioned runtime plan for one legal fixed aggregate
/// bit-stream source. Array extents are represented by REPEAT records.
std::optional<::llvm::SmallVector<uint64_t>>
getFixedBitStreamPlan(::mlir::Type type);

/// Build the inverse fixed-target plan. Copy records retain whether each
/// destination leaf is two- or four-state so final X/Z coercion is exact.
std::optional<::llvm::SmallVector<uint64_t>>
getFixedBitStreamImportPlan(::mlir::Type type);

/// Build explicit formatting metadata for a fixed unpacked-array element.
/// The plan records every array extent and byte stride plus the singular leaf
/// representation; an empty plan means the type is not such an array.
std::optional<::llvm::SmallVector<uint64_t>>
getFixedArrayPatternPlan(::mlir::Type type);

/// Build equivalent plans against structural provenance storage. These are
/// reserved for DPI aggregate marshalling, where naturally aligned managed
/// handles and padding must not change language bit-stream cast semantics.
std::optional<::llvm::SmallVector<uint64_t>>
getDPIAggregateBitStreamPlan(::mlir::Type type);
std::optional<::llvm::SmallVector<uint64_t>>
getDPIAggregateBitStreamImportPlan(::mlir::Type type);

/// Build the compact preorder plan for a bit-stream source containing at
/// least one dynamically sized member. Fixed arrays and structures are
/// represented structurally, while dynamic arrays, queues, typed associative
/// arrays, and strings become runtime traversal nodes. Class roots can extend
/// this grammar with the reserved OBJECT node without changing the packing
/// ABI.
std::optional<::llvm::SmallVector<uint64_t>>
getRecursiveBitStreamPlan(::mlir::Type type);

/// Build the version-two recursive plan for a source that may contain class
/// handles.  The resolver supplies the trusted nonzero dispatch-group ID for
/// each OBJECT record; `directRoot` is true only for a direct class-handle
/// source, never for a handle reached through an aggregate or container.
using ClassBitStreamGroupResolver =
    ::llvm::function_ref<std::optional<uint64_t>(ClassHandleType,
                                                 bool directRoot)>;
std::optional<::llvm::SmallVector<uint64_t>>
getRecursiveBitStreamPlan(::mlir::Type type,
                          ClassBitStreamGroupResolver resolver,
                          bool requireDynamic = true);

/// Target-independent identifier for one static class source and root
/// visibility mode.  Materialization rejects the vanishingly unlikely hash
/// collision before this ID becomes runtime authority.
uint64_t getClassBitStreamGroupID(ClassHandleType type, bool allowHiddenRoot);

/// Runtime-managed categories that can occupy a source value word. These are
/// bit flags because an overlapping union slot may legally represent more
/// than one category.
enum class ManagedHandleKind : uint32_t {
  Class = 1u << 0,
  String = 1u << 1,
  Container = 1u << 2,
  ReferencePath = 1u << 3,
};

/// Runtime managed values use an ABI-stable 64-bit tagged word even when the
/// target has 32-bit pointers.
constexpr unsigned managedHandleBitWidth = 64;
constexpr unsigned managedHandleByteWidth = managedHandleBitWidth / 8;
constexpr unsigned managedHandleByteAlignment = managedHandleByteWidth;

/// Runtime trace-layout encoding shared by native class and container
/// descriptors. Exact slots use the one-based managed kind enumerators;
/// candidate slots carry this flag plus a ManagedHandleKind mask.
constexpr uint32_t managedHandleCandidateFlag = uint32_t{1} << 31;

struct ManagedHandleSlot {
  uint64_t bitOffset;
  uint32_t kindMask;
  /// An overlapping source union can also contain ordinary bits. Such a slot
  /// is a root only when its current word names a live object of an allowed
  /// kind; exact slots must always contain a well-typed managed word.
  bool conditional;

  bool operator==(const ManagedHandleSlot &other) const {
    return bitOffset == other.bitOffset && kindMask == other.kindMask &&
           conditional == other.conditional;
  }
};

/// Encode a structured managed slot for the public runtime trace ABI.
std::optional<uint32_t>
getManagedHandleTraceKind(const ManagedHandleSlot &slot);

/// Append every managed-handle word in `type`, including its allowed runtime
/// categories and whether tracing is conditional on the current word.
bool getManagedHandleSlots(::mlir::Type type,
                           ::llvm::SmallVectorImpl<ManagedHandleSlot> &slots);

/// Compatibility projection used by analyses that only need root positions.
/// Append every managed-handle word in `type`, as a bit offset from the start
/// of its structural provenance representation.
bool getManagedHandleOffsets(::mlir::Type type,
                             ::llvm::SmallVectorImpl<uint64_t> &offsets);

/// True for a nullable one-word value owned by the precise managed heap.
bool isManagedHandleType(::mlir::Type type);

/// True for a frame-relative simulator identifier. These are opaque 64-bit
/// words wherever they are stored, independent of whatever they denote.
bool isSimulationHandleType(::mlir::Type type);

/// Bit width of a simulation handle word.
constexpr unsigned simulationHandleBitWidth = 64;

} // namespace obelisk::sim

#endif // OBELISK_DIALECT_SIMULATION_SIMULATIONTYPES_H
